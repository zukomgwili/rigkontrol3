/* Own reconstruction of NIUSBAudioEngine2.8.0 clock maintenance:
 * initHardware5744..577b, updateb880, anchorba80, timerbbb0,
 * start6433, stop683e, sleepbc50 and awakebcb0. Original instructions, not
 * recovered signed casts, determine the uint64 arithmetic and byte counter.
 * The public provider's error return is a necessary current-macOS binding. */
#include "AudioClock.h"
#include <string.h>

uint64_t ni_audio_clock_scale_time(uint64_t value,uint32_t numerator,uint32_t denominator) {
    if(!denominator)return UINT64_MAX;
    if(!numerator)return 0;
    uint64_t quotient=value/denominator,remainder=value%denominator;
    if(quotient>UINT64_MAX/numerator)return UINT64_MAX;
    uint64_t whole=quotient*numerator;
    /* Both ratio components are32-bit; remainder<denominator, so this
     * product fits64 bits even when value*numerator itself would overflow. */
    uint64_t fraction=(remainder*numerator)/denominator;
    if(whole>UINT64_MAX-fraction)return UINT64_MAX;
    return whole+fraction;
}

bool ni_audio_clock_init(NIAudioClock *clock,const NIAudioClockProvider *provider) {
    if(!clock||!provider||!provider->frame||!provider->uptime||
       !provider->to_nanoseconds||!provider->to_absolute)return false;
    memset(clock,0,sizeof(*clock));clock->provider=*provider;
    clock->interval_scaled=NI_AUDIO_CLOCK_INITIAL_INTERVAL;clock->counter=8;
    return true;
}

static int32_t boundary(NIAudioClock *clock,bool *provider_failure) {
    NIAudioClockProvider *p=&clock->provider;uint64_t ignored=0,first=0,frame=0;
    *provider_failure=false;
    uint64_t budget=p->to_absolute(p->context,1100000);
    uint64_t deadline=p->uptime(p->context)+budget;
    int32_t error=p->frame(p->context,&first,&ignored);
    if(error){*provider_failure=true;return error;}
    uint64_t before;
    do {
        before=p->uptime(p->context);
        error=p->frame(p->context,&frame,&ignored);
        if(error){*provider_failure=true;return error;}
    } while(frame==first&&deadline>before);
    uint64_t after=p->uptime(p->context);
    if(deadline<before)return NI_AUDIO_CLOCK_BOUNDARY_TIMEOUT;
    /* ba80 stores FIRST+1, even if a delayed observation skipped frames, and
     * the final local uptime rather than a framework-provided paired time. */
    clock->anchor_frame=first+1;clock->anchor_time=after;
    return 0;
}

static int32_t update_interval(NIAudioClock *clock) {
    NIAudioClockProvider *p=&clock->provider;uint64_t first=0,last=0,ignored=0,time=0;
    do {
        int32_t error=p->frame(p->context,&first,&ignored);if(error)return error;
        time=p->uptime(p->context);
        error=p->frame(p->context,&last,&ignored);if(error)return error;
    } while(first!=last);
    uint64_t ns=0;
    if(clock->anchor_frame)ns=p->to_nanoseconds(p->context,time);
    else {
        bool provider_failure=false;int32_t error=boundary(clock,&provider_failure);
        /* A current-API query error leaves the previous usable clock intact.
         * The recovered boundary TIMEOUT instead falls back to the already
         * successful stable sandwich, as b938..b940 does. */
        if(provider_failure)return error;
        if(error){clock->anchor_frame=first;clock->anchor_time=time;}
    }
    if(!clock->previous_frame) {
        clock->previous_frame=clock->anchor_frame;
        clock->previous_ns=p->to_nanoseconds(p->context,clock->anchor_time);
    } else {
        uint64_t delta=first-clock->previous_frame;if(!delta)return 0;
        /* IMUL's low64 result, unsigned DIV and logical SHR are intentional.
         * Unsigned C arithmetic retains wrap without introducing UB/clamps. */
        uint64_t measured=((ns-clock->previous_ns)*UINT64_C(10000))/delta;
        if(clock->interval_scaled)
            measured=(measured+clock->interval_scaled*UINT64_C(1023)+UINT64_C(512))>>10;
        clock->interval_scaled=measured;clock->previous_frame=first;clock->previous_ns=ns;
    }
    return 0;
}

static int32_t callback(NIAudioClock *clock) {
    int32_t error=update_interval(clock);
    NIAudioClockProvider *p=&clock->provider;
    if(error&&p->is_fatal&&p->is_fatal(p->context,error)) {
        clock->armed=false;return error;
    }
    /* bbe9->bbf4 advances callback phase independently of update success.
     * A transient public query failure preserves the estimator, not phase. */
    clock->counter=(uint8_t)(clock->counter+1u);
    if(clock->counter>=8) {
        bool provider_failure=false;int32_t anchor_error=boundary(clock,&provider_failure);
        clock->counter=0; /* Original ignores this boundary's return. */
        if(anchor_error&&p->is_fatal&&p->is_fatal(p->context,anchor_error)) {
            clock->armed=false;return anchor_error; /* Removal outranks a prior transient. */
        }
        if(!error)error=anchor_error;
    }
    /* setTimeoutMS128 rearms relative to this callback, without catch-up.
     * This runs on the existing serial owner, never the audio RT path. */
    uint64_t now=clock->provider.uptime(clock->provider.context);
    uint64_t delay=clock->provider.to_absolute(clock->provider.context,128000000);
    clock->next_callback=now+delay;return error;
}

int32_t ni_audio_clock_start(NIAudioClock *clock) {
    clock->armed=true;return callback(clock);
}
void ni_audio_clock_stop(NIAudioClock *clock) {clock->armed=false;}
int32_t ni_audio_clock_service(NIAudioClock *clock) {
    if(!clock->armed||clock->provider.uptime(clock->provider.context)<clock->next_callback)return 0;
    return callback(clock);
}
bool ni_audio_clock_valid(const NIAudioClock *clock) {return clock->anchor_frame!=0;}
void ni_audio_clock_enter_sleep(NIAudioClock *clock) {
    ni_audio_clock_stop(clock);clock->anchor_frame=clock->anchor_time=0;
    clock->previous_frame=clock->previous_ns=0;
}
void ni_audio_clock_prepare_wake(NIAudioClock *clock) {
    clock->counter=8;clock->interval_scaled=NI_AUDIO_CLOCK_INITIAL_INTERVAL;
}
