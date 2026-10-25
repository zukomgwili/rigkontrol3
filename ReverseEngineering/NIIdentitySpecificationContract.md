# Original RK3 information, specification and identity contract

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.23**, first source stage.

**The original 110-byte information and 14-byte specification now have concrete
producer/consumer mappings. The selected RK3 query uses the original serial
API and its real USB-information storage, not the native UID.** For RK3
`17cc:1940`, selector18's bytes originate in the same command01 payload that
the current owner already retains privately. This relation is proved below,
not inferred from the two records having equal length.

The current owner still lacks retained/published physical descriptor/string
metadata and the complete original error/lifetime/activation mapping. This is
source disposition, not implementation readiness, live identity capture or NI
application acceptance. No production code, application, endpoint, hardware,
firmware, driver, build, signing or security setting was changed or executed.
SIP and NI audio timing/recovery remain fixed.
[Selected binding](NIARMRecognitionBindingDecision.md),
[agent session](NIAgentRecognitionContract.md),
[frontend consumer](NIFrontendRecognitionContract.md).

## Sources and exact coverage

| Original source | SHA256 |
| --- | --- |
| NIHardwareAgent1.7.4/R199, original unslid base `0x100000000` | `f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258` |
| NIUSBAudioDriver2.8.0 x86_64, original unslid base0 | `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08` |

Previously selected 149/39 agent and 241 audio exports are reused **as assembly**,
with source/manifest/index/per-ASM hashes. Their reconstructed C is not used
for claims. The agent has11312 original `LC_FUNCTION_STARTS`; legacy reuse
start-address containment is not a fresh full-instruction-span proof. The
original audio image has **no LC_FUNCTION_STARTS**; none is invented for its
historical listings. Only the selected kernel information/specification,
command01/cache branch, three tiny string getters and finite initUSB string
window are interpreted here. No whole-program analysis or new kernel decode ran.
[Prior original-consumer inventory](NIHardwareAgentFeasibility.md),
[original audio evidence](AudioDriver.md).

## Selector3: all110 output bytes

Kernel `NIUSBUserClient::getDeviceInfo`, `0x9310`, requires the output-size word
at arguments+0x60 to equal **110**. Wrong size returns `0xe0000001` before
locking or writing. It loads output pointer+0x58, optionally locks user-client
lock+0x708, then requires provider+0x1970. Missing provider returns
`0xe00002d9`; a null output pointer returns `0xe0000001`. The acquired lock is
released on those paths. Success writes the fields below and returns0.
These constants are retained numerically; no unproved error-name alias is used.

| Output offsets (hex) | Exact writer/source | Current-owner provenance |
| --- | --- | --- |
| `00–01` | Low16 returned by USB object at provider+0xe0, virtual slot+0x8f0 (`0x9372–9378`) | Actual descriptor value is not in ninf. A matching VID constant is not a substitute for this read. |
| `02–03` | Low16 from the same object's slot+0x8f8 (`0x938e–9394`) | Actual descriptor value is not in ninf; do not manufacture it from the native UID. |
| `04–23` | `strncpy(output+4, getSerialString(provider),32)`; getter `0x1540` returns provider+0x178 | Physical serial bytes are not retained/published by the current owner. |
| `24` | **Unwritten by this writer** | No invented33rd serial byte/NUL or zero template. |
| `25–44` | `strncpy(output+0x25, getProductString(provider),32)`; getter `0x20a0` returns provider+0x138 | ninf's fixed product label is not this hardware string. |
| `45` | **Unwritten** | No invented33rd product byte/NUL. |
| `46–65` | `strncpy(output+0x46, getVendorString(provider),32)`; getter `0x20b0` returns provider+0x158 | Physical vendor string is not retained/published. |
| `66–67` | **Unwritten** | Preserve the lack of assignments; no whole-record-zero claim. |
| `68–69` | Word from provider+0x984 | Command01 payload bytes0–1, available privately in current raw14 cache. |
| `6a–6b` | Zero-extended byte from provider+0x986 stored as a word | Raw14 byte2, plus the writer's proved zero high byte. |
| `6c` | Byte provider+0x990 | Raw14 byte12. |
| `6d` | Byte provider+0x98f | Raw14 byte11. |

The three imported calls are proved `_strncpy`, not guessed from parameter
shapes. Within each32-byte slot it copies/pads according to that original
function; the four inter-slot/padding bytes remain untouched. The selected
writer does not initialize all110 bytes. Its original callers' initialization
and string readers must not be replaced by assumptions about zeroed memory.

The selected `initUSB` reuse window `0x961..0xa1c` shows the actual string-source
calls. For product/vendor/serial destinations+0x138/+0x158/+0x178, it obtains
indices through USB virtual slots+0x918/+0x910/+0x920 respectively, then invokes
slot+0x958 with zero-extended index, destination, **31** and **0x0409**. All
three return values are ignored. This window has no explicit buffer-zero or
NUL write. The logical labels come from NI's proved getters; the old USB
virtual method names, decoding, termination and error behavior are an explicit
OS-contract boundary. This report does not invent named GetStringDescriptor,
VendorID/ProductID ABI targets or UTF-8 behavior from conventions.

## Selector18 and the exact command01 cache

Kernel `getDeviceSpec`, `0x99d0`, applies the same exact-size/provider/output
checks and optional lock/unlock, with required size **14**. Success calls
`memcpy(output,provider+0x984,14)` and returns0. It issues no new USB query.

The existing `commandReplyDispatch` assembly loads the command byte at
`0x2186`, indexes signed four-byte entries using base `0x2560` and adds that
base before its indirect jump. Only entry1 at **`0x2564`** was read: bytes
`4a fd ff ff`, signed−694, select **`0x22aa`**. That selected branch advances
past the command byte, then calls proved `_bcopy(payload,provider+0x984,14)`.
This closes command01→cached14→selector18, without relying on an old C switch
or decoding the other table entries.

The branch subsequently increments cache bytes11/12 only under USB virtual
values compared to **0x17cc and0x2305**. It therefore does not apply that
correction to the selected RK3 product0x1940. Preserve the conditional rule;
do not apply the other product's correction to an RK3 or claim a universal
unchanged raw/spec relation for every NI product.

| Specification byte(s) | Source and selected consumers |
| --- | --- |
| `0–1` | Command01 payload0–1 → provider+984/985; S3word+68; also agent spec-read sentinel. |
| `2` | Raw2 → provider+986 → S3zero-extended word+6a. |
| `3` | Raw3 → selected FX2 connect's feature byte+98. |
| `4` | Raw4 → feature+9a. |
| `5` | Raw5 → feature+9c. |
| `6` | Raw6 → feature+9d. |
| `7` | Raw7 → feature+a0; current decoder contributes it to output-channel count. |
| `8` | Raw8 → feature+9f; current decoder contributes it to input-channel count. |
| `9` | Raw9 → feature+a2; current decoder's second output-count contribution. |
| `10` | Raw10 → feature+a1; current decoder's second input-count contribution. |
| `11` | Raw11 → S3byte+6d; current MIDI-output count. |
| `12` | Raw12 → S3byte+6c; current MIDI-input count. |
| `13` | Raw13 is returned unchanged by S18; current decoder calls this wire mode. No other agent meaning is inferred from the selected reads. |

All14 bytes remain part of the original returned specification, whether or
not this selected agent consumer reads each one. Feature offsets above are
in-process destinations, not invented public fields. Original connect also
has a specification-error fallback; this table describes its successful
specification-read branch, not a replacement default policy.

Current `rk3_device_info_parse` copies bytes1..14 after command01 into
`rk3_device_info.raw`; `NativeUSB` stores that result under its mutex. That is
actual byte provenance for successful RK3 replies. It does not prove matching
malformed-report behavior, initialization readiness, timing or generation
lifetime. The current parser validates15..64 total bytes; the selected old
cache-copy branch itself performs a fixed14-byte copy. The native CFData
`ninf` record does not expose raw14 today.
[Current decoder](../Driver/DeviceProtocol.c),
[current owner](../Driver/NativeUSB.c),
[current public records](../Driver/NativeControl.h).

## Agent reads, errors and connection order

`getCaiaqUSBDeviceInfo` `0x1002efd50` and `getCaiaqUSBDeviceSpec`
`0x1002f0310` call `IOConnectCallMethod` selectors3/18 with no input and initial
output counts110/14. They preclear **only** info word+0x68 or spec word+0,
respectively. On status0 with that word still0 they retry at most three more
times, calling original `std::this_thread::sleep_for` with100000000,
200000000 and300000000 nanoseconds. Thus the bound is initial+three calls;
this is original readiness polling, not a new retry scheme. Nonzero call status
ends the sequence. The final return depends on status==0, so exhausting the
zero-word check can still return true. No returned-size equality check or
reset of that count between retries appears here.

`FX2Device::OSImpl::getDeviceInfo` `0x1002f0970` returns false for handle0 or a
false S3 helper result. Success copies S3words+0/+2/+68 into USBDeviceInfo
header+0/+2/+4. When its Boolean string argument is true it runs strlen on
vendor+46, product+25 and serial+4, delegates conversion to metadata-named
`StringUTF8` `0x1002ae6f0`, then assigns GP strings at output+8/+20/+38.
There is no additional slot-bound strlen check. If strings are not requested,
those string fields are not assigned. The const getter `0x1002f0950` selects
handle+0x6c and requests strings. The generic conversion/assignment bodies are
not silently reconstructed from their names.

Temporary `getDeviceInfoForOSHandle` `0x1002f25a0` obtains IOMasterPort, opens
type0 with `IOServiceOpen`, calls the getter, closes the successfully opened
connection regardless of getter result, deallocates its acquired master-port
right and returns that Boolean. Its early failure branches return before the
later close/deallocate sequence; this is not rewritten into an invented
resource/retry policy.

The retained FX2 impl connect `0x1002ef650` opens type0, stores connection+6c
and service+78, retains the service, calls S3 and uses selected returned fields
without testing its Boolean. It also performs its original registry-text
operation and S18/feature/fallback operations. Success/failure must follow
its original branches; neither a field table nor current owner topology is
permission to make every information failure a new connection abort. Exact
unrecovered registry literals and unrelated product/domain behavior are not
invented by this report.

## Concrete real-serial query chain

The existing RK3 constructor stores vptr **0x1006112a0**. Its exact+c0 qword
`0x100611360` selects `ControllerBase::getSerialNumber(GP::String&)`
`0x1002df3a0`; its exact+c8 qword `0x100611368` selects the server's
`getSerialNumber(std::string&)` `0x100029f80`. The former calls the latter,
returns false without assigning output on false, otherwise delegates the
produced bytes to the **already recovered signed-byte ASCII widening** helper
`0x1002ae5f0/2ae600`, assigns the GP string and returns true even if empty.
Do not replace that operation with a new UTF-8 decode.

The server getter requires its backend pointer+0x40 and pointee connected
byte+0x10; otherwise it returns false with output untouched. It takes the
pointee GP string+0x58, delegates to metadata-named `wstrToUTF8`
`0x1002ae490`, replaces caller std::string storage and returns true. No native
UID, registry-entry ID, generated serial or fallback label exists in this body.
The converter's internal error/Unicode behavior remains a typed runtime
prerequisite; its symbol name alone is not an implementation.

The source storage linkage is concrete. Selected FX2 construction calls
`createOSImpl` `0x1002f1c80`, which allocates the impl, sets vptr
**0x100624db0**, stores its owner backlink+0x60 and returns the impl to the
USBDevice base constructor. Exact qwords at **0x100624dc0/+10** and
**0x100624dd0/+20** select the existing FX2 impl connect `0x1002ef650` and const
information getter `0x1002f0950`. `USBDevice::connect` `0x1002f5780` locks the
impl, calls the connect slot and first stores its Boolean at device+0x10.
On true it stores the OS handle and calls the info slot with destination
**device+0x20**, ignoring its return; then it invokes the device's original
virtual+0x10 hook and rereads connected byte+0x10 for its own result.
Consequently the S3serial→USBDeviceInfo+0x38 assignment targets
**device+0x20+0x38 = device+0x58**, exactly the serial getter's source.

USBDevice construction `0x1002f7670` stores the impl, clears connected+10 and
its other scalar fields, zeros header+20..25 and the three GP-string
representations+28/+40/+58. **Padding+26/+27 is unwritten.** Do not call the
whole native object zeroed, serialize its pointers or insert a new info-success
gate. Server object construction, original hooks, GP/runtime internals and
actual unchanged-frontend runtime acceptance remain separate retained gates.

The agent's recovered instance-query helper sends this successful serial API
result through its exact UTF-16 notification writer; the paired frontend
consumes the string at internal+28. It is not a synthetic device-list label.
[Original query writer/session](NIAgentRecognitionContract.md),
[original instance consumer](NIFrontendRecognitionContract.md),
[reviewed ASCII conversion](NIIPCTransport.md).

## Selected-owner disposition and finite remaining gates

| Required original value/behavior | Source disposition for the selected one-owner adapter |
| --- | --- |
| All14 RK3 specification bytes and S3cache-derived scalars | Real raw payload already retained privately; narrow, versioned, generation-qualified publication is still needed. Do not copy ninf's first14/110 bytes or equate record lengths. |
| USB virtual word reads and physical serial/product/vendor bytes | Not retained in current public records; require real owner-side capture and exact original OS-contract disposition. Native UID and fixed product label remain locators, never NI identity. |
| Original S3/S18 numeric errors, readiness checks and unchecked call order | Preserve or explicitly map them at the functional boundary. CoreAudio status, native last_error and current readiness flags are not automatically the original status/lifetime contract. |
| Unicode/OS and typed runtime | Retain the proved original call order/arguments; named but unrecovered conversion/USB virtual semantics remain explicit prerequisites before substituting another implementation. No whole-SDK or GP-library recovery is admitted here. |
| Events, termination and policy | [Phase2](NIEventOwnershipContract.md) owns callback layout/timestamps, fallback/registration, errors, cancellation, activation/baseline and single mapped-control policy authority. This first field proof does not authorize competing LED/MIDI writers or duplicate USB ownership. |

This closes the concrete first-stage field/serial provenance question, with
specific missing owner capabilities and retained semantic boundaries. It does
not implement a selector emulator, broaden ncmd, expose arbitrary USB writes,
change controller/audio startup, or waive full parity/stability cards. Any
production change needs its separate Beads plan and distinguishing field,
error, generation and teardown tests before execution.
[Current CFData client](../Driver/NativeClient.h),
[architecture/source gate](NIARMRecognitionBindingDecision.md).

## Planned offline conformance cases

These are **synthetic source-oracle cases**, not observed hardware data,
implemented tests or authority to change the driver. A later admitted test
card must exercise the actual selected adapter and preserve these distinctions.

| Synthetic case / original boundary | Required source-derived observation |
| --- | --- |
| S3 writer receives output sizes109 or111 | Return `0xe0000001`; no output writes or lock acquisition. The agent helper's separate sentinel preclear is not part of this writer fixture. |
| S3 size110 (or S18 size14), missing provider; then present provider/null output | Respectively `0xe00002d9` and `0xe0000001`, without output writes; release an acquired optional lock. |
| S3 output prefilled with110 bytes `0xa5`, valid numeric/string/cache source fixtures | Four gap bytes `0x24/0x45/0x66/0x67` remain `0xa5`; do not replace the writer with whole-record zeroing or invent33-byte strings. |
| RK3 command01 with payload **`01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e`**, selected1940 predicate | S18 returns those same14 bytes; S3word+68 is `0x0201`, word+6a `0x0003`, byte+6c `0x0d`, byte+6d `0x0c`. This literal is a synthetic fixture, not an RK3 capture. |
| Info/spec helper gets status0 and a zero sentinel on every call | Initial+three calls, original100/200/300ms sleep arguments, then true because final status is0. A nonzero call status ends without another readiness retry. |
| Serial backend absent/disconnected; separately a successful producer returns an empty string | False leaves the caller output unchanged; true/empty remains successful and is not replaced by native UID or a fallback identity. No invented OS/Unicode conversion vector is implied. |
| USBDevice impl-connect true, info-fill false, original hook leaves connected true; repeat with hook clearing it | The first returns true despite info failure; the second returns false after rereading the hook-updated flag. The info Boolean must not become a new gate. |

Descriptor decoding, old USB virtual ABI names and unrecovered Unicode/runtime
leaves still need their actual contracts; these cases do not supply fabricated
expected outputs for those boundaries. Generation, detach/callback and policy
cases belong to the next event/lifetime disposition, not an invented reset or
polling scheme in this identity proof.

## Private evidence and review

Private root: `.build/ni/owner-adapter-contract/identity/`.

| Packet | Scope / result | SHA256 |
| --- | --- | --- |
| `reuse-inventory.json` | Seven selected legacy agent/2kernel ASM, manifests, source pins and qualified coverage | `e5a3404a6693027731adde40dbf4ddfbdda21589675bac97d40ab0df8fadd04a` |
| `reuse-followup-inventory.json` | Three string getters, command01-only branch and finite initUSB string window | `ebe76e4a3d7246fc104475d02465750ed7ab603485ed820d9c66e72a84df7ace` |
| `reuse-factory-inventory.json` | Selected FX2 ctor/createOSImpl allocation/vptr/base chain | `37cb8c1e57bb5d0b83f525ae1e10d9191ae3de059e3ac637c0f6bc21299322c5` |
| `phase-a/data-evidence.json` | Exact query+c0 qword | `4460f7a3bff25ed1f4e78b6883554dde78a0dd3acfef3d814e9a4324196a6ff4` |
| `phase-b/evidence.json` | One224-byte cap /52 instruction spans | `584e9ecce8c2401c64341b71b035755c48c164197604937cba78968610ecddc5` |
| `phase-c/data-evidence.json` | Exact serial+c8 qword | `fc44ea1ebe7110149e8aa9bc5ed025cb6ffbdd9163ee4c413b9d098dc60404ae` |
| `phase-d/evidence.json` | One208-byte cap /57 instruction spans | `d93931dc8f971fccaa10aa4c8c084f012678beda488f100f2d4679522e7959db` |
| `phase-e/evidence.json` | Two176/128-byte caps /64 instruction spans | `74d047833a8c15573041c7be48d97fa049a1c754475f17e9f5cd5d1a37958f7f` |
| `phase-f/data-evidence.json` | Exactly two impl-dispatch qwords, no intervening slot | `415af893091146f83684aa1a34668af2fe966963cec47ae582a0c5af1cf5a0de` |
| `kernel-command01-data/data-evidence.json` | Exactly4 raw bytes for command01 table entry1 | `abfaac0063fc4457128c88cb4e9502c6cf0dc20d7419d0cee9fbefb678de8a8a` |
| `import-metadata/import-metadata.json` | Five exact original relocation/nlist/name associations: bcopy,memcpy,strncpy,IOLockLock/IOLockUnlock | `ce80ec357b5c86dfd054f93508b5b77a7b0afec325f56572ea76c8148e05f82b` |

Fresh agent packets total **four** added body caps / **173** decoded instruction
records across isolated project copies. Each preserves the861-function,
zero-instruction baseline; uses noanalysis/readOnly and explicit incremental
analysis off; prevalidates all absent/nonoverlapping targets before mutation;
checks exact deltas and full instruction spans within original LC/next-start/
text caps; exports ASM only; and preserves source/project/artifact hashes.
Actual guard/error logs and counts govern success. No new decompiler/C,
prototype guesses, flow overrides or automatic callee following ran. Data
packets preserve exact VM/file mapping, sizes, bytes and metadata; only the
admitted qwords/table entry/import associations were read. Every new packet
passed independent technical review before semantic use. Historical reuse
qualifiers remain separate from those fresh guarantees. Raw proprietary
listings/projects stay private and ignored.
