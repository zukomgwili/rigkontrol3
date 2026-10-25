#ifndef RK3_INPUT_GAIN_H
#define RK3_INPUT_GAIN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/* Original NI engine state: 64 raw Float32 slots, unity at engine creation.
 * Non-RT writers are serialized by the owner. Readers take one coherent stereo
 * snapshot; no float conversion occurs in the get/set path. */
typedef struct { _Atomic uint64_t pairs[32]; } RK3InputGain;
void rk3_input_gain_init(RK3InputGain *gain);
bool rk3_input_gain_set(RK3InputGain *gain,uint32_t inputs,uint32_t outputs,
    uint32_t channel,uint32_t bits);
bool rk3_input_gain_get(const RK3InputGain *gain,uint32_t inputs,
    uint32_t channel,uint32_t *bits);
uint64_t rk3_input_gain_snapshot(const RK3InputGain *gain,unsigned pair);
/* Converted caller buffer only. Never apply to shared packed wire storage.
 * A unity pair leaves every sample bit untouched. No clamp or finite filter. */
void rk3_input_gain_apply(uint64_t pair,float *samples,size_t frames);
#endif
