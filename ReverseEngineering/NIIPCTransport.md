# NI IPC framing and OS-port lifecycle

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.19**.

**The original NI agent uses named CFMessagePort ports. Its outgoing CFData
contains the supplied payload bytes, while the first payload word is also the
CFMessagePort message ID. The 12-byte NHL message-control prefix stays in the
process.** The original name path preserves the ASCII endpoint
`NIHWMainHandler`. Request/reply ownership, validation, timeouts, invalidation,
receive-thread startup, stop modes and local owner/destructor routes are
recovered below. These are contracts for a lift-and-shift, not a new protocol.

This research changes no production code, installed driver, audio ring,
timing, recovery, NI application, security setting or account. SIP remains a
required constraint. No NI executable, original-named endpoint or hardware
interaction was run. Full frontend connection/version/subscription/ownership
handshake, startup/signing expectations and actual NI automatic recognition
remain separate gates in **rigkontrol3-40h.2** and **rigkontrol3-40h.17**; this
report does not establish an ARM agent implementation or those outcomes.
[Prior feasibility scope](NIHardwareAgentFeasibility.md).

## Request and reply contract

Evidence is the pinned NIHardwareAgent **1.7.4/R199**, SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
Addresses below are original unslid x86_64 addresses. Object offsets describe
in-process NI objects, never a license to transmit native pointers or C++
containers. The paired Controller Editor identity remains the one established
in the prior feasibility report.

| Boundary | Recovered NI behavior and evidence |
| --- | --- |
| NHL send wrapper, `0x1002ec270` (prior reviewed packet) | Passes payload `message+12`, unsigned 32-bit byte count at `message+0`, and optional reply storage `message+u32(+4)` with capacity `u32(+8)`. Therefore the three control words are excluded from outgoing CFData. This is not a general schema for every payload. |
| `SendPort::sendMessage`, `0x1002eb940` | If its OS implementation is absent, returns false. Otherwise delegates unchanged arguments to `0x1002eda40`. Afterwards, if its invalidation flag at +0x48 and callback holder are set, invokes that callback, while preserving the send result (`0x1002eb958–0x1002eb97d`). There is no flag clear or reconnect here. |
| OS send, `0x1002eda40` | Requires a stored CF port, nonnull input bytes and nonzero input count. At `0x1002edaaa–0x1002edab6`, calls `CFDataCreate(NULL, originalBytes, originalCount)`: all supplied payload bytes are copied. At `0x1002edae2`, loads the first payload **32-bit word without masking** as the separate `SInt32 msgid`. No byte swap or extra wire header is added. The body does not first require count >=4. |
| Reply selection | A reply is requested only when **both** reply pointer and capacity are nonzero. The mode is `kCFRunLoopDefaultMode`; otherwise mode is NULL. The return-data slot is initially NULL and is supplied in both cases (`0x1002eda97–0x1002edb0b`). Both send and receive timeout values are the original **2.0 seconds**. |
| Send result | Releases the outgoing CFData after `CFMessagePortSendRequest`. API status 0 returns true without a requested reply. With a requested reply, requires nonnull returned data and a positive length no larger than capacity; the machine comparison is signed 64-bit. Copies exactly that reply length and releases returned CFData (`0x1002edbd4–0x1002edc03`, `0x1002ee09c–0x1002ee0bd`). No output-length value is returned; unused destination bytes are not cleared. |
| Invalid reply / send failure | Missing, empty or oversized requested reply returns false; a present invalid reply is released. Nonzero API statuses return false through diagnostics. The original switch explicitly distinguishes -4/-3/-2/-1; all other nonzero statuses use the default diagnostic route. No retry, invalidation, close or reopen is present in this send body. |
| OS receive callback, `0x1002ee910` | Uses context->owner, ignores the incoming `msgid`, and requires nonnull CFData, positive length and length **strictly less than** the registered receive capacity (signed 64-bit comparison, `0x1002ee91f–0x1002ee944`). Copies exactly that data into the registered buffer, then invokes owner virtual slot +0x10 with length and initially-zero reply pointer/count outputs. It does not reconstruct input bytes from `msgid`. |
| Callback reply / ownership | If both handler-produced reply pointer and count are nonzero, returns a new `CFDataCreate(NULL, replyBytes, replyCount)`. Otherwise, including rejected input, attempts to return a newly created **empty CFData**, rather than explicitly returning NULL (`0x1002ee984–0x1002ee9a0`). The callback does not release that returned data. It performs no additional reply-span validation. |

The earlier NHL receive callback `0x1002ec650` stores the incoming length's
low 32 bits at message+0, clears the internal +4/+8 reply fields, dispatches its
handler and exposes reply storage from those fields. This now connects to the
OS callback's actual CFData length, not its `msgid`. The NHL constructor
`0x1002ec340` initializes its separate internal buffer, clamps the unsigned
32-bit payload capacity to a minimum of 0x8000, then computes the allocation
size using **32-bit capacity +12**, which can wrap for extreme inputs. This is
not a no-overflow guarantee and does not prove the capacity later passed to
`ReceivePort::create`.

Apple's API declarations independently identify `SInt32 msgid/status`,
`CFIndex` data lengths and `double CFTimeInterval`. NULL reply mode means no
reply wait. Apple requires copying callback data kept after return and releases
the CFData returned by a callback; NI's input copy and newly created reply
follow that ownership boundary. These API facts interpret the observed calls,
not supply missing NI behavior.
[Apple CFMessagePort header](https://github.com/apple-oss-distributions/CF/blob/main/CFMessagePort.h),
[CFData API](https://developer.apple.com/documentation/corefoundation/cfdata),
[CFMessagePortSendRequest](https://developer.apple.com/documentation/corefoundation/cfmessageportsendrequest(_:_:_:_:_:_:_:)).

## Endpoint name and creation

The previously reviewed `ConnectionPort` constructor `0x1000203a0` passes
`NIHWMainHandler` and length 15 to NHL `IPCReceivePort`, which calls the base
`ReceivePort` constructor at `0x1002eba50`. That base reads the source pointer,
uses **strlen** and copies the NUL-terminated string into its name field at +8
(`0x1002eba6f–0x1002eba83`). It does not use the supplied CharSequence length
for that copy, nor add a prefix/suffix in this path.

Both OS open/create extract the stored name's data and count and call
`StringASCIIC1` at `0x1002ae5f0`, a thunk to `0x1002ae600`. Its actual loop
sign-extends each source char byte and stores one 16-bit unit, then terminates
with zero (`0x1002ae682`, `0x1002ae6c7–0x1002ae6db`). It is **not UTF-8
decoding**: high-bit bytes become 0xff80–0xffff units. Both callers pass those
units/count to `CFStringCreateWithCharacters`, then use the resulting CFString
for CFMessagePort creation. ASCII `NIHWMainHandler` is therefore unchanged
through the recovered construction/registration path. Actual successful
registration and frontend use were not executed.

| OS creation boundary | Original ownership and failure behavior |
| --- | --- |
| `open(SendPort*)`, `0x1002ed320` | Rejects an absent owner or already-present port, stores owner at OS +0x70, creates the temporary name CFString, calls `CFMessagePortCreateRemote`, then releases the temporary CFString. A NULL name or remote port follows diagnostics/false. On success, under the original recursive thread-owner/semaphore map lock, records CF port -> OS implementation, then installs `onPortInvalidated` and returns true. |
| `create(ReceivePort*)`, `0x1002ee4d0` | Rejects absent owner/already-present port, stores owner and builds a context with version 0, info=this and NULL retain/release/copyDescription. Calls `CFMessagePortCreateLocal` with callback `0x1002ee910` and **shouldFreeInfo=NULL**; releases the temporary name CFString. Returns true for nonnull port, false through diagnostics otherwise. No map insertion or invalidation callback registration appears in this local-create body. |
| Public receive create, `0x1002ebbc0` | Rejects NULL external buffer or zero count. Stores buffer/count at owner +0x30/+0x38 **before** OS create. Does not clear those fields when OS create fails. |

Apple documents same-name local-port reuse: creation can return an existing
local port and ignore the supplied callback/context. NI supplies no
`shouldFreeInfo` output and no context retain/release callbacks here; do not
invent an additional ownership/registration policy during the ARM port.
[CFMessagePortCreateLocal](https://developer.apple.com/documentation/corefoundation/cfmessageportcreatelocal(_:_:_:_:_:)).

## Receive-loop and owner lifetime

These are the original local operation routes, not a claim that the entire
application/client handshake or generic NI thread framework is recovered.

| Operation / original body | Exact observable behavior |
| --- | --- |
| Public start, `0x1002ebbf0` | Requires implementation and port; assigns the supplied callback holder through its existing helper, then tail-calls OS start. The holder helper's internals were not followed. |
| OS start, `0x1002ee9c0` | Acts only while active dword +0x78 is 0, sets it to 1, creates HEvent if absent, constructs original priority enum 4, and creates HThread only if its handle is absent. Calls `HEvent::wait` with literal **5000**. Failure routes through diagnostics then the same event cleanup; destroys/clears the event if present. Units, effective priority and GP thread scheduling policy were not inferred from helper names. |
| OS run, `0x1002eee70` | **Signals HEvent before checking active or creating/scheduling a source.** If active==1: creates CFMessagePort run-loop source with order 0, stores `CFRunLoopGetCurrent`, adds source in `kCFRunLoopCommonModes`, releases its local source reference and tail-calls `CFRunLoopRun`. Therefore the startup acknowledgment is not proof the listener was already scheduled. No explicit RemoveSource appears. |
| Public stop(false), `0x1002ebc30`; OS stop(false), `0x1002eec50` | OS body acts only if active!=0: clears active, invalidates port, stops saved run loop, calls original `HThread::waitTermination` then `HThread::close`, and zeros thread storage +0x10..+0x28. Public wrapper subsequently destroys/resets its callback holder. |
| Public stop(true); OS stop(true) | Public wrapper **first clears owner->implementation at +0x20**, then tail-calls OS stop(true), leaving its callback holder untouched. If active!=0, OS sets byte +0x30 to1, clears active, invalidates port and stops saved run loop. It skips waitTermination/close and thread-storage clearing. Exact GP handling of byte +0x30/deferred execution remains outside this packet. |
| Invalidation callback, `0x1002ed940` | Ignores context info, finds the incoming CF port in the locked map, then sets the mapped owner's byte +0x48 to1 if links exist. Does not erase the map entry, release a port, invoke the owner callback, retry or reconnect. The send wrapper's later callback dispatch is a distinct observed route. |
| OS close, `0x1002eece0` | First clears the CF invalidation callback. Removes map entry, releases CF port and nulls +0x50 **only when active==0 and port!=NULL**. It does not stop, join or invalidate by itself. |
| Public closes, send `0x1002eb990` / receive `0x1002ebca0` | Require implementation and port, then call OS close. Receive close afterwards clears buffer/count even if OS close skipped release due to active!=0. Neither public close stops the receive loop. |
| Owning destructors, `0x1002eb750`, `0x1002eb870`, `0x1002ebb20` | Send/receive destructors clean their callback holder; all three perform `resetMaster` if an implementation exists, reread the implementation and, only if it remains nonnull, dispatch **virtual slot +8**, then destroy the name string. They do not directly call stop/invalidate. `resetMaster`, `0x1002eee60`, only clears OS owner +0x70. |
| OS destructor variants, `0x1002ed140/160/170` | D1 tail-calls D2; D0 calls D2 then the known operator-delete import. D2 removes a remaining port from the locked map and releases it regardless of active, without explicit callback clearing, invalidate, stop or join. If owner exists and owner->implementation==NULL, dispatches owner **virtual slot +8**. The slot's exact vtable target is not established here. No generic deferred-thread cleanup policy is inferred. |

The map has original `pthread_self` owner/recursive-depth bookkeeping, atomic
contender counts and `semaphore_wait/signal`; the inspected bodies retain that
ordering. Apple documents that AddSource retains its source, so NI's immediate
release retains a documented run-loop owner. Port invalidation also invalidates
its associated source. These facts explain the observed ownership without
inventing a new remove-source/retry sequence. Apple says Stop exits the
innermost run-loop activation, not every possible activation.
[CFRunLoopAddSource](https://developer.apple.com/documentation/corefoundation/cfrunloopaddsource(_:_:_:)),
[CFMessagePortInvalidate](https://developer.apple.com/documentation/corefoundation/cfmessageportinvalidate(_:)),
[CFRunLoopStop](https://developer.apple.com/documentation/corefoundation/cfrunloopstop(_:)).

## ARM boundary and next proof

Keep payload bytes, full 32-bit message ID, 32-bit NHL control fields, signed
length comparisons, original 2-second timeouts, reply/no-reply selection,
copy/release rules, name byte widening, strict receive-capacity boundary and
all lifetime asymmetries above. Both x86_64 and arm64 macOS use 64-bit native
pointers/long/CFIndex; their different register/stack calling conventions are
an implementation boundary, not permission to change these contracts.
[Apple ARM64 ABI](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms),
[Apple Intel64 ABI / LP64](https://developer.apple.com/documentation/xcode/writing-64-bit-intel-code-for-apple-platforms).
Native compilation must rebuild internal C++ objects and calls rather than
copy x86 instructions or put C++/Boost object representations on the wire.
No unavailable API or required change to this CF contract has been established
by the recovered bodies; a replacement IPC system is not justified here.

**Smallest next proof, planned only:** an offline typed ARM contract harness
with synthetic buffers and recorded/injected CF call outcomes, no live
CFMessagePort registration. Check exact payload exclusion of the 12-byte
prefix, full first-word ID, optional reply selection, timeout/mode arguments,
empty/equal/oversized receive boundaries, reply copies/releases, error returns
and the separately recorded lifecycle call order. Use fixed-width byte fields
and explicit signed comparisons rather than copied C++ layouts. Include the
original quirks instead of silently correcting them. This proof needs its own
Beads admission before coding; it has **not** been implemented or executed.
It would not prove frontend acceptance, installation, device discovery,
application handshake, native-agent source completeness or GP deferred-thread
policy. Those remaining gates stay explicit; they do not justify altering
NI audio timing/recovery or disabling SIP.

## Private audit packet

**26 unique original functions** were examined. The Phase A supplement rereads
the same four Phase A functions, rather than adding four new ones. Only narrowly
admitted bodies were disassembled. Every run processed a fresh
nonhidden private copy of the preserved no-analysis import, with
`-process -noanalysis -readOnly` and explicit `enableCodeAnalysis(false)`.
Every target was absent and disjoint from the full local+external baseline
before mutation. Baseline function/instruction metadata stayed immutable;
function deltas matched exact whitelists; full new instruction spans stayed
inside original `LC_FUNCTION_STARTS` caps in `__TEXT.__text`. These caps are
analysis limits, not proof of logical function ends. Source-project file
hashes stayed unchanged. Script errors, guards and output counts were checked
independently of exit status. Every packet has **zero C/decompiler outputs**.

The initial send-body export decoded the status table incompletely. The
separate supplement reads only its 16 bytes at `0x1002ee4c0`, excludes those bytes
from code and seeds the four proven in-cap destinations. It is the authority
for status-branch claims. An 8-byte read at `0x1004de328` proves double 2.0.
Exact Mach-O indirect-symbol records associate `0x10060e6f0` with
`_kCFRunLoopDefaultMode` and `0x10060e6e8` with `_kCFRunLoopCommonModes`;
zero-filled on-disk pointer slots alone were not treated as runtime values.

Persistent private evidence root: `.build/ni/agent-ipc-transport/`.
Each packet preserves original caps/hashes, script/diff, command/log,
source-project before/after manifest, ASM, and metadata/guard audits. The
packet's `evidence.json` SHA256 anchors those artifacts:

| Packet | New functions / instructions | evidence.json SHA256 |
| --- | --- | --- |
| phase-a | 4 / 524 | `88e6d1d7fa9c571d8675b6316f288376ffc8cfc2ed88d41e4c058a3736fa706e` |
| phase-a-supplement | 4 / 542 | `4dd111a52da602a408bca51b046f824dd009527deb79153263118c4368a32eb4` |
| phase-b | 7 / 843 | `622dfdd7944c2662547221f021bbf679ca6489aa64877800c625692aa5df2a9b` |
| phase-c | 2 / 40 | `f29169ef60a8aaa7e4aef922a4ada0031525050525a160f037148835af3bb5f6` |
| phase-c-conversion | 1 / 71 | `8824b1ecf3c25e8a8ac220f13b4794bc5d85e775781fde9060d3b6b8e32d015c` |
| phase-d | 5 / 138 | `5196da0e1e9cf6c2afb7a16bb2ef2d1175401e2892915c77ad919f496e5771c4` |
| phase-e | 6 / 168 | `0c3fc40cd4cb8d737cf7a8ea2ee64f29dbf98e8738d5475b73e68a748cf1c983` |
| phase-f | 1 / 5 | `226bc25a8518896c71c784f0be4d506bb3c9e07975723c888e5d2f9db0441fd5` |

Each baseline contained 861 functions (431 local + 430 external) and 0 instructions;
counts are preserved-baseline deltas, never an assumption of an empty program.
Exact original whole-binary hash and LC metadata cap hashes are in each packet.
Raw proprietary instruction listings remain private/ignored; this report
records derived contracts and evidence addresses.
