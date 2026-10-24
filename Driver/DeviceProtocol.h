#ifndef RK3_DEVICE_PROTOCOL_H
#define RK3_DEVICE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NI 2.8.0 commandReplyDispatch@0x20e0 and setAudioParams@0x2d50.
 * This is a new bounded wire interface, not the Intel C++ object layout. */
enum {
    RK3_USB_VENDOR = 0x17cc,
    RK3_USB_PRODUCT = 0x1940,
    RK3_COMMAND_INFO = 1,
    RK3_COMMAND_ANALOG = 3,
    RK3_COMMAND_DIGITAL = 4,
    RK3_COMMAND_WRITE_IO = 5,
    RK3_COMMAND_MIDI_INPUT = 6,
    RK3_COMMAND_MIDI_OUTPUT = 7,
    RK3_COMMAND_AUDIO_PARAMS = 9,
    RK3_COMMAND_AUTO_REPORT = 11,
    RK3_COMMAND_MAX_BYTES = 64,
    RK3_INFO_BYTES = 14,
    RK3_AUDIO_PARAMS_BYTES = 6
};

typedef struct {
    uint8_t raw[RK3_INFO_BYTES];
    uint16_t input_channels;
    uint16_t output_channels;
    uint8_t wire_mode;
    uint8_t midi_inputs;
    uint8_t midi_outputs;
} rk3_device_info;

/* Output is untouched on failure. Additional trailing EP1 bytes are allowed. */
bool rk3_device_info_parse(const uint8_t *bytes, size_t length,
                           rk3_device_info *info);

/* Produces the complete six-byte command, including the leading 0x09.
 * Only NI-advertised rates, 16/24-bit widths, and capacities 1..512 accepted. */
bool rk3_audio_parameters(uint32_t rate, uint8_t bits, uint16_t capacity,
                           uint8_t command[RK3_AUDIO_PARAMS_BYTES]);
/* NI's stop sentinel deliberately uses rate and width codes 0xff. */
bool rk3_audio_stop(uint16_t capacity,
                    uint8_t command[RK3_AUDIO_PARAMS_BYTES]);
/* NI performFormatChange@0x68d0. Reject unknown mode / non-stereo topology. */
bool rk3_audio_capacity(uint32_t rate, uint8_t mode,
                        uint16_t inputs, uint16_t outputs, uint16_t *capacity);
/* Command 9 replies contain one result byte; only result 1 acknowledges. */
bool rk3_audio_ack(const uint8_t *bytes, size_t length, uint8_t *result);

#endif
