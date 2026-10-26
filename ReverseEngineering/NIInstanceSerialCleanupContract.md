# Original NI UTF-16 serial caller: cleanup contract

Date: **2026-10-26**. Beads authority: **rigkontrol3-40h.37**.
Baseline: `b5f543c`. This is selected original-source proof, with no ARM
implementation, installed-driver change or NI application acceptance run.

**Preserve ordered temporary cleanup and propagate the original exception.**
The missing caller code destroys the byte string when its producer or widening
constructor throws. If output copy throws, it first deletes the completed wide
temporary's heap storage, then destroys the byte string and resumes unwinding.
The caller introduces no catch-to-false, rollback or local termination handler.

This resolves the specific cleanup gap recorded in
[the preceding plan](NIInstanceSerialPlan.md). No further NI body recovery is
needed for this selected wrapper. A separate native implementation plan must
freeze the exact overload, ownership, necessary representation changes and
actual-operation tests before coding. Full original NI recognition remains open.

## Source and finite coverage

Source: NIHardwareAgent **1.7.4/R199**, original x86_64 image SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
The original image was never executed or modified.

| Packet | Admitted and observed coverage |
| --- | --- |
| M: selected metadata | Original header/load commands, pinned LC_FUNCTION_STARTS and compact-unwind metadata for exactly `0x1002df3a0` and `0x1002ae600`; selected LSDA addresses/bounds only. No instruction, LSDA content, symbol, stub or pointer interpretation. |
| B: ControllerBase UTF-16 getter | Exact original next-start cap `[0x1002df3a0,0x1002df480)`: **224 bytes, 66 fully contiguous instructions**, including padding. All instruction bytes match the original file-backed span. |
| E: caller LSDA | Exact `[0x100532440,0x10053246c)`, file offset **5,448,768**: **44 bytes**, fully parsed into three call-site rows, all action zero. No type-info pointer, personality, unwinder or callee body followed. |

The old export contained **170 bytes / 52 normal-path instructions**, ending at
`RET 0x1002df449`. Their address sequence matches the new export exactly. The
missing **54 bytes** are now classified as **46 bytes / 13 cleanup instructions**
at `[0x1002df44a,0x1002df478)`, plus one **eight-byte NOP** at
`[0x1002df478,0x1002df480)`. An original next-start cap is an analysis bound,
not a claim that its padding is executable function logic.

The caller's raw 224-byte hash is
`fe5af0d65c939dced6565b27dcb6b4bc41a18ebcd64e08052cd94f4536d075e2`,
matching the original cap hash already retained in the prior identity evidence.
The raw 44-byte LSDA hash is
`7866c9db02fb1e6d275294f5f47d07c312032f396e706f98b84466dd9e380d63`.

## Exact exception intervals and resource order

The selected compact-unwind entry starts at `0x1002df3a0`, covers through
`0x1002df480`, has encoding **`0x51010001`**, personality index **1** and an
exact caller LSDA row. The LSDA uses omitted LPStart (`0xff`), type encoding
`0x9b` and call-site encoding **3** (three little-endian unsigned four-byte
fields, followed by the action). All call-site actions are **0**. The type-base
address is recorded, but no positive action/type entry or pointer is interpreted.

Intervals below are start-inclusive, end-exclusive:

| Protected interval | Landing | Source-visible consequence |
| --- | --- | --- |
| `0x1002df3c5–0x1002df405` | `0x1002df44a` | Covers the virtual byte producer call at `0x1002df3c9` and StringASCII construction at `0x1002df400`. Save the exception, skip wide-temp destruction, destroy the byte `std::string`, then resume unwinding. |
| `0x1002df41f–0x1002df427` | `0x1002df44f` | Covers counted output assignment at `0x1002df422`. Save the exception; if the completed wide temporary uses heap storage, delete that storage; destroy the byte `std::string`; resume the same exception. |
| `0x1002df427–0x1002df478` | None | Includes normal and exceptional temporary cleanup and the resume call. This caller has no local landing/catch-all/termination handler for these calls. |

Both cleanup entries converge on byte-string destruction at `0x1002df46b`,
then `Unwind_Resume` at `0x1002df473`. The output-copy landing's conditional
wide-storage deletion is at `0x1002df45f`. The normal path similarly cleans the
wide temporary before the byte temporary, then returns true. A false producer
skips widening/output assignment, cleans the byte temporary and returns false.

Three already proved loader-metadata associations identify these calls:

| Target | Existing imported identity | Reused proof |
| --- | --- | --- |
| `0x10045d828` | `operator delete(void*)` / `__ZdlPv` | `.29` selected indirect-symbol/nlist/name binding |
| `0x10045d666` | `std::string` complete destructor / `D1` | `.33` selected indirect-symbol/nlist/name binding |
| `0x10045d87c` | `Unwind_Resume` | `.33` selected indirect-symbol/nlist/name binding |

The bindings are reused from existing source evidence, rather than inferred
from LLVM's generic stub labels. No new import metadata, stub instructions,
allocator, destructor or unwinder implementation was recovered.

A missing caller-local landing is not a claim about historical private
allocator/destructor behavior or what happens if an external operation violates
its own contract. Imported typed runtime contracts remain the seam. Do not add
a `.35`-style local nonthrowing/termination stage to this different caller.

## StringASCII disposition and unchanged value contract

M establishes an exact StringASCII unwind range
`[0x1002ae600,0x1002ae6f0)`, encoding **`0x010558d1`**, personality index **0**,
with **no LSDA**. This establishes no local exception-table cleanup to recover;
it does **not** establish that construction cannot throw. Its already recovered
normal path calls the original length-error/allocation imports. Constructor
failure is handled by the caller's first cleanup interval above.

No new StringASCII body decode was needed. Its old 224 decoded bytes within a
240-byte cap remain the normal-path proof; the other **16 bytes stay
unclassified and were not decoded by this work**. They are not labelled padding
or implicitly treated as recovered code.

The already proved value/ownership sequence remains authoritative:

1. Construct a fresh byte string and call the original byte producer once.
   The native counterpart is the actual [.35 getter](NISerialNumberImplementationPlan.md),
   using one coherent borrowed cache/connection snapshot.
2. On false, do not assign caller output. On true, use the produced string's
   actual pointer **and logical count** to construct StringASCII. Sign-extend
   every byte into one 16-bit unit; `80→ff80`, `ff→ffff`. This is not UTF-8 decoding.
3. Copy the counted widened result into output through the exact U16 assignment
   and growth already recovered in [.29](NIStringUTF8Contract.md). This is output
   **copy**, distinct from `.35`'s byte-result ownership transfer.
4. Destroy completed temporaries in the proved order; return true even if empty.

The endpoint-name proof helper uses `strlen` for its particular name caller;
it cannot substitute for this counted operation. Counted embedded NUL units
belong to the internal widening contract, while actual `.35` composition keeps
its earlier C-string truncation. Do not directly publish `.31`'s cached UTF-16,
introduce another codec/identifier, or bypass the original true/false authority.
Output mutation remains at counted assignment. The wrapper adds no rollback;
selected assignment-failure behavior remains the existing `.29` copy contract.

## Execution discipline and verification limits

Every exact packet, source/tool/reader hash, question, range and stop rule was
recorded in Beads and independently reviewed **before** the relevant new read.
M, B and E each executed **once** and passed. All source, metadata, tool and
admission checks passed; outputs, commands, stderr/stdout, raw ranges and exits
are preserved in `.build/ni/instance-serial-contract/`.

Before execution, reviews strengthened the readers' exact-plan checks and made
them parse the same plan bytes whose reviewed hash they checked. Inactive prior
versions are preserved. No source-packet failure was silently retried. The small
local evidence-publication duplicate binding was deduplicated from existing
records; it required no new original read.

Independent Spec/source interpretation and Standards/guard/evidence reviews
passed the selected results. The final report/manifest hash and exact fidelity
reviews are recorded in Beads `.37`; private proprietary ASM/raw bytes remain
ignored. Existing `.29`, `.33`, `.35` and `.36` results remain unchanged.

No production/test code, compilation or regression suite was required or run:
all Agent/Driver/Tools sources are unchanged. No application, IPC endpoint,
device/audio/MIDI operation, package/install, account/profile or security action
occurred. Installed `0.2.4/build9`, NI ring/calendar/timing/recovery, mappings,
calibration and LED authority remain untouched. SIP stays enabled.

Next: **rigkontrol3-40h.38** freezes the smallest native UTF-16 serial overload
plan from these completed contracts before coding. Use valid native ownership
and compiler unwind instead of x86 object/EH bytes, preserving the proved
count/sign/copy/cleanup/propagation order. No further NI body recovery is needed
for this slice. Full original NI application recognition in `.2` and reliability
work in `.3`/`.11` remain incomplete.
