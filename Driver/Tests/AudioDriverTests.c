/* Host ABI checks with a deterministic backend. Never opens USB. */
#include <assert.h>
#include <IOKit/IOReturn.h>
#include <stdio.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>
static CFDataRef property_data_create(CFAllocatorRef,const UInt8 *,CFIndex);
#define CFDataCreate property_data_create
#include "../Audio/RK3AudioDriver.c"
#undef CFDataCreate
#include "../InputGain.h"
static RK3InputGain mock_gain;

static unsigned starts,stops,capture_calls,render_calls,opens,closes,output_active_changes;
static unsigned spec_reads,identity_reads;
static int32_t mock_identity_error;
static RK3NativeUSBIdentity mock_identity_value={
    .version=1,.generation=7,.attempted_mask=0xff,.defined_mask=0xff,
    .vendor_id=0x1234,.product_id=0xabcd,.product_index=1,.manufacturer_index=2,
    .product={0x50},.manufacturer={0xc3,0xa9},.serial={0x53}
};
static bool inspect_spec_allocation,fail_spec_allocation,zero_spec,fail_registration;
static bool spec_owner_active,pause_spec_owner,pause_identity_owner,owner_entered,allow_owner,initializer_started;
static pthread_mutex_t spec_test_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t spec_test_condition=PTHREAD_COND_INITIALIZER;
static bool backend_output_active=true;
static uint64_t captured_first,rendered_first;
static size_t captured_frames,rendered_frames;
static rk3_usb_audio_panic_handler registered_panic;
static void *registered_context;
static bool fail_start, fail_output, mock_running, mock_connected=true;
static uint32_t mock_sample_rate=RATE;
static unsigned rate_changes;
static int32_t rate_error;
static bool mock_draining;
static int32_t stop_error;
static bool reconnect_at_rate_handoff;
static uint64_t rate_generation_argument;
static uint64_t mock_generation=7;
static int32_t mock_spec_error;
static rk3_usb_clock_snapshot mock_clock={.sample_frame=0,.host_time=1000000000,.seed=1,.zero_sample_frame=0,.zero_host_time=1000000000};
struct rk3_usb_session { int unused; };
static struct rk3_usb_session mock_session;
rk3_usb_session *rk3_usb_open(char *error,size_t size){(void)error;(void)size;++opens;return &mock_session;}
void rk3_usb_close(rk3_usb_session *s){assert(s==&mock_session&&!spec_owner_active);++closes;}
bool rk3_usb_info(rk3_usb_session *s,RK3NativeInfo *value){
    (void)s;memset(value,0,sizeof(*value));value->version=1;value->flags=(mock_connected?RK3_NATIVE_CONNECTED:0)|(mock_running?RK3_NATIVE_AUDIO_RUNNING:0);
    value->generation=mock_generation;value->midi_inputs=1;value->midi_outputs=1;
    value->audio_inputs=value->audio_outputs=2;value->sample_rate=mock_sample_rate;value->wire_mode=2;return true;
}
int32_t rk3_usb_copy_spec(rk3_usb_session *s,uint64_t generation,uint8_t raw[14]) {
    assert(s==&mock_session&&generation);
    int locked=pthread_mutex_trylock(&control);
    if(locked==0)pthread_mutex_unlock(&control);
    assert(locked==EBUSY); /* Frozen selection-to-owner lifetime gate. */
    pthread_mutex_lock(&spec_test_gate);spec_owner_active=true;++spec_reads;
    if(pause_spec_owner) {
        owner_entered=true;pthread_cond_broadcast(&spec_test_condition);
        while(!allow_owner)pthread_cond_wait(&spec_test_condition,&spec_test_gate);
    }
    static const uint8_t bytes[14]={1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    int32_t result=mock_spec_error;
    if(!result){if(zero_spec)memset(raw,0,14);else memcpy(raw,bytes,14);}
    spec_owner_active=false;pthread_mutex_unlock(&spec_test_gate);return result;
}
int32_t rk3_usb_copy_identity(rk3_usb_session *s,uint64_t generation,RK3NativeUSBIdentity *value) {
    assert(s==&mock_session&&generation);
    int locked=pthread_mutex_trylock(&control);
    if(locked==0)pthread_mutex_unlock(&control);
    assert(locked==EBUSY); /* Frozen selection-to-owner lifetime gate. */
    pthread_mutex_lock(&spec_test_gate);spec_owner_active=true;++identity_reads;
    if(pause_identity_owner) {
        owner_entered=true;pthread_cond_broadcast(&spec_test_condition);
        while(!allow_owner)pthread_cond_wait(&spec_test_condition,&spec_test_gate);
    }
    if(!mock_identity_error)*value=mock_identity_value;
    spec_owner_active=false;pthread_mutex_unlock(&spec_test_gate);
    return mock_identity_error;
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
int32_t rk3_usb_audio_stop(rk3_usb_session *s,uint32_t timeout){(void)s;(void)timeout;++stops;mock_running=false;return stop_error;}
int32_t rk3_usb_audio_set_sample_rate(rk3_usb_session *s,uint64_t expected_generation,uint32_t rate,uint32_t timeout){
    assert(s==&mock_session&&timeout==5000);++rate_changes;rate_generation_argument=expected_generation;
    if(reconnect_at_rate_handoff){reconnect_at_rate_handoff=false;++mock_generation;}
    if(expected_generation!=mock_generation)return ESTALE;
    if(!mock_connected)return ENODEV;
    if(mock_running||mock_draining)return EBUSY;
    if(rate_error)return rate_error;
    mock_sample_rate=rate;return 0;
}
void rk3_usb_set_output_active(rk3_usb_session *s,bool active){assert(s==&mock_session);backend_output_active=active;++output_active_changes;}
bool rk3_usb_set_audio_panic_handler(rk3_usb_session *s,rk3_usb_audio_panic_handler handler,void *context){assert(s==&mock_session);if(fail_registration)return false;registered_panic=handler;registered_context=context;return true;}
size_t rk3_usb_capture_at(rk3_usb_session *s,float *samples,uint64_t first,size_t n){
    assert(s==&mock_session);++capture_calls;captured_first=first;captured_frames=n;
    for(size_t i=0;i<n;++i){samples[2*i]=0.25f;samples[2*i+1]=-0.25f;}return n;
}
size_t rk3_usb_render_at(rk3_usb_session *s,const float *samples,uint64_t first,size_t n){
    assert(s==&mock_session);(void)samples;++render_calls;rendered_first=first;rendered_frames=n;return fail_output?0:n;
}

static CFDataRef property_data_create(CFAllocatorRef allocator,const UInt8 *bytes,CFIndex length) {
    if(inspect_spec_allocation) {
        assert(pthread_mutex_trylock(&control)==0);pthread_mutex_unlock(&control);
        if(fail_spec_allocation)return NULL;
    }
    return CFDataCreate(allocator,bytes,length);
}
static CFPropertyListRef spec_platform_reply,identity_platform_reply;
static unsigned identity_transports;
static OSStatus bridge_get(AudioObjectID device,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 *size,void *out){
    if(a->mSelector==RK3_PROPERTY_USB_IDENTITY) {
        ++identity_transports;
        if(identity_platform_reply) {
            CFRetain(identity_platform_reply);memcpy(out,&identity_platform_reply,sizeof(identity_platform_reply));
            *size=sizeof(identity_platform_reply);return 0;
        }
    }
    if(a->mSelector==RK3_PROPERTY_SPEC&&spec_platform_reply) {
        CFRetain(spec_platform_reply);memcpy(out,&spec_platform_reply,sizeof(spec_platform_reply));
        *size=sizeof(spec_platform_reply);return 0;
    }
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
    assert(f.mSampleRate==44100&&f.mChannelsPerFrame==2&&f.mBytesPerFrame==8);
    unsigned char guard[sizeof(f)];memset(guard,0x5a,sizeof(guard));
    assert(table.GetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(f)-1,&written,guard)==kAudioHardwareBadPropertySizeError);
    for(size_t i=0;i<sizeof(guard);++i)assert(guard[i]==0x5a);
    f.mSampleRate=32000;
    assert(table.SetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(f),&f)==kAudioHardwareUnsupportedOperationError);
    a.mSelector=kAudioDevicePropertyStreams;a.mScope=kAudioObjectPropertyScopeInput;
    AudioObjectID ids[2]={0};
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(ids),&written,ids)==0&&written==4&&ids[0]==INPUT);
    a.mSelector=kAudioDevicePropertyLatency;a.mScope=kAudioObjectPropertyScopeInput;
    UInt32 latency=99;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==64);
    a.mScope=kAudioObjectPropertyScopeOutput;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==128);
    a.mScope=kAudioObjectPropertyScopeGlobal;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==128);
    assert(table.GetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==0);
    assert(table.GetPropertyData(driver,OUTPUT,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==0);
    a.mSelector=kAudioDevicePropertySafetyOffset;
    for(unsigned i=0;i<3;++i){
        a.mScope=i==0?kAudioObjectPropertyScopeInput:i==1?kAudioObjectPropertyScopeOutput:kAudioObjectPropertyScopeGlobal;
        assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(latency),&written,&latency)==0&&latency==128);
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
    AudioServerPlugInCustomPropertyInfo metadata[6];
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(metadata),&written,metadata)==0);
    for(unsigned i=0;i<5;++i)assert(metadata[i].mPropertyDataType==kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
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
/* Frozen public nusb discovery; this fixture never opens USB. */
static void usb_identity_property_test(void) {
    AudioObjectPropertyAddress a={UINT32_C(0x6e757362),kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    assert(table.HasProperty(driver,DEVICE,0,&a));
    UInt32 size=0;
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&a,0,NULL,&size)==0&&size==sizeof(CFPropertyListRef));
}
static void usb_identity_raw_transport_test(void) {
    RK3NativeUSBIdentityQuery query={1,0,7};RK3NativeUSBIdentity value;
    memset(&value,0xa5,sizeof(value));
    assert(rk3_native_copy_blob(DEVICE,UINT32_C(0x6e757362),&query,sizeof(query),&value,sizeof(value))==0);
    assert(value.version==1&&value.status==0&&value.generation==7);
    assert(value.attempted_mask==0xff&&value.defined_mask==0xff);
    assert(value.vendor_id==0x1234&&value.product_id==0xabcd);
    assert(value.product_index==1&&value.manufacturer_index==2&&value.serial_index==0);
    static const uint8_t product[31]={0x50},manufacturer[31]={0xc3,0xa9},serial[31]={0x53};
    assert(!memcmp(value.product,product,31)&&!memcmp(value.manufacturer,manufacturer,31)&&!memcmp(value.serial,serial,31));
}
static void usb_identity_client_test(void) {
    RK3NativeUSBIdentityQuery query={1,0,7};RK3NativeUSBIdentity value={0};
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0);
    assert(value.vendor_id==0x1234&&value.product_id==0xabcd&&value.generation==7);
}
static void usb_identity_client_header_test(void) {
    RK3NativeUSBIdentityQuery query={1,0,7};RK3NativeUSBIdentity bad=mock_identity_value;
    bad.version=2;identity_platform_reply=CFDataCreate(NULL,(const UInt8 *)&bad,sizeof(bad));assert(identity_platform_reply);
    RK3NativeUSBIdentity result;memset(&result,0xa5,sizeof(result));RK3NativeUSBIdentity saved=result;
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&result)==kAudioHardwareIllegalOperationError&&!memcmp(&result,&saved,sizeof(result)));
    CFRelease(identity_platform_reply);identity_platform_reply=NULL;
}
static void usb_identity_client_field_test(void) {
    RK3NativeUSBIdentityQuery query={1,0,7};RK3NativeUSBIdentity bad[20];
    for(size_t i=0;i<20;++i)bad[i]=mock_identity_value;
    bad[0].attempted_mask=0x1ff;
    bad[1].defined_mask=0x1ff;
    bad[2].attempted_mask&=~UINT32_C(1);bad[2].defined_mask&=~UINT32_C(1);bad[2].vendor_id=0;
    bad[3].defined_mask&=~UINT32_C(1);
    bad[4].step_status[0]=-17;
    bad[5].step_status[0]=-17;bad[5].defined_mask&=~UINT32_C(1);
    bad[6].step_status[2]=-2;bad[6].defined_mask&=~UINT32_C(0x24);bad[6].product_index=0;memset(bad[6].product,0,31);
    bad[7].attempted_mask&=~UINT32_C(0x20);bad[7].defined_mask&=~UINT32_C(0x20);memset(bad[7].product,0,31);
    bad[8].step_status[2]=-2;bad[8].product_index=0;bad[8].attempted_mask&=~UINT32_C(0x20);bad[8].defined_mask&=~UINT32_C(0x24);bad[8].step_status[5]=-7;memset(bad[8].product,0,31);
    bad[9]=bad[8];bad[9].step_status[5]=0;bad[9].product[0]=1;
    bad[10].step_status[5]=-9;bad[10].defined_mask&=~UINT32_C(0x20);memset(bad[10].product,0,31);
    bad[11].step_status[5]=-9;
    bad[12].defined_mask&=~UINT32_C(0x20);
    memset(bad[13].product,0xff,31);
    bad[14].step_status[3]=-3;bad[14].defined_mask&=~UINT32_C(0x08);
    memset(bad[15].manufacturer,0xff,31);
    memset(bad[16].serial,0xff,31);
    bad[17].defined_mask&=~UINT32_C(0x80);
    bad[18]=bad[9];memset(bad[18].product,0,31);bad[18].product[30]=1;
    bad[19]=bad[17];memset(bad[19].serial,0,31);bad[19].serial[30]=1;
    for(size_t i=0;i<20;++i) {
        identity_platform_reply=CFDataCreate(NULL,(const UInt8 *)&bad[i],sizeof(bad[i]));assert(identity_platform_reply);
        RK3NativeUSBIdentity result;memset(&result,0xa5,sizeof(result));RK3NativeUSBIdentity saved=result;
        OSStatus error=rk3_native_copy_usb_identity(DEVICE,&query,&result);
        if(error!=kAudioHardwareIllegalOperationError||memcmp(&result,&saved,sizeof(result)))fprintf(stderr,"nusb invalid field case %zu accepted or changed output\n",i);
        assert(error==kAudioHardwareIllegalOperationError&&!memcmp(&result,&saved,sizeof(result)));
        CFRelease(identity_platform_reply);identity_platform_reply=NULL;
    }
}
static void usb_identity_partial_and_status_test(void) {
    RK3NativeUSBIdentityQuery query={1,0,7};RK3NativeUSBIdentity saved_mock=mock_identity_value,value;
    mock_identity_value.step_status[0]=-17;mock_identity_value.vendor_id=0;
    mock_identity_value.step_status[3]=-8;mock_identity_value.manufacturer_index=0;
    mock_identity_value.step_status[5]=-12345;
    mock_identity_value.attempted_mask=0xbf;mock_identity_value.defined_mask=0x36;
    memset(mock_identity_value.product,0,31);memset(mock_identity_value.manufacturer,0,31);memset(mock_identity_value.serial,0,31);
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0);
    assert(value.status==0&&value.generation==7&&value.attempted_mask==0xbf&&value.defined_mask==0x36);
    assert(value.step_status[0]==-17&&value.step_status[3]==-8&&value.step_status[5]==-12345&&value.step_status[7]==0);
    mock_identity_value=(RK3NativeUSBIdentity){.version=1,.generation=7,.attempted_mask=0xff,.defined_mask=0xff};
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.vendor_id==0&&value.product_id==0&&value.serial[0]==0);
    static const uint8_t units[]={0xed,0xa0,0xbd,0xed,0xb8,0x80,0};
    memcpy(mock_identity_value.serial,units,sizeof(units));
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&!memcmp(value.serial,units,sizeof(units)));
    memset(mock_identity_value.product,0xff,30);mock_identity_value.product[30]=0;
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.product[29]==0xff&&value.product[30]==0);
    mock_identity_value=saved_mock;
    for(unsigned i=0;i<2;++i) {
        mock_identity_error=i?ESTALE:ENODEV;memset(&value,0xa5,sizeof(value));
        assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0);
        RK3NativeUSBIdentity expected={.version=1,.status=mock_identity_error};assert(!memcmp(&value,&expected,sizeof(value)));
    }
    mock_identity_error=0;unsigned reads=identity_reads,opened=opens;usb=NULL;
    const RK3NativeUSBIdentity unavailable={.version=1,.status=ENODEV};
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&!memcmp(&value,&unavailable,sizeof(value))&&identity_reads==reads&&opens==opened);usb=&mock_session;
    const int32_t errors[]={EINVAL,EIO};
    for(size_t i=0;i<2;++i) {
        mock_identity_error=errors[i];memset(&value,0xa5,sizeof(value));RK3NativeUSBIdentity saved=value;
        assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==kAudioHardwareUnspecifiedError&&!memcmp(&value,&saved,sizeof(value)));
    }
    mock_identity_error=0;inspect_spec_allocation=fail_spec_allocation=true;
    memset(&value,0xa5,sizeof(value));RK3NativeUSBIdentity saved=value;
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==kAudioHardwareUnspecifiedError&&!memcmp(&value,&saved,sizeof(value)));
    fail_spec_allocation=false;assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0);inspect_spec_allocation=false;
}
static void usb_identity_client_envelope_test(void) {
    RK3NativeUSBIdentityQuery query={1,0,7};RK3NativeUSBIdentity bad[12];
    for(size_t i=0;i<12;++i)bad[i]=mock_identity_value;
    bad[0].version=0;bad[1].generation=8;bad[2].generation=0;bad[3].reserved0=1;
    bad[4].reserved[0]=1;bad[5].reserved[1]=1;bad[6].reserved[2]=1;
    bad[7]=(RK3NativeUSBIdentity){.version=1,.status=EINVAL};
    bad[8]=(RK3NativeUSBIdentity){.version=1,.status=ENODEV,.generation=7};
    bad[9]=(RK3NativeUSBIdentity){.version=1,.status=ESTALE,.attempted_mask=1};
    bad[10]=(RK3NativeUSBIdentity){.version=1,.status=ENODEV};bad[10].serial[30]=1;
    bad[11]=(RK3NativeUSBIdentity){.version=1,.status=ESTALE};bad[11].step_status[7]=-1;
    RK3NativeUSBIdentity result;memset(&result,0xa5,sizeof(result));RK3NativeUSBIdentity saved=result;
    for(size_t i=0;i<12;++i) {
        identity_platform_reply=CFDataCreate(NULL,(const UInt8 *)&bad[i],sizeof(bad[i]));assert(identity_platform_reply);
        assert(rk3_native_copy_usb_identity(DEVICE,&query,&result)==kAudioHardwareIllegalOperationError&&!memcmp(&result,&saved,sizeof(result)));
        CFRelease(identity_platform_reply);identity_platform_reply=NULL;
    }
    uint8_t body[161]={0};memcpy(body,&mock_identity_value,sizeof(mock_identity_value));
    for(size_t length=159;length<=161;length+=2) {
        identity_platform_reply=CFDataCreate(NULL,body,(CFIndex)length);
        assert(rk3_native_copy_usb_identity(DEVICE,&query,&result)==kAudioHardwareBadPropertySizeError&&!memcmp(&result,&saved,sizeof(result)));
        CFRelease(identity_platform_reply);identity_platform_reply=NULL;
    }
    identity_platform_reply=CFSTR("wrong identity response type");
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&result)==kAudioHardwareBadPropertySizeError&&!memcmp(&result,&saved,sizeof(result)));identity_platform_reply=NULL;
    RK3NativeUSBIdentityQuery invalid[]={ {2,0,7},{1,1,7},{1,0,0} };unsigned calls=identity_transports;
    for(size_t i=0;i<3;++i)assert(rk3_native_copy_usb_identity(DEVICE,&invalid[i],&result)==kAudioHardwareIllegalOperationError&&!memcmp(&result,&saved,sizeof(result)));
    assert(rk3_native_copy_usb_identity(DEVICE,NULL,&result)==kAudioHardwareIllegalOperationError&&!memcmp(&result,&saved,sizeof(result)));
    assert(rk3_native_copy_usb_identity(DEVICE,&query,NULL)==kAudioHardwareIllegalOperationError&&identity_transports==calls);
}
static void usb_identity_property_validation_test(void) {
    AudioObjectPropertyAddress a={RK3_PROPERTY_USB_IDENTITY,kAudioObjectPropertyScopeGlobal,0};
    Boolean writable=true;UInt32 size=0,written=UINT32_MAX;
    assert(table.IsPropertySettable(driver,DEVICE,0,&a,&writable)==0&&!writable);
    uint32_t ignored=0;assert(table.SetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(ignored),&ignored)==kAudioHardwareUnknownPropertyError);
    assert(!table.HasProperty(driver,INPUT,0,&a)&&!table.HasProperty(driver,OUTPUT,0,&a)&&!table.HasProperty(driver,kAudioObjectPlugInObject,0,&a));
    a.mScope=kAudioObjectPropertyScopeInput;assert(!table.HasProperty(driver,DEVICE,0,&a));
    a.mScope=kAudioObjectPropertyScopeOutput;assert(!table.HasProperty(driver,DEVICE,0,&a));
    a.mScope=kAudioObjectPropertyScopeGlobal;a.mElement=1;assert(!table.HasProperty(driver,DEVICE,0,&a));a.mElement=0;
    AudioObjectPropertyAddress metadata_address={kAudioObjectPropertyCustomPropertyInfoList,kAudioObjectPropertyScopeGlobal,0};
    AudioServerPlugInCustomPropertyInfo metadata[6];
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&metadata_address,0,NULL,&size)==0&&size==sizeof(metadata));
    assert(table.GetPropertyData(driver,DEVICE,0,&metadata_address,0,NULL,sizeof(metadata),&written,metadata)==0&&written==sizeof(metadata));
    static const UInt32 selectors[]={0x6e696e66,0x6e727074,0x6e636d64,0x6e676169,0x6e737063,0x6e757362};
    for(size_t i=0;i<6;++i) {
        assert(metadata[i].mSelector==selectors[i]&&metadata[i].mPropertyDataType==kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
        assert(metadata[i].mQualifierDataType==(i?kAudioServerPlugInCustomPropertyDataTypeCFPropertyList:kAudioServerPlugInCustomPropertyDataTypeNone));
    }
    RK3NativeUSBIdentityQuery query={1,0,7};unsigned reads=identity_reads;
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&a,123,NULL,&size)==0&&size==sizeof(CFPropertyListRef)&&identity_reads==reads);
    CFPropertyListRef sentinel=CFSTR("identity output unchanged"),output=sentinel;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&written==sizeof(output)&&output==sentinel);
    CFDataRef valid=CFDataCreate(NULL,(const UInt8 *)&query,sizeof(query));CFPropertyListRef null_object=NULL,wrong=CFSTR("wrong query type");
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(valid)-1,&valid,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==sentinel);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(valid),NULL,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==sentinel);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(null_object),&null_object,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==sentinel);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(wrong),&wrong,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==sentinel);
    for(size_t length=15;length<=17;length+=2) {
        uint8_t body[17]={0};memcpy(body,&query,sizeof(query));CFDataRef qualifier=CFDataCreate(NULL,body,(CFIndex)length);
        assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==sentinel);CFRelease(qualifier);
    }
    RK3NativeUSBIdentityQuery invalid[]={ {2,0,7},{1,1,7},{1,0,0} };
    for(size_t i=0;i<3;++i) {
        CFDataRef qualifier=CFDataCreate(NULL,(const UInt8 *)&invalid[i],sizeof(invalid[i]));
        assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(output),&written,&output)==kAudioHardwareIllegalOperationError&&written==sizeof(output)&&output==sentinel);CFRelease(qualifier);
    }
    written=UINT32_MAX;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(valid),&valid,sizeof(output)-1,&written,&output)==kAudioHardwareBadPropertySizeError&&written==UINT32_MAX&&output==sentinel);
    void *missing_output=NULL;UInt32 *missing_written=NULL;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(valid),&valid,sizeof(output),&written,missing_output)==kAudioHardwareBadPropertySizeError);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(valid),&valid,sizeof(output),missing_written,&output)==kAudioHardwareUnknownPropertyError);
    CFRelease(valid);assert(identity_reads==reads);
}
/* Frozen public raw nspc tracer; no NI driver or USB endpoint is opened. */
static void spec_property_transport_test(void) {
    const UInt32 selector=UINT32_C(0x6e737063);
    AudioObjectPropertyAddress a={selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    assert(table.HasProperty(driver,DEVICE,0,&a));
    UInt32 size=0;
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&a,0,NULL,&size)==0&&size==sizeof(CFPropertyListRef));
    struct {uint32_t version,reserved;uint64_t generation;} query={1,0,7};
    struct {uint32_t version;int32_t status;uint64_t generation;uint8_t raw[14],reserved[2];} reply;
    memset(&reply,0xa5,sizeof(reply));
    static const uint8_t expected[14]={1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    assert(rk3_native_copy_blob(DEVICE,selector,&query,sizeof(query),&reply,sizeof(reply))==0);
    assert(reply.version==1&&reply.status==0&&reply.generation==7);
    assert(memcmp(reply.raw,expected,14)==0&&reply.reserved[0]==0&&reply.reserved[1]==0);
}
static void spec_domain_reply_test(void) {
    RK3NativeSpecQuery query={1,0,7};RK3NativeSpec reply;
    memset(&reply,0xa5,sizeof(reply));mock_spec_error=ENODEV;
    assert(rk3_native_copy_blob(DEVICE,RK3_PROPERTY_SPEC,&query,sizeof(query),&reply,sizeof(reply))==0);
    const RK3NativeSpec expected={.version=1,.status=ENODEV};
    assert(memcmp(&reply,&expected,sizeof(reply))==0);mock_spec_error=0;
}
static void spec_client_transport_test(void) {
    RK3NativeSpecQuery query={1,0,7};RK3NativeSpec reply={0};
    static const uint8_t expected[14]={1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    assert(rk3_native_copy_spec(DEVICE,&query,&reply)==0);
    assert(reply.status==0&&reply.generation==7&&memcmp(reply.raw,expected,14)==0);
}
static void spec_client_invalid_reply_test(void) {
    RK3NativeSpecQuery query={1,0,7};RK3NativeSpec malformed={.version=2,.generation=7};
    RK3NativeSpec result;memset(&result,0xa5,sizeof(result));RK3NativeSpec saved=result;
    spec_platform_reply=CFDataCreate(NULL,(const UInt8 *)&malformed,sizeof(malformed));
    assert(spec_platform_reply);
    assert(rk3_native_copy_spec(DEVICE,&query,&result)==kAudioHardwareIllegalOperationError);
    assert(memcmp(&result,&saved,sizeof(result))==0);
    CFRelease(spec_platform_reply);spec_platform_reply=NULL;
}
static void spec_metadata_and_query_test(void) {
    AudioObjectPropertyAddress a={RK3_PROPERTY_SPEC,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    Boolean writable=true;UInt32 size=0,written=UINT32_MAX;CFPropertyListRef output=NULL;
    assert(table.IsPropertySettable(driver,DEVICE,0,&a,&writable)==0&&!writable);
    uint32_t ignored=0;
    assert(table.SetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(ignored),&ignored)==kAudioHardwareUnknownPropertyError);
    assert(!table.HasProperty(driver,INPUT,0,&a)&&!table.HasProperty(driver,kAudioObjectPlugInObject,0,&a));
    a.mScope=kAudioObjectPropertyScopeInput;assert(!table.HasProperty(driver,DEVICE,0,&a));
    a.mScope=kAudioObjectPropertyScopeOutput;assert(!table.HasProperty(driver,DEVICE,0,&a));
    a.mScope=kAudioObjectPropertyScopeGlobal;a.mElement=1;assert(!table.HasProperty(driver,DEVICE,0,&a));a.mElement=0;
    AudioObjectPropertyAddress metadata_address={kAudioObjectPropertyCustomPropertyInfoList,kAudioObjectPropertyScopeGlobal,0};
    AudioServerPlugInCustomPropertyInfo metadata[6];
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&metadata_address,0,NULL,&size)==0&&size==sizeof(metadata));
    assert(table.GetPropertyData(driver,DEVICE,0,&metadata_address,0,NULL,sizeof(metadata),&written,metadata)==0&&written==sizeof(metadata));
    assert(metadata[4].mSelector==UINT32_C(0x6e737063)&&metadata[4].mPropertyDataType==kAudioServerPlugInCustomPropertyDataTypeCFPropertyList&&metadata[4].mQualifierDataType==kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
    RK3NativeSpecQuery query={1,0,7};unsigned reads=spec_reads;
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&a,123,NULL,&size)==0&&size==sizeof(CFPropertyListRef)&&spec_reads==reads);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&written==sizeof(output)&&output==NULL);
    CFDataRef wrapped_query=CFDataCreate(NULL,(const UInt8 *)&query,sizeof(query));
    CFPropertyListRef null_object=NULL;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(wrapped_query)-1,&wrapped_query,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&written==sizeof(output)&&output==NULL);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(wrapped_query),NULL,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&written==sizeof(output)&&output==NULL);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(null_object),&null_object,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&written==sizeof(output)&&output==NULL);
    CFRelease(wrapped_query);
    CFPropertyListRef wrong=CFSTR("wrong");
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(wrong),&wrong,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==NULL);
    for(size_t length=15;length<=17;length+=2) {
        uint8_t body[17]={0};memcpy(body,&query,sizeof(query));CFDataRef qualifier=CFDataCreate(NULL,body,(CFIndex)length);
        assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==NULL);
        CFRelease(qualifier);
    }
    RK3NativeSpecQuery bad[]={ {2,0,7},{1,1,7},{1,0,0} };
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        CFDataRef qualifier=CFDataCreate(NULL,(const UInt8 *)&bad[i],sizeof(bad[i]));
        assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(output),&written,&output)==kAudioHardwareIllegalOperationError&&written==sizeof(output)&&output==NULL);
        CFRelease(qualifier);
    }
    CFPropertyListRef sentinel=CFSTR("unchanged property output");output=sentinel;
    CFDataRef invalid_header=CFDataCreate(NULL,(const UInt8 *)&bad[0],sizeof(bad[0]));
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(invalid_header),&invalid_header,sizeof(output),&written,&output)==kAudioHardwareIllegalOperationError&&written==sizeof(output)&&output==sentinel);
    CFRelease(invalid_header);output=NULL;
    CFDataRef qualifier=CFDataCreate(NULL,(const UInt8 *)&query,sizeof(query));written=UINT32_MAX;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(output)-1,&written,&output)==kAudioHardwareBadPropertySizeError&&written==UINT32_MAX&&output==NULL);
    void *missing_output=NULL;UInt32 *missing_written=NULL;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(output),&written,missing_output)==kAudioHardwareBadPropertySizeError);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(output),missing_written,&output)==kAudioHardwareUnknownPropertyError);
    CFRelease(qualifier);assert(spec_reads==reads);
}
static void spec_native_status_test(void) {
    RK3NativeSpecQuery query={1,0,7};RK3NativeSpec result;
    for(unsigned i=0;i<2;++i) {
        mock_spec_error=i?ESTALE:ENODEV;memset(&result,0xa5,sizeof(result));
        assert(rk3_native_copy_spec(DEVICE,&query,&result)==0);
        RK3NativeSpec expected={.version=1,.status=mock_spec_error};assert(memcmp(&result,&expected,sizeof(result))==0);
    }
    mock_spec_error=0;unsigned reads=spec_reads,opened=opens;usb=NULL;
    const RK3NativeSpec unavailable={.version=1,.status=ENODEV};
    assert(rk3_native_copy_spec(DEVICE,&query,&result)==0&&memcmp(&result,&unavailable,sizeof(result))==0&&spec_reads==reads&&opens==opened);usb=&mock_session;
    const int errors[]={EINVAL,EIO};
    for(size_t i=0;i<sizeof(errors)/sizeof(*errors);++i) {
        mock_spec_error=errors[i];memset(&result,0xa5,sizeof(result));RK3NativeSpec saved=result;
        assert(rk3_native_copy_spec(DEVICE,&query,&result)==kAudioHardwareUnspecifiedError&&memcmp(&result,&saved,sizeof(result))==0);
    }
    mock_spec_error=0;zero_spec=true;memset(&result,0xa5,sizeof(result));
    assert(rk3_native_copy_spec(DEVICE,&query,&result)==0&&result.status==0&&result.generation==7);
    const uint8_t zero[14]={0};assert(memcmp(result.raw,zero,14)==0);zero_spec=false;
    inspect_spec_allocation=true;fail_spec_allocation=true;memset(&result,0xa5,sizeof(result));RK3NativeSpec saved=result;
    assert(rk3_native_copy_spec(DEVICE,&query,&result)==kAudioHardwareUnspecifiedError&&memcmp(&result,&saved,sizeof(result))==0);
    fail_spec_allocation=false;assert(rk3_native_copy_spec(DEVICE,&query,&result)==0);inspect_spec_allocation=false;
}
static void spec_client_response_matrix_test(void) {
    RK3NativeSpecQuery query={1,0,7};RK3NativeSpec base={.version=1,.generation=7};
    RK3NativeSpec bad[9];for(size_t i=0;i<9;++i)bad[i]=base;
    bad[0].version=0;bad[1].reserved[0]=1;bad[2].reserved[1]=1;bad[3].generation=8;
    bad[4].generation=0;bad[5]=(RK3NativeSpec){.version=1,.status=EINVAL};bad[6].status=ENODEV;
    bad[7]=(RK3NativeSpec){.version=1,.status=ESTALE,.raw={1}};
    bad[8]=(RK3NativeSpec){.version=1,.status=ENODEV};bad[8].raw[13]=1;
    RK3NativeSpec result;memset(&result,0xa5,sizeof(result));RK3NativeSpec saved=result;
    for(size_t i=0;i<9;++i) {
        spec_platform_reply=CFDataCreate(NULL,(const UInt8 *)&bad[i],sizeof(bad[i]));assert(spec_platform_reply);
        assert(rk3_native_copy_spec(DEVICE,&query,&result)==kAudioHardwareIllegalOperationError&&memcmp(&result,&saved,sizeof(result))==0);
        CFRelease(spec_platform_reply);spec_platform_reply=NULL;
    }
    uint8_t body[33]={0};memcpy(body,&base,sizeof(base));
    for(size_t length=31;length<=33;length+=2) {
        spec_platform_reply=CFDataCreate(NULL,body,(CFIndex)length);
        assert(rk3_native_copy_spec(DEVICE,&query,&result)==kAudioHardwareBadPropertySizeError&&memcmp(&result,&saved,sizeof(result))==0);
        CFRelease(spec_platform_reply);spec_platform_reply=NULL;
    }
    spec_platform_reply=CFSTR("bad response type");
    assert(rk3_native_copy_spec(DEVICE,&query,&result)==kAudioHardwareBadPropertySizeError&&memcmp(&result,&saved,sizeof(result))==0);spec_platform_reply=NULL;
    RK3NativeSpecQuery bad_query[]={ {2,0,7},{1,1,7},{1,0,0} };unsigned reads=spec_reads;
    for(size_t i=0;i<3;++i)assert(rk3_native_copy_spec(DEVICE,&bad_query[i],&result)==kAudioHardwareIllegalOperationError&&memcmp(&result,&saved,sizeof(result))==0);
    assert(rk3_native_copy_spec(DEVICE,NULL,&result)==kAudioHardwareIllegalOperationError&&memcmp(&result,&saved,sizeof(result))==0);
    assert(rk3_native_copy_spec(DEVICE,&query,NULL)==kAudioHardwareIllegalOperationError&&spec_reads==reads);
}
static void *spec_get_during_initialize(void *context) {
    (void)context;RK3NativeSpecQuery query={1,0,7};RK3NativeSpec reply;
    assert(rk3_native_copy_spec(DEVICE,&query,&reply)==0&&reply.status==0&&reply.generation==7);return NULL;
}
static void *spec_failed_initialize(void *context) {
    pthread_mutex_lock(&spec_test_gate);initializer_started=true;pthread_cond_broadcast(&spec_test_condition);pthread_mutex_unlock(&spec_test_gate);
    assert(table.Initialize(driver,(AudioServerPlugInHostRef)context)==kAudioHardwareUnspecifiedError);return NULL;
}
static void spec_initialization_lifetime_test(void) {
    assert(host==NULL&&opens==0);AudioServerPlugInHostInterface init_host={0};
    pause_spec_owner=true;owner_entered=allow_owner=initializer_started=false;fail_registration=true;
    pthread_t reader,initializer;assert(pthread_create(&reader,NULL,spec_get_during_initialize,NULL)==0);
    pthread_mutex_lock(&spec_test_gate);while(!owner_entered)pthread_cond_wait(&spec_test_condition,&spec_test_gate);pthread_mutex_unlock(&spec_test_gate);
    assert(pthread_create(&initializer,NULL,spec_failed_initialize,&init_host)==0);
    pthread_mutex_lock(&spec_test_gate);while(!initializer_started)pthread_cond_wait(&spec_test_condition,&spec_test_gate);
    allow_owner=true;pthread_cond_broadcast(&spec_test_condition);pthread_mutex_unlock(&spec_test_gate);
    assert(pthread_join(reader,NULL)==0&&pthread_join(initializer,NULL)==0);
    assert(opens==1&&closes==1&&usb==NULL&&host==NULL&&device_control_queue==NULL);
    pause_spec_owner=false;fail_registration=false;usb=&mock_session;
}
static void *usb_identity_get_during_initialize(void *context) {
    (void)context;RK3NativeUSBIdentityQuery query={1,0,7};RK3NativeUSBIdentity value;
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.status==0&&value.generation==7&&value.vendor_id==0x1234);
    return NULL;
}
static void usb_identity_initialization_lifetime_test(void) {
    assert(host==NULL);unsigned opened=opens,closed=closes;AudioServerPlugInHostInterface init_host={0};
    pause_identity_owner=true;owner_entered=allow_owner=initializer_started=false;fail_registration=true;
    pthread_t reader,initializer;assert(pthread_create(&reader,NULL,usb_identity_get_during_initialize,NULL)==0);
    pthread_mutex_lock(&spec_test_gate);while(!owner_entered)pthread_cond_wait(&spec_test_condition,&spec_test_gate);pthread_mutex_unlock(&spec_test_gate);
    assert(pthread_create(&initializer,NULL,spec_failed_initialize,&init_host)==0);
    pthread_mutex_lock(&spec_test_gate);while(!initializer_started)pthread_cond_wait(&spec_test_condition,&spec_test_gate);
    allow_owner=true;pthread_cond_broadcast(&spec_test_condition);pthread_mutex_unlock(&spec_test_gate);
    assert(pthread_join(reader,NULL)==0&&pthread_join(initializer,NULL)==0);
    assert(opens==opened+1&&closes==closed+1&&usb==NULL&&host==NULL&&device_control_queue==NULL);
    pause_identity_owner=false;fail_registration=false;usb=&mock_session;
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
static struct { AudioObjectID object;AudioObjectPropertyAddress address; } notifications[256];
static unsigned notification_count;
static bool inspect_rate_notifications,reentrant_perform,panic_during_request;
static Float64 reentrant_follow_rate;
static Float64 nominal_rate(void) {
    AudioObjectPropertyAddress a={kAudioDevicePropertyNominalSampleRate,kAudioObjectPropertyScopeGlobal,0};
    Float64 value=0;UInt32 written=0;
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(value),&written,&value)==0&&written==sizeof(value));return value;
}
static OSStatus nominal_set(Float64 value) {
    AudioObjectPropertyAddress a={kAudioDevicePropertyNominalSampleRate,kAudioObjectPropertyScopeGlobal,0};
    return table.SetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(value),&value);
}
static OSStatus changed(AudioServerPlugInHostRef owner,AudioObjectID object_id,UInt32 n,const AudioObjectPropertyAddress *addresses){
    assert(owner==&fake_host&&(object_id==DEVICE||object_id==INPUT||object_id==OUTPUT)&&n&&addresses);
    assert(pthread_mutex_trylock(&control)==0);pthread_mutex_unlock(&control);
    for(UInt32 i=0;i<n;++i){assert(notification_count<256);notifications[notification_count].object=object_id;notifications[notification_count++].address=addresses[i];}
    if(inspect_rate_notifications) {
        assert(nominal_rate()==mock_sample_rate); /* Reenter a public property getter. */
        assert(nominal_set(mock_sample_rate)==0); /* And the same-rate setter. */
    }
    return 0;
}
static OSStatus request_change(AudioServerPlugInHostRef owner,AudioObjectID object_id,UInt64 action,void *data){
    assert(owner==&fake_host&&object_id==DEVICE&&data==NULL);
    assert(dispatch_get_specific(&queue_key)==&queue_key); /* Enqueued off USB worker. */
    assert(pthread_mutex_trylock(&control)==0);pthread_mutex_unlock(&control);
    ++configuration_requests;requested_action=action;OSStatus result=configuration_result;
    if(panic_during_request) {
        panic_during_request=false;configuration_result=0;
        assert(registered_panic);registered_panic(registered_context,mock_generation,kIOReturnIsoTooOld);
    }
    if(reentrant_perform) {
        reentrant_perform=false;configuration_result=0;
        assert(table.PerformDeviceConfigurationChange(driver,DEVICE,action,NULL)==0);
        if(reentrant_follow_rate){Float64 rate=reentrant_follow_rate;reentrant_follow_rate=0;assert(nominal_set(rate)==0);}
    }
    return result;
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
    assert(configuration_requests==1&&requested_action==pending_configuration.action&&pending_configuration.kind==CONFIGURATION_RESTART);
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
    assert(table.AbortDeviceConfigurationChange(driver,DEVICE,requested_action,NULL)==0);
    assert(!atomic_load(&configuration_pending)&&atomic_load(&last_configuration_error)!=0);
    configuration_result=kAudioHardwareUnspecifiedError;
    trigger_panic(mock_generation);flush_configuration_queue();
    assert(configuration_requests==3&&!atomic_load(&configuration_pending));
    assert(atomic_load(&last_configuration_error)==configuration_result);
    configuration_result=0;trigger_panic(mock_generation);flush_configuration_queue();
    assert(configuration_requests==4&&atomic_load(&configuration_pending));
    assert(table.AbortDeviceConfigurationChange(driver,DEVICE,requested_action,NULL)==0);
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
static const struct {uint32_t rate,input,output,offset;} rate_cases[]={
    {44100,64,128,128},{48000,68,136,136},{88200,88,176,176},{96000,92,184,184},{192000,140,280,280}};
static AudioStreamBasicDescription literal_format(uint32_t rate) {
    AudioStreamBasicDescription value={0};value.mSampleRate=rate;value.mFormatID=kAudioFormatLinearPCM;
    value.mFormatFlags=kAudioFormatFlagsNativeFloatPacked;value.mBytesPerPacket=8;value.mFramesPerPacket=1;
    value.mBytesPerFrame=8;value.mChannelsPerFrame=2;value.mBitsPerChannel=32;return value;
}
static void assert_rate_values(unsigned index) {
    assert(index<5);uint32_t rate=rate_cases[index].rate;assert(nominal_rate()==rate&&mock_sample_rate==rate);
    for(unsigned object_index=0;object_index<2;++object_index)for(unsigned property=0;property<2;++property) {
        AudioObjectPropertyAddress a={property?kAudioStreamPropertyPhysicalFormat:kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,0};
        AudioStreamBasicDescription value={0},expected=literal_format(rate);UInt32 written=0;
        assert(table.GetPropertyData(driver,object_index?OUTPUT:INPUT,0,&a,0,NULL,sizeof(value),&written,&value)==0);
        assert(memcmp(&value,&expected,sizeof(value))==0);
    }
    for(unsigned scope=0;scope<3;++scope) {
        AudioObjectPropertyScope scopes[]={kAudioObjectPropertyScopeInput,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyScopeGlobal};
        AudioObjectPropertyAddress a={kAudioDevicePropertyLatency,scopes[scope],0};UInt32 value=0,written=0;
        assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(value),&written,&value)==0);
        assert(value==(scope?rate_cases[index].output:rate_cases[index].input));
        a.mSelector=kAudioDevicePropertySafetyOffset;
        assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(value),&written,&value)==0&&value==rate_cases[index].offset);
    }
}
static void rate_metadata_test(void) {
    AudioObjectPropertyAddress a={kAudioDevicePropertyAvailableNominalSampleRates,kAudioObjectPropertyScopeGlobal,0};
    AudioValueRange ranges[5];UInt32 size=0,written=0;
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&a,0,NULL,&size)==0&&size==sizeof(ranges));
    assert(table.GetPropertyData(driver,DEVICE,0,&a,0,NULL,sizeof(ranges),&written,ranges)==0&&written==sizeof(ranges));
    for(unsigned i=0;i<5;++i)assert(ranges[i].mMinimum==rate_cases[i].rate&&ranges[i].mMaximum==rate_cases[i].rate);
    for(unsigned object_index=0;object_index<2;++object_index)for(unsigned property=0;property<2;++property) {
        AudioStreamRangedDescription formats[5];AudioObjectID object_id=object_index?OUTPUT:INPUT;
        a.mSelector=property?kAudioStreamPropertyAvailablePhysicalFormats:kAudioStreamPropertyAvailableVirtualFormats;
        assert(table.GetPropertyDataSize(driver,object_id,0,&a,0,NULL,&size)==0&&size==sizeof(formats));
        assert(table.GetPropertyData(driver,object_id,0,&a,0,NULL,sizeof(formats),&written,formats)==0&&written==sizeof(formats));
        for(unsigned i=0;i<5;++i){AudioStreamBasicDescription expected=literal_format(rate_cases[i].rate);
            assert(memcmp(&formats[i].mFormat,&expected,sizeof(expected))==0&&formats[i].mSampleRateRange.mMinimum==rate_cases[i].rate&&formats[i].mSampleRateRange.mMaximum==rate_cases[i].rate);}
        memset(formats,0x5a,sizeof(formats));written=0xabcdef;
        assert(table.GetPropertyData(driver,object_id,0,&a,0,NULL,sizeof(formats)-1,&written,formats)==kAudioHardwareBadPropertySizeError&&written==0xabcdef);
        const uint8_t *bytes=(const uint8_t *)formats;for(size_t i=0;i<sizeof(formats);++i)assert(bytes[i]==0x5a);
    }
    assert_rate_values(0);
}
static uint64_t rate_request(Float64 rate) {
    assert(nominal_set(rate)==0);flush_configuration_queue();assert(atomic_load(&configuration_pending));return requested_action;
}
static void assert_rate_notifications(void) {
    const AudioObjectPropertySelector selectors[]={kAudioDevicePropertyNominalSampleRate,RK3_PROPERTY_INFO,
        kAudioDevicePropertyLatency,kAudioDevicePropertyLatency,kAudioDevicePropertyLatency,
        kAudioDevicePropertySafetyOffset,kAudioDevicePropertySafetyOffset,kAudioDevicePropertySafetyOffset,
        kAudioStreamPropertyPhysicalFormat,kAudioStreamPropertyVirtualFormat,kAudioStreamPropertyPhysicalFormat,kAudioStreamPropertyVirtualFormat};
    const AudioObjectPropertyScope scopes[]={kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyScopeInput,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyScopeInput,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyScopeGlobal};
    assert(notification_count==12);
    for(unsigned i=0;i<12;++i)assert(notifications[i].object==(i<8?DEVICE:i<10?INPUT:OUTPUT)&&
        notifications[i].address.mSelector==selectors[i]&&notifications[i].address.mScope==scopes[i]&&notifications[i].address.mElement==0);
}
static void rate_commit(uint64_t action,unsigned index) {
    notification_count=0;inspect_rate_notifications=true;
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,action,NULL)==0);
    inspect_rate_notifications=false;assert_rate_values(index);assert_rate_notifications();assert(!atomic_load(&configuration_pending));
}
static void rate_handshake(void) {
    fake_host=(AudioServerPlugInHostInterface){.PropertiesChanged=changed,.RequestDeviceConfigurationChange=request_change};
    host=&fake_host;device_control_queue=dispatch_queue_create("rk3.fake.host.rates",DISPATCH_QUEUE_SERIAL);
    dispatch_queue_set_specific(device_control_queue,&queue_key,&queue_key,NULL);configuration_result=0;configuration_requests=0;
    assert(rk3_usb_set_audio_panic_handler(&mock_session,audio_panic_handler,NULL));
    assert(!atomic_load(&running)&&!mock_running);assert(nominal_set(44100)==0);flush_configuration_queue();assert(configuration_requests==0&&rate_changes==0);
    const Float64 invalid[]={NAN,INFINITY,-INFINITY,-1,0,32000,44100.5,4294967296.0};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)assert(nominal_set(invalid[i])==kAudioHardwareUnsupportedOperationError);
    AudioObjectPropertyAddress a={kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,0};
    AudioStreamBasicDescription invalid_format=literal_format(48000);invalid_format.mChannelsPerFrame=1;
    assert(table.SetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(invalid_format),&invalid_format)==kAudioHardwareUnsupportedOperationError);
    invalid_format=literal_format(48000);invalid_format.mReserved=1;
    assert(table.SetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(invalid_format),&invalid_format)==kAudioHardwareUnsupportedOperationError);
    invalid_format=literal_format(48000);invalid_format.mSampleRate=NAN;
    assert(table.SetPropertyData(driver,INPUT,0,&a,0,NULL,sizeof(invalid_format),&invalid_format)==kAudioHardwareUnsupportedOperationError);
    flush_configuration_queue();assert(configuration_requests==0&&rate_changes==0);
    /* Exercise all four stream-format setter routes and the nominal route. */
    for(unsigned i=0;i<5;++i) {
        unsigned index=(i+1)%5;Float64 old=nominal_rate();unsigned requests=configuration_requests;
        if(i<4) {
            a.mSelector=i<2?kAudioStreamPropertyVirtualFormat:kAudioStreamPropertyPhysicalFormat;
            AudioStreamBasicDescription value=literal_format(rate_cases[index].rate);
            assert(table.SetPropertyData(driver,i%2?OUTPUT:INPUT,0,&a,0,NULL,sizeof(value),&value)==0);
        } else assert(nominal_set(rate_cases[index].rate)==0);
        assert(nominal_rate()==old);flush_configuration_queue();assert(configuration_requests==requests+1);
        uint64_t action=requested_action;assert(nominal_rate()==old);rate_commit(action,index);
    }
    /* Duplicate requests coalesce; conflicts and stale callbacks cannot commit. */
    uint64_t first=rate_request(48000);unsigned requests=configuration_requests;
    assert(nominal_set(48000)==0&&nominal_set(44100)==kAudioHardwareIllegalOperationError&&nominal_set(96000)==kAudioHardwareIllegalOperationError);
    flush_configuration_queue();assert(configuration_requests==requests&&nominal_rate()==44100);
    assert(table.AbortDeviceConfigurationChange(driver,DEVICE,first,NULL)==0&&nominal_rate()==44100);
    uint64_t second=rate_request(96000);assert(first!=second);
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,first,NULL)==kAudioHardwareUnsupportedOperationError);
    assert(table.AbortDeviceConfigurationChange(driver,DEVICE,first,NULL)==kAudioHardwareUnsupportedOperationError);
    assert(atomic_load(&configuration_pending)&&nominal_rate()==44100);rate_commit(second,3);
    rate_commit(rate_request(44100),0);
    /* A zero client count after Stop timeout is not proof of callback drain. */
    assert(table.StartIO(driver,DEVICE,88)==0);uint64_t action=rate_request(48000);unsigned changed_rates=rate_changes;
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,action,NULL)==kAudioHardwareIllegalOperationError&&rate_changes==changed_rates);
    stop_error=ETIMEDOUT;mock_draining=true;assert(table.StopIO(driver,DEVICE,88)==ETIMEDOUT);stop_error=0;notification_count=0;
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,action,NULL)==EBUSY&&nominal_rate()==44100&&notification_count==0);
    mock_draining=false;
    rate_error=EIO;action=rate_request(48000);notification_count=0;
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,action,NULL)==EIO&&nominal_rate()==44100&&mock_sample_rate==44100&&notification_count==0);rate_error=0;
    configuration_result=kAudioHardwareUnspecifiedError;assert(nominal_set(48000)==0);flush_configuration_queue();configuration_result=0;
    assert(!atomic_load(&configuration_pending)&&nominal_rate()==44100);
    /* Reconnect invalidates the captured cohort before dispatch or Perform. */
    requests=configuration_requests;dispatch_suspend(device_control_queue);assert(nominal_set(48000)==0);++mock_generation;
    dispatch_resume(device_control_queue);flush_configuration_queue();assert(configuration_requests==requests&&!atomic_load(&configuration_pending));--mock_generation;
    action=rate_request(48000);++mock_generation;
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,action,NULL)==kAudioHardwareNotRunningError&&nominal_rate()==44100);--mock_generation;
    /* HAL observes G1, but the worker reaches G2 before accepting the request.
     * The original generation must cross the backend handoff unchanged. */
    action=rate_request(48000);uint64_t generation=mock_generation;reconnect_at_rate_handoff=true;notification_count=0;
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,action,NULL)==ESTALE);
    assert(rate_generation_argument==generation&&mock_generation==generation+1&&nominal_rate()==44100&&mock_sample_rate==44100&&notification_count==0);
    --mock_generation;
    /* Consume a deferred panic only when the covering rate transaction commits. */
    action=rate_request(48000);requests=configuration_requests;trigger_panic(mock_generation);flush_configuration_queue();rate_commit(action,1);
    flush_configuration_queue();assert(configuration_requests==requests);
    action=rate_request(44100);requests=configuration_requests;dispatch_suspend(device_control_queue);trigger_panic(mock_generation);
    rate_commit(action,0);dispatch_resume(device_control_queue);flush_configuration_queue();assert(configuration_requests==requests);
    action=rate_request(48000);requests=configuration_requests;trigger_panic(mock_generation);flush_configuration_queue();
    assert(table.AbortDeviceConfigurationChange(driver,DEVICE,action,NULL)==0);flush_configuration_queue();assert(configuration_requests==requests+1);
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,requested_action,NULL)==0&&nominal_rate()==44100);
    action=rate_request(48000);requests=configuration_requests;trigger_panic(mock_generation);flush_configuration_queue();rate_error=EIO;
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,action,NULL)==EIO);rate_error=0;flush_configuration_queue();assert(configuration_requests==requests+1);
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,requested_action,NULL)==0&&nominal_rate()==44100);
    configuration_result=kAudioHardwareUnspecifiedError;panic_during_request=true;requests=configuration_requests;
    assert(nominal_set(48000)==0);flush_configuration_queue();flush_configuration_queue();
    assert(configuration_requests==requests+2&&atomic_load(&configuration_pending)&&nominal_rate()==44100);
    assert(table.PerformDeviceConfigurationChange(driver,DEVICE,requested_action,NULL)==0);
    /* A synchronous host Perform admits another request before returning the old
     * request's error. The late old error must not erase the newer request. */
    reentrant_perform=true;reentrant_follow_rate=96000;configuration_result=kAudioHardwareUnspecifiedError;
    assert(nominal_set(48000)==0);flush_configuration_queue();flush_configuration_queue();
    assert(nominal_rate()==48000&&atomic_load(&configuration_pending));
    rate_commit(requested_action,3);rate_commit(rate_request(44100),0);
    /* New start epoch supersedes old sample positions, retaining NI's anchor. */
    mock_clock=(rk3_usb_clock_snapshot){.sample_frame=0,.host_time=4000000000,.seed=3,.zero_sample_frame=0,.zero_host_time=3999123456};
    Float64 frame=99;UInt64 time=0,seed=0;
    assert(table.GetZeroTimeStamp(driver,DEVICE,88,&frame,&time,&seed)==0&&frame==0&&time==3999123456&&seed==3);
    assert(PERIOD==16384);assert(rk3_usb_set_audio_panic_handler(&mock_session,NULL,NULL));
    flush_configuration_queue();dispatch_release(device_control_queue);device_control_queue=NULL;host=NULL;
}
int main(void){
    assert(RK3AudioFactory(NULL,kAudioServerPlugInTypeUUID)==driver);
    rk3_input_gain_init(&mock_gain);usb=&mock_session;usb_identity_property_test();usb_identity_raw_transport_test();usb_identity_client_test();usb_identity_client_header_test();usb_identity_client_field_test();usb_identity_partial_and_status_test();usb_identity_client_envelope_test();usb_identity_property_validation_test();spec_property_transport_test();spec_domain_reply_test();spec_client_transport_test();spec_client_invalid_reply_test();spec_metadata_and_query_test();spec_native_status_test();spec_client_response_matrix_test();spec_initialization_lifetime_test();usb_identity_initialization_lifetime_test();properties();input_gain_transport_test();audio();stream_activity();panic_handshake();rate_metadata_test();rate_handshake();assert(opens==2&&closes==2);
    puts("Audio driver: host ABI, CFData, NI timing, addressed PCM, lifecycle, clock and panic handshake passed");return 0;
}
