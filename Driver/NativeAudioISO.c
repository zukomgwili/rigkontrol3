/* ARM/current-macOS binding for NI's shared rings and input-led calendar.
 * Original NI 2.8.0: prepareMemory59d0, clip6ab0, convert6e90,
 * queueOut7620, readHandler7a80, writeHandler7ec0, timestamp8100/b790.
 * Mapped output stays live after enqueue. There is no render/capture FIFO. */
#include "NativeAudioISO.h"
#include "AudioCodec.h"
#include "AudioRing.h"
#include "AudioSchedule.h"
#include <IOKit/usb/USBSpec.h>
#include <mach/mach_time.h>
#include <os/log.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(ATOMIC_LLONG_LOCK_FREE==2,"ARM capture requires lock-free stereo snapshots");
enum {INPUT_SLOTS=64,OUTPUT_SLOTS=128,MICROFRAMES=8,TRANSFER_BYTES=4096};
typedef struct {uint64_t usb_frame;uint32_t offset,total;} ZeroEvent;
typedef struct {
    struct rk3_native_audio_iso *engine;
    unsigned slot;
    bool pending;
    int32_t result;
} Transfer;
struct rk3_native_audio_iso {
    RK3HostInterface *interface;
    uint8_t input_pipe,output_pipe;
    rk3_native_audio_command command;
    void *command_context;
    RK3HostBuffer *input_buffer,*wire_buffer;
    RK3HostIsoList *input_list,*output_list;
    uint8_t *input_data;
    uint8_t output_pcm[NI_AUDIO_RING_PACKED_BYTES],input_pcm[NI_AUDIO_RING_PACKED_BYTES];
    NIAudioRing output_ring;
    NIAudioDecoder decoder;
    NIAudioSchedule schedule;
    Transfer inputs[INPUT_SLOTS],outputs[OUTPUT_SLOTS];
    unsigned ready_inputs[INPUT_SLOTS],ready_head,ready_count;
    uint64_t input_epoch[INPUT_SLOTS],consumed_epoch[INPUT_SLOTS];
    unsigned read_slot,read_microframe;
    uint64_t published_frames,wire_bytes;
    ZeroEvent zero_events[8];unsigned zero_head,zero_count;
    bool initial_timestamp_queued;
    uint64_t output_sample[OUTPUT_SLOTS],output_usb_frame[OUTPUT_SLOTS];
    uint32_t output_bytes[OUTPUT_SLOTS];
    size_t pending_count;
    bool usb_released,servicing,submit_diagnostic_logged,priming_pending,clock_starting,clock_servicing;
    uint64_t clock_incarnation;
    NIAudioClock ni_clock;
    mach_timebase_info_data_t timebase;
    NIAudioClockProvider clock_provider;
    atomic_uint_fast64_t capture_samples[NI_AUDIO_RING_FRAME_COUNT];
    atomic_flag processing,render_busy;
    atomic_bool running,pcm_ready,panic_pending,output_active,output_written;
    atomic_uint active_calls,panic_delay;
    atomic_int_least32_t fatal_error,panic_error,last_output_error;
    atomic_uint_fast64_t clock_sequence,clock_frame,clock_host,clock_seed,zero_frame,zero_host;
};
static atomic_uint_fast64_t next_clock_incarnation=1;
static bool accepted(int32_t status) {return status==0||(uint32_t)status==UINT32_C(0xe00002e7);}
static bool device_gone(int32_t status) {return status==kIOReturnNoDevice||status==kIOReturnNotOpen;}
static void note_stream_error(rk3_native_audio_iso *e,int32_t error) {
    int_least32_t empty=0;
    if(error)(void)atomic_compare_exchange_strong(&e->last_output_error,&empty,error);
}
/* Temporary .11 observation: only the first rejected submission per start.
 * Copy decoder fields with a single try; never wait for RT ownership or log
 * while holding its guard. No scheduling or sample-placement policy changes. */
static void log_submit_error(rk3_native_audio_iso *e,bool input,unsigned slot,uint64_t frame,int32_t error) {
    if(e->submit_diagnostic_logged)return;
    e->submit_diagnostic_logged=true;
    unsigned cursor=UINT32_MAX,micro=UINT32_MAX,priming=UINT32_MAX;
    uint64_t cursor_epoch=0,cursor_consumed=0,slot_epoch=0,slot_consumed=0;
    bool busy=atomic_flag_test_and_set_explicit(&e->processing,memory_order_acquire);
    if(!busy) {
        cursor=e->read_slot;micro=e->read_microframe;priming=e->schedule.priming;
        if(cursor<INPUT_SLOTS){cursor_epoch=e->input_epoch[cursor];cursor_consumed=e->consumed_epoch[cursor];}
        if(input&&slot<INPUT_SLOTS){slot_epoch=e->input_epoch[slot];slot_consumed=e->consumed_epoch[slot];}
        atomic_flag_clear_explicit(&e->processing,memory_order_release);
    }
    unsigned head=e->ready_count?e->ready_inputs[e->ready_head]:UINT32_MAX;
    uint64_t head_frame=head<INPUT_SLOTS?e->schedule.input_frame[head]:0;
    os_log_error(OS_LOG_DEFAULT,
        "[DEBUG-rk3-timing] engine %{public}s reject=0x%{public}x pipeIndex=%{public}u slot=%{public}u target=%{public}llu calendar=%{public}llu ready=%{public}u head=%{public}u headFrame=%{public}llu pending=%{public}llu priming=%{public}u decoderBusy=%{public}u cursor=%{public}u micro=%{public}u cursorEpoch=%{public}llu cursorConsumed=%{public}llu slotEpoch=%{public}llu slotConsumed=%{public}llu seed=%{public}llu",
        input?"IN":"OUT",(unsigned)error,(unsigned)(input?e->input_pipe:e->output_pipe),slot,
        (unsigned long long)frame,(unsigned long long)e->schedule.last_input_frame,
        e->ready_count,head,(unsigned long long)head_frame,(unsigned long long)e->pending_count,
        priming,(unsigned)busy,cursor,micro,(unsigned long long)cursor_epoch,
        (unsigned long long)cursor_consumed,(unsigned long long)slot_epoch,
        (unsigned long long)slot_consumed,(unsigned long long)atomic_load(&e->clock_seed));
}
static void request_panic(rk3_native_audio_iso *e,uint32_t delay,int32_t error) {
    note_stream_error(e,error);
    if(device_gone(error)) {
        atomic_store(&e->fatal_error,error);atomic_store(&e->running,false);atomic_store(&e->pcm_ready,false);return;
    }
    /* Fields are published before pending. A primary decoder and worker can
     * request this concurrently; the first reason remains authoritative. */
    int_least32_t empty=0;
    if(atomic_compare_exchange_strong(&e->panic_error,&empty,error?error:kIOReturnIOError)) {
        atomic_store(&e->panic_delay,delay);
        atomic_store_explicit(&e->panic_pending,true,memory_order_release);
    }
}
/* Diagnostic marks use their own Mach reads. Normal END can reuse the NI
 * callback's existing uptime; never add/move a provider observation. */
static void observe_clock(void *context,bool beginning,uint64_t tick,bool has_tick) {
    rk3_native_audio_iso *e=context;
    rk3_host_measurement_clock(e->interface,beginning,has_tick?tick:mach_absolute_time());
}
static uint64_t default_to_ns(void *context,uint64_t ticks) {
    const rk3_native_audio_iso *e=context;
    return ni_audio_clock_scale_time(ticks,e->timebase.numer,e->timebase.denom);
}
static uint64_t default_to_ticks(void *context,uint64_t ns) {
    const rk3_native_audio_iso *e=context;
    return ni_audio_clock_scale_time(ns,e->timebase.denom,e->timebase.numer);
}
static uint64_t ticks_to_ns(const rk3_native_audio_iso *e,uint64_t ticks) {
    return e->clock_provider.to_nanoseconds(e->clock_provider.context,ticks);
}
static uint64_t ns_to_ticks(const rk3_native_audio_iso *e,uint64_t ns) {
    return e->clock_provider.to_absolute(e->clock_provider.context,ns);
}
static uint64_t default_uptime(void *context) {(void)context;return mach_absolute_time();}
static int32_t default_frame(void *context,uint64_t *frame,uint64_t *paired) {
    rk3_native_audio_iso *e=context;return rk3_host_bus_frame(e->interface,frame,paired);
}
static bool clock_source_fatal(void *context,int32_t error) {(void)context;return device_gone(error);}
static void clock_error(rk3_native_audio_iso *e,int32_t result) {
    /* Legacy frame sampling has no NSError return. Keep a failed periodic
     * observation's last clock and cadence, without inventing audio panic.
     * Actual closed/removed ownership retains the existing fatal path. */
    if(device_gone(result))request_panic(e,0,result);
}
/* NI updateFrequency=0 publishes the complete eight-microframe IRP at
 * primary interrupt time. Modern per-transaction publication must not expose a
 * partial denominator to takeSimpleTimeStamp's byte-fraction calculation. */
static bool input_batch_ready(rk3_native_audio_iso *e,unsigned slot) {
    for(unsigned m=0;m<8;++m) {
        RK3HostIsoFrame f;
        if(!rk3_host_iso_list_snapshot(e->input_list,slot*8+m,&f)||f.status==RK3_HOST_ISO_PENDING)return false;
        if(f.actual>512){request_panic(e,0,kIOReturnOverrun);return false;}
    }
    return true;
}
static void queue_zero_locked(rk3_native_audio_iso *e,unsigned slot,unsigned micro,unsigned byte_offset) {
    uint32_t prefix=0,total=0;
    for(unsigned m=0;m<8;++m) {
        RK3HostIsoFrame f;
        if(!rk3_host_iso_list_snapshot(e->input_list,slot*8+m,&f)||f.actual>512){request_panic(e,0,kIOReturnBadArgument);return;}
        total+=f.actual;if(m<micro)prefix+=f.actual;
    }
    if(prefix+byte_offset>total||e->zero_count==8){request_panic(e,0,kIOReturnOverrun);return;}
    e->zero_events[(e->zero_head+e->zero_count)%8]=(ZeroEvent){e->schedule.input_frame[slot],prefix+byte_offset,total};
    ++e->zero_count;
}
/* Clock publication remains on the serial worker. RT primary processing only
 * records the exact event coordinates; it does no USB/time-anchor work. */
static void publish_zero_events(rk3_native_audio_iso *e) {
    /* Initial observations may fail without losing the exact queued ring
     * event. Its coordinates remain pending until maintenance has an anchor. */
    if(!ni_audio_clock_valid(&e->ni_clock))return;
    for(unsigned attempt=0;attempt<8;++attempt) {
        if(atomic_flag_test_and_set_explicit(&e->processing,memory_order_acquire))return;
        if(!e->zero_count){atomic_flag_clear_explicit(&e->processing,memory_order_release);return;}
        ZeroEvent event=e->zero_events[e->zero_head];e->zero_head=(e->zero_head+1)%8;--e->zero_count;
        atomic_flag_clear_explicit(&e->processing,memory_order_release);
        uint64_t ns=0;
        if(ni_audio_timestamp_nanoseconds(e->ni_clock.anchor_frame,ticks_to_ns(e,e->ni_clock.anchor_time),e->ni_clock.interval_scaled,
            event.usb_frame,event.offset,event.total,&ns)!=NI_AUDIO_SCHEDULE_OK){request_panic(e,0,kIOReturnBadArgument);return;}
        uint64_t ticks=ns_to_ticks(e,ns),previous=atomic_load(&e->zero_host);
        if(previous&&ticks<previous)continue; /* recovered takeSimpleTimeStamp guard */
        uint64_t frame=previous?atomic_load(&e->zero_frame)+NI_AUDIO_RING_FRAME_COUNT:0;
        if(previous&&frame<atomic_load(&e->zero_frame)){request_panic(e,0,kIOReturnOverrun);return;}
        atomic_fetch_add_explicit(&e->clock_sequence,1,memory_order_acq_rel);
        atomic_store_explicit(&e->zero_frame,frame,memory_order_relaxed);
        atomic_store_explicit(&e->zero_host,ticks,memory_order_relaxed);
        atomic_fetch_add_explicit(&e->clock_sequence,1,memory_order_release);
    }
}
static uint64_t packed_stereo(const uint8_t *p) {
    uint64_t value=0;for(unsigned i=0;i<6;++i)value=(value<<8)|p[i];return value;
}
static void publish_capture(rk3_native_audio_iso *e) {
    while(e->published_frames<e->decoder.completed_frames) {
        size_t frame=(size_t)(e->published_frames%NI_AUDIO_RING_FRAME_COUNT);
        atomic_store_explicit(&e->capture_samples[frame],packed_stereo(e->input_pcm+frame*6),memory_order_release);
        ++e->published_frames;
    }
    /* Preserve the actual partially updated packed frame too. NI conversion
     * reads the sample-position ring, not only a dequeued completed-frame list. */
    size_t frame=(size_t)(e->decoder.completed_frames%NI_AUDIO_RING_FRAME_COUNT);
    atomic_store_explicit(&e->capture_samples[frame],packed_stereo(e->input_pcm+frame*6),memory_order_release);
}
/* processing serializes decoder/cursors against list reset, not USB calls.
 * RT callers try it once and can always read the last coherent stereo ring. */
static void consume_input_locked(rk3_native_audio_iso *e) {
    if(e->schedule.priming) return;
    uint8_t *rings[]={e->input_pcm};
    for(unsigned visited=0;visited<INPUT_SLOTS*MICROFRAMES;++visited) {
        unsigned slot=e->read_slot,micro=e->read_microframe;
        if(micro==0&&(e->consumed_epoch[slot]==e->input_epoch[slot]||!input_batch_ready(e,slot)))return;
        RK3HostIsoFrame f;
        if(!rk3_host_iso_list_snapshot(e->input_list,slot*8+micro,&f)) {request_panic(e,0,kIOReturnBadArgument);return;}
        if(f.status==RK3_HOST_ISO_PENDING) return;
        if(f.actual>512) {request_panic(e,0,kIOReturnOverrun);return;}
        if(accepted(f.status)&&f.actual) {
            if(!e->initial_timestamp_queued){queue_zero_locked(e,slot,micro,0);e->initial_timestamp_queued=true;}
            const uint8_t *wire=e->input_data+slot*TRANSFER_BYTES+micro*512;
            for(unsigned byte=0;byte<f.actual;++byte) {
                uint64_t wraps=e->decoder.ring_wraps;
                if(ni_audio_decode(&e->decoder,wire+byte,1,rings,NI_AUDIO_RING_PACKED_BYTES)!=NI_AUDIO_OK){request_panic(e,0,kIOReturnBadArgument);return;}
                /* Mode2@0x82b0 timestamps after consuming the wrap check plane,
                 * before the following payload byte, hence byte+1. */
                if(e->decoder.ring_wraps!=wraps)queue_zero_locked(e,slot,micro,byte+1);
            }
            publish_capture(e);
            if(e->decoder.errors)request_panic(e,0,kIOReturnIOError);
        }
        if(++e->read_microframe==8) {
            e->consumed_epoch[slot]=e->input_epoch[slot];e->read_microframe=0;e->read_slot=(slot+1)%INPUT_SLOTS;
        }
    }
}
static bool prepare_input_locked(rk3_native_audio_iso *e,unsigned slot) {
    for(unsigned m=0;m<8;++m) {
        int32_t result=rk3_host_iso_list_set(e->input_list,slot*8+m,512,slot*TRANSFER_BYTES+m*512,false);
        if(result) {request_panic(e,0,result);return false;}
    }
    ++e->input_epoch[slot];return true;
}
static void read_complete(void *,int32_t,RK3HostIsoList *,size_t,size_t);
static void write_complete(void *,int32_t,RK3HostIsoList *,size_t,size_t);
static int32_t submit_input(rk3_native_audio_iso *e,unsigned slot,uint64_t frame) {
    /* NI queueIn0x6657/0x666a rejects stopped/panic before queue ownership. */
    if(!atomic_load(&e->running)||atomic_load(&e->panic_pending))return kIOReturnError;
    Transfer *t=&e->inputs[slot];t->pending=true;++e->pending_count;
    int32_t result=0;
    for(unsigned attempt=0;attempt<6;++attempt) {
        result=rk3_host_iso_enqueue(e->interface,e->input_pipe,e->input_buffer,e->input_list,
            slot*8,8,frame,read_complete,t);
        if(!result)return 0;
        note_stream_error(e,result);
        if(result==kIOReturnIsoTooOld)break; /* original queueIn does not retry a past frame */
    }
    t->pending=false;--e->pending_count;
    log_submit_error(e,true,slot,frame,result);
    /* queueIn0x673f increments its -6 counter; zero branches0x6743->0x6757
     * directly to return. Only IsoTooOld0x6768 arms the100ms panic timer.
     * Other exhausted rejections remain diagnostics, not an invented restart.
     * Actual removal is handled by the current platform's fatal lifecycle. */
    if(result==kIOReturnIsoTooOld||device_gone(result))
        request_panic(e,result==kIOReturnIsoTooOld?100:0,result);
    return result;
}
static void clear_inactive_output(rk3_native_audio_iso *e) {
    if(atomic_load(&e->output_active)||!atomic_load_explicit(&e->pcm_ready,memory_order_acquire))return;
    if(atomic_flag_test_and_set_explicit(&e->render_busy,memory_order_acquire))return;
    if(atomic_load(&e->output_written)||e->output_ring.stream_written) {
        (void)ni_audio_ring_clear_stream_original(&e->output_ring);
        e->output_ring.stream_written=false;atomic_store(&e->output_written,false);
    }
    atomic_flag_clear_explicit(&e->render_busy,memory_order_release);
}
static bool submit_output(rk3_native_audio_iso *e,const NIAudioOutputPlan *p) {
    unsigned slot=p->output_slot;
    /* NI queueOutIrp0x7620 submits all eight microframes even when every
     * requested count is zero. Keep the mapped backing and slot lifetime:
     * completion still publishes the saved output sample position. */
    size_t offset=p->segment_offset[0];
    for(unsigned m=0;m<8;++m) {
        uint32_t n=p->requested[m];
        int32_t result=rk3_host_iso_list_set(e->output_list,slot*8+m,n,(uint32_t)offset,
            offset+n>NI_AUDIO_RING_WIRE_BYTES);
        if(result) {request_panic(e,0,result);return false;}
        offset=(offset+n)%NI_AUDIO_RING_WIRE_BYTES;
    }
    /* The same mapped ring is passed every time. The host later writes future
     * sample positions directly into it, matching NI's descriptor ranges. */
    e->output_sample[slot]=e->wire_bytes/8;e->wire_bytes+=p->total_bytes;
    e->output_usb_frame[slot]=p->output_frame;e->output_bytes[slot]=p->total_bytes;
    Transfer *t=&e->outputs[slot];t->pending=true;++e->pending_count;
    int32_t result=rk3_host_iso_enqueue(e->interface,e->output_pipe,e->wire_buffer,e->output_list,
        slot*8,8,p->output_frame,write_complete,t);
    if(result) {
        t->pending=false;--e->pending_count;note_stream_error(e,result);
        log_submit_error(e,false,slot,p->output_frame,result);
        if(device_gone(result)) request_panic(e,0,result);
        /* NI queueOut leaves the reserved slot busy after a failed submission.
         * It does not directly schedule panic; subsequent exhaustion does. */
        return false;
    }
    return true;
}
/* NI7e51..7ea8: after IN then OUT attempts, reset search/cursors, clear
 * priming and scan again. RT ownership can defer this due phase, never drop
 * it or let another ready callback overtake it. No USB call under this guard. */
static bool finish_priming(rk3_native_audio_iso *e) {
    if(!e->priming_pending)return true;
    if(!atomic_load(&e->running)){e->priming_pending=false;return true;}
    if(atomic_flag_test_and_set_explicit(&e->processing,memory_order_acquire))return false;
    /* Discard only the initial epoch; already-published requeues stay live. */
    for(unsigned i=0;i<INPUT_SLOTS;++i)if(e->input_epoch[i]==1)e->consumed_epoch[i]=1;
    (void)ni_audio_schedule_finish_priming(&e->schedule);
    e->read_slot=0;e->read_microframe=0;
    (void)ni_audio_decoder_set_priming(&e->decoder,false);
    (void)ni_audio_ring_set_priming(&e->output_ring,false);
    e->priming_pending=false;
    atomic_store_explicit(&e->pcm_ready,true,memory_order_release);
    consume_input_locked(e);
    atomic_flag_clear_explicit(&e->processing,memory_order_release);
    return true;
}
void rk3_native_audio_iso_service(rk3_native_audio_iso *e) {
    if(!e||e->servicing||!atomic_load(&e->running)) return;
    e->servicing=true;
    clear_inactive_output(e);
    publish_zero_events(e);
    /* A previously due phase also completes when IN set recoverable panic. */
    if(!finish_priming(e)){e->servicing=false;return;}
    while(e->ready_count&&!atomic_load(&e->panic_pending)&&atomic_load(&e->running)) {
        if(atomic_flag_test_and_set_explicit(&e->processing,memory_order_acquire)) break;
        unsigned slot=e->ready_inputs[e->ready_head];
        consume_input_locked(e);
        if(atomic_load(&e->panic_pending)||!atomic_load(&e->running)) {
            atomic_flag_clear_explicit(&e->processing,memory_order_release);break;
        }
        if(!e->schedule.priming&&e->consumed_epoch[slot]!=e->input_epoch[slot]) {
            atomic_flag_clear_explicit(&e->processing,memory_order_release);break;
        }
        NIAudioPacketResult frames[8];bool valid=true;
        for(unsigned m=0;m<8;++m) {
            RK3HostIsoFrame f;
            if(!rk3_host_iso_list_snapshot(e->input_list,slot*8+m,&f)||f.actual>512) {valid=false;break;}
            frames[m]=(NIAudioPacketResult){f.status,(uint16_t)f.actual};
        }
        NIAudioOutputPlan plan;
        NIAudioScheduleResult planned=valid?ni_audio_schedule_input_completed(&e->schedule,slot,frames,&plan):NI_AUDIO_SCHEDULE_INVALID;
        if(planned!=NI_AUDIO_SCHEDULE_OK) {
            request_panic(e,planned==NI_AUDIO_SCHEDULE_NO_OUTPUT?100:0,
                planned==NI_AUDIO_SCHEDULE_NO_OUTPUT?kIOReturnNoResources:kIOReturnBadArgument);
            atomic_flag_clear_explicit(&e->processing,memory_order_release);break;
        }
        e->ready_head=(e->ready_head+1)%INPUT_SLOTS;--e->ready_count;
        bool prepared=prepare_input_locked(e,slot);
        atomic_flag_clear_explicit(&e->processing,memory_order_release);
        if(!prepared) break;
        (void)submit_input(e,slot,plan.requeued_input_frame);
        if(atomic_load(&e->running)) (void)submit_output(e,&plan); /* original queueIn then queueOut */
        if(plan.ends_priming)e->priming_pending=true;
        if(!finish_priming(e))break;
    }
    publish_zero_events(e);
    e->servicing=false;
}
static void read_complete(void *context,int32_t result,RK3HostIsoList *list,size_t first,size_t count) {
    Transfer *t=context;rk3_native_audio_iso *e=t->engine;
    if(!t->pending) {request_panic(e,0,kIOReturnInternalError);return;}
    t->pending=false;--e->pending_count;
    if(!atomic_load(&e->running)||atomic_load(&e->panic_pending)) return;
    if(list!=e->input_list||first!=t->slot*8||count!=8) {request_panic(e,0,kIOReturnBadArgument);return;}
    if(device_gone(result)) {request_panic(e,0,result);return;}
    /* NI consumes accepted individual frame results even after several
     * recoverable request statuses. Abort callbacks retire without requeue. */
    if(result==kIOReturnAborted) return;
    if(e->ready_count==INPUT_SLOTS) {request_panic(e,100,kIOReturnNoResources);return;}
    t->result=result;e->ready_inputs[(e->ready_head+e->ready_count)%INPUT_SLOTS]=t->slot;++e->ready_count;
    rk3_native_audio_iso_service(e);
}
static void write_complete(void *context,int32_t result,RK3HostIsoList *list,size_t first,size_t count) {
    Transfer *t=context;rk3_native_audio_iso *e=t->engine;
    if(!t->pending) {request_panic(e,0,kIOReturnInternalError);return;}
    t->pending=false;--e->pending_count;
    uint32_t ring_position=0;
    (void)ni_audio_schedule_output_completed(&e->schedule,t->slot,&ring_position);
    if(!atomic_load(&e->running)||atomic_load(&e->panic_pending)) return;
    if(list!=e->output_list||first!=t->slot*8||count!=8) {request_panic(e,0,kIOReturnBadArgument);return;}
    if(device_gone(result)) {request_panic(e,0,result);return;}
    if(!accepted(result))note_stream_error(e,result);
    for(unsigned m=0;m<8;++m) {
        RK3HostIsoFrame frame;
        if(!rk3_host_iso_list_snapshot(e->output_list,t->slot*8+m,&frame)) {
            note_stream_error(e,kIOReturnBadArgument);break;
        }
        if(frame.requested&&(!accepted(frame.status)||frame.actual!=frame.requested))
            note_stream_error(e,accepted(frame.status)?kIOReturnUnderrun:frame.status);
    }
    clear_inactive_output(e);
    publish_zero_events(e);
    if(!ni_audio_clock_valid(&e->ni_clock)) return;
    uint64_t ns=0;
    if(ni_audio_timestamp_nanoseconds(e->ni_clock.anchor_frame,ticks_to_ns(e,e->ni_clock.anchor_time),e->ni_clock.interval_scaled,
        e->output_usb_frame[t->slot],0,e->output_bytes[t->slot],&ns)!=NI_AUDIO_SCHEDULE_OK) {request_panic(e,0,kIOReturnBadArgument);return;}
    uint64_t sample=e->output_sample[t->slot];
    uint64_t host_ticks=ns_to_ticks(e,ns);
    if(sample<atomic_load(&e->clock_frame)||host_ticks<atomic_load(&e->clock_host))return;
    atomic_fetch_add_explicit(&e->clock_sequence,1,memory_order_acq_rel);
    atomic_store_explicit(&e->clock_frame,sample,memory_order_relaxed);
    atomic_store_explicit(&e->clock_host,host_ticks,memory_order_relaxed);
    atomic_fetch_add_explicit(&e->clock_sequence,1,memory_order_release);
}
static int32_t validate_pipe(rk3_native_audio_iso *e,uint8_t pipe,uint8_t direction,uint8_t number) {
    RK3HostPipeProperties p;int32_t result=rk3_host_pipe_properties(e->interface,pipe,&p);
    if(result) return result;
    return p.direction==direction&&p.number==number&&p.transfer_type==kUSBIsoc&&p.max_packet_size==512&&p.interval==1?0:kIOReturnBadArgument;
}
rk3_native_audio_iso *rk3_native_audio_iso_create(RK3HostInterface *interface,uint8_t in,uint8_t out,
    rk3_native_audio_command command,void *context) {
    return rk3_native_audio_iso_create_with_clock(interface,in,out,command,context,NULL);
}
rk3_native_audio_iso *rk3_native_audio_iso_create_with_clock(RK3HostInterface *interface,uint8_t in,uint8_t out,
    rk3_native_audio_command command,void *context,const NIAudioClockProvider *provider) {
    if(provider&&(!provider->frame||!provider->uptime||!provider->to_nanoseconds||!provider->to_absolute))return NULL;
    if(!interface||!in||!out||!command||!rk3_host_primary_lists_supported()) return NULL;
    rk3_native_audio_iso *e=calloc(1,sizeof(*e));if(!e)return NULL;
    e->interface=interface;e->input_pipe=in;e->output_pipe=out;e->command=command;e->command_context=context;
    rk3_host_retain(interface);(void)mach_timebase_info(&e->timebase);
    e->clock_provider=provider?*provider:(NIAudioClockProvider){e,default_frame,default_uptime,default_to_ns,default_to_ticks,clock_source_fatal};
    e->clock_provider.is_fatal=clock_source_fatal; /* Existing ISO removal policy. */
    (void)ni_audio_clock_init(&e->ni_clock,&e->clock_provider);
    ni_audio_clock_set_observer(&e->ni_clock,observe_clock,e);
    atomic_flag_clear(&e->processing);atomic_flag_clear(&e->render_busy);
    atomic_init(&e->running,false);atomic_init(&e->pcm_ready,false);atomic_init(&e->panic_pending,false);
    atomic_init(&e->output_active,true);atomic_init(&e->output_written,false);
    atomic_init(&e->panic_delay,0);atomic_init(&e->panic_error,0);atomic_init(&e->fatal_error,0);atomic_init(&e->last_output_error,0);
    atomic_init(&e->active_calls,0);atomic_init(&e->clock_seed,0);
    atomic_init(&e->clock_sequence,0);atomic_init(&e->clock_frame,0);atomic_init(&e->clock_host,0);
    atomic_init(&e->zero_frame,0);atomic_init(&e->zero_host,0);
    for(unsigned i=0;i<NI_AUDIO_RING_FRAME_COUNT;++i)atomic_init(&e->capture_samples[i],0);
    int32_t error=0;
    e->input_buffer=rk3_host_buffer_create(interface,INPUT_SLOTS*TRANSFER_BYTES,&error);
    e->wire_buffer=rk3_host_buffer_create(interface,NI_AUDIO_RING_WIRE_BYTES,&error);
    e->input_list=rk3_host_iso_list_create(interface,INPUT_SLOTS*8,&error);
    e->output_list=rk3_host_iso_list_create(interface,OUTPUT_SLOTS*8,&error);
    if(!e->input_buffer||!e->wire_buffer||!e->input_list||!e->output_list) {(void)rk3_native_audio_iso_destroy(e);return NULL;}
    e->input_data=rk3_host_buffer_bytes(e->input_buffer);
    if(ni_audio_ring_init(&e->output_ring,rk3_host_buffer_bytes(e->wire_buffer),NI_AUDIO_RING_WIRE_BYTES,
        e->output_pcm,NI_AUDIO_RING_PACKED_BYTES)!=NI_AUDIO_RING_OK) {(void)rk3_native_audio_iso_destroy(e);return NULL;}
    for(unsigned i=0;i<INPUT_SLOTS;++i)e->inputs[i]=(Transfer){e,i,false,0};
    for(unsigned i=0;i<OUTPUT_SLOTS;++i)e->outputs[i]=(Transfer){e,i,false,0};
    return e;
}
static int32_t start_engine(rk3_native_audio_iso *e) {
    if(!e||e->usb_released)return kIOReturnBadArgument;
    if(atomic_load(&e->running))return 0;
    if(!rk3_native_audio_iso_is_quiescent(e))return kIOReturnBusy;
    int32_t result=validate_pipe(e,e->input_pipe,kUSBIn,2);
    if(!result)result=validate_pipe(e,e->output_pipe,kUSBOut,6);
    if(result)return result;
    const uint8_t stop[]={9,255,255,136,0,1},start[]={9,1,2,136,0,1};
    /* performStart0x5fd6 ignores the rate-ff STOP result; only the real-rate
     * setAudioParams result at0x63bc is tested. The bounded command adapter
     * retains its own pending write storage even if this attempt fails. */
    (void)e->command(e->command_context,stop,sizeof(stop),1200);
    /* Original start aborts and resets both AUDIO pipes. These optional clears
     * are attempted like NI; the modern ISO pipe may not support halt state. */
    (void)rk3_host_abort(e->interface,e->input_pipe);(void)rk3_host_abort(e->interface,e->output_pipe);
    (void)rk3_host_clear_stall(e->interface,e->input_pipe);(void)rk3_host_clear_stall(e->interface,e->output_pipe);
    uint64_t frame=0,host=0;result=rk3_host_bus_frame(e->interface,&frame,&host);if(result)return result;
    if(ni_audio_schedule_init(&e->schedule,frame,2,1)!=NI_AUDIO_SCHEDULE_OK||
       ni_audio_ring_reset(&e->output_ring)!=NI_AUDIO_RING_OK||
       ni_audio_decoder_init(&e->decoder,NI_AUDIO_MODE_2,1,NI_AUDIO_RING_PACKED_BYTES,true)!=NI_AUDIO_OK)return kIOReturnBadArgument;
    memset(e->input_data,0,INPUT_SLOTS*TRANSFER_BYTES);memset(e->input_pcm,0,sizeof(e->input_pcm));
    memset(e->input_epoch,0,sizeof(e->input_epoch));memset(e->consumed_epoch,0,sizeof(e->consumed_epoch));
    e->submit_diagnostic_logged=false;e->priming_pending=false;
    e->ready_head=0;e->ready_count=0;e->zero_head=0;e->zero_count=0;e->initial_timestamp_queued=false;e->read_slot=0;e->read_microframe=0;e->published_frames=0;e->wire_bytes=0;
    for(unsigned i=0;i<NI_AUDIO_RING_FRAME_COUNT;++i)atomic_store(&e->capture_samples[i],0);
    atomic_store(&e->fatal_error,0);atomic_store(&e->panic_error,0);atomic_store(&e->last_output_error,0);
    atomic_store(&e->panic_delay,0);atomic_store(&e->panic_pending,false);atomic_store(&e->pcm_ready,false);
    atomic_store(&e->clock_frame,0);atomic_store(&e->clock_host,0);atomic_store(&e->zero_frame,0);atomic_store(&e->zero_host,0);atomic_fetch_add(&e->clock_seed,1);
    /* clock_seed is informative; Host owns the unique diagnostic stream
     * incarnation even if a replacement engine's seed starts at one again. */
    rk3_host_measurement_begin_stream(e->interface,atomic_load(&e->clock_seed));
    atomic_store(&e->running,true);
    for(unsigned i=0;i<INPUT_SLOTS;++i) {
        if(!atomic_load(&e->running))return atomic_load(&e->fatal_error)?atomic_load(&e->fatal_error):kIOReturnAborted;
        /* Initial queueIn results are only logged at0x6356..0x638b; the
         * loop continues through64 at0x639a. Its panic guard precedes the
         * global calendar increment, so later guarded calls are no-ops. */
        if(atomic_load(&e->panic_pending))continue;
        uint64_t scheduled=0;
        if(ni_audio_schedule_begin_input(&e->schedule,i,&scheduled)!=NI_AUDIO_SCHEDULE_OK){request_panic(e,0,kIOReturnBadArgument);return kIOReturnBadArgument;}
        if(!prepare_input_locked(e,i))
            return atomic_load(&e->fatal_error)?atomic_load(&e->fatal_error):atomic_load(&e->panic_error);
        result=submit_input(e,i,scheduled);
        if(device_gone(result))return result;
        /* Nonfatal initial rejection remains a diagnostic; real format setup
         * still follows. NativeUSB separately requires a real completed clock. */
    }
    /* Original0x63bc performs one format attempt, then0x63c5 initializes-3:
     *0x63d0..0x640c gives four further attempts with200ms delays. Existing
     * recoverable panic does not suppress them. Actual stopped/fatal lifetime
     * remains a modern binding guard; NativeUSB separately checks cancellation. */
    for(unsigned attempt=0;attempt<5;++attempt) {
        if(e->command(e->command_context,start,sizeof(start),1200)) {
            int32_t observed=ni_audio_clock_start(&e->ni_clock);clock_error(e,observed);
            return device_gone(observed)?observed:0;
        }
        if(!atomic_load(&e->running))break;
        if(attempt!=4) {
            uint64_t deadline=mach_absolute_time()+ns_to_ticks(e,200000000);
            while(mach_absolute_time()<deadline&&atomic_load(&e->running))
                (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,true);
        }
    }
    /*0x640e/0x6410 returns the format rejection via0x644a without arming a
     * new panic timer. Any earlier TooOld reason remains authoritative. */
    return kIOReturnIOError;
}
int32_t rk3_native_audio_iso_start(rk3_native_audio_iso *e) {
    if(!e||e->usb_released)return kIOReturnBadArgument;
    if(atomic_load(&e->running))return 0;
    if(!rk3_native_audio_iso_is_quiescent(e))return kIOReturnBusy;
    uint64_t incarnation=atomic_fetch_add(&next_clock_incarnation,1);
    if(!incarnation)return kIOReturnNoResources;
    e->clock_incarnation=incarnation;e->clock_starting=true;
    int32_t result=start_engine(e);
    e->clock_starting=false;return result;
}
bool rk3_native_audio_iso_clock_schedule(rk3_native_audio_iso *e,rk3_native_audio_clock_schedule *schedule) {
    if(!e||!schedule||e->clock_starting||e->clock_servicing||
        !atomic_load(&e->running)||!e->ni_clock.armed)return false;
    *schedule=(rk3_native_audio_clock_schedule){e->clock_incarnation,e->ni_clock.next_callback};return true;
}
void rk3_native_audio_iso_clock_service(rk3_native_audio_iso *e) {
    if(!e||e->clock_starting||e->clock_servicing||!atomic_load(&e->running))return;
    e->clock_servicing=true;atomic_fetch_add(&e->active_calls,1);
    clock_error(e,ni_audio_clock_service(&e->ni_clock));
    atomic_fetch_sub(&e->active_calls,1);e->clock_servicing=false;
}
void rk3_native_audio_iso_stop(rk3_native_audio_iso *e) {
    if(!e)return;e->priming_pending=false;atomic_store(&e->running,false);atomic_store(&e->pcm_ready,false);
    ni_audio_clock_stop(&e->ni_clock);
    if(!e->usb_released){(void)rk3_host_abort(e->interface,e->input_pipe);(void)rk3_host_abort(e->interface,e->output_pipe);}
}
size_t rk3_native_audio_iso_pending(const rk3_native_audio_iso *e){return e?e->pending_count:0;}
bool rk3_native_audio_iso_is_quiescent(const rk3_native_audio_iso *e){return !e||(!e->pending_count&&!atomic_load(&e->active_calls));}
bool rk3_native_audio_iso_release_usb(rk3_native_audio_iso *e) {
    if(!e||e->usb_released)return true;
    if(atomic_load(&e->running)||!rk3_native_audio_iso_is_quiescent(e))return false;
    rk3_host_iso_list_release(e->input_list);rk3_host_iso_list_release(e->output_list);
    rk3_host_buffer_release(e->input_buffer);rk3_host_buffer_release(e->wire_buffer);
    e->input_list=NULL;e->output_list=NULL;e->input_buffer=NULL;e->wire_buffer=NULL;e->input_data=NULL;
    e->usb_released=true;return true;
}
bool rk3_native_audio_iso_destroy(rk3_native_audio_iso *e) {
    if(!e)return true;
    if(atomic_load(&e->running)||!rk3_native_audio_iso_is_quiescent(e)||!rk3_native_audio_iso_release_usb(e))return false;
    rk3_host_release(e->interface);free(e);return true;
}
int32_t rk3_native_audio_iso_error(const rk3_native_audio_iso *e){return e?atomic_load(&e->fatal_error):kIOReturnBadArgument;}
int32_t rk3_native_audio_iso_stream_error(const rk3_native_audio_iso *e){return e?atomic_load(&e->last_output_error):kIOReturnBadArgument;}
bool rk3_native_audio_iso_running(const rk3_native_audio_iso *e){return e&&atomic_load(&e->running);}
void rk3_native_audio_iso_set_output_active(rk3_native_audio_iso *e,bool active){if(e)atomic_store(&e->output_active,active);}
bool rk3_native_audio_iso_panic(const rk3_native_audio_iso *e,uint32_t *delay,int32_t *error) {
    if(!e||!atomic_load_explicit(&e->panic_pending,memory_order_acquire))return false;
    if(delay)*delay=atomic_load(&e->panic_delay);if(error)*error=atomic_load(&e->panic_error);return true;
}
size_t rk3_native_audio_iso_render_at(rk3_native_audio_iso *e,const float *samples,uint64_t first,size_t frames) {
    if(!e||(!samples&&frames)||frames>NI_AUDIO_RING_FRAME_COUNT)return 0;
    atomic_fetch_add(&e->active_calls,1);size_t written=0;
    if(atomic_load(&e->running)) {
        if(atomic_load(&e->output_active))atomic_store(&e->output_written,true);
        if(!atomic_load(&e->output_active)||!atomic_load_explicit(&e->pcm_ready,memory_order_acquire))written=frames; /* original priming clip no-op */
        else if(!atomic_flag_test_and_set_explicit(&e->render_busy,memory_order_acquire)) {
            NIAudioRingResult result=ni_audio_ring_write_f32_at(&e->output_ring,first,samples,frames);
            if(result>=0)written=frames;
            atomic_flag_clear_explicit(&e->render_busy,memory_order_release);
        }
    }
    atomic_fetch_sub(&e->active_calls,1);return written;
}
size_t rk3_native_audio_iso_capture_at(rk3_native_audio_iso *e,float *samples,uint64_t first,size_t frames) {
    if(!e||(!samples&&frames)||frames>NI_AUDIO_RING_FRAME_COUNT)return 0;
    atomic_fetch_add(&e->active_calls,1);size_t captured=0;
    if(atomic_load(&e->running)) {
        if(!atomic_flag_test_and_set_explicit(&e->processing,memory_order_acquire)) {
            consume_input_locked(e);atomic_flag_clear_explicit(&e->processing,memory_order_release);
        }
        for(size_t i=0;i<frames;++i) {
            uint64_t value=atomic_load_explicit(&e->capture_samples[(first%NI_AUDIO_RING_FRAME_COUNT+i)%NI_AUDIO_RING_FRAME_COUNT],memory_order_acquire);
            int32_t left=(int32_t)((value>>24)&0xffffff),right=(int32_t)(value&0xffffff);
            if(left&0x800000)left-=0x1000000;if(right&0x800000)right-=0x1000000;
            samples[i*2]=(float)left/8388608.0f;samples[i*2+1]=(float)right/8388608.0f;
        }
        captured=frames;
    }
    atomic_fetch_sub(&e->active_calls,1);return captured;
}
bool rk3_native_audio_iso_clock(rk3_native_audio_iso *e,rk3_native_audio_clock *clock) {
    if(!e||!clock)return false;atomic_fetch_add(&e->active_calls,1);bool valid=false;
    for(unsigned attempt=0;attempt<3&&atomic_load(&e->running)&&atomic_load_explicit(&e->pcm_ready,memory_order_acquire);++attempt) {
        uint64_t seq=atomic_load_explicit(&e->clock_sequence,memory_order_acquire);if(seq&1)continue;
        rk3_native_audio_clock value={atomic_load(&e->clock_frame),atomic_load(&e->clock_host),atomic_load(&e->clock_seed),atomic_load(&e->zero_frame),atomic_load(&e->zero_host)};
        if(seq==atomic_load_explicit(&e->clock_sequence,memory_order_acquire)&&value.host_time&&value.zero_host_time){*clock=value;valid=true;break;}
    }
    atomic_fetch_sub(&e->active_calls,1);return valid;
}
