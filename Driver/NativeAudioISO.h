#ifndef RK3_NATIVE_AUDIO_ISO_H
#define RK3_NATIVE_AUDIO_ISO_H
#include "NativeHostUSB.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct rk3_native_audio_iso rk3_native_audio_iso;
typedef bool (*rk3_native_audio_command)(void *,const uint8_t *,size_t,uint32_t);
typedef struct {
    uint64_t sample_frame,host_time,seed;
    uint64_t zero_sample_frame,zero_host_time; /* exact NI input-ring timestamp */
} rk3_native_audio_clock;
/* One worker owns USB submissions/callback retirement. NativeHostUSB delivers
 * accepted ISO callbacks in per-pipe submission order, restoring the ordering
 * needed by NI's positional ring traversal despite modern raw callback order.
 * The live output ring is
 * kernel-backed mapped DMA storage. HAL writes use NI host sample positions;
 * no per-transfer PCM snapshots or FIFO queue-lead adjustment is performed. */
rk3_native_audio_iso *rk3_native_audio_iso_create(RK3HostInterface *,uint8_t,uint8_t,
    rk3_native_audio_command,void *);
int32_t rk3_native_audio_iso_start(rk3_native_audio_iso *);
void rk3_native_audio_iso_stop(rk3_native_audio_iso *);
void rk3_native_audio_iso_service(rk3_native_audio_iso *);
size_t rk3_native_audio_iso_pending(const rk3_native_audio_iso *);
bool rk3_native_audio_iso_is_quiescent(const rk3_native_audio_iso *);
bool rk3_native_audio_iso_release_usb(rk3_native_audio_iso *);
bool rk3_native_audio_iso_destroy(rk3_native_audio_iso *);
int32_t rk3_native_audio_iso_error(const rk3_native_audio_iso *);
/* First stream diagnostic, including logged OUT failures which do not directly
 * trigger NI panic. Cleared by restart; it does not mean device removal. */
int32_t rk3_native_audio_iso_stream_error(const rk3_native_audio_iso *);
bool rk3_native_audio_iso_running(const rk3_native_audio_iso *);
void rk3_native_audio_iso_set_output_active(rk3_native_audio_iso *,bool);
/* Stable pending panic until restart. Delays are recovered NI trigger delays,
 * not an independent auto-restart policy; NativeUSB asks the HAL host to pause
 * and resume IO. NoDevice is fatal and is not reported as an audio panic. */
bool rk3_native_audio_iso_panic(const rk3_native_audio_iso *,uint32_t *,int32_t *);
/* RT: bounded CPU work only. Input polls primary-updated transaction state
 * before reading the requested ring positions. Capture snapshots use lock-free
 * stereo atomics to separate the ARM host IO thread from the serial USB worker.
 * A busy primary decoder leaves the last coherent ring view available; it does
 * not dequeue/drop samples, reset a FIFO tail or insert short-read padding. */
size_t rk3_native_audio_iso_render_at(rk3_native_audio_iso *,const float *,uint64_t,size_t);
size_t rk3_native_audio_iso_capture_at(rk3_native_audio_iso *,float *,uint64_t,size_t);
bool rk3_native_audio_iso_clock(rk3_native_audio_iso *,rk3_native_audio_clock *);
#endif
