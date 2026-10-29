/* Native AudioServerPlugIn adapter. Hardware behavior lives in the freshly
 * reconstructed NI transport/codec; this file implements the current SDK ABI. */
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/AudioHardware.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "../NativeUSB.h"
#include "../AudioTiming.h"
#include "../PedalSettings.h"

#ifndef RK3_AUDIO_LIFECYCLE_TRACE
#define RK3_AUDIO_LIFECYCLE_TRACE 0
#endif
#if RK3_AUDIO_LIFECYCLE_TRACE != 0 && RK3_AUDIO_LIFECYCLE_TRACE != 1
#error RK3_AUDIO_LIFECYCLE_TRACE must be zero or one
#endif
#if RK3_AUDIO_LIFECYCLE_TRACE
#include "AudioLifecycleTraceState.h"
static RK3AudioTraceState audio_lifecycle_trace;
#define RK3_AUDIO_TRACE_BASE(name) name##_without_trace
#else
#define RK3_AUDIO_TRACE_BASE(name) name
#endif

enum { DEVICE=2, INPUT=3, OUTPUT=4, RATE=RK3_AUDIO_DEFAULT_SAMPLE_RATE, PERIOD=16384, CLIENT_LIMIT=64 };
static AudioServerPlugInDriverInterface table;
static AudioServerPlugInDriverInterface *interface=&table;
static AudioServerPlugInDriverRef driver=&interface;
static AudioServerPlugInHostRef host;
static rk3_usb_session *usb;
static _Atomic uint32_t references=1;
static _Atomic uint32_t running;
static atomic_bool input_active=true,output_active=true;
static pthread_mutex_t control=PTHREAD_MUTEX_INITIALIZER;
static uint32_t clients[CLIENT_LIMIT];
static bool client_active[CLIENT_LIMIT];
static dispatch_source_t monitor;
static dispatch_queue_t device_control_queue;
static atomic_bool configuration_pending;
static atomic_int last_configuration_error;
static _Atomic uint32_t sample_rate=RATE;
#define RK3_AUDIO_RESTART_ACTION UINT64_C(0x726b33617564696f)
typedef enum { CONFIGURATION_NONE,CONFIGURATION_RESTART,CONFIGURATION_RATE } ConfigurationKind;
static struct {
    ConfigurationKind kind;
    uint64_t action,generation;
    uint32_t rate;
} pending_configuration;
static uint64_t next_configuration_action=RK3_AUDIO_RESTART_ACTION;
static _Atomic uint64_t rate_action_in_flight;
static uint64_t last_rate_commit_action,last_rate_commit_generation;
static bool deferred_panic;
static uint64_t deferred_panic_generation;
static RK3NativeInfo info(void);
static void request_configuration(uint64_t action);
/* All transaction fields except rate_action_in_flight are protected by control.
 * The worker only captures that token and enqueues; it never waits for control. */
static uint64_t begin_configuration_locked(ConfigurationKind kind,uint64_t generation,uint32_t rate) {
    if(next_configuration_action==UINT64_MAX)return 0;
    uint64_t action=next_configuration_action++;
    pending_configuration.kind=kind;pending_configuration.action=action;
    pending_configuration.generation=generation;pending_configuration.rate=rate;
    atomic_store(&configuration_pending,true);
    atomic_store(&rate_action_in_flight,kind==CONFIGURATION_RATE?action:0);return action;
}
static uint64_t finish_configuration_locked(int32_t error,bool committed) {
    uint64_t retry=deferred_panic&&!committed?deferred_panic_generation:0;
    deferred_panic=false;deferred_panic_generation=0;
    memset(&pending_configuration,0,sizeof(pending_configuration));
    atomic_store(&rate_action_in_flight,0);atomic_store(&configuration_pending,false);
    atomic_store(&last_configuration_error,error);return retry;
}
/* NI panic pauses/resumes its host engine. The current host's configuration
 * handshake is the documented equivalent: it stops IO before Perform and
 * restarts outstanding IO afterward (AudioServerPlugIn.h71-77). */
static void audio_panic_handler(void *context,uint64_t generation,int32_t error) {
    (void)context;(void)error;
    if(!device_control_queue)return;
    uint64_t covered_rate_action=atomic_load(&rate_action_in_flight);
    dispatch_async(device_control_queue,^{
        pthread_mutex_lock(&control);RK3NativeInfo current=info();
        if(!usb||!host||current.generation!=generation||
           !(current.flags&RK3_NATIVE_CONNECTED)) {
            pthread_mutex_unlock(&control);return;
        }
        /* A queued panic can reach this queue after its covering rate Perform.
         * Preserve that identity instead of submitting a redundant restart. */
        if(covered_rate_action&&last_rate_commit_generation==generation&&
           covered_rate_action==last_rate_commit_action) {pthread_mutex_unlock(&control);return;}
        if(pending_configuration.kind==CONFIGURATION_RATE) {
            deferred_panic=true;deferred_panic_generation=generation;
            pthread_mutex_unlock(&control);return;
        }
        if(pending_configuration.kind!=CONFIGURATION_NONE){pthread_mutex_unlock(&control);return;}
        uint64_t action=begin_configuration_locked(CONFIGURATION_RESTART,generation,0);
        pthread_mutex_unlock(&control);if(action)request_configuration(action);
    });
}
static void request_configuration(uint64_t action) {
    pthread_mutex_lock(&control);
    if(pending_configuration.action!=action||pending_configuration.kind==CONFIGURATION_NONE) {
        pthread_mutex_unlock(&control);return;
    }
    RK3NativeInfo current=info();
    if(!usb||!host||!(current.flags&RK3_NATIVE_CONNECTED)||current.generation!=pending_configuration.generation) {
        uint64_t retry=finish_configuration_locked(kAudioHardwareNotRunningError,false);
        pthread_mutex_unlock(&control);if(retry)audio_panic_handler(NULL,retry,0);return;
    }
    AudioServerPlugInHostRef owner=host;pthread_mutex_unlock(&control);
    OSStatus result=owner->RequestDeviceConfigurationChange(owner,DEVICE,action,NULL);
    pthread_mutex_lock(&control);
    uint64_t retry=0;
    /* The host may synchronously Perform/Abort and admit newer work before its
     * request function returns. Its result belongs only to this exact token. */
    if(result&&pending_configuration.kind!=CONFIGURATION_NONE&&pending_configuration.action==action)
        retry=finish_configuration_locked(result,false);
    pthread_mutex_unlock(&control);if(retry)audio_panic_handler(NULL,retry,0);
}
static OSStatus request_sample_rate(uint32_t rate) {
    pthread_mutex_lock(&control);
    if(pending_configuration.kind!=CONFIGURATION_NONE) {
        RK3NativeInfo current=info();
        bool same=pending_configuration.kind==CONFIGURATION_RATE&&pending_configuration.rate==rate&&
            current.generation==pending_configuration.generation&&(current.flags&RK3_NATIVE_CONNECTED);
        pthread_mutex_unlock(&control);return same?0:kAudioHardwareIllegalOperationError;
    }
    if(rate==atomic_load(&sample_rate)){pthread_mutex_unlock(&control);return 0;}
    RK3NativeInfo current=info();
    if(!usb||!host||!device_control_queue||!(current.flags&RK3_NATIVE_CONNECTED)) {
        pthread_mutex_unlock(&control);return kAudioHardwareNotRunningError;
    }
    uint64_t action=begin_configuration_locked(CONFIGURATION_RATE,current.generation,rate);
    pthread_mutex_unlock(&control);if(!action)return kAudioHardwareUnspecifiedError;
    dispatch_async(device_control_queue,^{request_configuration(action);});return 0;
}
static mach_timebase_info_data_t timebase;
static _Atomic uint64_t last_frame, last_time, last_seed;
static _Atomic uint32_t clock_revision;

static bool valid(AudioServerPlugInDriverRef value) { return value==driver; }
static bool object(AudioObjectID value) { return value>=kAudioObjectPlugInObject && value<=OUTPUT; }
static OSStatus checked(AudioServerPlugInDriverRef value, AudioObjectID value_id) {
    return valid(value)&&object(value_id)?0:kAudioHardwareBadObjectError;
}
static AudioStreamBasicDescription format_at(uint32_t rate) {
    AudioStreamBasicDescription f={0};
    f.mSampleRate=rate; f.mFormatID=kAudioFormatLinearPCM;
    f.mFormatFlags=kAudioFormatFlagsNativeFloatPacked;
    f.mBytesPerPacket=8; f.mFramesPerPacket=1; f.mBytesPerFrame=8;
    f.mChannelsPerFrame=2; f.mBitsPerChannel=32; return f;
}
static AudioStreamBasicDescription format(void) { return format_at(atomic_load(&sample_rate)); }
static RK3NativeInfo info(void) {
    RK3NativeInfo value={0}; value.version=RK3_CONTROL_VERSION;
    if(usb) (void)rk3_usb_info(usb,&value);
    return value;
}
static OSStatus read_cfdata(UInt32 size,const void *data,void *wire,size_t wire_size) {
    if(size!=sizeof(CFPropertyListRef)||!data)return kAudioHardwareBadPropertySizeError;
    CFPropertyListRef object=NULL;memcpy(&object,data,sizeof(object));
    if(!object||CFGetTypeID(object)!=CFDataGetTypeID()||CFDataGetLength((CFDataRef)object)!=(CFIndex)wire_size)
        return kAudioHardwareBadPropertySizeError;
    memcpy(wire,CFDataGetBytePtr((CFDataRef)object),wire_size);return 0;
}
static OSStatus gain_error(int32_t error) {
    if(!error)return 0;
    return error==EINVAL?kAudioHardwareIllegalOperationError:
        error==ENODEV||error==ESTALE?kAudioHardwareNotRunningError:kAudioHardwareUnspecifiedError;
}
static OSStatus gain_query(UInt32 size,const void *data,RK3NativeGainQuery *query) {
    OSStatus error=read_cfdata(size,data,query,sizeof(*query));if(error)return error;
    return query->version==RK3_CONTROL_VERSION&&query->generation?0:kAudioHardwareIllegalOperationError;
}
static OSStatus return_cfdata(const void *wire,size_t wire_size,void *output) {
    CFDataRef object=CFDataCreate(NULL,wire,(CFIndex)wire_size);
    if(!object)return kAudioHardwareUnspecifiedError;
    /* CoreAudio takes the returned +1 property-list reference. */
    memcpy(output,&object,sizeof(object));return 0;
}
static void notify(AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress a={selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    if(host) (void)host->PropertiesChanged(host,DEVICE,1,&a);
}
static void notify_rate_change(void) {
    if(!host)return;
    AudioObjectPropertyAddress device_changes[]={
        {kAudioDevicePropertyNominalSampleRate,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain},
        {RK3_PROPERTY_INFO,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain},
        {kAudioDevicePropertyLatency,kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMain},
        {kAudioDevicePropertyLatency,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyElementMain},
        {kAudioDevicePropertyLatency,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain},
        {kAudioDevicePropertySafetyOffset,kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMain},
        {kAudioDevicePropertySafetyOffset,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyElementMain},
        {kAudioDevicePropertySafetyOffset,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain}};
    AudioObjectPropertyAddress stream_changes[]={
        {kAudioStreamPropertyPhysicalFormat,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain},
        {kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain}};
    (void)host->PropertiesChanged(host,DEVICE,sizeof(device_changes)/sizeof(device_changes[0]),device_changes);
    (void)host->PropertiesChanged(host,INPUT,2,stream_changes);
    (void)host->PropertiesChanged(host,OUTPUT,2,stream_changes);
}
static HRESULT query(void *value, REFIID uuid, LPVOID *result) {
    if(!result) return E_POINTER;
    *result=NULL;
    if(value!=driver) return E_NOINTERFACE;
    CFUUIDRef id=CFUUIDCreateFromUUIDBytes(NULL,uuid);
    bool matches=CFEqual(id,IUnknownUUID)||CFEqual(id,kAudioServerPlugInDriverInterfaceUUID);
    CFRelease(id);
    if(!matches) return E_NOINTERFACE;
    atomic_fetch_add(&references,1); *result=driver; return S_OK;
}
static ULONG retain(void *value) { return value==driver?atomic_fetch_add(&references,1)+1:0; }
static ULONG release(void *value) {
    if(value!=driver) return 0;
    uint32_t count=atomic_load(&references);
    /* Singleton and worker code have process lifetime; retain the base reference. */
    while(count>1&&!atomic_compare_exchange_weak(&references,&count,count-1)) {}
    return count>1?count-1:1;
}
static OSStatus initialize(AudioServerPlugInDriverRef value, AudioServerPlugInHostRef owner) {
    if(!valid(value)||!owner) return kAudioHardwareIllegalOperationError;
    pthread_mutex_lock(&control);
    if(!host) {
        char error[192];
        usb=rk3_usb_open(error,sizeof(error));
        if(!usb) { pthread_mutex_unlock(&control); return kAudioHardwareUnspecifiedError; }
        host=owner; (void)mach_timebase_info(&timebase);
        dispatch_queue_t queue=dispatch_queue_create("org.rigkontrol3.ni-arm.properties",DISPATCH_QUEUE_SERIAL);
        device_control_queue=queue;
        if(!rk3_usb_set_audio_panic_handler(usb,audio_panic_handler,NULL)) {
            rk3_usb_close(usb);usb=NULL;host=NULL;device_control_queue=NULL;
            dispatch_release(queue);pthread_mutex_unlock(&control);return kAudioHardwareUnspecifiedError;
        }
        monitor=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,queue);
        if(monitor) {
            dispatch_source_set_timer(monitor,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),NSEC_PER_SEC/2,NSEC_PER_SEC/20);
            dispatch_source_set_event_handler(monitor,^{
                static uint32_t flags;static uint64_t generation;static int32_t last_error;
                RK3NativeInfo current=info();
                bool transport_changed=flags!=current.flags||generation!=current.generation;
                if(transport_changed){notify(kAudioDevicePropertyDeviceIsAlive);notify(kAudioDevicePropertyDeviceIsRunning);notify(RK3_PROPERTY_INPUT_GAIN);}
                if(transport_changed||last_error!=current.last_error)notify(RK3_PROPERTY_INFO);
                flags=current.flags;generation=current.generation;last_error=current.last_error;
            });
            dispatch_resume(monitor);
        }
        /* The process-lifetime singleton owns this queue even if no monitor
         * source could be created; asynchronous panic work must retain it. */
    }
    pthread_mutex_unlock(&control); return 0;
}
static OSStatus create(AudioServerPlugInDriverRef v,CFDictionaryRef d,const AudioServerPlugInClientInfo *c,AudioObjectID *o) {
    (void)v;(void)d;(void)c;if(o)*o=kAudioObjectUnknown;return kAudioHardwareUnsupportedOperationError;
}
static OSStatus destroy(AudioServerPlugInDriverRef v,AudioObjectID o) { (void)v;(void)o;return kAudioHardwareUnsupportedOperationError; }
static OSStatus client(AudioServerPlugInDriverRef v,AudioObjectID o,const AudioServerPlugInClientInfo *c) {
    (void)c;return valid(v)&&o==DEVICE?0:kAudioHardwareBadObjectError;
}
#if RK3_AUDIO_LIFECYCLE_TRACE
static OSStatus trace_client(AudioServerPlugInDriverRef v,AudioObjectID o,const AudioServerPlugInClientInfo *c,uint32_t kind) {
    OSStatus result=client(v,o,c);
    bool present=result==0&&c!=NULL;
    rk3_audio_trace_record(&audio_lifecycle_trace,kind,present?c->mClientID:0,present,
        present?c->mProcessID:0,present,o,0,result,mach_absolute_time());
    return result;
}
static OSStatus trace_add_client(AudioServerPlugInDriverRef v,AudioObjectID o,const AudioServerPlugInClientInfo *c) {
    return trace_client(v,o,c,RK3_AUDIO_TRACE_EVENT_ADD);
}
static OSStatus trace_remove_client(AudioServerPlugInDriverRef v,AudioObjectID o,const AudioServerPlugInClientInfo *c) {
    return trace_client(v,o,c,RK3_AUDIO_TRACE_EVENT_REMOVE);
}
#endif
static OSStatus configuration(AudioServerPlugInDriverRef v,AudioObjectID o,UInt64 action,void *data) {
    (void)data;
    if(!valid(v)||o!=DEVICE)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&control);
    if(pending_configuration.kind==CONFIGURATION_NONE||action!=pending_configuration.action) {
        pthread_mutex_unlock(&control);return kAudioHardwareUnsupportedOperationError;
    }
    if(atomic_load(&running)){pthread_mutex_unlock(&control);return kAudioHardwareIllegalOperationError;}
    RK3NativeInfo current=info();OSStatus result=0;bool changed=false;
    if(!usb||!(current.flags&RK3_NATIVE_CONNECTED)||current.generation!=pending_configuration.generation)
        result=kAudioHardwareNotRunningError;
    else if(pending_configuration.kind==CONFIGURATION_RATE) {
        /* The owner confirms the existing engine is idle/drained and the NI
         * command was acknowledged before either published rate changes. */
        result=rk3_usb_audio_set_sample_rate(usb,pending_configuration.generation,pending_configuration.rate,5000);
        if(!result) {
            atomic_store(&sample_rate,pending_configuration.rate);changed=true;
            last_rate_commit_action=action;last_rate_commit_generation=current.generation;
        }
    }
    /* Host StopIO has drained only the audio engine. A subsequent Host StartIO
     * resets the NI rings/clock while the controller's USB lease remains valid. */
    uint64_t retry=finish_configuration_locked(result,result==0);
    pthread_mutex_unlock(&control);
    if(changed)notify_rate_change();if(retry)audio_panic_handler(NULL,retry,0);return result;
}
static OSStatus abort_configuration(AudioServerPlugInDriverRef v,AudioObjectID o,UInt64 action,void *data) {
    (void)data;
    if(!valid(v)||o!=DEVICE)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&control);
    if(pending_configuration.kind==CONFIGURATION_NONE||action!=pending_configuration.action) {
        pthread_mutex_unlock(&control);return kAudioHardwareUnsupportedOperationError;
    }
    uint64_t retry=finish_configuration_locked(kAudioHardwareUnspecifiedError,false);
    pthread_mutex_unlock(&control);if(retry)audio_panic_handler(NULL,retry,0);return 0;
}
static bool has(AudioObjectID o, const AudioObjectPropertyAddress *a) {
    if(!a||!object(o)||a->mElement!=kAudioObjectPropertyElementMain) return false;
    switch(a->mSelector) {
        case kAudioObjectPropertyBaseClass: case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner: case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer: case kAudioObjectPropertyOwnedObjects: return true;
        default: break;
    }
    if(o==kAudioObjectPlugInObject) {
        switch(a->mSelector) {
            case kAudioPlugInPropertyBundleID: case kAudioPlugInPropertyDeviceList:
            case kAudioPlugInPropertyTranslateUIDToDevice: case kAudioPlugInPropertyResourceBundle:
            case kAudioPlugInPropertyBoxList: return true;
            default:return false;
        }
    }
    if(o==DEVICE) {
        switch(a->mSelector) {
#if RK3_AUDIO_LIFECYCLE_TRACE
            case RK3_AUDIO_TRACE_PROPERTY:return a->mScope==kAudioObjectPropertyScopeGlobal&&a->mElement==kAudioObjectPropertyElementMain;
#endif
            case kAudioDevicePropertyDeviceUID: case kAudioDevicePropertyModelUID:
            case kAudioDevicePropertyTransportType: case kAudioDevicePropertyRelatedDevices:
            case kAudioDevicePropertyClockDomain: case kAudioDevicePropertyDeviceIsAlive:
            case kAudioDevicePropertyDeviceIsRunning: case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: case kAudioDevicePropertyLatency:
            case kAudioDevicePropertyStreams: case kAudioObjectPropertyControlList:
            case kAudioDevicePropertySafetyOffset: case kAudioDevicePropertyNominalSampleRate:
            case kAudioDevicePropertyAvailableNominalSampleRates: case kAudioDevicePropertyIsHidden:
            case kAudioDevicePropertyPreferredChannelsForStereo: case kAudioDevicePropertyZeroTimeStampPeriod:
            case kAudioDevicePropertyClockAlgorithm: case kAudioDevicePropertyClockIsStable:
            case kAudioDevicePropertyStreamConfiguration: case kAudioObjectPropertyCustomPropertyInfoList:return true;
            case RK3_PROPERTY_SPEC:case RK3_PROPERTY_USB_IDENTITY:return a->mScope==kAudioObjectPropertyScopeGlobal;
            case RK3_PROPERTY_INPUT_GAIN:case RK3_PROPERTY_DIRECT_MONITOR:return a->mScope==kAudioObjectPropertyScopeGlobal;
            case RK3_PROPERTY_INFO: case RK3_PROPERTY_REPORTS: case RK3_PROPERTY_COMMAND: return true;
            default:return false;
        }
    }
    switch(a->mSelector) {
        case kAudioStreamPropertyIsActive: case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType: case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency: case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat: case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats: return true;
        default:return false;
    }
}
static Boolean has_property(AudioServerPlugInDriverRef v,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a) {
    (void)pid;return valid(v)&&has(o,a);
}
static bool settable(AudioObjectID o,AudioObjectPropertySelector s) {
    return (o==DEVICE&&(s==RK3_PROPERTY_COMMAND||s==RK3_PROPERTY_INPUT_GAIN||s==RK3_PROPERTY_DIRECT_MONITOR||s==kAudioDevicePropertyNominalSampleRate))||
        ((o==INPUT||o==OUTPUT)&&(s==kAudioStreamPropertyIsActive||s==kAudioStreamPropertyVirtualFormat||s==kAudioStreamPropertyPhysicalFormat));
}
static OSStatus is_settable(AudioServerPlugInDriverRef v,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a,Boolean *result) {
    (void)pid;OSStatus error=checked(v,o);if(error)return error;
    if(!result||!has(o,a))return kAudioHardwareUnknownPropertyError;
    *result=settable(o,a->mSelector);return 0;
}
static UInt32 streams(AudioObjectPropertyScope scope,AudioObjectID values[2]) {
    UInt32 n=0;
    if(scope==kAudioObjectPropertyScopeGlobal||scope==kAudioObjectPropertyScopeInput)values[n++]=INPUT;
    if(scope==kAudioObjectPropertyScopeGlobal||scope==kAudioObjectPropertyScopeOutput)values[n++]=OUTPUT;
    return n;
}
static UInt32 size_for(AudioObjectID o,const AudioObjectPropertyAddress *a) {
    switch(a->mSelector) {
        case kAudioObjectPropertyName:case kAudioObjectPropertyManufacturer:
        case kAudioPlugInPropertyBundleID:case kAudioPlugInPropertyResourceBundle:
        case kAudioDevicePropertyDeviceUID:case kAudioDevicePropertyModelUID:return sizeof(CFStringRef);
        case kAudioObjectPropertyOwnedObjects:
            if(o==kAudioObjectPlugInObject)return sizeof(AudioObjectID);
            if(o==DEVICE){AudioObjectID ids[2];return streams(a->mScope,ids)*sizeof(AudioObjectID);}return 0;
        case kAudioObjectPropertyControlList:case kAudioPlugInPropertyBoxList:return 0;
        case kAudioDevicePropertyStreams:{AudioObjectID ids[2];return streams(a->mScope,ids)*sizeof(AudioObjectID);}
        case kAudioDevicePropertyNominalSampleRate:return sizeof(Float64);
        case kAudioDevicePropertyAvailableNominalSampleRates:return RK3_AUDIO_RATE_COUNT*sizeof(AudioValueRange);
        case kAudioDevicePropertyPreferredChannelsForStereo:return 2*sizeof(UInt32);
        case kAudioDevicePropertyStreamConfiguration:return sizeof(AudioBufferList);
        case kAudioStreamPropertyVirtualFormat:case kAudioStreamPropertyPhysicalFormat:return sizeof(AudioStreamBasicDescription);
        case kAudioStreamPropertyAvailableVirtualFormats:case kAudioStreamPropertyAvailablePhysicalFormats:return RK3_AUDIO_RATE_COUNT*sizeof(AudioStreamRangedDescription);
        case kAudioObjectPropertyCustomPropertyInfoList:return (7+RK3_AUDIO_LIFECYCLE_TRACE)*sizeof(AudioServerPlugInCustomPropertyInfo);
#if RK3_AUDIO_LIFECYCLE_TRACE
        case RK3_AUDIO_TRACE_PROPERTY:return sizeof(CFPropertyListRef);
#endif
        case RK3_PROPERTY_INFO:case RK3_PROPERTY_REPORTS:case RK3_PROPERTY_COMMAND:case RK3_PROPERTY_INPUT_GAIN:case RK3_PROPERTY_DIRECT_MONITOR:case RK3_PROPERTY_SPEC:case RK3_PROPERTY_USB_IDENTITY:return sizeof(CFPropertyListRef);
        default:return sizeof(UInt32);
    }
}
static OSStatus get_size(AudioServerPlugInDriverRef v,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 *size) {
    (void)pid;(void)qs;(void)q;OSStatus error=checked(v,o);if(error)return error;
    if(!size||!has(o,a))return kAudioHardwareUnknownPropertyError;
    *size=size_for(o,a);return 0;
}
static OSStatus get(AudioServerPlugInDriverRef v,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 capacity,UInt32 *written,void *out) {
    (void)pid;OSStatus error=checked(v,o);if(error)return error;
    if(!written||!has(o,a))return kAudioHardwareUnknownPropertyError;
    UInt32 size=size_for(o,a);if(capacity<size||(!out&&size))return kAudioHardwareBadPropertySizeError;
    *written=size;if(!size)return 0;
    uint32_t number=0;CFStringRef string=NULL;
    switch(a->mSelector) {
        case kAudioObjectPropertyBaseClass:number=kAudioObjectClassID;break;
        case kAudioObjectPropertyClass:number=o==1?kAudioPlugInClassID:o==DEVICE?kAudioDeviceClassID:kAudioStreamClassID;break;
        case kAudioObjectPropertyOwner:number=o==1?kAudioObjectUnknown:o==DEVICE?kAudioObjectPlugInObject:DEVICE;break;
        case kAudioObjectPropertyName:string=o==1?CFSTR("RK3 NI ARM Driver"):o==DEVICE?CFSTR("Rig Kontrol 3 (NI ARM)"):o==INPUT?CFSTR("Guitar inputs"):CFSTR("Outputs");break;
        case kAudioObjectPropertyManufacturer:string=CFSTR("Independent NI protocol reconstruction");break;
        case kAudioPlugInPropertyBundleID:string=CFSTR("org.rigkontrol3.ni-arm.audio");break;
        case kAudioPlugInPropertyResourceBundle:string=CFSTR("");break;
        case kAudioDevicePropertyDeviceUID:string=CFSTR(RK3_NATIVE_DEVICE_UID);break;
        case kAudioDevicePropertyModelUID:string=CFSTR("org.rigkontrol3.ni-arm.model1940");break;
        case kAudioPlugInPropertyTranslateUIDToDevice:
            if(qs!=sizeof(CFStringRef)||!q)return kAudioHardwareBadPropertySizeError;
            number=CFEqual(*(CFStringRef const *)q,CFSTR(RK3_NATIVE_DEVICE_UID))?DEVICE:kAudioObjectUnknown;break;
        case kAudioPlugInPropertyDeviceList:case kAudioDevicePropertyRelatedDevices:number=DEVICE;break;
        case kAudioObjectPropertyOwnedObjects:
            if(o==1){number=DEVICE;break;}
            /* device falls through to its scoped streams */
            // fall through
        case kAudioDevicePropertyStreams:{AudioObjectID ids[2];UInt32 n=streams(a->mScope,ids);memcpy(out,ids,n*sizeof(*ids));return 0;}
        case kAudioDevicePropertyTransportType:number=kAudioDeviceTransportTypeUSB;break;
        case kAudioDevicePropertyDeviceIsAlive:number=(info().flags&RK3_NATIVE_CONNECTED)!=0;break;
        case kAudioDevicePropertyDeviceIsRunning:number=(info().flags&RK3_NATIVE_AUDIO_RUNNING)!=0;break;
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:number=1;break;
        case kAudioDevicePropertyLatency:
            if(o==DEVICE)number=a->mScope==kAudioObjectPropertyScopeInput?
                rk3_audio_input_latency_frames(atomic_load(&sample_rate)):rk3_audio_output_latency_frames(atomic_load(&sample_rate));
            break;
        case kAudioDevicePropertySafetyOffset:number=rk3_audio_sample_offset_frames(atomic_load(&sample_rate));break;
        case kAudioDevicePropertyZeroTimeStampPeriod:number=PERIOD;break;
        case kAudioDevicePropertyClockAlgorithm:number=kAudioDeviceClockAlgorithmRaw;break;
        case kAudioDevicePropertyClockIsStable:number=0;break;
        case kAudioDevicePropertyPreferredChannelsForStereo:{UInt32 channels[]={1,2};memcpy(out,channels,sizeof(channels));return 0;}
        case kAudioDevicePropertyNominalSampleRate:{Float64 rate=atomic_load(&sample_rate);memcpy(out,&rate,sizeof(rate));return 0;}
        case kAudioDevicePropertyAvailableNominalSampleRates:{
            AudioValueRange ranges[RK3_AUDIO_RATE_COUNT];
            for(unsigned i=0;i<RK3_AUDIO_RATE_COUNT;++i){Float64 rate=rk3_audio_sample_rate_at(i);ranges[i]=(AudioValueRange){rate,rate};}
            memcpy(out,ranges,sizeof(ranges));return 0;}
        case kAudioDevicePropertyStreamConfiguration:{AudioBufferList list={1,{{2,0,NULL}}};memcpy(out,&list,sizeof(list));return 0;}
        case kAudioStreamPropertyIsActive:number=atomic_load(o==INPUT?&input_active:&output_active);break;
        case kAudioStreamPropertyStartingChannel:number=1;break;
        case kAudioStreamPropertyDirection:number=o==INPUT;break;
        case kAudioStreamPropertyTerminalType:number=kAudioStreamTerminalTypeLine;break;
        case kAudioStreamPropertyVirtualFormat:case kAudioStreamPropertyPhysicalFormat:{AudioStreamBasicDescription f=format();memcpy(out,&f,sizeof(f));return 0;}
        case kAudioStreamPropertyAvailableVirtualFormats:case kAudioStreamPropertyAvailablePhysicalFormats:{
            AudioStreamRangedDescription formats[RK3_AUDIO_RATE_COUNT];
            for(unsigned i=0;i<RK3_AUDIO_RATE_COUNT;++i){uint32_t rate=rk3_audio_sample_rate_at(i);formats[i]=(AudioStreamRangedDescription){format_at(rate),{rate,rate}};}
            memcpy(out,formats,sizeof(formats));return 0;}
        case kAudioObjectPropertyCustomPropertyInfoList:{
            AudioServerPlugInCustomPropertyInfo custom[]={
                {RK3_PROPERTY_INFO,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeNone},
                {RK3_PROPERTY_REPORTS,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList},
                {RK3_PROPERTY_COMMAND,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList},
                {RK3_PROPERTY_INPUT_GAIN,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList},
                {RK3_PROPERTY_SPEC,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList},
                {RK3_PROPERTY_USB_IDENTITY,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList},
                {RK3_PROPERTY_DIRECT_MONITOR,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList},
#if RK3_AUDIO_LIFECYCLE_TRACE
                {RK3_AUDIO_TRACE_PROPERTY,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeNone},
#endif
            };
            memcpy(out,custom,sizeof(custom));return 0;}
#if RK3_AUDIO_LIFECYCLE_TRACE
        case RK3_AUDIO_TRACE_PROPERTY:{
            if(qs||q)return kAudioHardwareBadPropertySizeError;
            RK3AudioTraceSnapshot value;
            rk3_audio_trace_snapshot(&audio_lifecycle_trace,&value);
            return return_cfdata(&value,sizeof(value),out);
        }
#endif
        case RK3_PROPERTY_USB_IDENTITY:{
            RK3NativeUSBIdentityQuery query;OSStatus error=read_cfdata(qs,q,&query,sizeof(query));if(error)return error;
            if(query.version!=RK3_CONTROL_VERSION||query.reserved||!query.generation)return kAudioHardwareIllegalOperationError;
            RK3NativeUSBIdentity value={.version=RK3_CONTROL_VERSION,.generation=query.generation};
            pthread_mutex_lock(&control);
            int32_t result=usb?rk3_usb_copy_identity(usb,query.generation,&value):ENODEV;
            pthread_mutex_unlock(&control);
            if(result!=0&&result!=ENODEV&&result!=ESTALE)return kAudioHardwareUnspecifiedError;
            if(result)value=(RK3NativeUSBIdentity){.version=RK3_CONTROL_VERSION,.status=result};
            return return_cfdata(&value,sizeof(value),out);
        }
        case RK3_PROPERTY_SPEC:{
            RK3NativeSpecQuery query;OSStatus error=read_cfdata(qs,q,&query,sizeof(query));if(error)return error;
            if(query.version!=RK3_CONTROL_VERSION||query.reserved||!query.generation)return kAudioHardwareIllegalOperationError;
            RK3NativeSpec value={.version=RK3_CONTROL_VERSION,.generation=query.generation};
            pthread_mutex_lock(&control);
            int32_t result=usb?rk3_usb_copy_spec(usb,query.generation,value.raw):ENODEV;
            pthread_mutex_unlock(&control);
            if(result!=0&&result!=ENODEV&&result!=ESTALE)return kAudioHardwareUnspecifiedError;
            if(result){value.status=result;value.generation=0;memset(value.raw,0,sizeof(value.raw));}
            return return_cfdata(&value,sizeof(value),out);
        }
        case RK3_PROPERTY_DIRECT_MONITOR:{
            RK3NativeMonitorQuery query;OSStatus error=read_cfdata(qs,q,&query,sizeof(query));if(error)return error;
            if(query.version!=RK3_CONTROL_VERSION||query.reserved||!query.generation)return kAudioHardwareIllegalOperationError;
            RK3NativeMonitorValue value={.version=RK3_CONTROL_VERSION,.generation=query.generation};
            uint8_t mode=0;
            /* GET waits on this non-RT caller, never under the HAL control lock. */
            error=gain_error(usb?rk3_usb_direct_monitor_get(usb,query.generation,&mode):ENODEV);
            value.mode=mode;return error?error:return_cfdata(&value,sizeof(value),out);
        }
        case RK3_PROPERTY_INPUT_GAIN:{
            RK3NativeGainQuery query;OSStatus error=gain_query(qs,q,&query);if(error)return error;
            RK3NativeGainValue value={.version=RK3_CONTROL_VERSION,.channel=query.channel,.generation=query.generation};
            error=gain_error(usb?rk3_usb_input_gain_get(usb,query.generation,query.channel,&value.value_bits):ENODEV);
            return error?error:return_cfdata(&value,sizeof(value),out);
        }
        case RK3_PROPERTY_INFO:{RK3NativeInfo current=info();return return_cfdata(&current,sizeof(current),out);}
        case RK3_PROPERTY_REPORTS:{
            RK3NativeReportQuery query;RK3NativeReports reports;
            OSStatus decoded=read_cfdata(qs,q,&query,sizeof(query));if(decoded)return decoded;
            if(query.version!=RK3_CONTROL_VERSION||query.reserved||!usb||!rk3_usb_reports(usb,&query,&reports))
                return kAudioHardwareIllegalOperationError;
            return return_cfdata(&reports,sizeof(reports),out);
        }
        case RK3_PROPERTY_COMMAND:return kAudioHardwareIllegalOperationError;
        default:break;
    }
    if(string){CFRetain(string);memcpy(out,&string,sizeof(string));}else memcpy(out,&number,sizeof(number));
    return 0;
}
static OSStatus set(AudioServerPlugInDriverRef v,AudioObjectID o,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 size,const void *data) {
    (void)pid;OSStatus error=checked(v,o);if(error)return error;
    if(!has(o,a)||!settable(o,a->mSelector))return kAudioHardwareUnknownPropertyError;
    if(size!=size_for(o,a)||!data)return kAudioHardwareBadPropertySizeError;
    if(a->mSelector==RK3_PROPERTY_DIRECT_MONITOR){
        RK3NativeMonitorQuery query;RK3NativeMonitorValue value;
        OSStatus decoded=read_cfdata(qs,q,&query,sizeof(query));if(decoded)return decoded;
        decoded=read_cfdata(size,data,&value,sizeof(value));if(decoded)return decoded;
        if(query.version!=RK3_CONTROL_VERSION||query.reserved||!query.generation||
           value.version!=RK3_CONTROL_VERSION||value.generation!=query.generation)
            return kAudioHardwareIllegalOperationError;
        /* NI consumes only the scalar low byte and does not update its cache
         * or report hardware-state success after this setter. */
        return gain_error(usb?rk3_usb_direct_monitor_set(usb,query.generation,(uint8_t)value.mode):ENODEV);
    }
    if(a->mSelector==RK3_PROPERTY_INPUT_GAIN){
        RK3NativeGainQuery query;RK3NativeGainValue value;
        OSStatus error=gain_query(qs,q,&query);if(error)return error;
        error=read_cfdata(size,data,&value,sizeof(value));if(error)return error;
        if(value.version!=RK3_CONTROL_VERSION||value.reserved||value.channel!=query.channel||
           value.generation!=query.generation)return kAudioHardwareIllegalOperationError;
        bool changed=false;
        error=gain_error(usb?rk3_usb_input_gain_set(usb,query.generation,query.channel,value.value_bits,&changed):ENODEV);
        if(!error&&changed)notify(RK3_PROPERTY_INPUT_GAIN);return error;
    }
    if(a->mSelector==RK3_PROPERTY_COMMAND){
        RK3NativeCommand command_value;RK3NativeCommandLease lease_value;
        OSStatus decoded=read_cfdata(size,data,&command_value,sizeof(command_value));if(decoded)return decoded;
        decoded=read_cfdata(qs,q,&lease_value,sizeof(lease_value));if(decoded)return decoded;
        const RK3NativeCommand *command=&command_value;
        const RK3NativeCommandLease *lease=&lease_value;
        if(lease->version!=RK3_CONTROL_VERSION||lease->reserved)return kAudioHardwareIllegalOperationError;
        RK3NativeInfo current=info();
        bool midi=command->length>=4&&command->length<=64&&command->bytes[0]==7&&
            command->bytes[1]<current.midi_outputs&&command->bytes[2]>0&&command->bytes[2]<=61&&
            command->length==(uint32_t)(command->bytes[2]+3);
        bool leds=command->length==9&&command->bytes[0]==5&&
            command->bytes[6]<=1&&command->bytes[7]==0&&command->bytes[8]==0;
        bool pedals=rk3_pedal_configuration_valid(command->bytes,command->length);
        if(command->version!=RK3_CONTROL_VERSION||(!midi&&!leds&&!pedals))return kAudioHardwareIllegalOperationError;
        return usb&&rk3_usb_command_for_generation(usb,command->bytes,command->length,lease->generation,500)?0:kAudioHardwareUnspecifiedError;
    }
    if(a->mSelector==kAudioDevicePropertyNominalSampleRate) {
        Float64 rate;memcpy(&rate,data,sizeof(rate));
        if(!isfinite(rate)||rate<0||rate>UINT32_MAX||!rk3_audio_sample_rate_supported((uint32_t)rate)||rate!=(Float64)(uint32_t)rate)
            return kAudioHardwareUnsupportedOperationError;
        return request_sample_rate((uint32_t)rate);
    }
    if(a->mSelector==kAudioStreamPropertyIsActive) {
        UInt32 active=0;memcpy(&active,data,sizeof(active));if(active>1)return kAudioHardwareIllegalOperationError;
        atomic_store(o==INPUT?&input_active:&output_active,active!=0);
        if(o==OUTPUT&&usb)rk3_usb_set_output_active(usb,active!=0);
#if RK3_AUDIO_LIFECYCLE_TRACE
        rk3_audio_trace_record(&audio_lifecycle_trace,RK3_AUDIO_TRACE_EVENT_ACTIVITY,0,false,
            pid,true,o,active,0,mach_absolute_time());
#endif
        AudioObjectPropertyAddress changed={kAudioStreamPropertyIsActive,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        if(host)(void)host->PropertiesChanged(host,o,1,&changed);return 0;
    }
    AudioStreamBasicDescription requested;memcpy(&requested,data,sizeof(requested));
    AudioStreamBasicDescription expected=format();
    Float64 rate=requested.mSampleRate;
    if(!isfinite(rate)||rate<0||rate>UINT32_MAX||rate!=(Float64)(uint32_t)rate||!rk3_audio_sample_rate_supported((uint32_t)rate)||
        requested.mFormatID!=expected.mFormatID||requested.mFormatFlags!=expected.mFormatFlags||requested.mBytesPerPacket!=8||
        requested.mFramesPerPacket!=1||requested.mBytesPerFrame!=8||requested.mChannelsPerFrame!=2||requested.mBitsPerChannel!=32||requested.mReserved!=0)
        return kAudioHardwareUnsupportedOperationError;
    return request_sample_rate((uint32_t)rate);
}
static OSStatus RK3_AUDIO_TRACE_BASE(start)(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c) {
    if(!valid(v)||o!=DEVICE)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&control);
    size_t slot=CLIENT_LIMIT;
    for(size_t i=0;i<CLIENT_LIMIT;++i){if(client_active[i]&&clients[i]==c){pthread_mutex_unlock(&control);return 0;}if(!client_active[i])slot=i;}
    if(slot==CLIENT_LIMIT||!usb){pthread_mutex_unlock(&control);return kAudioHardwareIllegalOperationError;}
    int32_t error=atomic_load(&running)==0?rk3_usb_audio_start(usb,5000):0;
    if(!error){clients[slot]=c;client_active[slot]=true;atomic_fetch_add(&running,1);}
    pthread_mutex_unlock(&control);if(!error)notify(kAudioDevicePropertyDeviceIsRunning);return error;
}
static OSStatus RK3_AUDIO_TRACE_BASE(stop)(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c) {
    if(!valid(v)||o!=DEVICE)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&control);bool found=false;
    for(size_t i=0;i<CLIENT_LIMIT;++i)if(client_active[i]&&clients[i]==c){client_active[i]=false;found=true;break;}
    int32_t error=0;
    if(found&&atomic_fetch_sub(&running,1)==1)error=rk3_usb_audio_stop(usb,5000);
    pthread_mutex_unlock(&control);if(found)notify(kAudioDevicePropertyDeviceIsRunning);return error;
}
#if RK3_AUDIO_LIFECYCLE_TRACE
static OSStatus start(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c) {
    rk3_audio_trace_record(&audio_lifecycle_trace,RK3_AUDIO_TRACE_EVENT_START_ENTER,c,true,0,false,o,0,0,mach_absolute_time());
    OSStatus result=start_without_trace(v,o,c);
    rk3_audio_trace_record(&audio_lifecycle_trace,RK3_AUDIO_TRACE_EVENT_START_EXIT,c,true,0,false,o,0,result,mach_absolute_time());
    return result;
}
static OSStatus stop(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c) {
    rk3_audio_trace_record(&audio_lifecycle_trace,RK3_AUDIO_TRACE_EVENT_STOP_ENTER,c,true,0,false,o,0,0,mach_absolute_time());
    OSStatus result=stop_without_trace(v,o,c);
    rk3_audio_trace_record(&audio_lifecycle_trace,RK3_AUDIO_TRACE_EVENT_STOP_EXIT,c,true,0,false,o,0,result,mach_absolute_time());
    return result;
}
static uint32_t trace_category(UInt32 operation) {
    return operation==kAudioServerPlugInIOOperationReadInput?RK3_AUDIO_TRACE_READ:
        operation==kAudioServerPlugInIOOperationWriteMix?RK3_AUDIO_TRACE_WRITE:RK3_AUDIO_TRACE_OTHER;
}
#endif
static OSStatus timestamp(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c,Float64 *sample,UInt64 *time,UInt64 *seed) {
    (void)c;if(!valid(v)||o!=DEVICE||!sample||!time||!seed)return kAudioHardwareBadObjectError;
    rk3_usb_clock_snapshot clock;
    if(!usb||!rk3_usb_clock(usb,&clock)||!clock.host_time)return kAudioHardwareNotRunningError;
    if(!clock.seed||!clock.zero_host_time)return kAudioHardwareNotRunningError;
    uint64_t frame=clock.zero_sample_frame;
    if(frame%PERIOD)return kAudioHardwareIllegalOperationError;
    uint32_t revision=atomic_load_explicit(&clock_revision,memory_order_acquire);
    if(!(revision&1)) {
        uint64_t old_seed=atomic_load_explicit(&last_seed,memory_order_relaxed);
        uint64_t old_frame=atomic_load_explicit(&last_frame,memory_order_relaxed);
        if(clock.seed>old_seed||(clock.seed==old_seed&&frame>old_frame)) {
            uint32_t expected=revision;
            if(atomic_compare_exchange_strong_explicit(&clock_revision,&expected,revision+1,memory_order_acquire,memory_order_relaxed)) {
                /* NI timestamps its input ring boundary at the exact USB
                 * byte fraction. Do not infer it from a nominal sample rate. */
                atomic_store_explicit(&last_time,clock.zero_host_time,memory_order_relaxed);
                atomic_store_explicit(&last_frame,frame,memory_order_relaxed);
                atomic_store_explicit(&last_seed,clock.seed,memory_order_relaxed);
                atomic_store_explicit(&clock_revision,revision+2,memory_order_release);
            }
        }
    }
    /* Bounded coherent snapshot: no RT mutex or unbounded spin. */
    for(unsigned attempt=0;attempt<3;++attempt) {
        uint32_t first=atomic_load_explicit(&clock_revision,memory_order_acquire);
        if(first&1)continue;
        uint64_t f=atomic_load_explicit(&last_frame,memory_order_relaxed);
        uint64_t t=atomic_load_explicit(&last_time,memory_order_relaxed);
        uint64_t s=atomic_load_explicit(&last_seed,memory_order_relaxed);
        if(first==atomic_load_explicit(&clock_revision,memory_order_acquire)&&s) {
            *sample=(Float64)f;*time=t;*seed=s;return 0;
        }
    }
    return kAudioHardwareNotRunningError;
}
static OSStatus will(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c,UInt32 operation,Boolean *perform,Boolean *in_place) {
    (void)c;if(!valid(v)||o!=DEVICE||!perform||!in_place)return kAudioHardwareBadObjectError;
    *perform=operation==kAudioServerPlugInIOOperationReadInput||operation==kAudioServerPlugInIOOperationWriteMix;*in_place=true;
#if RK3_AUDIO_LIFECYCLE_TRACE
    rk3_audio_trace_will(&audio_lifecycle_trace,c,trace_category(operation),*perform);
#endif
    return 0;
}
static OSStatus edge(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c,UInt32 operation,UInt32 frames,const AudioServerPlugInIOCycleInfo *cycle) {
    (void)c;(void)operation;(void)frames;(void)cycle;return valid(v)&&o==DEVICE?0:kAudioHardwareBadObjectError;
}
static OSStatus io(AudioServerPlugInDriverRef v,AudioObjectID o,AudioObjectID stream,UInt32 c,UInt32 operation,UInt32 frames,const AudioServerPlugInIOCycleInfo *cycle,void *main_buffer,void *secondary) {
#if RK3_AUDIO_LIFECYCLE_TRACE
    /* Invocation counters only: no buffer, PCM or timestamp inspection. */
    rk3_audio_trace_io(&audio_lifecycle_trace,c,stream,trace_category(operation));
#endif
    (void)c;(void)secondary;
    if(!valid(v)||o!=DEVICE||(stream!=INPUT&&stream!=OUTPUT))return kAudioHardwareBadObjectError;
    if(!usb||!main_buffer||frames>PERIOD||!cycle)return kAudioHardwareIllegalOperationError;
    const AudioTimeStamp *stamp=operation==kAudioServerPlugInIOOperationReadInput?&cycle->mInputTime:&cycle->mOutputTime;
    if(!(stamp->mFlags&kAudioTimeStampSampleTimeValid)||!isfinite(stamp->mSampleTime)||
       stamp->mSampleTime< -0x1p63||stamp->mSampleTime>=0x1p63)return kAudioHardwareIllegalOperationError;
    /* The old host supplied a ring firstSampleFrame. Preserve that position,
     * including initial negative input times, using unsigned modulo16384. */
    uint64_t first=(uint64_t)(int64_t)floor(stamp->mSampleTime);
    if(operation==kAudioServerPlugInIOOperationReadInput&&stream==INPUT){
        if(!atomic_load(&input_active)){memset(main_buffer,0,frames*2*sizeof(float));return 0;}
        size_t actual=rk3_usb_capture_at(usb,main_buffer,first,frames);
        if(actual<frames)memset((float *)main_buffer+actual*2,0,(frames-actual)*2*sizeof(float));return 0;
    }
    if(operation==kAudioServerPlugInIOOperationWriteMix&&stream==OUTPUT){
        if(!atomic_load(&output_active))return 0;
        return rk3_usb_render_at(usb,main_buffer,first,frames)==frames?0:kAudioHardwareUnspecifiedError;
    }
    return kAudioHardwareUnsupportedOperationError;
}
static AudioServerPlugInDriverInterface table={NULL,query,retain,release,initialize,create,destroy,
#if RK3_AUDIO_LIFECYCLE_TRACE
    trace_add_client,trace_remove_client,
#else
    client,client,
#endif
    configuration,abort_configuration,has_property,is_settable,get_size,get,set,start,stop,timestamp,will,edge,io,edge};
__attribute__((visibility("default"))) void *RK3AudioFactory(CFAllocatorRef allocator,CFUUIDRef type) {
    (void)allocator;if(!type||!CFEqual(type,kAudioServerPlugInTypeUUID))return NULL;
    retain(driver);return driver;
}
