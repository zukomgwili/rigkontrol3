/* Native SDK probe for this project's shared HAL. Default is property reads.
 * --audio-seconds N explicitly starts full-duplex IO with silent output only.
 * No USB library, system-default change, tone or recording file is involved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RK3_INPUT_PROPERTY ((AudioObjectPropertySelector)0x726b3369) /* rk3i */
#define RK3_DEVICE_UID CFSTR("com.zukomgwili.rigkontrol3.audio.device")

struct probe_stats {
    _Atomic uint64_t callbacks;
    _Atomic uint64_t input_frames;
    _Atomic uint64_t silent_output_frames;
    _Atomic uint32_t peak_bits;
};

static AudioObjectPropertyAddress address(AudioObjectPropertySelector selector,
                                         AudioObjectPropertyScope scope)
{
    return (AudioObjectPropertyAddress){selector, scope,
                                       kAudioObjectPropertyElementMain};
}

static void set_integer(CFMutableDictionaryRef dictionary, CFStringRef key,
                        int64_t value)
{
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt64Type, &value);
    CFDictionarySetValue(dictionary, key, number);
    CFRelease(number);
}

static void set_float(CFMutableDictionaryRef dictionary, CFStringRef key,
                      double value)
{
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberDoubleType, &value);
    CFDictionarySetValue(dictionary, key, number);
    CFRelease(number);
}

static CFMutableDictionaryRef dictionary(void)
{
    return CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                     &kCFTypeDictionaryValueCallBacks);
}

static AudioDeviceID find_device(void)
{
    AudioObjectPropertyAddress property = address(kAudioHardwarePropertyDevices,
                                                  kAudioObjectPropertyScopeGlobal);
    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                                    &property, 0, NULL, &size);
    if (status != noErr || size == 0) return kAudioObjectUnknown;
    AudioDeviceID *devices = malloc(size);
    if (!devices) return kAudioObjectUnknown;
    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &property,
                                        0, NULL, &size, devices);
    AudioDeviceID found = kAudioObjectUnknown;
    for (UInt32 i = 0; status == noErr && i < size / sizeof(*devices); ++i) {
        CFStringRef uid = NULL;
        UInt32 uid_size = sizeof(uid);
        AudioObjectPropertyAddress uid_property = address(kAudioDevicePropertyDeviceUID,
                                                           kAudioObjectPropertyScopeGlobal);
        OSStatus uid_status = AudioObjectGetPropertyData(devices[i], &uid_property,
                                                          0, NULL, &uid_size, &uid);
        if (uid_status == noErr && uid) {
            if (CFEqual(uid, RK3_DEVICE_UID)) found = devices[i];
            CFRelease(uid);
        }
        if (found != kAudioObjectUnknown) break;
    }
    free(devices);
    return found;
}

static OSStatus copy_control_state(AudioDeviceID device, CFPropertyListRef *state)
{
    *state = NULL;
    AudioObjectPropertyAddress property = address(RK3_INPUT_PROPERTY,
                                                  kAudioObjectPropertyScopeGlobal);
    if (!AudioObjectHasProperty(device, &property))
        return kAudioHardwareUnknownPropertyError;
    CFMutableDictionaryRef qualifier = dictionary();
    set_integer(qualifier, CFSTR("version"), 1);
    CFPropertyListRef qualifier_ref = qualifier;
    UInt32 size = sizeof(*state);
    OSStatus status = AudioObjectGetPropertyData(device, &property,
                                                 sizeof(qualifier_ref), &qualifier_ref,
                                                 &size, state);
    CFRelease(qualifier);
    if (status == noErr &&
        (size != sizeof(*state) || !*state ||
         CFGetTypeID(*state) != CFDictionaryGetTypeID())) {
        if (*state) CFRelease(*state);
        *state = NULL;
        return kAudioHardwareIllegalOperationError;
    }
    return status;
}

static OSStatus read_format(AudioDeviceID device, AudioObjectPropertyScope scope,
                            AudioStreamBasicDescription *format)
{
    AudioObjectPropertyAddress streams_property = address(kAudioDevicePropertyStreams,
                                                           scope);
    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(device, &streams_property,
                                                    0, NULL, &size);
    if (status != noErr) return status;
    if (size != sizeof(AudioStreamID)) return kAudioHardwareUnsupportedOperationError;
    AudioStreamID stream = kAudioObjectUnknown;
    status = AudioObjectGetPropertyData(device, &streams_property, 0, NULL, &size,
                                        &stream);
    if (status != noErr) return status;
    AudioObjectPropertyAddress format_property = address(kAudioStreamPropertyVirtualFormat,
                                                          kAudioObjectPropertyScopeGlobal);
    size = sizeof(*format);
    return AudioObjectGetPropertyData(stream, &format_property, 0, NULL, &size,
                                       format);
}

static CFDictionaryRef format_dictionary(const AudioStreamBasicDescription *format)
{
    CFMutableDictionaryRef result = dictionary();
    set_float(result, CFSTR("sampleRate"), format->mSampleRate);
    set_integer(result, CFSTR("formatID"), format->mFormatID);
    set_integer(result, CFSTR("formatFlags"), format->mFormatFlags);
    set_integer(result, CFSTR("channels"), format->mChannelsPerFrame);
    set_integer(result, CFSTR("bitsPerChannel"), format->mBitsPerChannel);
    set_integer(result, CFSTR("bytesPerFrame"), format->mBytesPerFrame);
    return result;
}

static bool supported_format(const AudioStreamBasicDescription *format)
{
    bool noninterleaved = (format->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    UInt32 bytes_per_frame = noninterleaved ? sizeof(float) : 2 * sizeof(float);
    return format->mFormatID == kAudioFormatLinearPCM &&
           (format->mFormatFlags & kAudioFormatFlagIsFloat) != 0 &&
           (format->mFormatFlags & kAudioFormatFlagIsBigEndian) == 0 &&
           format->mBitsPerChannel == 32 && format->mChannelsPerFrame == 2 &&
           format->mSampleRate == 48000.0 && format->mFramesPerPacket == 1 &&
           format->mBytesPerFrame == bytes_per_frame &&
           format->mBytesPerPacket == bytes_per_frame;
}

static bool control_is_ready(CFDictionaryRef state)
{
    if (!state || CFGetTypeID(state) != CFDictionaryGetTypeID()) return false;
    CFTypeRef value = CFDictionaryGetValue(state, CFSTR("state"));
    CFTypeRef boot = CFDictionaryGetValue(state, CFSTR("boot"));
    CFTypeRef epoch = CFDictionaryGetValue(state, CFSTR("epoch"));
    int64_t epoch_value = 0;
    return value && CFGetTypeID(value) == CFStringGetTypeID() &&
           CFEqual(value, CFSTR("ready")) && boot &&
           CFGetTypeID(boot) == CFStringGetTypeID() &&
           CFStringGetLength(boot) > 0 && epoch &&
           CFGetTypeID(epoch) == CFNumberGetTypeID() &&
           CFNumberGetValue(epoch, kCFNumberSInt64Type, &epoch_value) &&
           epoch_value > 0;
}

static bool same_ready_identity(CFDictionaryRef baseline, CFDictionaryRef state)
{
    return control_is_ready(baseline) && control_is_ready(state) &&
           CFEqual(CFDictionaryGetValue(baseline, CFSTR("boot")),
                   CFDictionaryGetValue(state, CFSTR("boot"))) &&
           CFEqual(CFDictionaryGetValue(baseline, CFSTR("epoch")),
                   CFDictionaryGetValue(state, CFSTR("epoch")));
}

static OSStatus io_callback(AudioDeviceID device, const AudioTimeStamp *now,
                             const AudioBufferList *input,
                             const AudioTimeStamp *input_time,
                             AudioBufferList *output,
                             const AudioTimeStamp *output_time, void *context)
{
    (void)device; (void)now; (void)input_time; (void)output_time;
    struct probe_stats *stats = context;
    uint64_t input_frames = 0, output_frames = 0;
    float peak = 0.0f;
    if (input) {
        for (UInt32 b = 0; b < input->mNumberBuffers; ++b) {
            const AudioBuffer *buffer = &input->mBuffers[b];
            if (!buffer->mData || buffer->mNumberChannels == 0) continue;
            size_t samples = buffer->mDataByteSize / sizeof(float);
            uint64_t frames = samples / buffer->mNumberChannels;
            if (frames > input_frames) input_frames = frames;
            const float *values = buffer->mData;
            for (size_t s = 0; s < samples; ++s) {
                float magnitude = fabsf(values[s]);
                if (isfinite(magnitude) && magnitude > peak) peak = magnitude;
            }
        }
    }
    if (output) {
        for (UInt32 b = 0; b < output->mNumberBuffers; ++b) {
            AudioBuffer *buffer = &output->mBuffers[b];
            if (!buffer->mData || buffer->mNumberChannels == 0) continue;
            memset(buffer->mData, 0, buffer->mDataByteSize);
            uint64_t frames = buffer->mDataByteSize /
                              (sizeof(float) * buffer->mNumberChannels);
            if (frames > output_frames) output_frames = frames;
        }
    }
    uint32_t peak_bits;
    memcpy(&peak_bits, &peak, sizeof(peak_bits));
    uint32_t previous = atomic_load_explicit(&stats->peak_bits, memory_order_relaxed);
    while (peak_bits > previous &&
           !atomic_compare_exchange_weak_explicit(&stats->peak_bits, &previous,
                                                   peak_bits, memory_order_relaxed,
                                                   memory_order_relaxed)) { }
    atomic_fetch_add_explicit(&stats->callbacks, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->input_frames, input_frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->silent_output_frames, output_frames, memory_order_relaxed);
    return noErr;
}

static int silent_audio_probe(AudioDeviceID device, unsigned seconds,
                               CFMutableDictionaryRef report,
                               CFDictionaryRef baseline)
{
    struct probe_stats *stats = calloc(1, sizeof(*stats));
    if (!stats) return 1;
    atomic_init(&stats->callbacks, 0);
    atomic_init(&stats->input_frames, 0);
    atomic_init(&stats->silent_output_frames, 0);
    atomic_init(&stats->peak_bits, 0);
    if (!atomic_is_lock_free(&stats->callbacks) || !atomic_is_lock_free(&stats->peak_bits)) {
        fprintf(stderr, "Probe counters are not lock-free on this architecture.\n");
        free(stats);
        return 1;
    }
    AudioDeviceIOProcID procedure = NULL;
    OSStatus status = AudioDeviceCreateIOProcID(device, io_callback, stats, &procedure);
    set_integer(report, CFSTR("createIOProcStatus"), status);
    if (status != noErr) { free(stats); return 1; }
    status = AudioDeviceStart(device, procedure);
    set_integer(report, CFSTR("startStatus"), status);
    OSStatus property_status = noErr;
    bool identity_stable = false;
    if (status == noErr) {
        CFPropertyListRef during = NULL;
        property_status = copy_control_state(device, &during);
        set_integer(report, CFSTR("propertyWhileRunningStatus"), property_status);
        identity_stable = property_status == noErr && same_ready_identity(baseline, during);
        CFDictionarySetValue(report, CFSTR("controlIdentityStableWhileRunning"),
                             identity_stable ? kCFBooleanTrue : kCFBooleanFalse);
        if (during) { CFDictionarySetValue(report, CFSTR("controlWhileRunning"), during); CFRelease(during); }
        if (identity_stable) {
            struct timespec remaining = {(time_t)seconds, 0};
            while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) { }
        }
    }
    OSStatus stop_status = AudioDeviceStop(device, procedure);
    OSStatus destroy_status = AudioDeviceDestroyIOProcID(device, procedure);
    set_integer(report, CFSTR("stopStatus"), stop_status);
    set_integer(report, CFSTR("destroyIOProcStatus"), destroy_status);
    uint64_t callbacks = atomic_load_explicit(&stats->callbacks, memory_order_relaxed);
    uint64_t input_frames = atomic_load_explicit(&stats->input_frames, memory_order_relaxed);
    uint64_t output_frames = atomic_load_explicit(&stats->silent_output_frames, memory_order_relaxed);
    uint32_t peak_bits = atomic_load_explicit(&stats->peak_bits, memory_order_relaxed);
    float peak;
    memcpy(&peak, &peak_bits, sizeof(peak));
    set_integer(report, CFSTR("callbacks"), (int64_t)callbacks);
    set_integer(report, CFSTR("inputFrames"), (int64_t)input_frames);
    set_integer(report, CFSTR("silentOutputFrames"), (int64_t)output_frames);
    set_float(report, CFSTR("inputPeak"), peak);
    // Retain callback storage until process exit if CoreAudio failed to detach.
    if (destroy_status == noErr) free(stats);
    return status != noErr || property_status != noErr || !identity_stable ||
           stop_status != noErr || destroy_status != noErr ||
           callbacks == 0 || input_frames == 0 || output_frames == 0;
}

static void usage(FILE *stream, const char *program)
{
    fprintf(stream, "Usage: %s [--audio-seconds N]\n"
                    "Default: read shared-HAL identity, formats and rk3i state.\n"
                    "N must be 1..10; output is silence and input is counted only.\n", program);
}

int main(int argc, char **argv)
{
    unsigned seconds = 0;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(stdout, argv[0]); return 0; }
    if (argc != 1) {
        if (argc != 3 || strcmp(argv[1], "--audio-seconds") != 0) { usage(stderr, argv[0]); return 2; }
        char *end = NULL;
        errno = 0;
        long requested = strtol(argv[2], &end, 10);
        if (errno || !end || *end || requested < 1 || requested > 10) { usage(stderr, argv[0]); return 2; }
        seconds = (unsigned)requested;
    }
    AudioDeviceID device = find_device();
    if (device == kAudioObjectUnknown) { fprintf(stderr, "RK3 audio device UID not found.\n"); return 3; }
    CFMutableDictionaryRef report = dictionary();
    CFDictionarySetValue(report, CFSTR("deviceUID"), RK3_DEVICE_UID);
    set_integer(report, CFSTR("deviceID"), device);
    set_integer(report, CFSTR("requestedAudioSeconds"), seconds);
    AudioStreamBasicDescription input_format = {0}, output_format = {0};
    OSStatus input_status = read_format(device, kAudioObjectPropertyScopeInput, &input_format);
    OSStatus output_status = read_format(device, kAudioObjectPropertyScopeOutput, &output_format);
    set_integer(report, CFSTR("inputFormatStatus"), input_status);
    set_integer(report, CFSTR("outputFormatStatus"), output_status);
    if (input_status == noErr) { CFDictionaryRef f = format_dictionary(&input_format); CFDictionarySetValue(report, CFSTR("inputFormat"), f); CFRelease(f); }
    if (output_status == noErr) { CFDictionaryRef f = format_dictionary(&output_format); CFDictionarySetValue(report, CFSTR("outputFormat"), f); CFRelease(f); }
    CFPropertyListRef state = NULL;
    OSStatus property_status = copy_control_state(device, &state);
    set_integer(report, CFSTR("controlPropertyStatus"), property_status);
    if (state) { CFDictionarySetValue(report, CFSTR("control"), state); CFRelease(state); }
    CFDictionaryRef baseline = CFDictionaryGetValue(report, CFSTR("control"));
    CFDictionarySetValue(report, CFSTR("controlReadyAtBaseline"),
                         control_is_ready(baseline) ? kCFBooleanTrue : kCFBooleanFalse);
    int result = property_status != noErr || input_status != noErr || output_status != noErr ||
                 !supported_format(&input_format) || !supported_format(&output_format);
    if (seconds) {
        if (result || !control_is_ready(baseline) || input_status != noErr || output_status != noErr ||
            !supported_format(&input_format) || !supported_format(&output_format)) {
            fprintf(stderr, "Refusing audio: ready shared control state with a positive epoch and 48 kHz Float32 stereo formats are required.\n");
            result = 1;
        } else {
            result = silent_audio_probe(device, seconds, report, baseline);
            state = NULL;
            property_status = copy_control_state(device, &state);
            set_integer(report, CFSTR("propertyAfterStopStatus"), property_status);
            bool identity_stable = property_status == noErr && same_ready_identity(baseline, state);
            CFDictionarySetValue(report, CFSTR("controlIdentityStableAfterStop"),
                                 identity_stable ? kCFBooleanTrue : kCFBooleanFalse);
            if (state) { CFDictionarySetValue(report, CFSTR("controlAfterStop"), state); CFRelease(state); }
            if (property_status != noErr || !identity_stable) result = 1;
        }
    }
    CFErrorRef error = NULL;
    CFDataRef data = CFPropertyListCreateData(NULL, report, kCFPropertyListXMLFormat_v1_0,
                                             0, &error);
    if (data) {
        fwrite(CFDataGetBytePtr(data), 1, (size_t)CFDataGetLength(data), stdout);
        CFRelease(data);
    } else {
        fprintf(stderr, "Failed to serialize probe report.\n");
        result = 1;
    }
    if (error) CFRelease(error);
    CFRelease(report);
    return result;
}
