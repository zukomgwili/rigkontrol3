#ifndef RK3_CONTROL_H
#define RK3_CONTROL_H

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RK3_CONTROL_INPUT_SELECTOR 'rk3i'
#define RK3_CONTROL_OUTPUT_SELECTOR 'rk3o'
#define RK3_CONTROL_PACKET_CAPACITY 256
#define RK3_CONTROL_COMMAND_CAPACITY 16
#define RK3_CONTROL_COMPLETION_CAPACITY 64

enum rk3_control_state {
    RK3_CONTROL_WAITING,
    RK3_CONTROL_READY,
    RK3_CONTROL_FAILED
};

struct rk3_control_command {
    char client[37];
    uint64_t id;
    uint64_t epoch;
    uint64_t deadline_mach;
    uint32_t timeout_ms;
    uint32_t length;
    uint8_t bytes[64];
};

void rk3_control_initialize(void);
/* Invalidates pending work/old input, advances epoch, then publishes READY. */
uint64_t rk3_control_begin_epoch(void);
void rk3_control_set_state(enum rk3_control_state state, int32_t error);
void rk3_control_publish_input(const uint8_t *bytes, size_t length, uint64_t mach_time);
/* At most one command is in flight. Expired queued commands complete as timeout. */
bool rk3_control_take_command(struct rk3_control_command *out_command);
void rk3_control_complete_command(const struct rk3_control_command *command,
                                  int32_t status, uint32_t actual);
/* Returned dictionary follows Create ownership; caller must release it. */
CFDictionaryRef rk3_control_copy_input(CFDictionaryRef qualifier, OSStatus *error);
OSStatus rk3_control_enqueue_output(CFDictionaryRef request);

#endif
