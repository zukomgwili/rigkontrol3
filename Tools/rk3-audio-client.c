/* Standard CoreAudio client acceptance test for the installed native driver.
 * Default sends silence; --monitor copies guitar input1 to both outs. */
#include "../Driver/NativeControl.h"
#include <CoreAudio/CoreAudio.h>
#include <errno.h>
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
/* Nonnegative finite IEEE754 float encodings preserve magnitude ordering. */
static _Atomic uint32_t input_peak_bits;
_Static_assert(sizeof(float)==sizeof(uint32_t),"CoreAudio float size");
static bool monitor_input;
static float monitor_gain=0.1f,monitor_limit=0.2f;
static volatile sig_atomic_t stop_requested,dump_requested;
static void handle_signal(int signal_number){if(signal_number==SIGUSR1)dump_requested=1;else stop_requested=1;}
static double monotonic_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void print_stats(bool final,OSStatus stopped){
    uint64_t count=atomic_load(&samples),calls=atomic_load(&cycles),energy=atomic_load(&energy_scaled);
    uint32_t peak_bits=atomic_load(&input_peak_bits);float peak;memcpy(&peak,&peak_bits,sizeof(peak));
    printf("CoreAudio callbacks=%llu input1frames=%llu RMS=%.6f input_peak=%.6f",(unsigned long long)calls,(unsigned long long)count,count?sqrt((double)energy/(count*1000000000.0)):0,(double)peak);
    if(final)printf(" stop=%d\n",(int)stopped);else printf(" (running)\n");
    fflush(stdout);
}
static OSStatus process(AudioDeviceID device,const AudioTimeStamp *now,const AudioBufferList *input,const AudioTimeStamp *input_time,AudioBufferList *output,const AudioTimeStamp *output_time,void *context){
    (void)device;(void)now;(void)input_time;(void)output_time;(void)context;
    atomic_fetch_add_explicit(&cycles,1,memory_order_relaxed);
    const AudioBuffer *in=input&&input->mNumberBuffers?&input->mBuffers[0]:NULL;
    size_t available=in&&in->mData&&in->mNumberChannels?in->mDataByteSize/(sizeof(float)*in->mNumberChannels):0;
    const float *source=available?in->mData:NULL;
    uint64_t total_energy=0;uint32_t block_peak=0;
    for(size_t frame=0;frame<available;++frame){
        float value=source[frame*in->mNumberChannels];if(!isfinite(value))continue;
        double square=(double)value*value;if(square>4)square=4;total_energy+=(uint64_t)(square*1000000000.0);
        float magnitude=fabsf(value);uint32_t bits;memcpy(&bits,&magnitude,sizeof(bits));if(bits>block_peak)block_peak=bits;
    }
    atomic_fetch_add_explicit(&samples,available,memory_order_relaxed);atomic_fetch_add_explicit(&energy_scaled,total_energy,memory_order_relaxed);
    uint32_t previous_peak=atomic_load_explicit(&input_peak_bits,memory_order_relaxed);
    while(block_peak>previous_peak&&!atomic_compare_exchange_weak_explicit(&input_peak_bits,&previous_peak,block_peak,memory_order_relaxed,memory_order_relaxed)){}
    if(output)for(UInt32 b=0;b<output->mNumberBuffers;++b){
        AudioBuffer *out=&output->mBuffers[b];if(!out->mData||!out->mNumberChannels)continue;
        float *target=out->mData;size_t frames=out->mDataByteSize/(sizeof(float)*out->mNumberChannels);
        memset(out->mData,0,out->mDataByteSize);
        if(monitor_input&&source)for(size_t frame=0;frame<frames&&frame<available;++frame){
            float value=source[frame*in->mNumberChannels]*monitor_gain;if(!isfinite(value))value=0;if(value>monitor_limit)value=monitor_limit;if(value< -monitor_limit)value= -monitor_limit;
            for(UInt32 channel=0;channel<out->mNumberChannels;++channel)target[frame*out->mNumberChannels+channel]=value;
        }
    }
    return 0;
}
static AudioObjectPropertyAddress property(UInt32 s){return (AudioObjectPropertyAddress){s,kAudioObjectPropertyScopeGlobal,0};}
static bool parse_gain(const char *text,float *gain){
    const char *cursor=text;bool digits=false;
    if(*cursor=='+'||*cursor=='-')++cursor;
    while(*cursor>='0'&&*cursor<='9'){digits=true;++cursor;}
    if(*cursor=='.'){++cursor;while(*cursor>='0'&&*cursor<='9'){digits=true;++cursor;}}
    if(!digits)return false;
    if(*cursor=='e'||*cursor=='E'){
        ++cursor;if(*cursor=='+'||*cursor=='-')++cursor;digits=false;
        while(*cursor>='0'&&*cursor<='9'){digits=true;++cursor;}if(!digits)return false;
    }
    if(*cursor)return false;
    char *end;errno=0;double value=strtod(text,&end);
    if(end==text||*end||errno==ERANGE||!isfinite(value)||value<0||value>1)return false;
    *gain=(float)value;return true;
}
static bool configure_buffer(AudioDeviceID device,UInt32 requested){
    AudioObjectPropertyAddress address=property(kAudioDevicePropertyBufferFrameSize);
    if(requested){
        Boolean settable=false;OSStatus error=AudioObjectIsPropertySettable(device,&address,&settable);
        if(error||!settable){fprintf(stderr,"CoreAudio buffer size is not settable (status=%d)\n",(int)error);return false;}
        AudioObjectPropertyAddress range_address=property(kAudioDevicePropertyBufferFrameSizeRange);
        if(AudioObjectHasProperty(device,&range_address)){
            AudioValueRange range;UInt32 size=sizeof(range);error=AudioObjectGetPropertyData(device,&range_address,0,NULL,&size,&range);
            if(error||size!=sizeof(range)){fprintf(stderr,"Cannot query supported CoreAudio buffer range (status=%d)\n",(int)error);return false;}
            if(!isfinite(range.mMinimum)||!isfinite(range.mMaximum)||requested<range.mMinimum||requested>range.mMaximum){
                fprintf(stderr,"Requested buffer %u frames is outside supported range %.0f..%.0f\n",(unsigned)requested,range.mMinimum,range.mMaximum);return false;
            }
        }
        error=AudioObjectSetPropertyData(device,&address,0,NULL,sizeof(requested),&requested);
        if(error){fprintf(stderr,"CoreAudio rejected buffer %u frames (status=%d)\n",(unsigned)requested,(int)error);return false;}
    }
    UInt32 frames=0,size=sizeof(frames);OSStatus error=AudioObjectGetPropertyData(device,&address,0,NULL,&size,&frames);
    if(error||size!=sizeof(frames)||!frames){
        fprintf(stderr,"Cannot query actual CoreAudio buffer frames (status=%d)\n",(int)error);return !requested;
    }
    printf("CoreAudio buffer=%u frames (%s)\n",(unsigned)frames,requested?"requested":"default");fflush(stdout);
    if(requested&&frames!=requested){fprintf(stderr,"CoreAudio selected %u frames instead of requested %u\n",(unsigned)frames,(unsigned)requested);return false;}
    return true;
}
int main(int argc,char **argv){
    unsigned seconds=10;UInt32 buffer_frames=0;bool live=false,seconds_given=false;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--monitor")){monitor_input=true;continue;}
        if(!strcmp(argv[i],"--live")){live=true;continue;}
        if(!strcmp(argv[i],"--gain")&&i+1<argc){if(!parse_gain(argv[++i],&monitor_gain)){fprintf(stderr,"Gain must be a finite decimal from 0 to 1\n");return 2;}monitor_limit=0.95f;continue;}
        if(!strcmp(argv[i],"--buffer-frames")&&i+1<argc){const char *text=argv[++i];char *end;unsigned long n=strtoul(text,&end,10);if(end==text||*end||n<32||n>1024){fprintf(stderr,"Buffer frames must be an integer from 32 to 1024\n");return 2;}buffer_frames=(UInt32)n;continue;}
        if(!strcmp(argv[i],"--seconds")&&i+1<argc){char *end;unsigned long n=strtoul(argv[++i],&end,10);if(*end||n<1||n>120)return 2;seconds=(unsigned)n;seconds_given=true;continue;}
        fprintf(stderr,"Usage:%s [--monitor] [--gain 0..1] [--buffer-frames 32..1024] [--seconds 1..120 | --live]\n",argv[0]);return 2;
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
    if(!configure_buffer(found,buffer_frames))return 1;
    AudioDeviceIOProcID callback=NULL;OSStatus error=AudioDeviceCreateIOProcID(found,process,NULL,&callback);
    if(!error)error=AudioDeviceStart(found,callback);
    if(error){fprintf(stderr,"CoreAudio start error:%d\n",(int)error);if(callback)AudioDeviceDestroyIOProcID(found,callback);return 1;}
    if(live)printf("Standard CoreAudio client running until stopped; output=%s; gain=%.3f limit=%.3f; PID=%d.\n",monitor_input?"guitar monitor":"silence",(double)monitor_gain,(double)monitor_limit,getpid());
    else printf("Standard CoreAudio client running for %u seconds; output=%s; gain=%.3f limit=%.3f.\n",seconds,monitor_input?"guitar monitor":"silence",(double)monitor_gain,(double)monitor_limit);
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
