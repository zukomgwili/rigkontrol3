#ifndef RK3_NATIVE_CLIENT_H
#define RK3_NATIVE_CLIENT_H
#include "NativeControl.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>
#include <limits.h>

/* Public CoreAudio clients use CFData property-list objects. NativeControl's
 * fixed-width structs are the data INSIDE those objects, never raw RPC bytes.
 * None in AudioServerPlugInCustomPropertyInfo means no data, not opaque data. */
static inline OSStatus rk3_native_copy_blob(AudioObjectID device, UInt32 selector,
    const void *query, size_t query_length, void *output, size_t output_length) {
    if(!output || (query_length && !query) || query_length>(size_t)LONG_MAX || output_length>(size_t)LONG_MAX)return kAudioHardwareIllegalOperationError;
    CFDataRef qualifier=query_length?CFDataCreate(NULL,query,(CFIndex)query_length):NULL;
    if(query_length&&!qualifier)return kAudioHardwareUnspecifiedError;
    CFPropertyListRef object=NULL;
    UInt32 size=sizeof(object);
    AudioObjectPropertyAddress address={selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    OSStatus error=AudioObjectGetPropertyData(device,&address,qualifier?sizeof(qualifier):0,
        qualifier?&qualifier:NULL,&size,&object);
    if(qualifier)CFRelease(qualifier);
    if(error==0 && (size!=sizeof(object)||!object||CFGetTypeID(object)!=CFDataGetTypeID()||
        CFDataGetLength((CFDataRef)object)!=(CFIndex)output_length))error=kAudioHardwareBadPropertySizeError;
    if(error==0)memcpy(output,CFDataGetBytePtr((CFDataRef)object),output_length);
    if(object)CFRelease(object);
    return error;
}
static inline OSStatus rk3_native_copy_input_gain(AudioObjectID device,
    const RK3NativeGainQuery *query,RK3NativeGainValue *value) {
    if(!query||!value)return kAudioHardwareIllegalOperationError;
    RK3NativeGainValue current;
    OSStatus error=rk3_native_copy_blob(device,RK3_PROPERTY_INPUT_GAIN,query,sizeof(*query),&current,sizeof(current));
    if(error)return error;
    if(current.version!=RK3_CONTROL_VERSION||current.reserved||current.channel!=query->channel||
       current.generation!=query->generation)return kAudioHardwareIllegalOperationError;
    *value=current;return 0;
}
static inline OSStatus rk3_native_copy_direct_monitor(AudioObjectID device,
    const RK3NativeMonitorQuery *query,RK3NativeMonitorValue *value) {
    if(!query||!value)return kAudioHardwareIllegalOperationError;
    RK3NativeMonitorValue current;
    OSStatus error=rk3_native_copy_blob(device,RK3_PROPERTY_DIRECT_MONITOR,query,sizeof(*query),&current,sizeof(current));
    if(error)return error;
    if(current.version!=RK3_CONTROL_VERSION||current.generation!=query->generation||
       current.value>255||current.reply_received>1||(!current.reply_received&&current.value!=255))
        return kAudioHardwareIllegalOperationError;
    *value=current;return 0;
}
static inline OSStatus rk3_native_set_direct_monitor(AudioObjectID device,
    const RK3NativeMonitorQuery *query,const RK3NativeMonitorValue *value) {
    if(!query||!value)return kAudioHardwareIllegalOperationError;
    CFDataRef qualifier=CFDataCreate(NULL,(const UInt8 *)query,sizeof(*query));
    CFDataRef data=CFDataCreate(NULL,(const UInt8 *)value,sizeof(*value));
    if(!qualifier||!data){if(qualifier)CFRelease(qualifier);if(data)CFRelease(data);return kAudioHardwareUnspecifiedError;}
    AudioObjectPropertyAddress address={RK3_PROPERTY_DIRECT_MONITOR,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    OSStatus error=AudioObjectSetPropertyData(device,&address,sizeof(qualifier),&qualifier,sizeof(data),&data);
    CFRelease(data);CFRelease(qualifier);return error;
}
static inline OSStatus rk3_native_set_input_gain(AudioObjectID device,
    const RK3NativeGainQuery *query,const RK3NativeGainValue *value) {
    if(!query||!value)return kAudioHardwareIllegalOperationError;
    CFDataRef qualifier=CFDataCreate(NULL,(const UInt8 *)query,sizeof(*query));
    CFDataRef data=CFDataCreate(NULL,(const UInt8 *)value,sizeof(*value));
    if(!qualifier||!data){if(qualifier)CFRelease(qualifier);if(data)CFRelease(data);return kAudioHardwareUnspecifiedError;}
    AudioObjectPropertyAddress address={RK3_PROPERTY_INPUT_GAIN,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    OSStatus error=AudioObjectSetPropertyData(device,&address,sizeof(qualifier),&qualifier,sizeof(data),&data);
    CFRelease(data);CFRelease(qualifier);return error;
}
static inline OSStatus rk3_native_copy_info(AudioObjectID device,RK3NativeInfo *info) {
    return rk3_native_copy_blob(device,RK3_PROPERTY_INFO,NULL,0,info,sizeof(*info));
}
static inline OSStatus rk3_native_copy_reports(AudioObjectID device,
    const RK3NativeReportQuery *query,RK3NativeReports *reports) {
    if(!query)return kAudioHardwareIllegalOperationError;
    return rk3_native_copy_blob(device,RK3_PROPERTY_REPORTS,query,sizeof(*query),reports,sizeof(*reports));
}
static inline OSStatus rk3_native_send_command(AudioObjectID device,
    const RK3NativeCommandLease *lease,const RK3NativeCommand *command) {
    if(!lease||!command)return kAudioHardwareIllegalOperationError;
    CFDataRef qualifier=CFDataCreate(NULL,(const UInt8 *)lease,sizeof(*lease));
    CFDataRef data=CFDataCreate(NULL,(const UInt8 *)command,sizeof(*command));
    if(!qualifier||!data){if(qualifier)CFRelease(qualifier);if(data)CFRelease(data);return kAudioHardwareUnspecifiedError;}
    AudioObjectPropertyAddress address={RK3_PROPERTY_COMMAND,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    OSStatus error=AudioObjectSetPropertyData(device,&address,sizeof(qualifier),&qualifier,sizeof(data),&data);
    CFRelease(data);CFRelease(qualifier);return error;
}
#endif
