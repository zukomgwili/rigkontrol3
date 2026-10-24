#ifndef RK3_AUDIO_TIMING_H
#define RK3_AUDIO_TIMING_H

/* Current native stereo48k adaptation, not a recovered NI timing constant.
 * Keep the NI input queue/calendar, but submit output four USB milliseconds
 * after the matching input frame. Host buffer and converter delay are extra. */
enum {
    RK3_AUDIO_SAMPLE_RATE = 48000,
    RK3_AUDIO_FRAMES_PER_USB_MILLISECOND = 48,
    RK3_AUDIO_OUTPUT_LEAD_MILLISECONDS = 4,
    RK3_AUDIO_INPUT_LATENCY_FRAMES = RK3_AUDIO_FRAMES_PER_USB_MILLISECOND,
    RK3_AUDIO_OUTPUT_LATENCY_FRAMES = RK3_AUDIO_OUTPUT_LEAD_MILLISECONDS * RK3_AUDIO_FRAMES_PER_USB_MILLISECOND
};

#endif
