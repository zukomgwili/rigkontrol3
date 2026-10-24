/* Exercises the actual HAL property hooks with a hardware-free native engine. */
#include "../RigKontrol3SharedHAL.c"
#include <assert.h>
#include <stdio.h>

static int fake_dead, fake_close_result, opens, closes, audio_starts, audio_stops;
static _Atomic bool block_read, read_entered, release_read;

int rk3_engine_open(struct rk3_engine *e, int rate)
{ ++opens; e->rate = rate; fake_dead = 0; return 0; }
int rk3_engine_close(struct rk3_engine *e)
{ (void)e; ++closes; return fake_close_result; }
int rk3_engine_audio_start(struct rk3_engine *e) { (void)e; ++audio_starts; return 0; }
void rk3_engine_audio_stop(struct rk3_engine *e) { (void)e; ++audio_stops; }
int rk3_engine_set_rate(struct rk3_engine *e, int rate) { e->rate = rate; return 0; }
int rk3_engine_usb_dead(struct rk3_engine *e) { (void)e; return fake_dead; }
int rk3_engine_out_fill_frames(struct rk3_engine *e) { (void)e; return -1; }
int rk3_engine_out_fill_setpoint(const struct rk3_engine *e) { (void)e; return 176; }
void rk3_engine_write_output(struct rk3_engine *e, const float *s, int n)
{ (void)e; (void)s; (void)n; }
void rk3_engine_read_input(struct rk3_engine *e, float *s, int n)
{
    (void)e;
    if (atomic_load(&block_read)) {
        atomic_store(&read_entered, true);
        while (!atomic_load(&release_read)) usleep(100);
    }
    for (int i = 0; i < n * 2; ++i) s[i] = 0.25f;
}

static AudioObjectPropertyAddress address(AudioObjectPropertySelector selector)
{
    return (AudioObjectPropertyAddress){selector, kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMain};
}
static CFMutableDictionaryRef dict(void)
{
    return CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}
static void num(CFMutableDictionaryRef d, CFStringRef key, int64_t value)
{
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt64Type, &value);
    CFDictionarySetValue(d, key, n);
    CFRelease(n);
}
static int64_t value(CFDictionaryRef d, CFStringRef key)
{
    int64_t result;
    assert(CFNumberGetValue(CFDictionaryGetValue(d, key), kCFNumberSInt64Type, &result));
    return result;
}
static bool string_is(CFDictionaryRef d, CFStringRef key, CFStringRef expected)
{ return CFEqual(CFDictionaryGetValue(d, key), expected); }
static CFMutableDictionaryRef qualifier(CFDictionaryRef baseline, uint64_t after)
{
    CFMutableDictionaryRef q = dict();
    num(q, CFSTR("version"), 1);
    if (baseline) {
        CFDictionarySetValue(q, CFSTR("boot"), CFDictionaryGetValue(baseline, CFSTR("boot")));
        CFDictionarySetValue(q, CFSTR("epoch"), CFDictionaryGetValue(baseline, CFSTR("epoch")));
        num(q, CFSTR("after"), (int64_t)after);
    }
    return q;
}
static CFDictionaryRef get(CFDictionaryRef q)
{
    AudioObjectPropertyAddress a = address(RK3_CONTROL_INPUT_SELECTOR);
    CFPropertyListRef output = NULL;
    UInt32 used = 0;
    assert(Drv_GetPropertyData(gDriverRef, kObjID_Device, getpid(), &a,
        sizeof(q), &q, sizeof(output), &used, &output) == 0);
    assert(used == sizeof(output) && CFGetTypeID(output) == CFDictionaryGetTypeID());
    assert(CFPropertyListIsValid(output, kCFPropertyListBinaryFormat_v1_0));
    return output;
}
static CFDictionaryRef baseline(void)
{
    CFMutableDictionaryRef q = qualifier(NULL, 0);
    CFDictionaryRef response = get(q);
    assert(CFArrayGetCount(CFDictionaryGetValue(response, CFSTR("packets"))) == 0);
    CFRelease(q);
    return response;
}
static CFMutableDictionaryRef request(CFDictionaryRef b, uint64_t id,
                                      const uint8_t *bytes, size_t size, int timeout)
{
    CFMutableDictionaryRef r = qualifier(b, 0);
    CFDictionaryRemoveValue(r, CFSTR("after"));
    CFDictionarySetValue(r, CFSTR("client"), CFSTR("D1920525-31DD-43F9-81BC-4A2E695DC763"));
    num(r, CFSTR("id"), (int64_t)id);
    num(r, CFSTR("timeoutMS"), timeout);
    CFDataRef data = CFDataCreate(NULL, bytes, (CFIndex)size);
    CFDictionarySetValue(r, CFSTR("bytes"), data);
    CFRelease(data);
    return r;
}
static OSStatus put(CFDictionaryRef request)
{
    AudioObjectPropertyAddress a = address(RK3_CONTROL_OUTPUT_SELECTOR);
    return Drv_SetPropertyData(gDriverRef, kObjID_Device, getpid(), &a,
                               0, NULL, sizeof(request), &request);
}
static CFDictionaryRef find_completion(CFDictionaryRef response, uint64_t id)
{
    CFArrayRef results = CFDictionaryGetValue(response, CFSTR("completions"));
    for (CFIndex i = CFArrayGetCount(results); i > 0; --i) {
        CFDictionaryRef r = CFArrayGetValueAtIndex(results, i - 1);
        if ((uint64_t)value(r, CFSTR("id")) == id) return r;
    }
    return NULL;
}

static void property_contract(void)
{
    AudioObjectPropertyAddress a = address(kAudioObjectPropertyCustomPropertyInfoList);
    UInt32 size = 0, used = 0;
    assert(Drv_HasProperty(gDriverRef, kObjID_Device, 0, &a));
    assert(!Drv_HasProperty(gDriverRef, kObjID_Stream_Input, 0, &a));
    assert(Drv_GetPropertyDataSize(gDriverRef, kObjID_Device, 0, &a, 0, NULL, &size) == 0);
    assert(size == 2 * sizeof(AudioServerPlugInCustomPropertyInfo));
    AudioServerPlugInCustomPropertyInfo info[2];
    assert(Drv_GetPropertyData(gDriverRef, kObjID_Device, 0, &a, 0, NULL,
                               sizeof(info), &used, info) == 0);
    assert(info[0].mSelector == RK3_CONTROL_INPUT_SELECTOR);
    assert(info[0].mPropertyDataType == kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
    assert(info[0].mQualifierDataType == kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
    assert(info[1].mSelector == RK3_CONTROL_OUTPUT_SELECTOR);
    assert(info[1].mQualifierDataType == kAudioServerPlugInCustomPropertyDataTypeNone);
    a = address(RK3_CONTROL_INPUT_SELECTOR);
    assert(Drv_GetPropertyDataSize(gDriverRef, kObjID_Device, 0, &a, 0, NULL, &size) == 0);
    assert(size == sizeof(CFPropertyListRef));
    Boolean settable;
    assert(Drv_IsPropertySettable(gDriverRef, kObjID_Device, 0, &a, &settable) == 0 && !settable);
    a = address(RK3_CONTROL_OUTPUT_SELECTOR);
    assert(Drv_IsPropertySettable(gDriverRef, kObjID_Device, 0, &a, &settable) == 0 && settable);
    a.mScope = kAudioObjectPropertyScopeInput;
    assert(!Drv_HasProperty(gDriverRef, kObjID_Device, 0, &a));

    a = address(kAudioDevicePropertyAvailableNominalSampleRates);
    AudioValueRange range;
    assert(Drv_GetPropertyData(gDriverRef, kObjID_Device, 0, &a, 0, NULL,
                               sizeof(range), &used, &range) == 0);
    assert(used == sizeof(range) && range.mMinimum == 48000 && range.mMaximum == 48000);
    a = address(kAudioDevicePropertyDeviceUID);
    CFStringRef uid;
    assert(Drv_GetPropertyData(gDriverRef, kObjID_Device, 0, &a, 0, NULL,
                               sizeof(uid), &used, &uid) == 0);
    assert(CFEqual(uid, CFSTR("com.zukomgwili.rigkontrol3.audio.device")));
    CFRelease(uid);
    puts("PASS custom CFPropertyList metadata, scope, stable audio UID, 48k format");
}

static void ordered_input(void)
{
    rk3_control_begin_epoch();
    for (unsigned i = 1; i <= 100; ++i) {
        uint8_t bytes[] = {0x03, (uint8_t)i};
        rk3_control_publish_input(bytes, sizeof(bytes), 1000 + i);
    }
    CFDictionaryRef b = baseline();
    assert(value(b, CFSTR("newest")) == 100);
    CFMutableDictionaryRef q = qualifier(b, 0);
    CFDictionaryRef response = get(q);
    CFArrayRef packets = CFDictionaryGetValue(response, CFSTR("packets"));
    assert(CFArrayGetCount(packets) == 64);
    for (CFIndex i = 0; i < 64; ++i) {
        CFDictionaryRef p = CFArrayGetValueAtIndex(packets, i);
        assert(value(p, CFSTR("sequence")) == i + 1);
        assert(value(p, CFSTR("time")) == 1001 + i);
        CFDataRef bytes = CFDictionaryGetValue(p, CFSTR("bytes"));
        assert(CFDataGetLength(bytes) == 2 && CFDataGetBytePtr(bytes)[1] == i + 1);
    }
    CFRelease(response);
    num(q, CFSTR("after"), 64);
    response = get(q);
    assert(CFArrayGetCount(CFDictionaryGetValue(response, CFSTR("packets"))) == 36);
    CFRelease(response);
    for (unsigned i = 0; i < 256; ++i) {
        uint8_t bytes[] = {0x04, 0};
        rk3_control_publish_input(bytes, sizeof(bytes), 2000 + i);
    }
    response = get(q);
    assert(string_is(response, CFSTR("state"), CFSTR("gap")));
    assert(CFArrayGetCount(CFDictionaryGetValue(response, CFSTR("packets"))) == 0);
    CFRelease(response);
    rk3_control_begin_epoch();
    response = get(q);
    assert(string_is(response, CFSTR("state"), CFSTR("stale")));
    CFRelease(response);
    CFRelease(q);
    CFRelease(b);
    puts("PASS baseline excludes history, ordered bounded batches, gap and stale epochs");
}

static void commands_and_failures(void)
{
    rk3_control_begin_epoch();
    CFDictionaryRef b = baseline();
    uint8_t led[64] = {0x05};
    CFMutableDictionaryRef r = request(b, 100, led, sizeof(led), 200);
    /* Prove control properties never attempt the audio lifecycle lock. */
    pthread_mutex_lock(&gStateMutex);
    assert(put(r) == 0);
    CFDictionaryRef before = baseline();
    pthread_mutex_unlock(&gStateMutex);
    assert(!find_completion(before, 100));
    CFRelease(before);
    assert(put(r) != 0); /* queued duplicate */
    struct rk3_control_command command, other;
    assert(rk3_control_take_command(&command));
    assert(!rk3_control_take_command(&other));
    assert(command.length == 64 && command.bytes[0] == 5);
    rk3_control_complete_command(&command, 0, 63); /* actual short write */
    CFDictionaryRef after = baseline();
    CFDictionaryRef completion = find_completion(after, 100);
    assert(completion && value(completion, CFSTR("actual")) == 63);
    assert(value(completion, CFSTR("status")) == 0);
    assert(put(r) != 0); /* completed duplicate */
    CFRelease(after);
    CFRelease(r);

    const uint8_t query[] = {4};
    for (unsigned i = 0; i < RK3_CONTROL_COMMAND_CAPACITY; ++i) {
        r = request(b, 200 + i, query, sizeof(query), 200);
        assert(put(r) == 0);
        CFRelease(r);
    }
    r = request(b, 999, query, sizeof(query), 200);
    assert(put(r) == kIOReturnNoResources);
    CFRelease(r);
    assert(rk3_control_take_command(&command));
    rk3_control_set_state(RK3_CONTROL_WAITING, kIOReturnNoDevice);
    rk3_control_complete_command(&command, 0, 1); /* late success cannot overwrite removal */
    after = baseline();
    completion = find_completion(after, 200);
    assert(completion && value(completion, CFSTR("status")) == (int32_t)kIOReturnNoDevice);
    assert(value(completion, CFSTR("actual")) == 0);
    assert(find_completion(after, 215));
    assert(!rk3_control_take_command(&command));
    CFRelease(after);
    rk3_control_begin_epoch();
    r = request(b, 1000, query, sizeof(query), 200);
    assert(put(r) != 0);
    CFRelease(r);
    CFRelease(b);

    b = baseline();
    r = request(b, 1001, query, sizeof(query), 1);
    assert(put(r) == 0);
    usleep(4000);
    assert(!rk3_control_take_command(&command));
    after = baseline();
    completion = find_completion(after, 1001);
    assert(completion && value(completion, CFSTR("status")) == (int32_t)kIOReturnTimeout);
    CFRelease(after);
    CFRelease(r);
    CFRelease(b);
    puts("PASS independent IPC lock, async actual-count ACK, capacity, removal and queued expiry");
}

static void malformed_requests(void)
{
    rk3_control_begin_epoch();
    CFDictionaryRef b = baseline();
    uint8_t bad[] = {0x09, 1, 2, 3, 4, 5};
    CFMutableDictionaryRef r = request(b, 2000, bad, sizeof(bad), 200);
    assert(put(r) != 0);
    uint8_t query[] = {4};
    CFRelease(r);
    r = request(b, 2001, query, sizeof(query), 201);
    assert(put(r) != 0);
    num(r, CFSTR("timeoutMS"), 200);
    double floating_version = 1.0;
    CFNumberRef f = CFNumberCreate(NULL, kCFNumberDoubleType, &floating_version);
    CFDictionarySetValue(r, CFSTR("version"), f);
    assert(put(r) != 0);
    CFRelease(f);
    num(r, CFSTR("version"), 1);
    CFDictionarySetValue(r, CFSTR("id"), kCFBooleanTrue);
    assert(put(r) != 0);
    num(r, CFSTR("id"), -1);
    assert(put(r) != 0);
    num(r, CFSTR("id"), 2001);
    num(r, CFSTR("version"), 2);
    assert(put(r) != 0);
    num(r, CFSTR("version"), 1);
    AudioObjectPropertyAddress a = address(RK3_CONTROL_OUTPUT_SELECTOR);
    assert(Drv_SetPropertyData(gDriverRef, kObjID_Device, 0, &a, 0, NULL,
                               sizeof(r) - 1, &r) == kAudioHardwareBadPropertySizeError);
    CFRelease(r);
    const uint8_t auto_message[] = {0x0b, 1, 10, 0};
    r = request(b, 2002, auto_message, sizeof(auto_message), 200);
    assert(put(r) == 0);
    struct rk3_control_command command;
    assert(rk3_control_take_command(&command));
    assert(command.length == sizeof(auto_message) && !memcmp(command.bytes, auto_message, sizeof(auto_message)));
    rk3_control_complete_command(&command, kIOReturnError, 2);
    CFDictionaryRef result = baseline();
    CFDictionaryRef completion = find_completion(result, 2002);
    assert(completion && value(completion, CFSTR("status")) == (int32_t)kIOReturnError);
    assert(value(completion, CFSTR("actual")) == 2);
    CFRelease(result);
    CFRelease(r);
    CFRelease(b);
    puts("PASS malformed/unsupported commands and numeric types rejected");
}

static _Atomic bool producer_finished;
static void *input_producer(void *unused)
{
    (void)unused;
    for (uint32_t sequence = 1; sequence <= 2000; ++sequence)
        rk3_control_publish_input((const uint8_t *)&sequence, sizeof(sequence), 1000000 + sequence);
    atomic_store(&producer_finished, true);
    return NULL;
}

static void concurrent_input(void)
{
    rk3_control_begin_epoch();
    CFDictionaryRef b = baseline();
    CFMutableDictionaryRef q = qualifier(b, 0);
    pthread_t producer;
    assert(!pthread_create(&producer, NULL, input_producer, NULL));
    uint64_t cursor = 0;
    do {
        num(q, CFSTR("after"), (int64_t)cursor);
        CFDictionaryRef response = get(q);
        if (string_is(response, CFSTR("state"), CFSTR("gap"))) {
            cursor = (uint64_t)value(response, CFSTR("newest"));
        } else {
            CFArrayRef packets = CFDictionaryGetValue(response, CFSTR("packets"));
            for (CFIndex i = 0; i < CFArrayGetCount(packets); ++i) {
                CFDictionaryRef packet = CFArrayGetValueAtIndex(packets, i);
                uint64_t sequence = (uint64_t)value(packet, CFSTR("sequence"));
                assert(sequence == cursor + 1);
                CFDataRef bytes = CFDictionaryGetValue(packet, CFSTR("bytes"));
                uint32_t payload;
                assert(CFDataGetLength(bytes) == sizeof(payload));
                memcpy(&payload, CFDataGetBytePtr(bytes), sizeof(payload));
                assert(payload == sequence);
                assert(value(packet, CFSTR("time")) == (int64_t)(1000000 + sequence));
                cursor = sequence;
            }
        }
        CFRelease(response);
    } while (!atomic_load(&producer_finished) || cursor < 2000);
    pthread_join(producer, NULL);
    CFRelease(q);
    CFRelease(b);
    puts("PASS concurrent producer/property reads preserve packet/time/sequence consistency");
}

static void *read_callback(void *unused)
{
    (void)unused;
    float samples[16];
    assert(Drv_DoIOOperation(gDriverRef, kObjID_Device, kObjID_Stream_Input, 0,
        kAudioServerPlugInIOOperationReadInput, 8, NULL, samples, NULL) == 0);
    return NULL;
}

static void active_audio_recovery(void)
{
    gIOCount = 1;
    gEngineOpen = 1;
    fake_dead = 1;
    atomic_store(&gEngineAccessible, true);
    atomic_store(&block_read, true);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, read_callback, NULL));
    for (unsigned i = 0; !atomic_load(&read_entered) && i < 1000; ++i) usleep(1000);
    assert(atomic_load(&read_entered));
    int prior_closes = closes;
    engine_supervisor_step();
    assert(closes == prior_closes); /* live RT reader prevents teardown */
    assert(!atomic_load(&gEngineAccessible));
    float silent[16];
    for (unsigned i = 0; i < 16; ++i) silent[i] = 1;
    assert(!Drv_DoIOOperation(gDriverRef, kObjID_Device, kObjID_Stream_Input, 0,
        kAudioServerPlugInIOOperationReadInput, 8, NULL, silent, NULL));
    for (unsigned i = 0; i < 16; ++i) assert(silent[i] == 0);
    atomic_store(&release_read, true);
    pthread_join(thread, NULL);
    atomic_store(&block_read, false);
    fake_close_result = 1;
    gEngine.rate = 12345;
    int prior_opens = opens;
    engine_supervisor_step();
    assert(gEngineQuarantined && gEngine.rate == 12345 && opens == prior_opens);
    engine_supervisor_step();
    assert(gEngine.rate == 12345 && opens == prior_opens);
    fake_close_result = 0;
    engine_supervisor_step();
    assert(!gEngineQuarantined && gEngineOpen && gEngine.rate == 48000);
    assert(opens == prior_opens + 1 && gIOCount == 1 && audio_starts == 1);
    assert(atomic_load(&gEngineAccessible));
    assert(!Drv_StopIO(gDriverRef, kObjID_Device, 0));
    assert(gIOCount == 0 && audio_stops == 1 && gEngineOpen);
    assert(!atomic_load(&gEngineAccessible));
    puts("PASS active-client unplug, RT-reader quiescence, quarantine and audio restart");
}

static void deferred_stop_start(void)
{
    assert(!Drv_StartIO(gDriverRef, kObjID_Device, 0));
    atomic_store(&block_read, true);
    atomic_store(&read_entered, false);
    atomic_store(&release_read, false);
    pthread_t reader;
    assert(!pthread_create(&reader, NULL, read_callback, NULL));
    for (unsigned i = 0; !atomic_load(&read_entered) && i < 1000; ++i) usleep(1000);
    assert(atomic_load(&read_entered));
    int prior_stops = audio_stops;
    assert(!Drv_StopIO(gDriverRef, kObjID_Device, 0));
    assert(gAudioStopPending && gIOCount == 0 && audio_stops == prior_stops);
    assert(Drv_StartIO(gDriverRef, kObjID_Device, 0) == kAudioHardwareNotRunningError);
    atomic_store(&release_read, true);
    pthread_join(reader, NULL);
    atomic_store(&block_read, false);
    assert(!Drv_StartIO(gDriverRef, kObjID_Device, 0));
    assert(!gAudioStopPending && audio_stops == prior_stops + 1 && gIOCount == 1);
    engine_supervisor_step();
    assert(gIOCount == 1 && audio_stops == prior_stops + 1);
    assert(!Drv_StopIO(gDriverRef, kObjID_Device, 0));
    puts("PASS deferred StopIO then rapid StartIO cannot stop the replacement audio run");
}

int main(void)
{
    rk3_control_initialize();
    property_contract();
    ordered_input();
    commands_and_failures();
    malformed_requests();
    concurrent_input();
    active_audio_recovery();
    deferred_stop_start();
    assert(opens == 1); /* only explicit fake recovery touched engine open */
    puts("All hardware-free shared HAL control/property tests passed.");
    return 0;
}
