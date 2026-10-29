# Original NI instance serial: planning decision

Date: **2026-10-26**. Beads authority: **rigkontrol3-40h.36**.
Baseline: `2ed6969`. This result plans the next step; it implements no code.

**Retain the original byte-serial producer, counted signed-byte widening and
counted output copy.** Reuse `.35` and the already proved `.29` assignment.
Do not decode the produced bytes as UTF-8 or publish cached UTF-16 directly.

One indispensable source gap prevents a code-ready plan: the old ControllerBase
caller export contains **170 decoded normal-path bytes inside a 224-byte cap**.
Its missing **54 bytes** and selected exception metadata must establish cleanup
before native ownership/unwind is chosen. The StringASCII audit separately
leaves 16 unclassified bytes in its existing 240-byte cap; those bytes are not
automatically admitted as new work or assumed to be padding.

The independently reviewed plan below confines the next source task to that
caller and the selected unwind facts. It reuses the known assignment, conversion,
notification and frontend contracts. The proposed native overload is conditional;
no production/test file, interface or cleanup policy is admitted by this result.

Exact Beads design: **11,797 bytes**, SHA256
`ee0daf06e0040d1e9d4f5ca6ac5eae2c3684762646341a3968ffc7aa9374af7f`.
Independent Spec/scope and Standards/test-efficacy planning reviews passed.
Private planning evidence: `.build/ni/instance-serial-plan/20261026-2ed6969/`;
its final manifest hash and exact-fidelity review are recorded in Beads `.36`.

Next: **rigkontrol3-40h.37**, source completion only. It must record/review exact
metadata and instruction/data packets before reading them; this pass performed
no new NI recovery, reader authoring, compilation, tests, application, endpoint,
device, audio/MIDI, package/install or security action. Existing Agent/Driver/Tools
sources, `.31`/`.33`/`.35` results and installed `0.2.4/build9` remain unchanged.
SIP stays enabled, with NI ring/timing/recovery/mappings/calibration/LED authority
preserved. Full original NI recognition in `.2` and reliability `.3`/`.11` remain
incomplete. The future checks below are requirements, not executed results.

## Exact reviewed pre-code governing plan

# Original NI instance serial: minimum next-step plan

Date: 2026-10-26. Authority: rigkontrol3-40h.36. Baseline: 2ed6969.
This is planning only. No production/test code, new NI byte/metadata recovery,
compilation, runtime, device, package/install or security action is admitted
or executed by this plan. SIP must stay enabled. Full NI recognition is open.

## Decision

The next desired library operation is the original ControllerBase UTF-16
serial getter, composed with the implemented .35 byte-serial getter. Retain
its original sequence: fresh byte string; call the byte producer exactly once;
false returns without output assignment; true constructs a counted StringASCII
temporary; copy its counted units into the existing output; destroy temporaries;
return true, including an empty result. Do not publish .31's cached UTF-16 field
directly, decode the produced bytes as UTF-8, or substitute a device UID/label.

Implementation is NOT admitted yet: the existing caller export proves only
the normal path. Its missing tail and selected exception metadata must be
resolved before choosing native cleanup/noexcept behavior. The minimum next
executable task is a bounded source-completion prerequisite, described below.
This is a concrete prerequisite for preserving NI behavior, not an invitation
to recover the whole GP framework, agent, transport or application.

## Existing evidence and what it proves

Original source is NIHardwareAgent 1.7.4/R199, x86_64 image SHA256
f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258.
Only previously exported artifacts were read in this planning step.

| Existing operation | Already proved contract and limit |
| --- | --- |
| ControllerBase getter, 0x1002df3a0 | Fresh std::string; virtual +0xc8 producer; false skips assignment; true extracts actual pointer AND logical size, calls StringASCII, then pointer/count COPY via 0x100007f40. Normal widened-temp deletion precedes byte-string destruction; returns true for empty. Export has 52 instructions/170 bytes ending RET 0x1002df449, inside the old 224-byte cap [0x1002df3a0,0x1002df480). The remaining 54 bytes are unclassified. No caller unwind/LSDA cleanup proof was found among existing exports. |
| StringASCII thunk/body, 0x1002ae5f0/0x1002ae600 | Body reads explicit CharSequence count, constructs owned U16 storage, sign-extends each byte and writes a U16 terminator. 0x80 becomes 0xff80; 0xff becomes 0xffff. Counted embedded NUL units survive this helper. Existing audit covers 71 instructions/224 bytes of the 240-byte cap [0x1002ae600,0x1002ae6f0); the other 16 bytes have not been classified. This is not blanket proof of allocation/unwind behavior. |
| Exact U16 copy 0x100007f40 and growth 0x100008140 | Fully recovered in .29: counted copy, NUL/length commit, allocation before commit, current destination retained on selected allocation/length throws. Reuse its proof; no new assignment/allocator body recovery. .31 already uses native owned U16 fields and the corresponding copy operation. |
| Byte serial operation .35 | Actual get_serial_number(cache,connected,std::string&) is implemented and independently reviewed. Its conversion, false/empty distinctions, ownership and selected exception contract are authoritative. Remains an unlinked library. |
| Query/writer/consumer | Existing query 0x10000f120 calls virtual +0xc0 under its original list mutex, emits 0x02444e2b only on true in response to 0x02447143, and produces no synchronous list. Existing UTF16 writer count/clamp/surrogate/unwritten-word rules and unchanged frontend consumer at internal +28 remain later integration contracts; do not implement them in this slice. |

The endpoint proof helper ni_ipc_proof_name_units deliberately calls strlen
because its endpoint-name caller did. It is NOT the general counted serial
converter and must not be reused for this operation. A future private counted
helper can test embedded-NUL preservation, while actual public composition
must retain .35's earlier C-string truncation. These are different test seams.

## Precisely bounded prerequisite, before implementation

Create one dependent source-completion task. Its question is: what cleanup,
propagation or termination does the original ControllerBase getter perform
when its producer, StringASCII construction or output copy throws, and which
selected StringASCII allocation/cleanup facts are indispensable to port it?

1. Rehash the original image and existing evidence/pins. Use existing original
   LC_FUNCTION_STARTS and source/section maps; verify the caller's original
   next-start cap [0x1002df3a0,0x1002df480), 224 bytes. Do not infer function
   completion from the old normal RET or treat the missing tail as padding.
2. First record/review a metadata-only reader packet in Beads: exact two target
   entries 0x1002df3a0 and 0x1002ae600; select their compact-unwind entry/encoding,
   personality index and presence/absence of LSDA. If LSDA exists, derive its
   exact section/file bounds from original indexed rows without reading contents.
   Import associations are metadata names only for already observed calls and
   any indispensable new tail calls; no stub, pointed-to object or callee body.
   Absence of LSDA is a fact to establish, not a parser assertion failure to hide.
3. After exact derived bounds/tool hashes/questions/stops are recorded and
   independently reviewed, decode ONLY the caller's already selected 224-byte
   cap with the bounded LLVM reader used in .33. Reconcile it with the old 170
   normal bytes and classify the missing 54; do not count an old Ghidra body cap
   as proof that all bytes were decoded. No recursive analysis or decompiler.
4. Parse only selected, exactly bounded unwind/LSDA records required to relate
   calls to landing pads, cleanup/rethrow or compiler termination. Record raw
   bytes, interval endpoints, actions and the imported destructor/delete names.
   Unsupported encoding, out-of-cap landing, chained/unresolved type action,
   unknown indirect target or changed source hash STOPS that packet. A separately
   recorded/reviewed exact amendment is required; never expand automatically.
5. The StringASCII normal loop is already available. Its missing 16 bytes are
   not an automatic second body admission. Inspect that same 240-byte cap only
   if the selected metadata establishes a relevant cleanup landing/coverage gap
   needed for the chosen operation, with the reason and exact packet reviewed
   first. Otherwise record why existing normal proof plus selected metadata
   suffices. Do not rerecover .29 assignment/growth, .35 converter, framework,
   std/allocator/unwinder implementations, query, session or frontend bodies.
6. Record the exact source result, coverage, failures and remaining limits in
   the source task and independently review it. Only then finalize an ARM
   implementation plan. A source-completion task is not an implementation card.

This task performs NONE of those new reads or decoding. No reader is authored
or executed here. Exact unknown LSDA sizes/offsets are deliberately not guessed;
they are the first metadata-stage result, before any instruction/data packet.

## Conditional native operation and ARM justification

Expected narrow module: extend existing Agent/NISerialNumber.h/.cpp with a
UTF-16-output overload of get_serial_number(cache,connected,output), plus the
existing serial test fixture and a separate result report. This mirrors the
original two serial overloads and hides the conversion behind the same small
interface. It is a candidate, NOT a frozen coding contract until the prerequisite
proves its remaining cleanup/allocation behavior.

Reuse one coherent caller-owned DeviceInformation/connected snapshot, with
backing lifetime through the call; add no connection/default/readiness authority,
cache, device refresh, callback framework, backend/owner/property or lock/timer.
Use the actual .35 producer once; explicit bytes/count to a private counted
signed-byte widening operation; counted COPY to output (not ownership transfer).
False and exceptions before the assignment point cannot mutate output; selected
assignment failures follow .29's current-field contract. Temporary cleanup and
any nonthrowing stages remain UNDECIDED until the source prerequisite; do not
assume RAII or borrow .35's different unwind contract.

The necessary platform substitution is valid native C++ owned objects,
allocation and compiler unwind instead of x86 24-byte string/tag/vtable/EH
representations. Source byte sign extension is explicit and must not depend on
ARM's plain-char default. Keep 8-bit bytes and 16-bit units with compile-time
width checks. No new codec, normalization, size cap, fallback identifier, exception
to false conversion or historical-private allocator equivalence is justified.
Original ownership/allocation ordering must guide the final native plan.

## Future actual-operation checks, not executed results

Freeze these only after the source result and exact implementation plan review:

- Meaningful actual public runtime RED then GREEN; false/null/disconnected retain
  sentinel output and perform no widening; successful empty replaces old output
  and returns true; byte producer called exactly once using the supplied snapshot.
- Real .35 composition for ASCII, non-ASCII and non-BMP cached serials. For U+00e9,
  .35 UTF8 bytes c3 a9 must yield U16 ffc3 ffa9, not U+00e9 or 00c3 00a9. Preserve
  .35's first-NUL truncation rather than fabricate public counted-NUL byte output.
- Counted private helper witnesses with high bytes and embedded NUL suffix, clearly
  labelled internal-only, distinguish sign extension and count from strlen.
- Heap-sized old output and widened temporary: verify actual output COPY with
  independently owned storage, no alias/dangling lifetime, original order and
  source unchanged. Allocation/exception/termination cases depend on the selected
  source cleanup result; do not install a new strong guarantee or catch policy.
- Strict native ARM C++ and ASan/UBSan, actual production object/import checks
  BEFORE executing the offline fixture; no live driver/query/IPC/application
  imports or generic self-spawn allowance. Any termination-child test must first
  be justified by selected source and planned with .35's bounded owned-child rules.
- Compile only distinguishing semantic negative controls for sign extension,
  explicit count, false/empty, producer selection and copy/lifetime behavior;
  failures must be assertions with valid memory, not arbitrary crashes/timeouts.
- Rerun relevant .35/.31 fixtures once on final shared source if changed; do not
  rebuild unchanged Driver/Tools or repeat physical/audio tests for a library.

## Limits and handoff

This plan neither implements the desired overload nor admits a guessed cleanup
policy. The specific missing original caller evidence prevents a code-ready plan;
source completion is the smallest justified next step. No full-agent scaffolding,
version/session/IPC integration, installed NI application recognition, live test,
audio/timing/recovery change or security/account/profile action is authorized.

Full .2 original NI automatic recognition and .3/.11 reliability remain incomplete.
Installed 0.2.4/build9 frozen e16097a package and rollback session
20261026T110147Z-3be14ddc stay untouched. The mutable default driver build directory
still contains build8; no installation is part of this plan. Preserve NI ring,
calendar, timing/recovery, mappings/calibration and LED authority. SIP stays enabled.

The source task must use Beads before every exact reader/code/data packet, reuse
existing proofs and stop after answering the selected cleanup question. After its
reviewed result, finalize only the minimal native overload plan before coding.
