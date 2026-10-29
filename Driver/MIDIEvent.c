#include "MIDIEvent.h"

#include <string.h>

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static RK3MIDIResult validate_port(uint8_t port, uint8_t port_count)
{
    if (port_count == 0 || port_count > RK3_MIDI_HARDWARE_PORT_LIMIT || port >= port_count)
        return RK3_MIDI_INVALID_PORT;
    return RK3_MIDI_OK;
}

static RK3MIDIResult validate_event(const RK3MIDIEvent *event, uint8_t port_count)
{
    if (event == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    if (event->byte_count > RK3_MIDI_EVENT_BYTES)
        return RK3_MIDI_INVALID_LENGTH;
    return validate_port(event->port, port_count);
}

RK3MIDIResult rk3_midi_event_decode(const uint8_t *wire, size_t length,
                                  uint8_t port_count, RK3MIDIEvent *event)
{
    RK3MIDIEvent decoded = {0};
    RK3MIDIResult result;
    if (wire == NULL || event == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    if (length != RK3_MIDI_EVENT_SIZE)
        return RK3_MIDI_INVALID_LENGTH;
    decoded.port = wire[0];
    decoded.byte_count = read_le32(wire + 4);
    result = validate_event(&decoded, port_count);
    if (result != RK3_MIDI_OK)
        return result;
    memcpy(decoded.bytes, wire + 8, decoded.byte_count);
    *event = decoded;
    return RK3_MIDI_OK;
}

RK3MIDIResult rk3_midi_event_encode(const RK3MIDIEvent *event, uint8_t port_count,
                                  uint8_t *wire, size_t length)
{
    uint8_t encoded[RK3_MIDI_EVENT_SIZE] = {0};
    RK3MIDIResult result;
    if (wire == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    if (length != RK3_MIDI_EVENT_SIZE)
        return RK3_MIDI_INVALID_LENGTH;
    result = validate_event(event, port_count);
    if (result != RK3_MIDI_OK)
        return result;
    encoded[0] = event->port;
    write_le32(encoded + 4, event->byte_count);
    memcpy(encoded + 8, event->bytes, event->byte_count);
    memcpy(wire, encoded, sizeof(encoded));
    return RK3_MIDI_OK;
}

RK3MIDIResult rk3_midi_timestamped_event_decode(const uint8_t *wire, size_t length,
                                  uint8_t port_count, RK3MIDITimestampedEvent *event)
{
    RK3MIDITimestampedEvent decoded = {0};
    RK3MIDIResult result;
    if (wire == NULL || event == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    if (length != RK3_MIDI_TIMESTAMPED_EVENT_SIZE)
        return RK3_MIDI_INVALID_LENGTH;
    result = rk3_midi_event_decode(wire, RK3_MIDI_EVENT_SIZE, port_count, &decoded.event);
    if (result != RK3_MIDI_OK)
        return result;
    decoded.seconds = read_le32(wire + 72);
    decoded.nanoseconds = read_le32(wire + 76);
    if (decoded.nanoseconds >= UINT32_C(1000000000))
        return RK3_MIDI_INVALID_TIMESTAMP;
    *event = decoded;
    return RK3_MIDI_OK;
}

RK3MIDIResult rk3_midi_timestamped_event_encode(const RK3MIDITimestampedEvent *event,
                                  uint8_t port_count, uint8_t *wire, size_t length)
{
    uint8_t encoded[RK3_MIDI_TIMESTAMPED_EVENT_SIZE] = {0};
    RK3MIDIResult result;
    if (event == NULL || wire == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    if (length != RK3_MIDI_TIMESTAMPED_EVENT_SIZE)
        return RK3_MIDI_INVALID_LENGTH;
    if (event->nanoseconds >= UINT32_C(1000000000))
        return RK3_MIDI_INVALID_TIMESTAMP;
    result = rk3_midi_event_encode(&event->event, port_count, encoded, RK3_MIDI_EVENT_SIZE);
    if (result != RK3_MIDI_OK)
        return result;
    write_le32(encoded + 72, event->seconds);
    write_le32(encoded + 76, event->nanoseconds);
    memcpy(wire, encoded, sizeof(encoded));
    return RK3_MIDI_OK;
}

RK3MIDIResult rk3_midi_parse_usb_input(const uint8_t *report, size_t length,
                                     uint8_t port_count, RK3MIDIEvent *event)
{
    RK3MIDIEvent decoded = {0};
    RK3MIDIResult result;
    if (report == NULL || event == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    if (length == 0 || length > RK3_MIDI_USB_REPORT_BYTES)
        return RK3_MIDI_INVALID_LENGTH;
    if (report[0] != RK3_MIDI_USB_INPUT_COMMAND)
        return RK3_MIDI_NOT_MIDI;
    if (length < 3 || report[2] > RK3_MIDI_USB_PAYLOAD_BYTES || report[2] > length - 3)
        return RK3_MIDI_INVALID_LENGTH;
    decoded.port = report[1];
    decoded.byte_count = report[2];
    result = validate_port(decoded.port, port_count);
    if (result != RK3_MIDI_OK)
        return result;
    memcpy(decoded.bytes, report + 3, decoded.byte_count);
    *event = decoded;
    return RK3_MIDI_OK;
}

RK3MIDIResult rk3_midi_encode_usb_output(const RK3MIDIEvent *event, uint8_t port_count,
                                       uint8_t *report, size_t capacity, size_t *length)
{
    uint8_t encoded[RK3_MIDI_USB_REPORT_BYTES] = {0};
    size_t needed;
    RK3MIDIResult result;
    if (report == NULL || length == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    result = validate_event(event, port_count);
    if (result != RK3_MIDI_OK)
        return result;
    if (event->byte_count > RK3_MIDI_USB_PAYLOAD_BYTES)
        return RK3_MIDI_INVALID_LENGTH;
    needed = 3 + event->byte_count;
    if (capacity < needed)
        return RK3_MIDI_INVALID_LENGTH;
    encoded[0] = RK3_MIDI_USB_OUTPUT_COMMAND;
    encoded[1] = event->port;
    encoded[2] = (uint8_t)event->byte_count;
    memcpy(encoded + 3, event->bytes, event->byte_count);
    memcpy(report, encoded, needed);
    *length = needed;
    return RK3_MIDI_OK;
}

RK3MIDIResult rk3_midi_send_usb_output(uint8_t port, uint8_t port_count,
                                     const uint8_t *bytes, size_t byte_count,
                                     RK3MIDIReportWriter writer, void *context,
                                     size_t *bytes_sent)
{
    size_t sent = 0;
    RK3MIDIResult result;
    if (bytes_sent == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    *bytes_sent = 0;
    result = validate_port(port, port_count);
    if (result != RK3_MIDI_OK)
        return result;
    if (byte_count != 0 && (bytes == NULL || writer == NULL))
        return RK3_MIDI_INVALID_ARGUMENT;
    while (sent < byte_count) {
        uint8_t report[3 + RK3_MIDI_OUTPUT_CHUNK_BYTES];
        size_t count = byte_count - sent;
        if (count > RK3_MIDI_OUTPUT_CHUNK_BYTES)
            count = RK3_MIDI_OUTPUT_CHUNK_BYTES;
        report[0] = RK3_MIDI_USB_OUTPUT_COMMAND;
        report[1] = port;
        report[2] = (uint8_t)count;
        memcpy(report + 3, bytes + sent, count);
        result = writer(context, report, count + 3);
        if (result != RK3_MIDI_OK)
            return result;
        sent += count;
        *bytes_sent = sent;
    }
    return RK3_MIDI_OK;
}

RK3MIDIResult rk3_midi_time_to_nanoseconds(uint32_t seconds, uint32_t nanoseconds,
                                        uint64_t *result)
{
    if (result == NULL)
        return RK3_MIDI_INVALID_ARGUMENT;
    if (nanoseconds >= UINT32_C(1000000000))
        return RK3_MIDI_INVALID_TIMESTAMP;
    *result = (uint64_t)seconds * UINT64_C(1000000000) + nanoseconds;
    return RK3_MIDI_OK;
}

static RK3MIDIResult scale_time(uint64_t value, uint32_t numerator,
                               uint32_t denominator, uint64_t *result)
{
    uint64_t whole, fraction;
    if (result == NULL || numerator == 0 || denominator == 0)
        return RK3_MIDI_INVALID_ARGUMENT;
    whole = value / denominator;
    /* Remainder and numerator each fit 32 bits, so their product fits 64. */
    fraction = ((value % denominator) * numerator) / denominator;
    if (whole > UINT64_MAX / numerator)
        return RK3_MIDI_OVERFLOW;
    whole *= numerator;
    if (fraction > UINT64_MAX - whole)
        return RK3_MIDI_OVERFLOW;
    *result = whole + fraction;
    return RK3_MIDI_OK;
}

RK3MIDIResult rk3_midi_host_ticks_to_nanoseconds(uint64_t ticks, uint32_t numerator,
                                              uint32_t denominator, uint64_t *result)
{
    return scale_time(ticks, numerator, denominator, result);
}

RK3MIDIResult rk3_midi_nanoseconds_to_host_ticks(uint64_t nanoseconds, uint32_t numerator,
                                              uint32_t denominator, uint64_t *result)
{
    return scale_time(nanoseconds, denominator, numerator, result);
}
