/* Installed-driver diagnostic only: public CoreAudio CFData properties.
 * Never initializes another bundle, claims USB, starts audio or changes SIP. */
#include "../Driver/NativeClient.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *name) {
    fprintf(stderr,"Usage: %s get CHANNEL | set CHANNEL U32_BITS\n",name);
    fprintf(stderr,"CHANNEL is zero-based; U32_BITS is the raw Float32 pattern (for example 0x3f800000 = unity).\n");
}
static bool number(const char *text,int base,uint32_t *value) {
    if(!text||!*text||*text=='-')return false;
    char *end=NULL;errno=0;unsigned long long parsed=strtoull(text,&end,base);
    if(errno||!end||*end||parsed>UINT32_MAX)return false;
    *value=(uint32_t)parsed;return true;
}
static bool find_device(AudioObjectID *device,RK3NativeInfo *information) {
    AudioObjectPropertyAddress address={kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioObjectID ids[1024];UInt32 size=sizeof(ids);
    OSStatus error=AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,ids);
    if(error||size>sizeof(ids)||size%sizeof(ids[0]))return false;
    unsigned matches=0;
    for(UInt32 i=0;i<size/sizeof(ids[0]);++i) {
        RK3NativeInfo candidate;
        if(rk3_native_copy_info(ids[i],&candidate)||candidate.version!=RK3_CONTROL_VERSION||
           !(candidate.flags&RK3_NATIVE_CONNECTED)||!candidate.generation||
           memcmp(candidate.device_uid,RK3_NATIVE_DEVICE_UID,sizeof(RK3_NATIVE_DEVICE_UID)))continue;
        *device=ids[i];*information=candidate;++matches;
    }
    return matches==1;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--help")){usage(argv[0]);return 0;}
    bool setting=argc==4&&!strcmp(argv[1],"set");
    bool getting=argc==3&&!strcmp(argv[1],"get");
    uint32_t channel=0,bits=0;
    if((!setting&&!getting)||!number(argv[2],10,&channel)||(setting&&!number(argv[3],0,&bits))) {
        usage(argv[0]);return 2;
    }
    AudioObjectID device=0;RK3NativeInfo information;
    if(!find_device(&device,&information)) {
        fprintf(stderr,"Expected exactly one connected native RK3 audio device.\n");return 1;
    }
    RK3NativeGainQuery query={RK3_CONTROL_VERSION,channel,information.generation};
    RK3NativeGainValue value;
    OSStatus error=rk3_native_copy_input_gain(device,&query,&value);
    if(!error&&setting) {
        value.value_bits=bits;
        error=rk3_native_set_input_gain(device,&query,&value);
        if(!error)error=rk3_native_copy_input_gain(device,&query,&value);
        if(!error&&value.value_bits!=bits)error=kAudioHardwareUnspecifiedError;
    }
    if(error) {
        fprintf(stderr,"Input gain property failed: %" PRId32 ". No retry or USB command was issued.\n",(int32_t)error);return 1;
    }
    printf("{\"channel\":%" PRIu32 ",\"generation\":%" PRIu64 ",\"value_bits\":\"0x%08" PRIx32 "\"}\n",
        value.channel,value.generation,value.value_bits);
    return 0;
}
