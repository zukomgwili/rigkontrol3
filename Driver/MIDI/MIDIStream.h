#ifndef RK3_MIDI_STREAM_H
#define RK3_MIDI_STREAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RK3_MIDI_SYSEX_CHUNK = 256 };
typedef enum {
    RK3_MIDI_STREAM_OK = 0,
    RK3_MIDI_STREAM_INVALID_ARGUMENT,
    RK3_MIDI_STREAM_EMIT_FAILED
} RK3MIDIStreamResult;

/* A packet is either one complete message with explicit status, one realtime
 * byte, or one SysEx-only fragment. Callback storage expires when it returns.
 * Host timestamps have caller-defined units; the native driver uses Mach ticks.
 */
typedef bool (*RK3MIDIStreamEmit)(void *context, uint64_t timestamp,
                                 const uint8_t *bytes, size_t length);
typedef struct {
    uint32_t signature;
    uint8_t running_status;
    uint8_t message[3];
    uint8_t message_length;
    uint8_t message_expected;
    uint64_t message_time;
    bool in_sysex;
    uint16_t sysex_length;
    uint64_t sysex_time;
    uint8_t sysex[RK3_MIDI_SYSEX_CHUNK];
    uint8_t cable;
} RK3MIDIStream;

/* Reset is required after transport generation changes/history loss. It drops
 * partial messages, running status, partial SysEx, and the legacy cable value.
 * State belongs to one source and one I/O thread; there are no allocations.
 */
void rk3_midi_stream_reset(RK3MIDIStream *stream);
RK3MIDIStreamResult rk3_midi_stream_feed(RK3MIDIStream *stream,
    const uint8_t *bytes, size_t length, uint64_t timestamp,
    RK3MIDIStreamEmit emit, void *context);
#endif
