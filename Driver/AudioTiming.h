#ifndef RK3_AUDIO_TIMING_H
#define RK3_AUDIO_TIMING_H
/* Recovered NI performFormatChange@0x6a68/0x6a7c/0x6a8c:
 * f=floor(rate/8000)+11; input latency4f, output latency8f, sample offset8f.
 * These are original host timing declarations, NOT the USB queue horizon. */
enum {
    RK3_AUDIO_SAMPLE_RATE=48000,
    RK3_AUDIO_PACKET_FRAMES=RK3_AUDIO_SAMPLE_RATE/8000+11,
    RK3_AUDIO_INPUT_LATENCY_FRAMES=RK3_AUDIO_PACKET_FRAMES*4,
    RK3_AUDIO_OUTPUT_LATENCY_FRAMES=RK3_AUDIO_PACKET_FRAMES*8,
    RK3_AUDIO_SAMPLE_OFFSET_FRAMES=RK3_AUDIO_PACKET_FRAMES*8
};
#endif
