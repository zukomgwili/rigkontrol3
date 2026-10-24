/* Host ABI checks with a deterministic backend. Never opens USB. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Audio/RK3AudioDriver.c"

static unsigned starts,stops;
static bool fail_start, fail_output, mock_running;
static uint64_t mock_generation=7;
static rk3_usb_clock_snapshot mock_clock={0,1000000000,1};
struct rk3_usb_session { int unused; };
static struct rk3_usb_session mock_session;
rk3_usb_session *rk3_usb_open(char *error,size_t size){(void)error;(void)size;return &mock_session;}
void rk3_usb_close(rk3_usb_session *s){(void)s;}
bool rk3_usb_info(rk3_usb_session *s,RK3NativeInfo *value){
    (void)s;memset(value,0,sizeof(*value));value->version=1;value->flags=RK3_NATIVE_CONNECTED|(mock_running?RK3_NATIVE_AUDIO_RUNNING:0);
    value->generation=mock_generation;value->midi_inputs=1;value->midi_outputs=1;
    value->audio_inputs=value->audio_outputs=2;value->sample_rate=RATE;value->wire_mode=2;return true;
}
bool rk3_usb_reports(rk3_usb_session *s,const RK3NativeReportQuery *q,RK3NativeReports *r){
    (void)s;if(q->version!=1)return false;memset(r,0,sizeof(*r));r->version=1;return true;
}
bool rk3_usb_command_for_generation(rk3_usb_session *s,const uint8_t *b,size_t n,uint64_t generation,uint32_t timeout){
    (void)s;(void)b;(void)n;(void)timeout;return generation==mock_generation;
}
bool rk3_usb_clock(rk3_usb_session *s,rk3_usb_clock_snapshot *c){(void)s;*c=mock_clock;return true;}
int32_t rk3_usb_audio_start(rk3_usb_session *s,uint32_t timeout){(void)s;(void)timeout;++starts;if(!fail_start)mock_running=true;return fail_start?-1:0;}
int32_t rk3_usb_audio_stop(rk3_usb_session *s,uint32_t timeout){(void)s;(void)timeout;++stops;mock_running=false;return 0;}
size_t rk3_usb_capture(rk3_usb_session *s,float *samples,size_t n){(void)s;for(size_t i=0;i<n;++i){samples[2*i]=0.25f;samples[2*i+1]=-0.25f;}return n;}
size_t rk3_usb_render(rk3_usb_session *s,const float *samples,size_t n){(void)s;(void)samples;return fail_output?0:n;}

static OSStatus bridge_get(AudioObjectID device,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 *size,void *out){
    UInt32 written=0;OSStatus error=table.GetPropertyData(driver,device,0,a,qs,q,*size,&written,out);
    if(!error)*size=written;return error;
}
static OSStatus bridge_set(AudioObjectID device,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 size,const void *data){
    return table.SetPropertyData(driver,device,0,a,qs,q,size,data);
}
#define AudioObjectGetPropertyData bridge_get
#define AudioObjectSetPropertyData bridge_set
#include "../NativeClient.h"
#undef AudioObjectGetPropertyData
#undef AudioObjectSetPropertyData
static void properties(void){
    UInt32 size,written;AudioObjectPropertyAddress a={kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,0};
    assert(table.HasProperty(driver,INPUT,0,&a));
    assert(table.GetPropertyDataSize(driver,INPUT,0,&a,0,NULL,&size)==0&&size==sizeof(AudioStreamBasicDescription));
    AudioStreamBasicDescription f={0};
    assert(table.GetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(f),&written,&f)==0);
    assert(f.mSampleRate==48000&&f.mChannelsPerFrame==2&&f.mBytesPerFrame==8);
    unsigned char guard[sizeof(f)];memset(guard,0x5a,sizeof(guard));
    assert(table.GetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(f)-1,&written,guard)==kAudioHardwareBadPropertySizeError);
    for(size_t i=0;i<sizeof(guard);++i)assert(guard[i]==0x5a);
    f.mSampleRate=44100;
    assert(table.SetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(f),&f)==kAudioHardwareUnsupportedOperationError);
    a.mSelector=kAudioDevicePropertyStreams;a.mScope=kAudioObjectPropertyScopeInput;
    AudioObjectID ids[2]={0};
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(ids),&written,ids)==0&&written==4&&ids[0]==INPUT);
    a.mSelector=kAudioDevicePropertyLatency;a.mScope=kAudioObjectPropertyScopeGlobal;
    UInt32 latency=99;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==3072);
    assert(table.GetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==0);
    a.mSelector=RK3_PROPERTY_COMMAND;a.mScope=kAudioObjectPropertyScopeGlobal;
    RK3NativeCommand command={1,4,{7,0,1,0xf8}};
    RK3NativeCommandLease lease={1,0,7};
    assert(rk3_native_send_command(DEVICE,NULL,&command)!=0);
    assert(table.SetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(command),&command)==kAudioHardwareBadPropertySizeError);
    assert(rk3_native_send_command(DEVICE,&lease,&command)==0);
    lease.generation=6;
    assert(rk3_native_send_command(DEVICE,&lease,&command)!=0);
    lease.generation=7;
    RK3NativeCommand leds={1,9,{5,0,0x40,0x40,0,0xff,1,0,0}};
    assert(rk3_native_send_command(DEVICE,&lease,&leds)==0);
    leds.bytes[6]=2;
    assert(rk3_native_send_command(DEVICE,&lease,&leds)!=0);
    command.bytes[1]=1;
    assert(rk3_native_send_command(DEVICE,&lease,&command)!=0);
    a.mSelector=RK3_PROPERTY_INFO;
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&a,0,NULL,&size)==0&&size==sizeof(CFPropertyListRef));
    RK3NativeInfo current={0};
    assert(rk3_native_copy_info(DEVICE,&current)==0&&current.version==1&&current.generation==7);
    a.mSelector=kAudioObjectPropertyCustomPropertyInfoList;
    AudioServerPlugInCustomPropertyInfo metadata[3];
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(metadata),&written,metadata)==0);
    for(unsigned i=0;i<3;++i)assert(metadata[i].mPropertyDataType==kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
    assert(metadata[0].mQualifierDataType==kAudioServerPlugInCustomPropertyDataTypeNone);
    assert(metadata[1].mQualifierDataType==kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
    RK3NativeReportQuery query={1,0,0};RK3NativeReports reports;
    assert(rk3_native_copy_blob(DEVICE,RK3_PROPERTY_REPORTS,&query,SIZE_MAX,&reports,sizeof(reports))==kAudioHardwareIllegalOperationError);
    assert(rk3_native_copy_blob(DEVICE,RK3_PROPERTY_REPORTS,NULL,0,&reports,SIZE_MAX)==kAudioHardwareIllegalOperationError);
    assert(rk3_native_copy_reports(DEVICE,&query,&reports)==0&&reports.version==1);
    a.mSelector=RK3_PROPERTY_REPORTS;CFPropertyListRef wrong=CFSTR("wrong-type"),output=NULL;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(wrong),&wrong,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==NULL);
    CFDataRef short_query=CFDataCreate(NULL,(const UInt8 *)&query,sizeof(query)-1);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(short_query),&short_query,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==NULL);
    CFRelease(short_query);
    a.mSelector=RK3_PROPERTY_COMMAND;
    CFDataRef valid_lease=CFDataCreate(NULL,(const UInt8 *)&lease,sizeof(lease));
    assert(table.SetPropertyData(driver,DEVICE,0,&a,sizeof(valid_lease),&valid_lease,sizeof(wrong),&wrong)==kAudioHardwareBadPropertySizeError);
    CFRelease(valid_lease);
}
static void audio(void){
    usb=&mock_session;
    fail_start=true;assert(table.StartIO(driver,DEVICE,1)!=0&&atomic_load(&running)==0);
    fail_start=false;assert(table.StartIO(driver,DEVICE,1)==0&&atomic_load(&running)==1);
    assert(table.StartIO(driver,DEVICE,1)==0&&starts==2); /* duplicate does not start again */
    assert(table.StartIO(driver,DEVICE,2)==0&&starts==2);
    float buffer[16]={0};
    AudioServerPlugInIOCycleInfo cycle={0};
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,buffer,NULL)==0);
    assert(buffer[0]==0.25f&&buffer[1]==-0.25f);
    fail_output=true;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)!=0);
    fail_output=false;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)==0);
    assert(table.StopIO(driver,DEVICE,1)==0&&stops==0);
    assert(table.StopIO(driver,DEVICE,2)==0&&stops==1);
    Float64 frame;UInt64 time,seed;mach_timebase_info(&timebase);
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&frame==0&&time==1000000000&&seed==1);
    mock_clock.sample_frame=PERIOD+48;mock_clock.host_time+=1000000000;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&frame==PERIOD);
    UInt64 same_time=time;mock_clock.sample_frame+=48;mock_clock.host_time+=50000000;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&time==same_time);
    mock_clock.seed=2;mock_clock.sample_frame=0;mock_clock.host_time+=1000000000;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&seed==2&&frame==0);
    same_time=time;
    /* A delayed caller captured seed1 before another caller committed seed2. */
    mock_clock.seed=1;mock_clock.sample_frame=PERIOD*4;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&seed==2&&frame==0&&time==same_time);
    AudioObjectPropertyAddress address={kAudioDevicePropertyDeviceIsRunning,kAudioObjectPropertyScopeGlobal,0};
    UInt32 physical=99,written=0;mock_running=false;atomic_store(&running,1);
    assert(table.GetPropertyData(driver,DEVICE,0,&address,0,NULL,sizeof(physical),&written,&physical)==0&&physical==0);
}
int main(void){
    assert(RK3AudioFactory(NULL,kAudioServerPlugInTypeUUID)==driver);
    usb=&mock_session;properties();audio();
    puts("Audio driver: host ABI, property bounds, generation lease, client lifetime, PCM and clock checks passed");return 0;
}
