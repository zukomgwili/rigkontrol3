# Original NI agent recognition contract

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.21**.

This source-only recovery connects NI's original CFMessagePort transport to
its device selection, connection, subscription, notification and client
lifetime boundaries. It uses NIHardwareAgent **1.7.4/R199**, SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
Addresses are original unslid x86_64 addresses. Internal object/message
offsets are distinct from wire offsets: the first twelve message bytes remain
in-process. See [transport contract](NIIPCTransport.md),
[paired frontend](NIFrontendRecognitionContract.md) and
[implementation gate](NIRecognitionImplementationPlan.md).

No NI application/helper, endpoint, hardware, driver, service, signing asset
or security setting was executed or changed. SIP stays enabled; NI audio
timing and recovery remain fixed. These are original-derived contracts, not
production code or proof of unchanged NI application acceptance.

## Registration and state

| Original boundary | Assembly-grounded behavior |
| --- | --- |
| NHL `IPCReceivePort::create`, `0x1002ec500` | Loads allocation pointer +0x60, adds **12**, loads unsigned stored capacity N at +0x68 and tail-calls public receive create `0x1002ebbc0`. Registered target is the payload area and count is **N**, not N+12. |
| NHL `receiveEvents`, `0x1002ec520` | Copies/clones the supplied callback representation, swaps it into +0x70 through metadata-named `std::function::swap` at `0x1002ec730`, disposes the temporary and starts the lower receive loop with an empty callback holder. Generic `std::function` internals were not followed or treated as wire data. |
| Two-argument NHL receive constructor, `0x1002ec2a0` | Selects at least 32768 payload bytes, stores N, performs wrapping unsigned 32-bit N+12 allocation arithmetic, zeroes the allocation and initializes internal header words `4,16,0,0`. Calls the original default base receive constructor `0x1002eb9b0`; its ownership policy is not inferred from the recovered bool overload. |
| Base `Client` constructor, `0x1000893b0` | Stores incoming ESI at +8, role EDX at +0xc and ECX at +0x10; zeros +0x14/+0x18 and copies the original UTF-16 GP string at +0x20. At the reviewed callers these are the request word from message+0x14, actual role and generated local client ID. |
| `Connection` constructor, `0x1000203e0` | Calls that base constructor, stores vtable `0x10060f940` and clears pointers +0x38/+0x40/+0x48. |

The earlier receive constructor `0x1002ec340` likewise stores N before its
wrapping N+12 calculation. Combined with the original lower callback's
positive-length **strictly less than N** test, this distinguishes the twelve
internal control bytes from registered payload capacity. Do not silently fix
overflow/equality behavior as part of an ARM calling-convention change.

## Device selection, roles and generated names

The main-dispatch mapping below **reuses** the previously reviewed
`DeviceManager::handleRequest` assembly at `0x10001dc60` from
[`.18` feasibility evidence](NIHardwareAgentFeasibility.md); it is not a new
recovery cap or additional function count. That dispatcher requires at least
four payload bytes and masks the first payload word to its low24 bits.

| Low24 command / comparison | Original selected call site and target |
| --- | --- |
| `0x444300`, `0x10001dcb8` | `0x10001de08` calls `0x10000d770`, ordinary `MsgDeviceConnect` overload |
| `0x444900`, `0x10001dd45` | `0x10001e038` calls `0x10000d800`, identifier overload |
| `0x447500`, `0x10001dece` | `0x10001e298` calls `0x10000da50`, subscription |

Current CE's full `0x02444300` therefore selects **ordinary connect**. The
shared request constructor carrying a UTF-16 string does not make that request
an identifier-overload call. Original `.18` packet:
`.build/ni/agent-feasibility/20261025-ipc-four-stage3-delta/`, `evidence.json`
SHA256 `80b0b6db5ba3ea84bd28bef2656b69b0b9b65cdbccf2f157e68c047fa69b984c`;
reused `10001dc60` ASM SHA256
`10aa80f9ce4c084c798a2867cb1c8e0b6cc84e0c96bff1e7b761ec4a9cf62021`.
Only that reviewed assembly is reused here; no prior C output was reinterpreted.

`DeviceClass::connectClient(MsgDeviceConnect&)` at `0x10000d770` requires raw
class field +0xe0 to equal 1, selects a listed controller under its mutex and
delegates to `ControllerBase::connectClient` at `0x10002b110`; otherwise it
passes result `0x6563746c` to the original result writer. The field's complete
meaning is not invented as an instance count.

The identifier overload `0x10000d800` reads NUL-terminated 16-bit units at
message+0x20 and searches listed controllers. An exact ten-unit **`Don't care`**
match selects a listed controller without matching its stored identifier;
otherwise it compares the requested units to the controller's stored units.
The matching controller delegates to `0x10002f020`.

Both ControllerBase overloads inspect message byte +0x0f, which is the high
byte of the first payload word, separately from role word +0x18:

| Header high byte / actual role | Original result |
| --- | --- |
| 0 | `0x6f617069` |
| 3 or greater | `0x75617069` |
| 1 or 2, role `0x70726d79` (`prmy`) | Continue |
| 2, role `0x4d696469` (`Midi`) | Continue |
| 1, role `0x4d696469` | `0x6f617069` |
| Unsupported role | `0x65726f6c` |

`DeviceClass::subscribeClient` at `0x10000da50` accepts header high byte 1 or
2 with either of those roles; it does **not** apply the controller overloads'
old-header/Midi rejection. The paired CE constructor ORs `0x02000000` into its
request command, supplies product ID `0x1940` at internal+0x10, its device
argument at +0x14, role at +0x18 and UTF-16 string at +0x20. Thus the header
byte is not an invented client role.

Successful paths increment their original 32-bit local identifier under the
original mutex, construct a Connection and receive port, attach their bound
handler and queue the connection. The paths **ignore receive-create return
values** before replying; the extra MIDI receiver's create result is likewise
ignored. Do not insert new short-circuit/rollback behavior.

| Path | Exact original base format and suffixes |
| --- | --- |
| Plain controller | `NIHW%s%04d` |
| Identifier controller | `NIHW%s-%s%04d`; an empty identifier first becomes `Undefined%04d` using the original static counter |
| Subscription | `NIHWS%s%04d` |
| Per-client ports | Append `Request` and `Notification`; additional MIDI receive name appends `Midi` to Request |

The formatting path invokes the metadata-named Boost find/format operation
with a space and empty replacement; that generic body was not followed.
Identifier conversion delegates to original GP UTF-16-to-UTF-8 conversion,
whose complete character semantics are not invented here. The Request ports
bind `ControllerBase::onConnectionMessage` (`0x10002bf30`) or
`DeviceClass::onSubscriberMessage` (`0x10000e6a0`). The additional controller
MIDI receiver uses the same connection handler.

## Exact connection reply

| Writer | Observed internal message layout |
| --- | --- |
| `setResult`, `0x1002cf290` | If either reply offset +4 or count +8 is zero, sets offset **24**, count **4**; otherwise preserves the existing span. Writes the result word at +24. |
| `setInOutPortNames`, `0x1002cf120`, both names nonempty | First slot n1=`min_u32(u32(length1)+1,512)`; second n2=`min_u32(u32(length2)+1,508)`. First length at +28, bounded NUL bytes at +32; second length at +32+n1, bytes at +36+n1. Result `0x74727565` at +24; reply offset **24**, count **n1+n2+16**. |
| Same writer, either name empty | Clears result +24, first length +28 and a 16-bit zero +32; reply offset **24**, count **12**. |

Keep the unsigned 32-bit length operations and embedded-NUL/truncation
behavior. The nonempty reply count includes four bytes past the natural end
of those written fields; this helper does not write them. Their contents are
not proved zero or meaningful. Preserve the count rather than trimming it or
inventing tail data. The paired frontend's result/name getters use these fixed
positions, then its caller separately requires exact result `0x74727565`.

**Derived capacity inference:** the writer's maximum slot sum is
512+508+16 = **1036** bytes, exceeding CE's original requested reply capacity
`0x408` = **1032**. The reviewed original lower sender rejects a reply longer
than its requested capacity, so a reply at that maximum is rejected. This is
an arithmetic/source-contract inference, not proof that real RK3-generated
names reach both maxima. Preserve both original values rather than normalizing
either side.

After setting names and result true, both controller connect paths call
`onInstancesChanged(true)` (`0x10002ee00`). That method locks, recursively
invokes itself with false, unlocks and **falls through** into the selection
body again; there is no early return after recursion. For raw field +0x23c==2
it searches clients for role `0x496e7374`; otherwise a positive signed +0x234
selects clients with flag bit 4 at +0x18. At its first match it constructs
payload words `0x02564e66,0x494e5354,0x55706474` (count12, reply offset16/count0),
calls client virtual slot +0x18 and returns. This is not a broadcast to every
client. The true invocation has two logical passes; actual duplicate physical
delivery is not asserted without the called object's effects.

## Confirmation, initial messages and query

The controller and subscriber handlers require payload count >=4 and dispatch
using the first payload word's low24 bits. CE's reviewed post-connect command
is full **`0x02404300`**, with bounded ASCII count at internal+24 and name at
+28. The agent selected case `0x404300` calls **strlen(+28)**; it does not
validate or use the supplied name count in this branch.

If connection+0x38 already exists, either handler returns false without
assigning a reply. Otherwise it allocates the original IPCSendPort using that
name, stores it at +0x38 **before** `SendPort::open` (`0x1002eb910`), and assigns
reply offset16/count4. Success writes `0x74727565` at +16 and returns true;
failure writes zero and returns false, **without clearing +0x38** here. The
reviewed NHL callback clears reply control fields first and returns the
handler-assigned span independently of its boolean result: open failure still
has a four-zero-byte reply, while duplicate confirmation assigns no new reply.

The original NHL send constructor `0x1002ec250` calls base constructor
`0x1002eb7b0`, then sets its vtable. The base uses strlen on the supplied name,
creates the original OS implementation, zeros its callback holder and clears
invalidation byte+0x48. Public open stores the original callback holder and
tail-calls already-reviewed OS open `0x1002ed320`. Generic callback-holder copy
internals are not expanded. Controller open binds `onConnectionDropped`
(`0x100030f10`); subscriber open binds `removeSubscriber` (`0x10000f010`).

Controller confirmation success sends the following initial payloads through
the stored notification sender; send results are ignored:

| Predicate | Exact payload / count |
| --- | --- |
| Always | `0x02444e00`, true-word `0x74727565` or 0 according to raw controller byte+0x39; count8 |
| Raw word+0x28 nonzero | `0x02237349`, low32 of original truncating double+0x30→integer, 0; count12 |
| Same predicate | `0x0223733f`, raw word+0x28, 0; count12 |

All have internal reply offset16/count0. Their complete application-domain
meanings are not guessed from opcodes. Subscriber confirmation has no matching
controller initial-state sends.

Subscriber dispatch `0x447143` calls `onClientQueryConnected` (`0x10000f120`).
With a nonnull client, under its original mutex it visits listed controllers,
asks each original virtual slot+0xc0 to produce a GP string, and on true
constructs/sends a notification to the requesting client. It does not assign
a synchronous reply or fabricate an identifier when that virtual call fails.
The reviewed `MessageSetUTF16String` constructor (`0x1002cf040`) ORs
`0x02000000` into raw type `0x444e2b`, giving full **`0x02444e2b`**. It sets
reply offset16/count0, stores n=`min_u32(low32(sourceLength)+1,512)` at +24,
zeroes 1024 bytes at +28 and copies bounded NUL-terminated 16-bit units there.
Payload count is **16+2*n**. Preserve original high-surrogate tail handling
and unsigned wrap behavior. This constructor leaves +16/+20 untouched; those
bytes are not invented as zeros. The controller virtual string producer is
an explicit boundary, not a guessed serial-number schema.

Subscriber dispatch `0x536756` returns `DeviceManager::getVersion`
(`0x10001dc30`) at reply offset16/count4. It reads cache `0x100660a20`; when zero,
gets the current module pointer from `0x10067a808` through `0x1002c2820`, asks
`OSModule::getVersion` (`0x1002c27f0`) and caches its result shifted right8.
OSModule passes bundle at object+0x18 to `FileManager::getFileVersion`
(`0x1002b14b0`), returning its output word only on true, otherwise zero.
FileManager clears the output/returns false for a null bundle. Otherwise it
first calls imported `CFBundleGetVersionNumber` and returns true for nonzero.
On zero it asks `CFBundleGetValueForInfoDictionaryKey` with original imported
`kCFBundleVersionKey` (loader metadata binds `0x10060e6c0`), requires CFString
type, delegates CFString-to-GP-string conversion and searches for a digit. The fallback
accumulates decimal runs with original 32-bit arithmetic, masks each component
to eight bits and packs it at shifts 24,16,8,0 along its dot-continuation path;
it is not a new semantic-version parser. No hardcoded service version or
equivalence to CE's minimum `0x10001` is established. A zero result is recomputed on later calls because
zero is also the cache sentinel.

## Lifetime, focus and explicit boundaries

Subscriber `0x442d00` calls `unsubscribeClient` (`0x10000eee0`): optional
original diagnostics, then `removeSubscriber`. Removal holds class mutex
+0x140, searches for the exact client pointer, erases it through metadata-named
original deque::erase `0x10000f830`, invokes virtual slot+0x20 then +8 and sets
class byte+0x1b4=1. The generic container body is not expanded.

Exactly three original Connection vtable qwords bind selected operations:

| Slot | Raw target / original symbol identity |
| --- | --- |
| +8 | `0x100020420`, deleting `Connection::~Connection` |
| +0x18 | `0x100020530`, `Connection::sendMessage` |
| +0x20 | `0x100020550`, `Connection::shutDown` |

The separately recovered `Connection::sendMessage` body (`0x100020530`)
returns false if +0x38 is null; otherwise it returns the original NHL send
result. Thus initial/state sends now link to the already reviewed CFMessagePort
transport rather than an invented queue.

`Connection::shutDown` (`0x100020550`) affects **only** request receiver+0x40:
it calls original public receive stop(true), then clears +0x40. It does not
close or delete the receiver, nor touch +0x38/+0x48 here. D2 destructor
`0x100020450` closes and dispatches the deleting-destructor slot on notification
sender+0x38 if present; for each remaining receiver+0x40 and +0x48 it calls
stop(false), public close and its deleting-destructor slot, then tail-calls
metadata-named base Client destructor `0x100089540`. D0 `0x100020420` calls D2,
then imported operator delete. Preserve the shutdown→D0 asymmetry and the
original lower stop(true)/deferred-owner route from the transport contract;
do not substitute joining all receivers or immediate deletion.

Normal-client invalidation `onConnectionDropped` at `0x100030f10` holds the controller mutex,
finds the exact pointer, clears focus via `setFocusClient(NULL,false)` if it
was current, decrements positive raw+0x234 only for client flag bit4, erases,
then invokes shutdown and deleting-destructor slots. If it was raw+0x228 it
clears that pointer under its second mutex and conditionally calls original
`setMIDITransportEnabled(true,false)` (`0x100029bd0`). It sets byte+0x21c=1;
when the removed client was prior focus it invokes original
`chooseNewFocusClient(currentMode,false)` (`0x1000298c0`). Those domain
continuations are preserved boundaries, not permission to invent a new focus
or MIDI-transport policy.

`setFocusClient` (`0x100030690`) holds its own mutex. When moving from a
non-null different client it clears current+0x220, optionally sends old client
`0x02434e00,0`, then sends new client `0x02434e00,0x74727565`. It publishes new
+0x220 and mode+0x23c **only if that send returns true**. Midi/SMap select mode1,
prmy/scnd/Slep mode0, Inst mode2; an unrecognized role retains the prior raw
mode. Null new client clears +0x220; it does not independently reset +0x23c.
When new mode0 is successfully published, the initial original virtual+0x1c8
predicate was false, a later call to that predicate is true and raw+0x228 is
nonnull, the body sends that target payload
`0x02564e66,0x4d547074,1` (count12, reply offset16/count0); its result is ignored.
At the end +0x244 mirrors +0x23c only when the mode is not2, then original
virtual+0x120 is invoked unconditionally. These virtual continuations are
preserved boundaries, not replaced with no-ops.

Controller command `0x432d00` is a mutex-protected queue/focus handoff calling
`setFocusClient(next,true)`, then returning true without assigning a reply.
It is **not** evidence for labeling that command generic disconnect cleanup.
Explicit controller disconnect dispatch also distinguishes `0x442d00` (calls
`removeClient`, returns true without a new reply) from `0x446400` (calls the
same helper, then writes reply offset16/count4/result `0x74727565`). The
original `removeClient` body (`0x100030380`) is distinct from invalidation:
for a nonnull argument it clears matching current focus **before** searching
the queue, applies the same found-client flag/decrement/erase/shutdown/delete
sequence, retains the original raw+0x228/MIDI and focus-choice continuations,
and calls `onInstancesChanged(false)` near the end even when the pointer was
not found. `onConnectionDropped` searches first and does not make that final
instances-changed call. The former-focus pointer comparison can also cause
`chooseNewFocusClient` when the explicit removal search did not find a client.
Neither body is substituted with the other from its name.

Unrelated controller RPC branches, full device-information schema, hardware
binding, all GP/Boost internals and every original application path are outside
this recovery. Actual frontend acceptance remains a later explicit gate.

## Evidence and review

Private root: `.build/ni/recognition-contract/agent/`. Each packet uses the
pinned source and a fresh nonhidden private copy of the preserved no-analysis
import. Every run uses `-process -noanalysis -readOnly`, explicit
`DisassembleCommand.enableCodeAnalysis(false)`, prevalidates all absent and
nonoverlapping targets before mutation, preserves all local/external function
and instruction metadata, checks exact target deltas and full new instruction
spans inside original `LC_FUNCTION_STARTS`/next-start/`__text` caps, and exports
ASM only. Actual guard markers, script errors and counts govern success, not
process exit status alone. Original source/project hashes remain unchanged.
Independent technical review passed before each packet's semantic use.

| Packet | New functions / full instruction spans | evidence.json SHA256 |
| --- | --- | --- |
| A | 5 /839 | `9d6f519267e504fadccdd20fd84d3d8681b6c92813e66490439fbb994633e07c` |
| B | 4 /1702 | `a8c738673d69114e1b17177ad130e194a3d28fb2303944fbfc8906231ccc886a` |
| C | 4 /491 | `f3f26a4bfd555b421ef6e335d537b82ee7add30d6fe607993605f78347fe5417` |
| D | 1 /2147 | `0598a5dda4aa0a659c6858ec167fea43944eaad6ad83427350431ec81c7dccd6` |
| E | 10 /634 | `acb4eca2879a4dc9baabc35738ac9c590bb7a56c9a7b53eedf4b65ae55cdced4` |
| F | 3 /81 | `392c43a0cab87e29d33cc62171aa62c666fe9c7ba82cd8317043f4680ca01dc1` |
| G | 6 /348 | `3977a1291ff733672789f876f1aa6ab6364557d939c9bdea709f3d2786b6ac38` |

These are **33** body caps and **6242** decoded instruction records across
isolated packets, each starting from 861 functions/zero instructions. D exports
its whole cap for guard auditing but semantics are limited to selected actual
recognition/lifetime branches; it does not admit unrelated RPC recovery.
Original LC envelopes are hard analysis limits, not guaranteed complete
logical function/exception-path boundaries. No decompiler/C output, guessed
types/prototypes, flow overrides or automatic callee following ran.

| Exact raw-data packet | data-evidence.json SHA256 |
| --- | --- |
| B:20 bytes `0x100562f60`,35 bytes `0x1005534ed` | `be35a2329caf8cd95bb8aa5a264ff6526a2a34131acfcf8e6caf49370752458c` |
| C:bounded NUL strings at `0x100553943/953/959`, four-byte suffix `0x10055394e` | `94124483c73453c53e24cd670881b022d4db2104d34f10a37f342474df2faa2f` |
| Corrected identifier format at `0x100553961`,14 bytes through NUL | `5068453ac5d13152cf7ffdbe3d30e5b3547adfda03799adc3bbe47326008185b` |
| F:exact qwords `0x10060f948/958/960` | `3f634310b605f51ec822a3b6d69087131cf663e7e0ce1880976053215f246360` |

Each data packet preserves source hash, VM-to-file mapping, exact raw bytes,
lengths and relevant terminators/metadata. The initial `0x100553959` identifier
format association was a misaddress inside `Undefined%04d`; it is explicitly
abandoned. The actual B LEA target is `0x100553961`, recovered separately after
its corrected scope was recorded. No semantic claim uses the wrong pointer.
Raw proprietary listings and copied project packets remain private/ignored.

Final loader-only `phase-g-key-binding/metadata-evidence.json` SHA256:
`e2c1c32eef9123cecf1d5c8355e7c4ec9ac349d7bdabbf2f404c6f4481ab690c`.
It binds GOT section slot214 / indirect index646 / nlist22741 to imported
`_kCFBundleVersionKey`, with exact load-command, section, indirect, nlist and
name hashes. Resolver SHA256:
`6cfacf2c7fc9eb28688fece9e337b789e69d1d0bf89da6c46d03e1571710e67c`.
No raw GOT pointer value, target body or module initializer was read. This
metadata and the final report passed independent review; the bundle's actual
runtime value is still dynamic, not frozen to an invented original word.
