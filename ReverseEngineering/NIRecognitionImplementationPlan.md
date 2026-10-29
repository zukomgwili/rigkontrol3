# Minimum faithful ARM plan for NI application recognition

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.17**.

**Conditional candidate, if driver-access adaptation is necessary: reconstruct
NI's supporting hardware agent for ARM, retaining its controller domain and
frontend protocol, with one isolated adapter at its
original `FX2Device::OSImpl` driver-access boundary.** Keep Controller Editor
and Guitar Rig unchanged. This identifies the component and seam to port; it
**does not admit a production driver binding yet**. Source-contract gates
`.21` and `.22` must establish the real recognition path before backend code.

The first meaningful result is an unchanged NI frontend recognizing the
**attached, real RK3** through its original session, controls and LED path.
A version-response server, synthetic device list or manually mapped MIDI port
is insufficient. This planning pass makes no source, build, application,
endpoint, driver, hardware, signing, profile or security change. SIP stays
enabled. Original NI audio ring, clocks, queue calendar and recovery remain
fixed requirements, outside this recognition plan's change scope.

## Evidence that constrains the plan

| Established fact | Consequence |
| --- | --- |
| Controller Editor 1.7.4/R199 and its exact paired NIHardwareAgent were identified from the supplied installer. The frontend is x86_64; original source/headers/build project for the agent were not found. | The initial named legacy recognition target is this pair. A native agent requires source reconstruction, not recompiling the Intel binary. No unchanged-frontend launch or recognition has passed. |
| The original agent's RK3 filter uses `de_caiaq_driver_NIUSBAudioDevice`, first-match/termination notifications and type-0 user-client open. | Preserve notification, device lifetime and open behavior. The matching criterion is the potential platform adaptation, not permission to rewrite the controller domain. |
| The qualified DriverKit proxy published one marked service, but its exact legacy class query found zero entries while name matching found the same service. | The tested user-class/name mechanism cannot be claimed to satisfy NI's fixed class lookup. The original agent notification/Rosetta chain was not run; the result is not universal DriverKit incompatibility. |
| Original agent access includes selector 3 with 110-byte device information, selector 18 with 14-byte specification, analog/digital subscriptions 7/8 with fallbacks and MIDI structs via 4/12 (72 bytes). | Any selected adapter must preserve required original bytes, error results, callbacks and lifetime. A native CoreMIDI endpoint or current HAL property is not evidence of that compatibility. |
| `.19` recovered CFMessagePort bytes, IDs, names, timeouts, ownership and bounded port lifecycle. `.20` passed strict ARM and ASan/UBSan for 49 transport scenarios and 18 lifecycle cases, with nine compiled semantic mutations rejected. | Retain that contract. The offline result proves typed byte handling and synthetic operation order, not a complete agent, real GP execution, frontend handshake or live recognition. |

Sources: [paired-agent feasibility](NIHardwareAgentFeasibility.md),
[class-matching result](DriverKitClassMatching.md),
[deployment and driver seam](KernelBindingDecision.md),
[original IPC contract](NIIPCTransport.md),
[offline ARM proof](../Tools/NIIPCContract/README.md).

Controller Editor 1.7.4/R199's binary SHA256 is
`ef1cee113d431ccd6946ee92631c55a6dedeb2b9a5d8ea12b150f91acc2cfad9`;
the paired agent is
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
The unchanged Intel frontend and native ARM agent remain separate processes,
communicating through the recovered IPC. Do not load Intel C++ code into the
ARM agent or rebuild/re-sign the frontend to avoid its access/identity gate.
Apple's general Rosetta support on this macOS 27 host does not establish that
this exact 2014 frontend and its dependencies launch successfully; that remains
a bounded later acceptance check. No compatibility promise for future macOS
versions follows from this plan.
[Apple Rosetta support](https://support.apple.com/en-us/102527),
[Apple translation environment](https://developer.apple.com/documentation/Apple-Silicon/about-the-rosetta-translation-environment).

Guitar Rig 6 MIDI operation is a separate acceptance target: NI's administrator
states RK3 is not supported by its native integration. That is not permission
to drop legacy recognition or invent native GR6 support as an original feature.
[NI support statement](https://community.native-instruments.com/discussion/12972/rig-kontrol-3-midi-issues).

## Retained behavior and minimum seams

| Seam | Retain from NI | Permitted change after its gate |
| --- | --- | --- |
| Frontend / agent | `NIHWMainHandler`, per-client port/session identities, actual version/role acceptance, messages, subscription/focus/ownership and disconnect rules. | Native implementation and verified launch packaging only. No frontend patch, re-signing, injection, new UI, fabricated device or version-acceptance bypass. |
| NHL / CF transport | Internal 32-bit count/reply fields separate from transmitted bytes; full first-word message ID; original 2-second timeouts; signed limits, copy/release and empty-CFData behavior; name-byte widening and lifetime asymmetries. | Typed ARM objects/calls and defined fixed-width, unaligned access. The proof files are an oracle/reference, not a production-ready agent or runtime. |
| Controller / mapping | Original RK3 device construction, reports, calibration, assignments, feedback, persistent settings and client behavior. | Reconstruct source and isolate OS dependencies. Keep original domain decisions; do not substitute the old bridge's behavior or a new default profile. |
| `FX2Device::OSImpl` | Real device discovery, information/specification, sync/async operations, fallbacks, error and termination lifetime. | Exactly one selected platform adapter whose required contracts have passed. No competing USB owner or silent transport substitution. |

Original raw controls, mapping and settings are documented in
[ControllerMapping.md](ControllerMapping.md); DIN transport remains separate in
[MIDIDriver.md](MIDIDriver.md). Full original parity stays in `.2` and its
related cards. The recognition slice does not waive toe/external/DIN, power,
settings, display, audio-rate or stability requirements.

## Source gates before implementation

These are bounded next reads, not claims that five functions recover the whole
application protocol. All original body work needs the recorded Beads scope,
original metadata caps, immutable baseline and ASM-only guards used in `.19`.
An unresolved callee stops that batch; record the exact next question before
following it. Do not repeat the completed 26-function transport audit.

| Beads gate | First bounded question and exact boundary | Completion condition |
| --- | --- | --- |
| `.21`: original registration and request contract | `IPCReceivePort::create`, `0x1002ec500`, and `receiveEvents`, `0x1002ec520`, connect the recovered internal allocation/callback to actual registration. Then inspect the already identified direct request targets: `DeviceClass::connectClient(MsgDeviceConnect&)`, `0x10000d770`; `connectClient(MsgDeviceConnectWithIdentifier&)`, `0x10000d800`; `subscribeClient(MsgDeviceSubscribe&)`, `0x10000da50`. | Exact registered span/capacity, request/reply fields, identifiers, role and port rules, version source and error/ownership transitions for the selected session. Function names alone supply no wire codes. Further session/focus/shutdown bodies are admitted only when this chain proves them indispensable. |
| `.22`: unchanged frontend and startup/access boundary | Begin with metadata-only inventory of the exact paired Controller Editor: imports/symbols, bundle/helper launch declarations and access/identity references. Then record exact bounded caller bodies separately if necessary. An IOKit dylib dependency alone proves neither direct driver access nor an agent-only path. | Identify the frontend's actual discovery/startup route, direct-driver consumers if any, launch/path/identity/signature expectations and the required unchanged binary/version. Establish whether an ARM agent adapter alone can preserve its required boundary. |
| `.17`: select one driver-access binding | Compare **only the operations required by those recovered consumers** against the existing owner and supported DriverKit client contract. | One source-backed adapter decision, exact minimum substitutions and distinguishing tests; no backend code while either route's prerequisites remain unknown. |

The first metadata-only `.22` pass is now complete. In the exact frontend
binary it found six CFMessagePort imports and no direct undefined
`IOServiceOpen`, `IOConnect*`, `IOServiceMatching`, matching-notification or
`SecCode`/`SecStaticCode` imports. It also found `IOServiceGetMatchingServices`,
`IOBSDNameMatching` and registry imports. These observations **do not prove an
agent-only hardware path**, rule out indirect/dynamic access, or establish
launch/signature behavior. The exact metadata-confirmed next caller boundaries
are below; their bodies remain unread and require the separately recorded
`.22` four-body plan. Symbol names/signatures do not prove return types or a
runtime call relation between these methods.

| Exact frontend symbol | Original address | Missing contract |
| --- | --- | --- |
| `NI::NHL2::RigControl3::connectToDevice` | `0x10067d150` | RK3 client role and device connection request. |
| `NI::NHL2::Controller::establishConnection` | `0x100676970` | Actual session establishment and required backend access. |
| `NI::NHL2::Controller::fixServiceStartup` | `0x10068ff90` | Frontend helper discovery/startup and identity expectations. |
| `NI::NHL2::Controller::getServiceVersion` | `0x100677940` | Original version-query/acceptance boundary. |

These are concrete source questions, not permission to analyze the full
frontend or infer agent-only access from imports.

Private packet:
`.build/ni/recognition-plan/frontend-metadata/20261025T193207Z/evidence.json`,
SHA256 `35ae0027d8fe14478e6d9ab26bb38a5924cf0dca718d636d973da8d98b4456f3`.
Its six data-only metadata commands returned zero; original files remained
unchanged. `.22` remains open for the actual access/startup contract.

The already-proved factory/filter/type-0/open/selector evidence is reused,
not re-extracted. A module-version reply must come from its recovered original
source/computation; emitting a guessed value to pass a frontend check is not
recognition. The full per-client handshake, GP deferred cleanup and exact
virtual targets remain unproved until their owning source gates close.

## Driver-access decision without a new architecture detour

For this conditional component candidate, the binding below is a **decision
gate**, not two parallel implementation projects. If `.21`/`.22` establish
that the unchanged original agent already satisfies the required recognition
path, retain it rather than reconstructing an unnecessary component. The
modern proxy and offline proof alone do not make an agent port mandatory.

| Candidate binding | What must be demonstrated before selection | Stop condition |
| --- | --- | --- |
| One agent-side adapter to the existing native USB owner | The unchanged frontend reaches required hardware functions through the agent, and that owner can supply **every required original operation** with original data/error/event/lifetime semantics. Preserve a single owner for combined audio/control. | Current `ninf/nrpt/ncmd` CFData properties are a new internal protocol, not NI's original IOKit ABI. If a required direct frontend lookup/selector or operation cannot be preserved, do not choose this route merely because it exists. |
| A selector-compatible DriverKit service with an adapted agent lookup | A documented matching route, type-0 user client, all required sync/async/memory/lifetime contracts, actual capability profiles and a **single** USB/audio ownership plan. | No supported old-kernel-class alias is established. Do not add a second USB owner beside the HAL, infer AudioDriverKit is mandatory from the IPC proof, or begin a full audio migration to make a naming test pass. Missing USB/client/audio contracts stop selection. |

The existing owner interface is **known to be insufficient unchanged** for
the original user-client contract. `NativeControl.h` defines `ninf` as a
240-byte `RK3NativeInfo`, `nrpt` as cursor-based raw report history, and `ncmd`
as the declared command-05 LED / command-07 MIDI path. `NativeClient.h` wraps
those records in CoreAudio CFData properties. They are not the original
selector-3 110-byte information, selector-18 14-byte specification or original
subscription ABI. A coincidentally 72-byte native command record is not proof
of the original 72-byte MIDI selector layout. Do not invent missing metadata
from the native UID/product string or copy a prefix and call it NI information.
Selecting this owner therefore needs source-derived operation/field mappings
and any strictly necessary extension, with original errors/lifetime tests.
[Current native records](../Driver/NativeControl.h),
[current CoreAudio property client](../Driver/NativeClient.h).

The current owner opens RK3 interface 0/alternate 1 for commands and audio;
shared operation is not two independent claims of the same device.
[Existing native owner and sharing](NativeUSB.md),
[current binding capability distinctions](SelectedBindingContracts.md).
A new internal adapter would need an explicit necessity argument tied to the
unavailable kernel deployment and recovered consumer, plus parity tests; this
plan does not bless the current CFData protocol by resemblance alone.

Apple's DriverKit client example supports name matching with matching
notifications, `IOServiceOpen`, and scalar/struct/asynchronous user-client
methods. It makes a necessary lookup adaptation technically plausible; it
does not establish NI's exact ABI or a deployable RK3 backend.
[Apple client/DriverKit example](https://developer.apple.com/documentation/driverkit/communicating-between-a-driverkit-extension-and-a-client-app).

## Signed deployment and identity gate

Keep SIP enabled. The retained IOAudio kernel model remains the smaller
structural port, but the observed certificate/MDM deployment prerequisites are
unavailable; the user rejected the SIP-disabled personal-testing route.
Neither fact says ARM lacks IOAudioEngine. This plan does not reopen that
security choice or request account changes.
[Recorded kernel eligibility](KernelBindingDecision.md),
[Apple deprecated-kext policy](https://developer.apple.com/support/kernel-extensions).

The successful software probe established base DriverKit development signing
and normal approval, not USB transport, user-client or AudioDriverKit access.
A selected DriverKit route needs profiles authorizing its **actual** required
capabilities and this test Mac before backend/hardware execution. Apple's USB
transport entitlement specifies device-descriptor dictionaries; the client
access entitlement identifies the extension the owned ARM agent may open.
A broad allow-any-client entitlement is not needed merely because an owned
agent connects, and cannot repair class matching.
[USB transport entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.driverkit.transport.usb),
[client access entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.driverkit.userclient-access),
[broad client-access entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.driverkit.allow-any-userclient-access).

The paired bundle IDs are exactly `com.native-instruments.Controller Editor`
(with the space) and `com.native-instruments.NIHardwareService`. Data-only
installer text declares a hidden login item and an `open -a` of
`/Library/Application Support/Native Instruments/Hardware/NIHardwareAgent.app`.
This is positive original packaging evidence, not proof of what the frontend's
`fixServiceStartup` accepts or requires. No installer, helper or application
was run. Do not replay installer scripts as a substitute for the source gate.

Do not reuse NI's signing identity or assume its helper bundle/path can be
changed freely. Resolve the actual frontend launch and caller checks in `.22`.
If an unchanged frontend itself must open a protected service, agent signing
cannot grant that application a new entitlement; stop for that exact contract.
Normal macOS approval and an owned reversible deployment remain requirements
of a later, separately admitted runtime card. No current activation or reboot
is requested by this offline plan.

## Smallest implementation and acceptance slice

After `.21`/`.22` and the one-binding decision, record a dedicated code card
before writing production source. Implement only the recovered path from
frontend discovery/session to **real RK3 enumeration and original events**:

1. If the native agent adaptation is required by those gates, build its actual
   NHL/CF runtime and source-proven session path.
   Retain original buffers, version/role rules, ports, errors and teardown;
   resolve the specific GP/runtime dependencies instead of transplanting
   opaque test commands or inventing thread/deferred-cleanup behavior.
2. Implement the selected `FX2Device::OSImpl` adapter using real device
   information/specification, original subscriptions and output operations.
   Retain NI's mapping/controller core and one combined USB owner. No synthetic
   RK3, constant fake serial/capabilities or success-only backend is accepted.
3. Verify the complete selected session with literal original-derived golden
   requests/replies and driver outcomes. Keep `.20`'s strict/sanitizer and
   semantic negative controls as regression evidence; add tests at newly
   recovered session/device lifetime seams, not another generic IPC redesign.
4. On a separately admitted signed test deployment, the **unchanged paired
   Controller Editor** must discover the attached RK3 without manual MIDI
   setup, display its real identity, acquire/release its original session and
   update actual switches/pedal/LEDs through that path. Verify all eight
   switches and built-in expression plus an LED feedback round trip; detach
   must remove the real device, reconnect must restore the original session
   without invented input or duplicate ownership. Existing combined-audio
   requirements remain regressions, not a new audio backend in this card.

**RED** is a changed/missing original field or lifetime, fake/absent device,
manual-only recognition, unsupported caller access, conflicting USB owner,
missing signed capability or a need to alter NI timing/recovery/SIP to proceed.
**GREEN** is original-derived offline conformance plus actual unchanged-client
recognition and physical control/feedback through the one selected binding.
A successful handshake alone closes neither this acceptance nor `.2` parity.

Before runtime, stage owned artifacts separately, preserve existing drivers,
preferences and exact endpoint/process baselines, and record rollback commands.
After a failed or completed bounded test, stop only owned processes, release
only owned ports/device claims, restore the preserved deployment and verify
ordinary audio/MIDI availability. Never patch/re-sign NI applications, kill
unrelated services or change security to pass acceptance. The exact installer,
authentication and teardown operations belong to that runtime card.

This plan completes the bounded planning deliverable. **`.17` remains in
progress with production binding unadmitted** until the source and capability
contracts select one adapter. There is no new production coding or runtime
authorization hidden in the offline IPC proof or this plan.
