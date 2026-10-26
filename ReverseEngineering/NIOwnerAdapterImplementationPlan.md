# Smallest owner-adapter implementation plan

## Current implementation-plan update after .30

The exact typed information-binding plan is now independently reviewed and
recorded in [NIInformationImplementationPlan.md](NIInformationImplementationPlan.md).
Its Beads design SHA256 is `a6d46f382e5ff27f5477b623952d9b0f20141e12ec12add01f5118584d0ebc6c` (19,331 bytes).
`.30` completes planning only; `.31` remains open and unimplemented. This
supersedes the earlier “still needs its exact plan” and next-step wording below.

The next operation uses the existing generation-qualified identity/SPEC14 cache,
original readiness waits, CF conversion, sequential owned-field copying and
exception behavior. The only shared-header edits planned are C/C++ assertion
spelling and one byte-pointer cast. No driver algorithm, installed bundle,
audio timing/recovery or security setting changes in this planning step.
Original NI application recognition and the remaining `.2` gates are incomplete.
The frozen `.28` admission record and `.29` source result below remain historical
source authority; neither is re-executed here.

## Source-gate update after .28 planning

The selected `.29` inbound StringUTF8 and owned-field replacement source proof
is now recovered; see [the exact contract](NIStringUTF8Contract.md). The frozen
`.28` plan below remains the admission record. Its old `.29` unexecuted status
is historical and superseded by that result. No ARM binding code is admitted
by source-proof completion: `.2` still needs its exact status/cohort/lifetime
and typed implementation plan. Outgoing `wstrToUTF8`, runtime and full NI
application recognition remain separate gates. No driver/runtime/security
change occurred in this proof.

## Current .28 decision after hardware identity validation

Authoritative Beads plan SHA256: `68ef4c736e99ee5f2c4c270eb5a5fec97432fa3fea960dbba8b7ab16be0ea284` (10,395 bytes).
Independent Spec and Scope/Standards reviews passed before this report update.
The current decision below supersedes historical next-step/status wording.

Date: 2026-10-26. Beads authority: rigkontrol3-40h.28; baseline 1c0a1d8.
This is planning only. No production/test code, source-body extraction, build,
signing, installation, NI application, endpoint, device/audio/MIDI/service or
security action is admitted or executed by this planning pass. SIP stays enabled.

### Decision and necessity

Keep the selected single NativeUSB/HAL owner and unchanged NI frontends. The
next eventual functional operation is the REAL const
FX2Device::OSImpl::getDeviceInfo path 0x1002f0950 -> 0x1002f0970, which requests
strings=true and fills the original USBDeviceInfo storage. Consume the existing
nusb physical identity and nspc SPEC14 operations; add no driver property,
110-byte IOConnect emulator, new USB owner, frontend server or runtime project.
Do not substitute a strings=false-only milestone for the required path.

The unavailable legacy kernel deployment/private service under the recorded
SIP/signing constraints necessitated the already selected owner access seam.
Native ARM typed objects/calls replace x86 C++ layouts at that seam; this is
not evidence that ARM requires new identity, Unicode, retry or policy behavior.
Original frontend wire framing and NI device behavior remain the contract.

.24/.26 implemented these native inputs; .27 physically verified installed
0.2.4/build9, exact12 files, same-owner generation1->2, full identity/SPEC14,
unplug ENODEV and stale-generation rejection. Its result manifest is
458515b4df24d5c7ccc9f804a3a6441fa740cddfa0fb37f6443e1f7743c1c751.
That all-success ASCII run does not prove NI's downstream string conversion,
NI errors, policy, frontend recognition or audio timing acceptance.

### Exact next gate: .29 source proof before any binding code

rigkontrol3-40h.29 will recover the selected StringUTF8 entry 0x1002ae6f0 and
only indispensable conversion/owned-assignment behavior used by the actual
strings=true information path. The original agent is NIHardwareAgent1.7.4/R199,
SHA256 f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258,
unslid base0x100000000. The separate serial-output wstrToUTF8 entry0x1002ae490
remains an explicit LATER gate before instance/serial publication. It is not
included merely because it appears in the same chain. The already-proved
signed-byte ASCII widening 0x1002ae5f0/0x1002ae600 is reused, not re-recovered.

.29 phase A is metadata-only: verify the exact original image and preserved
manifest/index provenance, locate the root's existing aliases/LC_FUNCTION_STARTS
start and next bound, and inventory any ALREADY recovered applicable packet.
Do not read/decode a new instruction body, table or callee in this phase.
Use original metadata bounds, never Ghidra-created starts or a guessed byte cap.
Reused ASM needs its source/baseline/span/analysis-option guards verified before
interpretation; a prior export name alone does not establish applicability.

Before phase B, record an exact immutable packet in Beads: selected original
address interval(s), full-instruction/file-backed bounds, source and baseline
hashes, existing-ASM reuse or new-read disposition, exact tool/analysis-off
options, output preservation/hash checks, allowed call/data bindings, questions
and finite stop. Independently review that packet before body work. No new body
read is authorized by this .28 plan or by .29's mere existence. A wrapper,
indirect target, conversion or assignment/allocation callee outside the exact
packet is a STOP: record the precise required operation and plan its own exact
cap before following it. No automatic recursion, whole-GP recovery, decompiler
or compiler/runtime project. No source-proof execution occurs this turn.

Required .29 result: actual byte-to-16-bit-string behavior, length/NUL rules,
high bytes/malformed units/truncation, empty input, destination replacement,
copy/alias/move and destruction/ownership, allocation/error/exception behavior
where applicable. Distinguish proved behavior, unchanged opaque boundaries and
unresolved required leaves. Derive literal conformance vectors from actual
selected branches; do not invent malformed/Unicode/error outputs from names.
A precise unresolved gate is recorded as incomplete proof, never a no-op stub.

### Retained original behavior at the eventual operation

The original S3 words0/+2/+68 supply USBDeviceInfo header0/+2/+4. S3 cache-derived
fields come from exact SPEC14, including little-endian raw0..1, zero-extended
raw2, raw12 and raw11 in their recovered positions. The native UID, ninf label,
registry ID and topology are not physical NI identity. An internal S3 field
oracle may verify provenance; no selector-shaped public service is needed.

For strings=true, original vendor->product->serial conversion/assignment order
and actual owned GP storage must be preserved. strings=false leaves those
string fields untouched but is not the const path's acceptance. False/handle0
information results preserve caller output as recovered. No whole110-byte
zeroing, invented padding writes or new strlen guard may be called NI behavior.
The selected686 reader defines at most30 content bytes plus an in-range NUL
inside31; its defined cleared empty result remains data even when NI ignored
a reader error. Undefined/unattempted native fields are invalid, not empty or
zero physical identity, and must not be fed into the original conversion.

Use cache/cohort authority, not ninf flags/last_error/UID. Require coherent
nonzero generation across successful native inputs; do not retry or silently
acquire a fresh lease to conceal mixed/stale data. Exact native-unavailable,
OSStatus and original NI Boolean/IOReturn translation is a separate required
binding-contract decision before code, with no invented numeric equivalence.
An injected success path is not a complete faithful functional adapter.

NI's recovered info/spec helpers preclear only their sentinel, perform initial
plus at most3 zero-sentinel reads with100/200/300ms sleeps, stop on nonzero
status, and can returntrue after all zero sentinels if finalstatus0. Preserve
that behavior when admitted; do not tune timing or graft these retries onto
native getters. USBDevice::connect ignores the info-fill Boolean, calls the
original hook and rereads connected. No new all-metadata-success startup gate.
Its spec-failure/registry/domain branches and allocation/cleanup remain required
original contracts; this planning pass does not invent their missing parts.

### Later coding and distinguishing acceptance

No production code card/files/interface are admitted yet. After .29 closes its
actual required leaves, update .2 with a separately reviewed exact code plan
for this one information/storage operation. Freeze any necessary status,
generation, object lifetime and output-preservation mapping then; do not invent
an interim GP abstraction, setter, notification loop or success-only adapter now.

Future offline tests must exercise the actual admitted operation with literal
source-derived vectors: real const strings=true conversion and assignment,
header fields/order, false/handle0 output preservation, empty success,
defined-cleared/error versus identical undefined bytes, strings=false retention,
coherent versus mixed/stale input, original sentinel/sleep/error order and
ignored-info-result/hook reread. High-byte/malformed/allocation/alias vectors
wait for their actual recovered contracts. Semantic mutations must fail useful
assertions; strict ARM and ASan/UBSan plus independent Spec/Standards review
apply to any later code, not to this unexecuted proof plan.

wstrToUTF8 roundtrip and serial/instance publication, original GP/NHL runtime,
version/session/confirmation/teardown, activation/events/timestamps and one
mapped-controller/LED policy authority remain required under .2. Existing
nrpt history/default MIDI policy does not satisfy those original operations.
Full unchanged Controller Editor/Guitar Rig recognition and audio reliability
remain unproved. .27 and this planning completion close neither .2 nor .3/.11.

### Stop, evidence and handoff

Stop at any new owner/backend/class shim, fabricated identity/version,
new schema/selector emulator, frontend patch, direct USB, runtime port,
audio ring/calendar/clock/retry/recovery change, security/profile/trust action,
or unproved required converter/error/lifetime. Record the smallest exact
prerequisite in Beads before expansion. Preserve installedbuild9, its immutable
sourcee16097a and actualSESSION20261026T110147Z-3be14ddc plus verified rollback.
No install/rollback or new hardware run is needed for this source proof.

.28 completion means this minimum-slice decision and exact NEXT source gate
are reviewed and recorded; it does not mean a code-ready adapter or recognition.
.29 remains OPEN/unexecuted, metadata-first; .2 depends on it. Raw proprietary
ASM/unique serials stay ignored locally; commit prose, source pins and results
only. Beads database stays in the originalcheckout, preserve stagedexportblob
113d42992a0ee90acfb640fc694b469abba536a6, passiveexportactiveworktreeonly and sync
Git and Dolt separately. No driver/test code or live action occurred here.

### Existing input pins at baseline1c0a1d8

- `ReverseEngineering/NIARMRecognitionBindingDecision.md`: `d20fd5e597eae062f67d37de29312c1789a217e04abcce798baf728c1d4f9475`
- `ReverseEngineering/NIIdentitySpecificationContract.md`: `aa5d5ae5aa23b293f4bfe6818b24904c3be5d5aab4a762f9c8e4f83866ce3caf`
- `ReverseEngineering/NIUSBIdentityAcquisitionContract.md`: `4a0fc576953cd085c9432a9e49e0b7c36f7fdbbc15f3314073f819be213139fe`
- `ReverseEngineering/NIAgentRecognitionContract.md`: `f768651d043258cc232162e0a796c7c77947ff0f9852ba87c7f456c839d3b1bd`
- `ReverseEngineering/NIFrontendRecognitionContract.md`: `39374b3e3ebf50f1d3a750e52ee850989411aba19a56f5b1a697ee28be7b01f7`
- `ReverseEngineering/NIEventOwnershipContract.md`: `c83cfa2ab6d8f17eda56ba823666e8cce723c1a292a77c613a2ef4f3b8070990`
- `ReverseEngineering/HardwareValidation.md`: `b51226b49f6a48185ad24157d4119f85afb18ad6db50735b33b0bdbb579106d5`
- `Driver/NativeControl.h`: `e25bf632ef1146a9d51f295c6c0e274b07530d009ea26c88bde22f14bddf74d5`
- `Driver/NativeClient.h`: `e9113b3d89a22e01c5f5237e0e6dd6e3b46db848e307d4d5e38126720506128c`
- `Tools/NIIPCContract/README.md`: `62c691d979d0588e15695f41da19ba7a73ec117f94f04a128fce9823bfe1c606`


## Historical .24 SPEC14 implementation plan

This 2026-10-25 plan is retained for traceability. `.24` and `.26` are now
implemented, and `.27` passed bounded installed identity/generation validation.
Its old pending/deferred statements do not override the current decision above.

Date: **2026-10-25 UTC**. Beads: **rigkontrol3-40h.2**;
planned code card **rigkontrol3-40h.24**, depending on `.17` and `.23`.

**Select one read-only, generation-qualified publication of the original
14-byte RK3 specification through NativeUSB → HAL → NativeClient.** It uses
already-cached command01 bytes and adds zero USB operations. This turn plans
only; no production/test code, build, runtime, signing or security work occurs.

This planned slice will implement one useful original functional operation, not NI recognition,
a 110-byte IOConnect emulator or an ninf prefix. Mappings, calibration, LEDs,
audio ring/calendar/clocks/timing/recovery, existing property ABIs and SIP
remain unchanged. [Selected architecture](NIARMRecognitionBindingDecision.md),
[proved byte provenance](NIIdentitySpecificationContract.md),
[event/policy limits](NIEventOwnershipContract.md).

### Why specification first

`.23` proves command01 payload→original cache→S18 for RK3; the current decoder
already retains those14 bytes. The2305-only correction does not apply to1940.
A physical descriptor/string/spec snapshot is **deferred**: it would add capture
operations and unresolved old USB/string semantics merely to fill a larger
record. Neither native UID/product labels nor equal record lengths supply NI
identity. The spec-only vertical slice is independently implementable.
[Decoder](../Driver/DeviceProtocol.c), [owner](../Driver/NativeUSB.c).

### Frozen native V1 contract

Add **`RK3_PROPERTY_SPEC = 0x6e737063` (`nspc`)**, GET-only on the audio device,
global scope/main element. Keep ninf/nrpt/ncmd/gain unchanged. Fixed-width
scalars follow the existing ARM-to-ARM host byte order; raw14 stays byte-exact.

| Record / offset | Field |
| --- | --- |
| Query16 /0 | u32 version=1 (`RK3_CONTROL_VERSION`) |
| Query /4 | u32 reserved=0 |
| Query /8 | u64 expected_generation, nonzero |
| Reply32 /0 | u32 version=1 |
| Reply /4 | int32 native errno status:0, ENODEV or ESTALE |
| Reply /8 | u64 generation |
| Reply /16 | uint8 raw[14] |
| Reply /30 | uint8 reserved[2], both0 |

Add size/offset assertions for query16/reply32/status4/generation8/raw16/
reserved30. Use memcpy and typed temporaries, not casts into CFData bytes.
Status0 requires the exact requested/current committed nonzero generation.
No new sentinel, topology, stereo or content validation is allowed. A valid
INFO refresh on an **already committed** cohort may publish all-zero raw14;
do not bypass existing connection readiness to manufacture that fixture.

ENODEV takes precedence when unavailable/closing; ESTALE applies only when a
cohort is available but the nonzero requested generation differs. Domain-error
replies have generation0 and canonical zero raw/reserved bytes, explicitly
**invalid data**, not a successful zero specification. These native errno
values are not NI IOReturns; later NI translation remains separate.

Malformed CFData wrapping/type/length, version/reserved or generation0 returns
a CoreAudio validation error without creating a new CFData reference or changing
the caller's output payload. OSStatus noErr means
transport/shape success only: the structured native status must still be
checked. Client output stays unchanged on transport/shape failure; a valid
structured domain-error reply may be copied as an error result.
[Native records](../Driver/NativeControl.h), [client](../Driver/NativeClient.h).

### Exact `.24` files and seams

| File | Bounded change |
| --- | --- |
| `Driver/NativeControl.h` | Selector, the two V1 records and assertions only. |
| `Driver/NativeUSB.h` | Declare/document `int32_t rk3_usb_copy_spec(session, expected_generation, out14)`. |
| `Driver/NativeUSB.c` | Small mutex-protected pending/published spec state and metadata-only stage/revoke/commit/refresh hooks below; cache getter. |
| `Driver/Audio/RK3AudioDriver.c` | Fifth custom-property metadata, CFPropertyList data **and qualifier**, pointer-sized property, read-only/global/main GET, exact query checking and canonical32 response. |
| `Driver/NativeClient.h` | One typed helper: copy into temporary32, validate, then assign caller result. |
| `Driver/Tests/NativeUSBTests.c`, `Driver/Tests/AudioDriverTests.c` | Actual-owner/client→HAL→owner cases below; update fifth-property assertions. |
| `ReverseEngineering/NativeUSB.md` | Document the implemented functional property/cohort/error contract. |

No NativeHostUSB, DeviceProtocol, NativeAudioISO, Controller/MIDI policy,
command, scheduler or audio-algorithm edits. Add no module, owner, worker,
per-callback epoch framework, timer, notifier, utility or dependency.

### Cohort and lifetime rules

Current INFO arrives before generation increments; status.info survives
disconnect. Neither it, info_received nor audio/state flags certify provenance.
Use simple pending-valid/collecting, a committed-generation marker independent
of published availability, and published raw14 state under the **existing
session mutex**, leaving legacy generation intact.

| Existing seam | Metadata-only action |
| --- | --- |
| New physical attempt, after the previous mandatory drain and before reader/query | Revoke previous publication, clear pending validity, enable collection. |
| `report_complete` existing valid command01 branch | Stage decoded raw14 only for the collecting attempt. If its attempt is committed at the current generation, atomically publish/refresh14 and mark available, even if previously unavailable; do not freeze it for the generation. |
| `connect_device` existing generation-increment critical section | Record the newly incremented nonzero committed generation regardless of pending validity. Atomically publish staged valid bytes/availability; missing stage stays unavailable, without changing connection success. Later same-attempt INFO can make it available. |
| Known physical transport loss, explicit attempt retirement/retry, `disconnect_device` entry, both close APIs | Clear committed marker, availability, collection and pending validity before abort, drain, wait or release. Late teardown INFO cannot revalidate or seed the next cohort. |
| Audio-only STOP/START/panic/recovery retaining USB | Keep valid spec. No invalidation based on generic set_error, FAILED/READY/RUNNING flags, ISO pointers or audio unpublication. |

Callback context is only the session. Existing mandatory drain must prevent
old-interface callbacks crossing into a newly opened attempt. Hold an old
callback pending in tests and prove it blocks new open/commit. Do not inject
an indistinguishable callback after completed drain and invent an epoch guard.
Stop if that actual lifetime guarantee cannot be demonstrated.

The getter returns EINVAL for NULL session/output or generation0, ENODEV for
unavailable/closing, then ESTALE for mismatch. Every failure leaves out14 unchanged.
Status0 copies14 while holding the session mutex; no USB/Host/ISO access,
allocation, worker/condition wait or freshness probe. This locked copy is the
linearization point: before observed revocation a read may return the prior
cohort; after revocation it cannot. It promises neither instantaneous unplug
detection nor validity beyond the returned snapshot.

For **this new HAL branch only**, lock existing `control` before selecting
usb, call the getter (session mutex), and unlock control **before CFData
allocation**. This serializes initialization/failure-close. Absent global usb produces canonical ENODEV
without calling a NULL getter; an unexpected private errno is a transport error,
not a new reply status. Older getters are
not claimed to pin usb. No ensure/open call, new usb mutex or global lifetime
rewrite. Standalone NativeUSB callers must finish calls before close
relinquishes ownership; tests must not read a freed handle.
[Actual owner lifetime](../Driver/NativeUSB.c), [HAL](../Driver/Audio/RK3AudioDriver.c).

### HAL/client validation

`GetPropertyDataSize` stays metadata-only: return sizeof(CFPropertyListRef)
without fetching or requiring a qualifier. Actual GET uses read_cfdata to
check wrapping and exact16-byte body, then version/reserved/nonzero generation.
Preserve the existing prologue's `*written` pointer-size convention after valid
capacity, including later qualifier errors; test it separately from unchanged
data output. SET rejection follows the existing read-only guard.

The client uses rk3_native_copy_blob into a temporary, validates reply version,
reserved/status, success generation or canonical failure, then assigns its
typed result. No raw14-only convenience API is needed. CF retain/release and
allocation-failure paths follow the existing helpers; allocation is outside
both owner/HAL locks. Installed AudioServerPlugIn.h155–176,279–295 supports
custom CFPropertyList data/qualifier marshalling; no new IPC is necessary.
[SDK header](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/CoreAudio.framework/Headers/AudioServerPlugIn.h:155),
[existing actual fixture](../Driver/Tests/NativeUSBTests.c).

### Distinguishing RED/GREEN and required checks

Planned synthetic offline cases—not tests run or hardware results. RED is the
absent property and incorrect status.info/flag composition; GREEN must exercise
the actual production getter and client→HAL→NativeUSB seam, not a second model.

| Case | Required observation |
| --- | --- |
| INFO before generation commit | Unavailable under old generation; staged bytes publish only with the new commit. |
| Commit with no staged INFO, then later valid INFO on that same committed attempt | Commit records its generation but remains unavailable; later INFO publishes14 at that same generation without changing readiness or creating another generation. |
| Old raw/status/generation persist after revocation, before state changes | ENODEV/out14 unchanged; canonical invalid HAL domain reply. |
| Old callback pending through teardown | Drain blocks new open/commit; late retiring INFO cannot revalidate; only new-attempt INFO seeds the next commit. |
| Current/wrong generation; unavailable; bad private args | Exact14 on0; ESTALE only while available; ENODEV precedence; EINVAL without writes. |
| Later valid INFO on committed cohort, including all-zero payload | Atomic same-generation refresh, no torn snapshot or new content/readiness gate. |
| Audio-only stop/start/panic | Valid spec/generation retained. |
| Synthetic accepted1940 INFO refresh payload01..0e on an already committed cohort | Exact14 owner→CFData→client; no2305 correction, swap, prefix or ninf copy. Do not bypass startup topology/readiness for this fixture. |
| Object/scope/element/SET; NULL or wrong pointer-size; valid wrong CF type; body15/17; bad header/gen0 | Transport errors/no owner fetch; size discovery qualifier-independent. Never fuzz arbitrary invalid process addresses. |
| Bad reply length/type/header/status/generation/canonical failure | Client result unchanged; balanced CF ownership. A valid ENODEV/ESTALE envelope remains explicitly invalid raw data. |
| Getter versus commit/revoke; HAL initialization failure-close | Whole snapshot or error at defined lock point; actual control gate protects session selection; no freed-handle API call. |

Fixture USB/Host/command/ISO counters must show **zero additional operations**;
control/mapping/calibration/LED state must remain unchanged. Use actual drain
and mutex ordering, not arbitrary sleeps or a new scheduler. Check wire layout
and all fifth-property metadata fields. After separately admitting code, run
focused NativeUSBTests/AudioDriverTests, then existing strict ARM and ASan/UBSan
gates: `sh Tools/build-native.sh test` and `sh Tools/build-native.sh sanitize`.
These commands are plans only here. Independently review the completed change.
[Gate script](../Tools/build-native.sh).

### Stops and handoff

Stop for new USB transfers, descriptors/string decoding, another owner,
polling/notifications, activation, changed audio/USB error policy, a new lifetime
framework or audio timing/recovery changes. Also stop if cohort/drain safety
cannot be proved with the actual fixture; record the blocker before expansion.

`.24` is the one next bounded code card; **no coding is
executed this turn**. Acceptance is this readonly operation/offline conformance,
not NI recognition. Physical identity, original NI error/OS-string semantics,
events/policy and agent work remain under `.2` with separate plans.
Rollback reverts only the additive selector/client/cache/test/doc changes.
No installed bundles, services, security, mappings or calibration need changes
for this offline card. A later signed package/install/live acceptance needs its
own scope and does not follow automatically from planning or passing tests.
