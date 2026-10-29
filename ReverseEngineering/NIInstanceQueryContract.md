# Original NI instance-query cleanup and opaque-storage contract

Date2026-10-26; Beads authority `rigkontrol3-40h.43`. Source proof only.
No native query, driver change or NI application-recognition acceptance run.

**Preserve ordered scratch cleanup, nonthrowing mutex release and propagation
of the same exception. Preserve opaque message prestate without defining it.**

The selected query's own exception table and mapped cleanup code establish
these requirements. This resolves its selected cleanup gap without importing
another caller's policy or decoding the complete unclassified tail. A separate
native implementation plan must fix the interface, valid ownership/storage and
actual-operation tests before coding. Full NI recognition remains incomplete.

## Source and exact finite reads

Original NIHardwareAgent1.7.4/R199 x86_64 image:8,343,520 bytes, SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
Every packet freshly verified this identity before and after its read; the image
was never executed or modified. Whole-image streamed checksums are identity
guards. LLVM object loading is an explicit typed decoder boundary; printed stub
labels do not establish new import identities.

Every exact packet, numeric range, reader/interpreter/tool hash and stop rule
was recorded in Beads and independently reviewed BEFORE execution. Each packet
executed once with exit0 and all guards passing:

| Packet | Original file offsets and lengths | Interpretation |
| --- | --- | --- |
| H | 0/32;32/3600;5756470/28 | Header/load commands and unwind header only; no offset following |
| I | 5756498/52;5756554/120 | 13 common encodings and10 first-level index records; select query block only |
| P | 5783490/12;5756674/2712 | Selected page header and its339 LSDA-address index rows; no LSDA contents |
| Q | 5783502/2804;5123428/8 | 701 selected-page encoding words and query-owned LSDA header prefix |
| E | 5123428/76 | Exactly the query's declared LSDA table/action/type data |
| B | 61951/57 | Only mapped cleanup text `[0x10000f1ff,0x10000f238)` |

Metadata read spans total9,444 bytes including an eight-byte prefix reread;
unique metadata bytes total9,436. The query's own header establishes its76-byte
LSDA extent for this proof: callsite table ends at70 and type base is76. The
next address in its index block corroborates that end but was never claimed
as a global allocation-boundary proof. Of Q's eight prefix bytes, only five
header bytes were interpreted there; three were left uninterpreted until E.
No type-pointer dereference, personality/callee-body recovery or new symbol/
stub binding proof was undertaken. LLVM object loading remains the boundary
described above. No other second-level page or LSDA-index block was read.

The compact entry covers exactly `[0x10000f120,0x10000f240)`, encoding
`0x510558d1`, personality index1, with the exact query LSDA row. LSDA address
`0x1004e2d64`, file5123428, uses omitted LPStart `ff`, type encoding `9b` and
callsite encoding3. Its65-byte callsite table contains five rows. Action1 is
one positive filter1, next0, with encoded NULL type entry; no pointer is
resolved or action chain traversed.

The earlier normal export remains223 contiguous bytes/60 instructions through
RET`0x10000f1fe`. B adds57 contiguous bytes/14 instructions, each checked against
original bytes; raw SHA256
`e62d0d168f38750746d84a66be77a36ef505b83ade887445ae6054c7c14b80e9`.
The remaining8 bytes `[0x10000f238,0x10000f240)` remain UNREAD/UNCLASSIFIED by
this work. They are not labelled padding or required recovery.
Raw76-byte LSDA SHA256:
`1c577d7d7f1061fb3945b21cc80981c443ee4943df0db4970e1d597a6d2c118e`.

## Selected original exception policy

Start-inclusive/end-exclusive intervals:

| Protected interval | Landing/action | Consequence with recovered code |
| --- | --- | --- |
| f120–f19e | None/action0 | Includes acquisition call atf14d; no query-local cleanup handler |
| f19e–f1c6 | f1ff/action0 | Getter, writer or inline-send exception retires scratch, unlocks, resumes same exception |
| f1e5–f1ed | f228/action1 | Normal mutex-release exception calls compiler termination helper |
| f214–f220 | f230/action1 | Mutex-release exception during unwinding calls the same helper |
| f220–f238 | None/action0 | Resume and termination-helper calls have no additional local handler |

Addresses in this table share prefix`0x10000`. The cleanup entry saves the
original exception from RAX into RBX atf1ff, tests the current serial scratch
tag, and conditionally deletes its heap pointer atf20f. It loads the saved
mutex from rbp-468 and calls unlock atf21b, then passes the original exception
to `Unwind_Resume` atf223. There is no catch-to-false, new reply, rollback of
previous notifications or retry policy.

The normal release handler f228 and unwinding release handler f230 pass the
current exception to metadata-named `___clang_call_terminate` at100004270.
The normal and exceptional release stages are therefore nonthrowing/termination
stages; a native release guard must preserve that policy. Do not replace these
calls with a newly throwing mutex/status-check policy. Acquisition's original
return status is ignored; no new system-error conversion is admitted.

Normal scratch deletion atf1e0 and exceptional deletion atf20f have no local
catch entry. The existing typed nonthrowing operator-delete boundary is retained;
this is not a claim about behavior when a private runtime violates its contract.
Operator-delete and `Unwind_Resume` identities reuse .37's verified loader
associations. Pthread names reuse selected existing generated function records.
The compiler helper's name/role reuses the prior
[conversion contract](NIWstrToUTF8Contract.md), not LLVM stub labels; the selected
phase-e baseline metadata has no helper row. No helper body was recovered.

The normal lifecycle remains: null client skips everything; nonnull client
locks class+e8, constructs one fresh serial scratch, reuses it and the same
message across the original list, and publishes only true getter results,
including true-empty. Send result is ignored; serial heap retirement precedes
unlock. Calls stay inline under the mutex with original2.0-second transport
timeouts/mode choices. No queue, thread, connection authority or timing change.
Valid native objects/compiler unwind may replace x86 layout/EH bytes later,
while retaining these stages and ordering. A native query interface is not
chosen or implemented in this source-proof task.

## Opaque message storage: exact contract and limits

The1052-byte message at rbp-460 has writer-unwritten internal bytes16..23
(words+16/+20 at rbp-450/-44c; wire+4/+8). The selected normal query/writer/send
chain initializes none of them before CFDataCreate copies all payload bytes.
Their values are opaque stack prestate; no identity, version or zero value is
established by the source. Their byte values remain observable in transport.

The existing frontend `onServerSubscriptionMessage` export at1006787d0 proves
its selected normal add/remove branches read count+0, low24 type+12 and
NUL-scanned U16 at+28, without reading+16/+20/+24. They construct a separate
GP string and pass that string to callback+10/+18, not the original frame.
This is74 instructions/248 decoded bytes in a320-byte cap; its other72 bytes
remain unclassified and were not newly recovered. Other status commands do
read+16, so this is not a global unused-header assertion. Nor does the agent
image's transport implementation prove every lower frontend/lifetime path.

The valid native algorithm-boundary contract is consequently explicit:

- Caller supplies live, exclusively held1052-byte message storage, with defined
  prestate in those8 opaque bytes. The query preserves their exact values
  across all writer/send iterations; it does not choose a default or meaning.
- Other writer-defined bytes are set by the actual completed writer before a
  successful send. A false getter never publishes stale scratch/frame content.
- Source and frame remain separate/stable; inline transport copies payload
  before frame reuse. No borrowed deferred-send pointer or C++ indeterminate-
  byte read is admitted.
- Future code must not silently value-initialize/zero the frame, invent IDs,
  transplant x86 GP/stack objects, or claim arbitrary and zero values equivalent.

This is a representation/lifetime precondition for a future typed query, not
an implementation or proof of a concrete runtime provider. `.2`'s integration
plan must identify that provider and its value provenance explicitly. The
selected source contract does not promise global frontend equivalence or NI
application acceptance. Searching old stack bits or a speculative initializer
would not establish a stable protocol field; no such search was performed.

## Review history, preserved evidence and next step

The frozen .42 plan and exact .43 admissions remain in Beads. Private source,
metadata, raw spans, per-packet argv/stdout/stderr/exits, readers, hashes and
review records live under `.build/ni/instance-query-contract/20261026-4e477b4/`.
All unsupported encoding/mapping/action/target cases required STOP and a new
recorded packet; none occurred in the original reads.

Before B executed, one review caught missing partial-output preservation on
timeout. That was fixed and repinned; a subsequent inactive administrative AST
check caught one indentation error, also fixed before execution. Inactive
versions remain preserved. Bv3 passed both reviews, then executed once. No failed
original-source packet was retried. Timeout handling was source-reviewed, not
claimed as an executed timeout test. Optional decoder-capability probes found
capstone/llvm-mc unavailable; they accessed no NI source and prompted reuse of
the already reviewed LLVM method, not a new decoder.

No production/test code, compiler/regression suite, NI app, IPC endpoint,
device/audio/MIDI operation, package/install/account or security action occurred.
Independent source/EH and storage/scope reviews passed the selected findings.
Final report/evidence fidelity reviews are recorded in Beads after the immutable
manifest is created. Root launch records are external: the initial launcher was
archived after H–E, and the revised launcher before B; neither is falsely
reported as a pre-reviewed wrapper.
All102 Agent/Driver/Tools files remain byte-identical. Installed frozen0.2.4/
build9, mappings/calibration/LED authority and NI ring/timing/recovery remain
unchanged; SIP stays enabled. Prior .39/.41 code/tests and .42 planning remain
preserved. Full original NI automatic recognition in `.2` and reliability in
`.3/.11` are still required and incomplete. Next: a separate smallest native
query implementation plan, with explicit caller storage/ownership and actual
public-operation tests, before coding.
