/* New ARM64 CoreMIDI driver. Interface behavior comes from the SDK and the
 * original NI binary analysis recorded in ReverseEngineering/MIDIDriver.md.
 * The native HAL owns USB; this bundle has no legacy-driver/app dependency. */
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIDriver.h>
#include <CoreMIDI/MIDIServices.h>
#include <CoreMIDI/MIDISetup.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "MIDIEvent.h"
#include "MIDIStream.h"
#include "NativeControl.h"
#include "Controller.h"

enum { OUTPUT_SLOTS = 256, OUTPUT_BYTES = OUTPUT_SLOTS * 8,
       MAX_SEND_PACKETS = 256, MAX_AUDIO_DEVICES = 4096 };

_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2 &&
               ATOMIC_BOOL_LOCK_FREE == 2,"CoreMIDI Send requires lock-free atomics");

static const CFTimeInterval DISCOVERY_INTERVAL = 0.5;
static const CFTimeInterval INPUT_INTERVAL = 0.005;

/* A new factory identity; never impersonates the original NI plug-in. */
static CFUUIDRef factory_id(void) {
    return CFUUIDGetConstantUUIDWithBytes(NULL,0xb4,0x3a,0x1d,0x48,0x91,0x39,0x48,0xb3,
                                         0x83,0x57,0x61,0x43,0x80,0x7b,0xb4,0x10);
}

static CFStringRef identity_key(void) { return CFSTR(RK3_CONTROLLER_IDENTITY_KEY); }
static CFStringRef controller_identity(void) { return CFSTR(RK3_NATIVE_DEVICE_UID ".controller"); }

typedef struct {
    AudioDeviceID audio_device;
    uint64_t generation;
    uint64_t lifecycle_epoch;
    RK3NativeCommand command;
    uint32_t kind; /* 0: USB command; 1: controller feedback bytes */
} OutputEntry;

typedef struct {
    MIDIDriverInterface *interface; /* MIDIDriverRef points at this first member. */
    atomic_uint references;
    pthread_mutex_t state_lock;
    CFRunLoopRef main_loop;
    CFRunLoopRef io_loop;
    CFRunLoopTimerRef discovery_timer;
    CFRunLoopTimerRef input_timer;
    bool started;                  /* state_lock, except server-main setup */
    MIDIDeviceRef midi_device;      /* setup/property calls: server main only */
    MIDIEndpointRef source;         /* state_lock protects I/O readers */
    MIDIEndpointRef destination;
    MIDIDeviceRef controller_device;
    MIDIEndpointRef controller_source, controller_destination;
    atomic_bool controller_enabled;
    atomic_uint_fast64_t controller_listener_epoch;
    uint64_t din_min_sequence, controller_min_sequence;
    uint32_t controller_enable_snapshot;
    uint64_t controller_enable_sequence, controller_enable_epoch;
    RK3ControllerState controller; /* I/O run loop only */
    RK3MIDIStream feedback_parser;
    uint64_t controller_generation, controller_epoch, controller_after_sequence;
    bool controller_force_leds;
    atomic_uint_fast64_t calibration;
    uint64_t applied_calibration;
    OutputEntry feedback[OUTPUT_SLOTS];
    atomic_uint_fast64_t feedback_read, feedback_write;
    atomic_flag feedback_consumer;
    atomic_bool source_enabled;
    bool published_online;         /* server main only */
    RK3MIDIStream parser;          /* driver I/O run loop only */
    atomic_bool parser_reset;
    bool baseline_pending;         /* never replay pre-listener history */
    uint64_t cursor;
    OSStatus last_setup_error;      /* suppress repeating setup diagnostics */
    atomic_bool online;
    atomic_uint audio_device;
    atomic_uint_fast64_t generation;
    atomic_uint_fast64_t lifecycle_epoch;
    atomic_uint_fast64_t listener_epoch;
    atomic_uint failed_device;
    atomic_uint_fast64_t failed_generation;
    atomic_flag output_producer;
    atomic_uint_fast64_t output_read;
    atomic_uint_fast64_t output_write;
    OutputEntry output[OUTPUT_SLOTS];
    atomic_bool worker_stop;
    pthread_t worker;
    bool worker_running;           /* server main only */
} Driver;

static MIDIDriverInterface interface_table;
static OSStatus stop_driver(MIDIDriverRef self);

static Driver *driver_from_ref(MIDIDriverRef self) { return (Driver *)self; }
static MIDIDriverRef driver_ref(Driver *driver) { return (MIDIDriverRef)&driver->interface; }

static ULONG add_ref(void *self) {
    Driver *driver = self;
    return (ULONG)(atomic_fetch_add_explicit(&driver->references,1,memory_order_relaxed)+1);
}

static ULONG release_ref(void *self) {
    Driver *driver = self;
    unsigned previous = atomic_fetch_sub_explicit(&driver->references,1,memory_order_acq_rel);
    if (previous != 1) return (ULONG)(previous-1);
    /* Timers and the worker retain the object. CoreMIDI calls Stop before its
     * final Release, so no callback/worker can still reference this allocation. */
    if (driver->io_loop != NULL) CFRelease(driver->io_loop);
    CFRelease(driver->main_loop);
    pthread_mutex_destroy(&driver->state_lock);
    CFPlugInRemoveInstanceForFactory(factory_id());
    free(driver);
    return 0;
}

static HRESULT query_interface(void *self, REFIID iid, LPVOID *output) {
    if (output == NULL) return E_POINTER;
    *output = NULL;
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(NULL,iid);
    if (requested == NULL) return E_OUTOFMEMORY;
    bool supported = CFEqual(requested,kMIDIDriverInterface2ID) || CFEqual(requested,IUnknownUUID);
    CFRelease(requested);
    if (!supported) return E_NOINTERFACE;
    add_ref(self);
    *output = self;
    return S_OK;
}

static AudioObjectPropertyAddress address(AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress result = {selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    return result;
}

static bool read_info(AudioDeviceID device, RK3NativeInfo *info) {
    AudioObjectPropertyAddress property = address(RK3_PROPERTY_INFO);
    UInt32 size = sizeof(*info);
    memset(info,0,sizeof(*info));
    return AudioObjectGetPropertyData(device,&property,0,NULL,&size,info) == noErr &&
        size == sizeof(*info) && info->version == RK3_CONTROL_VERSION &&
        memchr(info->device_uid,0,sizeof(info->device_uid)) != NULL &&
        memchr(info->product_name,0,sizeof(info->product_name)) != NULL &&
        strcmp(info->device_uid,RK3_NATIVE_DEVICE_UID) == 0;
}

/* Only discovery runs here. CoreMIDI setup calls never run on the I/O worker. */
static AudioDeviceID discover_audio(RK3NativeInfo *info) {
    AudioObjectPropertyAddress property = address(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&property,0,NULL,&size) != noErr ||
        size == 0 || size % sizeof(AudioDeviceID) != 0 ||
        size > MAX_AUDIO_DEVICES * sizeof(AudioDeviceID)) return kAudioObjectUnknown;
    AudioDeviceID *devices = malloc(size);
    if (devices == NULL) return kAudioObjectUnknown;
    UInt32 capacity = size;
    AudioDeviceID found = kAudioObjectUnknown;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject,&property,0,NULL,&size,devices) == noErr &&
        size <= capacity && size % sizeof(AudioDeviceID) == 0) {
        for (size_t index = 0; index < size / sizeof(*devices); ++index) {
            RK3NativeInfo candidate;
            AudioObjectPropertyAddress info_property = address(RK3_PROPERTY_INFO);
            if (!AudioObjectHasProperty(devices[index],&info_property) || !read_info(devices[index],&candidate)) continue;
            if (found != kAudioObjectUnknown) { found = kAudioObjectUnknown; break; }
            found = devices[index];
            *info = candidate;
        }
    }
    free(devices);
    return found;
}

static SInt32 stable_id(const char *uid, const char *role) {
    uint32_t hash = UINT32_C(2166136261);
    const char *parts[] = {"org.rigkontrol3.ni-arm.midi:",uid,":",role};
    for (size_t part = 0; part < sizeof(parts)/sizeof(parts[0]); ++part)
        for (const unsigned char *p = (const unsigned char *)parts[part]; *p != 0; ++p)
            hash = (hash ^ *p) * UINT32_C(16777619);
    return (SInt32)(hash == 0 ? 1 : hash);
}

static OSStatus bind_endpoints(Driver *driver, MIDIDeviceRef device) {
    if (MIDIDeviceGetNumberOfEntities(device) != 2) return kMIDISetupFormatErr;
    MIDIEntityRef input = MIDIDeviceGetEntity(device,0), output = MIDIDeviceGetEntity(device,1);
    if (input == 0 || output == 0 || MIDIEntityGetNumberOfSources(input) != 1 ||
        MIDIEntityGetNumberOfDestinations(input) != 0 || MIDIEntityGetNumberOfSources(output) != 0 ||
        MIDIEntityGetNumberOfDestinations(output) != 1) return kMIDISetupFormatErr;
    MIDIEndpointRef source = MIDIEntityGetSource(input,0), destination = MIDIEntityGetDestination(output,0);
    if (source == 0 || destination == 0) return kMIDISetupFormatErr;
    OSStatus result = MIDIEndpointSetRefCons(source,NULL,driver);
    if (result == noErr) {
        result = MIDIEndpointSetRefCons(destination,NULL,driver);
        if (result != noErr) (void)MIDIEndpointSetRefCons(source,NULL,NULL);
    }
    if (result != noErr) return result;
    pthread_mutex_lock(&driver->state_lock);
    driver->midi_device = device;
    driver->source = source;
    driver->destination = destination;
    pthread_mutex_unlock(&driver->state_lock);
    return noErr;
}

static bool device_matches(MIDIDeviceRef device) {
    CFStringRef identity = NULL;
    bool matches = MIDIObjectGetStringProperty(device,identity_key(),&identity) == noErr &&
                   identity != NULL && CFEqual(identity,CFSTR(RK3_NATIVE_DEVICE_UID));
    if (identity != NULL) CFRelease(identity);
    return matches;
}

static OSStatus ensure_device(Driver *driver, const RK3NativeInfo *info) {
    if (driver->midi_device != 0) return noErr;
    MIDIDeviceListRef list = MIDIGetDriverDeviceList(driver_ref(driver));
    if (list == 0) return kMIDINoCurrentSetup;
    OSStatus result = noErr;
    MIDIDeviceRef device = 0;
    for (ItemCount index = 0; index < MIDIDeviceListGetNumberOfDevices(list); ++index) {
        MIDIDeviceRef candidate = MIDIDeviceListGetDevice(list,index);
        if (device_matches(candidate)) { device = candidate; break; }
    }
    if (device != 0) {
        result = bind_endpoints(driver,device);
        MIDIDeviceListDispose(list);
        return result;
    }
    CFStringRef name = CFStringCreateWithCString(NULL,info->product_name,kCFStringEncodingUTF8);
    if (name == NULL || CFStringGetLength(name) == 0) {
        if (name != NULL) CFRelease(name);
        name = CFRetain(CFSTR("Rig Kontrol 3 Native"));
    }
    result = MIDIDeviceCreate(driver_ref(driver),name,CFSTR("Native Instruments"),CFSTR("Rig Kontrol 3"),&device);
    CFRelease(name);
    if (result != noErr) { MIDIDeviceListDispose(list); return result; }
    MIDIEntityRef input = 0, output = 0;
    result = MIDIDeviceAddEntity(device,CFSTR("DIN Input"),false,1,0,&input);
    if (result == noErr) result = MIDIDeviceAddEntity(device,CFSTR("DIN Output"),false,0,1,&output);
    if (result == noErr) result = MIDIObjectSetStringProperty(device,identity_key(),CFSTR(RK3_NATIVE_DEVICE_UID));
    if (result == noErr) result = MIDIObjectSetIntegerProperty(device,kMIDIPropertyUniqueID,stable_id(info->device_uid,"device"));
    if (result == noErr) result = MIDIObjectSetIntegerProperty(MIDIEntityGetSource(input,0),kMIDIPropertyUniqueID,stable_id(info->device_uid,"din-input"));
    if (result == noErr) result = MIDIObjectSetIntegerProperty(MIDIEntityGetDestination(output,0),kMIDIPropertyUniqueID,stable_id(info->device_uid,"din-output"));
    if (result == noErr) result = bind_endpoints(driver,device);
    if (result == noErr) result = MIDISetupAddDevice(device);
    if (result == noErr) {
        (void)MIDIDeviceListAddDevice(list,device);
    } else {
        pthread_mutex_lock(&driver->state_lock);
        driver->midi_device = 0;
        driver->source = 0;
        driver->destination = 0;
        pthread_mutex_unlock(&driver->state_lock);
        (void)MIDIDeviceDispose(device); /* AddDevice failed or was never attempted. */
    }
    MIDIDeviceListDispose(list);
    return result;
}


static bool controller_matches(MIDIDeviceRef device) {
    CFStringRef identity = NULL;
    bool matches = MIDIObjectGetStringProperty(device,identity_key(),&identity) == noErr &&
                   identity != NULL && CFEqual(identity,controller_identity());
    if (identity != NULL) CFRelease(identity);
    return matches;
}

static OSStatus bind_controller(Driver *driver,MIDIDeviceRef device) {
    if (MIDIDeviceGetNumberOfEntities(device) != 1) return kMIDISetupFormatErr;
    MIDIEntityRef entity = MIDIDeviceGetEntity(device,0);
    if (MIDIEntityGetNumberOfSources(entity) != 1 || MIDIEntityGetNumberOfDestinations(entity) != 1) return kMIDISetupFormatErr;
    MIDIEndpointRef source = MIDIEntityGetSource(entity,0), destination = MIDIEntityGetDestination(entity,0);
    OSStatus result = MIDIEndpointSetRefCons(source,NULL,driver);
    if (result == noErr) result = MIDIEndpointSetRefCons(destination,(void *)(uintptr_t)1,driver);
    if (result != noErr) { (void)MIDIEndpointSetRefCons(source,NULL,NULL); return result; }
    pthread_mutex_lock(&driver->state_lock);
    driver->controller_device = device; driver->controller_source = source; driver->controller_destination = destination;
    pthread_mutex_unlock(&driver->state_lock);
    return noErr;
}

static OSStatus ensure_controller(Driver *driver) {
    if (driver->controller_device != 0) return noErr;
    MIDIDeviceListRef list = MIDIGetDriverDeviceList(driver_ref(driver));
    if (list == 0) return kMIDINoCurrentSetup;
    for (ItemCount index=0; index<MIDIDeviceListGetNumberOfDevices(list); ++index) {
        MIDIDeviceRef device = MIDIDeviceListGetDevice(list,index);
        if (controller_matches(device)) {
            OSStatus result = bind_controller(driver,device); MIDIDeviceListDispose(list); return result;
        }
    }
    MIDIDeviceRef device = 0; MIDIEntityRef entity = 0;
    OSStatus result = MIDIDeviceCreate(driver_ref(driver),CFSTR("Rig Kontrol 3 Controller"),
        CFSTR("Native Instruments"),CFSTR("Native Controller Mapping"),&device);
    if (result == noErr) result = MIDIDeviceAddEntity(device,CFSTR("Controls"),false,1,1,&entity);
    if (result == noErr) result = MIDIObjectSetStringProperty(device,identity_key(),controller_identity());
    if (result == noErr) result = MIDIObjectSetIntegerProperty(device,kMIDIPropertyUniqueID,stable_id(RK3_NATIVE_DEVICE_UID,"controller-device"));
    if (result == noErr) result = MIDIObjectSetIntegerProperty(MIDIEntityGetSource(entity,0),kMIDIPropertyUniqueID,stable_id(RK3_NATIVE_DEVICE_UID,"controller-source"));
    if (result == noErr) result = MIDIObjectSetIntegerProperty(MIDIEntityGetDestination(entity,0),kMIDIPropertyUniqueID,stable_id(RK3_NATIVE_DEVICE_UID,"controller-feedback"));
    if (result == noErr) result = MIDIObjectSetStringProperty(MIDIEntityGetSource(entity,0),kMIDIPropertyName,CFSTR("Controller"));
    if (result == noErr) result = MIDIObjectSetStringProperty(MIDIEntityGetDestination(entity,0),kMIDIPropertyName,CFSTR("LED Feedback"));
    if (result == noErr) result = bind_controller(driver,device);
    if (result == noErr) result = MIDISetupAddDevice(device);
    if (result == noErr) (void)MIDIDeviceListAddDevice(list,device);
    else if (device != 0) {
        pthread_mutex_lock(&driver->state_lock);
        driver->controller_device=0;driver->controller_source=0;driver->controller_destination=0;
        pthread_mutex_unlock(&driver->state_lock);
        (void)MIDIDeviceDispose(device);
    }
    MIDIDeviceListDispose(list);return result;
}

static void poll_calibration(Driver *driver) {
    if (driver->controller_device == 0) return;
    CFDataRef data = NULL;
    OSStatus result = MIDIObjectGetDataProperty(driver->controller_device,CFSTR(RK3_CONTROLLER_CALIBRATION_KEY),&data);
    if (result == kMIDIUnknownProperty) {
        uint32_t initial[4]={1,2,(uint32_t)(atomic_load(&driver->calibration)>>32),(uint32_t)atomic_load(&driver->calibration)};
        data=CFDataCreate(NULL,(const UInt8 *)initial,sizeof(initial));
        if(data != NULL) (void)MIDIObjectSetDataProperty(driver->controller_device,CFSTR(RK3_CONTROLLER_CALIBRATION_KEY),data);
    }
    if (data != NULL) {
        if(CFDataGetLength(data)==16) {
            uint32_t values[4];memcpy(values,CFDataGetBytePtr(data),sizeof(values));
            if(values[0]==1&&values[1]==2&&values[2]<=1023&&values[3]<=1023&&values[3]>=values[2]&&values[3]-values[2]>=103)
                atomic_store_explicit(&driver->calibration,((uint64_t)values[2]<<32)|values[3],memory_order_release);
        }
        CFRelease(data);
    }
}

static void publish_offline(Driver *driver, bool offline) {
    if (driver->midi_device != 0 && driver->published_online == offline) {
        if (MIDIObjectSetIntegerProperty(driver->midi_device,kMIDIPropertyOffline,offline ? 1 : 0) == noErr)
            driver->published_online = !offline;
        if (driver->controller_device != 0) (void)MIDIObjectSetIntegerProperty(driver->controller_device,kMIDIPropertyOffline,offline ? 1 : 0);
    }
}

static void refresh_topology(Driver *driver) {
    RK3NativeInfo info;
    AudioDeviceID device = discover_audio(&info);
    bool usable = device != kAudioObjectUnknown &&
        (info.flags & RK3_NATIVE_CONNECTED) != 0 && (info.flags & RK3_NATIVE_FAILED) == 0 &&
        info.generation != 0 && info.midi_inputs == 1 && info.midi_outputs == 1;
    if (usable && atomic_load_explicit(&driver->failed_device,memory_order_acquire) == device &&
        atomic_load_explicit(&driver->failed_generation,memory_order_acquire) == info.generation) usable = false;
    if (usable) {
        OSStatus result = ensure_device(driver,&info);
        if (result == noErr) result = ensure_controller(driver);
        if (result == noErr) poll_calibration(driver);
        if (result != noErr) {
            if (driver->last_setup_error != result) fprintf(stderr,"RK3 MIDI setup failed: %d\n",(int)result);
            driver->last_setup_error = result;
            usable = false;
        } else driver->last_setup_error = noErr;
    }
    pthread_mutex_lock(&driver->state_lock);
    if (!driver->started) usable = false;
    if (usable && atomic_load_explicit(&driver->failed_device,memory_order_relaxed) == device &&
        atomic_load_explicit(&driver->failed_generation,memory_order_relaxed) == info.generation) usable = false;
    if (usable) {
        if (atomic_load_explicit(&driver->audio_device,memory_order_relaxed) != device ||
            atomic_load_explicit(&driver->generation,memory_order_relaxed) != info.generation) {
            driver->cursor = 0;
            driver->din_min_sequence=0;driver->controller_min_sequence=0;
            atomic_fetch_add_explicit(&driver->lifecycle_epoch,1,memory_order_acq_rel);
            atomic_store_explicit(&driver->parser_reset,true,memory_order_release);
            driver->baseline_pending = atomic_load(&driver->source_enabled);
        }
        atomic_store_explicit(&driver->audio_device,device,memory_order_relaxed);
        atomic_store_explicit(&driver->generation,info.generation,memory_order_release);
    }
    atomic_store_explicit(&driver->online,usable,memory_order_release);
    pthread_mutex_unlock(&driver->state_lock);
    publish_offline(driver,!usable);
}

static void discovery_tick(CFRunLoopTimerRef timer, void *context) {
    (void)timer;
    refresh_topology(context);
}

static void record_io_failure(Driver *driver, AudioDeviceID device, uint64_t generation, uint64_t epoch) {
    pthread_mutex_lock(&driver->state_lock);
    if (atomic_load_explicit(&driver->audio_device,memory_order_acquire) == device &&
        atomic_load_explicit(&driver->generation,memory_order_acquire) == generation &&
        atomic_load_explicit(&driver->lifecycle_epoch,memory_order_acquire) == epoch) {
        atomic_store_explicit(&driver->failed_device,device,memory_order_relaxed);
        atomic_store_explicit(&driver->failed_generation,generation,memory_order_release);
        atomic_store_explicit(&driver->online,false,memory_order_release);
    }
    pthread_mutex_unlock(&driver->state_lock);
}

typedef struct {
    Driver *driver;
    AudioDeviceID device;
    uint64_t generation;
    uint64_t epoch;
    uint64_t listener_epoch;
    MIDIEndpointRef source;
    bool failed;
    bool controller;
} Delivery;

static bool delivery_current(const Delivery *delivery) {
    Driver *driver = delivery->driver;
    return atomic_load_explicit(&driver->online,memory_order_acquire) &&
        atomic_load_explicit(delivery->controller ? &driver->controller_enabled : &driver->source_enabled,memory_order_acquire) &&
        atomic_load_explicit(&driver->audio_device,memory_order_acquire) == delivery->device &&
        atomic_load_explicit(&driver->generation,memory_order_acquire) == delivery->generation &&
        atomic_load_explicit(&driver->lifecycle_epoch,memory_order_acquire) == delivery->epoch &&
        atomic_load_explicit(delivery->controller ? &driver->controller_listener_epoch : &driver->listener_epoch,memory_order_acquire) == delivery->listener_epoch;
}

static bool deliver_message(void *context,uint64_t timestamp,const uint8_t *bytes,size_t length) {
    Delivery *delivery = context;
#ifdef RK3_MIDI_TESTING
    extern void rk3_test_before_delivery(void *);
    rk3_test_before_delivery(delivery->driver);
#endif
    if (!delivery_current(delivery)) return false;
    MIDIPacketList list;
    MIDIPacket *packet = MIDIPacketListInit(&list);
    packet = MIDIPacketListAdd(&list,sizeof(list),packet,timestamp,length,bytes);
    /* No driver mutex is held while calling into CoreMIDI. Timers retain the
     * object; saved endpoints survive offline transitions. */
    if (packet == NULL || MIDIReceived(delivery->source,&list) != noErr) {
        delivery->failed = true;
        return false;
    }
    return true;
}


static bool queue_entry(Driver *driver,const OutputEntry *entry) {
    if(atomic_flag_test_and_set_explicit(&driver->output_producer,memory_order_acquire)) return false;
    uint64_t head=atomic_load_explicit(&driver->output_write,memory_order_relaxed);
    uint64_t tail=atomic_load_explicit(&driver->output_read,memory_order_acquire);
    bool valid=atomic_load(&driver->online)&&atomic_load(&driver->audio_device)==entry->audio_device&&
        atomic_load(&driver->generation)==entry->generation&&atomic_load(&driver->lifecycle_epoch)==entry->lifecycle_epoch&&head-tail<OUTPUT_SLOTS;
    if(valid) {driver->output[head%OUTPUT_SLOTS]=*entry;atomic_store_explicit(&driver->output_write,head+1,memory_order_release);}
    atomic_flag_clear_explicit(&driver->output_producer,memory_order_release);return valid;
}

typedef struct { Delivery delivery; uint64_t timestamp; bool permitted; } ControllerDelivery;
static bool emit_controller(void *context,const uint8_t message[3]) {
    ControllerDelivery *delivery=context;
    /* Assignment/baseline/LED state continues to track while no listener exists. */
    if(!delivery->permitted||!delivery_current(&delivery->delivery))return true;
    return deliver_message(&delivery->delivery,delivery->timestamp,message,3);
}
static bool queue_leds(void *context,const uint8_t *bytes,size_t length) {
    Delivery *delivery=context;
    if(length!=9||bytes[0]!=5)return false;
    OutputEntry entry={.audio_device=delivery->device,.generation=delivery->generation,
        .lifecycle_epoch=delivery->epoch,.command={.version=RK3_CONTROL_VERSION,.length=9},.kind=0};
    memcpy(entry.command.bytes,bytes,length);return queue_entry(delivery->driver,&entry);
}
static bool apply_feedback(void *context,uint64_t timestamp,const uint8_t *bytes,size_t length) {
    (void)timestamp;Driver *driver=context;
    if(length==3&&bytes[0]==0xb0) (void)rk3_controller_feedback(&driver->controller,bytes[0],bytes[1],bytes[2]);
    return true;
}
static void prepare_controller(Driver *driver,const RK3NativeInfo *info,uint64_t epoch,uint64_t latest,bool enable_seed) {
    if(driver->controller_generation!=info->generation||driver->controller_epoch!=epoch) {
        rk3_controller_reset_inputs(&driver->controller);rk3_midi_stream_reset(&driver->feedback_parser);
        driver->controller_generation=info->generation;driver->controller_epoch=epoch;
        driver->controller_after_sequence=latest;driver->controller_force_leds=true;
        uint32_t snapshot=info->digital_snapshot;
        pthread_mutex_lock(&driver->state_lock);
        if(enable_seed&&driver->controller_enable_epoch==epoch&&atomic_load(&driver->controller_enabled)) {
            snapshot=driver->controller_enable_snapshot;
            driver->controller_after_sequence=driver->controller_enable_sequence;
        }
        pthread_mutex_unlock(&driver->state_lock);
        if(snapshot&UINT32_C(0x80000000)) {
            uint8_t baseline[]={4,(uint8_t)snapshot,(uint8_t)(snapshot>>8)};
            (void)rk3_controller_report(&driver->controller,baseline,sizeof(baseline),emit_controller,
                &(ControllerDelivery){.permitted=false});
        }
    }
    uint64_t calibration=atomic_load_explicit(&driver->calibration,memory_order_acquire);
    if(calibration!=driver->applied_calibration) {
        (void)rk3_controller_set_calibration(&driver->controller,2,
            (double)(calibration>>32)/1023.0,(double)(uint32_t)calibration/1023.0);
        driver->applied_calibration=calibration;
    }
}
static void drain_feedback(Driver *driver,const Delivery *delivery) {
    if(atomic_flag_test_and_set_explicit(&driver->feedback_consumer,memory_order_acquire))return;
    if(!atomic_load(&driver->online)||atomic_load(&driver->generation)!=delivery->generation||atomic_load(&driver->lifecycle_epoch)!=delivery->epoch) {
        atomic_flag_clear_explicit(&driver->feedback_consumer,memory_order_release);return;
    }
    for(unsigned count=0;count<OUTPUT_SLOTS;++count) {
        uint64_t tail=atomic_load_explicit(&driver->feedback_read,memory_order_relaxed);
        uint64_t head=atomic_load_explicit(&driver->feedback_write,memory_order_acquire);
        if(tail==head)break;
        OutputEntry entry=driver->feedback[tail%OUTPUT_SLOTS];
        atomic_store_explicit(&driver->feedback_read,tail+1,memory_order_release);
        if(entry.audio_device!=delivery->device||entry.generation!=delivery->generation||entry.lifecycle_epoch!=delivery->epoch)continue;
        (void)rk3_midi_stream_feed(&driver->feedback_parser,entry.command.bytes+3,entry.command.bytes[2],0,apply_feedback,driver);
    }
    atomic_flag_clear_explicit(&driver->feedback_consumer,memory_order_release);
}

static void input_tick(CFRunLoopTimerRef timer, void *context) {
    (void)timer;
    Driver *driver = context;
    pthread_mutex_lock(&driver->state_lock);
    bool active = driver->started && atomic_load_explicit(&driver->online,memory_order_acquire);
    AudioDeviceID device = atomic_load_explicit(&driver->audio_device,memory_order_relaxed);
    uint64_t generation = atomic_load_explicit(&driver->generation,memory_order_relaxed);
    uint64_t epoch = atomic_load_explicit(&driver->lifecycle_epoch,memory_order_relaxed);
    RK3NativeReportQuery query = {RK3_CONTROL_VERSION,0,driver->cursor};
    pthread_mutex_unlock(&driver->state_lock);
    if (!active) return;
    if (atomic_exchange_explicit(&driver->parser_reset,false,memory_order_acq_rel)) rk3_midi_stream_reset(&driver->parser);
    RK3NativeInfo info;
    if (!read_info(device,&info) || info.generation != generation ||
        (info.flags & RK3_NATIVE_CONNECTED) == 0) return;
    AudioObjectPropertyAddress property = address(RK3_PROPERTY_REPORTS);
    RK3NativeReports batch;
    UInt32 size = sizeof(batch);
    OSStatus result = AudioObjectGetPropertyData(device,&property,sizeof(query),&query,&size,&batch);
    if (result != noErr || size != sizeof(batch) || batch.version != RK3_CONTROL_VERSION ||
        batch.count > RK3_NATIVE_REPORT_LIMIT || batch.next_sequence < query.after_sequence ||
        batch.next_sequence > batch.latest_sequence) {
        record_io_failure(driver,device,generation,epoch);
        return;
    }
    uint64_t previous = query.after_sequence;
    for (uint32_t index = 0; index < batch.count; ++index) {
        const RK3NativeReport *report = &batch.reports[index];
        if (report->sequence <= previous || report->sequence > batch.next_sequence || report->length > 64) {
            record_io_failure(driver,device,generation,epoch); return;
        }
        previous = report->sequence;
    }
    pthread_mutex_lock(&driver->state_lock);
    if (!driver->started || !atomic_load_explicit(&driver->online,memory_order_acquire) ||
        atomic_load_explicit(&driver->audio_device,memory_order_relaxed) != device ||
        atomic_load_explicit(&driver->generation,memory_order_relaxed) != generation ||
        atomic_load_explicit(&driver->lifecycle_epoch,memory_order_relaxed) != epoch ||
        driver->cursor != query.after_sequence) {
        pthread_mutex_unlock(&driver->state_lock); return;
    }
    if (driver->baseline_pending) {
        driver->din_min_sequence = batch.latest_sequence;
        driver->baseline_pending = false;
        atomic_store_explicit(&driver->parser_reset,true,memory_order_release);
    }
    driver->cursor = batch.next_sequence;
    uint64_t din_min=driver->din_min_sequence,controller_min=driver->controller_min_sequence;
    Delivery delivery = {driver,device,generation,epoch,atomic_load(&driver->listener_epoch),driver->source,false,false};
    Delivery controls = {driver,device,generation,epoch,atomic_load(&driver->controller_listener_epoch),driver->controller_source,false,true};
    pthread_mutex_unlock(&driver->state_lock);
    prepare_controller(driver,&info,epoch,batch.latest_sequence,true);
    drain_feedback(driver,&controls);
    if (batch.dropped != 0) {
        rk3_midi_stream_reset(&driver->parser);rk3_controller_reset_inputs(&driver->controller);
        driver->controller_generation=0;prepare_controller(driver,&info,epoch,batch.latest_sequence,false);
    }
    for (uint32_t index = 0; index < batch.count; ++index) {
        if (!atomic_load(&driver->online)||atomic_load(&driver->generation)!=generation||atomic_load(&driver->lifecycle_epoch)!=epoch) break;
        const RK3NativeReport *report = &batch.reports[index];
        if(report->length!=0&&(report->bytes[0]==3||report->bytes[0]==4)) {
            if(report->sequence>driver->controller_after_sequence) {
                ControllerDelivery context={controls,report->host_time?report->host_time:mach_absolute_time(),report->sequence>controller_min};
                (void)rk3_controller_report(&driver->controller,report->bytes,report->length,emit_controller,&context);
                if(context.delivery.failed) record_io_failure(driver,device,generation,epoch);
            }
            continue;
        }
        if(report->sequence<=din_min||!delivery_current(&delivery))continue;
        RK3MIDIEvent event;
        RK3MIDIResult parsed = rk3_midi_parse_usb_input(report->bytes,report->length,1,&event);
        if (parsed != RK3_MIDI_OK) {
            if (report->length != 0 && report->bytes[0] == 6) rk3_midi_stream_reset(&driver->parser);
            continue;
        }
        uint64_t timestamp = report->host_time != 0 ? report->host_time : mach_absolute_time();
        if (rk3_midi_stream_feed(&driver->parser,event.bytes,event.byte_count,timestamp,deliver_message,&delivery) != RK3_MIDI_STREAM_OK) {
            if (delivery.failed) record_io_failure(driver,device,generation,epoch);
            else atomic_store(&driver->parser_reset,true);
            break;
        }
    }
    if(rk3_controller_flush_leds(&driver->controller,driver->controller_force_leds,queue_leds,&controls)==RK3_CONTROLLER_OK)
        driver->controller_force_leds=false;
}

static void *output_worker(void *context) {
    Driver *driver = context;
    while (!atomic_load_explicit(&driver->worker_stop,memory_order_acquire)) {
        uint64_t read = atomic_load_explicit(&driver->output_read,memory_order_relaxed);
        uint64_t write = atomic_load_explicit(&driver->output_write,memory_order_acquire);
        if (read == write) {
            const struct timespec delay = {0,1000000};
            (void)nanosleep(&delay,NULL);
            continue;
        }
        OutputEntry entry = driver->output[read % OUTPUT_SLOTS];
        atomic_store_explicit(&driver->output_read,read+1,memory_order_release);
        if (!atomic_load_explicit(&driver->online,memory_order_acquire) ||
            atomic_load_explicit(&driver->audio_device,memory_order_acquire) != entry.audio_device ||
            atomic_load_explicit(&driver->generation,memory_order_acquire) != entry.generation ||
            atomic_load_explicit(&driver->lifecycle_epoch,memory_order_acquire) != entry.lifecycle_epoch) continue;
        if(entry.kind==1) {
            uint64_t head=atomic_load_explicit(&driver->feedback_write,memory_order_relaxed);
            uint64_t tail=atomic_load_explicit(&driver->feedback_read,memory_order_acquire);
            if(head-tail>=OUTPUT_SLOTS) {record_io_failure(driver,entry.audio_device,entry.generation,entry.lifecycle_epoch);continue;}
            driver->feedback[head%OUTPUT_SLOTS]=entry;
            atomic_store_explicit(&driver->feedback_write,head+1,memory_order_release);continue;
        }
        AudioObjectPropertyAddress property = address(RK3_PROPERTY_COMMAND);
        RK3NativeCommandLease lease = {RK3_CONTROL_VERSION,0,entry.generation};
        OSStatus result = AudioObjectSetPropertyData(entry.audio_device,&property,sizeof(lease),&lease,
                                                     sizeof(entry.command),&entry.command);
        if (result != noErr) {
            fprintf(stderr,"RK3 MIDI hardware write failed: %d\n",(int)result);
            record_io_failure(driver,entry.audio_device,entry.generation,entry.lifecycle_epoch);
        }
    }
    release_ref(driver);
    return NULL;
}

static const void *retain_timer_context(const void *context) { add_ref((void *)context); return context; }
static void release_timer_context(const void *context) { release_ref((void *)context); }

static OSStatus start_driver(MIDIDriverRef self, MIDIDeviceListRef devices) {
    Driver *driver = driver_from_ref(self);
    if (CFRunLoopGetCurrent() != driver->main_loop) return kMIDIWrongThread;
    pthread_mutex_lock(&driver->state_lock);
    bool already_started = driver->started;
    pthread_mutex_unlock(&driver->state_lock);
    if (already_started) return noErr;
    CFRunLoopRef io_loop = MIDIGetDriverIORunLoop();
    if (io_loop == NULL) return kMIDIServerStartErr;
    if (driver->io_loop != NULL) CFRelease(driver->io_loop);
    driver->io_loop = (CFRunLoopRef)CFRetain(io_loop);
    for (ItemCount index = 0; index < MIDIDeviceListGetNumberOfDevices(devices); ++index) {
        MIDIDeviceRef device = MIDIDeviceListGetDevice(devices,index);
        (void)MIDIObjectSetIntegerProperty(device,kMIDIPropertyOffline,1);
        if (controller_matches(device)) {
            OSStatus result=bind_controller(driver,device);if(result!=noErr)return result;
        }
        if (device_matches(device)) {
            OSStatus result = bind_endpoints(driver,device);
            if (result != noErr) return result;
        }
    }
    pthread_mutex_lock(&driver->state_lock);
    driver->started = true;
    driver->published_online = false;
    atomic_store(&driver->source_enabled,false);
    atomic_store(&driver->parser_reset,true);
    driver->baseline_pending = false;
    driver->cursor = 0;
    pthread_mutex_unlock(&driver->state_lock);
    atomic_store_explicit(&driver->worker_stop,false,memory_order_release);
    atomic_store_explicit(&driver->failed_device,kAudioObjectUnknown,memory_order_release);
    atomic_store_explicit(&driver->failed_generation,0,memory_order_release);
    atomic_store_explicit(&driver->output_read,0,memory_order_relaxed);
    atomic_store_explicit(&driver->output_write,0,memory_order_relaxed);
    add_ref(driver); /* worker owns one reference until it exits */
    if (pthread_create(&driver->worker,NULL,output_worker,driver) != 0) {
        release_ref(driver);
        (void)stop_driver(self);
        return kMIDIServerStartErr;
    }
    driver->worker_running = true;
    CFRunLoopTimerContext timer_context = {0,driver,retain_timer_context,release_timer_context,NULL};
    driver->discovery_timer = CFRunLoopTimerCreate(NULL,CFAbsoluteTimeGetCurrent()+DISCOVERY_INTERVAL,
        DISCOVERY_INTERVAL,0,0,discovery_tick,&timer_context);
    driver->input_timer = CFRunLoopTimerCreate(NULL,CFAbsoluteTimeGetCurrent()+INPUT_INTERVAL,
        INPUT_INTERVAL,0,0,input_tick,&timer_context);
    if (driver->discovery_timer == NULL || driver->input_timer == NULL) {
        (void)stop_driver(self); return kMIDIServerStartErr;
    }
    refresh_topology(driver);
    CFRunLoopAddTimer(driver->main_loop,driver->discovery_timer,kCFRunLoopDefaultMode);
    CFRunLoopAddTimer(driver->io_loop,driver->input_timer,kCFRunLoopDefaultMode);
    return noErr;
}

static OSStatus stop_driver(MIDIDriverRef self) {
    Driver *driver = driver_from_ref(self);
    if (CFRunLoopGetCurrent() != driver->main_loop) return kMIDIWrongThread;
    pthread_mutex_lock(&driver->state_lock);
    driver->started = false;
    atomic_store_explicit(&driver->online,false,memory_order_release);
    atomic_fetch_add_explicit(&driver->lifecycle_epoch,1,memory_order_acq_rel);
    atomic_store_explicit(&driver->parser_reset,true,memory_order_release);
    pthread_mutex_unlock(&driver->state_lock);
    if (driver->discovery_timer != NULL) {
        CFRunLoopTimerInvalidate(driver->discovery_timer);
        CFRelease(driver->discovery_timer);
        driver->discovery_timer = NULL;
    }
    if (driver->input_timer != NULL) {
        CFRunLoopTimerInvalidate(driver->input_timer);
        CFRelease(driver->input_timer);
        driver->input_timer = NULL;
    }
    atomic_store_explicit(&driver->worker_stop,true,memory_order_release);
    if (driver->worker_running) { pthread_join(driver->worker,NULL); driver->worker_running = false; }
    while(atomic_flag_test_and_set_explicit(&driver->feedback_consumer,memory_order_acquire)) {
        const struct timespec delay={0,1000000};(void)nanosleep(&delay,NULL);
    }
    atomic_store(&driver->feedback_read,atomic_load(&driver->feedback_write));
    atomic_flag_clear_explicit(&driver->feedback_consumer,memory_order_release);
    /* Wait only on the server main thread; Send itself never waits. A producer
     * which began before online=false must finish its bounded copy before a
     * later Start can reset/reuse the queue. */
    while (atomic_flag_test_and_set_explicit(&driver->output_producer,memory_order_acquire)) {
        const struct timespec delay = {0,1000000};
        (void)nanosleep(&delay,NULL);
    }
    atomic_store_explicit(&driver->output_read,0,memory_order_relaxed);
    atomic_store_explicit(&driver->output_write,0,memory_order_relaxed);
    atomic_flag_clear_explicit(&driver->output_producer,memory_order_release);
    if (driver->source != 0) (void)MIDIEndpointSetRefCons(driver->source,NULL,NULL);
    if (driver->destination != 0) (void)MIDIEndpointSetRefCons(driver->destination,NULL,NULL);
    if (driver->controller_source != 0) (void)MIDIEndpointSetRefCons(driver->controller_source,NULL,NULL);
    if (driver->controller_destination != 0) (void)MIDIEndpointSetRefCons(driver->controller_destination,NULL,NULL);
    publish_offline(driver,true);
    pthread_mutex_lock(&driver->state_lock);
    driver->midi_device = 0;
    driver->source = 0;
    driver->destination = 0;
    driver->controller_device=0;driver->controller_source=0;driver->controller_destination=0;
    atomic_store(&driver->controller_enabled,false);
    driver->source_enabled = false;
    driver->baseline_pending = false;
    pthread_mutex_unlock(&driver->state_lock);
    return noErr;
}

static OSStatus find_devices(MIDIDriverRef self, MIDIDeviceListRef devices) { (void)self; (void)devices; return noErr; }
static OSStatus configure_device(MIDIDriverRef self, MIDIDeviceRef device) { (void)self; (void)device; return noErr; }
static OSStatus flush_output(MIDIDriverRef self, MIDIEndpointRef endpoint, void *first, void *second) {
    (void)self; (void)endpoint; (void)first; (void)second;
    return noErr; /* No future timestamp scheduling, matching the NI interface. */
}
static OSStatus monitor_output(MIDIDriverRef self, MIDIEndpointRef endpoint, const MIDIPacketList *packets) {
    (void)self; (void)endpoint; (void)packets; return noErr;
}

static OSStatus enable_source(MIDIDriverRef self, MIDIEndpointRef source, Boolean enabled) {
    Driver *driver=driver_from_ref(self);
    pthread_mutex_lock(&driver->state_lock);
    bool controller=source!=0&&source==driver->controller_source;
    bool valid_source=controller||(source!=0&&source==driver->source);
    AudioDeviceID device=atomic_load(&driver->audio_device);
    uint64_t generation=atomic_load(&driver->generation),epoch=atomic_load(&driver->lifecycle_epoch);
    bool online=atomic_load(&driver->online);
    pthread_mutex_unlock(&driver->state_lock);
    if(!valid_source)return kMIDIUnknownEndpoint;
    uint64_t latest=0,baseline_sequence=0;uint32_t snapshot=0;
    if(enabled&&online) {
        RK3NativeReportQuery query={RK3_CONTROL_VERSION,0,0};RK3NativeReports reports;UInt32 size=sizeof(reports);
        AudioObjectPropertyAddress property=address(RK3_PROPERTY_REPORTS);
        if(AudioObjectGetPropertyData(device,&property,sizeof(query),&query,&size,&reports)!=noErr||size!=sizeof(reports)||reports.version!=RK3_CONTROL_VERSION)
            return kMIDIMessageSendErr;
        latest=reports.latest_sequence;
        if(controller) {
            RK3NativeInfo current;
            if(!read_info(device,&current)||current.generation!=generation)return kMIDINoConnection;
            snapshot=current.digital_snapshot;
            /* The snapshot and history are separate HAL reads. Keep the first
             * cursor for silent catch-up; a second barrier suppresses delivery
             * of transitions which occurred while obtaining that snapshot. */
            baseline_sequence=latest;size=sizeof(reports);
            if(AudioObjectGetPropertyData(device,&property,sizeof(query),&query,&size,&reports)!=noErr||
                size!=sizeof(reports)||reports.version!=RK3_CONTROL_VERSION||reports.latest_sequence<latest)
                return kMIDIMessageSendErr;
            latest=reports.latest_sequence;
        }
    }
    pthread_mutex_lock(&driver->state_lock);
    if(online&&(atomic_load(&driver->generation)!=generation||atomic_load(&driver->lifecycle_epoch)!=epoch)) {
        pthread_mutex_unlock(&driver->state_lock);return kMIDINoConnection;
    }
    atomic_bool *flag=controller?&driver->controller_enabled:&driver->source_enabled;
    atomic_uint_fast64_t *listener=controller?&driver->controller_listener_epoch:&driver->listener_epoch;
    if((enabled!=0)!=atomic_load(flag))atomic_fetch_add(listener,1);
    if(enabled&&!atomic_load(flag)) {
        if(controller) {
            driver->controller_min_sequence=latest;driver->controller_enable_sequence=baseline_sequence;
            driver->controller_enable_snapshot=snapshot;driver->controller_enable_epoch=epoch;
        }
        else {driver->din_min_sequence=latest;driver->baseline_pending=false;atomic_store(&driver->parser_reset,true);}
    }
    atomic_store(flag,enabled!=0);pthread_mutex_unlock(&driver->state_lock);return noErr;
}

/* Real-time Send performs bounded copying and lock-free queue publication only.
 * Success means accepted for delivery. The private worker reports later USB
 * failure by taking the device offline until its transport generation changes. */
static OSStatus send_packets(MIDIDriverRef self, const MIDIPacketList *packets, void *port, void *context) {
    Driver *driver = driver_from_ref(self);
    uint64_t epoch = atomic_load_explicit(&driver->lifecycle_epoch,memory_order_acquire);
    if (packets == NULL || context != driver || (port != NULL && port != (void *)(uintptr_t)1)) return kMIDIUnknownEndpoint;
    if (!atomic_load_explicit(&driver->online,memory_order_acquire)) return kMIDINoConnection;
    if (packets->numPackets > MAX_SEND_PACKETS) return kMIDIMessageSendErr;
    size_t total = 0;
    const MIDIPacket *packet = &packets->packet[0];
    for (UInt32 index = 0; index < packets->numPackets; ++index) {
        if (packet->length > OUTPUT_BYTES-total) return kMIDIMessageSendErr;
        total += packet->length;
        packet = MIDIPacketNext(packet);
    }
    if (total == 0) return noErr;
#ifdef RK3_MIDI_TESTING
    extern void rk3_test_before_producer(void *);
    rk3_test_before_producer(driver);
#endif
    if (atomic_flag_test_and_set_explicit(&driver->output_producer,memory_order_acquire)) return kMIDIMessageSendErr;
    if (atomic_load_explicit(&driver->lifecycle_epoch,memory_order_acquire) != epoch ||
        !atomic_load_explicit(&driver->online,memory_order_acquire)) {
        atomic_flag_clear_explicit(&driver->output_producer,memory_order_release); return kMIDINoConnection;
    }
    uint64_t write = atomic_load_explicit(&driver->output_write,memory_order_relaxed);
    uint64_t read = atomic_load_explicit(&driver->output_read,memory_order_acquire);
    size_t needed = (total+7)/8;
    if (write-read > OUTPUT_SLOTS || needed > OUTPUT_SLOTS-(write-read)) {
        atomic_flag_clear_explicit(&driver->output_producer,memory_order_release); return kMIDIMessageSendErr;
    }
    AudioDeviceID audio_device = atomic_load_explicit(&driver->audio_device,memory_order_acquire);
    uint64_t generation = atomic_load_explicit(&driver->generation,memory_order_acquire);
    RK3NativeCommand command = {.version=RK3_CONTROL_VERSION,.length=3,.bytes={7,0,0}};
    packet = &packets->packet[0];
    for (UInt32 index = 0; index < packets->numPackets; ++index) {
        for (UInt16 byte = 0; byte < packet->length; ++byte) {
            command.bytes[command.length++] = packet->data[byte];
            ++command.bytes[2];
            if (command.bytes[2] == 8) {
                driver->output[write++ % OUTPUT_SLOTS] = (OutputEntry){audio_device,generation,epoch,command,port != NULL ? 1u : 0u};
                memset(command.bytes+3,0,8);
                command.length = 3; command.bytes[2] = 0;
            }
        }
        packet = MIDIPacketNext(packet);
    }
    if (command.bytes[2] != 0) driver->output[write++ % OUTPUT_SLOTS] = (OutputEntry){audio_device,generation,epoch,command,port != NULL ? 1u : 0u};
    OSStatus result = noErr;
    if (!atomic_load_explicit(&driver->online,memory_order_acquire) ||
        atomic_load_explicit(&driver->audio_device,memory_order_acquire) != audio_device ||
        atomic_load_explicit(&driver->generation,memory_order_acquire) != generation ||
        atomic_load_explicit(&driver->lifecycle_epoch,memory_order_acquire) != epoch) result = kMIDINoConnection;
    else atomic_store_explicit(&driver->output_write,write,memory_order_release);
    atomic_flag_clear_explicit(&driver->output_producer,memory_order_release);
    return result;
}

static MIDIDriverInterface interface_table = {
    ._reserved=NULL,.QueryInterface=query_interface,.AddRef=add_ref,.Release=release_ref,
    .FindDevices=find_devices,.Start=start_driver,.Stop=stop_driver,.Configure=configure_device,
    .Send=send_packets,.EnableSource=enable_source,.Flush=flush_output,.Monitor=monitor_output,
    .SendPackets=NULL,.MonitorEvents=NULL
};

__attribute__((visibility("default")))
void *RK3MIDIDriverFactory(CFAllocatorRef allocator, CFUUIDRef type) {
    (void)allocator;
    if (type == NULL || !CFEqual(type,kMIDIDriverTypeID)) return NULL;
    Driver *driver = calloc(1,sizeof(*driver));
    if (driver == NULL) return NULL;
    driver->interface = &interface_table;
    atomic_init(&driver->references,1);
    atomic_init(&driver->online,false);
    atomic_init(&driver->source_enabled,false);
    atomic_init(&driver->controller_enabled,false);atomic_init(&driver->controller_listener_epoch,0);
    driver->controller_enable_epoch=UINT64_MAX;
    atomic_init(&driver->calibration,1023);driver->applied_calibration=UINT64_MAX;
    atomic_init(&driver->feedback_read,0);atomic_init(&driver->feedback_write,0);atomic_flag_clear(&driver->feedback_consumer);
    rk3_controller_init(&driver->controller);rk3_midi_stream_reset(&driver->feedback_parser);
    atomic_init(&driver->parser_reset,true);
    rk3_midi_stream_reset(&driver->parser);
    atomic_init(&driver->audio_device,kAudioObjectUnknown);
    atomic_init(&driver->generation,0);
    atomic_init(&driver->lifecycle_epoch,0);
    atomic_init(&driver->listener_epoch,0);
    atomic_init(&driver->failed_device,kAudioObjectUnknown);
    atomic_init(&driver->failed_generation,0);
    atomic_init(&driver->output_read,0);
    atomic_init(&driver->output_write,0);
    atomic_init(&driver->worker_stop,true);
    atomic_flag_clear(&driver->output_producer);
    if (pthread_mutex_init(&driver->state_lock,NULL) != 0) { free(driver); return NULL; }
    driver->main_loop = (CFRunLoopRef)CFRetain(CFRunLoopGetCurrent());
    CFPlugInAddInstanceForFactory(factory_id());
    return driver_ref(driver);
}
