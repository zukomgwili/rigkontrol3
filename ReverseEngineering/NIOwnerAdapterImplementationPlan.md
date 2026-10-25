# Smallest owner-adapter implementation plan

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

## Why specification first

`.23` proves command01 payload→original cache→S18 for RK3; the current decoder
already retains those14 bytes. The2305-only correction does not apply to1940.
A physical descriptor/string/spec snapshot is **deferred**: it would add capture
operations and unresolved old USB/string semantics merely to fill a larger
record. Neither native UID/product labels nor equal record lengths supply NI
identity. The spec-only vertical slice is independently implementable.
[Decoder](../Driver/DeviceProtocol.c), [owner](../Driver/NativeUSB.c).

## Frozen native V1 contract

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

## Exact `.24` files and seams

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

## Cohort and lifetime rules

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

## HAL/client validation

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

## Distinguishing RED/GREEN and required checks

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

## Stops and handoff

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
