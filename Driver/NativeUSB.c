#include "NativeUSB.h"
#include "NativeAudioISO.h"
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
    rk3_native_audio_iso *audio;
} RetiredAudio;

struct rk3_usb_session {
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    atomic_uint references;
    atomic_bool closing;
    atomic_uint rt_readers;
    CFRunLoopRef run_loop;
    Request *head, *tail;
    Request *active_request; /* worker-owned reference, valid through completion */
    size_t queued;
    rk3_usb_status status;
    uint64_t generation;
    RK3NativeReport reports[REPORT_HISTORY];
    size_t history_count, history_head;
    uint64_t sequence;
    IOUSBDeviceInterface300 **device;
    IOUSBInterfaceInterface300 **interface;
    CFRunLoopSourceRef source;
    bool opened, changed_alt, pending, stopping_io, transport_failed;
    bool pending_write;
    IOReturn write_result;
    size_t write_length;
    bool info_received;
    bool digital_known;
    uint16_t digital_snapshot;
    int ack_result;
    UInt8 original_alt, command_in, command_out, iso_in, iso_out;
    uint8_t read_buffer[64];
    uint8_t write_buffer[64];
    _Atomic(rk3_native_audio_iso *) audio;
    atomic_uint_fast64_t clock_generation;
    bool want_audio;
    RetiredAudio *retired;
    size_t retired_count;
};

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

static void release_session(rk3_usb_session *session) {
    if (atomic_fetch_sub_explicit(&session->references, 1, memory_order_acq_rel) != 1) return;
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
    rk3_native_audio_iso *current = atomic_load(&session->audio);
    RetiredAudio **link = &session->retired;
    while (*link != NULL) {
        RetiredAudio *retired = *link;
        if (retired->audio != current && rk3_native_audio_iso_is_quiescent(retired->audio) &&
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
    if (atomic_load(&session->closing) || session->queued == COMMAND_QUEUE) {
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

static IOUSBInterfaceInterface300 **find_interface(IOUSBDeviceInterface300 **device) {
    IOUSBFindInterfaceRequest request = { kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
                                        kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
    io_iterator_t iterator = IO_OBJECT_NULL;
    if ((*device)->CreateInterfaceIterator(device, &request, &iterator) != kIOReturnSuccess) return NULL;
    IOUSBInterfaceInterface300 **selected = NULL;
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        IOCFPlugInInterface **plugin = NULL;
        IOUSBInterfaceInterface300 **interface = NULL;
        SInt32 score = 0;
        IOReturn result = IOCreatePlugInInterfaceForService(service, kIOUSBInterfaceUserClientTypeID,
            kIOCFPlugInInterfaceID, &plugin, &score);
        IOObjectRelease(service);
        if (result != kIOReturnSuccess || plugin == NULL) continue;
        HRESULT status = (*plugin)->QueryInterface(plugin,
            CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID *)&interface);
        (*plugin)->Release(plugin);
        if (status != S_OK || interface == NULL) continue;
        UInt8 number = 255;
        if ((*interface)->GetInterfaceNumber(interface, &number) == kIOReturnSuccess && number == 0 && selected == NULL)
            selected = interface;
        else (*interface)->Release(interface);
    }
    IOObjectRelease(iterator);
    return selected;
}

static bool arm_reader(rk3_usb_session *session);

static void report_complete(void *context, IOReturn result, void *argument) {
    rk3_usb_session *session = context;
    session->pending = false;
    if (session->stopping_io) return;
    uintptr_t length = (uintptr_t)argument;
    if (result != kIOReturnSuccess || length > sizeof(session->read_buffer)) {
        session->transport_failed = true;
        set_error(session, result != kIOReturnSuccess ? result : EOVERFLOW, "EP1 receive failed");
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
    if (!atomic_load(&session->closing) && !arm_reader(session)) session->transport_failed = true;
}

static bool arm_reader(rk3_usb_session *session) {
    if (session->pending || session->stopping_io) return false;
    session->pending = true;
    IOReturn result = (*session->interface)->ReadPipeAsync(session->interface, session->command_in,
        session->read_buffer, sizeof(session->read_buffer), report_complete, session);
    if (result != kIOReturnSuccess) {
        session->pending = false;
        set_error(session, result, "Cannot arm persistent EP1 reader");
        return false;
    }
    return true;
}

static void command_write_complete(void *context, IOReturn result, void *argument) {
    rk3_usb_session *session = context;
    if (!session->pending_write) {
        session->transport_failed = true;
        set_error(session, EPROTO, "Unexpected command write completion"); return;
    }
    session->write_result = result;
    if (result == kIOReturnSuccess && (uintptr_t)argument != session->write_length)
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
        session->pending_write || length == 0 || length > 64 || atomic_load(&session->closing)) return false;
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
    IOReturn result = (*session->interface)->WritePipeAsyncTO(session->interface, session->command_out,
        session->write_buffer, (UInt32)length, write_timeout, write_timeout, command_write_complete, session);
    if (result != kIOReturnSuccess) {
        session->pending_write = false;
        session->transport_failed = true;
        set_error(session, result, "Cannot submit asynchronous EP1 command write"); return false;
    }
    while (session->pending_write) {
        if (active_request_cancelled(session) || session->transport_failed || seconds_now() >= write_deadline) {
            session->transport_failed = true;
            set_error(session, ETIMEDOUT, "Asynchronous EP1 command write timed out or was cancelled");
            (void)(*session->interface)->AbortPipe(session->interface, session->command_out);
            return false; /* Session-owned buffer remains valid until its callback drains. */
        }
        /* Firmware may wait for another command read before accepting OUT.
         * Pump this same run loop so persistent EP1-IN can complete/rearm. */
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
    }
    if (session->write_result != kIOReturnSuccess) {
        session->transport_failed = true;
        set_error(session, session->write_result, "Asynchronous EP1 command write failed"); return false;
    }
    if (active_request_cancelled(session) || seconds_now() >= write_deadline) {
        session->transport_failed = true;
        set_error(session, ETIMEDOUT, "EP1 write completion exceeded its request deadline"); return false;
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
            set_error(session, ETIMEDOUT, "Device command reply timed out"); return false;
        }
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
    }
    /* NI performAudioEngineStart ignores setAudioParams' stop-sentinel
     * result, then requires success for the real format. Hardware returns
     * ACK9=0 for the rate-ff stop. Still require an actual bounded reply. */
    bool stop_sentinel = length == RK3_AUDIO_PARAMS_BYTES &&
        bytes[0] == RK3_COMMAND_AUDIO_PARAMS && bytes[1] == 255;
    if (bytes[0] == RK3_COMMAND_AUDIO_PARAMS && !stop_sentinel && session->ack_result != 1) {
        set_error(session, EPROTO, "Device rejected audio parameters"); return false;
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
    return command_worker(session, audio_initialization, sizeof(audio_initialization), COMMAND_TIMEOUT) &&
        command_worker(session, controller_configuration, sizeof(controller_configuration), COMMAND_TIMEOUT) &&
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
        if (cancelled || atomic_load(&session->closing) || session->transport_failed ||
            error != 0 || !rk3_native_audio_iso_running(audio) || seconds_now() >= deadline) {
            set_error(session, error != 0 ? error : ETIMEDOUT, "No valid completed-device audio clock before startup deadline");
            return false;
        }
        rk3_native_audio_clock clock;
        if (rk3_native_audio_iso_clock(audio, &clock)) return true;
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
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
    IOReturn result = (*session->interface)->ClearPipeStallBothEnds(session->interface, session->command_in);
    if (result != kIOReturnSuccess) { set_error(session, result, "Cannot reset command input pipe"); return false; }
    result = (*session->interface)->ClearPipeStallBothEnds(session->interface, session->command_out);
    if (result != kIOReturnSuccess) { set_error(session, result, "Cannot reset command output pipe"); return false; }
    return true;
}

static bool start_command_io(rk3_usb_session *session) {
    if (!reset_command_pipes(session)) return false;
    IOReturn result = (*session->interface)->CreateInterfaceAsyncEventSource(session->interface, &session->source);
    if (result != kIOReturnSuccess || session->source == NULL) { set_error(session, result, "Cannot create USB callback source"); return false; }
    CFRunLoopAddSource(session->run_loop, session->source, kCFRunLoopDefaultMode);
    session->stopping_io = false;
    session->transport_failed = false;
    if (!arm_reader(session)) return false;
    uint8_t command = RK3_COMMAND_INFO;
    return command_worker(session, &command, 1, REPLY_TIMEOUT_MS);
}

static bool connect_device(rk3_usb_session *session) {
    session->device = find_device();
    if (session->device == NULL) { set_error(session, ENODEV, "No unambiguous accessible RK3 device"); return false; }
    if (!configure_device(session)) return false;
    session->interface = find_interface(session->device);
    if (session->interface == NULL) { set_error(session, ENODEV, "Interface0 is unavailable"); return false; }
    IOReturn result = (*session->interface)->GetAlternateSetting(session->interface, &session->original_alt);
    if (result != kIOReturnSuccess) { set_error(session, result, "Cannot read alternate setting"); return false; }
    result = (*session->interface)->USBInterfaceOpen(session->interface);
    if (result != kIOReturnSuccess) { set_error(session, result, "Interface0 is in use or unavailable"); return false; }
    session->opened = true;
    if (session->original_alt != 1) {
        result = (*session->interface)->SetAlternateInterface(session->interface, 1);
        if (result != kIOReturnSuccess) { set_error(session, result, "Cannot select alternate1"); return false; }
        session->changed_alt = true;
    }
    UInt8 count = 0;
    result = (*session->interface)->GetNumEndpoints(session->interface, &count);
    if (result != kIOReturnSuccess) { set_error(session, result, "Cannot enumerate USB endpoints"); return false; }
    for (unsigned int pipe = 1; pipe <= count; ++pipe) {
        UInt8 direction = 0, number = 0, type = 0, interval = 0;
        UInt16 maximum = 0;
        result = (*session->interface)->GetPipeProperties(session->interface, (UInt8)pipe,
            &direction, &number, &type, &maximum, &interval);
        if (result != kIOReturnSuccess) { set_error(session, result, "Cannot inspect USB pipe"); return false; }
        if (number == 1 && type == kUSBBulk && direction == kUSBIn) session->command_in = (UInt8)pipe;
        if (number == 1 && type == kUSBBulk && direction == kUSBOut) session->command_out = (UInt8)pipe;
        if (number == 2 && type == kUSBIsoc && direction == kUSBIn) session->iso_in = (UInt8)pipe;
        if (number == 6 && type == kUSBIsoc && direction == kUSBOut) session->iso_out = (UInt8)pipe;
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
    retired->next = session->retired;
    session->retired = retired;
    ++session->retired_count;
    pthread_mutex_lock(&session->mutex);
    ++session->generation;
    atomic_store(&session->clock_generation, session->generation);
    session->status.state = RK3_USB_READY;
    session->status.error_code = 0;
    session->status.error[0] = '\0';
    pthread_mutex_unlock(&session->mutex);
    /* Old engines are unpublished before reconnect. Publish the new engine
     * only after its generation so clock readers cannot assign an old lease
     * to a newly published engine. Readers also recheck pointer+generation. */
    atomic_store(&session->audio, audio);
    if (session->want_audio) {
        pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_STARTING; pthread_mutex_unlock(&session->mutex);
        int32_t started = rk3_native_audio_iso_start(audio);
        if (started != 0 || !wait_audio_clock(session, audio)) {
            rk3_native_audio_iso_stop(audio);
            set_error(session, started != 0 ? started : ETIMEDOUT, "Audio restart after reconnect has no valid clock"); return false;
        }
        pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_RUNNING; pthread_mutex_unlock(&session->mutex);
    }
    return true;
}

static bool io_pending(rk3_usb_session *session, rk3_native_audio_iso *audio) {
    return session->pending || session->pending_write || (audio != NULL && !rk3_native_audio_iso_is_quiescent(audio));
}

static void disconnect_device(rk3_usb_session *session) {
    rk3_native_audio_iso *audio = atomic_exchange(&session->audio, NULL);
    session->stopping_io = true;
    if (audio != NULL) rk3_native_audio_iso_stop(audio);
    if (session->pending && session->opened) (void)(*session->interface)->AbortPipe(session->interface, session->command_in);
    if (session->pending_write && session->opened) (void)(*session->interface)->AbortPipe(session->interface, session->command_out);
    double deadline = seconds_now() + (double)DRAIN_MS / 1000.0;
    while (io_pending(session, audio) && seconds_now() < deadline)
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
    if (io_pending(session, audio) && session->opened) {
        IOReturn result = (*session->interface)->USBInterfaceClose(session->interface);
        if (result == kIOReturnSuccess) session->opened = false;
        else set_error(session, result, "USB close during cancellation failed");
    }
    /* Cancellation retains the worker/session/audio buffers indefinitely if
     * necessary. Never free outstanding kernel I/O or exit coreaudiod. */
    while (io_pending(session, audio)) {
        set_error(session, ETIMEDOUT, "Waiting for USB cancellation callbacks; storage retained");
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    }
    if (session->interface != NULL && (session->changed_alt || audio != NULL) && !session->opened) {
        IOReturn result = (*session->interface)->USBInterfaceOpen(session->interface);
        if (result == kIOReturnSuccess) session->opened = true;
        else set_error(session, result, "Cannot reopen to restore original alternate");
    }
    /* Detach pinned USB buffers while the borrowed interface is still open.
     * Retired CPU queues remain alive for a racing real-time pointer load. */
    if (audio != NULL && !rk3_native_audio_iso_release_usb(audio))
        set_error(session, EBUSY, "Cannot detach native audio USB buffers");
    if (session->interface != NULL && session->opened && session->changed_alt) {
        IOReturn result = (*session->interface)->SetAlternateInterface(session->interface, session->original_alt);
        if (result != kIOReturnSuccess) set_error(session, result, "Cannot restore original USB alternate");
    }
    if (session->interface != NULL && session->opened) {
        IOReturn result = (*session->interface)->USBInterfaceClose(session->interface);
        if (result != kIOReturnSuccess) set_error(session, result, "USB interface close failed");
    }
    if (session->source != NULL) {
        CFRunLoopRemoveSource(session->run_loop, session->source, kCFRunLoopDefaultMode);
        CFRelease(session->source);
    }
    if (session->interface != NULL) (*session->interface)->Release(session->interface);
    if (session->device != NULL) (*session->device)->Release(session->device);
    session->source = NULL; session->interface = NULL; session->device = NULL;
    session->opened = session->changed_alt = session->pending = false;
    session->pending_write = false;
    session->command_in = session->command_out = session->iso_in = session->iso_out = 0;
    reclaim_retired(session);
}

static void *worker_main(void *context) {
    rk3_usb_session *session = context;
    pthread_mutex_lock(&session->mutex);
    session->run_loop = CFRunLoopGetCurrent(); CFRetain(session->run_loop);
    pthread_mutex_unlock(&session->mutex);
    bool connected = false;
    double next_connect = 0;
    while (!atomic_load(&session->closing)) {
        if (!connected && seconds_now() >= next_connect) {
            pthread_mutex_lock(&session->mutex);
            session->history_count = session->history_head = 0;
            session->digital_known = false;
            session->digital_snapshot = 0;
            pthread_mutex_unlock(&session->mutex);
            connected = connect_device(session);
            if (!connected) disconnect_device(session);
            next_connect = seconds_now() + (double)CONNECT_RETRY_MS / 1000.0;
        }
        Request *request = NULL;
        pthread_mutex_lock(&session->mutex);
        if (session->head != NULL) {
            request = session->head; session->head = request->next;
            if (session->head == NULL) session->tail = NULL;
            --session->queued;
        }
        bool cancelled = request != NULL && request->cancelled;
        pthread_mutex_unlock(&session->mutex);
        if (request != NULL) {
            session->active_request = request;
            int32_t result = cancelled ? ETIMEDOUT : connected ? 0 : ENODEV;
            rk3_native_audio_iso *audio = atomic_load_explicit(&session->audio, memory_order_acquire);
            if (request->kind == REQUEST_STOP) {
                /* A stop revokes automatic restart even when the device is absent. */
                session->want_audio = false;
                result = 0;
            }
            if (result == 0 && request->kind == REQUEST_COMMAND) {
                if (request->generation == 0 || request->generation != session->generation) result = ESTALE;
                else if (!command_worker(session, request->bytes, request->length, REPLY_TIMEOUT_MS)) result = EIO;
            } else if (result == 0 && request->kind == REQUEST_START) {
                pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_STARTING; pthread_mutex_unlock(&session->mutex);
                result = audio == NULL ? ENODEV : rk3_native_audio_iso_running(audio) ? 0 : rk3_native_audio_iso_start(audio);
                if (result == 0 && !wait_audio_clock(session, audio)) result = ETIMEDOUT;
                pthread_mutex_lock(&session->mutex);
                bool expired = request->cancelled || seconds_now() >= request->deadline;
                pthread_mutex_unlock(&session->mutex);
                if (expired) {
                    if (audio != NULL) rk3_native_audio_iso_stop(audio);
                    session->want_audio = false;
                    session->transport_failed = true;
                    result = ETIMEDOUT;
                }
                if (result != 0) {
                    if (audio != NULL) rk3_native_audio_iso_stop(audio);
                    session->transport_failed = true;
                }
                if (result == 0) session->want_audio = true;
                pthread_mutex_lock(&session->mutex);
                session->status.state = result == 0 ? RK3_USB_RUNNING : RK3_USB_FAILED;
                if (result == 0) { session->status.error_code = 0; session->status.error[0] = '\0'; }
                else { session->status.error_code = result; snprintf(session->status.error, sizeof(session->status.error), "Native ISO audio startup failed"); }
                pthread_mutex_unlock(&session->mutex);
            } else if (result == 0 && request->kind == REQUEST_STOP) {
                session->want_audio = false;
                if (connected) { pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_STOPPING; pthread_mutex_unlock(&session->mutex); }
                if (audio != NULL) rk3_native_audio_iso_stop(audio);
                double deadline = seconds_now() + (double)DRAIN_MS / 1000.0;
                while (audio != NULL && !rk3_native_audio_iso_is_quiescent(audio) && seconds_now() < deadline)
                    (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
                if (audio != NULL && !rk3_native_audio_iso_is_quiescent(audio)) {
                    result = ETIMEDOUT; session->transport_failed = true;
                    set_error(session, result, "ISO audio stop did not drain");
                } else if (connected) {
                    pthread_mutex_lock(&session->mutex); session->status.state = RK3_USB_READY; pthread_mutex_unlock(&session->mutex);
                }
            }
            session->active_request = NULL;
            pthread_mutex_lock(&session->mutex); complete_locked(request, result); pthread_mutex_unlock(&session->mutex);
        }
        rk3_native_audio_iso *current_audio = atomic_load(&session->audio);
        if (connected && current_audio != NULL && rk3_native_audio_iso_error(current_audio) != 0) {
            set_error(session, rk3_native_audio_iso_error(current_audio), "Asynchronous ISO audio failure");
            session->transport_failed = true;
        }
        if (connected && session->transport_failed) { disconnect_device(session); connected = false; next_connect = seconds_now() + 1; }
        reclaim_retired(session);
        if (connected) (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
        else {
            struct timespec relative = { 0, 100000000 };
            pthread_mutex_lock(&session->mutex);
            if (!atomic_load(&session->closing) && session->head == NULL)
                (void)pthread_cond_timedwait_relative_np(&session->wake, &session->mutex, &relative);
            pthread_mutex_unlock(&session->mutex);
        }
    }
    disconnect_device(session);
    pthread_mutex_lock(&session->mutex);
    while (session->head != NULL) {
        Request *request = session->head; session->head = request->next;
        complete_locked(request, ECANCELED);
    }
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
    atomic_store(&session->closing, true); wake_worker_locked(session);
    pthread_mutex_unlock(&session->mutex);
    release_session(session);
}

bool rk3_usb_close_wait(rk3_usb_session *session, uint32_t timeout_ms) {
    if (session == NULL) return true;
    if (timeout_ms > 30000) timeout_ms = 30000;
    double deadline = seconds_now() + (double)timeout_ms / 1000.0;
    pthread_mutex_lock(&session->mutex);
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

int32_t rk3_usb_audio_start(rk3_usb_session *session, uint32_t timeout_ms) { return submit(session, REQUEST_START, NULL, 0, 0, timeout_ms); }
int32_t rk3_usb_audio_stop(rk3_usb_session *session, uint32_t timeout_ms) { return submit(session, REQUEST_STOP, NULL, 0, 0, timeout_ms); }

size_t rk3_usb_render(rk3_usb_session *session, const float *interleaved, size_t frames) {
    if (session == NULL || interleaved == NULL) return 0;
    atomic_fetch_add(&session->rt_readers, 1);
    rk3_native_audio_iso *audio = atomic_load(&session->audio);
    size_t accepted = audio != NULL ? rk3_native_audio_iso_render(audio, interleaved, frames) : 0;
    atomic_fetch_sub(&session->rt_readers, 1);
    return accepted;
}

size_t rk3_usb_capture(rk3_usb_session *session, float *interleaved, size_t frames) {
    if (interleaved == NULL || frames > SIZE_MAX / (2 * sizeof(float))) return 0;
    if (session != NULL) atomic_fetch_add(&session->rt_readers, 1);
    rk3_native_audio_iso *audio = session != NULL ? atomic_load(&session->audio) : NULL;
    size_t captured = audio != NULL ? rk3_native_audio_iso_capture(audio, interleaved, frames) : 0;
    if (session != NULL) atomic_fetch_sub(&session->rt_readers, 1);
    if (captured < frames) memset(interleaved + 2 * captured, 0, (frames - captured) * 2 * sizeof(float));
    return captured;
}

bool rk3_usb_clock(rk3_usb_session *session, rk3_usb_clock_snapshot *clock) {
    if (session == NULL || clock == NULL) return false;
    atomic_fetch_add(&session->rt_readers, 1);
    uint64_t generation = atomic_load(&session->clock_generation);
    rk3_native_audio_iso *audio = atomic_load(&session->audio);
    rk3_native_audio_clock value;
    if (audio == NULL || !rk3_native_audio_iso_clock(audio, &value) ||
        audio != atomic_load(&session->audio) || generation != atomic_load(&session->clock_generation)) {
        atomic_fetch_sub(&session->rt_readers, 1); return false;
    }
    *clock = (rk3_usb_clock_snapshot){ value.sample_frame, value.host_time,
        (generation << 32) | (value.seed & UINT32_MAX) };
    atomic_fetch_sub(&session->rt_readers, 1);
    return true;
}
