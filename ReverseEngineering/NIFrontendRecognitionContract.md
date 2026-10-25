# Controller Editor recognition and startup source contract

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.22**.

This source-only recovery examines the exact Controller Editor **1.7.4/R199**
paired with the original NIHardwareAgent. It establishes the selected RK3
connection, version, subscription and bootstrap operations below; it does not establish a
working native agent, all frontend access paths or live NI recognition.
No NI application, helper, script, endpoint, device, driver or security setting
was executed or changed. SIP remains an unchanged requirement.

The frontend SHA256 is
`ef1cee113d431ccd6946ee92631c55a6dedeb2b9a5d8ea12b150f91acc2cfad9`.
Addresses are original unslid x86_64 addresses. In-process message offsets are
separate from transmitted bytes, as established by the
[original transport recovery](NIIPCTransport.md). The matching agent's side is
recorded in [the agent recognition contract](NIAgentRecognitionContract.md).

## RK3 caller and version behavior

| Original boundary | Assembly-grounded contract |
| --- | --- |
| `RigControl3::connectToDevice`, `0x10067d150` | Reorders its arguments, supplies product word `0x1940`, and tail-jumps to generic `Controller::connectToDevice` at `0x100677410`. It does not itself discover a driver or construct the request. |
| Generic connect, `0x100677410` | If controller byte +0x38 is nonzero, returns true immediately. Otherwise saves the caller callback at +0x58 and constructs a request through `0x100664e50`, passing raw message type `0x444300`, product and forwarded caller word, string and role word. It selects connection mode 1 only when the role word equals `0x4d696469`, otherwise 0, and calls `establishConnection` with minimum version `0x10001`. The request layout is recovered below. |
| `establishConnection`, `0x100676970` | Rejects an already-connected controller, clears its error word +0x3c, and first prepares a version query with internal count 4, full payload command `0x02536756`, reply offset 16 and capacity 4. It uses the static connection sender `0x10068c730`. Successful version retrieval is compared **unsigned** against the supplied minimum; a lower value records `0x76657273` and returns false. |
| Failed version query | Clears the saved service-version cache and calls `fixServiceStartup`. A true startup result loops back to retry the version query. A false startup result proceeds to the actual connect request; it is **not** an unconditional abort or an invented missing-version rejection. No delay or finite retry count appears in this inspected loop. |
| Actual connect result | Sends the supplied connection request. Transport failure records `0x736e6664`. On transport success, requires `getResult()==0x74727565` before constructing the per-client ports; other result values become the controller's error word. Subsequent source operations open returned send-port names and create a returned receive-port name, with mode-dependent behavior. |
| Connected utility `getServiceVersion`, `0x100677940` | Requires controller byte +0x38 already nonzero. Builds the same version command/count/reply fields, dispatches virtual slot +0xd8, and copies its reply word only on success. The compiled Controller and RigControl3 slot maps identify the selected sender described below; this utility remains distinct from the static startup query path. |

`0x02536756` is the **full** request word, including its high byte. The agent
manager's earlier low-24-bit dispatch does not justify clearing that byte in
new code. Result constants and errors above are numeric original words; they
are not byte-swapped human-readable payload strings.

## Current connect request layout

The shared request constructor `0x100664e50` ORs its message type with
`0x02000000`; current RK3 connect therefore transmits full word
**`0x02444300`**. The high byte 2 is a constructor-added header value, not
the role word. Its in-process layout is:

| Offset | Source-defined value |
| --- | --- |
| +0 | Outgoing payload count, ultimately low32(`20 + 2*originalStringLength + 2`) |
| +4 / +8 | Reply offset 24 / capacity `0x408` |
| +12 | Full message type, `0x02444300` for this caller |
| +16 / +20 | Product `0x1940` / forwarded caller word |
| +24 | Forwarded 32-bit client-role word |
| +28 | `min_u32(low32(originalStringLength)+1,1024)` |
| +32 | Forwarded UTF-16 request string units (bounded and NUL-terminated) |

The constructor clears `0x804` bytes from +28, copies bounded 16-bit units,
and avoids leaving a trailing high surrogate at the truncation boundary.
Its final outgoing count uses the **original**, rather than clipped, string
length and 32-bit arithmetic. Do not silently equate that count with copied
units, infer safe arbitrary-input bounds, or assign hardware-identifier semantics
to the forwarded request string. The reviewed original dispatcher routes this
caller's `0x444300` to ordinary `MsgDeviceConnect`; its dedicated identifier
overload uses `0x444900`. The semantics of the forwarded +20 caller word beyond
its storage are unproved here.

## Reply access and port ownership

The frontend accessors read fixed positions in its in-process connection
message, rather than independently validating an arbitrary external frame:

- `getResult`, `0x100664f60`, reads the word at message+24.
- `getInPortName`, `0x100664f70`, returns message+32 when that result is nonzero;
  otherwise it returns the literal **`<null>`**, including its terminating NUL.
- `getOutPortName`, `0x100664f90`, returns
  `message + 36 + u32(message+28)` when the result is nonzero, otherwise the
  same literal pointer. It adds no further span or termination checks.

These address calculations match the agent setter's recovered result/span/name
positions. The accessors accept any nonzero result locally; the caller's
separate exact `0x74727565` comparison is the connection-success gate.

In `establishConnection`, the first returned name constructs a SendPort at
controller+0x40 and opens it with an invalidation callback. Connection mode 1
also appends the exact four bytes **`Midi`** to that name and opens the second
SendPort at +0x48. The other returned name constructs an IPCReceivePort at +0x50,
creates it and installs a mode-dependent receive handler. The source then sends
an additional `0x404300` message through the first returned SendPort and requires
both transport success and result `0x74727565` before retaining connected state.
The constructor `0x100664d90` makes that confirmation word **`0x02404300`**.
It stores `n=min_u32(low32(ASCII length)+1,512)` at message+24, NUL-terminated
name bytes at +28 and outgoing payload count `16+n`. It clears only the 512-byte
name region from +28: message+16/+20 are **unwritten in this body**, not proved
reserved zero words. The caller's reply at +16 is distinct from the earlier
connect result at +24. Per-client NHL sender `0x10068c700` passes the same
payload+12/count and optional reply offset/capacity into the raw sender.

On the inspected receive/create/confirmation failure path, the controller
clears connected state, closes/releases its stored SendPorts and dispatches the
receive object's virtual slot +8. The port targets and generic GP/runtime
cleanup require the separately recovered original runtime; no replacement
thread or automatic recovery policy is inferred.

## Main connection sender

NHL `IPCSendPort::sendMessageToConnectionPort`, `0x10068c730`, passes outgoing
message+12 with the 32-bit count at +0. Its optional reply pointer is
message+u32(+4), enabled by the 32-bit capacity at +8. The exact 15-character
name is **`NIHWMainHandler`**; its 16-byte NUL-terminated source literal was
separately recovered. Intermediate `0x10068bf00` tail-jumps to the lower
static sender `0x10068e0b0`.

That lower sender constructs a **stack-local** SendPort, opens it with an empty
invalidation handler, sends only if open succeeds, closes after that send, then
destroys the local object. This body has no persistent NI connection cache;
it does not prove absence of framework-internal port reuse.

The selected open wrapper reaches OS open `0x10068cf00`, which creates a
CFString from its stored name and calls `CFMessagePortCreateRemote`, releases
the temporary CFString, records the port mapping and installs invalidation
handling. The raw send wrapper reaches `0x10068d620`, whose API preparation
copies exactly the supplied bytes into CFData and duplicates their first
32-bit word as `msgid`. Reply mode is `kCFRunLoopDefaultMode` only when both
reply pointer and capacity are nonzero, otherwise NULL. The separately mapped
original timeout constant is **2.0 seconds** for both timeout arguments.

A requested reply must be nonnull, positive length and no larger than the
signed capacity; accepted bytes are copied and returned CFData is released.
Nonzero API statuses follow diagnostics/false without a retry in this body.
The four status -4/-3/-2/-1 arms were recovered only after excluding the exact
16-byte data table and seeding its four proven in-cap targets. The earlier
trailing table-as-instruction artifact is not original executable behavior.
This proves a terminal CFMessagePort path for these operations, not the absence
of every other frontend hardware-access path.

The compiled virtual sender `sendServerMessage`, `0x100677de0`, first requires
connected byte +0x38. It selects the secondary SendPort +0x48 only for low-24-bit
command `0x4d5300` when that port exists; otherwise it selects the primary
SendPort +0x40. It then calls the already recovered per-client NHL sender
`0x10068c700`. The selected connected-version query, instance query and
disconnect commands therefore use the primary port through the same CF path,
with their original message fields and no newly inferred transport policy.

## Subscription, instance notifications and disconnect

`subscribeToDevice`, `0x1006774f0`, uses the same request constructor with raw
message type `0x447500`, hence full **`0x02447500`**. It stores the subscription
callback at controller+0x60 and calls establishment in mode 2 with minimum
version **`0x10200`**. This is distinct from the ordinary connect minimum and
role-dependent mode selection.

`queryConnectedDevices`, `0x100677bf0`, acts only while connected and dispatches
full **`0x02447143`**, count 4 and reply capacity zero, through virtual slot
+0xd8. It does not obtain a synchronous device list in this body. The paired agent sends asynchronous UTF-16 instance messages consumed by the
mode-2 handler recovered below; this query body obtains no synchronous list.

For the controller callback at +0x58, `onServerMessage`, `0x100678100`, dispatches
low-24-bit types after requiring count at least 4 and that callback:
`0x444e00` compares message+16 with `0x74727565`, updates controller+0x68 and
invokes callback slot +0xa8 with the resulting Boolean. `0x434e00` similarly
updates byte +0x70 and calls slot +0xb0. These match the paired agent's initial
status/focus messages; no deeper callback or unrelated payload branch is
recovered here. Mode 2 establishment binds encoded member pointer `0xe9`, selecting virtual
offset +0xe8 rather than the ordinary handler at +0xe0. The selected base
constructor `0x100676890` actually writes vptr `0x100be9540`, exactly 16 bytes
after the declared Controller table header. Its +0xd8/+0xe0/+0xe8 slots resolve
to `sendServerMessage` (`0x100677de0`), `onServerMessage` (`0x100678100`) and
`onServerSubscriptionMessage` (`0x1006787d0`). The separately declared RigControl3
table has the same three pointers; this is compiled-data evidence, not proof
that a runtime RK3 object has been constructed or accepted.

`onServerSubscriptionMessage`, `0x1006787d0`, requires internal count at least
4 and a subscription callback at +0x60. Low-24-bit type **`0x444e2b`** scans
NUL-terminated 16-bit units beginning at message+28, constructs a GP String and
invokes callback slot +0x10. Type **`0x444e2d`** similarly invokes slot +0x18.
It ignores the declared length word at +24 and adds no span or termination
check. The first branch matches the agent's recovered asynchronous query
writer. The second is a source-observed consumer; this report does not infer
an uninspected producer or recover the application's later UI callback body.

`disconnectFromDevice`, `0x100677560`, first notifies its stored controller
callback with false and resets local state. While connected, cached version
at least `0x10200` selects full **`0x02442d00`**, count 4/no reply; older versions
select **`0x02446400`**, count 4, reply offset 16/capacity 4. Both use virtual
slot +0xd8. The source ignores the send result, clears connected state, closes
and releases the two SendPorts, then calls receiver stop(false), close and
virtual slot +8 before nulling its pointer. The declared sender slot is resolved above; generic receiver/runtime cleanup
helpers remain separate from those observed calls. For normal controller request ports, the paired agent routes these two
explicit disconnect commands to its original remove-client helper, with an
old-version reply only for `0x446400`. On a subscription request port,
`0x442d00` instead routes through `DeviceClass::unsubscribeClient` to
`removeSubscriber`; no subscriber `0x446400` support is inferred. A distinct
server focus-transfer branch must not be mislabeled as disconnect cleanup.

## Frontend bootstrap

`fixServiceStartup`, `0x10068ff90`, first passes this exact original command to
`system` and proceeds only if its return value is zero:

```text
open '/Library/Application Support/Native Instruments/Hardware/NIHardwareAgent.app'
```

**That command was recovered as data and was never executed.** This is an
explicit application path in the frontend's code, not merely the previously
inspected installer's declaration. A zero `system` result is not proof that a
replacement agent is ready or accepted.

It creates a UTF-8 CFString for the same path and a POSIX CFURL, releasing the
temporary CFString. Missing string/URL returns false. With a URL, it calls the
original `_LIAEFind` helper, then `_LIAEAddURLAtEnd(URL,1)` only if Find returns
false. It releases the URL and returns true; it ignores AddURL's status.

The selected `_LIAEFind` and `_LIAEAddURLAtEnd` bodies use copied filesystem
paths/comparison and URL-to-FSRef conversion, but delegate list retrieval and
insertion to unread helpers. Their implementation details, login-item effects,
OS trust checks and current-host success are not claimed here. No explicit
vendor-signature check was established by these selected source bodies; this
is not proof that every possible frontend or OS check is absent.

## Evidence and remaining gate

Only exact original `LC_FUNCTION_STARTS` envelopes were analyzed. These caps
are analysis limits, not guaranteed complete logical function ends. A single
no-analysis import of this pinned frontend created the preserved metadata
project; every later packet used a fresh nonhidden private copy, read-only
processing, automatic and incremental analysis explicitly disabled, immutable
baseline metadata and exact target deltas/full instruction-span checks.
No C/decompiler, guessed types/prototypes, whole-program analysis or automatic
callee following ran. Raw proprietary listings remain private/ignored.

Private evidence root: `.build/ni/recognition-contract/frontend/`.

| Packet | New functions / instructions | Evidence SHA256 |
| --- | --- | --- |
| Import | One original image, no instruction analysis | `8df833015957f9d5c26e01d48e2e133768e69da6069b50b75a197e2f63bc5cf2` |
| Batch A | 4 / 481 | `c809160de646e5056dd54bd1a3e7e70ada5f46bb89a6dc3c232d3dfedbc46160` |
| Batch B | 7 / 180 | `a22e51ab005a6a27d2684275a849531021cbdb804770adf933703bf707438384` |
| B literal data | Two NUL spans and one exact four-byte span | `07149a0e9cdb9464e7889b6d485e004ee391283a837c6d846ba00b08157a88c6` |
| Batch C | 1 / 60 | `1fa3d6fe16b4bef3542938243929e3eab17c736a53546cb390c7699d3c1976b5` |
| Batch D, preliminary | 6 / 817, before exact status-table exclusion | `0ca0c96e8aac38f487f6beb9cfb2a1b7327e89263d10cc29181fa6d1639ec8b0` |
| D literal data | 16-byte endpoint and initial one-byte fallback test | `994599ee38114803c77467aa15be53ac1b244602923ad00326d40e000ed15f84` |
| D fallback data | Exact `<null>` NUL span | `2dbd21736625f7670afe3d45e590b49ec89bfc8ebde2ea7f52a1611df6b22535` |
| D supplement | Same 6 functions / 835 instructions, data table excluded | `76d30bf65a83d508dfb08b0128a34f3d7b26f72230221d2918bae9fe8e7cf893` |
| D terminal data | Timeout, status table and loader-associated default mode | `247589f0c6aa4e5dd969103479ec68aaf2ae0780937b9401e2c662058b7cc05f` |
| Batch E | 2 / 71 | `d91e316f6d757374ce8be718e88b5dc2271f615c059d391b600a04909802561d` |
| Batch F | 4 / 485 | `af39bdbd2a70c83dd4d5e72b1001b3b43d829e5ef27d59c927c1069f61b1a95b` |
| Batch G | 2 / 96 | `40f0c7815587fd08ede63604174343fdcba64d45c6075b14b7c2dad3d5e8bf38` |
| G compiled table data | Two exact 16-byte headers and six exact eight-byte slots | `e3623123348f355fb843316f7207ceaf8c784240220855d1ddae1e4a49f452ce` |
| Batch H | 1 / 19 | `641d45d541114cc5d1b5c1d8c89196dd5899de1ce064359bee5ad95cf0441235` |

The D supplement adds **19 status-arm instructions** and removes **one** earlier
table-as-instruction record: all 816 retained preliminary-D instruction records
are identical, and the other five D listings are unchanged. The net difference
is 18 instructions, not 18 newly recovered instructions. The initial one-byte
fallback test disproved an empty-string assumption; a separately admitted
NUL-span read established `<null>` before the report used that value.

Each processing baseline had **977 functions and zero instructions**. The
source image and preserved import stayed unchanged. Independent packet review
passed before the cap-local conclusions above.

The selected source contract now covers the RK3 connection request, version
gate, returned port names, activation confirmation, subscription query and its
instance consumer, status/focus consumers, explicit disconnect sends and
frontend bootstrap path. Each has source or compiled-table evidence, paired
with the original agent where the corresponding producer or consumer was
recovered. The derived table is compiled-data evidence; no unchanged NI
application was run to prove its live object, discovery or acceptance.

This recovery does not establish a global absence of direct hardware access or
dynamic identity checks, port the opaque original GP/login-item/runtime
helpers, recover every GUI callback or implement a deployable native agent.
Those are limits of the bounded source proof, not permission to redesign the
original protocol. `.17` still owns the necessary driver-access binding and
implementation decision; further recovery or coding must be planned there
first. Full original RK3 behavior and SIP stay fixed.
