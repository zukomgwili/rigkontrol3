#include "InputGain.h"
#include <string.h>
_Static_assert(ATOMIC_LLONG_LOCK_FREE==2,"Input gain requires lock-free stereo snapshots");
#define UNITY UINT32_C(0x3f800000)
void rk3_input_gain_init(RK3InputGain *gain) {
    for(unsigned i=0;i<32;++i)atomic_init(&gain->pairs[i],((uint64_t)UNITY<<32)|UNITY);
}
bool rk3_input_gain_set(RK3InputGain *gain,uint32_t inputs,uint32_t outputs,
    uint32_t channel,uint32_t bits) {
    uint32_t pairs=(inputs>outputs?inputs:outputs)/2;
    if(!gain||channel>=64||channel>=pairs*2)return false;
    unsigned shift=(channel&1)*32;uint64_t mask=UINT64_C(0xffffffff)<<shift;
    uint64_t value=atomic_load_explicit(&gain->pairs[channel/2],memory_order_relaxed);
    atomic_store_explicit(&gain->pairs[channel/2],(value&~mask)|((uint64_t)bits<<shift),memory_order_release);
    return true;
}
bool rk3_input_gain_get(const RK3InputGain *gain,uint32_t inputs,uint32_t channel,uint32_t *bits) {
    if(!gain||!bits||channel>=64||channel>=inputs)return false;
    *bits=(uint32_t)(atomic_load_explicit(&gain->pairs[channel/2],memory_order_acquire)>>((channel&1)*32));return true;
}
uint64_t rk3_input_gain_snapshot(const RK3InputGain *gain,unsigned pair) {
    return gain&&pair<32?atomic_load_explicit(&gain->pairs[pair],memory_order_acquire):((uint64_t)UNITY<<32)|UNITY;
}
void rk3_input_gain_apply(uint64_t pair,float *samples,size_t frames) {
    if(!samples||pair==(((uint64_t)UNITY<<32)|UNITY))return;
    uint32_t left=(uint32_t)pair,right=(uint32_t)(pair>>32);float l,r;
    memcpy(&l,&left,sizeof(l));memcpy(&r,&right,sizeof(r));
    for(size_t i=0;i<frames;++i){samples[2*i]=l*samples[2*i];samples[2*i+1]=r*samples[2*i+1];}
}
