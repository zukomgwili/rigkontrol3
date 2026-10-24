/* Hardware-free fake IOUSBLib tests. No device discovery or USB call is linked.
 * clang -std=c11 -fblocks -Wall -Wextra -Werror -fsanitize=address,undefined \
 *   SharedHAL/Native/Tests/engine_lifecycle_test.c SharedHAL/Native/codec_mode2.c \
 *   -framework CoreFoundation -framework IOKit -o /tmp/rk3-engine-test
 * /tmp/rk3-engine-test
 */
#define RK3_ENGINE_TESTING 1
#define RK3_INIT_TIMEOUT_MS 300
#define RK3_AUDIO_TIMEOUT_MS 300
#define RK3_CLOSE_TIMEOUT_MS 100
#include "../rk3_engine.c"
#include <assert.h>
#include <stdio.h>

struct fake_packet { uint8_t bytes[64]; size_t length; };
struct fake_completion { UInt8 pipe; IOAsyncCallback1 callback; void *context; };
static IOUSBInterfaceInterface300 fake_interface;
static IOUSBInterfaceInterface300 *fake_interface_pointer = &fake_interface;
static IOAsyncCallback1 read_callback;
static void *read_context, *read_buffer;
static struct fake_packet pending_packets[32];
static unsigned packet_head, packet_tail;
static struct fake_completion isoch[32];
static unsigned isoch_count;
static atomic_uint read_arms, abort_reads, writes, closed, epochs, published, completions;
static atomic_int state_value, last_error;
static int suppress_audio_ack, fail_read_arm, fail_iso_after, hold_abort;
static unsigned iso_submits;
static atomic_int short_next_led;
static pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct rk3_control_command queued_commands[16];
static unsigned command_head, command_tail;
static uint64_t completion_ids[16];
static int32_t completion_status[16];
static uint32_t completion_actual[16];

static void deliver_packet(void)
{
	if (!read_callback || packet_head == packet_tail) return;
	struct fake_packet packet = pending_packets[packet_head++ % 32];
	IOAsyncCallback1 callback = read_callback;
	void *context = read_context, *buffer = read_buffer;
	read_callback = NULL;
	CFRunLoopPerformBlock(CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, ^{
		memcpy(buffer, packet.bytes, packet.length);
		callback(context, kIOReturnSuccess, (void *)(uintptr_t)packet.length);
	});
}
static void packet_push(const uint8_t *bytes, size_t length)
{
	assert(packet_tail - packet_head < 32 && length <= 64);
	struct fake_packet *p = &pending_packets[packet_tail++ % 32];
	memcpy(p->bytes, bytes, length); p->length = length;
	deliver_packet();
}
static IOReturn fake_read(void *self, UInt8 pipe, void *buffer, UInt32 size,
		IOAsyncCallback1 callback, void *context)
{
	(void)self; (void)pipe;
	if (fail_read_arm) return kIOReturnNoDevice;
	assert(size == 512 && !read_callback);
	atomic_fetch_add(&read_arms, 1);
	read_callback = callback; read_context = context; read_buffer = buffer;
	deliver_packet();
	return kIOReturnSuccess;
}
static IOReturn fake_write(void *self, UInt8 pipe, void *buffer, UInt32 length,
		UInt32 no_data, UInt32 timeout, IOAsyncCallback1 callback, void *context)
{
	(void)self; (void)pipe;
	assert(no_data > 0 && no_data <= 200 && timeout == no_data);
	assert(atomic_load(&read_arms) > 0); /* IN was armed before ANY command */
	uint8_t command = ((uint8_t *)buffer)[0];
	if (command == EP1_CMD_AUDIO_PARAMS) assert(isoch_count == RK3_N_XFERS);
	unsigned write_index = atomic_fetch_add(&writes, 1);
	if (write_index == 0) assert(command == 1 && length == 1);
	if (write_index == 1) {
		assert(command == 5 && length == 64);
		for (unsigned i = 1; i < 64; i++)
			assert(((uint8_t *)buffer)[i] == ((i == 2 || i == 3) ? 0x40 : 0));
	}
	if (write_index == 2) {
		const uint8_t expected[] = { 0x0b, 1, 10, 0 };
		assert(length == 4 && memcmp(buffer, expected, 4) == 0);
	}
	CFRunLoopPerformBlock(CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, ^{
		uint32_t actual = command == 5 && atomic_exchange(&short_next_led, 0) ? length - 1 : length;
		callback(context, kIOReturnSuccess, (void *)(uintptr_t)actual);
		if (command == EP1_CMD_GET_DEVICE_INFO) {
			const uint8_t unsolicited[] = { 3, 0, 0, 0, 0, 0, 88 };
			const uint8_t spec[] = { 1, 13, 0, 6, 0, 3, 9, 41, 2, 2, 0, 0, 1, 1, 2 };
			packet_push(unsolicited, sizeof(unsolicited));
			packet_push(spec, sizeof(spec));
		} else if (command == EP1_CMD_AUDIO_PARAMS) {
			const uint8_t analog[] = { 3, 0, 0, 0, 0, 0, 95 };
			const uint8_t digital[] = { 4, 16, 0, 0, 0, 0, 0, 0 };
			const uint8_t ack[] = { 9, 1 };
			packet_push(analog, sizeof(analog)); packet_push(digital, sizeof(digital));
			if (!suppress_audio_ack) packet_push(ack, sizeof(ack));
		}
	});
	return kIOReturnSuccess;
}
static IOReturn fake_iso(void *self, UInt8 pipe, void *buffer, UInt64 frame,
		UInt32 count, IOUSBIsocFrame *frames, IOAsyncCallback1 callback, void *context)
{
	(void)self; (void)buffer; (void)frame; (void)frames;
	assert(count == 32);
	if (fail_iso_after >= 0 && (int)iso_submits++ >= fail_iso_after) return kIOReturnNoBandwidth;
	assert(isoch_count < 32);
	isoch[isoch_count++] = (struct fake_completion){ pipe, callback, context };
	return kIOReturnSuccess;
}
static IOReturn fake_abort(void *self, UInt8 pipe)
{
	(void)self;
	if (pipe == 1) {
		atomic_fetch_add(&abort_reads, 1);
		if (read_callback && !hold_abort) {
			IOAsyncCallback1 cb = read_callback; void *context = read_context;
			read_callback = NULL;
			CFRunLoopPerformBlock(CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, ^{
				cb(context, kIOReturnAborted, NULL);
			});
		}
	}
	for (unsigned i = 0; i < isoch_count;) {
		if (isoch[i].pipe != pipe) { i++; continue; }
		struct fake_completion completion = isoch[i];
		isoch[i] = isoch[--isoch_count];
		CFRunLoopPerformBlock(CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, ^{
			completion.callback(completion.context, kIOReturnAborted, NULL);
		});
	}
	return kIOReturnSuccess;
}
static IOReturn fake_bus_frame(void *self, UInt64 *frame, AbsoluteTime *time)
{
	(void)self; (void)time; *frame = 1000; return kIOReturnSuccess;
}
static IOReturn fake_source(void *self, CFRunLoopSourceRef *source)
{
	(void)self; CFRunLoopSourceContext context = {0};
	*source = CFRunLoopSourceCreate(NULL, 0, &context); return kIOReturnSuccess;
}
int rk3_usb_open(struct rk3_usb *u)
{
	memset(u, 0, sizeof(*u));
	u->intf = &fake_interface_pointer;
	u->pipe_cmd_in = 1; u->pipe_cmd_out = 2; u->pipe_iso_in = 3; u->pipe_iso_out = 4;
	return 0;
}
void rk3_usb_close(struct rk3_usb *u) { assert(!read_callback && isoch_count == 0); u->intf = NULL; atomic_fetch_add(&closed, 1); }
int caiaq_compute_bpp(int rate, int alignment, int streams) { assert(rate == 48000 && alignment == 2 && streams == 1); return 88; }
void rk3_control_initialize(void) {}
uint64_t rk3_control_begin_epoch(void) { atomic_store(&published, 0); atomic_store(&state_value, RK3_CONTROL_READY); return atomic_fetch_add(&epochs, 1) + 1; }
void rk3_control_set_state(enum rk3_control_state state, int32_t error) { atomic_store(&state_value, state); atomic_store(&last_error, error); }
void rk3_control_publish_input(const uint8_t *bytes, size_t length, uint64_t time) { assert(bytes && length && time); atomic_fetch_add(&published, 1); }
bool rk3_control_take_command(struct rk3_control_command *out)
{
	pthread_mutex_lock(&command_mutex);
	bool available = command_head != command_tail;
	if (available) *out = queued_commands[command_head++ % 16];
	pthread_mutex_unlock(&command_mutex); return available;
}
void rk3_control_complete_command(const struct rk3_control_command *command, int32_t status, uint32_t actual)
{
	unsigned i = atomic_load(&completions); assert(i < 16);
	completion_ids[i] = command->id; completion_status[i] = status; completion_actual[i] = actual;
	atomic_fetch_add_explicit(&completions, 1, memory_order_release);
}
static void queue_led(uint64_t id)
{
	pthread_mutex_lock(&command_mutex);
	struct rk3_control_command *command = &queued_commands[command_tail++ % 16];
	memset(command, 0, sizeof(*command));
	command->id = id; command->epoch = atomic_load(&epochs); command->deadline_mach = after_ms(200);
	command->timeout_ms = 200; command->length = 64; command->bytes[0] = 5;
	command->bytes[5] = (uint8_t)id;
	pthread_mutex_unlock(&command_mutex);
}
static void reset_fake(void)
{
	assert(!read_callback && isoch_count == 0);
	packet_head = packet_tail = command_head = command_tail = 0;
	iso_submits = 0; suppress_audio_ack = fail_read_arm = hold_abort = 0; fail_iso_after = -1;
	atomic_store(&read_arms, 0); atomic_store(&abort_reads, 0); atomic_store(&writes, 0);
	atomic_store(&closed, 0); atomic_store(&published, 0); atomic_store(&completions, 0);
	atomic_store(&short_next_led, 0);
	fake_interface = (IOUSBInterfaceInterface300){0};
	fake_interface.CreateInterfaceAsyncEventSource = fake_source;
	fake_interface.ReadPipeAsync = fake_read; fake_interface.WritePipeAsyncTO = fake_write;
	fake_interface.ReadIsochPipeAsync = fake_iso; fake_interface.WriteIsochPipeAsync = fake_iso;
	fake_interface.AbortPipe = fake_abort; fake_interface.GetBusFrameNumber = fake_bus_frame;
}
int main(void)
{
	struct rk3_engine *e = calloc(1, sizeof(*e)); assert(e);
	reset_fake();
	assert(rk3_engine_open(e, 48000) == 0);
	assert(atomic_load(&published) == 0); /* pre-ready analog never replayed */
	assert(rk3_engine_audio_start(e) == 0);
	assert(atomic_load(&published) == 3); /* analog + digital + ACK interleaving */
	assert(atomic_load(&abort_reads) == 0); /* no abort/replacement during09 */
	assert(atomic_load(&e->audio_running));
	rk3_engine_audio_stop(e);
	assert(!atomic_load(&e->audio_running) && atomic_load(&abort_reads) == 0);
	queue_led(1); queue_led(2); queue_led(3);
	uint64_t deadline = after_ms(200);
	while (atomic_load_explicit(&completions, memory_order_acquire) != 3 && mach_absolute_time() < deadline) usleep(1000);
	assert(atomic_load(&completions) == 3);
	for (unsigned i = 0; i < 3; i++) { assert(completion_ids[i] == i + 1); assert(completion_status[i] == 0 && completion_actual[i] == 64); }
	assert(rk3_engine_close(e) == 0 && e->control == NULL && atomic_load(&closed) == 1);

	reset_fake(); memset(e, 0, sizeof(*e));
	assert(rk3_engine_open(e, 48000) == 0);
	suppress_audio_ack = 1;
	assert(rk3_engine_audio_start(e) != 0); /* timed-out request outlives caller safely */
	assert(rk3_engine_close(e) == 0);
	assert(!atomic_load(&e->audio_running));

	reset_fake(); memset(e, 0, sizeof(*e));
	assert(rk3_engine_open(e, 48000) == 0);
	fail_iso_after = 3;
	assert(rk3_engine_audio_start(e) != 0); /* partial IN prime is aborted/drained */
	assert(rk3_engine_close(e) == 0 && isoch_count == 0);

	reset_fake(); memset(e, 0, sizeof(*e));
	assert(rk3_engine_open(e, 48000) == 0);
	atomic_store(&short_next_led, 1);
	queue_led(77);
	deadline = after_ms(200);
	while (atomic_load_explicit(&completions, memory_order_acquire) != 1 && mach_absolute_time() < deadline) usleep(1000);
	assert(completion_ids[0] == 77 && completion_status[0] == 0 && completion_actual[0] == 63);
	assert(rk3_engine_usb_dead(e)); /* short transfer fails session even with status0 */
	assert(rk3_engine_close(e) == 0);

	reset_fake(); memset(e, 0, sizeof(*e)); fail_read_arm = 1;
	assert(rk3_engine_open(e, 48000) != 0 && e->control == NULL);

	reset_fake(); memset(e, 0, sizeof(*e));
	assert(rk3_engine_open(e, 48000) == 0);
	hold_abort = 1;
	assert(rk3_engine_close(e) != 0 && e->control != NULL && atomic_load(&closed) == 0);
	/* Simulate delayed aborted callback arriving after bounded close returned. */
	IOAsyncCallback1 cb = read_callback; void *context = read_context; read_callback = NULL;
	CFRunLoopPerformBlock(e->runloop, kCFRunLoopDefaultMode, ^{ cb(context, kIOReturnAborted, NULL); });
	CFRunLoopWakeUp(e->runloop);
	assert(rk3_engine_close(e) == 0 && e->control == NULL && atomic_load(&closed) == 1);
	free(e);
	puts("shared engine lifecycle: all fake-USB tests passed");
	return 0;
}
