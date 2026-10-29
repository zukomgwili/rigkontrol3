#ifndef RK3_MIDI_EVENT_H
#define RK3_MIDI_EVENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RK3_MIDI_EVENT_SIZE = 72,
    RK3_MIDI_TIMESTAMPED_EVENT_SIZE = 80,
    RK3_MIDI_EVENT_BYTES = 64,
    RK3_MIDI_USB_REPORT_BYTES = 64,
    RK3_MIDI_USB_PAYLOAD_BYTES = 61,
    RK3_MIDI_OUTPUT_CHUNK_BYTES = 8,
    RK3_MIDI_HARDWARE_PORT_LIMIT = 64,
    RK3_MIDI_USB_INPUT_COMMAND = 6,
    RK3_MIDI_USB_OUTPUT_COMMAND = 7
};

typedef enum {
    RK3_MIDI_OK = 0,
    RK3_MIDI_INVALID_ARGUMENT,
    RK3_MIDI_INVALID_LENGTH,
    RK3_MIDI_INVALID_PORT,
    RK3_MIDI_NOT_MIDI,
    RK3_MIDI_INVALID_TIMESTAMP,
    RK3_MIDI_OVERFLOW,
    RK3_MIDI_OUTPUT_FAILED
} RK3MIDIResult;

/* Host values. Encode/decode functions serialize every integer little-endian. */
typedef struct {
    uint8_t port;
    uint8_t reserved[3];
    uint32_t byte_count;
    uint8_t bytes[RK3_MIDI_EVENT_BYTES];
} RK3MIDIEvent;

typedef struct {
    RK3MIDIEvent event;
    uint32_t seconds;
    uint32_t nanoseconds;
} RK3MIDITimestampedEvent;

#if defined(__cplusplus)
#define RK3_MIDI_STATIC_ASSERT static_assert
#else
#define RK3_MIDI_STATIC_ASSERT _Static_assert
#endif
RK3_MIDI_STATIC_ASSERT(sizeof(RK3MIDIEvent) == 72, "MIDI user-client event ABI size");
RK3_MIDI_STATIC_ASSERT(offsetof(RK3MIDIEvent, byte_count) == 4, "MIDI length ABI offset");
RK3_MIDI_STATIC_ASSERT(offsetof(RK3MIDIEvent, bytes) == 8, "MIDI payload ABI offset");
RK3_MIDI_STATIC_ASSERT(sizeof(RK3MIDITimestampedEvent) == 80, "Timestamped MIDI ABI size");
RK3_MIDI_STATIC_ASSERT(offsetof(RK3MIDITimestampedEvent, seconds) == 72, "MIDI seconds offset");
RK3_MIDI_STATIC_ASSERT(offsetof(RK3MIDITimestampedEvent, nanoseconds) == 76, "MIDI nanos offset");
#undef RK3_MIDI_STATIC_ASSERT

/* Hardware MIDI only: port_count comes from the verified device descriptor.
 * Analog/digital controller reports and host-monitor ports (64..127) are separate.
 * On error these decode/encode functions leave caller output unchanged.
 */
RK3MIDIResult rk3_midi_event_decode(const uint8_t *wire, size_t length,
                                  uint8_t port_count, RK3MIDIEvent *event);
RK3MIDIResult rk3_midi_event_encode(const RK3MIDIEvent *event, uint8_t port_count,
                                  uint8_t *wire, size_t length);
RK3MIDIResult rk3_midi_timestamped_event_decode(const uint8_t *wire, size_t length,
                                  uint8_t port_count, RK3MIDITimestampedEvent *event);
RK3MIDIResult rk3_midi_timestamped_event_encode(const RK3MIDITimestampedEvent *event,
                                  uint8_t port_count, uint8_t *wire, size_t length);

/* Command 6 is [6, port, count, bytes...]. Trailing USB report padding is allowed;
 * count must fit the actual transfer, not just the endpoint's capacity.
 */
RK3MIDIResult rk3_midi_parse_usb_input(const uint8_t *report, size_t length,
                                     uint8_t port_count, RK3MIDIEvent *event);
/* Command 7 is [7, port, count, bytes...]. No truncation is performed. */
RK3MIDIResult rk3_midi_encode_usb_output(const RK3MIDIEvent *event, uint8_t port_count,
                                       uint8_t *report, size_t capacity, size_t *length);

/* Callback must synchronously consume/copy the report; its storage is temporary.
 * Caller serializes this operation with other endpoint writes. Nonzero callback
 * results stop output immediately. bytes_sent counts only successful chunks.
 */
typedef RK3MIDIResult (*RK3MIDIReportWriter)(void *context, const uint8_t *report,
                                          size_t length);
RK3MIDIResult rk3_midi_send_usb_output(uint8_t port, uint8_t port_count,
                                     const uint8_t *bytes, size_t byte_count,
                                     RK3MIDIReportWriter writer, void *context,
                                     size_t *bytes_sent);

/* Original timestamped ABI uses seconds and nanoseconds, not microseconds.
 * Host tick conversions use mach_timebase_info's numerator/denominator and floor
 * fractional results. Overflow and invalid timebases are reported, never wrapped.
 */
RK3MIDIResult rk3_midi_time_to_nanoseconds(uint32_t seconds, uint32_t nanoseconds,
                                        uint64_t *result);
RK3MIDIResult rk3_midi_host_ticks_to_nanoseconds(uint64_t ticks, uint32_t numerator,
                                              uint32_t denominator, uint64_t *result);
RK3MIDIResult rk3_midi_nanoseconds_to_host_ticks(uint64_t nanoseconds, uint32_t numerator,
                                              uint32_t denominator, uint64_t *result);

#ifdef __cplusplus
}
#endif
#endif
