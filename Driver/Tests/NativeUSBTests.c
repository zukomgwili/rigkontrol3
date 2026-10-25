/* Hardware-free transport tests. NativeAudioISO is mocked only in this file;
 * the production driver links the real ISO implementation. */
#include "../NativeUSB.c"
#include <assert.h>

struct rk3_native_audio_iso { int unused; };
static bool fake_audio_running, fake_clock_valid, fake_audio_quiescent=true;
static bool fake_panic_pending;
static uint32_t fake_panic_delay;
static int32_t fake_panic_error;
static unsigned fake_stop_calls, fake_start_calls, fake_service_calls;
static bool check_host_lifetime, mapped_released, fake_output_active;
static unsigned release_busy, close_busy, release_usb_calls, host_close_calls, host_release_calls, host_alt_calls, activity_calls;
static rk3_usb_session *activity_publication;
static int32_t fake_audio_error, fake_stream_error, fake_start_result=ENODEV;
rk3_native_audio_iso *rk3_native_audio_iso_create(RK3HostInterface *interface,
    uint8_t input, uint8_t output, rk3_native_audio_command command, void *context) {
    (void)interface; (void)input; (void)output; (void)command; (void)context; return NULL;
}
int32_t rk3_native_audio_iso_start(rk3_native_audio_iso *audio) {
    (void)audio;++fake_start_calls;if(fake_start_result==0){fake_audio_running=true;fake_panic_pending=false;fake_audio_error=fake_stream_error=0;}return fake_start_result;
}
void rk3_native_audio_iso_stop(rk3_native_audio_iso *audio) { (void)audio; fake_audio_running=false; ++fake_stop_calls; }
size_t rk3_native_audio_iso_pending(const rk3_native_audio_iso *audio) { (void)audio; return fake_audio_quiescent?0:1; }
bool rk3_native_audio_iso_is_quiescent(const rk3_native_audio_iso *audio) { return audio==NULL||fake_audio_quiescent; }
bool rk3_native_audio_iso_release_usb(rk3_native_audio_iso *audio) {
    (void)audio;if(check_host_lifetime){++release_usb_calls;if(release_busy){--release_busy;return false;}mapped_released=true;}return true;
}
bool rk3_native_audio_iso_destroy(rk3_native_audio_iso *audio) { (void)audio; return true; }
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
    if(frames) {samples[0]=0.5f;samples[1]=-0.5f;return 1;}return 0;
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
static unsigned int controller_fail_at;
static unsigned int command_io_trace[16], command_io_trace_length, reset_failure;
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
    return kIOReturnSuccess;
}
static int32_t fake_write(void *self, uint8_t pipe, const void *buffer, size_t size,
                          uint32_t no_data, RK3HostBulkCompletion callback, void *context) {
    (void)self; (void)pipe; (void)buffer;
    assert(size > 0 && size <= 64 && no_data > 0 && no_data <= 200);
    ++command_writes;last_command_size=size;memcpy(last_command_bytes,buffer,size);
    if (ack_test_session != NULL) ack_test_session->ack_result = ack_test_result;
    if (command_io_session != NULL) {
        trace_command(COMMAND_WRITE);
        assert(size == 1 && ((const uint8_t *)buffer)[0] == RK3_COMMAND_INFO);
        command_io_session->info_received = true;
    }
    if (controller_test_session != NULL) {
        assert(controller_count < 4);
        controller_lengths[controller_count] = size;
        memcpy(controller_commands[controller_count++], buffer, size);
        if (controller_fail_at == controller_count) return kIOReturnIOError;
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
    session->status.info.midi_outputs = 1;
    session->generation = 9;
    fake_audio_quiescent = true; fake_panic_pending = false;
    fake_audio_error = fake_stream_error = 0; fake_audio_running = fake_clock_valid = false; fake_stop_calls = fake_start_calls = 0; fake_start_result=ENODEV;
}

static void finish(rk3_usb_session *session) {
    assert(session->head == NULL && session->queued == 0 && session->stop_head == NULL && session->deferred_stops == 0);
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
    atomic_store(&session.audio,&audio);fake_clock_valid=true;
    rk3_usb_clock_snapshot clock={0};assert(rk3_usb_clock(&session,&clock));
    assert(clock.sample_frame==20000&&clock.host_time==3000);
    assert(clock.seed==((UINT64_C(1)<<32)|1));
    assert(clock.zero_sample_frame==16384&&clock.zero_host_time==2000);
    assert(atomic_load(&session.rt_readers)==0);
    fake_clock_valid=false;assert(!rk3_usb_clock(&session,&clock));
    assert(atomic_load(&session.rt_readers)==0);
    atomic_store(&session.audio,NULL);finish(&session);
}

static void retirement_test(void) {
    rk3_usb_session session;
    initialize(&session);
    rk3_native_audio_iso old = {0}, current = {0};
    RetiredAudio *first = calloc(1, sizeof(*first)), *second = calloc(1, sizeof(*second));
    assert(first != NULL && second != NULL);
    first->audio = &old; first->next = second; second->audio = &current;
    session.retired = first; session.retired_count = 2;
    atomic_store(&session.audio, &current); atomic_store(&session.rt_readers, 1);
    reclaim_retired(&session); assert(session.retired_count == 2);
    atomic_store(&session.rt_readers, 0);
    reclaim_retired(&session); assert(session.retired_count == 1 && session.retired->audio == &current);
    atomic_store(&session.audio, NULL);
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
    configuration_open = true; ++configuration_opens; return kIOReturnSuccess;
}
static IOReturn fake_configuration_set(void *self, UInt8 value) {
    (void)self; trace_configuration(CONFIG_SET); assert(configuration_open && value == 1);
    ++configuration_sets;
    if (configuration_result == kIOReturnSuccess) fake_configuration = value;
    return configuration_result;
}
static IOReturn fake_configuration_close(void *self) {
    (void)self; trace_configuration(CONFIG_CLOSE); assert(configuration_open);
    configuration_open = false; ++configuration_closes; return kIOReturnSuccess;
}
static void configuration_test(void) {
    rk3_usb_session session; initialize(&session);
    IOUSBDeviceInterface300 functions = {0};
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
    return reset_failure == event ? kIOReturnIOError : kIOReturnSuccess;
}
static void command_pipe_reset_test(void) {
    for (unsigned int failure = 0; failure <= 2; ++failure) {
        rk3_usb_session session; initialize(&session);
        RK3HostInterface functions = {0};
        functions.clear_stall = fake_reset_pipe;
        functions.bulk_read = fake_read; functions.bulk_write = fake_write;
        RK3HostInterface *interface = &functions;
        session.interface = interface; session.opened = true;
        session.command_in = 4; session.command_out = 7; session.run_loop = CFRunLoopGetCurrent();
        command_io_session = &session; command_io_trace_length = 0; reset_failure = failure;
        unsigned int before_reads = read_arms, before_writes = command_writes;
        bool result = start_command_io(&session);
        if (failure == 0) {
            const unsigned int expected[] = {COMMAND_RESET_IN,COMMAND_RESET_OUT,COMMAND_READ,COMMAND_WRITE};
            assert(result && command_io_trace_length == 4 && memcmp(command_io_trace, expected, sizeof(expected)) == 0);
            assert(read_arms == before_reads + 1 && command_writes == before_writes + 1 && session.pending);
        } else {
            assert(!result && command_io_trace_length == failure);
            assert(!session.pending && read_arms == before_reads && command_writes == before_writes);
            assert(session.status.error_code == kIOReturnIOError);
        }
        command_io_session = NULL;
        finish(&session);
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
        controller_fail_at = failure; controller_reply_length = 3;
        assert(initialize_device_commands(&session) == (failure == 0));
        assert(controller_count == (failure == 0 ? 4 : failure));
        if (failure == 0) {
            const uint8_t audio[] = {5,0,0x40,0x40,0,0,0}, automatic[] = {11,1,2,5};
            assert(controller_lengths[0] == sizeof(audio) && memcmp(controller_commands[0], audio, sizeof(audio)) == 0);
            assert(controller_lengths[1] == 33 && controller_commands[1][0] == 15 && controller_commands[1][1] == 2);
            for (size_t index = 2; index < 33; ++index) assert(controller_commands[1][index] == 0);
            assert(controller_lengths[2] == sizeof(automatic) && memcmp(controller_commands[2], automatic, sizeof(automatic)) == 0);
            assert(controller_lengths[3] == 1 && controller_commands[3][0] == 4);
            assert(session.digital_known && session.digital_snapshot == 0x1a5);
            RK3NativeInfo info; assert(rk3_usb_info(&session, &info) && info.digital_snapshot == UINT32_C(0x800001a5));
            assert(!rk3_usb_command(&session, controller_commands[1], 33, 10));
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
static void panic_containment_test(void) {
    for(unsigned delay=0;delay<=100;delay+=100) {
        rk3_usb_session session;initialize(&session);
        RK3HostInterface interface={.bulk_read=fake_read,.bulk_write=fake_write};
        session.interface=&interface;session.opened=true;session.command_in=4;session.command_out=7;
        session.status.state=RK3_USB_RUNNING;session.want_audio=true;
        session.digital_known=true;session.digital_snapshot=0x80;
        session.worker_thread=pthread_self();session.worker_thread_known=true;
        rk3_native_audio_iso audio={0};atomic_store(&session.audio,&audio);
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
        atomic_store(&session.audio,NULL);finish(&session);
    }
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};atomic_store(&session.audio,&audio);
    session.status.state=RK3_USB_RUNNING;session.want_audio=true;fake_audio_running=true;
    fake_audio_error=kIOReturnIsoTooOld; /* OUT errors without an NI panic do not request recovery. */
    audio_panic_tick(&session,100);assert(!session.panic_pending&&!session.transport_failed&&session.status.state==RK3_USB_RUNNING);
    fake_audio_error=kIOReturnNoDevice;audio_panic_tick(&session,101);
    assert(session.transport_failed&&session.status.state==RK3_USB_FAILED&&session.generation==9);
    atomic_store(&session.audio,NULL);finish(&session);
}
static void cooperative_stop_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};atomic_store(&session.audio,&audio);
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
    assert(stop->complete&&stop->result==ETIMEDOUT&&!session.transport_failed&&atomic_load(&session.audio)==&audio);
    RK3NativeInfo info;assert(rk3_usb_info(&session,&info)&&info.flags==RK3_NATIVE_CONNECTED);
    free_test_request(stop);fake_audio_quiescent=true;
    atomic_store(&session.audio,NULL);finish(&session);
}
static void panic_startup_and_address_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};atomic_store(&session.audio,&audio);
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
    atomic_store(&session.audio,NULL);finish(&session);
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
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};atomic_store(&session.audio,&audio);
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
    atomic_store(&session.audio,NULL);finish(&session);
}

static void activity_publication_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso first={0},second={0};
    assert(atomic_load(&session.output_active));
    unsigned before=activity_calls;rk3_usb_set_output_active(&session,false);
    assert(!atomic_load(&session.output_active)&&activity_calls==before&&atomic_load(&session.rt_readers)==0);
    activity_publication=&session;publish_audio(&session,&first);activity_publication=NULL;
    assert(atomic_load(&session.audio)==&first&&!fake_output_active&&activity_calls==before+1);
    rk3_usb_set_output_active(&session,true);assert(fake_output_active&&atomic_load(&session.output_active));
    atomic_store(&session.audio,NULL);rk3_usb_set_output_active(&session,false);
    activity_publication=&session;publish_audio(&session,&second);activity_publication=NULL;
    assert(atomic_load(&session.audio)==&second&&!fake_output_active&&atomic_load(&session.rt_readers)==0);
    atomic_store(&session.audio,NULL);finish(&session);
}
static void host_storage_lifetime_test(void) {
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};atomic_store(&session.audio,&audio);
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
    rk3_usb_session session;initialize(&session);rk3_native_audio_iso audio={0};atomic_store(&session.audio,&audio);
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
    atomic_store(&session.audio,NULL);finish(&session);
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

int main(void) {
    history_test(); request_test(); active_deadline_test(); clock_startup_test(); clock_snapshot_forwarding_test(); retirement_test(); close_wait_test(); configuration_test(); command_pipe_reset_test(); stop_ack_test(); controller_startup_test(); asynchronous_write_test(); panic_containment_test(); cooperative_stop_test(); panic_startup_and_address_test(); callback_lease_test(); activity_publication_test(); host_storage_lifetime_test(); ep1_read_status_test(); ep1_write_no_replay_test(); ep1_late_success_and_queue_test(); ep1_suspended_and_removal_test(); ep1_clear_failure_test(); stream_diagnostic_visibility_test(); connection_state_test();
    puts("NativeUSB: NI EP1 error matrix/no replay, gated retirement, audio-only panic, cooperative drain and addressed I/O passed");
    return 0;
}
