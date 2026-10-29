/* Safe raw-sweep measurement for any of the three native pedal inputs.
 * This utility guard is separate from NI's recovered recalibration algorithm. */
#include "../Driver/NativeClient.h"
#include "../Driver/PedalSettings.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreMIDI/CoreMIDI.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;
}
static void pause_ms(long ms) {
    struct timespec t={ms/1000,ms%1000*1000000};nanosleep(&t,NULL);
}
static AudioObjectPropertyAddress address(UInt32 selector) {
    return (AudioObjectPropertyAddress){selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
}
static AudioObjectID audio_device(void) {
    AudioObjectPropertyAddress a=address(kAudioHardwarePropertyDevices);UInt32 size=0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,NULL,&size)||
       size>65536||size%sizeof(AudioObjectID))return 0;
    AudioObjectID *devices=malloc(size?size:1);if(!devices)return 0;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&size,devices)) {
        free(devices);return 0;
    }
    AudioObjectID found=0;
    for(UInt32 i=0;i<size/sizeof(*devices);++i) {
        a=address(kAudioDevicePropertyDeviceUID);CFStringRef uid=NULL;UInt32 bytes=sizeof(uid);
        if(!AudioObjectGetPropertyData(devices[i],&a,0,NULL,&bytes,&uid)&&uid) {
            if(CFEqual(uid,CFSTR(RK3_NATIVE_DEVICE_UID)))found=devices[i];
            CFRelease(uid);
        }
    }
    free(devices);return found;
}
static MIDIDeviceRef controller_device(void) {
    for(ItemCount i=0;i<MIDIGetNumberOfDevices();++i) {
        MIDIDeviceRef device=MIDIGetDevice(i);CFStringRef identity=NULL;
        if(!MIDIObjectGetStringProperty(device,CFSTR(RK3_CONTROLLER_IDENTITY_KEY),&identity)&&identity) {
            bool match=CFEqual(identity,CFSTR(RK3_NATIVE_DEVICE_UID ".controller"));
            CFRelease(identity);if(match)return device;
        }
    }
    return 0;
}
static bool read_info(AudioObjectID device,RK3NativeInfo *info) {
    return !rk3_native_copy_info(device,info)&&info->version==RK3_CONTROL_VERSION&&
        (info->flags&RK3_NATIVE_CONNECTED)&&!(info->flags&RK3_NATIVE_FAILED)&&info->generation;
}
static bool copy_data(MIDIDeviceRef device,CFStringRef key,void *value,size_t size) {
    CFDataRef data=NULL;
    OSStatus error=MIDIObjectGetDataProperty(device,key,&data);
    bool valid=!error&&data&&CFGetTypeID(data)==CFDataGetTypeID()&&
        CFDataGetLength(data)==(CFIndex)size;
    if(valid)memcpy(value,CFDataGetBytePtr(data),size);
    if(data)CFRelease(data);
    return valid;
}
static bool copy_settings(MIDIDeviceRef device,RK3PedalSettings *settings) {
    return copy_data(device,CFSTR(RK3_CONTROLLER_PEDAL_SETTINGS_KEY),settings,sizeof(*settings))&&
        rk3_pedal_settings_valid(settings);
}
static bool open_ready(AudioObjectID *audio,MIDIDeviceRef *controller,
                       RK3NativeInfo *info,RK3PedalSettings *settings) {
    double deadline=now()+10;
    do {
        *audio=audio_device();*controller=controller_device();
        if(*audio&&*controller&&read_info(*audio,info)&&copy_settings(*controller,settings))return true;
        pause_ms(100);
    } while(now()<deadline);
    fprintf(stderr,"Native audio/controller driver or pedal settings are not ready\n");return false;
}
static bool same_generation(AudioObjectID audio,uint64_t generation) {
    RK3NativeInfo info;
    return read_info(audio,&info)&&info.generation==generation;
}
/* CoreMIDI caches requested settings. Only the driver's same-generation,
 * matching applied status confirms that they reached the mapper and hardware. */
static bool wait_applied(AudioObjectID audio,MIDIDeviceRef controller,uint64_t generation,
                         const RK3PedalSettings *wanted) {
    double deadline=now()+5;
    do {
        if(!same_generation(audio,generation)) {
            fprintf(stderr,"Device disconnected/reconnected before settings application\n");return false;
        }
        RK3PedalSettings current;
        if(!copy_settings(controller,&current)||memcmp(&current,wanted,sizeof(current))) {
            fprintf(stderr,"Requested settings changed before application was confirmed\n");return false;
        }
        RK3PedalStatus status;
        if(copy_data(controller,CFSTR(RK3_CONTROLLER_PEDAL_STATUS_KEY),&status,sizeof(status))&&
           status.version==1&&!status.reserved&&status.audio_device==audio&&
           status.generation==generation&&
           !memcmp(&status.settings,wanted,sizeof(*wanted))) {
            if(!status.status) {
                if(same_generation(audio,generation))return true;
                fprintf(stderr,"Device changed while reading applied status\n");return false;
            }
            if(status.status!=EAGAIN&&status.status!=EINPROGRESS) {
                fprintf(stderr,"Driver rejected pedal settings: native error %d\n",status.status);return false;
            }
        }
        pause_ms(50);
    } while(now()<deadline);
    fprintf(stderr,"Settings application was not confirmed within five seconds\n");return false;
}
static bool save_settings(MIDIDeviceRef controller,const RK3PedalSettings *settings) {
    CFDataRef data=CFDataCreate(NULL,(const UInt8 *)settings,sizeof(*settings));
    OSStatus error=data?MIDIObjectSetDataProperty(controller,CFSTR(RK3_CONTROLLER_PEDAL_SETTINGS_KEY),data):-1;
    if(data)CFRelease(data);
    if(error)fprintf(stderr,"Saving requested pedal settings failed: %d\n",(int)error);
    return !error;
}
static bool unsigned_argument(const char *text,unsigned low,unsigned high,unsigned *value) {
    char *end=NULL;errno=0;unsigned long parsed=strtoul(text,&end,10);
    if(errno||end==text||*end||parsed<low||parsed>high)return false;
    *value=(unsigned)parsed;return true;
}

int main(int argc,char **argv) {
    unsigned duration=20,axis=2;bool seconds_given=false,axis_given=false;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--help")&&argc==2) {
            printf("Usage: %s [--axis 0..2] [--seconds 5..120]\n0/1 are external inputs; 2 is the built-in pedal (default).\n",argv[0]);return 0;
        }
        if(!strcmp(argv[i],"--axis")&&!axis_given&&i+1<argc) {
            axis_given=true;if(unsigned_argument(argv[++i],0,2,&axis))continue;
        } else if(!strcmp(argv[i],"--seconds")&&!seconds_given&&i+1<argc) {
            seconds_given=true;if(unsigned_argument(argv[++i],5,120,&duration))continue;
        }
        fprintf(stderr,"Usage: %s [--axis 0..2] [--seconds 5..120]\n",argv[0]);return 2;
    }
    MIDIClientRef client=0;
    OSStatus error=MIDIClientCreate(CFSTR("RK3 native calibration"),NULL,NULL,&client);
    if(error){fprintf(stderr,"CoreMIDI client: %d\n",(int)error);return 1;}
    AudioObjectID device=0;MIDIDeviceRef controller=0;RK3NativeInfo info;
    RK3PedalSettings settings;
    if(!open_ready(&device,&controller,&info,&settings)||
       !wait_applied(device,controller,info.generation,&settings)) {
        MIDIClientDispose(client);return 1;
    }
    uint64_t generation=info.generation;
    RK3NativeReportQuery query={RK3_CONTROL_VERSION,0,0};RK3NativeReports batch;
    if(rk3_native_copy_reports(device,&query,&batch)||batch.version!=RK3_CONTROL_VERSION) {
        fprintf(stderr,"Cannot establish a fresh raw-report baseline\n");MIDIClientDispose(client);return 1;
    }
    query.after_sequence=batch.latest_sequence;
    const uint32_t raw_max=axis==2?1023:4095,minimum_span=(raw_max+9)/10;
    uint32_t minimum=raw_max,maximum=0;uint64_t samples=0;bool failed=false;
    printf("Recording %u seconds: sweep pedal input %u fully heel-to-toe and back several times.\n",duration,axis);fflush(stdout);
    double deadline=now()+duration;
    while(now()<deadline) {
        if(!same_generation(device,generation)) {
            fprintf(stderr,"Device disconnected/reconnected; no calibration saved\n");failed=true;break;
        }
        if(rk3_native_copy_reports(device,&query,&batch)||batch.version!=RK3_CONTROL_VERSION||
           batch.count>RK3_NATIVE_REPORT_LIMIT||batch.dropped) {failed=true;break;}
        for(uint32_t i=0;i<batch.count;++i) {
            RK3NativeReport *r=&batch.reports[i];
            if(r->length<7||r->length>64||r->bytes[0]!=3)continue;
            uint32_t raw=((uint32_t)r->bytes[1+axis*2]<<8)|r->bytes[2+axis*2];
            if(raw>raw_max)continue;
            if(raw<minimum)minimum=raw;
            if(raw>maximum)maximum=raw;
            ++samples;
        }
        query.after_sequence=batch.next_sequence;pause_ms(10);
    }
    if(failed||samples<10||maximum<=minimum||maximum-minimum<minimum_span) {
        fprintf(stderr,"No calibration saved: samples=%llu range=%u..%u (need at least %u raw counts).\n",
            (unsigned long long)samples,minimum,maximum,minimum_span);
        MIDIClientDispose(client);return 1;
    }
    RK3PedalSettings current;
    if(!same_generation(device,generation)||!copy_settings(controller,&current)||
       memcmp(&current,&settings,sizeof(settings))) {
        fprintf(stderr,"Device or pedal settings changed during measurement; no calibration saved\n");
        MIDIClientDispose(client);return 1;
    }
    current.low[axis]=(float)((double)minimum/raw_max);
    current.high[axis]=(float)((double)maximum/raw_max);
    if(!save_settings(controller,&current)||!wait_applied(device,controller,generation,&current)) {
        fprintf(stderr,"Calibration application was not confirmed\n");MIDIClientDispose(client);return 1;
    }
    printf("Saved and applied pedal %u calibration: %u..%u (%llu samples).\n",
        axis,minimum,maximum,(unsigned long long)samples);
    MIDIClientDispose(client);return 0;
}
