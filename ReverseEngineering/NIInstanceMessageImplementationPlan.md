# Original instance-message writer: implementation result

`rigkontrol3-40h.41` implements only the frozen `.40` plan, authorized by the
user on 2026-10-26. The historical planning report and its exact 17,602-byte
plan are preserved below without edits. Plan SHA256:
`074db89cff2668b6cf783fd282fa70962e3500969320e0c22f2c81c9761229e5`.
The historical report's wording about awaiting implementation describes `.40`.

## Implemented operation

`Agent/NIInstanceMessage.h/.cpp` provides one void writer and a caller-owned
1052-byte storage alias. It ports NI's original `MessageSetUTF16String` normal
operation from the existing 210-byte/62-instruction export. No additional NI
binary or metadata was read or recovered. The remaining 14 bytes of the
original 224-byte cap remain unclassified.

The writer retains low-32-bit increment wrapping before unsigned clamp512,
full raw-type OR02000000, count/reply control words, untouched caller bytes
16..23, exactly1024 bytes of character clearing, unconditional first-unit load,
bounded NUL copy and capacity-only previous-unit surrogate backoff. The copied
last unit remains after an earlier backoff NUL; declared counts do not shrink.
Natural source-NUL termination does not apply that backoff. Wrapped0/1 uses a
valid byte cursor inside the whole storage, including the previous word at+26.

Platform bindings replace GP/x86 object layout with a valid native owned U16
input and explicit caller-owned byte storage. Fixed-width/little-endian checks
and memcpy word accesses preserve values without alignment or aliasing errors.
The original zero operation uses public native bzero. No owning temporary,
allocation, extra status, noexcept/catch policy, prep Boolean, getter, list,
mutex, sender, connection authority or runtime is added. Source and storage
remain separate, alive and stable; caller prestate is neither read nor repaired.

The separate strict ARM64 production object has exactly one exported writer
and only `_bzero` undefined. The library remains unlinked from all driver and
agent runtimes. This result does not publish an actual NI message or prove
application recognition.

## Executed offline checks

The fixture includes the actual implementation to reach its private counted
writer only for the explicitly planned synthetic wrap/prefix cases. Public
calls use real owned native strings. No giant/fabricated std object or invalid
pointer was created. Every private source has512 readable units. Literal
headers/units/count tables, untouched caller prestate, canaries, cleared tails,
source snapshots and call-only scalar/array new counters exercise the result.
No source normalization, separate query-gate imitation or faulting read oracle
is used. Physical first-read behavior is source-reviewed rather than claimed
from identical output bytes. No leak-freedom claim is made.

| Gate | Recorded result |
| --- | --- |
| Initial direct-compiler attempt | Compile exit1: C++ array header not found; no executable run and no functional RED credit |
| Actual public ASCII RED | Strict compile0, ARM64 and imports checked before run; no-op writer exits1 at ASCII payload24 assertion |
| Actual public ASCII GREEN | Same public test against real writer, compile0/guard/run0 |
| Separate production object | Strict compile0, ARM64; only bzero import and one public writer; no execution |
| Final strict suite | Compile0/ARM64/import guard/run0;19 public calls and20 bounded private-prefix cases |
| Initial sanitized startup | Compile0/guards then abort-6: macOS rejects detect_leaks=1; no positive result credited |
| Final ASan+UBSan suite | Compile0/ARM64/import guard/run0;19 public calls and20 private-prefix cases; no sanitizer diagnostics |

Public cases cover ASCII/empty/full raw-type bits, counted embedded NUL,
length510/511/512/700, natural unpaired surrogates, capacity high/low and
non-low residuals, non-high previous units, leading NUL at clipped count,
FFC3/FFA9/FFFF units and long-to-short reuse. Private literal count/prefix
cases cover0,1,510,511,512,fffffffe,ffffffff,100000000,1ffffffff,SIZE_MAX,
each with nonzero and leading-NUL source. The actual writer allocates no native
heap in any tested call; production source/import review independently excludes
allocation. Count bounds are checked before any derived wire-span pointer.

Both independent source/fixture/control-safety reviews passed before final
positive and control executions. All nine isolated source-only controls then
compiled successfully, passed ARM/import guards and failed the intended normal
assertion with exit1. No compiler error, signal, timeout or startup abort counts
as a rejected semantic control:

| Control | Assertion detecting it |
| --- | --- |
| Mask raw type bits | full message type |
| Wide/preclamped count | declared payload count |
| Clear only copied prefix | ASCII terminator and cleared tail |
| Zero opaque caller words | opaque caller words unchanged |
| Continue bounded copy past NUL | embedded NUL stops before B but retains declared size |
| Inspect current surrogate unit | capacity terminator backs over previous high surrogate |
| Erase the residual in valid backoff | capacity residual after earlier NUL retained |
| Backoff on natural NUL | natural unpaired high surrogate retained |
| Include first12 control bytes in payload count | ASCII payload24 |

Strict flags: C++17, arm64, minmacOS13, Wall/Wextra/Werror/pedantic/g/O2.
Sanitized flags use O1/frame-pointer/ASan+UBSan. The explicit xcrun SDK
isysroot fixes native C++ header search; it changes no writer behavior. The
sanitized retry disables only unsupported LeakSanitizer detect_leaks; ASan and
UBSan halt-on-error remain enabled. Source stayed unchanged after first ASCII
GREEN; the expanded fixture stayed unchanged through final gates and controls.
These two stopped attempts and their exact runner versions are preserved.

Per-run snapshots, compiler path/hash/version, SDK path, exact argv/exits,
lipo/nm/otool outputs and pre-run import decisions are saved under
`.build/ni/instance-message-implementation/20261026-85149c7/`.
Compiler identity was captured before each compilation; remaining tool and
public SDK-header identities are captured at publication, not claimed as earlier
pre-run checks. Initial runner was saved after its compile stop, explicitly.
`final-evidence.json` pins this report, generated evidence, final source and
all99 unchanged existing Agent/Driver/Tools files. Independent final fidelity
review results are recorded in Beads externally to avoid a circular self-hash.

Final source SHA256:

- Header:352fe148d65dbcca0e6904a83d7d217b1d982ec18f253f18a4159d1c79096582.
- CPP:c7f0e6bc017ede65dacff24e5efae105b23c8492ccc42ca9ee74d1fd3489dcd7.
- Fixture:18d1e81ee7594a1351e058d401cb5afa56ff3cebd421d0445711746a0f6799b5.
- Nine control definitions:53943158f450f41c85901c797dea8a65e0370b43cefcb7bd12a65e9e468f8dee.

## Scope and remaining integration

Only the three admitted new source/fixture paths, this existing report and
passive Beads export change. All99 existing Agent/Driver/Tools paths are
byte-identical; old serial, information, driver and physical suites were not
repeated. The installed frozen0.2.4/build9 package is unchanged. No driver
build/package/signing/install, real NI port/application, device/audio/MIDI/USB
operation or security change occurred. NI ring/calendar/timing/recovery,
mappings/calibration/LED authority and SIP settings remain untouched.

`.42` is open, unstarted planning of the smallest original query/caller step.
Original nonnull/true-only gate, shared serial scratch across the list, same
message storage, original mutex/getter/construct/send order, ignored send result
and scratch destruction before unlock remain required. The query's65-byte/EH
coverage gap and actual untouched-word storage provenance need a precise plan
before any additional original reads or code. No guessed zeros, new policies
or automatic broader recovery are admitted. Full `.2` original NI automatic
recognition/lifecycle/session/events/policy and `.3/.11` reliability remain
required and incomplete.

---

## Preserved historical planning report

# Original NI instance-message writer plan

`rigkontrol3-40h.40` completes planning only. The next implementation card is
`rigkontrol3-40h.41`; it remains open and unimplemented, awaiting a separate
instruction. No source/test code was changed or compiled in this step.

The selected slice is NI's existing in-place `MessageSetUTF16String` writer,
using a native owned UTF-16 input and caller-owned 1052-byte message storage.
It preserves the original wrapping count, untouched caller words, bounded
copy and surrogate truncation behavior. The only platform bindings are valid
native string/storage types, fixed-width byte accesses and the public native
memory-zero primitive. The writer remains unlinked; it sends no message.

Both independent plan reviews passed without required corrections: Spec,
framing and bounds; Standards, scope, ownership and test efficacy. Reviews
applied to the exact 17,602-byte plan below, SHA256
`074db89cff2668b6cf783fd282fa70962e3500969320e0c22f2c81c9761229e5`.
These are plan reviews, not executed implementation tests.

Existing exports suffice for this writer. No new original NI binary/metadata
recovery, device/runtime test, installation or security action occurred.
Private planning evidence is `.build/ni/instance-message-plan/20261026-a4e2e61/`.

Original query true-only gating, its shared scratch/list/lock/send lifecycle,
remaining 65-byte/exception gap and actual untouched-word provenance are
explicit future integration gates. Full original NI automatic recognition
and reliability remain incomplete in `.2`, `.3` and `.11`. Installed build 9,
NI audio/ring/timing/recovery and SIP remain unchanged.

The following frozen pre-code plan is preserved byte for byte. Later
implementation results must be recorded separately from this historical plan.

---

# Original NI instance-message writer: exact native pre-code plan

Date: 2026-10-26. Authority: rigkontrol3-40h.40. Baseline: a4e2e61.
Planning only: no production/test code, compilation/tests, new NI byte/metadata
recovery, runtime, device, installation or security action in this step.
SIP stays enabled. Full original NI application recognition is incomplete.

## Selected original operation and scope decision

Port the existing MessageSetUTF16String constructor 0x1002cf040 as one in-place
native message writer. This is the smallest original publication operation
whose functional behavior is already proved. It consumes the U16 value produced
upstream, preserving the constructor's actual bytes and count arithmetic.

Do NOT add a convenience preparation Boolean/helper around the getter, new send
callback, per-controller local serial scratch, controller list, lock or runtime.
The writer itself is unconditional and has no producer/status result. Do not
claim its tests prove the original query's true-only publication gate.

Retain the existing query contract for later integration: nonnull client, original
mutex held across list iteration/getter/constructor/send, one serial scratch
initialized once and reused across the whole list, one message storage reused,
only true getter results constructed/sent, true-empty still sent, false scratch
not published, send return ignored, scratch destruction before unlock. No
synchronous device-list reply or retry policy is added. The old query export
has 223 decoded bytes/60 instructions inside its 288-byte cap, leaving65 bytes
and selected exception cleanup unproved. That gate belongs to a future whole
query/lifetime plan; do not recover it merely to implement this pure writer.

## Existing proof, coverage and limits

Original NIHardwareAgent1.7.4/R199 image SHA256:
f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258.
Only already exported source/report artifacts are used here.

Writer export: 210 decoded bytes/62 instructions inside original224-byte cap
[0x1002cf040,0x1002cf120). The remaining14 bytes stay unclassified and unread;
do not call this full-cap recovery. The normal writer has only the named
___bzero memory operation and scalar/word loads/stores, with no owned C++ local,
allocation, caller list or lock. No new resource-unwind policy is needed for
this selected operation. Leave its native function potentially throwing; do
not infer or add noexcept/catch/termination from a partial export or C API name.
Imported memory-runtime internals remain typed boundaries, not recovered code.

Writer ASM SHA32933979ceb3b2e514bfcb9d2ebe28b1a0d00bf96fce6239b22735287771c43e;
writer audit SHA956e11b67d5fa1689497e68a2d2ecdb62c2a263c86bda167a0ee3c8900048cd9;
writer phase-F evidence SHA392c43a0cab87e29d33cc62171aa62c666fe9c7ba82cd8317043f4680ca01dc1.
Query ASM SHAbee7b2f94bacac7a90c3a33d65cc3cc98287a1b5286160aa92f9536a50874ad1;
query audit SHA2b7ca67cdef25ca90634facccd606b963def192a04df60a5cf0143636b2b3d27.

## One native module, one public writer

New Agent/NIInstanceMessage.h/.cpp in namespace rk3::ni:

  using InstanceMessageStorage = std::array<std::uint8_t, 1052>;
  void write_instance_message(InstanceMessageStorage& storage,
                              std::uint32_t raw_type,
                              const std::u16string& value);

One storage alias and one function; no public payload/view/count helper,
constructor object, status enum, default type, allocation or transport adapter.
Raw type is the original constructor parameter, not a newly interpreted enum.
The existing query supplies raw 0x00444e2b; the resulting word is 0x02444e2b.
Preserve all other supplied bits when ORing 0x02000000, not a low24 mask.

Storage is the exact fixed in-process message area, not just its wire payload.
The caller owns its lifetime and exclusive mutation, source object/data remain
alive/stable through the call, and source/storage do not overlap. This matches
the selected original query's separate serial object and stack message; do not
add a snapshot/copy allocation to repair unsupported overlapping storage.
The type is native byte storage; never memcpy an x86 C++/GP object into it.

Bytes16..23 are caller prestate, not writer-defined identity/version/product
fields. The writer neither reads, validates nor initializes them. Tests supply
defined nonzero bytes before observing them. A caller may only observe/send
those bytes after establishing their values; this plan does NOT assert the
original query initialized them (its normal body does not). Future runtime
binding must resolve caller-storage provenance; silently supplying zeros is
not admitted. All other writer-defined words/character storage follow below.

## Exact internal layout and wire distinction

Use native little-endian word values with explicit fixed widths and valid byte
accesses. Header/control offsets are internal offsets from storage.data():

- +0: first16, then final payload byte count16+2*n.
- +4: reply offset16. +8: reply count0 (not a requested reply).
- +12: raw_type OR 0x02000000, retaining all supplied bits.
- +16 and +20: untouched four-byte words, no invented semantics/defaults.
- +24: declared U16 count n, not actual scanned string length.
- +28..+1051: exactly1024 character-region bytes, cleared before bounded copy.

n = min_u32(wrap_u32(low32(original U16 logical length)+1),512).
The increment wraps at32 bits BEFORE the unsigned clamp. Header n and payload
count do not shrink after a source NUL or surrogate backoff. With n512, payload
is1040 bytes and the whole internal area is12+1040=1052. With n0, payload16
contains no character units, although the writer still clears its full region.

Original transport sends storage+12 for the count at+0; the first12 bytes are
in-process count/reply control, not wire bytes. The first wire word is the full
message type at internal+12. No real port/sender/consumer is run in this slice.
Tests must validate count<=1040 BEFORE making any derived wire span, including
for bad-count controls; never construct an out-of-bounds view to observe failure.

## Frozen copy/truncation behavior

Implement one private counted writer taking the same storage/raw type plus
(const char16_t* source, std::uint64_t logical_length). Public entry obtains
actual value.data()/size() and delegates. Width checks keep64-bit size/count.
Private word load/store helpers may use std::memcpy; no public extra interface.

Retain the original source-level sequence: set+4/+8/+12; clear ONLY1024 bytes
at+28; set base count16; derive/store n and final count; read firstsourceunit;
perform the following bounded NUL-copy; write final terminator. No whole-frame
zeroing, metadata repair, new source normalization or shorter zeroing pass.

- Start destination cursor at internal byte28 and ordinal1. Always load source[0]
  even when n0. If it is NUL, write NUL at the current cursor and finish.
- For each nonzero loaded unit, write it at cursor. If ordinal>=n, take the
  CAPACITY exit immediately (do not read source[n]). Otherwise advance cursor
  by2, load source[ordinal], increment ordinal, and continue only if nonzero.
- On capacity exit, inspect the PREVIOUS destination word at cursor-2.
  If (word & 0xfc00)==0xd800, move cursor back by2. Then write NUL at cursor.
  Do not inspect/validate the just-copied last unit as a low surrogate.
- On ordinary source-NUL exit, write NUL at current cursor with NO surrogate
  cleanup. An unpaired high surrogate at natural string end remains unchanged.
- If backoff moved the terminator earlier, the just-written final unit remains
  AFTER that NUL inside the advertised n units. Do not zero/erase it or shorten
  header counts. This residual is original behavior, not an error to fix.

Cursor is a byte offset into the WHOLE1052-byte object, not a pointer restricted
to a separate U16 subarray. For n0 or1 with firstsourceunit nonzero, capacity
exit examines internal+26 (upper half of freshly written nword, zero); cursor-2
is valid within storage. Do not underflow a U16 data-index or read before its
own subarray. Valid accesses stay inside the full byte storage.

Readable source prefix is at most max(1,n)<=512 units, or through an earlier
NUL. Public owned strings provide the needed terminator/prefix. Private tests
may supply a bounded512-unit prefix with synthetic logical_length for wrap
cases, without constructing a fake giant std::u16string/view/GP object or
allocating multi-gigabyte input. These are internal arithmetic/prefix oracles,
not claimed actual producer outputs. The first-read rule is source-reviewed;
final-byte equality alone cannot prove physical reads, and no faulting-pointer
or crash oracle is admitted to manufacture such a distinction.

## Necessary native binding, no new policy

Represent input as a valid native const U16 string, and output as explicit byte
storage; replace GP short/long tag access and x86 layout with native data()/size().
Use the native public bzero declaration from strings.h for the same1024-byte
zero operation, not a historical stub address/private declaration. Let native
compiler/libc choose its memory-zero entry. Word access uses std::memcpy to
avoid alignment/aliasing violations in byte storage, preserving original values.

Compile-time checks: CHAR_BIT8, sizeof(uint8_t)==1, uint16_t/char16_t2,
uint32_t4, uint64_t/size_t8 and little-endian target; std::array storage size1052.
No big-endian codec or old GP ABI emulation is needed. Native word/memory
primitives are typed boundaries; do not recover their implementations. No
algorithmic retuning, buffer-length status or changed exception/connection
policy. No audio/ring/calendar/retry/timing/recovery/controller change follows.

## Exact future implementation paths

Only four paths plus passive Beads export:
1. Agent/NIInstanceMessage.h (new): storage alias/function/concise lifetime and
   untouched-word contract, no extra public helpers.
2. Agent/NIInstanceMessage.cpp (new): public delegate/private counted writer
   and fixed-width byte primitives, no owning local, allocation or test hooks.
3. Agent/Tests/NIInstanceMessageTests.cpp (new): actual public operation and
   clearly labelled private bounded wrap/prefix cases.
4. ReverseEngineering/NIInstanceMessageImplementationPlan.md: retain this exact
   pre-code plan and add later implementation results separately.

No existing Agent/Driver/Tools/header/buildscript/plist/package/concert or prior
report/evidence changes. Serial module/getters remain untouched and unlinked.
No preparation Boolean/helper or full query integration is admitted here.

## Actual conformance/TDD plan, not executed results

Public writer is the agreed primary seam; private counted writer is used only
for valid-prefix logical lengths impossible to create as small owned inputs.
Future implementation begins one literal ASCII test against a compiled no-op
writer scaffold: strict compilation/import guards succeed, actual public runtime
fails expected word/byte assertion (RED). Implement the smallest source-faithful
writer, obtain GREEN, preserve snapshots/logs. Extend one behavior slice at a
time. Compiler failure/missing symbol or a fake writer is not functional RED.

Public case oracles use defined caller storage with distinct bytes at16..23,
source snapshots and outside canaries. Compare literal header words/critical
units plus whole cleared tail. Do not recompute expected output with a copy of
the writer algorithm or use the fixture to fake query gating.

- Empty: n1/payload18; all1024 data bytes zero; opaque8bytes unchanged.
- ASCII RK3: n4/payload24; units0052,004b,0033,0000 then zeros.
- Raw type00444e2b→02444e2b, already02000000 bit retained, high-bit example
  81444e2b→83444e2b (no low24 mask); count/reply/type/control vs wire offsets.
- Logical string A,NUL,B has n4/payload24; stops at firstNUL, B is not copied,
  tail remains zero. Declaredcount reflects logical size, not scanned length.
- Length510→n511/count1038, length511→n512/count1040, length512→n512/count1040;
  literal known Q prefixes and specified final slots. Longer owned input may be
  used as a bounded witness, never allow copied prefix beyond512.
- Natural source end containing unpaired D800 stays D800 before NUL, including
  length511 ending at510 while n512. Do not run capacity cleanup just because
  declared n equals512.
- Capacity case:512 nonzero units, at510 D800 and511 DC00; expected510=NUL,
  511=DC00 residual, previous Q prefix intact, n512/count1040 unchanged.
  Repeat with DBFF then 'Z': backoff still occurs and residual005a remains.
  Non-high preceding D7FF or DC00 does not backoff; final511 becomes NUL.
- Leading source NUL with large logical length leaves all datazero but retains
  declared clipped count. Pure16-bit inputs including FFC3/FFA9 are copied as
  units, not decoded or converted again. Source contents/lifetime unchanged.
- Reuse same storage for a long then short write with persistent opaque8bytes;
  no stale data except the specified residual within the current cap case.
- No native heap allocation within writer: count actual globalnew attempts
  around only the call, with inputs/expected values prepared outside that phase.
  No allocation-fault/cleanup/termination machinery invented for this pure writer.

Private internal metadata/prefix literal cases, all backed by512 readable units
so every reviewed wrong-count control still has a valid bounded prefix:
logical lengths0,1,510,511,512,fffffffe,ffffffff,100000000,1ffffffff,SIZE_MAX.
Expected n respectively1,2,511,512,512,512,0,1,0,0; payload18,20,1038,1040,
1040,1040,16,18,16,16. Use firstunit nonzero and zero variants for wrapped0/1.
No fake native string header/view or inaccessible/null source pointer. Preserve
explicit readbound in source, and let sanitizers verify only valid actual accesses.
Do not claim a runtime read distinction from identical final bytes.

Safe isolated compiled controls must exercise actual writer/private algorithm,
retain bounded read/write ranges and fail relevant normal assertions, not crashes:
wrong type masking; nonwrapping/wide count then clamp (privateprefix512 safe);
clear only copied prefix instead of1024; zero opaque words; bounded continue
past source NUL through atmostn (public string has valid length+terminator,
private prefix512); check current rather than previous surrogate unit; clear the
residual slot ONLY inside a valid backoff branch; backoff on ordinary NUL exit;
and wrong payload count including the first12 control bytes. Validate the field
count before any wire-span construction, so a bad count never makes a bad view.
Unknown/unsafe controls STOP for review. Count only compile0/import/ARMpass then
intended assertion with normal exit1; no compiler errors/signals/timeouts as passes.
No need controls for a byte-identical first-read change; source review covers it.

Final future gates: strict xcrun clang++ C++17/arm64/minmacOS13/Wall/Wextra/Werror/
pedantic/g/O2, plus O1/frame-pointer/ASan+UBSan; fresh binaries/evidence. Fixture
may include actual cpp to reach the private counted seam without public API.
Compile separate strict production object, save lipo/nm/otool evidence and inspect
imports BEFORE each run. Production needs only memory/native data/compiler
runtime; no allocation, serial getter, CF, device/audio/MIDI/USB/IPC, clock, process,
file/network/app import. Fixture may allocate its synthetic strings and print
assertions; no children, threads, port, timer, file/network/device operations.
No CoreFoundation/CoreAudio framework required for this data-only slice.

Run final positive strict+san once each, safe reviewed controls separately.
Existing serial/NIInformation/Driver/Tools sources remain byte-identical and are
not linked/changed, so do not repeat their frozen suites, oldcontrols or physical
checks. Preserve all earlier evidence. Any preserved-path change STOPs for an
amended plan/relevant gates. Independent Spec and Standards/framing/bounds/
test-efficacy reviews, then final report/evidence fidelity reviews, before
closure. All source/tool/header/commands/exits/stopped attempts preserved;
Git diff/sourcepin checks and Git+Dolt pushes finish the session.

## What this does not prove, and next integration constraint

This is a code-ready plan for the writer only. Future tests may show raw frame
compatibility with existing .39-produced unit values and .20 wire layout; they
cannot claim actual query true-only behavior, list/lock/scratch lifecycle or
send-result handling until those production paths are implemented from proof.
Do not add test-only if(getter) logic and present it as that production gate.

Before whole query code: retain caller scratch across the list, same message
storage across successful iterations, original mutex/getter/send order and
ignored send result; assess the existing65-byte caller/exception gap precisely,
and resolve actual untouched-word storage provenance. Do not fill the gaps with
fresh strings permessage, guessed header zeros, retries, status or fake version.
No new original recovery is performed/admitted by .40, and no automatic further
body recovery is needed for the selected normal writer operation.

Full .2 original NI automatic recognition, runtime/lifecycle/session/events/policy
and .3/.11 reliability remain REQUIRED/INCOMPLETE. Installed frozen0.2.4/build9
sourcee16097a145960471b6dd37ebbb795f86c4e3ed02/rollback20261026T110147Z-3be14ddc
unchanged; mutabledefault drivers still8, no installation here. SIP, NI ring/
calendar/timing/recovery/mappings/calibration/LED authority stay unchanged.
