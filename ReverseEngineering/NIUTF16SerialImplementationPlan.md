# Native NI UTF-16 serial overload: reviewed implementation plan

## Implementation result: .39

Date: **2026-10-26**. Baseline: `b3f01ad`. The planned UTF-16 serial overload
is implemented and passes the selected offline conformance checks. It remains
an **unlinked library operation**. Full original NI recognition is incomplete.

The single new overload calls the actual existing byte producer once, widens
its explicit byte count with NI's signed-byte rule and copies the owned UTF-16
temporary into output. Native object lifetime preserves the recovered cleanup
and exception propagation. Mutable cached-field output aliasing works after
input/CF borrowing ends. No new identity, codec, state or connection policy was
added; all three existing byte-getter functions are byte-for-byte unchanged.

The original explicit length guard remains first. Native `std::length_error`
and native string storage/SSO/max-size/unwind are platform bindings; the current
diagnostic is not a claim about NI's historical private `what()` text. The two
cpp-private hooks default to real operations and the fixture forwards those
same operations without replacing the producer result or copied value.

### Checks actually executed

- Public TDD: the first ASCII success assertion failed at runtime against a
  successfully compiled false scaffold, then passed with the real operation.
  Strict ARM compilation/import guards preceded both executions. The initial
  ASCII-only runs did not redundantly run the old regression groups; snapshots
  preserve the actual RED/GREEN source and logs.
- The final expanded strict and ASan/UBSan runs each pass **22 byte public calls,
  20 UTF-16 public calls, six internal-helper cases and three existing controlled
  child modes**. No new child mode or local termination policy was introduced.
- Real CF/std paths cover false versus empty, literal signed-byte non-ASCII and
  non-BMP values, first-NUL behavior, mutable output aliasing and cache preservation
  for distinct output. Internal counted-NUL witnesses are labelled separately;
  they are not claimed as values produced by the byte getter's C-string stage.
- Actual scalar allocation faults and tracked live payload identities verify
  independently owned COPY output, original in-place assignment with ample
  capacity, completed-wide cleanup before byte cleanup, and propagation/preserved
  destination state on widening and output-copy failure, including alias cases.
  Retired temporary pointers are never dereferenced. Real producer failures
  normally leave the fresh outer byte destination empty; those cases do not
  pretend to observe a heap-byte destructor. The later allocation-fault cases
  provide the actual heap cleanup witnesses.
- The two huge-count helper cases throw the standard length-error category;
  a 4096-byte allocation gate prevents any real huge allocation before malloc
  while permitting bounded exception diagnostic storage. Native construction
  also rejects huge counts, so these cases alone do not prove the explicit
  source guard or distinguish its removal. Source review establishes retention.
- **Nine compiled semantic negative controls** each pass strict compilation,
  pre-run import/ARM checks and then fail the intended assertion with normal
  exit 1: unsigned widening, strlen count, guarded cached-UTF16 bypass, repeated
  producer, empty-as-false, active-callsite MOVE, swallowed exception, premature
  output clear and valid early byte retirement after widening. No compiler
  failure, signal, crash or timeout is counted. MOVE is rejected at the missing
  counted-COPY assertion; separate positive heap cases establish COPY ownership.
- Control copies omit exactly ten old main-call lines (nine byte-test groups
  and the child group) after those regressions already passed in each full
  positive run. All new assertions/functions and argument handling remain
  unchanged. The inherited success-print text is not evidence of child execution;
  all accepted controls fail before that line. No old 15-control rerun occurred.
- The separate production object is strict ARM64 with CF, strings/stdexcept,
  allocation, memory and compiler/runtime imports only. All final fixtures and
  controls are ARM64; saved `nm` checks precede execution and `otool` libraries
  are retained. Production has no direct device/query, audio/MIDI, IPC, timer
  or process-spawn import. These are direct-import observations, not a claim
  about undocumented historical framework internals.
- Independent Spec and Standards/scope/ownership/test-efficacy reviews passed
  the exact three final source hashes and the control definitions/fixture diff.
  Original byte-test bodies and child-process policy remain unchanged. Parent
  sanitizer success and deliberate child `_Exit` markers do not prove historical
  private allocator behavior or child leak-freedom. The old intentional raw-array
  exception policy remains tested separately and was not repaired.

The final expanded suite already reruns the original 22+3 checks once in each
mode. Unchanged NIInformation and Driver/Tools paths were neither rebuilt nor
presented as newly tested; their prior source/evidence pins remain intact.
The production header/implementation did not change after first GREEN. The
fixture expanded afterward, before its single final run in each mode; no final
gate rerun was required.

Private evidence: `.build/ni/utf16-serial-implementation/20261026-b3f01ad/`.
The final manifest hash and exact report/evidence fidelity reviews are recorded
in Beads `.39`. It pins commands, snapshots, objects/binaries, symbols/libraries,
actual RED/GREEN and final logs, controls and independent review records.

### Scope and remaining work

Only the four admitted paths and passive Beads export change. No NI body/metadata
recovery, driver/backend/owner/property, runtime/frontend/agent integration,
application/endpoint/device/audio/MIDI, package/install or security action
occurred. Installed `0.2.4/build9`, NI ring/calendar/timing/recovery, mappings,
calibration and LED authority remain untouched. SIP stays enabled.

Next: **rigkontrol3-40h.40**, planning the smallest original instance-publication
step from the completed producer and existing query/writer/frontend proofs
before coding. Do not invent identity or zero original unwritten words. Full
`.2` original NI recognition/lifecycle/session/events/policy and `.3`/`.11`
reliability remain required and incomplete.

## Historical .38 planning record

Date: **2026-10-26**. Beads authority: **rigkontrol3-40h.38**.
Baseline: `d445f7e`. This result is planning only; implementation is not started.

**Add one UTF-16-output overload to the existing serial module.** Call the
actual `.35` byte getter once, widen its counted bytes with NI's signed-byte
rule, then copy into output. Native owned temporaries preserve `.37`'s ordered
cleanup and exception propagation. Keep the existing byte getter unchanged.

The plan retains NI's explicit source length guard and uses native C++ objects,
storage and unwind for ARM. The current native length-error diagnostic is not
claimed as NI's historical private `what()` text. No new codec, identity,
connection policy, driver owner, runtime or frontend is proposed.

Output may alias a mutable cached UTF-16 field after all input/CF borrowing
ends. Review corrected the initial blanket input-stability wording before code,
and added a literal public alias case. The earlier draft is preserved privately.

Exact reviewed pre-code design: **17,417 bytes**, SHA256
`70b5b084e65e4d1ceb355e7dadfb43b789d0d0a06d3fa5c5e527766b9fc4e80c`.
It is stored identically in Beads `.38` and future implementation `.39`.
Independent Spec/native ownership and Standards/test-efficacy/ownership reviews
passed the exact final plan. Source/native-header pins and final report/evidence
fidelity reviews are recorded in Beads `.38` and private evidence directory
`.build/ni/utf16-serial-plan/20261026-d445f7e/`.

Only this report and passive Beads export change. All Agent/Driver/Tools sources,
prior reports/evidence and installed `0.2.4/build9` remain unchanged. No NI source
recovery, production/test code, compilation, tests, application/device/audio/MIDI,
package/install or security action occurred. SIP stays enabled; NI timing,
calendar/recovery, mappings/calibration and LED authority remain untouched.

Next: **rigkontrol3-40h.39**, implementing only the four-path offline slice below.
All conformance checks below are future requirements, not results from `.38`.
Full original NI recognition in `.2` and reliability `.3`/`.11` remain incomplete.

## Exact reviewed pre-code design

# Native NI UTF-16 serial overload: exact pre-code plan

Date: 2026-10-26. Authority: rigkontrol3-40h.38. Baseline: d445f7e.
This step plans only. No production/test code, compiler/test execution, new NI
body/metadata recovery, runtime, device, package/install or security action.
SIP stays enabled. Full original NI application recognition remains incomplete.

## One existing module, one additional public operation

Add this overload to Agent/NISerialNumber.h/.cpp, in namespace rk3::ni:

  bool get_serial_number(const DeviceInformation* cached_info,
                         bool connected, std::u16string& output);

The existing std::string overload remains unchanged. Neither overload has a
connected default. The new operation is potentially throwing: no noexcept,
catch-to-false, rollback wrapper, termination stage or status/error enum.
No public helper, callback, adapter, dependency object, owner/cache/property,
connection authority, query/refresh, timer, lock or runtime is added.

The caller supplies the same coherent borrowed cache/connection snapshot used
by .35, protects input from concurrent external mutation while it is read or
borrowed, and keeps the owning objects alive through the call. Output is also
synchronized by its caller. Output MAY alias a mutable cached U16 field, including
serial: actual .35 finishes its cache/CF-borrow reads and releases CF, then owns
independent bytes; widening owns independent U16 storage before final assignment.
That intentional output write may update the aliased cache field. It is not a
concurrent-write allowance, and casting away constness of an actually const
object is not admitted. Do not add a no-alias check or blanket unchanged-cache
promise. A pointer is not persistent identity or a connection promise after
return. No information refresh/success gate/generated identity/metadata-derived
connection state. False/empty remain different outcomes.

## Frozen operation and ownership

1. Construct a fresh local std::string bytes, before any wide temporary.
2. Call the ACTUAL existing .35 std::string overload exactly once, with the
   supplied pointer and connected value. A false result returns false without
   widening or output assignment; the local byte string is destroyed normally.
   Do not duplicate its guards outside the producer or bypass it with .31 data.
3. On true, pass bytes.data() AND bytes.size() to the private counted helper
   described below, constructing one owned std::u16string widened temporary.
4. Perform counted COPY: output.assign(widened.data(), widened.size()). Do not
   clear output first, move/swap widened into output, return borrowed storage,
   or replace the assignment with pointer-only/NUL-scanning assignment.
5. Return true, including empty. Normal native destruction retires widened
   before bytes. The output remains independently owned after both retire.

The .37 source contract fixes exceptional resource order:
- Producer or widening-construction exception: destroy the byte temporary and
  propagate the exception. There is no completed widened temporary to destroy.
- Output-copy exception: destroy completed widened storage BEFORE byte storage,
  propagate the exception. Current-field failure preservation is .29's counted
  assignment contract, not a new transactional rollback in this caller.
- No caller-local catch-all/termination landing exists. Do not import .35's
  different CF-release/reserve stages or add new child termination modes here.

Use ordinary native object lifetime in declaration order to obtain this behavior.
Do not emulate x86 tags, 24-byte objects, raw string-pointer transfers or unwind
records. Existing .35 conversion, CF cleanup, reserve/transfer and exceptional
raw-array policy must remain untouched.

## Private counted widening, retained guard and native binding

One anonymous-namespace helper takes (const char* data, std::size_t count) and
returns an owned std::u16string. It is not a new public interface.

- Retain the original explicit unsigned guard FIRST:
  count >= UINT64_C(0x7ffffffffffffff0) throws std::length_error("basic_string")
  before constructing wide storage or traversing input. Use the standard typed
  exception; do not bind a private libc++ class/ABI or manufacture another
  dummy string/max_size()+1 operation merely to provoke a length error.
- The diagnostic "basic_string" is the CURRENT native SDK string diagnostic,
  not a recovered historical NI/private what() contract. Freeze length_error
  category, propagation and guard order; do not claim old message equality or
  no allocation for the exception object's own diagnostic/runtime storage.
- Construct std::u16string(count, u' ') before the loop. The SPACE initializer
  matches the already-proved shared pattern. Native storage supplies its NUL.
- For each index below count, read one char, convert to unsigned char, then
  store the 16-bit value byte<0x80 ? byte : (0xff00U | byte). Express this with
  unsigned arithmetic, independent of plain-char signedness and out-of-range
  signed casts. Preserve counted embedded NUL and one output unit per byte.
- No UTF-8 decoder, normalization, code-point interpretation, strlen-derived
  count, new cap, zero-fill policy or fallback identifier is added.
- Keep compile-time 8-bit-char/16-bit-char16_t/64-bit-size_t checks. Existing
  Darwin CF width/lifetime assumptions remain .35's separate contract.

The original explicit guard is source behavior and is retained even though
native construction may reject a smaller count first for representability.
The native standard string must perform its own valid-object length/allocation
checks. Its SSO, capacity rounding, max_size and allocation requests are native
library/ABI bindings, not x86 wire/functional values to transplant. Do not force
11-unit SSO or original rounded raw requests into a native string object.
The necessary platform changes are valid native C++ ownership, storage and
compiler unwind, plus an explicit sign rule independent of ARM char defaults.
No audio/USB, timing/calendar/recovery or controller-policy change follows.

## Exact admitted implementation paths

Only these four paths, plus passive Beads export:
1. Agent/NISerialNumber.h: add the one overload and its concise contract.
2. Agent/NISerialNumber.cpp: add the private counted helper and overload,
   standard includes/width checks and at most the two private hooks below.
   Existing .35 functions/algorithms remain unchanged.
3. Agent/Tests/NISerialNumberTests.cpp: extend actual-path fixture/oracles while
   retaining all existing 22 byte-public cases and three finite child modes.
4. ReverseEngineering/NIUTF16SerialImplementationPlan.md: retain this exact
   pre-code design and add implementation results separately after execution.

Agent/NIInformation.h/.cpp/Tests, Driver/Tools/shared headers, build scripts,
plists/packages, .29/.33/.35/.36/.37 reports and their evidence stay unchanged.
No agent/main/server/frontend/GP framework project or build integration occurs.
The resulting operation remains an unlinked library, not NI recognition.

## Minimal fixture instrumentation, actual operations

The public overload above is the primary agreed conformance seam. Tests call
it with synthetic owned DeviceInformation and the real .35/CF/std path. No fake
producer result or fake widening/copy replaces that path.

At most TWO cpp-only default-real expression hooks are permitted, following
the existing reserve-hook convention with own markers/undef cleanup:
- RK3_NI_SERIAL_PRODUCE_BYTES(cache,connected,bytes), defaulting to the actual
  qualified std::string overload. Fixture forwarding counts once, binds the
  existing current_output to the real local byte string, calls the actual .35
  getter with the same arguments, captures its live heap pointer/count on true,
  then marks the widening phase. It does not fabricate a result or catch/alter
  exceptions. Stop a repeated-producer mutant on its second invocation before
  the fixture's one-CF-object bookkeeping can produce an incidental failure.
- RK3_NI_SERIAL_ASSIGN_UTF16(output,widened), defaulting to the real counted
  output.assign(data,size). Fixture forwarding records the completed wide
  pointer/count while alive, marks copy phase, and calls the SAME real counted
  std::u16string::assign. It does not fabricate success/value or repair cleanup.

No third guard hook, public DI, lifecycle observer framework, private allocator
replacement or new process helper. Source review verifies default expressions;
semantic controls must mutate code actually compiled/executed by the selected
fixture, never a preprocessor-disabled default and then claim coverage.

Extend the existing bounded allocation ledger only with the needed wide/copy
phase labels and scalar deletion ordinal. Keep actual global new/delete fault
injection and real CF/string operations. Identify byte/wide payload allocations
by captured LIVE pointer identity; do not guess requests from x86 layout or
native rounding. After lifetime ends, compare identities/ledger state only,
never dereference a retired pointer/current_output. Output-liveness checks must
precede value reads when checking ownership controls. Bounded traces must fail
explicitly on exhaustion, not silently discard observations.

The real .35 producer throws before changing its fresh outer byte destination;
that destination is normally empty on producer failure. Do not claim a heap-byte
cleanup oracle there. Widen-allocation failure supplies the actual heap-byte
cleanup witness; output-copy failure supplies wide-before-byte cleanup.

## Actual-path TDD and final checks, future requirements

All items here are planned, NOT executed in .38. Use vertical public slices.
First add the overload declaration/false scaffold and one literal public ASCII
success assertion: strict compilation/import guard must succeed, then the
actual compiled public call must fail the success assertion at runtime (RED).
Implement the smallest frozen real flow and pass that assertion (GREEN), save
both source/log snapshots. Grow one behavior/fault slice at a time, preserving
new functional RED/GREEN evidence where additional implementation is needed.
A missing file/compiler failure or a mock returning a literal is not RED.

Public cases and independent literal oracles:
- Null+false, null+true, nonnull+false: actual byte producer once with unchanged
  arguments; false preserves destination value/data/capacity; no CF/widen/copy.
- Successful empty and ASCII replace old output, return true and terminate.
- U+00e9 cached input becomes .35 UTF8 c3 a9, then units FFC3 FFA9. Non-BMP U+1f600
  becomes units FFF0 FF9F FF98 FF80. Do not derive expected units with the loop
  under test or treat these as decoded Unicode code points.
- Counted cached A,NUL,B produces only U+0041 after .35's C-string stage; a
  leading NUL produces successful empty. Distinct-output cases leave the cache
  unchanged. Add an actual public ALIASED-output case: mutable cache.serial is
  initially U+00e9, pass that same field as output, expect units FFC3 FFA9 after
  return. No input/CF borrow may be used after assignment. Before-assignment
  exceptions preserve the aliased field; copy failure retains .29's current-field
  guarantee. The normal flow needs no special alias branch or new restriction.
- Use 128-unit heap witnesses after asserting their sizes exceed actual native
  byte/U16 inline capacities. With old outputs having insufficient and ample
  capacity, prove COPY: output.data differs from the live completed wide payload,
  both temporaries retire in order and output remains live/valid. For sufficient
  old capacity retain the original in-place counted-copy behavior; do not assert
  an exact SSO size, allocation request or private reserve/shrink policy.
- Real producer CF boundary/byte-allocation faults propagate their original
  type and preserve the wide output before assignment; no widening/copy occurs.
  Do not add the old raw-array fault path just to repeat .35 coverage.
- Actual widening scalar-allocation failure: copy is never entered; heap-byte
  allocation is retired, output value/data/capacity unchanged; bad_alloc escapes.
- Actual output-copy scalar-allocation failure with insufficient old capacity:
  completed wide payload retirement ordinal precedes byte payload retirement;
  output current-field value/data/capacity retained; bad_alloc escapes.
- Normal heap success also proves completed-wide retirement before byte.

Internal helper conformance is separate, not reported as actual producer output:
use a bounded NUL-terminated array A,80,NUL,FF,Z with explicit count5 and literal
units 0041,FF80,0000,FFFF,005A; test valid empty/short/heap counts. This detects
strlen and unsigned-widening substitutions without exposing another public API.

Boundary safety and honesty: test count at the original guard and SIZE_MAX
without permitting real huge allocation or buffer traversal; intercept a huge
storage request before malloc and keep any diagnostic allocation bounded. Do
not assert zero allocations for a length_error's own storage. Preserve the guard
in every normal-input mutant and review safety before execution. The native
constructor also rejects huge counts: observing length_error alone does NOT
prove the explicit source guard is retained or distinguish its removal. Retain
it by source review; no fake mutation success or third hook merely to claim a
runtime distinction. Unknown/unsafe controls STOP rather than execute.

Compile only safe distinguishing semantic controls, in isolated private copies:
unsigned widening; strlen after the retained guard (bounded sentinel-backed
witnesses); cached-UTF16 bypass; repeated actual producer; false/empty conflation;
move instead of COPY; swallowed exceptions; early output clear; premature byte
retirement AFTER widening via a valid scoped swap/destruction (never a dangling
borrow). The cleanup-order control remains valid C++ and can be caught without
reading freed storage. Each counted runtime control must compile successfully,
pass import/architecture guards and fail its relevant assertion with normal
exit1; no crash/signal/timeout or compiler error counts. Do not run a new noexcept
control that would terminate the entire fixture, or repeat .35's 15 controls.

Build/run policy for the future implementation:
- Strict: xcrun clang++ -std=c++17 -arch arm64 -mmacosx-version-min=13.0
  -Wall -Wextra -Werror -pedantic -g -O2; fixture links CoreFoundation.
- Sanitized: same, -O1 -fno-omit-frame-pointer -fsanitize=address,undefined.
- Compile a separate production NISerialNumber.o with strict flags; lipo/nm
  checks and saved otool libraries for fixtures. Inspect imports BEFORE each
  execution, rejecting device/query/USB/audio/MIDI/IPC/live runtime imports.
  Production needs only CF, std strings/stdexcept/allocation/compiler runtime.
- Fixture retains only existing own-binary, fixed-argument three child modes
  and their bounded wait/pipe helpers. No new children, fork/shell/app launch,
  broadened import exception or new termination policy. Preserve .35's exact
  controlled markers/exits and child ownership/reaping. Child _Exit is not a
  leak-freedom proof; previous intentional raw-array policy remains explicit.
- Run final expanded serial strict+san once each after source is final: this
  already reruns .35's 22+3 cases; do not run a separate frozen duplicate.
  NIInformation h/cpp/fixture stay byte-identical and no information-acquisition
  path changes, so no extra .31 or Driver/Tools/physical/audio rerun is needed.
  Any proposed preserved-path change STOPs for an amended plan and relevant gates.
- Fresh evidence outputs, source/tool/header pins, commands, build/import/run
  statuses and stopped attempts preserved. Do not present old results as new.
- Independent Spec and Standards/ownership/test-efficacy reviews of actual final
  code/fixture, then report/evidence fidelity review. Resolve findings before
  closure. Git diff checks/source pins and Git+Dolt pushes finish the session.

## Source and platform authority

Reuse the already exported StringASCII normal body and .29 fill/counted-copy/
growth/import evidence, completed .37 caller224B/LSDA44B/three action0 rows, and
actual .35 module/fixture. StringASCII noLSDA is not cannot-throw proof; its16
unclassified bytes remain unread and no more NI body recovery is admitted.

Local native SDK string header:
/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1/string
SHA256 d92c171760a81ad2564b4d065c5f1599fe7e6d2a3d1e9f74ead391fc293d2fec.
Current constructor/length/max_size/assignment declarations interpret native
bindings, not historical NI-private implementation or what() equivalence.

Only this planning report and passive Beads export change in .38. A separate
implementation card must carry this exact reviewed plan before production/test
code. Full .2 original NI recognition/agent lifecycle/session/events/policy and
.3/.11 reliability remain REQUIRED/INCOMPLETE. Installed frozen0.2.4/build9 source
e16097a145960471b6dd37ebbb795f86c4e3ed02/rollback20261026T110147Z-3be14ddc,
NI ring/calendar/timing/recovery, mappings/calibration/LED authority and SIP
settings remain untouched. Mutable default drivers still build8; no package,
installation, NI app/hardware acceptance or security action is part of this plan.
