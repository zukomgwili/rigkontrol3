#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../Driver/NativeControl.h"

static char output[16384],errors[1024];
static unsigned queries;
static unsigned info_reads,identity_reads,spec_reads;
static uint64_t expected_generation=7;
static RK3NativeInfo before_info,after_info;
static RK3NativeUSBIdentity identity;
static RK3NativeSpec spec;
static UInt32 devices_size,uid_size,blob_size_selector;
static unsigned uid_mode;
static OSStatus devices_error,uid_error,before_error,after_error,identity_error,spec_error;
static UInt32 malformed_selector;
static int malformed_kind;
static CFTypeRef held[4096];static size_t held_count;
static void hold(CFTypeRef object) {
    assert(object&&held_count<sizeof(held)/sizeof(held[0]));held[held_count++]=CFRetain(object);
}
static CFStringRef string(const char *text) {
    CFMutableStringRef value=CFStringCreateMutable(NULL,0);assert(value);
    CFStringAppendCString(value,text,kCFStringEncodingUTF8);return value;
}
static void balanced(void) {
    for(size_t i=0;i<held_count;++i)assert(CFGetRetainCount(held[i])==1);
}
static void reset(void) {
    balanced();for(size_t i=0;i<held_count;++i)CFRelease(held[i]);held_count=0;
    output[0]=errors[0]=0;queries=info_reads=identity_reads=spec_reads=0;expected_generation=7;
    devices_size=2*sizeof(AudioObjectID);uid_size=sizeof(CFStringRef);uid_mode=0;blob_size_selector=0;
    devices_error=uid_error=before_error=after_error=identity_error=spec_error=0;malformed_selector=0;malformed_kind=0;
    before_info=(RK3NativeInfo){.version=1,.flags=RK3_NATIVE_FAILED,.generation=7,.last_error=-123};
    memcpy(before_info.device_uid,RK3_NATIVE_DEVICE_UID,sizeof(RK3_NATIVE_DEVICE_UID));
    memcpy(before_info.product_name,"Context only",13);after_info=before_info;
    identity=(RK3NativeUSBIdentity){.version=1,.generation=7,.attempted_mask=0xff,.defined_mask=0xff,
        .vendor_id=0x1234,.product_id=0xabcd,.product_index=1,.manufacturer_index=2,.serial_index=0,
        .product={0x50},.manufacturer={0xed,0xa0,0x80},.serial={0x53}};
    spec=(RK3NativeSpec){.version=1,.generation=7,.raw={1,2,3,4,5,6,7,8,9,10,11,12,13,14}};
}
static int capture(FILE *stream,const char *format,va_list args) {
    char *buffer=stream==stderr?errors:output;
    size_t capacity=stream==stderr?sizeof(errors):sizeof(output);
    size_t used=strlen(buffer);assert(used<capacity);
    int result=vsnprintf(buffer+used,capacity-used,format,args);
    assert(result>=0&&(size_t)result<capacity-used);return result;
}
static int test_printf(const char *format,...) {
    va_list args;va_start(args,format);int result=capture(stdout,format,args);va_end(args);return result;
}
static int test_fprintf(FILE *stream,const char *format,...) {
    va_list args;va_start(args,format);int result=capture(stream,format,args);va_end(args);return result;
}
static OSStatus test_get(AudioObjectID device,const AudioObjectPropertyAddress *address,
    UInt32 qualifier_size,const void *qualifier,UInt32 *size,void *data) {
    ++queries;assert(address->mScope==kAudioObjectPropertyScopeGlobal&&address->mElement==kAudioObjectPropertyElementMain);
    if(address->mSelector==kAudioHardwarePropertyDevices) {
        const AudioObjectID devices[]={10,11};assert(device==kAudioObjectSystemObject&&*size>=sizeof(devices));
        assert(!qualifier_size&&!qualifier);memcpy(data,devices,sizeof(devices));
        if(devices_size==1024*sizeof(AudioObjectID)) {
            AudioObjectID *ids=data;for(unsigned i=0;i<1024;++i)ids[i]=100+i;ids[1023]=11;
        }
        *size=devices_size;return devices_error;
    }
    if(address->mSelector==kAudioDevicePropertyDeviceUID) {
        assert((device==10||device==11||device>=100)&&*size==sizeof(CFStringRef)&&!qualifier_size&&!qualifier);
        *size=uid_size;if(uid_error)return uid_error;
        if(uid_mode==3)*(CFStringRef *)data=NULL;
        else if(uid_mode==4)*(CFPropertyListRef *)data=CFDataCreateMutable(NULL,0);
        else *(CFStringRef *)data=string(uid_mode==1?"unrelated":uid_mode==2?RK3_NATIVE_DEVICE_UID:
            device!=11?"unrelated":RK3_NATIVE_DEVICE_UID);
        if(*(CFTypeRef *)data)hold(*(CFTypeRef *)data);
        return 0;
    }
    assert(device==11&&*size==sizeof(CFPropertyListRef));
    const void *bytes=NULL;size_t length=0;
    if(address->mSelector==RK3_PROPERTY_INFO) {
        assert(!qualifier_size&&!qualifier&&info_reads<2);
        OSStatus error=info_reads?after_error:before_error;
        bytes=info_reads++?&after_info:&before_info;length=sizeof(before_info);if(error)return error;
    } else {
        assert(qualifier_size==sizeof(CFDataRef)&&qualifier);
        CFDataRef q=*(const CFDataRef *)qualifier;assert(q&&CFGetTypeID(q)==CFDataGetTypeID()&&CFDataGetLength(q)==16);
        hold(q);
        RK3NativeUSBIdentityQuery value;memcpy(&value,CFDataGetBytePtr(q),sizeof(value));
        assert(value.version==1&&!value.reserved&&value.generation==expected_generation);
        if(address->mSelector==RK3_PROPERTY_USB_IDENTITY) {++identity_reads;if(identity_error)return identity_error;bytes=&identity;length=sizeof(identity);}
        else {assert(address->mSelector==RK3_PROPERTY_SPEC);++spec_reads;if(spec_error)return spec_error;bytes=&spec;length=sizeof(spec);}
    }
    if(address->mSelector==malformed_selector) {
        if(malformed_kind==1)--length;
        else if(malformed_kind==2){*(CFStringRef *)data=string("wrong type");hold(*(CFTypeRef *)data);return 0;}
        else if(malformed_kind==3){*(CFDataRef *)data=NULL;return 0;}
        else if(malformed_kind==4) {
            uint8_t longer[sizeof(RK3NativeUSBIdentity)+1]={0};assert(length<sizeof(longer));memcpy(longer,bytes,length);
            *(CFDataRef *)data=CFDataCreate(NULL,longer,(CFIndex)length+1);hold(*(CFTypeRef *)data);return 0;
        }
    }
    if(address->mSelector==blob_size_selector)--*size;
    *(CFDataRef *)data=CFDataCreate(NULL,bytes,(CFIndex)length);hold(*(CFTypeRef *)data);return 0;
}
#define AudioObjectGetPropertyData test_get
#define printf test_printf
#define fprintf test_fprintf
#define main identity_readback_main
#include "../rk3-identity-readback.c"
#undef main
#undef fprintf
#undef printf
#undef AudioObjectGetPropertyData

static char *plain[]={"rk3-identity-readback"};
static void run(int argc,char **argv,int result) {
    output[0]=errors[0]=0;assert(identity_readback_main(argc,argv)==result);balanced();
    if(result!=2) {
        assert(output[0]=='{'&&output[strlen(output)-1]=='\n');
        fputs(output,stdout);
    }
}
static void error_cases(void) {
    reset();before_info.generation=after_info.generation=0;run(1,plain,3);
    assert(info_reads==1&&!identity_reads&&!spec_reads&&strstr(output,"\"query_generation\":null"));
    assert(strstr(output,"\"identity\":{\"state\":\"NOTQUERIED\",\"os_status\":null,\"native\":null}"));
    reset();identity=(RK3NativeUSBIdentity){.version=1,.status=ENODEV};run(1,plain,3);
    assert(strstr(output,"\"payload_valid\":false,\"payload\":null")&&strstr(output,"\"raw_hex\":\"010203"));
    reset();spec=(RK3NativeSpec){.version=1,.status=ENODEV};run(1,plain,3);
    assert(strstr(output,"\"payload_valid\":false,\"raw_hex\":null")&&strstr(output,"\"vendor_id\":4660"));
    reset();expected_generation=6;identity=(RK3NativeUSBIdentity){.version=1,.status=ESTALE};spec=(RK3NativeSpec){.version=1,.status=ESTALE};
    char *old[]={"rk3-identity-readback","--generation","6"};run(3,old,3);
    assert(info_reads==2&&identity_reads==1&&spec_reads==1&&strstr(output,"\"query_generation\":6"));
    reset();expected_generation=6;identity.generation=spec.generation=6;run(3,old,1);
    assert(strstr(output,"\"aggregate\":\"INCOHERENT\""));
    reset();expected_generation=6;identity.generation=6;spec=(RK3NativeSpec){.version=1,.status=ESTALE};
    int mixed_first=identity_readback_main(3,old);bool first_coherent_error=strstr(output,"\"aggregate\":\"INCOHERENT\"")!=NULL;
    fputs(output,stdout);
    reset();expected_generation=6;identity=(RK3NativeUSBIdentity){.version=1,.status=ENODEV};spec.generation=6;
    int mixed_second=identity_readback_main(3,old);bool second_coherent_error=strstr(output,"\"aggregate\":\"INCOHERENT\"")!=NULL;
    fputs(output,stdout);balanced();
    assert(mixed_first==1&&mixed_second==1&&first_coherent_error&&second_coherent_error);
    reset();after_info.generation=8;identity=(RK3NativeUSBIdentity){.version=1,.status=ENODEV};run(1,plain,1);
    assert(strstr(output,"\"aggregate\":\"INCOHERENT\""));
    reset();identity_error=-99;run(1,plain,1);
    assert(info_reads==2&&spec_reads==1&&strstr(output,"\"identity\":{\"state\":\"INVALID\",\"os_status\":-99,\"native\":null}"));
    reset();spec_error=-98;run(1,plain,1);assert(strstr(output,"\"os_status\":-98,\"native\":null"));
    reset();before_error=-97;run(1,plain,1);assert(info_reads==1&&!identity_reads&&!spec_reads);
    reset();after_error=-96;run(1,plain,1);assert(strstr(output,"\"os_status\":-96,\"value\":null"));
    for(unsigned i=0;i<3;++i) {
        reset();if(i==0)before_info.version=2;
        if(i==1)memset(before_info.device_uid,'x',sizeof(before_info.device_uid));
        if(i==2)memset(before_info.product_name,'x',sizeof(before_info.product_name));
        run(1,plain,1);assert(info_reads==1&&!identity_reads&&!spec_reads&&strstr(output,"\"value\":null"));
    }
    reset();after_info.version=2;run(1,plain,1);assert(strstr(output,"\"aggregate\":\"INVALID\""));
    for(unsigned i=0;i<3;++i) {
        reset();malformed_selector=i==0?RK3_PROPERTY_INFO:i==1?RK3_PROPERTY_USB_IDENTITY:RK3_PROPERTY_SPEC;
        malformed_kind=1;run(1,plain,1);assert(strstr(output,"\"state\":\"INVALID\""));
    }
    for(int i=2;i<=4;++i) {
        reset();malformed_selector=RK3_PROPERTY_USB_IDENTITY;malformed_kind=i;run(1,plain,1);
        assert(strstr(output,"\"identity\":{\"state\":\"INVALID\""));
    }
    reset();blob_size_selector=RK3_PROPERTY_SPEC;run(1,plain,1);assert(strstr(output,"\"spec\":{\"state\":\"INVALID\""));
    reset();identity.status=ENODEV;run(1,plain,1);assert(strstr(output,"\"identity\":{\"state\":\"INVALID\""));
    reset();spec.version=2;run(1,plain,1);assert(strstr(output,"\"spec\":{\"state\":\"INVALID\""));
    reset();identity.attempted_mask|=0x100;run(1,plain,1);assert(strstr(output,"\"identity\":{\"state\":\"INVALID\""));
    reset();identity.generation=8;run(1,plain,1);assert(strstr(output,"\"identity\":{\"state\":\"INVALID\""));
    reset();spec.generation=8;run(1,plain,1);assert(strstr(output,"\"spec\":{\"state\":\"INVALID\""));
    reset();identity=(RK3NativeUSBIdentity){.version=1,.status=EINVAL};run(1,plain,1);
    assert(strstr(output,"\"identity\":{\"state\":\"INVALID\""));
    reset();memset(identity.product,0xff,31);run(1,plain,1);assert(strstr(output,"\"identity\":{\"state\":\"INVALID\""));
}
static void discovery_cases(void) {
    reset();devices_error=-91;run(1,plain,1);assert(queries==1&&!info_reads&&strstr(output,"\"device\":null"));
    const UInt32 bad_sizes[]={0,1,1024*sizeof(AudioObjectID)+sizeof(AudioObjectID)};
    for(unsigned i=0;i<3;++i) {reset();devices_size=bad_sizes[i];run(1,plain,1);assert(queries==1&&!info_reads);}
    for(unsigned i=1;i<=4;++i){reset();uid_mode=i;run(1,plain,1);assert(!info_reads&&!identity_reads&&!spec_reads);}
    reset();uid_error=-90;run(1,plain,1);assert(queries==2&&!info_reads);
    reset();uid_size=sizeof(CFStringRef)-1;run(1,plain,1);assert(queries==2&&!info_reads);
    reset();devices_size=1024*sizeof(AudioObjectID);run(1,plain,0);assert(queries==1029&&info_reads==2);
}
static void partial_cases(void) {
    reset();identity.vendor_id=0;identity.step_status[0]=-17;
    identity.manufacturer_index=0;identity.step_status[3]=-8;
    identity.step_status[5]=-12345;memset(identity.product,0,31);
    memset(identity.manufacturer,0,31);memset(identity.serial,0,31);
    identity.attempted_mask=0xbf;identity.defined_mask=0x36;run(1,plain,0);
    assert(strstr(output,"\"partial_identity\":true")&&strstr(output,"\"vendor_id\":null"));
    assert(strstr(output,"\"manufacturer_hex\":null,\"serial_hex\":null"));
    assert(strstr(output,"\"0xffffffef\"")&&strstr(output,"\"0xffffcfc7\""));
    assert(strstr(output,"\"product_hex\":\"00000000000000000000000000000000000000000000000000000000000000\""));
    reset();identity.vendor_id=identity.product_id=0;identity.product_index=identity.manufacturer_index=identity.serial_index=0;
    memset(identity.product,0,31);memset(identity.manufacturer,0,31);memset(identity.serial,0,31);memset(spec.raw,0,14);
    run(1,plain,0);assert(strstr(output,"\"vendor_id\":0,\"product_id\":0")&&strstr(output,"\"partial_identity\":false"));
    reset();memset(identity.product,0xab,30);identity.product[30]=0;run(1,plain,0);
    assert(strstr(output,"\"product_hex\":\"abababababababababababababababababababababababababababababab00\""));
    reset();before_info.flags=after_info.flags=0;run(1,plain,0);assert(strstr(output,"\"flags\":0"));
    reset();before_info.flags=after_info.flags=RK3_NATIVE_AUDIO_RUNNING;run(1,plain,0);
    assert(strstr(output,"\"flags\":2"));
}

int main(void) {
    (void)test_get;(void)test_printf;(void)test_fprintf;
    char *args[]={"rk3-identity-readback","--help"};
    assert(identity_readback_main(2,args)==0);
    assert(queries==0&&strstr(errors,"Usage:"));
    reset();
    assert(identity_readback_main(1,plain)==0);
    assert(queries==7&&info_reads==2&&identity_reads==1&&spec_reads==1);
    assert(strstr(output,"\"aggregate\":\"AVAILABLE\"")&&strstr(output,"\"query_generation\":7"));
    assert(strstr(output,"\"vendor_id\":4660")&&strstr(output,"\"product_id\":43981"));
    assert(strstr(output,"\"manufacturer_hex\":\"eda08000000000000000000000000000000000000000000000000000000000\""));
    assert(strstr(output,"\"raw_hex\":\"0102030405060708090a0b0c0d0e\""));
    reset();expected_generation=UINT64_MAX;before_info.generation=after_info.generation=UINT64_MAX;
    identity.generation=spec.generation=UINT64_MAX;
    char *maximum[]={"rk3-identity-readback","--generation","18446744073709551615"};
    assert(identity_readback_main(3,maximum)==0);
    assert(strstr(output,"\"query_generation\":18446744073709551615"));
    const char *bad[]={"0","-1","+1"," 1","1 ","1x","18446744073709551616","", "0x10"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        reset();char *invalid[]={"rk3-identity-readback","--generation",(char *)bad[i]};
        assert(identity_readback_main(3,invalid)==2&&queries==0&&output[0]==0);
    }
    reset();char *missing[]={"rk3-identity-readback","--generation"};assert(identity_readback_main(2,missing)==2&&queries==0);
    reset();char *extra[]={"rk3-identity-readback","--help","extra"};assert(identity_readback_main(3,extra)==2&&queries==0);
    reset();char *unknown[]={"rk3-identity-readback","--watch"};assert(identity_readback_main(2,unknown)==2&&queries==0);
    reset();char *duplicate[]={"rk3-identity-readback","--generation","7","--generation","7"};
    assert(identity_readback_main(5,duplicate)==2&&queries==0);
    discovery_cases();error_cases();partial_cases();
    reset();
    puts("Identity readback CLI tests passed");return 0;
}
