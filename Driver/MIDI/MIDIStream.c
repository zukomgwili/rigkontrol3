#include "MIDIStream.h"
#include <string.h>

#define STREAM_SIGNATURE UINT32_C(0x524b334d)

void rk3_midi_stream_reset(RK3MIDIStream *stream) {
    if (stream != NULL) {
        memset(stream,0,sizeof(*stream));
        stream->signature = STREAM_SIGNATURE;
    }
}

static bool valid(const RK3MIDIStream *stream) {
    return stream != NULL && stream->signature == STREAM_SIGNATURE &&
        ((stream->message_expected == 0 && stream->message_length == 0) ||
         ((stream->message_expected == 2 || stream->message_expected == 3) &&
          stream->message_length >= 1 && stream->message_length < stream->message_expected)) &&
        stream->sysex_length < RK3_MIDI_SYSEX_CHUNK &&
        (stream->running_status == 0 || (stream->running_status >= 0x80 && stream->running_status < 0xf0));
}

static bool emit_packet(RK3MIDIStream *stream,RK3MIDIStreamEmit emit,void *context,
                        uint64_t time,const uint8_t *bytes,size_t length) {
    if (emit(context,time,bytes,length)) return true;
    rk3_midi_stream_reset(stream);
    return false;
}

static bool flush_sysex(RK3MIDIStream *stream,RK3MIDIStreamEmit emit,void *context) {
    if (stream->sysex_length == 0) return true;
    if (!emit_packet(stream,emit,context,stream->sysex_time,stream->sysex,stream->sysex_length)) return false;
    stream->sysex_length = 0;
    return true;
}

static bool append_sysex(RK3MIDIStream *stream,uint8_t byte,uint64_t timestamp,
                         RK3MIDIStreamEmit emit,void *context) {
    if (stream->sysex_length == 0) stream->sysex_time = timestamp;
    stream->sysex[stream->sysex_length++] = byte;
    return stream->sysex_length < RK3_MIDI_SYSEX_CHUNK || flush_sysex(stream,emit,context);
}

static uint8_t channel_size(uint8_t status) { return (status & 0xe0) == 0xc0 ? 2 : 3; }

static void begin_message(RK3MIDIStream *stream,uint8_t status,uint8_t expected,uint64_t timestamp) {
    stream->message[0] = status;
    stream->message_length = 1;
    stream->message_expected = expected;
    stream->message_time = timestamp;
}

static bool finish_message(RK3MIDIStream *stream,RK3MIDIStreamEmit emit,void *context) {
    if (stream->message[0] == 0xf5) {
        stream->cable = stream->message[1]; /* NI compatibility: consume cable marker. */
    } else if (!emit_packet(stream,emit,context,stream->message_time,stream->message,stream->message_length)) {
        return false;
    }
    stream->message_length = stream->message_expected = 0;
    return true;
}

RK3MIDIStreamResult rk3_midi_stream_feed(RK3MIDIStream *stream,
    const uint8_t *bytes,size_t length,uint64_t timestamp,
    RK3MIDIStreamEmit emit,void *context) {
    if (!valid(stream) || (length != 0 && bytes == NULL) || emit == NULL)
        return RK3_MIDI_STREAM_INVALID_ARGUMENT;
    for (size_t index = 0; index < length; ++index) {
        uint8_t byte = bytes[index];
        if (byte >= 0xf8) {
            /* NI's mask passes clock/start/continue/stop/reset; undefined F9/FD
             * and active sensing FE are suppressed without disturbing state. */
            if (byte != 0xf9 && byte != 0xfd && byte != 0xfe) {
                if (stream->in_sysex && !flush_sysex(stream,emit,context)) return RK3_MIDI_STREAM_EMIT_FAILED;
                if (!emit_packet(stream,emit,context,timestamp,&byte,1)) return RK3_MIDI_STREAM_EMIT_FAILED;
            }
            continue;
        }
        if (byte < 0x80) {
            if (stream->in_sysex) {
                if (!append_sysex(stream,byte,timestamp,emit,context)) return RK3_MIDI_STREAM_EMIT_FAILED;
            } else {
                if (stream->message_expected == 0) {
                    if (stream->running_status == 0) continue;
                    begin_message(stream,stream->running_status,channel_size(stream->running_status),timestamp);
                }
                stream->message[stream->message_length++] = byte;
                if (stream->message_length == stream->message_expected &&
                    !finish_message(stream,emit,context)) return RK3_MIDI_STREAM_EMIT_FAILED;
            }
            continue;
        }
        /* A non-realtime status interrupts SysEx. NI closes the old portion
         * with F7 before processing the new status; never mix other messages
         * into a packet containing a SysEx fragment. */
        if (stream->in_sysex) {
            if (!append_sysex(stream,0xf7,timestamp,emit,context) ||
                !flush_sysex(stream,emit,context)) return RK3_MIDI_STREAM_EMIT_FAILED;
            stream->in_sysex = false;
            if (byte == 0xf7) continue;
        }
        stream->message_length = stream->message_expected = 0;
        if (byte < 0xf0) {
            stream->running_status = byte;
            begin_message(stream,byte,channel_size(byte),timestamp);
            continue;
        }
        stream->running_status = 0;
        switch (byte) {
            case 0xf0:
                stream->in_sysex = true;
                if (!append_sysex(stream,byte,timestamp,emit,context)) return RK3_MIDI_STREAM_EMIT_FAILED;
                break;
            case 0xf1: case 0xf3: case 0xf5:
                begin_message(stream,byte,2,timestamp); break;
            case 0xf2:
                begin_message(stream,byte,3,timestamp); break;
            case 0xf6:
                if (!emit_packet(stream,emit,context,timestamp,&byte,1)) return RK3_MIDI_STREAM_EMIT_FAILED;
                break;
            default: /* Undefined F4 and orphan F7: discard safely. */ break;
        }
    }
    return RK3_MIDI_STREAM_OK;
}
