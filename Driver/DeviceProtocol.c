#include "DeviceProtocol.h"
#include <string.h>

static bool rate_code(uint32_t rate, uint8_t *code) {
    switch (rate) {
        case 44100: *code = 0; return true;
        case 48000: *code = 1; return true;
        case 96000: *code = 2; return true;
        case 192000: *code = 3; return true;
        case 88200: *code = 4; return true;
        default: return false;
    }
}

bool rk3_device_info_parse(const uint8_t *bytes, size_t length,
                           rk3_device_info *info) {
    if (!bytes || !info || length < RK3_INFO_BYTES + 1 ||
        length > RK3_COMMAND_MAX_BYTES || bytes[0] != RK3_COMMAND_INFO)
        return false;
    rk3_device_info value = {0};
    memcpy(value.raw, bytes + 1, RK3_INFO_BYTES);
    value.input_channels = (uint16_t)bytes[9] + bytes[11];
    value.output_channels = (uint16_t)bytes[8] + bytes[10];
    value.wire_mode = bytes[14];
    value.midi_inputs = bytes[13];
    value.midi_outputs = bytes[12];
    *info = value;
    return true;
}

static bool encode(uint8_t rate, uint8_t width, uint16_t capacity,
                    uint8_t command[RK3_AUDIO_PARAMS_BYTES]) {
    if (!command || !capacity || capacity > 512) return false;
    const uint8_t value[RK3_AUDIO_PARAMS_BYTES] = {
        RK3_COMMAND_AUDIO_PARAMS, rate, width,
        (uint8_t)capacity, (uint8_t)(capacity >> 8), 1
    };
    memcpy(command, value, sizeof(value));
    return true;
}

bool rk3_audio_parameters(uint32_t rate, uint8_t bits, uint16_t capacity,
                           uint8_t command[RK3_AUDIO_PARAMS_BYTES]) {
    uint8_t code;
    if (!rate_code(rate, &code) || (bits != 16 && bits != 24)) return false;
    return encode(code, bits == 16 ? 1 : 2, capacity, command);
}

bool rk3_audio_stop(uint16_t capacity,
                    uint8_t command[RK3_AUDIO_PARAMS_BYTES]) {
    return encode(255, 255, capacity, command);
}

bool rk3_audio_capacity(uint32_t rate, uint8_t mode,
                        uint16_t inputs, uint16_t outputs, uint16_t *capacity) {
    uint8_t unused;
    if (!capacity || !rate_code(rate, &unused) ||
        (mode != 0 && mode != 2 && mode != 3) ||
        (inputs & 1) || (outputs & 1)) return false;
    uint32_t groups = (inputs > outputs ? inputs : outputs) / 2;
    if (!groups) return false;
    uint32_t value = (rate / 8000 + 11) * 2 * (mode == 0 ? 3 : 4) * groups;
    *capacity = value > 512 ? 512 : (uint16_t)value;
    return true;
}

bool rk3_audio_ack(const uint8_t *bytes, size_t length, uint8_t *result) {
    if (!bytes || !result || length < 2 || length > RK3_COMMAND_MAX_BYTES ||
        bytes[0] != RK3_COMMAND_AUDIO_PARAMS) return false;
    *result = bytes[1];
    return true;
}
