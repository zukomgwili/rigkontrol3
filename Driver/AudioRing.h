#ifndef NI_AUDIO_RING_H
#define NI_AUDIO_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RK3: one stereo group, original NI mode 2. The caller supplies both buffers.
 * The wire buffer is the live storage referenced by output descriptors, not a
 * snapshot/FIFO. Binding it to current USB APIs and establishing DMA visibility
 * are the platform adapter's responsibility; this portable core supplies none
 * of those guarantees. CPU calls on one state must be serialized by the caller.
 * Evidence: original NI 2.8.0 prepareMemory 0x59d0, clipOutputSamples 0x6ab0,
 * fillErrorCorrectionPatternMode2 0x87f0, clearSampleBufferForStream 0x7510. */
enum {
    NI_AUDIO_RING_FRAME_COUNT = 16384,
    NI_AUDIO_RING_PACKED_BYTES = NI_AUDIO_RING_FRAME_COUNT * 6,
    NI_AUDIO_RING_WIRE_BYTES = NI_AUDIO_RING_FRAME_COUNT * 8
};

typedef enum {
    NI_AUDIO_RING_OK = 0,
    NI_AUDIO_RING_PRIMING = 1, /* original clip returns success without PCM work */
    NI_AUDIO_RING_INVALID = -1
} NIAudioRingResult;

typedef struct {
    uint32_t signature;
    uint8_t *wire;
    uint8_t *packed;
    size_t wire_capacity, packed_capacity;
    bool priming;
    bool stream_written;
} NIAudioRing;

/* Buffers, state, and subsequent Float32 input must not overlap. Only the fixed
 * ring extents are touched; additional capacity is left intact. Init also resets
 * the ring. Invalid arguments do not modify the state or backing storage. */
NIAudioRingResult ni_audio_ring_init(NIAudioRing *state,
    uint8_t *wire, size_t wire_capacity, uint8_t *packed, size_t packed_capacity);
/* Original start resets PCM/checks and enters priming but leaves the separate
 * stream-written flag unchanged. Init establishes its initial false value. */
NIAudioRingResult ni_audio_ring_reset(NIAudioRing *state);
NIAudioRingResult ni_audio_ring_set_priming(NIAudioRing *state, bool priming);

/* Clip the externally populated big-endian packed stereo ring at host sample
 * positions. Positions are reduced modulo 16384 without overflowing uint64_t;
 * frames must be <=16384. Payload wire lanes {0,1,3,4,5,7} receive packed bytes
 * (6*frame-2+k) mod98304, k=0..5. Existing check lanes2/6 are preserved.
 * The last right-channel low bytes of a block are carried by the next wire
 * frame's leading lanes when a later clip covers that next frame, as in NI.
 * Calls mark stream_written even during priming. Priming performs no PCM writes.
 * Float32 input is a host block beginning at its first requested frame (not a
 * full host mix ring). Wrapped blocks follow original IOAudioStream ordering:
 * convert+clip the tail, then convert+clip the head. A zero-frame call accepts
 * a NULL Float32 pointer and otherwise does no work. */
NIAudioRingResult ni_audio_ring_clip_packed_at(NIAudioRing *state,
    uint64_t first_sample_frame, size_t frames);
NIAudioRingResult ni_audio_ring_write_f32_at(NIAudioRing *state,
    uint64_t first_sample_frame, const float *interleaved_stereo, size_t frames);

/* Exact original mode-2/G1 clear quirk: zeros wire lanes0..5 in every frame,
 * including check lane2; leaves lanes6/7 and the packed ring unchanged. This is
 * NOT reset or a corrected payload-only clear. Original writeHandler 0x7ec0
 * calls it after a previously written output stream becomes inactive, then
 * clears the separate stream_written flag. This function does not clear that
 * flag or infer host-client activity; its caller must establish those facts. */
NIAudioRingResult ni_audio_ring_clear_stream_original(NIAudioRing *state);

#endif
