/* Original direct-monitor operations through the sole installed USB owner.
 * No USB claim, audio start, automatic readback, retries or routing labels. */
#include "../Driver/NativeClient.h"
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const char *name) {
    fprintf(stderr,"Usage: %s get | set U32_MODE\n",name);
    fprintf(stderr,"SET sends the low byte once; success does not prove hardware routing changed.\n");
    fprintf(stderr,"GET sends one query; 0xff may be NI's timeout value or an actual reply.\n");
    fprintf(stderr,"Raw modes have no verified routing labels. No automatic query, persistence or replay.\n");
}
static bool parse_mode(const char *text,uint32_t *value) {
    if(!text||!*text||*text=='-')return false;
    char *end=NULL;errno=0;unsigned long long parsed=strtoull(text,&end,0);
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
    bool getting=argc==2&&!strcmp(argv[1],"get");
    bool setting=argc==3&&!strcmp(argv[1],"set");uint32_t mode=0;
    if((!getting&&!setting)||(setting&&!parse_mode(argv[2],&mode))){usage(argv[0]);return 2;}
    AudioObjectID device=0;RK3NativeInfo information;
    if(!find_device(&device,&information)) {
        fprintf(stderr,"Expected exactly one connected native RK3 audio device.\n");return 1;
    }
    RK3NativeMonitorQuery query={RK3_CONTROL_VERSION,0,information.generation};
    RK3NativeMonitorValue value={RK3_CONTROL_VERSION,mode,information.generation};
    OSStatus error=setting?rk3_native_set_direct_monitor(device,&query,&value):
        rk3_native_copy_direct_monitor(device,&query,&value);
    if(error){fprintf(stderr,"Direct monitor property failed: %" PRId32 ". No retry.\n",(int32_t)error);return 1;}
    if(setting)printf("{\"set_mode_byte\":%u,\"generation\":%" PRIu64 ",\"hardware_readback_performed\":false}\n",
        (unsigned)(uint8_t)mode,information.generation);
    else printf("{\"mode\":%" PRIu32 ",\"generation\":%" PRIu64 "}\n",value.mode,value.generation);
    return 0;
}
