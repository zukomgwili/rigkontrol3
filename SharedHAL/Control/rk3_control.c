#include "rk3_control.h"
#include <IOKit/IOReturn.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <limits.h>
#include <string.h>

struct packet {
    uint64_t sequence, time;
    uint32_t length;
    uint8_t bytes[512];
};
struct completion {
    char client[37];
    uint64_t id, epoch;
    int32_t status;
    uint32_t actual;
};
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static char boot[37];
static mach_timebase_info_data_t timebase;
static enum rk3_control_state state = RK3_CONTROL_WAITING;
static int32_t state_error;
static uint64_t epoch, newest;
static size_t packet_count;
static struct packet packets[RK3_CONTROL_PACKET_CAPACITY];
static struct rk3_control_command commands[RK3_CONTROL_COMMAND_CAPACITY];
static size_t command_head, command_count;
static struct rk3_control_command active_command;
static bool active;
static struct completion completions[RK3_CONTROL_COMPLETION_CAPACITY];
static size_t completion_head, completion_count;

static void initialize_once(void)
{
    CFUUIDRef uuid = CFUUIDCreate(NULL);
    CFStringRef text = uuid ? CFUUIDCreateString(NULL, uuid) : NULL;
    if (text) {
        CFStringGetCString(text, boot, sizeof(boot), kCFStringEncodingASCII);
        CFRelease(text);
    }
    if (uuid) CFRelease(uuid);
    mach_timebase_info(&timebase);
}

void rk3_control_initialize(void) { pthread_once(&once, initialize_once); }

static bool number(CFDictionaryRef dictionary, CFStringRef key, uint64_t *out)
{
    CFTypeRef value = CFDictionaryGetValue(dictionary, key);
    int64_t signed_value;
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID() ||
        CFNumberIsFloatType(value) ||
        !CFNumberGetValue(value, kCFNumberSInt64Type, &signed_value) || signed_value < 0)
        return false;
    *out = (uint64_t)signed_value;
    return true;
}

static bool uuid_string(CFTypeRef value, char out[37])
{
    if (!value || CFGetTypeID(value) != CFStringGetTypeID() ||
        CFStringGetLength(value) != 36 ||
        !CFStringGetCString(value, out, 37, kCFStringEncodingASCII)) return false;
    for (unsigned i = 0; i < 36; ++i) {
        char c = out[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (c >= 'a' && c <= 'f') out[i] = (char)(c - 'a' + 'A');
        else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static bool version_one(CFDictionaryRef dictionary)
{
    uint64_t version;
    return dictionary && CFGetTypeID(dictionary) == CFDictionaryGetTypeID() &&
        number(dictionary, CFSTR("version"), &version) && version == 1;
}

static void append_completion(const struct rk3_control_command *command,
                              int32_t status, uint32_t actual)
{
    size_t slot = (completion_head + completion_count) % RK3_CONTROL_COMPLETION_CAPACITY;
    if (completion_count == RK3_CONTROL_COMPLETION_CAPACITY) {
        slot = completion_head;
        completion_head = (completion_head + 1) % RK3_CONTROL_COMPLETION_CAPACITY;
    } else ++completion_count;
    struct completion *result = &completions[slot];
    memcpy(result->client, command->client, sizeof(result->client));
    result->id = command->id;
    result->epoch = command->epoch;
    result->status = status;
    result->actual = actual;
}

static void invalidate_commands(int32_t error)
{
    if (active) append_completion(&active_command, error, 0);
    active = false;
    while (command_count) {
        append_completion(&commands[command_head], error, 0);
        command_head = (command_head + 1) % RK3_CONTROL_COMMAND_CAPACITY;
        --command_count;
    }
    command_head = 0;
}

uint64_t rk3_control_begin_epoch(void)
{
    rk3_control_initialize();
    pthread_mutex_lock(&mutex);
    invalidate_commands(kIOReturnNoDevice);
    ++epoch;
    newest = 0;
    packet_count = 0;
    state = RK3_CONTROL_READY;
    state_error = 0;
    uint64_t result = epoch;
    pthread_mutex_unlock(&mutex);
    return result;
}

void rk3_control_set_state(enum rk3_control_state new_state, int32_t error)
{
    rk3_control_initialize();
    pthread_mutex_lock(&mutex);
    state = new_state;
    state_error = error;
    if (new_state != RK3_CONTROL_READY) {
        invalidate_commands(error ? error : kIOReturnNoDevice);
        packet_count = 0;
    }
    pthread_mutex_unlock(&mutex);
}

void rk3_control_publish_input(const uint8_t *bytes, size_t length, uint64_t time)
{
    if (!bytes || !length || length > sizeof(packets[0].bytes) || !time) return;
    rk3_control_initialize();
    pthread_mutex_lock(&mutex);
    if (state == RK3_CONTROL_READY) {
        ++newest;
        struct packet *packet = &packets[(newest - 1) % RK3_CONTROL_PACKET_CAPACITY];
        packet->sequence = newest;
        packet->time = time;
        packet->length = (uint32_t)length;
        memcpy(packet->bytes, bytes, length);
        if (packet_count < RK3_CONTROL_PACKET_CAPACITY) ++packet_count;
    }
    pthread_mutex_unlock(&mutex);
}

bool rk3_control_take_command(struct rk3_control_command *out)
{
    if (!out) return false;
    rk3_control_initialize();
    pthread_mutex_lock(&mutex);
    while (state == RK3_CONTROL_READY && !active && command_count) {
        struct rk3_control_command command = commands[command_head];
        command_head = (command_head + 1) % RK3_CONTROL_COMMAND_CAPACITY;
        --command_count;
        if (mach_absolute_time() >= command.deadline_mach) {
            append_completion(&command, kIOReturnTimeout, 0);
            continue;
        }
        active_command = command;
        active = true;
        *out = command;
        pthread_mutex_unlock(&mutex);
        return true;
    }
    pthread_mutex_unlock(&mutex);
    return false;
}

void rk3_control_complete_command(const struct rk3_control_command *command,
                                  int32_t status, uint32_t actual)
{
    if (!command) return;
    rk3_control_initialize();
    pthread_mutex_lock(&mutex);
    if (active && command->epoch == active_command.epoch &&
        command->id == active_command.id && !strcmp(command->client, active_command.client)) {
        append_completion(command, status, actual);
        active = false;
    }
    pthread_mutex_unlock(&mutex);
}

static void put_number(CFMutableDictionaryRef dictionary, CFStringRef key, int64_t value)
{
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt64Type, &value);
    CFDictionarySetValue(dictionary, key, number);
    CFRelease(number);
}

static void put_string(CFMutableDictionaryRef dictionary, CFStringRef key, const char *value)
{
    CFStringRef string = CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8);
    CFDictionarySetValue(dictionary, key, string);
    CFRelease(string);
}

static CFMutableDictionaryRef dictionary_create(void)
{
    return CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

CFDictionaryRef rk3_control_copy_input(CFDictionaryRef qualifier, OSStatus *error)
{
    if (error) *error = kAudioHardwareIllegalOperationError;
    if (!version_one(qualifier)) return NULL;
    bool has_boot = CFDictionaryContainsKey(qualifier, CFSTR("boot"));
    bool has_epoch = CFDictionaryContainsKey(qualifier, CFSTR("epoch"));
    if (has_boot != has_epoch) return NULL;
    char wanted_boot[37] = {0};
    uint64_t wanted_epoch = 0, after = 0;
    if (has_boot && (!uuid_string(CFDictionaryGetValue(qualifier, CFSTR("boot")), wanted_boot) ||
        !number(qualifier, CFSTR("epoch"), &wanted_epoch))) return NULL;
    if (CFDictionaryContainsKey(qualifier, CFSTR("after")) &&
        !number(qualifier, CFSTR("after"), &after)) return NULL;

    rk3_control_initialize();
    struct packet input[64];
    struct completion results[RK3_CONTROL_COMPLETION_CAPACITY];
    size_t input_count = 0, result_count;
    uint64_t copied_epoch, oldest, copied_newest;
    int32_t copied_error;
    const char *state_name;
    pthread_mutex_lock(&mutex);
    copied_epoch = epoch;
    copied_newest = newest;
    oldest = packet_count ? newest - packet_count + 1 : newest;
    copied_error = state_error;
    state_name = state == RK3_CONTROL_READY ? "ready" :
        state == RK3_CONTROL_FAILED ? "failed" : "waiting";
    if (has_boot && (strcmp(wanted_boot, boot) || wanted_epoch != epoch)) state_name = "stale";
    else if (has_boot && state == RK3_CONTROL_READY) {
        if (after > newest) state_name = "stale";
        else if ((packet_count && after < oldest - 1) || (!packet_count && after < newest))
            state_name = "gap";
        else for (uint64_t sequence = after + 1;
                     sequence <= newest && input_count < 64; ++sequence) {
            input[input_count++] = packets[(sequence - 1) % RK3_CONTROL_PACKET_CAPACITY];
        }
    }
    result_count = completion_count;
    for (size_t i = 0; i < result_count; ++i)
        results[i] = completions[(completion_head + i) % RK3_CONTROL_COMPLETION_CAPACITY];
    pthread_mutex_unlock(&mutex);

    CFMutableDictionaryRef response = dictionary_create();
    put_number(response, CFSTR("version"), 1);
    put_string(response, CFSTR("boot"), boot);
    put_number(response, CFSTR("epoch"), (int64_t)copied_epoch);
    put_string(response, CFSTR("state"), state_name);
    put_number(response, CFSTR("error"), copied_error);
    put_number(response, CFSTR("oldest"), (int64_t)oldest);
    put_number(response, CFSTR("newest"), (int64_t)copied_newest);
    CFMutableArrayRef input_array = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    for (size_t i = 0; i < input_count; ++i) {
        CFMutableDictionaryRef record = dictionary_create();
        put_number(record, CFSTR("sequence"), (int64_t)input[i].sequence);
        put_number(record, CFSTR("time"), (int64_t)input[i].time);
        CFDataRef data = CFDataCreate(NULL, input[i].bytes, input[i].length);
        CFDictionarySetValue(record, CFSTR("bytes"), data);
        CFRelease(data);
        CFArrayAppendValue(input_array, record);
        CFRelease(record);
    }
    CFDictionarySetValue(response, CFSTR("packets"), input_array);
    CFRelease(input_array);
    CFMutableArrayRef result_array = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    for (size_t i = 0; i < result_count; ++i) {
        CFMutableDictionaryRef record = dictionary_create();
        put_string(record, CFSTR("client"), results[i].client);
        put_number(record, CFSTR("id"), (int64_t)results[i].id);
        put_number(record, CFSTR("epoch"), (int64_t)results[i].epoch);
        put_number(record, CFSTR("status"), results[i].status);
        put_number(record, CFSTR("actual"), results[i].actual);
        CFArrayAppendValue(result_array, record);
        CFRelease(record);
    }
    CFDictionarySetValue(response, CFSTR("completions"), result_array);
    CFRelease(result_array);
    if (error) *error = 0;
    return response;
}

static bool same_command(const struct rk3_control_command *command,
                         const char *client, uint64_t id)
{
    return command->id == id && !strcmp(command->client, client);
}

OSStatus rk3_control_enqueue_output(CFDictionaryRef request)
{
    if (!version_one(request)) return kAudioHardwareIllegalOperationError;
    struct rk3_control_command command = {0};
    char wanted_boot[37];
    uint64_t timeout;
    CFTypeRef data = CFDictionaryGetValue(request, CFSTR("bytes"));
    if (!uuid_string(CFDictionaryGetValue(request, CFSTR("boot")), wanted_boot) ||
        !uuid_string(CFDictionaryGetValue(request, CFSTR("client")), command.client) ||
        !number(request, CFSTR("epoch"), &command.epoch) ||
        !number(request, CFSTR("id"), &command.id) || !command.id ||
        !number(request, CFSTR("timeoutMS"), &timeout) || timeout < 1 || timeout > 200 ||
        !data || CFGetTypeID(data) != CFDataGetTypeID()) return kAudioHardwareIllegalOperationError;
    CFIndex length = CFDataGetLength(data);
    if (length < 1 || length > 64) return kAudioHardwareIllegalOperationError;
    const uint8_t *bytes = CFDataGetBytePtr(data);
    static const uint8_t auto_message[] = {0x0b, 0x01, 0x0a, 0x00};
    if (!((length == 64 && bytes[0] == 0x05) || (length == 1 && bytes[0] == 0x04) ||
        (length == 4 && !memcmp(bytes, auto_message, 4)))) return kAudioHardwareIllegalOperationError;
    command.length = (uint32_t)length;
    command.timeout_ms = (uint32_t)timeout;
    memcpy(command.bytes, bytes, (size_t)length);
    rk3_control_initialize();
    uint64_t ticks = timeout * 1000000ULL * timebase.denom / timebase.numer;
    command.deadline_mach = mach_absolute_time() + ticks;

    pthread_mutex_lock(&mutex);
    OSStatus result = kAudioHardwareIllegalOperationError;
    if (strcmp(wanted_boot, boot) || command.epoch != epoch) goto done;
    result = kAudioHardwareNotRunningError;
    if (state != RK3_CONTROL_READY) goto done;
    result = kIOReturnNoResources;
    if (command_count == RK3_CONTROL_COMMAND_CAPACITY) goto done;
    result = kAudioHardwareIllegalOperationError;
    if (active && same_command(&active_command, command.client, command.id)) goto done;
    for (size_t i = 0; i < command_count; ++i)
        if (same_command(&commands[(command_head + i) % RK3_CONTROL_COMMAND_CAPACITY],
                         command.client, command.id)) goto done;
    for (size_t i = 0; i < completion_count; ++i) {
        struct completion *previous = &completions[(completion_head + i) % RK3_CONTROL_COMPLETION_CAPACITY];
        if (previous->id == command.id && !strcmp(previous->client, command.client)) goto done;
    }
    commands[(command_head + command_count) % RK3_CONTROL_COMMAND_CAPACITY] = command;
    ++command_count;
    result = 0;
done:
    pthread_mutex_unlock(&mutex);
    return result;
}
