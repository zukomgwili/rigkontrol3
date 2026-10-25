/* Host ABI checks with a deterministic backend. Never opens USB. */
#include <assert.h>
#include <IOKit/IOReturn.h>
#include <stdio.h>
#include <string.h>
#include "../Audio/RK3AudioDriver.c"
#include "../InputGain.h"
static RK3InputGain mock_gain;

static unsigned starts,stops,capture_calls,render_calls,opens,output_active_changes;
static bool backend_output_active=true;
static uint64_t captured_first,rendered_first;
static size_t captured_frames,rendered_frames;
static rk3_usb_audio_panic_handler registered_panic;
static void *registered_context;
static bool fail_start, fail_output, mock_running, mock_connected=true;
static uint64_t mock_generation=7;
static rk3_usb_clock_snapshot mock_clock={.sample_frame=0,.host_time=1000000000,.seed=1,.zero_sample_frame=0,.zero_host_time=1000000000};
struct rk3_usb_session { int unused; };
static struct rk3_usb_session mock_session;
rk3_usb_session *rk3_usb_open(char *error,size_t size){(void)error;(void)size;++opens;return &mock_session;}
void rk3_usb_close(rk3_usb_session *s){(void)s;}
bool rk3_usb_info(rk3_usb_session *s,RK3NativeInfo *value){
    (void)s;memset(value,0,sizeof(*value));value->version=1;value->flags=(mock_connected?RK3_NATIVE_CONNECTED:0)|(mock_running?RK3_NATIVE_AUDIO_RUNNING:0);
    value->generation=mock_generation;value->midi_inputs=1;value->midi_outputs=1;
    value->audio_inputs=value->audio_outputs=2;value->sample_rate=RATE;value->wire_mode=2;return true;
}
int32_t rk3_usb_input_gain_get(rk3_usb_session *s,uint64_t generation,uint32_t channel,uint32_t *bits) {
    (void)s;if(!mock_connected)return ENODEV;if(generation!=mock_generation)return ESTALE;
    return rk3_input_gain_get(&mock_gain,2,channel,bits)?0:EINVAL;
}
int32_t rk3_usb_input_gain_set(rk3_usb_session *s,uint64_t generation,uint32_t channel,uint32_t bits,bool *changed) {
    (void)s;*changed=false;if(!mock_connected)return ENODEV;if(generation!=mock_generation)return ESTALE;
    uint32_t previous=0;(void)rk3_input_gain_get(&mock_gain,2,channel,&previous);
    if(!rk3_input_gain_set(&mock_gain,2,2,channel,bits))return EINVAL;
    *changed=previous!=bits;return 0;
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
void rk3_usb_set_output_active(rk3_usb_session *s,bool active){assert(s==&mock_session);backend_output_active=active;++output_active_changes;}
bool rk3_usb_set_audio_panic_handler(rk3_usb_session *s,rk3_usb_audio_panic_handler handler,void *context){assert(s==&mock_session);registered_panic=handler;registered_context=context;return true;}
size_t rk3_usb_capture_at(rk3_usb_session *s,float *samples,uint64_t first,size_t n){
    assert(s==&mock_session);++capture_calls;captured_first=first;captured_frames=n;
    for(size_t i=0;i<n;++i){samples[2*i]=0.25f;samples[2*i+1]=-0.25f;}return n;
}
size_t rk3_usb_render_at(rk3_usb_session *s,const float *samples,uint64_t first,size_t n){
    assert(s==&mock_session);(void)samples;++render_calls;rendered_first=first;rendered_frames=n;return fail_output?0:n;
}

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
    a.mSelector=kAudioDevicePropertyLatency;a.mScope=kAudioObjectPropertyScopeInput;
    UInt32 latency=99;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==68);
    a.mScope=kAudioObjectPropertyScopeOutput;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==136);
    a.mScope=kAudioObjectPropertyScopeGlobal;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==136);
    assert(table.GetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==0);
    assert(table.GetPropertyData(driver,OUTPUT,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==0);
    a.mSelector=kAudioDevicePropertySafetyOffset;
    for(unsigned i=0;i<3;++i){
        a.mScope=i==0?kAudioObjectPropertyScopeInput:i==1?kAudioObjectPropertyScopeOutput:kAudioObjectPropertyScopeGlobal;
        assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==136);
    }
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
    AudioServerPlugInCustomPropertyInfo metadata[4];
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(metadata),&written,metadata)==0);
    for(unsigned i=0;i<4;++i)assert(metadata[i].mPropertyDataType==kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
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
/* Tracer through NativeClient's CFData bridge into the production HAL table. */
static void input_gain_transport_test(void) {
    struct { uint32_t version,channel; uint64_t generation; } query={1,0,7};
    struct { uint32_t version,channel; uint64_t generation; uint32_t value_bits,reserved; } value={0};
    assert(rk3_native_copy_blob(DEVICE,UINT32_C(0x6e676169),&query,sizeof(query),&value,sizeof(value))==0);
    assert(value.version==1&&value.channel==0&&value.generation==7&&value.value_bits==UINT32_C(0x3f800000));
}
static void audio(void){
    usb=&mock_session;
    fail_start=true;assert(table.StartIO(driver,DEVICE,1)!=0&&atomic_load(&running)==0);
    fail_start=false;assert(table.StartIO(driver,DEVICE,1)==0&&atomic_load(&running)==1);
    assert(table.StartIO(driver,DEVICE,1)==0&&starts==2); /* duplicate does not start again */
    assert(table.StartIO(driver,DEVICE,2)==0&&starts==2);
    float buffer[16]={0};
    AudioServerPlugInIOCycleInfo cycle={0};
    cycle.mInputTime.mFlags=cycle.mOutputTime.mFlags=kAudioTimeStampSampleTimeValid;
    cycle.mInputTime.mSampleTime=12345.75;cycle.mOutputTime.mSampleTime=67890.25;
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,buffer,NULL)==0);
    assert(buffer[0]==0.25f&&buffer[1]==-0.25f);
    assert(captured_first==12345&&captured_frames==8);
    fail_output=true;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)!=0);
    fail_output=false;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)==0);
    assert(rendered_first==67890&&rendered_frames==8);
    /* Modern startup can request negative input sample positions. Keep their
     * signed-floor ring position, rather than replacing them with FIFO order. */
    cycle.mInputTime.mSampleTime=-68.25;
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,buffer,NULL)==0);
    assert(captured_first==(uint64_t)(int64_t)-69&&captured_first%PERIOD==PERIOD-69);
    cycle.mInputTime.mSampleTime=-0x1p63;
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,buffer,NULL)==0);
    assert(captured_first==UINT64_C(0x8000000000000000)&&captured_first%PERIOD==0);
    const double invalid_samples[]={NAN,INFINITY,-INFINITY,0x1p63,-0x1p63-2048.0};
    unsigned calls=capture_calls;
    for(size_t i=0;i<sizeof(invalid_samples)/sizeof(invalid_samples[0]);++i){
        cycle.mInputTime.mSampleTime=invalid_samples[i];
        assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,buffer,NULL)!=0);
    }
    assert(capture_calls==calls);
    cycle.mInputTime.mSampleTime=55;cycle.mInputTime.mFlags=kAudioTimeStampHostTimeValid;
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,buffer,NULL)!=0);
    cycle.mInputTime.mFlags=kAudioTimeStampSampleTimeValid;
    assert(io(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,NULL,buffer,NULL)!=0);
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,NULL,NULL)!=0);
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,PERIOD+1,&cycle,buffer,NULL)!=0);
    assert(capture_calls==calls);
    unsigned outputs=render_calls;
    cycle.mOutputTime.mFlags=0;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)!=0);
    cycle.mOutputTime.mFlags=kAudioTimeStampSampleTimeValid;cycle.mOutputTime.mSampleTime=NAN;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)!=0);
    assert(render_calls==outputs);
    cycle.mOutputTime.mSampleTime=456.75;cycle.mInputTime.mSampleTime=NAN;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)==0);
    assert(rendered_first==456); /* Only the selected stream timestamp matters. */
    assert(table.StopIO(driver,DEVICE,1)==0&&stops==0);
    assert(table.StopIO(driver,DEVICE,2)==0&&stops==1);
    Float64 frame;UInt64 time,seed;mach_timebase_info(&timebase);
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&frame==0&&time==1000000000&&seed==1);
    mock_clock.sample_frame=PERIOD+49;mock_clock.host_time+=1000000000;
    mock_clock.zero_sample_frame=PERIOD;mock_clock.zero_host_time=1900123456;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&frame==PERIOD&&time==1900123456);
    /* A variable USB packet count makes the exact boundary differ from nominal
     * rate back-extrapolation. Raw output progression must not replace it. */
    UInt64 same_time=time;mock_clock.sample_frame+=44;mock_clock.host_time+=50000000;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&time==same_time);
    mock_clock.seed=2;mock_clock.sample_frame=0;mock_clock.host_time+=1000000000;
    mock_clock.zero_sample_frame=0;mock_clock.zero_host_time=2900654321;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&seed==2&&frame==0&&time==2900654321);
    same_time=time;
    /* A delayed caller captured seed1 before another caller committed seed2. */
    mock_clock.seed=1;mock_clock.sample_frame=PERIOD*4;mock_clock.zero_sample_frame=PERIOD*4;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==0&&seed==2&&frame==0&&time==same_time);
    mock_clock.seed=2;mock_clock.zero_sample_frame=0;mock_clock.zero_host_time=0;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==kAudioHardwareNotRunningError);
    mock_clock.zero_host_time=same_time;mock_clock.zero_sample_frame=PERIOD+1;
    assert(table.GetZeroTimeStamp(driver,DEVICE,1,&frame,&time,&seed)==kAudioHardwareIllegalOperationError);
    mock_clock.zero_sample_frame=0;
    AudioObjectPropertyAddress address={kAudioDevicePropertyDeviceIsRunning,kAudioObjectPropertyScopeGlobal,0};
    UInt32 physical=99,written=0;mock_running=false;atomic_store(&running,1);
    assert(table.GetPropertyData(driver,DEVICE,0,&address,0,NULL,sizeof(physical),&written,&physical)==0&&physical==0);
    atomic_store(&running,0);
}
static void stream_activity(void){
    AudioObjectPropertyAddress address={kAudioStreamPropertyIsActive,kAudioObjectPropertyScopeGlobal,0};
    UInt32 value=0,written=0;
    assert(table.SetPropertyData(driver,INPUT,0,&address,0,NULL,sizeof(value),&value)==0);
    assert(table.SetPropertyData(driver,OUTPUT,0,&address,0,NULL,sizeof(value),&value)==0);
    assert(output_active_changes==1&&!backend_output_active);
    value=99;assert(table.GetPropertyData(driver,INPUT,0,&address,0,NULL,sizeof(value),&written,&value)==0&&value==0);
    value=99;assert(table.GetPropertyData(driver,OUTPUT,0,&address,0,NULL,sizeof(value),&written,&value)==0&&value==0);
    AudioServerPlugInIOCycleInfo cycle={0};cycle.mInputTime.mFlags=cycle.mOutputTime.mFlags=kAudioTimeStampSampleTimeValid;
    cycle.mInputTime.mSampleTime=234;cycle.mOutputTime.mSampleTime=567;
    float buffer[16];for(unsigned i=0;i<16;++i)buffer[i]=0.5f;
    unsigned inputs=capture_calls,outputs=render_calls;
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,buffer,NULL)==0);
    for(unsigned i=0;i<16;++i)assert(buffer[i]==0);
    assert(capture_calls==inputs);
    for(unsigned i=0;i<16;++i)buffer[i]=0.5f;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)==0);
    assert(render_calls==outputs); /* Inactive output never reaches the wire ring. */
    value=1;
    assert(table.SetPropertyData(driver,INPUT,0,&address,0,NULL,sizeof(value),&value)==0);
    assert(table.SetPropertyData(driver,OUTPUT,0,&address,0,NULL,sizeof(value),&value)==0);
    assert(output_active_changes==2&&backend_output_active);
    assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,8,&cycle,buffer,NULL)==0);
    assert(capture_calls==inputs+1&&buffer[0]==0.25f&&buffer[1]==-0.25f);
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,8,&cycle,buffer,NULL)==0);
    assert(render_calls==outputs+1);
    value=2;
    assert(table.SetPropertyData(driver,INPUT,0,&address,0,NULL,sizeof(value),&value)!=0);
    assert(table.SetPropertyData(driver,OUTPUT,0,&address,0,NULL,sizeof(value),&value)!=0);
    assert(output_active_changes==2&&backend_output_active);
    value=0;assert(table.GetPropertyData(driver,INPUT,0,&address,0,NULL,sizeof(value),&written,&value)==0&&value==1);
    value=0;assert(table.GetPropertyData(driver,OUTPUT,0,&address,0,NULL,sizeof(value),&written,&value)==0&&value==1);
}
static unsigned configuration_requests;
static OSStatus configuration_result;
static UInt64 requested_action;
static char queue_key;
static AudioServerPlugInHostInterface fake_host;
static OSStatus changed(AudioServerPlugInHostRef owner,AudioObjectID object_id,UInt32 n,const AudioObjectPropertyAddress *addresses){
    assert(owner==&fake_host&&object_id==DEVICE&&n==1&&addresses);return 0;
}
static OSStatus request_change(AudioServerPlugInHostRef owner,AudioObjectID object_id,UInt64 action,void *data){
    assert(owner==&fake_host&&object_id==DEVICE&&data==NULL);
    assert(dispatch_get_specific(&queue_key)==&queue_key); /* Enqueued off USB worker. */
    ++configuration_requests;requested_action=action;return configuration_result;
}
static void flush_configuration_queue(void){dispatch_sync(device_control_queue,^{ });}
static void trigger_panic(uint64_t generation){
    assert(registered_panic);registered_panic(registered_context,generation,kIOReturnIsoTooOld);
}
static void panic_handshake(void){
    /* Configure only fake bindings. Do not call Initialize or open USB. */
    fake_host=(AudioServerPlugInHostInterface){.PropertiesChanged=changed,.RequestDeviceConfigurationChange=request_change};
    host=&fake_host;device_control_queue=dispatch_queue_create("rk3.fake.host.configuration",DISPATCH_QUEUE_SERIAL);
    dispatch_queue_set_specific(device_control_queue,&queue_key,&queue_key,NULL);
    assert(rk3_usb_set_audio_panic_handler(&mock_session,audio_panic_handler,NULL));
    atomic_store(&configuration_pending,false);
    assert(table.StartIO(driver,DEVICE,77)==0);
    unsigned initial_starts=starts,initial_stops=stops;
    trigger_panic(mock_generation);trigger_panic(mock_generation);flush_configuration_queue();
    assert(configuration_requests==1&&requested_action==RK3_AUDIO_RESTART_ACTION);
    assert(atomic_load(&configuration_pending)&&starts==initial_starts&&stops==initial_stops);
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,requested_action,NULL)==kAudioHardwareIllegalOperationError);
    assert(atomic_load(&configuration_pending));
    assert(table.StopIO(driver,DEVICE,77)==0);
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,requested_action,NULL)==0);
    assert(!atomic_load(&configuration_pending));
    assert(table.StartIO(driver,DEVICE,77)==0&&starts==initial_starts+1);
    assert(table.StopIO(driver,DEVICE,77)==0);
    trigger_panic(mock_generation);flush_configuration_queue();
    assert(configuration_requests==2&&atomic_load(&configuration_pending));
    assert(table.AbortDeviceConfigurationChange(driver,DEVICE,RK3_AUDIO_RESTART_ACTION,NULL)==0);
    assert(!atomic_load(&configuration_pending)&&atomic_load(&last_configuration_error)!=0);
    configuration_result=kAudioHardwareUnspecifiedError;
    trigger_panic(mock_generation);flush_configuration_queue();
    assert(configuration_requests==3&&!atomic_load(&configuration_pending));
    assert(atomic_load(&last_configuration_error)==configuration_result);
    configuration_result=0;trigger_panic(mock_generation);flush_configuration_queue();
    assert(configuration_requests==4&&atomic_load(&configuration_pending));
    assert(table.AbortDeviceConfigurationChange(driver,DEVICE,RK3_AUDIO_RESTART_ACTION,NULL)==0);
    /* A USB generation can change while the callback waits on the control
     * queue. Drop that stale request before touching the host configuration. */
    dispatch_suspend(device_control_queue);trigger_panic(mock_generation);++mock_generation;
    dispatch_resume(device_control_queue);flush_configuration_queue();
    assert(configuration_requests==4&&!atomic_load(&configuration_pending));--mock_generation;
    mock_connected=false;trigger_panic(mock_generation);flush_configuration_queue();
    assert(configuration_requests==4&&!atomic_load(&configuration_pending));mock_connected=true;
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,RK3_AUDIO_RESTART_ACTION+1,NULL)==kAudioHardwareUnsupportedOperationError);
    assert(table.AbortDeviceConfigurationChange(driver,DEVICE,RK3_AUDIO_RESTART_ACTION+1,NULL)==kAudioHardwareUnsupportedOperationError);
    assert(rk3_usb_set_audio_panic_handler(&mock_session,NULL,NULL));
    flush_configuration_queue();dispatch_release(device_control_queue);device_control_queue=NULL;host=NULL;
}
int main(void){
    assert(RK3AudioFactory(NULL,kAudioServerPlugInTypeUUID)==driver);
    rk3_input_gain_init(&mock_gain);usb=&mock_session;properties();input_gain_transport_test();audio();stream_activity();panic_handshake();assert(opens==0);
    puts("Audio driver: host ABI, CFData, NI timing, addressed PCM, lifecycle, clock and panic handshake passed");return 0;
}
