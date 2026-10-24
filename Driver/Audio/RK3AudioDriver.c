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
#include <stdlib.h>
#include <string.h>
#include "../NativeUSB.h"
#include "../AudioTiming.h"

enum { DEVICE=2, INPUT=3, OUTPUT=4, RATE=48000, PERIOD=16384, CLIENT_LIMIT=64 };
static AudioServerPlugInDriverInterface table;
static AudioServerPlugInDriverInterface *interface=&table;
static AudioServerPlugInDriverRef driver=&interface;
static AudioServerPlugInHostRef host;
static rk3_usb_session *usb;
static _Atomic uint32_t references=1;
static _Atomic uint32_t running;
static pthread_mutex_t control=PTHREAD_MUTEX_INITIALIZER;
static uint32_t clients[CLIENT_LIMIT];
static bool client_active[CLIENT_LIMIT];
static dispatch_source_t monitor;
static mach_timebase_info_data_t timebase;
static _Atomic uint64_t last_frame, last_time, last_seed;
static _Atomic uint32_t clock_revision;

static bool valid(AudioServerPlugInDriverRef value) { return value==driver; }
static bool object(AudioObjectID value) { return value>=kAudioObjectPlugInObject && value<=OUTPUT; }
static OSStatus checked(AudioServerPlugInDriverRef value, AudioObjectID value_id) {
    return valid(value)&&object(value_id)?0:kAudioHardwareBadObjectError;
}
static AudioStreamBasicDescription format(void) {
    AudioStreamBasicDescription f={0};
    f.mSampleRate=RATE; f.mFormatID=kAudioFormatLinearPCM;
    f.mFormatFlags=kAudioFormatFlagsNativeFloatPacked;
    f.mBytesPerPacket=8; f.mFramesPerPacket=1; f.mBytesPerFrame=8;
    f.mChannelsPerFrame=2; f.mBitsPerChannel=32; return f;
}
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
        monitor=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,queue);
        if(monitor) {
            dispatch_source_set_timer(monitor,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),NSEC_PER_SEC/2,NSEC_PER_SEC/20);
            dispatch_source_set_event_handler(monitor,^{
                static uint32_t flags; static uint64_t generation;
                RK3NativeInfo current=info();
                if(flags!=current.flags||generation!=current.generation) {
                    flags=current.flags; generation=current.generation;
                    notify(kAudioDevicePropertyDeviceIsAlive); notify(kAudioDevicePropertyDeviceIsRunning); notify(RK3_PROPERTY_INFO);
                }
            });
            dispatch_resume(monitor);
        }
        dispatch_release(queue);
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
static OSStatus configuration(AudioServerPlugInDriverRef v,AudioObjectID o,UInt64 action,void *data) {
    (void)action;(void)data;return valid(v)&&o==DEVICE?0:kAudioHardwareBadObjectError;
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
            case kAudioDevicePropertyStreamConfiguration: case kAudioObjectPropertyCustomPropertyInfoList:
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
    return (o==DEVICE&&(s==RK3_PROPERTY_COMMAND||s==kAudioDevicePropertyNominalSampleRate))||
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
        case kAudioDevicePropertyAvailableNominalSampleRates:return sizeof(AudioValueRange);
        case kAudioDevicePropertyPreferredChannelsForStereo:return 2*sizeof(UInt32);
        case kAudioDevicePropertyStreamConfiguration:return sizeof(AudioBufferList);
        case kAudioStreamPropertyVirtualFormat:case kAudioStreamPropertyPhysicalFormat:return sizeof(AudioStreamBasicDescription);
        case kAudioStreamPropertyAvailableVirtualFormats:case kAudioStreamPropertyAvailablePhysicalFormats:return sizeof(AudioStreamRangedDescription);
        case kAudioObjectPropertyCustomPropertyInfoList:return 3*sizeof(AudioServerPlugInCustomPropertyInfo);
        case RK3_PROPERTY_INFO:case RK3_PROPERTY_REPORTS:case RK3_PROPERTY_COMMAND:return sizeof(CFPropertyListRef);
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
                RK3_AUDIO_INPUT_LATENCY_FRAMES:RK3_AUDIO_OUTPUT_LATENCY_FRAMES;
            break;
        case kAudioDevicePropertySafetyOffset:number=128;break;
        case kAudioDevicePropertyZeroTimeStampPeriod:number=PERIOD;break;
        case kAudioDevicePropertyClockAlgorithm:number=kAudioDeviceClockAlgorithmRaw;break;
        case kAudioDevicePropertyClockIsStable:number=0;break;
        case kAudioDevicePropertyPreferredChannelsForStereo:{UInt32 channels[]={1,2};memcpy(out,channels,sizeof(channels));return 0;}
        case kAudioDevicePropertyNominalSampleRate:{Float64 rate=RATE;memcpy(out,&rate,sizeof(rate));return 0;}
        case kAudioDevicePropertyAvailableNominalSampleRates:{AudioValueRange range={RATE,RATE};memcpy(out,&range,sizeof(range));return 0;}
        case kAudioDevicePropertyStreamConfiguration:{AudioBufferList list={1,{{2,0,NULL}}};memcpy(out,&list,sizeof(list));return 0;}
        case kAudioStreamPropertyIsActive:case kAudioStreamPropertyStartingChannel:number=1;break;
        case kAudioStreamPropertyDirection:number=o==INPUT;break;
        case kAudioStreamPropertyTerminalType:number=kAudioStreamTerminalTypeLine;break;
        case kAudioStreamPropertyVirtualFormat:case kAudioStreamPropertyPhysicalFormat:{AudioStreamBasicDescription f=format();memcpy(out,&f,sizeof(f));return 0;}
        case kAudioStreamPropertyAvailableVirtualFormats:case kAudioStreamPropertyAvailablePhysicalFormats:{AudioStreamRangedDescription f={format(),{RATE,RATE}};memcpy(out,&f,sizeof(f));return 0;}
        case kAudioObjectPropertyCustomPropertyInfoList:{
            AudioServerPlugInCustomPropertyInfo custom[]={
                {RK3_PROPERTY_INFO,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeNone},
                {RK3_PROPERTY_REPORTS,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList},
                {RK3_PROPERTY_COMMAND,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList}};
            memcpy(out,custom,sizeof(custom));return 0;}
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
        bool leds=command->length==9&&command->bytes[0]==5&&command->bytes[1]==0&&
            command->bytes[2]==0x40&&command->bytes[3]==0x40&&command->bytes[4]==0&&
            command->bytes[6]<=1&&command->bytes[7]==0&&command->bytes[8]==0;
        if(command->version!=RK3_CONTROL_VERSION||(!midi&&!leds))return kAudioHardwareIllegalOperationError;
        return usb&&rk3_usb_command_for_generation(usb,command->bytes,command->length,lease->generation,500)?0:kAudioHardwareUnspecifiedError;
    }
    if(a->mSelector==kAudioDevicePropertyNominalSampleRate)return *(const Float64 *)data==RATE?0:kAudioHardwareUnsupportedOperationError;
    if(a->mSelector==kAudioStreamPropertyIsActive)return *(const UInt32 *)data==1?0:kAudioHardwareUnsupportedOperationError;
    AudioStreamBasicDescription expected=format();
    const AudioStreamBasicDescription *requested=data;
    return requested->mSampleRate==expected.mSampleRate&&requested->mFormatID==expected.mFormatID&&
        requested->mFormatFlags==expected.mFormatFlags&&requested->mBytesPerPacket==8&&requested->mFramesPerPacket==1&&
        requested->mBytesPerFrame==8&&requested->mChannelsPerFrame==2&&requested->mBitsPerChannel==32?0:kAudioHardwareUnsupportedOperationError;
}
static OSStatus start(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c) {
    if(!valid(v)||o!=DEVICE)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&control);
    size_t slot=CLIENT_LIMIT;
    for(size_t i=0;i<CLIENT_LIMIT;++i){if(client_active[i]&&clients[i]==c){pthread_mutex_unlock(&control);return 0;}if(!client_active[i])slot=i;}
    if(slot==CLIENT_LIMIT||!usb){pthread_mutex_unlock(&control);return kAudioHardwareIllegalOperationError;}
    int32_t error=atomic_load(&running)==0?rk3_usb_audio_start(usb,5000):0;
    if(!error){clients[slot]=c;client_active[slot]=true;atomic_fetch_add(&running,1);}
    pthread_mutex_unlock(&control);if(!error)notify(kAudioDevicePropertyDeviceIsRunning);return error;
}
static OSStatus stop(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c) {
    if(!valid(v)||o!=DEVICE)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&control);bool found=false;
    for(size_t i=0;i<CLIENT_LIMIT;++i)if(client_active[i]&&clients[i]==c){client_active[i]=false;found=true;break;}
    int32_t error=0;
    if(found&&atomic_fetch_sub(&running,1)==1)error=rk3_usb_audio_stop(usb,5000);
    pthread_mutex_unlock(&control);if(found)notify(kAudioDevicePropertyDeviceIsRunning);return error;
}
static OSStatus timestamp(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c,Float64 *sample,UInt64 *time,UInt64 *seed) {
    (void)c;if(!valid(v)||o!=DEVICE||!sample||!time||!seed)return kAudioHardwareBadObjectError;
    rk3_usb_clock_snapshot clock;
    if(!usb||!rk3_usb_clock(usb,&clock)||!clock.host_time)return kAudioHardwareNotRunningError;
    if(!clock.seed||!timebase.numer||!timebase.denom)return kAudioHardwareNotRunningError;
    uint64_t frame=(clock.sample_frame/PERIOD)*PERIOD;
    uint32_t revision=atomic_load_explicit(&clock_revision,memory_order_acquire);
    if(!(revision&1)) {
        uint64_t old_seed=atomic_load_explicit(&last_seed,memory_order_relaxed);
        uint64_t old_frame=atomic_load_explicit(&last_frame,memory_order_relaxed);
        if(clock.seed>old_seed||(clock.seed==old_seed&&frame>old_frame)) {
            uint32_t expected=revision;
            if(atomic_compare_exchange_strong_explicit(&clock_revision,&expected,revision+1,memory_order_acquire,memory_order_relaxed)) {
                long double delta=(long double)(clock.sample_frame-frame)*1000000000.0L*timebase.denom/(RATE*(long double)timebase.numer);
                uint64_t ticks=(uint64_t)delta;
                atomic_store_explicit(&last_time,clock.host_time>ticks?clock.host_time-ticks:1,memory_order_relaxed);
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
    *perform=operation==kAudioServerPlugInIOOperationReadInput||operation==kAudioServerPlugInIOOperationWriteMix;*in_place=true;return 0;
}
static OSStatus edge(AudioServerPlugInDriverRef v,AudioObjectID o,UInt32 c,UInt32 operation,UInt32 frames,const AudioServerPlugInIOCycleInfo *cycle) {
    (void)c;(void)operation;(void)frames;(void)cycle;return valid(v)&&o==DEVICE?0:kAudioHardwareBadObjectError;
}
static OSStatus io(AudioServerPlugInDriverRef v,AudioObjectID o,AudioObjectID stream,UInt32 c,UInt32 operation,UInt32 frames,const AudioServerPlugInIOCycleInfo *cycle,void *main_buffer,void *secondary) {
    (void)c;(void)cycle;(void)secondary;
    if(!valid(v)||o!=DEVICE||(stream!=INPUT&&stream!=OUTPUT))return kAudioHardwareBadObjectError;
    if(!usb||!main_buffer||frames>PERIOD)return kAudioHardwareIllegalOperationError;
    if(operation==kAudioServerPlugInIOOperationReadInput&&stream==INPUT){
        size_t actual=rk3_usb_capture(usb,main_buffer,frames);
        if(actual<frames)memset((float *)main_buffer+actual*2,0,(frames-actual)*2*sizeof(float));return 0;
    }
    if(operation==kAudioServerPlugInIOOperationWriteMix&&stream==OUTPUT){
        return rk3_usb_render(usb,main_buffer,frames)==frames?0:kAudioHardwareUnspecifiedError;
    }
    return kAudioHardwareUnsupportedOperationError;
}
static AudioServerPlugInDriverInterface table={NULL,query,retain,release,initialize,create,destroy,client,client,
    configuration,configuration,has_property,is_settable,get_size,get,set,start,stop,timestamp,will,edge,io,edge};
__attribute__((visibility("default"))) void *RK3AudioFactory(CFAllocatorRef allocator,CFUUIDRef type) {
    (void)allocator;if(!type||!CFEqual(type,kAudioServerPlugInTypeUUID))return NULL;
    retain(driver);return driver;
}
