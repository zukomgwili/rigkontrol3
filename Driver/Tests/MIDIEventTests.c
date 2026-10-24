#include "../MIDIEvent.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_bytes(const uint8_t *a, const uint8_t *b, size_t length)
{
    assert(memcmp(a, b, length) == 0);
}

static void test_event_abi(void)
{
    const uint8_t note[] = {0x90, 60, 100};
    RK3MIDIEvent event = {0}, decoded, unchanged;
    uint8_t wire[RK3_MIDI_EVENT_SIZE], saved[RK3_MIDI_EVENT_SIZE];
    event.port = 1;
    event.byte_count = sizeof(note);
    memcpy(event.bytes, note, sizeof(note));
    memset(event.reserved, 0xff, sizeof(event.reserved));
    memset(event.bytes + sizeof(note), 0xff, sizeof(event.bytes) - sizeof(note));
    assert(rk3_midi_event_encode(&event, 2, wire, sizeof(wire)) == RK3_MIDI_OK);
    assert(wire[0] == 1 && wire[1] == 0 && wire[2] == 0 && wire[3] == 0);
    assert(wire[4] == 3 && wire[5] == 0 && wire[6] == 0 && wire[7] == 0);
    assert_bytes(wire + 8, note, sizeof(note));
    for (size_t i = 11; i < sizeof(wire); ++i)
        assert(wire[i] == 0);
    assert(rk3_midi_event_decode(wire, sizeof(wire), 2, &decoded) == RK3_MIDI_OK);
    assert(decoded.port == 1 && decoded.byte_count == 3);
    assert_bytes(decoded.bytes, note, sizeof(note));
    memset(&unchanged, 0x5a, sizeof(unchanged));
    decoded = unchanged;
    wire[4] = 65;
    assert(rk3_midi_event_decode(wire, sizeof(wire), 2, &decoded) == RK3_MIDI_INVALID_LENGTH);
    assert_bytes((uint8_t *)&decoded, (uint8_t *)&unchanged, sizeof(decoded));
    wire[4] = 3;
    wire[7] = 1; /* A hostile 32-bit length must not be treated as uint8_t. */
    assert(rk3_midi_event_decode(wire, sizeof(wire), 2, &decoded) == RK3_MIDI_INVALID_LENGTH);
    wire[7] = 0;
    assert(rk3_midi_event_decode(wire, sizeof(wire), 1, &decoded) == RK3_MIDI_INVALID_PORT);
    for (size_t length = 0; length < sizeof(wire); ++length)
        assert(rk3_midi_event_decode(wire, length, 2, &decoded) == RK3_MIDI_INVALID_LENGTH);
    assert(rk3_midi_event_decode(wire, sizeof(wire) + 1, 2, &decoded) == RK3_MIDI_INVALID_LENGTH);
    memset(wire, 0xa5, sizeof(wire));
    memcpy(saved, wire, sizeof(saved));
    event.byte_count = 65;
    assert(rk3_midi_event_encode(&event, 2, wire, sizeof(wire)) == RK3_MIDI_INVALID_LENGTH);
    assert_bytes(wire, saved, sizeof(wire));
    event.byte_count = 64;
    assert(rk3_midi_event_encode(&event, 2, wire, sizeof(wire)) == RK3_MIDI_OK);
    event.byte_count = 0;
    assert(rk3_midi_event_encode(&event, 2, wire, sizeof(wire)) == RK3_MIDI_OK);
    assert(rk3_midi_event_decode(wire, sizeof(wire), 2, &decoded) == RK3_MIDI_OK);
    assert(decoded.byte_count == 0);
}

static void test_usb_input(void)
{
    uint8_t report[65] = {6, 0, 3, 0x90, 60, 127};
    RK3MIDIEvent event = {0}, saved;
    assert(rk3_midi_parse_usb_input(report, 6, 1, &event) == RK3_MIDI_OK);
    assert(event.port == 0 && event.byte_count == 3 && event.bytes[0] == 0x90);
    assert(rk3_midi_parse_usb_input(report, 64, 1, &event) == RK3_MIDI_OK);
    saved = event;
    for (size_t length = 0; length < 6; ++length) {
        assert(rk3_midi_parse_usb_input(report, length, 1, &event) == RK3_MIDI_INVALID_LENGTH);
        assert_bytes((uint8_t *)&event, (uint8_t *)&saved, sizeof(event));
    }
    assert(rk3_midi_parse_usb_input(report, 65, 1, &event) == RK3_MIDI_INVALID_LENGTH);
    report[1] = 1;
    assert(rk3_midi_parse_usb_input(report, 6, 1, &event) == RK3_MIDI_INVALID_PORT);
    report[1] = 0;
    report[2] = 62;
    assert(rk3_midi_parse_usb_input(report, 64, 1, &event) == RK3_MIDI_INVALID_LENGTH);
    report[2] = 61;
    for (size_t i = 3; i < 64; ++i)
        report[i] = (uint8_t)(i - 3);
    assert(rk3_midi_parse_usb_input(report, 64, 1, &event) == RK3_MIDI_OK);
    assert(event.byte_count == 61 && event.bytes[60] == 60);
    report[2] = 0;
    assert(rk3_midi_parse_usb_input(report, 3, 1, &event) == RK3_MIDI_OK);
    /* Controller reports are routed to their own handlers, never guessed as MIDI. */
    report[0] = 3;
    assert(rk3_midi_parse_usb_input(report, 17, 1, &event) == RK3_MIDI_NOT_MIDI);
    report[0] = 4;
    assert(rk3_midi_parse_usb_input(report, 9, 1, &event) == RK3_MIDI_NOT_MIDI);
    report[0] = 6;
    report[1] = 64;
    assert(rk3_midi_parse_usb_input(report, 3, 64, &event) == RK3_MIDI_INVALID_PORT);
    report[1] = 0;
    assert(rk3_midi_parse_usb_input(report, 3, 0, &event) == RK3_MIDI_INVALID_PORT);
    assert(rk3_midi_parse_usb_input(report, 3, 65, &event) == RK3_MIDI_INVALID_PORT);
}

typedef struct {
    uint8_t reports[16][11];
    size_t lengths[16];
    size_t calls;
    size_t fail_at;
} Capture;

static RK3MIDIResult capture_report(void *context, const uint8_t *report, size_t length)
{
    Capture *capture = context;
    assert(capture->calls < 16 && length <= 11);
    if (capture->calls == capture->fail_at) {
        ++capture->calls;
        return RK3_MIDI_OUTPUT_FAILED;
    }
    memcpy(capture->reports[capture->calls], report, length);
    capture->lengths[capture->calls] = length;
    ++capture->calls;
    return RK3_MIDI_OK;
}

static void test_usb_output(void)
{
    RK3MIDIEvent event = {0};
    uint8_t report[64], saved[64], stream[100];
    size_t length = 1234, sent = 1234;
    Capture capture = {.fail_at = SIZE_MAX};
    event.port = 0;
    event.byte_count = 61;
    for (size_t i = 0; i < sizeof(event.bytes); ++i)
        event.bytes[i] = (uint8_t)i;
    assert(rk3_midi_encode_usb_output(&event, 1, report, sizeof(report), &length) == RK3_MIDI_OK);
    assert(length == 64 && report[0] == 7 && report[1] == 0 && report[2] == 61);
    assert_bytes(report + 3, event.bytes, 61);
    memcpy(saved, report, sizeof(saved));
    length = 1234;
    event.byte_count = 62;
    assert(rk3_midi_encode_usb_output(&event, 1, report, sizeof(report), &length) == RK3_MIDI_INVALID_LENGTH);
    assert(length == 1234);
    assert_bytes(report, saved, sizeof(report));
    event.byte_count = 3;
    assert(rk3_midi_encode_usb_output(&event, 1, report, 5, &length) == RK3_MIDI_INVALID_LENGTH);
    assert_bytes(report, saved, sizeof(report));
    for (size_t i = 0; i < sizeof(stream); ++i)
        stream[i] = (uint8_t)i;
    assert(rk3_midi_send_usb_output(0, 1, stream, sizeof(stream), capture_report, &capture, &sent) == RK3_MIDI_OK);
    assert(sent == 100 && capture.calls == 13);
    for (size_t i = 0; i < capture.calls; ++i) {
        size_t count = i == 12 ? 4 : 8;
        assert(capture.lengths[i] == count + 3);
        assert(capture.reports[i][0] == 7 && capture.reports[i][1] == 0);
        assert(capture.reports[i][2] == count);
        assert_bytes(capture.reports[i] + 3, stream + i * 8, count);
    }
    memset(&capture, 0, sizeof(capture));
    capture.fail_at = 1;
    assert(rk3_midi_send_usb_output(0, 1, stream, 20, capture_report, &capture, &sent) == RK3_MIDI_OUTPUT_FAILED);
    assert(sent == 8 && capture.calls == 2);
    assert(rk3_midi_send_usb_output(0, 1, NULL, 0, NULL, NULL, &sent) == RK3_MIDI_OK);
    assert(sent == 0);
    assert(rk3_midi_send_usb_output(1, 1, stream, 20, capture_report, &capture, &sent) == RK3_MIDI_INVALID_PORT);
    assert(rk3_midi_send_usb_output(0, 1, NULL, 1, capture_report, &capture, &sent) == RK3_MIDI_INVALID_ARGUMENT);
}

static void test_timestamps(void)
{
    RK3MIDITimestampedEvent event = {0}, decoded, saved;
    uint8_t wire[80], original[80];
    uint64_t result = 99;
    event.event.byte_count = 1;
    event.event.bytes[0] = 0xf8;
    event.seconds = 0x01020304;
    event.nanoseconds = 123456789;
    assert(rk3_midi_timestamped_event_encode(&event, 1, wire, sizeof(wire)) == RK3_MIDI_OK);
    assert(wire[72] == 4 && wire[73] == 3 && wire[74] == 2 && wire[75] == 1);
    assert(wire[76] == 0x15 && wire[77] == 0xcd && wire[78] == 0x5b && wire[79] == 7);
    assert(rk3_midi_timestamped_event_decode(wire, sizeof(wire), 1, &decoded) == RK3_MIDI_OK);
    assert(decoded.seconds == event.seconds && decoded.nanoseconds == event.nanoseconds);
    saved = decoded;
    memcpy(original, wire, sizeof(wire));
    wire[76] = 0; wire[77] = 0xca; wire[78] = 0x9a; wire[79] = 0x3b; /* 1e9 */
    assert(rk3_midi_timestamped_event_decode(wire, sizeof(wire), 1, &decoded) == RK3_MIDI_INVALID_TIMESTAMP);
    assert_bytes((uint8_t *)&decoded, (uint8_t *)&saved, sizeof(decoded));
    memcpy(wire, original, sizeof(wire));
    event.nanoseconds = 1000000000;
    assert(rk3_midi_timestamped_event_encode(&event, 1, wire, sizeof(wire)) == RK3_MIDI_INVALID_TIMESTAMP);
    assert_bytes(wire, original, sizeof(wire));
    assert(rk3_midi_time_to_nanoseconds(2, 123456789, &result) == RK3_MIDI_OK);
    assert(result == UINT64_C(2123456789));
    assert(rk3_midi_time_to_nanoseconds(0, 1000000000, &result) == RK3_MIDI_INVALID_TIMESTAMP);
    assert(rk3_midi_host_ticks_to_nanoseconds(24000000, 125, 3, &result) == RK3_MIDI_OK);
    assert(result == UINT64_C(1000000000));
    assert(rk3_midi_nanoseconds_to_host_ticks(result, 125, 3, &result) == RK3_MIDI_OK);
    assert(result == 24000000);
    assert(rk3_midi_host_ticks_to_nanoseconds(1, 125, 3, &result) == RK3_MIDI_OK && result == 41);
    assert(rk3_midi_host_ticks_to_nanoseconds(UINT64_MAX, 2, 3, &result) == RK3_MIDI_OK);
    assert(result == (UINT64_MAX / 3) * 2);
    result = 99;
    assert(rk3_midi_host_ticks_to_nanoseconds(UINT64_MAX, 2, 1, &result) == RK3_MIDI_OVERFLOW);
    assert(result == 99);
    assert(rk3_midi_host_ticks_to_nanoseconds((UINT64_MAX / 3) * 2 + 1, 3, 2, &result) == RK3_MIDI_OVERFLOW);
    assert(result == 99);
    assert(rk3_midi_host_ticks_to_nanoseconds(1, 0, 1, &result) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_host_ticks_to_nanoseconds(1, 1, 0, &result) == RK3_MIDI_INVALID_ARGUMENT);
}

static void test_null_arguments(void)
{
    RK3MIDIEvent event = {0};
    uint8_t wire[80] = {0};
    size_t length;
    assert(rk3_midi_event_decode(NULL, 72, 1, &event) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_event_decode(wire, 72, 1, NULL) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_event_encode(NULL, 1, wire, 72) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_event_encode(&event, 1, NULL, 72) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_parse_usb_input(NULL, 64, 1, &event) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_encode_usb_output(&event, 1, NULL, 64, &length) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_encode_usb_output(&event, 1, wire, 64, NULL) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_time_to_nanoseconds(0, 0, NULL) == RK3_MIDI_INVALID_ARGUMENT);
    assert(rk3_midi_nanoseconds_to_host_ticks(0, 1, 1, NULL) == RK3_MIDI_INVALID_ARGUMENT);
}

int main(void)
{
    test_event_abi();
    test_usb_input();
    test_usb_output();
    test_timestamps();
    test_null_arguments();
    puts("MIDI event/USB/timestamp tests passed");
    return 0;
}
