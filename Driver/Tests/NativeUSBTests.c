/* Hardware-free transport tests. NativeAudioISO is mocked only in this file;
 * the production driver links the real ISO implementation. */
#include "../NativeUSB.c"
#include <assert.h>

struct rk3_native_audio_iso { int unused; };
static bool fake_audio_running, fake_clock_valid;
static int32_t fake_audio_error;
rk3_native_audio_iso *rk3_native_audio_iso_create(IOUSBInterfaceInterface300 **interface,
    uint8_t input, uint8_t output, rk3_native_audio_command command, void *context) {
    (void)interface; (void)input; (void)output; (void)command; (void)context; return NULL;
}
int32_t rk3_native_audio_iso_start(rk3_native_audio_iso *audio) { (void)audio; return ENODEV; }
void rk3_native_audio_iso_stop(rk3_native_audio_iso *audio) { (void)audio; }
size_t rk3_native_audio_iso_pending(const rk3_native_audio_iso *audio) { (void)audio; return 0; }
bool rk3_native_audio_iso_is_quiescent(const rk3_native_audio_iso *audio) { (void)audio; return true; }
bool rk3_native_audio_iso_release_usb(rk3_native_audio_iso *audio) { (void)audio; return true; }
bool rk3_native_audio_iso_destroy(rk3_native_audio_iso *audio) { (void)audio; return true; }
int32_t rk3_native_audio_iso_error(const rk3_native_audio_iso *audio) { (void)audio; return fake_audio_error; }
bool rk3_native_audio_iso_running(const rk3_native_audio_iso *audio) { (void)audio; return fake_audio_running; }
size_t rk3_native_audio_iso_render(rk3_native_audio_iso *audio, const float *samples, size_t frames) {
    (void)audio; (void)samples; (void)frames; return 0;
}
size_t rk3_native_audio_iso_capture(rk3_native_audio_iso *audio, float *samples, size_t frames) {
    (void)audio; (void)samples; (void)frames; return 0;
}
bool rk3_native_audio_iso_clock(rk3_native_audio_iso *audio, rk3_native_audio_clock *clock) {
    (void)audio;
    if (fake_clock_valid) *clock = (rk3_native_audio_clock){48, 1000, 1};
    return fake_clock_valid;
}

static unsigned int read_arms;
static unsigned int command_writes;
static rk3_usb_session *command_io_session;
static rk3_usb_session *ack_test_session;
static int ack_test_result;
static IOReturn command_write_result;
static IOReturn command_callback_result;
static bool short_write_callback, defer_write, signal_deferred;
static IOAsyncCallback1 deferred_write_callback;
static void *deferred_write_context, *deferred_write_buffer;
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
static IOReturn fake_read(void *self, UInt8 pipe, void *buffer, UInt32 size,
                         IOAsyncCallback1 callback, void *context) {
    (void)self; (void)pipe; (void)buffer; (void)callback; (void)context;
    assert(size == 64); ++read_arms;
    if (command_io_session != NULL) trace_command(COMMAND_READ);
    return kIOReturnSuccess;
}
static IOReturn fake_write(void *self, UInt8 pipe, void *buffer, UInt32 size,
                          UInt32 no_data, UInt32 completion, IOAsyncCallback1 callback, void *context) {
    (void)self; (void)pipe; (void)buffer;
    assert(size > 0 && size <= 64 && no_data > 0 && no_data <= 200 && completion == no_data);
    ++command_writes;
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
            report_complete(controller_test_session, kIOReturnSuccess, (void *)(uintptr_t)controller_reply_length);
        }
    }
    if (command_write_result != kIOReturnSuccess) return command_write_result;
    if (defer_write) {
        deferred_write_callback = callback; deferred_write_context = context;
        deferred_write_buffer = buffer; deferred_write_length = size;
        if (signal_deferred) CFRunLoopSourceSignal(deferred_source);
    } else callback(context, command_callback_result, (void *)(uintptr_t)(short_write_callback ? size-1 : size));
    return kIOReturnSuccess;
}

static void initialize(rk3_usb_session *session) {
    memset(session, 0, sizeof(*session));
    assert(pthread_mutex_init(&session->mutex, NULL) == 0);
    assert(pthread_cond_init(&session->wake, NULL) == 0);
    atomic_init(&session->closing, false);
    atomic_init(&session->audio, NULL);
    atomic_init(&session->clock_generation, 1);
    atomic_init(&session->rt_readers, 0);
    atomic_init(&session->references, 2);
    session->status.info.midi_outputs = 1;
    session->generation = 9;
}

static void finish(rk3_usb_session *session) {
    assert(session->head == NULL && session->queued == 0);
    pthread_cond_destroy(&session->wake);
    pthread_mutex_destroy(&session->mutex);
}

static void history_test(void) {
    rk3_usb_session session;
    initialize(&session);
    IOUSBInterfaceInterface300 functions = {0};
    functions.ReadPipeAsync = fake_read;
    IOUSBInterfaceInterface300 *interface = &functions;
    session.interface = &interface;
    for (size_t index = 0; index < 300; ++index) {
        session.read_buffer[0] = 6;
        session.read_buffer[1] = (uint8_t)index;
        report_complete(&session, kIOReturnSuccess, (void *)(uintptr_t)2);
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
    report_complete(&session, kIOReturnSuccess, (void *)(uintptr_t)2);
    query.after_sequence = 0;
    assert(rk3_usb_reports(&session, &query, &second) && second.count == 1 && second.reports[0].sequence == 301);
    query.after_sequence = 300;
    assert(rk3_usb_reports(&session, &query, &second) && second.reports[0].sequence == 301);
    session.stopping_io = true;
    report_complete(&session, kIOReturnAborted, NULL);
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
    assert(rk3_usb_capture(&session, samples, 4) == 0);
    for (size_t index = 0; index < 8; ++index) assert(samples[index] == 0);
    assert(rk3_usb_render(&session, samples, 4) == 0);
    finish(&session);
}

static void active_deadline_test(void) {
    rk3_usb_session session;
    initialize(&session);
    IOUSBInterfaceInterface300 functions = {0}; functions.WritePipeAsyncTO = fake_write;
    IOUSBInterfaceInterface300 *interface = &functions;
    session.interface = &interface; session.opened = true;
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
static IOReturn fake_command_source(void *self, CFRunLoopSourceRef *source) {
    (void)self; trace_command(COMMAND_SOURCE);
    CFRunLoopSourceContext context = {0};
    *source = CFRunLoopSourceCreate(NULL, 0, &context);
    return *source != NULL ? kIOReturnSuccess : kIOReturnNoMemory;
}
static void command_pipe_reset_test(void) {
    for (unsigned int failure = 0; failure <= 2; ++failure) {
        rk3_usb_session session; initialize(&session);
        IOUSBInterfaceInterface300 functions = {0};
        functions.ClearPipeStallBothEnds = fake_reset_pipe;
        functions.CreateInterfaceAsyncEventSource = fake_command_source;
        functions.ReadPipeAsync = fake_read; functions.WritePipeAsyncTO = fake_write;
        IOUSBInterfaceInterface300 *interface = &functions;
        session.interface = &interface; session.opened = true;
        session.command_in = 4; session.command_out = 7; session.run_loop = CFRunLoopGetCurrent();
        command_io_session = &session; command_io_trace_length = 0; reset_failure = failure;
        unsigned int before_reads = read_arms, before_writes = command_writes;
        bool result = start_command_io(&session);
        if (failure == 0) {
            const unsigned int expected[] = {COMMAND_RESET_IN,COMMAND_RESET_OUT,COMMAND_SOURCE,COMMAND_READ,COMMAND_WRITE};
            assert(result && command_io_trace_length == 5 && memcmp(command_io_trace, expected, sizeof(expected)) == 0);
            assert(read_arms == before_reads + 1 && command_writes == before_writes + 1 && session.pending);
            CFRunLoopRemoveSource(session.run_loop, session.source, kCFRunLoopDefaultMode); CFRelease(session.source);
        } else {
            assert(!result && command_io_trace_length == failure);
            assert(session.source == NULL && !session.pending && read_arms == before_reads && command_writes == before_writes);
            assert(session.status.error_code == kIOReturnIOError);
        }
        command_io_session = NULL;
        finish(&session);
    }
}

static void stop_ack_test(void) {
    rk3_usb_session session; initialize(&session);
    IOUSBInterfaceInterface300 functions = {0}; functions.WritePipeAsyncTO = fake_write;
    IOUSBInterfaceInterface300 *interface = &functions;
    session.interface = &interface; session.opened = true; ack_test_session = &session;
    const uint8_t stop[] = {9,255,255,136,0,1}, start[] = {9,1,2,136,0,1};
    ack_test_result = 0;
    assert(command_worker(&session, stop, sizeof(stop), 10));
    assert(!command_worker(&session, start, sizeof(start), 10));
    ack_test_result = 1;
    assert(command_worker(&session, stop, sizeof(stop), 10));
    assert(command_worker(&session, start, sizeof(start), 10));
    ack_test_result = -1;
    assert(!command_worker(&session, stop, sizeof(stop), 2)); /* missing ACK remains failure */
    ack_test_result = 0; command_write_result = kIOReturnIOError;
    assert(!command_worker(&session, stop, sizeof(stop), 10)); /* actual write must succeed */
    command_write_result = kIOReturnSuccess; ack_test_session = NULL;
    finish(&session);
}

static void controller_startup_test(void) {
    for (unsigned int failure = 0; failure <= 4; ++failure) {
        rk3_usb_session session; initialize(&session);
        IOUSBInterfaceInterface300 functions = {0};
        functions.WritePipeAsyncTO = fake_write; functions.ReadPipeAsync = fake_read;
        IOUSBInterfaceInterface300 *interface = &functions;
        session.interface = &interface; session.opened = session.pending = true;
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
    IOUSBInterfaceInterface300 functions = {0};
    functions.WritePipeAsyncTO = fake_write; functions.ReadPipeAsync = fake_read;
    IOUSBInterfaceInterface300 *interface = &functions;
    session.interface = &interface; session.opened = session.pending = true;
    Request active = {.deadline = seconds_now() + 0.01}; session.active_request = &active;
    controller_test_session = &session; controller_count = 0; controller_fail_at = 0; controller_reply_length = 2;
    assert(!initialize_device_commands(&session) && controller_count == 4 && !session.digital_known);
    controller_test_session = NULL;
    session.transport_failed = false; session.read_buffer[0] = 4;
    session.read_buffer[1] = session.read_buffer[2] = 255;
    report_complete(&session, kIOReturnSuccess, (void *)(uintptr_t)3);
    assert(session.digital_known && session.digital_snapshot == 0x1ff);
    report_complete(&session, kIOReturnSuccess, (void *)(uintptr_t)65);
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
            deferred_write_callback(deferred_write_context, kIOReturnSuccess, (void *)(uintptr_t)deferred_write_length);
        return;
    }
    unsigned int previous_reads = read_arms;
    event->session->read_buffer[0] = 3; event->session->read_buffer[1] = 42;
    report_complete(event->session, kIOReturnSuccess, (void *)(uintptr_t)2);
    event->read_rearmed = event->session->pending && read_arms == previous_reads + 1;
    assert(event->read_rearmed && event->session->pending_write);
    deferred_write_callback(deferred_write_context, kIOReturnSuccess, (void *)(uintptr_t)deferred_write_length);
}

static void asynchronous_write_test(void) {
    IOUSBInterfaceInterface300 functions = {0};
    functions.WritePipeAsyncTO = fake_write; functions.ReadPipeAsync = fake_read;
    functions.AbortPipe = fake_abort_command;
    IOUSBInterfaceInterface300 *interface = &functions;
    uint8_t command[] = {7,0,1,0xf8};
    for (unsigned int scenario = 0; scenario < 7; ++scenario) {
        rk3_usb_session session; initialize(&session);
        session.interface = &interface; session.opened = true;
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
            session.stopping_io = true; report_complete(&session, kIOReturnAborted, NULL);
        } else if (scenario == 6) {
            assert(!session.pending_write && session.transport_failed && command_aborts == previous_aborts);
        } else if (scenario >= 4) {
            assert(session.pending_write && session.transport_failed && io_pending(&session, NULL));
            assert(command_aborts == previous_aborts + 1 && deferred_write_buffer == session.write_buffer && deferred_write_context == &session);
            uint8_t preserved[sizeof(command)]; memcpy(preserved, session.write_buffer, sizeof(command));
            command[3] ^= 1; /* A caller/request buffer can die or change after timeout. */
            unsigned int previous_writes = command_writes;
            assert(!command_worker(&session, command, sizeof(command), 10) && command_writes == previous_writes);
            assert(memcmp(session.write_buffer, preserved, sizeof(preserved)) == 0);
            deferred_write_callback(deferred_write_context, kIOReturnAborted, NULL);
            assert(!session.pending_write && !io_pending(&session, NULL));
            command[3] ^= 1;
        } else {
            assert(!session.pending_write && session.transport_failed);
            if (scenario == 3) assert(session.status.error_code == kIOReturnUnderrun);
        }
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), deferred_source, kCFRunLoopDefaultMode);
        CFRelease(deferred_source); deferred_source = NULL;
        finish(&session);
    }
    command_write_result = command_callback_result = kIOReturnSuccess;
    defer_write = short_write_callback = signal_deferred = false;
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
    history_test(); request_test(); active_deadline_test(); clock_startup_test(); retirement_test(); close_wait_test(); configuration_test(); command_pipe_reset_test(); stop_ack_test(); controller_startup_test(); asynchronous_write_test(); connection_state_test();
    puts("NativeUSB hardware-free history, cancellation, command policy and generation tests passed");
    return 0;
}
