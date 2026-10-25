#ifndef NI_AUDIO_CLOCK_H
#define NI_AUDIO_CLOCK_H
#include <stdbool.h>
#include <stdint.h>

/* The frame query's paired framework timestamp is deliberately separate from
 * local uptime. NI samples local uptime between frame reads and after its
 * boundary probe; those observations are not interchangeable. */
typedef struct {
    void *context;
    int32_t (*frame)(void *,uint64_t *,uint64_t *);
    uint64_t (*uptime)(void *);
    uint64_t (*to_nanoseconds)(void *,uint64_t);
    uint64_t (*to_absolute)(void *,uint64_t);
    /* Optional binding classification, not a recovery policy. NULL treats
     * provider failures as nonfatal observations. */
    bool (*is_fatal)(void *,int32_t);
} NIAudioClockProvider;

/* Optional temporary diagnostic observer, separate from provider/time reads.
 * BEGIN has no existing tick; normal END reuses the callback's existing local
 * uptime. Fatal END has no such sample. Observer must not reenter the clock,
 * allocate, wait, log, query USB or change its timing/provider state. */
typedef void (*NIAudioClockObserver)(void *,bool beginning,uint64_t existing_tick,bool has_tick);

/* NI 2.8.0 engine fields bd58/bd70/bd78/bd80/bd88/bd90. AbsoluteTime stays
 * in provider ticks; only the estimator's previous time is nanoseconds. */
typedef struct {
    NIAudioClockProvider provider;
    uint64_t interval_scaled,anchor_frame,anchor_time,previous_frame,previous_ns;
    uint64_t next_callback;
    uint8_t counter;
    bool armed;
    NIAudioClockObserver observer;
    void *observer_context;
} NIAudioClock;

#define NI_AUDIO_CLOCK_INITIAL_INTERVAL UINT64_C(10000000000)
#define NI_AUDIO_CLOCK_BOUNDARY_TIMEOUT ((int32_t)UINT32_C(0xe00002bc))

bool ni_audio_clock_init(NIAudioClock *,const NIAudioClockProvider *);
void ni_audio_clock_set_observer(NIAudioClock *,NIAudioClockObserver,void *);
/* Mach timebase binding: floor(value*numerator/denominator), saturated at
 * UINT64_MAX on overflow (or a zero denominator). No estimator arithmetic. */
uint64_t ni_audio_clock_scale_time(uint64_t,uint32_t,uint32_t);
/* Ordinary same-engine start invokes one immediate maintenance callback; it
 * does not reset the estimator or eight-tick counter. Stop only disarms. */
int32_t ni_audio_clock_start(NIAudioClock *);
void ni_audio_clock_stop(NIAudioClock *);
int32_t ni_audio_clock_service(NIAudioClock *);
bool ni_audio_clock_valid(const NIAudioClock *);
/* State transitions only. No OS wake hook is implied. The caller clears NI's
 * output stream after prepare_wake and before start, as awakeFromSleep does. */
void ni_audio_clock_enter_sleep(NIAudioClock *);
void ni_audio_clock_prepare_wake(NIAudioClock *);

#endif
