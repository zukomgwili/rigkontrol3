#include "NativeUSB.h"
#include "NativeAudioISO.h"
#include "NativeHostUSB.h"
#include "InputGain.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>
#include <errno.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { REPORT_HISTORY = 256, COMMAND_QUEUE = 32, COMMAND_TIMEOUT = 200,
       CONNECT_RETRY_MS = 1000, REPLY_TIMEOUT_MS = 3000, DRAIN_MS = 2000 };
typedef enum { REQUEST_COMMAND, REQUEST_START, REQUEST_STOP } RequestKind;
typedef struct Request {
    struct Request *next;
    RequestKind kind;
    uint8_t bytes[64];
    size_t length;
    uint64_t generation;
    double deadline;
    bool complete, cancelled;
    int32_t result;
    unsigned int references;
    pthread_cond_t condition;
} Request;
typedef struct RetiredAudio {
    struct RetiredAudio *next;
    rk3_native_audio_iso *audio; /* Immutable with generation/counts after publication. */
    uint64_t generation;
    uint32_t inputs,outputs;
    RK3InputGain gain;
} RetiredAudio;
typedef struct AudioClockTimer AudioClockTimer;

struct rk3_usb_session {
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    atomic_uint references;
    atomic_bool closing;
    atomic_uint rt_readers;
    CFRunLoopRef run_loop;
    CFRunLoopTimerRef clock_timer;
    AudioClockTimer *clock_timer_context;
    Request *head, *tail;
    Request *active_request; /* worker-owned reference, valid through completion */
    Request *stop_head, *stop_tail; /* worker-owned pending stops; never block EP1 */
    size_t deferred_stops; /* mutex protects admission accounting */
    double stop_deadline;
    pthread_t worker_thread;
    bool worker_thread_known;
    rk3_usb_audio_panic_handler panic_handler; /* mutex, including callback lease */
    void *panic_context;
    unsigned panic_callbacks;
    bool panic_seen, panic_pending; /* worker only; cleared on a new successful start */
    double panic_deadline;
    int32_t panic_error;
    size_t queued;
    rk3_usb_status status;
    uint64_t generation;
    RK3NativeReport reports[REPORT_HISTORY];
    size_t history_count, history_head;
    uint64_t sequence;
    IOUSBDeviceInterface300 **device;
    RK3HostInterface *interface;
    bool opened, changed_alt, pending, stopping_io, transport_failed;
    bool pending_write;
    bool command_recovering, recovery_clears_output, recovery_rearms_input, command_suspended, recovery_timed_out;
    double command_recovery_deadline;
    IOReturn write_result;
    size_t write_length;
    bool info_received;
    /* Session mutex protects specification staging and the committed USB cohort. */
    bool spec_collecting, spec_pending_valid, spec_available;
    uint64_t spec_generation;
    uint8_t spec_pending[RK3_INFO_BYTES], spec_raw[RK3_INFO_BYTES];
    RK3NativeUSBIdentity identity_pending,identity_current;
    bool identity_pending_valid,identity_available;
    bool digital_known;
    uint16_t digital_snapshot;
    int ack_result;
    UInt8 original_alt, command_in, command_out, iso_in, iso_out;
    uint8_t read_buffer[64];
    uint8_t write_buffer[64];
    _Atomic(RetiredAudio *) audio;
    atomic_uint_fast64_t clock_generation;
    atomic_bool output_active;
    bool want_audio;
    RetiredAudio *retired;
    size_t retired_count;
};
struct AudioClockTimer {
    atomic_uint references;
    rk3_usb_session *session;
    rk3_native_audio_iso *audio;
    uint64_t generation,incarnation,deadline;
};
static void release_session(rk3_usb_session *);
static void set_audio_error(rk3_usb_session *,int32_t,const char *);

static void spec_revoke_locked(rk3_usb_session *session) {
    session->spec_collecting=false;session->spec_pending_valid=false;
    session->spec_available=false;session->spec_generation=0;
    session->identity_pending_valid=false;session->identity_available=false;
}
static void spec_revoke(rk3_usb_session *session) {
    pthread_mutex_lock(&session->mutex);spec_revoke_locked(session);pthread_mutex_unlock(&session->mutex);
}
static void spec_begin_attempt_locked(rk3_usb_session *session) {
    spec_revoke_locked(session);
    session->spec_collecting=!atomic_load(&session->closing);
}
static void spec_commit_locked(rk3_usb_session *session) {
    if (!session->spec_collecting || atomic_load(&session->closing) || !session->generation) return;
    session->spec_generation=session->generation;
    session->spec_available=session->spec_pending_valid;
    if (session->spec_available) memcpy(session->spec_raw,session->spec_pending,RK3_INFO_BYTES);
    session->identity_available=session->identity_pending_valid;
    if (session->identity_available) {
        session->identity_current=session->identity_pending;
        session->identity_current.generation=session->spec_generation;
    }
}
static void spec_stage_locked(rk3_usb_session *session,const uint8_t raw[RK3_INFO_BYTES]) {
    if (!session->spec_collecting || atomic_load(&session->closing)) return;
    memcpy(session->spec_pending,raw,RK3_INFO_BYTES);session->spec_pending_valid=true;
    if (session->spec_generation && session->spec_generation==session->generation) {
        memcpy(session->spec_raw,raw,RK3_INFO_BYTES);session->spec_available=true;
    }
}

/* Worker-only loads do not outlive its serialized retirement operations.
 * External readers pin rt_readers before taking the whole context. */
static rk3_native_audio_iso *current_audio(rk3_usb_session *session) {
    RetiredAudio *context=atomic_load(&session->audio);return context?context->audio:NULL;
}
static const void *retain_audio_clock_timer_context(const void *info) {
    AudioClockTimer *timer=(AudioClockTimer *)info;
    atomic_fetch_add_explicit(&timer->references,1,memory_order_relaxed);return info;
}
static void release_audio_clock_timer_context(const void *info) {
    AudioClockTimer *timer=(AudioClockTimer *)info;
    if(atomic_fetch_sub_explicit(&timer->references,1,memory_order_acq_rel)!=1)return;
    rk3_usb_session *session=timer->session;free(timer);release_session(session);
}
static void cancel_audio_clock_timer(rk3_usb_session *session) {
    CFRunLoopTimerRef timer=session->clock_timer;AudioClockTimer *context=session->clock_timer_context;
    session->clock_timer=NULL;session->clock_timer_context=NULL;
    if(timer){CFRunLoopTimerInvalidate(timer);CFRelease(timer);}
    if(context)release_audio_clock_timer_context(context);
}
static int32_t sync_audio_clock_timer(rk3_usb_session *);
static void audio_clock_timer_callback(CFRunLoopTimerRef timer,void *info) {
    AudioClockTimer *binding=(AudioClockTimer *)retain_audio_clock_timer_context(info);
    rk3_usb_session *session=binding->session;CFRetain(timer);
    /* Test identity before even asking a possibly retired engine for state. */
    if(timer!=session->clock_timer||binding!=session->clock_timer_context)goto done;
    rk3_native_audio_clock_schedule schedule;
    if(atomic_load(&session->closing)||session->stopping_io||session->transport_failed||
        session->generation!=binding->generation||current_audio(session)!=binding->audio||
        !rk3_native_audio_iso_clock_schedule(binding->audio,&schedule)||
        schedule.incarnation!=binding->incarnation||schedule.deadline!=binding->deadline) {
        cancel_audio_clock_timer(session);goto done;
    }
    bool due=mach_absolute_time()>=binding->deadline;
    /* interval=0 timers auto-invalidate after firing. Detach this object and
     * create the next one-shot, rather than trying to resurrect this timer. */
    cancel_audio_clock_timer(session);
    if(due)rk3_native_audio_iso_clock_service(binding->audio);
    if(!atomic_load(&session->closing)&&!session->stopping_io&&!session->transport_failed&&
        !session->clock_timer&&session->generation==binding->generation&&
        current_audio(session)==binding->audio&&
        rk3_native_audio_iso_clock_schedule(binding->audio,&schedule)&&
        schedule.incarnation==binding->incarnation) {
        int32_t error=sync_audio_clock_timer(session);
        if(error){rk3_native_audio_iso_stop(binding->audio);set_audio_error(session,error,"Cannot schedule NI audio clock timer");}
    }
done:
    CFRelease(timer);release_audio_clock_timer_context(binding);
}
static int32_t sync_audio_clock_timer(rk3_usb_session *session) {
    rk3_native_audio_iso *audio=current_audio(session);rk3_native_audio_clock_schedule schedule;
    if(atomic_load(&session->closing)||session->stopping_io||session->transport_failed||!audio||
        !rk3_native_audio_iso_clock_schedule(audio,&schedule)) {
        cancel_audio_clock_timer(session);return 0;
    }
    AudioClockTimer *old=session->clock_timer_context;
    if(old&&session->clock_timer&&CFRunLoopTimerIsValid(session->clock_timer)&&old->audio==audio&&
        old->generation==session->generation&&old->incarnation==schedule.incarnation&&old->deadline==schedule.deadline)
        return 0; /* Repeated pumps must not postpone an already due event. */
    cancel_audio_clock_timer(session);
    if(!session->run_loop)return EINVAL;
    AudioClockTimer *binding=calloc(1,sizeof(*binding));if(!binding)return ENOMEM;
    atomic_init(&binding->references,1);atomic_fetch_add_explicit(&session->references,1,memory_order_relaxed);
    binding->session=session;binding->audio=audio;binding->generation=session->generation;
    binding->incarnation=schedule.incarnation;binding->deadline=schedule.deadline;
    mach_timebase_info_data_t scale;
    if(mach_timebase_info(&scale)!=KERN_SUCCESS||!scale.denom){release_audio_clock_timer_context(binding);return EIO;}
    uint64_t now=mach_absolute_time();uint64_t remaining=schedule.deadline>now?schedule.deadline-now:0;
    CFAbsoluteTime fire=CFAbsoluteTimeGetCurrent()+(CFTimeInterval)((long double)remaining*scale.numer/scale.denom/1000000000.0L);
    CFRunLoopTimerContext context={0,binding,retain_audio_clock_timer_context,release_audio_clock_timer_context,NULL};
    CFRunLoopTimerRef timer=CFRunLoopTimerCreate(NULL,fire,0,0,0,audio_clock_timer_callback,&context);
    if(!timer){release_audio_clock_timer_context(binding);return ENOMEM;}
    session->clock_timer=timer;session->clock_timer_context=binding;
    CFRunLoopAddTimer(session->run_loop,timer,kCFRunLoopDefaultMode);return 0;
}
static void stop_audio_clock(rk3_usb_session *session,rk3_native_audio_iso *audio) {
    cancel_audio_clock_timer(session);if(audio)rk3_native_audio_iso_stop(audio);
}
static double seconds_now(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void set_error(rk3_usb_session *session, int32_t code, const char *text) {
    pthread_mutex_lock(&session->mutex);
    session->status.error_code = code;
    snprintf(session->status.error, sizeof(session->status.error), "%s", text);
    session->status.state = RK3_USB_FAILED;
    pthread_mutex_unlock(&session->mutex);
}

/* An ISO engine can fail while command/DIN/controller USB is still healthy.
 * Do not publish FAILED: CoreMIDI treats that flag as shared-device removal. */
static bool device_lost(int32_t error) {
    return error == kIOReturnNoDevice || error == kIOReturnNotAttached || error == kIOReturnNotOpen ||
        (uint32_t)error == UINT32_C(0xe000404e); /* old invalid-interface status */
}

static int32_t audio_failure(rk3_native_audio_iso *audio) {
    int32_t error = rk3_native_audio_iso_error(audio);
    if (error != 0) return error;
    uint32_t delay = 0; int32_t reason = 0;
    return rk3_native_audio_iso_panic(audio, &delay, &reason) && reason != 0 ? reason : ETIMEDOUT;
}

static void command_warning(rk3_usb_session *session, int32_t code, const char *text) {
    pthread_mutex_lock(&session->mutex);
    if (!session->transport_failed) {
        session->status.error_code = code;
        snprintf(session->status.error, sizeof(session->status.error), "%s", text);
    }
    pthread_mutex_unlock(&session->mutex);
}

static void set_audio_error(rk3_usb_session *session, int32_t code, const char *text) {
    pthread_mutex_lock(&session->mutex);
    if (!session->transport_failed) {
        session->status.error_code = code;
        snprintf(session->status.error, sizeof(session->status.error), "%s", text);
        session->status.state = RK3_USB_STOPPING;
    }
    pthread_mutex_unlock(&session->mutex);
}

/* Setter and publication share this non-RT gate. A reconnect cannot overwrite
 * a newer desired output activity update after its setter has returned. */
static void publish_audio(rk3_usb_session *session, RetiredAudio *context) {
    pthread_mutex_lock(&session->mutex);
    rk3_native_audio_iso_set_output_active(context->audio, atomic_load(&session->output_active));
    atomic_store(&session->audio, context);
    pthread_mutex_unlock(&session->mutex);
}

static void service_audio(rk3_usb_session *session) {
    rk3_native_audio_iso *audio = current_audio(session);
    if (audio == NULL) {cancel_audio_clock_timer(session);return;}
    rk3_native_audio_iso_service(audio);
    int32_t timer_error=sync_audio_clock_timer(session);
    if(timer_error){stop_audio_clock(session,audio);set_audio_error(session,timer_error,"Cannot schedule NI audio clock timer");}
    int32_t diagnostic = rk3_native_audio_iso_stream_error(audio);
    if (diagnostic != 0) {
        pthread_mutex_lock(&session->mutex);
        if (session->status.error_code == 0) {
            session->status.error_code = diagnostic;
            snprintf(session->status.error, sizeof(session->status.error), "Audio transfer diagnostic");
        }
        pthread_mutex_unlock(&session->mutex);
    }
}

static void check_connection_presence(rk3_usb_session *session) {
    if (session->interface != NULL && session->opened && !session->stopping_io &&
        !session->transport_failed && !rk3_host_connected(session->interface)) {
        spec_revoke(session);
        session->transport_failed = true;
        set_error(session, kIOReturnNoDevice, "RK3 interface termination reported by host interest");
    }
}

static void audio_panic_tick(rk3_usb_session *session, double now);
static void service_command_recovery(rk3_usb_session *session, double now);
static void pump_callbacks(rk3_usb_session *session, double interval) {
    (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, interval, true);
    check_connection_presence(session);
    service_command_recovery(session, seconds_now());
    service_audio(session);
    /* A command ACK wait must not defer the recovered panic timer until the
     * entire command deadline expires. The handler only enqueues host work. */
    audio_panic_tick(session, seconds_now());
}

static void release_session(rk3_usb_session *session) {
    if (atomic_fetch_sub_explicit(&session->references, 1, memory_order_acq_rel) != 1) return;
    cancel_audio_clock_timer(session);
    RetiredAudio *retired = session->retired;
    while (retired != NULL) {
        RetiredAudio *next = retired->next;
        (void)rk3_native_audio_iso_destroy(retired->audio);
        free(retired);
        retired = next;
    }
    pthread_cond_destroy(&session->wake);
    pthread_mutex_destroy(&session->mutex);
    free(session);
}

static void reclaim_retired(rk3_usb_session *session) {
    /* All publication, pointer loads and reader pins use seq_cst ordering.
     * A reader pinned before unpublish prevents reclamation; a later reader
     * can only acquire the current published engine, which is never freed. */
    if (atomic_load(&session->rt_readers) != 0) return;
    RetiredAudio *current = atomic_load(&session->audio);
    RetiredAudio **link = &session->retired;
    while (*link != NULL) {
        RetiredAudio *retired = *link;
        if(retired!=current&&session->clock_timer_context&&session->clock_timer_context->audio==retired->audio)
            cancel_audio_clock_timer(session);
        if (retired != current && rk3_native_audio_iso_is_quiescent(retired->audio) &&
            rk3_native_audio_iso_destroy(retired->audio)) {
            *link = retired->next; --session->retired_count; free(retired);
        } else link = &retired->next;
    }
}

static void wake_worker_locked(rk3_usb_session *session) {
    pthread_cond_signal(&session->wake);
    if (session->run_loop != NULL) CFRunLoopWakeUp(session->run_loop);
}

static void complete_locked(Request *request, int32_t result) {
    request->result = result;
    request->complete = true;
    pthread_cond_broadcast(&request->condition);
    if (--request->references == 0) {
        pthread_cond_destroy(&request->condition);
        free(request);
    }
}

static int32_t submit(rk3_usb_session *session, RequestKind kind,
                      const uint8_t *bytes, size_t length, uint64_t generation, uint32_t timeout_ms) {
    if (session == NULL || timeout_ms == 0 || timeout_ms > 5000 || length > 64) return EINVAL;
    Request *request = calloc(1, sizeof(*request));
    if (request == NULL) return ENOMEM;
    int status = pthread_cond_init(&request->condition, NULL);
    if (status != 0) { free(request); return status; }
    request->kind = kind;
    request->references = 2;
    request->length = length;
    request->generation = generation;
    if (length != 0) memcpy(request->bytes, bytes, length);
    double deadline = seconds_now() + (double)timeout_ms / 1000.0;
    request->deadline = deadline;
    pthread_mutex_lock(&session->mutex);
    if (atomic_load(&session->closing) || session->queued + session->deferred_stops >= COMMAND_QUEUE) {
        pthread_mutex_unlock(&session->mutex);
        pthread_cond_destroy(&request->condition); free(request);
        return ECANCELED;
    }
    if (session->tail != NULL) session->tail->next = request;
    else session->head = request;
    session->tail = request;
    ++session->queued;
    wake_worker_locked(session);
    while (!request->complete) {
        double remaining = deadline - seconds_now();
        if (remaining <= 0) break;
        struct timespec relative = { (time_t)remaining,
            (long)((remaining - (double)(time_t)remaining) * 1e9) };
        int waited = pthread_cond_timedwait_relative_np(&request->condition, &session->mutex, &relative);
        if (waited != 0 && waited != ETIMEDOUT) break;
    }
    int32_t result = request->complete ? request->result : ETIMEDOUT;
    if (!request->complete) request->cancelled = true;
    if (--request->references == 0) {
        pthread_cond_destroy(&request->condition); free(request);
    }
    pthread_mutex_unlock(&session->mutex);
    return result;
}

static IOUSBDeviceInterface300 **make_device(io_service_t service) {
    IOCFPlugInInterface **plugin = NULL;
    IOUSBDeviceInterface300 **device = NULL;
    SInt32 score = 0;
    IOReturn result = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID,
        kIOCFPlugInInterfaceID, &plugin, &score);
    if (result != kIOReturnSuccess || plugin == NULL) return NULL;
    HRESULT status = (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID300), (LPVOID *)&device);
    (*plugin)->Release(plugin);
    return status == S_OK ? device : NULL;
}

static IOUSBDeviceInterface300 **find_device(void) {
    const char *classes[] = { "IOUSBHostDevice", kIOUSBDeviceClassName };
    uint64_t ids[16];
    size_t count = 0;
    IOUSBDeviceInterface300 **selected = NULL;
    bool ambiguous = false;
    for (size_t family = 0; family < 2; ++family) {
        CFMutableDictionaryRef matching = IOServiceMatching(classes[family]);
        if (matching == NULL) continue;
        int32_t vendor = RK3_USB_VENDOR, product = RK3_USB_PRODUCT;
        CFNumberRef vid = CFNumberCreate(NULL, kCFNumberSInt32Type, &vendor);
        CFNumberRef pid = CFNumberCreate(NULL, kCFNumberSInt32Type, &product);
        if (vid == NULL || pid == NULL) {
            if (vid != NULL) CFRelease(vid);
            if (pid != NULL) CFRelease(pid);
            CFRelease(matching); continue;
        }
        CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vid);
        CFDictionarySetValue(matching, CFSTR(kUSBProductID), pid);
        CFRelease(vid); CFRelease(pid);
        io_iterator_t iterator = IO_OBJECT_NULL;
        if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != kIOReturnSuccess) continue;
        io_service_t service;
        while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
            uint64_t identifier = 0;
            bool duplicate = false;
            IOReturn result = IORegistryEntryGetRegistryEntryID(service, &identifier);
            for (size_t index = 0; index < count; ++index) if (ids[index] == identifier) duplicate = true;
            if (result != kIOReturnSuccess || duplicate) { IOObjectRelease(service); continue; }
            if (count == 16) { IOObjectRelease(service); ambiguous = true; break; }
            ids[count++] = identifier;
            IOUSBDeviceInterface300 **device = make_device(service);
            IOObjectRelease(service);
            if (device == NULL) continue;
            if (selected == NULL) selected = device;
            else { (*device)->Release(device); ambiguous = true; }
        }
        IOObjectRelease(iterator);
    }
    if (ambiguous && selected != NULL) { (*selected)->Release(selected); selected = NULL; }
    return selected;
}

static bool advertised_configuration(IOUSBDeviceInterface300 **device, UInt8 value) {
    UInt8 count = 0;
    if ((*device)->GetNumberOfConfigurations(device, &count) != kIOReturnSuccess) return false;
    for (unsigned int index = 0; index < count; ++index) {
        IOUSBConfigurationDescriptorPtr descriptor = NULL;
        if ((*device)->GetConfigurationDescriptorPtr(device, (UInt8)index, &descriptor) != kIOReturnSuccess || descriptor == NULL) return false;
        const uint8_t *bytes = (const uint8_t *)descriptor;
        size_t total = (size_t)bytes[2] | ((size_t)bytes[3] << 8);
        if (bytes[0] < 9 || total < 9) return false;
        if (bytes[5] != value) continue;
        for (size_t offset = 0; offset < total;) {
            if (total - offset < 2 || bytes[offset] < 2 || bytes[offset] > total - offset) return false;
            const uint8_t *entry = bytes + offset;
            if (entry[1] == kUSBInterfaceDesc && entry[0] >= 9 && entry[2] == 0 && entry[3] == 1) return true;
            offset += entry[0];
        }
    }
    return false;
}

static io_service_t find_interface(IOUSBDeviceInterface300 **device) {
    IOUSBFindInterfaceRequest request = { kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
                                        kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
    io_iterator_t iterator = IO_OBJECT_NULL;
    if ((*device)->CreateInterfaceIterator(device, &request, &iterator) != kIOReturnSuccess) return IO_OBJECT_NULL;
    io_service_t selected = IO_OBJECT_NULL;
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBInterfaceNumber), NULL, 0);
        int number = -1;
        bool matches = value != NULL && CFGetTypeID(value) == CFNumberGetTypeID() &&
            CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &number) && number == 0;
        if (value != NULL) CFRelease(value);
        if (matches && selected == IO_OBJECT_NULL) selected = service;
        else IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return selected;
}

static bool arm_reader(rk3_usb_session *session);
static void begin_command_recovery(rk3_usb_session *session, bool clear_output, bool rearm_input);

static void report_complete(void *context, int32_t result, size_t length) {
    rk3_usb_session *session = context;
    session->pending = false;
    if (session->stopping_io) return;
    if (result != kIOReturnSuccess) {
        if (device_lost(result)) {
            spec_revoke(session);
            session->transport_failed = true;
            set_error(session, result, "Command interface unavailable");
        } else if (result == kIOReturnAborted) {
            /* NI returns without rearm. A modern clear can abort our pending
             * read; its separate gate rearms only after that callback retires. */
            if (!session->command_recovering) session->command_suspended = true;
        } else if (result == kIOReturnNotResponding) {
            session->command_recovering = false; session->command_suspended = true;
            command_warning(session, result, "EP1 not responding; both command pipes aborted, reader suspended");
            (void)rk3_host_abort(session->interface, session->command_in);
            (void)rk3_host_abort(session->interface, session->command_out);
        } else if ((uint32_t)result == UINT32_C(0xe000404f)) {
            command_warning(session, result, "EP1 input stalled; clearing input endpoint");
            if (!session->command_recovering) begin_command_recovery(session, false, true);
        } else {
            command_warning(session, result, "EP1 input error; NI reader rearm");
            if (!session->command_recovering && !session->command_suspended &&
                !atomic_load(&session->closing)) (void)arm_reader(session);
        }
        return;
    }
    if (length > sizeof(session->read_buffer)) {
        spec_revoke(session);
        session->transport_failed = true;
        set_error(session, EOVERFLOW, "EP1 report exceeds its fixed buffer");
        return;
    }
    pthread_mutex_lock(&session->mutex);
    RK3NativeReport *report = &session->reports[session->history_head];
    *report = (RK3NativeReport){ .sequence = ++session->sequence,
        .host_time = mach_absolute_time(), .length = (uint32_t)length };
    memcpy(report->bytes, session->read_buffer, length);
    session->history_head = (session->history_head + 1) % REPORT_HISTORY;
    if (session->history_count < REPORT_HISTORY) ++session->history_count;
    else ++session->status.reports_dropped;
    ++session->status.reports_received;
    pthread_mutex_unlock(&session->mutex);
    if (length != 0 && session->read_buffer[0] == RK3_COMMAND_INFO) {
        rk3_device_info info;
        if (rk3_device_info_parse(session->read_buffer, length, &info)) {
            pthread_mutex_lock(&session->mutex);
            session->status.info = info;
            spec_stage_locked(session,info.raw);
            pthread_mutex_unlock(&session->mutex);
            session->info_received = true;
        }
    }
    if (length >= 2 && session->read_buffer[0] == RK3_COMMAND_AUDIO_PARAMS)
        session->ack_result = session->read_buffer[1];
    if (length >= 3 && session->read_buffer[0] == RK3_COMMAND_DIGITAL) {
        pthread_mutex_lock(&session->mutex);
        session->digital_snapshot = (uint16_t)(((uint16_t)session->read_buffer[1] |
            ((uint16_t)session->read_buffer[2] << 8)) & 0x1ff);
        session->digital_known = true;
        pthread_mutex_unlock(&session->mutex);
    }
    if (!session->command_recovering && !session->command_suspended &&
        !atomic_load(&session->closing)) (void)arm_reader(session);
}

static bool arm_reader(rk3_usb_session *session) {
    if (session->pending || session->stopping_io || session->command_recovering || session->command_suspended) return false;
    session->pending = true;
    IOReturn result = rk3_host_bulk_read(session->interface, session->command_in,
        session->read_buffer, sizeof(session->read_buffer), 0, report_complete, session);
    if (result != kIOReturnSuccess) {
        session->pending = false;
        session->command_suspended = true;
        if (device_lost(result)) {
            spec_revoke(session);
            session->transport_failed = true;
            set_error(session, result, "Command interface unavailable while arming reader");
        } else command_warning(session, result, "EP1 read submission failed; reader suspended");
        return false;
    }
    return true;
}

/* NI commandReplyDispatch@0x221c and sendCommand@0x26c9/0x26db use
 * ClearPipeStall(false). IOUSBHost's public clearStall has no controller-only
 * variant: it resets the endpoint's halt/data toggle and aborts pending I/O.
 * This endpoint-local substitution retains the shared connection, and its
 * abort/drain gate prevents a late old callback from sharing a new read buffer.
 * Never replay a failed command/MIDI packet. */
static void begin_command_recovery(rk3_usb_session *session, bool clear_output, bool rearm_input) {
    if (session->stopping_io || atomic_load(&session->closing) || session->transport_failed) return;
    int32_t input_error = 0, output_error = 0;
    if (session->command_recovering) {
        if (!clear_output || session->recovery_clears_output) return;
        /* A concurrent write failure can extend an IN-only recovery. IN has
         * already been cleared; clear OUT once without repeating the first. */
        session->recovery_clears_output = true;
        session->recovery_rearms_input |= rearm_input;
        output_error = rk3_host_clear_stall(session->interface, session->command_out);
    } else {
        session->command_recovering = true;
        session->recovery_clears_output = clear_output;
        session->recovery_rearms_input = rearm_input;
        session->recovery_timed_out = false;
        session->command_recovery_deadline = seconds_now() + (double)DRAIN_MS / 1000.0;
        input_error = rk3_host_clear_stall(session->interface, session->command_in);
        output_error = clear_output ? rk3_host_clear_stall(session->interface, session->command_out) : 0;
    }
    int32_t error = input_error != 0 ? input_error : output_error;
    if (error != 0) {
        session->command_recovering = false; session->command_suspended = true;
        if (device_lost(error)) {
            spec_revoke(session);
            session->transport_failed = true;
            set_error(session, error, "Command interface unavailable during endpoint clear");
        } else command_warning(session, error, "EP1 endpoint clear failed; reader suspended");
    }
}

static void service_command_recovery(rk3_usb_session *session, double now) {
    if (!session->command_recovering || session->stopping_io || session->transport_failed ||
        atomic_load(&session->closing)) return;
    if (session->pending || (session->recovery_clears_output && session->pending_write)) {
        if (!session->recovery_timed_out && now >= session->command_recovery_deadline) {
            session->recovery_timed_out = true;
            command_warning(session, ETIMEDOUT, "EP1 clear still awaiting callbacks; command storage retained");
        }
        return;
    }
    bool delayed = session->recovery_timed_out, rearm = session->recovery_rearms_input;
    session->command_recovering = session->recovery_clears_output = session->recovery_rearms_input = false;
    if (delayed) command_warning(session, ETIMEDOUT, "EP1 clear drained after deadline; command storage retired");
    if (rearm) {session->command_suspended = false;(void)arm_reader(session);}
}

static bool command_write_failure(rk3_usb_session *session, int32_t error) {
    if (device_lost(error)) {
        spec_revoke(session);
        session->transport_failed = true;
        set_error(session, error, "Command interface unavailable during write");
    } else {
        command_warning(session, error, "EP1 command write failed; packet not replayed");
        if ((uint32_t)error == UINT32_C(0xe000404f) ||
            (uint32_t)error == UINT32_C(0xe0004051) || error == kIOReturnTimeout)
            begin_command_recovery(session, true, session->pending && !session->command_suspended);
    }
    return false;
}

static void command_write_complete(void *context, int32_t result, size_t actual) {
    rk3_usb_session *session = context;
    if (!session->pending_write) {
        spec_revoke(session);
        session->transport_failed = true;
        set_error(session, EPROTO, "Unexpected command write completion"); return;
    }
    session->write_result = result;
    if (result == kIOReturnSuccess && actual != session->write_length)
        session->write_result = kIOReturnUnderrun;
    session->pending_write = false;
}

static bool active_request_cancelled(rk3_usb_session *session) {
    if (atomic_load(&session->closing)) return true;
    if (session->active_request == NULL) return false;
    pthread_mutex_lock(&session->mutex);
    bool cancelled = session->active_request->cancelled;
    pthread_mutex_unlock(&session->mutex);
    return cancelled;
}

static bool command_worker(void *context, const uint8_t *bytes, size_t length, uint32_t timeout_ms) {
    rk3_usb_session *session = context;
    if (session->interface == NULL || !session->opened || session->transport_failed ||
        session->pending_write || session->command_recovering || length == 0 || length > 64 || atomic_load(&session->closing)) return false;
    double deadline = seconds_now() + (double)timeout_ms / 1000.0;
    if (session->active_request != NULL) {
        pthread_mutex_lock(&session->mutex);
        bool cancelled = session->active_request->cancelled;
        double request_deadline = session->active_request->deadline;
        pthread_mutex_unlock(&session->mutex);
        if (cancelled || seconds_now() >= request_deadline) return false;
        if (deadline > request_deadline) deadline = request_deadline;
    }
    double remaining_ms = (deadline - seconds_now()) * 1000.0;
    if (remaining_ms < 1) return false;
    UInt32 write_timeout = remaining_ms < COMMAND_TIMEOUT ? (UInt32)remaining_ms : COMMAND_TIMEOUT;
    session->ack_result = -1;
    if (bytes[0] == RK3_COMMAND_INFO) session->info_received = false;
    bool digital_read = bytes[0] == RK3_COMMAND_DIGITAL && length == 1;
    if (digital_read) {
        pthread_mutex_lock(&session->mutex); session->digital_known = false; pthread_mutex_unlock(&session->mutex);
    }
    memcpy(session->write_buffer, bytes, length);
    session->write_length = length;
    session->write_result = kIOReturnNotReady;
    session->pending_write = true;
    double write_deadline = seconds_now() + (double)write_timeout / 1000.0;
    if (write_deadline > deadline) write_deadline = deadline;
    IOReturn result = rk3_host_bulk_write(session->interface, session->command_out,
        session->write_buffer, length, write_timeout, command_write_complete, session);
    if (result != kIOReturnSuccess) {
        session->pending_write = false;
        return command_write_failure(session, result);
    }
    while (session->pending_write) {
        bool cancelled = active_request_cancelled(session);
        if (cancelled || session->transport_failed || seconds_now() >= write_deadline) {
            if (session->transport_failed) return false;
            command_warning(session, cancelled ? ECANCELED : ETIMEDOUT,
                "EP1 write cancelled/timed out; packet not replayed, storage retained");
            if (cancelled) (void)rk3_host_abort(session->interface, session->command_out);
            else begin_command_recovery(session, true, session->pending && !session->command_suspended);
            return false; /* Keep the session buffer until its callback retires. */
        }
        /* Firmware may wait for another command read before accepting OUT.
         * Pump this same run loop so persistent EP1-IN can complete/rearm. */
        pump_callbacks(session, 0.005);
    }
    if (session->write_result != kIOReturnSuccess)
        return command_write_failure(session, session->write_result);
    if (active_request_cancelled(session) || seconds_now() >= write_deadline) {
        command_warning(session, ETIMEDOUT, "EP1 write completed after caller deadline; packet not replayed");
        return false;
    }
    while ((bytes[0] == RK3_COMMAND_INFO && !session->info_received) ||
           (bytes[0] == RK3_COMMAND_AUDIO_PARAMS && session->ack_result < 0) ||
           (digital_read && !session->digital_known)) {
        bool cancelled = false;
        if (session->active_request != NULL) {
            pthread_mutex_lock(&session->mutex);
            cancelled = session->active_request->cancelled;
            pthread_mutex_unlock(&session->mutex);
        }
        if (cancelled || session->transport_failed || atomic_load(&session->closing) || seconds_now() >= deadline) {
            if (bytes[0] == RK3_COMMAND_AUDIO_PARAMS && !session->transport_failed)
                set_audio_error(session, ETIMEDOUT, "Audio parameter reply timed out; command transport remains connected");
            else set_error(session, ETIMEDOUT, "Device command reply timed out");
            return false;
        }
        pump_callbacks(session, 0.01);
    }
    /* NI performAudioEngineStart ignores setAudioParams' stop-sentinel
     * result, then requires success for the real format. Hardware returns
     * ACK9=0 for the rate-ff stop. Still require an actual bounded reply. */
    bool stop_sentinel = length == RK3_AUDIO_PARAMS_BYTES &&
        bytes[0] == RK3_COMMAND_AUDIO_PARAMS && bytes[1] == 255;
    if (bytes[0] == RK3_COMMAND_AUDIO_PARAMS && !stop_sentinel && session->ack_result != 1) {
        set_audio_error(session, EPROTO, "Device rejected audio parameters"); return false;
    }
    pthread_mutex_lock(&session->mutex);
    ++session->status.commands_completed;
    pthread_mutex_unlock(&session->mutex);
    return true;
}

static bool initialize_device_commands(rk3_usb_session *session) {
    /* NI RK3 setupTimerCallback@0x365d..0x3691 sends this exact command5
     * before audio-engine creation. NIHardwareAgent configures the RK3 with
     * command0f's 32-byte payload and subscribes with command0b [1,2,5]. */
    const uint8_t audio_initialization[] = {5, 0, 0x40, 0x40, 0, 0, 0};
    const uint8_t controller_configuration[33] = {0x0f, 2};
    const uint8_t controller_reports[] = {0x0b, 1, 2, 5};
    const uint8_t snapshot_command[] = {RK3_COMMAND_DIGITAL};
    /* setupTimerCallback0x3691 ignores command05's nonfatal result before
     * engine creation. Pending write storage still blocks later admission;
     * removal, cancellation and stopped lifetime remain binding guards. */
    (void)command_worker(session, audio_initialization, sizeof(audio_initialization), COMMAND_TIMEOUT);
    if (session->transport_failed || session->stopping_io || active_request_cancelled(session)) return false;
    /* A9's merged controller activation/defaults and required digital baseline
     * remain unresolved; this slice changes only the original command05 gate. */
    return command_worker(session, controller_configuration, sizeof(controller_configuration), COMMAND_TIMEOUT) &&
        command_worker(session, controller_reports, sizeof(controller_reports), COMMAND_TIMEOUT) &&
        command_worker(session, snapshot_command, sizeof(snapshot_command), REPLY_TIMEOUT_MS);
}

static bool wait_audio_clock(rk3_usb_session *session, rk3_native_audio_iso *audio) {
    double deadline = seconds_now() + (double)REPLY_TIMEOUT_MS / 1000.0;
    if (session->active_request != NULL && deadline > session->active_request->deadline)
        deadline = session->active_request->deadline;
    for (;;) {
        bool cancelled = false;
        if (session->active_request != NULL) {
            pthread_mutex_lock(&session->mutex);
            cancelled = session->active_request->cancelled;
            pthread_mutex_unlock(&session->mutex);
        }
        int32_t error = rk3_native_audio_iso_error(audio);
        uint32_t panic_delay = 0; int32_t panic_reason = 0;
        bool panic = rk3_native_audio_iso_panic(audio, &panic_delay, &panic_reason);
        if (cancelled || atomic_load(&session->closing) || session->transport_failed ||
            error != 0 || panic || !rk3_native_audio_iso_running(audio) || seconds_now() >= deadline) {
            set_audio_error(session, error != 0 ? error : panic && panic_reason != 0 ? panic_reason : ETIMEDOUT,
                "No valid completed-device audio clock before startup deadline");
            return false;
        }
        rk3_native_audio_clock clock;
        if (rk3_native_audio_iso_clock(audio, &clock)) return true;
        pump_callbacks(session, 0.01);
    }
}

/* Selected IOUSBFamily 686 reader; capture always requests 31 output bytes. */
static int32_t read_identity_string(IOUSBDeviceInterface300 **device,uint8_t index,
    uint8_t *output,int count,uint16_t language,bool *defined) {
    *defined=false;
    if (count<6) return (int32_t)UINT32_C(0xe00002c2);
    memset(output,0,(size_t)count);*defined=true;
    /* Retain the original exact clears and suffix, without reading unspecified
     * stack bytes. A prior fallback clear makes the whole scratch defined. */
    uint8_t scratch[256];size_t known=2;memset(scratch,0,2);
    IOUSBDevRequestTO request={.bmRequestType=0x80,.bRequest=6,
        .wValue=(uint16_t)(0x300|index),.wIndex=language,.wLength=2,
        .pData=scratch,.noDataTimeout=5000,.completionTimeout=0};
    int32_t result=(*device)->DeviceRequestTO(device,&request);
    if (result && !((uint32_t)result==UINT32_C(0xe00002e8)&&scratch[1]==3)) {
        memset(scratch,0,256);known=256;request.wLength=256;request.wLenDone=0;
        result=(*device)->DeviceRequestTO(device,&request);
        if (result && (uint32_t)result!=UINT32_C(0xe00002e7)) return result;
    }
    unsigned length=scratch[0];
    if (!length) return 0;
    if (scratch[1]!=3) return (int32_t)UINT32_C(0xe00002e9);
    scratch[0]&=0xfe;memset(scratch,0,length);if(length>known)known=length;
    request.wLength=(uint16_t)length;request.wLenDone=0;
    result=(*device)->DeviceRequestTO(device,&request);
    if (result) return result;
    if (scratch[1]!=3) return (int32_t)UINT32_C(0xe00002e9);
    unsigned end=(unsigned)(scratch[0]&0xfe),written=0;
    for (unsigned offset=2;offset+1<end;offset+=2) {
        /* The original loads this unit before checking the output budget. */
        if (offset+1>=known) {*defined=false;memset(output,0,(size_t)count);return 0;}
        uint16_t unit=(uint16_t)(scratch[offset]|((uint16_t)scratch[offset+1]<<8));
        if (!unit) break;
        uint8_t encoded[3];unsigned bytes;
        if (unit<0x80) {encoded[0]=(uint8_t)unit;bytes=1;}
        else if (unit<0x800) {
            encoded[0]=(uint8_t)(0xc0|(unit>>6));encoded[1]=(uint8_t)(0x80|(unit&0x3f));bytes=2;
        } else {
            encoded[0]=(uint8_t)(0xe0|(unit>>12));encoded[1]=(uint8_t)(0x80|((unit>>6)&0x3f));
            encoded[2]=(uint8_t)(0x80|(unit&0x3f));bytes=3;
        }
        written+=bytes;
        if (written>(unsigned)(count-1)) break;
        memcpy(output+written-bytes,encoded,bytes);
    }
    return 0;
}

/* NI ignores string-reader failures for startup. Preserve each raw result and
 * defined bytes without making metadata acquisition a new readiness gate. */
static void capture_identity(IOUSBDeviceInterface300 **device,RK3NativeUSBIdentity *value) {
    *value=(RK3NativeUSBIdentity){.version=RK3_CONTROL_VERSION,.attempted_mask=RK3_USB_IDENTITY_GETTER_MASK};
    UInt16 word=0;
    value->step_status[RK3_USB_IDENTITY_VENDOR_ID]=(*device)->GetDeviceVendor(device,&word);
    if (!value->step_status[RK3_USB_IDENTITY_VENDOR_ID]) {
        value->vendor_id=word;value->defined_mask|=1u<<RK3_USB_IDENTITY_VENDOR_ID;
    }
    word=0;value->step_status[RK3_USB_IDENTITY_PRODUCT_ID]=(*device)->GetDeviceProduct(device,&word);
    if (!value->step_status[RK3_USB_IDENTITY_PRODUCT_ID]) {
        value->product_id=word;value->defined_mask|=1u<<RK3_USB_IDENTITY_PRODUCT_ID;
    }
    IOReturn (*indices[3])(void *,UInt8 *)={(*device)->USBGetProductStringIndex,
        (*device)->USBGetManufacturerStringIndex,(*device)->USBGetSerialNumberStringIndex};
    uint8_t *index_values[3]={&value->product_index,&value->manufacturer_index,&value->serial_index};
    uint8_t *strings[3]={value->product,value->manufacturer,value->serial};
    for (unsigned field=0;field<3;++field) {
        unsigned index_step=RK3_USB_IDENTITY_PRODUCT_INDEX+field;
        unsigned reader_step=RK3_USB_IDENTITY_PRODUCT_READER+field;
        UInt8 index=0;value->step_status[index_step]=indices[field](device,&index);
        if (value->step_status[index_step]) continue;
        *index_values[field]=index;value->defined_mask|=1u<<index_step;
        value->attempted_mask|=1u<<reader_step;
        bool defined=false;
        value->step_status[reader_step]=read_identity_string(device,index,strings[field],
            RK3_USB_IDENTITY_STRING_BYTES,0x0409,&defined);
        if (defined)value->defined_mask|=1u<<reader_step;
    }
}

static bool configure_device(rk3_usb_session *session) {
    UInt8 configuration = 0;
    IOReturn result = (*session->device)->GetConfiguration(session->device, &configuration);
    if (result != kIOReturnSuccess) { set_error(session, result, "Cannot read USB configuration"); return false; }
    /* NI initUSB@0x8e0 reapplies its advertised configuration even on an
     * already configured device. Preserve that warm-device initialization;
     * cached configuration==1 does not establish command-pipe readiness. */
    {
        if (!advertised_configuration(session->device, 1)) { set_error(session, EPROTO, "Configuration1/interface0/alt1 unavailable"); return false; }
        result = (*session->device)->USBDeviceOpen(session->device);
        if (result != kIOReturnSuccess) { set_error(session, result, "Cannot open RK3 device without seizure"); return false; }
        RK3NativeUSBIdentity identity;capture_identity(session->device,&identity);
        pthread_mutex_lock(&session->mutex);
        if (session->spec_collecting&&!atomic_load(&session->closing)) {
            session->identity_pending=identity;session->identity_pending_valid=true;
        }
        pthread_mutex_unlock(&session->mutex);
        result = (*session->device)->SetConfiguration(session->device, 1);
        IOReturn closed = (*session->device)->USBDeviceClose(session->device);
        if (result != kIOReturnSuccess || closed != kIOReturnSuccess) { set_error(session, result != kIOReturnSuccess ? result : closed, "USB configuration selection failed"); return false; }
        if ((*session->device)->GetConfiguration(session->device, &configuration) != kIOReturnSuccess || configuration != 1) { set_error(session, EPROTO, "USB configuration readback mismatch"); return false; }
    }
    if (!advertised_configuration(session->device, configuration)) { set_error(session, EPROTO, "Configured interface0/alt1 unavailable"); return false; }
    return true;
}

static bool reset_command_pipes(rk3_usb_session *session) {
    if (session->interface == NULL || !session->opened || !session->command_in || !session->command_out) {
        set_error(session, EINVAL, "Command pipes are not open"); return false;
    }
    /* NI initUSB@0x0d84/0x0d99 clears both bulk pipes with
     * IOUSBPipe::ClearPipeStall(true). Apple's user-space equivalent also
     * resynchronizes endpoint halt/toggle state on the device. Warm-open
     * differential hardware testing confirmed these two clears are needed. */
    /* initUSB ignores both returns. Keep that nonfatal failure policy while
     * retaining modern ownership/cancellation guards before each operation. */
    const UInt8 pipes[] = {session->command_in,session->command_out};
    for (size_t index = 0; index < sizeof(pipes) / sizeof(*pipes); ++index) {
        if (session->transport_failed || session->stopping_io || active_request_cancelled(session)) return false;
        IOReturn result = rk3_host_clear_stall(session->interface, pipes[index]);
        if (result != kIOReturnSuccess) {
            const char *text = index == 0 ? "Cannot reset command input pipe" : "Cannot reset command output pipe";
            if (device_lost(result)) {
                spec_revoke(session);
                session->transport_failed = true; set_error(session,result,text); return false;
            }
            command_warning(session,result,text);
        }
    }
    return !session->transport_failed && !session->stopping_io && !active_request_cancelled(session);
}

static bool start_command_io(rk3_usb_session *session) {
    /* connect_device has opened a fresh interface. Retire the previous lease's
     * stop/failure state before clears; preserve any failure raised by them. */
    session->stopping_io = false;
    session->command_recovering = session->recovery_clears_output = session->recovery_rearms_input = session->command_suspended = false;
    session->recovery_timed_out = false;
    session->transport_failed = false;
    if (!reset_command_pipes(session)) return false;
    /* initHardware0x1038 does not test initial Read64 admission before its
     * one INFO attempt at0x123f. A rejected reader stays suspended; the A7
     * bounded reply/readiness policy below is deliberately unchanged. */
    (void)arm_reader(session);
    if (session->transport_failed || session->stopping_io || active_request_cancelled(session)) return false;
    uint8_t command = RK3_COMMAND_INFO;
    return command_worker(session, &command, 1, REPLY_TIMEOUT_MS);
}

static bool connect_device(rk3_usb_session *session) {
    session->device = find_device();
    if (session->device == NULL) { set_error(session, ENODEV, "No unambiguous accessible RK3 device"); return false; }
    if (!configure_device(session)) return false;
    io_service_t service = find_interface(session->device);
    if (service == IO_OBJECT_NULL) { set_error(session, ENODEV, "Interface0 is unavailable"); return false; }
    int32_t open_error = 0;
    /* configure_device has closed the parent before this sole child claimant. */
    session->interface = rk3_host_open(service, session->run_loop, &open_error);
    IOObjectRelease(service);
    if (session->interface == NULL) { set_error(session, open_error, "Cannot open RK3 host interface"); return false; }
    session->opened = true;
    IOReturn result = rk3_host_get_alt(session->interface, &session->original_alt);
    if (result != kIOReturnSuccess) { set_error(session, result, "Cannot read alternate setting"); return false; }
    if (session->original_alt != 1) {
        result = rk3_host_set_alt(session->interface, 1);
        if (result != kIOReturnSuccess) { set_error(session, result, "Cannot select alternate1"); return false; }
        session->changed_alt = true;
    }
    UInt8 count = 0;
    result = rk3_host_num_pipes(session->interface, &count);
    if (result != kIOReturnSuccess) { set_error(session, result, "Cannot enumerate USB endpoints"); return false; }
    for (unsigned int pipe = 1; pipe <= count; ++pipe) {
        RK3HostPipeProperties properties;
        result = rk3_host_pipe_properties(session->interface, (UInt8)pipe, &properties);
        if (result != kIOReturnSuccess) { set_error(session, result, "Cannot inspect USB pipe"); return false; }
        if (properties.number == 1 && properties.transfer_type == kUSBBulk && properties.direction == kUSBIn) session->command_in = (UInt8)pipe;
        if (properties.number == 1 && properties.transfer_type == kUSBBulk && properties.direction == kUSBOut) session->command_out = (UInt8)pipe;
        if (properties.number == 2 && properties.transfer_type == kUSBIsoc && properties.direction == kUSBIn) session->iso_in = (UInt8)pipe;
        if (properties.number == 6 && properties.transfer_type == kUSBIsoc && properties.direction == kUSBOut) session->iso_out = (UInt8)pipe;
    }
    if (!session->command_in || !session->command_out || !session->iso_in || !session->iso_out) { set_error(session, EPROTO, "Expected command/audio endpoints absent"); return false; }
    if (!start_command_io(session)) return false;
    pthread_mutex_lock(&session->mutex);
    rk3_device_info info = session->status.info;
    pthread_mutex_unlock(&session->mutex);
    if (info.input_channels != 2 || info.output_channels != 2 || info.wire_mode != 2) { set_error(session, ENOTSUP, "Only verified stereo input/output wire mode2 is supported"); return false; }
    if (!initialize_device_commands(session)) return false;
    reclaim_retired(session);
    if (session->retired_count >= 4) {
        set_error(session, EBUSY, "Waiting for real-time readers before allocating another USB audio engine");
        return false;
    }
    rk3_native_audio_iso *audio = rk3_native_audio_iso_create(session->interface, session->iso_in, session->iso_out, command_worker, session);
    RetiredAudio *retired = calloc(1, sizeof(*retired));
    if (audio == NULL || retired == NULL) {
        if (audio != NULL) (void)rk3_native_audio_iso_destroy(audio);
        free(retired); set_error(session, ENOMEM, "Cannot allocate native audio state"); return false;
    }
    retired->audio = audio;
    retired->inputs=info.input_channels;retired->outputs=info.output_channels;
    rk3_input_gain_init(&retired->gain);
    retired->next = session->retired;
    session->retired = retired;
    ++session->retired_count;
    pthread_mutex_lock(&session->mutex);
    ++session->generation;
    spec_commit_locked(session);
    retired->generation=session->generation;
    atomic_store(&session->clock_generation, session->generation);
    session->status.state = RK3_USB_READY;
    session->status.error_code = 0;
    session->status.error[0] = '\0';
    pthread_mutex_unlock(&session->mutex);
    /* Old engines are unpublished before reconnect. Publish the new engine
     * only after its generation so clock readers cannot assign an old lease
     * to a newly published engine. Readers also recheck pointer+generation. */
    publish_audio(session, retired);
    session->panic_seen = session->panic_pending = false;
    if (session->want_audio) {
        pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_STARTING; pthread_mutex_unlock(&session->mutex);
        int32_t started = rk3_native_audio_iso_start(audio);
        if(started==0)started=sync_audio_clock_timer(session);
        if (started != 0 || !wait_audio_clock(session, audio)) {
            stop_audio_clock(session,audio);
            int32_t failure = started != 0 ? started : audio_failure(audio);
            if (device_lost(failure)) { spec_revoke(session); session->transport_failed = true; set_error(session, failure, "USB interface removed during audio start"); }
            else set_audio_error(session, failure, "Audio restart after reconnect has no valid clock");
            return !session->transport_failed;
        }
        pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_RUNNING; pthread_mutex_unlock(&session->mutex);
    }
    return true;
}

static bool io_pending(rk3_usb_session *session, rk3_native_audio_iso *audio) {
    return session->pending || session->pending_write ||
        (session->interface != NULL && rk3_host_pending(session->interface) != 0) ||
        (audio != NULL && !rk3_native_audio_iso_is_quiescent(audio));
}

static void disconnect_device(rk3_usb_session *session) {
    pthread_mutex_lock(&session->mutex);
    spec_revoke_locked(session);
    RetiredAudio *context=atomic_exchange(&session->audio,NULL);
    pthread_mutex_unlock(&session->mutex);
    rk3_native_audio_iso *audio=context?context->audio:NULL;
    session->panic_pending = false; session->panic_seen = true;
    session->stopping_io = true;
    session->command_recovering = false; session->command_suspended = true;
    stop_audio_clock(session,audio);
    if (session->pending && session->opened) (void)rk3_host_abort(session->interface, session->command_in);
    if (session->pending_write && session->opened) (void)rk3_host_abort(session->interface, session->command_out);
    double deadline = seconds_now() + (double)DRAIN_MS / 1000.0;
    while (io_pending(session, audio) && seconds_now() < deadline)
        pump_callbacks(session, 0.02);
    /* Cancellation retains the worker/session/audio buffers indefinitely if
     * necessary. Never free outstanding kernel I/O or exit coreaudiod. */
    while (io_pending(session, audio)) {
        set_error(session, ETIMEDOUT, "Waiting for USB cancellation callbacks; storage retained");
        pump_callbacks(session, 0.05);
    }
    /* Detach pinned USB buffers while the borrowed interface is still open.
     * Retired CPU queues remain alive for a racing real-time pointer load. */
    while (audio != NULL && !rk3_native_audio_iso_release_usb(audio)) {
        set_error(session, EBUSY, "Waiting for mapped audio buffers; interface retained");
        pump_callbacks(session, 0.05);
    }
    if (session->interface != NULL && session->opened && session->changed_alt) {
        IOReturn result = rk3_host_set_alt(session->interface, session->original_alt);
        if (result != kIOReturnSuccess) set_error(session, result, "Cannot restore original USB alternate");
    }
    if (session->interface != NULL && session->opened) {
        IOReturn result;
        while ((result = rk3_host_close(session->interface)) == kIOReturnBusy) {
            set_error(session, result, "Waiting for host mappings before USB close");
            pump_callbacks(session, 0.05);
        }
        if (result != kIOReturnSuccess) set_error(session, result, "USB interface close failed");
    }
    if (session->interface != NULL) rk3_host_release(session->interface);
    if (session->device != NULL) (*session->device)->Release(session->device);
    session->interface = NULL; session->device = NULL;
    session->opened = session->changed_alt = session->pending = false;
    session->pending_write = false;
    session->command_in = session->command_out = session->iso_in = session->iso_out = 0;
    reclaim_retired(session);
}

/* Preserve NI's panic timer's scope: notify the AUDIO host without resetting
 * EP1, histories, assignments or the USB connection-generation lease. ISO
 * classifies the exact recovered triggers and supplies their 0/100ms delay. */
static void audio_panic_tick(rk3_usb_session *session, double now) {
    rk3_native_audio_iso *audio = current_audio(session);
    if (audio == NULL || session->stopping_io || session->transport_failed) return;
    int32_t error = rk3_native_audio_iso_error(audio);
    if (device_lost(error)) {
        spec_revoke(session);
        session->transport_failed = true;
        set_error(session, error, "USB interface removed during audio I/O");
        return;
    }
    uint32_t delay = 0; int32_t panic_error = 0;
    if (session->want_audio && !session->panic_seen &&
        rk3_native_audio_iso_panic(audio, &delay, &panic_error)) {
        session->panic_seen = session->panic_pending = true;
        session->panic_deadline = now + (double)delay / 1000.0;
        session->panic_error = panic_error;
        set_audio_error(session, panic_error, "NI audio panic awaiting host pause/resume");
    } else if (error != 0 && !rk3_native_audio_iso_running(audio) &&
               !session->panic_pending && !session->panic_seen) {
        set_audio_error(session, error, "Native audio engine unavailable; USB controls remain connected");
    }
    if (!session->panic_pending || !session->want_audio || now < session->panic_deadline) return;
    pthread_mutex_lock(&session->mutex);
    rk3_usb_audio_panic_handler handler = session->panic_handler;
    void *callback_context = session->panic_context;
    bool notify = handler != NULL && !atomic_load(&session->closing);
    if (notify) ++session->panic_callbacks;
    pthread_mutex_unlock(&session->mutex);
    if (!notify) return;
    session->panic_pending = false; /* stable ISO signal is notified once per start */
    handler(callback_context, session->generation, session->panic_error);
    pthread_mutex_lock(&session->mutex);
    --session->panic_callbacks; pthread_cond_broadcast(&session->wake);
    pthread_mutex_unlock(&session->mutex);
}

static void defer_stop(rk3_usb_session *session, Request *request, double now) {
    request->next = NULL;
    pthread_mutex_lock(&session->mutex);
    if (session->stop_tail != NULL) session->stop_tail->next = request;
    else { session->stop_head = request; session->stop_deadline = now + (double)DRAIN_MS / 1000.0; }
    session->stop_tail = request; ++session->deferred_stops;
    pthread_mutex_unlock(&session->mutex);
}

static void complete_stops(rk3_usb_session *session, bool connected, double now) {
    if (session->stop_head == NULL) return;
    rk3_native_audio_iso *audio = current_audio(session);
    bool drained = rk3_native_audio_iso_is_quiescent(audio);
    if (!drained && now < session->stop_deadline) return;
    if (!drained) set_audio_error(session, ETIMEDOUT, "ISO stop is still draining; storage retained");
    pthread_mutex_lock(&session->mutex);
    Request *request = session->stop_head;
    session->stop_head = session->stop_tail = NULL; session->deferred_stops = 0;
    if (drained && connected && !session->transport_failed) session->status.state = RK3_USB_READY;
    while (request != NULL) {
        Request *next = request->next;
        complete_locked(request, drained ? 0 : ETIMEDOUT); request = next;
    }
    pthread_mutex_unlock(&session->mutex);
}

/* One request per outer turn: deferred stops never prevent subsequent MIDI or
 * LED commands from using the healthy shared command transport. */
static void process_one_request(rk3_usb_session *session, bool connected) {
    Request *request = NULL;
    pthread_mutex_lock(&session->mutex);
    Request **link = &session->head;
    Request *previous = NULL;
    if (session->command_recovering || session->pending_write) {
        /* Only later, not-yet-submitted packets wait behind endpoint cleanup.
         * Audio Stop can bypass them, so shared EP1 cancellation cannot deadlock
         * the HAL's pause lifecycle. The failed packet is never requeued. */
        while (*link != NULL && (*link)->kind != REQUEST_STOP) {
            previous = *link; link = &(*link)->next;
        }
    }
    if (*link != NULL) {
        request = *link; *link = request->next;
        if (session->tail == request) session->tail = previous;
        --session->queued;
    }
    bool cancelled = request != NULL && request->cancelled;
    pthread_mutex_unlock(&session->mutex);
    if (request != NULL) {
        session->active_request = request;
        int32_t result = cancelled ? ETIMEDOUT : connected ? 0 : ENODEV;
        rk3_native_audio_iso *audio = current_audio(session);
        bool deferred = false;
        if (request->kind == REQUEST_STOP) {
            /* A stop revokes automatic restart even when the device is absent. */
            session->want_audio = false;
            session->panic_pending = false; session->panic_seen = true;
            result = 0;
        }
        if (result == 0 && request->kind == REQUEST_COMMAND) {
            if (request->generation == 0 || request->generation != session->generation) result = ESTALE;
            else if (!command_worker(session, request->bytes, request->length, REPLY_TIMEOUT_MS)) result = EIO;
        } else if (result == 0 && request->kind == REQUEST_START) {
            pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_STARTING; pthread_mutex_unlock(&session->mutex);
            if(audio&&!rk3_native_audio_iso_running(audio))cancel_audio_clock_timer(session);
            result = audio == NULL ? ENODEV : session->stop_head != NULL ? kIOReturnBusy :
                rk3_native_audio_iso_running(audio) ? 0 :
                !rk3_native_audio_iso_is_quiescent(audio) ? kIOReturnBusy : rk3_native_audio_iso_start(audio);
            if(result==0)result=sync_audio_clock_timer(session);
            if (result == 0 && !wait_audio_clock(session, audio)) result = audio_failure(audio);
            pthread_mutex_lock(&session->mutex);
            bool expired = request->cancelled || seconds_now() >= request->deadline;
            pthread_mutex_unlock(&session->mutex);
            if (expired) {
                stop_audio_clock(session,audio);
                session->want_audio = false;
                result = ETIMEDOUT;
            }
            if (result != 0) {
                stop_audio_clock(session,audio);
                if (device_lost(result)) {
                    spec_revoke(session);
                    session->transport_failed = true;
                    set_error(session, result, "USB interface removed during audio start");
                }
            }
            if (result == 0) {
                session->want_audio = true;
                session->panic_seen = session->panic_pending = false;
            }
            pthread_mutex_lock(&session->mutex);
            if (!session->transport_failed) session->status.state = result == 0 ? RK3_USB_RUNNING : RK3_USB_STOPPING;
            if (result == 0) { session->status.error_code = 0; session->status.error[0] = '\0'; }
            else { session->status.error_code = result; snprintf(session->status.error, sizeof(session->status.error), "Native ISO audio startup failed"); }
            pthread_mutex_unlock(&session->mutex);
        } else if (result == 0 && request->kind == REQUEST_STOP) {
            session->want_audio = false;
            if (connected) { pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_STOPPING; pthread_mutex_unlock(&session->mutex); }
            stop_audio_clock(session,audio);
            if (audio != NULL && !rk3_native_audio_iso_is_quiescent(audio)) {
                defer_stop(session, request, seconds_now()); deferred = true;
            } else if (connected && !session->transport_failed) {
                pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_READY; pthread_mutex_unlock(&session->mutex);
            }
        }
        session->active_request = NULL;
        if (!deferred) { pthread_mutex_lock(&session->mutex); complete_locked(request, result); pthread_mutex_unlock(&session->mutex); }
    }
}

static void *worker_main(void *context) {
    rk3_usb_session *session = context;
    pthread_mutex_lock(&session->mutex);
    session->run_loop = CFRunLoopGetCurrent(); CFRetain(session->run_loop);
    session->worker_thread = pthread_self(); session->worker_thread_known = true;
    pthread_mutex_unlock(&session->mutex);
    bool connected = false;
    double next_connect = 0;
    while (!atomic_load(&session->closing)) {
        if (!connected && seconds_now() >= next_connect) {
            pthread_mutex_lock(&session->mutex);
            spec_begin_attempt_locked(session);
            session->history_count = session->history_head = 0;
            session->digital_known = false;
            session->digital_snapshot = 0;
            pthread_mutex_unlock(&session->mutex);
            connected = connect_device(session);
            if (!connected) disconnect_device(session);
            next_connect = seconds_now() + (double)CONNECT_RETRY_MS / 1000.0;
        }
        check_connection_presence(session);
        service_command_recovery(session, seconds_now());
        process_one_request(session, connected && !session->transport_failed);
        service_command_recovery(session, seconds_now());
        service_audio(session);
        if (connected) audio_panic_tick(session, seconds_now());
        complete_stops(session, connected, seconds_now());
        if (connected && session->transport_failed) { disconnect_device(session); connected = false; next_connect = seconds_now() + 1; }
        reclaim_retired(session);
        if (connected) pump_callbacks(session, 0.01);
        else {
            struct timespec relative = { 0, 100000000 };
            pthread_mutex_lock(&session->mutex);
            if (!atomic_load(&session->closing) && session->head == NULL)
                (void)pthread_cond_timedwait_relative_np(&session->wake, &session->mutex, &relative);
            pthread_mutex_unlock(&session->mutex);
        }
    }
    disconnect_device(session);
    cancel_audio_clock_timer(session);
    pthread_mutex_lock(&session->mutex);
    while (session->head != NULL) {
        Request *request = session->head; session->head = request->next;
        complete_locked(request, ECANCELED);
    }
    while (session->stop_head != NULL) {
        Request *request = session->stop_head; session->stop_head = request->next;
        complete_locked(request, ECANCELED);
    }
    session->stop_tail = NULL; session->deferred_stops = 0;
    CFRunLoopRef run_loop = session->run_loop;
    session->run_loop = NULL;
    session->status.state = RK3_USB_CLOSED;
    pthread_cond_broadcast(&session->wake);
    pthread_mutex_unlock(&session->mutex);
    CFRelease(run_loop);
    release_session(session);
    return NULL;
}

rk3_usb_session *rk3_usb_open(char *error, size_t error_size) {
    rk3_usb_session *session = calloc(1, sizeof(*session));
    if (session == NULL) { if (error != NULL && error_size) snprintf(error, error_size, "Allocation failed"); return NULL; }
    int result = pthread_mutex_init(&session->mutex, NULL);
    if (result != 0) { free(session); return NULL; }
    result = pthread_cond_init(&session->wake, NULL);
    if (result != 0) { pthread_mutex_destroy(&session->mutex); free(session); return NULL; }
    atomic_init(&session->references, 2); atomic_init(&session->closing, false); atomic_init(&session->audio, NULL);
    atomic_init(&session->clock_generation, 0);
    atomic_init(&session->output_active, true);
    atomic_init(&session->rt_readers, 0);
    session->status.state = RK3_USB_OPENING;
    pthread_t thread;
    result = pthread_create(&thread, NULL, worker_main, session);
    if (result != 0) {
        if (error != NULL && error_size) snprintf(error, error_size, "USB worker creation failed: %d", result);
        atomic_store(&session->references, 1); release_session(session); return NULL;
    }
    pthread_detach(thread);
    if (error != NULL && error_size) error[0] = '\0';
    return session;
}

void rk3_usb_close(rk3_usb_session *session) {
    if (session == NULL) return;
    pthread_mutex_lock(&session->mutex);
    spec_revoke_locked(session);
    atomic_store(&session->closing, true); wake_worker_locked(session);
    pthread_mutex_unlock(&session->mutex);
    release_session(session);
}

bool rk3_usb_close_wait(rk3_usb_session *session, uint32_t timeout_ms) {
    if (session == NULL) return true;
    if (timeout_ms > 30000) timeout_ms = 30000;
    double deadline = seconds_now() + (double)timeout_ms / 1000.0;
    pthread_mutex_lock(&session->mutex);
    spec_revoke_locked(session);
    atomic_store(&session->closing, true); wake_worker_locked(session);
    while (session->status.state != RK3_USB_CLOSED) {
        double remaining = deadline - seconds_now();
        if (remaining <= 0) break;
        struct timespec relative = { (time_t)remaining,
            (long)((remaining - (double)(time_t)remaining) * 1e9) };
        int waited = pthread_cond_timedwait_relative_np(&session->wake, &session->mutex, &relative);
        if (waited != 0 && waited != ETIMEDOUT) break;
    }
    bool closed = session->status.state == RK3_USB_CLOSED;
    pthread_mutex_unlock(&session->mutex);
    release_session(session);
    return closed;
}

bool rk3_usb_get_status(rk3_usb_session *session, rk3_usb_status *status) {
    if (session == NULL || status == NULL) return false;
    pthread_mutex_lock(&session->mutex); *status = session->status; pthread_mutex_unlock(&session->mutex);
    return true;
}

bool rk3_usb_info(rk3_usb_session *session, RK3NativeInfo *info) {
    if (session == NULL || info == NULL) return false;
    pthread_mutex_lock(&session->mutex);
    *info = (RK3NativeInfo){ .version = RK3_CONTROL_VERSION, .generation = session->generation,
        .last_error = session->status.error_code, .sample_rate = 48000,
        .audio_inputs = session->status.info.input_channels, .audio_outputs = session->status.info.output_channels,
        .midi_inputs = session->status.info.midi_inputs, .midi_outputs = session->status.info.midi_outputs,
        .wire_mode = session->status.info.wire_mode,
        .digital_snapshot = session->digital_known ? UINT32_C(0x80000000) | session->digital_snapshot : 0 };
    /* Stopping ISO does not disconnect USB or its DIN/controller endpoints. */
    if (session->status.state == RK3_USB_READY || session->status.state == RK3_USB_RUNNING ||
        session->status.state == RK3_USB_STARTING || session->status.state == RK3_USB_STOPPING)
        info->flags |= RK3_NATIVE_CONNECTED;
    if (session->status.state == RK3_USB_RUNNING) info->flags |= RK3_NATIVE_AUDIO_RUNNING;
    if (session->status.state == RK3_USB_FAILED) info->flags |= RK3_NATIVE_FAILED;
    snprintf(info->device_uid, sizeof(info->device_uid), "%s", RK3_NATIVE_DEVICE_UID);
    snprintf(info->product_name, sizeof(info->product_name), "Rig Kontrol 3 Native ARM");
    pthread_mutex_unlock(&session->mutex);
    return true;
}

int32_t rk3_usb_copy_spec(rk3_usb_session *session, uint64_t generation,
                          uint8_t out14[RK3_INFO_BYTES]) {
    if (!session || !generation || !out14) return EINVAL;
    pthread_mutex_lock(&session->mutex);
    int32_t result=atomic_load(&session->closing)||!session->spec_available?ENODEV:
        generation!=session->spec_generation?ESTALE:0;
    if (!result) memcpy(out14,session->spec_raw,RK3_INFO_BYTES);
    pthread_mutex_unlock(&session->mutex);
    return result;
}

int32_t rk3_usb_copy_identity(rk3_usb_session *session,uint64_t generation,
                              RK3NativeUSBIdentity *output) {
    if (!session || !generation || !output) return EINVAL;
    pthread_mutex_lock(&session->mutex);
    int32_t result=atomic_load(&session->closing)||!session->identity_available||
        !session->spec_generation||session->spec_generation!=session->generation?ENODEV:
        generation!=session->spec_generation?ESTALE:0;
    if (!result)*output=session->identity_current;
    pthread_mutex_unlock(&session->mutex);return result;
}

bool rk3_usb_reports(rk3_usb_session *session, const RK3NativeReportQuery *query, RK3NativeReports *reports) {
    if (session == NULL || query == NULL || reports == NULL || query->version != RK3_CONTROL_VERSION) return false;
    pthread_mutex_lock(&session->mutex);
    *reports = (RK3NativeReports){ .version = RK3_CONTROL_VERSION, .next_sequence = query->after_sequence,
        .latest_sequence = session->sequence };
    if (session->history_count != 0) {
        size_t first = (session->history_head + REPORT_HISTORY - session->history_count) % REPORT_HISTORY;
        uint64_t oldest = session->reports[first].sequence;
        if (query->after_sequence < oldest && oldest - query->after_sequence > 1)
            reports->dropped = oldest - query->after_sequence - 1;
        for (size_t index = 0; index < session->history_count && reports->count < RK3_NATIVE_REPORT_LIMIT; ++index) {
            const RK3NativeReport *report = &session->reports[(first + index) % REPORT_HISTORY];
            if (report->sequence > query->after_sequence) {
                reports->reports[reports->count++] = *report;
                reports->next_sequence = report->sequence;
            }
        }
    }
    pthread_mutex_unlock(&session->mutex);
    return true;
}

bool rk3_usb_command_for_generation(rk3_usb_session *session, const uint8_t *bytes, size_t length,
                                    uint64_t generation, uint32_t timeout_ms) {
    if (session == NULL || bytes == NULL || length > 64) return false;
    bool midi = length >= 4 && bytes[0] == RK3_COMMAND_MIDI_OUTPUT &&
        bytes[2] != 0 && bytes[2] <= 61 && length == (size_t)bytes[2] + 3;
    bool led = length == 9 && bytes[0] == RK3_COMMAND_WRITE_IO && bytes[1] == 0 &&
        bytes[2] == 0x40 && bytes[3] == 0x40 && bytes[4] == 0 &&
        bytes[6] <= 1 && bytes[7] == 0 && bytes[8] == 0;
    if (!midi && !led) return false;
    pthread_mutex_lock(&session->mutex);
    bool valid_port = !midi || bytes[1] < session->status.info.midi_outputs;
    pthread_mutex_unlock(&session->mutex);
    return valid_port && generation != 0 && submit(session, REQUEST_COMMAND, bytes, length, generation, timeout_ms) == 0;
}

bool rk3_usb_command(rk3_usb_session *session, const uint8_t *bytes, size_t length, uint32_t timeout_ms) {
    if (session == NULL) return false;
    pthread_mutex_lock(&session->mutex); uint64_t generation = session->generation; pthread_mutex_unlock(&session->mutex);
    return rk3_usb_command_for_generation(session, bytes, length, generation, timeout_ms);
}

bool rk3_usb_write_midi(rk3_usb_session *session, uint8_t port, const uint8_t *bytes, size_t length, uint32_t timeout_ms) {
    if (bytes == NULL || length == 0 || length > 61) return false;
    uint8_t command[64] = { RK3_COMMAND_MIDI_OUTPUT, port, (uint8_t)length };
    memcpy(command + 3, bytes, length);
    return rk3_usb_command(session, command, length + 3, timeout_ms);
}

/* Only the non-RT property path takes this gate. Publication/unpublication
 * use the same lock; a lease cannot commit into an already retired context. */
static int32_t input_gain(rk3_usb_session *session,uint64_t generation,uint32_t channel,uint32_t *bits,bool write,bool *changed) {
    if(changed)*changed=false;
    if(!session||!bits||!generation)return EINVAL;
    atomic_fetch_add(&session->rt_readers,1);pthread_mutex_lock(&session->mutex);
    RetiredAudio *context=atomic_load(&session->audio);
    int32_t result=0;
    if(!context||atomic_load(&session->closing)||
       (session->status.state!=RK3_USB_READY&&session->status.state!=RK3_USB_RUNNING&&
        session->status.state!=RK3_USB_STARTING&&session->status.state!=RK3_USB_STOPPING))result=ENODEV;
    else if(generation!=context->generation||generation!=session->generation)result=ESTALE;
    else if(write){
        uint32_t previous=(uint32_t)(rk3_input_gain_snapshot(&context->gain,channel/2)>>((channel&1)*32));
        if(!rk3_input_gain_set(&context->gain,context->inputs,context->outputs,channel,*bits))result=EINVAL;
        else if(changed)*changed=previous!=*bits;
    } else if(!rk3_input_gain_get(&context->gain,context->inputs,channel,bits))result=EINVAL;
    pthread_mutex_unlock(&session->mutex);atomic_fetch_sub(&session->rt_readers,1);return result;
}
int32_t rk3_usb_input_gain_get(rk3_usb_session *session,uint64_t generation,uint32_t channel,uint32_t *bits) {
    return input_gain(session,generation,channel,bits,false,NULL);
}
int32_t rk3_usb_input_gain_set(rk3_usb_session *session,uint64_t generation,uint32_t channel,uint32_t bits,bool *changed) {
    return input_gain(session,generation,channel,&bits,true,changed);
}
void rk3_usb_set_output_active(rk3_usb_session *session, bool active) {
    if (session == NULL) return;
    atomic_fetch_add(&session->rt_readers, 1);
    pthread_mutex_lock(&session->mutex);
    atomic_store(&session->output_active, active);
    rk3_native_audio_iso *audio = current_audio(session);
    if (audio != NULL) rk3_native_audio_iso_set_output_active(audio, active);
    pthread_mutex_unlock(&session->mutex);
    atomic_fetch_sub(&session->rt_readers, 1);
}

bool rk3_usb_set_audio_panic_handler(rk3_usb_session *session,
    rk3_usb_audio_panic_handler handler, void *context) {
    if (session == NULL) return false;
    pthread_mutex_lock(&session->mutex);
    if ((handler != NULL && atomic_load(&session->closing)) ||
        (session->panic_callbacks != 0 && session->worker_thread_known &&
         pthread_equal(pthread_self(), session->worker_thread))) {
        pthread_mutex_unlock(&session->mutex); return false;
    }
    session->panic_handler = NULL;
    while (session->panic_callbacks != 0) pthread_cond_wait(&session->wake, &session->mutex);
    session->panic_context = context; session->panic_handler = handler;
    wake_worker_locked(session);
    pthread_mutex_unlock(&session->mutex);
    return true;
}

int32_t rk3_usb_audio_start(rk3_usb_session *session, uint32_t timeout_ms) { return submit(session, REQUEST_START, NULL, 0, 0, timeout_ms); }
int32_t rk3_usb_audio_stop(rk3_usb_session *session, uint32_t timeout_ms) { return submit(session, REQUEST_STOP, NULL, 0, 0, timeout_ms); }

size_t rk3_usb_render_at(rk3_usb_session *session, const float *interleaved,
    uint64_t first_sample_frame, size_t frames) {
    if (session == NULL || interleaved == NULL) return 0;
    atomic_fetch_add(&session->rt_readers, 1);
    rk3_native_audio_iso *audio = current_audio(session);
    size_t accepted = audio != NULL ? rk3_native_audio_iso_render_at(audio, interleaved, first_sample_frame, frames) : 0;
    atomic_fetch_sub(&session->rt_readers, 1);
    return accepted;
}

size_t rk3_usb_capture_at(rk3_usb_session *session, float *interleaved,
    uint64_t first_sample_frame, size_t frames) {
    if (interleaved == NULL || frames > SIZE_MAX / (2 * sizeof(float))) return 0;
    if (session != NULL) atomic_fetch_add(&session->rt_readers, 1);
    RetiredAudio *context=session?atomic_load(&session->audio):NULL;
    size_t captured=context?rk3_native_audio_iso_capture_at(context->audio,interleaved,first_sample_frame,frames):0;
    if(context){
        uint64_t gain=rk3_input_gain_snapshot(&context->gain,0);
#ifdef RK3_NATIVE_USB_TESTING
        extern void rk3_test_input_gain_snapshot(void);
        rk3_test_input_gain_snapshot();
#endif
        rk3_input_gain_apply(gain,interleaved,captured);
    }
    if (session != NULL) atomic_fetch_sub(&session->rt_readers, 1);
    if (captured < frames) memset(interleaved + 2 * captured, 0, (frames - captured) * 2 * sizeof(float));
    return captured;
}

bool rk3_usb_clock(rk3_usb_session *session, rk3_usb_clock_snapshot *clock) {
    if (session == NULL || clock == NULL) return false;
    atomic_fetch_add(&session->rt_readers, 1);
    uint64_t generation = atomic_load(&session->clock_generation);
    RetiredAudio *context=atomic_load(&session->audio);
    rk3_native_audio_clock value;
    if (context == NULL || !rk3_native_audio_iso_clock(context->audio, &value) ||
        context != atomic_load(&session->audio) || generation != atomic_load(&session->clock_generation)) {
        atomic_fetch_sub(&session->rt_readers, 1); return false;
    }
    *clock = (rk3_usb_clock_snapshot){
        .sample_frame = value.sample_frame, .host_time = value.host_time,
        .seed = (generation << 32) | (value.seed & UINT32_MAX),
        .zero_sample_frame = value.zero_sample_frame, .zero_host_time = value.zero_host_time
    };
    atomic_fetch_sub(&session->rt_readers, 1);
    return true;
}
