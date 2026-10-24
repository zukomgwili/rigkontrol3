/* Standard CoreAudio client acceptance test for the installed native driver.
 * Default sends silence; --monitor copies guitar input1 quietly to both outs. */
#include "../Driver/NativeControl.h"
#include <CoreAudio/CoreAudio.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static atomic_uint_fast64_t cycles,samples,energy_scaled;
static bool monitor_input;
static volatile sig_atomic_t stop_requested,dump_requested;
static void handle_signal(int signal_number){if(signal_number==SIGUSR1)dump_requested=1;else stop_requested=1;}
static double monotonic_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void print_stats(bool final,OSStatus stopped){
    uint64_t count=atomic_load(&samples),calls=atomic_load(&cycles),energy=atomic_load(&energy_scaled);
    printf("CoreAudio callbacks=%llu input1frames=%llu RMS=%.6f",(unsigned long long)calls,(unsigned long long)count,count?sqrt((double)energy/(count*1000000000.0)):0);
    if(final)printf(" stop=%d\n",(int)stopped);else printf(" (running)\n");
    fflush(stdout);
}
static OSStatus process(AudioDeviceID device,const AudioTimeStamp *now,const AudioBufferList *input,const AudioTimeStamp *input_time,AudioBufferList *output,const AudioTimeStamp *output_time,void *context){
    (void)device;(void)now;(void)input_time;(void)output_time;(void)context;
    atomic_fetch_add_explicit(&cycles,1,memory_order_relaxed);
    const AudioBuffer *in=input&&input->mNumberBuffers?&input->mBuffers[0]:NULL;
    size_t available=in&&in->mData&&in->mNumberChannels?in->mDataByteSize/(sizeof(float)*in->mNumberChannels):0;
    const float *source=available?in->mData:NULL;
    uint64_t total_energy=0;
    for(size_t frame=0;frame<available;++frame){float value=source[frame*in->mNumberChannels];if(isfinite(value)){double square=(double)value*value;if(square>4)square=4;total_energy+=(uint64_t)(square*1000000000.0);}}
    atomic_fetch_add_explicit(&samples,available,memory_order_relaxed);atomic_fetch_add_explicit(&energy_scaled,total_energy,memory_order_relaxed);
    if(output)for(UInt32 b=0;b<output->mNumberBuffers;++b){
        AudioBuffer *out=&output->mBuffers[b];if(!out->mData||!out->mNumberChannels)continue;
        float *target=out->mData;size_t frames=out->mDataByteSize/(sizeof(float)*out->mNumberChannels);
        memset(out->mData,0,out->mDataByteSize);
        if(monitor_input&&source)for(size_t frame=0;frame<frames&&frame<available;++frame){
            float value=source[frame*in->mNumberChannels]*0.1f;if(!isfinite(value))value=0;if(value>0.2f)value=0.2f;if(value< -0.2f)value= -0.2f;
            for(UInt32 channel=0;channel<out->mNumberChannels;++channel)target[frame*out->mNumberChannels+channel]=value;
        }
    }
    return 0;
}
static AudioObjectPropertyAddress property(UInt32 s){return (AudioObjectPropertyAddress){s,kAudioObjectPropertyScopeGlobal,0};}
int main(int argc,char **argv){
    unsigned seconds=10;bool live=false,seconds_given=false;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--monitor")){monitor_input=true;continue;}
        if(!strcmp(argv[i],"--live")){live=true;continue;}
        if(!strcmp(argv[i],"--seconds")&&i+1<argc){char *end;unsigned long n=strtoul(argv[++i],&end,10);if(*end||n<1||n>120)return 2;seconds=(unsigned)n;seconds_given=true;continue;}
        fprintf(stderr,"Usage:%s [--monitor] [--seconds 1..120 | --live]\n",argv[0]);return 2;
    }
    if(live&&seconds_given){fprintf(stderr,"Choose either --seconds or --live\n");return 2;}
    signal(SIGINT,handle_signal);signal(SIGTERM,handle_signal);signal(SIGUSR1,handle_signal);
    AudioObjectPropertyAddress a=property(kAudioHardwarePropertyDevices);UInt32 size=0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,NULL,&size)||size>65536)return 1;
    AudioObjectID *devices=malloc(size?size:1);if(!devices)return 1;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&size,devices)){free(devices);return 1;}
    AudioObjectID found=0;
    for(UInt32 i=0;i<size/sizeof(*devices);++i){CFStringRef uid=NULL;UInt32 n=sizeof(uid);a=property(kAudioDevicePropertyDeviceUID);
        if(!AudioObjectGetPropertyData(devices[i],&a,0,NULL,&n,&uid)&&uid){if(CFEqual(uid,CFSTR(RK3_NATIVE_DEVICE_UID)))found=devices[i];CFRelease(uid);}}
    free(devices);if(!found){fprintf(stderr,"Native RK3 audio device not found\n");return 1;}
    AudioDeviceIOProcID callback=NULL;OSStatus error=AudioDeviceCreateIOProcID(found,process,NULL,&callback);
    if(!error)error=AudioDeviceStart(found,callback);
    if(error){fprintf(stderr,"CoreAudio start error:%d\n",(int)error);if(callback)AudioDeviceDestroyIOProcID(found,callback);return 1;}
    if(live)printf("Standard CoreAudio client running until stopped; output=%s; PID=%d.\n",monitor_input?"quiet guitar monitor":"silence",getpid());
    else printf("Standard CoreAudio client running for %u seconds; output=%s.\n",seconds,monitor_input?"quiet guitar monitor":"silence");
    fflush(stdout);
    double deadline=monotonic_now()+seconds;
    while(!stop_requested&&(live||monotonic_now()<deadline)){
        if(dump_requested){dump_requested=0;print_stats(false,0);}
        struct timespec delay={0,100000000};nanosleep(&delay,NULL);
    }
    OSStatus stopped=AudioDeviceStop(found,callback);AudioDeviceDestroyIOProcID(found,callback);
    print_stats(true,stopped);
    return stopped||!atomic_load(&cycles)||!atomic_load(&samples)?1:0;
}
