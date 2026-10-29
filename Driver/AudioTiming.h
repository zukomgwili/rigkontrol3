#ifndef RK3_AUDIO_TIMING_H
#define RK3_AUDIO_TIMING_H
#include <stdbool.h>
#include <stdint.h>
/* Recovered NI performFormatChange@0x6a68/0x6a7c/0x6a8c:
 * f=floor(rate/8000)+11; input latency4f, output latency8f, sample offset8f.
 * These are original host timing declarations, NOT the USB queue horizon. */
enum { RK3_AUDIO_DEFAULT_SAMPLE_RATE=44100, RK3_AUDIO_RATE_COUNT=5 };
static inline uint32_t rk3_audio_sample_rate_at(unsigned index) {
    const uint32_t rates[RK3_AUDIO_RATE_COUNT]={44100,48000,88200,96000,192000};
    return index<RK3_AUDIO_RATE_COUNT?rates[index]:0;
}
static inline bool rk3_audio_sample_rate_supported(uint32_t rate) {
    for(unsigned i=0;i<RK3_AUDIO_RATE_COUNT;++i)
        if(rate==rk3_audio_sample_rate_at(i))return true;
    return false;
}
static inline uint32_t rk3_audio_packet_frames(uint32_t rate) {
    return rk3_audio_sample_rate_supported(rate)?rate/8000+11:0;
}
static inline uint32_t rk3_audio_input_latency_frames(uint32_t rate) {
    return rk3_audio_packet_frames(rate)*4;
}
static inline uint32_t rk3_audio_output_latency_frames(uint32_t rate) {
    return rk3_audio_packet_frames(rate)*8;
}
static inline uint32_t rk3_audio_sample_offset_frames(uint32_t rate) {
    return rk3_audio_packet_frames(rate)*8;
}
#endif
