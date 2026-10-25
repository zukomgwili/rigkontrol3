# NI event delivery and controller ownership contract

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.23**.
This is a source-only adapter contract. It records reused original assembly,
its limits and current-owner differences; it admits no production code,
new hardware operation, audio change or security change. SIP stays enabled.
The paired [identity and specification contract](NIIdentitySpecificationContract.md)
records the device-information, specification and serial-producing fields;
this report owns event activation, delivery and controller policy.

## Evidence boundary

The source images are NIHardwareAgent 1.7.4/R199, SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`,
and NIUSBAudioDriver 2.8.0 x86_64, SHA256
`41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`.
Addresses are original unslid addresses in their respective images. Internal
object offsets, mapped event bytes and USB bytes are separate schemas.

The initial inventory reuses existing `controller174`, `controller174-extra`
and `audio280-text` exports. Those historical exports included decompilation;
they are **not** newly guarded no-analysis packets. This report's reused facts
come from selected ASM, not their C output. Original agent
`LC_FUNCTION_STARTS` envelopes bound those selected address records. The kernel
image has **no LC_FUNCTION_STARTS** command: its inventory limits use the
existing 241-entry function index and do not establish original LC caps.
Address containment does not prove the final instruction's full byte span.
No new kernel disassembly is needed merely to repeat existing evidence.

Private inventory: `.build/ni/owner-adapter-contract/events/existing-coverage.json`,
SHA256 `54f11c064a5936a787c47da13ff90089ff1b1518aba55f36e30d45040adbf66d`.
It records source/export/index/manifest hashes and the exact provenance of each
address limit. Private original listings remain ignored. The existing
[22 synchronous / 12 asynchronous disposition](SelectedBindingContracts.md#13-required-ni-clientcontrol-contracts-complete-disposition-partial-support)
and [controller reconstruction](ControllerMapping.md) remain the operation
inventory; this report does not expand every shared-device method into an RK3
requirement.

## Original activation and configuration

`GuitarRig::Controller::postConnect` (`0x100040730`) invokes controller virtual
slot +0x1e8 before tail-calling `FX2Controller::postConnect` (`0x1000390b0`).
The latter first calls `ControllerBase::postConnect` (`0x100029500`, a recovered no-op), then invokes device `subscribeToEvents`. Failure
ultimately disconnects the device and clears controller byte +0x39. When
byte +0x258 is false and either controller MIDI-count word +0xe0/+0xe4 is
nonzero, success proceeds to `triggerAsyncMidiRead`; its failure also takes
that disconnect path.

Existing `FX2Controller::configureDevice` (`0x100038bb0`) requires connected
byte +0x39, constructs a zeroed 32-byte configuration through
`0x1002ec8a0`, invokes virtual +0x1e0 to fill it, and writes only if that virtual
call returns true. It does not use the write's result to branch in this body.
`getDeviceConfiguration` (`0x100038a90`) sets the initial word to 2, clears
byte 2 and fills the ring-versus-tip and switch masks from the original pedal
objects. These are live configuration values, not an unconditional all-default
profile. `writeConfiguration` (`0x1002f1e00`) sends selector 10 with one scalar
32 and a 32-byte structure; it returns whether the call's error is zero.
The existing kernel selector/device chain sends command `0x0f` with those
configuration bytes, as recorded in the controller reconstruction.

The selected original feature initialization and callback construction retain
their conditional behavior. `FX2Controller::init` (`0x100038c80`) constructs
features, invokes its feature virtual, sets thresholds and installs analog,
digital and conditional MIDI callbacks. `RigControl3::initDeviceFeatures`
(`0x100040f60`) delegates to its base and stores word `0x0101` at features+0x2b.
The separately recovered feature constructor (`0x1002ec860`) sets feature+0x21
to 5, +0x22 to 0, +0x23 to 2 and +0x24..+0x2c to zero. Its other explicit
stores remain in the private listing; bytes it does not write are not invented
as zeros. These defaults combine with the selected feature initializer, rather
than replacing it.

## Registration, subscriptions and fallbacks

`FX2Device::subscribeToEvents` (`0x1002f1cf0`) requires device byte +0x10 and
then calls `OSImpl::subscribeToEvents` (`0x1002f0d20`). That body:

- Creates a receive port with argument `0x39`, wraps it in CFMachPort, creates
  its run-loop source and adds it to the current run loop.
- When device feature byte +0x9c is nonzero, attempts async selector 8;
  then feature +0x9a selects 7; feature +0x98 selects 9. A failed attempted
  subscription returns false through its original diagnostics path.
- Calls scalar selector 5 with three 64-bit scalars: 1, analog threshold
  (feature+0x9b when enabled, otherwise 2), and ERP threshold
  (feature+0x99 when enabled, otherwise 5). This call's return is ignored in
  the selected body. The kernel sends command `0x0b` with the three bytes.
- Has an additional memory-map/selector-10 branch only for product `0x0808`.
  That branch does not establish an RK3 sample/pad requirement.

`OSImpl::asyncConnect` (`0x1002f1020`) accepts initial selectors 6..10,
prepares the original three-word async reference and calls
`IOConnectCallAsyncScalarMethod`. Separately reviewed original relative-i32
tables at `0x1002f160c` (20 bytes) and `0x1002f1620` (44 bytes) establish
the following selected arms. Values in the size column are the actual second
scalar, not an invented event-capacity interpretation.

| Initial selector | Buffer offset / scalar | Callback | Error fallback |
| --- | --- | --- | --- |
| 6 | OSImpl+0x174 / `0x280` | `MidiDataCallbackTS` | Selector 0, same buffer/scalar, `MidiDataCallback` |
| 7 | OSImpl+0xa4 / `0x88` | `AnalogInputCallbackTS` | Selector 1, OSImpl+0xac / `0x80`, same callback |
| 8 | OSImpl+0x7c / `0x28` | `DigitalInputCallbackTS` | Selector 2, OSImpl+0x84 / `0x20`, same callback |

The reference contains the callback and context; these selected arms send two
input scalars and no synchronous output. On **any initial call error**, the
source takes its selected fallback; it does not restrict fallback to one named
unsupported-operation error. It returns true on accepted initial or fallback
submission, otherwise false. Inputs outside 6..10 return false before that
initial call. No new callback thread, polling or successful-error substitution
is implied. Selector 9/10 table entries remain original shared-device branches,
not newly assigned RK3 capabilities.

Existing `createOSImpl` (`0x1002f1c80`) allocates the object, installs its
original base/type and owner, then calls `bzero(object+0x68,0x38c)`. This covers
the initial timestamp-prefix fields. Analog/digital fallback passes a buffer
eight bytes later while its callback still reads the original prefix. Fresh
prefixes start zero; this does **not** establish that a prefix remains zero
after an earlier timestamped delivery or that every fallback error transition
has the same state. Do not replace the original retained prefix with a new
receipt timestamp. The kernel's interpretation of the second scalar must be
mapped from its actual selected method, independently of the agent's value.

## Raw USB event dispatch

Three separately admitted four-byte entries at kernel `0x256c`, `0x2570` and
`0x2578` prove commands 3, 4 and 6 using the source's unsigned report-byte
index and signed-offset base `0x2560`. They select existing arms `0x23ae`,
`0x23fc` and `0x244a`; no other command-table entry was read for this event
proof. The selected transformations are:

| Raw command | Original transform and direct target |
| --- | --- |
| 3 | Advance the raw pointer by 1; pass 16 bytes and Boolean true to `processAnalogInputData` at `0xaec0`. |
| 4 | Advance by 1; pass 8 bytes and Boolean true to `processDigitalInputData` at `0xb030`. |
| 6 | Port is byte+1, count is byte+2, payload starts +3; a count above 61 exits before the selected subscriber's processing. Accepted data calls `processMIDIData` at `0xade0`. |

The 16/8 values are fixed source call arguments; they do not establish actual
USB packet length or a zero-filled tail. A new adapter must not silently add
a padding or short-packet rule to make these source calls fit its history.

These arms visit the corresponding original 32-entry subscriber arrays at
provider+0x1d0/+0x2d0/+0x5d0 and surround each nonnull client call with that
client's virtual +0x20/+0x28 operations. Their common exit `0x224c` tests
provider byte +0x8ea; when zero it invokes pipe virtual +0x1d0 with the same
input memory object, requested size 64 and completion storage provider+0x198,
then performs the original unlock and owner virtual +0x28 return path.
No new owner generation, replay, queue lead or audio change is inferred from
this reader continuation.

## Analog and digital mapped event delivery

Kernel `analogInputRead` (`0x9da0`) rejects absent provider with `0xe00002d9`,
zero supplied address/length with `0xe00002c2`, an existing mapped request with
`0xe00002d5`, and failed descriptor creation with `0xe00002de`. It copies the
original 64-byte async reference, sets an initial-delivery flag and registers
with the provider. Failed provider registration invokes descriptor virtual slot +0x28, clears
its stored pointer and returns `0xe00002d5`. Timestamped wrapper `0xa1b0`
first sets the timestamp flag, then tail-calls the same registration body.

`digitalInputRead` (`0x9ea0`) has the corresponding provider/address/length/
duplicate checks. It creates the primary descriptor and 512 secondary buffers,
with buffer allocation failure returning `0xe00002bd`, copies its async
reference, initializes flags/indices and registers. These are original source
operations; this report does not infer that every early-error path rolls back
all preceding allocations. Timestamped wrapper `0xa1d0` sets its flag and
uses the same registration.

`processAnalogInputData` (`0xaec0`) and `processDigitalInputData` (`0xb030`)
require provider, stored async reference and mapped descriptor. A false incoming
flag also requires their initial-delivery flag; accepted delivery clears that
initial flag. The timestamped path calls imported
`clock_get_system_nanotime` **during mapped delivery**, selects the low 32 bits
of its seconds output and the 32-bit nanoseconds output, and passes these
to descriptor virtual +0x1e8 as four-byte operations at offsets 0 and 4,
and passes payload with offset 8. The paired agent callbacks consume that
prefix/payload layout. A descriptor virtual +0x1c8 return at most 11 rejects
that timestamped operation. The source calls descriptor virtual +0x1f0 with zero
and tests its result, passes offset/address/count arguments through +0x1e8,
then invokes +0x1f8 with zero. It uses the +0x1c8 return and low-byte input
count as the payload limit. Untimestamped payload starts at offset zero.
These exact indirect operations are retained; names such as prepare, length,
writeBytes or complete require a separately established original class ABI.
The raw dispatch mapping above supplies 16 analog bytes or 8 digital bytes;
it does not make the current owner's full USB record the event schema.

The selected successful primary deliveries invoke `sendAsyncResult64` with
result zero and no scalar arguments. Their stored descriptor/reference fields
remain set; they are not the MIDI one-shot request described below. Both clear
their initial flag
before checking virtual +0x1f0's result. A failed check or too-small timestamp
span produces no async-result notification in these bodies. The prefix/payload
+0x1e8
return values are not checked; no new successful-byte-count rule is invented.
Analog ignores the async-result return; digital only follows its original
diagnostic branch for a nonzero result. The digital path also
has the original armed/ring-buffer behavior. `digitalInputArm` (`0x9ad0`)
passes a secondary buffer address/length to primary-descriptor virtual +0x1e8
at offset zero, then invokes the original async-result call.
No required default-RK3 caller of that optional arm operation is established
by the selected agent subscription path; do not invent one or discard its
recorded behavior.

Agent `AnalogInputCallbackTS` (`0x1002f1740`) reads the two U32 values at
OSImpl+0xa4/+0xa8, computes `u64(first)*1000000000+second`, and passes the
payload beginning at +0xac to the stored callback. Digital counterpart
`0x1002f17e0` does the same at +0x7c/+0x80, payload +0x84. These bodies do
not inspect their result-status argument or add a new callback-buffer length
check. The narrow primary-source comparison below supplies a qualified legacy-x86
timebase relationship; a seconds/nanoseconds shape alone supplies none.

## Conditional agent MIDI and separate DIN path

Timestamped agent callback `0x1002f1650` walks records with stride 80,
reads two U32 values at record+72/+76 and applies the same nanosecond arithmetic.
Untimestamped callback `0x1002f19a0` uses stride 72 and calls
`Timestamp::now` (`0x1002cf3e0`) once for the callback batch. That wrapper
tail-jumps to `Timer::getCurrentTicks` (`0x1002c0df0`), whose recovered body
calls imported `UpTime`, passes its return to `AbsoluteToNanoseconds` and
returns that result. It is not a direct `mach_absolute_time` return. The installed
SDK's DriverServices.h declares `UpTime()->AbsoluteTime`,
`AbsoluteToNanoseconds(AbsoluteTime)->Nanoseconds` and
`Nanoseconds=UnsignedWide`; the declarations remain present with macOS 10.8
deprecation. This does not establish a scalar ARM return ABI, field packing,
rounding or a need to replace the original calls. Retain the typed boundary
until a separately planned ARM build/type proof.
[Installed DriverServices.h](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/CarbonCore.framework/Versions/A/Headers/DriverServices.h:60).
Both classify a raw port byte <=63 as enum value 1, otherwise 2, and mark only
the final record with the batch-final Boolean. Those enum values must not be
renamed DIN/controller roles solely from their numbers.

The kernel timestamped MIDI wrapper `0xa190` enables its flag and tail-calls
`MIDIRead` (`0x9c50`). The latter stores a single mapped request, treats the
second scalar as event capacity and uses wrapping `capacity*72` mapping-length
arithmetic. Thus the agent's raw `0x280` scalar and the kernel's capacity
interpretation are recorded separately; no presumed eight-record capacity
is silently substituted. `sendAsyncMIDI` (`0xab60`) chooses stride 72 or 80 from the flag,
scans logical ports 0..127 and emits at most 64 queued bytes per filled record.
The local record writer sets only byte 0 for the raw port, a U32 count at +4,
and the copied bytes at +8; timestamped records additionally set +72/+76.
Bytes +1..+3 and any unused payload tail are not written by this fill path,
so no port-word or zero-tail value is fabricated. Positive output completes
with one scalar record count, then clears the async
reference, invokes descriptor virtual +0x28 and clears the stored request.
With no queued data, the mapped request remains pending.
The MIDI writer removes or shortens the port queue **before** testing the
primary mapped descriptor's +0x1f0 result. Failure there does not restore those
bytes or increment the completed-record count; an accepted copy's +0x1e8
return is not checked before count advancement. A failed port-descriptor
+0x1f0 check skips that port. These source error/loss ordering differences
must not become a new replay or success policy. `processMIDIData` (`0xade0`)
returns without queuing if its provider is absent or the raw port byte is
>=128. Otherwise it uses the low-eight-bit count, discards that port's existing
queue before appending when the resulting count would reach or exceed 256,
and attempts delivery. Its port-buffer +0x1f0/+0x1e8 results are unchecked
before updating the queued count. The selected dispatch caller does not test
a returned success/error status; no original return type is inferred.
Timestamped records are stamped during this copy/delivery loop, not at USB
report receipt. Preserve the observed allocation/stride distinction; do not
silently fix it or infer safe arbitrary capacities.

`FX2Controller::processMidiData` (`0x100039870`) preserves the original
port/mode/current-focus conditions, dispatches a current-client message only
under its selected role/mask branch, and rearms `triggerAsyncMidiRead` at the
final-record boundary. `OSImpl::triggerAsyncMidiRead` (`0x1002f1a80`) attempts
selector 6 through `asyncConnect` and retries immediately: its counter starts
at -1, increments after failure and repeats while <=3, hence up to five
attempts. No new timer or backoff is inferred. `sendMidiEvents`
(`0x1002f23a0`) sends the supplied 72-byte structure through selector 12 when
its raw enum argument equals 1, otherwise selector 4; it returns call-success.
These are distinct fake-input and hardware-output operations, not one generic
USB-write API.

The original CoreMIDI plug-in's DIN path is separately documented in
[MIDIDriver.md](MIDIDriver.md): it uses async selector 0 and synchronous
selectors 3/4, with its own parser and delivery timestamps. It does not convert
analog/digital pedal reports into MIDI. Controller assignment/focus policy
belongs to the original agent, not a replacement DIN parser.

## Teardown and ownership

Existing agent `FX2Device::OSImpl::disconnect` (`0x1002f08c0`) clears +0x3f8,
removes/releases its run-loop source, deallocates its receive Mach port,
closes the service connection and clears those handles. It then tail-calls recovered `USBDevice::OSImpl::disconnect` (`0x1002f5290`),
which releases nonzero registry object +8 through `IOObjectRelease`, clears
+0x10, invokes `delete[]` on nonnull +0x18 and clears it.

Factory `onDeviceRemoved` (`0x1002f60c0`) drains `IOIteratorNext`, searches its
original tree for the exact returned registry handle and invokes the original
removal callback only under the observed owner/node predicates. The callback
receives that handle and the original filter address; it runs before the
matched entry is released/erased. The source also calls
`clearHIDDeviceInfoCacheFor` (`0x1002f34f0`) and releases the iterated handle.
That shared cache helper and generic container operation stay opaque; no HID
feature or direct user-client close is invented from their names. Worker
errors alone must not be relabeled physical removal.

Kernel `clientClose` (`0xa550`) invokes its original virtual continuation,
calls the original analog, digital, ERP, MIDI and other deregistration helpers,
invokes provider virtual +0x28 and clears its pointer. `disable` (`0xa820`)
clears its provider between the original lock-like helper calls when present.
`finalize` (`0xa620`) invokes virtual +0x28 on stored MIDI/analog/digital
descriptors and secondary buffers and clears their fields and async-reference
entries. Selected other descriptors first receive virtual +0x1f8(0), then
+0x28. Literal slots/order remain authoritative; no new original class-vtable
name mapping is claimed. No synthetic successful cancellation callback is established by
these selected bodies. The exact framework virtual continuations and callback
quiescence guarantees are not supplied by resemblance to current worker code.

The previously reviewed [agent recognition/lifetime contract](NIAgentRecognitionContract.md)
remains authoritative for focus, explicit removal versus invalidation, state
notification and shutdown/destructor asymmetry. The recovered `setMIDITransportEnabled(bool,bool)` (`0x100029bd0`) stores
the requested Boolean at controller+0x248. Its optional second Boolean selects
mutex +0x118 wrapping a recursive call with that Boolean false. Enabling with
no stored +0x228 client scans the original queue and stores the first role
`Midi` or `SMap` client under mutex +0x198. Enabling with a client already
stored leaves that pointer; disabling clears it under that mutex. This is an
original client-policy operation, not a USB command or a new profile.
`chooseNewFocusClient` and the original higher mapping/assignment continuations
remain retained boundaries; the current driver's independent default policy
does not stand in for them.

## Timebase qualification

The parent's bounded primary-source check uses Apple's **xnu-2782.40.9 i386**
implementation. In that version `clock_get_system_nanotime` samples
`rtc_nanotime_read` and divides nanoseconds into seconds/remainder;
`mach_absolute_time` samples the same source and the documented timebase ratio
is 1:1. This supports a legacy-x86 non-calendar nanotime relationship. It
does not establish the exact running macOS implementation, an ARM 1:1 ratio,
or equivalence of USB receipt and subscriber-fulfilment placement/scheduling.
[Apple legacy i386 clock implementation](https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-2782.40.9/osfmk/i386/rtclock.c).

That tag's LP64 `clock_sec_t` is an unsigned long, while nanoseconds are
unsigned int. The NI event format is consequently grounded in the actual
assembly's **low32(seconds)** extraction, rather than a claimed 32-bit API
seconds pointer. The paired callback's nanosecond arithmetic remains the
observed wire-consumer behavior.
[Apple legacy clock declarations](https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-2782.40.9/osfmk/kern/clock.h).

## New bounded evidence

Each new agent packet uses a fresh private copy of the preserved no-analysis
import, read-only processing and `DisassembleCommand.enableCodeAnalysis(false)`.
It checks all target absence/disjoint caps before mutation, preserves every
baseline function/instruction and requires the exact function delta and full
instruction spans within original LC caps. Headless exit zero alone is not
a pass. Independent technical review passed before new semantics were used.

| Packet | Exact result | Evidence SHA256 |
| --- | --- | --- |
| A | 861→867 functions; 381 full instruction spans; 6 ASM / 0 C | `61d023dd1ab5e809c1f11dae7d2beb6e713c38a102442037d72af8bf52b7df4f` |
| B | 861→862 functions; 6 full instruction spans; 1 ASM / 0 C | `6bfe1d47558ec5573730b4bc254e740fb820d6da4d10f51a89f81f23ec82784e` |
| Async tables | Exact 20+44 bytes; VM mapping, hashes and signed destinations tied to existing ASM | `12215721ce68ce7cc32816bb63eb078fd6edfb08a838d160989539588d4735bc` |
| Kernel import bindings | Exactly 14 original external relocation/nlist/name associations; no new body bytes | `c4203a6259849ebd032af214c2a76636f38b3359eefc0538f77f69512877d8f3` |
| Kernel event entries | Three independent four-byte spans for commands3/4/6; signed targets into existing selected ASM | `fb649996a0605ac6d0ef97405e6b113472a7439848af9166e874f2cc90fa61a3` |
| Existing initial-state followup | Adds only old `createOSImpl` ASM; historical/address-only qualifications retained | `8abe07d097f8bf5cbe832596d3a9ac712558fb0784a7719fa6ed3a07ea2810e5` |

The fourteen direct bindings establish the selected calls to
`clock_get_system_nanotime`, `sendAsyncResult64`, descriptor creators and
`bcopy` from the original kernel's relocation/symbol metadata. Synthetic
Ghidra external addresses or old C labels alone are not used as that proof.
Descriptor virtual slots remain literal where no original slot-name ABI was
proved. Source images and preserved projects remain unchanged.

## Current-owner differences and stops

| Current boundary | Difference that an adapter must resolve before coding |
| --- | --- |
| `NativeUSB.c:report_complete` | Stores a monotonic sequence and `mach_absolute_time` at worker receipt before report parsing. This is not the original delivery-time pair or proof of equivalent timebase. |
| `nrpt` | Up to 32 raw reports per call from a 256-record non-destructive history, independent cursors and explicit overrun. It is not analog/digital persistent mapped registration, digital arm/ring semantics or one-shot MIDI capacity/completion. |
| `initialize_device_commands` | Unconditionally writes default `0f`, enables default `0b`, sends `04` and requires a baseline before connection readiness. Original configuration and subscriptions belong to the agent's conditional post-connect lifecycle. No original equivalence is established for the extra readiness gate. |
| Current CoreMIDI `input_tick` | Parses 03/04 using the native default controller policy and independently flushes LEDs; it is separate from NI's original assignment, focus, conditional MIDI and configuration policy. An agent adapter must avoid duplicate producers/LED owners. |
| Current first-snapshot suppression | It is a current policy, not proof of NI's original initial-event behavior. Original initial flags, command dispatch and controller state must determine the required baseline. |
| Current generation/retirement guards | Useful ownership mechanics, but they do not prove original subscription errors, retained-buffer lifetime, cancel callbacks or termination order. |
| Current command/result boundary | Native Boolean results and HAL's unspecified-error collapse do not preserve the original selector/subscription errors by themselves. Required failures and completion/no-completion ordering need an explicit typed mapping. |

## Planned distinguishing conformance cases

These are a plan for a later admitted seam proof, **not executed tests or new
test code**. Expected values come from the original call/field operations;
unknown class-slot or typed-return details must remain explicit in fixtures.

| Case | Original-derived observation and negative control |
| --- | --- |
| Raw event transformation | Check commands3/4 at payload+1 with counts16/8, and command6 port+1/count+2/payload+3 with the observed61-byte bound. Check the selected MIDI callee's absent-provider and port>=128 no-queue branches, low8 count and the caller's untested returned status. Treating every full USB report as the same typed event, adding zero-tail assumptions or accepting a62-byte MIDI payload must fail. |
| Submission and fallback | Exercise initial 6/7/8 success and several nonzero call errors. Error must select 0/1/2 with the exact table's address/scalar/callback changes; limiting fallback to unsupported errors, changing buffer offsets or supplying event counts in place of raw scalar values must fail. Inputs outside 6..10 must not submit. |
| Prefix lifetime | Check the initial object's exact bzero region, then timestamped delivery followed by fallback on the same object. Fresh prefixes are zero; retained prefixes must not be reset or replaced with a new receipt time. Do not assume every subsequent fallback still has zero timestamps. |
| Timestamp representation and placement | Use seconds with nonzero high32 and known nanoseconds; check NI's low32 extraction and the paired `u64(seconds32)*1000000000+nanoseconds32` operation. Give ingress and fulfilment different fixture times so an ingress stamp fails. Keep `UpTime`→`AbsoluteToNanoseconds` typed; raw ARM tick return, an assumed 1:1 ratio or guessed UnsignedWide packing must fail. |
| Persistent versus one-shot delivery | Repeated analog/digital primary deliveries retain stored registration fields. A positive MIDI batch completes with one scalar count and clears its request; no queued data keeps it pending. Check raw agent scalar values, original capacity*72 arithmetic, 72/80 writer stride and batch-final rearm separately; a presumed eight-record normalization must fail. |
| Registration and delivery failures | Exercise the recorded absent-provider, zero address/length, duplicate, descriptor/secondary allocation and provider-registration errors. Check initial-flag clearing before the +0x1f0 result, no async-result notification on its failure/small timestamp span, unchecked +0x1e8 results and original MIDI queue-shortening-before-target-failure order. Automatic replay or an invented completion must fail. |
| Close, cancellation and removal | Assert the original source/remove-source/port-deallocate/service-close/base-handle order and field clearing, plus the factory's exact-handle and callback predicates. Do not generate or replay a completion merely because cancellation/reconnect occurred. Keep callback-storage retirement and real host quiescence as separate acceptance gates; handle aliases or transient worker errors must not manufacture removal. |
| Ownership, mapping and DIN | Exercise original `Midi`/`SMap` queue selection, enable/disable pointer +0x228 and the independently recovered focus rules. Require one mapped-event/controller/LED policy owner and keep physical DIN distinct. Duplicating the native default mapper beside the retained agent, or treating fake-input selector12 as a USB send, must fail. |

The proof must not alter scheduling, retries, clock maintenance or recovery
to make a vector pass. Hardware/unchanged-frontend acceptance remains a later
explicit gate.

No change to NI's audio ring, queue calendar, clock maintenance or recovery
follows from this event contract. The parent recorded the identity disposition
and exact event packets before new recovery. This report combines qualified existing ASM reuse with seven
new guarded functions, two exact agent tables, three four-byte kernel event
entries and fourteen original import bindings. It is not runtime, frontend
acceptance or complete driver parity.
