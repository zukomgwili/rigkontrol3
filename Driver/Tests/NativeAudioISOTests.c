#include "NativeAudioISO.h"
#include <assert.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A fake IOUSBLib interface exercises callbacks and ownership without USB I/O. */
typedef struct {
    void *data,*context;
    IOUSBLowLatencyIsocFrame *frames;
    IOAsyncCallback1 callback;
    uint64_t scheduled_frame;
    unsigned microframes;
    bool pending;
} MockTransfer;
static struct {
    MockTransfer reads[64],writes[128];
    unsigned read_count,write_count,allocations,destroys,commands,retains,releases;
    unsigned policy_calls,property_calls,bad_property;
    bool fail_command,fail_write;
    uint8_t command_bytes[2][6];
} mock;
static ULONG retain_interface(void *self) {(void)self;++mock.retains;return 1;}
static ULONG release_interface(void *self) {(void)self;++mock.releases;return 0;}

static IOReturn create_buffer(void *self,void **buffer,IOByteCount size,UInt32 type) {
    (void)self;
    assert(type <= kUSBLowLatencyFrameListBuffer);
    *buffer=calloc(1,(size_t)size);assert(*buffer != NULL);++mock.allocations;
    return kIOReturnSuccess;
}
static IOReturn destroy_buffer(void *self,void *buffer) {(void)self;free(buffer);++mock.destroys;return 0;}
static IOReturn set_policy(void *self,UInt8 pipe,UInt16 size,UInt8 interval) {
    (void)self;assert((pipe == 1 || pipe == 2) && size == 136 && interval == 1);
    ++mock.policy_calls;return 0;
}
static IOReturn pipe_properties(void *self,UInt8 pipe,UInt8 *direction,UInt8 *number,
    UInt8 *type,UInt16 *size,UInt8 *interval) {
    (void)self;assert(pipe == 1 || pipe == 2);++mock.property_calls;
    *direction=pipe == 1 ? kUSBIn : kUSBOut;*number=pipe == 1 ? 2 : 6;
    *type=kUSBIsoc;*size=512;*interval=1;
    unsigned defect=mock.bad_property;
    if(pipe == 2) defect=defect > 5 ? defect-5 : 0;
    else if(defect > 5) defect=0;
    if(defect == 1) *direction^=1;
    if(defect == 2) *number=3;
    if(defect == 3) *type=kUSBBulk;
    if(defect == 4) *size=256;
    if(defect == 5) *interval=2;
    return 0;
}
static IOReturn get_frame(void *self,UInt64 *frame,AbsoluteTime *stamp) {
    (void)self;*frame=1000;memset(stamp,0,sizeof(*stamp));return 0;
}
static bool send_command(void *context,const uint8_t *bytes,size_t length,uint32_t timeout) {
    (void)context;assert(length == 6 && bytes[0] == 9 && timeout == 1200);
    if(mock.commands < 2) memcpy(mock.command_bytes[mock.commands],bytes,6);
    ++mock.commands;return !mock.fail_command;
}
static MockTransfer *find_transfer(MockTransfer *transfers,unsigned capacity,
                                  IOUSBLowLatencyIsocFrame *frames,unsigned *count) {
    for(unsigned index=0;index<*count;++index) if(transfers[index].frames == frames) return transfers+index;
    assert(*count < capacity);return transfers+(*count)++;
}
static IOReturn read_iso(void *self,UInt8 pipe,void *data,UInt64 frame,UInt32 count,
    UInt32 frequency,IOUSBLowLatencyIsocFrame *frames,IOAsyncCallback1 callback,void *context) {
    (void)self;assert(pipe == 1 && count == 8 && frequency == 0);
    MockTransfer *transfer=find_transfer(mock.reads,64,frames,&mock.read_count);
    assert(!transfer->pending);
    for(unsigned index=0;index<8;++index) assert(frames[index].frReqCount == 512);
    *transfer=(MockTransfer){data,context,frames,callback,frame,count,true};return 0;
}
static IOReturn write_iso(void *self,UInt8 pipe,void *data,UInt64 frame,UInt32 count,
    UInt32 frequency,IOUSBLowLatencyIsocFrame *frames,IOAsyncCallback1 callback,void *context) {
    (void)self;assert(pipe == 2 && count > 0 && count <= 8 && frequency == 1);
    if(mock.fail_write) return kIOReturnIOError;
    unsigned trailing=0;
    while(trailing < count && frames[count-trailing-1].frReqCount == 0) ++trailing;
    /* Reproduce the observed current-host rejection at the real API seam. */
    if(trailing >= 2) return kIOReturnBadArgument;
    MockTransfer *transfer=find_transfer(mock.writes,128,frames,&mock.write_count);
    assert(!transfer->pending);
    *transfer=(MockTransfer){data,context,frames,callback,frame,count,true};return 0;
}
static IOReturn abort_pipe(void *self,UInt8 pipe) {
    (void)self;
    MockTransfer *transfers=pipe == 1 ? mock.reads : mock.writes;
    unsigned count=pipe == 1 ? mock.read_count : mock.write_count;
    for(unsigned index=0;index<count;++index) {
        MockTransfer *transfer=transfers+index;
        if(transfer->pending) {
            transfer->pending=false;
            transfer->callback(transfer->context,kIOReturnAborted,transfer->frames);
        }
    }
    return 0;
}

static void complete_input_counts(unsigned slot,const uint16_t counts[8],bool corrupt) {
    MockTransfer *transfer=mock.reads+slot;assert(transfer->pending);
    for(unsigned index=0;index<8;++index) {
        transfer->frames[index].frStatus=0;transfer->frames[index].frActCount=counts[index];
        const uint8_t period[]={1,0x40,0,0,0,0x40,0,0};
        uint8_t *packet=(uint8_t *)transfer->data+index*512;
        assert(counts[index] <= 512 && counts[index]%8 == 0);
        for(unsigned frame=0;frame<counts[index]/8;++frame) memcpy(packet+frame*8,period,8);
    }
    if(corrupt) ((uint8_t *)transfer->data)[4]=7;
    transfer->pending=false;
    transfer->callback(transfer->context,0,transfer->frames);
}
static void complete_input(unsigned slot,bool corrupt) {
    const uint16_t counts[8]={48,48,48,48,48,48,48,48};
    complete_input_counts(slot,counts,corrupt);
}
static void complete_output(unsigned slot) {
    MockTransfer *transfer=mock.writes+slot;assert(transfer->pending);
    for(unsigned index=0;index<transfer->microframes;++index) {
        transfer->frames[index].frStatus=0;
        transfer->frames[index].frActCount=transfer->frames[index].frReqCount;
        uint64_t stamp=mach_absolute_time();
        memcpy(&transfer->frames[index].frTimeStamp,&stamp,sizeof(stamp));
    }
    transfer->pending=false;
    transfer->callback(transfer->context,0,transfer->frames);
}

static rk3_native_audio_iso *new_engine(IOUSBInterfaceInterface300 **interface) {
    memset(&mock,0,sizeof(mock));
    rk3_native_audio_iso *engine=rk3_native_audio_iso_create(interface,1,2,send_command,NULL);
    assert(engine != NULL && mock.allocations == 4);
    assert(rk3_native_audio_iso_is_quiescent(engine));return engine;
}

static void test_full_session(IOUSBInterfaceInterface300 **interface) {
    rk3_native_audio_iso *engine=new_engine(interface);
    assert(rk3_native_audio_iso_start(engine) == 0);
    const uint8_t stop[]={9,255,255,136,0,1},start[]={9,1,2,136,0,1};
    assert(mock.commands == 2 && memcmp(mock.command_bytes[0],stop,6) == 0 && memcmp(mock.command_bytes[1],start,6) == 0);
    assert(mock.read_count == 64 && rk3_native_audio_iso_pending(engine) == 64);
    assert(mock.reads[0].scheduled_frame == 1101 && mock.reads[63].scheduled_frame == 1164);
    assert(!rk3_native_audio_iso_destroy(engine));
    assert(!rk3_native_audio_iso_release_usb(engine));
    float render[192];for(unsigned index=0;index<192;++index) render[index]=0.5f;
    assert(rk3_native_audio_iso_render(engine,render,96) == 0);
    rk3_native_audio_clock clock;
    for(unsigned slot=0;slot<64;++slot) {
        complete_input(slot,false);
        assert(mock.writes[slot].scheduled_frame == 1105+slot);
        uint8_t *wire=mock.writes[slot].data;
        assert(wire[0] == 0 && wire[1] == 0 && wire[2] == 1 && wire[3] == 0 && wire[6] == 0);
        complete_output(slot);
        assert(!rk3_native_audio_iso_clock(engine,&clock));
    }
    assert(rk3_native_audio_iso_pending(engine) == 64);
    complete_input(0,false);
    assert(mock.reads[0].scheduled_frame == 1229 && mock.writes[0].scheduled_frame == 1169);
    uint8_t *wire=mock.writes[0].data;
    assert(wire[0] == 0 && wire[1] == 0 && wire[2] == 1 && wire[3] == 0 && wire[7] == 0);
    float captured[96];assert(rk3_native_audio_iso_capture(engine,captured,48) == 48);
    assert(captured[0] == 0.0f && captured[1] == 0.5f);
    for(unsigned index=2;index<96;++index) assert(captured[index] == 0.5f);
    assert(rk3_native_audio_iso_capture(engine,captured,48) == 0);
    assert(rk3_native_audio_iso_clock(engine,&clock));
    assert(clock.sample_frame == 3072 && clock.host_time != 0 && clock.seed != 0);
    assert(rk3_native_audio_iso_render(engine,render,96) == 96);
    complete_output(0);
    complete_input(1,false);
    assert(mock.writes[1].scheduled_frame == 1170);
    wire=mock.writes[1].data;
    assert(wire[0] == 0 && wire[1] == 0 && wire[2] == 1 && wire[3] == 0x40 && wire[7] == 0x40);
    complete_output(1);
    assert(rk3_native_audio_iso_clock(engine,&clock) && clock.sample_frame == 3168);
    rk3_native_audio_iso_stop(engine);
    assert(rk3_native_audio_iso_pending(engine) == 0 && !rk3_native_audio_iso_running(engine));
    assert(!rk3_native_audio_iso_clock(engine,&clock));
    assert(rk3_native_audio_iso_render(engine,render,96) == 0);
    assert(rk3_native_audio_iso_release_usb(engine) && mock.destroys == 4);
    assert(rk3_native_audio_iso_release_usb(engine) && mock.destroys == 4);
    assert(rk3_native_audio_iso_render(engine,render,96) == 0);
    assert(rk3_native_audio_iso_start(engine) == kIOReturnNoDevice);
    assert(rk3_native_audio_iso_destroy(engine) && mock.destroys == 4 && mock.releases == 1);
}

static void test_failures(IOUSBInterfaceInterface300 **interface) {
    rk3_native_audio_iso *engine=new_engine(interface);mock.fail_command=true;
    assert(rk3_native_audio_iso_start(engine) != 0 && mock.read_count == 0);
    assert(rk3_native_audio_iso_destroy(engine));
    engine=new_engine(interface);
    assert(rk3_native_audio_iso_start(engine) == 0);mock.fail_write=true;
    complete_input(0,false);
    assert(!rk3_native_audio_iso_running(engine) && rk3_native_audio_iso_error(engine) != 0);
    assert(rk3_native_audio_iso_is_quiescent(engine) && rk3_native_audio_iso_destroy(engine));
    engine=new_engine(interface);
    assert(rk3_native_audio_iso_start(engine) == 0);
    for(unsigned slot=0;slot<64;++slot) {complete_input(slot,false);complete_output(slot);}
    complete_input(0,true);
    assert(!rk3_native_audio_iso_running(engine) && rk3_native_audio_iso_error(engine) != 0);
    assert(rk3_native_audio_iso_is_quiescent(engine) && rk3_native_audio_iso_destroy(engine));
}

static void test_empty_output_tails(IOUSBInterfaceInterface300 **interface) {
    rk3_native_audio_iso *engine=new_engine(interface);
    assert(rk3_native_audio_iso_start(engine) == 0);
    for(unsigned slot=0;slot<64;++slot) {complete_input(slot,false);complete_output(slot);}
    complete_input(0,false);complete_output(0);
    const uint16_t first[8]={136,0,136,0,0,0,0,0},zero[8]={0};
    complete_input_counts(1,first,false);
    assert(rk3_native_audio_iso_running(engine) && mock.write_count == 64);
    assert(mock.writes[1].microframes == 3 && mock.writes[1].scheduled_frame == 1170);
    for(unsigned index=0;index<3;++index) assert(mock.writes[1].frames[index].frReqCount == first[index]);
    for(unsigned index=0;index<272;++index)
        assert(((uint8_t *)mock.writes[1].data)[index] == (index%8 == 2 ? 1 : 0));
    complete_output(1); /* Unsubmitted tail descriptors retain the pending marker. */
    rk3_native_audio_clock before,after;
    assert(rk3_native_audio_iso_clock(engine,&before) && before.sample_frame == 3154);
    complete_input_counts(2,zero,false);
    assert(mock.write_count == 64 && !mock.writes[2].pending && rk3_native_audio_iso_pending(engine) == 64);
    assert(rk3_native_audio_iso_clock(engine,&after) && after.sample_frame == before.sample_frame && after.host_time == before.host_time);
    const uint16_t next[8]={0,136,0,136,0,0,0,0};
    complete_input_counts(3,next,false);
    assert(mock.write_count == 64 && mock.writes[3].microframes == 4);
    assert(mock.writes[3].scheduled_frame == 1172 && mock.reads[3].scheduled_frame == 1232);
    for(unsigned index=0;index<4;++index) assert(mock.writes[3].frames[index].frReqCount == next[index]);
    complete_output(3);
    assert(rk3_native_audio_iso_clock(engine,&after) && after.sample_frame == 3188);
    assert(mock.policy_calls == 0 && mock.property_calls == 2);
    rk3_native_audio_iso_stop(engine);assert(rk3_native_audio_iso_destroy(engine));
}

/* Exercise the actual production LowLatencyWrite submission calendar: retaining
 * 64 prequeued reads must not force each live output 64 ms into the future. */
static void test_low_latency_startup(IOUSBInterfaceInterface300 **interface) {
    rk3_native_audio_iso *engine=new_engine(interface);
    assert(rk3_native_audio_iso_start(engine) == 0);
    float samples[96];rk3_native_audio_clock clock;
    for(unsigned slot=0;slot<64;++slot) {
        uint64_t received=mock.reads[slot].scheduled_frame;
        complete_input(slot,false);
        assert(mock.writes[slot].scheduled_frame == received+RK3_AUDIO_OUTPUT_LEAD_MILLISECONDS);
        complete_output(slot);
        /* Early completed output must not let Start return and accumulate
         * 60 ms of render while prepare_output_pcm is still priming. */
        assert(!rk3_native_audio_iso_clock(engine,&clock));
        assert(rk3_native_audio_iso_render(engine,samples,48) == 0);
        assert(rk3_native_audio_iso_capture(engine,samples,48) == 0);
    }
    complete_input(0,false);complete_output(0);
    assert(rk3_native_audio_iso_clock(engine,&clock));
    /* Let capture accumulate for 20 ms as if the first HAL callback were late.
     * The consumer owns its tail and discards old data before returning PCM. */
    for(unsigned slot=1;slot<=20;++slot) {complete_input(slot,false);complete_output(slot);}
    assert(rk3_native_audio_iso_capture(engine,samples,48) == 48);
    assert(rk3_native_audio_iso_capture(engine,samples,48) == 48);
    assert(rk3_native_audio_iso_capture(engine,samples,48) == 0);
    rk3_native_audio_iso_stop(engine);
    assert(rk3_native_audio_iso_destroy(engine));
}

static void test_out_of_order_input(IOUSBInterfaceInterface300 **interface) {
    rk3_native_audio_iso *engine=new_engine(interface);
    assert(rk3_native_audio_iso_start(engine) == 0);
    complete_input(1,false); /* Reversed calendar must not reach the encoder. */
    assert(!rk3_native_audio_iso_running(engine) && rk3_native_audio_iso_error(engine) != 0);
    assert(mock.write_count == 0 && rk3_native_audio_iso_pending(engine) == 0);
    assert(rk3_native_audio_iso_is_quiescent(engine) && rk3_native_audio_iso_destroy(engine));
}

static void test_pipe_geometry(IOUSBInterfaceInterface300 **interface) {
    for(unsigned defect=1;defect<=10;++defect) {
        rk3_native_audio_iso *engine=new_engine(interface);mock.bad_property=defect;
        assert(rk3_native_audio_iso_start(engine) == kIOReturnBadArgument);
        assert(mock.commands == 0 && mock.read_count == 0 && mock.policy_calls == 0);
        assert(rk3_native_audio_iso_destroy(engine));
    }
}

int main(void) {
    IOUSBInterfaceInterface300 methods={0};
    methods.AddRef=retain_interface;methods.Release=release_interface;
    methods.LowLatencyCreateBuffer=create_buffer;methods.LowLatencyDestroyBuffer=destroy_buffer;
    methods.SetPipePolicy=set_policy;methods.GetBusFrameNumber=get_frame;
    methods.GetPipeProperties=pipe_properties;
    methods.LowLatencyReadIsochPipeAsync=read_iso;methods.LowLatencyWriteIsochPipeAsync=write_iso;
    methods.AbortPipe=abort_pipe;
    IOUSBInterfaceInterface300 *pointer=&methods;
    test_low_latency_startup(&pointer);test_full_session(&pointer);test_failures(&pointer);test_empty_output_tails(&pointer);test_out_of_order_input(&pointer);test_pipe_geometry(&pointer);
    puts("NativeAudioISO: fake-interface startup, priming, PCM queues, clock, abort/drain and failures passed.");
    return 0;
}
