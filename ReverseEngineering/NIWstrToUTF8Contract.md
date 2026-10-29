# NI outgoing serial conversion and caller ownership

Date: **2026-10-26**. Beads authority: **rigkontrol3-40h.33**.
Baseline: `af00ce0`. This is a recovered source contract. No ARM converter,
agent runtime, application recognition, hardware or audio test is implemented
or executed by this result. SIP and installed drivers remain untouched.

**Retain NI's counted UTF-16 → CoreFoundation UTF-8 → C-string assignment
operation, followed by the caller's ownership transfer.** This operation is
not the inverse of `.31`'s inbound conversion, and its caller does not perform
`.31`'s field-copy sequence. No further NI body recovery is needed for this
selected converter/caller slice; existing typed platform/runtime calls remain
boundaries. A separate `.34` implementation plan is required before coding.

[Serial provenance](NIIdentitySpecificationContract.md#concrete-real-serial-query-chain),
[recognition plan](NISerialPublicationPlan.md),
[inbound conversion](NIStringUTF8Contract.md),
[existing ARM information operation](NIInformationImplementationPlan.md).

## Original source and finite coverage

Source: NIHardwareAgent **1.7.4/R199**, original base `0x100000000`, SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
Original `LC_FUNCTION_STARTS`: 11,312 entries, SHA256
`ccc1c7d6c1b95caddc11728c7953bb4380e288c0a0d6dd51d4eb41253`.

| Selected original function | Entry → next-start cap | Bytes | Decoded instructions |
| --- | --- | ---: | ---: |
| `NI::GP::wstrToUTF8(CharSequence<char16_t> const&)` | `1002ae490 → 1002ae5f0` | 352 | 94 |
| Server `ControllerBase::getSerialNumber(std::string&) const` | `100029f80 → 10002a050` | 208 | 60 |
| **Total** | **2 disjoint original caps** | **560** | **154** |

Each packet's exact original metadata, source/file bounds, tool/reader hashes,
questions, output preservation and stops were recorded and independently
reviewed **before** reading its bodies/data. LLVM decoded only these caps;
all 560 bytes match the original source with contiguous full instructions.
A next-start cap does not assert alignment padding belongs to the function.
No decompiler, recursive analyzer or callee-body expansion was used.

Selected cleanup data totals **132 unique bytes**: root LSDA 92 and caller
LSDA 40. The action-zero-only reader first stopped as required on root action 1
and exported only its 92 bytes. That failure is preserved. A separately planned
and reviewed revision resumed the same bounds and interpreted only the first
single action and in-cap null type row. Thus **224 data bytes were exported
across both attempts**, not 224 unique source bytes. No action chains, type-info
objects, pointer contents, personality or unwinder bodies were followed.

Metadata binds 14 imported calls/GOT names and the original compiler-helper
name. The selected CF declarations/comments and DWARF encoding constants are
pinned in local evidence. These are typed API/format contracts, not recovered
private implementations or proof of identical historical framework failures.

## Exact outgoing conversion

The root initializes a fresh returned `std::string` representation. It reads
**both** the input pointer and explicit unit count from `CharSequence<char16_t>`
and calls the original imported boundary:

`CFStringCreateWithCharactersNoCopy(NULL, pointer, count, kCFAllocatorNull)`.

The SDK declares a counted `UniChar` input. `kCFAllocatorNull` does not transfer
ownership of that backing buffer to CF; its lifetime must cover the CF object.
Native ARM code must keep typed units/count and that lifetime, without copying
NI's x86 GP object representation into a wire record.

The source does **not check the creation result for NULL** before calling
`CFStringGetLength`. It next passes that length and `0x08000100` (UTF-8) to
`CFStringGetMaximumSizeForEncoding`. Do not import inbound `.29`/`.31`'s explicit
NULL-to-empty behavior into this different operation. No portable outcome for
violating the subsequent APIs' object preconditions is inferred here.

Let `M` be the returned maximum encoded size. The original branches on its
signed value:

- `M <= 0`: skip byte conversion/assignment, retain the fresh empty result,
  then perform normal CF release. A valid empty serial follows the normal
  empty-value path; this is distinct from a failed producer Boolean.
- `M > 0`: use capacity `M+1`. Through **65,536 inclusive**, the temporary is
  dynamically allocated in the same stack frame. Larger sizes use raw `new[]`;
  the original large-capacity signed/overflow guard remains part of the source
  path. This threshold is the maximum encoded byte size, not UTF-16 input length.

Both positive branches call
`CFStringGetCString(cf, buffer, M+1, kCFStringEncodingUTF8)`. The source **ignores
its Boolean return** and then calls imported `std::string::assign(char const*)`
with the buffer, not a returned byte count. No buffer zero-fill precedes these
calls. The heap branch normally deletes its array after assignment. Both normal
branches release CF and return the independently owned string.

The selected SDK contract says GetCString can fail and guarantees its trailing
zero on successful conversion. The source does not define a new fallback text
or a Boolean failure result when conversion fails; an unwritten/nonterminated
buffer is not grounds to invent deterministic string contents. Future fault
oracles must provide valid backing storage and distinguish branch observation
from private framework/undefined-memory behavior. No replacement codec,
normalization, buffer initialization or status-gated assignment is admitted.

For a successfully produced buffer, pointer-only assignment retains C-string
termination behavior. It must not be replaced by explicit-count assignment
that preserves a suffix after an encoded NUL. Valid non-ASCII/non-BMP conversion
must keep the same CF calls; no private malformed-unit policy is reconstructed.

## Actual server output replacement

The selected server getter returns false if backend `+0x40` is null or its
connected byte `+0x10` is zero. Neither branch writes the caller's output.
It reads the **stored** serial at backend `+0x58`, selects its original owned
short/long representation, and builds the pointer/count sequence for the root.
It does not refresh USB metadata or add a readiness loop to each serial query.

Conversion completes into a fresh local `std::string` **before any output
mutation**. A conversion exception therefore leaves the old caller output
untouched while propagating through this caller.

On normal return, the getter:

1. Clears the existing destination string: heap data's leading zero and logical
   length, or the observed inline empty representation. It does not zero an
   entire caller object indiscriminately.
2. Calls the metadata-proved imported `std::string::reserve(size_t)` with **0**.
   Its old/private allocator and capacity policy was not decoded; do not claim
   the import's name alone proves when an allocation is released.
3. Transfers the converted temporary's complete owned representation into the
   destination, empties the temporary and destroys that now-empty temporary.
4. Returns true, including when the converted value is empty.

The observed 24-byte transfer is x86 C++ ownership provenance, not bytes to
serialize or transplant to ARM. A later native plan must preserve ownership
transfer and the clear/reserve stage through valid native C++ objects. Copying
into the output would add a different allocation/exception path. No whole-GP,
standard-library implementation or x86 capacity/tag emulation is needed here.

## Cleanup and exception coverage

The compact-unwind metadata and selected original LSDAs establish these
start-inclusive/end-exclusive ranges. Action 0 below has a cleanup landing;
selected action 1 has one positive filter, no chained action, and a null type
entry. Its landing calls metadata-named `___clang_call_terminate` at
`0x100004270`. That compiler/runtime implementation is retained as a typed
boundary; its body is not recovered or transplanted.

| Function / protected interval | Action and landing | Source-visible result |
| --- | --- | --- |
| Root `1002ae4d7–1002ae4e4` | 0 → `1002ae5b1` | Destroy the fresh returned string and resume unwinding; no CF release in this landing. |
| Root `1002ae4e4–1002ae57a` | 0 → `1002ae5b6` | Release CF, destroy returned string, resume; covers length/maximum, allocation, GetCString and assignment. |
| Root normal CF release `1002ae582–1002ae58a` | 1 → `1002ae5d1` | Catch-all edge to the named compiler termination helper. |
| Root cleanup CF release `1002ae5b9–1002ae5c1` | 1 → `1002ae5d9` | Same compiler-helper edge during cleanup. |
| Caller `100029f80–100029ffe` | No local landing | Conversion/guards precede the destination mutation stage; no invented catch-to-false. |
| Caller reserve stage `100029ffe–10002a008` | 1 → `10002a040` | Catch-all edge to the named compiler termination helper, not a new rollback/error result. |

The retained no-landing root intervals are recorded in the evidence. In
particular, the array deletion is on the normal path; the conversion/assignment
cleanup landing contains **no array deletion**. Do not add array RAII repair.
Conversely, outgoing CF cleanup **does run** on the selected conversion throw
path, unlike inbound `.29`'s selected constructor. Do not reuse its absent
exceptional CF release as a universal string-conversion rule. Native compiler
unwind/ownership and nonthrowing-stage semantics must replace x86 EH layout,
without changing this resource order or turning exceptions into false/empty.

Only the single action/null type row inside each admitted table was needed.
The bounded format interpretation follows LLVM's
[action-table emission](https://llvm.org/doxygen/EHStreamer_8cpp_source.html),
[null type-reference emission](https://llvm.org/doxygen/AsmPrinterDwarf_8cpp_source.html)
and [exception-table model](https://llvm.org/docs/ExceptionHandling.html#exception-tables).
No generic unwinder/runtime or exception-type project was undertaken.

## Later actual-operation conformance cases

These are source-derived requirements for the separately planned ARM port,
**not tests executed in `.33`**:

| Case | Required distinction |
| --- | --- |
| Backend missing or connected false | False, old output unchanged, no conversion or fresh device-information query. |
| Normal stored serial and valid empty serial | Original counted API calls; true with the converted value, including empty. |
| Input pointer with an explicit count distinct from its first terminator | Preserve the counted UTF-16 construction and later C-string termination; do not normalize both into one count rule. |
| Valid ASCII, non-ASCII and non-BMP units | Use actual CF conversion and owned output; literal vectors follow the retained API contract. |
| Maximum encoded size nonpositive versus positive | Fresh empty branch versus conversion and pointer-only assignment; exact `M+1` capacity and threshold placement. |
| Controlled conversion-return observation | Return value remains unused; do not infer a deterministic API-failure string or execute invalid C-string storage. |
| Conversion/allocation or assignment exception | Original CF/string cleanup, no newly added array cleanup, caller's old output still intact before root return. |
| Replacing an existing long output / converted temporary destruction | Clear and reserve(0) stage followed by independent ownership transfer; no new output-copy allocation. |
| Unexpected exception in the selected release/reserve stages | Preserve original nonthrowing/compiler-helper semantics, not catch-to-false or transactional rollback. |
| Composition with instance enumeration | Reuse NI's already-proved signed-byte ASCII widening and true-only asynchronous writer; do not publish `.31`'s UTF-16 serial directly. |

The stored serial linkage, original connection-hook behavior, signed-byte
widening, query `0x02447143`, notification `0x02444e2b`, internal framing and
unchanged frontend consumer remain their existing proved contracts. This
source result does not implement their runtime, connection authority or ports.

## Evidence, limits and next step

Private evidence root: `.build/ni/serial-publication-contract/`.
Its `final-evidence.json` pins the original-source packet provenance, admitted
plans/readers, successful outputs, preserved stopped attempt, import/cleanup
metadata, selected primary declarations, independent reviews and this report.
Beads `.33` records the manifest hash. Proprietary ASM/raw bytes stay ignored.

The initial metadata-reader hash typo was corrected in Beads before execution;
only the corrected reader ran. A caller-plan descriptive guard still said 352
bytes while its operative bounds correctly said 208; both reviewers caught it,
and its exact revised plan passed before any caller decode. The action-1 stop
and separately reviewed resumption are preserved; no silent retry occurred.

`.33` closes the selected source contract only. `.34` must plan the smallest
faithful typed ARM serial producer before coding, including cached-state
lifetime/authority, exceptions/nonthrowing stages, necessary ABI changes,
minimum files and meaningful actual-path RED/GREEN/negative controls. No
production interface/files are admitted by this source report alone.

All Agent/Driver/Tools sources, `.31`'s report/evidence, installed `0.2.4/build9`,
NI ring/calendar/timing/recovery, mappings, calibration, LED policy and security
settings remain unchanged. No application, endpoint, device, audio/MIDI,
service, compile or physical acceptance run occurred. Full original NI
recognition under `.2` and reliability under `.3`/`.11` remain incomplete.
