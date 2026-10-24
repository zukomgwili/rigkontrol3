#include "NativeAudioISO.h"
#include "AudioCodec.h"
#include "AudioSchedule.h"

#include <mach/mach_time.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2,"Audio requires lock-free 64-bit counters");
enum { QUEUE_FRAMES = 16384, PACKED_BYTES = 98304, TRANSFER_BYTES = 4096 };

typedef struct {
    atomic_uint_fast64_t head, tail;
    float samples[QUEUE_FRAMES * 2];
} StereoQueue;
typedef struct {
    struct rk3_native_audio_iso *engine;
    unsigned slot;
    bool pending;
} Transfer;

struct rk3_native_audio_iso {
    IOUSBInterfaceInterface300 **interface;
    uint8_t input_pipe, output_pipe;
    rk3_native_audio_command command;
    void *command_context;
    uint8_t *input_data, *output_data;
    IOUSBLowLatencyIsocFrame *input_frames, *output_frames;
    Transfer inputs[64], outputs[128];
    size_t pending_count;
    bool usb_released;
    unsigned read_slot, read_microframe;
    NIAudioSchedule schedule;
    NIAudioEncoder encoder;
    NIAudioDecoder decoder;
    uint8_t output_pcm[PACKED_BYTES], input_pcm[PACKED_BYTES];
    uint64_t capture_next_frame, wire_bytes_scheduled;
    uint64_t output_sample_end[128];
    unsigned output_microframes[128];
    StereoQueue render_queue, capture_queue;
    atomic_flag render_busy, capture_busy;
    atomic_bool running;
    atomic_int_least32_t error;
    atomic_uint active_calls;
    atomic_uint_fast64_t clock_sequence, clock_frame, clock_host, clock_seed;
};

static size_t queue_write(StereoQueue *queue,const float *samples,size_t frames) {
    uint64_t head=atomic_load_explicit(&queue->head,memory_order_relaxed);
    uint64_t tail=atomic_load_explicit(&queue->tail,memory_order_acquire);
    uint64_t available=QUEUE_FRAMES-(head-tail);
    if (frames > available) frames=(size_t)available;
    for(size_t index=0;index<frames;++index) {
        size_t target=(size_t)((head+index)%QUEUE_FRAMES)*2;
        queue->samples[target]=samples[index*2];
        queue->samples[target+1]=samples[index*2+1];
    }
    atomic_store_explicit(&queue->head,head+frames,memory_order_release);
    return frames;
}

static size_t queue_read(StereoQueue *queue,float *samples,size_t frames) {
    uint64_t tail=atomic_load_explicit(&queue->tail,memory_order_relaxed);
    uint64_t head=atomic_load_explicit(&queue->head,memory_order_acquire);
    uint64_t available=head-tail;
    if(frames > available) frames=(size_t)available;
    for(size_t index=0;index<frames;++index) {
        size_t source=(size_t)((tail+index)%QUEUE_FRAMES)*2;
        samples[index*2]=queue->samples[source];
        samples[index*2+1]=queue->samples[source+1];
    }
    atomic_store_explicit(&queue->tail,tail+frames,memory_order_release);
    return frames;
}

static void fail_engine(rk3_native_audio_iso *engine,int32_t error) {
    int_least32_t expected=0;
    (void)atomic_compare_exchange_strong(&engine->error,&expected,error);
    bool was_running=atomic_exchange(&engine->running,false);
    if(was_running) {
        (void)(*engine->interface)->AbortPipe(engine->interface,engine->input_pipe);
        (void)(*engine->interface)->AbortPipe(engine->interface,engine->output_pipe);
    }
}

static bool accepted_status(IOReturn status) {
    return status == kIOReturnSuccess || (uint32_t)status == UINT32_C(0xe00002e7);
}

static void reset_frames(IOUSBLowLatencyIsocFrame *frames,const uint16_t *requests) {
    for(unsigned index=0;index<8;++index) {
        frames[index].frStatus=(IOReturn)kUSBLowLatencyIsochTransferKey;
        frames[index].frReqCount=requests == NULL ? 512 : requests[index];
        frames[index].frActCount=0;
        memset(&frames[index].frTimeStamp,0,sizeof(frames[index].frTimeStamp));
    }
}

static void read_complete(void *context,IOReturn result,void *argument);
static void write_complete(void *context,IOReturn result,void *argument);

static bool submit_input(rk3_native_audio_iso *engine,unsigned slot,uint64_t frame) {
    Transfer *transfer=&engine->inputs[slot];
    reset_frames(engine->input_frames+slot*8,NULL);
    transfer->pending=true;
    ++engine->pending_count;
    IOReturn result=(*engine->interface)->LowLatencyReadIsochPipeAsync(engine->interface,
        engine->input_pipe,engine->input_data+slot*TRANSFER_BYTES,frame,8,0,
        engine->input_frames+slot*8,read_complete,transfer);
    if(result != kIOReturnSuccess) {
        transfer->pending=false;
        --engine->pending_count;
        fail_engine(engine,result);
        return false;
    }
    return true;
}

static void prepare_output_pcm(rk3_native_audio_iso *engine,size_t wire_bytes,bool priming_ended) {
    size_t remaining=engine->encoder.staged_size-engine->encoder.staged_position;
    if(wire_bytes <= remaining) return;
    size_t frames=(wire_bytes-remaining+7)/8;
    size_t first=engine->encoder.frame;
    if(remaining != 0) first=(first+1)%QUEUE_FRAMES;
    for(size_t index=0;index<frames;++index) {
        float sample[2]={0,0};
        if(!engine->schedule.priming && !priming_ended) (void)queue_read(&engine->render_queue,sample,1);
        size_t position=((first+index)%QUEUE_FRAMES)*6;
        (void)ni_audio_pcm24_write(sample[0],NI_AUDIO_BIG_ENDIAN,engine->output_pcm+position);
        (void)ni_audio_pcm24_write(sample[1],NI_AUDIO_BIG_ENDIAN,engine->output_pcm+position+3);
    }
}

static bool submit_output(rk3_native_audio_iso *engine,const NIAudioOutputPlan *plan) {
    unsigned slot=plan->output_slot;
    unsigned count=8;
    while(count > 0 && plan->requested[count-1] == 0) --count;
    engine->output_microframes[slot]=count;
    /* Current IOUSBHost rejects some empty transactions at the end of its
     * exact-length data descriptor. Keep interior gaps and the NI calendar;
     * only omit trailing empty transactions from this API submission. */
    if(count == 0) {
        uint32_t position;
        if(ni_audio_schedule_output_completed(&engine->schedule,slot,&position) != NI_AUDIO_SCHEDULE_OK) {
            fail_engine(engine,kIOReturnInternalError);return false;
        }
        return true;
    }
    prepare_output_pcm(engine,plan->total_bytes,plan->priming_ended);
    const uint8_t *rings[]={engine->output_pcm};
    if(ni_audio_encode(&engine->encoder,rings,PACKED_BYTES,
                      engine->output_data+slot*TRANSFER_BYTES,plan->total_bytes) != NI_AUDIO_OK) {
        fail_engine(engine,kIOReturnBadArgument);return false;
    }
    reset_frames(engine->output_frames+slot*8,plan->requested);
    Transfer *transfer=&engine->outputs[slot];
    transfer->pending=true;
    ++engine->pending_count;
    engine->wire_bytes_scheduled+=plan->total_bytes;
    engine->output_sample_end[slot]=engine->wire_bytes_scheduled/8;
    IOReturn result=(*engine->interface)->LowLatencyWriteIsochPipeAsync(engine->interface,
        engine->output_pipe,engine->output_data+slot*TRANSFER_BYTES,plan->output_frame,count,1,
        engine->output_frames+slot*8,write_complete,transfer);
    if(result != kIOReturnSuccess) {
        transfer->pending=false;
        --engine->pending_count;
        fail_engine(engine,result);return false;
    }
    return true;
}

static void consume_ready_input(rk3_native_audio_iso *engine) {
    if(engine->schedule.priming) return;
    uint8_t *rings[]={engine->input_pcm};
    for(unsigned visited=0;visited<512;++visited) {
        unsigned slot=engine->read_slot,microframe=engine->read_microframe;
        volatile IOUSBLowLatencyIsocFrame *frame=engine->input_frames+slot*8+microframe;
        IOReturn status=frame->frStatus;
        if((uint32_t)status == (uint32_t)kUSBLowLatencyIsochTransferKey) return;
        atomic_thread_fence(memory_order_acquire);
        size_t count=frame->frActCount;
        if(count > 512) {fail_engine(engine,kIOReturnOverrun);return;}
        if(accepted_status(status) && count != 0) {
            if(ni_audio_decode(&engine->decoder,
                engine->input_data+slot*TRANSFER_BYTES+microframe*512,count,rings,PACKED_BYTES) != NI_AUDIO_OK) {
                fail_engine(engine,kIOReturnBadArgument);return;
            }
            if(engine->decoder.errors != 0) {fail_engine(engine,kIOReturnIOError);return;}
            while(engine->capture_next_frame < engine->decoder.completed_frames) {
                size_t position=(size_t)(engine->capture_next_frame%QUEUE_FRAMES)*6;
                float sample[2];
                (void)ni_audio_pcm24_read(engine->input_pcm+position,NI_AUDIO_BIG_ENDIAN,&sample[0]);
                (void)ni_audio_pcm24_read(engine->input_pcm+position+3,NI_AUDIO_BIG_ENDIAN,&sample[1]);
                (void)queue_write(&engine->capture_queue,sample,1);
                ++engine->capture_next_frame;
            }
        }
        if(++engine->read_microframe == 8) {
            engine->read_microframe=0;
            engine->read_slot=(engine->read_slot+1)%64;
        }
    }
}

static void read_complete(void *context,IOReturn result,void *argument) {
    Transfer *transfer=context;
    rk3_native_audio_iso *engine=transfer->engine;
    if(!transfer->pending) {fail_engine(engine,kIOReturnInternalError);return;}
    transfer->pending=false;
    --engine->pending_count;
    if(!atomic_load(&engine->running)) return;
    if(argument != engine->input_frames+transfer->slot*8 || !accepted_status(result)) {
        fail_engine(engine,result == kIOReturnSuccess ? kIOReturnBadArgument : result);return;
    }
    consume_ready_input(engine);
    if(!atomic_load(&engine->running)) return;
    NIAudioPacketResult packets[8];
    for(unsigned index=0;index<8;++index) {
        IOUSBLowLatencyIsocFrame *frame=engine->input_frames+transfer->slot*8+index;
        packets[index].status=frame->frStatus;
        packets[index].actual_bytes=frame->frActCount;
    }
    NIAudioOutputPlan plan;
    if(ni_audio_schedule_input_completed(&engine->schedule,transfer->slot,packets,&plan) != NI_AUDIO_SCHEDULE_OK) {
        fail_engine(engine,kIOReturnNoResources);return;
    }
    /* Snapshot counts, then requeue IN before OUT: this determines NI's frame lead. */
    if(!submit_input(engine,transfer->slot,plan.requeued_input_frame)) return;
    if(!submit_output(engine,&plan)) return;
    if(plan.priming_ended) {
        engine->read_slot=0;engine->read_microframe=0;
        (void)ni_audio_decoder_set_priming(&engine->decoder,false);
        consume_ready_input(engine);
    }
}

static void write_complete(void *context,IOReturn result,void *argument) {
    Transfer *transfer=context;
    rk3_native_audio_iso *engine=transfer->engine;
    if(!transfer->pending) {fail_engine(engine,kIOReturnInternalError);return;}
    transfer->pending=false;
    --engine->pending_count;
    uint32_t position;
    (void)ni_audio_schedule_output_completed(&engine->schedule,transfer->slot,&position);
    if(!atomic_load(&engine->running)) return;
    if(argument != engine->output_frames+transfer->slot*8 || !accepted_status(result)) {
        fail_engine(engine,result == kIOReturnSuccess ? kIOReturnBadArgument : result);return;
    }
    uint64_t sample=engine->output_sample_end[transfer->slot];
    uint64_t host=0;
    IOUSBLowLatencyIsocFrame *frames=engine->output_frames+transfer->slot*8;
    for(unsigned index=0;index<engine->output_microframes[transfer->slot];++index) {
        if(!accepted_status(frames[index].frStatus) || frames[index].frActCount != frames[index].frReqCount) {
            fail_engine(engine,kIOReturnIOError);return;
        }
        uint64_t stamp=0;
        _Static_assert(sizeof(stamp) == sizeof(frames[index].frTimeStamp),"USB timestamp size");
        memcpy(&stamp,&frames[index].frTimeStamp,sizeof(stamp));
        if(stamp != 0) host=stamp;
    }
    if(host == 0) host=mach_absolute_time();
    if(sample < atomic_load(&engine->clock_frame)) return;
    atomic_fetch_add_explicit(&engine->clock_sequence,1,memory_order_acq_rel);
    atomic_store_explicit(&engine->clock_frame,sample,memory_order_relaxed);
    atomic_store_explicit(&engine->clock_host,host,memory_order_relaxed);
    atomic_fetch_add_explicit(&engine->clock_sequence,1,memory_order_release);
}

static bool allocate_buffer(rk3_native_audio_iso *engine,void **buffer,size_t size,uint32_t type) {
    return (*engine->interface)->LowLatencyCreateBuffer(engine->interface,buffer,size,type) == kIOReturnSuccess;
}

static IOReturn validate_audio_pipe(rk3_native_audio_iso *engine,uint8_t pipe,
    uint8_t expected_direction,uint8_t expected_number) {
    UInt8 direction=0,number=0,type=0,interval=0;UInt16 capacity=0;
    IOReturn result=(*engine->interface)->GetPipeProperties(engine->interface,pipe,
        &direction,&number,&type,&capacity,&interval);
    if(result != kIOReturnSuccess) return result;
    return direction == expected_direction && number == expected_number &&
        type == kUSBIsoc && capacity == 512 && interval == 1 ?
        kIOReturnSuccess : kIOReturnBadArgument;
}

rk3_native_audio_iso *rk3_native_audio_iso_create(IOUSBInterfaceInterface300 **interface,
    uint8_t input_pipe,uint8_t output_pipe,rk3_native_audio_command command,void *context) {
    if(interface == NULL || input_pipe == 0 || output_pipe == 0 || command == NULL) return NULL;
    rk3_native_audio_iso *engine=calloc(1,sizeof(*engine));
    if(engine == NULL) return NULL;
    engine->interface=interface;engine->input_pipe=input_pipe;engine->output_pipe=output_pipe;
    (*interface)->AddRef(interface);
    engine->command=command;engine->command_context=context;
    atomic_flag_clear(&engine->render_busy);atomic_flag_clear(&engine->capture_busy);
    atomic_init(&engine->running,false);atomic_init(&engine->error,0);atomic_init(&engine->active_calls,0);
    atomic_init(&engine->clock_seed,mach_absolute_time());
    atomic_init(&engine->clock_sequence,0);atomic_init(&engine->clock_frame,0);atomic_init(&engine->clock_host,0);
    atomic_init(&engine->render_queue.head,0);atomic_init(&engine->render_queue.tail,0);
    atomic_init(&engine->capture_queue.head,0);atomic_init(&engine->capture_queue.tail,0);
    if(!allocate_buffer(engine,(void **)&engine->input_data,64*TRANSFER_BYTES,kUSBLowLatencyReadBuffer) ||
       !allocate_buffer(engine,(void **)&engine->output_data,128*TRANSFER_BYTES,kUSBLowLatencyWriteBuffer) ||
       !allocate_buffer(engine,(void **)&engine->input_frames,64*8*sizeof(*engine->input_frames),kUSBLowLatencyFrameListBuffer) ||
       !allocate_buffer(engine,(void **)&engine->output_frames,128*8*sizeof(*engine->output_frames),kUSBLowLatencyFrameListBuffer)) {
        (void)rk3_native_audio_iso_destroy(engine);return NULL;
    }
    for(unsigned slot=0;slot<64;++slot) {engine->inputs[slot].engine=engine;engine->inputs[slot].slot=slot;}
    for(unsigned slot=0;slot<128;++slot) {engine->outputs[slot].engine=engine;engine->outputs[slot].slot=slot;}
    return engine;
}

int32_t rk3_native_audio_iso_start(rk3_native_audio_iso *engine) {
    if(engine == NULL) return kIOReturnBadArgument;
    if(engine->usb_released) return kIOReturnNoDevice;
    if(atomic_load(&engine->running)) return kIOReturnSuccess;
    if(!rk3_native_audio_iso_is_quiescent(engine)) return kIOReturnBusy;
    /* Retain the descriptor's existing bandwidth allocation. The current host
     * rejects legacy SetPipePolicy even for its descriptor size; original NI
     * code did not check that call's result. Validate the actual pipes instead. */
    IOReturn result=validate_audio_pipe(engine,engine->input_pipe,kUSBIn,2);
    if(result != kIOReturnSuccess) return result;
    result=validate_audio_pipe(engine,engine->output_pipe,kUSBOut,6);
    if(result != kIOReturnSuccess) return result;
    const uint8_t stop[]={9,255,255,136,0,1},start[]={9,1,2,136,0,1};
    if(!engine->command(engine->command_context,stop,sizeof(stop),1200)) return kIOReturnIOError;
    UInt64 frame=0;AbsoluteTime stamp;
    result=(*engine->interface)->GetBusFrameNumber(engine->interface,&frame,&stamp);
    if(result != kIOReturnSuccess) return result;
    if(ni_audio_schedule_init(&engine->schedule,frame,2,1) != NI_AUDIO_SCHEDULE_OK ||
       ni_audio_encoder_init(&engine->encoder,NI_AUDIO_MODE_2,1,PACKED_BYTES) != NI_AUDIO_OK ||
       ni_audio_decoder_init(&engine->decoder,NI_AUDIO_MODE_2,1,PACKED_BYTES,true) != NI_AUDIO_OK) return kIOReturnBadArgument;
    memset(engine->output_pcm,0,PACKED_BYTES);memset(engine->input_pcm,0,PACKED_BYTES);
    memset(engine->input_data,0,64*TRANSFER_BYTES);memset(engine->output_data,0,128*TRANSFER_BYTES);
    engine->read_slot=0;engine->read_microframe=0;engine->capture_next_frame=0;engine->wire_bytes_scheduled=0;
    atomic_store(&engine->render_queue.head,0);atomic_store(&engine->render_queue.tail,0);
    atomic_store(&engine->capture_queue.head,0);atomic_store(&engine->capture_queue.tail,0);
    atomic_store(&engine->error,0);atomic_fetch_add(&engine->clock_seed,1);
    atomic_store(&engine->clock_host,0);atomic_store(&engine->clock_frame,0);
    atomic_store(&engine->running,true);
    for(unsigned slot=0;slot<64;++slot) {
        uint64_t scheduled=0;
        if(ni_audio_schedule_begin_input(&engine->schedule,slot,&scheduled) != NI_AUDIO_SCHEDULE_OK ||
           !submit_input(engine,slot,scheduled)) {
            fail_engine(engine,kIOReturnIOError);return atomic_load(&engine->error);
        }
    }
    for(unsigned attempt=0;attempt<5;++attempt) {
        if(engine->command(engine->command_context,start,sizeof(start),1200))
            return atomic_load(&engine->running) ? 0 : atomic_load(&engine->error);
        if(!atomic_load(&engine->running)) return atomic_load(&engine->error);
        if(attempt != 4) {
            struct timespec now;
            if(clock_gettime(CLOCK_MONOTONIC,&now) != 0) {
                fail_engine(engine,kIOReturnIOError);return kIOReturnIOError;
            }
            double deadline=(double)now.tv_sec+(double)now.tv_nsec/1e9+0.2;
            while(atomic_load(&engine->running)) {
                (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,true);
                if(clock_gettime(CLOCK_MONOTONIC,&now) != 0 ||
                   (double)now.tv_sec+(double)now.tv_nsec/1e9 >= deadline) break;
            }
        }
    }
    fail_engine(engine,kIOReturnIOError);return kIOReturnIOError;
}

void rk3_native_audio_iso_stop(rk3_native_audio_iso *engine) {
    if(engine == NULL) return;
    atomic_store(&engine->running,false);
    if(engine->usb_released) return;
    (void)(*engine->interface)->AbortPipe(engine->interface,engine->input_pipe);
    (void)(*engine->interface)->AbortPipe(engine->interface,engine->output_pipe);
}

size_t rk3_native_audio_iso_pending(const rk3_native_audio_iso *engine) {return engine == NULL ? 0 : engine->pending_count;}
bool rk3_native_audio_iso_is_quiescent(const rk3_native_audio_iso *engine) {
    return engine == NULL || (engine->pending_count == 0 && atomic_load(&engine->active_calls) == 0);
}

bool rk3_native_audio_iso_release_usb(rk3_native_audio_iso *engine) {
    if(engine == NULL || engine->usb_released) return true;
    if(atomic_load(&engine->running) || engine->pending_count != 0) return false;
    void **buffers[]={(void **)&engine->input_data,(void **)&engine->output_data,
                      (void **)&engine->input_frames,(void **)&engine->output_frames};
    for(unsigned index=0;index<4;++index) {
        if(*buffers[index] != NULL) {
            IOReturn result=(*engine->interface)->LowLatencyDestroyBuffer(engine->interface,*buffers[index]);
            if(result != kIOReturnSuccess) {
                int_least32_t expected=0;
                (void)atomic_compare_exchange_strong(&engine->error,&expected,result);
            }
            *buffers[index]=NULL;
        }
    }
    engine->usb_released=true;
    return true;
}

bool rk3_native_audio_iso_destroy(rk3_native_audio_iso *engine) {
    if(engine == NULL) return true;
    if(atomic_load(&engine->running) || !rk3_native_audio_iso_is_quiescent(engine)) return false;
    if(!rk3_native_audio_iso_release_usb(engine)) return false;
    (*engine->interface)->Release(engine->interface);
    free(engine);return true;
}

int32_t rk3_native_audio_iso_error(const rk3_native_audio_iso *engine) {return engine == NULL ? kIOReturnBadArgument : atomic_load(&engine->error);}
bool rk3_native_audio_iso_running(const rk3_native_audio_iso *engine) {return engine != NULL && atomic_load(&engine->running);}

size_t rk3_native_audio_iso_render(rk3_native_audio_iso *engine,const float *samples,size_t frames) {
    if(engine == NULL || samples == NULL) return 0;
    atomic_fetch_add(&engine->active_calls,1);
    size_t accepted=0;
    if(atomic_load(&engine->running) && !atomic_flag_test_and_set_explicit(&engine->render_busy,memory_order_acquire)) {
        accepted=queue_write(&engine->render_queue,samples,frames);
        atomic_flag_clear_explicit(&engine->render_busy,memory_order_release);
    }
    atomic_fetch_sub(&engine->active_calls,1);return accepted;
}

size_t rk3_native_audio_iso_capture(rk3_native_audio_iso *engine,float *samples,size_t frames) {
    if(engine == NULL || samples == NULL) return 0;
    atomic_fetch_add(&engine->active_calls,1);
    size_t captured=0;
    if(atomic_load(&engine->running) && !atomic_flag_test_and_set_explicit(&engine->capture_busy,memory_order_acquire)) {
        captured=queue_read(&engine->capture_queue,samples,frames);
        atomic_flag_clear_explicit(&engine->capture_busy,memory_order_release);
    }
    atomic_fetch_sub(&engine->active_calls,1);return captured;
}

bool rk3_native_audio_iso_clock(rk3_native_audio_iso *engine,rk3_native_audio_clock *snapshot) {
    if(engine == NULL || snapshot == NULL) return false;
    atomic_fetch_add(&engine->active_calls,1);
    bool valid=false;
    for(unsigned attempt=0;attempt<3 && atomic_load(&engine->running);++attempt) {
        uint64_t sequence=atomic_load_explicit(&engine->clock_sequence,memory_order_acquire);
        if((sequence&1) != 0) continue;
        rk3_native_audio_clock result={atomic_load(&engine->clock_frame),atomic_load(&engine->clock_host),atomic_load(&engine->clock_seed)};
        if(sequence == atomic_load_explicit(&engine->clock_sequence,memory_order_acquire) && result.host_time != 0) {
            *snapshot=result;valid=true;break;
        }
    }
    atomic_fetch_sub(&engine->active_calls,1);return valid;
}
