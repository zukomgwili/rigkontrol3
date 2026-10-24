/* rk3_engine.c — see rk3_engine.h */

#include "rk3_engine.h"


#include <os/log.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>

static os_log_t eng_log(void)
{
	static os_log_t l;
	static dispatch_once_t once;
	dispatch_once(&once, ^{ l = os_log_create("com.mihai.rk3", "engine"); });
	return l;
}
#define ELOG(fmt, ...) os_log(eng_log(), "[rk3] " fmt, ##__VA_ARGS__)

/* ---- ring helpers (single producer + single consumer per ring) ------- */

static inline unsigned ring_avail(unsigned w, unsigned r)
{
	return (w - r + RK3_RING_BYTES) % RK3_RING_BYTES;   /* bytes readable */
}
static inline unsigned ring_space(unsigned w, unsigned r)
{
	/* keep one byte free so full != empty */
	return RK3_RING_BYTES - 1 - ring_avail(w, r);
}

/* ---- cadence mirror (engine-thread only) ----------------------------- */

static int pat_count(struct rk3_engine *e) { return e->pat_head - e->pat_tail; }

static void pat_push(struct rk3_engine *e, struct rk3_iso_xfer *x)
{
	if (pat_count(e) >= RK3_PAT_RING)
		e->pat_tail++;
	int *slot = e->pat[e->pat_head % RK3_PAT_RING];
	for (int f = 0; f < RK3_FRAMES_PER_XFER; f++)
		slot[f] = x->frames[f].frActCount;
	e->pat_head++;
}

/* Synthesize the device's bursty 88/0 cadence for one transfer (startup). */
static void pat_synth(struct rk3_engine *e, int *lens)
{
	int pkt_frames = e->bpp / 8;
	int need = e->rate / 8000;
	int rem  = e->rate % 8000;
	for (int f = 0; f < RK3_FRAMES_PER_XFER; f++) {
		e->out_acc += need * 8000 + rem;
		if (e->out_acc >= pkt_frames * 8000) {
			e->out_acc -= pkt_frames * 8000;
			lens[f] = e->bpp;
		} else {
			lens[f] = 0;
		}
	}
}

static void pat_pop(struct rk3_engine *e, int *lens)
{
	if (pat_count(e) > 0) {
		int *slot = e->pat[e->pat_tail % RK3_PAT_RING];
		for (int f = 0; f < RK3_FRAMES_PER_XFER; f++)
			lens[f] = slot[f];
		e->pat_tail++;
	} else {
		pat_synth(e, lens);
	}
}

/*
 * Pop one OUT transfer's worth (RK3_OUT_FRAMES_PER_XFER microframes) of cadence,
 * refilling the IN-sized staging block as it drains. RK3_OUT_FRAMES_PER_XFER
 * divides RK3_FRAMES_PER_XFER, so each captured IN block feeds an exact integer
 * number of OUT transfers — the OUT chain stays rate-locked to the device clock
 * even though it ships smaller transfers than capture.
 */
static void pat_pop_out(struct rk3_engine *e, int *lens)
{
	if (e->pat_stage_off >= RK3_FRAMES_PER_XFER) {
		pat_pop(e, e->pat_stage);
		e->pat_stage_off = 0;
	}
	for (int f = 0; f < RK3_OUT_FRAMES_PER_XFER; f++)
		lens[f] = e->pat_stage[e->pat_stage_off + f];
	e->pat_stage_off += RK3_OUT_FRAMES_PER_XFER;
}

/* ---- isoc completion callbacks (ported from stream.c) ---------------- */

static void resubmit_in(struct rk3_iso_xfer *x);
static int  submit_out(struct rk3_iso_xfer *x);
static void fill_out(struct rk3_engine *e, struct rk3_iso_xfer *x);
static void out_done(void *refcon, IOReturn result, void *arg0);
static void in_done(void *refcon, IOReturn result, void *arg0);
static void fail_engine(struct rk3_engine *e, int32_t status);
static void iso_retire(struct rk3_iso_xfer *x);
static IOReturn iso_track(struct rk3_iso_xfer *x, IOReturn result);

/*
 * Isoc submit wrappers — the ONLY place the basic vs low-latency API divergence
 * lives. The callback bodies (in_done/fill_out/out_done/...) access x->buf and
 * x->frames[f] identically either way; only the submit call name + the extra
 * updateFrequency arg (and the kernel-shared buffer allocation) differ. See
 * RK3_USE_LOWLAT in rk3_engine.h.
 */
static IOReturn rk3_read_isoc(struct rk3_engine *e, struct rk3_iso_xfer *x,
			      UInt32 nframes)
{
#if RK3_USE_LOWLAT
	return iso_track(x, (*e->usb.intf)->LowLatencyReadIsochPipeAsync(e->usb.intf,
			e->usb.pipe_iso_in, x->buf, x->frame_start, nframes,
			RK3_LL_UPDATE_FREQ, x->frames, in_done, x));
#else
	return iso_track(x, (*e->usb.intf)->ReadIsochPipeAsync(e->usb.intf,
			e->usb.pipe_iso_in, x->buf, x->frame_start, nframes,
			x->frames, in_done, x));
#endif
}

static IOReturn rk3_write_isoc(struct rk3_engine *e, struct rk3_iso_xfer *x,
			       UInt32 nframes)
{
#if RK3_USE_LOWLAT
	return iso_track(x, (*e->usb.intf)->LowLatencyWriteIsochPipeAsync(e->usb.intf,
			e->usb.pipe_iso_out, x->buf, x->frame_start, nframes,
			RK3_LL_UPDATE_FREQ, x->frames, out_done, x));
#else
	return iso_track(x, (*e->usb.intf)->WriteIsochPipeAsync(e->usb.intf,
			e->usb.pipe_iso_out, x->buf, x->frame_start, nframes,
			x->frames, out_done, x));
#endif
}

#if RK3_USE_LOWLAT
/*
 * Allocate one transfer's kernel-shared data + frame-list buffers. read=1 for
 * the capture pipe, 0 for playback. bufsz/nframes are direction-specific (IN is
 * full size, OUT the smaller OUT_ values). Returns 0 on success. The basic path
 * uses inline arrays and needs no allocation, so these are LL-only.
 */
static int ll_alloc_xfer(struct rk3_engine *e, struct rk3_iso_xfer *x,
			 size_t bufsz, int nframes, int read)
{
	IOReturn kr = (*e->usb.intf)->LowLatencyCreateBuffer(e->usb.intf,
			(void **)&x->buf, bufsz,
			read ? kUSBLowLatencyReadBuffer : kUSBLowLatencyWriteBuffer);
	if (kr != kIOReturnSuccess || !x->buf) {
		ELOG("LowLatencyCreateBuffer(data) 0x%08x", kr);
		x->buf = NULL;
		return -1;
	}
	kr = (*e->usb.intf)->LowLatencyCreateBuffer(e->usb.intf,
			(void **)&x->frames,
			(IOByteCount)(nframes * sizeof(rk3_isoc_frame_t)),
			kUSBLowLatencyFrameListBuffer);
	if (kr != kIOReturnSuccess || !x->frames) {
		ELOG("LowLatencyCreateBuffer(framelist) 0x%08x", kr);
		x->frames = NULL;
		return -1;
	}
	memset(x->buf, 0, bufsz);
	return 0;
}

static void ll_free_xfer(struct rk3_engine *e, struct rk3_iso_xfer *x)
{
	if (x->buf) {
		(*e->usb.intf)->LowLatencyDestroyBuffer(e->usb.intf, x->buf);
		x->buf = NULL;
	}
	if (x->frames) {
		(*e->usb.intf)->LowLatencyDestroyBuffer(e->usb.intf, x->frames);
		x->frames = NULL;
	}
}

/* Allocate all isoc buffers for a stream start; frees partial on failure. */
static int ll_alloc_all(struct rk3_engine *e)
{
	for (int i = 0; i < RK3_N_XFERS; i++)
		if (ll_alloc_xfer(e, &e->in[i], RK3_XFER_BUF,
				  RK3_FRAMES_PER_XFER, 1) != 0)
			return -1;
	for (int i = 0; i < RK3_N_OUT_XFERS; i++)
		if (ll_alloc_xfer(e, &e->out[i], RK3_OUT_XFER_BUF,
				  RK3_OUT_FRAMES_PER_XFER, 0) != 0)
			return -1;
	return 0;
}

static void ll_free_all(struct rk3_engine *e)
{
	for (int i = 0; i < RK3_N_XFERS; i++)
		ll_free_xfer(e, &e->in[i]);
	for (int i = 0; i < RK3_N_OUT_XFERS; i++)
		ll_free_xfer(e, &e->out[i]);
}
#endif /* RK3_USE_LOWLAT */

#define SCHED_MARGIN 4
static UInt64 schedule_frame(struct rk3_engine *e, UInt64 want)
{
	UInt64 cur = 0;
	AbsoluteTime at;
	(*e->usb.intf)->GetBusFrameNumber(e->usb.intf, &cur, &at);
	if (want < cur + SCHED_MARGIN)
		want = cur + SCHED_MARGIN;
	return want;
}

static void in_done(void *refcon, IOReturn result, void *arg0)
{
	struct rk3_iso_xfer *x = refcon;
	struct rk3_engine *e = x->e;
	(void)arg0;
	iso_retire(x);
	if (result == kIOReturnNoDevice || result == kIOReturnNotAttached ||
	    result == kIOReturnNotResponding) { fail_engine(e, (int32_t)result); return; }

	if (!atomic_load_explicit(&e->audio_running, memory_order_acquire))
		return;

	/* Deinterleave each microframe into a scratch then push to the cap ring. */
	uint8_t scratch[RK3_XFER_BUF];
	int pos = 0;
	for (int f = 0; f < RK3_FRAMES_PER_XFER; f++) {
		int act = x->frames[f].frActCount;
		if (act <= 0)
			continue;
		caiaq_capture_mode2(&e->codec, x->buf + f * RK3_FRAME_BYTES, act,
				    scratch, &pos, sizeof(scratch));
	}
	if (pos > 0) {
		/* device clock: count frames actually produced, even if the ring drops them */
		atomic_fetch_add_explicit(&e->dev_frames,
				(unsigned)(pos / RK3_BYTES_PER_FRAME), memory_order_relaxed);
		unsigned w = atomic_load_explicit(&e->cap_w, memory_order_relaxed);
		unsigned r = atomic_load_explicit(&e->cap_r, memory_order_acquire);
		if (ring_space(w, r) < (unsigned)pos) {
			atomic_fetch_add_explicit(&e->cap_overruns, 1,
						  memory_order_relaxed);
		} else {
			unsigned first = RK3_RING_BYTES - w;
			if (first > (unsigned)pos) first = pos;
			memcpy(e->cap_buf + w, scratch, first);
			if ((unsigned)pos > first)
				memcpy(e->cap_buf, scratch + first, pos - first);
			atomic_store_explicit(&e->cap_w,
				(w + pos) % RK3_RING_BYTES, memory_order_release);
		}
	}

	/* mirror this capture's per-microframe cadence for playback */
	pat_push(e, x);

	resubmit_in(x);
}

/*
 * Fill one OUT transfer from the playback ring. Pops the captured cadence
 * (rate-locked to the device), guarantees a non-zero trailing frame, and packs
 * the audio CONTIGUOUSLY by frReqCount via caiaq_playback_mode2.
 *
 * Startup gate: until CoreAudio has buffered RK3_OUT_CUSHION (pb_started, set in
 * write_output), the engine sends digital silence and does NOT touch pb_r. Once
 * started, pb_r is read from the ring at RK3_OUT_PHASE (set in write_output) and
 * advances continuously, preserving the proven 4-byte OUT phase across packets.
 */
static void fill_out(struct rk3_engine *e, struct rk3_iso_xfer *x)
{
	int lens[RK3_OUT_FRAMES_PER_XFER];
	pat_pop_out(e, lens);

	if (lens[RK3_OUT_FRAMES_PER_XFER - 1] == 0) {
		int j = RK3_OUT_FRAMES_PER_XFER - 2;
		while (j >= 0 && lens[j] == 0)
			j--;
		if (j >= 0) {
			lens[RK3_OUT_FRAMES_PER_XFER - 1] = lens[j];
			lens[j] = 0;
		}
	}

	int started = atomic_load_explicit(&e->pb_started, memory_order_acquire);
	unsigned r = atomic_load_explicit(&e->pb_r, memory_order_relaxed);
	unsigned w = atomic_load_explicit(&e->pb_w, memory_order_acquire);
	unsigned avail = started ? ring_avail(w, r) : 0;

	int off = 0;
	for (int f = 0; f < RK3_OUT_FRAMES_PER_XFER; f++) {
		int len = lens[f];
		if (len > RK3_FRAME_BYTES) len = RK3_FRAME_BYTES;
		x->frames[f].frReqCount = len;
		x->frames[f].frActCount = 0;
		x->frames[f].frStatus = 0;
		if (len > 0) {
			int audio = len - (len + 1) / 4;   /* audio bytes in pkt */
			if (avail < (unsigned)audio) {
				/* not started yet, or underrun: emit silence */
				if (started)
					atomic_fetch_add_explicit(&e->pb_underruns,
							1, memory_order_relaxed);
				caiaq_playback_mode2(&e->codec, x->buf + off, len,
						     NULL, NULL, 0);
			} else {
				caiaq_playback_mode2(&e->codec, x->buf + off, len,
						     e->pb_buf, (int *)&r,
						     RK3_RING_BYTES);
				avail -= audio;
			}
		}
		off += len;
	}
	if (started) {
		atomic_store_explicit(&e->pb_r, r, memory_order_release);
		/* windowed-min out_fill telemetry: post-consume trough (this is the
		 * sample right after the OUT bite, i.e. closest to underrun). */
		unsigned wf = atomic_load_explicit(&e->pb_w, memory_order_relaxed);
		unsigned of = ring_avail(wf, r) / RK3_BYTES_PER_FRAME;
		if (of < e->out_fill_min) e->out_fill_min = of;
	}
}

static int submit_out(struct rk3_iso_xfer *x)
{
	struct rk3_engine *e = x->e;
	for (int tries = 0; tries < 8; tries++) {
		IOReturn kr = rk3_write_isoc(e, x, RK3_OUT_FRAMES_PER_XFER);
		if (kr == kIOReturnSuccess)
			return 0;
		e->out_errs++;
		UInt64 cur = 0;
		AbsoluteTime at;
		(*e->usb.intf)->GetBusFrameNumber(e->usb.intf, &cur, &at);
		UInt64 floor = cur + RK3_OUT_MIN_LEAD;
		x->frame_start = (x->frame_start < floor)
				 ? floor : x->frame_start + RK3_OUT_FRAME_STRIDE;
	}
	e->out_drops++;
	fail_engine(e, (int32_t)kIOReturnIOError);
	return -1;
}

static void out_done(void *refcon, IOReturn result, void *arg0)
{
	struct rk3_iso_xfer *x = refcon;
	struct rk3_engine *e = x->e;
	(void)arg0;
	iso_retire(x);
	if (result == kIOReturnNoDevice || result == kIOReturnNotAttached ||
	    result == kIOReturnNotResponding) { fail_engine(e, (int32_t)result); return; }
	if (!atomic_load_explicit(&e->audio_running, memory_order_acquire))
		return;
	x->frame_start += (UInt64)RK3_N_OUT_XFERS * RK3_OUT_FRAME_STRIDE;
	fill_out(e, x);
	submit_out(x);

	/*
	 * Drift-vs-jitter probe. out_done fires once per OUT transfer (every
	 * RK3_OUT_FRAME_STRIDE ms-frame = 8000/RK3_OUT_FRAMES_PER_XFER per sec)
	 * regardless of N_OUT_XFERS, so tick every (8000/OUT_FPX) calls ≈ 1 second. Log
	 * the OUT ring fill, the CAPTURE ring fill (so we catch a silent input-latency
	 * balloon — the IN ring growing if CoreAudio drains it slower than the device
	 * fills it), and the CUMULATIVE measured device rate from a FIXED anchor
	 * (converges as the run lengthens). Monotonic out_fill decline => drift; noisy
	 * around a stable mean with underruns on dips => jitter/margin.
	 */
	if (e->dbg_host0 == 0) {
		e->dbg_host0 = mach_absolute_time();
		e->dbg_devframes0 = atomic_load_explicit(&e->dev_frames, memory_order_relaxed);
	}
	if (++e->dbg_ctr >= (unsigned)(8000 / RK3_OUT_FRAMES_PER_XFER)) {
		e->dbg_ctr = 0;
		static mach_timebase_info_data_t tb;
		if (tb.denom == 0) mach_timebase_info(&tb);
		UInt64 now = mach_absolute_time();
		double elapsed_s = (double)(now - e->dbg_host0) * tb.numer / tb.denom / 1e9;
		unsigned w = atomic_load_explicit(&e->pb_w, memory_order_acquire);
		unsigned r = atomic_load_explicit(&e->pb_r, memory_order_relaxed);
		unsigned fill = ring_avail(w, r) / RK3_BYTES_PER_FRAME;
		unsigned cw = atomic_load_explicit(&e->cap_w, memory_order_acquire);
		unsigned cr = atomic_load_explicit(&e->cap_r, memory_order_relaxed);
		unsigned capfill = ring_avail(cw, cr) / RK3_BYTES_PER_FRAME;
		unsigned long long df = atomic_load_explicit(&e->dev_frames, memory_order_relaxed);
		double rate = elapsed_s > 0 ?
			(double)(df - e->dbg_devframes0) / elapsed_s : 0.0;
		unsigned ofmin = e->out_fill_min, cfmin = atomic_exchange(&e->cap_fill_min, ~0u);
		e->out_fill_min = ~0u;

		ELOG("clk: out_fill=%u (min %u) cap_fill=%u (min %u)  dev_rate=%.2f Hz (nominal %d)  underruns=%u",
		     fill, ofmin == ~0u ? 0 : ofmin, capfill, cfmin == ~0u ? 0 : cfmin,
		     rate, e->rate, atomic_load(&e->pb_underruns));
	}
}

static void resubmit_in(struct rk3_iso_xfer *x)
{
	struct rk3_engine *e = x->e;
	x->frame_start = schedule_frame(e, x->frame_start + RK3_N_XFERS * RK3_FRAME_STRIDE);
	for (int f = 0; f < RK3_FRAMES_PER_XFER; f++) {
		x->frames[f].frReqCount = RK3_FRAME_BYTES;
		x->frames[f].frActCount = 0;
		x->frames[f].frStatus = 0;
	}
	IOReturn kr = rk3_read_isoc(e, x, RK3_FRAMES_PER_XFER);
	if (kr != kIOReturnSuccess &&
	    atomic_load_explicit(&e->audio_running, memory_order_acquire)) {
		e->in_errs++;
		x->frame_start = schedule_frame(e, 0);
		kr = rk3_read_isoc(e, x, RK3_FRAMES_PER_XFER);
		if (kr != kIOReturnSuccess) fail_engine(e, (int32_t)kr);
	}
}

/* ---- engine thread --------------------------------------------------- */

static int prime_capture(struct rk3_engine *e)
{
#if RK3_USE_LOWLAT
	/* Allocate this start's kernel-shared isoc buffers (freed in audio_stop). */
	if (ll_alloc_all(e) != 0) {
		ELOG("prime: LowLatency buffer allocation failed");
		ll_free_all(e);
		return -1;
	}
#endif

	UInt64 frame = 0;
	AbsoluteTime atTime;
	if ((*e->usb.intf)->GetBusFrameNumber(e->usb.intf, &frame, &atTime) != kIOReturnSuccess) return -1;
	UInt64 start = frame + 10;

	for (int i = 0; i < RK3_N_XFERS; i++) {
		e->in[i].e = e;
		e->in[i].index = i;
		e->in[i].frame_start = start + (UInt64)i * RK3_FRAME_STRIDE;
		for (int f = 0; f < RK3_FRAMES_PER_XFER; f++)
			e->in[i].frames[f].frReqCount = RK3_FRAME_BYTES;
		IOReturn kr = rk3_read_isoc(e, &e->in[i], RK3_FRAMES_PER_XFER);
		if (kr != kIOReturnSuccess) return -1;
	}

	return 0;
}

static int prime_playback(struct rk3_engine *e)
{
	UInt64 frame = 0;
	AbsoluteTime at_time;
	if ((*e->usb.intf)->GetBusFrameNumber(e->usb.intf, &frame, &at_time) != kIOReturnSuccess) return -1;
	UInt64 start = frame + 10;
	for (int i = 0; i < RK3_N_OUT_XFERS; i++) {
		e->out[i].e = e;
		e->out[i].index = i;
		e->out[i].frame_start = start + (UInt64)i * RK3_OUT_FRAME_STRIDE;
		fill_out(e, &e->out[i]);
		if (submit_out(&e->out[i]) != 0) return -1;
	}
	return 0;
}

/*
 * Promote the engine's USB/CFRunLoop thread to time-constraint (real-time)
 * scheduling so isoc completion callbacks are serviced on time even under DAW
 * load. Insurance for the smaller OUT transfers (higher completion rate): the
 * storm risk is a default-priority thread falling behind the completion stream.
 * preemptible=1 so we still yield to CoreAudio's own real-time IO thread.
 * Best-effort; a failure here is non-fatal (logged, engine runs default-sched).
 */
#ifndef RK3_ENGINE_TESTING
static void engine_set_realtime(void)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	double abs_per_ms = 1e6 * (double)tb.denom / (double)tb.numer;
	thread_time_constraint_policy_data_t pol;
	pol.period      = 0;                              /* aperiodic (event-driven) */
	pol.computation = (uint32_t)(0.5 * abs_per_ms);  /* ~0.5 ms to service a cb */
	pol.constraint  = (uint32_t)(2.0 * abs_per_ms);  /* deadline ~2 ms */
	pol.preemptible = 1;
	kern_return_t kr = thread_policy_set(mach_thread_self(),
			THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&pol,
			THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	if (kr != KERN_SUCCESS)
		ELOG("thread_policy_set RT failed: 0x%x", kr);
}

#endif /* !RK3_ENGINE_TESTING */

/* Shared EP1 owner. All USB calls and completion state below belong to the
 * engine runloop. Only lifecycle request publication/cancellation crosses
 * threads. No CoreMIDI, UDP, display animation or second EP1 reader. */
#include "../Control/rk3_control.h"
#include <stdlib.h>
#include <time.h>

#define RK3_COMMAND_TICK_SECONDS 0.005
#ifndef RK3_INIT_TIMEOUT_MS
#define RK3_INIT_TIMEOUT_MS 1500
#endif
#ifndef RK3_AUDIO_TIMEOUT_MS
#define RK3_AUDIO_TIMEOUT_MS 1500
#endif
#ifndef RK3_CLOSE_TIMEOUT_MS
#define RK3_CLOSE_TIMEOUT_MS 2000
#endif

enum lifecycle_action { LIFE_START, LIFE_STOP };
struct lifecycle_request {
	atomic_uint references;
	atomic_int cancelled;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	int done, result;
	enum lifecycle_action action;
	int stage;
};

enum internal_command { INTERNAL_NONE, INTERNAL_INFO, INTERNAL_NEUTRAL, INTERNAL_AUTO, INTERNAL_AUDIO };
struct rk3_engine_control {
	struct rk3_engine *owner;
	pthread_mutex_t api_mutex;
	pthread_mutex_t request_mutex;
	struct lifecycle_request *queued, *life; /* queued under mutex; life owner-only */
	atomic_int closing, exited, ready;
	int thread_created, close_started, startup_phase;
	int32_t startup_error;
	CFRunLoopSourceRef source;
	CFRunLoopTimerRef timer;
	CFRunLoopTimerRef rearm_timer;
	uint8_t input[512];
	int input_active, output_active;
	uint64_t last_read_arm;
	unsigned iso_in_flight;
	uint8_t output[64];
	uint32_t output_length;
	int output_external;
	struct rk3_control_command external;
	enum internal_command internal;
	uint8_t expected_reply;
	int write_done, reply_done;
	int32_t write_status;
	uint8_t reply[512];
	size_t reply_length;
	uint64_t deadline;
};

static uint64_t after_ms(unsigned ms)
{
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	uint64_t ticks = (uint64_t)ms * 1000000ULL * tb.denom / tb.numer;
	uint64_t now = mach_absolute_time();
	return UINT64_MAX - now < ticks ? UINT64_MAX : now + ticks;
}

static struct timespec wall_deadline(unsigned ms)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += ms / 1000;
	ts.tv_nsec += (long)(ms % 1000) * 1000000;
	if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
	return ts;
}

static struct lifecycle_request *life_new(enum lifecycle_action action)
{
	struct lifecycle_request *r = calloc(1, sizeof(*r));
	if (!r) return NULL;
	atomic_init(&r->references, 2); /* caller + engine, including after timeout */
	atomic_init(&r->cancelled, 0);
	pthread_mutex_init(&r->mutex, NULL);
	pthread_cond_init(&r->condition, NULL);
	r->action = action;
	return r;
}

static void life_release(struct lifecycle_request *r)
{
	if (atomic_fetch_sub(&r->references, 1) != 1) return;
	pthread_cond_destroy(&r->condition);
	pthread_mutex_destroy(&r->mutex);
	free(r);
}

static int life_complete(struct lifecycle_request *r, int result)
{
	if (!r) return 0;
	pthread_mutex_lock(&r->mutex);
	int cancelled = atomic_load(&r->cancelled);
	r->result = cancelled && result == 0 ? (int32_t)kIOReturnTimeout : result;
	r->done = 1;
	pthread_cond_broadcast(&r->condition);
	pthread_mutex_unlock(&r->mutex);
	life_release(r);
	return cancelled;
}

static void fail_engine(struct rk3_engine *e, int32_t status)
{
	struct rk3_engine_control *c = e->control;
	if (status == 0) status = (int32_t)kIOReturnError;
	if (!atomic_exchange(&e->usb_dead, 1)) {
		ELOG("shared engine failed 0x%08x", status);
		c->startup_error = status;
		atomic_store_explicit(&c->ready, 0, memory_order_release);
		rk3_control_set_state(RK3_CONTROL_FAILED, status);
	}
	atomic_store_explicit(&e->audio_running, 0, memory_order_release);
	/* Closing is completed only after all aborted callbacks have returned. */
	atomic_store_explicit(&c->closing, 1, memory_order_release);
}

static void engine_tick(CFRunLoopTimerRef timer, void *info);
static void control_arm_read(struct rk3_engine *e);

static void control_rearm(CFRunLoopTimerRef timer, void *info)
{
	(void)timer;
	control_arm_read(info);
}

static void control_read_done(void *refcon, IOReturn status, void *arg0)
{
	struct rk3_engine *e = refcon;
	struct rk3_engine_control *c = e->control;
	c->input_active = 0; /* even aborted callbacks must retire their reference */
	if (atomic_load(&c->closing)) return;
	if (status != kIOReturnSuccess) { fail_engine(e, (int32_t)status); return; }
	size_t length = (size_t)(uintptr_t)arg0;
	if (length > sizeof(c->input)) { fail_engine(e, (int32_t)kIOReturnOverrun); return; }
	uint64_t acquired = mach_absolute_time();
	if (length > 0) {
		/* Publish every complete report, including replies. The app owns switch
		 * baselining; a new epoch never replays packets acquired before ready. */
		if (atomic_load_explicit(&c->ready, memory_order_acquire))
			rk3_control_publish_input(c->input, length, acquired);
		if (c->internal != INTERNAL_NONE && c->expected_reply &&
		    !c->reply_done && c->input[0] == c->expected_reply &&
		    acquired <= c->deadline) {
			memcpy(c->reply, c->input, length);
			c->reply_length = length;
			c->reply_done = 1;
		}
	}
	/* A short floor limits empty/immediate-success storms without putting a
	 * timeout on the persistent IN transfer (which would reset its toggle). */
	mach_timebase_info_data_t tb;
	mach_timebase_info(&tb);
	double elapsed_ms = (double)(acquired - c->last_read_arm) * tb.numer / tb.denom / 1e6;
	if (elapsed_ms >= 1.0) control_arm_read(e);
	else CFRunLoopTimerSetNextFireDate(c->rearm_timer,
		CFAbsoluteTimeGetCurrent() + (1.0 - elapsed_ms) / 1000.0);
}

static void control_arm_read(struct rk3_engine *e)
{
	struct rk3_engine_control *c = e->control;
	if (c->input_active || atomic_load(&c->closing)) return;
	c->last_read_arm = mach_absolute_time();
	IOReturn status = (*e->usb.intf)->ReadPipeAsync(e->usb.intf, e->usb.pipe_cmd_in,
		c->input, sizeof(c->input), control_read_done, e);
	if (status == kIOReturnSuccess) c->input_active = 1;
	else fail_engine(e, (int32_t)status);
}

static void control_write_done(void *refcon, IOReturn status, void *arg0)
{
	struct rk3_engine *e = refcon;
	struct rk3_engine_control *c = e->control;
	uint64_t transferred = (uint64_t)(uintptr_t)arg0;
	uint32_t actual = transferred > UINT32_MAX ? UINT32_MAX : (uint32_t)transferred;
	c->output_active = 0;
	if (c->output_external) {
		rk3_control_complete_command(&c->external, (int32_t)status, actual);
		c->output_external = 0;
	} else {
		c->write_done = 1;
		c->write_status = (int32_t)status;
	}
	if (atomic_load(&c->closing)) return;
	if (status != kIOReturnSuccess || actual != c->output_length)
		fail_engine(e, status == kIOReturnSuccess ? (int32_t)kIOReturnUnderrun : (int32_t)status);
}

static void control_submit_write(struct rk3_engine *e, const uint8_t *bytes,
		uint32_t length, unsigned timeout_ms)
{
	struct rk3_engine_control *c = e->control;
	memcpy(c->output, bytes, length);
	c->output_length = length;
	/* Buffer stays owned until completion, including cancellation/teardown. */
	IOReturn status = (*e->usb.intf)->WritePipeAsyncTO(e->usb.intf, e->usb.pipe_cmd_out,
		c->output, length, timeout_ms, timeout_ms, control_write_done, e);
	if (status == kIOReturnSuccess) c->output_active = 1;
	else {
		if (c->output_external) {
			rk3_control_complete_command(&c->external, (int32_t)status, 0);
			c->output_external = 0;
		}
		c->write_done = 1;
		c->write_status = (int32_t)status;
		fail_engine(e, (int32_t)status);
	}
}

static void internal_begin(struct rk3_engine *e, enum internal_command kind,
		const uint8_t *bytes, uint32_t length, uint8_t reply_command)
{
	struct rk3_engine_control *c = e->control;
	c->internal = kind;
	c->expected_reply = reply_command; /* register BEFORE asynchronous OUT */
	c->reply_done = reply_command == 0;
	c->reply_length = 0;
	c->write_done = 0;
	c->write_status = 0;
	c->deadline = after_ms(1000);
	c->output_external = 0;
	control_submit_write(e, bytes, length, 200);
}

static void audio_abort(struct rk3_engine *e)
{
	atomic_store_explicit(&e->audio_running, 0, memory_order_release);
	(*e->usb.intf)->AbortPipe(e->usb.intf, e->usb.pipe_iso_in);
	(*e->usb.intf)->AbortPipe(e->usb.intf, e->usb.pipe_iso_out);
}

static void finish_lifecycle(struct rk3_engine_control *c, int result)
{
	struct lifecycle_request *r = c->life;
	c->life = NULL;
	int was_start = r && r->action == LIFE_START;
	if (life_complete(r, result) && result == 0 && was_start)
		fail_engine(c->owner, (int32_t)kIOReturnTimeout);
}

static void reset_audio_state(struct rk3_engine *e);
static void engine_apply_rate_scale(struct rk3_engine *e);

static void engine_tick(CFRunLoopTimerRef timer, void *info)
{
	(void)timer;
	struct rk3_engine *e = info;
	struct rk3_engine_control *c = e->control;
	if (atomic_load_explicit(&c->closing, memory_order_acquire)) {
		if (!c->close_started) {
			c->close_started = 1;
			atomic_store(&c->ready, 0);
			if (!atomic_load(&e->usb_dead))
				rk3_control_set_state(RK3_CONTROL_WAITING, (int32_t)kIOReturnAborted);
			audio_abort(e);
			(*e->usb.intf)->AbortPipe(e->usb.intf, e->usb.pipe_cmd_in);
			(*e->usb.intf)->AbortPipe(e->usb.intf, e->usb.pipe_cmd_out);
			CFRunLoopTimerSetNextFireDate(c->rearm_timer, CFAbsoluteTimeGetCurrent() + 1e9);
			if (c->life) finish_lifecycle(c, (int32_t)kIOReturnAborted);
			pthread_mutex_lock(&c->request_mutex);
			struct lifecycle_request *queued = c->queued;
			c->queued = NULL;
			pthread_mutex_unlock(&c->request_mutex);
			life_complete(queued, (int32_t)kIOReturnAborted);
		}
		if (!c->input_active && !c->output_active && c->iso_in_flight == 0)
			CFRunLoopStop(e->runloop);
		return;
	}

	if (c->internal != INTERNAL_NONE) {
		if (mach_absolute_time() > c->deadline) {
			/* No request IDs exist on the wire. Reopen after timeout instead of
			 * letting a late ACK satisfy a later request with the same command. */
			fail_engine(e, (int32_t)kIOReturnTimeout);
			return;
		}
		if (c->write_done && c->reply_done) {
			enum internal_command completed = c->internal;
			c->internal = INTERNAL_NONE;
			if (c->write_status != 0) { fail_engine(e, c->write_status); return; }
			if (completed == INTERNAL_INFO) {
				struct caiaq_device_spec spec;
				if (c->reply_length < 1 + sizeof(spec)) {
					fail_engine(e, (int32_t)kIOReturnBadArgument); return;
				}
				memcpy(&spec, c->reply + 1, sizeof(spec));
				/* This codec is mode2 stereo; reject incompatible descriptors. */
				if (spec.data_alignment != 2 || spec.num_analog_audio_in != 2 ||
				    spec.num_analog_audio_out != 2) {
					fail_engine(e, (int32_t)kIOReturnUnsupported); return;
				}
				e->data_alignment = spec.data_alignment;
				e->n_streams = 1;
				e->bpp = caiaq_compute_bpp(48000, spec.data_alignment, 1);
				engine_apply_rate_scale(e);
				c->startup_phase = 1;
			} else if (completed == INTERNAL_NEUTRAL) {
				c->startup_phase = 2;
			} else if (completed == INTERNAL_AUTO) {
				(void)rk3_control_begin_epoch();
				atomic_store_explicit(&c->ready, 1, memory_order_release);
				c->startup_phase = 3;
				e->start_rc = 0;
				atomic_store_explicit(&e->thread_ready, 1, memory_order_release);
			} else if (completed == INTERNAL_AUDIO) {
				if (c->reply_length < 2 || c->reply[1] != 1 || !c->life ||
				    atomic_load(&c->life->cancelled)) {
					fail_engine(e, (int32_t)kIOReturnNotReady); return;
				}
				if (prime_playback(e) != 0) {
					fail_engine(e, (int32_t)kIOReturnIOError); return;
				}
				finish_lifecycle(c, 0);
			}
		}
	}

	if (c->startup_phase == 1 && c->internal == INTERNAL_NONE && !c->output_active) {
		/* Required neutral RK3 initialization, not a cosmetic display animation.
		 * Match app/legacy reserved payload bytes while all LEDs remain off. */
		uint8_t neutral[64] = { EP1_CMD_WRITE_IO };
		neutral[2] = 0x40; neutral[3] = 0x40;
		internal_begin(e, INTERNAL_NEUTRAL, neutral, sizeof(neutral), 0);
		return;
	}
	if (c->startup_phase == 2 && c->internal == INTERNAL_NONE && !c->output_active) {
		const uint8_t auto_msg[] = { EP1_CMD_AUTO_MSG, 1, 10, 0 };
		internal_begin(e, INTERNAL_AUTO, auto_msg, sizeof(auto_msg), 0);
		return;
	}
	if (!atomic_load_explicit(&c->ready, memory_order_acquire)) return;

	if (!c->life) {
		pthread_mutex_lock(&c->request_mutex);
		c->life = c->queued;
		c->queued = NULL;
		pthread_mutex_unlock(&c->request_mutex);
	}
	if (c->life) {
		struct lifecycle_request *r = c->life;
		if (atomic_load(&r->cancelled)) {
			if (r->stage != 0)
				fail_engine(e, (int32_t)kIOReturnTimeout);
			else finish_lifecycle(c, (int32_t)kIOReturnTimeout);
			return;
		}
		if (r->action == LIFE_STOP) {
			if (r->stage == 0) { audio_abort(e); r->stage = 1; }
			if (c->iso_in_flight == 0) {
#if RK3_USE_LOWLAT
				ll_free_all(e);
#endif
				finish_lifecycle(c, 0);
			}
			return;
		}
		if (r->stage == 0 && atomic_load(&e->audio_running)) { finish_lifecycle(c, 0); return; }
		if (r->stage == 0 && !c->output_active && c->internal == INTERNAL_NONE) {
			reset_audio_state(e); /* caller has quiesced realtime ring users */
			caiaq_codec_reset(&e->codec);
			/* Recovered NI ordering: have capture requests in flight before
			 * enabling streaming. OUT waits for the real params ACK. */
			atomic_store_explicit(&e->audio_running, 1, memory_order_release);
			if (prime_capture(e) != 0) { fail_engine(e, (int32_t)kIOReturnIOError); return; }
			uint8_t params[] = { EP1_CMD_AUDIO_PARAMS, CAIAQ_SR_48000,
				CAIAQ_DEPTH_24, e->bpp & 0xff, (e->bpp >> 8) & 0xff, 1 };
			r->stage = 1;
			internal_begin(e, INTERNAL_AUDIO, params, sizeof(params), EP1_CMD_AUDIO_PARAMS);
		}
		return;
	}

	if (!c->output_active && c->internal == INTERNAL_NONE &&
	    rk3_control_take_command(&c->external)) {
		uint64_t now = mach_absolute_time();
		if (now >= c->external.deadline_mach) {
			rk3_control_complete_command(&c->external, (int32_t)kIOReturnTimeout, 0);
			return;
		}
		mach_timebase_info_data_t tb;
		mach_timebase_info(&tb);
		uint64_t remaining = (uint64_t)(((__uint128_t)(c->external.deadline_mach - now) * tb.numer) / tb.denom / 1000000ULL);
		unsigned timeout = remaining > c->external.timeout_ms ? c->external.timeout_ms : (unsigned)remaining;
		if (timeout == 0 || c->external.length == 0 || c->external.length > 64) {
			rk3_control_complete_command(&c->external, (int32_t)kIOReturnTimeout, 0);
			return;
		}
		c->output_external = 1;
		control_submit_write(e, c->external.bytes, (uint32_t)c->external.length, timeout);
	}
}

static void *engine_thread(void *argument)
{
	struct rk3_engine *e = argument;
	struct rk3_engine_control *c = e->control;
	e->runloop = CFRunLoopGetCurrent();
	CFRetain(e->runloop); /* survives thread exit until close joins */
#ifndef RK3_ENGINE_TESTING
	engine_set_realtime();
#endif
	IOReturn status = (*e->usb.intf)->CreateInterfaceAsyncEventSource(e->usb.intf, &c->source);
	if (status != kIOReturnSuccess || !c->source) {
		c->startup_error = status == 0 ? (int32_t)kIOReturnNoMemory : (int32_t)status;
		goto exit;
	}
	CFRunLoopAddSource(e->runloop, c->source, kCFRunLoopDefaultMode);
	CFRunLoopTimerContext context = { 0, e, NULL, NULL, NULL };
	c->timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(), RK3_COMMAND_TICK_SECONDS,
		0, 0, engine_tick, &context);
	c->rearm_timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 1e9, 1e9,
		0, 0, control_rearm, &context);
	if (!c->timer || !c->rearm_timer) { c->startup_error = (int32_t)kIOReturnNoMemory; goto exit; }
	CFRunLoopAddTimer(e->runloop, c->timer, kCFRunLoopDefaultMode);
	CFRunLoopAddTimer(e->runloop, c->rearm_timer, kCFRunLoopDefaultMode);
	atomic_store_explicit(&e->running, 1, memory_order_release);
	control_arm_read(e); /* BEFORE even GET_DEVICE_INFO: OUT needs a draining IN */
	if (!atomic_load(&c->closing)) {
		const uint8_t info[] = { EP1_CMD_GET_DEVICE_INFO };
		internal_begin(e, INTERNAL_INFO, info, sizeof(info), EP1_CMD_GET_DEVICE_INFO);
	}
	CFRunLoopRun(); /* closing tick stops only when every async reference retired */
exit:
	if (c->timer) { CFRunLoopTimerInvalidate(c->timer); CFRelease(c->timer); c->timer = NULL; }
	if (c->rearm_timer) { CFRunLoopTimerInvalidate(c->rearm_timer); CFRelease(c->rearm_timer); c->rearm_timer = NULL; }
	if (c->source) {
		CFRunLoopRemoveSource(e->runloop, c->source, kCFRunLoopDefaultMode);
		CFRelease(c->source); c->source = NULL;
	}
#if RK3_USE_LOWLAT
	ll_free_all(e); /* only reached after all isoc callbacks have retired */
#endif
	if (c->startup_error && !atomic_load(&e->usb_dead)) {
		atomic_store(&e->usb_dead, 1);
		rk3_control_set_state(RK3_CONTROL_FAILED, c->startup_error);
	}
	if (!atomic_load_explicit(&e->thread_ready, memory_order_acquire)) {
		e->start_rc = c->startup_error ? c->startup_error : (int32_t)kIOReturnAborted;
		atomic_store_explicit(&e->thread_ready, 1, memory_order_release);
	}
	atomic_store_explicit(&e->running, 0, memory_order_release);
	atomic_store_explicit(&c->exited, 1, memory_order_release);
	return NULL;
}

static int lifecycle_call(struct rk3_engine *e, enum lifecycle_action action)
{
	struct rk3_engine_control *c = e->control;
	if (!c || !atomic_load(&c->ready) || atomic_load(&c->closing))
		return (int32_t)kIOReturnNotReady;
	struct lifecycle_request *r = life_new(action);
	if (!r) return (int32_t)kIOReturnNoMemory;
	pthread_mutex_lock(&c->request_mutex);
	if (c->queued || atomic_load(&c->closing) || atomic_load(&c->exited) || !atomic_load(&c->ready)) {
		pthread_mutex_unlock(&c->request_mutex);
		life_release(r); life_release(r);
		return (int32_t)kIOReturnBusy;
	}
	c->queued = r;
	pthread_mutex_unlock(&c->request_mutex);
	struct timespec deadline = wall_deadline(RK3_AUDIO_TIMEOUT_MS);
	pthread_mutex_lock(&r->mutex);
	int wait_result = 0;
	while (!r->done && wait_result == 0)
		wait_result = pthread_cond_timedwait(&r->condition, &r->mutex, &deadline);
	int result = r->done ? r->result : (int32_t)kIOReturnTimeout;
	if (!r->done) atomic_store(&r->cancelled, 1);
	pthread_mutex_unlock(&r->mutex);
	life_release(r); /* engine reference remains valid after caller's timeout */
	return result;
}

/* Reset all audio-streaming state (rings, cadence mirror, diagnostics, clock
 * telemetry) for a fresh audio_start. Does NOT touch USB / thread / MIDI. */
static void reset_audio_state(struct rk3_engine *e)
{
	e->pat_head = e->pat_tail = e->out_acc = 0;
	e->pat_stage_off = RK3_FRAMES_PER_XFER;   /* force a pop on first OUT fill */
	e->in_errs = e->out_errs = e->out_drops = 0;

	/*
	 * Capture ring empty. Playback ring empty + ungated: the engine sends
	 * silence on OUT until write_output has buffered RK3_OUT_CUSHION, then it
	 * sets pb_r = RK3_OUT_PHASE and pb_started (preserving the 4-byte phase).
	 */
	atomic_store(&e->cap_w, 0);
	atomic_store(&e->cap_r, 0);
	atomic_store(&e->cap_primed, 0);
	e->cap_settle_left = e->cap_settle_init;
	e->cap_measure_left = e->cap_measure_init;
	e->cap_meas_min = ~0u;
	e->out_fill_min = ~0u;
	atomic_store(&e->cap_fill_min, ~0u);
	atomic_store(&e->pb_w, 0);
	atomic_store(&e->pb_r, 0);
	atomic_store(&e->pb_started, 0);
	atomic_store(&e->cap_overruns, 0);
	atomic_store(&e->pb_underruns, 0);
	atomic_store(&e->pb_overruns, 0);

	atomic_store(&e->dev_frames, 0);
	e->dbg_ctr = 0;
	e->dbg_host0 = 0;
	e->dbg_devframes0 = 0;
}

/* ---- public API ------------------------------------------------------ */

/* Recompute the rate-scaled fill constants from e->rate. The reference values are
 * tuned at 48 kHz and describe fixed amounts of time / producer-consumer bite, so
 * they scale linearly with the rate. Called at open and on every set_rate. */
static void engine_apply_rate_scale(struct rk3_engine *e)
{
	e->out_cushion       = RK3_SCALE_TO_RATE(RK3_OUT_CUSHION, e->rate);
	e->out_cushion      -= e->out_cushion % RK3_BYTES_PER_FRAME;  /* keep frame-aligned */
	e->out_fill_setpoint = RK3_SCALE_TO_RATE(RK3_OUT_FILL_SETPOINT, e->rate);
	e->cap_fill_floor    = RK3_SCALE_TO_RATE(RK3_CAP_FILL_FLOOR, e->rate);
	e->cap_settle_init   = RK3_SCALE_TO_RATE(RK3_CAP_SETTLE_FRAMES, e->rate);
	e->cap_measure_init  = RK3_SCALE_TO_RATE(RK3_CAP_MEASURE_FRAMES, e->rate);
}

static IOReturn iso_track(struct rk3_iso_xfer *x, IOReturn result)
{
	if (result == kIOReturnSuccess) { x->in_flight = 1; x->e->control->iso_in_flight++; }
	return result;
}
static void iso_retire(struct rk3_iso_xfer *x)
{
	if (x->in_flight) { x->in_flight = 0; x->e->control->iso_in_flight--; }
}

int rk3_engine_set_rate(struct rk3_engine *e, int rate)
{
	if (rate != 48000 || atomic_load(&e->audio_running)) return RK3_EINVAL;
	e->rate = rate;
	return 0;
}
int rk3_engine_out_fill_setpoint(const struct rk3_engine *e) { return e->out_fill_setpoint; }
void rk3_engine_wake_controls(struct rk3_engine *e) { (void)e; /* 5ms owner timer */ }

int rk3_engine_open(struct rk3_engine *e, int rate)
{
	if (!e || rate != 48000) return RK3_EINVAL;
	if (e->control) return (int32_t)kIOReturnBusy;
	rk3_control_initialize();
	rk3_control_set_state(RK3_CONTROL_WAITING, 0);
	struct rk3_engine_control *c = calloc(1, sizeof(*c));
	if (!c) return (int32_t)kIOReturnNoMemory;
	pthread_mutex_init(&c->api_mutex, NULL);
	pthread_mutex_init(&c->request_mutex, NULL);
	atomic_init(&c->closing, 0); atomic_init(&c->exited, 0); atomic_init(&c->ready, 0);
	c->owner = e;
	e->control = c;
	e->runloop = NULL;
	e->rate = rate;
	e->start_rc = (int32_t)kIOReturnNotReady;
	atomic_store(&e->running, 0); atomic_store(&e->audio_running, 0);
	atomic_store(&e->usb_dead, 0); atomic_store(&e->thread_ready, 0);
	int status = rk3_usb_open(&e->usb);
	if (status != 0) {
		c->startup_error = status;
		rk3_control_set_state(status == RK3_ENODEVICE ? RK3_CONTROL_WAITING : RK3_CONTROL_FAILED, status);
		atomic_store(&c->exited, 1);
		(void)rk3_engine_close(e);
		return status;
	}
	if (pthread_create(&e->thread, NULL, engine_thread, e) != 0) {
		atomic_store(&c->exited, 1);
		(void)rk3_engine_close(e);
		return (int32_t)kIOReturnNoResources;
	}
	c->thread_created = 1;
	uint64_t deadline = after_ms(RK3_INIT_TIMEOUT_MS);
	while (!atomic_load_explicit(&e->thread_ready, memory_order_acquire) &&
	       mach_absolute_time() < deadline) usleep(1000);
	status = atomic_load_explicit(&e->thread_ready, memory_order_acquire)
		? e->start_rc : (int32_t)kIOReturnTimeout;
	if (status == 0 && (!atomic_load(&c->ready) || atomic_load(&c->closing)))
		status = (int32_t)kIOReturnNoDevice;
	if (status != 0) (void)rk3_engine_close(e);
	return status;
}

int rk3_engine_audio_start(struct rk3_engine *e)
{
	if (!e || !e->control) return (int32_t)kIOReturnNotReady;
	struct rk3_engine_control *c = e->control;
	pthread_mutex_lock(&c->api_mutex);
	int status = lifecycle_call(e, LIFE_START);
	pthread_mutex_unlock(&c->api_mutex);
	return status;
}
void rk3_engine_audio_stop(struct rk3_engine *e)
{
	if (!e || !e->control) return;
	struct rk3_engine_control *c = e->control;
	pthread_mutex_lock(&c->api_mutex);
	if (!atomic_load(&c->closing)) (void)lifecycle_call(e, LIFE_STOP);
	pthread_mutex_unlock(&c->api_mutex);
}
int rk3_engine_close(struct rk3_engine *e)
{
	if (!e || !e->control) return 0;
	struct rk3_engine_control *c = e->control;
	pthread_mutex_lock(&c->api_mutex);
	atomic_store_explicit(&c->closing, 1, memory_order_release);
	atomic_store_explicit(&e->audio_running, 0, memory_order_release);
	uint64_t deadline = after_ms(RK3_CLOSE_TIMEOUT_MS);
	while (!atomic_load_explicit(&c->exited, memory_order_acquire) &&
	       mach_absolute_time() < deadline) usleep(1000);
	if (!atomic_load_explicit(&c->exited, memory_order_acquire)) {
		pthread_mutex_unlock(&c->api_mutex);
		return (int32_t)kIOReturnTimeout; /* preserve EVERYTHING for live callbacks */
	}
	if (c->thread_created) pthread_join(e->thread, NULL);
	if (e->runloop) { CFRelease(e->runloop); e->runloop = NULL; }
	rk3_usb_close(&e->usb);
	pthread_mutex_unlock(&c->api_mutex);
	pthread_mutex_destroy(&c->request_mutex);
	pthread_mutex_destroy(&c->api_mutex);
	free(c); e->control = NULL;
	return 0;
}
int rk3_engine_usb_dead(struct rk3_engine *e) { return atomic_load(&e->usb_dead); }
int rk3_engine_start(struct rk3_engine *e, int rate)
{
	int status = rk3_engine_open(e, rate);
	if (status == 0) status = rk3_engine_audio_start(e);
	if (status != 0) (void)rk3_engine_close(e);
	return status;
}
void rk3_engine_stop(struct rk3_engine *e)
{
	rk3_engine_audio_stop(e);
	(void)rk3_engine_close(e);
}

/* ---- Float32 <-> S24_3BE bridges (CoreAudio IO thread) --------------- */

static inline float s24be_to_f32(const uint8_t *p)
{
	int32_t v = (int32_t)((uint32_t)p[0] << 24 |
			      (uint32_t)p[1] << 16 |
			      (uint32_t)p[2] << 8);   /* sign in MSB */
	return (float)(v >> 8) / 8388608.0f;
}
static inline void f32_to_s24be(float f, uint8_t *p)
{
	if (f > 1.0f) f = 1.0f;
	if (f < -1.0f) f = -1.0f;
	int32_t v = (int32_t)(f * 8388607.0f);
	p[0] = (uint8_t)(v >> 16);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)v;
}

void rk3_engine_read_input(struct rk3_engine *e, float *dst, int nframes)
{
	if (!dst || nframes <= 0) return;
	if (!atomic_load_explicit(&e->audio_running, memory_order_acquire)) {
		memset(dst, 0, (size_t)nframes * RK3_CHANNELS * sizeof(float));
		return;
	}
	int need = nframes * RK3_BYTES_PER_FRAME;
	unsigned r = atomic_load_explicit(&e->cap_r, memory_order_relaxed);
	unsigned w = atomic_load_explicit(&e->cap_w, memory_order_acquire);

	/*
	 * One-time startup capture drain. The capture standing fill is an uncontrolled
	 * startup offset (~490 frames ≈ 10 ms of hidden input latency) that the device's
	 * initial clock transient builds up over the first ~1 s — so we cannot drain at
	 * pb_started (too early, fill not yet established). Wait RK3_CAP_SETTLE_FRAMES
	 * past pb_started, then collapse the fill to RK3_CAP_FILL_SETPOINT once. The
	 * dropped amount is a frame multiple, so cap_r stays frame-aligned. Runs exactly
	 * once (cap_primed). read_input is the sole cap_r writer, so this is SPSC-safe.
	 */
	if (!atomic_load_explicit(&e->cap_primed, memory_order_relaxed) &&
	    atomic_load_explicit(&e->pb_started, memory_order_acquire)) {
		if (e->cap_settle_left > 0) {
			e->cap_settle_left -= nframes;   /* wait out the IN startup transient */
		} else if (e->cap_measure_left > 0) {
			unsigned hf = ring_avail(w, r) / RK3_BYTES_PER_FRAME;
			if (hf < e->cap_meas_min) e->cap_meas_min = hf;  /* find the trough */
			e->cap_measure_left -= nframes;
		} else {
			if (e->cap_meas_min != ~0u &&
			    e->cap_meas_min > (unsigned)e->cap_fill_floor) {
				unsigned drop = (e->cap_meas_min - e->cap_fill_floor)
						* RK3_BYTES_PER_FRAME;
				r = (r + drop) % RK3_RING_BYTES;   /* trough -> floor */
			}
			atomic_store_explicit(&e->cap_primed, 1, memory_order_relaxed);
		}
	}

	unsigned avail = ring_avail(w, r);
	{                                   /* windowed-min cap_fill telemetry */
		unsigned af = avail / RK3_BYTES_PER_FRAME;
		unsigned previous = atomic_load(&e->cap_fill_min);
		if (af < previous) atomic_store(&e->cap_fill_min, af);
	}
	int got = (avail < (unsigned)need) ? (int)avail : need;
	got -= got % RK3_BYTES_PER_FRAME;

	int i = 0;
	for (; i < got / RK3_BYTES_PER_FRAME; i++) {
		/* The RK3 streams its two capture channels in the opposite order to
		 * its silkscreened input jacks, so swap them here: device sample 0 ->
		 * Reaper input 2, device sample 1 -> Reaper input 1. */
		float c0 = s24be_to_f32(&e->cap_buf[r]); r = (r + 3) % RK3_RING_BYTES;
		float c1 = s24be_to_f32(&e->cap_buf[r]); r = (r + 3) % RK3_RING_BYTES;
		dst[i * 2 + 0] = c1;
		dst[i * 2 + 1] = c0;
	}
	atomic_store_explicit(&e->cap_r, r, memory_order_release);
	for (; i < nframes; i++) {          /* zero-fill underrun */
		dst[i * 2 + 0] = 0.0f;
		dst[i * 2 + 1] = 0.0f;
	}
}

void rk3_engine_write_output(struct rk3_engine *e, const float *src, int nframes)
{
	if (!src || nframes <= 0 || !atomic_load_explicit(&e->audio_running, memory_order_acquire)) return;
	int want = nframes * RK3_BYTES_PER_FRAME;
	unsigned w = atomic_load_explicit(&e->pb_w, memory_order_relaxed);
	unsigned r = atomic_load_explicit(&e->pb_r, memory_order_acquire);
	if (ring_space(w, r) < (unsigned)want) {
		atomic_fetch_add_explicit(&e->pb_overruns, 1, memory_order_relaxed);
		return;                      /* ring full: drop this block */
	}
	for (int i = 0; i < nframes; i++) {
		f32_to_s24be(src[i * 2 + 0], &e->pb_buf[w]); w = (w + 3) % RK3_RING_BYTES;
		f32_to_s24be(src[i * 2 + 1], &e->pb_buf[w]); w = (w + 3) % RK3_RING_BYTES;
	}
	atomic_store_explicit(&e->pb_w, w, memory_order_release);

	/*
	 * Startup gate: once the cushion is buffered, hold the read pointer
	 * RK3_OUT_PHASE bytes in (the proven 4-byte OUT rotation) and release the
	 * engine to start playout. Set pb_r before pb_started (release) so fill_out,
	 * which reads pb_started with acquire, sees the seeded pb_r.
	 */
	if (!atomic_load_explicit(&e->pb_started, memory_order_relaxed) &&
	    w >= (unsigned)e->out_cushion) {
		atomic_store_explicit(&e->pb_r, RK3_OUT_PHASE, memory_order_relaxed);
		atomic_store_explicit(&e->pb_started, 1, memory_order_release);
	}
}

int rk3_engine_out_fill_frames(struct rk3_engine *e)
{
	if (!atomic_load_explicit(&e->pb_started, memory_order_acquire))
		return -1;
	unsigned w = atomic_load_explicit(&e->pb_w, memory_order_acquire);
	unsigned r = atomic_load_explicit(&e->pb_r, memory_order_relaxed);
	return (int)(ring_avail(w, r) / RK3_BYTES_PER_FRAME);
}
