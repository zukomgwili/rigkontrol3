/* One-time native driver pedal calibration. Reads raw reports and stores the
 * measured range on the native Controller MIDI device; no GUI bridge. */
#include "../Driver/NativeControl.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreMIDI/CoreMIDI.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void pause_ms(long ms){struct timespec t={ms/1000,ms%1000*1000000};nanosleep(&t,NULL);}
static AudioObjectPropertyAddress address(UInt32 selector){return (AudioObjectPropertyAddress){selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};}
static AudioObjectID audio_device(void){
    AudioObjectPropertyAddress a=address(kAudioHardwarePropertyDevices);UInt32 size=0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,NULL,&size)||size>65536||size%sizeof(AudioObjectID))return 0;
    AudioObjectID *devices=malloc(size?size:1);if(!devices)return 0;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&size,devices)){free(devices);return 0;}
    AudioObjectID found=0;
    for(UInt32 i=0;i<size/sizeof(*devices);++i){
        a=address(kAudioDevicePropertyDeviceUID);CFStringRef uid=NULL;UInt32 bytes=sizeof(uid);
        if(!AudioObjectGetPropertyData(devices[i],&a,0,NULL,&bytes,&uid)&&uid){
            if(CFEqual(uid,CFSTR(RK3_NATIVE_DEVICE_UID)))found=devices[i];CFRelease(uid);
        }
    }
    free(devices);return found;
}
static MIDIDeviceRef controller_device(void){
    for(ItemCount i=0;i<MIDIGetNumberOfDevices();++i){
        MIDIDeviceRef device=MIDIGetDevice(i);CFStringRef identity=NULL;
        if(!MIDIObjectGetStringProperty(device,CFSTR(RK3_CONTROLLER_IDENTITY_KEY),&identity)&&identity){
            bool match=CFEqual(identity,CFSTR(RK3_NATIVE_DEVICE_UID ".controller"));CFRelease(identity);
            if(match)return device;
        }
    }
    return 0;
}
static bool read_info(AudioObjectID device,RK3NativeInfo *info){
    UInt32 bytes=sizeof(*info);AudioObjectPropertyAddress a=address(RK3_PROPERTY_INFO);
    return !AudioObjectGetPropertyData(device,&a,0,NULL,&bytes,info)&&bytes==sizeof(*info)&&
        info->version==1&&(info->flags&RK3_NATIVE_CONNECTED);
}
int main(int argc,char **argv){
    unsigned duration=20;
    if(argc==3&&strcmp(argv[1],"--seconds")==0){
        char *end=NULL;errno=0;unsigned long value=strtoul(argv[2],&end,10);
        if(errno||end==argv[2]||*end||value<5||value>120){fprintf(stderr,"Choose 5..120 seconds\n");return 2;}duration=(unsigned)value;
    }else if(argc!=1){fprintf(stderr,"Usage: %s [--seconds 5..120]\nSweeps built-in pedal, then saves measured endpoints. No change on insufficient travel or disconnect.\n",argv[0]);return 2;}
    MIDIClientRef client=0;
    OSStatus error=MIDIClientCreate(CFSTR("RK3 native calibration"),NULL,NULL,&client);
    if(error){fprintf(stderr,"CoreMIDI client: %d\n",(int)error);return 1;}
    AudioObjectID device=audio_device();MIDIDeviceRef controller=0;RK3NativeInfo info={0};
    double deadline=now()+10;
    while(now()<deadline){controller=controller_device();if(device&&controller&&read_info(device,&info))break;if(!device)device=audio_device();pause_ms(100);}
    if(!device||!controller||!read_info(device,&info)){fprintf(stderr,"Native audio/controller driver is not connected\n");MIDIClientDispose(client);return 1;}
    uint64_t generation=info.generation;
    RK3NativeReportQuery query={1,0,0};RK3NativeReports batch;UInt32 size=sizeof(batch);
    AudioObjectPropertyAddress a=address(RK3_PROPERTY_REPORTS);
    if(AudioObjectGetPropertyData(device,&a,sizeof(query),&query,&size,&batch)||size!=sizeof(batch)||batch.version!=1){MIDIClientDispose(client);return 1;}
    query.after_sequence=batch.latest_sequence;
    printf("Recording %u seconds: sweep the built-in pedal fully heel-to-toe and back several times.\n",duration);fflush(stdout);
    uint32_t minimum=1023,maximum=0;uint64_t samples=0;bool failed=false;
    deadline=now()+duration;
    while(now()<deadline){
        if(!read_info(device,&info)||info.generation!=generation){fprintf(stderr,"Device disconnected/reconnected; no calibration saved\n");failed=true;break;}
        size=sizeof(batch);
        if(AudioObjectGetPropertyData(device,&a,sizeof(query),&query,&size,&batch)||size!=sizeof(batch)||batch.version!=1||batch.count>32||batch.dropped){failed=true;break;}
        for(uint32_t i=0;i<batch.count;++i){
            RK3NativeReport *r=&batch.reports[i];
            if(r->length<7||r->length>64||r->bytes[0]!=3)continue;
            uint32_t raw=((uint32_t)r->bytes[5]<<8)|r->bytes[6];
            if(raw>1023)continue;
            if(raw<minimum)minimum=raw;if(raw>maximum)maximum=raw;++samples;
        }
        query.after_sequence=batch.next_sequence;pause_ms(10);
    }
    if(failed||samples<10||maximum<=minimum||maximum-minimum<103){
        fprintf(stderr,"No calibration saved: samples=%llu range=%u..%u (need at least 103 raw counts).\n",(unsigned long long)samples,minimum,maximum);
        MIDIClientDispose(client);return 1;
    }
    if(!read_info(device,&info)||info.generation!=generation){
        fprintf(stderr,"Device changed before commit; no calibration saved\n");MIDIClientDispose(client);return 1;
    }
    uint32_t calibration[4]={1,2,minimum,maximum};
    CFDataRef data=CFDataCreate(NULL,(const UInt8 *)calibration,sizeof(calibration));
    error=data?MIDIObjectSetDataProperty(controller,CFSTR(RK3_CONTROLLER_CALIBRATION_KEY),data):-1;
    if(data)CFRelease(data);
    MIDIClientDispose(client);
    if(error){fprintf(stderr,"Measured %u..%u, but saving failed: %d\n",minimum,maximum,(int)error);return 1;}
    printf("Saved built-in pedal calibration: %u..%u (%llu samples).\n",minimum,maximum,(unsigned long long)samples);return 0;
}
