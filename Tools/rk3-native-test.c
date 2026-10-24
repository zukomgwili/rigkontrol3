/* Bounded native-backend acceptance probe; no GUI and no system installation. */
#include "../Driver/NativeUSB.h"
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile sig_atomic_t stop_requested;
static void stop_signal(int number){(void)number;stop_requested=1;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void pause_ms(long ms){struct timespec t={ms/1000,(ms%1000)*1000000};nanosleep(&t,NULL);}
int main(int argc,char **argv){
    bool audio=false;
    if(argc==2&&strcmp(argv[1],"--audio")==0)audio=true;
    else if(argc!=1){fprintf(stderr,"Usage: %s [--audio]\nDefault only validates native session info/reports. Audio sends silence for five seconds.\n",argv[0]);return 2;}
    signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);
    char error[192]={0};rk3_usb_session *session=rk3_usb_open(error,sizeof(error));
    if(!session){fprintf(stderr,"Open: %s\n",error);return 1;}
    RK3NativeInfo info={0};double deadline=now()+10;
    while(now()<deadline&&!stop_requested){
        rk3_usb_info(session,&info);
        if(info.flags&(RK3_NATIVE_CONNECTED|RK3_NATIVE_FAILED))break;
        pause_ms(20);
    }
    if(!(info.flags&RK3_NATIVE_CONNECTED)){
        rk3_usb_status status={0};rk3_usb_get_status(session,&status);
        fprintf(stderr,"Not ready: state=%d error=%d %s\n",status.state,status.error_code,status.error);
        (void)rk3_usb_close_wait(session,5000);return 1;
    }
    printf("Native session ready: generation=%llu, audio=%u/%u mode=%u MIDI=%u/%u\n",
        (unsigned long long)info.generation,info.audio_inputs,info.audio_outputs,info.wire_mode,info.midi_inputs,info.midi_outputs);
    printf("Digital baseline: valid=%u raw=0x%03x\n",(info.digital_snapshot>>31)&1,info.digital_snapshot&0x1ff);
    RK3NativeReportQuery query={RK3_CONTROL_VERSION,0,0};RK3NativeReports reports;
    if(!rk3_usb_reports(session,&query,&reports)){(void)rk3_usb_close_wait(session,5000);return 1;}
    printf("Reports retained=%u latest=%llu lost=%llu\n",reports.count,(unsigned long long)reports.latest_sequence,(unsigned long long)reports.dropped);
    if(!audio){bool closed=rk3_usb_close_wait(session,5000);puts(closed?"Native session drained and closed":"Native session shutdown timed out");return closed?0:1;}
    int32_t result=rk3_usb_audio_start(session,5000);
    if(result){fprintf(stderr,"Actual audio start failed: 0x%08x\n",(unsigned)result);(void)rk3_usb_close_wait(session,5000);return 1;}
    float silence[512]={0},captured[512];uint64_t frames_in=0,frames_out=0,clock_first=0,clock_last=0;
    long double energy=0;float peak=0;uint64_t clock_seed=0;
    deadline=now()+5;
    while(now()<deadline&&!stop_requested){
        size_t sent=rk3_usb_render(session,silence,48);
        size_t received=rk3_usb_capture(session,captured,256);
        frames_in+=received;frames_out+=sent;
        for(size_t i=0;i<received*2;++i){float value=captured[i];if(!isfinite(value)){fprintf(stderr,"Nonfinite captured sample\n");stop_requested=1;break;}energy+=(long double)value*value;if(fabsf(value)>peak)peak=fabsf(value);}
        rk3_usb_clock_snapshot clock;
        if(rk3_usb_clock(session,&clock)){
            if(!clock_seed){clock_first=clock.sample_frame;clock_seed=clock.seed;}
            if(clock.seed!=clock_seed||clock.sample_frame<clock_last){fprintf(stderr,"Unexpected clock reset/backwards position\n");stop_requested=1;break;}
            clock_last=clock.sample_frame;
        }
        rk3_usb_status status;
        rk3_usb_get_status(session,&status);
        if(status.state==RK3_USB_FAILED){fprintf(stderr,"Audio backend failed: %s\n",status.error);stop_requested=1;break;}
        pause_ms(1);
    }
    result=rk3_usb_audio_stop(session,5000);
    bool closed=rk3_usb_close_wait(session,5000);
    if(!closed)fprintf(stderr,"Native session shutdown timed out\n");
    printf("Captured=%llu frames; queued silence=%llu; clock=%llu..%llu seed=%llu; peak=%.6f RMS=%.6Lf; stop=0x%08x\n",
        (unsigned long long)frames_in,(unsigned long long)frames_out,(unsigned long long)clock_first,
        (unsigned long long)clock_last,(unsigned long long)clock_seed,peak,
        frames_in?sqrtl(energy/(frames_in*2)):0,(unsigned)result);
    return result||!closed||stop_requested||frames_in==0||clock_last<=clock_first?1:0;
}
