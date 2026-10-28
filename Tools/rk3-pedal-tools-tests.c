/* Offline production-source CLI checks. Every reached device/property/time API
 * is replaced below; valid parser tests stop at the fake client creation.
 * Build once with -DRK3_CALIBRATE_CLI and once without it, linking CoreAudio,
 * CoreMIDI and CoreFoundation. Both variants perform no live device access. */
#include <CoreAudio/CoreAudio.h>
#include <CoreMIDI/CoreMIDI.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "../Driver/NativeClient.h"
#include "../Driver/PedalSettings.h"
static unsigned client_calls,info_calls,status_reads;
static unsigned scenario,save_calls;
static bool full_measurement;
static double clock_value;
static RK3PedalSettings expected;
enum {MATCH=0,FINAL_ERROR,PENDING_SUCCESS,PENDING_TIMEOUT,STALE_GENERATION,
      WRONG_SETTINGS,REQUEST_REPLACED,DISCONNECTED,BAD_STATUS_VERSION,
      BAD_STATUS_SIZE,BAD_REQUEST_SIZE,BAD_REQUEST_TYPE,GENERATION_DURING_ACK,STALE_PROVIDER,BAD_STATUS_RESERVED,INSUFFICIENT_SPAN,RAW_DROP,MEASUREMENT_DISCONNECT,
      MEASUREMENT_SETTINGS_CHANGE,APPLICATION_FAILURE,SAVE_ERROR};
static OSStatus fake_client(CFStringRef name,MIDINotifyProc notify,void *context,MIDIClientRef *client) {
    (void)name;(void)notify;(void)context;++client_calls;
    if(full_measurement){*client=9;return 0;}return -123;
}
static OSStatus fake_info(AudioObjectID audio,RK3NativeInfo *info) {
    (void)audio;memset(info,0,sizeof(*info));++info_calls;
    info->version=1;info->generation=7;info->flags=RK3_NATIVE_CONNECTED;
    if(scenario==DISCONNECTED)info->flags=0;
    if(scenario==GENERATION_DURING_ACK&&info_calls>1)info->generation=8;
    if(scenario==MEASUREMENT_DISCONNECT&&clock_value>=1)info->generation=8;
    return 0;
}
static OSStatus fake_data(MIDIObjectRef object,CFStringRef key,CFDataRef *data) {
    (void)object;
    if(CFEqual(key,CFSTR(RK3_CONTROLLER_PEDAL_SETTINGS_KEY))) {
        RK3PedalSettings current=expected;
        if(scenario==REQUEST_REPLACED||
           (scenario==MEASUREMENT_SETTINGS_CHANGE&&clock_value>=1))current.ring_mask^=1;
        if(scenario==BAD_REQUEST_TYPE) {*data=(CFDataRef)CFRetain(CFSTR("bad type"));return 0;}
        *data=CFDataCreate(NULL,(const UInt8 *)&current,
            scenario==BAD_REQUEST_SIZE?sizeof(current)-1:sizeof(current));return 0;
    }
    assert(CFEqual(key,CFSTR(RK3_CONTROLLER_PEDAL_STATUS_KEY)));
    ++status_reads;
    RK3PedalStatus status={.version=1,.generation=7,.audio_device=1,.settings=expected};
    if(scenario==FINAL_ERROR||(scenario==APPLICATION_FAILURE&&save_calls))status.status=EIO;
    if(scenario==PENDING_SUCCESS&&status_reads<3)status.status=EAGAIN;
    if(scenario==PENDING_TIMEOUT)status.status=EINPROGRESS;
    if(scenario==STALE_GENERATION)status.generation=6;
    if(scenario==WRONG_SETTINGS)status.settings.switch_mask^=2;
    if(scenario==BAD_STATUS_VERSION)status.version=2;
    if(scenario==STALE_PROVIDER)status.audio_device=2;
    if(scenario==BAD_STATUS_RESERVED)status.reserved=1;
    *data=CFDataCreate(NULL,(const UInt8 *)&status,
        scenario==BAD_STATUS_SIZE?sizeof(status)-1:sizeof(status));return 0;
}
static OSStatus fake_save(MIDIObjectRef object,CFStringRef key,CFDataRef data) {
    (void)object;assert(CFEqual(key,CFSTR(RK3_CONTROLLER_PEDAL_SETTINGS_KEY)));
    ++save_calls;if(scenario==SAVE_ERROR)return -123;
    assert(CFDataGetLength(data)==sizeof(expected));
    memcpy(&expected,CFDataGetBytePtr(data),sizeof(expected));return 0;
}
static OSStatus fake_dispose(MIDIClientRef client) {(void)client;return 0;}
static ItemCount fake_count(void) {return 1;}
static MIDIDeviceRef fake_device(ItemCount index) {assert(index==0);return 2;}
static OSStatus fake_string(MIDIObjectRef object,CFStringRef key,CFStringRef *value) {
    (void)object;assert(CFEqual(key,CFSTR(RK3_CONTROLLER_IDENTITY_KEY)));
    *value=CFRetain(CFSTR(RK3_NATIVE_DEVICE_UID ".controller"));return 0;
}
static OSStatus fake_audio_size(AudioObjectID object,const AudioObjectPropertyAddress *a,
                                UInt32 qualifier_size,const void *qualifier,UInt32 *size) {
    (void)object;(void)qualifier_size;(void)qualifier;
    assert(a->mSelector==kAudioHardwarePropertyDevices);*size=sizeof(AudioObjectID);return 0;
}
static OSStatus fake_audio_data(AudioObjectID object,const AudioObjectPropertyAddress *a,
                                UInt32 qualifier_size,const void *qualifier,UInt32 *size,void *output) {
    (void)object;(void)qualifier_size;(void)qualifier;
    if(a->mSelector==kAudioHardwarePropertyDevices) {
        assert(*size==sizeof(AudioObjectID));*(AudioObjectID *)output=1;return 0;
    }
    assert(a->mSelector==kAudioDevicePropertyDeviceUID&&*size==sizeof(CFStringRef));
    *(CFStringRef *)output=CFRetain(CFSTR(RK3_NATIVE_DEVICE_UID));return 0;
}
#ifdef RK3_CALIBRATE_CLI
static unsigned test_axis,report_calls;
static uint32_t test_low,test_high;
static OSStatus fake_reports(AudioObjectID device,const RK3NativeReportQuery *query,
                              RK3NativeReports *batch) {
    (void)device;memset(batch,0,sizeof(*batch));batch->version=1;
    ++report_calls;
    batch->latest_sequence=batch->next_sequence=100+report_calls;
    if(report_calls==1) {assert(query->after_sequence==0);return 0;}
    if(scenario==RAW_DROP&&report_calls>=3) {batch->dropped=1;return 0;}
    batch->count=1;batch->reports[0].length=7;batch->reports[0].bytes[0]=3;
    uint32_t raw=(report_calls&1)?test_low:test_high;
    if(scenario==INSUFFICIENT_SPAN)raw=test_low;
    batch->reports[0].bytes[1+test_axis*2]=(uint8_t)(raw>>8);
    batch->reports[0].bytes[2+test_axis*2]=(uint8_t)raw;
    return 0;
}
#endif
static int fake_clock(clockid_t id,struct timespec *t) {
    (void)id;t->tv_sec=(time_t)clock_value;t->tv_nsec=(long)((clock_value-t->tv_sec)*1e9);return 0;
}
static int fake_sleep(const struct timespec *t,struct timespec *left) {
    (void)left;clock_value+=t->tv_sec+t->tv_nsec/1e9;return 0;
}
#define MIDIClientCreate fake_client
#define MIDIObjectGetDataProperty fake_data
#define MIDIObjectSetDataProperty fake_save
#define MIDIClientDispose fake_dispose
#define MIDIGetNumberOfDevices fake_count
#define MIDIGetDevice fake_device
#define MIDIObjectGetStringProperty fake_string
#define AudioObjectGetPropertyDataSize fake_audio_size
#define AudioObjectGetPropertyData fake_audio_data
#ifdef RK3_CALIBRATE_CLI
#define rk3_native_copy_reports fake_reports
#endif
#define rk3_native_copy_info fake_info
#define clock_gettime fake_clock
#define nanosleep fake_sleep
#define main rk3_cli_main
#ifndef RK3_TOOL_SOURCE
#ifdef RK3_CALIBRATE_CLI
#define RK3_TOOL_SOURCE "rk3-calibrate.c"
#else
#define RK3_TOOL_SOURCE "rk3-pedals.c"
#endif
#endif
#include RK3_TOOL_SOURCE
#undef main
static void test_arguments(void) {
    struct Case {int argc;const char *argv[9];int status,clients;};
#ifdef RK3_CALIBRATE_CLI
    const struct Case cases[]={
        {2,{"rk3","--help"},0,0},
        {2,{"rk3","--axis"},2,0},
        {3,{"rk3","--axis","3"},2,0},
        {3,{"rk3","--axis","-1"},2,0},
        {3,{"rk3","--seconds","4"},2,0},
        {3,{"rk3","--seconds","121"},2,0},
        {3,{"rk3","--seconds","5x"},2,0},
        {5,{"rk3","--axis","0","--axis","1"},2,0},
        {1,{"rk3"},1,1},
        {5,{"rk3","--axis","0","--seconds","5"},1,1},
        {5,{"rk3","--seconds","120","--axis","1"},1,1},
        {3,{"rk3","--axis","2"},1,1}
    };
#else
    const struct Case cases[]={
        {2,{"rk3","--help"},0,0},
        {1,{"rk3"},2,0},
        {3,{"rk3","--axis","0"},2,0},
        {4,{"rk3","--axis","3","--ring"},2,0},
        {3,{"rk3","--show","--ring"},2,0},
        {5,{"rk3","--axis","0","--tip","--ring"},2,0},
        {5,{"rk3","--axis","0","--switch","--continuous"},2,0},
        {4,{"rk3","--axis","","--tip"},2,0},
        {2,{"rk3","--show"},1,1},
        {4,{"rk3","--axis","0","--ring"},1,1},
        {5,{"rk3","--axis","1","--switch","--tip"},1,1},
        {5,{"rk3","--axis","2","--continuous","--ring"},1,1}
    };
#endif
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        client_calls=0;
        assert(rk3_cli_main(cases[i].argc,(char **)cases[i].argv)==cases[i].status);
        assert(client_calls==(unsigned)cases[i].clients);
    }
}
static void test_acknowledgements(void) {
    const struct {unsigned scenario;bool result;} cases[]={
        {MATCH,true},{FINAL_ERROR,false},{PENDING_SUCCESS,true},{PENDING_TIMEOUT,false},
        {STALE_GENERATION,false},{WRONG_SETTINGS,false},{REQUEST_REPLACED,false},
        {DISCONNECTED,false},{BAD_STATUS_VERSION,false},{BAD_STATUS_SIZE,false},
        {BAD_REQUEST_SIZE,false},{BAD_REQUEST_TYPE,false},{GENERATION_DURING_ACK,false},
        {STALE_PROVIDER,false},{BAD_STATUS_RESERVED,false}
    };
    expected=rk3_pedal_settings_initial();expected.ring_mask=1;
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        scenario=cases[i].scenario;clock_value=0;info_calls=status_reads=0;
        assert(wait_applied(1,2,7,&expected)==cases[i].result);
        assert(clock_value<=5.1);
    }
}
#ifdef RK3_CALIBRATE_CLI
static void test_measurement(void) {
    const struct {unsigned scenario;int result,saves;} cases[]={
        {MATCH,0,1},{INSUFFICIENT_SPAN,1,0},{RAW_DROP,1,0},
        {MEASUREMENT_DISCONNECT,1,0},{MEASUREMENT_SETTINGS_CHANGE,1,0},
        {APPLICATION_FAILURE,1,1},{SAVE_ERROR,1,1}
    };
    full_measurement=true;
    for(unsigned axis=0;axis<3;++axis) {
        const unsigned total=axis==0?sizeof(cases)/sizeof(cases[0]):1;
        for(unsigned c=0;c<total;++c) {
            scenario=cases[c].scenario;clock_value=0;info_calls=status_reads=save_calls=report_calls=0;
            test_axis=axis;test_low=axis==2?88:204;test_high=axis==2?648:3990;
            expected=rk3_pedal_settings_initial();expected.ring_mask=5;expected.switch_mask=2;
            RK3PedalSettings before=expected;
            char axis_string[2]={(char)('0'+axis),0};
            char *argv[]={"rk3","--axis",axis_string,"--seconds","5"};
            assert(rk3_cli_main(5,argv)==cases[c].result);
            assert(save_calls==(unsigned)cases[c].saves);
            if(!cases[c].result) {
                uint32_t raw_max=axis==2?1023:4095;
                before.low[axis]=(float)((double)test_low/raw_max);
                before.high[axis]=(float)((double)test_high/raw_max);
                assert(!memcmp(&before,&expected,sizeof(expected)));
            }
        }
    }
    full_measurement=false;
    puts("PASS: calibration axes0/1/2 and six measurement/save/application failure paths");
}
#endif
int main(void) {
    test_arguments();test_acknowledgements();
#ifdef RK3_CALIBRATE_CLI
    test_measurement();
#endif
    puts("PASS: 12 parser cases and 15 applied-status cases; device APIs stubbed");return 0;
}
