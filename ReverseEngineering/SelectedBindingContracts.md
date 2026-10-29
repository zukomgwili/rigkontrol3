# Selected public binding: implementation contracts

Evidence date: **2026-10-25 UTC**. Pre-change source: **6fae284**. Authority: **rigkontrol3-40h.6**, with the
capability matrix in `.13` and deployment choice in `.17`. The user requires
**SIP enabled**. The selected baseline is the existing AudioServerPlugIn,
user IOUSBHost and native CoreMIDI binding. This is not another architecture
assessment. No code, build, installation, runtime, USB or security operation
was performed by this bounded check.

**Contract disposition is complete for the scopes below; implementation and
physical parity are not.** In particular, the public time/frame interfaces are
adequate for the original clock-state correction in `.14`. DMA visibility,
primary publication timing and worker deadlines stay separately unresolved;
they do not prevent a deterministic correction that does not change them.
A later required-contract contradiction stops its owning card, not every
unrelated port change. Nothing here closes `.6`'s remaining physical evidence.

## Evidence and execution boundary

The reference is the pinned NI audio2.8.0 image with SHA-256
`41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`.
Original addresses below are image-relative. Existing C/ASM evidence and tests
are catalogued in [the audit](ParityAudit.md) and its
[capability matrix](Audit/parity-matrix.json). Complete proprietary bodies are
not reproduced. No new web search was required: the selected **SDK27.0** and
existing originals/reports provide the contracts used here.

Current code:
[HAL](../Driver/Audio/RK3AudioDriver.c),
[USB owner](../Driver/NativeUSB.c),
[Host adapter](../Driver/NativeHostUSB.m),
[ISO engine](../Driver/NativeAudioISO.c),
[ring](../Driver/AudioRing.c),
[schedule](../Driver/AudioSchedule.c).
All USB calls and callback retirement belong to the USB worker CFRunLoop.
IOUSBHost's raw completions arrive on its serial dispatch queue and are
published to that worker; interest notifications use a different queue.
CoreAudio IO/timestamp calls use the host's deadline thread. The existing RT
paths pin engine lifetime and avoid USB calls, allocation and mutex waits.
Those are execution/ownership facts, not a proof of kernel-workloop timing.

Primary local SDK references (absolute paths identify the inspected version):

- [IOUSBHostObject.h](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostObject.h):31–45 execution/interest queues;351–360 frame/time getter;390–413 mapped allocation.
- [IOUSBHostDefinitions.h](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostDefinitions.h):18–21 AbsoluteTime;63–96 transaction offsets/Wrap/status/count/timestamp;177–184 shared-list option.
- [IOUSBHostPipe.h](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostPipe.h):74–84 clear side effects;223–236 bulk enqueue;324–350 absolute-frame ISO enqueue.
- [AudioServerPlugIn.h](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/CoreAudio.framework/Headers/AudioServerPlugIn.h):71–89 configuration/RT contract;218–238 positioned IO times;335–340 WriteMix;987–1015 zero timestamps/seed.
- [AudioHardwareBase.h](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/CoreAudio.framework/Headers/AudioHardwareBase.h):688–707 scoped latency/safety offset;1082–1084 stream activity.

## 1. Clock maintenance: adequate for `.14`; pre-change difference

**Original B12:** `updateWallTimePerUSBCycle@0xb880` obtains a stable
frame/local-uptime/frame sample. It handles a missing anchor, including a
failed-boundary-probe fallback, and preserves the integer interval estimator.
`timestampTimerCallback@0xbbb0` updates that estimator, increments the byte
counter, probes a boundary only when the incremented value reaches8, resets
the counter and rearms the timer for128ms after its work. This is not an
anchor probe on every128ms estimator update.

`getAnchorFrameAndTimeStamp@0xba80` has a1.1ms boundary budget. On success it
records the **first observed USB frame+1** and **local uptime after the
boundary loop**. A later framework frame/time pair is not automatically the
same observation. Initial engine setup sets counter8 at`0x575c`; wake at
`0xbcb0` sets8 and interval10^10, clears output and follows the same update /
increment / threshold sequence. `performAudioEngineStart@0x6433` directly
invokes the timer callback; that function's instructions do **not** reset
`+bd90`. Do not reset8 on every existing-engine restart without caller evidence.
A first missing-anchor update and the threshold path may both request anchors.

**Actual public counterpart:** `frameNumberWithTime:` returns controller USB
frames without microframes; IOUSBHostTime is AbsoluteTime. Together with local
`mach_absolute_time` and timebase conversion it can supply both original
bracketing and boundary samples. Current `rk3_host_bus_frame` is worker-only.
Public CFRunLoop timer/service facilities can execute the state machine on
that owner; their scheduling is best effort, not NI kernel deadline parity.

**Difference at the checked baseline:** `refresh_anchor` rate-limits to128ms but combines the
estimator and boundary probe, saves the latest framework pair, and is invoked
from output/zero-event activity. ISO start also forces a probe. No public API
contract inspected requires these algorithm/cadence changes.

**Admission:** adequate for a narrow restoration of original clock state,
read/call ordering, estimator and boundary cadence, after the `.14` card records
the exact source-backed scope. It need not wait for a DMA/publication or TooOld
solution. Runtime wake-event delivery stays in `.10`; pure wake-state tests do
not prove the host invokes that event. Priming/queue side-effect placement stays
in `.16`. Coordinate their call sites rather than silently importing changes.

**Red/green and stop:** at the actual engine/time-source seam, script USB frame
and local-time observations and record estimator, boundary and timer calls.
The current every128ms-probe path must fail a multi-tick negative control.
Check first create/start, at least two eight-tick groups, existing-engine
restart without an invented reset, explicit wake, missing-anchor fallback,
unchanged-frame samples, boundary timeout, source failure/removal and the
original rounding/overflow cases. Preserve B11 timestamp vectors and public
clock monotonicity/seed rules. No real sleeps or hardware are needed to prove
call/counter arithmetic. Stop if a caller's reset/order cannot be recovered or
an implementation would change queue lead, decoding or failure policy to make
the clock test pass. An installed clock observation later tests timing quality;
no evidence currently says this correction fixes TooOld.

## 2. Host positions and PCM: adequate representation; scoped parity

**B3–B5:** NI `clipOutputSamples@0x6ab0` and `convertInputSamples@0x6e90` use
host-requested positions, original scaling, mode2 lanes/carry and ring wrap.
The HAL uses `mInputTime/mOutputTime.mSampleTime`; the API explicitly gives the
start in the device timeline. It passes those positions to `_capture_at` and
`_render_at`; RT capture reads atomic coherent stereo views, not a consuming
FIFO. Canonical host conversion and the publicly advertised Float32 format
must remain distinguished from NI's packed24 stream metadata.

**Disposition:** adequate for positioned conversion/ring implementation at the
verified stereo48k format. Existing `AudioRingTests` addressed/split/wrap/carry
vectors and HAL negative sample-time tests are relevant. Host-format equivalence
and other original rates remain B2/B3 requirements, not an ARM exemption.
**Test/stop:** identical original-derived output bytes for varied block splits,
repeatable addressed input, negative initial times and ring crossings; reject
nonfinite/out-of-range times without mutation. Stop any change that replaces
positions with a FIFO or an elapsed-host-time counter.

## 3. Live mapped DMA backing: adequate storage API, physical timing pending

**B7/P3:** `prepareMemory@0x59d0` and `queueOutIrp@0x7620` retain shared output
backing, including split ring ranges. The chosen `dataWithCapacity:options:`
contract maps controller-optimized IOBufferMemoryDescriptor storage and promises
no DMA bounce. Transaction offsets/Wrap can address the same live ring.
The adapter retains interface, storage and lists through ordered completion;
close/alternate changes are Busy until those owners retire.

**Disposition:** adequate to implement same-backing representation and lifetime;
not proof of safe physical late writes, cache/DMA visibility or prefetch bounds.
Existing deferred-pointer, wrap, retirement-before-wake and delayed-abort tests
prove software facts only. **Test/stop:** those seam tests must retain backing
identity across enqueue, later positioned writes, wrap and cancellation, with
no snapshots or early free. Actual audio acceptance must later demonstrate
continuity with the original write-ahead/calendar. If the API demonstrably
snapshots/bounces or cannot satisfy required visibility, stop this row; no
guessed cache flush, queue lead or silently changed ring is admitted.

## 4. Primary polling/publication: explicit unresolved timing boundary

**B10/P4:** NI `readFrameList@0x6f60` and low-latency IN use whole-eight-result
publication before callback delivery. Shared-list allocation maps into kernel
and task; the modern fields describe completion status/actual counts. Current
capture polls all eight statuses before using the denominator and serializes
reset/decode; complete/partial stereo views are atomic.

**Disposition:** supports shared representation and completion-result use.
Exact NI primary-interrupt cadence and inter-field publication ordering before
the framework callback are not established by allocation, volatile reads or
an acquire fence alone. No private interrupt flag is a solution. **Test/stop:**
retain delayed-callback primary-capture, partial-batch denominator and racing
reset/lifetime negative tests. Physical timing evidence is still required for
a primary-equivalence claim. If observed publication cannot meet a required
read deadline, stop B10 and identify the exact conflict. This uncertainty does
not block `.14`'s local estimator/counter restoration.

## 5. Calendar, callbacks and queue ownership: software contract adequate

**B6/B8/C2/P5:** NI queues64 IN/128 OUT, eight transactions per request, with
absolute USB frames and device-led counts. Public ISO enqueue accepts absolute
start frames and caller lists. The adapter imposes acceptance-order callbacks
per pipe, including error/abort/zero-count requests; failed enqueue takes no
position. Busy/pending retires before user callback for safe requeue, and owner
references survive the entire ordered drain. Endpoints remain independent.

**Disposition:** adequate for original calendar/slot ownership implementation.
`.12` proves a real reversed-arrival deadlock and the ordered fix at both engine
and actual adapter seams. `test_lifted_calendar_and_deferred_dma`, empty-output,
rejected/reentrant/reverse-abort cases cover further ownership. None establishes
system callback deadlines. **Test/stop:** preserve target oldframe+64, initial
calendar/counts, every zero-count lifetime and no early list reuse under reversed
arrivals. Keep `.11`'s observed late-target fault open. Stop if proposed changes
alter lead/counts or replay accepted/failed transfers to hide a deadline miss.

## 6. Output/zero timestamps and host metadata: arithmetic adequate

**B11/B13:** NI `writeHandler@0x7ec0` publishes completed OUT start position;
`takeSimpleTimeStamp@0x8100` and `generateTimeStamp@0xb790` use exact input
boundary byte fractions and all eight actual counts. Current saved coordinates,
integer helper and HAL GetZeroTimeStamp preserve those inputs and return a
sample/AbsoluteTime/seed mapping, as the public host expects.

**Disposition:** adequate for current position/fraction arithmetic; anchor
acquisition remains contract1. Period16384 is a clock boundary, not a host buffer
quantum or signal-latency measurement. NI68/136/136 numbers at48k match current
constants, but mapping legacy sampleOffset136 to both modern SafetyOffset
scopes is still unverified host behavior. **Test/stop:** preserve variable-size
wrap, before-anchor fraction, zero-total, backwards-time and seed/reset vectors.
Metadata tests must independently inspect each scope and actual host write-ahead.
Do not infer total latency from USB horizon or cure a metadata mismatch by
changing NI's clock formula. Unchanged metadata does not block `.14`.

## 7. Priming/start order: representable, correction still pending

**B9:** NI `readHandler@0x7a80` changes priming after slot63's actual IN requeue
and OUT submission; clip marks written during priming without PCM conversion.
Current code applies the priming-ended plan before those actual calls.
The selected API permits the original order; it does not require this difference.

**Disposition:** adequate binding, unmatched implementation order owned by `.16`.
**Test/stop:** log the actual seam submissions and state/mark-written side effects;
RED must detect the current earlier transition, including rejected calls and
restart guards. Preserve initial nonfatal-IN/STOP/real-format retry policy and
no phantom clock readiness. Stop if the original branch order is unresolved.
No change to `.14` clock math may silently rewrite this separate sequence.

## 8. Stop, removal and panic: scoped lifecycle adequate

**C3–C6/P7:** NI decode/input/output errors, immediate/100ms panic scope and
pause/resume differ from actual removal. Current host configuration handshake
has documented stop/Perform/restart order. The owner retains EP1 and USB
generation for audio panic, aborts/drains audio, and pins mappings/RT readers
until quiescent. Cached actual service termination triggers full cleanup.

**Disposition:** adequate to implement scoped error/lifetime/restart behavior;
existing `.8` installed recovery and NativeUSB/Host lifetime/panic tests support
it. This is not steady-state acceptance. **Test/stop:** keep errors diagnostic,
no failed command replay, stable controls/generation during audio-only recovery,
no rearm after close, no free before delayed abort or racing RT read, actual
NoDevice full cleanup and clock reseeding. Stop if host configuration cannot
establish quiescence or recovery starts before drain. No whole-device reset is
an acceptable substitute for a recoverable audio panic.

## 9. Inactive output: signal available, last-client equivalence unproved

**B16/P8:** NI `writeHandler@0x7ec0` invokes its clear after output was
written and DWORD `[output-stream object + 0xa8]` is zero. The
[source-correlated x86 layout proof](NIOutputBindingResolution.md) now identifies
that field as UInt32 `numClients`, matching NI's offset and `0x150` allocation.
The clear-before-latch-reset ordering is instruction-proved. Public StreamIsActive describes
an enabled stream doing IO; StartIO/StopIO identifies device clients, not a
proven per-direction last-client count. Current output-active atomic drives the
original clear helper.

**Disposition:** the clear bytes and explicit stream-enable propagation have
tests; the original output-client trigger remains unproved in the current M3,
`rigkontrol3-40h.3` (the historical `.10` work was absorbed). The bounded
[output lifecycle decision](Evidence/Build16OutputLifecycleDecision.json)
separates two acceptance obligations:

- **Public routing:** the SDK requires NULL data for an unused IOProc stream.
  The frozen strict helper still fails that assertion before output starts;
  this is neither an NI ring-clear test nor permission to relax the assertion.
- **NI output ownership:** previously written output plus zero clients on that
  output stream must reach NI's literal clear, then survive output restart
  without stale replay while input remains active. The client-only observer
  did not establish that ownership event: output callbacks stopped, input
  progressed, and global activity stayed 1 in all 90 samples. Its sampled
  negative result does not prove a hidden consumer or a missing driver hook.

**Test/stop:** retain the original three stop/restart and no-stale-replay gate.
Synthetic stream-property changes establish software propagation only.
Device StartIO/StopIO arguments and operation-capability queries do not by
themselves supply the missing per-direction ownership witness. Stop this row
at that limit; no idle timeout, invented client count, forced global inactivity
or new output-mixing policy is justified. It does not block independent gates.

The [build 17 native trace](Evidence/Build17HostClientLifecycleValidation.json)
now establishes the observed aggregation: two same-process IOProcs used one
native device client. ReadInput and WriteMix continued after the public output
callback stopped; native StopIO appeared only when input stopped too. Remove
was first observed in the post-exit snapshot. This narrows the missing binding
to a real output-stream ownership signal; it does not make the device client's
combined lifetime a per-output count. Original ring-clear/replay acceptance
remains open. The previous non-measurement build 16 is restored.

The [offline original-trigger check](NIOutputClearTrigger.md) proves that
Apple 203.3's stream counter counts attached client buffers: kernel user-client
Start/Stop attaches/detaches them while retaining registrations, and
registration/freeing can also change the count. This is a mixed lifecycle,
not a public IOProc count. The follow-up resolves the compiled field identity;
the inspected published AudioServerPlugIn contract supplies no persistent
per-output attachment/detach event to bind its automatic zero-count trigger.
Device lifetime, explicit stream disable and processing boundaries retain their
distinct scopes. No synthetic counter patch is justified. The clear/replay and
public NULL gates remain open in M3; exact historical runtime revision is unclaimed.

## 10. Sleep/wake: original state operations portable, host event mapping open

**C7/P9:** NI device power`0x1fe0` enters/awakens engine; `enterSleep@0xbc50`
invalidates anchors/measurement and cancels the timer; `awake@0xbcb0` resets
interval/counter, clears output and restarts maintenance. Current binding
expects host StopIO/StartIO and treats suspend as non-removal. No source proof
establishes every required original event/reset through those callbacks.

**Disposition:** state transformations implementable; real event mapping and
host keeping input/IO active remain `.10`. **Test/stop:** explicit state-fixture
wake tests can verify `.14` clock semantics, while a separately admitted physical
sleep/wake verifies delivery, clear, seed and next controls without USB churn.
If host lifecycle omits a required event, stop that mapping with concrete
evidence. Do not add a speculative EP1 wake rearm or reset the device.

## 11. Scheduling and current TooOld: no equivalent guarantee established

**C1/P2:** original kernel workloops versus default pthread/dispatch/runloop
have different scheduling domains. Public callback queue serialization and
frame APIs do not promise the old bound. The recorded build6 TooOld includes
54.54ms raw-to-worker and17.70ms enqueue wall gaps; those do not identify
CPU work, blocking or scheduling as the cause.

**Disposition:** enough execution/ownership structure to continue individually
admitted corrections; not enough to claim timing parity or select QoS as a fix.
`.11` owns one discriminating branch and the recorded stable-operation predicate.
**Test/stop:** use its exact admitted evidence and error predicate; any eventual
implementation must keep the original calendar and pass actual stability.
Stop a measurement that cannot distinguish its hypothesis or requires unrelated
priority/lead/backend changes. Do not make `.6` depend circularly on `.11` to
admit the software contracts already evidenced here.

## 12. Remaining controls/formats/diagnostics are not silently passed

B1/B2 rate/default/format changes, B14 input gains, B15 direct-monitor commands
and C8 original statistics remain their existing capability owners. The public
HAL supports described formats/properties and a configuration handshake, but
that alone does not implement missing original values, settings or private
client contracts. `.2` supplies the separately required consumer/schema/lifetime
mapping; root merges that disposition into this report. No settings/display or
feedback capability becomes optional merely because its old consumer had a GUI.

P6/`.9` still records the selected user API's broader clear/abort semantics;
its tested retirement gate is contingent on that API, not NI false-clear parity.
Clock maintenance requires only frame/time reads and does not require another
command recovery design. Required original behavior stays tracked until it is
implemented, proved inapplicable, or explicitly excepted by the user.

## 13. Required NI client/control contracts: complete disposition, partial support

The complete **22 synchronous / 12 asynchronous** operation packet is recorded
in `.2`, using the pinned audio/MIDI images, NIHardwareAgent1.7.4 call sites,
[the controller reconstruction](ControllerMapping.md), current
[NativeControl](../Driver/NativeControl.h), [NativeClient](../Driver/NativeClient.h),
[Controller](../Driver/Controller.c) and [CoreMIDI](../Driver/MIDI/RK3MIDIDriver.c)
source. No direct modern Guitar Rig/Controller Editor call is inferred merely
from a method name. The ledger distinguishes attached RK3 requirements,
proved no-ops/other-product paths and applicability-unknown shared methods.

The selected public boundary can carry typed functional operations. It does
**not** register the old NI IOService or externalMethod ABI. Current `ninf`
returns240-byte CFData information; `nrpt` uses a16-byte query and2848-byte
response with up to32 records of88 bytes from256 retained reports; `ncmd`
uses a72-byte command and16-byte generation lease. Only validated DIN07 and
the fixed LED05 command are currently admitted. No arbitrary USB escape is
exposed. Source-side and caller type/size/version/lease checks protect ownership;
objects are copied/retained/released across the public CFPropertyList boundary.
The old110-byte info,14-byte spec,72/80-byte event schemas and mapped callback
lifetimes are different contracts, not restored by coincident structure sizes.

| Original selectors/role | Native functional disposition at checked baseline | Continuing owner/stop |
| --- | --- | --- |
| S0/S1 I2C | No counterpart; RK3 consumer applicability unknown | `.2`: prove applicable consumer/schema before register access |
| S2 digital write | Default local/incoming LEDs supported; four programmable display bytes missing | `.2`: preserve display/assignment role; no GUI project by default |
| S3 info / S18 spec | Native topology available; complete original fields, identity and NI connection ABI missing/unresolved | `.2`/`.1`: enumerate required fields/consumers, do not claim NI recognition |
| S4 DIN write | Ordered immediate native transport; no future scheduling; queued admission/error semantics differ | `.2`/`fyx`: original byte/error/lifetime vectors and physical DIN still required |
| S5 auto reports | Fixed recovered defaults implemented; live settings interface and original activation order missing | `.2`/`.16`: preserve owner/configuration/registration behavior |
| S6 analog LEDs; S7/S17 LCD | No counterpart; RK3 applicability unproved | `.2`: do not mistake these for the required digital/display bytes |
| S8/S9 buffer size; S11 serial write | Selected original wrappers are successful no-ops | No invented queue-size/serial control follows from helper names |
| S10 thresholds/config | Recovered all-tip/continuous default only; per-pedal tip/ring/switch settings missing | `.2`/`.7`: coherent configuration, persistence, calibration and physical acceptance |
| S12 fake MIDI/monitor | Mapped Controller role exists; arbitrary injection/monitor subscription ABI missing | `.2`: retain confirmed conditional agent consumer requirements |
| S13/S14 direct monitor | Original command0e/low-byte SET and command12/shared-cache GET now have the typed native `ndmo` binding; physical routing meanings remain unverified | Existing `.1`: [direct-monitor contract](DirectMonitorContract.md); installed/physical qualification stays in `.3`, without guessed mode labels |
| S15/S16 channel gain | Unity capture only; original callable software gain missing | `.15`: exact Float32/schema/channel/lifetime plan before code |
| S19/S20 user data | No counterpart; applicable RK3 requirement unproved | `.2`: no firmware/storage writes inferred from names |
| S21 digital arm | Native baselines/history differ; original caller/buffer ABI absent | `.2`: preserve required event/baseline function without claiming emulation |
| A0 DIN receive | Native parsing/lifetime exists; original buffer/timestamp point and subscription ABI differ | `.2`/`fyx`: error/loss/time vectors plus physical DIN |
| A1/A7 analog; A2/A8 digital | Raw/default functionality supported; original mapped callback/time ABI absent | `.2`/`.7`: nondefault/toe/external and required consumer contracts remain |
| A3/A9 ERP; A11 encoder | No proved default RK3 requirement | `.2`: applicability unknown, no automatic rotary subsystem |
| A4 serial read | Original successful no-op | No missing serial function inferred |
| A5/A10 sample side channel | Confirmed agent branch is PID0808, not attached1940 | No RK3 pad feature inferred; revisit only with caller evidence |
| A6 timestamped MIDI | Portable codec exists, required conditional agent subscription is not implemented | `.2`: codec/public MIDI is not legacy service compatibility |

Default CC mapping comes from the original agent/Map subsystem, not the DIN
plug-in. Native built-in calibration88–648 and tested binary LED feedback remain
supported, but arbitrary assignment/profile feedback, selectable settings,
old profile/preference import and NI focus/identity behavior remain incomplete.
A host must supply effect-state MIDI; the driver does not discover unsent state.
The retired bridge/MainStage checks are not fresh-native host acceptance.

**Adequacy for the next changes:** `.14` changes no client interface or control
value and is independent of these missing functions. Software capture gain is
a second small representable gap: original`88f0/8930` copies exact four-byte
Float32 patterns, default1, and`6e90/c650` multiplies each input after conversion.
The setter/getter have distinct original channel bounds. A standard normalized
CoreAudio volume scalar is not automatically equivalent: no original clamp or
finite/range validation was recovered. Typed CFData can retain bits, but `.15`
must record exact schema, numeric domain, validation/errors and RT publication
before coding. Direct monitoring, display and pedal modes are separate plans.

**Stop:** any affected implementation must cite its original operation/consumer,
exact values/order/error/lifetime, selected transport and distinguishing test.
Missing legacy NI recognition stays missing/unresolved, not waived by SIP or
generic MIDI success. If a required functional/client contract cannot be
represented, stop that owner row and record the exact blocker; do not silently
omit it or start an editor/shim/backend project. Physical pending and a known
missing callable function remain different statuses. This contract check closes
neither `.2`'s full implementation nor combined reliable-operation acceptance.

## Bounded conclusion and later verification

The `.6` contract-entry check is satisfied for the narrow implementation scopes
identified as adequate; unresolved rows remain open. **`.14` can proceed to its
recorded original-derived clock correction and red/green tests without waiting
for unrelated DMA/publication or final stability proof**, once its card captures
the precise start/restart/wake/read-order boundaries above and root completes
the requested `.2` check. This is no blanket coding or runtime authorization.

After a specific code change is admitted, the existing offline gate commands
are `sh Tools/build-native.sh test` and `sh Tools/build-native.sh sanitize`;
they were not run during this audit. New clock seam tests must first expose the
old cadence/sequence error; existing timestamp, lifetime, ring and calendar
regressions remain unchanged unless the scoped correction requires an explained
fixture update. Actual installed tests still need their own bounded card.
The original functions and required externally visible behavior are retained
as requirements; a proved public-contract incompatibility stops its owner card
and returns the exact decision to the user. SIP remains enabled.

## Source implementation checkpoint: candidate0.2.2/build7

The entry checks above preceded coding. Beads `.14` then admitted the exact
clock restoration; `.15` admitted software input gain with its precise public
schema and per-engine ownership. Those scoped corrections are implemented.
The full14-target strict ARM gate and the same14-target ASan/UBSan gate pass,
as do focused old-behavior RED/green regressions and static analysis of the
five changed core/adapter paths. Independent source reviews passed after the
clock's transient-error counter advancement was corrected against instructions.

Clock now retains NI's stable sandwich/boundary distinction, cold bootstrap,
integer filter,128ms-relative/eight-tick phase and ordinary restart state. Both
default Mach conversions use exact integer scaling. Gain preserves all64 raw
Float32 slots, original asymmetric channel predicates, unity bypass and one
post-convert stereo snapshot tied to the borrowed engine context. The gain
property is a native functional interface, not legacy NI service emulation.

These are source/software results. **Installed0.2.1/build6 is unchanged**;
no new hardware result, clock deadline guarantee, real wake-event delivery,
physical gain observation, NI-private client compatibility or TooOld solution
is established. `.6`/`.2` and `.14`/`.15` remain open for their outstanding
implementation/installed/physical requirements. Other rates, direct monitor,
settings/display, priming/startup and unresolved contract rows remain tracked.
No old evidence or original functionality is excluded by these two corrections.
