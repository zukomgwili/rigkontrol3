/* One-shot installed-driver metadata diagnostic; no device or audio writes. */
#include "../Driver/NativeClient.h"
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

typedef struct { bool queried,valid;OSStatus error;RK3NativeInfo value; } InfoRead;
typedef struct { bool queried,valid;OSStatus error;RK3NativeUSBIdentity value; } IdentityRead;
typedef struct { bool queried,valid;OSStatus error;RK3NativeSpec value; } SpecRead;

static OSStatus find_device(AudioObjectID *selected) {
    AudioObjectPropertyAddress address={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioObjectID devices[1024];UInt32 size=sizeof(devices);
    OSStatus error=AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,devices);
    if(error)return error;
    if(size>sizeof(devices)||size%sizeof(devices[0]))return kAudioHardwareBadPropertySizeError;
    unsigned matches=0;
    for(UInt32 i=0;i<size/sizeof(devices[0]);++i) {
        address.mSelector=kAudioDevicePropertyDeviceUID;
        CFStringRef uid=NULL;UInt32 bytes=sizeof(uid);
        error=AudioObjectGetPropertyData(devices[i],&address,0,NULL,&bytes,&uid);
        if(!error&&(bytes!=sizeof(uid)||!uid||CFGetTypeID(uid)!=CFStringGetTypeID()))
            error=kAudioHardwareBadPropertySizeError;
        if(!error&&CFEqual(uid,CFSTR(RK3_NATIVE_DEVICE_UID))) {*selected=devices[i];++matches;}
        if(uid)CFRelease(uid);
        if(error)return error;
    }
    return matches==1?0:kAudioHardwareBadDeviceError;
}
static InfoRead read_info(AudioObjectID device) {
    InfoRead result={.queried=true};RK3NativeInfo value;
    result.error=rk3_native_copy_info(device,&value);
    if(!result.error&&(value.version!=RK3_CONTROL_VERSION||
       !memchr(value.device_uid,0,sizeof(value.device_uid))||!memchr(value.product_name,0,sizeof(value.product_name))))
        result.error=kAudioHardwareIllegalOperationError;
    result.valid=result.error==0;if(result.valid)result.value=value;return result;
}
static void hex(const uint8_t *bytes,size_t count) {
    printf("\"");for(size_t i=0;i<count;++i)printf("%02x",(unsigned)bytes[i]);printf("\"");
}
static void call_state(bool queried,bool valid,OSStatus error) {
    printf("{\"state\":\"%s\",\"os_status\":",!queried?"NOTQUERIED":valid?"VALID":"INVALID");
    if(queried)printf("%" PRId32,(int32_t)error);else printf("null");
}
static void print_info(const InfoRead *result) {
    call_state(result->queried,result->valid,result->error);printf(",\"value\":");
    if(!result->valid)printf("null");
    else printf("{\"flags\":%" PRIu32 ",\"generation\":%" PRIu64 ",\"last_error\":%" PRId32 "}",
        result->value.flags,result->value.generation,result->value.last_error);
    printf("}");
}
static void print_identity(const IdentityRead *result) {
    call_state(result->queried,result->valid,result->error);printf(",\"native\":");
    if(!result->valid)printf("null");
    else {
        const RK3NativeUSBIdentity *v=&result->value;
        printf("{\"status\":%" PRId32 ",\"generation\":%" PRIu64 ",\"payload_valid\":%s,\"payload\":",
            v->status,v->generation,v->status?"false":"true");
        if(v->status)printf("null");
        else {
            printf("{\"attempted_mask\":\"0x%08" PRIx32 "\",\"defined_mask\":\"0x%08" PRIx32 "\",\"step_status_hex\":[",v->attempted_mask,v->defined_mask);
            for(unsigned i=0;i<RK3_USB_IDENTITY_STEP_COUNT;++i)printf("%s\"0x%08" PRIx32 "\"",i?",":"",(uint32_t)v->step_status[i]);
            const uint32_t values[]={v->vendor_id,v->product_id,v->product_index,v->manufacturer_index,v->serial_index};
            const char *keys[]={"vendor_id","product_id","product_index","manufacturer_index","serial_index"};
            printf("]");
            for(unsigned i=0;i<5;++i) {
                printf(",\"%s\":",keys[i]);if(v->defined_mask&(1u<<i))printf("%" PRIu32,values[i]);else printf("null");
            }
            const uint8_t *strings[]={v->product,v->manufacturer,v->serial};
            const char *names[]={"product_hex","manufacturer_hex","serial_hex"};
            for(unsigned i=0;i<3;++i) {
                printf(",\"%s\":",names[i]);
                if(v->defined_mask&(1u<<(RK3_USB_IDENTITY_PRODUCT_READER+i)))hex(strings[i],RK3_USB_IDENTITY_STRING_BYTES);else printf("null");
            }
            printf("}");
        }
        printf("}");
    }
    printf("}");
}
static void print_spec(const SpecRead *result) {
    call_state(result->queried,result->valid,result->error);printf(",\"native\":");
    if(!result->valid)printf("null");
    else {
        const RK3NativeSpec *v=&result->value;
        printf("{\"status\":%" PRId32 ",\"generation\":%" PRIu64 ",\"payload_valid\":%s,\"raw_hex\":",v->status,v->generation,v->status?"false":"true");
        if(v->status)printf("null");else hex(v->raw,sizeof(v->raw));printf("}");
    }
    printf("}");
}
static bool parse_generation(const char *text,uint64_t *value) {
    if(!text||!*text)return false;
    uint64_t number=0;
    for(const char *p=text;*p;++p) {
        if(*p<'0'||*p>'9')return false;
        unsigned digit=(unsigned)(*p-'0');
        if(number>(UINT64_MAX-digit)/10)return false;
        number=number*10+digit;
    }
    if(!number)return false;
    *value=number;return true;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--help")) {
        fprintf(stderr,"Usage: %s [--generation U64]\n",argv[0]);return 0;
    }
    uint64_t generation=0;bool explicit_generation=argc==3&&!strcmp(argv[1],"--generation");
    if((argc!=1&&!explicit_generation)||(explicit_generation&&!parse_generation(argv[2],&generation))) {
        fprintf(stderr,"Usage: %s [--generation U64]\n",argv[0]);return 2;
    }
    AudioObjectID device=0;OSStatus discovery=find_device(&device);
    InfoRead before={0},after={0};IdentityRead identity={0};SpecRead spec={0};
    const char *aggregate="INVALID";int result=1;
    if(!discovery) {
        before=read_info(device);
        if(before.valid&&!explicit_generation)generation=before.value.generation;
        if(before.valid&&!generation){aggregate="UNAVAILABLE";result=3;}
        if(before.valid&&generation) {
            RK3NativeUSBIdentityQuery iq={RK3_CONTROL_VERSION,0,generation};
            identity.queried=true;identity.error=rk3_native_copy_usb_identity(device,&iq,&identity.value);identity.valid=identity.error==0;
            RK3NativeSpecQuery sq={RK3_CONTROL_VERSION,0,generation};
            spec.queried=true;spec.error=rk3_native_copy_spec(device,&sq,&spec.value);spec.valid=spec.error==0;
            after=read_info(device);
            if(after.valid&&before.value.generation!=after.value.generation)aggregate="INCOHERENT";
            else if(after.valid&&before.value.generation!=generation&&
                ((identity.valid&&!identity.value.status)||(spec.valid&&!spec.value.status)))aggregate="INCOHERENT";
            else if(after.valid&&identity.valid&&spec.valid) {
                if(identity.value.status||spec.value.status){aggregate="UNAVAILABLE";result=3;}
                else {aggregate="AVAILABLE";result=0;}
            }
        }
    }
    printf("{\"discovery\":{\"os_status\":%" PRId32 ",\"device\":",(int32_t)discovery);
    if(discovery)printf("null");else printf("%" PRIu32,device);
    printf("},\"query_generation\":");if(generation)printf("%" PRIu64,generation);else printf("null");
    printf(",\"before\":");print_info(&before);printf(",\"identity\":");print_identity(&identity);
    printf(",\"spec\":");print_spec(&spec);printf(",\"after\":");print_info(&after);
    printf(",\"aggregate\":\"%s\",\"partial_identity\":",aggregate);
    if(!identity.valid||identity.value.status)printf("null");
    else {
        bool partial=identity.value.defined_mask!=RK3_USB_IDENTITY_ALL_MASK;
        for(unsigned i=0;i<RK3_USB_IDENTITY_STEP_COUNT;++i)if(identity.value.step_status[i])partial=true;
        printf("%s",partial?"true":"false");
    }
    printf("}\n");return result;
}
