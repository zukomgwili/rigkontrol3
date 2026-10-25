# Native USB transport

`Driver/NativeUSB.h/.c` supplies the reusable native owner of the RK3 USB
interface, with `NativeHostUSB.h/.m` supplying the current IOUSBHost binding. It derives from the fresh diagnostic probe and the NI evidence
recorded in `AudioDriver.md` and `MIDIDriver.md`. It does not use the earlier
bridge application or a public RK3 driver implementation.

`rk3_usb_open` creates one detached worker and returns immediately. The
worker enumerates only VID `17cc` / PID `1940`, refuses ambiguous accessible
devices, and retries absent/failed devices. Like original NI `initUSB` at
`0x8e0`, it reapplies advertised USB configuration1 even when already
configured, closes the device and verifies readback before claiming the
newly instantiated interface. The retained legacy parent-device handle is
closed before the single IOUSBHost child-interface claim. It opens interface0 without
seizure, selects advertised alt1, validates bulk EP1 IN/OUT and isochronous
EP2 IN / EP6 OUT, and starts its persistent 64-byte command reader before
querying device information. Connection is published only after a real
command1 reply validates stereo input/output and wire mode2.

The verified reply is:

```
01 0d 00 06 00 03 09 29 02 02 00 00 01 01 02
```

Its observed topology is audio 2-in / 2-out, MIDI 1-in / 1-out, mode2. The
first audio backend supports 48 kHz stereo Float32 host data only, through
`NativeAudioISO` and the independently reconstructed codec/scheduler.
After device-info validation and before creating the audio engine, the
worker sends the exact RK3-specific NI initialization
`05 00 40 40 00 00 00` (`setupTimerCallback`, `0x365d`..`0x3691`). Its purpose
remains unknown. The `.16` source correction attempts it once and ignores a
nonfatal write failure, matching NI's unchecked send at `0x3691`. No command5
acknowledgement is invented. Actual removal/cancellation still stops use of
the interface; a pending timed-out write keeps its storage and prevents a later
command from overwriting it. These are ownership guards, not new NI retries.

The same source correction restores the unchecked startup-clear/read policy:
`initUSB` attempts IN then OUT clears at `0x0d84/0x0d99` without testing their
returns, and `initHardware` attempts the initial Read64 at `0x1038` before its
one INFO command at `0x123f`. Recoverable clear rejection no longer skips the
other clear; recoverable read-admission rejection no longer suppresses INFO.
A rejected read owns no pending buffer and remains suspended. The current
bounded information-reply wait still cannot manufacture a reply or readiness.
That wait and fixed topology admission remain unresolved A7 differences; they
are not claimed ARM requirements. These corrections pass actual-module offline
regressions and sanitizers; installed build8 is unchanged.
Current startup also merges the separate NIHardwareAgent controller lifecycle
into connection readiness. That unconditional activation is not part of the
original audio-only setup and awaits parity review. It supplies the exact 33-byte
`0f 02 <31 zero bytes>` configuration, subscribes with `0b 01 02 05`, and
queries current digital inputs with command04. Connection generation is
published only after a bounded report4 of at least three bytes arrives.
The low nine raw switch bits are cached; `NativeInfo.digital_snapshot`
contains those bits and bit31 indicates validity. Reconnection clears the
known state and obtains a new baseline before publishing the connection.

## Control and report contract

All interface operations and callback delivery belong to the worker's
CFRunLoop. Public control calls enqueue heap-owned bounded requests and
wait for actual completion. Caller timeouts do not free requests still
owned by the worker. Expired commands/start requests not yet begun are
cancelled; active startup writes and ACK waits honor the outer deadline.
If startup finishes after cancellation, its ISO work is stopped and the
session fails rather than reporting successful audio start.

EP1 reads/writes use asynchronous IOUSBHost bulk transfers. Writes snapshot
command bytes into independently owned bounded storage; audio never uses this
copying path. Waiting for completion pumps the worker CFRunLoop, keeping the
persistent read active. Full byte count is required. Timeout/cancellation
aborts OUT; storage survives until the actual callback retires.

Recovery follows original commandReplyDispatch/sendCommand rather than
turning every pipe error into a whole-device reconnection:

| Original event | Retained behavior |
| --- | --- |
| Read Aborted (`0xe00002eb`) | Retire without rearm. |
| Read NotResponding (`0xe00002ed`) | Abort command IN then OUT; suspend reader without rearm. |
| Read PipeStalled (`0xe000404f`) | Clear IN, rearm after retirement. |
| Other read error | Record error and rearm. |
| Write PipeStalled / USBTransactionTimeout (`0xe0004051`) | Clear IN then OUT, return failure once; do not replay the command. |

NI recovery uses `ClearPipeStall(false)`: it also completes outstanding I/O
and clears the controller halt/toggle, while suppressing the device endpoint
ClearFeature request. The current public IOUSBHost `clearStall` aborts affected
pending I/O and resets endpoint halt/toggle; it exposes no Boolean to suppress
that device request. The missing choice is not whether to retain/drain pending
buffers. Modern kernel `clearStall(bool)` retains that original choice. Within the chosen binding, the retirement logic
tracks an interrupted read, preserves any successful report already delivered,
and permits exactly one replacement read after retirement. It does not reset
the device or replay failed MIDI/LED data. Startup still clears both ends, as
NI did. A genuine service-termination notification updates a cached connected
flag; transient pipe errors and suspend/resume notifications are not treated
as device removal.

The original wake path unsuspends the USB device and restores audio clocks;
no EP1 rearm occurs there. A speculative power-triggered read retry has not
been added. Sleep/wake and the lifetime after NotResponding remain physical
validation requirements, not established guarantees of the current binding.

The exposed command API accepts NI command7 MIDI output and only the exact
LED frame `05 00 40 40 00 <mask> <toe 0|1> 00 00`. It validates
port/count/length or every fixed LED field and uses bounded 200 ms USB writes. Generation-qualified
commands keep their lease in the queued request; the worker rechecks it
immediately before the actual write. Old MIDI requests therefore cannot
cross a disconnect/reconnect. Other initialization/configuration commands
stay private to the worker. Arbitrary firmware, IO and report configuration
writes are not exposed. Controller-to-MIDI mapping belongs to its separate
native policy component.

Internal command9 retains strict ACK=1 for the real sample rate. Its rate-ff
stop sentinel accepts a bounded ACK, including the observed zero result,
after a successful write: NI `performAudioEngineStart` ignores that initial
stop result before configuring the real rate. A missing ACK or failed write
still makes the command worker return failure. The engine ignores that
initial stop result, as NI does, before attempting the real format; it does
not ignore real-format rejection, active owner cancellation or device loss.
Individual initial IN failures are likewise logged while the 64-slot attempt
loop continues (the NI restart guard suppresses submissions after panic).
The owner still requires a real valid clock before exposing started audio.

The last 256 incoming command reports form a non-destructive history. Each
has a session-wide monotonic sequence and a `mach_absolute_time()` receipt
timestamp. Separate clients keep independent cursors through the fixed
`NativeControl.h` records. `NativeClient.h` wraps these records in CFDataRef
property-list values and qualifiers for supported cross-process CoreAudio
marshalling; None does not describe opaque bytes. Reconnect clears report contents before arming the
new reader, increments connection generation after validation, and keeps
the global sequence increasing. A new generation cannot replay old MIDI.
History overrun is explicit in each returned batch.

## Original specification snapshot: SPEC14

The read-only **`nspc`** (`0x6e737063`) DEVICE/global/main custom property
publishes the original NI S18 specification: exactly the 14 opaque bytes
already accepted from command01, without its command byte. For RK3
VID17cc/PID1940 there is no PID2305-only correction. A read sends no USB
command, starts no owner and performs no audio, controller, calibration or
LED operation. Existing `ninf`, `nrpt`, `ncmd` and `ngai` records are unchanged.
This is a native functional API, not the original NI private IOKit ABI or a
complete NI application-recognition implementation.

The qualifier and returned value are owned CFData property-list objects;
property-size discovery returns `sizeof(CFPropertyListRef)` without requiring
a qualifier. Fixed-width scalar fields use the existing ARM host byte order.

| Record / offset | Field |
| --- | --- |
| Query16 /0 | U32 version1 |
| Query /4 | U32 reserved0 |
| Query /8 | U64 nonzero expected generation |
| Reply32 /0 | U32 version1 |
| Reply /4 | I32 native errno status: 0, ENODEV or ESTALE |
| Reply /8 | U64 generation |
| Reply /16 | Original raw14 |
| Reply /30 | Two reserved zero bytes |

On status0, the reply contains the requested current committed generation and
all14 original bytes. They are not decoded, swapped, filtered or normalized;
a later accepted all-zero INFO refresh is valid data. ENODEV takes precedence
when specification data is unavailable or the owner is closing. ESTALE means
that an available cohort has a different generation. Both error envelopes have
generation0 and zero raw/reserved bytes: these bytes are **invalid data**, not
a successful zero specification. Native errno is distinct from CoreAudio
OSStatus and original NI IOReturn values. A valid domain-error envelope returns
CoreAudio noErr; clients must inspect the envelope status.

Pending INFO belongs to the current physical connection attempt. It becomes
available atomically with the existing generation commit; a commit without
pending INFO remains unavailable without changing connection success. A later
accepted INFO on that same committed attempt publishes or refreshes all14 at
the same generation. Close, known physical transport loss and attempt retirement
revoke publication before teardown/drain. INFO received during retirement
cannot republish data. The existing mandatory callback drain still prevents an
old interface's callbacks from crossing into a new attempt. Audio-only
stop/start, panic/recovery and generic diagnostic state changes retain the
physical cohort.

`rk3_usb_copy_spec` checks and copies under the existing session mutex. It
returns EINVAL for a NULL session/output or generation0 and writes the caller's
14-byte buffer only on success. Callers must retain ownership of the session
through the call. The new HAL getter alone uses the existing `control` gate to
pin its selected session, then releases that gate before allocating CFData.
The copy is a coherent cached snapshot, not an instantaneous physical-unplug
probe or a promise that the connection remains live afterward.

Malformed wrappers, types, sizes, version/reserved fields or generation0 are
CoreAudio transport/validation errors. The HAL preserves its existing
written-size convention while leaving the payload output unchanged on those
errors. NativeClient validates a temporary reply's length, version, reserved
bytes, recognized status, success generation and canonical failure shape before
assigning the caller's result. Transport/shape failures leave that result
unchanged; a validated ENODEV/ESTALE envelope may be returned as such.

This slice is verified offline through the public owner getter and the actual
NativeClient → HAL → NativeUSB path, including staging, same-generation
promotion/refresh, retirement/drain, generation changes, read-only metadata,
error/output guards and audio-only retention. The hardware-boundary fixtures
also check that reads add zero USB/ISO/control operations. The reviewed plan is
[`NIOwnerAdapterImplementationPlan.md`](NIOwnerAdapterImplementationPlan.md),
and implementation/results are tracked in Beads `rigkontrol3-40h.24`. Passing
these software checks does not establish installed-driver or NI application
acceptance; those remain separate project work.

## Audio and lifetime

Audio start requires NI parameter acknowledgement, real completed-output
progress and an exact input ring-boundary timestamp. The clock exposes OUT
start position/USB time plus input zero sample/time and a seed combining USB
generation with audio clock generation. Reads recheck the published engine
and connection around the snapshot; callback receipt time is not substituted
for a missing hardware anchor.

`NativeAudioISO` uses the original live mapped ring, host sample positions and
input-led calendar. Render/capture/clock allocate nothing, take no mutex and
issue no USB calls. Capture polls mapped completion data and reads the
requested ring positions instead of draining a FIFO. Explicit shared lists
require macOS 26.4+. Their allocation and completion-update contract do not
establish exact NI primary-interrupt publication timing; that remains unverified. See `AudioDriver.md` for exact byte placement, whole-IRP
publication, zero timestamps and original 68/136/136 latency/offset values.

NI audio panic is distinct from device removal. The owner preserves EP1,
connection generation and controller state while the host stops/restarts audio
through its documented configuration handshake. Decode errors request immediate
panic; input TooOld and output-slot exhaustion use 100 ms. Six other failed
IN attempts return their diagnostic without inventing panic. Failed OUT keeps
its schedule slot busy. Errors remain observable even without generation
churn. Start cannot overtake a pending Stop/drain; audio ACK failure does not
permanently fail the shared controller connection. Actual device removal
still disconnects and discovers a new generation.

The registered panic callback is copied under the state mutex and invoked
outside it. Unregistration waits for an already captured call; a callback
cannot synchronously unregister itself. The HAL queues its host notification
and returns immediately, so worker command service continues while audio
buffers drain. Generation checks reject stale notifications. Output-active
state is published to each new engine before exposing it to host callers.

Close relinquishes the client's reference and requests worker cleanup.
`rk3_usb_close_wait` waits a bounded interval for CLOSED before relinquishing
that reference. The worker prevents rearming, aborts pending I/O and drains
callbacks. Delayed cancellation retains storage and worker ownership rather
than freeing live buffers. Mapped ISO storage is released only after audio
callbacks/RT calls quiesce, before alternate restoration and interface close.
Real-time readers pin the engine; retired CPU objects are reclaimed only when
those readers drain. Retirement is capped at four engines. The IOUSBHost
adapter retains each interface, mapped buffer and transaction list through
completion delivery; close and alternate changes return Busy while these
owners remain. A cached, separately retained run loop protects completion
publication against immediate worker-side retirement.

## Software input gain: candidate0.2.2

The original engine's64 raw Float32 slots initialize to unity. Setter/getter
bounds follow the original distinct predicates; the attached RK3 still has two
inputs/two outputs. Capture applies one coherent stereo gain snapshot **after**
packed-to-Float32 conversion, to the actual returned caller frames only. It
never changes packed ring storage, output render, sample positions, controller
state or unread-tail zero filling. Unity preserves all sample bits without
multiplication. No clamp, dB mapping or finite-value filter is introduced.
Raw NaN/Inf/signed-zero gain storage is exact; cross-ISA arithmetic payloads or
FP flags are not claimed identical.

Gain state belongs to the existing published engine context and retires with
it. RT readers pin one context and use that engine's gains even if a new USB
generation is published during conversion. A new context initializes to unity;
same-context Stop/Start and audio-only recovery retain gains. Non-RT GET/SET
validates the current context/generation and commits under the same short gate
used for publication/unpublication. No RT mutex/allocation/IPC or new global
reader-drain wait is added. No disk-persistence behavior is invented.

The native functional API is a new DEVICE/global/main CFPropertyList custom
property **`ngai`**, not an emulated NI service/selector ABI:

| CFData record | Fields | Exact size |
| --- | --- | --- |
| Query qualifier | U32 version1, U32 zero-based channel, U64 generation |16bytes |
| Value | Query fields, U32 raw Float32 value_bits, U32 reserved0 |24bytes |

GET returns an owned CFData Value; NativeClient validates/copies/releases it.
SET requires matching Query/Value identity and a nonzero current generation.
Malformed type/size, wrong version/reserved, invalid channel and stale/offline
requests fail without mutation. Numeric value_bits are not restricted. A
successful software SET commits before returning; changed-only notification
runs outside driver/session locks. This operation sends no USB command and
does not widen the MIDI/LED command whitelist.

`Tools/rk3-input-gain.c` is a diagnostic for the **installed candidate**, using
ordinary CoreAudio/NativeClient only. `get CHANNEL` reads; explicit
`set CHANNEL U32_BITS` commits and checks exact readback. It refuses an absent
or ambiguous native provider, performs no retry, claims no USB interface and
starts no audio. Only compilation/`--help` is exercised before installation.
Real cross-process and finite-gain audio validation remain in Beads `.15`.
Direct monitoring is a separate unimplemented function; software gain does not
provide hardware monitor routing.

## Verification

The full transport/ISO/codec/scheduler library builds as ARM64 with strict
C11 warnings treated as errors. Clang static analysis passes.
`Driver/Tests/NativeUSBTests.c` runs without USB enumeration or hardware
access, with AddressSanitizer and UndefinedBehaviorSanitizer. It covers
history overflow and independent cursors, reconnect without old-report
replay, sequence continuity, cancellation without rearming, queued request
ownership after timeout, stale command leases, command/port/length policy,
addressed capture, audio-only panic with stable controller generation, transient
EP1 clear/rearm without command replay, no write after active-request expiration/cancellation,
and valid-clock startup gating versus timeout/cancellation/engine errors.
It also verifies pinned retirement and successful/timed-out close waiting.

These checks establish software invariants. Earlier versions demonstrated capture and controller input, but build 4 failed
audio continuity. Version 0.2.1/build 6 is now installed; a longer silent run
still fails with a late request. Current physical results are
recorded in `HardwareValidation.md`; that run alone does not establish clean
audible signal flow, DMA timing, DIN MIDI or recovery.
