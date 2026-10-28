#ifndef RK3_AUDIO_LIFECYCLE_TRACE_STATE_H
#define RK3_AUDIO_LIFECYCLE_TRACE_STATE_H
#include "../AudioLifecycleTrace.h"
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE==2&&ATOMIC_LONG_LOCK_FREE==2&&ATOMIC_LLONG_LOCK_FREE==2,"trace requires lock-free ARM64 words");
typedef struct {
    _Atomic uint64_t key,adds,removes,io_read,io_write,io_other,will_read,will_write,will_other;
    _Atomic int32_t pid;
    _Atomic uint32_t pid_state,will_true_mask,last_read_stream,last_write_stream;
} RK3AudioTraceSlot;
typedef struct {RK3AudioTraceEvent value;_Atomic uint32_t ready;} RK3AudioTraceJournal;
typedef struct {
    _Atomic uint32_t faults;
    _Atomic uint64_t reservations;
    RK3AudioTraceSlot clients[RK3_AUDIO_TRACE_CLIENT_LIMIT];
    RK3AudioTraceJournal events[RK3_AUDIO_TRACE_EVENT_LIMIT];
} RK3AudioTraceState;

/* Quiescent CPU fixtures only. Runtime owns a process-lifetime static BSS
 * instance, and exposes no reset/arming operation. */
static inline void rk3_audio_trace_init(RK3AudioTraceState *s){
    atomic_init(&s->faults,0);atomic_init(&s->reservations,0);
    for(unsigned i=0;i<RK3_AUDIO_TRACE_CLIENT_LIMIT;++i){RK3AudioTraceSlot *c=s->clients+i;
        atomic_init(&c->key,0);atomic_init(&c->adds,0);atomic_init(&c->removes,0);atomic_init(&c->io_read,0);
        atomic_init(&c->io_write,0);atomic_init(&c->io_other,0);atomic_init(&c->will_read,0);atomic_init(&c->will_write,0);atomic_init(&c->will_other,0);
        atomic_init(&c->pid,0);atomic_init(&c->pid_state,0);atomic_init(&c->will_true_mask,0);atomic_init(&c->last_read_stream,0);atomic_init(&c->last_write_stream,0);}
    for(unsigned i=0;i<RK3_AUDIO_TRACE_EVENT_LIMIT;++i){memset(&s->events[i].value,0,sizeof(s->events[i].value));atomic_init(&s->events[i].ready,0);}
}
static inline void rk3_audio_trace_fault(RK3AudioTraceState *s,uint32_t fault){atomic_fetch_or_explicit(&s->faults,fault,memory_order_relaxed);}
static inline uint64_t rk3_audio_trace_increment(RK3AudioTraceState *s,_Atomic uint64_t *word){
    uint64_t previous=atomic_fetch_add_explicit(word,1,memory_order_relaxed);
    if(previous==UINT64_MAX)rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_WRAP);return previous;
}
static inline RK3AudioTraceSlot *rk3_audio_trace_slot(RK3AudioTraceState *s,uint32_t cid){
    uint64_t key=(uint64_t)cid+1;
    for(unsigned probe=0;probe<RK3_AUDIO_TRACE_CLIENT_LIMIT;++probe){
        RK3AudioTraceSlot *c=s->clients+((cid+probe)&(RK3_AUDIO_TRACE_CLIENT_LIMIT-1));
        uint64_t found=atomic_load_explicit(&c->key,memory_order_acquire);
        if(found==key)return c;
        if(!found&&atomic_compare_exchange_strong_explicit(&c->key,&found,key,memory_order_acq_rel,memory_order_acquire))return c;
        if(found==key)return c;
    }
    rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_CLIENT_FULL);return NULL;
}
static inline void rk3_audio_trace_io(RK3AudioTraceState *s,uint32_t cid,uint32_t stream,uint32_t category){
    RK3AudioTraceSlot *c=rk3_audio_trace_slot(s,cid);if(!c)return;
    if(category==RK3_AUDIO_TRACE_READ){rk3_audio_trace_increment(s,&c->io_read);atomic_store_explicit(&c->last_read_stream,stream,memory_order_relaxed);}
    else if(category==RK3_AUDIO_TRACE_WRITE){rk3_audio_trace_increment(s,&c->io_write);atomic_store_explicit(&c->last_write_stream,stream,memory_order_relaxed);}
    else {rk3_audio_trace_increment(s,&c->io_other);if(category!=RK3_AUDIO_TRACE_OTHER)rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_BAD_METADATA);}
}
static inline void rk3_audio_trace_will(RK3AudioTraceState *s,uint32_t cid,uint32_t category,bool perform){
    RK3AudioTraceSlot *c=rk3_audio_trace_slot(s,cid);if(!c)return;uint32_t mask;
    if(category==RK3_AUDIO_TRACE_READ){rk3_audio_trace_increment(s,&c->will_read);mask=1;}
    else if(category==RK3_AUDIO_TRACE_WRITE){rk3_audio_trace_increment(s,&c->will_write);mask=2;}
    else {rk3_audio_trace_increment(s,&c->will_other);mask=4;if(category!=RK3_AUDIO_TRACE_OTHER)rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_BAD_METADATA);}
    if(perform)atomic_fetch_or_explicit(&c->will_true_mask,mask,memory_order_relaxed);
}
static inline RK3AudioTraceJournal *rk3_audio_trace_reserve(RK3AudioTraceState *s,uint64_t *reservation){
    uint64_t next=atomic_load_explicit(&s->reservations,memory_order_relaxed);
    /* Strong-CAS failures imply another reservation advanced the monotonic,
     * capped word: at most 256 failures, never an unbounded wait or wrap. */
    for(unsigned attempt=0;attempt<=RK3_AUDIO_TRACE_EVENT_LIMIT;++attempt){
        if(next>=RK3_AUDIO_TRACE_EVENT_LIMIT){rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_EVENT_FULL|(next==UINT64_MAX?RK3_AUDIO_TRACE_FAULT_WRAP:0));return NULL;}
        if(atomic_compare_exchange_strong_explicit(&s->reservations,&next,next+1,memory_order_relaxed,memory_order_relaxed)){
            *reservation=next;return s->events+next;}
    }
    rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_EVENT_FULL);return NULL;
}
static inline void rk3_audio_trace_record(RK3AudioTraceState *s,uint32_t kind,uint32_t cid,bool client_present,
    int32_t pid,bool pid_present,uint32_t object,uint32_t value,int32_t status,uint64_t hosttime){
    if(kind<RK3_AUDIO_TRACE_EVENT_ADD||kind>RK3_AUDIO_TRACE_EVENT_ACTIVITY)rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_BAD_METADATA);
    if((kind==RK3_AUDIO_TRACE_EVENT_ADD||kind==RK3_AUDIO_TRACE_EVENT_REMOVE)&&!client_present)rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_BAD_METADATA);
    RK3AudioTraceSlot *c=client_present?rk3_audio_trace_slot(s,cid):NULL;
    bool known=pid_present,publishing_seen=false;int32_t actual_pid=pid;
    if(c){
        if(kind==RK3_AUDIO_TRACE_EVENT_ADD){uint64_t adds=rk3_audio_trace_increment(s,&c->adds);
            if(adds)rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_REUSE);
            if(!adds&&pid_present){uint32_t unknown=RK3_AUDIO_TRACE_PID_UNKNOWN;
                if(atomic_compare_exchange_strong_explicit(&c->pid_state,&unknown,RK3_AUDIO_TRACE_PID_PUBLISHING,memory_order_relaxed,memory_order_relaxed)){
                    atomic_store_explicit(&c->pid,pid,memory_order_relaxed);atomic_store_explicit(&c->pid_state,RK3_AUDIO_TRACE_PID_READY,memory_order_release);
                }else if(unknown==RK3_AUDIO_TRACE_PID_PUBLISHING)publishing_seen=true;
                else rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_REUSE);}}
        if(kind==RK3_AUDIO_TRACE_EVENT_REMOVE)rk3_audio_trace_increment(s,&c->removes);
        uint32_t state=atomic_load_explicit(&c->pid_state,memory_order_acquire);
        if(state==RK3_AUDIO_TRACE_PID_PUBLISHING||publishing_seen)known=false;
        else if(state==RK3_AUDIO_TRACE_PID_READY){int32_t first_pid=atomic_load_explicit(&c->pid,memory_order_relaxed);
            if(pid_present&&pid!=first_pid)rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_REUSE);
            if(!pid_present){actual_pid=first_pid;known=true;}}
        else if(state!=RK3_AUDIO_TRACE_PID_UNKNOWN){rk3_audio_trace_fault(s,RK3_AUDIO_TRACE_FAULT_BAD_METADATA);known=false;}
    }
    uint64_t reservation=0;RK3AudioTraceJournal *event=rk3_audio_trace_reserve(s,&reservation);if(!event)return;
    RK3AudioTraceEvent entry={0};entry.reservation=reservation;entry.host_time=hosttime;entry.ready=1;entry.kind=kind;
    entry.client_id=client_present?cid:0;entry.identity_flags=(client_present?RK3_AUDIO_TRACE_IDENTITY_CLIENT:0)|(known?RK3_AUDIO_TRACE_IDENTITY_PID:0);
    entry.pid=known?actual_pid:0;entry.status=status;entry.object_id=object;entry.value=value;
    if(c){entry.io_read=atomic_load_explicit(&c->io_read,memory_order_relaxed);entry.io_write=atomic_load_explicit(&c->io_write,memory_order_relaxed);}
    event->value=entry;atomic_store_explicit(&event->ready,1,memory_order_release);
}
static inline void rk3_audio_trace_snapshot(const RK3AudioTraceState *s,RK3AudioTraceSnapshot *out){
    uint64_t begin=atomic_load_explicit(&s->reservations,memory_order_relaxed);
    uint32_t faults=atomic_load_explicit(&s->faults,memory_order_relaxed);
    memset(out,0,sizeof(*out));out->version=RK3_AUDIO_TRACE_VERSION;out->build=RK3_AUDIO_TRACE_BUILD;
    out->client_limit=RK3_AUDIO_TRACE_CLIENT_LIMIT;out->event_limit=RK3_AUDIO_TRACE_EVENT_LIMIT;
    out->faults=faults;out->events_begin=begin<RK3_AUDIO_TRACE_EVENT_LIMIT?begin:RK3_AUDIO_TRACE_EVENT_LIMIT;
    for(unsigned i=0;i<RK3_AUDIO_TRACE_CLIENT_LIMIT;++i){const RK3AudioTraceSlot *c=s->clients+i;RK3AudioTraceClient *v=out->clients+i;
        v->key=atomic_load_explicit(&c->key,memory_order_acquire);if(!v->key)continue;++out->used_clients;
        if(v->key>(uint64_t)UINT32_MAX+1)out->faults|=RK3_AUDIO_TRACE_FAULT_BAD_METADATA;
        v->pid_state=atomic_load_explicit(&c->pid_state,memory_order_acquire);
        if(v->pid_state==RK3_AUDIO_TRACE_PID_READY)v->pid=atomic_load_explicit(&c->pid,memory_order_relaxed);
        else if(v->pid_state==RK3_AUDIO_TRACE_PID_PUBLISHING)++out->pending_pids;
        else if(v->pid_state!=RK3_AUDIO_TRACE_PID_UNKNOWN)out->faults|=RK3_AUDIO_TRACE_FAULT_BAD_METADATA;
        v->adds=atomic_load_explicit(&c->adds,memory_order_relaxed);v->removes=atomic_load_explicit(&c->removes,memory_order_relaxed);
        v->io_read=atomic_load_explicit(&c->io_read,memory_order_relaxed);v->io_write=atomic_load_explicit(&c->io_write,memory_order_relaxed);v->io_other=atomic_load_explicit(&c->io_other,memory_order_relaxed);
        v->will_read=atomic_load_explicit(&c->will_read,memory_order_relaxed);v->will_write=atomic_load_explicit(&c->will_write,memory_order_relaxed);v->will_other=atomic_load_explicit(&c->will_other,memory_order_relaxed);
        v->will_true_mask=atomic_load_explicit(&c->will_true_mask,memory_order_relaxed);v->last_read_stream=atomic_load_explicit(&c->last_read_stream,memory_order_relaxed);v->last_write_stream=atomic_load_explicit(&c->last_write_stream,memory_order_relaxed);
    }
    for(uint64_t i=0;i<out->events_begin;++i){const RK3AudioTraceJournal *event=s->events+i;
        if(atomic_load_explicit(&event->ready,memory_order_acquire))out->events[i]=event->value;
        else {out->events[i].reservation=i;++out->pending_events;}}
#ifdef RK3_AUDIO_TRACE_SNAPSHOT_TEST_HOOK
    RK3_AUDIO_TRACE_SNAPSHOT_TEST_HOOK(s);
#endif
    uint64_t end=atomic_load_explicit(&s->reservations,memory_order_relaxed);
    out->events_end=end<RK3_AUDIO_TRACE_EVENT_LIMIT?end:RK3_AUDIO_TRACE_EVENT_LIMIT;
    out->faults|=atomic_load_explicit(&s->faults,memory_order_relaxed);
}
#endif
