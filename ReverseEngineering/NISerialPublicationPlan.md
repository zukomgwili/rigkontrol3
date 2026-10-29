# Original NI serial recognition: next-step plan

Status on 2026-10-26: **.32 planning complete; .33 source proof unexecuted**.
Original NI automatic recognition remains incomplete under `.2`.

Authoritative Beads design SHA256: `c210b71f7e9c301f7147f58f3a9249b6e8ec21399bbc9ac69f30f03f9fa67828` (12,069 bytes),
stored identically in `.32` and `.33`. Independent Spec and Standards/scope
reviews passed the exact plan and all ten input pins with no findings.

The next required proof is NI's outgoing `wstrToUTF8` operation and only its
indispensable output ownership/replacement behavior. USB identity, SPEC14,
inbound conversion and IPC framing are already covered; they are not reopened.
No implementation interface or file list is chosen before that source proof.

This step used existing prose contracts only. No original binary metadata,
body, ASM or data interpretation, code, build, runtime, device, installed-driver
or security action occurred. `.31`'s source, report and evidence remain intact.
SIP stays enabled. Existing driver tests were not rerun for this planning step.

The exact admitted design follows; earlier next-step wording in historical
reports is superseded by this current Beads decision.

---

# Minimum original NI serial/instance recognition step

Date: 2026-10-26. Beads authority: rigkontrol3-40h.32; baseline e4bb2ec.
This is planning only. Next source gate: rigkontrol3-40h.33, OPEN/unexecuted.
No new binary metadata/body/data extraction, ASM interpretation, code, fixture,
compile, signing, installation, application, endpoint, device/audio/MIDI/service
or security action occurs in .32. SIP stays enabled. Full recognition is open.

## Decision and reason

Keep the selected original supporting-agent adapter to the existing sole
NativeUSB/HAL owner; keep NI frontends unchanged. The next eventual original
operation is the serial producer used by instance enumeration, not another USB
identity getter or a new IPC protocol. Before its ARM implementation can be
planned, recover the remaining outgoing wstrToUTF8 operation at original agent
0x1002ae490 and only indispensable output replacement/ownership/exception leaves.
Do not infer conversion from its name or assume the inbound .31 operation is
its inverse. Do not introduce an interim codec, direct UTF16 shortcut, fake
identifier or success-only publication adapter.

No production interface or coding file list is admitted by this decision.
The future code plan must follow the actual .33 result, rather than freeze a
converter/error/connected-state design before its source contract is known.
This is one necessary source dependency on the existing recognition path;
it does not reopen the completed USB identity, SPEC14, IPC or inbound-string work.

## Already proved chain to reuse

Original source: NIHardwareAgent1.7.4/R199, unslid base0x100000000,
SHA256f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258.
The existing cited reports, not fresh body reads, establish:

1. FX2 information fills USBDeviceInfo at device+0x20. The serial field +0x38
   is therefore device+0x58. .24/.26 published physical SPEC14/identity; .27
   bounded hardware validation passed. .31 now implements the selected typed
   information operation with actual NativeClient and owned UTF16 strings.
   Old reports saying native physical identity is not retained are historical,
   superseded by those results; they are not grounds to recapture the device.
2. Server getSerialNumber(std::string&)0x100029f80 requires backend+0x40 and
   its connected byte+0x10. Failure returns false before output replacement.
   Success reads THAT STORED serial at backend+0x58, calls wstrToUTF8,
   replaces caller std::string and returns true, including successful empty data.
   Conversion/replacement exceptions and details are not guessed here.
3. RK3 vptr0x1006112a0 slots+c0/+c8 select ControllerBase GP serial getter
   0x1002df3a0 and the server getter above. The controller calls the server;
   false preserves output; true uses the already-proved SIGNED-BYTE ASCII
   widening at0x1002ae5f0/2ae600, assigns its GP value and returns true even empty.
   A second UTF8 decode or direct use of .31's UTF16 serial changes this chain.
4. Subscriber query full0x02447143 has no synchronous device-list reply.
   Under its original mutex, onClientQueryConnected0x10000f120 visits listed
   controllers and calls actual virtual+c0. Only true produces an asynchronous
   notification to the requesting client; false is not an invented identifier.
5. Recovered MessageSetUTF16String0x1002cf040 produces full0x02444e2b, reply
   offset16/count0, n=min_u32(low32(length)+1,512) at internal+24, zeroes1024
   bytes at+28 and bounded-copies units. Payload count16+2*n, original surrogate
   tail/unsigned-wrap handling and unwritten+16/+20 remain source contracts.
   These internal offsets are not a new raw C++/IPC wire representation.
6. Unchanged frontend subscription handler0x1006787d0 consumes NUL-terminated
   units from internal+28 and invokes its original callback slot+0x10. Existing
   transport/session proofs do not establish actual application recognition.

The server reads cached device-owned information. Do not call .31/get_device_info
again for every serial query, add its100/200/300ms readiness loop to enumeration,
or derive connected state from NativeClient success, ninf flags, metadata
availability, UID, product label or topology. The original ignored info-fill
Boolean, connect hook and subsequent connected reread remain unchanged gates.
Neither this plan nor .31 implements that whole object/lifecycle in a runtime.

## Exact next source gate: .33

First use existing original metadata/manifests to identify the wstrToUTF8 root
symbol0x1002ae490, actual LC_FUNCTION_STARTS entry/next cap, file-backed __text,
import labels and relevant existing artifact inventory. Record source hash,
metadata origin/hash, exact finite addresses/byte extents, existing-ASM reuse
versus fresh decode, immutable output location and checks in .33 BEFORE reading
or interpreting ANY old/new body or pointed-to data. Metadata is not permission
to follow a body automatically; independently review the exact packet first.
No metadata extraction or body work is executed by .32 itself.

After that review, decode only the admitted original function span with the
recorded exact tool/arguments and analysis-off/no-recursion method. Preserve
source-byte correspondence, contiguous full-instruction coverage, output hashes,
source/tool/metadata hashes before and after, and finite questions/stop rules.
A next-function cap is a decode bound, not proof padding belongs to the function.
Existing recovered ASM may be reused only if its original-image/range/method
provenance matches; old decompiler C output is not source authority.

Recover exactly the used operation's:
- Input representation read: counted UTF16 vs NUL termination, length handling,
  and which units/terminator are consumed. Do not infer strlen/count behavior.
- Actual conversion API/call arguments, buffer sizing, produced byte length,
  embedded NUL/high-byte/Unicode rules where source/API-defined, empty and
  any source-distinct platform NULL/error outcomes.
- Output ownership, construction/transfer versus actual caller replacement,
  lifetime through the serial getter and required normal cleanup.
- Failure/exception propagation, current-output/earlier mutation guarantees
  and original cleanup placement. Do not transplant .29's unwind placement
  into this different converter merely because both involve strings.

Use already-proved caller dispatch/false-output preservation, signed-byte
widening and notification constructors by reference. Read only indispensable
additional output assignment/allocation/cleanup operations needed to establish
this selected serial path; a symbol name or generic standard-library policy
is not a recovered branch. Every necessary out-of-cap callee/indirect target or
LSDA/data extent is a STOP: record its precise missing operation, metadata,
file bounds, exact cap/encoding/tool plan and independently review that packet
BEFORE following it. No automatic callgraph, whole GP/stdlib/framework, allocator,
personality/unwinder, other product or agent/server project is admitted.

Do not invent malformed-input output or claim historical/private CF decoder
internals. Preserve original typed platform calls if proved, distinguish their
opaque API contract from decoded NI code, and record any unresolved leaf as
incomplete source proof. Do not add a codec, fallback text, NI error number,
connection gate, C++ ABI emulation or cleanup repair to hide such a gap.

## Source acceptance and later actual-path tests

.33 closes only when the ACTUAL required conversion/replacement semantics are
source-pinned, independently reviewed and all indispensable leaves are proved
or retained as unchanged typed platform calls with justified explicit limits.
A root wrapper export alone is not proof of the called operation. An unresolved
required operation leaves .33 open with its exact next packet, not a fake stub.
Commit only prose/results/pins; proprietary ASM and unique serials stay ignored.
No implementation/runtime or full NI recognition acceptance is claimed.

Derive literal future conformance vectors from the actual source branches:
normal/empty serial, any proved count/NUL distinction, valid non-ASCII/non-BMP
units through the SAME original API if used, byte count/termination, owned
replacement, platform failure and allocation/cleanup stages. No guessed
malformed-unit output, fabricated string limits or numerical OSStatus/NI errors.
Compose with the already-proved signed-byte widening rather than normalizing
it into a modern Unicode roundtrip. Caller backend/connected false must retain
old output; successful empty remains distinct from failed production. Publication
must preserve the existing query-writer counts/fields, ignored/unwritten bytes
and true-only sends; actual unchanged NI acceptance is a later live gate.

These are future source-derived actual-operation tests, not model/source-grep
oracles or tests executed here. After .33, .2 requires a separate exact ARM
implementation plan: typed interface/storage/lifecycle authority, source-pinned
status and exceptions, minimum files, why each ABI/platform change is necessary,
meaningful runtimeRED/GREEN and distinguishing compiled negative controls,
strictARM/sanitizer gates and independent Spec/Standards review BEFORE code.
No coding task or production file list is guessed while .33 is incomplete.

## Scope, preservation and stops

.32 planning deliverables: Beads design/notes, this technical plan report,
ignored planning evidence and passive export only. Agent/Driver/Tools sources,
all .31 source/report/evidence pins and installed bundles stay byte-identical.
No new data schema, property, owner/backend, USB claim/read, virtual MIDI/LED
writer, callback/ring/calendar/clock/QoS/retry/recovery change, frontend patch,
IPC port/server, app launch, package/install or security/account/profile action.
Keep original mappings/calibration/LED authority and SIP ENABLED.

Installed0.2.4/build9/frozene16097a package and actual rollbackSESSION
20261026T110147Z-3be14ddc are preserved. Mutable.build/native/drivers is still8;
this plan authorizes no packaging/install or hardware retest. No system rollback
is needed for a documentation-only step. .31's56strict+56san cases,10compiled
negative controls and14+14driver regressions are prior evidence, not rerun now.
Full .2 recognition and .3/.11 reliability remain open.

.32 completion means a reviewed minimum-next-step decision, NOT .33 execution,
code admission or recognition. .33 stays OPEN/unexecuted. Any scope expansion
requires its precise plan recorded/reviewed in Beads first; do not turn this
conversion dependency into full-agent or SDK archaeology.

DBONLY original checkout /Users/zukomgwili/src/rigkontrol3. Preserve its staged
.beads/issues.jsonl blob113d42992a0ee90acfb640fc694b469abba536a6; passive export
activeworktreeonly/no import. Gitpullrebase/push and separate Doltpush required.

## Baseline input pins

- `ReverseEngineering/NIIdentitySpecificationContract.md`: `aa5d5ae5aa23b293f4bfe6818b24904c3be5d5aab4a762f9c8e4f83866ce3caf`
- `ReverseEngineering/NIAgentRecognitionContract.md`: `f768651d043258cc232162e0a796c7c77947ff0f9852ba87c7f456c839d3b1bd`
- `ReverseEngineering/NIFrontendRecognitionContract.md`: `39374b3e3ebf50f1d3a750e52ee850989411aba19a56f5b1a697ee28be7b01f7`
- `ReverseEngineering/NIIPCTransport.md`: `19929978a90f30b7123f67e4f06a79c1adde9afdc1bef9099820b49c34c1ca32`
- `ReverseEngineering/NIStringUTF8Contract.md`: `ac7c44dcdb20c3ac40937868ad08cebade6de15c351e28389f7113773f0a7ea0`
- `ReverseEngineering/NIARMRecognitionBindingDecision.md`: `d20fd5e597eae062f67d37de29312c1789a217e04abcce798baf728c1d4f9475`
- `ReverseEngineering/NIInformationImplementationPlan.md`: `d7ab03feb26ce3592d8db51c68a418781eb008e97ddb91c60ab212072afe6637`
- `Agent/NIInformation.h`: `2fd2567f9ad5407a8169ecca70932552aba781ee80137b9bc1427fb1bd018bdc`
- `Agent/NIInformation.cpp`: `96136b1e58ebaf09250ddd880e9d9918751b07116d7ee0074b85510dcc6c2a3a`
- `Agent/Tests/NIInformationTests.cpp`: `e95954a106d5cfe12352cbb28e67af1c0fc6aa3562358d9f86bb9df1679c0e10`
