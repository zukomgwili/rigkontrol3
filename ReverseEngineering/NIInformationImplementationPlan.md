# ARM NI information binding: reviewed implementation plan

## Implementation result: .31

Date: **2026-10-26**. Source baseline: `5bd4c25`. The planned library operation
is implemented and passes offline conformance checks. It is not linked into an
agent or installed driver. **Original NI automatic recognition remains unproved.**

The new `Agent/NIInformation.h/.cpp` operation accepts the existing owner's
borrowed device/generation and reads actual `NativeClient` identity and SPEC14
records. It preserves the original initial read plus three zero-sentinel retries,
100/200/300 ms waits, final status-zero success, CF UTF-8 conversion, independently
owned UTF-16 values, sequential field copies, partial updates and propagated
allocation exceptions. Native unavailable/schema errors return false before
output changes; they are not relabelled as numerically equivalent NI errors.

The only shared-driver edits are the scoped C/C++ assertion spelling macro in
`NativeControl.h` and one `const UInt8 *` query cast in `NativeClient.h`. All 41
ABI assertion expressions/messages and all types, fields, selectors and layout
values remain unchanged. ARM typed objects/compiler unwind replace x86 object
representations. No new UTF decoder, owner, backend, property, lifecycle loop or
frontend patch was introduced. Driver algorithms and all Tools sources remain
byte-identical to the baseline after normalizing those two compatibility edits.

### Verification actually executed

- Strict C11 and C++17 shared-header checks pass, including all ABI assertions
  and the assertion-macro leak guard. Separate preserved pre-edit failures prove
  the assertion and byte-pointer changes are required for strict C++ compilation.
- Two functional RED/GREEN steps are preserved: a compiled false scaffold fails
  the real default strings-enabled success assertion; the single-acquisition
  implementation then fails the bounded pair/wait assertion before the original
  readiness loop is added. Both final behaviors pass.
- **56 public-operation cases** pass under strict ARM64 C++17 and ASan/UBSan.
  Actual module and NativeClient helpers use synthetic GET responses with real
  CFData/CFString objects. Cases cover every required availability bit, native
  and malformed replies, fixed generation, zero/error readiness order, omitted
  default argument, explicit strings=false, Unicode/first NUL, CF NULL, direct
  and fallback extraction, owned copies and six actual allocation-failure stages.
- Allocation/deletion observations distinguish a copied output allocation from
  the conversion temporary. Failure cases verify header/earlier-field mutation,
  current-field preservation, exception propagation and NI's cleanup placement.
  Fixture-owned CF witnesses are cleaned only after the relevant observations.
- **Ten compiled semantic negative controls** each exit 1 at a relevant assertion:
  wrong generation, missing required reader, added unrequested-field gate,
  zero-sentinel failure, changed wait, reader-error failure, moving instead of
  copying, aggregate commit, exception-to-false and extra exceptional CF cleanup.
  Build failure or a crash is not accepted as negative-control success.
- The standalone production object and both final fixture binaries are ARM64.
  Before every fixture execution, import inspection confirms the real CoreAudio
  GET is absent. The separately compiled production object references real GET;
  neither form imports a SET, audio start/listener, IOKit/USB, IPC-port or run-loop
  operation. This is a direct-import check, not a claim about framework internals.
- Existing `sh Tools/build-native.sh test` and `sanitize` each pass **14/14** on
  the final identical sources. The two expected MIDI-output rejection messages
  are test vectors. The scripts compile diagnostics and locally ad-hoc-sign an
  unused audio-client artifact; no hardware diagnostic/helper is executed.
- Independent Spec and Standards/scope/test-efficacy reviews pass all five final
  source hashes with no remaining findings. `git diff --check` passes.

Private evidence: `.build/ni/information-binding-implementation/20261026-5bd4c25/`.
`execution-evidence.json` pins source snapshots, commands, RED/GREEN logs, symbol
and architecture checks, negative controls, both driver regression logs and
fixture binaries, review records and this report. Beads `.31` records its hash.
Raw NI body recovery was unnecessary and was not performed.

### Limits and remaining work

This closes only `.31`'s selected library operation and offline conformance.
`.2` still requires original serial/instance publication, agent runtime,
version/session/event and mapped-controller policy integration and actual NI
application recognition. `.3`/`.11` retain their separate reliability gates.
The lease is not persistent identity across owner restart, the two GETs are not
atomic, and successful return does not guarantee continued physical connection.
No broader historical CF decoder or unreachable large-input coverage is claimed.

Installed `0.2.4/build9`, its frozen `e16097a` package and rollback session
`20261026T110147Z-3be14ddc` are untouched. NI audio ring/calendar/clock/timing/
recovery, mappings, calibration, LED policy and security settings are unchanged.
SIP stays enabled. There is no new package, installation or live acceptance run.
The next bounded recognition step must be planned in `.2` before further code.

## Historical .30 planning record

Status on 2026-10-26: **planning complete; implementation not started**.
`rigkontrol3-40h.30` records the plan; `rigkontrol3-40h.31` is the next
implementation task. Original NI automatic recognition remains incomplete.

Authoritative Beads design SHA256: `a6d46f382e5ff27f5477b623952d9b0f20141e12ec12add01f5118584d0ebc6c` (19,331 bytes).
The exact design below passed independent Spec and Scope/Standards reviews.
The first draft's fixture include-order finding was corrected before admission:
the GET redirect must precede the first NativeClient include. The reviewed
revision also requires an import guard before execution, functional runtime RED,
and an allocation-based oracle that distinguishes copying from moving.

No production/test code, compilation, signing, installation, NI application,
endpoint, device/audio/MIDI/service or security action occurred in this planning
step. Installed build 9 and all NI driver algorithms remain unchanged. SIP stays
enabled. The tests below are required future work, not executed results.

The body below mirrors the authoritative design byte for byte, starting at its
heading. Later implementation results must be added separately so the admitted
pre-code plan remains identifiable.

---

# Typed ARM NI information operation: exact pre-code plan

Date: 2026-10-26. Authority: rigkontrol3-40h.30, baseline d73b5e0.
Next implementation: rigkontrol3-40h.31. This pass plans only; no production/test
code, new NI body/data recovery, compilation, signing, installation, application,
endpoint, device/audio/MIDI/service or security action occurs. SIP stays enabled.

## One operation and its interface

Implement one library module at the selected supporting-agent information seam.
No app/main/project/runtime host is created or linked into installed drivers.
The original const FX2 getter calls the strings-enabled helper; its Boolean,
values, sequential mutation and exceptions are retained. One public function
also preserves the original helper's strings=false mode, while acceptance MUST
exercise the real default strings=true operation.

Formal interface, not source written this turn:

namespace rk3 { namespace ni {
  struct OwnerLease { AudioObjectID device; std::uint64_t generation; };
  struct DeviceInformation {
    std::uint16_t vendor_id{}, product_id{}, spec_word0{};
    std::u16string vendor, product, serial;
  };
  bool get_device_info(OwnerLease, DeviceInformation&, bool strings = true);
}}

spec_word0 means exactly little-endian raw specification bytes0/1 (original
USBDeviceInfo header+4), not an invented firmware interpretation. The other
header words are actual VID/PID. All strings are independently owned values.
Output must be an already-constructed valid object; its representation/padding
is never serialized, compared as bytes, or constrained to NI's110/24-byte layouts.
Use sizeof-width assertions for UInt16/char16_t/AudioObjectID/generation only.

The caller supplies an existing AudioObjectID and nonzero expected generation.
This borrowed locator is not persistent identity across CoreAudio/owner restart.
The function does not discover, open, retain or reconnect a USB device, create
an agent connected flag, or reacquire a generation. AudioObjectID0 or generation0
returns false without GETs, sleeps or output writes. No VID/PID constant filter,
native UID/product-label substitution, logging or public diagnostic schema.

## Exact six-path implementation scope

1. Agent/NIInformation.h: only the typed value/lease/function above.
2. Agent/NIInformation.cpp: actual fixed-lease acquisition/readiness and original
   CF conversion/owned-field copying. Private helpers and one private wait seam
   stay inside this module; no generic GP/backend/dependency-injection framework.
3. Agent/Tests/NIInformationTests.cpp: source-inclusion fixture for the actual
   module/public function, actual NativeClient helpers and real CFData/CFString.
4. Driver/NativeControl.h: replace _Static_assert spelling with one scoped
   RK3_CONTROL_STATIC_ASSERT selecting static_assert in C++/_Static_assert in C;
   undefine it at file end. Retain EVERY expression/message/type/field/value.
   Existing MIDIEvent.h48-59 is the local convention. No wire/selector changes.
5. Driver/NativeClient.h: ONLY cast the copy_blob CFDataCreate query argument
   to (const UInt8*) at its currentline16. Other CFDataCreate calls already cast.
   No change to client validation, return codes, lifetime, GET/SET or payloads.
6. ReverseEngineering/NIInformationImplementationPlan.md: mark the implemented
   result, tests/evidence and limits after execution; no extra report/README.

Passive .beads/issues.jsonl is tracking, not another implementation path.
Tools/build-native.sh, build-drivers.sh, install scripts, plists, Audio/MIDI/USB/
Host/ISO/controller/gain/clock/ring code stay byte-identical. No build-script
stanza or package is needed: compile this library object and fixture directly.

## Necessary platform changes, without new NI behavior

The .17 binding selected the existing owner because the legacy service/kernel
loading path was unavailable under recorded signing/SIP constraints. Existing
nusb/nspc replace that unavailable driver access, using real source-proved data.
This is the chosen deployment seam, not a universal ARM hardware restriction.
Native C++ objects/calls and compiler-generated ARM unwind replace x86 object
layout/register/LSDA representation. Preserve logical UTF16, copying, exception
and resource order; do not transplant representation words or invent a codec.
The two header spelling/cast edits are necessary to include the ACTUAL C client
under strict C++17; they do not relax warnings or change C/wire behavior. Do not
add duplicate wire structs, a C wrapper/backend or compiler-warning suppression.

## Frozen functional failure and cohort disposition

This is a Boolean operation, so it needs NO invented NI IOReturn number.
False is the original getter's no-output failure path. At the functional seam,
explicitly choose false for any CoreAudio transport/schema/client error, valid
native ENODEV/ESTALE, or undefined required input. This is necessary handling of
unrepresentable/unavailable native data, not proof of numeric equivalence to a
legacy error. Do not cast errno/OSStatus/raw-step IOReturn between domains.

Use actual rk3_native_copy_usb_identity and rk3_native_copy_spec as the ONLY
record validators. One attempt at fixedG:

1. GET nusb with version1/reserved0/generationG. Immediately stopfalse on client
   OSStatus error, native status!=0, or missing required defined fields.
   Required mask is VID|PID =0x03 for strings=false; add the THREE reader bits
   (product/manufacturer/serial) for strings=true, total0xe3. Do not require
   all steps/index fields as a separate policy, all0xff, or all raw statuses0.
2. GET nspc with the SAMEG. Immediately stopfalse on client/transport/native
   failure. Existing helpers enforce reply generation/header/canonical errors;
   do not duplicate a CFData parser or silently repair malformed records.
3. The successful group supplies one source snapshot: identity fixed withinG
   and one accepted raw14 cache copy. Both replies must bindG. This pair is not
   an atomic transaction or a guarantee of continued connection after return.
   A disconnect/stale lease between reads discards the group with false; no
   ninf CONNECTED/FAILED/last_error gate, after-query, fresh lease or retry.

Required defined data is data even when a reader's raw return is nonzero and
its output is the proved cleared31-byte string. That converts as empty, just
as NI ignored that reader error. Identical zero bytes with defined=false are
UNAVAILABLE, never a zero ID, successful empty serial, label or fallback value.
A strings=false call ignores unavailable unrequested strings but still requires
real header data. NativeClient's existing whole-record validity rules remain.

## Preserve the original readiness loop

Complete acquisition in locals before changing output. One logical original
S3 read is the pair above, not an S18-helper loop nested inside it. Compute the
sentinel/spec_word0 from raw0|raw1<<8. On successfulzero, request100000000ns then
repeat the WHOLEpair; subsequent successfulzeros request200000000ns and
300000000ns. Maximum4pairs/3waits. A fourth successfulzero STILL succeeds.
Nonzero sentinel succeeds immediately. Any failed attempt endsfalse with
output unchanged, even after earlier zero-sentinel successful attempts.
No retries for transport/native/undefined errors, no wait after the fourth,
no new polling/timer/QoS or wall-clock deadline guarantee. Existing property
GETs are cache-only and add zero USB operations. Original connected-hook/info
Boolean handling is not changed by this getter implementation.

Keep default blocking std::this_thread::sleep_for(std::chrono::nanoseconds(ns)).
One PRIVATE implementation-only macro seam RK3_NI_INFORMATION_SLEEP_FOR takes
that duration and defaults to the same std call; the fixture substitutes a
record-only function. It is not in the public header, a new scheduler or UI.

## Exact CF conversion, mutation and cleanup

After acquisition/readiness succeeds, assign header VID/PID/spec_word0 FIRST.
With strings=false, returntrue without CF/string assignment. Otherwise process
manufacturer->out.vendor, product->out.product, serial->out.serial, in that order.
For EACH: construct converted owned value, COPY assign(c_str(),size()) to the
field, then destroy the converted temporary BEFORE starting the next field.
Do not preconvert all3, move/swap into output or aggregate-commit a new object.

Retain the actual .29 source calls and semantics: CStringNoCopy(NULL,bytes,
0x08000100,kCFAllocatorNull), length in16bitunits, owned char16_t storage initially
U+0020, CharactersPtr direct-copy or Characters range(0,N) fallback, explicit
owned termination. Input is the validated native31-byte array with in-rangeNUL;
do not normalize/ASCII-widen or use a new explicit-count UTF8 decoder.
CFcreateNULL produces empty convertedvalue and normalgettertrue, notfalse.

Private materialization follows the recovered branch placement, including
stack temporary atN<=65536 and raw new UniChar[N+1]/normal delete[] above it.
Use typed AppleClang __builtin_alloca/alloca for the original dynamic stack
allocation; no C++VLA or invented fixed limit. Retain the selected algorithm,
not a general public StringUTF8/GP implementation. The real USB path is bounded
by its31-byte inputs; no broader utility/hardware coverage is claimed.
Copy UniChar units via typed iterators into std::u16string, not an aliasing
reinterpret_cast to char16_t*. Native std ownership replaces ABI bitfields.

Fresh internal C2-style construction may swap its owned temporary with a fresh
empty value, matching the original transfer. Public output fields MUST copy.
Exceptions propagate. Growth failure leaves the CURRENT field unchanged;
header and earlier fields remain updated. Do not catch-to-false or rollback.

Use a raw CFStringRef released ONLY on the normal source path after owned-copy
construction. Generic CFRelease RAII/catch cleanup would add a resource action
absent from NI's selected unwind; do not add it. Likewise raw large-array delete[]
is on its original normal path, not newly added exception cleanup. Automatic
owned std::u16string destructors must provide the original string cleanups;
compiler emits native ARM unwind. No x86-layout copying, custom allocator or
extra driver cleanup. Source.29 is authoritative for all cleanup placement.

## Actual-path tests and smallest test seams

Fixture calls ONLY the public function; source-includes the actual.cpp for
private OS/wait/fault seams, rather than another parser/mapper/algorithm model.
Freeze this include/binding order (SDK declarations precede all redirects):
include real CoreAudio/CoreFoundation SDK and NativeControl; declare the typed
fixture GET function; define AudioObjectGetPropertyData to that fixture name;
FIRST include actual NativeClient.h under that GET redirect, then undef it.
Only AFTER that, activate CFString/CFRelease/private-wait wrappers and source-
include actual NIInformation.cpp (its NativeClient include is header-guarded).
Undef fixture macros afterward. Thus the client's inline GET is ALREADY bound
to the stub, while its CFData helpers remain real. Do not preinclude NativeClient
before the GET redirect or rely on an unplanned same-symbol linker override.
Mock only that GET and relevant CF/platform wait/allocation calls. The CoreAudio
stub asserts exact object/global/main selector, CFData
qualifier size16/version1/reserved0/G; returns genuine CFData with160/32 bodies.
Reject any other selector, discovery/listener/SET/Start/USB/port action.

Baseline conversion delegates to actual CFString APIs with synthetic data.
Use fixture wrappers to force CharactersPtr NULL or a consistent stable U16
pointer obtained from the SAME real CF contents, and CFcreateNULL. No fake UTF8
algorithm. CFRelease observations distinguish created CFString from CFData.
When observing exception cleanup, fixture-owned stable input copies/witnesses
must keep CF objects valid; release leaked witness resources AFTER assertions
as fixture cleanup, never as a production-policy repair.

Use test-local replaceable C++ new/delete faults around ACTUAL std::u16string
allocation, with fixed-capacity trace storage (no logging allocation while
armed). CF wrappers mark conversion/assignment phases; ensure fixture old-field
capacity<requested length before growth fault. Do not replace assignment with
a model or throw on an unrelated allocation. Count normal witness allocations
then target each real stage; verify actual strong current-field guarantee.
These hooks and allocator tracking stay in the fixture, not public/module DI.
For a meaningful COPY-versus-move check use a heap-sized synthetic value beyond
fixture destination capacity. Track the conversion allocation, the distinct
field allocation and the conversion block's deletion after assignment; the
field must remain valid afterward. Also check sufficient-capacity replacement
retains its own destination buffer. Value equality alone cannot detect stealing
the conversion buffer, so it is not enough for the compiled move mutant.

Required distinguishing cases:
- Invalidlease0:0GET/0sleep/outunchanged; exact successfulpair/no ninf/VIDlabels.
- Wrong CFtype/body/header/generation/reserved/nativecanonicalfailure through
  ACTUAL NativeClient; identityfailure stopsbefore spec, specfailure afterpair;
  canonical ENODEV/ESTALE vs availableall-zero raw must behave differently.
- Mixed/staleG between reads false/nooutput/no freshG; successful snapshots make
  no post-return connectivity promise. IDs copied from suppliedvalues, notconstants.
- strings=true required0xe3; defined-cleared/erroracceptedempty; sameundefined
  zeros false; strings=false withonlyrequiredheaderdata retains oldstrings.
- Nonzero sentinel1pair/0wait; zero→nonzero after2/3/4pairs with100/200/300ms
  prefixes; fourzeros8GETs/3waits/thentrue; errorafterzero endsfalse/nooutput.
- Exactword raw01,02 ->0x0201; validASCII andUTF8 c3a9 /f09f9880 withrealCF;
  firstNUL prefix; CFcreateNULL clearschosenfield but returnsnormaltrue.
- Direct/fallback/empty conversion, ownedvalues after CF/temp teardown;
  long→short/empty,10/11-unit values, preexisting outputfield capacities.
- Conversion and field-copy allocations failing atvendor/product/serial:
  original partialmutation, currentfield retained, exacttemporarycleanup and
  propagation; noextraCFRelease/false/transactionalrollback.

Malformed/private CF decoding results are not invented. Exact historical
framework internals/capacity layout are not an ARM wire contract. No fixtures
using real attached serials. No public endpoint/app/hardware execution.

## TDD and bounded quality gates after code admission

In .31, log header C++ compatibility/absence failures separately. Functional
RED must reach a relevant runtime assertion at the public operation once the
headers/declarations compile; a temporary false-return scaffold in the planned
files is allowed only as a test-first stage, never the shipped implementation.
A missing file or compiler error is not the sole functional RED. Preserve
commands/source hashes/logs. GREEN uses exactly the planned module, real
client/CF objects and distinguishing cases above. Add compiled semantic negative controls
for wronglease/flag gate, sentinel-zero-as-failure, reader-error-as-failure,
borrow/move/aggregate-commit and exception-to-false/extraCFcleanup only where
meaningful actual-path assertions can distinguish them. Mutants must compile
and fail the relevant assertion, not just fail build/crash.

Compile strict ARM64 C++17 with -Wall -Wextra -Werror -pedantic -g -O2 and minimum
macOS13.0; ASan/UBSan uses -O1 -fno-omit-frame-pointer -fsanitize=address,undefined.
Build production.cpp independently as object; fixture executable source-includes
it once, links CoreFoundation/CoreAudio and runs offline. Use direct xcrun
clang++ commands, no new build runner/script/dependency. Both forms must compile.

Check NativeClient/NativeControl header inclusion with strict C11 AND C++17,
all unchanged layout assertions active/no leaked assertion macro. Since2shared
headers change, run existing sh Tools/build-native.sh test AND sanitize once
on finalidenticalsource: all14strict/14san fixtures pass. Those existing gates
compile diagnostic executables and locallyad-hoc-sign an audio test binary;
NEVER execute a deviceprobe/audiohelper or install/signsystembundles. Existing
build scripts stayunchanged. Do not repeat fullgates absent codechange/failure.

Inspect lipo/nm/otool forarm64 and read-only GET/CF/string/sleep/standardallocation
imports. No setters/StartIO/IOProc/listeners/IOKit/USB/libusb/CFMessagePort/runloop
or app-launch operation; NativeClient unused setters must remain unreferenced.
BEFORE executing either fixture binary, inspect its undefined symbols: the
real AudioObjectGetPropertyData must be absent/resolved to the fixture; stop
before running if the include seam accidentally left a real GET import.
The separately compiled production object should reference the real GET via
NativeClient. This is a direct-import
guard, not a claim about all private framework internals. Independent Spec+
Standards review must pass actualimplementation/tests and hashes beforeclosure.

## Acceptance, stops and handoff

.30 closes when this exact plan is independently reviewed/recorded; .31 remains
OPEN/unimplemented, no automatic code/package/live action this planning turn.
.31 closes ONLY the actual libraryoperation/offlineconformance, not NI app
recognition, fullagent, owner lifecycle or audio/reliability .2/.3/.11.
OutgoingwstrToUTF8/instancepublication and originalversion/session/events/policy
staytheir latergates. No newbodyrecovery needed for this selected operation.

Stop beforeanyscope/file drift, duplicatewire/Cwrapper/backend/owner, fake
identity/numericNIerror, changedCF/arrayexceptioncleanup, topology/readiness/
connectiongate, newtimer/QoS/ring/recovery/securitysetting or requiredunknown
contract. Record/reviewthepreciseexceptioninBeads beforeimplementationchange.
Preserve installed0.2.4/build9/frozene16097a/SESSION20261026T110147Z-3be14ddc and
rollback artifacts. Revert only the6source/test/docpaths ifneeded; no system
rollback is needed for an unlinkedoffline library/header compatibility change.

AlltasktrackinginBeads, originalcheckoutDBonly. Preserveprimaryindexblob
113d42992a0ee90acfb640fc694b469abba536a6, passiveexportactiveworktreeonly;
Gitpullrebase/push andseparateDoltpushbeforehandoff. Rawprivateartifactsignored.

## Baseline input pins

- `ReverseEngineering/NIStringUTF8Contract.md`: `ac7c44dcdb20c3ac40937868ad08cebade6de15c351e28389f7113773f0a7ea0`
- `ReverseEngineering/NIIdentitySpecificationContract.md`: `aa5d5ae5aa23b293f4bfe6818b24904c3be5d5aab4a762f9c8e4f83866ce3caf`
- `ReverseEngineering/NIARMRecognitionBindingDecision.md`: `d20fd5e597eae062f67d37de29312c1789a217e04abcce798baf728c1d4f9475`
- `ReverseEngineering/NativeUSB.md`: `ab0d239a499adc8a15a4bb153cae778be131dc8f0556b1942adee9c7ef500b9f`
- `Driver/NativeControl.h`: `e25bf632ef1146a9d51f295c6c0e274b07530d009ea26c88bde22f14bddf74d5`
- `Driver/NativeClient.h`: `e9113b3d89a22e01c5f5237e0e6dd6e3b46db848e307d4d5e38126720506128c`
- `Driver/MIDIEvent.h`: `39bd956c32e4ef1f64ae54030551c3b5b49b0891802fb3e9f72d092bd34c0017`
- `Tools/build-native.sh`: `44cc5252607013b9d93e17930cc6be9f1d83bb3050d407d4846775af0d8b239f`
