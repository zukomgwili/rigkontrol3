#ifndef RK3_NATIVE_CLIENT_H
#define RK3_NATIVE_CLIENT_H
#include "NativeControl.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

/* Public CoreAudio clients use CFData property-list objects. NativeControl's
 * fixed-width structs are the data INSIDE those objects, never raw RPC bytes.
 * None in AudioServerPlugInCustomPropertyInfo means no data, not opaque data. */
static inline OSStatus rk3_native_copy_blob(AudioObjectID device, UInt32 selector,
    const void *query, size_t query_length, void *output, size_t output_length) {
    if(!output || (query_length && !query) || query_length>(size_t)LONG_MAX || output_length>(size_t)LONG_MAX)return kAudioHardwareIllegalOperationError;
    CFDataRef qualifier=query_length?CFDataCreate(NULL,(const UInt8 *)query,(CFIndex)query_length):NULL;
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
static inline OSStatus rk3_native_copy_spec(AudioObjectID device,
    const RK3NativeSpecQuery *query,RK3NativeSpec *value) {
    if(!query||!value||query->version!=RK3_CONTROL_VERSION||query->reserved||!query->generation)
        return kAudioHardwareIllegalOperationError;
    RK3NativeSpec current;
    OSStatus error=rk3_native_copy_blob(device,RK3_PROPERTY_SPEC,query,sizeof(*query),&current,sizeof(current));
    if(error)return error;
    if(current.version!=RK3_CONTROL_VERSION||current.reserved[0]||current.reserved[1])
        return kAudioHardwareIllegalOperationError;
    if(current.status==0) {
        if(current.generation!=query->generation)return kAudioHardwareIllegalOperationError;
    } else {
        if((current.status!=ENODEV&&current.status!=ESTALE)||current.generation)
            return kAudioHardwareIllegalOperationError;
        for(size_t i=0;i<sizeof(current.raw);++i)if(current.raw[i])return kAudioHardwareIllegalOperationError;
    }
    *value=current;return 0;
}
static inline OSStatus rk3_native_copy_usb_identity(AudioObjectID device,
    const RK3NativeUSBIdentityQuery *query,RK3NativeUSBIdentity *value) {
    if(!query||!value||query->version!=RK3_CONTROL_VERSION||query->reserved||!query->generation)
        return kAudioHardwareIllegalOperationError;
    RK3NativeUSBIdentity current;
    OSStatus error=rk3_native_copy_blob(device,RK3_PROPERTY_USB_IDENTITY,query,sizeof(*query),&current,sizeof(current));
    if(error)return error;
    if(current.version!=RK3_CONTROL_VERSION||current.reserved0||current.reserved[0]||current.reserved[1]||current.reserved[2])
        return kAudioHardwareIllegalOperationError;
    if(current.status==0) {
        if(current.generation!=query->generation||
           (current.attempted_mask&~RK3_USB_IDENTITY_ALL_MASK)||
           (current.defined_mask&~current.attempted_mask)||
           (current.attempted_mask&RK3_USB_IDENTITY_GETTER_MASK)!=RK3_USB_IDENTITY_GETTER_MASK)
            return kAudioHardwareIllegalOperationError;
        const uint32_t scalars[]={current.vendor_id,current.product_id,current.product_index,
            current.manufacturer_index,current.serial_index};
        for(unsigned i=0;i<RK3_USB_IDENTITY_PRODUCT_READER;++i) {
            int defined=(current.defined_mask&(UINT32_C(1)<<i))!=0;
            if(defined!=(current.step_status[i]==0)||(!defined&&scalars[i]))
                return kAudioHardwareIllegalOperationError;
        }
        const uint8_t *strings[]={current.product,current.manufacturer,current.serial};
        for(unsigned i=0;i<3;++i) {
            unsigned step=RK3_USB_IDENTITY_PRODUCT_READER+i;
            int attempted=(current.attempted_mask&(UINT32_C(1)<<step))!=0;
            int defined=(current.defined_mask&(UINT32_C(1)<<step))!=0;
            int eligible=(current.defined_mask&(UINT32_C(1)<<(RK3_USB_IDENTITY_PRODUCT_INDEX+i)))!=0;
            if(attempted!=eligible||(!attempted&&current.step_status[step])||
               (attempted&&current.step_status[step]&&!defined))
                return kAudioHardwareIllegalOperationError;
            if(!defined||current.step_status[step]) {
                for(size_t j=0;j<RK3_USB_IDENTITY_STRING_BYTES;++j)
                    if(strings[i][j])return kAudioHardwareIllegalOperationError;
            } else if(!memchr(strings[i],0,RK3_USB_IDENTITY_STRING_BYTES))
                return kAudioHardwareIllegalOperationError;
        }
    } else {
        if((current.status!=ENODEV&&current.status!=ESTALE)||current.generation)
            return kAudioHardwareIllegalOperationError;
        const uint8_t *body=(const uint8_t *)&current;
        for(size_t i=offsetof(RK3NativeUSBIdentity,attempted_mask);i<sizeof(current);++i)
            if(body[i])return kAudioHardwareIllegalOperationError;
    }
    *value=current;return 0;
}
static inline OSStatus rk3_native_copy_direct_monitor(AudioObjectID device,
    const RK3NativeMonitorQuery *query,RK3NativeMonitorValue *value) {
    if(!query||!value||query->version!=RK3_CONTROL_VERSION||query->reserved||!query->generation)
        return kAudioHardwareIllegalOperationError;
    RK3NativeMonitorValue current;
    OSStatus error=rk3_native_copy_blob(device,RK3_PROPERTY_DIRECT_MONITOR,
        query,sizeof(*query),&current,sizeof(current));
    if(error)return error;
    if(current.version!=RK3_CONTROL_VERSION||current.generation!=query->generation||current.mode>UINT8_MAX)
        return kAudioHardwareIllegalOperationError;
    *value=current;return 0;
}
static inline OSStatus rk3_native_set_direct_monitor(AudioObjectID device,
    const RK3NativeMonitorQuery *query,const RK3NativeMonitorValue *value) {
    if(!query||!value||query->version!=RK3_CONTROL_VERSION||query->reserved||!query->generation||
       value->version!=RK3_CONTROL_VERSION||value->generation!=query->generation)
        return kAudioHardwareIllegalOperationError;
    CFDataRef qualifier=CFDataCreate(NULL,(const UInt8 *)query,sizeof(*query));
    CFDataRef data=CFDataCreate(NULL,(const UInt8 *)value,sizeof(*value));
    if(!qualifier||!data){if(qualifier)CFRelease(qualifier);if(data)CFRelease(data);return kAudioHardwareUnspecifiedError;}
    AudioObjectPropertyAddress address={RK3_PROPERTY_DIRECT_MONITOR,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    OSStatus error=AudioObjectSetPropertyData(device,&address,sizeof(qualifier),&qualifier,sizeof(data),&data);
    CFRelease(data);CFRelease(qualifier);return error;
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
