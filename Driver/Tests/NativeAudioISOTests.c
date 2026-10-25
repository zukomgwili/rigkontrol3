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
    int32_t fail_read,fail_write;
    bool persistent_read_failure,persistent_write_failure,fail_command,fail_stop,defer_abort,primary_supported;
    uint8_t commands_bytes[2][6];
    MockTransfer *last_write;
} mock;
static atomic_bool block_snapshot,snapshot_entered,release_snapshot;

static uint64_t ticks_for_ns(uint64_t nanoseconds) {
    mach_timebase_info_data_t scale;assert(mach_timebase_info(&scale) == 0);
    return nanoseconds*scale.denom/scale.numer;
}
static uint64_t host_for_frame(uint64_t frame) {
    assert(frame >= 1000);return mock.anchor_host+ticks_for_ns((frame-1000)*UINT64_C(1000000));
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
    if(mock.commands < 2)memcpy(mock.commands_bytes[mock.commands],bytes,6);
    ++mock.commands;
    if(bytes[1]!=255) {
        mock.reads_at_format=mock.read_submissions;
        if(mock.format_failures_remaining){--mock.format_failures_remaining;return false;}
    }
    return !mock.fail_command&&!(mock.fail_stop&&bytes[1]==255);
}
static rk3_native_audio_iso *new_engine(void) {
    memset(&mock,0,sizeof(mock));mock.interface.references=1;mock.bus_frame=1000;
    mock.anchor_host=mach_absolute_time();mock.primary_supported=true;
    atomic_store(&block_snapshot,false);atomic_store(&snapshot_entered,false);atomic_store(&release_snapshot,false);
    rk3_native_audio_iso *engine=rk3_native_audio_iso_create(&mock.interface,1,2,command,NULL);
    assert(engine && rk3_native_audio_iso_is_quiescent(engine));return engine;
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
        assert(mock.last_write && mock.last_write->scheduled_frame == 1165+slot);
    }
    assert(mock.write_count == 64);
    /* First non-priming data becomes visible to the host before IN callback. */
    input_primary(0,false);
    float initial[2];assert(rk3_native_audio_iso_capture_at(engine,initial,0,1)==1);
    rk3_native_audio_iso_service(engine); /* worker publishes the exact initial zero event */
    output_callback(0);
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
        rk3_native_audio_iso_service(engine);input_callback(slot,0);
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
    /* Force the128ms anchor refresh to receive a backwards host-time pair while
     * bus/sample frames advance. This deliberately malformed clock source must
     * not make the public current-position clock regress. */
    struct timespec delay={0,130000000};nanosleep(&delay,NULL);
    assert(mock.anchor_host>ticks_for_ns(500000000));mock.anchor_host-=ticks_for_ns(500000000);
    output_callback(1);assert(rk3_native_audio_iso_clock(engine,&after));
    assert(after.sample_frame==before.sample_frame&&after.host_time==before.host_time);
    assert(after.zero_sample_frame==before.zero_sample_frame&&after.zero_host_time==before.zero_host_time);
    destroy_engine(engine);
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
int main(void) {
    test_lifted_calendar_and_deferred_dma();test_capture_primary_and_repeatable_positions();
    test_delayed_callbacks_against_advancing_bus();test_completion_acceptance_order_binding();test_microframe_ring_wrap();
    test_empty_output_request_lifecycle();test_panic_classification();test_input_retry_exhaustion_no_invented_panic();test_initial_start_nonfatal_failures();test_real_format_retries_preserve_panic_policy();
    test_priming_written_inactive_clear();test_partial_output_diagnostic();
    test_exact_variable_count_zero_event();test_backward_output_host_time_guard();
    test_delayed_abort_storage();test_live_rt_storage();
    puts("NativeAudioISO: NI shared ring, deferred DMA, primary capture, advancing bus, clock, panic and lifetime passed.");
    return 0;
}
