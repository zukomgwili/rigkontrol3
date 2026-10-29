/* Native get/set for NI's existing pedal tip/ring and switch options. */
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

static void show_settings(const char *label,const RK3PedalSettings *settings) {
    printf("%s:\n",label);
    for(unsigned i=0;i<3;++i)
        printf("  axis %u: %s, %s, low %.9g, high %.9g\n",i,
            settings->ring_mask&(1u<<i)?"ring":"tip",
            settings->switch_mask&(1u<<i)?"switch":"continuous",
            (double)settings->low[i],(double)settings->high[i]);
}
int main(int argc,char **argv) {
    unsigned axis=0;bool axis_given=false,show=false;int ring=-1,as_switch=-1;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--help")&&argc==2) {
            printf("Usage: %s --show\n       %s --axis 0..2 [--tip | --ring] [--continuous | --switch]\n",
                argv[0],argv[0]);return 0;
        }
        if(!strcmp(argv[i],"--show")&&argc==2) {show=true;continue;}
        if(!strcmp(argv[i],"--axis")&&!axis_given&&i+1<argc) {
            axis_given=true;if(unsigned_argument(argv[++i],0,2,&axis))continue;
        } else if(!strcmp(argv[i],"--tip")&&ring<0) {ring=0;continue;
        } else if(!strcmp(argv[i],"--ring")&&ring<0) {ring=1;continue;
        } else if(!strcmp(argv[i],"--continuous")&&as_switch<0) {as_switch=0;continue;
        } else if(!strcmp(argv[i],"--switch")&&as_switch<0) {as_switch=1;continue;}
        fprintf(stderr,"Usage: %s --show | --axis 0..2 [--tip | --ring] [--continuous | --switch]\n",argv[0]);return 2;
    }
    if(!show&&(!axis_given||(ring<0&&as_switch<0))) {
        fprintf(stderr,"Choose --show or an axis and at least one pedal option\n");return 2;
    }
    MIDIClientRef client=0;
    OSStatus error=MIDIClientCreate(CFSTR("RK3 native pedal settings"),NULL,NULL,&client);
    if(error){fprintf(stderr,"CoreMIDI client: %d\n",(int)error);return 1;}
    AudioObjectID audio=0;MIDIDeviceRef controller=0;RK3NativeInfo info;
    RK3PedalSettings settings;
    if(!open_ready(&audio,&controller,&info,&settings)) {MIDIClientDispose(client);return 1;}
    if(show) {
        show_settings("Requested",&settings);
        RK3PedalStatus status;
        if(copy_data(controller,CFSTR(RK3_CONTROLLER_PEDAL_STATUS_KEY),&status,sizeof(status))&&
           status.version==1&&!status.reserved&&rk3_pedal_settings_valid(&status.settings)) {
            printf("Applied status: native error %d, audio device %u (current %u), generation %llu (current %llu)\n",
                status.status,status.audio_device,audio,
                (unsigned long long)status.generation,(unsigned long long)info.generation);
            show_settings("Applied",&status.settings);
        } else fprintf(stderr,"Applied status is not available\n");
        bool applied=wait_applied(audio,controller,info.generation,&settings);
        MIDIClientDispose(client);return applied?0:1;
    }
    RK3PedalSettings current;
    if(!copy_settings(controller,&current)||memcmp(&current,&settings,sizeof(settings))) {
        fprintf(stderr,"Pedal settings changed before commit\n");MIDIClientDispose(client);return 1;
    }
    uint32_t bit=1u<<axis;
    if(ring>=0)settings.ring_mask=(settings.ring_mask&~bit)|(ring?bit:0);
    if(as_switch>=0)settings.switch_mask=(settings.switch_mask&~bit)|(as_switch?bit:0);
    if(!same_generation(audio,info.generation)||!save_settings(controller,&settings)||
       !wait_applied(audio,controller,info.generation,&settings)) {
        MIDIClientDispose(client);return 1;
    }
    show_settings("Saved and applied",&settings);
    MIDIClientDispose(client);return 0;
}
