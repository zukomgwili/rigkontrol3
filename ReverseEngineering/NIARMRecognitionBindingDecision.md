# ARM binding decision for original NI recognition

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.17**.

**Select the existing single NativeUSB/HAL owner, with one narrow functional
adapter at the supporting ARM agent's original `FX2Device::OSImpl` boundary.**
Keep Controller Editor and Guitar Rig unchanged. Retain NI's recovered agent
protocol and required controller domain; adapt its platform access rather than
adding a selector-compatible DriverKit provider.

This is a firm **architecture decision**, not implementation or deployment
approval. The recognition slice is not buildable or accepted yet. Its real
device information, specification, event/error/lifetime mapping and control
policy ownership must be established first. This decision closes neither full
NI parity nor `.11`'s unresolved callback/deadline stability work. SIP, NI's
audio ring, frame/calendar, clock, timing and recovery remain fixed.
[Existing owner](NativeUSB.md), [prior decision gate](NIRecognitionImplementationPlan.md),
[remaining binding contracts](SelectedBindingContracts.md).

## Why this is the minimum available change

The selected unchanged Controller Editor **1.7.4/R199** reaches the inspected
RK3 session through CFMessagePort `NIHWMainHandler`, ordinary connect
`0x02444300`, returned Request/Notification names and activation confirmation
`0x02404300`. Subscription, asynchronous instance/status consumers and explicit
disconnect also have paired source contracts. Its bootstrap explicitly opens
`/Library/Application Support/Native Instruments/Hardware/NIHardwareAgent.app`.
This establishes a concrete agent boundary for this selected frontend path;
it does not prove every NI application or every access path is agent-only.
[Frontend contract](NIFrontendRecognitionContract.md),
[agent contract](NIAgentRecognitionContract.md).

The original supporting agent's factory uses the fixed kernel-class query
`de_caiaq_driver_NIUSBAudioDevice`, first-match/termination notifications and a
type-0 private user client. The chosen HAL owner supplies no such service.
The qualified DriverKit probe published its service successfully but found
zero exact legacy-class matches; name matching found that same marked service.
The original agent's Rosetta/matching-notification chain was not executed, so
this is not an observed live failure of that binary. However, neither the
reviewed factory nor the tested name/user-class mechanism supplies a supported
route from the unchanged helper to this selected owner. Retaining the Intel
helper unchanged is therefore **not** the selected solution. Supporting-agent
adaptation is necessary for this architecture; the ARM instruction set alone
is not the reason for replacing its driver-access boundary.
[Original factory/driver seam](NIHardwareAgentFeasibility.md),
[class-match evidence and limits](DriverKitClassMatching.md).

Keeping NI's kernel audio/private-user-client model would be a smaller
structural port, but the recorded signing/deprecated-KPI deployment prerequisites
are unavailable and the user requires SIP enabled. That observed constraint,
the existing combined owner and the actual frontend/agent boundary justify the
chosen substitution. They do not establish that ARM cannot use IOAudioEngine
or that HAL is universally mandatory. No security or account change is
requested. [Recorded deployment decision](KernelBindingDecision.md).

## Alternatives rejected for this decision

| Alternative | Why it is not the minimum selected route |
| --- | --- |
| Selector-compatible DriverKit plus adapted agent lookup | It still needs the agent adaptation, then adds a new provider, required user-client/USB capabilities and deployment, every applicable selector/event/lifetime contract, and a single combined USB/audio ownership solution. The software-only probe established none of those hardware contracts. No demonstrated recognition benefit offsets those additional changes here. This is not a finding that DriverKit can never implement the operations. |
| Unchanged Intel helper plus current HAL | The required legacy service/selector boundary is absent. Native audio/MIDI operation is not evidence that the helper can discover or open it. |
| Retained-kernel deployment under current conditions | Recorded signing/loading prerequisites are unavailable with the user's security constraint. No SIP-disabled path is reopened. |

A kernel-class alias shim, frontend patch/re-signing, injection, fabricated
version/device list or generic MIDI bridge is not part of this selection.
The unchanged frontend protocol is the external contract; new `IOConnect`
byte emulation is unnecessary at an agent boundary we are already adapting.
[Class-match limits](DriverKitClassMatching.md),
[recovered transport](NIIPCTransport.md).

## Necessary platform seams, with behavior retained

| Selected seam | Minimum substitution and invariant |
| --- | --- |
| Native agent ABI | Reconstruct typed ARM calls and owned C++/GP/runtime state. Do not copy x86 instructions, transplant C++/Boost layouts onto the wire or load Intel code into the ARM process. Preserve original fixed-width fields and explicit copy/lifetime rules. |
| Factory / `FX2Device::OSImpl` | Replace the unavailable legacy class/open/private-driver access with discovery and generation-qualified operations on the **existing** owner. Preserve original device eligibility, first-match/termination effects, activation, errors and callback lifetime; no second USB claim. |
| Owner control boundary | Use narrow, versioned functional CFData properties through the existing CoreAudio client. Map only source-proved information, configuration, events and output operations; extend it only where the next contract proves necessity. The public transport is internal to the ARM components, not NI's selector ABI. |
| Frontend / agent | Keep original CF framing, full command words, counts, errors, version computation, role rules, names, confirmation, notifications and asymmetric teardown. Preserve observed bootstrap path through a later verified native package; do not assume NI signing identity or all OS/frontend checks are absent. |

The original service version is derived from the module bundle version,
including its original fallback parser, then shifted/cached. A constant chosen
to exceed the frontend minimum is forbidden. Original quirks remain contracts:
for example reply counts, untouched constructor bytes and failure-retained
state are not normalized during the port. Generic GP/thread/deferred-cleanup
and original domain continuations remain explicit prerequisites, not no-ops.
[Agent lifecycle/version](NIAgentRecognitionContract.md),
[frontend startup and limits](NIFrontendRecognitionContract.md),
[transport/ARM boundary](NIIPCTransport.md).

## The present owner is a transport starting point, not NI parity

| Current source fact | Required disposition before an agent implementation |
| --- | --- |
| `ninf` exposes a **240-byte** `RK3NativeInfo`, native UID/product strings, topology, status and generation. | Recover/map the original selector-3 **110-byte** information and the real NI identity. The native UID is an owner locator, not a physical NI identifier, serial or fabricated query result. |
| Owner `rk3_device_info.raw` stores **14 bytes from USB command01**. Original selector18 also returns **14 bytes**. | Establish their actual field/producer/consumer relation. Equal length is not equivalence; never copy command01 bytes and call them the original specification without proof. |
| `nrpt` is non-destructive cursor history: up to32 records per batch, 256 retained, explicit loss and worker-receipt host timestamps. | Map original analog/digital callback layouts, timestamp origin, subscription/fallback order, baseline, overrun, errors and termination. History polling/receipt time is not automatically the original mapped callback contract; no new timer or polling cadence is selected here. |
| `ncmd` accepts generation-leased fixed LED05 and DIN07 writes. Its **72-byte** record is not the original **72-byte** MIDI selector struct. | Preserve required output schemas, completion/error semantics and stale-generation rejection. Arbitrary USB/configuration writes remain unavailable; add only individually proved operations, not an escape hatch. |

Sources: [native records](../Driver/NativeControl.h),
[CFData client](../Driver/NativeClient.h),
[actual owner info/history/command implementation](../Driver/NativeUSB.c),
[USB command01 decoder](../Driver/DeviceProtocol.h),
[HAL property validation](../Driver/Audio/RK3AudioDriver.c),
[required original operations](SelectedBindingContracts.md).

There must also be **one mapped-control policy owner**, not merely one USB
owner. The original supporting agent owns assignments, calibration, focus and
LED/display policy; DIN transport is a separate role. Current native MIDI
includes its own mapped-controller/feedback policy, and NativeUSB currently
merges unconditional controller activation into audio readiness. Those are not
authority to activate twice, let two writers drive LEDs, duplicate mapped MIDI,
or replace the original focus/configuration/baseline sequence. Partition DIN
transport, the mapped stream and policy authority explicitly in the next
source card. Keep audio-only initialization distinct from controller activation;
any necessary owner change needs its own source-backed implementation scope.
[Original mapping/activation and documented new-host differences](ControllerMapping.md),
[current MIDI policy](../Driver/MIDI/RK3MIDIDriver.c),
[current startup distinction](NativeUSB.md).

## One prerequisite source card before code

The planned prerequisite is **rigkontrol3-40h.23**, depending on `.17`:
**map real RK3 information, specification and identity at the FX2
OSImpl-to-owner seam, then map required event/error/termination order**. Reuse
the existing reviewed packets first. The card is source planning only; its
existence admits no extraction or production code.
Known initial boundaries are `FX2Device::OSImpl::connect` `0x1002ef650`,
`getDeviceInfoForOSHandle` `0x1002f25a0`, `getCaiaqUSBDeviceInfo`
`0x1002efd50` and `getCaiaqUSBDeviceSpec` `0x1002f0310`. Their existing evidence
establishes type0 open and selector3/18 output sizes, not all fields.
[Existing address/contract inventory](NIHardwareAgentFeasibility.md).

Its first deliverable is a field-complete original-to-owner matrix for the
110-byte information, 14-byte specification and actual instance string
producer consumed by the recovered query/session. Every field needs its real
source, representation, error and lifetime; no guessed identity or constants.
Then admit only the bounded event/order follow-up needed at
`subscribeToEvents` `0x1002f0d20`, `asyncConnect` `0x1002f1020` and confirmed
output call sites such as `sendMidiEvents` `0x1002f23a0`. Capture timestamp,
registration/fallback, configuration, baseline, termination and policy authority
before defining new properties or a scheduler. These addresses are reused
metadata references, **not** a new extraction plan admitted by this document.
Any exact missing body/data read must be scoped in Beads before action.

Completion means the required recognition-slice operations have explicit
representable mappings and distinguishing tests, with precise owner extensions
and remaining domain/runtime source dependencies recorded. A missing required
field or lifetime contract blocks production code; it does not trigger a full
agent rewrite, another architecture experiment or a success-only backend.
Full settings, display, toe/external pedals, DIN, rates and stability remain
in their existing parity cards. No new extraction, implementation, build,
endpoint, application, hardware or security action occurred in this decision.

## Subsequent tests, stops and rollback

A separately recorded implementation card can follow `.23`'s source gate. Its
minimum offline checks must expose wrong real-info/spec fields, missing or
wrong-generation identity, event ordering/timestamp differences, overrun,
original errors and termination with pending callbacks. Exercise the actual
functional adapter, not just structurally similar test records. Retain the
completed transport proof and add only tests for newly mapped seams; unchanged
CF bytes, dynamic version, full handshake and shutdown asymmetry remain
regressions. Policy tests must reject duplicate activation, mapped output or
LED writers while preserving distinct DIN behavior.
[Existing transport proof and limits](../Tools/NIIPCContract/README.md).

Later signed acceptance must use the **unchanged paired Controller Editor**:
recognize the attached real RK3 without manual MIDI setup, report real identity,
complete subscription/connect/confirmation, deliver controls and LED feedback,
then remove/reconnect without stale events or duplicate ownership. Audio and
controls must coexist while NI's audio timing/recovery remain unchanged.
Guitar Rig and other named NI targets require their own unchanged-application
acceptance; this CE slice is not a blanket compatibility result. `.11`'s
unresolved deadlines and existing full-audio/parity gates remain open.
[Acceptance boundary](NIRecognitionImplementationPlan.md).

Stop at an unrepresentable original field/event/error/lifetime contract,
conflicting policy owners, required direct frontend driver access, unavailable
packaging/identity requirement, need for a second USB owner, or an audio timing
change. Record that exact blocker before any new implementation or architecture
decision; do not invent resets, polling, retries or timeout changes.

A later deployment must preserve verifiable copies of the prior owned agent
and driver state, prevent simultaneous original-name agents, and provide
removal/restoration of only the new agent/owner extension. Restoring an old
Intel helper alone is not a working recognition claim. Retain the existing
audio/control rollback and security configuration. This decision installs,
activates, removes and restarts nothing.
