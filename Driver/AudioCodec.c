#include "AudioCodec.h"

#include <math.h>
#include <string.h>

#define NI_AUDIO_ENCODER_SIGNATURE UINT32_C(0x4e494145)
#define NI_AUDIO_DECODER_SIGNATURE UINT32_C(0x4e494144)

static bool valid_order(NIAudioByteOrder order) {
    return order == NI_AUDIO_BIG_ENDIAN || order == NI_AUDIO_LITTLE_ENDIAN;
}

static bool valid_configuration(NIAudioMode mode, unsigned groups, size_t ring_bytes) {
    return (mode == NI_AUDIO_MODE_2 || mode == NI_AUDIO_MODE_3) &&
           groups > 0 && groups <= NI_AUDIO_MAX_GROUPS &&
           ring_bytes > 0 && ring_bytes <= NI_AUDIO_PACKED_RING_BYTES && ring_bytes % 6 == 0;
}

static bool valid_encoder(const NIAudioEncoder *state) {
    return state != NULL && state->signature == NI_AUDIO_ENCODER_SIGNATURE &&
           valid_configuration(state->mode, state->groups, state->ring_bytes) &&
           state->frame < state->ring_bytes / 6 &&
           (state->staged_size == 0 || state->staged_size == 8 * state->groups) &&
           state->staged_position <= state->staged_size;
}

static bool valid_decoder(const NIAudioDecoder *state) {
    if (state == NULL || state->signature != NI_AUDIO_DECODER_SIGNATURE ||
        !valid_configuration(state->mode, state->groups, state->ring_bytes) ||
        state->byte_cursor >= state->ring_bytes || state->group >= state->groups ||
        (state->errors & ~(unsigned)(NI_AUDIO_ERROR_SYNC | NI_AUDIO_ERROR_DEVICE)) != 0) {
        return false;
    }
    if (state->mode == NI_AUDIO_MODE_3) {
        return state->byte_cursor % 6 == 0 && state->pending_size < 8;
    }
    return state->pending_size < state->groups && state->group == 0 &&
           (!state->check_plane || state->byte_cursor % 3 == 2);
}

NIAudioStatus ni_audio_pcm24_read(const uint8_t *bytes, NIAudioByteOrder order, float *sample) {
    if (bytes == NULL || sample == NULL || !valid_order(order)) {
        return NI_AUDIO_INVALID_ARGUMENT;
    }
    uint32_t value;
    if (order == NI_AUDIO_BIG_ENDIAN) {
        value = (uint32_t)bytes[0] << 16 | (uint32_t)bytes[1] << 8 | bytes[2];
    } else {
        value = (uint32_t)bytes[2] << 16 | (uint32_t)bytes[1] << 8 | bytes[0];
    }
    int32_t signed_value = (int32_t)value;
    if ((value & UINT32_C(0x800000)) != 0) {
        signed_value -= INT32_C(0x1000000);
    }
    *sample = (float)signed_value / 8388608.0f;
    return NI_AUDIO_OK;
}

NIAudioStatus ni_audio_pcm24_write(float sample, NIAudioByteOrder order, uint8_t *bytes) {
    if (bytes == NULL || !valid_order(order)) {
        return NI_AUDIO_INVALID_ARGUMENT;
    }
    int32_t value;
    if (isnan(sample)) {
        value = 0;
    } else if (sample <= -1.0f) {
        value = -INT32_C(0x800000);
    } else if (sample >= 1.0f) {
        value = INT32_C(0x7fffff);
    } else {
        /* NI's Float32ToSwapInt24_Portable, with portable signed division. */
        double scaled = (double)sample * 2147483648.0 + 128.0;
        if (scaled >= 2147483519.0) {
            value = INT32_C(0x7fffff);
        } else {
            int64_t wide = (int64_t)scaled;
            value = (int32_t)(wide >= 0 ? wide / 256 : -((-wide + 255) / 256));
        }
    }
    uint32_t packed = (uint32_t)value & UINT32_C(0xffffff);
    if (order == NI_AUDIO_BIG_ENDIAN) {
        bytes[0] = (uint8_t)(packed >> 16);
        bytes[1] = (uint8_t)(packed >> 8);
        bytes[2] = (uint8_t)packed;
    } else {
        bytes[0] = (uint8_t)packed;
        bytes[1] = (uint8_t)(packed >> 8);
        bytes[2] = (uint8_t)(packed >> 16);
    }
    return NI_AUDIO_OK;
}

NIAudioStatus ni_audio_encoder_init(NIAudioEncoder *state, NIAudioMode mode,
                                    unsigned groups, size_t ring_bytes) {
    if (state == NULL || !valid_configuration(mode, groups, ring_bytes)) {
        return NI_AUDIO_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof(*state));
    state->signature = NI_AUDIO_ENCODER_SIGNATURE;
    state->mode = mode;
    state->groups = groups;
    state->ring_bytes = ring_bytes;
    return NI_AUDIO_OK;
}

static void stage_frame(NIAudioEncoder *state, const uint8_t *const *rings) {
    const size_t groups = state->groups;
    const size_t frame_byte = state->frame * 6;
    if (state->mode == NI_AUDIO_MODE_3) {
        for (unsigned group = 0; group < state->groups; ++group) {
            const uint8_t *sample = rings[group] + frame_byte;
            uint8_t *wire = state->staged + group * 8;
            memcpy(wire, sample, 3);
            wire[3] = (uint8_t)(2 * group);
            memcpy(wire + 4, sample + 3, 3);
            wire[7] = (uint8_t)(2 * group + 1);
        }
    } else {
        for (unsigned group = 0; group < state->groups; ++group) {
            size_t source = (frame_byte + state->ring_bytes - 2) % state->ring_bytes;
            size_t destination = group;
            for (unsigned byte = 0; byte < 6; ++byte) {
                state->staged[destination] = rings[group][source];
                destination += groups;
                if (source % 3 == 2) {
                    destination += groups;
                }
                source = (source + 1) % state->ring_bytes;
            }
            state->staged[2 * groups + group] = (uint8_t)(2 * group + 1);
            state->staged[6 * groups + group] = (uint8_t)(2 * group);
        }
    }
    state->staged_size = 8 * groups;
    state->staged_position = 0;
}

NIAudioStatus ni_audio_encode(NIAudioEncoder *state, const uint8_t *const *rings,
                              size_t ring_capacity, uint8_t *wire, size_t wire_size) {
    if (!valid_encoder(state)) {
        return NI_AUDIO_INVALID_STATE;
    }
    if (rings == NULL || (wire == NULL && wire_size != 0)) {
        return NI_AUDIO_INVALID_ARGUMENT;
    }
    if (ring_capacity < state->ring_bytes) {
        return NI_AUDIO_RING_TOO_SMALL;
    }
    for (unsigned group = 0; group < state->groups; ++group) {
        if (rings[group] == NULL) {
            return NI_AUDIO_INVALID_ARGUMENT;
        }
    }
    while (wire_size != 0) {
        if (state->staged_size == 0) {
            stage_frame(state, rings);
        }
        size_t count = state->staged_size - state->staged_position;
        if (wire_size < count) {
            count = wire_size;
        }
        memcpy(wire, state->staged + state->staged_position, count);
        wire += count;
        wire_size -= count;
        state->staged_position += count;
        if (state->staged_position == state->staged_size) {
            state->staged_position = 0;
            state->staged_size = 0;
            state->frame = (state->frame + 1) % (state->ring_bytes / 6);
        }
    }
    return NI_AUDIO_OK;
}

NIAudioStatus ni_audio_decoder_init(NIAudioDecoder *state, NIAudioMode mode,
                                    unsigned groups, size_t ring_bytes, bool correction_enabled) {
    if (state == NULL || !valid_configuration(mode, groups, ring_bytes)) {
        return NI_AUDIO_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof(*state));
    state->signature = NI_AUDIO_DECODER_SIGNATURE;
    state->mode = mode;
    state->groups = groups;
    state->ring_bytes = ring_bytes;
    state->byte_cursor = mode == NI_AUDIO_MODE_2 ? 2 : 0;
    state->correction_enabled = correction_enabled;
    state->priming = true;
    state->first_unit = true;
    state->check_plane = mode == NI_AUDIO_MODE_2;
    return NI_AUDIO_OK;
}

NIAudioStatus ni_audio_decoder_set_priming(NIAudioDecoder *state, bool priming) {
    if (!valid_decoder(state)) {
        return NI_AUDIO_INVALID_STATE;
    }
    state->priming = priming;
    return NI_AUDIO_OK;
}

static void advance_cursor(NIAudioDecoder *state, size_t bytes) {
    state->byte_cursor += bytes;
    if (state->byte_cursor == state->ring_bytes) {
        state->byte_cursor = 0;
        ++state->ring_wraps;
    }
}

static bool mute(const NIAudioDecoder *state) {
    return state->correction_enabled && (state->errors & NI_AUDIO_ERROR_SYNC) != 0;
}

static void decode_mode2_plane(NIAudioDecoder *state, uint8_t *const *rings) {
    if (state->check_plane) {
        for (unsigned group = 0; group < state->groups; ++group) {
            uint8_t check = state->pending[group];
            unsigned expected = (2 * group | (unsigned)(state->byte_cursor / 3 & 1)) ^ 1;
            if (!state->priming && !state->first_unit && (check & 0x3f) != expected) {
                state->errors |= NI_AUDIO_ERROR_SYNC;
            }
            if ((check & 0x80) != 0) {
                state->errors |= NI_AUDIO_ERROR_DEVICE;
            }
        }
        state->first_unit = false;
        advance_cursor(state, 1);
        state->check_plane = false;
    } else {
        for (unsigned group = 0; group < state->groups; ++group) {
            rings[group][state->byte_cursor] = mute(state) ? 0 : state->pending[group];
        }
        if (state->byte_cursor % 6 == 5) ++state->completed_frames;
        state->first_unit = false;
        if (state->byte_cursor % 3 == 2) {
            state->check_plane = true;
        } else {
            advance_cursor(state, 1);
        }
    }
}

static void decode_mode3_group(NIAudioDecoder *state, uint8_t *const *rings) {
    if (state->group == 0) {
        advance_cursor(state, 6);
    }
    uint8_t *sample = rings[state->group] + state->byte_cursor;
    if (mute(state)) {
        memset(sample, 0, 6);
    } else {
        memcpy(sample, state->pending, 3);
        memcpy(sample + 3, state->pending + 4, 3);
    }
    unsigned expected = state->group * 2;
    if (state->pending[3] != expected ||
        (!state->priming && !state->first_unit && state->pending[7] != (state->pending[3] | 1))) {
        state->errors |= NI_AUDIO_ERROR_SYNC;
    }
    ++state->group;
    if (state->group == state->groups) {
        state->group = 0;
        state->first_unit = false;
        ++state->completed_frames;
    }
}

NIAudioStatus ni_audio_decode(NIAudioDecoder *state, const uint8_t *wire,
                              size_t wire_size, uint8_t *const *rings, size_t ring_capacity) {
    if (!valid_decoder(state)) {
        return NI_AUDIO_INVALID_STATE;
    }
    if (rings == NULL || (wire == NULL && wire_size != 0)) {
        return NI_AUDIO_INVALID_ARGUMENT;
    }
    if (ring_capacity < state->ring_bytes) {
        return NI_AUDIO_RING_TOO_SMALL;
    }
    for (unsigned group = 0; group < state->groups; ++group) {
        if (rings[group] == NULL) {
            return NI_AUDIO_INVALID_ARGUMENT;
        }
    }
    const size_t unit_size = state->mode == NI_AUDIO_MODE_2 ? state->groups : 8;
    while (wire_size != 0) {
        size_t count = unit_size - state->pending_size;
        if (wire_size < count) {
            count = wire_size;
        }
        memcpy(state->pending + state->pending_size, wire, count);
        state->pending_size += count;
        wire += count;
        wire_size -= count;
        if (state->pending_size == unit_size) {
            if (state->mode == NI_AUDIO_MODE_2) {
                decode_mode2_plane(state, rings);
            } else {
                decode_mode3_group(state, rings);
            }
            state->pending_size = 0;
        }
    }
    return NI_AUDIO_OK;
}
