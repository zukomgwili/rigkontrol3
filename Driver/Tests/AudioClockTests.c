#include "AudioClock.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t frames[64];size_t count,index;
    int32_t errors[64];
    uint64_t times[64];size_t time_count,time_index;
    uint64_t now;
    unsigned numer,denom;
    char trace[256];size_t trace_count;
} Source;
static void trace(Source *s,char operation) {
    assert(s->trace_count+1<sizeof(s->trace));s->trace[s->trace_count++]=operation;
    s->trace[s->trace_count]=0;
}
static int32_t frame(void *context,uint64_t *value,uint64_t *paired) {
    Source *s=context;trace(s,'F');assert(s->index<s->count);
    size_t index=s->index++;if(s->errors[index])return s->errors[index];
    *value=s->frames[index];s->now=*value*UINT64_C(1000000)*s->denom/s->numer;
    *paired=UINT64_C(9000000000000); /* Must never replace local uptime. */
    return 0;
}
static uint64_t uptime(void *context) {
    Source *s=context;trace(s,'T');
    if(s->time_count){assert(s->time_index<s->time_count);return s->times[s->time_index++];}
    return s->now;
}
static uint64_t to_ns(void *context,uint64_t n) {Source *s=context;trace(s,'N');return n*s->numer/s->denom;}
static uint64_t to_absolute(void *context,uint64_t n) {Source *s=context;trace(s,'A');return n*s->denom/s->numer;}
static void set_frames(Source *s,const uint64_t *frames,size_t count) {
    assert(count<=64);memcpy(s->frames,frames,count*sizeof(*frames));s->count=count;s->index=0;
    s->trace_count=0;s->trace[0]=0;
}
static void set_times(Source *s,const uint64_t *times,size_t count) {
    assert(count<=64);memcpy(s->times,times,count*sizeof(*times));s->time_count=count;s->time_index=0;
}
static NIAudioClock new_clock(Source *s) {
    if(!s->numer)s->numer=1;if(!s->denom)s->denom=1;
    const NIAudioClockProvider p={s,frame,uptime,to_ns,to_absolute,NULL};NIAudioClock c;
    assert(ni_audio_clock_init(&c,&p));return c;
}
static void test_cold_and_two_eight_tick_groups(void) {
    Source s={0};NIAudioClock c=new_clock(&s);
    const uint64_t cold[]={1000,1000,1000,1001,1001,1002};set_frames(&s,cold,6);
    assert(ni_audio_clock_start(&c)==0);
    assert(!strcmp(s.trace,"FTFATFTFTNATFTFTTA"));
    assert(c.counter==0&&c.anchor_frame==1002&&c.anchor_time==1002000000);
    assert(c.previous_frame==1001&&c.previous_ns==1001000000);
    assert(c.interval_scaled==10000000000&&c.next_callback==1130000000);
    for(unsigned tick=1;tick<=16;++tick) {
        uint64_t f=c.next_callback/1000000;
        const uint64_t observations[]={f,f,f,f+1};set_frames(&s,observations,tick%8==0?4:2);
        s.now=c.next_callback;assert(ni_audio_clock_service(&c)==0);
        assert(s.index==s.count&&c.counter==tick%8);
        assert(!strcmp(s.trace,tick%8==0?"TFTFNATFTFTTA":"TFTFNTA"));
        assert(c.interval_scaled==10000000000);
    }
}
static void test_integer_estimator_and_stable_sandwich(void) {
    Source s={0};NIAudioClock c=new_clock(&s);
    c.anchor_frame=c.previous_frame=1000;c.anchor_time=c.previous_ns=1000000000;c.counter=0;
    const uint64_t f[]={1127,1128,1128,1128};set_frames(&s,f,4);
    /* The first crossing sandwich is discarded. The last matching frame
     * sample supplies local uptime, not either framework paired timestamp. */
    const uint64_t t[]={1127000000,1128012800,1128012800};set_times(&s,t,3);
    assert(ni_audio_clock_start(&c)==0&&s.index==4);
    assert(c.interval_scaled==10000000977&&c.previous_frame==1128&&c.previous_ns==1128012800);
    assert(c.anchor_frame==1000&&c.anchor_time==1000000000&&c.counter==1);
    const uint64_t unchanged[]={1128,1128};set_frames(&s,unchanged,2);
    const uint64_t same_times[]={1129000000,1129000000};set_times(&s,same_times,2);
    assert(ni_audio_clock_start(&c)==0);
    assert(c.interval_scaled==10000000977&&c.previous_frame==1128&&c.previous_ns==1128012800);
    c.interval_scaled=0;const uint64_t next[]={1256,1256};set_frames(&s,next,2);
    const uint64_t next_times[]={1256012800,1256012800};set_times(&s,next_times,2);
    assert(ni_audio_clock_start(&c)==0&&c.interval_scaled==10000000000);
    /* Low64 multiply/add and unsigned DIV are explicit b965..b98c facts.
     * (300000000 + (2^64-1)*1023 +512) modulo2^64 >>10 =292968. */
    c.interval_scaled=UINT64_MAX;c.previous_frame=10;c.previous_ns=UINT64_MAX-9999;
    const uint64_t overflow_frames[]={11,11};set_frames(&s,overflow_frames,2);
    const uint64_t overflow_times[]={20000,20000};set_times(&s,overflow_times,2);
    assert(ni_audio_clock_start(&c)==0&&c.interval_scaled==292968);
    c.interval_scaled=0;c.previous_frame=10;c.previous_ns=100;
    const uint64_t backward_frames[]={9,9};set_frames(&s,backward_frames,2);
    const uint64_t backward_times[]={200,200};set_times(&s,backward_times,2);
    assert(ni_audio_clock_start(&c)==0&&c.interval_scaled==0);
}
static NIAudioClock boundary_clock(Source *s) {
    NIAudioClock c=new_clock(s);c.counter=7;
    c.anchor_frame=c.previous_frame=1999;c.anchor_time=c.previous_ns=9999000000;
    return c;
}
static void test_boundary_local_time_deadline_and_skipped_frame(void) {
    Source s={0};NIAudioClock c=boundary_clock(&s);
    const uint64_t f[]={2000,2000,2000,2000,2001};set_frames(&s,f,5);
    const uint64_t t[]={10000000000,10000000000,10000700000,10001005000,10001006000,10001006000};
    set_times(&s,t,6);assert(ni_audio_clock_start(&c)==0);
    assert(c.anchor_frame==2001&&c.anchor_time==10001006000&&c.counter==0);
    c=boundary_clock(&s);const uint64_t skipped[]={2000,2000,2000,2003};set_frames(&s,skipped,4);
    const uint64_t skip_time[]={10000000000,10000000000,10001005000,10001006000,10001006000};
    set_times(&s,skip_time,5);assert(ni_audio_clock_start(&c)==0);
    assert(c.anchor_frame==2001&&c.anchor_time==10001006000); /* FIRST+1, not returned2003. */
    c=boundary_clock(&s);const uint64_t timeout_frames[]={2000,2000,2000,2000};set_frames(&s,timeout_frames,4);
    const uint64_t timeout_times[]={10000000000,10000000000,10001100001,10001200000,10001200000};
    set_times(&s,timeout_times,5);assert(ni_audio_clock_start(&c)==NI_AUDIO_CLOCK_BOUNDARY_TIMEOUT);
    assert(c.anchor_frame==1999&&c.anchor_time==9999000000&&c.counter==0&&c.armed);
    /* Equality at the BEFORE sample succeeds; AFTER can exceed the deadline. */
    c=boundary_clock(&s);set_frames(&s,timeout_frames,4);
    const uint64_t equality[]={10000000000,10000000000,10001100000,10001200000,10001200000};
    set_times(&s,equality,5);assert(ni_audio_clock_start(&c)==0);
    assert(c.anchor_frame==2001&&c.anchor_time==10001200000);
}
static void test_bootstrap_timeout_fallback_and_source_failure(void) {
    Source s={0};NIAudioClock c=new_clock(&s);
    const uint64_t f[]={1000,1000,1000,1000,1001,1002};set_frames(&s,f,6);
    const uint64_t t[]={1000000000,1000000000,1001100001,1001200000,1001200000,1001200000,1002000000,1002000000};
    set_times(&s,t,8);assert(ni_audio_clock_start(&c)==0);
    assert(c.previous_frame==1000&&c.previous_ns==1000000000);
    assert(c.anchor_frame==1002&&c.anchor_time==1002000000&&c.counter==0);
    /* Selected public provider errors have no original value-only equivalent.
     * They preserve a prior usable observation; no retry/panic policy lives in
     * this component. The owner classifies actual removal separately. */
    uint64_t old_anchor=c.anchor_frame,old_time=c.anchor_time,old_previous=c.previous_frame;
    s.time_count=0;const uint64_t failed[]={1128};set_frames(&s,failed,1);s.errors[0]=-77;
    assert(ni_audio_clock_start(&c)==-77&&c.armed);
    assert(c.anchor_frame==old_anchor&&c.anchor_time==old_time&&c.previous_frame==old_previous&&c.counter==1);
    memset(s.errors,0,sizeof(s.errors));
    c=boundary_clock(&s);const uint64_t failed_boundary[]={2000,2000,2000};set_frames(&s,failed_boundary,3);
    s.errors[2]=-88;assert(ni_audio_clock_start(&c)==-88&&c.counter==0);
    assert(c.anchor_frame==1999&&c.anchor_time==9999000000);
}
static void test_restart_sleep_nonunity_and_late_service(void) {
    Source s={.numer=125,.denom=3};NIAudioClock c=new_clock(&s);
    const uint64_t cold[]={1000,1000,1000,1001,1001,1002};set_frames(&s,cold,6);
    assert(ni_audio_clock_start(&c)==0&&c.anchor_time==24048000&&c.previous_ns==1001000000);
    c.interval_scaled=10000000977;c.counter=3;
    uint64_t anchor=c.anchor_frame,anchor_time=c.anchor_time,previous=c.previous_frame,previous_ns=c.previous_ns;
    ni_audio_clock_stop(&c);s.now=UINT64_C(3000000000)*3/125;size_t old_index=s.index;
    assert(ni_audio_clock_service(&c)==0&&s.index==old_index);
    assert(c.anchor_frame==anchor&&c.anchor_time==anchor_time&&c.previous_frame==previous&&c.previous_ns==previous_ns);
    const uint64_t same[]={1001,1001};set_frames(&s,same,2);
    assert(ni_audio_clock_start(&c)==0&&c.counter==4&&c.interval_scaled==10000000977);
    ni_audio_clock_enter_sleep(&c);
    assert(!c.armed&&!c.anchor_frame&&!c.anchor_time&&!c.previous_frame&&!c.previous_ns);
    assert(c.counter==4&&c.interval_scaled==10000000977);
    ni_audio_clock_prepare_wake(&c);assert(c.counter==8&&c.interval_scaled==10000000000);
    set_frames(&s,cold,6);assert(ni_audio_clock_start(&c)==0&&c.counter==0);
    assert(c.previous_frame==1001&&c.anchor_frame==1002);
    const uint64_t late[]={3000,3000};set_frames(&s,late,2);s.now=72000000;
    assert(ni_audio_clock_service(&c)==0&&s.index==2&&c.counter==1&&c.next_callback==75072000);
    size_t queries=s.index;assert(ni_audio_clock_service(&c)==0&&s.index==queries);
}
static void test_exact_mach_timebase_scaling(void) {
    /* Independently worked integer constants:2^53+1 is divisible by3, so
     * (9007199254740993/3)*125 =375299968947541375 exactly. ARM long-double
     * has53 significant bits; using it loses this actual binding observation. */
    uint64_t actual=ni_audio_clock_scale_time(UINT64_C(9007199254740993),125,3);
    if(actual!=UINT64_C(375299968947541375))
        fprintf(stderr,"Mach125/3 precision RED: actual=%" PRIu64 " expected=375299968947541375\n",actual);
    assert(actual==UINT64_C(375299968947541375));
    assert(ni_audio_clock_scale_time(UINT64_C(375299968947541375),3,125)==UINT64_C(9007199254740993));
    assert(ni_audio_clock_scale_time(UINT64_C(9007199254740993),1,1)==UINT64_C(9007199254740993));
    assert(ni_audio_clock_scale_time(UINT64_MAX-1,1,1)==UINT64_MAX-1);
    assert(ni_audio_clock_scale_time(1,125,3)==41);
    assert(ni_audio_clock_scale_time(41,3,125)==0&&ni_audio_clock_scale_time(42,3,125)==1);
    /* The mathematical answer fits although value*numerator does not. */
    assert(ni_audio_clock_scale_time(UINT64_MAX,3,125)==UINT64_C(442721857769029238));
    assert(ni_audio_clock_scale_time(UINT64_MAX,1,UINT32_MAX)==UINT64_C(4294967297));
    assert(ni_audio_clock_scale_time(UINT64_MAX,UINT32_MAX,UINT32_MAX)==UINT64_MAX);
    assert(ni_audio_clock_scale_time(UINT64_MAX-1,UINT32_MAX,UINT32_MAX)==UINT64_MAX-1);
    assert(ni_audio_clock_scale_time(UINT64_MAX,125,3)==UINT64_MAX);
    assert(ni_audio_clock_scale_time(UINT64_MAX,2,1)==UINT64_MAX);
    assert(ni_audio_clock_scale_time(UINT64_MAX,0,3)==0);
    assert(ni_audio_clock_scale_time(0,1,0)==UINT64_MAX);
}
static void test_transient_error_preserves_callback_phase(void) {
    Source s={0};NIAudioClock c=new_clock(&s);c.counter=3;
    c.anchor_frame=c.previous_frame=1000;c.anchor_time=c.previous_ns=1000000000;
    const uint64_t failed[]={1128};set_frames(&s,failed,1);s.errors[0]=-77;
    assert(ni_audio_clock_start(&c)==-77);
    assert(c.counter==4&&c.armed);
    assert(c.anchor_frame==1000&&c.anchor_time==1000000000);
    assert(c.previous_frame==1000&&c.previous_ns==1000000000&&c.interval_scaled==10000000000);
}
static void test_transient_error_still_reaches_boundary(void) {
    Source s={0};NIAudioClock c=new_clock(&s);c.counter=7;s.now=2000000000;
    c.anchor_frame=c.previous_frame=1000;c.anchor_time=c.previous_ns=1000000000;
    const uint64_t f[]={1128,2000,2001};set_frames(&s,f,3);s.errors[0]=-77;
    assert(ni_audio_clock_start(&c)==-77);
    assert(c.counter==0&&s.index==3&&c.armed);
    assert(c.anchor_frame==2001&&c.anchor_time==2001000000);
    assert(c.previous_frame==1000&&c.previous_ns==1000000000&&c.interval_scaled==10000000000);
}
static bool source_error_fatal(void *context,int32_t error) {(void)context;return error==-99||error==-100;}
static void test_fatal_query_stops_and_overrides_transient(void) {
    Source s={0};NIAudioClock c=new_clock(&s);c.provider.is_fatal=source_error_fatal;c.counter=7;
    c.anchor_frame=c.previous_frame=1000;c.anchor_time=c.previous_ns=1000000000;
    const uint64_t first_fatal[]={1128};set_frames(&s,first_fatal,1);s.errors[0]=-99;
    assert(ni_audio_clock_start(&c)==-99&&!c.armed&&c.counter==7&&s.index==1);
    assert(!strcmp(s.trace,"F")); /* No threshold query or timer rearm after removal. */
    assert(c.anchor_frame==1000&&c.previous_frame==1000&&c.interval_scaled==10000000000);
    size_t queries=s.index;assert(ni_audio_clock_service(&c)==0&&s.index==queries);

    c=new_clock(&s);c.provider.is_fatal=source_error_fatal;c.counter=7;s.now=2000000000;
    c.anchor_frame=c.previous_frame=1000;c.anchor_time=c.previous_ns=1000000000;
    const uint64_t later_fatal[]={1128,2000};set_frames(&s,later_fatal,2);
    memset(s.errors,0,sizeof(s.errors));s.errors[0]=-77;s.errors[1]=-99;
    assert(ni_audio_clock_start(&c)==-99&&!c.armed&&c.counter==0&&s.index==2);
    assert(!strcmp(s.trace,"FATF")); /* The later removal outranks initial-77. */
    assert(c.anchor_frame==1000&&c.previous_frame==1000&&c.interval_scaled==10000000000);

    c=new_clock(&s);c.provider.is_fatal=source_error_fatal;c.counter=7;s.now=2000000000;
    c.anchor_frame=c.previous_frame=1000;c.anchor_time=c.previous_ns=1000000000;
    const uint64_t loop_fatal[]={1128,2000,2001};set_frames(&s,loop_fatal,3);
    memset(s.errors,0,sizeof(s.errors));s.errors[0]=-77;s.errors[2]=-100;
    assert(ni_audio_clock_start(&c)==-100&&!c.armed&&c.counter==0&&s.index==3);
    assert(!strcmp(s.trace,"FATFTF"));
    assert(c.anchor_frame==1000&&c.previous_frame==1000&&c.interval_scaled==10000000000);
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--transient-threshold")) {
        test_transient_error_still_reaches_boundary();return 0;
    }
    test_transient_error_preserves_callback_phase();test_transient_error_still_reaches_boundary();
    test_fatal_query_stops_and_overrides_transient();
    test_exact_mach_timebase_scaling();
    test_cold_and_two_eight_tick_groups();
    test_integer_estimator_and_stable_sandwich();test_boundary_local_time_deadline_and_skipped_frame();
    test_bootstrap_timeout_fallback_and_source_failure();test_restart_sleep_nonunity_and_late_service();
    puts("AudioClock: NI cadence, bootstrap, integer/filter, timeout, restart and sleep-state vectors passed.");return 0;
}
