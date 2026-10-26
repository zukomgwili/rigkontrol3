# ARM NI serial producer: reviewed implementation plan

Status on 2026-10-26: **.34 planning complete; .35 implementation not started**.
Full original NI application recognition remains incomplete under `.2`.

Authoritative Beads design SHA256: `c45c3b90700cb7cf8eef422fc41487523ad6bfa2f2e211154f9d3727ffc31d94` (19,677 bytes),
stored identically in `.34` and `.35`. Independent Spec and Standards/scope/
test-efficacy/FFI-safety reviews passed the exact plan and eight source pins.

The chosen module has one public function reading borrowed cached information
and the caller's original connection-state snapshot. It preserves the recovered
CF conversion and native ownership transfer. The plan explicitly confines its
UTF-16 pointer interoperation to the Darwin CF interface; it makes no portable
aliasing proof or historical allocator/CF failure-policy claim.

No production/test code, original NI metadata/body recovery, compilation,
runtime, device, installed-driver or security action occurred in this step.
`.31` and `.33` source/report/evidence remain unchanged. SIP stays enabled.
The checks described below are required future work, not executed results.

The exact frozen pre-code design follows. Later implementation results must be
added separately so this admission record remains identifiable.

---

# Typed ARM original NI serial producer: exact pre-code plan

Date: 2026-10-26. Authority: rigkontrol3-40h.34; baseline039d74c.
Next implementation: rigkontrol3-40h.35, OPEN/unimplemented. This pass plans only:
no production/test code, NI body/data/metadata recovery, compilation, runtime,
application, device/audio/MIDI/service, package/install or security action.
SIP stays enabled. Full NI recognition is incomplete.

## One module, one public operation

Implement the actual selected original SERVER serial producer, using .33's
converter/caller contract and existing .31 owned information type. No new
agent/main/runtime, USB owner/backend/property or publication server is created.

Frozen interface (not source written this turn):

namespace rk3 { namespace ni {
  struct DeviceInformation;
  bool get_serial_number(const DeviceInformation* cached_info,
                         bool connected, std::string& output);
}}

The header includes <string> and forward-declares the EXISTING information type;
no new source-view/cache/status class, raw ABI record or extra converter entry.
Connected has NO default. The function is NOT noexcept: conversion exceptions
propagate. Output is an already-constructed valid native std::string.

The caller supplies one coherent borrowed cached-record/original-connected-state
snapshot and keeps the record and serial backing alive and unchanged through
the call. Synchronization and original object lifecycle belong to that caller;
this function neither retains pointers across calls nor creates new state/locks.
This is the functional counterpart of original backend+40/connected+10/serial+58,
not code that supplies those missing agent objects or lifecycle today.

If cached_info is null OR connected is false, returnfalse without dereferencing
the serial, CF calls, allocation or output writes. Otherwise consume ONLY its
stored serial, not vendor/product/VID, a fresh .31 read, HAL flags, ninf status,
UID/topology or an invented metadata-success connection gate. No device query,
lease/generation refresh, reconnect, polling, timer, IPC or physical connection
check. Success is not a guarantee of continued physical connection after return.
The serial count must be representable by CFIndex, as required by the same CF
boundary; original valid RK3 cache values satisfy this. No new runtime length
cap/truncation/fallback/error mapping is introduced.

## Exact four-path implementation scope

1. Agent/NISerialNumber.h: only the forward declaration/function above and its
   essential borrowed-lifetime/false/exception contract.
2. Agent/NISerialNumber.cpp: actual conversion, local CF release guard and tiny
   private nonthrowing output-transfer operation. Helpers stay private, without
   a generic GP/string/backend/allocator or dependency-injection framework.
3. Agent/Tests/NISerialNumberTests.cpp: actual-public-operation source-inclusion
   fixture, real CF/std strings and fixed private OS/allocation/reserve seams.
   The SAME binary may self-spawn only its finite offline child modes below.
4. ReverseEngineering/NISerialNumberImplementationPlan.md: this frozen plan and
   later implementation results/commands/evidence/limitations in separate text.

Passive .beads/issues.jsonl is tracking. Existing Agent/NIInformation.h/.cpp and
its fixture, all Driver/Tools/scripts/plists/concerts, .31/.33 reports and prior
private evidence stay byte-identical. No build target/runner or installed bundle
is added. Compile library object and fixture directly with existing Xcode tools.

## Necessary platform representation choices

Keep the ORIGINAL CF C-call boundary: pass cached serial data and its explicit
UTF16 count to CFStringCreateWithCharactersNoCopy(NULL,...,kCFAllocatorNull).
On this Darwin-only port, confine reinterpret_cast<const UniChar*> to THAT
opaque API argument. Assert both units are16bit and char16_t alignment suffices
for UniChar, CFIndex is signed64, size_t is64. Do not dereference the resulting
UniChar pointer in our C++ implementation or fixture; observations use pointer
identity/count and memcpy into actual UniChar objects. Keep source storage alive
through CF release. This is an explicit selected Darwin CF FFI assumption,
NOT an ISO strict-aliasing proof; sanitizer results do not prove alias legality.
It retains the recovered original char16 pointer/count forwarding. A new typed
input copy/allocator or fixed buffer cap would add work/failure/order changes
and is not admitted. Do not substitute a different CF byte-constructor/codec.

Native std::string ownership/move and compiler-generated ARM unwind replace
x86 string tags/24byte copying/LSDA encodings. Never serialize/compare native
object padding or transplant x86 representations. Source-visible clear and
reserve(0), CF calls/arguments, allocation request and cleanup order remain.
These are ABI adaptations at the selected .17 supporting-agent seam, not a
universal claim that ARM requires different NI Unicode, identity or behavior.

## Exact converter and size behavior

Construct the fresh result std::string BEFORE CF creation. Perform the original
counted CF creation, with NO creation-NULL test or NULL-to-empty/false repair.
Immediately after creation returns, install a local unconditional CF release
guard whose destructor is noexcept. No guard is installed if creation itself
throws. Then call CFStringGetLength and CFStringGetMaximumSizeForEncoding with
kCFStringEncodingUTF8 (08000100), preserving their order and typed boundaries.

For signed maximum M<=0, keep the fresh empty result; normal guard destruction
releases CF. For M>0, compute capacity=uint64_t(M)+1 without signed-overflow UB.
At M<=65536 inclusive use __builtin_alloca(capacity) in the SAME converter frame
through GetCString/assignment, not a helper returning dead stack storage/C++VLA.
Above that use raw ::operator new[](request) returning the char buffer:
request=capacity when capacity<=CFIndex::max(); otherwise SIZE_MAX. Since M is
positive signed64, the otherwise case is exactly M==CFIndex::max(). This retains
the recovered array-allocation request/exception boundary. Do not replace it
with a newfalse/lengtherror result, silent clamp, modern array-new-expression
precheck or a custom allocator. Match normal raw allocation with explicit
::operator delete[](buffer); no sized-delete mismatch or array RAII.

Call CFStringGetCString(cf,buffer,static_cast<CFIndex>(capacity),kCFStringEncodingUTF8). IGNORE its
Boolean, then use real result.assign(buffer), the C-string overload. No buffer
zero-fill or explicit produced-byte-count assignment. Delete the raw heap buffer
ONLY after normal assignment. Return the owned result; CF guard runs normally
before the caller can commit it. A <=0 maximum is an empty VALUE, not a false
producer. No API failure/malformed-private-CF/unsafe-buffer contents are invented.
Do not execute real CF calls with invalid NULL objects or unterminated storage
in tests merely to manufacture a deterministic outcome.

The result precedes the guard in lifetime order: on selected conversion/allocation
or assignment throw, guard releases CF, then result string is destroyed and
exception propagates. The raw array is intentionally NOT deleted on that throw
path, matching .33. Do not copy .31's absent exceptional CF release into this
outgoing operation or add array/vector cleanup. The guard's noexcept destructor
also preserves the source's termination edge if CF release unexpectedly throws.
Use native C++ semantics; no original compiler-helper/personality implementation.

## Exact caller mutation and nonthrowing stage

Complete conversion into a fresh local value BEFORE changing output. Thus
false guards and conversion throws preserve all caller output. Then call a
tiny PRIVATE noexcept transfer helper which does, in this order:

1. output.clear();
2. output.reserve(0), via the fixed private reserve seam described below;
3. output=std::move(converted), ownership transfer, NOT assign/copy/swap;
4. converted.clear() to express the original logically empty moved
   temporary using valid native objects, followed by ordinary temporary destruction.

The typed clear in step4 is allowed to guarantee the source's emptied temporary
without assuming a portable moved-from representation; no byte-zeroing of a
C++ object. It is nonthrowing and adds no independent allocation/policy.
Returntrue after this stage, including empty converted data. Do not call reserve
on the old output BEFORE clearing, skip its original call, or aggregate-rollback.
Do not claim the old/current imported reserve(0) allocator internals are equal.
Native move releases prior destination ownership without inventing a copied
output allocation. If the reserve stage unexpectedly throws, native noexcept
termination applies; do not turn it into false, normal propagation or rollback.
Public get_serial_number remains potentially throwing outside this tiny stage.

The only extra implementation-only seam is RK3_NI_SERIAL_RESERVE_ZERO(value),
defaulting to (value).reserve(0). It is inside the noexcept helper, not public.
A fixture wrapper normally calls the SAME real std::string reserve(0), observing
already-empty output and normal order; a child can inject an unexpected throw.
No generic operation table/callback, new exception class or error/status schema.
The implementation undefines only its own default macro/marker at file end.

## Actual-path fixture and safe fault/termination observations

Tests call ONLY the public getter and source-include actual .cpp for private
seams. Preinclude all actual standard, CF SDK and NIInformation headers FIRST;
declare/define fixture wrappers against those real declarations BEFORE redirects.
Then redirect only used CF calls/CFRelease and the private reserve macro and
include actual NISerialNumber.cpp once; undef redirects afterward. Do not macro-
rewrite SDK prototypes, replace std string algorithms, or test a mapper/model.
No NativeClient inclusion, CoreAudio GET/SET/listener or device operation.

Baseline wrappers forward to actual CF APIs and std reserve(0). Assert exact
allocator/encoding/pointer/count/capacity/order at these seams. The CF input
view is never read through a C++ UniChar alias of char16_t; use memcpy into typed
witness units. CF objects/input witnesses must remain valid for their full
observed lifetime. Tracking/fault setup must not allocate while armed; use fixed
trace storage and phase-target actual global new/new[]/delete/delete[] calls.
Exclude fixture setup/CF-internal work from C++ allocation fault targets.

Required cases, with source-derived literals and real data APIs:
- Null record and connectedfalse with a valid cachedrecord: false/unchanged
  output/0CF/0allocation/0reserve; explicitconnectedargument, no requery.
- Cached ASCII and valid empty serial: actual public operation true/value,
  exact counted creation→length→maximum→positiveGetCString/assign or emptybranch
  →CF release→clear/reserve/transfer. Old nonempty output replaced even empty.
- ValidU16 e9 and d83d/de00 through realCF produce UTF8 c3a9 and f09f9880;
  owned output survives CF/temp teardown. A,0000,B counted3 follows counted
  CF creation then C-string assignment yielding A, not explicit-count suffix.
- Observe M<=0 (0 and -1) with a controlled maximum wrapper; emptyvalue/true
  and no GetCString/byteallocation, CF stillreleased. This is sourcebranch
  observation, not invented realframework error policy.
- Safely force maximum65536/65537 with valid tiny actual CF contents to distinguish
  stack/heap placement and capacity65537/65538; real GetCString gets ample actual
  storage. No read of uninitialized buffer bytes to test absent zero-fill;
  that obligation is checked in source review.
- At forced M==CFIndex::max(), record exact SIZE_MAX raw-array allocation attempt
  and throw at that hook BEFORE real huge allocation/GetCString. PreserveCF
  cleanup/oldoutput; a new-expression precheck skipping the hook must fail.
- Boolean-ignored case delegates a REAL successful GetCString (valid terminated
  bytes), then the wrapper returnsfalse. Result stilluses those validbytes.
  This demonstrates unused status; it does NOT claim real failure writes them.
- Creation-boundary throw: no CF guard/release, oldout unchanged. Length/maximum
  boundary throw and actual std string assignment allocation failure: original
  CF release then result destruction, no false swallowing or outputclear.
- Heap-buffer assignment allocation failure: CF released, calleroldout preserved,
  rawarray notdeletedbyproduction. Observe this first; fixture frees its own
  tracked leftover array only AFTER assertions with tracking/faults disabled,
  so sanitizer cleanliness does not silently repair the production policy.
- Heap-sized conversion above native string SSO, below destination capacity
  and above it: actual conversion allocation becomes output.data(), stays alive
  after temporary destruction, and no output COPY allocation appears. Record
  old destination release without assuming reserve(0)'s private shrink policy.
  Value equality alone is NOT a move-versus-copy ownership oracle.

Use the SAME test executable for finite child modes, launched only by
posix_spawn of its own pinned absolute executable path and fixed arguments:
(a) CF-createNULL → mocked NEXT GetLength(NULL) records/exits BEFORE any real
invalidCF call/release; (b) unexpected normal-path CFRelease throw with fixed
terminate handler marker; (c) reserve-wrapper throw after outputclear, same
expected-termination discipline. Normal-path release is necessary to distinguish
removed-noexcept from double-exception automatic termination. No fork with
inherited CF state, external app, shell command or generic process harness.
Child main catches an unexpectedly escaped injected exception and exits with
a DISTINCT controlled failure marker, so removing noexcept is an assertion-
detected negative control rather than accepted arbitrary crash. std::terminate
handler uses fixed nonallocating marker output and _Exit. Parent verifies exact
marker+normal expected exitstatus (signals/crashes/timeouts are failures), caps
each child wait at10seconds, terminates/reaps ONLY its returned owned childPID
on timeout and closes its pipes. No actual invalid API behavior is invoked.

## Meaningful TDD, negative controls and bounded gates

In .35, first declare header/temporary false-return scaffold in planned files
and make the first actual normal public successful-cache case fail a runtime
assertion; missing-file/compiler failure is not the sole functionalRED. Implement
only needed behavior forGREEN, then extend cases/gates; preserve source/log hashes.
No test/build/code runs in .34 itself. Existing user agreement to this seam/test
plan supplies scope; no extra confirmation is required to execute admitted .35.

Compile semantic negative controls ONLY when each can fail a relevant runtime
assertion safely: ignoredconnectedfalse withnonnullcache, wrongexplicitcount,
zero-maximum-as-failure, gatedGetCStringBoolean, explicit-countsuffix assignment,
earlyoutputclear, copiedoutput, exception-to-false, omittedCFcleanup, addedarray
cleanup, skippedreserve, changedallocationrequest/precheck and removednoexcept.
Target a safe relevant case first or use bounded child modes; compilation error,
signal, arbitrary crash or timeout is NOT a successful distinguishing oracle.
No fake borrowed std::string representation or undefined-buffer mutation test.

Direct xcrun clang++ strict -std=c++17 -arch arm64 -mmacosx-version-min=13.0
-Wall -Wextra -Werror -pedantic -g -O2; sanitizer -O1
-fno-omit-frame-pointer -fsanitize=address,undefined. Compile actualproduction
.cpp independently as object. Fixture source-includes .cpp ONCE, links actual
CoreFoundation, and runs only synthetic data. Do not link duplicate .cpp objects.
Use direct commands/logs, no new repo buildrunner/script/target or warning waiver.

BEFORE executing each fixture/child binary, nm import inspection must reject
AudioObjectGet/SetPropertyData, AudioDeviceStart/IOProc/listeners, IOService/USB/
libusb/MIDI/CFMessagePort/runloop/app-launch operations. Production object must
have only CF/string/allocation/standard compiler/runtime imports, NO spawn/fork/
wait/device/IPC/timer. Fixture may additionally use ONLY its documented own-
binary posix_spawn/owned-wait/pipe/terminate helpers. lipo/otool confirm ARM64
and expected frameworks. A direct-import check is not a private framework proof.
All child modes have the same synthetic-only import guard and finite arguments.

Run existing Agent/Tests/NIInformationTests.cpp once strict and once sanitized
from unchanged source into a NEW evidence directory, preserving its old pinned
binaries/evidence. That checks this neighboring typed module's compatibility.
No full14+14Driver rebuild is required: no Driver/Tools/shared header changes
are in scope; their prior results remain prior evidence, not rerun/claimed now.
If a new concern justifies changed shared scope, STOP and record/review it before
code/gate expansion. Independent Spec and Standards/test-efficacy reviews must
pass actual implementation/tests/pins; final report/evidence fidelity also needed.

## Completion, stops and handoff

.34 closes only on exact independently reviewed plan/pins/docs. .35 remains
OPEN/unimplemented; no automatic implementation/package/live action this
planning pass. .35 closes only the actual selected library/offline contract,
not full NI recognition, agent/lifecycle/state/publication or audio stability.
Original signed-byte ASCII widening and true-only notification path remain
existing downstream contracts; this operation does not send a notification or
publish .31UTF16 directly. .2/.3/.11 stay open.

Stop before file/interface/scope drift, unresolved original operation, new
input copy/cap/codec/CFNULL policy/buffer-fill/error schema, altered resource
cleanup/move/noexcept, fresh info query, new connected authority/owner/cache/
backend/property/runtime, x86 representation copying, timing/ring/recovery/QoS/
mapping/calibration/LED/security changes. Record and independently review the
precise exception before implementing it. No further NI body recovery needed.

Preserve installed0.2.4/build9/frozene16097a/SESSION20261026T110147Z-3be14ddc;
mutabledefault.build/native/drivers remains8, no packaging/install here.
Rollback for an unlinked new module needs only its four paths; no system or
security rollback. DBONLY originalcheckout /Users/zukomgwili/src/rigkontrol3,
preserve primarystagedexport113d42992a0ee90acfb640fc694b469abba536a6,
passiveexportactiveworktreeonly/noimport. Gitpullrebase/push and separateDoltpush
before handoff. Proprietary source/evidence remains ignored, no unique serials.

## Baseline input pins

- `ReverseEngineering/NIWstrToUTF8Contract.md`: `b5ff8e69b45ae6b7cdec6d7eb80c9e20d9c734ef2abb771266daf5b57b30b918`
- `ReverseEngineering/NIIdentitySpecificationContract.md`: `aa5d5ae5aa23b293f4bfe6818b24904c3be5d5aab4a762f9c8e4f83866ce3caf`
- `ReverseEngineering/NIAgentRecognitionContract.md`: `f768651d043258cc232162e0a796c7c77947ff0f9852ba87c7f456c839d3b1bd`
- `ReverseEngineering/NIARMRecognitionBindingDecision.md`: `d20fd5e597eae062f67d37de29312c1789a217e04abcce798baf728c1d4f9475`
- `Agent/NIInformation.h`: `2fd2567f9ad5407a8169ecca70932552aba781ee80137b9bc1427fb1bd018bdc`
- `Agent/NIInformation.cpp`: `96136b1e58ebaf09250ddd880e9d9918751b07116d7ee0074b85510dcc6c2a3a`
- `Agent/Tests/NIInformationTests.cpp`: `e95954a106d5cfe12352cbb28e67af1c0fc6aa3562358d9f86bb9df1679c0e10`
- `ReverseEngineering/NIInformationImplementationPlan.md`: `d7ab03feb26ce3592d8db51c68a418781eb008e97ddb91c60ab212072afe6637`
