#ifndef NI_AUDIO_CODEC_H
#define NI_AUDIO_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fresh portable reconstruction. Evidence: ReverseEngineering/AudioDriver.md. */
enum {
    NI_AUDIO_MAX_GROUPS = 32,
    NI_AUDIO_PACKED_RING_BYTES = 0x18000,
    NI_AUDIO_MAX_WIRE_FRAME_BYTES = 8 * NI_AUDIO_MAX_GROUPS
};

typedef enum {
    NI_AUDIO_OK = 0,
    NI_AUDIO_INVALID_ARGUMENT = -1,
    NI_AUDIO_INVALID_STATE = -2,
    NI_AUDIO_RING_TOO_SMALL = -3
} NIAudioStatus;

typedef enum { NI_AUDIO_MODE_2 = 2, NI_AUDIO_MODE_3 = 3 } NIAudioMode;
typedef enum { NI_AUDIO_BIG_ENDIAN = 0, NI_AUDIO_LITTLE_ENDIAN = 1 } NIAudioByteOrder;
enum { NI_AUDIO_ERROR_SYNC = 1, NI_AUDIO_ERROR_DEVICE = 2 };

/* State has no allocations and is confined to one streaming queue/thread. */
typedef struct {
    uint32_t signature;
    NIAudioMode mode;
    unsigned groups;
    size_t ring_bytes;
    size_t frame;
    size_t staged_size;
    size_t staged_position;
    uint8_t staged[NI_AUDIO_MAX_WIRE_FRAME_BYTES];
} NIAudioEncoder;

typedef struct {
    uint32_t signature;
    NIAudioMode mode;
    unsigned groups;
    size_t ring_bytes;
    size_t byte_cursor;
    uint64_t ring_wraps;
    uint64_t completed_frames;
    unsigned group;
    unsigned errors;
    bool correction_enabled;
    bool priming;
    bool first_unit;
    bool check_plane;
    size_t pending_size;
    uint8_t pending[NI_AUDIO_MAX_GROUPS];
} NIAudioDecoder;

/* Byte order is explicit: the original advertised packed PCM was big endian.
 * Read returns exactly signed_pcm24 / 2^23. Write uses the recovered portable
 * scaling and rounding for finite [-1,1], clips out-of-range values, maps NaN
 * to zero, and does not emulate undefined Intel conversions of nonfinite data.
 * Invalid arguments leave the destination unchanged. */
NIAudioStatus ni_audio_pcm24_read(const uint8_t *bytes, NIAudioByteOrder order, float *sample);
NIAudioStatus ni_audio_pcm24_write(float sample, NIAudioByteOrder order, uint8_t *bytes);

/* ring_bytes is a positive multiple of six, at most the original 0x18000.
 * Shorter rings permit bounded host buffers and deterministic wrap tests.
 * Every ring pointer covers ring_capacity bytes; rings and wire buffers must
 * not overlap. Ring bytes are opaque packed stereo PCM: tags never change
 * sample byte order. The encoder starts at frame zero and snapshots a frame
 * before delivering its first byte, keeping partial reads consistent. */
NIAudioStatus ni_audio_encoder_init(NIAudioEncoder *state, NIAudioMode mode,
                                    unsigned groups, size_t ring_bytes);
NIAudioStatus ni_audio_encode(NIAudioEncoder *state, const uint8_t *const *rings,
                              size_t ring_capacity, uint8_t *wire, size_t wire_size);

/* Caller clears the rings before initialization. The recovered receive cursor
 * starts at byte 2 for mode 2, byte 0 for mode 3, and advances before storing
 * audio. Arbitrary byte chunks, including partial planes/groups, are retained.
 * Priming starts enabled: mode-2 sync checks and mode-3 odd tags are suppressed
 * during priming; mode-3 even tags and mode-2 device flags remain checked.
 * Once a sync error latches, correction zeroes subsequent audio writes. The
 * corrupt mode-3 group's own bytes have already been written, as in NI code.
 * Reset by reinitializing; errors do not silently clear at a chunk boundary.
 * Invalid arguments/state leave both state and buffers unchanged. */
NIAudioStatus ni_audio_decoder_init(NIAudioDecoder *state, NIAudioMode mode,
                                    unsigned groups, size_t ring_bytes,
                                    bool correction_enabled);
NIAudioStatus ni_audio_decoder_set_priming(NIAudioDecoder *state, bool priming);
NIAudioStatus ni_audio_decode(NIAudioDecoder *state, const uint8_t *wire,
                              size_t wire_size, uint8_t *const *rings,
                              size_t ring_capacity);

#endif
