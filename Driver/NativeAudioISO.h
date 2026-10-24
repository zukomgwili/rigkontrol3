#ifndef RK3_NATIVE_AUDIO_ISO_H
#define RK3_NATIVE_AUDIO_ISO_H

#include "AudioTiming.h"
#include <IOKit/usb/IOUSBLib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct rk3_native_audio_iso rk3_native_audio_iso;
typedef bool (*rk3_native_audio_command)(void *context, const uint8_t *bytes,
                                        size_t length, uint32_t timeout_ms);
typedef struct {
    uint64_t sample_frame;
    uint64_t host_time;
    uint64_t seed;
} rk3_native_audio_clock;

/* Create/start/stop/pending/destroy and every IOKit call belong exclusively to
 * the owning NativeUSB CFRunLoop worker. The engine retains a COM reference to
 * the interface. The owner keeps it open and retains its async source through
 * stop and callback drain, then calls release_usb before closing the interface.
 * Command callback writes on that worker and pumps its loop. Real rates require
 * ACK9=1; the bounded rate-255 stop accepts the device's received ACK9=0/1.
 * This first engine supports verified 48kHz/stereo/mode2 only. Its native OUT
 * calendar uses AudioTiming.h; the recovered NI reference scheduler is unchanged.
 * A clock is published only after priming and a valid post-priming capture batch. */
rk3_native_audio_iso *rk3_native_audio_iso_create(IOUSBInterfaceInterface300 **interface,
    uint8_t input_pipe, uint8_t output_pipe, rk3_native_audio_command command, void *context);
int32_t rk3_native_audio_iso_start(rk3_native_audio_iso *engine);
void rk3_native_audio_iso_stop(rk3_native_audio_iso *engine);
size_t rk3_native_audio_iso_pending(const rk3_native_audio_iso *engine);
bool rk3_native_audio_iso_is_quiescent(const rk3_native_audio_iso *engine);
/* Worker: after stop and pending()==0, destroy the LowLatency buffers while
 * the interface is still open. CPU queues/object remain valid for late RT
 * calls, which return zero while stopped. Idempotent; false if I/O remains.
 * The retained COM reference is released only by final destroy. */
bool rk3_native_audio_iso_release_usb(rk3_native_audio_iso *engine);
/* Returns false without releasing anything while callbacks or RT calls remain.
 * NativeUSB must unpublish this engine before eventual destruction. */
bool rk3_native_audio_iso_destroy(rk3_native_audio_iso *engine);
int32_t rk3_native_audio_iso_error(const rk3_native_audio_iso *engine);
bool rk3_native_audio_iso_running(const rk3_native_audio_iso *engine);

/* HAL paths: no mutex, allocation, run-loop work or USB call. Bounded SPSC
 * Float32 stereo queues; competing callers return zero rather than wait.
 * Return actual frames accepted/read; both return zero before PCM readiness.
 * Capture discards an old backlog on the consumer side, retaining at most the
 * requested frames plus one input IRP, so a late client cannot inherit stale
 * startup audio indefinitely. This intentionally drops samples after a late
 * consumer; it does not promise lossless recording through a host stall.
 * NativeUSB retains the engine pointer
 * until all concurrent calls have completed, including across disconnects. */
size_t rk3_native_audio_iso_render(rk3_native_audio_iso *engine,
                                  const float *interleaved, size_t frames);
size_t rk3_native_audio_iso_capture(rk3_native_audio_iso *engine,
                                   float *interleaved, size_t frames);
bool rk3_native_audio_iso_clock(rk3_native_audio_iso *engine,
                                rk3_native_audio_clock *snapshot);

#endif
