#include <stdbool.h>
static bool publish_during_snapshot;
#define RK3_AUDIO_TRACE_SNAPSHOT_TEST_HOOK(s) do {if(publish_during_snapshot){publish_during_snapshot=false; \
    rk3_audio_trace_record((RK3AudioTraceState *)(s),RK3_AUDIO_TRACE_EVENT_ACTIVITY,0,false,0,false,4,77,0,123);}} while(0)
#include "../Audio/AudioLifecycleTraceState.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static RK3AudioTraceState state;
static RK3AudioTraceSnapshot snapshot;
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static RK3AudioTraceClient *client(uint32_t id){
    for(unsigned i=0;i<RK3_AUDIO_TRACE_CLIENT_LIMIT;++i)if(snapshot.clients[i].key==(uint64_t)id+1)return snapshot.clients+i;
    return NULL;
}
static void basic(void){
    rk3_audio_trace_init(&state);CHECK(atomic_is_lock_free(&state.reservations)&&atomic_is_lock_free(&state.faults)&&atomic_is_lock_free(&state.clients[0].pid)&&atomic_is_lock_free(&state.clients[0].io_read));
    rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ADD,0,true,0,true,2,0,0,10);
    rk3_audio_trace_io(&state,0,3,RK3_AUDIO_TRACE_READ);rk3_audio_trace_io(&state,0,4,RK3_AUDIO_TRACE_WRITE);rk3_audio_trace_io(&state,0,9,RK3_AUDIO_TRACE_OTHER);
    rk3_audio_trace_will(&state,0,RK3_AUDIO_TRACE_READ,true);rk3_audio_trace_will(&state,0,RK3_AUDIO_TRACE_WRITE,true);rk3_audio_trace_will(&state,0,RK3_AUDIO_TRACE_OTHER,false);
    rk3_audio_trace_snapshot(&state,&snapshot);RK3AudioTraceClient *c=client(0);
    CHECK(snapshot.version==1&&snapshot.build==17&&snapshot.client_limit==64&&snapshot.event_limit==256&&sizeof(snapshot)==22576);
    CHECK(c&&c->key==1&&c->pid==0&&c->pid_state==2&&c->adds==1&&c->io_read==1&&c->io_write==1&&c->io_other==1);
    CHECK(c->will_read==1&&c->will_write==1&&c->will_other==1&&c->will_true_mask==3&&c->last_read_stream==3&&c->last_write_stream==4&&!c->reserved);
    CHECK(snapshot.events[0].identity_flags==3&&snapshot.events[0].pid==0&&snapshot.events[0].ready==1&&snapshot.events_begin==1&&snapshot.events_end==1&&!snapshot.faults);
    rk3_audio_trace_will(&state,0,RK3_AUDIO_TRACE_OTHER,true);rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_REMOVE,0,true,0,true,2,0,-17,11);
    rk3_audio_trace_snapshot(&state,&snapshot);CHECK(client(0)->will_true_mask==7&&client(0)->removes==1&&snapshot.events[1].status==-17&&snapshot.events[1].io_read==1);
    rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ADD,0,true,99,true,2,0,0,12);rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK((snapshot.faults&RK3_AUDIO_TRACE_FAULT_REUSE)&&client(0)->pid==0&&client(0)->adds==2&&snapshot.events[2].pid==99);
    rk3_audio_trace_init(&state);rk3_audio_trace_io(&state,UINT32_MAX,8,RK3_AUDIO_TRACE_READ);rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK(client(UINT32_MAX)&&client(UINT32_MAX)->key==UINT64_C(4294967296)&&client(UINT32_MAX)->pid_state==0&&!snapshot.faults);
    rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ACTIVITY,0,false,501,true,4,0,0,13);rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK(snapshot.events[0].identity_flags==RK3_AUDIO_TRACE_IDENTITY_PID&&snapshot.events[0].client_id==0&&snapshot.events[0].pid==501);
}
static void bounds(void){
    rk3_audio_trace_init(&state);for(unsigned i=0;i<64;++i)rk3_audio_trace_io(&state,i*64,3,RK3_AUDIO_TRACE_READ);
    rk3_audio_trace_io(&state,4096,3,RK3_AUDIO_TRACE_READ);rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK(snapshot.used_clients==64&&(snapshot.faults&RK3_AUDIO_TRACE_FAULT_CLIENT_FULL));
    for(unsigned i=0;i<64;++i)CHECK(client(i*64)&&client(i*64)->io_read==1);
    rk3_audio_trace_init(&state);for(unsigned i=0;i<256;++i)rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ACTIVITY,0,false,1,true,4,i,0,100+i);
    RK3AudioTraceEvent first=state.events[0].value;rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ACTIVITY,0,false,1,true,4,999,0,999);
    rk3_audio_trace_snapshot(&state,&snapshot);CHECK(snapshot.events_end==256&&!snapshot.pending_events&&(snapshot.faults&RK3_AUDIO_TRACE_FAULT_EVENT_FULL));
    CHECK(!memcmp(&snapshot.events[0],&first,sizeof(first)));for(unsigned i=0;i<256;++i)CHECK(snapshot.events[i].reservation==i&&snapshot.events[i].value==i&&snapshot.events[i].ready==1);
    rk3_audio_trace_init(&state);RK3AudioTraceSlot *c=rk3_audio_trace_slot(&state,7);atomic_store(&c->io_read,UINT64_MAX);rk3_audio_trace_io(&state,7,3,RK3_AUDIO_TRACE_READ);
    rk3_audio_trace_snapshot(&state,&snapshot);CHECK((snapshot.faults&RK3_AUDIO_TRACE_FAULT_WRAP)&&client(7)->io_read==0);
    rk3_audio_trace_init(&state);atomic_store(&state.reservations,UINT64_MAX);rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ACTIVITY,0,false,0,false,4,0,0,0);
    CHECK(atomic_load(&state.reservations)==UINT64_MAX&&!atomic_load(&state.events[0].ready));
    rk3_audio_trace_snapshot(&state,&snapshot);CHECK((snapshot.faults&RK3_AUDIO_TRACE_FAULT_WRAP)&&snapshot.pending_events==256);
}
static void partial(void){
    rk3_audio_trace_init(&state);RK3AudioTraceSlot *c=rk3_audio_trace_slot(&state,6);atomic_store(&c->pid_state,1);
    rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ADD,6,true,501,true,2,0,0,20);rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK(snapshot.pending_pids==1&&client(6)->pid_state==1&&client(6)->pid==0&&!snapshot.faults&&snapshot.events[0].identity_flags==1);
    atomic_store(&c->pid,501);atomic_store_explicit(&c->pid_state,2,memory_order_release);rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK(!snapshot.pending_pids&&client(6)->pid==501&&client(6)->pid_state==2&&snapshot.events[0].identity_flags==1);
    rk3_audio_trace_init(&state);atomic_store(&state.reservations,2);state.events[0].value=(RK3AudioTraceEvent){.reservation=0,.kind=999,.value=999};
    state.events[1].value=(RK3AudioTraceEvent){.reservation=1,.ready=1,.kind=7,.value=42};atomic_store_explicit(&state.events[1].ready,1,memory_order_release);
    rk3_audio_trace_snapshot(&state,&snapshot);CHECK(snapshot.pending_events==1&&!snapshot.events[0].ready&&!snapshot.events[0].kind&&snapshot.events[1].value==42);
    rk3_audio_trace_init(&state);rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ADD,0,false,0,false,2,0,0,0);rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK((snapshot.faults&RK3_AUDIO_TRACE_FAULT_BAD_METADATA)&&!snapshot.used_clients&&snapshot.events[0].identity_flags==0);
    rk3_audio_trace_init(&state);rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ADD,1,true,0,false,2,0,0,0);rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK(!snapshot.faults&&client(1)->pid_state==0&&snapshot.events[0].identity_flags==1);
    rk3_audio_trace_io(&state,1,0,99);rk3_audio_trace_snapshot(&state,&snapshot);CHECK(snapshot.faults&RK3_AUDIO_TRACE_FAULT_BAD_METADATA);
    rk3_audio_trace_init(&state);rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ACTIVITY,0,false,0,false,4,76,0,122);
    publish_during_snapshot=true;rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK(snapshot.events_begin==1&&snapshot.events_end==2&&!snapshot.pending_events&&snapshot.events[0].ready&&!snapshot.events[1].ready);
    CHECK(snapshot.events_begin!=snapshot.events_end); /* Explicitly incomplete even with no reserved-prefix holes. */
    rk3_audio_trace_snapshot(&state,&snapshot);CHECK(snapshot.events_begin==2&&snapshot.events_end==2&&snapshot.events[1].value==77&&snapshot.events[1].ready);
}
typedef struct {unsigned index;_Atomic unsigned *done;} ThreadArgs;
static void *worker(void *opaque){ThreadArgs *a=opaque;
    rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_ADD,a->index*64,true,(int32_t)(501+a->index),true,2,0,0,100+a->index);
    for(unsigned i=0;i<10000;++i){rk3_audio_trace_io(&state,0,3,RK3_AUDIO_TRACE_READ);rk3_audio_trace_will(&state,a->index*64,RK3_AUDIO_TRACE_WRITE,true);}
    for(unsigned i=0;i<40;++i)rk3_audio_trace_record(&state,RK3_AUDIO_TRACE_EVENT_START_EXIT,a->index*64,true,0,false,2,i,0,1000+a->index*40+i);
    atomic_fetch_add_explicit(a->done,1,memory_order_release);return NULL;
}
static void concurrent(void){
    rk3_audio_trace_init(&state);_Atomic unsigned done=0;pthread_t threads[4];ThreadArgs args[4];
    for(unsigned i=0;i<4;++i){args[i]=(ThreadArgs){i,&done};CHECK(!pthread_create(threads+i,NULL,worker,args+i));}
    while(atomic_load_explicit(&done,memory_order_acquire)<4){rk3_audio_trace_snapshot(&state,&snapshot);CHECK(snapshot.events_end>=snapshot.events_begin);
        for(uint64_t i=snapshot.events_begin;i<snapshot.events_end;++i)CHECK(!snapshot.events[i].ready);
        for(unsigned i=0;i<snapshot.events_end;++i)if(snapshot.events[i].ready){RK3AudioTraceEvent *e=snapshot.events+i;unsigned index=e->client_id/64;
            CHECK(e->reservation==i&&e->identity_flags==3&&e->client_id%64==0&&index<4&&e->pid==(int32_t)(501+index)&&e->object_id==2&&!e->status);
            CHECK(e->kind==RK3_AUDIO_TRACE_EVENT_ADD?e->host_time==100+index&&e->value==0:
                e->kind==RK3_AUDIO_TRACE_EVENT_START_EXIT&&e->value<40&&e->host_time==1000+index*40+e->value);}}
    for(unsigned i=0;i<4;++i)CHECK(!pthread_join(threads[i],NULL));rk3_audio_trace_snapshot(&state,&snapshot);
    CHECK(!snapshot.faults&&!snapshot.pending_pids&&!snapshot.pending_events&&snapshot.events_end==164&&snapshot.used_clients==4&&client(0)->io_read==40000);
    for(unsigned i=0;i<4;++i)CHECK(client(i*64)->pid==(int32_t)(501+i)&&client(i*64)->will_write==10000&&client(i*64)->adds==1);
}
int main(void){basic();bounds();partial();concurrent();printf("AudioLifecycleTraceTests PASS checks=%u; CPU only, no CoreAudio/USB calls\n",checks);return 0;}
