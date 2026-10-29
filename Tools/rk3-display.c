/* Original indexed display bytes through the native Controller mailbox. */
#include "../Driver/NativeClient.h"
#include "../Driver/Display.h"
#include <CoreMIDI/CoreMIDI.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;
}
static bool pause_ms(long ms) {
    struct timespec t={ms/1000,ms%1000*1000000};
    while(nanosleep(&t,&t))if(errno!=EINTR)return false;
    return true;
}
static AudioObjectPropertyAddress address(UInt32 selector) {
    return (AudioObjectPropertyAddress){selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
}
static AudioObjectID audio_device(void) {
    AudioObjectPropertyAddress a=address(kAudioHardwarePropertyDevices);UInt32 size=0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,NULL,&size)||
       size>65536||size%sizeof(AudioObjectID))return 0;
    UInt32 capacity=size;AudioObjectID *devices=malloc(size?size:1);if(!devices)return 0;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&size,devices)||
       size>capacity||size%sizeof(*devices)) {free(devices);return 0;}
    AudioObjectID found=0;bool duplicate=false;
    for(UInt32 i=0;i<size/sizeof(*devices);++i) {
        a=address(kAudioDevicePropertyDeviceUID);CFStringRef uid=NULL;UInt32 bytes=sizeof(uid);
        if(!AudioObjectGetPropertyData(devices[i],&a,0,NULL,&bytes,&uid)&&uid) {
            if(bytes==sizeof(uid)&&CFGetTypeID(uid)==CFStringGetTypeID()&&
               CFEqual(uid,CFSTR(RK3_NATIVE_DEVICE_UID))) {
                if(found)duplicate=true;else found=devices[i];
            }
            CFRelease(uid);
        }
    }
    free(devices);return duplicate?0:found;
}
static MIDIDeviceRef controller_device(void) {
    MIDIDeviceRef found=0;
    for(ItemCount i=0;i<MIDIGetNumberOfDevices();++i) {
        MIDIDeviceRef device=MIDIGetDevice(i);CFStringRef identity=NULL;
        if(!MIDIObjectGetStringProperty(device,CFSTR(RK3_CONTROLLER_IDENTITY_KEY),&identity)&&identity) {
            bool match=CFGetTypeID(identity)==CFStringGetTypeID()&&
                CFEqual(identity,CFSTR(RK3_NATIVE_DEVICE_UID ".controller"));
            CFRelease(identity);if(match) {if(found)return 0;found=device;}
        }
    }
    return found;
}
static bool read_info(AudioObjectID audio,uint64_t generation) {
    RK3NativeInfo info;
    return !rk3_native_copy_info(audio,&info)&&info.version==RK3_CONTROL_VERSION&&
        (info.flags&RK3_NATIVE_CONNECTED)&&!(info.flags&RK3_NATIVE_FAILED)&&
        info.generation&&(!generation||info.generation==generation);
}
/* A successful read with the wrong object/type/shape is never a pending ACK. */
static bool copy_data(MIDIDeviceRef device,CFStringRef key,void *value,size_t size) {
    CFDataRef data=NULL;OSStatus error=MIDIObjectGetDataProperty(device,key,&data);
    bool valid=!error&&data&&CFGetTypeID(data)==CFDataGetTypeID()&&CFDataGetLength(data)==(CFIndex)size;
    if(valid)memcpy(value,CFDataGetBytePtr(data),size);
    if(data)CFRelease(data);
    return valid;
}
static bool copy_status(MIDIDeviceRef controller,RK3DisplayStatus *status) {
    return copy_data(controller,CFSTR(RK3_CONTROLLER_DISPLAY_STATUS_KEY),status,sizeof(*status))&&
        rk3_display_status_valid(status);
}
static bool current(AudioObjectID audio,MIDIDeviceRef controller,const RK3DisplayStatus *lease,
                    RK3DisplayStatus *status) {
    return audio_device()==audio&&controller_device()==controller&&
        read_info(audio,lease->generation)&&copy_status(controller,status)&&
        rk3_display_same_status_lease(lease,status)&&status->epoch&&
        status->generation&&status->audio_device==audio&&read_info(audio,lease->generation);
}
static bool open_ready(AudioObjectID *audio,MIDIDeviceRef *controller,RK3DisplayStatus *status) {
    double deadline=now()+10;
    do {
        *audio=audio_device();*controller=controller_device();
        if(*audio&&*controller&&copy_status(*controller,status)&&!status->status&&
           status->audio_device==*audio&&read_info(*audio,status->generation)) {
            RK3DisplayStatus check;if(current(*audio,*controller,status,&check)&&!check.status) {
                *status=check;return true;
            }
        }
        if(!pause_ms(100))break;
    } while(now()<deadline);
    fprintf(stderr,"Current native audio/controller display state is not ready\n");return false;
}
static bool same_request(MIDIDeviceRef controller,const RK3DisplayRequest *wanted) {
    RK3DisplayRequest request;
    return copy_data(controller,CFSTR(RK3_CONTROLLER_DISPLAY_REQUEST_KEY),&request,sizeof(request))&&
        rk3_display_request_valid(&request)&&!memcmp(&request,wanted,sizeof(request));
}
static bool wait_applied(AudioObjectID audio,MIDIDeviceRef controller,const RK3DisplayStatus *lease,
                         const RK3DisplayRequest *request,RK3DisplayStatus *applied) {
    double deadline=now()+5;
    do {
        RK3DisplayStatus status;
        if(!current(audio,controller,lease,&status)||!same_request(controller,request)) {
            fprintf(stderr,"Display lease, state or request changed before acknowledgement\n");return false;
        }
        if(status.request_id==request->request_id) {
            if(!status.status) {
                if(status.content[request->index]!=request->value||
                   !same_request(controller,request)||!current(audio,controller,lease,applied)||
                   applied->status||applied->request_id!=request->request_id||
                   applied->content[request->index]!=request->value) {
                    fprintf(stderr,"Display state changed while checking acknowledgement\n");return false;
                }
                return true;
            }
            if(status.status!=EAGAIN&&status.status!=EINPROGRESS) {
                fprintf(stderr,"Driver rejected display request: native error %d\n",status.status);return false;
            }
        } else if(status.status&&status.status!=EAGAIN&&status.status!=EINPROGRESS) {
            fprintf(stderr,"Display runtime is unavailable: native error %d\n",status.status);return false;
        }
        if(!pause_ms(50))break;
    } while(now()<deadline);
    fprintf(stderr,"Logical display acknowledgement was not confirmed within five seconds\n");return false;
}
static bool unsigned_argument(const char *text,unsigned high,unsigned *value) {
    if(!text||!*text)return false;
    for(const char *p=text;*p;++p)if(*p<'0'||*p>'9')return false;
    char *end=NULL;errno=0;unsigned long parsed=strtoul(text,&end,10);
    if(errno||*end||parsed>high)return false;
    *value=(unsigned)parsed;return true;
}
static void show(const RK3DisplayStatus *status) {
    printf("Logical display content (indices 0..3): %u %u %u %u\n",
        status->content[0],status->content[1],status->content[2],status->content[3]);
    puts("Logical Controller state; not hardware readback or USB completion.");
}
int main(int argc,char **argv) {
    unsigned index=0,value=0;bool index_given=false,value_given=false,show_only=false;
    if(argc==2&&!strcmp(argv[1],"--help")) {
        printf("Usage: %s --show\n       %s --index 0..3 --value 0..255\n",argv[0],argv[0]);return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--show"))show_only=true;
    else for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--index")&&!index_given&&i+1<argc) {
            index_given=true;if(unsigned_argument(argv[++i],3,&index))continue;
        } else if(!strcmp(argv[i],"--value")&&!value_given&&i+1<argc) {
            value_given=true;if(unsigned_argument(argv[++i],255,&value))continue;
        }
        fprintf(stderr,"Usage: %s --show | --index 0..3 --value 0..255\n",argv[0]);return 2;
    }
    if(!show_only&&(!index_given||!value_given)) {
        fprintf(stderr,"Choose --show or both --index and --value\n");return 2;
    }
    MIDIClientRef client=0;OSStatus error=MIDIClientCreate(CFSTR("RK3 native display"),NULL,NULL,&client);
    if(error) {fprintf(stderr,"CoreMIDI client: %d\n",(int)error);return 1;}
    int result=1;AudioObjectID audio=0;MIDIDeviceRef controller=0;RK3DisplayStatus lease;
    if(!open_ready(&audio,&controller,&lease))goto done;
    if(show_only) {show(&lease);result=0;goto done;}
    RK3DisplayStatus check;
    if(!current(audio,controller,&lease,&check)||check.status) {
        fprintf(stderr,"Display runtime changed before commit\n");goto done;
    }
    RK3DisplayRequest request={.version=1,.index=index,.session=lease.session,.epoch=lease.epoch,
        .generation=lease.generation,.audio_device=audio,.value=value};
    do {arc4random_buf(&request.request_id,sizeof(request.request_id));}
    while(!request.request_id||request.request_id==check.request_id);
    CFDataRef data=CFDataCreate(NULL,(const UInt8 *)&request,sizeof(request));
    error=data?MIDIObjectSetDataProperty(controller,CFSTR(RK3_CONTROLLER_DISPLAY_REQUEST_KEY),data):-1;
    if(data)CFRelease(data);
    if(error) {fprintf(stderr,"Writing display request failed: %d\n",(int)error);goto done;}
    RK3DisplayStatus applied;
    if(!wait_applied(audio,controller,&lease,&request,&applied))goto done;
    show(&applied);result=0;
done:
    error=MIDIClientDispose(client);
    if(error) {fprintf(stderr,"Disposing CoreMIDI client failed: %d\n",(int)error);result=1;}
    return result;
}
