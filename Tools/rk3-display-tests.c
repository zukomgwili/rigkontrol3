/* Execute the production CLI with every device/property/time API stubbed.
 * No MIDI server, driver initialization, audio start or USB access occurs. */
#include <CoreAudio/CoreAudio.h>
#include <CoreMIDI/CoreMIDI.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../Driver/NativeClient.h"
#include "../Driver/Display.h"

enum {MATCH,CLIENT_ERROR,PENDING_SUCCESS,PENDING_TIMEOUT,SET_ERROR,READ_ERROR,
      BAD_STATUS_TYPE,BAD_STATUS_SIZE,BAD_STATUS_VERSION,BAD_STATUS_RESERVED,
      BAD_STATUS_PADDING,NEW_SESSION,NEW_EPOCH,NEW_GENERATION,NEW_PROVIDER,
      NEW_CONTROLLER,DISCONNECTED,REQUEST_REPLACED,REQUEST_REPLACED_AT_ACK,
      BAD_REQUEST_TYPE,BAD_REQUEST_SIZE,BAD_REQUEST_VERSION,WRONG_BYTE,
      UNRELATED_ACK,DRIVER_ERROR,OPEN_PENDING,OPEN_BAD_STATUS,DUPLICATE_AUDIO,
      DUPLICATE_CONTROLLER,INFO_ERROR,DISPOSE_ERROR,RANDOM_COLLISION};
static unsigned scenario,client_calls,dispose_calls,save_calls,status_reads,request_reads,random_calls,device_calls;
static unsigned pending_reads;
static double clock_value;
static RK3DisplayRequest mailbox;
static RK3DisplayStatus initial;
static char output[8192];static size_t output_size;

static int fake_printf(const char *format,...) {
    va_list args;va_start(args,format);
    int count=vsnprintf(output+output_size,sizeof(output)-output_size,format,args);va_end(args);
    assert(count>=0&&(size_t)count<sizeof(output)-output_size);output_size+=(size_t)count;return count;
}
static int fake_puts(const char *s) {return fake_printf("%s\n",s);}
static int fake_fprintf(FILE *stream,const char *format,...) {(void)stream;(void)format;return 0;}
static OSStatus fake_client(CFStringRef name,MIDINotifyProc notify,void *context,MIDIClientRef *client) {
    (void)name;(void)notify;(void)context;++client_calls;
    if(scenario==CLIENT_ERROR)return -1;
    *client=9;return 0;
}
static OSStatus fake_dispose(MIDIClientRef client) {assert(client==9);++dispose_calls;return scenario==DISPOSE_ERROR?-1:0;}
static ItemCount fake_count(void) {++device_calls;return scenario==DUPLICATE_CONTROLLER?2:1;}
static MIDIDeviceRef fake_device(ItemCount index) {
    return (scenario==NEW_CONTROLLER&&save_calls?3:2)+(MIDIDeviceRef)index;
}
static OSStatus fake_string(MIDIObjectRef object,CFStringRef key,CFStringRef *value) {
    assert(object>=2&&CFEqual(key,CFSTR(RK3_CONTROLLER_IDENTITY_KEY)));
    *value=CFRetain(CFSTR(RK3_NATIVE_DEVICE_UID ".controller"));return 0;
}
static OSStatus fake_audio_size(AudioObjectID object,const AudioObjectPropertyAddress *a,
                               UInt32 qualifier_size,const void *qualifier,UInt32 *size) {
    (void)object;(void)qualifier_size;(void)qualifier;++device_calls;
    assert(a->mSelector==kAudioHardwarePropertyDevices);
    *size=(UInt32)(sizeof(AudioObjectID)*(scenario==DUPLICATE_AUDIO?2:1));return 0;
}
static OSStatus fake_audio_data(AudioObjectID object,const AudioObjectPropertyAddress *a,
                               UInt32 qualifier_size,const void *qualifier,UInt32 *size,void *data) {
    (void)object;(void)qualifier_size;(void)qualifier;
    if(a->mSelector==kAudioHardwarePropertyDevices) {
        AudioObjectID *ids=data;ids[0]=scenario==NEW_PROVIDER&&save_calls?4:1;
        if(scenario==DUPLICATE_AUDIO)ids[1]=4;
    } else {
        assert(a->mSelector==kAudioDevicePropertyDeviceUID&&*size==sizeof(CFStringRef));
        *(CFStringRef *)data=CFRetain(CFSTR(RK3_NATIVE_DEVICE_UID));
    }
    return 0;
}
static OSStatus fake_info(AudioObjectID audio,RK3NativeInfo *info) {
    (void)audio;memset(info,0,sizeof(*info));info->version=1;info->generation=7;info->flags=RK3_NATIVE_CONNECTED;
    if(save_calls&&scenario==NEW_GENERATION)info->generation=8;
    if(save_calls&&scenario==DISCONNECTED)info->flags=0;
    return save_calls&&scenario==INFO_ERROR?-5:0;
}
static OSStatus fake_data(MIDIObjectRef object,CFStringRef key,CFDataRef *data) {
    (void)object;*data=NULL;
    if(CFEqual(key,CFSTR(RK3_CONTROLLER_DISPLAY_REQUEST_KEY))) {
        ++request_reads;RK3DisplayRequest copy=mailbox;
        if(scenario==REQUEST_REPLACED||(scenario==REQUEST_REPLACED_AT_ACK&&request_reads>=2))++copy.request_id;
        if(scenario==BAD_REQUEST_VERSION)copy.version=2;
        if(scenario==BAD_REQUEST_TYPE) {*data=(CFDataRef)CFRetain(CFSTR("bad"));return 0;}
        *data=CFDataCreate(NULL,(const UInt8 *)&copy,scenario==BAD_REQUEST_SIZE?sizeof(copy)-1:sizeof(copy));return 0;
    }
    assert(CFEqual(key,CFSTR(RK3_CONTROLLER_DISPLAY_STATUS_KEY)));++status_reads;
    RK3DisplayStatus status=initial;
    if(scenario==OPEN_PENDING)status.status=EINPROGRESS;
    if(scenario==OPEN_BAD_STATUS)status.version=2;
    if(save_calls) {
        ++pending_reads;status.request_id=mailbox.request_id;status.content[mailbox.index]=(uint8_t)mailbox.value;
        if(scenario==READ_ERROR)return -8;
        if(scenario==PENDING_TIMEOUT||(scenario==PENDING_SUCCESS&&pending_reads<3))status.status=EINPROGRESS;
        if(scenario==NEW_SESSION)++status.session;
        if(scenario==NEW_EPOCH)++status.epoch;
        if(scenario==NEW_GENERATION)++status.generation;
        if(scenario==BAD_STATUS_VERSION)status.version=2;
        if(scenario==BAD_STATUS_RESERVED)status.reserved=1;
        if(scenario==BAD_STATUS_PADDING)status.reserved_bytes[3]=1;
        if(scenario==WRONG_BYTE)status.content[mailbox.index]^=1;
        if(scenario==UNRELATED_ACK)++status.request_id;
        if(scenario==DRIVER_ERROR)status.status=EIO;
        if(scenario==BAD_STATUS_TYPE) {*data=(CFDataRef)CFRetain(CFSTR("bad"));return 0;}
    }
    *data=CFDataCreate(NULL,(const UInt8 *)&status,save_calls&&scenario==BAD_STATUS_SIZE?sizeof(status)-1:sizeof(status));return 0;
}
static OSStatus fake_set(MIDIObjectRef object,CFStringRef key,CFDataRef data) {
    assert(object==2&&CFEqual(key,CFSTR(RK3_CONTROLLER_DISPLAY_REQUEST_KEY)));
    ++save_calls;if(scenario==SET_ERROR)return -9;
    assert(CFGetTypeID(data)==CFDataGetTypeID()&&CFDataGetLength(data)==sizeof(mailbox));
    memcpy(&mailbox,CFDataGetBytePtr(data),sizeof(mailbox));assert(rk3_display_request_valid(&mailbox));
    assert(rk3_display_same_lease(&mailbox,&initial));return 0;
}
static void fake_random(void *bytes,size_t size) {
    assert(size==sizeof(uint64_t));++random_calls;
    uint64_t value=scenario==RANDOM_COLLISION&&random_calls<=2?(random_calls==1?0:initial.request_id):101;
    memcpy(bytes,&value,size);
}
static int fake_clock(clockid_t clock,struct timespec *t) {
    assert(clock==CLOCK_MONOTONIC);t->tv_sec=(time_t)clock_value;
    t->tv_nsec=(long)((clock_value-t->tv_sec)*1e9);return 0;
}
static int fake_sleep(const struct timespec *t,struct timespec *left) {
    (void)left;clock_value+=t->tv_sec+t->tv_nsec/1e9;return 0;
}
#define MIDIClientCreate fake_client
#define MIDIClientDispose fake_dispose
#define MIDIGetNumberOfDevices fake_count
#define MIDIGetDevice fake_device
#define MIDIObjectGetStringProperty fake_string
#define MIDIObjectGetDataProperty fake_data
#define MIDIObjectSetDataProperty fake_set
#define AudioObjectGetPropertyDataSize fake_audio_size
#define AudioObjectGetPropertyData fake_audio_data
#define rk3_native_copy_info fake_info
#define arc4random_buf fake_random
#define clock_gettime fake_clock
#define nanosleep fake_sleep
#define printf fake_printf
#define puts fake_puts
#define fprintf fake_fprintf
#define main rk3_display_main
#include "rk3-display.c"
#undef main
#undef puts

static void reset(unsigned next) {
    scenario=next;client_calls=dispose_calls=save_calls=status_reads=request_reads=random_calls=device_calls=pending_reads=0;
    clock_value=0;output_size=0;output[0]=0;memset(&mailbox,0,sizeof(mailbox));
    initial=(RK3DisplayStatus){.version=1,.session=55,.epoch=3,.generation=7,.audio_device=1,
        .request_id=17,.content={0,64,128,255}};
}
static void arguments(void) {
    const struct {int argc;const char *argv[8];int result;unsigned clients;} cases[]={
        {2,{"rk3","--help"},0,0},{1,{"rk3"},2,0},
        {2,{"rk3","--index"},2,0},{3,{"rk3","--index","0"},2,0},
        {3,{"rk3","--value","0"},2,0},{3,{"rk3","--show","--help"},2,0},
        {5,{"rk3","--index","4","--value","1"},2,0},
        {5,{"rk3","--index","-1","--value","1"},2,0},
        {5,{"rk3","--index","0","--value","256"},2,0},
        {5,{"rk3","--index","0","--value","+1"},2,0},
        {5,{"rk3","--index","0","--value"," 1"},2,0},
        {5,{"rk3","--index","0","--value",""},2,0},
        {7,{"rk3","--index","0","--value","1","--value","2"},2,0},
        {5,{"rk3","--index","0","--index","1"},2,0},
        {2,{"rk3","--show"},1,1},
        {5,{"rk3","--index","0","--value","0"},1,1},
        {5,{"rk3","--value","255","--index","3"},1,1}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        reset(CLIENT_ERROR);assert(rk3_display_main(cases[i].argc,(char **)cases[i].argv)==cases[i].result);
        assert(client_calls==cases[i].clients&&!device_calls&&!save_calls&&!dispose_calls);
    }
}
static void acknowledgements(void) {
    const unsigned failures[]={SET_ERROR,READ_ERROR,BAD_STATUS_TYPE,BAD_STATUS_SIZE,BAD_STATUS_VERSION,
        BAD_STATUS_RESERVED,BAD_STATUS_PADDING,NEW_SESSION,NEW_EPOCH,NEW_GENERATION,NEW_PROVIDER,
        NEW_CONTROLLER,DISCONNECTED,REQUEST_REPLACED,REQUEST_REPLACED_AT_ACK,BAD_REQUEST_TYPE,
        BAD_REQUEST_SIZE,BAD_REQUEST_VERSION,WRONG_BYTE,UNRELATED_ACK,DRIVER_ERROR,PENDING_TIMEOUT,
        INFO_ERROR,DISPOSE_ERROR};
    char *argv[]={"rk3","--index","1","--value","255"};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);++i) {
        reset(failures[i]);assert(rk3_display_main(5,argv)==1);
        assert(client_calls==1&&dispose_calls==1&&save_calls==1&&clock_value<=5.1);
        if(failures[i]!=DISPOSE_ERROR)assert(!strstr(output,"Logical display content"));
    }
    for(unsigned s=0;s<3;++s) {
        reset(s==0?MATCH:s==1?PENDING_SUCCESS:RANDOM_COLLISION);
        assert(rk3_display_main(5,argv)==0&&save_calls==1&&dispose_calls==1);
        assert(strstr(output,"indices 0..3): 0 255 128 255")&&strstr(output,"not hardware readback or USB completion"));
        if(s==2)assert(random_calls==3);
    }
    for(unsigned index=0;index<4;++index)for(unsigned value=0;value<=255;value+=255) {
        reset(MATCH);initial.content[index]=(uint8_t)value;char number[2]={(char)('0'+index),0};
        char *args[]={"rk3","--index",number,"--value",value?"255":"0"};
        assert(rk3_display_main(5,args)==0&&mailbox.index==index&&mailbox.value==value&&save_calls==1);
    }
}
static void opening_and_show(void) {
    char *show_args[]={"rk3","--show"};
    reset(MATCH);assert(rk3_display_main(2,show_args)==0&&!save_calls&&!random_calls);
    assert(strstr(output,"indices 0..3): 0 64 128 255")&&strstr(output,"not hardware readback"));
    const unsigned failures[]={OPEN_PENDING,OPEN_BAD_STATUS,DUPLICATE_AUDIO,DUPLICATE_CONTROLLER};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);++i) {
        reset(failures[i]);assert(rk3_display_main(2,show_args)==1&&dispose_calls==1);
        assert(!save_calls&&!random_calls&&clock_value>=10&&clock_value<=10.1);
    }
}
int main(void) {
    arguments();acknowledgements();opening_and_show();
    puts("PASS: display CLI production body; 17 parser, 35 ACK/boundary and 5 logical/open cases, all device APIs stubbed");
    return 0;
}
