#include "AudioSchedule.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void begin_all(NIAudioSchedule *state) {
    for (unsigned slot=0;slot<64;++slot) {
        uint64_t frame=0;
        assert(ni_audio_schedule_begin_input(state,slot,&frame) == NI_AUDIO_SCHEDULE_OK);
        assert(frame == 1101+slot);
    }
}

static void test_capacity_and_input_layout(void) {
    uint16_t capacity=0;
    const uint32_t rates[]={44100,48000,88200,96000,192000};
    const uint16_t expected[]={128,136,176,184,280};
    for (unsigned index=0;index<5;++index) {
        assert(ni_audio_packet_capacity(rates[index],2,1,&capacity) == NI_AUDIO_SCHEDULE_OK);
        assert(capacity == expected[index]);
    }
    assert(ni_audio_packet_capacity(192000,3,32,&capacity) == NI_AUDIO_SCHEDULE_OK && capacity == 512);
    assert(ni_audio_packet_capacity(48000,0,1,&capacity) == NI_AUDIO_SCHEDULE_OK && capacity == 102);
    assert(ni_audio_packet_capacity(12345,2,1,&capacity) == NI_AUDIO_SCHEDULE_INVALID && capacity == 102);
    assert(ni_audio_packet_capacity(48000,1,1,&capacity) == NI_AUDIO_SCHEDULE_INVALID);
    size_t offset=0;
    assert(ni_audio_input_buffer_offset(0,7,&offset) == NI_AUDIO_SCHEDULE_OK && offset == 3584);
    assert(ni_audio_input_buffer_offset(63,7,&offset) == NI_AUDIO_SCHEDULE_OK && offset == 261632);
    assert(ni_audio_input_buffer_offset(64,0,&offset) == NI_AUDIO_SCHEDULE_INVALID);
}

static void test_original_frame_progression(void) {
    NIAudioSchedule state;
    assert(ni_audio_schedule_init(&state,1000,2,1) == NI_AUDIO_SCHEDULE_OK);
    begin_all(&state);
    NIAudioPacketResult packets[8]={{0,48},{0,48},{0,40},{0,48},{0,40},{0,48},{0,40},{0,40}};
    NIAudioOutputPlan plan;
    assert(ni_audio_schedule_input_completed(&state,0,packets,&plan) == NI_AUDIO_SCHEDULE_OK);
    assert(plan.received_frame == 1101 && plan.requeued_input_frame == 1165 && plan.output_frame == 1165);
    assert(plan.total_bytes == 352 && plan.sample_frame == 0 && plan.output_slot == 0);
    assert(plan.segment_offset[0] == 0 && plan.segment_bytes[0] == 352 && plan.segment_bytes[1] == 0);
    assert(state.priming && !plan.ends_priming);
    uint32_t sample=99;
    assert(ni_audio_schedule_output_completed(&state,0,&sample) == NI_AUDIO_SCHEDULE_OK && sample == 0);
    assert(ni_audio_schedule_input_completed(&state,1,packets,&plan) == NI_AUDIO_SCHEDULE_OK);
    assert(plan.received_frame == 1102 && plan.output_frame == 1166 && plan.sample_frame == 44);
    /* Priming ends on slot 63, not an inferred number of successful callbacks. */
    assert(ni_audio_schedule_input_completed(&state,63,packets,&plan) == NI_AUDIO_SCHEDULE_OK);
    assert(plan.ends_priming && state.priming && state.next_output_slot == 3);
    assert(ni_audio_schedule_finish_priming(&state) == NI_AUDIO_SCHEDULE_OK);
    assert(!state.priming && state.next_output_slot == 0);
}

static void test_status_filter_wrap_and_exhaustion(void) {
    NIAudioSchedule state;
    assert(ni_audio_schedule_init(&state,1000,2,1) == NI_AUDIO_SCHEDULE_OK);
    begin_all(&state);
    state.ring_cursor=state.ring_bytes-10;
    NIAudioPacketResult packets[8]={{0,8},{(int32_t)UINT32_C(0xe00002e7),16},{-1,512},{0,0},{0,0},{0,0},{0,0},{0,0}};
    NIAudioOutputPlan plan;
    assert(ni_audio_schedule_input_completed(&state,0,packets,&plan) == NI_AUDIO_SCHEDULE_OK);
    assert(plan.total_bytes == 24 && plan.requested[1] == 16 && plan.requested[2] == 0);
    assert(plan.segment_bytes[0] == 10 && plan.segment_bytes[1] == 14 && plan.segment_offset[1] == 0);
    assert(plan.sample_frame == 16382 && state.ring_cursor == 14);
    packets[0].actual_bytes=513;
    NIAudioSchedule saved=state;
    NIAudioOutputPlan unchanged=plan;
    assert(ni_audio_schedule_input_completed(&state,0,packets,&plan) == NI_AUDIO_SCHEDULE_INVALID);
    assert(memcmp(&state,&saved,sizeof(state)) == 0 && memcmp(&plan,&unchanged,sizeof(plan)) == 0);
    memset(packets,0,sizeof(packets));
    for(unsigned index=1;index<128;++index)
        assert(ni_audio_schedule_input_completed(&state,0,packets,&plan) == NI_AUDIO_SCHEDULE_OK);
    uint64_t frame_before=state.last_input_frame;
    assert(ni_audio_schedule_input_completed(&state,0,packets,&plan) == NI_AUDIO_SCHEDULE_NO_OUTPUT);
    assert(state.restart_requested && state.last_input_frame == frame_before);
    assert(ni_audio_schedule_input_completed(&state,0,packets,&plan) == NI_AUDIO_SCHEDULE_RESTART);
}

static void test_clock_arithmetic(void) {
    uint64_t timestamp=0;
    const uint64_t interval=UINT64_C(10000000000);
    assert(ni_audio_timestamp_nanoseconds(100,1000000000,interval,100,0,0,&timestamp) == NI_AUDIO_SCHEDULE_OK);
    assert(timestamp == 1001000000);
    assert(ni_audio_timestamp_nanoseconds(100,1000000000,interval,102,1,3,&timestamp) == NI_AUDIO_SCHEDULE_OK);
    assert(timestamp == 1003333333);
    assert(ni_audio_timestamp_nanoseconds(100,1000000000,interval,99,1,3,&timestamp) == NI_AUDIO_SCHEDULE_OK);
    assert(timestamp == 1000333334); /* NI's division before subtract causes upward fraction. */
    assert(ni_audio_timestamp_nanoseconds(100,1000000000,interval,99,3,3,&timestamp) == NI_AUDIO_SCHEDULE_OK);
    assert(timestamp == 1001000000);
    uint64_t unchanged=timestamp;
    assert(ni_audio_timestamp_nanoseconds(100,1000000000,interval,101,2,1,&timestamp) == NI_AUDIO_SCHEDULE_INVALID);
    assert(timestamp == unchanged);
    assert(ni_audio_timestamp_nanoseconds(0,UINT64_MAX,interval,0,0,0,&timestamp) == NI_AUDIO_SCHEDULE_OVERFLOW);
    assert(ni_audio_timestamp_nanoseconds(0,0,interval,UINT64_MAX,0,0,&timestamp) == NI_AUDIO_SCHEDULE_OVERFLOW);
    assert(ni_audio_timestamp_nanoseconds(100,0,interval,0,0,0,&timestamp) == NI_AUDIO_SCHEDULE_OVERFLOW);
    NIAudioSchedule state;
    assert(ni_audio_schedule_init(&state,UINT64_MAX,2,1) == NI_AUDIO_SCHEDULE_OVERFLOW);
}

int main(void) {
    test_capacity_and_input_layout();
    test_original_frame_progression();
    test_status_filter_wrap_and_exhaustion();
    test_clock_arithmetic();
    puts("AudioSchedule: NI input-led frames, capacities, priming, wrap and clock checks passed.");
    return 0;
}
