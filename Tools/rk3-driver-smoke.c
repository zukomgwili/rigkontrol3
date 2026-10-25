/* Loads the native audio bundle in this diagnostic process and exercises its
 * real host interface. Does not install a driver or emit audible output. */
#include "../Driver/NativeControl.h"
#include "../Driver/AudioTiming.h"
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static OSStatus changed(AudioServerPlugInHostRef h,AudioObjectID o,UInt32 n,const AudioObjectPropertyAddress *a){(void)h;(void)o;(void)n;(void)a;return 0;}
static OSStatus copy_storage(AudioServerPlugInHostRef h,CFStringRef key,CFPropertyListRef *value){(void)h;(void)key;if(value)*value=NULL;return kAudioHardwareUnknownPropertyError;}
static OSStatus write_storage(AudioServerPlugInHostRef h,CFStringRef key,CFPropertyListRef value){(void)h;(void)key;(void)value;return 0;}
static OSStatus delete_storage(AudioServerPlugInHostRef h,CFStringRef key){(void)h;(void)key;return 0;}
static OSStatus configuration(AudioServerPlugInHostRef h,AudioObjectID device,UInt64 action,void *data){(void)h;(void)device;(void)action;(void)data;return kAudioHardwareUnsupportedOperationError;}
static struct AudioServerPlugInHostInterface host={changed,copy_storage,write_storage,delete_storage,configuration};
static bool system_driver_present(void){
    AudioObjectPropertyAddress a={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,0};
    AudioObjectID ids[1024];UInt32 bytes=sizeof(ids);
    if(AudioObjectGetPropertyData(1,&a,0,NULL,&bytes,ids))return false;
    for(UInt32 i=0;i<bytes/sizeof(ids[0]);++i){
        a.mSelector=kAudioDevicePropertyDeviceUID;CFStringRef uid=NULL;UInt32 size=sizeof(uid);
        if(!AudioObjectGetPropertyData(ids[i],&a,0,NULL,&size,&uid)&&uid){
            bool found=CFEqual(uid,CFSTR(RK3_NATIVE_DEVICE_UID));CFRelease(uid);if(found)return true;
        }
    }
    return false;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void pause_ms(long ms){struct timespec t={ms/1000,ms%1000*1000000};nanosleep(&t,NULL);}
static OSStatus copy_info(AudioServerPlugInDriverRef driver,AudioObjectID device,RK3NativeInfo *info){
    AudioObjectPropertyAddress address={RK3_PROPERTY_INFO,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    CFPropertyListRef object=NULL;UInt32 size=0;
    OSStatus error=(*driver)->GetPropertyData(driver,device,0,&address,0,NULL,sizeof(object),&size,&object);
    if(!error&&(size!=sizeof(object)||!object||CFGetTypeID(object)!=CFDataGetTypeID()||
        CFDataGetLength((CFDataRef)object)!=(CFIndex)sizeof(*info)))error=kAudioHardwareBadPropertySizeError;
    if(!error){
        RK3NativeInfo decoded;memcpy(&decoded,CFDataGetBytePtr((CFDataRef)object),sizeof(decoded));
        if(decoded.version!=RK3_CONTROL_VERSION)error=kAudioHardwareIllegalOperationError;
        else *info=decoded;
    }
    if(object)CFRelease(object);
    return error;
}
int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"Usage: %s AUDIO_BUNDLE\n",argv[0]);return 2;}
    if(system_driver_present()){fprintf(stderr,"The installed driver owns this device. Use rk3-audio-client for installed-driver checks.\n");return 2;}
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)argv[1],strlen(argv[1]),true);
    CFPlugInRef plugin=url?CFPlugInCreate(NULL,url):NULL;if(url)CFRelease(url);
    if(!plugin){fprintf(stderr,"Bundle load failed\n");return 1;}
    void *symbol=CFBundleGetFunctionPointerForName(CFPlugInGetBundle(plugin),CFSTR("RK3AudioFactory"));
    void *(*factory)(CFAllocatorRef,CFUUIDRef)=NULL;memcpy(&factory,&symbol,sizeof(symbol));
    if(!factory){fprintf(stderr,"Missing factory\n");return 1;}
    AudioServerPlugInDriverRef driver=factory(NULL,kAudioServerPlugInTypeUUID);
    if(!driver||(*driver)->Initialize(driver,&host)){fprintf(stderr,"Driver Initialize failed\n");return 1;}
    AudioObjectPropertyAddress a={kAudioPlugInPropertyDeviceList,kAudioObjectPropertyScopeGlobal,0};
    AudioObjectID device=0;UInt32 size=0;
    if((*driver)->GetPropertyData(driver,1,0,&a,0,NULL,sizeof(device),&size,&device)||!device)return 1;
    RK3NativeInfo info={0};double deadline=now()+10;
    while(now()<deadline){
        if(copy_info(driver,device,&info))return 1;
        if(info.flags&(RK3_NATIVE_CONNECTED|RK3_NATIVE_FAILED))break;pause_ms(10);
    }
    if(!(info.flags&RK3_NATIVE_CONNECTED)){fprintf(stderr,"Driver connection failed:%d flags%u\n",info.last_error,info.flags);return 1;}
    OSStatus error=(*driver)->StartIO(driver,device,42);
    if(error){fprintf(stderr,"StartIO:%d\n",(int)error);return 1;}
    float input[512],silence[512]={0};uint64_t calls=0;long double energy=0;
    Float64 first_frame=0,last_frame=0;UInt64 timestamp=0,seed=0;
    error=(*driver)->GetZeroTimeStamp(driver,device,42,&first_frame,&timestamp,&seed);
    AudioServerPlugInIOCycleInfo cycle={0};mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    cycle.mInputTime.mFlags=kAudioTimeStampSampleTimeValid;
    cycle.mOutputTime.mFlags=kAudioTimeStampSampleTimeValid;deadline=now()+1;
    while(!error&&now()<deadline){
        error=(*driver)->GetZeroTimeStamp(driver,device,42,&last_frame,&timestamp,&seed);
        if(!error){
            uint64_t tick=mach_absolute_time();
            long double elapsed=tick>=timestamp?(long double)(tick-timestamp)*timebase.numer/timebase.denom/1e9L:0;
            Float64 current=last_frame+(Float64)(elapsed*RK3_AUDIO_SAMPLE_RATE);
            cycle.mInputTime.mSampleTime=floor(current)-RK3_AUDIO_SAMPLE_OFFSET_FRAMES-RK3_AUDIO_INPUT_LATENCY_FRAMES;
            cycle.mOutputTime.mSampleTime=floor(current)+RK3_AUDIO_SAMPLE_OFFSET_FRAMES+RK3_AUDIO_OUTPUT_LATENCY_FRAMES;
        }
        if(!error)error=(*driver)->DoIOOperation(driver,device,3,42,kAudioServerPlugInIOOperationReadInput,256,&cycle,input,NULL);
        if(!error)error=(*driver)->DoIOOperation(driver,device,4,42,kAudioServerPlugInIOOperationWriteMix,256,&cycle,silence,NULL);
        if(error)break;
        for(unsigned i=0;i<512;++i){if(!isfinite(input[i])){error=kAudioHardwareUnspecifiedError;break;}energy+=(long double)input[i]*input[i];}
        ++calls;pause_ms(5);
    }
    OSStatus stopped=(*driver)->StopIO(driver,device,42);
    printf("Real audio bundle: rate%u inputs%u outputs%u snapshot0x%08x; IOcycles%llu; zero-frame%.0f..%.0f; RMS%.6Lf; I/O%d stop%d\n",
        info.sample_rate,info.audio_inputs,info.audio_outputs,info.digital_snapshot,(unsigned long long)calls,first_frame,last_frame,
        calls?sqrtl(energy/(calls*512)):0,(int)error,(int)stopped);
    /* The bundle singleton and worker deliberately have process lifetime. */
    return error||stopped||calls<10||last_frame<=first_frame?1:0;
}
