#include "NativeAudioISO.h"
#include "NativeHostUSB.h"
#include "AudioRing.h"

#include <assert.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Actual module, as in NativeUSBTests: deterministic ownership contention
 * without adding a production probe or exporting private state. */
#include "../NativeAudioISO.c"

/* Hardware-free host binding. The bus advances independently of callback
 * delivery, and queued output retains the external ring instead of copying. */
struct RK3HostInterface { unsigned references; };
struct RK3HostBuffer { uint8_t *bytes; size_t size; };
typedef struct {
    RK3HostIsoFrame frame;
    uint32_t offset;
    bool wrap;
} MockEntry;
struct RK3HostIsoList { MockEntry *entries; size_t count; };
typedef struct {
    RK3HostBuffer *buffer;
    RK3HostIsoList *list;
    size_t first,count;
    uint64_t scheduled_frame;
    RK3HostIsoCompletion completion;
    void *context;
    bool pending,aborted;
    uint64_t acceptance;
} MockTransfer;
static struct {
    RK3HostInterface interface;
    MockTransfer reads[64],writes[128];
    unsigned read_count,write_count,read_submissions,write_submissions;
    unsigned buffers,buffer_releases,lists,list_releases,commands,retains,releases;
    unsigned aborts[3],clears[3],pipe_queries,read_failures_remaining,reads_at_format,format_failures_remaining;
    uint64_t bus_frame,anchor_host,accepted[3];
    unsigned clock_queries,clock_queries_before_format;
    unsigned stream_begins,clock_begins,clock_ends;
    uint64_t stream_epoch,clock_begin_tick,clock_end_tick;
    unsigned stream_read_submissions;
    uint64_t clock_script[16];unsigned clock_script_count,clock_script_index;
    uint64_t clock_after_boundary_shift;unsigned clock_shift_applied;
    int32_t clock_failure;bool persistent_clock_failure,poison_clock_pair;
    int32_t clock_error_script[4];unsigned clock_error_count,clock_error_index;
    int32_t fail_read,fail_write;
    bool persistent_read_failure,persistent_write_failure,fail_command,fail_stop,defer_abort,primary_supported;
    uint8_t commands_bytes[2][6];
    MockTransfer *last_write;
} mock;
static atomic_bool block_snapshot,snapshot_entered,release_snapshot;
static rk3_native_audio_iso *priming_observer;
static rk3_native_audio_iso *clock_start_observer;
static unsigned clock_start_observations;
static void (*clock_frame_hook)(void);
static unsigned priming_attempts[2];
static bool hold_priming_guard,fail_priming_prepare;
static void observe_priming_attempt(uint8_t pipe) {
    if(!priming_observer)return;
    rk3_native_audio_iso *e=priming_observer;
    ++priming_attempts[pipe-1];
    /* NI7e35/7e40 precede7e92. Both RT entry points must still see priming,
     * including when epoch2 input0 has already been published by hardware. */
    assert(e->schedule.priming&&e->decoder.priming&&e->output_ring.priming);
    assert(!atomic_load(&e->pcm_ready));
    float capture[2]={1,1},render[2]={0.5f,0.5f};
    assert(rk3_native_audio_iso_capture_at(e,capture,0,1)==1);
    assert(capture[0]==0&&capture[1]==0&&!e->initial_timestamp_queued);
    assert(rk3_native_audio_iso_render_at(e,render,128,1)==1);
    const uint8_t silent[8]={0,0,1,0,0,0,0,0};
    assert(!memcmp((uint8_t *)rk3_host_buffer_bytes(e->wire_buffer)+128*8,silent,8));
    if(pipe==2&&hold_priming_guard)
        assert(!atomic_flag_test_and_set_explicit(&e->processing,memory_order_acquire));
}

static uint64_t ticks_for_ns(uint64_t nanoseconds) {
    mach_timebase_info_data_t scale;assert(mach_timebase_info(&scale) == 0);
    return nanoseconds*scale.denom/scale.numer;
}
static uint64_t host_for_frame(uint64_t frame) {
    assert(frame >= 1000);return mock.anchor_host+ticks_for_ns((frame-1000)*UINT64_C(1000000));
}
static int32_t clock_frame(void *context,uint64_t *frame,uint64_t *paired_time) {
    (void)context;++mock.clock_queries;
    if(clock_frame_hook)clock_frame_hook();
    if(mock.commands<2)++mock.clock_queries_before_format;
    if(mock.clock_error_index<mock.clock_error_count) {
        int32_t error=mock.clock_error_script[mock.clock_error_index++];if(error)return error;
    }
    if(mock.clock_failure) {
        int32_t error=mock.clock_failure;if(!mock.persistent_clock_failure)mock.clock_failure=0;
        return error;
    }
    /* Four observations span a boundary in this deterministic controller.
     * Local uptime is exactly the controller frame boundary, independently
     * of callback delivery and without a real sleep. */
    if(mock.clock_script_count) {
        assert(mock.clock_script_index<mock.clock_script_count);
        mock.bus_frame=mock.clock_script[mock.clock_script_index++];
    } else if(mock.clock_queries%4==0)++mock.bus_frame;
    *frame=mock.bus_frame;*paired_time=host_for_frame(mock.bus_frame);
    if(mock.poison_clock_pair)*paired_time+=ticks_for_ns(UINT64_C(10000000000));
    return 0;
}
static uint64_t clock_uptime(void *context) {
    (void)context;uint64_t time=host_for_frame(mock.bus_frame);
    if(mock.clock_script_count==4&&mock.clock_script_index==4&&mock.clock_after_boundary_shift) {
        ++mock.clock_shift_applied;assert(time>mock.clock_after_boundary_shift);
        time-=mock.clock_after_boundary_shift;
    }
    return time;
}
static uint64_t clock_to_ns(void *context,uint64_t ticks) {
    (void)context;mach_timebase_info_data_t scale;assert(mach_timebase_info(&scale)==0);
    return ticks*scale.numer/scale.denom;
}
static uint64_t clock_to_ticks(void *context,uint64_t ns) {(void)context;return ticks_for_ns(ns);}
static const NIAudioClockProvider clock_provider={NULL,clock_frame,clock_uptime,clock_to_ns,clock_to_ticks,NULL};
void rk3_host_measurement_begin_stream(RK3HostInterface *interface,uint64_t epoch) {
    assert(interface==&mock.interface);++mock.stream_begins;mock.stream_epoch=epoch;mock.stream_read_submissions=mock.read_submissions;
}
void rk3_host_measurement_clock(RK3HostInterface *interface,bool beginning,uint64_t tick) {
    assert(interface==&mock.interface);
    assert(mock.stream_begins>0&&mock.commands>=2);
    if(beginning){++mock.clock_begins;mock.clock_begin_tick=tick;}else {++mock.clock_ends;mock.clock_end_tick=tick;}
}
void rk3_host_retain(RK3HostInterface *interface) { assert(interface == &mock.interface);++interface->references;++mock.retains; }
void rk3_host_release(RK3HostInterface *interface) { assert(interface == &mock.interface && interface->references > 0);--interface->references;++mock.releases; }
int32_t rk3_host_pipe_properties(RK3HostInterface *interface, uint8_t pipe, RK3HostPipeProperties *properties) {
    assert(interface == &mock.interface && (pipe == 1 || pipe == 2));++mock.pipe_queries;
    *properties=(RK3HostPipeProperties){pipe == 1 ? 1 : 0,pipe == 1 ? 2 : 6,1,1,512};return 0;
}
int32_t rk3_host_clear_stall(RK3HostInterface *interface, uint8_t pipe) {
    assert(interface == &mock.interface && pipe >= 1 && pipe <= 2);++mock.clears[pipe];return 0;
}
int32_t rk3_host_bus_frame(RK3HostInterface *interface, uint64_t *frame, uint64_t *host_time) {
    assert(interface == &mock.interface);*frame=mock.bus_frame;*host_time=host_for_frame(mock.bus_frame);return 0;
}
bool rk3_host_primary_lists_supported(void) {return mock.primary_supported;}
RK3HostBuffer *rk3_host_buffer_create(RK3HostInterface *interface, size_t size, int32_t *error) {
    assert(interface == &mock.interface);RK3HostBuffer *buffer=calloc(1,sizeof(*buffer));assert(buffer);
    buffer->bytes=calloc(1,size);assert(buffer->bytes);buffer->size=size;++mock.buffers;*error=0;return buffer;
}
void *rk3_host_buffer_bytes(RK3HostBuffer *buffer) {return buffer->bytes;}
size_t rk3_host_buffer_size(const RK3HostBuffer *buffer) {return buffer->size;}
void rk3_host_buffer_release(RK3HostBuffer *buffer) {
    if(!buffer)return;
    for(unsigned i=0;i<mock.read_count;++i)assert(!mock.reads[i].pending || mock.reads[i].buffer != buffer);
    for(unsigned i=0;i<mock.write_count;++i)assert(!mock.writes[i].pending || mock.writes[i].buffer != buffer);
    free(buffer->bytes);free(buffer);++mock.buffer_releases;
}
RK3HostIsoList *rk3_host_iso_list_create(RK3HostInterface *interface, size_t count, int32_t *error) {
    assert(interface == &mock.interface);RK3HostIsoList *list=calloc(1,sizeof(*list));assert(list);
    list->entries=calloc(count,sizeof(*list->entries));assert(list->entries);list->count=count;++mock.lists;*error=0;return list;
}
size_t rk3_host_iso_list_count(const RK3HostIsoList *list) {return list->count;}
void rk3_host_iso_list_release(RK3HostIsoList *list) {
    if(!list)return;
    for(unsigned i=0;i<mock.read_count;++i)assert(!mock.reads[i].pending || mock.reads[i].list != list);
    for(unsigned i=0;i<mock.write_count;++i)assert(!mock.writes[i].pending || mock.writes[i].list != list);
    free(list->entries);free(list);++mock.list_releases;
}
int32_t rk3_host_iso_list_set(RK3HostIsoList *list, size_t index, uint32_t requested, uint32_t offset, bool wrap) {
    assert(index < list->count && requested <= 512);
    if(fail_priming_prepare){fail_priming_prepare=false;return kIOReturnBusy;}
    list->entries[index]=(MockEntry){{RK3_HOST_ISO_PENDING,requested,0,0},offset,wrap};return 0;
}
bool rk3_host_iso_list_snapshot(const RK3HostIsoList *list, size_t index, RK3HostIsoFrame *frame) {
    if(index >= list->count)return false;
    if(atomic_load(&block_snapshot)) {
        atomic_store(&snapshot_entered,true);
        while(!atomic_load(&release_snapshot)){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
    }
    *frame=list->entries[index].frame;return true;
}
static MockTransfer *find_transfer(MockTransfer *transfers, unsigned capacity, unsigned *count,
    RK3HostIsoList *list, size_t first) {
    for(unsigned i=0;i<*count;++i)if(transfers[i].list == list && transfers[i].first == first)return transfers+i;
    assert(*count < capacity);return transfers+(*count)++;
}
int32_t rk3_host_iso_enqueue(RK3HostInterface *interface, uint8_t pipe,
    RK3HostBuffer *buffer, RK3HostIsoList *list, size_t first, size_t count,
    uint64_t frame, RK3HostIsoCompletion completion, void *context) {
    assert(interface == &mock.interface && (pipe == 1 || pipe == 2));
    assert(count > 0 && count <= 8 && first+count <= list->count);
    int32_t *failure=pipe == 1 ? &mock.fail_read : &mock.fail_write;
    if(pipe == 1)++mock.read_submissions;else ++mock.write_submissions;
    observe_priming_attempt(pipe);
    if(pipe==1&&mock.read_failures_remaining){--mock.read_failures_remaining;return kIOReturnNoResources;}
    if(*failure){int32_t result=*failure;if(pipe==1 ? !mock.persistent_read_failure : !mock.persistent_write_failure)*failure=0;return result;}
    if(frame < mock.bus_frame)return kIOReturnIsoTooOld;
    for(size_t i=0;i<count;++i){
        MockEntry *entry=list->entries+first+i;
        assert(entry->offset < buffer->size);
        assert(entry->wrap || (size_t)entry->offset+entry->frame.requested <= buffer->size);
        assert(entry->frame.status == RK3_HOST_ISO_PENDING);
    }
    MockTransfer *transfer=pipe == 1 ? find_transfer(mock.reads,64,&mock.read_count,list,first) :
        find_transfer(mock.writes,128,&mock.write_count,list,first);
    assert(!transfer->pending);
    *transfer=(MockTransfer){buffer,list,first,count,frame,completion,context,true,false,++mock.accepted[pipe]};
    if(pipe == 2)mock.last_write=transfer;
    return 0;
}
size_t rk3_host_pending(const RK3HostInterface *interface) {
    assert(interface == &mock.interface);size_t pending=0;
    for(unsigned i=0;i<mock.read_count;++i)pending+=mock.reads[i].pending;
    for(unsigned i=0;i<mock.write_count;++i)pending+=mock.writes[i].pending;
    return pending;
}
static void deliver(MockTransfer *transfer, int32_t status) {
    assert(transfer->pending);transfer->pending=false;
    transfer->completion(transfer->context,status,transfer->list,transfer->first,transfer->count);
}
int32_t rk3_host_abort(RK3HostInterface *interface, uint8_t pipe) {
    assert(interface == &mock.interface && (pipe == 1 || pipe == 2));++mock.aborts[pipe];
    MockTransfer *transfers=pipe == 1 ? mock.reads : mock.writes;
    unsigned count=pipe == 1 ? mock.read_count : mock.write_count;
    for(unsigned i=0;i<count;++i)if(transfers[i].pending){
        transfers[i].aborted=true;
        if(!mock.defer_abort)deliver(transfers+i,kIOReturnAborted);
    }
    return 0;
}
static void drain_aborted(void) {
    for(unsigned i=0;i<mock.read_count;++i)if(mock.reads[i].pending && mock.reads[i].aborted)deliver(mock.reads+i,kIOReturnAborted);
    for(unsigned i=0;i<mock.write_count;++i)if(mock.writes[i].pending && mock.writes[i].aborted)deliver(mock.writes+i,kIOReturnAborted);
}
static bool command(void *context, const uint8_t *bytes, size_t length, uint32_t timeout) {
    (void)context;assert(length == 6 && bytes[0] == 9 && timeout == 1200);
    if(clock_start_observer) {
        rk3_native_audio_clock_schedule schedule={99,99};
        assert(!rk3_native_audio_iso_clock_schedule(clock_start_observer,&schedule));
        assert(schedule.incarnation==99&&schedule.deadline==99);++clock_start_observations;
    }
    if(mock.commands < 2)memcpy(mock.commands_bytes[mock.commands],bytes,6);
    ++mock.commands;
    if(bytes[1]!=255) {
        mock.reads_at_format=mock.read_submissions;
        if(mock.format_failures_remaining){--mock.format_failures_remaining;return false;}
    }
    return !mock.fail_command&&!(mock.fail_stop&&bytes[1]==255);
}
static rk3_native_audio_iso *new_engine_rate(uint32_t rate) {
    memset(&mock,0,sizeof(mock));mock.interface.references=1;mock.bus_frame=1000;
    mock.anchor_host=mach_absolute_time();mock.primary_supported=true;
    atomic_store(&block_snapshot,false);atomic_store(&snapshot_entered,false);atomic_store(&release_snapshot,false);
    rk3_native_audio_iso *engine=rk3_native_audio_iso_create_with_clock(&mock.interface,1,2,command,NULL,&clock_provider);
    assert(engine && rk3_native_audio_iso_is_quiescent(engine));
    if(rate)assert(rk3_native_audio_iso_set_sample_rate(engine,rate)==0);
    return engine;
}
/* Keep all existing 48k witnesses on their original rate explicitly. */
static rk3_native_audio_iso *new_engine(void) {return new_engine_rate(48000);}
static void destroy_engine(rk3_native_audio_iso *);
static void test_clock_start_order_and_idle_cadence(void) {
    rk3_native_audio_iso *engine=new_engine();destroy_engine(engine);
    engine=rk3_native_audio_iso_create_with_clock(&mock.interface,1,2,command,NULL,&clock_provider);
    assert(engine&&rk3_native_audio_iso_set_sample_rate(engine,48000)==0);mock.commands=0;
    mock.poison_clock_pair=true;
    const uint64_t cold[]={1000,1000,1000,1001,1001,1002};
    memcpy(mock.clock_script,cold,sizeof(cold));mock.clock_script_count=6;
    assert(rk3_native_audio_iso_start(engine)==0);
    assert(mock.stream_begins==1&&mock.clock_begins==1&&mock.clock_ends==1);
    assert(mock.stream_epoch==1&&mock.stream_read_submissions==0);
    assert(mock.clock_end_tick==host_for_frame(1002));
    /* Original performStart0x6433 invokes the first maintenance callback only
     * after64 queued inputs and accepted real parameters. The cold callback
     * has both a bootstrap and the counter8->9 boundary probe. */
    assert(mock.reads_at_format==64&&mock.clock_queries_before_format==0);
    assert(mock.clock_queries==6&&mock.clock_script_index==6);
    for(unsigned tick=1;tick<=16;++tick) {
        mock.bus_frame+=128;unsigned before=mock.clock_queries;
        uint64_t frame=mock.bus_frame;
        const uint64_t ordinary[]={frame,frame,frame,frame+1};
        memcpy(mock.clock_script,ordinary,sizeof(ordinary));mock.clock_script_index=0;
        mock.clock_script_count=tick%8==0?4:2;
        rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);
        assert(mock.clock_queries-before==mock.clock_script_count);
        assert(mock.clock_script_index==mock.clock_script_count);
        assert(mock.clock_begins==tick+1&&mock.clock_ends==tick+1&&mock.clock_end_tick==host_for_frame(mock.bus_frame));
        unsigned marks=mock.clock_begins;rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);
        assert(mock.clock_begins==marks&&mock.clock_ends==marks&&mock.clock_queries-before==mock.clock_script_count);
    }
    /* Ordinary same-engine stop/restart keeps the estimator and counter.
     * Three more callbacks leave counter3; restart reaches4 with NO boundary.
     * The fourth later callback reaches8, rather than a newly reset phase. */
    for(unsigned tick=0;tick<3;++tick) {
        mock.bus_frame+=128;uint64_t frame=mock.bus_frame;
        mock.clock_script[0]=mock.clock_script[1]=frame;mock.clock_script_count=2;mock.clock_script_index=0;
        rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);assert(mock.clock_script_index==2);
    }
    rk3_native_audio_iso_stop(engine);drain_aborted();unsigned stopped_queries=mock.clock_queries;
    mock.bus_frame+=128;rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);assert(mock.clock_queries==stopped_queries);
    mock.clock_script[0]=mock.clock_script[1]=mock.bus_frame;mock.clock_script_index=0;
    unsigned marks=mock.clock_begins;
    assert(rk3_native_audio_iso_start(engine)==0&&mock.clock_queries==stopped_queries+2);
    assert(mock.stream_begins==2&&mock.stream_epoch==2&&mock.clock_begins==marks+1&&mock.clock_ends==marks+1);
    for(unsigned tick=1;tick<=4;++tick) {
        mock.bus_frame+=128;uint64_t frame=mock.bus_frame;
        const uint64_t observations[]={frame,frame,frame,frame+1};
        memcpy(mock.clock_script,observations,sizeof(observations));mock.clock_script_index=0;
        mock.clock_script_count=tick==4?4:2;
        rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);assert(mock.clock_script_index==mock.clock_script_count);
    }
    destroy_engine(engine);
}
static void destroy_engine(rk3_native_audio_iso *engine) {
    rk3_native_audio_iso_stop(engine);drain_aborted();
    assert(rk3_native_audio_iso_is_quiescent(engine));
    assert(rk3_native_audio_iso_release_usb(engine));
    assert(mock.buffers == mock.buffer_releases && mock.lists == mock.list_releases);
    assert(rk3_native_audio_iso_destroy(engine));assert(mock.retains == mock.releases);
}
static uint8_t *entry_bytes(MockTransfer *transfer, size_t microframe) {
    MockEntry *entry=transfer->list->entries+transfer->first+microframe;
    assert(entry->offset < transfer->buffer->size);return transfer->buffer->bytes+entry->offset;
}
static void hardware_input(unsigned slot, const uint16_t counts[8], bool corrupt) {
    MockTransfer *transfer=mock.reads+slot;assert(transfer->pending && transfer->count == 8);
    const uint8_t period[8]={1,0x40,0,0,0,0x40,0,0};
    for(size_t i=0;i<8;++i){
        MockEntry *entry=transfer->list->entries+transfer->first+i;
        assert(counts[i] <= entry->frame.requested && counts[i]%8 == 0);
        uint8_t *bytes=entry_bytes(transfer,i);
        for(unsigned offset=0;offset<counts[i];offset+=8)memcpy(bytes+offset,period,8);
        entry->frame.actual=counts[i];entry->frame.host_time=host_for_frame(transfer->scheduled_frame)+ticks_for_ns(i*125000);
        entry->frame.status=0;
    }
    if(corrupt)entry_bytes(transfer,0)[4]=7;
}
static void input_primary(unsigned slot, bool corrupt) {
    const uint16_t counts[8]={48,48,48,48,48,48,48,48};hardware_input(slot,counts,corrupt);
}
static void input_callback(unsigned slot, unsigned lag_ms) {
    MockTransfer *transfer=mock.reads+slot;
    uint64_t now=transfer->scheduled_frame+1+lag_ms;if(now > mock.bus_frame)mock.bus_frame=now;
    deliver(transfer,0);
}
static void output_callback_with_short(unsigned slot, int short_microframe) {
    MockTransfer *transfer=mock.writes+slot;assert(transfer->pending);
    uint64_t now=transfer->scheduled_frame+1;if(now > mock.bus_frame)mock.bus_frame=now;
    for(size_t i=0;i<transfer->count;++i){
        MockEntry *entry=transfer->list->entries+transfer->first+i;
        entry->frame.actual=entry->frame.requested;
        if((int)i==short_microframe&&entry->frame.actual) --entry->frame.actual;
        entry->frame.host_time=host_for_frame(transfer->scheduled_frame)+ticks_for_ns(i*125000);
        entry->frame.status=0;
    }
    deliver(transfer,0);
}
static void output_callback(unsigned slot) {output_callback_with_short(slot,-1);}
static void prime(rk3_native_audio_iso *engine) {
    assert(rk3_native_audio_iso_start(engine) == 0);
    assert(mock.read_count == 64 && rk3_native_audio_iso_pending(engine) == 64);
    assert(mock.reads[0].scheduled_frame == 1101 && mock.reads[63].scheduled_frame == 1164);
    for(unsigned slot=0;slot<64;++slot){
        input_primary(slot,false);input_callback(slot,0);
        /* Model the independent worker timer, never the IN callback itself. */
        rk3_native_audio_iso_clock_service(engine);
        assert(mock.last_write && mock.last_write->scheduled_frame == 1165+slot);
    }
    assert(mock.write_count == 64);
    /* First non-priming data becomes visible to the host before IN callback. */
    input_primary(0,false);
    float initial[2];assert(rk3_native_audio_iso_capture_at(engine,initial,0,1)==1);
    rk3_native_audio_iso_service(engine); /* worker publishes the exact initial zero event */
    output_callback(0);
}
/* Source-derived transition order, tested at the actual enqueue call site.
 * Errors here occur AFTER a valid boundary plan; neither USB return controls
 * NI's postphase. RAW reverse delivery remains tested by the Host contract. */
static void test_original_priming_transition_order(void) {
    for(unsigned variant=0;variant<7;++variant) {
        rk3_native_audio_iso *e=new_engine();assert(rk3_native_audio_iso_start(e)==0);
        for(unsigned slot=0;slot<63;++slot){input_primary(slot,false);input_callback(slot,0);}
        input_primary(0,false); /* Fresh epoch2; do not lose it with epoch1. */
        input_primary(63,false);
        priming_observer=e;priming_attempts[0]=priming_attempts[1]=0;
        hold_priming_guard=variant>=4;
        if(variant==1){mock.fail_read=kIOReturnNoResources;mock.persistent_read_failure=true;}
        if(variant==2||variant==6)mock.fail_read=kIOReturnIsoTooOld;
        if(variant==3)mock.fail_write=kIOReturnNoResources;
        input_callback(63,0);
        priming_observer=NULL;
        assert(priming_attempts[0]==(variant==1?6:1)&&priming_attempts[1]==1);
        if(hold_priming_guard) {
            assert(e->schedule.priming&&!atomic_load(&e->pcm_ready));
            unsigned reads=mock.read_submissions,writes=mock.write_submissions;
            /* No later ready input may overtake the due transition. */
            if(variant==4)input_callback(0,0);
            if(variant==5)rk3_native_audio_iso_stop(e);
            atomic_flag_clear_explicit(&e->processing,memory_order_release);
            if(variant==5) {
                assert(rk3_native_audio_iso_start(e)==0);
                rk3_native_audio_iso_service(e);
                assert(e->schedule.priming&&!atomic_load(&e->pcm_ready));
                destroy_engine(e);continue;
            }
            rk3_native_audio_iso_service(e);
            assert(mock.read_submissions==reads+(variant==4)&&mock.write_submissions==writes+(variant==4));
        }
        assert(!e->schedule.priming&&!e->decoder.priming&&!e->output_ring.priming);
        assert(atomic_load(&e->pcm_ready)&&e->initial_timestamp_queued);
        assert(e->decoder.completed_frames==48); /* The original postscan ran. */
        if(variant!=4)assert(e->schedule.next_output_slot==0);
        if(variant==2||variant==6){uint32_t delay;int32_t error;
            assert(rk3_native_audio_iso_panic(e,&delay,&error)&&delay==100&&error==kIOReturnIsoTooOld);}
        if(variant==3)assert(e->schedule.output_busy[63]);
        float pcm[2];assert(rk3_native_audio_iso_capture_at(e,pcm,0,1)==1&&pcm[1]==0.5f);
        const float render[2]={0.5f,0.5f};
        assert(rk3_native_audio_iso_render_at(e,render,128,1)==1);
        assert(((uint8_t *)rk3_host_buffer_bytes(e->wire_buffer))[128*8+3]==0x40);
        unsigned reads=mock.read_submissions,writes=mock.write_submissions;
        rk3_native_audio_iso_service(e);
        assert(mock.read_submissions==reads&&mock.write_submissions==writes);
        destroy_engine(e);
    }
    /* No postphase before a boundary has reached the two submission calls. */
    for(unsigned variant=0;variant<5;++variant) {
        rk3_native_audio_iso *e=new_engine();assert(rk3_native_audio_iso_start(e)==0);
        for(unsigned slot=0;slot<63;++slot){input_primary(slot,false);input_callback(slot,0);}
        input_primary(63,false);unsigned reads=mock.read_submissions,writes=mock.write_submissions;
        if(variant==0)deliver(mock.reads+63,kIOReturnAborted);
        if(variant==1){for(unsigned i=0;i<128;++i)e->schedule.output_busy[i]=true;input_callback(63,0);}
        if(variant==2){mock.fail_read=kIOReturnNoDevice;mock.persistent_read_failure=true;input_callback(63,0);}
        if(variant==3){mock.reads[63].list->entries[63*8].frame.actual=513;input_callback(63,0);}
        /* Violated Host-list lifetime is a pre-submit ownership stop, not a
         * recoverable USB rejection and not permission to use busy storage. */
        if(variant==4){fail_priming_prepare=true;input_callback(63,0);}
        assert(e->schedule.priming&&!atomic_load(&e->pcm_ready));
        if(variant!=2)assert(mock.read_submissions==reads&&mock.write_submissions==writes);
        else assert(!rk3_native_audio_iso_running(e)&&mock.write_submissions==writes);
        destroy_engine(e);
    }
}

static void test_lifted_calendar_and_deferred_dma(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);
    const uint8_t stop[6]={9,255,255,136,0,1},start[6]={9,1,2,136,0,1};
    assert(mock.commands == 2 && !memcmp(mock.commands_bytes[0],stop,6) && !memcmp(mock.commands_bytes[1],start,6));
    assert(mock.aborts[1] && mock.aborts[2] && mock.clears[1] && mock.clears[2]);
    RK3HostBuffer *wire=mock.writes[2].buffer;assert(wire->size == NI_AUDIO_RING_WIRE_BYTES);
    for(unsigned i=0;i<64;++i)assert(mock.writes[i].buffer == wire);
    /* OUT2 includes sample128 at wire offset1024; its future descriptor was
     * already queued while silent. Later host rendering must update that view. */
    uint8_t *future=wire->bytes+128*8;
    const uint8_t silent[8]={0,0,1,0,0,0,0,0};assert(!memcmp(future,silent,8));
    MockEntry *queued=mock.writes[2].list->entries+mock.writes[2].first+5;
    assert(mock.writes[2].pending && queued->offset == 1008 && queued->frame.requested == 48);
    assert(entry_bytes(&mock.writes[2],5)+16 == future);
    unsigned submissions=mock.write_submissions;
    float pcm[256];for(unsigned i=0;i<256;++i)pcm[i]=0.5f;
    assert(rk3_native_audio_iso_render_at(engine,pcm,128,128) == 128);
    const uint8_t audible[8]={0,0,1,0x40,0,0,0,0x40};assert(!memcmp(future,audible,8));
    assert(mock.write_submissions == submissions);
    rk3_native_audio_clock clock;assert(rk3_native_audio_iso_clock(engine,&clock));
    uint64_t expected_host=host_for_frame(1166);
    uint64_t clock_error=clock.host_time > expected_host ? clock.host_time-expected_host : expected_host-clock.host_time;
    /* Absolute-time<->nanosecond conversion can round by a host tick; it must
     * not substitute the last microframe timestamp or IRP-end sample index. */
    assert(clock.sample_frame == 0 && clock_error <= 2 && clock.seed);
    uint64_t zero_error=clock.zero_host_time > expected_host ? clock.zero_host_time-expected_host : expected_host-clock.zero_host_time;
    assert(clock.zero_sample_frame==0&&zero_error<=2);
    destroy_engine(engine);
}
static void test_capture_primary_and_repeatable_positions(void) {
    rk3_native_audio_iso *engine=new_engine();assert(rk3_native_audio_iso_start(engine) == 0);
    float pcm[96];for(unsigned i=0;i<96;++i)pcm[i]=0.75f;
    unsigned before=mock.write_submissions;
    assert(rk3_native_audio_iso_render_at(engine,pcm,0,48) == 48);
    assert(mock.write_submissions == before);
    memset(pcm,0xff,sizeof(pcm));assert(rk3_native_audio_iso_capture_at(engine,pcm,0,48) == 48);
    for(unsigned i=0;i<96;++i)assert(pcm[i] == 0);
    for(unsigned slot=0;slot<64;++slot){input_primary(slot,false);input_callback(slot,0);}
    output_callback(0);
    input_primary(0,false);
    /* NI requested updateFrequency0: exposing only microframe0 on a newer
     * per-transaction list must not publish capture with a partial denominator. */
    for(unsigned micro=1;micro<8;++micro){
        MockEntry *entry=mock.reads[0].list->entries+mock.reads[0].first+micro;
        entry->frame.status=RK3_HOST_ISO_PENDING;entry->frame.actual=0;
    }
    assert(rk3_native_audio_iso_capture_at(engine,pcm,0,6)==6);
    for(unsigned i=0;i<12;++i)assert(pcm[i]==0);
    rk3_native_audio_clock unavailable;assert(!rk3_native_audio_iso_clock(engine,&unavailable));
    input_primary(0,false); /* All primary statuses complete, callback withheld. */
    float again[96];
    assert(rk3_native_audio_iso_capture_at(engine,pcm,0,48) == 48);
    assert(pcm[0] == 0 && pcm[1] == 0.5f);
    for(unsigned i=2;i<96;++i)assert(pcm[i] == 0.5f);
    assert(rk3_native_audio_iso_capture_at(engine,again,0,48) == 48);
    assert(!memcmp(pcm,again,sizeof(pcm))); /* Reading does not dequeue or trim. */
    assert(rk3_native_audio_iso_capture_at(engine,again,16,16) == 16);
    for(unsigned i=0;i<32;++i)assert(again[i] == 0.5f);
    destroy_engine(engine);
}
static void test_delayed_callbacks_against_advancing_bus(void) {
    rk3_native_audio_iso *engine=new_engine();assert(rk3_native_audio_iso_start(engine) == 0);
    for(unsigned slot=0;slot<64;++slot){
        uint64_t received=mock.reads[slot].scheduled_frame;
        input_primary(slot,false);input_callback(slot,10);
        assert(mock.last_write && mock.last_write->scheduled_frame == received+64);
        assert(mock.last_write->scheduled_frame > mock.bus_frame);
    }
    uint32_t delay=0;int32_t error=0;
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
    assert(rk3_native_audio_iso_error(engine) == 0);
    destroy_engine(engine);
}
/* NativeHostUSB callback contract: successful requests retire in acceptance
 * order on each pipe, even when raw system completions arrive out of order.
 * Primary-list publication is independent of that ordered callback delivery.
 * This exercises the production ISO engine, not a second scheduling model. */
typedef struct {
    uint64_t acceptance,frame,due;
    bool primary,raw_complete;
} CompletionEvent;
typedef struct {
    unsigned inversions,submissions,pending_reads;
    bool stalled;
} CompletionBindingResult;
static MockTransfer *oldest_pending_input(void) {
    MockTransfer *oldest=NULL;
    for(unsigned i=0;i<mock.read_count;++i)if(mock.reads[i].pending&&
        (!oldest||mock.reads[i].acceptance<oldest->acceptance))oldest=mock.reads+i;
    return oldest;
}
static unsigned pending_inputs(void) {
    unsigned count=0;for(unsigned i=0;i<mock.read_count;++i)count+=mock.reads[i].pending;return count;
}
static void deliver_ordered_inputs(CompletionEvent events[64]) {
    for(;;) {
        MockTransfer *oldest=oldest_pending_input();
        if(!oldest)return;
        CompletionEvent *event=events+(size_t)(oldest-mock.reads);
        if(event->acceptance!=oldest->acceptance||!event->raw_complete)return;
        /* deliver() retires before invoking the engine callback. That callback
         * may enqueue another request for this same reusable input slot. */
        deliver(oldest,0);
    }
}
static CompletionBindingResult simulate_completion_binding(bool ordered) {
    CompletionEvent events[64]={{0}};
    CompletionBindingResult result={0};
    rk3_native_audio_iso *engine=new_engine();assert(rk3_native_audio_iso_start(engine)==0);
    unsigned last_submissions=mock.read_submissions;uint64_t last_progress=1000;
    for(uint64_t now=1000;now<21000;++now) {
        mock.bus_frame=now;
        for(unsigned slot=0;slot<64;++slot)if(mock.reads[slot].pending) {
            MockTransfer *transfer=mock.reads+slot;CompletionEvent *event=events+slot;
            if(event->acceptance!=transfer->acceptance)
                *event=(CompletionEvent){transfer->acceptance,transfer->scheduled_frame,
                    transfer->scheduled_frame+1+(slot==0?1:0),false,false};
            if(!event->primary&&event->frame+1<=now){input_primary(slot,false);event->primary=true;}
        }
        for(unsigned slot=0;slot<mock.write_count;++slot)
            if(mock.writes[slot].pending&&mock.writes[slot].scheduled_frame+1<=now)output_callback(slot);
        /* Coalesced raw arrivals deliberately visit slot1 before slot0 when
         * both are due. Slot0 adds only1ms callback delay; every primary batch
         * still publishes exactly at its scheduled USB frame+1ms. */
        for(unsigned reverse=0;reverse<64;++reverse) {
            unsigned slot=63-reverse;MockTransfer *transfer=mock.reads+slot;
            CompletionEvent *event=events+slot;
            if(!transfer->pending||event->acceptance!=transfer->acceptance||
                !event->primary||event->raw_complete||event->due>now)continue;
            assert(now-event->frame-1<=1);
            MockTransfer *oldest=oldest_pending_input();assert(oldest);
            if(oldest->acceptance<transfer->acceptance&&!events[(size_t)(oldest-mock.reads)].raw_complete)
                ++result.inversions;
            event->raw_complete=true;
            if(!ordered)deliver(transfer,0);
        }
        if(ordered)deliver_ordered_inputs(events);
        rk3_native_audio_iso_service(engine);
        if(now%3==0) {
            float samples[256];
            assert(rk3_native_audio_iso_capture_at(engine,samples,(now-1000)*48,128)==128);
            rk3_native_audio_iso_service(engine);
        }
        uint32_t delay=0;int32_t error=0;
        assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
        assert(rk3_native_audio_iso_stream_error(engine)==0&&rk3_native_audio_iso_error(engine)==0);
        if(mock.read_submissions!=last_submissions){last_submissions=mock.read_submissions;last_progress=now;}
        if(ordered)assert(pending_inputs()==64);
        if(now>1200&&now-last_progress>=128){result.stalled=true;break;}
    }
    result.submissions=mock.read_submissions;result.pending_reads=pending_inputs();
    if(ordered) {
        rk3_native_audio_clock clock;assert(rk3_native_audio_iso_clock(engine,&clock));
        assert(clock.sample_frame>UINT64_C(900000));
    }
    destroy_engine(engine);return result;
}
static void test_completion_acceptance_order_binding(void) {
    /* Negative control proves this fixture can expose the original callback-
     * arrival FIFO/consumed-epoch cycle. Unordered delivery violates the Host
     * contract; it is not an alternative supported binding for this engine.
     * The valid binding must prevent that failure without changing NI timing. */
    CompletionBindingResult unordered=simulate_completion_binding(false);
    assert(unordered.inversions>0&&unordered.stalled&&unordered.pending_reads==0);
    CompletionBindingResult ordered=simulate_completion_binding(true);
    assert(ordered.inversions>100&&!ordered.stalled&&ordered.pending_reads==64);
    assert(ordered.submissions>19500);
}
static void complete_due_outputs(uint64_t input_frame) {
    for(unsigned i=0;i<mock.write_count;++i)if(mock.writes[i].pending && mock.writes[i].scheduled_frame <= input_frame)output_callback(i);
}
static void test_microframe_ring_wrap(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);
    bool found=false;
    for(unsigned step=0;step<400 && !found;++step){
        unsigned slot=step%64;uint64_t frame=mock.reads[slot].scheduled_frame;
        complete_due_outputs(frame);input_primary(slot,false);input_callback(slot,0);
        MockTransfer *out=mock.last_write;assert(out);
        for(size_t i=0;i<out->count;++i){
            MockEntry *entry=out->list->entries+out->first+i;
            if((size_t)entry->offset+entry->frame.requested > out->buffer->size){
                assert(entry->wrap && entry->frame.requested == 48);
                assert(entry->offset == NI_AUDIO_RING_WIRE_BYTES-32);
                found=true;
            }
        }
    }
    assert(found);destroy_engine(engine);
}
static void test_empty_output_request_lifecycle(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);
    rk3_native_audio_clock before,after;assert(rk3_native_audio_iso_clock(engine,&before));
    const uint16_t zero[8]={0};hardware_input(0,zero,false);
    unsigned submissions=mock.write_submissions;size_t pending=rk3_native_audio_iso_pending(engine);
    input_callback(0,0);
    assert(mock.write_submissions==submissions+1&&rk3_native_audio_iso_pending(engine)==pending+1);
    MockTransfer *empty=mock.last_write;
    assert(empty&&empty->pending&&empty->count==8&&empty->buffer->size==NI_AUDIO_RING_WIRE_BYTES);
    assert(empty->scheduled_frame==1229&&empty->first==0);
    for(size_t micro=0;micro<8;++micro) {
        MockEntry *entry=empty->list->entries+empty->first+micro;
        assert(entry->frame.requested==0&&entry->offset==3072*8&&!entry->wrap);
    }
    assert(rk3_native_audio_iso_clock(engine,&after));
    assert(after.sample_frame==before.sample_frame&&after.host_time==before.host_time);
    output_callback(0); /* zero payload still owns its slot until completion */
    assert(!empty->pending&&rk3_native_audio_iso_pending(engine)==pending);
    assert(rk3_native_audio_iso_clock(engine,&after));
    uint64_t expected=host_for_frame(empty->scheduled_frame+1);
    uint64_t error=after.host_time>expected?after.host_time-expected:expected-after.host_time;
    assert(after.sample_frame==3072&&error<=2);
    assert(after.zero_sample_frame==before.zero_sample_frame&&after.zero_host_time==before.zero_host_time);
    destroy_engine(engine);
    /* Repeated empty input preserves NI's OUT slot ownership/exhaustion too;
     * zero-count requests may not be silently treated as completed locally. */
    engine=new_engine();assert(rk3_native_audio_iso_start(engine)==0);
    for(unsigned step=0;step<128;++step) {
        unsigned slot=step%64;hardware_input(slot,zero,false);input_callback(slot,0);
        assert(mock.last_write&&mock.last_write->pending&&mock.last_write->count==8);
    }
    assert(mock.write_submissions==128&&rk3_native_audio_iso_pending(engine)==192);
    uint32_t delay=0;int32_t reason=0;
    assert(!rk3_native_audio_iso_panic(engine,&delay,&reason));
    hardware_input(0,zero,false);input_callback(0,0);
    assert(rk3_native_audio_iso_panic(engine,&delay,&reason)&&delay==100&&reason==kIOReturnNoResources);
    destroy_engine(engine);
}
static void test_panic_classification(void) {
    uint32_t delay=0;int32_t error=0;
    rk3_native_audio_iso *engine=new_engine();assert(rk3_native_audio_iso_start(engine) == 0);
    mock.fail_read=kIOReturnIsoTooOld;input_primary(0,false);input_callback(0,0);
    assert(rk3_native_audio_iso_panic(engine,&delay,&error) && delay == 100 && error == kIOReturnIsoTooOld);
    assert(rk3_native_audio_iso_panic(engine,&delay,&error) && delay == 100 && error == kIOReturnIsoTooOld);
    rk3_native_audio_iso_stop(engine);drain_aborted();
    assert(rk3_native_audio_iso_start(engine) == 0);
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
    destroy_engine(engine);
    engine=new_engine();prime(engine);
    input_primary(1,true);float corrupt_capture[96];
    assert(rk3_native_audio_iso_capture_at(engine,corrupt_capture,0,48) == 48);
    assert(rk3_native_audio_iso_panic(engine,&delay,&error) && delay == 0);
    destroy_engine(engine);
    engine=new_engine();assert(rk3_native_audio_iso_start(engine) == 0);
    mock.fail_write=kIOReturnIsoTooOld;mock.persistent_write_failure=true;
    for(unsigned step=0;step<128;++step){
        input_primary(step%64,false);input_callback(step%64,0);
        if(step==0){
            assert(rk3_native_audio_iso_stream_error(engine)==kIOReturnIsoTooOld);
            assert(rk3_native_audio_iso_error(engine)==0&&!rk3_native_audio_iso_panic(engine,&delay,&error));
        }
    }
    assert(mock.write_submissions == 128);
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
    input_primary(0,false);input_callback(0,0);
    assert(rk3_native_audio_iso_panic(engine,&delay,&error) && delay == 100);
    destroy_engine(engine);
    engine=new_engine();assert(rk3_native_audio_iso_start(engine) == 0);
    mock.fail_write=kIOReturnNoDevice;input_primary(0,false);input_callback(0,0);
    assert(rk3_native_audio_iso_error(engine) == kIOReturnNoDevice);
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
    destroy_engine(engine);
}
static void test_input_retry_exhaustion_no_invented_panic(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);
    rk3_native_audio_clock before,after;assert(rk3_native_audio_iso_clock(engine,&before));
    unsigned reads=mock.read_submissions,writes=mock.write_submissions;
    mock.fail_read=kIOReturnNoResources;mock.persistent_read_failure=true;
    input_primary(0,false);input_callback(0,0);
    assert(mock.read_submissions==reads+6&&!mock.reads[0].pending);
    assert(mock.write_submissions==writes+1); /* readHandler still calls queueOut */
    uint32_t delay=0;int32_t error=0;
    assert(rk3_native_audio_iso_running(engine)&&rk3_native_audio_iso_error(engine)==0);
    assert(rk3_native_audio_iso_stream_error(engine)==kIOReturnNoResources);
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
    mock.fail_read=0;mock.persistent_read_failure=false;
    input_primary(1,false);input_callback(1,0);
    assert(mock.reads[1].pending&&mock.read_submissions==reads+7);
    output_callback(0);assert(rk3_native_audio_iso_clock(engine,&after)&&after.seed==before.seed);
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
    destroy_engine(engine);
}
static void test_initial_start_nonfatal_failures(void) {
    rk3_native_audio_clock clock;uint32_t delay=0;int32_t error=0;
    rk3_native_audio_iso *engine=new_engine();mock.fail_stop=true;
    assert(rk3_native_audio_iso_start(engine)==0);
    assert(mock.commands==2&&mock.read_submissions==64&&mock.reads_at_format==64);
    assert(!rk3_native_audio_iso_clock(engine,&clock)); /* real clock gate is still required */
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));destroy_engine(engine);
    /* One rejected initial slot is logged, then the remaining63 are submitted
     * before real audio parameters. Each slot has the original6-attempt budget. */
    engine=new_engine();mock.read_failures_remaining=6;
    assert(rk3_native_audio_iso_start(engine)==0);
    assert(mock.read_submissions==69&&mock.read_count==63&&mock.reads_at_format==69&&mock.commands==2);
    assert(rk3_native_audio_iso_stream_error(engine)==kIOReturnNoResources);
    assert(rk3_native_audio_iso_error(engine)==0&&!rk3_native_audio_iso_panic(engine,&delay,&error));
    assert(!rk3_native_audio_iso_clock(engine,&clock));destroy_engine(engine);
    /* All64 may reject without bypassing the real-format call. Success from
     * that command alone never fabricates a completed-device clock. */
    engine=new_engine();mock.fail_read=kIOReturnNoResources;mock.persistent_read_failure=true;
    assert(rk3_native_audio_iso_start(engine)==0);
    assert(mock.read_submissions==64*6&&mock.read_count==0&&mock.reads_at_format==64*6&&mock.commands==2);
    assert(rk3_native_audio_iso_pending(engine)==0&&rk3_native_audio_iso_stream_error(engine)==kIOReturnNoResources);
    assert(!rk3_native_audio_iso_clock(engine,&clock)&&!rk3_native_audio_iso_panic(engine,&delay,&error));
    destroy_engine(engine);
    /* Original queueIn's restart guard precedes its calendar increment: once
     * TooOld sets panic, later initial calls cannot enqueue more USB reads. */
    engine=new_engine();mock.fail_read=kIOReturnIsoTooOld;mock.persistent_read_failure=true;
    assert(rk3_native_audio_iso_start(engine)==0);
    assert(mock.read_submissions==1&&mock.commands==2&&mock.reads_at_format==1);
    assert(rk3_native_audio_iso_panic(engine,&delay,&error)&&delay==100&&error==kIOReturnIsoTooOld);
    assert(!rk3_native_audio_iso_clock(engine,&clock));destroy_engine(engine);
    engine=new_engine();mock.fail_read=kIOReturnNoDevice;mock.persistent_read_failure=true;
    assert(rk3_native_audio_iso_start(engine)==kIOReturnNoDevice);
    assert(mock.read_submissions==6&&mock.commands==1&&rk3_native_audio_iso_error(engine)==kIOReturnNoDevice);
    assert(!rk3_native_audio_iso_running(engine)&&!rk3_native_audio_iso_clock(engine,&clock));destroy_engine(engine);
}
static void test_real_format_retries_preserve_panic_policy(void) {
    rk3_native_audio_clock clock;uint32_t delay=0;int32_t error=0;
    rk3_native_audio_iso *engine=new_engine();
    mock.fail_read=kIOReturnIsoTooOld;mock.persistent_read_failure=true;mock.format_failures_remaining=1;
    assert(rk3_native_audio_iso_start(engine)==0);
    assert(mock.commands==3&&mock.format_failures_remaining==0&&mock.read_submissions==1);
    assert(rk3_native_audio_iso_panic(engine,&delay,&error)&&delay==100&&error==kIOReturnIsoTooOld);
    assert(!rk3_native_audio_iso_clock(engine,&clock));destroy_engine(engine);
    engine=new_engine();mock.format_failures_remaining=5;
    assert(rk3_native_audio_iso_start(engine)==kIOReturnIOError);
    assert(mock.commands==6&&mock.format_failures_remaining==0&&mock.read_submissions==64);
    assert(rk3_native_audio_iso_error(engine)==0&&!rk3_native_audio_iso_panic(engine,&delay,&error));
    assert(!rk3_native_audio_iso_clock(engine,&clock));destroy_engine(engine);
}
static void test_priming_written_inactive_clear(void) {
    rk3_native_audio_iso *engine=new_engine();assert(rk3_native_audio_iso_start(engine)==0);
    const float pcm[2]={0.5f,0.25f};
    assert(rk3_native_audio_iso_render_at(engine,pcm,17,1)==1); /* Marks written while priming. */
    for(unsigned slot=0;slot<64;++slot){input_primary(slot,false);input_callback(slot,0);}
    uint8_t *wire=mock.writes[0].buffer->bytes;
    for(unsigned lane=0;lane<8;++lane)wire[17*8+lane]=(uint8_t)(0xa0+lane);
    rk3_native_audio_iso_set_output_active(engine,false);rk3_native_audio_iso_service(engine);
    const uint8_t cleared[8]={0,0,0,0,0,0,0xa6,0xa7};
    assert(!memcmp(wire+17*8,cleared,8)&&wire[2]==0);
    /* No post-priming render preceded the clear. Reactivation must accept
     * addressed data again while preserving NI's original check-lane quirk. */
    assert(rk3_native_audio_iso_render_at(engine,pcm,17,1)==1);
    assert(!memcmp(wire+17*8,cleared,8));
    rk3_native_audio_iso_set_output_active(engine,true);
    assert(rk3_native_audio_iso_render_at(engine,pcm,17,1)==1);
    const uint8_t reactivated[8]={0,0,0,0x40,0,0,0xa6,0x20};
    assert(!memcmp(wire+17*8,reactivated,8));destroy_engine(engine);
}
static void test_partial_output_diagnostic(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);
    output_callback_with_short(1,3);
    assert(rk3_native_audio_iso_stream_error(engine)==kIOReturnUnderrun);
    uint32_t delay=0;int32_t error=0;
    assert(rk3_native_audio_iso_error(engine)==0&&!rk3_native_audio_iso_panic(engine,&delay,&error));
    rk3_native_audio_clock clock;assert(rk3_native_audio_iso_clock(engine,&clock)&&clock.sample_frame==48);
    destroy_engine(engine);
}
static void test_exact_variable_count_zero_event(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);
    rk3_native_audio_clock initial;assert(rk3_native_audio_iso_clock(engine,&initial));
    const uint16_t variable[8]={40,40,40,40,48,48,48,48}; /* 44 sample frames,352 wire bytes. */
    uint64_t wrap_usb_frame=0;
    for(unsigned step=0;step<=341;++step){
        unsigned slot=step%64;uint64_t frame=mock.reads[slot].scheduled_frame;
        complete_due_outputs(frame);
        if(step==341){hardware_input(slot,variable,false);wrap_usb_frame=frame;}
        else input_primary(slot,false);
        float sample[2];assert(rk3_native_audio_iso_capture_at(engine,sample,0,1)==1);
        rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);input_callback(slot,0);
    }
    /* Cursor starts2. The first wrap consumes131069 wire bytes, so after
     *341 ordinary384-byte IRPs the event offset is125 in the variable IRP:
     * micro3 prefix120 +5 consumed bytes. Timestamp uses total352, not48kHz. */
    assert(wrap_usb_frame==1506);
    rk3_native_audio_clock clock;assert(rk3_native_audio_iso_clock(engine,&clock));
    uint64_t expected=host_for_frame(wrap_usb_frame+1)+ticks_for_ns(UINT64_C(1000000)*125/352);
    uint64_t difference=clock.zero_host_time>expected?clock.zero_host_time-expected:expected-clock.zero_host_time;
    assert(clock.zero_sample_frame==NI_AUDIO_RING_FRAME_COUNT&&difference<=2);
    uint64_t nominal=initial.zero_host_time+ticks_for_ns(UINT64_C(1000000000)*NI_AUDIO_RING_FRAME_COUNT/48000);
    difference=clock.zero_host_time>nominal?clock.zero_host_time-nominal:nominal-clock.zero_host_time;
    assert(difference>ticks_for_ns(10000));destroy_engine(engine);
}
static void test_backward_output_host_time_guard(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);
    rk3_native_audio_clock before,after;assert(rk3_native_audio_iso_clock(engine,&before));
    /* A malformed post-boundary local uptime can be earlier even though the
     * deadline sample and USB frame advanced. Original ba80 checks the BEFORE
     * sample against its deadline. Preserve the public monotonic guard without
     * replacing NI's local-time contract with a paired framework timestamp. */
    mock.clock_after_boundary_shift=ticks_for_ns(500000000);
    for(unsigned tick=0;tick<7;++tick) {
        mock.bus_frame+=128;uint64_t f=mock.bus_frame;
        const uint64_t observations[]={f,f,f,f+1};
        memcpy(mock.clock_script,observations,sizeof(observations));
        mock.clock_script_count=4;mock.clock_script_index=0;
        rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);
    }
    assert(mock.clock_shift_applied>0);
    output_callback(1);assert(rk3_native_audio_iso_clock(engine,&after));
    assert(after.sample_frame==before.sample_frame&&after.host_time==before.host_time);
    assert(after.zero_sample_frame==before.zero_sample_frame&&after.zero_host_time==before.zero_host_time);
    destroy_engine(engine);
}

static void test_clock_source_failure_and_queued_zero(void) {
    rk3_native_audio_iso *engine=new_engine();
    mock.clock_failure=kIOReturnNoResources;mock.persistent_clock_failure=true;
    assert(rk3_native_audio_iso_start(engine)==0);
    for(unsigned slot=0;slot<64;++slot){input_primary(slot,false);input_callback(slot,0);}
    input_primary(0,false);float sample[2];unsigned before_capture=mock.clock_queries;
    assert(rk3_native_audio_iso_capture_at(engine,sample,0,1)==1);
    assert(mock.clock_queries==before_capture); /* RT never invokes the provider. */
    rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);output_callback(0);
    rk3_native_audio_clock clock;uint32_t delay=0;int32_t error=0;
    assert(!rk3_native_audio_iso_clock(engine,&clock));
    assert(rk3_native_audio_iso_running(engine)&&rk3_native_audio_iso_error(engine)==0);
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
    /* The initial ring event was generated while no anchor existed. A later
     * successful maintenance callback must publish THAT event (frame1165),
     * rather than dropping it or inventing a new zero at the recovery time. */
    mock.clock_failure=0;mock.persistent_clock_failure=false;mock.bus_frame+=128;
    rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);output_callback(1);
    assert(rk3_native_audio_iso_clock(engine,&clock));
    uint64_t expected=host_for_frame(1166);
    uint64_t difference=clock.zero_host_time>expected?clock.zero_host_time-expected:expected-clock.zero_host_time;
    assert(clock.zero_sample_frame==0&&clock.sample_frame==48&&difference<=2);
    destroy_engine(engine);

    engine=new_engine();prime(engine);rk3_native_audio_clock prior;
    assert(rk3_native_audio_iso_clock(engine,&prior));
    mock.clock_failure=kIOReturnNoResources;mock.persistent_clock_failure=true;mock.bus_frame+=128;
    rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);
    assert(rk3_native_audio_iso_clock(engine,&clock)&&clock.seed==prior.seed&&clock.host_time==prior.host_time);
    assert(rk3_native_audio_iso_running(engine)&&rk3_native_audio_iso_error(engine)==0);
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));
    output_callback(1);assert(rk3_native_audio_iso_clock(engine,&clock)&&clock.sample_frame==48);
    expected=host_for_frame(1167);difference=clock.host_time>expected?clock.host_time-expected:expected-clock.host_time;
    assert(difference<=2);destroy_engine(engine);

    const int32_t removed[]={kIOReturnNoDevice,kIOReturnNotOpen};
    for(unsigned i=0;i<2;++i) {
        engine=new_engine();prime(engine);mock.clock_failure=removed[i];mock.bus_frame+=128;
        unsigned marks=mock.clock_begins;uint64_t before_mark=mach_absolute_time();
        rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);uint64_t after_mark=mach_absolute_time();
        assert(mock.clock_begins==marks+1&&mock.clock_ends==marks+1&&mock.clock_end_tick>=before_mark&&mock.clock_end_tick<=after_mark);
        assert(rk3_native_audio_iso_error(engine)==removed[i]&&!rk3_native_audio_iso_running(engine));
        assert(!rk3_native_audio_iso_clock(engine,&clock)&&!rk3_native_audio_iso_panic(engine,&delay,&error));
        unsigned queries=mock.clock_queries;rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);assert(mock.clock_queries==queries);
        destroy_engine(engine);
    }
    /* Prime's one elapsed maintenance tick leaves counter1. Six more active
     * ticks reach7. At the threshold, initial transient followed by removal
     * must return the fatal error to ISO, without another provider query. */
    engine=new_engine();prime(engine);
    for(unsigned tick=0;tick<6;++tick) {
        mock.bus_frame+=128;uint64_t f=mock.bus_frame;
        mock.clock_script[0]=mock.clock_script[1]=f;mock.clock_script_count=2;mock.clock_script_index=0;
        rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);assert(mock.clock_script_index==2);
    }
    mock.clock_error_script[0]=kIOReturnNoResources;mock.clock_error_script[1]=kIOReturnNoDevice;
    mock.clock_error_count=2;mock.bus_frame+=128;unsigned queries=mock.clock_queries;
    rk3_native_audio_iso_clock_service(engine);rk3_native_audio_iso_service(engine);
    assert(mock.clock_queries==queries+2&&rk3_native_audio_iso_error(engine)==kIOReturnNoDevice);
    assert(!rk3_native_audio_iso_running(engine)&&!rk3_native_audio_iso_clock(engine,&clock));
    assert(!rk3_native_audio_iso_panic(engine,&delay,&error));destroy_engine(engine);
}

static void test_delayed_abort_storage(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);mock.defer_abort=true;
    rk3_native_audio_iso_stop(engine);
    assert(rk3_native_audio_iso_pending(engine) > 0);
    assert(!rk3_native_audio_iso_release_usb(engine));assert(!rk3_native_audio_iso_destroy(engine));
    assert(mock.buffer_releases == 0 && mock.list_releases == 0);
    drain_aborted();assert(rk3_native_audio_iso_is_quiescent(engine));destroy_engine(engine);
}
typedef struct {rk3_native_audio_iso *engine;size_t frames;} CaptureCall;
static void *blocked_capture(void *context) {
    CaptureCall *call=context;float pcm[96];call->frames=rk3_native_audio_iso_capture_at(call->engine,pcm,0,48);return NULL;
}
static void test_live_rt_storage(void) {
    rk3_native_audio_iso *engine=new_engine();prime(engine);
    atomic_store(&block_snapshot,true);CaptureCall call={engine,0};pthread_t thread;
    assert(pthread_create(&thread,NULL,blocked_capture,&call) == 0);
    for(unsigned i=0;i<1000 && !atomic_load(&snapshot_entered);++i){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
    assert(atomic_load(&snapshot_entered));
    rk3_native_audio_iso_stop(engine);assert(!rk3_native_audio_iso_is_quiescent(engine));
    assert(!rk3_native_audio_iso_release_usb(engine) && !rk3_native_audio_iso_destroy(engine));
    atomic_store(&release_snapshot,true);assert(pthread_join(thread,NULL) == 0);
    atomic_store(&block_snapshot,false);assert(call.frames <= 48);destroy_engine(engine);
}
static void test_due_clock_not_in_input_completion(void) {
    rk3_native_audio_iso *engine=new_engine();
    clock_start_observer=engine;clock_start_observations=0;
    assert(rk3_native_audio_iso_start(engine)==0);
    clock_start_observer=NULL;assert(clock_start_observations==2);
    rk3_native_audio_clock_schedule first,after;
    assert(rk3_native_audio_iso_clock_schedule(engine,&first)&&first.incarnation&&first.deadline);
    engine->ni_clock.next_callback=host_for_frame(mock.bus_frame);
    unsigned queries=mock.clock_queries,reads=mock.read_submissions,writes=mock.write_submissions;
    uint64_t previous=mock.reads[0].scheduled_frame;
    input_primary(0,false);input_callback(0,0);
    assert(mock.clock_queries==queries&&"IN completion must not run due clock maintenance");
    assert(mock.read_submissions==reads+1&&mock.reads[0].scheduled_frame==previous+64);
    assert(mock.write_submissions==writes+1);
    rk3_native_audio_iso_clock_service(engine);
    assert(mock.clock_queries>queries&&rk3_native_audio_iso_clock_schedule(engine,&after));
    assert(after.incarnation==first.incarnation&&after.deadline==host_for_frame(mock.bus_frame)+ticks_for_ns(128000000));
    queries=mock.clock_queries;rk3_native_audio_iso_clock_service(engine);assert(mock.clock_queries==queries);
    rk3_native_audio_iso_stop(engine);drain_aborted();
    assert(!rk3_native_audio_iso_clock_schedule(engine,&after));
    assert(rk3_native_audio_iso_start(engine)==0&&rk3_native_audio_iso_clock_schedule(engine,&after));
    assert(after.incarnation!=first.incarnation);first=after;
    destroy_engine(engine);
    engine=new_engine();assert(rk3_native_audio_iso_start(engine)==0);
    assert(rk3_native_audio_iso_clock_schedule(engine,&after)&&after.incarnation!=first.incarnation);
    destroy_engine(engine);
}
static rk3_native_audio_iso *clock_lifetime_observer;
static void observe_clock_lifetime(void) {
    clock_frame_hook=NULL;
    rk3_native_audio_iso *engine=clock_lifetime_observer;
    rk3_native_audio_clock_schedule schedule={99,99};
    assert(!rk3_native_audio_iso_clock_schedule(engine,&schedule));
    assert(schedule.incarnation==99&&schedule.deadline==99);
    unsigned queries=mock.clock_queries;
    rk3_native_audio_iso_clock_service(engine);
    assert(mock.clock_queries==queries); /* A nested worker pump cannot recurse. */
    rk3_native_audio_iso_stop(engine); /* The fake host drains aborts immediately. */
    assert(rk3_native_audio_iso_pending(engine)==0);
    assert(!rk3_native_audio_iso_is_quiescent(engine));
    assert(!rk3_native_audio_iso_release_usb(engine)&&!rk3_native_audio_iso_destroy(engine));
    assert(rk3_native_audio_iso_start(engine)==kIOReturnBusy);
}
static void test_clock_callback_lifetime(void) {
    rk3_native_audio_iso *engine=new_engine();
    assert(rk3_native_audio_iso_start(engine)==0);
    engine->ni_clock.next_callback=host_for_frame(mock.bus_frame);
    clock_lifetime_observer=engine;clock_frame_hook=observe_clock_lifetime;
    rk3_native_audio_iso_clock_service(engine);
    assert(!clock_frame_hook&&!rk3_native_audio_iso_running(engine));
    rk3_native_audio_clock_schedule schedule;
    assert(!rk3_native_audio_iso_clock_schedule(engine,&schedule));
    assert(rk3_native_audio_iso_is_quiescent(engine));
    destroy_engine(engine);clock_lifetime_observer=NULL;
}
static void test_original_default_rate_command(void) {
    rk3_native_audio_iso *engine=new_engine_rate(0);
    assert(rk3_native_audio_iso_start(engine)==0);
    const uint8_t stop[6]={9,255,255,128,0,1},start[6]={9,0,2,128,0,1};
    assert(mock.commands==2&&!memcmp(mock.commands_bytes[0],stop,6)&&!memcmp(mock.commands_bytes[1],start,6));
    destroy_engine(engine);
}
static void test_all_original_rate_start_commands(void) {
    static const struct {uint32_t rate;uint8_t command[6];} cases[]={
        {44100,{9,0,2,128,0,1}},{48000,{9,1,2,136,0,1}},
        {88200,{9,4,2,176,0,1}},{96000,{9,2,2,184,0,1}},{192000,{9,3,2,24,1,1}}
    };
    for(unsigned n=0;n<sizeof(cases)/sizeof(cases[0]);++n) {
        rk3_native_audio_iso *e=new_engine_rate(cases[n].rate);
        NIAudioClock previous=e->ni_clock;
        unsigned commands=mock.commands;
        assert(rk3_native_audio_iso_set_sample_rate(e,176400)==kIOReturnBadArgument);
        assert(e->sample_rate==cases[n].rate&&mock.commands==commands);
        assert(!memcmp(&previous,&e->ni_clock,sizeof(previous)));
        assert(rk3_native_audio_iso_start(e)==0);
        const uint8_t stop[]={9,255,255,cases[n].command[3],cases[n].command[4],1};
        assert(mock.commands==2&&!memcmp(mock.commands_bytes[0],stop,6)&&!memcmp(mock.commands_bytes[1],cases[n].command,6));
        assert(mock.read_submissions==64&&mock.reads_at_format==64&&mock.reads[0].scheduled_frame==1101&&mock.reads[63].scheduled_frame==1164);
        assert(e->input_buffer->size==64*4096&&e->wire_buffer->size==NI_AUDIO_RING_WIRE_BYTES);
        for(unsigned slot=0;slot<64;++slot)for(unsigned m=0;m<8;++m)
            assert(mock.reads[slot].list->entries[slot*8+m].frame.requested==512);
        assert(rk3_native_audio_iso_set_sample_rate(e,48000)==kIOReturnBusy);
        assert(e->sample_rate==cases[n].rate&&mock.commands==2);
        mock.defer_abort=true;rk3_native_audio_iso_stop(e);
        assert(!rk3_native_audio_iso_is_quiescent(e));
        assert(rk3_native_audio_iso_set_sample_rate(e,48000)==kIOReturnBusy);
        drain_aborted();assert(rk3_native_audio_iso_is_quiescent(e));
        previous=e->ni_clock;uint64_t seed=atomic_load(&e->clock_seed);
        assert(rk3_native_audio_iso_set_sample_rate(e,48000)==0);
        assert(!memcmp(&previous,&e->ni_clock,sizeof(previous))&&atomic_load(&e->clock_seed)==seed);
        assert(mock.commands==2&&rk3_native_audio_iso_start(e)==0&&atomic_load(&e->clock_seed)==seed+1);
        assert(e->sample_rate==48000);destroy_engine(e);
    }
}
static void rate_counts(uint32_t rate,uint64_t packet,uint16_t counts[8]) {
    for(unsigned m=0;m<8;++m) {
        uint64_t position=packet*8+m;
        counts[m]=(uint16_t)(((position+1)*rate/8000-position*rate/8000)*8);
    }
}
static void test_fractional_and_high_rate_input_led_wrap(void) {
    const uint32_t rates[]={44100,88200,192000};
    for(unsigned r=0;r<3;++r) {
        uint32_t rate=rates[r];rk3_native_audio_iso *e=new_engine_rate(rate);
        assert(rk3_native_audio_iso_start(e)==0);
        uint16_t counts[8];
        for(unsigned slot=0;slot<64;++slot) {
            rate_counts(rate,slot,counts);hardware_input(slot,counts,false);input_callback(slot,0);
            rk3_native_audio_iso_clock_service(e);
            assert(mock.last_write&&mock.last_write->scheduled_frame==1165+slot);
            for(unsigned m=0;m<8;++m)assert(mock.last_write->list->entries[mock.last_write->first+m].frame.requested==counts[m]);
        }
        assert(!e->schedule.priming&&mock.write_count==64);
        bool wrap=false,variable=false;uint16_t previous=0;
        for(unsigned step=0;step<500;++step) {
            unsigned slot=step%64;uint64_t frame=mock.reads[slot].scheduled_frame;
            complete_due_outputs(frame);rate_counts(rate,(uint64_t)step+64,counts);
            for(unsigned m=0;m<8;++m){if(previous&&previous!=counts[m])variable=true;previous=counts[m];}
            hardware_input(slot,counts,false);
            float input[2];assert(rk3_native_audio_iso_capture_at(e,input,step,1)==1);
            rk3_native_audio_iso_clock_service(e);rk3_native_audio_iso_service(e);input_callback(slot,0);
            MockTransfer *out=mock.last_write;assert(out&&out->scheduled_frame==frame+64);
            for(unsigned m=0;m<8;++m) {
                MockEntry *entry=out->list->entries+out->first+m;
                assert(entry->frame.requested==counts[m]);
                if((size_t)entry->offset+entry->frame.requested>out->buffer->size){assert(entry->wrap);wrap=true;}
            }
            uint32_t delay=0;int32_t error=0;
            assert(!rk3_native_audio_iso_panic(e,&delay,&error)&&rk3_native_audio_iso_stream_error(e)==0);
        }
        rk3_native_audio_clock clock;assert(rk3_native_audio_iso_clock(e,&clock));
        assert(clock.zero_sample_frame>=NI_AUDIO_RING_FRAME_COUNT&&wrap);
        if(rate!=192000)assert(variable);
        assert(e->ni_clock.interval_scaled==NI_AUDIO_CLOCK_INITIAL_INTERVAL);
        destroy_engine(e);
    }
}
int main(void) {
    test_original_default_rate_command();
    test_all_original_rate_start_commands();
    test_fractional_and_high_rate_input_led_wrap();
    test_due_clock_not_in_input_completion();
    test_clock_callback_lifetime();
    test_original_priming_transition_order();
    test_clock_start_order_and_idle_cadence();
    test_lifted_calendar_and_deferred_dma();test_capture_primary_and_repeatable_positions();
    test_delayed_callbacks_against_advancing_bus();test_completion_acceptance_order_binding();test_microframe_ring_wrap();
    test_empty_output_request_lifecycle();test_panic_classification();test_input_retry_exhaustion_no_invented_panic();test_initial_start_nonfatal_failures();test_real_format_retries_preserve_panic_policy();
    test_priming_written_inactive_clear();test_partial_output_diagnostic();
    test_exact_variable_count_zero_event();test_backward_output_host_time_guard();
    test_clock_source_failure_and_queued_zero();
    test_delayed_abort_storage();test_live_rt_storage();
    puts("NativeAudioISO: NI shared ring, deferred DMA, primary capture, advancing bus, clock, panic and lifetime passed.");
    return 0;
}
