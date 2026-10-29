/* Hardware-free transport tests. NativeAudioISO is mocked only in this file;
 * the production driver links the real ISO implementation. */
#include <stdbool.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <CoreFoundation/CoreFoundation.h>
static bool fail_clock_timer_create;
static unsigned clock_timer_create_calls;
static CFRunLoopTimerRef test_clock_timer_create(CFAllocatorRef,CFAbsoluteTime,
    CFTimeInterval,CFOptionFlags,CFIndex,CFRunLoopTimerCallBack,CFRunLoopTimerContext *);
static int owner_test_attr_init(pthread_attr_t *);
static int owner_test_attr_set_qos(pthread_attr_t *,qos_class_t,int);
static int owner_test_attr_destroy(pthread_attr_t *);
static int owner_test_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
static int owner_test_detach(pthread_t);
static int owner_test_mutex_destroy(pthread_mutex_t *);
static int owner_test_cond_destroy(pthread_cond_t *);
static int rate_test_mutex_unlock(pthread_mutex_t *);
static void *owner_test_calloc(size_t,size_t);
static void owner_test_free(void *);
#define CFRunLoopTimerCreate test_clock_timer_create
#define pthread_attr_init owner_test_attr_init
#define pthread_attr_set_qos_class_np owner_test_attr_set_qos
#define pthread_attr_destroy owner_test_attr_destroy
#define pthread_create owner_test_create
#define pthread_detach owner_test_detach
#define pthread_mutex_destroy owner_test_mutex_destroy
#define pthread_cond_destroy owner_test_cond_destroy
#define pthread_mutex_unlock rate_test_mutex_unlock
#define calloc owner_test_calloc
#define free owner_test_free
#define RK3_NATIVE_USB_TESTING 1
#include "../NativeUSB.c"
#undef CFRunLoopTimerCreate
#undef pthread_attr_init
#undef pthread_attr_set_qos_class_np
#undef pthread_attr_destroy
#undef pthread_create
#undef pthread_detach
#undef pthread_mutex_destroy
#undef pthread_cond_destroy
#undef pthread_mutex_unlock
#undef calloc
#undef free
#include <assert.h>

/* Observe the actual transaction's unlock boundary without changing its lock.
 * A changed selection must already have a completed successful request. */
static rk3_usb_session *rate_atomic_session;
static Request *rate_atomic_request;
static uint32_t rate_atomic_previous;
static bool rate_atomic_observed;
static int rate_test_mutex_unlock(pthread_mutex_t *mutex) {
    if(rate_atomic_session&&mutex==&rate_atomic_session->mutex&&
        rate_atomic_session->sample_rate!=rate_atomic_previous) {
        assert(rate_atomic_request&&rate_atomic_request->complete&&rate_atomic_request->result==0);
        assert(rate_atomic_session->active_request==NULL);rate_atomic_observed=true;
    }
    return pthread_mutex_unlock(mutex);
}

/* Exercise the real public open/close lifetime without entering worker_main,
 * which owns hardware discovery. Only its pthread entry is substituted. */
static bool owner_test_active;
static unsigned owner_test_failure;
static rk3_usb_session *owner_test_session;
static pthread_t owner_test_thread;
static atomic_uint owner_attr_inits,owner_attr_sets,owner_attr_destroys,owner_creates,owner_detaches;
static atomic_uint owner_mutex_destroys,owner_cond_destroys,owner_allocations,owner_frees;
static atomic_bool owner_entered;
static _Atomic(void *) monitor_free_target;
static atomic_uint monitor_free_count;
static atomic_int owner_requested_qos,owner_requested_relative,owner_requested_result;
static int owner_test_attr_init(pthread_attr_t *attributes) {
    if(owner_test_active){atomic_fetch_add(&owner_attr_inits,1);if(owner_test_failure==1)return ENOMEM;}
    return pthread_attr_init(attributes);
}
static int owner_test_attr_set_qos(pthread_attr_t *attributes,qos_class_t qos,int relative) {
    if(owner_test_active){
        atomic_fetch_add(&owner_attr_sets,1);
        assert(qos==QOS_CLASS_USER_INTERACTIVE&&relative==0);
        if(owner_test_failure==2)return EINVAL;
    }
    return pthread_attr_set_qos_class_np(attributes,qos,relative);
}
static int owner_test_attr_destroy(pthread_attr_t *attributes) {
    if(owner_test_active)atomic_fetch_add(&owner_attr_destroys,1);
    int result=pthread_attr_destroy(attributes);
    return owner_test_active&&owner_test_failure==4&&result==0?EIO:result;
}
static void *owner_controlled_worker(void *context) {
    rk3_usb_session *session=context;qos_class_t qos=QOS_CLASS_UNSPECIFIED;int relative=99;
    int result=pthread_get_qos_class_np(pthread_self(),&qos,&relative);
    atomic_store(&owner_requested_result,result);atomic_store(&owner_requested_qos,(int)qos);
    atomic_store(&owner_requested_relative,relative);
    pthread_mutex_lock(&session->mutex);atomic_store(&owner_entered,true);pthread_cond_broadcast(&session->wake);
    while(!atomic_load(&session->closing))pthread_cond_wait(&session->wake,&session->mutex);
    session->status.state=RK3_USB_CLOSED;pthread_cond_broadcast(&session->wake);pthread_mutex_unlock(&session->mutex);
    release_session(session);return NULL;
}
static int owner_test_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*entry)(void *),void *context) {
    if(!owner_test_active)return pthread_create(thread,attributes,entry,context);
    atomic_fetch_add(&owner_creates,1);
    assert(entry==worker_main&&context==owner_test_session);
    assert(attributes&&"USB owner must receive explicit QoS attributes");
    qos_class_t qos=QOS_CLASS_UNSPECIFIED;int relative=99;
    assert(!pthread_attr_get_qos_class_np((pthread_attr_t *)attributes,&qos,&relative));
    assert(qos==QOS_CLASS_USER_INTERACTIVE&&relative==0);
    if(owner_test_failure==3)return EAGAIN;
    int result=pthread_create(thread,attributes,owner_controlled_worker,context);
    if(result==0)owner_test_thread=*thread;return result;
}
static int owner_test_detach(pthread_t thread) {
    if(!owner_test_active)return pthread_detach(thread);
    atomic_fetch_add(&owner_detaches,1);assert(pthread_equal(thread,owner_test_thread));
    return 0; /* Join the controlled worker after the real close_wait. */
}
static int owner_test_mutex_destroy(pthread_mutex_t *mutex) {
    if(owner_test_active)atomic_fetch_add(&owner_mutex_destroys,1);
    return pthread_mutex_destroy(mutex);
}
static int owner_test_cond_destroy(pthread_cond_t *condition) {
    if(owner_test_active)atomic_fetch_add(&owner_cond_destroys,1);
    return pthread_cond_destroy(condition);
}
static void *owner_test_calloc(size_t count,size_t size) {
    void *result=calloc(count,size);
    if(owner_test_active){
        assert(count==1&&size==sizeof(rk3_usb_session)&&result);
        atomic_fetch_add(&owner_allocations,1);owner_test_session=result;
    }
    return result;
}
static void owner_test_free(void *memory) {
    if(owner_test_active&&memory==owner_test_session)atomic_fetch_add(&owner_frees,1);
    if(memory&&memory==atomic_load(&monitor_free_target))atomic_fetch_add(&monitor_free_count,1);
    free(memory);
}
static void owner_creation_test(void) {
    /* Keep the setup-error shims available in the pre-correction RED build. */
    (void)owner_test_attr_init;(void)owner_test_attr_set_qos;(void)owner_test_attr_destroy;
    for(unsigned failure=0;failure<5;++failure) {
        owner_test_active=true;owner_test_failure=failure;owner_test_session=NULL;
        atomic_store(&owner_attr_inits,0);atomic_store(&owner_attr_sets,0);atomic_store(&owner_attr_destroys,0);
        atomic_store(&owner_creates,0);atomic_store(&owner_detaches,0);atomic_store(&owner_mutex_destroys,0);
        atomic_store(&owner_cond_destroys,0);atomic_store(&owner_allocations,0);atomic_store(&owner_frees,0);
        atomic_store(&owner_entered,false);atomic_store(&owner_requested_result,-1);
        atomic_store(&owner_requested_qos,(int)QOS_CLASS_UNSPECIFIED);atomic_store(&owner_requested_relative,99);
        char error[128]="unchanged";rk3_usb_session *session=rk3_usb_open(error,sizeof(error));
        assert(atomic_load(&owner_allocations)==1&&atomic_load(&owner_attr_inits)==1);
        assert(atomic_load(&owner_attr_sets)==(failure==1?0u:1u));
        assert(atomic_load(&owner_attr_destroys)==(failure==1?0u:1u));
        assert(atomic_load(&owner_creates)==(failure==1||failure==2?0u:1u));
        if(failure>=1&&failure<=3) {
            assert(!session&&error[0]&&atomic_load(&owner_detaches)==0);
            int expected=failure==1?ENOMEM:failure==2?EINVAL:EAGAIN;char expected_error[128];
            snprintf(expected_error,sizeof(expected_error),"USB worker creation failed: %d",expected);
            assert(!strcmp(error,expected_error)&&!atomic_load(&owner_entered));
        } else {
            assert(session==owner_test_session&&!error[0]&&atomic_load(&owner_detaches)==1);
            assert(atomic_load(&owner_frees)==0);
            pthread_mutex_lock(&session->mutex);
            double deadline=seconds_now()+2;
            while(!atomic_load(&owner_entered)&&seconds_now()<deadline) {
                struct timespec relative={0,10000000};
                (void)pthread_cond_timedwait_relative_np(&session->wake,&session->mutex,&relative);
            }
            pthread_mutex_unlock(&session->mutex);assert(atomic_load(&owner_entered));
            /* These getters prove requested QoS, never effective priority or deadlines. */
            assert(atomic_load(&owner_requested_result)==0);
            assert(atomic_load(&owner_requested_qos)==(int)QOS_CLASS_USER_INTERACTIVE);
            assert(atomic_load(&owner_requested_relative)==0);
            RK3NativeInfo info;
            assert(rk3_usb_info(session,&info)&&info.sample_rate==44100);
            assert(rk3_usb_close_wait(session,1000));assert(!pthread_join(owner_test_thread,NULL));
        }
        assert(atomic_load(&owner_cond_destroys)==1&&atomic_load(&owner_mutex_destroys)==1);
        assert(atomic_load(&owner_frees)==1);
        owner_test_active=false;owner_test_session=NULL;
    }
}

static CFRunLoopTimerRef test_clock_timer_create(CFAllocatorRef allocator,
    CFAbsoluteTime date,CFTimeInterval interval,CFOptionFlags flags,CFIndex order,
    CFRunLoopTimerCallBack callback,CFRunLoopTimerContext *context) {
    ++clock_timer_create_calls;
    if(fail_clock_timer_create)return NULL;
    return CFRunLoopTimerCreate(allocator,date,interval,flags,order,callback,context);
}

struct rk3_native_audio_iso {
    uint64_t incarnation,deadline;
    uint32_t sample_rate;
    bool clock_armed;
};
typedef void (*TestHook)(void);
static _Atomic(TestHook) capture_hook,gain_snapshot_hook;
static TestHook clock_service_hook;
static unsigned fake_clock_schedule_calls,fake_clock_service_calls;
static uint64_t fake_clock_interval_ns=128000000;
static const rk3_native_audio_iso *forbidden_clock_audio;
static rk3_usb_session *stop_timer_session;
static rk3_usb_session *destroy_timer_session;
static uint64_t test_clock_ticks(uint64_t ns) {
    mach_timebase_info_data_t scale;
    assert(mach_timebase_info(&scale)==KERN_SUCCESS&&scale.numer&&scale.denom);
    return ns*scale.denom/scale.numer;
}
void rk3_test_input_gain_snapshot(void){TestHook hook=atomic_load(&gain_snapshot_hook);if(hook)hook();}
static unsigned audio_destroy_calls;
static bool fake_audio_running, fake_clock_valid, fake_audio_quiescent=true;
static bool fake_panic_pending;
static uint32_t fake_panic_delay;
static int32_t fake_panic_error;
static unsigned fake_stop_calls, fake_start_calls, fake_service_calls;
static uint32_t fake_started_rate;
static bool check_host_lifetime, mapped_released, fake_output_active;
static unsigned release_busy, close_busy, release_usb_calls, host_close_calls, host_release_calls, host_alt_calls, activity_calls;
static rk3_usb_session *activity_publication;
static int32_t fake_audio_error, fake_stream_error, fake_start_result=ENODEV;
static int32_t fake_rate_result;
static unsigned fake_rate_calls;
rk3_native_audio_iso *rk3_native_audio_iso_create(RK3HostInterface *interface,
    uint8_t input, uint8_t output, rk3_native_audio_command command, void *context) {
    (void)interface; (void)input; (void)output; (void)command; (void)context; return NULL;
}
int32_t rk3_native_audio_iso_set_sample_rate(rk3_native_audio_iso *audio,uint32_t sample_rate) {
    ++fake_rate_calls;
    if(fake_rate_result)return fake_rate_result;
    assert(audio&&rk3_audio_sample_rate_supported(sample_rate));
    if(activity_publication)assert(atomic_load(&activity_publication->audio)==NULL);
    audio->sample_rate=sample_rate;return 0;
}
int32_t rk3_native_audio_iso_start(rk3_native_audio_iso *audio) {
    ++fake_start_calls;fake_started_rate=audio->sample_rate;
    if(fake_start_result==0){
        fake_audio_running=true;fake_panic_pending=false;fake_audio_error=fake_stream_error=0;
        ++audio->incarnation;audio->clock_armed=true;
        audio->deadline=mach_absolute_time()+test_clock_ticks(fake_clock_interval_ns);
    }
    return fake_start_result;
}
void rk3_native_audio_iso_stop(rk3_native_audio_iso *audio) {
    if(stop_timer_session)assert(stop_timer_session->clock_timer==NULL&&stop_timer_session->clock_timer_context==NULL);
    audio->clock_armed=false;fake_audio_running=false;++fake_stop_calls;
}
size_t rk3_native_audio_iso_pending(const rk3_native_audio_iso *audio) { (void)audio; return fake_audio_quiescent?0:1; }
bool rk3_native_audio_iso_is_quiescent(const rk3_native_audio_iso *audio) { return audio==NULL||fake_audio_quiescent; }
bool rk3_native_audio_iso_release_usb(rk3_native_audio_iso *audio) {
    (void)audio;if(check_host_lifetime){++release_usb_calls;if(release_busy){--release_busy;return false;}mapped_released=true;}return true;
}
bool rk3_native_audio_iso_destroy(rk3_native_audio_iso *audio) {
    if(destroy_timer_session)assert(!destroy_timer_session->clock_timer_context||destroy_timer_session->clock_timer_context->audio!=audio);
    ++audio_destroy_calls;return true;
}
int32_t rk3_native_audio_iso_error(const rk3_native_audio_iso *audio) { (void)audio; return fake_audio_error; }
int32_t rk3_native_audio_iso_stream_error(const rk3_native_audio_iso *audio) { (void)audio; return fake_stream_error; }
bool rk3_native_audio_iso_running(const rk3_native_audio_iso *audio) { (void)audio; return fake_audio_running; }
bool rk3_native_audio_iso_clock(rk3_native_audio_iso *audio, rk3_native_audio_clock *clock) {
    (void)audio;
    if (fake_clock_valid) *clock = (rk3_native_audio_clock){
        .sample_frame=20000, .host_time=3000, .seed=1,
        .zero_sample_frame=16384, .zero_host_time=2000
    };
    return fake_clock_valid;
}

bool rk3_native_audio_iso_panic(const rk3_native_audio_iso *audio,uint32_t *delay,int32_t *error) {
    (void)audio;if(!fake_panic_pending)return false;*delay=fake_panic_delay;*error=fake_panic_error;return true;
}
void rk3_native_audio_iso_service(rk3_native_audio_iso *audio) { (void)audio;++fake_service_calls; }
bool rk3_native_audio_iso_clock_schedule(rk3_native_audio_iso *audio,rk3_native_audio_clock_schedule *schedule) {
    assert(audio&&audio!=forbidden_clock_audio);++fake_clock_schedule_calls;
    if(!fake_audio_running||!audio->clock_armed)return false;
    *schedule=(rk3_native_audio_clock_schedule){audio->incarnation,audio->deadline};return true;
}
void rk3_native_audio_iso_clock_service(rk3_native_audio_iso *audio) {
    assert(audio&&audio!=forbidden_clock_audio);++fake_clock_service_calls;
    audio->deadline=mach_absolute_time()+test_clock_ticks(fake_clock_interval_ns);
    if(clock_service_hook)clock_service_hook();
}
void rk3_native_audio_iso_set_output_active(rk3_native_audio_iso *audio,bool active) {
    (void)audio;if(activity_publication)assert(atomic_load(&activity_publication->audio)==NULL);
    fake_output_active=active;++activity_calls;
}
static uint64_t addressed_first;
static size_t addressed_frames;
size_t rk3_native_audio_iso_render_at(rk3_native_audio_iso *audio,const float *samples,uint64_t first,size_t frames) {
    (void)audio;(void)samples;addressed_first=first;addressed_frames=frames;return frames;
}
size_t rk3_native_audio_iso_capture_at(rk3_native_audio_iso *audio,float *samples,uint64_t first,size_t frames) {
    (void)audio;addressed_first=first;addressed_frames=frames;
    if(frames) {samples[0]=0.5f;samples[1]=-0.5f;TestHook hook=atomic_load(&capture_hook);if(hook)hook();return 1;}return 0;
}

/* Fake only the public C host binding; no hardware object is created. */
struct RK3HostInterface {
    bool disconnected;
    int32_t (*bulk_read)(void *,uint8_t,void *,size_t,uint32_t,RK3HostBulkCompletion,void *);
    int32_t (*bulk_write)(void *,uint8_t,const void *,size_t,uint32_t,RK3HostBulkCompletion,void *);
    int32_t (*clear_stall)(void *,uint8_t);
    int32_t (*abort_pipe)(void *,uint8_t);
};
RK3HostInterface *rk3_host_open(io_service_t service,CFRunLoopRef loop,int32_t *error) {
    (void)service;(void)loop;*error=kIOReturnNoDevice;return NULL;
}
void rk3_host_measurement_begin_stream(RK3HostInterface *interface,uint64_t epoch){(void)interface;(void)epoch;}
void rk3_host_measurement_clock(RK3HostInterface *interface,bool beginning,uint64_t tick){(void)interface;(void)beginning;(void)tick;}
bool rk3_host_connected(const RK3HostInterface *interface) {return interface!=NULL&&!interface->disconnected;}
void rk3_host_retain(RK3HostInterface *interface) {(void)interface;}
void rk3_host_release(RK3HostInterface *interface) {(void)interface;if(check_host_lifetime){assert(mapped_released&&close_busy==0);++host_release_calls;}}
int32_t rk3_host_close(RK3HostInterface *interface) {
    (void)interface;if(check_host_lifetime){assert(mapped_released);++host_close_calls;if(close_busy){--close_busy;return kIOReturnBusy;}}return 0;
}
int32_t rk3_host_get_alt(RK3HostInterface *interface,uint8_t *alternate) {(void)interface;*alternate=1;return 0;}
int32_t rk3_host_set_alt(RK3HostInterface *interface,uint8_t alternate) {
    (void)interface;(void)alternate;if(check_host_lifetime){assert(mapped_released);++host_alt_calls;}return 0;
}
int32_t rk3_host_num_pipes(RK3HostInterface *interface,uint8_t *count) {(void)interface;*count=0;return 0;}
int32_t rk3_host_pipe_properties(RK3HostInterface *interface,uint8_t pipe,RK3HostPipeProperties *properties) {
    (void)interface;(void)pipe;(void)properties;return kIOReturnUnsupported;
}
int32_t rk3_host_abort(RK3HostInterface *interface,uint8_t pipe) {return interface->abort_pipe?interface->abort_pipe(interface,pipe):0;}
int32_t rk3_host_clear_stall(RK3HostInterface *interface,uint8_t pipe) {return interface->clear_stall?interface->clear_stall(interface,pipe):0;}
size_t rk3_host_pending(const RK3HostInterface *interface) {(void)interface;return 0;}
int32_t rk3_host_bulk_read(RK3HostInterface *interface,uint8_t pipe,void *bytes,size_t size,uint32_t timeout,RK3HostBulkCompletion completion,void *context) {
    return interface->bulk_read?interface->bulk_read(interface,pipe,bytes,size,timeout,completion,context):kIOReturnUnsupported;
}
int32_t rk3_host_bulk_write(RK3HostInterface *interface,uint8_t pipe,const void *bytes,size_t size,uint32_t timeout,RK3HostBulkCompletion completion,void *context) {
    return interface->bulk_write?interface->bulk_write(interface,pipe,bytes,size,timeout,completion,context):kIOReturnUnsupported;
}

static unsigned int read_arms;
static unsigned int command_writes;
static CFRunLoopSourceRef rate_ack_source;
static uint8_t last_command_bytes[64];
static size_t last_command_size;
static rk3_usb_session *command_io_session;
static rk3_usb_session *ack_test_session;
static int ack_test_result;
static IOReturn command_write_result;
static IOReturn command_callback_result;
static bool short_write_callback, defer_write, signal_deferred;
static RK3HostBulkCompletion deferred_write_callback;
static void *deferred_write_context;
static const void *deferred_write_buffer;
static UInt32 deferred_write_length;
static CFRunLoopSourceRef deferred_source;
static rk3_usb_session *controller_test_session;
static uint8_t controller_commands[4][64];
static size_t controller_lengths[4], controller_count, controller_reply_length;
static unsigned int controller_fail_at, controller_stop_kind;
static IOReturn controller_failure_result = kIOReturnIOError;
static unsigned int command_io_trace[16], command_io_trace_length, reset_failure;
static IOReturn reset_result, command_read_result;
enum { COMMAND_RESET_IN = 1, COMMAND_RESET_OUT, COMMAND_SOURCE, COMMAND_READ, COMMAND_WRITE };
static void trace_command(unsigned int event) {
    assert(command_io_trace_length < 16);
    command_io_trace[command_io_trace_length++] = event;
}
static int32_t fake_read(void *self, uint8_t pipe, void *buffer, size_t size,
                         uint32_t timeout, RK3HostBulkCompletion callback, void *context) {
    (void)self; (void)pipe; (void)buffer; (void)callback; (void)context; (void)timeout;
    assert(size == 64); ++read_arms;
    if (command_io_session != NULL) trace_command(COMMAND_READ);
    return command_read_result;
}
static int32_t fake_write(void *self, uint8_t pipe, const void *buffer, size_t size,
                          uint32_t no_data, RK3HostBulkCompletion callback, void *context) {
    (void)self; (void)pipe; (void)buffer;
    assert(size > 0 && size <= 64 && no_data > 0 && no_data <= 200);
    ++command_writes;last_command_size=size;memcpy(last_command_bytes,buffer,size);
    if (ack_test_session != NULL) {
        ack_test_session->ack_result = rate_ack_source ? -1 : ack_test_result;
        if(rate_ack_source)CFRunLoopSourceSignal(rate_ack_source);
    }
    if (command_io_session != NULL) {
        trace_command(COMMAND_WRITE);
        assert(size == 1 && ((const uint8_t *)buffer)[0] == RK3_COMMAND_INFO);
        if (command_io_session->pending) command_io_session->info_received = true;
    }
    if (controller_test_session != NULL) {
        assert(controller_count < 4);
        controller_lengths[controller_count] = size;
        memcpy(controller_commands[controller_count++], buffer, size);
        if (controller_fail_at == controller_count) return controller_failure_result;
        if (controller_count == 1) {
            if (controller_stop_kind == 1) controller_test_session->active_request->cancelled = true;
            if (controller_stop_kind == 2) atomic_store(&controller_test_session->closing,true);
            if (controller_stop_kind == 3) controller_test_session->stopping_io = true;
        }
        if (((const uint8_t *)buffer)[0] == RK3_COMMAND_DIGITAL) {
            controller_test_session->read_buffer[0] = RK3_COMMAND_DIGITAL;
            controller_test_session->read_buffer[1] = 0xa5;
            controller_test_session->read_buffer[2] = 0xf3;
            report_complete(controller_test_session, kIOReturnSuccess, controller_reply_length);
        }
    }
    if (command_write_result != kIOReturnSuccess) return command_write_result;
    if (defer_write) {
        deferred_write_callback = callback; deferred_write_context = context;
        deferred_write_buffer = buffer; deferred_write_length = size;
        if (signal_deferred) CFRunLoopSourceSignal(deferred_source);
    } else callback(context, command_callback_result, short_write_callback ? size-1 : size);
    return kIOReturnSuccess;
}

static void initialize(rk3_usb_session *session) {
    memset(session, 0, sizeof(*session));
    assert(pthread_mutex_init(&session->mutex, NULL) == 0);
    assert(pthread_cond_init(&session->wake, NULL) == 0);
    atomic_init(&session->closing, false);
    atomic_init(&session->audio, NULL);
    atomic_init(&session->clock_generation, 1);
    atomic_init(&session->output_active, true);
    atomic_init(&session->rt_readers, 0);
    atomic_init(&session->references, 2);
    session->sample_rate=44100;
    session->status.info.midi_outputs = 1;
    session->generation = 9;
    session->run_loop=CFRunLoopGetCurrent(); /* Borrowed fixture loop; no worker is created. */
    fake_audio_quiescent = true; fake_panic_pending = false;
    fake_audio_error = fake_stream_error = 0; fake_audio_running = fake_clock_valid = false; fake_stop_calls = fake_start_calls = 0; fake_start_result=ENODEV;
    clock_service_hook=NULL;fake_clock_schedule_calls=fake_clock_service_calls=0;
    fake_clock_interval_ns=128000000;forbidden_clock_audio=NULL;stop_timer_session=destroy_timer_session=NULL;
    fake_rate_result=0;fake_rate_calls=0;
    fake_started_rate=0;
    fail_clock_timer_create=false;
}

static RetiredAudio *test_context(rk3_usb_session *session,rk3_native_audio_iso *audio) {
    RetiredAudio *context=calloc(1,sizeof(*context));assert(context);
    context->audio=audio;context->generation=session->generation;context->inputs=context->outputs=2;
    rk3_input_gain_init(&context->gain);context->next=session->retired;session->retired=context;++session->retired_count;return context;
}
static void test_audio(rk3_usb_session *session,rk3_native_audio_iso *audio) {
    atomic_store(&session->audio,audio?test_context(session,audio):NULL);
}
static void finish(rk3_usb_session *session) {
    assert(session->head == NULL && session->queued == 0 && session->stop_head == NULL && session->deferred_stops == 0);
    cancel_audio_clock_timer(session);
    while(session->retired){RetiredAudio *next=session->retired->next;free(session->retired);session->retired=next;}
    pthread_cond_destroy(&session->wake);
    pthread_mutex_destroy(&session->mutex);
}

static void history_test(void) {
    rk3_usb_session session;
    initialize(&session);
    RK3HostInterface functions = {0};
    functions.bulk_read = fake_read;
    RK3HostInterface *interface = &functions;
    session.interface = interface;
    for (size_t index = 0; index < 300; ++index) {
        session.read_buffer[0] = 6;
        session.read_buffer[1] = (uint8_t)index;
        report_complete(&session, kIOReturnSuccess, 2);
        assert(session.pending);
    }
    assert(read_arms == 300 && session.status.reports_dropped == 44);
    RK3NativeReportQuery query = { .version = RK3_CONTROL_VERSION };
    RK3NativeReports first, second;
    assert(rk3_usb_reports(&session, &query, &first));
    assert(first.count == 32 && first.latest_sequence == 300 && first.dropped == 44);
    assert(first.reports[0].sequence == 45 && first.next_sequence == 76);
    assert(first.reports[0].host_time != 0);
    assert(rk3_usb_reports(&session, &query, &second));
    assert(memcmp(&first, &second, sizeof(first)) == 0); /* independent readers */
    query.after_sequence = first.next_sequence;
    assert(rk3_usb_reports(&session, &query, &second));
    assert(second.reports[0].sequence == 77 && second.dropped == 0);
    query.after_sequence = 300;
    assert(rk3_usb_reports(&session, &query, &second) && second.count == 0 && second.next_sequence == 300);
    session.history_count = session.history_head = 0; /* reconnect clears history, not sequence */
    report_complete(&session, kIOReturnSuccess, 2);
    query.after_sequence = 0;
    assert(rk3_usb_reports(&session, &query, &second) && second.count == 1 && second.reports[0].sequence == 301);
    query.after_sequence = 300;
    assert(rk3_usb_reports(&session, &query, &second) && second.reports[0].sequence == 301);
    session.stopping_io = true;
    report_complete(&session, kIOReturnAborted, 0);
    assert(!session.pending && !session.transport_failed && read_arms == 301);
    finish(&session);
}

typedef struct { rk3_usb_session *session; bool delay; uint64_t observed; bool cancelled; } Consumer;
static void *consume_one(void *context) {
    Consumer *consumer = context;
    if (consumer->delay) { struct timespec delay = {0, 50000000}; nanosleep(&delay, NULL); }
    rk3_usb_session *session = consumer->session;
    pthread_mutex_lock(&session->mutex);
    while (session->head == NULL) pthread_cond_wait(&session->wake, &session->mutex);
    Request *request = session->head;
    session->head = request->next; session->tail = NULL; --session->queued;
    consumer->observed = request->generation;
    consumer->cancelled = request->cancelled;
    int32_t result = request->cancelled ? ETIMEDOUT : request->generation == session->generation ? 0 : ESTALE;
    complete_locked(request, result);
    pthread_mutex_unlock(&session->mutex);
    return NULL;
}

static void request_test(void) {
    rk3_usb_session session;
    initialize(&session);
    uint8_t command[] = {7, 0, 1, 0xf8};
    Consumer consumer = { .session = &session };
    pthread_t thread;
    assert(pthread_create(&thread, NULL, consume_one, &consumer) == 0);
    assert(rk3_usb_command_for_generation(&session, command, sizeof(command), 9, 1000));
    pthread_join(thread, NULL); assert(consumer.observed == 9 && !consumer.cancelled);
    assert(pthread_create(&thread, NULL, consume_one, &consumer) == 0);
    assert(!rk3_usb_command_for_generation(&session, command, sizeof(command), 8, 1000));
    pthread_join(thread, NULL); assert(consumer.observed == 8); /* stale lease is retained */
    consumer.delay = true;
    assert(pthread_create(&thread, NULL, consume_one, &consumer) == 0);
    assert(!rk3_usb_command_for_generation(&session, command, sizeof(command), 9, 2));
    pthread_join(thread, NULL); assert(consumer.cancelled); /* request survives caller timeout */
    consumer.delay = false;
    uint8_t led[] = {5,0,0x40,0x40,0,0xff,1,0,0};
    assert(pthread_create(&thread, NULL, consume_one, &consumer) == 0);
    assert(rk3_usb_command_for_generation(&session, led, sizeof(led), 9, 1000));
    pthread_join(thread, NULL); assert(consumer.observed == 9);
    for (size_t index = 1; index < sizeof(led); ++index) {
        if (index == 5) continue; /* Full LED mask byte is explicitly permitted. */
        uint8_t saved = led[index]; led[index] = index == 6 ? 2 : (uint8_t)(saved ^ 1);
        assert(!rk3_usb_command_for_generation(&session, led, sizeof(led), 9, 10)); led[index] = saved;
    }
    assert(!rk3_usb_command_for_generation(&session, led, sizeof(led)-1, 9, 10));
    assert(!rk3_usb_command_for_generation(&session, led, sizeof(led), 0, 10));
    command[0] = 5; assert(!rk3_usb_command(&session, command, sizeof(command), 10));
    command[0] = 7; command[1] = 1; assert(!rk3_usb_command(&session, command, sizeof(command), 10));
    command[1] = 0; command[2] = 0; assert(!rk3_usb_command(&session, command, sizeof(command), 10));
    command[2] = 62; assert(!rk3_usb_command(&session, command, sizeof(command), 10));
    float samples[8]; for (size_t index = 0; index < 8; ++index) samples[index] = 1;
    assert(rk3_usb_capture_at(&session, samples, 0, 4) == 0);
    for (size_t index = 0; index < 8; ++index) assert(samples[index] == 0);
    assert(rk3_usb_render_at(&session, samples, 0, 4) == 0);
    finish(&session);
}

typedef struct {rk3_usb_session *session;bool reconnect;} PedalCommandConsumer;
static void *consume_pedal_command(void *context) {
    PedalCommandConsumer *consumer=context;
    rk3_usb_session *session=consumer->session;
    pthread_mutex_lock(&session->mutex);
    while(!session->head)pthread_cond_wait(&session->wake,&session->mutex);
    if(consumer->reconnect)++session->generation;
    pthread_mutex_unlock(&session->mutex);
    process_one_request(session,true);return NULL;
}
static void pedal_configuration_command_test(void) {
    unsigned previous_writes=command_writes;size_t previous_size=last_command_size;
    uint8_t previous_bytes[sizeof(last_command_bytes)];memcpy(previous_bytes,last_command_bytes,sizeof(previous_bytes));
    rk3_usb_session session;initialize(&session);
    RK3HostInterface functions={.bulk_write=fake_write};
    session.interface=&functions;session.opened=true;session.command_out=7;session.status.state=RK3_USB_READY;
    uint8_t command[65]={0x0f,2};
    PedalCommandConsumer consumer={.session=&session};pthread_t thread;
    for(unsigned ring=0;ring<8;++ring)for(unsigned switches=0;switches<8;++switches) {
        command[2]=(uint8_t)ring;command[3]=(uint8_t)switches;
        unsigned writes=command_writes;
        assert(pthread_create(&thread,NULL,consume_pedal_command,&consumer)==0);
        assert(rk3_usb_command_for_generation(&session,command,33,9,1000));
        assert(pthread_join(thread,NULL)==0);
        assert(command_writes==writes+1&&last_command_size==33&&!memcmp(last_command_bytes,command,33));
    }
    command[2]=command[3]=0;
    unsigned writes=command_writes;
    for(size_t index=0;index<33;++index) {
        uint8_t saved=command[index];command[index]=index==2||index==3?8:(uint8_t)(saved^1);
        assert(!rk3_usb_command_for_generation(&session,command,33,9,10));
        assert(!session.head&&command_writes==writes);command[index]=saved;
    }
    for(size_t length=0;length<=sizeof(command);++length) {
        if(length==33)continue;
        assert(!rk3_usb_command_for_generation(&session,command,length,9,10));
        assert(!session.head&&command_writes==writes);
    }
    assert(!rk3_usb_command_for_generation(&session,command,33,0,10)&&!session.head);
    assert(pthread_create(&thread,NULL,consume_pedal_command,&consumer)==0);
    assert(!rk3_usb_command_for_generation(&session,command,33,8,1000));
    assert(pthread_join(thread,NULL)==0&&command_writes==writes);
    /* A valid lease at admission is still rejected by the production worker
     * if a new USB generation replaces it before the queued command runs. */
    consumer.reconnect=true;
    assert(pthread_create(&thread,NULL,consume_pedal_command,&consumer)==0);
    assert(!rk3_usb_command_for_generation(&session,command,33,9,1000));
    assert(pthread_join(thread,NULL)==0&&session.generation==10&&command_writes==writes);
    finish(&session);command_writes=previous_writes;last_command_size=previous_size;
    memcpy(last_command_bytes,previous_bytes,sizeof(previous_bytes));
}

static void active_deadline_test(void) {
    rk3_usb_session session;
    initialize(&session);
    RK3HostInterface functions = {0}; functions.bulk_write = fake_write;
    RK3HostInterface *interface = &functions;
    session.interface = interface; session.opened = true;
    Request active = { .deadline = seconds_now() - 1 };
    session.active_request = &active;
    const uint8_t command[] = {7,0,1,0xf8};
    assert(!command_worker(&session, command, sizeof(command), 1000) && command_writes == 0);
    active.deadline = seconds_now() + 1; active.cancelled = true;
    assert(!command_worker(&session, command, sizeof(command), 1000) && command_writes == 0);
    active.cancelled = false;
    assert(command_worker(&session, command, sizeof(command), 1000) && command_writes == 1);
    assert(session.status.commands_completed == 1);
    finish(&session);
}

static void clock_startup_test(void) {
    rk3_usb_session session;
    initialize(&session);
    rk3_native_audio_iso audio = {0};
    Request active = { .deadline = seconds_now() + 1 };
    session.active_request = &active;
    fake_audio_running = fake_clock_valid = true;
    assert(wait_audio_clock(&session, &audio));
    active.cancelled = true;
    assert(!wait_audio_clock(&session, &audio));
    active.cancelled = false; active.deadline = seconds_now() - 1;
    assert(!wait_audio_clock(&session, &audio));
    active.deadline = seconds_now() + 1; fake_audio_error = EIO;
    assert(!wait_audio_clock(&session, &audio) && session.status.error_code == EIO);
    fake_audio_error = 0; fake_clock_valid = false; active.deadline = seconds_now() + 0.002;
    assert(!wait_audio_clock(&session, &audio));
    fake_audio_running = false;
    finish(&session);
}

static void clock_snapshot_forwarding_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};
    test_audio(&session,&audio);fake_clock_valid=true;
    rk3_usb_clock_snapshot clock={0};assert(rk3_usb_clock(&session,&clock));
    assert(clock.sample_frame==20000&&clock.host_time==3000);
    assert(clock.seed==((UINT64_C(1)<<32)|1));
    assert(clock.zero_sample_frame==16384&&clock.zero_host_time==2000);
    assert(atomic_load(&session.rt_readers)==0);
    fake_clock_valid=false;assert(!rk3_usb_clock(&session,&clock));
    assert(atomic_load(&session.rt_readers)==0);
    test_audio(&session,NULL);finish(&session);
}

static void retirement_test(void) {
    rk3_usb_session session;
    initialize(&session);
    rk3_native_audio_iso old = {0}, current = {0};
    RetiredAudio *first = calloc(1, sizeof(*first)), *second = calloc(1, sizeof(*second));
    assert(first != NULL && second != NULL);
    first->audio = &old; first->next = second; second->audio = &current;
    session.retired = first; session.retired_count = 2;
    atomic_store(&session.audio,second); atomic_store(&session.rt_readers, 1);
    reclaim_retired(&session); assert(session.retired_count == 2);
    atomic_store(&session.rt_readers, 0);
    reclaim_retired(&session); assert(session.retired_count == 1 && session.retired->audio == &current);
    test_audio(&session,NULL);
    reclaim_retired(&session); assert(session.retired_count == 0 && session.retired == NULL);
    finish(&session);
}

static void *fake_close_worker(void *context) {
    rk3_usb_session *session = context;
    struct timespec delay = {0, 5000000}; nanosleep(&delay, NULL);
    assert(atomic_load(&session->closing));
    pthread_mutex_lock(&session->mutex);
    session->status.state = RK3_USB_CLOSED; pthread_cond_broadcast(&session->wake);
    pthread_mutex_unlock(&session->mutex);
    release_session(session);
    return NULL;
}

static void close_wait_test(void) {
    for (unsigned int index = 0; index < 2; ++index) {
        rk3_usb_session *session = calloc(1, sizeof(*session)); assert(session != NULL);
        initialize(session);
        pthread_t thread;
        assert(pthread_create(&thread, NULL, fake_close_worker, session) == 0);
        assert(rk3_usb_close_wait(session, index == 0 ? 1000 : 0) == (index == 0));
        pthread_join(thread, NULL);
    }
}

static UInt8 fake_configuration;
static unsigned int configuration_sets, configuration_opens, configuration_closes;
static bool configuration_open;
static IOReturn configuration_result;
static unsigned int configuration_trace[16], configuration_trace_length;
enum { CONFIG_GET = 1, CONFIG_ADVERTISE, CONFIG_OPEN, CONFIG_SET, CONFIG_CLOSE };
static void trace_configuration(unsigned int event) {
    assert(configuration_trace_length < 16);
    configuration_trace[configuration_trace_length++] = event;
}
static void identity_configuration_event(unsigned int event);
static IOReturn fake_configuration_get(void *self, UInt8 *value) {
    (void)self; trace_configuration(CONFIG_GET); *value = fake_configuration; return kIOReturnSuccess;
}
static IOReturn fake_configuration_count(void *self, UInt8 *value) {
    (void)self; *value = 1; return kIOReturnSuccess;
}
static IOReturn fake_configuration_descriptor(void *self, UInt8 index, IOUSBConfigurationDescriptorPtr *descriptor) {
    (void)self; assert(index == 0); trace_configuration(CONFIG_ADVERTISE);
    static uint8_t bytes[] = {9,2,18,0,1,1,0,0x80,50,9,4,0,1,0,0xff,0,0,0};
    *descriptor = (IOUSBConfigurationDescriptorPtr)bytes; return kIOReturnSuccess;
}
static IOReturn fake_configuration_open(void *self) {
    (void)self; trace_configuration(CONFIG_OPEN); assert(!configuration_open);
    configuration_open = true;identity_configuration_event(CONFIG_OPEN); ++configuration_opens; return kIOReturnSuccess;
}
static IOReturn fake_configuration_set(void *self, UInt8 value) {
    (void)self; trace_configuration(CONFIG_SET); assert(configuration_open && value == 1);identity_configuration_event(CONFIG_SET);
    ++configuration_sets;
    if (configuration_result == kIOReturnSuccess) fake_configuration = value;
    return configuration_result;
}
static IOReturn fake_configuration_close(void *self) {
    (void)self; trace_configuration(CONFIG_CLOSE); assert(configuration_open);identity_configuration_event(CONFIG_CLOSE);
    configuration_open = false; ++configuration_closes; return kIOReturnSuccess;
}
static IOReturn fake_identity_request(void *,IOUSBDevRequestTO *);
static IOReturn fake_identity_vendor(void *,UInt16 *);
static IOReturn fake_identity_product(void *,UInt16 *);
static IOReturn fake_identity_product_index(void *,UInt8 *);
static IOReturn fake_identity_manufacturer_index(void *,UInt8 *);
static IOReturn fake_identity_serial_index(void *,UInt8 *);
static void identity_parent_api(IOUSBDeviceInterface300 *functions) {
    functions->GetDeviceVendor=fake_identity_vendor;functions->GetDeviceProduct=fake_identity_product;
    functions->USBGetProductStringIndex=fake_identity_product_index;
    functions->USBGetManufacturerStringIndex=fake_identity_manufacturer_index;
    functions->USBGetSerialNumberStringIndex=fake_identity_serial_index;functions->DeviceRequestTO=fake_identity_request;
}
static void configuration_test(void) {
    rk3_usb_session session; initialize(&session);
    IOUSBDeviceInterface300 functions = {0};identity_parent_api(&functions);
    functions.GetConfiguration = fake_configuration_get;
    functions.GetNumberOfConfigurations = fake_configuration_count;
    functions.GetConfigurationDescriptorPtr = fake_configuration_descriptor;
    functions.USBDeviceOpen = fake_configuration_open;
    functions.SetConfiguration = fake_configuration_set;
    functions.USBDeviceClose = fake_configuration_close;
    IOUSBDeviceInterface300 *device = &functions; session.device = &device;
    for (unsigned int initial = 0; initial <= 1; ++initial) {
        fake_configuration = (UInt8)initial;
        configuration_sets = configuration_opens = configuration_closes = configuration_trace_length = 0;
        configuration_result = kIOReturnSuccess;
        assert(configure_device(&session));
        assert(configuration_sets == 1 && configuration_opens == 1 && configuration_closes == 1 && !configuration_open);
        const unsigned int expected[] = {CONFIG_GET,CONFIG_ADVERTISE,CONFIG_OPEN,CONFIG_SET,CONFIG_CLOSE,CONFIG_GET,CONFIG_ADVERTISE};
        assert(configuration_trace_length == sizeof(expected) / sizeof(*expected));
        assert(memcmp(configuration_trace, expected, sizeof(expected)) == 0);
    }
    fake_configuration = 1; configuration_result = kIOReturnIOError;
    configuration_sets = configuration_opens = configuration_closes = configuration_trace_length = 0;
    assert(!configure_device(&session));
    assert(configuration_sets == 1 && configuration_opens == 1 && configuration_closes == 1 && !configuration_open);
    finish(&session);
}

static IOReturn fake_reset_pipe(void *self, UInt8 pipe) {
    (void)self;
    unsigned int event = pipe == 4 ? COMMAND_RESET_IN : COMMAND_RESET_OUT;
    assert(pipe == 4 || pipe == 7); trace_command(event);
    return reset_failure == event ? reset_result : kIOReturnSuccess;
}
static void command_pipe_reset_test(void) {
    /* NI initUSB ignores both clear results: a recoverable IN failure must
     * still attempt OUT, one reader and one INFO command, without replay. */
    for (unsigned int failure = 0; failure <= 2; ++failure) {
        rk3_usb_session session; initialize(&session);
        RK3HostInterface functions = {0};
        functions.clear_stall = fake_reset_pipe;
        functions.bulk_read = fake_read; functions.bulk_write = fake_write;
        session.interface = &functions; session.opened = true;
        session.command_in = 4; session.command_out = 7; session.run_loop = CFRunLoopGetCurrent();
        command_io_session = &session; command_io_trace_length = 0; reset_failure = failure; reset_result = kIOReturnIOError;
        unsigned int before_reads = read_arms, before_writes = command_writes;
        bool result = start_command_io(&session);
        const unsigned int expected[] = {COMMAND_RESET_IN,COMMAND_RESET_OUT,COMMAND_READ,COMMAND_WRITE};
        assert(result && command_io_trace_length == 4 && memcmp(command_io_trace, expected, sizeof(expected)) == 0);
        assert(read_arms == before_reads + 1 && command_writes == before_writes + 1 && session.pending);
        assert(!session.transport_failed && session.status.state != RK3_USB_FAILED);
        assert(session.status.error_code == (failure ? kIOReturnIOError : 0));
        command_io_session = NULL; finish(&session);
    }
    const IOReturn fatal[] = {kIOReturnNoDevice,kIOReturnNotAttached,kIOReturnNotOpen,(IOReturn)UINT32_C(0xe000404e)};
    for (size_t index = 0; index < sizeof(fatal) / sizeof(*fatal); ++index) {
        for (unsigned int failure = 1; failure <= 2; ++failure) {
            rk3_usb_session session; initialize(&session);
            RK3HostInterface functions = {.clear_stall=fake_reset_pipe,.bulk_read=fake_read,.bulk_write=fake_write};
            session.interface=&functions; session.opened=true; session.command_in=4; session.command_out=7;
            session.run_loop=CFRunLoopGetCurrent();
            command_io_session=&session; command_io_trace_length=0; reset_failure=failure; reset_result=fatal[index];
            unsigned int before_reads=read_arms,before_writes=command_writes;
            assert(!start_command_io(&session) && command_io_trace_length==failure);
            assert(!session.pending && read_arms==before_reads && command_writes==before_writes);
            assert(session.transport_failed && session.status.state==RK3_USB_FAILED && session.status.error_code==fatal[index]);
            command_io_session=NULL; finish(&session);
        }
    }
}

static void fresh_command_connection_test(void) {
    /* disconnect leaves these worker flags set. A newly opened interface must
     * initialize its lease before checking clears, not inherit the old stop. */
    for(unsigned int scenario=0;scenario<4;++scenario) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface functions={.clear_stall=fake_reset_pipe,.bulk_read=fake_read,.bulk_write=fake_write};
        session.interface=&functions;session.opened=true;session.command_in=4;session.command_out=7;
        session.run_loop=CFRunLoopGetCurrent();session.stopping_io=session.transport_failed=true;
        Request active={.deadline=seconds_now()+1,.cancelled=scenario==2};session.active_request=&active;
        if(scenario==1)atomic_store(&session.closing,true);
        command_io_session=&session;command_io_trace_length=0;reset_failure=scenario==3?1:0;reset_result=kIOReturnNoDevice;
        unsigned int before_reads=read_arms,before_writes=command_writes;
        bool ready=start_command_io(&session);
        const unsigned int expected[]={COMMAND_RESET_IN,COMMAND_RESET_OUT,COMMAND_READ,COMMAND_WRITE};
        if(scenario==0) {
            assert(ready&&command_io_trace_length==4&&memcmp(command_io_trace,expected,sizeof(expected))==0);
            assert(read_arms==before_reads+1&&command_writes==before_writes+1&&session.pending);
            assert(!session.stopping_io&&!session.transport_failed);
        } else {
            assert(!ready&&command_io_trace_length==(scenario==3?1u:0u));
            assert(!session.pending&&!session.pending_write&&read_arms==before_reads&&command_writes==before_writes);
            if(scenario==3)assert(session.transport_failed&&session.status.error_code==kIOReturnNoDevice);
        }
        command_io_session=NULL;finish(&session);
    }
}

static void initial_reader_rejection_test(void) {
    const IOReturn failures[] = {kIOReturnIOError,kIOReturnNoDevice,kIOReturnNotAttached,kIOReturnNotOpen,(IOReturn)UINT32_C(0xe000404e)};
    for (size_t index=0;index<sizeof(failures)/sizeof(*failures);++index) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface functions={.clear_stall=fake_reset_pipe,.bulk_read=fake_read,.bulk_write=fake_write};
        session.interface=&functions;session.opened=true;session.command_in=4;session.command_out=7;
        session.run_loop=CFRunLoopGetCurrent();
        /* A recoverable submission failure does not supply an INFO response.
         * Keep the current bounded readiness policy visible, not a false pass. */
        Request active={.deadline=seconds_now()+0.05};session.active_request=&active;
        command_io_session=&session;command_io_trace_length=0;reset_failure=0;command_read_result=failures[index];
        unsigned int before_reads=read_arms,before_writes=command_writes;
        assert(!start_command_io(&session));
        const unsigned int expected[]={COMMAND_RESET_IN,COMMAND_RESET_OUT,COMMAND_READ,COMMAND_WRITE};
        unsigned int expected_count=index==0?4:3;
        assert(command_io_trace_length==expected_count&&memcmp(command_io_trace,expected,expected_count*sizeof(*expected))==0);
        assert(read_arms==before_reads+1&&command_writes==before_writes+(index==0?1u:0u));
        assert(!session.pending&&!session.pending_write&&session.command_suspended&&!session.info_received);
        assert(session.status.info.input_channels==0&&session.status.info.output_channels==0);
        assert(session.transport_failed==(index!=0));
        assert(session.status.error_code==(index==0?ETIMEDOUT:failures[index]));
        command_io_session=NULL;command_read_result=0;finish(&session);
    }
}

static void stop_ack_test(void) {
    rk3_usb_session session; initialize(&session);
    RK3HostInterface functions = {0}; functions.bulk_write = fake_write;
    RK3HostInterface *interface = &functions;
    session.interface = interface; session.opened = true; ack_test_session = &session;
    const uint8_t stop[] = {9,255,255,136,0,1}, start[] = {9,1,2,136,0,1};
    ack_test_result = 0;
    assert(command_worker(&session, stop, sizeof(stop), 10));
    assert(!command_worker(&session, start, sizeof(start), 10));
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.flags==RK3_NATIVE_CONNECTED&&!session.transport_failed);
    ack_test_result = 1;
    assert(command_worker(&session, stop, sizeof(stop), 10));
    assert(command_worker(&session, start, sizeof(start), 10));
    ack_test_result = -1;
    assert(!command_worker(&session, stop, sizeof(stop), 2)); /* missing ACK remains failure */
    assert(rk3_usb_info(&session,&info)&&info.flags==RK3_NATIVE_CONNECTED&&!session.transport_failed);
    ack_test_result = 0; command_write_result = kIOReturnIOError;
    assert(!command_worker(&session, stop, sizeof(stop), 10)); /* actual write must succeed */
    command_write_result = kIOReturnSuccess; ack_test_session = NULL;
    finish(&session);
}

static void controller_startup_test(void) {
    for (unsigned int failure = 0; failure <= 4; ++failure) {
        rk3_usb_session session; initialize(&session);
        RK3HostInterface functions = {0};
        functions.bulk_write = fake_write; functions.bulk_read = fake_read;
        RK3HostInterface *interface = &functions;
        session.interface = interface; session.opened = session.pending = true;
        controller_test_session = &session; controller_count = 0;
        controller_fail_at = failure; controller_failure_result = kIOReturnIOError; controller_reply_length = 3;
        /* setupTimerCallback0x3691 ignores command05's result. Later
         * controller setup failures retain the unresolved A9 policy. */
        bool ready = failure <= 1;
        assert(initialize_device_commands(&session) == ready);
        assert(controller_count == (ready ? 4 : failure));
        if (ready) {
            const uint8_t audio[] = {5,0,0x40,0x40,0,0,0}, automatic[] = {11,1,2,5};
            assert(controller_lengths[0] == sizeof(audio) && memcmp(controller_commands[0], audio, sizeof(audio)) == 0);
            assert(controller_lengths[1] == 33 && controller_commands[1][0] == 15 && controller_commands[1][1] == 2);
            for (size_t index = 2; index < 33; ++index) assert(controller_commands[1][index] == 0);
            assert(controller_lengths[2] == sizeof(automatic) && memcmp(controller_commands[2], automatic, sizeof(automatic)) == 0);
            assert(controller_lengths[3] == 1 && controller_commands[3][0] == 4);
            assert(session.digital_known && session.digital_snapshot == 0x1a5);
            RK3NativeInfo info; assert(rk3_usb_info(&session, &info) && info.digital_snapshot == UINT32_C(0x800001a5));
            uint8_t invalid_configuration[33];memcpy(invalid_configuration,controller_commands[1],33);
            invalid_configuration[1]=3; /* Only the recovered type2 packet is public. */
            assert(!rk3_usb_command(&session, invalid_configuration, 33, 10));
            assert(!rk3_usb_command(&session, automatic, sizeof(automatic), 10));
        } else assert(!session.digital_known);
        controller_test_session = NULL; finish(&session);
    }
    rk3_usb_session session; initialize(&session);
    RK3HostInterface functions = {0};
    functions.bulk_write = fake_write; functions.bulk_read = fake_read;
    RK3HostInterface *interface = &functions;
    session.interface = interface; session.opened = session.pending = true;
    Request active = {.deadline = seconds_now() + 0.01}; session.active_request = &active;
    controller_test_session = &session; controller_count = 0; controller_fail_at = 0; controller_reply_length = 2;
    assert(!initialize_device_commands(&session) && controller_count == 4 && !session.digital_known);
    controller_test_session = NULL;
    session.transport_failed = false; session.read_buffer[0] = 4;
    session.read_buffer[1] = session.read_buffer[2] = 255;
    report_complete(&session, kIOReturnSuccess, 3);
    assert(session.digital_known && session.digital_snapshot == 0x1ff);
    report_complete(&session, kIOReturnSuccess, 65);
    assert(session.transport_failed && session.status.error_code == EOVERFLOW);
    finish(&session);
}

static void controller_startup_lifetime_test(void) {
    const IOReturn fatal[]={kIOReturnNoDevice,kIOReturnNotAttached,kIOReturnNotOpen,(IOReturn)UINT32_C(0xe000404e)};
    for(size_t index=0;index<sizeof(fatal)/sizeof(*fatal);++index) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface functions={.bulk_write=fake_write,.bulk_read=fake_read};
        session.interface=&functions;session.opened=session.pending=true;
        controller_test_session=&session;controller_count=0;controller_fail_at=1;controller_failure_result=fatal[index];
        assert(!initialize_device_commands(&session)&&controller_count==1);
        assert(session.transport_failed&&session.status.state==RK3_USB_FAILED&&session.status.error_code==fatal[index]);
        assert(!session.pending_write&&!session.digital_known);
        controller_test_session=NULL;finish(&session);
    }
    controller_failure_result=kIOReturnIOError;controller_fail_at=0;
    for(unsigned int stop=1;stop<=3;++stop) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface functions={.bulk_write=fake_write,.bulk_read=fake_read};
        session.interface=&functions;session.opened=session.pending=true;
        Request active={.deadline=seconds_now()+1};session.active_request=&active;
        controller_test_session=&session;controller_count=0;controller_stop_kind=stop;
        assert(!initialize_device_commands(&session)&&controller_count==1);
        assert(!session.pending_write&&!session.digital_known&&!session.transport_failed);
        controller_test_session=NULL;controller_stop_kind=0;finish(&session);
    }
    /* A timed-out admitted write still owns the session buffer. Ignoring
     * command05's result must not overwrite it with a later command. */
    rk3_usb_session session;initialize(&session);
    RK3HostInterface functions={.bulk_write=fake_write,.bulk_read=fake_read};
    session.interface=&functions;session.opened=session.pending=true;
    Request active={.deadline=seconds_now()+0.01};session.active_request=&active;
    controller_test_session=&session;controller_count=0;defer_write=true;
    assert(!initialize_device_commands(&session)&&controller_count==1);
    const uint8_t audio[]={5,0,0x40,0x40,0,0,0};
    assert(session.pending_write&&deferred_write_buffer==session.write_buffer&&deferred_write_length==sizeof(audio));
    assert(memcmp(deferred_write_buffer,audio,sizeof(audio))==0&&!session.digital_known&&!session.transport_failed);
    deferred_write_callback(deferred_write_context,kIOReturnSuccess,deferred_write_length);
    assert(!session.pending_write);
    defer_write=false;controller_test_session=NULL;finish(&session);
}

typedef struct { rk3_usb_session *session; bool cancel; bool complete_after_cancel; bool read_rearmed; } DeferredEvent;
static unsigned int command_aborts;
static IOReturn fake_abort_command(void *self, UInt8 pipe) {
    (void)self; assert(pipe == 7); ++command_aborts;
    return kIOReturnSuccess; /* Completion deliberately arrives later in the test. */
}
static void perform_deferred_write(void *context) {
    DeferredEvent *event = context;
    assert(event->session->pending_write);
    if (event->cancel) {
        pthread_mutex_lock(&event->session->mutex);
        event->session->active_request->cancelled = true;
        pthread_mutex_unlock(&event->session->mutex);
        if (event->complete_after_cancel)
            deferred_write_callback(deferred_write_context, kIOReturnSuccess, deferred_write_length);
        return;
    }
    unsigned int previous_reads = read_arms;
    event->session->read_buffer[0] = 3; event->session->read_buffer[1] = 42;
    report_complete(event->session, kIOReturnSuccess, 2);
    event->read_rearmed = event->session->pending && read_arms == previous_reads + 1;
    assert(event->read_rearmed && event->session->pending_write);
    deferred_write_callback(deferred_write_context, kIOReturnSuccess, deferred_write_length);
}

static void asynchronous_write_test(void) {
    RK3HostInterface functions = {0};
    functions.bulk_write = fake_write; functions.bulk_read = fake_read;
    functions.abort_pipe = fake_abort_command;
    RK3HostInterface *interface = &functions;
    uint8_t command[] = {7,0,1,0xf8};
    for (unsigned int scenario = 0; scenario < 7; ++scenario) {
        rk3_usb_session session; initialize(&session);
        session.interface = interface; session.opened = true;
        session.command_in = 4; session.command_out = 7;
        Request active = {.deadline = seconds_now() + (scenario == 4 ? 0.01 : 1)};
        session.active_request = &active;
        DeferredEvent event = {.session = &session, .cancel = scenario >= 5, .complete_after_cancel = scenario == 6};
        CFRunLoopSourceContext source_context = {0}; source_context.info = &event;
        source_context.perform = perform_deferred_write;
        deferred_source = CFRunLoopSourceCreate(NULL, 0, &source_context); assert(deferred_source != NULL);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), deferred_source, kCFRunLoopDefaultMode);
        command_write_result = scenario == 1 ? kIOReturnIOError : kIOReturnSuccess;
        command_callback_result = scenario == 2 ? kIOReturnIOError : kIOReturnSuccess;
        short_write_callback = scenario == 3;
        defer_write = scenario == 0 || scenario >= 4;
        signal_deferred = scenario == 0 || scenario >= 5;
        unsigned int previous_aborts = command_aborts;
        bool success = command_worker(&session, command, sizeof(command), 1000);
        assert(success == (scenario == 0));
        if (scenario == 0) {
            assert(event.read_rearmed && !session.pending_write && session.status.commands_completed == 1);
            session.stopping_io = true; report_complete(&session, kIOReturnAborted, 0);
        } else if (scenario == 6) {
            assert(!session.pending_write && !session.transport_failed && command_aborts == previous_aborts);
        } else if (scenario >= 4) {
            assert(session.pending_write && !session.transport_failed && io_pending(&session, NULL));
            assert(command_aborts == previous_aborts + (scenario==5?1u:0u) && deferred_write_buffer == session.write_buffer && deferred_write_context == &session);
            uint8_t preserved[sizeof(command)]; memcpy(preserved, session.write_buffer, sizeof(command));
            command[3] ^= 1; /* A caller/request buffer can die or change after timeout. */
            unsigned int previous_writes = command_writes;
            assert(!command_worker(&session, command, sizeof(command), 10) && command_writes == previous_writes);
            assert(memcmp(session.write_buffer, preserved, sizeof(preserved)) == 0);
            deferred_write_callback(deferred_write_context, kIOReturnAborted, 0);
            service_command_recovery(&session,seconds_now());
            assert(!session.pending_write && !io_pending(&session, NULL));
            command[3] ^= 1;
        } else {
            assert(!session.pending_write && !session.transport_failed);
            if (scenario == 3) assert(session.status.error_code == kIOReturnUnderrun);
        }
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), deferred_source, kCFRunLoopDefaultMode);
        CFRelease(deferred_source); deferred_source = NULL;
        finish(&session);
    }
    command_write_result = command_callback_result = kIOReturnSuccess;
    defer_write = short_write_callback = signal_deferred = false;
}

typedef struct { unsigned calls; uint64_t generation; int32_t error; rk3_usb_session *session; } PanicObservation;
static void observe_panic(void *context,uint64_t generation,int32_t error) {
    PanicObservation *observation=context;
    ++observation->calls;observation->generation=generation;observation->error=error;
    /* Refuse self-unregistration instead of deadlocking/freeing live context. */
    assert(!rk3_usb_set_audio_panic_handler(observation->session,NULL,NULL));
}
static Request *enqueue_test_request(rk3_usb_session *session,RequestKind kind,const uint8_t *bytes,size_t length) {
    Request *request=calloc(1,sizeof(*request));assert(request!=NULL);
    assert(pthread_cond_init(&request->condition,NULL)==0);
    request->kind=kind;request->references=2;request->generation=session->generation;
    request->deadline=seconds_now()+5;request->length=length;
    if(length)memcpy(request->bytes,bytes,length);
    if(session->tail)session->tail->next=request;else session->head=request;
    session->tail=request;++session->queued;return request;
}
static void free_test_request(Request *request) {
    assert(request->complete&&request->references==1);
    pthread_cond_destroy(&request->condition);free(request);
}

static void *rate_worker(void *context) {
    rk3_usb_session *session=context;
    pthread_mutex_lock(&session->mutex);
    while(!session->head)pthread_cond_wait(&session->wake,&session->mutex);
    assert(session->head->kind==REQUEST_RATE&&session->head->generation==9&&session->head->sample_rate==88200);
    pthread_mutex_unlock(&session->mutex);
    process_one_request(session,true);return NULL;
}
static void sample_rate_request_test(void) {
    rk3_usb_session session;initialize(&session);
    rk3_native_audio_iso audio={0};test_audio(&session,&audio);
    RK3HostInterface functions={.bulk_write=fake_write};session.interface=&functions;session.opened=true;
    session.status.state=RK3_USB_READY;session.status.info=(rk3_device_info){.input_channels=2,.output_channels=2,.wire_mode=2};
    ack_test_session=&session;ack_test_result=1;unsigned before=command_writes;
    pthread_t worker;assert(!pthread_create(&worker,NULL,rate_worker,&session));
    assert(!rk3_usb_audio_set_sample_rate(&session,9,88200,1000));assert(!pthread_join(worker,NULL));
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.sample_rate==88200);
    const uint8_t expected[]={9,4,2,176,0,1};
    assert(command_writes==before+1&&last_command_size==sizeof(expected)&&!memcmp(last_command_bytes,expected,sizeof(expected)));
    assert(audio.sample_rate==88200&&fake_rate_calls==1&&session.generation==9);
    assert(rk3_usb_audio_set_sample_rate(&session,9,32000,1)==EINVAL); /* Invalid rate never queues. */
    assert(rk3_usb_audio_set_sample_rate(&session,0,48000,1)==EINVAL);
    assert(!session.head&&!session.queued);
    /* The caller's old lease must not be replaced by a new API-entry snapshot. */
    session.generation=10;atomic_load(&session.audio)->generation=10;
    assert(!pthread_create(&worker,NULL,rate_worker,&session));
    assert(rk3_usb_audio_set_sample_rate(&session,9,88200,1000)==ESTALE);assert(!pthread_join(worker,NULL));
    assert(command_writes==before+1&&fake_rate_calls==1&&session.sample_rate==88200);
    ack_test_session=NULL;finish(&session);
}

static void rate_fixture(rk3_usb_session *session,rk3_native_audio_iso *audio,RK3HostInterface *interface) {
    initialize(session);test_audio(session,audio);audio->sample_rate=44100;
    *interface=(RK3HostInterface){.bulk_write=fake_write};session->interface=interface;session->opened=true;
    session->status.state=RK3_USB_READY;
    session->status.info=(rk3_device_info){.input_channels=2,.output_channels=2,.wire_mode=2};
    ack_test_session=session;ack_test_result=1;rate_ack_source=NULL;
    command_write_result=command_callback_result=0;defer_write=short_write_callback=false;
}
static Request *enqueue_rate(rk3_usb_session *session,uint32_t rate) {
    Request *request=enqueue_test_request(session,REQUEST_RATE,NULL,0);request->sample_rate=rate;return request;
}
static void sample_rate_vectors_test(void) {
    const uint32_t rates[]={44100,48000,88200,96000,192000};
    const uint8_t commands[][6]={{9,0,2,128,0,1},{9,1,2,136,0,1},{9,4,2,176,0,1},
        {9,2,2,184,0,1},{9,3,2,24,1,1}};
    for(unsigned i=0;i<5;++i) {
        rk3_usb_session session;rk3_native_audio_iso audio={0};RK3HostInterface interface;
        rate_fixture(&session,&audio,&interface);Request *request=enqueue_rate(&session,rates[i]);
        rate_atomic_session=&session;rate_atomic_request=request;rate_atomic_previous=44100;rate_atomic_observed=false;
        unsigned writes=command_writes;process_one_request(&session,true);
        assert(request->complete&&request->result==0&&fake_rate_calls==1);
        assert(command_writes==writes+1&&last_command_size==6&&!memcmp(last_command_bytes,commands[i],6));
        assert(audio.sample_rate==rates[i]&&session.sample_rate==rates[i]&&session.generation==9);
        assert(i==0||rate_atomic_observed);
        RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.sample_rate==rates[i]&&info.flags==RK3_NATIVE_CONNECTED);
        rate_atomic_session=NULL;rate_atomic_request=NULL;free_test_request(request);ack_test_session=NULL;finish(&session);
    }
}
static void sample_rate_admission_test(void) {
    for(unsigned scenario=0;scenario<12;++scenario) {
        rk3_usb_session session;rk3_native_audio_iso audio={0};RK3HostInterface interface;
        rate_fixture(&session,&audio,&interface);Request *request=enqueue_rate(&session,96000);
        Request stop={0};int32_t expected=kIOReturnBusy;
        switch(scenario) {
            case 0:request->sample_rate=32000;expected=EINVAL;break;
            case 1:request->generation=8;expected=ESTALE;break;
            case 2:request->deadline=seconds_now()-1;expected=ETIMEDOUT;break;
            case 3:request->cancelled=true;expected=ETIMEDOUT;break;
            case 4:session.opened=false;expected=ENODEV;break;
            case 5:atomic_store(&session.audio,NULL);expected=ENODEV;break;
            case 6:session.status.state=RK3_USB_RUNNING;break;
            case 7:session.want_audio=true;break;
            case 8:session.stop_head=&stop;break;
            case 9:fake_audio_running=true;break;
            case 10:fake_audio_quiescent=false;break;
            case 11:session.stopping_io=true;expected=ENODEV;break;
        }
        unsigned writes=command_writes;process_one_request(&session,true);
        assert(request->complete&&request->result==expected&&command_writes==writes&&fake_rate_calls==0);
        RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.sample_rate==44100&&audio.sample_rate==44100);
        session.stop_head=NULL;fake_audio_quiescent=true;fake_audio_running=false;
        free_test_request(request);ack_test_session=NULL;finish(&session);
    }
    rk3_usb_session session;rk3_native_audio_iso audio={0};RK3HostInterface interface;
    rate_fixture(&session,&audio,&interface);Request *request=enqueue_rate(&session,48000);
    session.command_recovering=true;unsigned writes=command_writes;process_one_request(&session,true);
    assert(!request->complete&&session.head==request&&command_writes==writes);
    session.command_recovering=false;process_one_request(&session,true);
    assert(request->complete&&request->result==0&&command_writes==writes+1);
    free_test_request(request);ack_test_session=NULL;finish(&session);
}
typedef struct {rk3_usb_session *session;unsigned mutation;int ack;RetiredAudio *replacement;Request stop;} RateACK;
static void rate_after_ack(void *context) {
    RateACK *event=context;rk3_usb_session *session=event->session;
    /* Delivered inside the real ACK wait, after its last pre-pump check. */
    pthread_mutex_lock(&session->mutex);session->ack_result=event->ack;
    switch(event->mutation) {
        case 0:session->active_request->cancelled=true;break;
        case 1:session->active_request->deadline=seconds_now()-1;break;
        case 2:++session->generation;break;
        case 3:atomic_store(&session->audio,event->replacement);break;
        case 4:session->opened=false;break;
        case 5:session->transport_failed=true;break;
        case 6:session->stopping_io=true;break;
        case 7:fake_audio_running=true;break;
        case 8:fake_audio_quiescent=false;break;
        case 9:session->want_audio=true;break;
        case 10:session->stop_head=&event->stop;break;
        case 11:atomic_store(&session->closing,true);break;
        case 12:
            session->command_recovering=true;session->pending=true;
            session->command_recovery_deadline=seconds_now()+1;break; /* EP1 clear callback still pending. */
        case 13:session->command_suspended=true;break;
    }
    pthread_mutex_unlock(&session->mutex);
}
static void sample_rate_late_ack_test(void) {
    for(unsigned mutation=0;mutation<12;++mutation) {
        rk3_usb_session session;rk3_native_audio_iso audio={0},replacement={0};RK3HostInterface interface;
        rate_fixture(&session,&audio,&interface);Request *request=enqueue_rate(&session,192000);
        RateACK event={.session=&session,.mutation=mutation,.ack=1,.replacement=test_context(&session,&replacement)};
        CFRunLoopSourceContext context={0};context.info=&event;context.perform=rate_after_ack;
        rate_ack_source=CFRunLoopSourceCreate(NULL,0,&context);assert(rate_ack_source);
        CFRunLoopAddSource(CFRunLoopGetCurrent(),rate_ack_source,kCFRunLoopDefaultMode);
        unsigned writes=command_writes;process_one_request(&session,true);
        int32_t expected=mutation<2?ETIMEDOUT:mutation<4?ESTALE:mutation<7||mutation==11?ENODEV:kIOReturnBusy;
        assert(request->complete&&request->result==expected&&command_writes==writes+1&&fake_rate_calls==0);
        assert(session.status.commands_completed==1&&session.sample_rate==44100&&audio.sample_rate==44100);
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(),rate_ack_source,kCFRunLoopDefaultMode);CFRelease(rate_ack_source);rate_ack_source=NULL;
        session.stop_head=NULL;fake_audio_quiescent=true;fake_audio_running=false;
        free_test_request(request);ack_test_session=NULL;finish(&session);
    }
}
static void sample_rate_failure_test(void) {
    for(unsigned failure=0;failure<4;++failure) {
        rk3_usb_session session;rk3_native_audio_iso audio={0};RK3HostInterface interface;
        rate_fixture(&session,&audio,&interface);Request *request=enqueue_rate(&session,88200);
        if(failure==0)ack_test_result=0;
        if(failure==1)command_write_result=kIOReturnIOError;
        if(failure==2){ack_test_result=-1;request->deadline=seconds_now()+0.002;}
        if(failure==3)fake_rate_result=kIOReturnBusy;
        unsigned writes=command_writes;process_one_request(&session,true);
        int32_t expected=failure==2?ETIMEDOUT:failure==3?kIOReturnBusy:EIO;
        if(!request->complete||request->result!=expected||command_writes!=writes+1)
            fprintf(stderr,"rate failure %u: complete=%d result=%d expected=%d writes=%u expected-writes=%u\n",
                failure,request->complete,request->result,expected,command_writes,writes+1);
        assert(request->complete&&request->result==expected&&command_writes==writes+1);
        assert(fake_rate_calls==(failure==3?1u:0u)&&session.sample_rate==44100&&audio.sample_rate==44100);
        assert(session.generation==9&&!session.want_audio&&!fake_start_calls&&!fake_stop_calls);
        free_test_request(request);ack_test_session=NULL;
        /* The next actual worker START sees the retained selection. Real ISO
         * tests separately verify that start sends its selected NI commands. */
        fake_start_result=0;fake_clock_valid=true;
        Request *start=enqueue_test_request(&session,REQUEST_START,NULL,0);process_one_request(&session,true);
        assert(start->complete&&start->result==0&&fake_started_rate==44100&&session.sample_rate==44100);
        assert(command_writes==writes+1&&session.generation==9);
        free_test_request(start);
        Request *stop=enqueue_test_request(&session,REQUEST_STOP,NULL,0);process_one_request(&session,true);
        assert(stop->complete&&stop->result==0);free_test_request(stop);finish(&session);
    }
    command_write_result=0;
}
static void sample_rate_explicit_retry_test(void) {
    for(unsigned failure=0;failure<2;++failure) {
        rk3_usb_session session;rk3_native_audio_iso audio={0};RK3HostInterface interface;
        rate_fixture(&session,&audio,&interface);Request *first=enqueue_rate(&session,88200);
        ack_test_result=failure?-1:0;if(failure)first->deadline=seconds_now()+0.002;
        unsigned writes=command_writes;process_one_request(&session,true);
        assert(first->complete&&first->result==(failure?ETIMEDOUT:EIO)&&command_writes==writes+1);
        assert(session.status.state==RK3_USB_READY&&session.status.error_code==(failure?ETIMEDOUT:EPROTO));
        assert(session.sample_rate==44100&&audio.sample_rate==44100&&fake_rate_calls==0);
        free_test_request(first);ack_test_result=1;
        Request *second=enqueue_rate(&session,96000);process_one_request(&session,true);
        const uint8_t expected[]={9,2,2,184,0,1};
        assert(second->complete&&second->result==0&&command_writes==writes+2);
        assert(last_command_size==6&&!memcmp(last_command_bytes,expected,6)&&fake_rate_calls==1);
        assert(session.sample_rate==96000&&audio.sample_rate==96000&&session.status.state==RK3_USB_READY);
        assert(session.status.error_code==0&&!session.status.error[0]&&!fake_start_calls&&!fake_stop_calls);
        free_test_request(second);ack_test_session=NULL;finish(&session);
    }
    for(unsigned mutation=12;mutation<14;++mutation) {
        rk3_usb_session session;rk3_native_audio_iso audio={0};RK3HostInterface interface;
        rate_fixture(&session,&audio,&interface);Request *request=enqueue_rate(&session,88200);
        RateACK event={.session=&session,.mutation=mutation,.ack=0};
        CFRunLoopSourceContext context={0};context.info=&event;context.perform=rate_after_ack;
        rate_ack_source=CFRunLoopSourceCreate(NULL,0,&context);assert(rate_ack_source);
        CFRunLoopAddSource(CFRunLoopGetCurrent(),rate_ack_source,kCFRunLoopDefaultMode);
        unsigned writes=command_writes;process_one_request(&session,true);
        assert(request->complete&&request->result==EIO&&command_writes==writes+1&&fake_rate_calls==0);
        assert(session.status.state==RK3_USB_STOPPING&&session.status.error_code==EPROTO&&session.sample_rate==44100);
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(),rate_ack_source,kCFRunLoopDefaultMode);CFRelease(rate_ack_source);rate_ack_source=NULL;
        session.pending=false;session.command_recovering=false;
        free_test_request(request);ack_test_session=NULL;finish(&session);
    }
}
static void sample_rate_publication_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso old={0},replacement={0};
    session.sample_rate=96000;RetiredAudio *first=test_context(&session,&old);
    activity_publication=&session;assert(!publish_audio(&session,first));activity_publication=NULL;
    assert(current_audio(&session)==&old&&old.sample_rate==96000&&fake_rate_calls==1);
    session.sample_rate=192000;++session.generation;atomic_store(&session.clock_generation,session.generation);
    RetiredAudio *next=test_context(&session,&replacement);fake_rate_result=kIOReturnBusy;
    assert(publish_audio(&session,next)==kIOReturnBusy&&current_audio(&session)==&old&&replacement.sample_rate==0);
    fake_rate_result=0;assert(!publish_audio(&session,next));
    assert(current_audio(&session)==&replacement&&replacement.sample_rate==192000&&next->generation==10);
    fake_clock_valid=true;rk3_usb_clock_snapshot clock;assert(rk3_usb_clock(&session,&clock)&&clock.seed==((UINT64_C(10)<<32)|1));
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.sample_rate==192000);
    finish(&session);
}

typedef struct { CFRunLoopTimerRef timer;AudioClockTimer *context; } HeldClockTimer;
static HeldClockTimer hold_clock_timer(rk3_usb_session *session) {
    assert(session->clock_timer&&session->clock_timer_context);
    CFRetain(session->clock_timer);
    return (HeldClockTimer){session->clock_timer,
        (AudioClockTimer *)retain_audio_clock_timer_context(session->clock_timer_context)};
}
static void release_held_clock_timer(HeldClockTimer held) {
    CFRelease(held.timer);release_audio_clock_timer_context(held.context);
}
static void arm_test_clock(rk3_usb_session *session,rk3_native_audio_iso *audio,uint64_t delay_ns) {
    test_audio(session,audio);fake_audio_running=fake_clock_valid=true;
    audio->incarnation=1;audio->clock_armed=true;
    audio->deadline=mach_absolute_time()+test_clock_ticks(delay_ns);
    assert(sync_audio_clock_timer(session)==0);
    assert(session->clock_timer&&session->clock_timer_context);
    assert(CFRunLoopTimerGetInterval(session->clock_timer)==0);
}
static void run_until_clock_services(unsigned target) {
    double deadline=seconds_now()+1;
    while(fake_clock_service_calls<target&&seconds_now()<deadline)
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.005,true);
    assert(fake_clock_service_calls>=target);
}
static void clock_timer_binding_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};
    arm_test_clock(&session,&audio,UINT64_C(1000000000));
    HeldClockTimer first=hold_clock_timer(&session);
    CFAbsoluteTime fire=CFRunLoopTimerGetNextFireDate(first.timer);
    unsigned created=clock_timer_create_calls;
    for(unsigned repeat=0;repeat<8;++repeat) {
        service_audio(&session);
        assert(session.clock_timer==first.timer&&session.clock_timer_context==first.context);
        assert(CFRunLoopTimerGetNextFireDate(first.timer)==fire);
        assert(clock_timer_create_calls==created&&fake_clock_service_calls==0);
    }
    /* Even an early entry uses a replacement one-shot with the original
     * deadline; a loop wakeup cannot become a maintenance callback. */
    audio_clock_timer_callback(first.timer,first.context);
    assert(fake_clock_service_calls==0&&session.clock_timer!=first.timer);
    assert(session.clock_timer_context->deadline==audio.deadline);
    assert(session.clock_timer_context->incarnation==audio.incarnation);
    assert(CFRunLoopTimerGetInterval(session.clock_timer)==0);
    assert(!CFRunLoopTimerIsValid(first.timer));
    release_held_clock_timer(first);
    /* Short fake-ISO deadlines exercise actual CF one-shot delivery and
     * replacement twice. The real ISO suite verifies NI's 128 ms cadence. */
    fake_clock_interval_ns=UINT64_C(2000000);audio.deadline=mach_absolute_time();
    assert(sync_audio_clock_timer(&session)==0);
    run_until_clock_services(2);
    assert(session.clock_timer&&session.clock_timer_context);
    assert(CFRunLoopTimerGetInterval(session.clock_timer)==0);
    assert(session.clock_timer_context->deadline==audio.deadline);
    assert(atomic_load(&session.references)==3);
    cancel_audio_clock_timer(&session);assert(atomic_load(&session.references)==2);
    finish(&session);
}
static void clock_timer_stop_restart_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};
    arm_test_clock(&session,&audio,UINT64_C(1000000000));
    HeldClockTimer old=hold_clock_timer(&session);stop_timer_session=&session;
    Request *stop=enqueue_test_request(&session,REQUEST_STOP,NULL,0);
    process_one_request(&session,true);assert(stop->complete&&stop->result==0);free_test_request(stop);
    assert(!session.clock_timer&&!session.clock_timer_context&&!fake_audio_running);
    unsigned snapshots=fake_clock_schedule_calls;
    audio_clock_timer_callback(old.timer,old.context);
    assert(fake_clock_schedule_calls==snapshots&&fake_clock_service_calls==0);
    fake_start_result=0;
    Request *start=enqueue_test_request(&session,REQUEST_START,NULL,0);
    process_one_request(&session,true);assert(start->complete&&start->result==0);free_test_request(start);
    assert(audio.incarnation==2&&session.clock_timer_context->incarnation==2);
    CFRunLoopTimerRef restarted=session.clock_timer;
    CFAbsoluteTime restarted_fire=CFRunLoopTimerGetNextFireDate(restarted);
    snapshots=fake_clock_schedule_calls;
    audio_clock_timer_callback(old.timer,old.context);
    assert(fake_clock_schedule_calls==snapshots&&session.clock_timer==restarted);
    start=enqueue_test_request(&session,REQUEST_START,NULL,0);
    process_one_request(&session,true);assert(start->complete&&start->result==0);free_test_request(start);
    assert(audio.incarnation==2&&session.clock_timer==restarted);
    assert(CFRunLoopTimerGetNextFireDate(restarted)==restarted_fire);
    assert(fake_clock_service_calls==0&&fake_start_calls==1);
    release_held_clock_timer(old);stop_timer_session=NULL;finish(&session);
}
static void clock_timer_guard_test(void) {
    for(unsigned scenario=0;scenario<7;++scenario) {
        rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0},replacement={0};
        arm_test_clock(&session,&audio,UINT64_C(1000000000));
        HeldClockTimer old=hold_clock_timer(&session);unsigned snapshots=fake_clock_schedule_calls;
        if(scenario==0)atomic_store(&session.closing,true);
        if(scenario==1)session.stopping_io=true;
        if(scenario==2)session.transport_failed=true;
        if(scenario==3)++session.generation;
        if(scenario==4)test_audio(&session,&replacement);
        if(scenario==5)++audio.incarnation;
        if(scenario==6)++audio.deadline;
        if(scenario<5)forbidden_clock_audio=&audio;
        audio_clock_timer_callback(old.timer,old.context);
        assert(!session.clock_timer&&!session.clock_timer_context&&fake_clock_service_calls==0);
        assert(fake_clock_schedule_calls==snapshots+(scenario>=5?1u:0u));
        forbidden_clock_audio=NULL;release_held_clock_timer(old);finish(&session);
    }
}
static void clock_timer_retirement_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso old_audio={0},new_audio={0};
    arm_test_clock(&session,&old_audio,UINT64_C(1000000000));
    HeldClockTimer old=hold_clock_timer(&session);
    ++session.generation;test_audio(&session,&new_audio);
    new_audio.incarnation=1;new_audio.clock_armed=true;
    new_audio.deadline=mach_absolute_time()+test_clock_ticks(UINT64_C(1000000000));
    destroy_timer_session=&session;unsigned destroyed=audio_destroy_calls;
    reclaim_retired(&session);
    assert(audio_destroy_calls==destroyed+1&&session.retired_count==1);
    assert(!session.clock_timer&&!session.clock_timer_context);
    forbidden_clock_audio=&old_audio;unsigned snapshots=fake_clock_schedule_calls;
    audio_clock_timer_callback(old.timer,old.context);
    assert(fake_clock_schedule_calls==snapshots&&fake_clock_service_calls==0);
    assert(sync_audio_clock_timer(&session)==0);
    assert(session.clock_timer_context->audio==&new_audio&&session.clock_timer_context->generation==10);
    release_held_clock_timer(old);forbidden_clock_audio=NULL;destroy_timer_session=NULL;finish(&session);
}
static rk3_usb_session *clock_hook_session;
static CFRunLoopTimerRef restarted_during_clock_service;
static unsigned restart_timer_create_count;
static void restart_from_clock_source(void *info) {
    assert(info==clock_hook_session);
    Request *stop=enqueue_test_request(clock_hook_session,REQUEST_STOP,NULL,0);
    process_one_request(clock_hook_session,true);assert(stop->complete&&stop->result==0);free_test_request(stop);
    Request *start=enqueue_test_request(clock_hook_session,REQUEST_START,NULL,0);
    process_one_request(clock_hook_session,true);assert(start->complete&&start->result==0);free_test_request(start);
    restarted_during_clock_service=clock_hook_session->clock_timer;
    restart_timer_create_count=clock_timer_create_calls;
}
static void restart_from_clock_service(void) {
    clock_service_hook=NULL;
    CFRunLoopSourceContext context={0};context.info=clock_hook_session;
    context.perform=restart_from_clock_source;
    CFRunLoopSourceRef source=CFRunLoopSourceCreate(NULL,0,&context);assert(source);
    CFRunLoopAddSource(CFRunLoopGetCurrent(),source,kCFRunLoopDefaultMode);
    CFRunLoopSourceSignal(source);
    /* Exercise the adapter's stale-incarnation guard through a nested pump.
     * This fake ISO permits immediate restart; the real ISO fixture separately
     * proves that its active clock callback prevents quiescence and restart. */
    pump_callbacks(clock_hook_session,0);
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(),source,kCFRunLoopDefaultMode);CFRelease(source);
}
static void clock_timer_reentrant_restart_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};
    arm_test_clock(&session,&audio,0);HeldClockTimer old=hold_clock_timer(&session);
    clock_hook_session=stop_timer_session=&session;fake_start_result=0;
    clock_service_hook=restart_from_clock_service;
    audio_clock_timer_callback(old.timer,old.context);
    assert(fake_clock_service_calls==1&&audio.incarnation==2&&session.clock_timer);
    assert(session.clock_timer==restarted_during_clock_service&&clock_timer_create_calls==restart_timer_create_count);
    assert(session.clock_timer_context->incarnation==2);
    release_held_clock_timer(old);clock_hook_session=stop_timer_session=NULL;finish(&session);
}
static void complete_command_from_clock_service(void) {
    clock_service_hook=NULL;
    assert(clock_hook_session->pending_write&&deferred_write_callback&&deferred_write_context==clock_hook_session);
    deferred_write_callback(deferred_write_context,kIOReturnSuccess,deferred_write_length);
}
static void clock_timer_command_pump_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};
    RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write};
    session.interface=&interface;session.opened=true;session.command_in=4;session.command_out=7;
    arm_test_clock(&session,&audio,0);
    clock_hook_session=&session;clock_service_hook=complete_command_from_clock_service;
    command_write_result=command_callback_result=kIOReturnSuccess;
    defer_write=true;signal_deferred=short_write_callback=false;deferred_write_callback=NULL;
    Request active={.deadline=seconds_now()+1};session.active_request=&active;
    const uint8_t command[]={7,0,1,0xf8};
    assert(command_worker(&session,command,sizeof(command),1000));
    assert(!session.pending_write&&fake_clock_service_calls==1&&session.clock_timer);
    assert(session.status.commands_completed==1&&!session.transport_failed&&session.generation==9);
    session.active_request=NULL;clock_hook_session=NULL;defer_write=false;finish(&session);
}
static void clock_timer_failure_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};
    test_audio(&session,&audio);fake_audio_running=fake_clock_valid=true;
    audio.incarnation=1;audio.clock_armed=true;audio.deadline=mach_absolute_time();
    session.run_loop=NULL;
    assert(sync_audio_clock_timer(&session)==EINVAL&&!session.clock_timer&&!session.clock_timer_context);
    session.run_loop=CFRunLoopGetCurrent();fail_clock_timer_create=true;
    assert(sync_audio_clock_timer(&session)==ENOMEM&&!session.clock_timer&&!session.clock_timer_context);
    assert(atomic_load(&session.references)==2&&session.generation==9&&!session.transport_failed);
    fake_audio_running=false;fake_start_result=0;stop_timer_session=&session;
    Request *start=enqueue_test_request(&session,REQUEST_START,NULL,0);
    process_one_request(&session,true);assert(start->complete&&start->result==ENOMEM);free_test_request(start);
    assert(fake_stop_calls==1&&!fake_audio_running&&session.status.state==RK3_USB_STOPPING);
    assert(session.status.error_code==ENOMEM&&!session.want_audio&&!session.transport_failed&&session.generation==9);
    assert(!session.clock_timer&&!session.clock_timer_context&&atomic_load(&session.references)==2);
    fail_clock_timer_create=false;stop_timer_session=NULL;finish(&session);
    initialize(&session);memset(&audio,0,sizeof(audio));arm_test_clock(&session,&audio,0);
    HeldClockTimer old=hold_clock_timer(&session);fail_clock_timer_create=true;stop_timer_session=&session;
    audio_clock_timer_callback(old.timer,old.context);
    assert(fake_clock_service_calls==1&&fake_stop_calls==1&&!fake_audio_running);
    assert(!session.clock_timer&&!session.clock_timer_context&&session.status.state==RK3_USB_STOPPING);
    assert(session.status.error_code==ENOMEM&&!session.transport_failed&&session.generation==9);
    release_held_clock_timer(old);fail_clock_timer_create=false;stop_timer_session=NULL;finish(&session);
}
static void panic_containment_test(void) {
    for(unsigned delay=0;delay<=100;delay+=100) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write};
        session.interface=&interface;session.opened=true;session.command_in=4;session.command_out=7;
        session.status.state=RK3_USB_RUNNING;session.want_audio=true;
        session.digital_known=true;session.digital_snapshot=0x80;
        session.worker_thread=pthread_self();session.worker_thread_known=true;
        rk3_native_audio_iso audio={0};test_audio(&session,&audio);
        assert(rk3_usb_input_gain_set(&session,9,0,UINT32_C(0x3f000000),NULL)==0);
        fake_audio_running=true;fake_panic_pending=true;fake_panic_delay=delay;fake_panic_error=kIOReturnIsoTooOld;
        PanicObservation observation={.session=&session};
        assert(rk3_usb_set_audio_panic_handler(&session,observe_panic,&observation));
        audio_panic_tick(&session,100);
        assert(observation.calls==(delay==0?1u:0u));
        if(delay) {audio_panic_tick(&session,100.099);assert(observation.calls==0);audio_panic_tick(&session,100.101);}
        assert(observation.calls==1&&observation.generation==9&&observation.error==kIOReturnIsoTooOld);
        audio_panic_tick(&session,200);assert(observation.calls==1);
        assert(!session.transport_failed&&session.generation==9&&session.digital_known&&session.digital_snapshot==0x80);
        RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.flags==RK3_NATIVE_CONNECTED);
        /* Shared reports and an actual queued LED write still work after panic. */
        session.read_buffer[0]=3;session.read_buffer[1]=42;report_complete(&session,0,2);
        assert(session.sequence==1&&session.history_count==1&&session.pending);
        const uint8_t led[]={5,0,0x40,0x40,0,0x10,0,0,0};
        Request *command=enqueue_test_request(&session,REQUEST_COMMAND,led,sizeof(led));
        process_one_request(&session,true);assert(command->complete&&command->result==0);
        free_test_request(command);
        assert(session.generation==9&&!session.transport_failed);
        uint32_t gain=0;assert(rk3_usb_input_gain_get(&session,9,0,&gain)==0&&gain==UINT32_C(0x3f000000));
        Request *stop=enqueue_test_request(&session,REQUEST_STOP,NULL,0);
        process_one_request(&session,true);assert(stop->complete&&stop->result==0);free_test_request(stop);
        fake_start_result=0;fake_clock_valid=true;
        Request *start=enqueue_test_request(&session,REQUEST_START,NULL,0);
        process_one_request(&session,true);assert(start->complete&&start->result==0);free_test_request(start);
        assert(session.status.state==RK3_USB_RUNNING&&!session.panic_seen&&session.generation==9);
        fake_panic_pending=true;fake_panic_delay=0;fake_panic_error=kIOReturnIOError;
        audio_panic_tick(&session,300);assert(observation.calls==2);
        assert(rk3_usb_set_audio_panic_handler(&session,NULL,NULL));
        session.stopping_io=true;report_complete(&session,kIOReturnAborted,0);
        test_audio(&session,NULL);finish(&session);
    }
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};test_audio(&session,&audio);
    session.status.state=RK3_USB_RUNNING;session.want_audio=true;fake_audio_running=true;
    fake_audio_error=kIOReturnIsoTooOld; /* OUT errors without an NI panic do not request recovery. */
    audio_panic_tick(&session,100);assert(!session.panic_pending&&!session.transport_failed&&session.status.state==RK3_USB_RUNNING);
    fake_audio_error=kIOReturnNoDevice;audio_panic_tick(&session,101);
    assert(session.transport_failed&&session.status.state==RK3_USB_FAILED&&session.generation==9);
    test_audio(&session,NULL);finish(&session);
}
static void cooperative_stop_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};test_audio(&session,&audio);
    RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write};session.interface=&interface;session.opened=true;
    session.status.state=RK3_USB_RUNNING;session.want_audio=true;
    session.panic_pending=true;session.panic_deadline=100;fake_audio_quiescent=false;
    Request *stop=enqueue_test_request(&session,REQUEST_STOP,NULL,0);
    process_one_request(&session,true);
    assert(!stop->complete&&session.deferred_stops==1&&!session.want_audio&&!session.panic_pending&&fake_stop_calls==1);
    const uint8_t midi[]={7,0,1,0xf8};Request *command=enqueue_test_request(&session,REQUEST_COMMAND,midi,sizeof(midi));
    unsigned before=command_writes;process_one_request(&session,true);
    assert(command->complete&&command->result==0&&command_writes==before+1&&!stop->complete);
    free_test_request(command);
    complete_stops(&session,true,session.stop_deadline-0.001);assert(!stop->complete);
    fake_audio_quiescent=true;fake_start_result=0;fake_clock_valid=true;
    Request *start=enqueue_test_request(&session,REQUEST_START,NULL,0);process_one_request(&session,true);
    assert(start->complete&&start->result==kIOReturnBusy&&fake_start_calls==0);
    free_test_request(start);
    complete_stops(&session,true,session.stop_deadline-0.001);
    assert(stop->complete&&stop->result==0&&session.status.state==RK3_USB_READY&&!session.transport_failed);
    free_test_request(stop);
    /* A stalled ISO drain retains storage and healthy MIDI instead of resetting USB. */
    fake_audio_quiescent=false;stop=enqueue_test_request(&session,REQUEST_STOP,NULL,0);process_one_request(&session,true);
    complete_stops(&session,true,session.stop_deadline+0.001);
    assert(stop->complete&&stop->result==ETIMEDOUT&&!session.transport_failed&&current_audio(&session)==&audio);
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.flags==RK3_NATIVE_CONNECTED);
    free_test_request(stop);fake_audio_quiescent=true;
    test_audio(&session,NULL);finish(&session);
}
static void panic_startup_and_address_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};test_audio(&session,&audio);
    session.status.state=RK3_USB_STARTING;fake_audio_running=true;fake_clock_valid=true;
    fake_panic_pending=true;fake_panic_error=kIOReturnIsoTooOld;fake_panic_delay=100;
    assert(!wait_audio_clock(&session,&audio));
    assert(session.status.error_code==kIOReturnIsoTooOld&&!session.transport_failed);
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.flags==RK3_NATIVE_CONNECTED);
    float samples[8]={1,1,1,1,1,1,1,1};
    assert(rk3_usb_render_at(&session,samples,UINT64_MAX,4)==4&&addressed_first==UINT64_MAX&&addressed_frames==4);
    assert(rk3_usb_capture_at(&session,samples,12345,4)==1&&addressed_first==12345&&addressed_frames==4);
    assert(samples[0]==0.5f&&samples[1]==-0.5f);
    for(size_t i=2;i<8;++i)assert(samples[i]==0);
    assert(atomic_load(&session.rt_readers)==0);
    test_audio(&session,NULL);finish(&session);
}

typedef struct {
    rk3_usb_session *session;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool entered,allow_return,unregister_started,unregistered;
} CallbackLease;
static void leased_panic(void *context,uint64_t generation,int32_t error) {
    CallbackLease *lease=context;assert(generation==9&&error==kIOReturnIOError);
    pthread_mutex_lock(&lease->mutex);lease->entered=true;pthread_cond_broadcast(&lease->condition);
    while(!lease->allow_return)pthread_cond_wait(&lease->condition,&lease->mutex);
    pthread_mutex_unlock(&lease->mutex);
}
static void *run_leased_panic(void *context) {
    CallbackLease *lease=context;
    pthread_mutex_lock(&lease->session->mutex);lease->session->worker_thread=pthread_self();lease->session->worker_thread_known=true;pthread_mutex_unlock(&lease->session->mutex);
    audio_panic_tick(lease->session,100);return NULL;
}
static void *unregister_leased_panic(void *context) {
    CallbackLease *lease=context;
    pthread_mutex_lock(&lease->mutex);lease->unregister_started=true;pthread_cond_broadcast(&lease->condition);pthread_mutex_unlock(&lease->mutex);
    assert(rk3_usb_set_audio_panic_handler(lease->session,NULL,NULL));
    pthread_mutex_lock(&lease->mutex);lease->unregistered=true;pthread_cond_broadcast(&lease->condition);pthread_mutex_unlock(&lease->mutex);
    return NULL;
}
static void callback_lease_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};test_audio(&session,&audio);
    session.want_audio=true;session.status.state=RK3_USB_RUNNING;
    fake_panic_pending=true;fake_panic_delay=0;fake_panic_error=kIOReturnIOError;
    CallbackLease lease={.session=&session};assert(pthread_mutex_init(&lease.mutex,NULL)==0);assert(pthread_cond_init(&lease.condition,NULL)==0);
    assert(rk3_usb_set_audio_panic_handler(&session,leased_panic,&lease));
    pthread_t callback_thread,unregister_thread;assert(pthread_create(&callback_thread,NULL,run_leased_panic,&lease)==0);
    pthread_mutex_lock(&lease.mutex);while(!lease.entered)pthread_cond_wait(&lease.condition,&lease.mutex);pthread_mutex_unlock(&lease.mutex);
    assert(pthread_create(&unregister_thread,NULL,unregister_leased_panic,&lease)==0);
    pthread_mutex_lock(&lease.mutex);while(!lease.unregister_started)pthread_cond_wait(&lease.condition,&lease.mutex);pthread_mutex_unlock(&lease.mutex);
    struct timespec delay={0,5000000};nanosleep(&delay,NULL);
    pthread_mutex_lock(&lease.mutex);assert(!lease.unregistered);lease.allow_return=true;pthread_cond_broadcast(&lease.condition);pthread_mutex_unlock(&lease.mutex);
    pthread_join(callback_thread,NULL);pthread_join(unregister_thread,NULL);
    assert(lease.unregistered&&session.panic_handler==NULL&&session.panic_callbacks==0);
    pthread_cond_destroy(&lease.condition);pthread_mutex_destroy(&lease.mutex);
    test_audio(&session,NULL);finish(&session);
}

static void activity_publication_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso first={0},second={0};
    assert(atomic_load(&session.output_active));
    unsigned before=activity_calls;rk3_usb_set_output_active(&session,false);
    assert(!atomic_load(&session.output_active)&&activity_calls==before&&atomic_load(&session.rt_readers)==0);
    activity_publication=&session;assert(!publish_audio(&session,test_context(&session,&first)));activity_publication=NULL;
    assert(current_audio(&session)==&first&&!fake_output_active&&activity_calls==before+1);
    rk3_usb_set_output_active(&session,true);assert(fake_output_active&&atomic_load(&session.output_active));
    test_audio(&session,NULL);rk3_usb_set_output_active(&session,false);
    activity_publication=&session;assert(!publish_audio(&session,test_context(&session,&second)));activity_publication=NULL;
    assert(current_audio(&session)==&second&&!fake_output_active&&atomic_load(&session.rt_readers)==0);
    test_audio(&session,NULL);finish(&session);
}
static void host_storage_lifetime_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};test_audio(&session,&audio);
    RK3HostInterface interface={0};session.interface=&interface;session.opened=session.changed_alt=true;
    check_host_lifetime=true;mapped_released=false;release_busy=close_busy=1;
    release_usb_calls=host_close_calls=host_release_calls=host_alt_calls=0;
    disconnect_device(&session);
    assert(release_usb_calls==2&&host_close_calls==2&&host_release_calls==1&&host_alt_calls==1);
    assert(session.interface==NULL&&atomic_load(&session.audio)==NULL&&session.generation==9);
    check_host_lifetime=false;finish(&session);
}

static unsigned ep1_clears,ep1_aborts;
static uint8_t ep1_clear_pipes[4],ep1_abort_pipes[4];
static int32_t ep1_clear_result;
static int32_t fake_ep1_clear(void *context,uint8_t pipe) {
    (void)context;assert(ep1_clears<4);ep1_clear_pipes[ep1_clears++]=pipe;return ep1_clear_result;
}
static int32_t fake_ep1_abort(void *context,uint8_t pipe) {
    (void)context;assert(ep1_aborts<4);ep1_abort_pipes[ep1_aborts++]=pipe;return 0;
}
static void ep1_read_status_test(void) {
    static const struct {int32_t status;unsigned rearms,clears,aborts;bool fatal;} cases[]={
        {(int32_t)UINT32_C(0xe000404f),1,1,0,false},
        {kIOReturnIOError,1,0,0,false},{kIOReturnTimeout,1,0,0,false},
        {kIOReturnAborted,0,0,0,false},{kIOReturnNotResponding,0,0,2,false},
        {kIOReturnNoDevice,0,0,0,true},{kIOReturnNotOpen,0,0,0,true},
        {(int32_t)UINT32_C(0xe000404e),0,0,0,true}
    };
    for(size_t index=0;index<sizeof(cases)/sizeof(*cases);++index) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write,.clear_stall=fake_ep1_clear,.abort_pipe=fake_ep1_abort};
        session.interface=&interface;session.opened=true;session.command_in=4;session.command_out=7;session.status.state=RK3_USB_READY;
        session.read_buffer[0]=3;session.read_buffer[1]=11;report_complete(&session,0,2);
        unsigned reads=read_arms;ep1_clears=ep1_aborts=0;ep1_clear_result=0;
        report_complete(&session,cases[index].status,0);pump_callbacks(&session,0);
        assert(session.sequence==1&&session.history_count==1&&session.generation==9);
        assert(session.transport_failed==cases[index].fatal&&read_arms==reads+cases[index].rearms);
        assert(ep1_clears==cases[index].clears&&ep1_aborts==cases[index].aborts);
        if(cases[index].fatal)assert(session.status.state==RK3_USB_FAILED);
        else {
            RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.flags==RK3_NATIVE_CONNECTED);
            assert(session.pending==(cases[index].rearms!=0));
            if(cases[index].clears)assert(ep1_clear_pipes[0]==4);
            if(cases[index].aborts)assert(ep1_abort_pipes[0]==4&&ep1_abort_pipes[1]==7&&session.command_suspended);
        }
        session.stopping_io=true;if(session.pending)report_complete(&session,kIOReturnAborted,0);
        finish(&session);
    }
}

static void ep1_write_no_replay_test(void) {
    const int32_t statuses[]={(int32_t)UINT32_C(0xe000404f),(int32_t)UINT32_C(0xe0004051),kIOReturnTimeout};
    for(size_t index=0;index<sizeof(statuses)/sizeof(*statuses);++index)for(unsigned submission=0;submission<2;++submission) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write,.clear_stall=fake_ep1_clear,.abort_pipe=fake_ep1_abort};
        session.interface=&interface;session.opened=true;session.command_in=4;session.command_out=7;session.status.state=RK3_USB_READY;
        session.read_buffer[0]=4;session.read_buffer[1]=0x80;session.read_buffer[2]=0;report_complete(&session,0,3);
        uint64_t sequence=session.sequence;unsigned reads=read_arms,writes=command_writes;
        ep1_clears=ep1_aborts=0;ep1_clear_result=0;
        command_write_result=submission?statuses[index]:0;command_callback_result=submission?0:statuses[index];
        const uint8_t failed[]={7,0,1,0xf8};assert(!command_worker(&session,failed,sizeof(failed),1000));
        assert(!session.transport_failed&&session.pending&&read_arms==reads&&command_writes==writes+1);
        assert(ep1_clears==2&&ep1_clear_pipes[0]==4&&ep1_clear_pipes[1]==7);
        RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.flags==RK3_NATIVE_CONNECTED&&info.generation==9);
        assert(session.sequence==sequence&&session.history_count==1&&session.digital_snapshot==0x80);
        /* Clear abort completion arrives AFTER the failed caller returns. No
         * new reader may reuse its buffer until this exact callback retires. */
        report_complete(&session,kIOReturnAborted,0);pump_callbacks(&session,0);
        assert(!session.transport_failed&&session.pending&&read_arms==reads+1&&command_writes==writes+1);
        command_write_result=command_callback_result=0;
        const uint8_t next[]={7,0,1,0xfa};assert(command_worker(&session,next,sizeof(next),1000));
        assert(command_writes==writes+2&&session.status.commands_completed==1&&session.generation==9);
        assert(last_command_size==sizeof(next)&&memcmp(last_command_bytes,next,sizeof(next))==0);
        session.stopping_io=true;report_complete(&session,kIOReturnAborted,0);finish(&session);
    }
    command_write_result=command_callback_result=0;
}

static void ep1_late_success_and_queue_test(void) {
    rk3_usb_session session;initialize(&session);
    RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write,.clear_stall=fake_ep1_clear,.abort_pipe=fake_ep1_abort};
    session.interface=&interface;session.opened=true;session.command_in=4;session.command_out=7;session.status.state=RK3_USB_READY;
    session.read_buffer[0]=4;session.read_buffer[1]=0x80;session.read_buffer[2]=0;report_complete(&session,0,3);
    unsigned reads=read_arms,writes=command_writes;ep1_clears=ep1_aborts=0;ep1_clear_result=0;
    begin_command_recovery(&session,true,true);
    const uint8_t next[]={7,0,1,0xfa};Request *command=enqueue_test_request(&session,REQUEST_COMMAND,next,sizeof(next));
    Request *stop=enqueue_test_request(&session,REQUEST_STOP,NULL,0);
    process_one_request(&session,true);
    assert(stop->complete&&stop->result==0&&!command->complete&&session.head==command&&session.tail==command);
    assert(command_writes==writes&&session.queued==1);free_test_request(stop);
    /* The old read may have succeeded just before clear. Keep its real input
     * report, but do not arm a replacement until the clear gate completes. */
    session.read_buffer[1]=0x20;report_complete(&session,0,3);
    assert(session.sequence==2&&session.history_count==2&&session.digital_snapshot==0x20&&read_arms==reads);
    process_one_request(&session,true);assert(!command->complete&&command_writes==writes);
    pump_callbacks(&session,0);assert(session.pending&&read_arms==reads+1);
    process_one_request(&session,true);assert(command->complete&&command->result==0&&command_writes==writes+1);
    free_test_request(command);
    assert(session.generation==9&&!session.transport_failed);
    session.stopping_io=true;report_complete(&session,kIOReturnAborted,0);finish(&session);
}
static void ep1_suspended_and_removal_test(void) {
    rk3_usb_session session;initialize(&session);
    RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write,.clear_stall=fake_ep1_clear,.abort_pipe=fake_ep1_abort};
    session.interface=&interface;session.opened=true;session.command_in=4;session.command_out=7;session.status.state=RK3_USB_READY;
    session.read_buffer[0]=3;report_complete(&session,0,1);unsigned reads=read_arms;
    ep1_clears=ep1_aborts=0;ep1_clear_result=0;
    report_complete(&session,kIOReturnNotResponding,0);check_connection_presence(&session);
    assert(session.command_suspended&&!session.pending&&!session.transport_failed&&session.generation==9);
    /* A later failed send clears endpoints but must not invent a restart of
     * the reader that NI had explicitly suspended before that send. */
    command_callback_result=(int32_t)UINT32_C(0xe000404f);
    const uint8_t command[]={7,0,1,0xf8};assert(!command_worker(&session,command,sizeof(command),1000));
    service_command_recovery(&session,seconds_now());
    assert(session.command_suspended&&!session.pending&&read_arms==reads&&!session.transport_failed);
    command_callback_result=0;
    interface.disconnected=true;check_connection_presence(&session);
    assert(session.transport_failed&&session.status.error_code==kIOReturnNoDevice&&session.generation==9);
    assert(session.history_count==1);finish(&session);
}
static void ep1_clear_failure_test(void) {
    const int32_t failures[]={kIOReturnIOError,kIOReturnNoDevice};
    for(size_t index=0;index<2;++index) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write,.clear_stall=fake_ep1_clear};
        session.interface=&interface;session.opened=true;session.command_in=4;session.command_out=7;session.status.state=RK3_USB_READY;
        session.pending=true;ep1_clears=ep1_aborts=0;ep1_clear_result=failures[index];unsigned reads=read_arms;
        report_complete(&session,(int32_t)UINT32_C(0xe000404f),0);pump_callbacks(&session,0);
        assert(ep1_clears==1&&!session.pending&&session.command_suspended&&read_arms==reads);
        assert(session.transport_failed==(index==1)&&session.generation==9);
        finish(&session);
    }
    ep1_clear_result=0;
}

static void stream_diagnostic_visibility_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};test_audio(&session,&audio);
    RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write};session.interface=&interface;session.opened=true;
    session.status.state=RK3_USB_RUNNING;session.status.info.midi_inputs=1;fake_audio_running=true;
    session.digital_known=true;session.digital_snapshot=0x10;
    fake_stream_error=kIOReturnIsoTooOld;service_audio(&session);
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info));
    assert(info.flags==(RK3_NATIVE_CONNECTED|RK3_NATIVE_AUDIO_RUNNING)&&info.generation==9);
    assert(info.last_error==kIOReturnIsoTooOld&&info.midi_inputs==1&&info.midi_outputs==1&&info.digital_snapshot==UINT32_C(0x80000010));
    assert(!session.transport_failed&&session.status.state==RK3_USB_RUNNING&&strcmp(session.status.error,"Audio transfer diagnostic")==0);
    fake_stream_error=kIOReturnIOError;service_audio(&session);assert(session.status.error_code==kIOReturnIsoTooOld);
    const uint8_t midi[]={7,0,1,0xf8};Request *request=enqueue_test_request(&session,REQUEST_COMMAND,midi,sizeof(midi));
    process_one_request(&session,true);assert(request->complete&&request->result==0);free_test_request(request);
    assert(session.generation==9&&!session.transport_failed&&session.status.state==RK3_USB_RUNNING);
    /* Only a real new successful engine start clears its diagnostic latch. */
    request=enqueue_test_request(&session,REQUEST_STOP,NULL,0);process_one_request(&session,true);free_test_request(request);
    fake_start_result=0;fake_clock_valid=true;
    request=enqueue_test_request(&session,REQUEST_START,NULL,0);process_one_request(&session,true);assert(request->complete&&request->result==0);free_test_request(request);
    service_audio(&session);assert(session.status.error_code==0&&fake_stream_error==0&&session.generation==9);
    test_audio(&session,NULL);finish(&session);
}

static void connection_state_test(void) {
    rk3_usb_session session; initialize(&session);
    static const struct { rk3_usb_state state; uint32_t flags; } cases[] = {
        {RK3_USB_OPENING, 0},
        {RK3_USB_READY, RK3_NATIVE_CONNECTED},
        {RK3_USB_STARTING, RK3_NATIVE_CONNECTED},
        {RK3_USB_RUNNING, RK3_NATIVE_CONNECTED | RK3_NATIVE_AUDIO_RUNNING},
        {RK3_USB_STOPPING, RK3_NATIVE_CONNECTED},
        {RK3_USB_FAILED, RK3_NATIVE_FAILED},
        {RK3_USB_CLOSED, 0},
    };
    for (size_t index = 0; index < sizeof(cases)/sizeof(*cases); ++index) {
        session.status.state = cases[index].state;
        RK3NativeInfo info;
        assert(rk3_usb_info(&session, &info) && info.flags == cases[index].flags);
        assert(info.generation == session.generation);
    }
    finish(&session);
}

/* Actual production NativeClient -> HAL -> NativeUSB property/capture seam.
 * Only the ISO/Host hardware objects are fakes, as above. */
#define initialize gain_hal_initialize
#include "../Audio/RK3AudioDriver.c"
#undef initialize
static OSStatus gain_bridge_get(AudioObjectID device,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 *size,void *out){
    UInt32 written=0;OSStatus error=table.GetPropertyData(driver,device,0,a,qs,q,*size,&written,out);
    if(!error)*size=written;return error;
}
static OSStatus gain_bridge_set(AudioObjectID device,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 size,const void *data){
    return table.SetPropertyData(driver,device,0,a,qs,q,size,data);
}
#define AudioObjectGetPropertyData gain_bridge_get
#define AudioObjectSetPropertyData gain_bridge_set
#include "../NativeClient.h"
#undef AudioObjectGetPropertyData
#undef AudioObjectSetPropertyData
static unsigned gain_notifications;
static AudioServerPlugInHostInterface gain_host;
static OSStatus gain_changed(AudioServerPlugInHostRef owner,AudioObjectID object_id,UInt32 count,const AudioObjectPropertyAddress *addresses){
    assert(owner==&gain_host&&object_id==DEVICE&&count==1&&addresses->mSelector==RK3_PROPERTY_INPUT_GAIN);
    ++gain_notifications;RK3NativeGainQuery query={1,0,9};RK3NativeGainValue value;
    /* Reentrant read proves notification occurs outside the session gate. */
    assert(rk3_native_copy_input_gain(DEVICE,&query,&value)==0);return 0;
}
static void gain_property_test(void){
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};test_audio(&session,&audio);
    session.status.state=RK3_USB_READY;usb=&session;gain_host.PropertiesChanged=gain_changed;host=&gain_host;gain_notifications=0;
    RK3NativeGainQuery query={1,0,9};RK3NativeGainValue value={0};
    assert(rk3_native_copy_input_gain(DEVICE,&query,&value)==0&&value.value_bits==UINT32_C(0x3f800000));
    static const uint32_t raw[]={0,UINT32_C(0x80000000),1,UINT32_C(0x3f000000),UINT32_C(0xbf800000),UINT32_C(0x40000000),
        UINT32_C(0x7f800000),UINT32_C(0xff800000),UINT32_C(0x7fc01234),UINT32_C(0x7f800001),UINT32_C(0xff800001)};
    for(size_t i=0;i<sizeof(raw)/sizeof(*raw);++i){
        value.value_bits=raw[i];assert(rk3_native_set_input_gain(DEVICE,&query,&value)==0);
        value.value_bits=0;assert(rk3_native_copy_input_gain(DEVICE,&query,&value)==0&&value.value_bits==raw[i]);
    }
    value.value_bits=UINT32_C(0x3f000000);assert(rk3_native_set_input_gain(DEVICE,&query,&value)==0);
    unsigned notifications=gain_notifications;
    assert(rk3_native_set_input_gain(DEVICE,&query,&value)==0&&gain_notifications==notifications);
    bool changed=true;
    assert(rk3_usb_input_gain_set(&session,9,0,value.value_bits,&changed)==0&&!changed);
    assert(rk3_usb_input_gain_set(&session,9,2,0,&changed)==EINVAL&&!changed);
    RK3NativeGainValue saved=value,bad=value;RK3NativeGainQuery wrong=query;
    wrong.generation=8;bad.generation=8;
    assert(rk3_native_set_input_gain(DEVICE,&wrong,&bad)==kAudioHardwareNotRunningError);
    assert(rk3_native_copy_input_gain(DEVICE,&wrong,&bad)==kAudioHardwareNotRunningError&&bad.generation==8);
    const uint32_t invalid[]={2,63,64,UINT32_MAX};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i){
        wrong=query;wrong.channel=invalid[i];bad=saved;bad.channel=invalid[i];
        assert(rk3_native_set_input_gain(DEVICE,&wrong,&bad)==kAudioHardwareIllegalOperationError);
        assert(rk3_native_copy_input_gain(DEVICE,&wrong,&bad)==kAudioHardwareIllegalOperationError);
    }
    wrong=query;wrong.version=2;assert(rk3_native_copy_input_gain(DEVICE,&wrong,&bad)==kAudioHardwareIllegalOperationError);
    wrong=query;wrong.generation=0;assert(rk3_native_copy_input_gain(DEVICE,&wrong,&bad)==kAudioHardwareIllegalOperationError);
    bad=saved;bad.reserved=1;assert(rk3_native_set_input_gain(DEVICE,&query,&bad)==kAudioHardwareIllegalOperationError);
    bad=saved;bad.version=2;assert(rk3_native_set_input_gain(DEVICE,&query,&bad)==kAudioHardwareIllegalOperationError);
    bad=saved;bad.channel=1;assert(rk3_native_set_input_gain(DEVICE,&query,&bad)==kAudioHardwareIllegalOperationError);
    bad=saved;bad.generation=8;assert(rk3_native_set_input_gain(DEVICE,&query,&bad)==kAudioHardwareIllegalOperationError);
    assert(rk3_native_copy_input_gain(DEVICE,&query,&value)==0&&memcmp(&value,&saved,sizeof(value))==0);
    AudioObjectPropertyAddress a={RK3_PROPERTY_INPUT_GAIN,kAudioObjectPropertyScopeGlobal,0};
    Boolean settable=false;UInt32 size=0,written=0;CFPropertyListRef output=NULL,wrong_type=CFSTR("bad");
    assert(table.HasProperty(driver,DEVICE,0,&a));
    assert(table.IsPropertySettable(driver,DEVICE,0,&a,&settable)==0&&settable);
    assert(table.GetPropertyDataSize(driver,DEVICE,0,&a,0,NULL,&size)==0&&size==sizeof(CFPropertyListRef));
    CFDataRef qualifier=CFDataCreate(NULL,(const UInt8 *)&query,sizeof(query));
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(wrong_type),&wrong_type,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError&&output==NULL);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(query),&query,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError);
    CFDataRef short_query=CFDataCreate(NULL,(const UInt8 *)&query,sizeof(query)-1);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(short_query),&short_query,sizeof(output),&written,&output)==kAudioHardwareBadPropertySizeError);CFRelease(short_query);
    assert(table.GetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(output)-1,&written,&output)==kAudioHardwareBadPropertySizeError);
    assert(table.SetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(wrong_type),&wrong_type)==kAudioHardwareBadPropertySizeError);
    CFDataRef short_value=CFDataCreate(NULL,(const UInt8 *)&saved,sizeof(saved)-1);
    assert(table.SetPropertyData(driver,DEVICE,0,&a,sizeof(qualifier),&qualifier,sizeof(short_value),&short_value)==kAudioHardwareBadPropertySizeError);CFRelease(short_value);CFRelease(qualifier);
    a.mScope=kAudioObjectPropertyScopeInput;assert(!table.HasProperty(driver,DEVICE,0,&a));
    a.mScope=kAudioObjectPropertyScopeGlobal;a.mElement=1;assert(!table.HasProperty(driver,DEVICE,0,&a));
    assert(rk3_native_copy_input_gain(999,&query,&value)==kAudioHardwareBadObjectError);
    assert(rk3_native_copy_input_gain(DEVICE,NULL,&value)==kAudioHardwareIllegalOperationError);
    /* Real property write reaches the production post-conversion capture path. */
    query.channel=1;value=(RK3NativeGainValue){1,1,9,UINT32_C(0x40000000),0};
    assert(rk3_native_set_input_gain(DEVICE,&query,&value)==0);
    AudioServerPlugInIOCycleInfo cycle={0};cycle.mInputTime.mFlags=cycle.mOutputTime.mFlags=kAudioTimeStampSampleTimeValid;
    cycle.mInputTime.mSampleTime=-0.25;cycle.mOutputTime.mSampleTime=123;
    float samples[8];
    for(unsigned i=0;i<2;++i){
        assert(table.DoIOOperation(driver,DEVICE,INPUT,1,kAudioServerPlugInIOOperationReadInput,4,&cycle,samples,NULL)==0);
        assert(addressed_first==UINT64_MAX&&samples[0]==0.25f&&samples[1]==-1);
        for(unsigned j=2;j<8;++j)assert(samples[j]==0); /* Missing-data tail stays silent. */
    }
    samples[0]=3;samples[1]=4;
    assert(table.DoIOOperation(driver,DEVICE,OUTPUT,1,kAudioServerPlugInIOOperationWriteMix,1,&cycle,samples,NULL)==0);
    assert(samples[0]==3&&samples[1]==4&&addressed_first==123);
    /* Same logical context survives actual owner Start/Stop request paths. */
    fake_start_result=0;fake_clock_valid=true;
    Request *request=enqueue_test_request(&session,REQUEST_START,NULL,0);process_one_request(&session,true);
    assert(request->complete&&request->result==0);free_test_request(request);
    request=enqueue_test_request(&session,REQUEST_STOP,NULL,0);process_one_request(&session,true);complete_stops(&session,true,seconds_now());
    assert(request->complete&&request->result==0);free_test_request(request);
    assert(rk3_native_copy_input_gain(DEVICE,&query,&value)==0&&value.value_bits==UINT32_C(0x40000000));
    test_audio(&session,NULL);assert(rk3_native_copy_input_gain(DEVICE,&query,&value)==kAudioHardwareNotRunningError);
    assert(rk3_native_set_input_gain(DEVICE,&query,&value)==kAudioHardwareNotRunningError);
    usb=NULL;host=NULL;finish(&session);
}
typedef struct {pthread_mutex_t mutex;pthread_cond_t condition;bool entered,resume;rk3_usb_session *session;float values[4];} GainBorrow;
static GainBorrow *gain_borrow;
static void pause_gain_capture(void){
    GainBorrow *borrow=gain_borrow;pthread_mutex_lock(&borrow->mutex);borrow->entered=true;pthread_cond_broadcast(&borrow->condition);
    while(!borrow->resume)pthread_cond_wait(&borrow->condition,&borrow->mutex);pthread_mutex_unlock(&borrow->mutex);
}
static void *capture_old_gain(void *context){GainBorrow *borrow=context;assert(rk3_usb_capture_at(borrow->session,borrow->values,0,2)==1);return NULL;}
static void gain_generation_borrow_test(void){
    for(unsigned phase=0;phase<2;++phase){
        rk3_usb_session session;initialize(&session);rk3_native_audio_iso old={0},new_audio={0};test_audio(&session,&old);
        session.status.state=RK3_USB_READY;
        assert(rk3_usb_input_gain_set(&session,9,0,UINT32_C(0x3f000000),NULL)==0);
        assert(rk3_usb_input_gain_set(&session,9,1,UINT32_C(0x40000000),NULL)==0);
        GainBorrow borrow={.session=&session};assert(!pthread_mutex_init(&borrow.mutex,NULL));assert(!pthread_cond_init(&borrow.condition,NULL));gain_borrow=&borrow;
        atomic_store(phase?&gain_snapshot_hook:&capture_hook,pause_gain_capture);
        pthread_t thread;assert(!pthread_create(&thread,NULL,capture_old_gain,&borrow));
        pthread_mutex_lock(&borrow.mutex);while(!borrow.entered)pthread_cond_wait(&borrow.condition,&borrow.mutex);pthread_mutex_unlock(&borrow.mutex);
        assert(atomic_load(&session.rt_readers)==1);
        RetiredAudio *retired=atomic_load(&session.audio);
        disconnect_device(&session); /* Actual owner unpublish must not wait for the pinned capture. */
        assert(atomic_load(&session.audio)==NULL&&session.retired_count==1);
        pthread_mutex_lock(&session.mutex);session.generation=10;session.status.state=RK3_USB_READY;pthread_mutex_unlock(&session.mutex);
        RetiredAudio *current=test_context(&session,&new_audio);assert(!publish_audio(&session,current));
        assert(current!=retired&&current->generation==10&&session.retired_count==2);
        assert(rk3_usb_input_gain_set(&session,10,0,UINT32_C(0x40400000),NULL)==0);
        uint32_t unchanged=91;assert(rk3_usb_input_gain_get(&session,9,0,&unchanged)==ESTALE&&unchanged==91);
        assert(rk3_usb_input_gain_set(&session,9,0,0,NULL)==ESTALE);
        unsigned before=audio_destroy_calls;reclaim_retired(&session);assert(audio_destroy_calls==before&&session.retired_count==2);
        atomic_store(&capture_hook,NULL);atomic_store(&gain_snapshot_hook,NULL);
        float fresh[4];assert(rk3_usb_capture_at(&session,fresh,0,2)==1&&fresh[0]==1.5f&&fresh[1]==-0.5f);
        pthread_mutex_lock(&borrow.mutex);borrow.resume=true;pthread_cond_broadcast(&borrow.condition);pthread_mutex_unlock(&borrow.mutex);
        assert(!pthread_join(thread,NULL));assert(borrow.values[0]==0.25f&&borrow.values[1]==-1&&borrow.values[2]==0&&borrow.values[3]==0);
        reclaim_retired(&session);assert(audio_destroy_calls==before+1&&session.retired_count==1&&session.retired==current);
        reclaim_retired(&session);assert(audio_destroy_calls==before+1);
        test_audio(&session,NULL);reclaim_retired(&session);assert(audio_destroy_calls==before+2&&session.retired_count==0);
        pthread_cond_destroy(&borrow.condition);pthread_mutex_destroy(&borrow.mutex);gain_borrow=NULL;finish(&session);
    }
}
static const uint8_t spec_startup_info[] = {1,0x0d,0,6,0,3,9,0x29,2,2,0,0,1,1,2};
static void spec_info(rk3_usb_session *session,const uint8_t payload[14]) {
    session->read_buffer[0]=RK3_COMMAND_INFO;memcpy(session->read_buffer+1,payload,14);
    session->pending=true;report_complete(session,0,15);
}
static void spec_attempt(rk3_usb_session *session) {
    pthread_mutex_lock(&session->mutex);spec_begin_attempt_locked(session);pthread_mutex_unlock(&session->mutex);
}
static void spec_commit(rk3_usb_session *session) {
    pthread_mutex_lock(&session->mutex);++session->generation;spec_commit_locked(session);pthread_mutex_unlock(&session->mutex);
}
static void spec_publication_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;
    uint8_t out[14],unchanged[14];memset(out,0xa5,sizeof(out));memcpy(unchanged,out,sizeof(out));
    spec_attempt(&session);spec_info(&session,spec_startup_info+1);
    assert(rk3_usb_copy_spec(&session,9,out)==ENODEV&&!memcmp(out,unchanged,14));
    spec_commit(&session);
    assert(rk3_usb_copy_spec(&session,10,out)==0&&!memcmp(out,spec_startup_info+1,14));
    finish(&session);
}
static void spec_refresh_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;
    uint8_t out[14],unchanged[14];memset(out,0xa5,14);memcpy(unchanged,out,14);
    assert(rk3_usb_copy_spec(NULL,9,out)==EINVAL&&!memcmp(out,unchanged,14));
    assert(rk3_usb_copy_spec(&session,0,out)==EINVAL&&!memcmp(out,unchanged,14));
    assert(rk3_usb_copy_spec(&session,9,NULL)==EINVAL);
    spec_attempt(&session);spec_commit(&session); /* A commit does not require metadata. */
    assert(rk3_usb_copy_spec(&session,10,out)==ENODEV&&!memcmp(out,unchanged,14));
    unsigned reads=read_arms,writes=command_writes,starts=fake_start_calls,services=fake_service_calls;
    spec_info(&session,spec_startup_info+1); /* Late first INFO promotes the same generation. */
    assert(rk3_usb_copy_spec(&session,10,out)==0&&!memcmp(out,spec_startup_info+1,14));
    const uint8_t refreshed[14]={1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    spec_info(&session,refreshed);
    assert(rk3_usb_copy_spec(&session,10,out)==0&&!memcmp(out,refreshed,14));
    memset(session.read_buffer+1,0xff,14);session.pending=true;report_complete(&session,0,14);
    assert(rk3_usb_copy_spec(&session,10,out)==0&&!memcmp(out,refreshed,14));
    memset(out,0xa5,14);
    assert(rk3_usb_copy_spec(&session,9,out)==ESTALE&&!memcmp(out,unchanged,14));
    const uint8_t zero[14]={0};spec_info(&session,zero); /* Refresh, never a startup bypass. */
    assert(rk3_usb_copy_spec(&session,10,out)==0&&!memcmp(out,zero,14));
    set_error(&session,EIO,"Diagnostic without USB retirement");
    assert(rk3_usb_copy_spec(&session,10,out)==0&&!memcmp(out,zero,14));
    assert(read_arms==reads&&command_writes==writes&&fake_start_calls==starts&&fake_service_calls==services);
    spec_attempt(&session); /* Same old status.info and generation cannot certify a new attempt. */
    memset(out,0xa5,14);
    assert(rk3_usb_copy_spec(&session,9,out)==ENODEV&&!memcmp(out,unchanged,14));
    spec_commit(&session);
    assert(rk3_usb_copy_spec(&session,11,out)==ENODEV&&!memcmp(out,unchanged,14));
    finish(&session);
}
static void spec_retirement_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;
    uint8_t out[14],unchanged[14];memset(out,0xa5,14);memcpy(unchanged,out,14);
    spec_attempt(&session);spec_info(&session,spec_startup_info+1);spec_commit(&session);
    disconnect_device(&session);
    assert(rk3_usb_copy_spec(&session,10,out)==ENODEV&&!memcmp(out,unchanged,14));
    /* Revocation cannot be resurrected by a late connection-publication section. */
    spec_commit(&session);
    assert(rk3_usb_copy_spec(&session,11,out)==ENODEV&&!memcmp(out,unchanged,14));
    session.stopping_io=false;session.command_suspended=true;
    spec_attempt(&session);spec_info(&session,spec_startup_info+1);spec_commit(&session);
    report_complete(&session,kIOReturnNoDevice,0);
    assert(rk3_usb_copy_spec(&session,12,out)==ENODEV&&!memcmp(out,unchanged,14));
    spec_info(&session,spec_startup_info+1);
    assert(rk3_usb_copy_spec(&session,12,out)==ENODEV&&!memcmp(out,unchanged,14));
    finish(&session);
}
static void spec_protocol_retirement_test(void) {
    for (unsigned kind=0;kind<2;++kind) {
        rk3_usb_session session;initialize(&session);session.command_suspended=true;
        uint8_t out[14],unchanged[14];memset(out,0xa5,14);memcpy(unchanged,out,14);
        spec_attempt(&session);spec_info(&session,spec_startup_info+1);spec_commit(&session);
        if (kind==0) report_complete(&session,0,sizeof(session.read_buffer)+1);
        else command_write_complete(&session,0,1);
        assert(rk3_usb_copy_spec(&session,10,out)==ENODEV&&!memcmp(out,unchanged,14));
        spec_info(&session,spec_startup_info+1);
        assert(rk3_usb_copy_spec(&session,10,out)==ENODEV&&!memcmp(out,unchanged,14));
        finish(&session);
    }
}
typedef struct {
    rk3_usb_session *session;
    pthread_mutex_t mutex;pthread_cond_t condition;
    CFRunLoopRef loop;CFRunLoopSourceRef source;
    bool entered,done,check_identity;
} SpecDrain;
static SpecDrain *spec_drain;
static int32_t spec_abort_pending(void *interface,uint8_t pipe) {
    (void)interface;assert(pipe==4);
    pthread_mutex_lock(&spec_drain->mutex);spec_drain->entered=true;
    pthread_cond_broadcast(&spec_drain->condition);pthread_mutex_unlock(&spec_drain->mutex);
    return 0; /* Keep the old callback outstanding until explicitly delivered. */
}
static void spec_deliver_retiring_info(void *context) {
    SpecDrain *drain=context;uint8_t out[14],unchanged[14];memset(out,0xa5,14);memcpy(unchanged,out,14);
    spec_info(drain->session,spec_startup_info+1);
    assert(rk3_usb_copy_spec(drain->session,10,out)==ENODEV&&!memcmp(out,unchanged,14));
    if(drain->check_identity) {
        RK3NativeUSBIdentity value,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));value=sentinel;
        assert(rk3_usb_copy_identity(drain->session,10,&value)==ENODEV&&!memcmp(&value,&sentinel,sizeof(value)));
    }
}
static void *spec_disconnect_pending(void *context) {
    SpecDrain *drain=context;CFRunLoopRef loop=CFRunLoopGetCurrent();
    CFRunLoopSourceContext source_context={.info=drain,.perform=spec_deliver_retiring_info};
    CFRunLoopSourceRef source=CFRunLoopSourceCreate(NULL,0,&source_context);assert(source);
    CFRunLoopAddSource(loop,source,kCFRunLoopDefaultMode);
    pthread_mutex_lock(&drain->mutex);drain->loop=loop;drain->source=source;pthread_mutex_unlock(&drain->mutex);
    disconnect_device(drain->session);
    /* This new publication cannot execute while the old callback retains its I/O. */
    spec_attempt(drain->session);spec_commit(drain->session);
    if(drain->check_identity) {
        RK3NativeUSBIdentity value,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));value=sentinel;
        assert(rk3_usb_copy_identity(drain->session,11,&value)==ENODEV&&!memcmp(&value,&sentinel,sizeof(value)));
    }
    pthread_mutex_lock(&drain->mutex);drain->done=true;pthread_cond_broadcast(&drain->condition);pthread_mutex_unlock(&drain->mutex);
    CFRunLoopRemoveSource(loop,source,kCFRunLoopDefaultMode);CFRelease(source);
    return NULL;
}
static void spec_drain_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;
    spec_attempt(&session);spec_info(&session,spec_startup_info+1);spec_commit(&session);
    RK3HostInterface interface={.abort_pipe=spec_abort_pending};session.interface=&interface;
    session.opened=session.pending=true;session.command_in=4;
    SpecDrain drain={.session=&session};assert(!pthread_mutex_init(&drain.mutex,NULL));assert(!pthread_cond_init(&drain.condition,NULL));spec_drain=&drain;
    pthread_t thread;assert(!pthread_create(&thread,NULL,spec_disconnect_pending,&drain));
    pthread_mutex_lock(&drain.mutex);while(!drain.entered)pthread_cond_wait(&drain.condition,&drain.mutex);
    assert(!drain.done);uint8_t out[14],unchanged[14];memset(out,0xa5,14);memcpy(unchanged,out,14);
    assert(rk3_usb_copy_spec(&session,10,out)==ENODEV&&!memcmp(out,unchanged,14));
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.generation==10);
    CFRunLoopSourceSignal(drain.source);CFRunLoopWakeUp(drain.loop);pthread_mutex_unlock(&drain.mutex);
    assert(!pthread_join(thread,NULL));
    assert(rk3_usb_info(&session,&info)&&info.generation==11);
    assert(rk3_usb_copy_spec(&session,11,out)==ENODEV&&!memcmp(out,unchanged,14));
    session.stopping_io=false;session.command_suspended=true;
    spec_info(&session,spec_startup_info+1);
    assert(rk3_usb_copy_spec(&session,11,out)==0&&!memcmp(out,spec_startup_info+1,14));
    pthread_cond_destroy(&drain.condition);pthread_mutex_destroy(&drain.mutex);spec_drain=NULL;finish(&session);
}
static void *spec_close_observer(void *context) {
    rk3_usb_session *session=context;
    /* This thread owns the existing worker reference throughout the getter. */
    pthread_mutex_lock(&session->mutex);
    while(!atomic_load(&session->closing))pthread_cond_wait(&session->wake,&session->mutex);
    pthread_mutex_unlock(&session->mutex);
    uint8_t out[14],unchanged[14];memset(out,0xa5,14);memcpy(unchanged,out,14);
    assert(rk3_usb_copy_spec(session,10,out)==ENODEV&&!memcmp(out,unchanged,14));
    spec_commit(session);spec_info(session,spec_startup_info+1);
    assert(rk3_usb_copy_spec(session,11,out)==ENODEV&&!memcmp(out,unchanged,14));
    pthread_mutex_lock(&session->mutex);session->status.state=RK3_USB_CLOSED;
    pthread_cond_broadcast(&session->wake);pthread_mutex_unlock(&session->mutex);
    release_session(session);return NULL;
}
static void spec_close_test(void) {
    for(unsigned wait=0;wait<2;++wait) {
        rk3_usb_session *session=calloc(1,sizeof(*session));assert(session);initialize(session);session->command_suspended=true;
        spec_attempt(session);spec_info(session,spec_startup_info+1);spec_commit(session);
        pthread_t thread;assert(!pthread_create(&thread,NULL,spec_close_observer,session));
        if(wait)assert(rk3_usb_close_wait(session,1000));else rk3_usb_close(session);
        /* The API relinquishes the caller reference. No call may use it now. */
        assert(!pthread_join(thread,NULL));
    }
}
static void spec_property_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;usb=&session;
    RK3NativeSpecQuery query={1,0,10};RK3NativeSpec value;
    spec_attempt(&session);spec_info(&session,spec_startup_info+1);spec_commit(&session);
    unsigned reads=read_arms,writes=command_writes,starts=fake_start_calls,services=fake_service_calls;
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==0&&value.generation==10);
    assert(!memcmp(value.raw,spec_startup_info+1,14)); /* No audio engine is required. */
    const uint8_t raw[14]={1,2,3,4,5,6,7,8,9,10,11,12,13,14};spec_info(&session,raw);
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==0&&value.generation==10&&!memcmp(value.raw,raw,14));
    query.generation=9;assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==ESTALE&&value.generation==0);
    for(size_t i=0;i<sizeof(value.raw);++i)assert(value.raw[i]==0);
    query.generation=10;
    const uint8_t zero[14]={0};spec_info(&session,zero);
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==0&&value.generation==10&&!memcmp(value.raw,zero,14));
    spec_info(&session,raw);
    assert(read_arms==reads&&command_writes==writes&&fake_start_calls==starts&&fake_service_calls==services);
    rk3_native_audio_iso audio={0};test_audio(&session,&audio);session.status.state=RK3_USB_READY;
    fake_start_result=0;fake_clock_valid=true;
    Request *request=enqueue_test_request(&session,REQUEST_START,NULL,0);process_one_request(&session,true);
    assert(request->complete&&request->result==0);free_test_request(request);
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==0&&!memcmp(value.raw,raw,14));
    fake_panic_pending=true;fake_panic_delay=0;fake_panic_error=kIOReturnIsoTooOld;audio_panic_tick(&session,100);
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==0&&!memcmp(value.raw,raw,14));
    request=enqueue_test_request(&session,REQUEST_STOP,NULL,0);process_one_request(&session,true);
    assert(request->complete&&request->result==0);free_test_request(request);
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==0&&!memcmp(value.raw,raw,14));
    request=enqueue_test_request(&session,REQUEST_START,NULL,0);process_one_request(&session,true);
    assert(request->complete&&request->result==0);free_test_request(request);
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==0&&!memcmp(value.raw,raw,14));
    disconnect_device(&session);
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==ENODEV&&value.generation==0);
    for(size_t i=0;i<sizeof(value.raw);++i)assert(value.raw[i]==0);
    session.stopping_io=false;session.command_suspended=true;spec_attempt(&session);
    const uint8_t next[14]={0x0e,0,6,0,3,9,0x29,2,2,0,0,1,1,2};
    spec_info(&session,next);spec_commit(&session);
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==ESTALE&&value.generation==0);
    for(size_t i=0;i<sizeof(value.raw);++i)assert(value.raw[i]==0);
    query.generation=11;
    assert(rk3_native_copy_spec(DEVICE,&query,&value)==0&&value.status==0&&value.generation==11&&!memcmp(value.raw,next,14));
    usb=NULL;finish(&session);
}
typedef struct {pthread_mutex_t mutex;pthread_cond_t condition;unsigned arrived,turn;} SpecBarrier;
static void spec_barrier(SpecBarrier *barrier) {
    pthread_mutex_lock(&barrier->mutex);unsigned turn=barrier->turn;
    if(++barrier->arrived==2){barrier->arrived=0;++barrier->turn;pthread_cond_broadcast(&barrier->condition);}
    else while(turn==barrier->turn)pthread_cond_wait(&barrier->condition,&barrier->mutex);
    pthread_mutex_unlock(&barrier->mutex);
}
typedef struct {
    rk3_usb_session *session;SpecBarrier barrier;uint64_t generation;int32_t result;uint8_t raw[14];
} SpecRace;
static void *spec_concurrent_getter(void *context) {
    SpecRace *race=context;
    for(unsigned round=0;round<32;++round)for(unsigned operation=0;operation<3;++operation){
        spec_barrier(&race->barrier);memset(race->raw,0xa5,14);
        race->result=rk3_usb_copy_spec(race->session,race->generation,race->raw);
        spec_barrier(&race->barrier);
    }
    return NULL;
}
static void spec_snapshot_race_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;
    spec_attempt(&session);spec_info(&session,spec_startup_info+1);spec_commit(&session);
    SpecRace race={.session=&session};assert(!pthread_mutex_init(&race.barrier.mutex,NULL));assert(!pthread_cond_init(&race.barrier.condition,NULL));
    pthread_t thread;assert(!pthread_create(&thread,NULL,spec_concurrent_getter,&race));
    uint8_t current[14];memcpy(current,spec_startup_info+1,14);
    for(unsigned round=0;round<32;++round){
        uint8_t refresh[14],next[14],unchanged[14];memset(refresh,0x20+(int)round,14);
        memcpy(next,spec_startup_info+1,14);next[0]=(uint8_t)(0x60+round);memset(unchanged,0xa5,14);
        race.generation=10+round;spec_barrier(&race.barrier);spec_info(&session,refresh);spec_barrier(&race.barrier);
        assert(race.result==0&&(!memcmp(race.raw,current,14)||!memcmp(race.raw,refresh,14)));
        spec_barrier(&race.barrier);spec_attempt(&session);spec_barrier(&race.barrier);
        assert((race.result==0&&!memcmp(race.raw,refresh,14))||(race.result==ENODEV&&!memcmp(race.raw,unchanged,14)));
        spec_info(&session,next);
        spec_barrier(&race.barrier);spec_commit(&session);spec_barrier(&race.barrier);
        assert((race.result==ENODEV||race.result==ESTALE)&&!memcmp(race.raw,unchanged,14));
        memcpy(current,next,14);
    }
    assert(!pthread_join(thread,NULL));uint8_t out[14];
    assert(rk3_usb_copy_spec(&session,42,out)==0&&!memcmp(out,current,14));
    pthread_cond_destroy(&race.barrier.condition);pthread_mutex_destroy(&race.barrier.mutex);finish(&session);
}
typedef struct {
    uint16_t length;uint8_t index;int32_t status;uint32_t actual;
    size_t copied;uint8_t bytes[256];
} IdentityReply;
typedef struct {
    pthread_mutex_t mutex;pthread_cond_t condition;
    bool entered,resume,returned;
} IdentityRequestGate;
typedef struct {
    IdentityReply replies[9];size_t count,used;
    int32_t getter_status[5];uint16_t vendor,product;uint8_t indices[3];
    unsigned trace[32],trace_count;rk3_usb_session *session;bool require_open;
    IdentityRequestGate *gate;
} IdentityIO;
static IdentityIO *identity_io;
static void identity_boundary(unsigned step) {
    IdentityIO *io=identity_io;if(!io)return;
    if(io->require_open)assert(configuration_open);
    if(io->session){assert(pthread_mutex_trylock(&io->session->mutex)==0);pthread_mutex_unlock(&io->session->mutex);}
    assert(io->trace_count<32);io->trace[io->trace_count++]=step;
}
static void identity_configuration_event(unsigned int event){if(identity_io)identity_boundary(100+event);}
static unsigned identity_release_calls;
static ULONG fake_identity_release(void *self) {
    (void)self;assert(!configuration_open);
    if(identity_io&&identity_io->gate)assert(identity_io->gate->returned);
    ++identity_release_calls;return 0;
}
static IOReturn fake_identity_vendor(void *self,UInt16 *value) {
    (void)self;identity_boundary(0);*value=identity_io?identity_io->vendor:0;return identity_io?identity_io->getter_status[0]:0;
}
static IOReturn fake_identity_product(void *self,UInt16 *value) {
    (void)self;identity_boundary(1);*value=identity_io?identity_io->product:0;return identity_io?identity_io->getter_status[1]:0;
}
static IOReturn fake_identity_index(unsigned slot,UInt8 *value) {
    identity_boundary(slot+2);*value=identity_io?identity_io->indices[slot]:0;return identity_io?identity_io->getter_status[slot+2]:0;
}
static IOReturn fake_identity_product_index(void *self,UInt8 *value){(void)self;return fake_identity_index(0,value);}
static IOReturn fake_identity_manufacturer_index(void *self,UInt8 *value){(void)self;return fake_identity_index(1,value);}
static IOReturn fake_identity_serial_index(void *self,UInt8 *value){(void)self;return fake_identity_index(2,value);}
static IOReturn fake_identity_request(void *self,IOUSBDevRequestTO *request) {
    (void)self;IdentityIO *io=identity_io;if(!io)return 0;
    assert(io->used<io->count);identity_boundary(16+(request->wValue&0xff));
    IdentityReply *reply=&io->replies[io->used++];
    assert(request->bmRequestType==0x80&&request->bRequest==6);
    assert(request->wValue==(uint16_t)(0x300|reply->index)&&request->wIndex==0x0409);
    assert(request->wLength==reply->length&&request->wLenDone==0);
    assert(request->noDataTimeout==5000&&request->completionTimeout==0);
    const uint8_t *before=request->pData;
    for(unsigned i=0;i<request->wLength;++i)assert(before[i]==0);
    if(io->gate&&io->used==1) {
        IdentityRequestGate *gate=io->gate;pthread_mutex_lock(&gate->mutex);
        gate->entered=true;pthread_cond_broadcast(&gate->condition);
        while(!gate->resume)pthread_cond_wait(&gate->condition,&gate->mutex);
        /* The original stack request and scratch remain owned until return. */
        assert(request->pData==before&&configuration_open);
        gate->returned=true;pthread_cond_broadcast(&gate->condition);pthread_mutex_unlock(&gate->mutex);
    }
    assert(reply->copied<=request->wLength);memcpy(request->pData,reply->bytes,reply->copied);
    request->wLenDone=reply->actual;return reply->status;
}
static void identity_reader_first_test(void) {
    IdentityIO io={.count=2,.replies={{.length=2,.index=7,.actual=2,.copied=2,.bytes={4,3}},
        {.length=4,.index=7,.actual=4,.copied=4,.bytes={4,3,0x41,0}}}};
    identity_io=&io;IOUSBDeviceInterface300 functions={.DeviceRequestTO=fake_identity_request};
    IOUSBDeviceInterface300 *device=&functions;uint8_t out[32];memset(out,0xa5,32);bool defined=false;
    assert(read_identity_string(&device,7,out,31,0x0409,&defined)==0);
    assert(defined&&out[0]==0x41&&out[1]==0&&out[30]==0&&out[31]==0xa5&&io.used==2);
    identity_io=NULL;
}
static void identity_reader_fallback_test(void) {
    for(unsigned kind=0;kind<2;++kind) {
        IdentityIO io={.count=2,.replies={{.length=2,.index=7,.status=(int32_t)UINT32_C(0xe00002e8),.copied=2,.bytes={4,3}},
            {.length=4,.index=7,.copied=4,.bytes={4,3,0x41,0}}}};
        if(kind){io.replies[0].bytes[1]=0;io.replies[1]=(IdentityReply){.length=256,.index=7,.status=(int32_t)UINT32_C(0xe00002bc)};}
        identity_io=&io;IOUSBDeviceInterface300 functions={.DeviceRequestTO=fake_identity_request};IOUSBDeviceInterface300 *device=&functions;
        uint8_t out[32];memset(out,0xa5,32);bool defined=false;
        int32_t expected=kind?(int32_t)UINT32_C(0xe00002bc):0;
        assert(read_identity_string(&device,7,out,31,0x0409,&defined)==expected&&defined);
        assert(out[0]==(kind?0:0x41)&&out[1]==0&&out[30]==0&&out[31]==0xa5&&io.used==2);
    }
    identity_io=NULL;
}
static void identity_reader_unicode_test(void) {
    const uint8_t inputs[][6]={{6,3,0xe9,0,0,0},{6,3,0x3d,0xd8,0,0xde},{6,3,0xac,0x20,0,0}};
    const uint8_t expected[][6]={{0xc3,0xa9,0,0,0,0},{0xed,0xa0,0xbd,0xed,0xb8,0x80},{0xe2,0x82,0xac,0,0,0}};
    for(unsigned kind=0;kind<3;++kind) {
        IdentityIO io={.count=2,.replies={{.length=2,.index=7,.copied=2,.bytes={6,3}},
            {.length=6,.index=7,.copied=6}}};memcpy(io.replies[1].bytes,inputs[kind],6);
        identity_io=&io;IOUSBDeviceInterface300 functions={.DeviceRequestTO=fake_identity_request};IOUSBDeviceInterface300 *device=&functions;
        uint8_t out[32];memset(out,0xa5,32);bool defined=false;
        assert(read_identity_string(&device,7,out,31,0x0409,&defined)==0&&defined);
        assert(!memcmp(out,expected[kind],6)&&out[30]==0&&out[31]==0xa5&&io.used==2);
    }
    identity_io=NULL;
}
static void expect_identity_reader(IdentityIO *io,uint8_t index,int count,int32_t status,
    bool expected_defined,const uint8_t expected[31]) {
    identity_io=io;IOUSBDeviceInterface300 functions={.DeviceRequestTO=fake_identity_request};IOUSBDeviceInterface300 *device=&functions;
    uint8_t out[32];memset(out,0xa5,32);bool defined=true;
    assert(read_identity_string(&device,index,out,count,0x0409,&defined)==status&&defined==expected_defined);
    if(count==31)assert(!memcmp(out,expected,31));else for(unsigned i=0;i<32;++i)assert(out[i]==0xa5);
    assert(out[31]==0xa5&&io->used==io->count);identity_io=NULL;
}
static IdentityIO identity_two_replies(uint16_t length,uint8_t index) {
    IdentityIO io={.count=2,.replies={{.length=2,.copied=2},{.copied=length}}};
    io.replies[0].index=io.replies[1].index=index;io.replies[0].bytes[0]=(uint8_t)length;io.replies[0].bytes[1]=3;
    io.replies[1].length=length;io.replies[1].bytes[0]=(uint8_t)length;io.replies[1].bytes[1]=3;
    return io;
}
static void identity_reader_oracles_test(void) {
    const uint8_t zero[31]={0};uint8_t expected[31];IdentityIO io={0};
    /* Count guard; initial zero length; initial success with wrong type. */
    expect_identity_reader(&io,7,5,(int32_t)UINT32_C(0xe00002c2),false,zero);
    io=(IdentityIO){.count=1,.replies={{.length=2,.index=7,.copied=2}}};expect_identity_reader(&io,7,31,0,true,zero);
    io=(IdentityIO){.count=1,.replies={{.length=2,.index=7,.copied=2,.bytes={4,0}}}};
    expect_identity_reader(&io,7,31,(int32_t)UINT32_C(0xe00002e9),true,zero);
    /* Error fallback may return Underrun with zero length and still succeed. */
    io=(IdentityIO){.count=2,.replies={{.length=2,.index=7,.status=kIOReturnIOError},
        {.length=256,.index=7,.status=(int32_t)UINT32_C(0xe00002e7)}}};expect_identity_reader(&io,7,31,0,true,zero);
    /* Odd original length is submitted unchanged, final even length controls conversion. */
    io=identity_two_replies(5,7);io.replies[1].bytes[2]=0x41;io.replies[1].bytes[4]=0xff;
    memset(expected,0,31);expected[0]=0x41;expect_identity_reader(&io,7,31,0,true,expected);
    for(unsigned length=0;length<=2;length+=2){
        io=identity_two_replies(4,7);io.replies[1].bytes[0]=(uint8_t)length;
        expect_identity_reader(&io,7,31,0,true,zero);
    }
    io=identity_two_replies(4,7);io.replies[1].status=kIOReturnIOError;
    expect_identity_reader(&io,7,31,kIOReturnIOError,true,zero);
    /* Fully supplied content remains defined even when every actual count is0. */
    io=identity_two_replies(4,7);io.replies[1].bytes[2]=0x41;
    memset(expected,0,31);expected[0]=0x41;expect_identity_reader(&io,7,31,0,true,expected);
    /* Whole-unit output budget, not a byte-wise truncation or character count. */
    io=identity_two_replies(62,7);
    for(unsigned i=0;i<29;++i)io.replies[1].bytes[2+2*i]=0x41;
    io.replies[1].bytes[60]=0xe9;memset(expected,0,31);memset(expected,0x41,29);
    expect_identity_reader(&io,7,31,0,true,expected);
    io=identity_two_replies(34,7);for(unsigned i=0;i<16;++i)io.replies[1].bytes[2+2*i]=0xe9;
    memset(expected,0,31);for(unsigned i=0;i<15;++i){expected[2*i]=0xc3;expected[2*i+1]=0xa9;}
    expect_identity_reader(&io,7,31,0,true,expected);
    /* Index0 is sent, and a supplied language-list unit is encoded as data. */
    io=identity_two_replies(4,0);io.replies[1].bytes[2]=9;io.replies[1].bytes[3]=4;
    memset(expected,0,31);expected[0]=0xd0;expected[1]=0x89;expect_identity_reader(&io,0,31,0,true,expected);
    io=identity_two_replies(8,7);io.replies[1].bytes[2]=0x41;io.replies[1].bytes[6]=0x42;
    memset(expected,0,31);expected[0]=0x41;expect_identity_reader(&io,7,31,0,true,expected);
}
static void identity_reader_definedness_test(void) {
    uint8_t expected[31]={0};IdentityIO io={.count=3,.replies={{.length=2,.index=7,.status=kIOReturnIOError},
        {.length=256,.index=7,.copied=6,.bytes={4,3,0,0,0x42,0}},
        {.length=4,.index=7,.copied=4,.bytes={6,3,0x41,0}}}};
    expected[0]=0x41;expected[1]=0x42;expect_identity_reader(&io,7,31,0,true,expected);
    /* The same final response without fallback needs bytes never initialized. */
    io=identity_two_replies(4,7);io.replies[1].bytes[0]=6;io.replies[1].bytes[2]=0x41;
    memset(expected,0,31);expect_identity_reader(&io,7,31,0,false,expected);
    /* Known NUL stops before the unknown unit. */
    io=identity_two_replies(4,7);io.replies[1].bytes[0]=6;expect_identity_reader(&io,7,31,0,true,expected);
    /* A full30-byte output still requires the next unit before the capacity test. */
    io=identity_two_replies(62,7);io.replies[1].bytes[0]=64;
    for(unsigned i=0;i<30;++i)io.replies[1].bytes[2+2*i]=0x41;
    expect_identity_reader(&io,7,31,0,false,expected);
}
static void identity_configure_fixture(rk3_usb_session *session,IOUSBDeviceInterface300 *functions,
    IOUSBDeviceInterface300 **device,IdentityIO *io) {
    *functions=(IOUSBDeviceInterface300){.Release=fake_identity_release,.GetConfiguration=fake_configuration_get,
        .GetNumberOfConfigurations=fake_configuration_count,.GetConfigurationDescriptorPtr=fake_configuration_descriptor,
        .USBDeviceOpen=fake_configuration_open,.SetConfiguration=fake_configuration_set,.USBDeviceClose=fake_configuration_close};
    identity_parent_api(functions);*device=functions;session->device=device;identity_io=io;
    io->session=session;io->require_open=true;fake_configuration=1;configuration_open=false;
    configuration_result=0;configuration_trace_length=configuration_sets=configuration_opens=configuration_closes=0;
}
static IdentityIO identity_capture_replies(void) {
    IdentityIO io={.count=6,.vendor=0x1234,.product=0xabcd,.indices={7,8,0}};
    for(unsigned field=0;field<3;++field){
        io.replies[2*field]=(IdentityReply){.length=2,.index=io.indices[field],.copied=2,.bytes={4,3}};
        io.replies[2*field+1]=(IdentityReply){.length=4,.index=io.indices[field],.copied=4,.bytes={4,3,(uint8_t)(0x50+field),0}};
    }
    return io;
}
static void identity_capture_publication_test(void) {
    rk3_usb_session session;initialize(&session);spec_attempt(&session);
    IOUSBDeviceInterface300 functions,*device;IdentityIO io=identity_capture_replies();
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));
    RK3NativeUSBIdentity value,saved;memset(&value,0xa5,sizeof(value));saved=value;
    assert(rk3_usb_copy_identity(&session,9,&value)==ENODEV&&!memcmp(&value,&saved,sizeof(value)));
    spec_commit(&session);
    assert(rk3_usb_copy_identity(&session,10,&value)==0);
    assert(value.version==1&&value.status==0&&value.generation==10&&value.attempted_mask==0xff&&value.defined_mask==0xff);
    assert(value.vendor_id==0x1234&&value.product_id==0xabcd&&value.product_index==7&&value.manufacturer_index==8&&value.serial_index==0);
    assert(value.product[0]==0x50&&value.manufacturer[0]==0x51&&value.serial[0]==0x52&&io.used==6);
    const unsigned expected[]={103,0,1,2,23,23,3,24,24,4,16,16,104,105};
    assert(io.trace_count==sizeof(expected)/sizeof(*expected)&&!memcmp(io.trace,expected,sizeof(expected)));
    session.device=NULL;identity_io=NULL;finish(&session);
}
static void identity_partial_capture_test(void) {
    rk3_usb_session session;initialize(&session);spec_attempt(&session);session.status.error_code=EINPROGRESS;
    IOUSBDeviceInterface300 functions,*device;IdentityIO io={.vendor=0xdead,.product=0,.indices={7,8,0},
        .getter_status={kIOReturnIOError,0,kIOReturnNoDevice,0,0},.count=4,
        .replies={{.length=2,.index=8,.status=kIOReturnIOError},{.length=256,.index=8,.status=kIOReturnIOError},
            {.length=2,.index=0,.copied=2,.bytes={4,3}},{.length=4,.index=0,.copied=4,.bytes={4,3,0x53,0}}}};
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));spec_commit(&session);
    RK3NativeUSBIdentity value;assert(rk3_usb_copy_identity(&session,10,&value)==0);
    assert(value.attempted_mask==0xdf&&value.defined_mask==0xda&&value.status==0&&value.generation==10);
    assert(value.vendor_id==0&&value.product_id==0&&value.product_index==0&&value.manufacturer_index==8&&value.serial_index==0);
    assert(value.step_status[0]==kIOReturnIOError&&value.step_status[2]==kIOReturnNoDevice&&value.step_status[5]==0&&value.step_status[6]==kIOReturnIOError);
    for(unsigned i=0;i<31;++i)assert(value.product[i]==0&&value.manufacturer[i]==0);
    assert(value.serial[0]==0x53&&io.used==4&&!session.transport_failed&&session.status.error_code==EINPROGRESS);
    assert(configuration_sets==1&&configuration_closes==1&&!configuration_open);
    usb=&session;RK3NativeUSBIdentityQuery query={1,0,10};RK3NativeUSBIdentity transported;
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&transported)==0&&!memcmp(&transported,&value,sizeof(value)));usb=NULL;
    identity_io=NULL;session.device=NULL;finish(&session);
    /* Reader raw0 with undefined native bytes remains a valid partial snapshot. */
    initialize(&session);spec_attempt(&session);io=identity_capture_replies();io.replies[1].bytes[0]=6;
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));spec_commit(&session);
    assert(rk3_usb_copy_identity(&session,10,&value)==0&&value.attempted_mask==0xff&&value.defined_mask==0xdf&&value.step_status[5]==0);
    for(unsigned i=0;i<31;++i)assert(value.product[i]==0);
    assert(value.manufacturer[0]==0x51&&value.serial[0]==0x52&&io.used==6);
    usb=&session;assert(rk3_native_copy_usb_identity(DEVICE,&query,&transported)==0&&!memcmp(&transported,&value,sizeof(value)));usb=NULL;
    identity_io=NULL;session.device=NULL;finish(&session);
}
static void identity_property_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;spec_attempt(&session);
    IOUSBDeviceInterface300 functions,*device;IdentityIO io=identity_capture_replies();
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));spec_commit(&session);usb=&session;
    RK3NativeUSBIdentityQuery query={1,0,10};RK3NativeUSBIdentity value,saved;
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.status==0&&value.generation==10);
    assert(value.vendor_id==0x1234&&value.product_id==0xabcd&&value.product[0]==0x50&&value.attempted_mask==0xff&&value.defined_mask==0xff);
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&strcmp(info.product_name,(char *)value.product)!=0);
    saved=value;uint8_t spec[14];assert(rk3_usb_copy_spec(&session,10,spec)==ENODEV);
    unsigned reads=read_arms,writes=command_writes,starts=fake_start_calls,services=fake_service_calls,requests=(unsigned)io.used;
    for(unsigned i=0;i<3;++i)assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&!memcmp(&value,&saved,sizeof(value)));
    spec_info(&session,spec_startup_info+1);
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&!memcmp(&value,&saved,sizeof(value)));
    assert(rk3_usb_copy_spec(&session,10,spec)==0);
    assert(read_arms==reads&&command_writes==writes&&fake_start_calls==starts&&fake_service_calls==services&&io.used==requests);
    RK3NativeUSBIdentity unchanged;memset(&unchanged,0xa5,sizeof(unchanged));value=unchanged;
    assert(rk3_usb_copy_identity(NULL,10,&value)==EINVAL&&!memcmp(&value,&unchanged,sizeof(value)));
    assert(rk3_usb_copy_identity(&session,0,&value)==EINVAL&&!memcmp(&value,&unchanged,sizeof(value)));
    assert(rk3_usb_copy_identity(&session,10,NULL)==EINVAL);
    assert(rk3_usb_copy_identity(&session,9,&value)==ESTALE&&!memcmp(&value,&unchanged,sizeof(value)));
    query.generation=9;assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.status==ESTALE&&value.generation==0&&value.attempted_mask==0);
    query.generation=10;
    rk3_native_audio_iso audio={0};test_audio(&session,&audio);session.status.state=RK3_USB_READY;
    fake_start_result=0;fake_clock_valid=true;
    Request *request=enqueue_test_request(&session,REQUEST_START,NULL,0);process_one_request(&session,true);
    assert(request->complete&&request->result==0);free_test_request(request);
    fake_panic_pending=true;fake_panic_delay=0;fake_panic_error=kIOReturnIsoTooOld;audio_panic_tick(&session,100);
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&!memcmp(&value,&saved,sizeof(value)));
    request=enqueue_test_request(&session,REQUEST_STOP,NULL,0);process_one_request(&session,true);assert(request->complete&&request->result==0);free_test_request(request);
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&!memcmp(&value,&saved,sizeof(value)));
    set_error(&session,EIO,"Unrelated diagnostic");assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&!memcmp(&value,&saved,sizeof(value)));
    report_complete(&session,kIOReturnNoDevice,0);value=unchanged;
    assert(rk3_usb_copy_identity(&session,9,&value)==ENODEV&&!memcmp(&value,&unchanged,sizeof(value)));
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.status==ENODEV&&value.generation==0&&value.defined_mask==0);
    disconnect_device(&session);identity_io=NULL;usb=NULL;finish(&session);
    /* All zero physical values/real index0/empty text are valid, not absent. */
    initialize(&session);spec_attempt(&session);io=(IdentityIO){.count=3,.replies={{.length=2},{.length=2},{.length=2}}};
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));spec_commit(&session);usb=&session;
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.status==0&&value.vendor_id==0&&value.product_id==0&&value.defined_mask==0xff&&io.used==3);
    for(unsigned i=0;i<31;++i)assert(value.product[i]==0&&value.manufacturer[i]==0&&value.serial[i]==0);
    identity_io=NULL;session.device=NULL;usb=NULL;finish(&session);
}
static void identity_pending_drain_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;spec_attempt(&session);
    IOUSBDeviceInterface300 functions,*device;IdentityIO io=identity_capture_replies();
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));
    spec_info(&session,spec_startup_info+1);spec_commit(&session);
    RK3NativeUSBIdentity value,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));
    assert(rk3_usb_copy_identity(&session,10,&value)==0);
    RK3HostInterface interface={.abort_pipe=spec_abort_pending};session.interface=&interface;
    session.opened=session.pending=true;session.command_in=4;
    SpecDrain drain={.session=&session,.check_identity=true};assert(!pthread_mutex_init(&drain.mutex,NULL));
    assert(!pthread_cond_init(&drain.condition,NULL));spec_drain=&drain;
    pthread_t thread;assert(!pthread_create(&thread,NULL,spec_disconnect_pending,&drain));
    pthread_mutex_lock(&drain.mutex);while(!drain.entered)pthread_cond_wait(&drain.condition,&drain.mutex);
    assert(!drain.done);value=sentinel;
    assert(rk3_usb_copy_identity(&session,10,&value)==ENODEV&&!memcmp(&value,&sentinel,sizeof(value)));
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.generation==10);
    CFRunLoopSourceSignal(drain.source);CFRunLoopWakeUp(drain.loop);pthread_mutex_unlock(&drain.mutex);
    assert(!pthread_join(thread,NULL));assert(rk3_usb_info(&session,&info)&&info.generation==11);
    value=sentinel;assert(rk3_usb_copy_identity(&session,11,&value)==ENODEV&&!memcmp(&value,&sentinel,sizeof(value)));
    session.stopping_io=false;session.command_suspended=true;spec_info(&session,spec_startup_info+1);
    assert(rk3_usb_copy_identity(&session,11,&value)==ENODEV&&!memcmp(&value,&sentinel,sizeof(value)));
    identity_io=NULL;spec_drain=NULL;pthread_cond_destroy(&drain.condition);pthread_mutex_destroy(&drain.mutex);finish(&session);
}
static void identity_opaque_transport_test(void) {
    rk3_usb_session session;initialize(&session);spec_attempt(&session);IOUSBDeviceInterface300 functions,*device;
    IdentityIO io=identity_capture_replies();io.replies[0].bytes[0]=6;
    io.replies[1]=(IdentityReply){.length=6,.index=7,.copied=6,.bytes={6,3,0x3d,0xd8,0,0}};
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));spec_commit(&session);usb=&session;
    RK3NativeUSBIdentityQuery query={1,0,10};RK3NativeUSBIdentity value;const uint8_t surrogate[]={0xed,0xa0,0xbd,0};
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.status==0&&value.defined_mask==0xff);
    assert(!memcmp(value.product,surrogate,sizeof(surrogate))); /* Opaque original code-unit encoding. */
    identity_io=NULL;session.device=NULL;usb=NULL;finish(&session);
}
/* The thread owns the existing worker reference; it never enumerates a device. */
static void *identity_close_worker(void *context) {
    rk3_usb_session *session=context;assert(configure_device(session));
    spec_commit(session);RK3NativeUSBIdentity value,unchanged;memset(&unchanged,0xa5,sizeof(unchanged));value=unchanged;
    assert(rk3_usb_copy_identity(session,10,&value)==ENODEV&&!memcmp(&value,&unchanged,sizeof(value)));
    disconnect_device(session);
    pthread_mutex_lock(&session->mutex);session->status.state=RK3_USB_CLOSED;
    pthread_cond_broadcast(&session->wake);pthread_mutex_unlock(&session->mutex);
    release_session(session);return NULL;
}
static void identity_held_request_close_test(void) {
    for(unsigned wait=0;wait<2;++wait) {
        rk3_usb_session *session=calloc(1,sizeof(*session));assert(session);initialize(session);spec_attempt(session);
        IdentityRequestGate gate={0};assert(!pthread_mutex_init(&gate.mutex,NULL));assert(!pthread_cond_init(&gate.condition,NULL));
        IdentityIO io=identity_capture_replies();io.gate=&gate;IOUSBDeviceInterface300 functions,*device;
        identity_configure_fixture(session,&functions,&device,&io);unsigned released=identity_release_calls;
        pthread_t thread;assert(!pthread_create(&thread,NULL,identity_close_worker,session));
        pthread_mutex_lock(&gate.mutex);while(!gate.entered)pthread_cond_wait(&gate.condition,&gate.mutex);
        RK3NativeUSBIdentity value,unchanged;memset(&unchanged,0xa5,sizeof(unchanged));value=unchanged;
        assert(rk3_usb_copy_identity(session,9,&value)==ENODEV&&!memcmp(&value,&unchanged,sizeof(value)));
        assert(configuration_open&&configuration_sets==0&&configuration_closes==0&&identity_release_calls==released);
        if(wait)assert(!rk3_usb_close_wait(session,0));else rk3_usb_close(session);
        /* Caller relinquished its handle. Only the retained worker may use it. */
        assert(!gate.returned&&configuration_open&&identity_release_calls==released);
        gate.resume=true;pthread_cond_broadcast(&gate.condition);pthread_mutex_unlock(&gate.mutex);
        assert(!pthread_join(thread,NULL));assert(gate.returned&&io.used==6);
        assert(configuration_sets==1&&configuration_closes==1&&identity_release_calls==released+1);
        identity_io=NULL;pthread_cond_destroy(&gate.condition);pthread_mutex_destroy(&gate.mutex);
    }
}
static void identity_cohort_test(void) {
    rk3_usb_session session;initialize(&session);session.command_suspended=true;spec_attempt(&session);
    IOUSBDeviceInterface300 functions,*device;IdentityIO io=identity_capture_replies();
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));spec_commit(&session);usb=&session;
    RK3NativeUSBIdentityQuery query={1,0,10};RK3NativeUSBIdentity value,first,unchanged;memset(&unchanged,0xa5,sizeof(unchanged));
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&first)==0&&first.status==0);
    disconnect_device(&session);session.stopping_io=false;spec_attempt(&session);spec_commit(&session);
    value=unchanged;assert(rk3_usb_copy_identity(&session,11,&value)==ENODEV&&!memcmp(&value,&unchanged,sizeof(value)));
    spec_info(&session,spec_startup_info+1);assert(rk3_usb_copy_identity(&session,11,&value)==ENODEV&&!memcmp(&value,&unchanged,sizeof(value)));
    /* A distinct completed capture publishes only at the next existing generation commit. */
    spec_attempt(&session);io=identity_capture_replies();io.vendor=0x4567;io.indices[0]=9;
    io.replies[0].index=io.replies[1].index=9;io.replies[1].bytes[2]=0x55;
    identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));
    assert(rk3_usb_copy_identity(&session,11,&value)==ENODEV);spec_commit(&session);
    assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.status==ESTALE&&value.generation==0);
    query.generation=12;assert(rk3_native_copy_usb_identity(DEVICE,&query,&value)==0&&value.status==0&&value.generation==12);
    assert(value.vendor_id==0x4567&&value.product_index==9&&value.product[0]==0x55&&value.manufacturer[0]==first.manufacturer[0]);
    disconnect_device(&session);session.stopping_io=false;spec_attempt(&session);
    io=identity_capture_replies();identity_configure_fixture(&session,&functions,&device,&io);configuration_result=kIOReturnIOError;
    assert(!configure_device(&session)&&io.used==6);disconnect_device(&session);spec_commit(&session);
    value=unchanged;assert(rk3_usb_copy_identity(&session,13,&value)==ENODEV&&!memcmp(&value,&unchanged,sizeof(value)));
    identity_io=NULL;usb=NULL;finish(&session);
}
typedef struct {
    rk3_usb_session *session;SpecBarrier barrier;uint64_t generation;int32_t result;RK3NativeUSBIdentity value;
} IdentityRace;
static void *identity_concurrent_getter(void *context) {
    IdentityRace *race=context;
    for(unsigned round=0;round<16;++round)for(unsigned operation=0;operation<2;++operation) {
        spec_barrier(&race->barrier);memset(&race->value,0xa5,sizeof(race->value));
        race->result=rk3_usb_copy_identity(race->session,race->generation,&race->value);spec_barrier(&race->barrier);
    }
    return NULL;
}
static void identity_snapshot_race_test(void) {
    rk3_usb_session session;initialize(&session);spec_attempt(&session);IOUSBDeviceInterface300 functions,*device;
    IdentityIO io=identity_capture_replies();identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));spec_commit(&session);
    IdentityRace race={.session=&session};assert(!pthread_mutex_init(&race.barrier.mutex,NULL));assert(!pthread_cond_init(&race.barrier.condition,NULL));
    pthread_t thread;assert(!pthread_create(&thread,NULL,identity_concurrent_getter,&race));
    RK3NativeUSBIdentity current,unchanged;assert(rk3_usb_copy_identity(&session,10,&current)==0);memset(&unchanged,0xa5,sizeof(unchanged));
    for(unsigned round=0;round<16;++round) {
        race.generation=10+round;spec_barrier(&race.barrier);spec_attempt(&session);spec_barrier(&race.barrier);
        assert((race.result==0&&!memcmp(&race.value,&current,sizeof(current)))||(race.result==ENODEV&&!memcmp(&race.value,&unchanged,sizeof(unchanged))));
        io=identity_capture_replies();io.vendor=(uint16_t)(0x6000+round);io.replies[1].bytes[2]=(uint8_t)(0x61+round);
        identity_configure_fixture(&session,&functions,&device,&io);assert(configure_device(&session));
        race.generation=11+round;spec_barrier(&race.barrier);spec_commit(&session);spec_barrier(&race.barrier);
        assert(rk3_usb_copy_identity(&session,11+round,&current)==0);
        assert((race.result==0&&!memcmp(&race.value,&current,sizeof(current)))||(race.result==ENODEV&&!memcmp(&race.value,&unchanged,sizeof(unchanged))));
    }
    assert(!pthread_join(thread,NULL));identity_io=NULL;session.device=NULL;
    pthread_cond_destroy(&race.barrier.condition);pthread_mutex_destroy(&race.barrier.mutex);finish(&session);
}

/* Monitor GET waits on its calling thread. The fake owner keeps an actual
 * CF run-loop source, and retires both command directions before teardown. */
typedef struct {
    RK3HostInterface interface; /* Fake host's self pointer. */
    rk3_usb_session *session;
    rk3_native_audio_iso audio;
    pthread_t worker;
    CFRunLoopSourceRef source;
    atomic_bool ready,done,pause_requests,reply_on_write,hold_write;
    atomic_uint jobs,completed,writes,reply_value,service_seen,clock_seen;
    atomic_int write_error;
    uint8_t packets[1024][4];size_t lengths[1024];
    RK3HostBulkCompletion input_callback,output_callback;
    void *input_context,*output_context;
} MonitorFixture;
enum { MONITOR_REPLY=1,MONITOR_REVOKE=2,MONITOR_GENERATION=4,MONITOR_RETIRE=8,
       MONITOR_SHORT=16,MONITOR_UNRELATED=32 };
static void monitor_deliver(MonitorFixture *f,uint8_t command,size_t length) {
    assert(f->session->pending&&f->input_callback);
    f->session->read_buffer[0]=command;
    f->session->read_buffer[1]=(uint8_t)atomic_load(&f->reply_value);
    f->input_callback(f->input_context,0,length);
}
static void monitor_retire(MonitorFixture *f) {
    if(f->session->pending)f->input_callback(f->input_context,kIOReturnAborted,0);
    if(f->session->pending_write)f->output_callback(f->output_context,kIOReturnAborted,0);
}
static void monitor_jobs(void *opaque) {
    MonitorFixture *f=opaque;unsigned jobs=atomic_exchange(&f->jobs,0);
    if(jobs&MONITOR_SHORT) {monitor_deliver(f,0x12,0);monitor_deliver(f,0x12,1);}
    if(jobs&MONITOR_UNRELATED)monitor_deliver(f,0x14,2);
    if(jobs&MONITOR_REPLY)monitor_deliver(f,0x12,2);
    if(jobs&MONITOR_RETIRE)monitor_retire(f);
    if(jobs&(MONITOR_REVOKE|MONITOR_GENERATION)) {
        pthread_mutex_lock(&f->session->mutex);
        if(jobs&MONITOR_REVOKE)spec_revoke_locked(f->session);
        else {++f->session->generation;f->session->spec_generation=f->session->generation;}
        pthread_mutex_unlock(&f->session->mutex);
    }
    atomic_fetch_add(&f->completed,1);
}
static void monitor_event(MonitorFixture *f,unsigned jobs,unsigned value) {
    unsigned before=atomic_load(&f->completed);atomic_store(&f->reply_value,value);
    assert(!atomic_exchange(&f->jobs,jobs));
    CFRunLoopSourceSignal(f->source);CFRunLoopWakeUp(f->session->run_loop);
    double end=seconds_now()+2;
    while(atomic_load(&f->completed)==before&&seconds_now()<end) {
        struct timespec delay={0,100000};nanosleep(&delay,NULL);
    }
    assert(atomic_load(&f->completed)>before);
}
static int32_t monitor_read(void *opaque,uint8_t pipe,void *bytes,size_t size,
    uint32_t timeout,RK3HostBulkCompletion callback,void *context) {
    (void)pipe;(void)timeout;MonitorFixture *f=opaque;
    assert(bytes==f->session->read_buffer&&size==64&&context==f->session);
    f->input_callback=callback;f->input_context=context;return 0;
}
static int32_t monitor_write(void *opaque,uint8_t pipe,const void *bytes,size_t size,
    uint32_t timeout,RK3HostBulkCompletion callback,void *context) {
    (void)pipe;(void)timeout;MonitorFixture *f=opaque;
    unsigned index=atomic_fetch_add(&f->writes,1);assert(index<1024&&size<=4);
    f->lengths[index]=size;memcpy(f->packets[index],bytes,size);
    f->output_callback=callback;f->output_context=context;
    int32_t error=atomic_load(&f->write_error);
    if(error)return error;
    if(!atomic_load(&f->hold_write))callback(context,0,size);
    if(size==1&&((const uint8_t *)bytes)[0]==0x12&&atomic_load(&f->reply_on_write))
        monitor_deliver(f,0x12,2); /* A reply during the query write must survive. */
    return 0;
}
static int32_t monitor_clear(void *opaque,uint8_t pipe) {
    MonitorFixture *f=opaque;
    if(atomic_load(&f->hold_write))return 0; /* Explicit RETIRE delivers both callbacks. */
    if(pipe==f->session->command_in&&f->session->pending)
        f->input_callback(f->input_context,kIOReturnAborted,0);
    if(pipe==f->session->command_out&&f->session->pending_write)
        f->output_callback(f->output_context,kIOReturnAborted,0);
    return 0;
}
static void *monitor_owner(void *opaque) {
    MonitorFixture *f=opaque;rk3_usb_session *session=f->session;
    session->run_loop=CFRunLoopGetCurrent();
    CFRunLoopSourceContext context={.info=f,.perform=monitor_jobs};
    f->source=CFRunLoopSourceCreate(NULL,0,&context);assert(f->source);
    CFRunLoopAddSource(session->run_loop,f->source,kCFRunLoopDefaultMode);
    assert(arm_reader(session));
    if(atomic_load(&session->audio))assert(!sync_audio_clock_timer(session));
    atomic_store(&f->ready,true);double end=seconds_now()+12;
    while(!atomic_load(&session->closing)) {
        assert(seconds_now()<end);
        if(!atomic_load(&f->pause_requests))process_one_request(session,true);
        pump_callbacks(session,.001);
        atomic_store(&f->service_seen,fake_service_calls);
        atomic_store(&f->clock_seen,fake_clock_service_calls);
    }
    monitor_retire(f);cancel_audio_clock_timer(session);
    CFRunLoopRemoveSource(session->run_loop,f->source,kCFRunLoopDefaultMode);
    CFRelease(f->source);
    pthread_mutex_lock(&session->mutex);
    while(session->head) {
        Request *request=session->head;session->head=request->next;--session->queued;
        complete_locked(request,ECANCELED);
    }
    session->tail=NULL;session->run_loop=NULL;session->status.state=RK3_USB_CLOSED;
    pthread_cond_broadcast(&session->wake);pthread_mutex_unlock(&session->mutex);
    atomic_store(&f->done,true);release_session(session);return NULL;
}
static void monitor_begin(MonitorFixture *f,bool audio) {
    memset(f,0,sizeof(*f));f->session=calloc(1,sizeof(*f->session));assert(f->session);
    initialize(f->session);f->session->spec_generation=9;f->session->monitor_mode=0x63;
    atomic_init(&f->ready,false);atomic_init(&f->done,false);atomic_init(&f->pause_requests,false);
    atomic_init(&f->reply_on_write,false);atomic_init(&f->hold_write,false);atomic_init(&f->jobs,0);
    atomic_init(&f->completed,0);atomic_init(&f->writes,0);atomic_init(&f->reply_value,0);
    atomic_init(&f->service_seen,0);atomic_init(&f->clock_seen,0);atomic_init(&f->write_error,0);
    /* Deliberately no spec_available, identity availability or engine requirement. */
    f->interface=(RK3HostInterface){.bulk_read=monitor_read,.bulk_write=monitor_write,
        .clear_stall=monitor_clear,.abort_pipe=monitor_clear};
    f->session->interface=&f->interface;f->session->opened=true;
    f->session->command_in=4;f->session->command_out=7;f->session->status.state=RK3_USB_READY;
    if(audio) {
        test_audio(f->session,&f->audio);fake_audio_running=fake_clock_valid=true;
        f->audio.clock_armed=true;f->audio.incarnation=1;
        f->audio.deadline=mach_absolute_time()+test_clock_ticks(1000000);
    }
    atomic_store(&monitor_free_target,f->session);atomic_store(&monitor_free_count,0);
    assert(!pthread_create(&f->worker,NULL,monitor_owner,f));double end=seconds_now()+2;
    while(!atomic_load(&f->ready)&&seconds_now()<end)sched_yield();
    assert(atomic_load(&f->ready));
}
static void monitor_end(MonitorFixture *f,bool already_closed) {
    if(!already_closed)rk3_usb_close(f->session);
    assert(!pthread_join(f->worker,NULL));
    assert(atomic_load(&f->done)&&atomic_load(&monitor_free_count)==1);
    atomic_store(&monitor_free_target,NULL);
}
typedef struct {
    MonitorFixture *fixture;
    unsigned sleeps,fragments,at,jobs,value;
    bool interrupt_once,fragment_pending,fail,pause,service;
    atomic_bool waiting,released;
} MonitorSleep;
static _Thread_local MonitorSleep *monitor_sleep;
int rk3_test_direct_monitor_nanosleep(const struct timespec *request,struct timespec *remaining) {
    MonitorSleep *sleep=monitor_sleep;
    if(!sleep)return nanosleep(request,remaining);
    assert(request->tv_sec==0);
    if(sleep->fragment_pending) {
        assert(request->tv_nsec==500000);sleep->fragment_pending=false;++sleep->fragments;return 0;
    }
    assert(request->tv_nsec==1000000);++sleep->sleeps;
    if(sleep->fail){errno=EIO;return -1;}
    if(sleep->service) {
        double end=seconds_now()+2;
        while(!atomic_load(&sleep->fixture->clock_seen)&&seconds_now()<end)sched_yield();
        assert(atomic_load(&sleep->fixture->clock_seen)&&atomic_load(&sleep->fixture->service_seen));
        assert(!rk3_usb_audio_stop(sleep->fixture->session,1000));
        const uint8_t midi[]={7,0,1,0xf8};
        assert(rk3_usb_command_for_generation(sleep->fixture->session,midi,sizeof(midi),9,1000));
        monitor_event(sleep->fixture,MONITOR_REPLY,0x34);sleep->service=false;
    }
    if(sleep->sleeps==sleep->at&&sleep->jobs)monitor_event(sleep->fixture,sleep->jobs,sleep->value);
    if(sleep->pause) {
        atomic_store(&sleep->waiting,true);double end=seconds_now()+2;
        while(!atomic_load(&sleep->released)&&seconds_now()<end)sched_yield();
        assert(atomic_load(&sleep->released));sleep->pause=false;
    }
    if(sleep->interrupt_once) {
        sleep->interrupt_once=false;sleep->fragment_pending=true;*remaining=(struct timespec){0,500000};
        errno=EINTR;return -1;
    }
    return 0;
}
static int32_t monitor_get(MonitorFixture *f,MonitorSleep *sleep,uint8_t *value) {
    sleep->fixture=f;monitor_sleep=sleep;
    int32_t result=rk3_usb_direct_monitor_get(f->session,9,value);monitor_sleep=NULL;return result;
}
static void direct_monitor_values_test(void) {
    MonitorFixture f;monitor_begin(&f,false);atomic_store(&f.reply_on_write,true);
    for(unsigned i=0;i<256;++i) {
        unsigned before=atomic_load(&f.writes);
        assert(!rk3_usb_direct_monitor_set(f.session,9,(uint8_t)i));
        assert(f.lengths[before]==2&&f.packets[before][0]==0x0e&&f.packets[before][1]==i);
        pthread_mutex_lock(&f.session->mutex);assert(f.session->monitor_mode==0x63);pthread_mutex_unlock(&f.session->mutex);
    }
    for(unsigned i=0;i<256;++i) {
        atomic_store(&f.reply_value,i);unsigned before=atomic_load(&f.writes);
        uint8_t value=0x42;MonitorSleep sleep={0};assert(!monitor_get(&f,&sleep,&value)&&value==i&&!sleep.sleeps);
        assert(atomic_load(&f.writes)==before+1&&f.lengths[before]==1&&f.packets[before][0]==0x12);
    }
    assert(rk3_usb_direct_monitor_set(f.session,0,1)==EINVAL);
    uint8_t unchanged=0x42;assert(rk3_usb_direct_monitor_get(f.session,0,&unchanged)==EINVAL);
    assert(rk3_usb_direct_monitor_get(f.session,8,&unchanged)==ESTALE&&unchanged==0x42);
    assert(rk3_usb_direct_monitor_get(NULL,9,&unchanged)==EINVAL);
    assert(rk3_usb_direct_monitor_get(f.session,9,NULL)==EINVAL);
    monitor_end(&f,false);
}
static void direct_monitor_poll_test(void) {
    MonitorFixture f;monitor_begin(&f,false);uint8_t value=0x42;
    monitor_event(&f,MONITOR_REPLY,0x1a);
    MonitorSleep sleep={0};assert(!monitor_get(&f,&sleep,&value)&&value==255&&sleep.sleeps==1000);
    sleep=(MonitorSleep){.at=2,.jobs=MONITOR_REPLY,.value=0x7f,.interrupt_once=true};
    assert(!monitor_get(&f,&sleep,&value)&&value==0x7f&&sleep.sleeps==2&&sleep.fragments==1);
    sleep=(MonitorSleep){.at=1,.jobs=MONITOR_SHORT|MONITOR_UNRELATED};
    assert(!monitor_get(&f,&sleep,&value)&&value==255&&sleep.sleeps==1000);
    sleep=(MonitorSleep){.at=1000,.jobs=MONITOR_REPLY,.value=0x5a};
    assert(!monitor_get(&f,&sleep,&value)&&value==255&&sleep.sleeps==1000);
    pthread_mutex_lock(&f.session->mutex);assert(f.session->monitor_mode==0x5a);pthread_mutex_unlock(&f.session->mutex);
    sleep=(MonitorSleep){.fail=true};value=0x42;
    assert(monitor_get(&f,&sleep,&value)==EIO&&value==0x42);
    atomic_store(&f.write_error,kIOReturnIOError);
    unsigned before=atomic_load(&f.writes);sleep=(MonitorSleep){0};
    assert(!rk3_usb_direct_monitor_set(f.session,9,0x11)&&atomic_load(&f.writes)==before+1);
    assert(!monitor_get(&f,&sleep,&value)&&value==255&&sleep.sleeps==1000);
    monitor_end(&f,false);
    monitor_begin(&f,false);sleep=(MonitorSleep){.at=1000,.jobs=MONITOR_GENERATION|MONITOR_REPLY,.value=0x11};
    value=0x42;assert(monitor_get(&f,&sleep,&value)==ESTALE&&value==0x42&&sleep.sleeps==1000);
    monitor_end(&f,false);
    monitor_begin(&f,false);sleep=(MonitorSleep){.at=1,.jobs=MONITOR_REVOKE};
    value=0x42;assert(monitor_get(&f,&sleep,&value)==ENODEV&&value==0x42);monitor_end(&f,false);
    monitor_begin(&f,false);atomic_store(&f.write_error,kIOReturnNoDevice);sleep=(MonitorSleep){0};
    value=0x42;assert(monitor_get(&f,&sleep,&value)==ENODEV&&value==0x42);monitor_end(&f,false);
    monitor_begin(&f,true);sleep=(MonitorSleep){.service=true};
    assert(!monitor_get(&f,&sleep,&value)&&value==0x34);monitor_end(&f,false);
}
typedef struct { MonitorFixture *fixture;MonitorSleep sleep;uint8_t value;int32_t result; } MonitorGetter;
static void *monitor_get_thread(void *opaque) {
    MonitorGetter *getter=opaque;getter->result=monitor_get(getter->fixture,&getter->sleep,&getter->value);return NULL;
}
static void monitor_wait_sleep(MonitorSleep *sleep) {
    double end=seconds_now()+2;
    while(!atomic_load(&sleep->waiting)&&seconds_now()<end)sched_yield();assert(atomic_load(&sleep->waiting));
}
static void direct_monitor_shared_close_test(void) {
    MonitorFixture f;monitor_begin(&f,false);
    MonitorGetter a={.fixture=&f,.sleep={.pause=true},.value=0x42},b=a;pthread_t first,second;
    assert(!pthread_create(&first,NULL,monitor_get_thread,&a));monitor_wait_sleep(&a.sleep);
    monitor_event(&f,MONITOR_REPLY,0x11);
    assert(!pthread_create(&second,NULL,monitor_get_thread,&b));monitor_wait_sleep(&b.sleep);
    pthread_mutex_lock(&f.session->mutex);assert(f.session->monitor_mode==-1);pthread_mutex_unlock(&f.session->mutex);
    assert(atomic_load(&f.writes)==2);monitor_event(&f,MONITOR_REPLY,0x66);
    atomic_store(&a.sleep.released,true);atomic_store(&b.sleep.released,true);
    assert(!pthread_join(first,NULL)&&!pthread_join(second,NULL));
    assert(!a.result&&!b.result&&a.value==0x66&&b.value==0x66);monitor_end(&f,false);
    monitor_begin(&f,false);a=(MonitorGetter){.fixture=&f,.sleep={.pause=true},.value=0x42};
    assert(!pthread_create(&first,NULL,monitor_get_thread,&a));monitor_wait_sleep(&a.sleep);
    rk3_usb_close(f.session);assert(!pthread_join(f.worker,NULL));
    assert(atomic_load(&f.done)&&!atomic_load(&monitor_free_count));
    atomic_store(&a.sleep.released,true);assert(!pthread_join(first,NULL));
    assert(a.result==ENODEV&&a.value==0x42&&atomic_load(&monitor_free_count)==1);
    atomic_store(&monitor_free_target,NULL);
    monitor_begin(&f,false);atomic_store(&f.pause_requests,true);
    a=(MonitorGetter){.fixture=&f,.value=0x42};
    assert(!pthread_create(&first,NULL,monitor_get_thread,&a));double end=seconds_now()+2;
    for(;;) {pthread_mutex_lock(&f.session->mutex);bool queued=f.session->head!=NULL;pthread_mutex_unlock(&f.session->mutex);
        if(queued)break;assert(seconds_now()<end);sched_yield();}
    rk3_usb_close(f.session);
    assert(!pthread_join(first,NULL)&&!pthread_join(f.worker,NULL));
    assert(a.result==ENODEV&&a.value==0x42&&!atomic_load(&f.writes)&&atomic_load(&monitor_free_count)==1);
    atomic_store(&monitor_free_target,NULL);
}
static void direct_monitor_property_bridge_test(void) {
    MonitorFixture f;monitor_begin(&f,false);usb=f.session;host=NULL;
    RK3NativeMonitorQuery query={.version=1,.generation=9};
    for(unsigned byte=0;byte<256;++byte) {
        RK3NativeMonitorValue value={.version=1,.mode=UINT32_C(0xaabbcc00)|byte,.generation=9};
        unsigned before=atomic_load(&f.writes);
        assert(!rk3_native_set_direct_monitor(DEVICE,&query,&value));
        assert(atomic_load(&f.writes)==before+1&&f.packets[before][1]==byte);
    }
    atomic_store(&f.reply_on_write,true);atomic_store(&f.reply_value,0xff);
    RK3NativeMonitorValue value={.mode=0x42};
    assert(!rk3_native_copy_direct_monitor(DEVICE,&query,&value)&&value.version==1&&value.mode==255&&value.generation==9);
    unsigned before=atomic_load(&f.writes);query.generation=8;value.mode=0x42;
    assert(rk3_native_copy_direct_monitor(DEVICE,&query,&value)==kAudioHardwareNotRunningError&&value.mode==0x42);
    query.generation=9;query.reserved=1;
    assert(rk3_native_copy_direct_monitor(DEVICE,&query,&value)==kAudioHardwareIllegalOperationError&&value.mode==0x42);
    assert(atomic_load(&f.writes)==before);
    assert(!rk3_usb_command_for_generation(f.session,(const uint8_t[]){0x0e,0},2,9,100));
    assert(!rk3_usb_command_for_generation(f.session,(const uint8_t[]){0x12},1,9,100));
    usb=NULL;monitor_end(&f,false);
}
static void direct_monitor_admission_drain_test(void) {
    MonitorFixture f;monitor_begin(&f,false);atomic_store(&f.pause_requests,true);
    uint8_t value=0x42;MonitorSleep sleep={0};
    pthread_mutex_lock(&f.session->mutex);f.session->queued=COMMAND_QUEUE;pthread_mutex_unlock(&f.session->mutex);
    assert(rk3_usb_direct_monitor_set(f.session,9,1)==ECANCELED);
    assert(monitor_get(&f,&sleep,&value)==ECANCELED&&value==0x42);
    pthread_mutex_lock(&f.session->mutex);f.session->queued=0;pthread_mutex_unlock(&f.session->mutex);
    assert(monitor_get(&f,&sleep,&value)==ETIMEDOUT&&value==0x42&&!sleep.sleeps&&!atomic_load(&f.writes));
    atomic_store(&f.pause_requests,false);double end=seconds_now()+2;
    for(;;) {pthread_mutex_lock(&f.session->mutex);bool empty=!f.session->head;pthread_mutex_unlock(&f.session->mutex);
        if(empty)break;assert(seconds_now()<end);sched_yield();}
    pthread_mutex_lock(&f.session->mutex);assert(f.session->monitor_mode==0x63);pthread_mutex_unlock(&f.session->mutex);
    atomic_store(&f.pause_requests,true);
    pthread_mutex_lock(&f.session->mutex);
    Request *expired=enqueue_test_request(f.session,REQUEST_MONITOR_QUERY,NULL,0);
    expired->deadline=seconds_now()-1; /* Not cancelled: test the worker's own deadline guard. */
    pthread_mutex_unlock(&f.session->mutex);atomic_store(&f.pause_requests,false);end=seconds_now()+2;
    for(;;) {pthread_mutex_lock(&f.session->mutex);bool done=expired->complete;pthread_mutex_unlock(&f.session->mutex);
        if(done)break;assert(seconds_now()<end);sched_yield();}
    assert(expired->result==ETIMEDOUT&&!atomic_load(&f.writes));free_test_request(expired);
    pthread_mutex_lock(&f.session->mutex);assert(f.session->monitor_mode==0x63);pthread_mutex_unlock(&f.session->mutex);
    atomic_store(&f.hold_write,true);assert(!rk3_usb_direct_monitor_set(f.session,9,0x34));
    assert(f.session->pending_write&&f.session->command_recovering);
    assert(rk3_usb_direct_monitor_set(f.session,9,0x56)==ETIMEDOUT&&atomic_load(&f.writes)==1);
    monitor_event(&f,MONITOR_RETIRE,0);atomic_store(&f.hold_write,false);end=seconds_now()+2;
    for(;;) {pthread_mutex_lock(&f.session->mutex);bool empty=!f.session->head;pthread_mutex_unlock(&f.session->mutex);
        if(empty)break;assert(seconds_now()<end);sched_yield();}
    assert(atomic_load(&f.writes)==1);monitor_end(&f,false);
}

int main(void) {
    owner_creation_test();
    direct_monitor_values_test();direct_monitor_poll_test();direct_monitor_shared_close_test();direct_monitor_admission_drain_test();direct_monitor_property_bridge_test();
    identity_capture_publication_test();identity_partial_capture_test();identity_property_test();
    identity_held_request_close_test();identity_cohort_test();identity_snapshot_race_test();identity_pending_drain_test();identity_opaque_transport_test();
    identity_reader_first_test();identity_reader_fallback_test();identity_reader_unicode_test();identity_reader_oracles_test();identity_reader_definedness_test();
    spec_publication_test();spec_refresh_test();spec_retirement_test();spec_protocol_retirement_test();spec_drain_test();spec_close_test();spec_property_test();spec_snapshot_race_test();
    gain_property_test();gain_generation_borrow_test();history_test(); request_test(); pedal_configuration_command_test(); active_deadline_test(); clock_startup_test(); clock_snapshot_forwarding_test(); retirement_test(); close_wait_test(); configuration_test(); command_pipe_reset_test(); fresh_command_connection_test(); initial_reader_rejection_test(); stop_ack_test(); controller_startup_test(); controller_startup_lifetime_test(); asynchronous_write_test(); panic_containment_test(); cooperative_stop_test(); panic_startup_and_address_test(); callback_lease_test(); activity_publication_test(); host_storage_lifetime_test(); ep1_read_status_test(); ep1_write_no_replay_test(); ep1_late_success_and_queue_test(); ep1_suspended_and_removal_test(); ep1_clear_failure_test(); stream_diagnostic_visibility_test(); connection_state_test();
    clock_timer_binding_test();clock_timer_stop_restart_test();clock_timer_guard_test();clock_timer_retirement_test();
    clock_timer_reentrant_restart_test();clock_timer_command_pump_test();clock_timer_failure_test();
    sample_rate_request_test();sample_rate_vectors_test();sample_rate_admission_test();
    sample_rate_late_ack_test();sample_rate_failure_test();sample_rate_explicit_retry_test();sample_rate_publication_test();
    puts("NativeUSB: acknowledged five-rate transactions/publication, explicit owner QoS/creation lifetime, worker clock one-shots/lifetime, NI EP1 error matrix/no replay, gated retirement, audio-only panic, cooperative drain and addressed I/O passed");
    return 0;
}
