#ifndef NI_AUDIO_SCHEDULE_H
#define NI_AUDIO_SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NI_AUDIO_INPUT_SLOTS = 64,
    NI_AUDIO_OUTPUT_SLOTS = 128,
    NI_AUDIO_MICROFRAMES = 8,
    NI_AUDIO_INPUT_PACKET_BYTES = 512,
    NI_AUDIO_INPUT_IRP_BYTES = 4096,
    NI_AUDIO_RING_FRAMES = 16384
};

typedef enum {
    NI_AUDIO_SCHEDULE_OK = 0,
    NI_AUDIO_SCHEDULE_INVALID = -1,
    NI_AUDIO_SCHEDULE_OVERFLOW = -2,
    NI_AUDIO_SCHEDULE_NO_OUTPUT = -3,
    NI_AUDIO_SCHEDULE_RESTART = -4
} NIAudioScheduleResult;

typedef struct { int32_t status; uint16_t actual_bytes; } NIAudioPacketResult;
typedef struct {
    uint16_t requested[NI_AUDIO_MICROFRAMES];
    uint32_t total_bytes;
    uint64_t received_frame;
    uint64_t requeued_input_frame;
    uint64_t output_frame;
    unsigned output_slot;
    uint32_t sample_frame;
    size_t segment_offset[2];
    size_t segment_bytes[2];
    bool priming_ended;
} NIAudioOutputPlan;

typedef struct {
    uint32_t signature;
    uint64_t last_input_frame;
    uint64_t input_frame[NI_AUDIO_INPUT_SLOTS];
    bool input_started[NI_AUDIO_INPUT_SLOTS];
    bool output_busy[NI_AUDIO_OUTPUT_SLOTS];
    uint32_t output_position[NI_AUDIO_OUTPUT_SLOTS];
    unsigned next_output_slot;
    unsigned groups;
    unsigned wire_width;
    size_t ring_bytes;
    size_t ring_cursor;
    uint32_t current_sample_frame;
    bool priming;
    bool restart_requested;
} NIAudioSchedule;

/* NI format-change command capacity; actual outgoing counts come from input.
 * Modes 0,2,3 are accepted; groups is max(input,output channels)/2, 1..32. */
NIAudioScheduleResult ni_audio_packet_capacity(uint32_t sample_rate, unsigned mode,
                                               unsigned groups, uint16_t *capacity);
NIAudioScheduleResult ni_audio_input_buffer_offset(unsigned input_slot,
                                                    unsigned microframe, size_t *offset);

/* Initialize the global queue counter to current USB frame +100. Call begin
 * once per input slot before submission; it preincrements and saves that frame.
 * Repeated begin on the same slot is rejected: use input_completed to requeue.
 * Plans reserve an output slot and advance state before the caller submits I/O.
 * Failed transport submissions must stop/reset the stream; do not keep queuing.
 * All state is confined to the USB session's serial queue. */
NIAudioScheduleResult ni_audio_schedule_init(NIAudioSchedule *state,
                                               uint64_t current_usb_frame,
                                               unsigned mode, unsigned groups);
NIAudioScheduleResult ni_audio_schedule_begin_input(NIAudioSchedule *state,
                                                       unsigned input_slot, uint64_t *frame);
NIAudioScheduleResult ni_audio_schedule_input_completed(NIAudioSchedule *state,
                                                         unsigned input_slot,
                                                         const NIAudioPacketResult packets[8],
                                                         NIAudioOutputPlan *plan);
NIAudioScheduleResult ni_audio_schedule_output_completed(NIAudioSchedule *state,
                                                          unsigned output_slot,
                                                          uint32_t *sample_frame);

/* Recovered generateTimeStamp arithmetic, expressed in nanoseconds. The NI
 * interval value is nanoseconds per USB frame times 10000 (initial 10^10).
 * Byte offset includes prior microframes in this 8-microframe IRP. An extra
 * one-frame interval is applied exactly as in NI. Overflow/underflow fails
 * without changing the result; no Apple absolute-time units are assumed. */
NIAudioScheduleResult ni_audio_timestamp_nanoseconds(uint64_t anchor_frame,
                                                       uint64_t anchor_nanoseconds,
                                                       uint64_t interval_scaled,
                                                       uint64_t usb_frame,
                                                       uint32_t byte_offset,
                                                       uint32_t total_bytes,
                                                       uint64_t *timestamp);

#endif
