#include "AudioSchedule.h"

#include <string.h>

#define NI_AUDIO_SCHEDULE_SIGNATURE UINT32_C(0x4e494153)

static bool valid_config(unsigned mode, unsigned groups) {
    return (mode == 0 || mode == 2 || mode == 3) && groups > 0 && groups <= 32;
}

static bool valid_schedule(const NIAudioSchedule *state) {
    return state != NULL && state->signature == NI_AUDIO_SCHEDULE_SIGNATURE &&
           state->groups > 0 && state->groups <= 32 &&
           (state->wire_width == 3 || state->wire_width == 4) &&
           state->ring_bytes == (size_t)state->wire_width * state->groups * 32768 &&
           state->ring_cursor < state->ring_bytes &&
           state->next_output_slot < NI_AUDIO_OUTPUT_SLOTS &&
           state->current_sample_frame < NI_AUDIO_RING_FRAMES;
}

NIAudioScheduleResult ni_audio_packet_capacity(uint32_t sample_rate, unsigned mode,
                                               unsigned groups, uint16_t *capacity) {
    if (capacity == NULL || !valid_config(mode,groups) ||
        (sample_rate != 44100 && sample_rate != 48000 && sample_rate != 88200 &&
         sample_rate != 96000 && sample_rate != 192000)) {
        return NI_AUDIO_SCHEDULE_INVALID;
    }
    unsigned width = mode == 0 ? 3 : 4;
    uint32_t bytes = (sample_rate / 8000 + 11) * width * groups * 2;
    *capacity = (uint16_t)(bytes < 512 ? bytes : 512);
    return NI_AUDIO_SCHEDULE_OK;
}

NIAudioScheduleResult ni_audio_input_buffer_offset(unsigned input_slot,
                                                    unsigned microframe, size_t *offset) {
    if (offset == NULL || input_slot >= NI_AUDIO_INPUT_SLOTS || microframe >= NI_AUDIO_MICROFRAMES) {
        return NI_AUDIO_SCHEDULE_INVALID;
    }
    *offset = ((size_t)input_slot * 8 + microframe) * 512;
    return NI_AUDIO_SCHEDULE_OK;
}

NIAudioScheduleResult ni_audio_schedule_init(NIAudioSchedule *state,
                                               uint64_t current_usb_frame,
                                               unsigned mode, unsigned groups) {
    if (state == NULL || !valid_config(mode,groups)) {
        return NI_AUDIO_SCHEDULE_INVALID;
    }
    if (current_usb_frame > UINT64_MAX - 100) {
        return NI_AUDIO_SCHEDULE_OVERFLOW;
    }
    memset(state,0,sizeof(*state));
    state->signature = NI_AUDIO_SCHEDULE_SIGNATURE;
    state->last_input_frame = current_usb_frame + 100;
    state->wire_width = mode == 0 ? 3 : 4;
    state->groups = groups;
    state->ring_bytes = (size_t)state->wire_width * groups * 32768;
    state->priming = true;
    return NI_AUDIO_SCHEDULE_OK;
}

NIAudioScheduleResult ni_audio_schedule_begin_input(NIAudioSchedule *state,
                                                       unsigned input_slot, uint64_t *frame) {
    if (!valid_schedule(state) || frame == NULL || input_slot >= NI_AUDIO_INPUT_SLOTS ||
        state->input_started[input_slot]) {
        return NI_AUDIO_SCHEDULE_INVALID;
    }
    if (state->restart_requested) return NI_AUDIO_SCHEDULE_RESTART;
    if (state->last_input_frame == UINT64_MAX) return NI_AUDIO_SCHEDULE_OVERFLOW;
    ++state->last_input_frame;
    state->input_started[input_slot] = true;
    state->input_frame[input_slot] = state->last_input_frame;
    *frame = state->last_input_frame;
    return NI_AUDIO_SCHEDULE_OK;
}

NIAudioScheduleResult ni_audio_schedule_input_completed(NIAudioSchedule *state,
                                                         unsigned input_slot,
                                                         const NIAudioPacketResult packets[8],
                                                         NIAudioOutputPlan *plan) {
    if (!valid_schedule(state) || input_slot >= NI_AUDIO_INPUT_SLOTS || packets == NULL ||
        plan == NULL || !state->input_started[input_slot]) {
        return NI_AUDIO_SCHEDULE_INVALID;
    }
    if (state->restart_requested) return NI_AUDIO_SCHEDULE_RESTART;
    if (state->last_input_frame == UINT64_MAX) return NI_AUDIO_SCHEDULE_OVERFLOW;
    NIAudioOutputPlan next = {0};
    for (unsigned index=0;index<8;++index) {
        if (packets[index].actual_bytes > NI_AUDIO_INPUT_PACKET_BYTES) {
            return NI_AUDIO_SCHEDULE_INVALID;
        }
        if (packets[index].status == 0 || (uint32_t)packets[index].status == UINT32_C(0xe00002e7)) {
            next.requested[index] = packets[index].actual_bytes;
            next.total_bytes += packets[index].actual_bytes;
        }
    }
    unsigned slot = state->next_output_slot;
    unsigned scanned = 0;
    while (scanned < NI_AUDIO_OUTPUT_SLOTS && state->output_busy[slot]) {
        slot = (slot + 1) % NI_AUDIO_OUTPUT_SLOTS;
        ++scanned;
    }
    if (scanned == NI_AUDIO_OUTPUT_SLOTS) {
        state->restart_requested = true;
        return NI_AUDIO_SCHEDULE_NO_OUTPUT;
    }
    next.received_frame = state->input_frame[input_slot];
    next.requeued_input_frame = state->last_input_frame + 1;
    next.output_frame = next.requeued_input_frame;
    next.output_slot = slot;
    next.sample_frame = (uint32_t)(state->ring_cursor / (state->wire_width * state->groups * 2));
    next.segment_offset[0] = state->ring_cursor;
    next.segment_bytes[0] = next.total_bytes;
    if (next.segment_bytes[0] > state->ring_bytes - state->ring_cursor) {
        next.segment_bytes[0] = state->ring_bytes - state->ring_cursor;
        next.segment_bytes[1] = next.total_bytes - next.segment_bytes[0];
    }
    state->last_input_frame = next.requeued_input_frame;
    state->input_frame[input_slot] = next.requeued_input_frame;
    state->output_busy[slot] = true;
    state->output_position[slot] = next.sample_frame;
    state->next_output_slot = (slot + 1) % NI_AUDIO_OUTPUT_SLOTS;
    state->ring_cursor = (state->ring_cursor + next.total_bytes) % state->ring_bytes;
    if (state->priming && input_slot == 63) {
        state->priming = false;
        state->next_output_slot = 0;
        next.priming_ended = true;
    }
    *plan = next;
    return NI_AUDIO_SCHEDULE_OK;
}

NIAudioScheduleResult ni_audio_schedule_output_completed(NIAudioSchedule *state,
                                                          unsigned output_slot,
                                                          uint32_t *sample_frame) {
    if (!valid_schedule(state) || sample_frame == NULL || output_slot >= NI_AUDIO_OUTPUT_SLOTS ||
        !state->output_busy[output_slot] || state->output_position[output_slot] >= NI_AUDIO_RING_FRAMES) {
        return NI_AUDIO_SCHEDULE_INVALID;
    }
    state->current_sample_frame = state->output_position[output_slot];
    state->output_busy[output_slot] = false;
    *sample_frame = state->current_sample_frame;
    return NI_AUDIO_SCHEDULE_OK;
}

NIAudioScheduleResult ni_audio_timestamp_nanoseconds(uint64_t anchor_frame,
                                                       uint64_t anchor_nanoseconds,
                                                       uint64_t interval_scaled,
                                                       uint64_t usb_frame,
                                                       uint32_t byte_offset,
                                                       uint32_t total_bytes,
                                                       uint64_t *timestamp) {
    if (timestamp == NULL || interval_scaled < 10000 || total_bytes > 4096 ||
        byte_offset > total_bytes) {
        return NI_AUDIO_SCHEDULE_INVALID;
    }
    uint64_t interval = interval_scaled / 10000;
    bool after = usb_frame >= anchor_frame;
    uint64_t delta = after ? usb_frame - anchor_frame : anchor_frame - usb_frame;
    if (delta > UINT64_MAX / interval) return NI_AUDIO_SCHEDULE_OVERFLOW;
    uint64_t shift = delta * interval;
    uint64_t fraction = 0;
    if (total_bytes != 0) {
        uint64_t remainder_product = (interval % total_bytes) * byte_offset;
        fraction = (interval / total_bytes) * byte_offset + remainder_product / total_bytes;
        if (!after && remainder_product % total_bytes != 0) ++fraction;
    }
    uint64_t result;
    if (after) {
        if (fraction > UINT64_MAX - shift) return NI_AUDIO_SCHEDULE_OVERFLOW;
        shift += fraction;
        if (shift > UINT64_MAX - anchor_nanoseconds) return NI_AUDIO_SCHEDULE_OVERFLOW;
        result = anchor_nanoseconds + shift;
    } else {
        shift -= fraction;
        if (shift > anchor_nanoseconds) return NI_AUDIO_SCHEDULE_OVERFLOW;
        result = anchor_nanoseconds - shift;
    }
    if (interval > UINT64_MAX - result) return NI_AUDIO_SCHEDULE_OVERFLOW;
    *timestamp = result + interval;
    return NI_AUDIO_SCHEDULE_OK;
}
