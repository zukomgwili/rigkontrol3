# NIHardwareAgent ARM feasibility while preserving NI frontends

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.18**.

**Decision: a conditional adaptation path exists; implementation feasibility
and unchanged NI frontend acceptance are not yet established.** The selected
original bodies expose the agent's driver-access seam, a named receive-port
construction, fixed-width message-wrapper fields and a version-query branch.
They do not yet establish the complete CFMessagePort envelope, connection
lifecycle or a buildable native agent. No implementation, application launch,
hardware operation or security change was made by this research.

The requirement remains NI's original frontend protocol and RK3 functionality,
including automatic recognition. A native adaptation inside the agent may
change the necessary driver-discovery boundary; it may not replace NI's
protocol with a generic MIDI bridge or assume that agent IPC is every
frontend's only driver-access path. Original audio ring, timing and recovery
behavior are outside this research's change scope. SIP stays enabled.

## Evidence and compatibility scope

The executable is NIHardwareAgent **1.7.4/R199**, SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
The examined material consists of the pinned binary and data files, existing
149+39 controller exports, seven selected discovery/filter exports, saved
symbol/dependency metadata, and the four narrowly admitted bodies below.
Original NI source, headers and an Xcode project are absent from this examined
reference inventory. Reconstructed C is evidence, not original build source.
[Existing controller reconstruction](ControllerMapping.md),
[existing discovery/filter evidence](KernelBindingDecision.md).

The user has included hardware-agent feasibility. Therefore the old discovery
call is a potential ported-component adaptation, while NI frontend behavior
and protocol remain requirements. The previous proxy exercised modern ARM
`IOServiceGetMatchingServices`; the original factory registers
`IOServiceAddMatchingNotification` callbacks. The actual original NI agent
notification/caller chain was not run. The modern proxy's class miss does not
alone prove that the unchanged NI agent fails or that an agent port is
necessarily required. It also does not make the name-only DriverKit mechanism
satisfy class matching. [Class-matching scope and evidence](DriverKitClassMatching.md).

The matched frontend is Controller Editor **1.7.4/R199**, bundle ID
`com.native-instruments.Controller Editor`, thin x86_64, minimum macOS 10.8 /
SDK 10.9, binary SHA256
`ef1cee113d431ccd6946ee92631c55a6dedeb2b9a5d8ea12b150f91acc2cfad9`.
Its packaged agent matches the exact pinned agent hash. A separately admitted
data-only installer extraction found both bundles although the overall
`pkgutil --expand-full` command returned exit 1; this is not a full-installer
extraction pass. No installer script or app ran, and that strand's own mount
was detached normally. Artifact identity and architecture are established;
frontend IPC, current-host execution and recognition are not. Guitar Rig 6's
legacy integration or direct-driver access must not be inferred from this
2014 pair's symbols.

## Established original agent-to-driver seam

These are facts from previously selected bodies, not new exports for this
research. They constrain an adapted backend without proving that a new
DriverKit user client already satisfies them.

| Original function/address | Boundary established by existing evidence |
| --- | --- |
| `USBDeviceFactory::OSImpl::initWithDeviceFilter`, `0x1002f5ab0` | Class-filter dictionary for first-match/termination notifications; RK3 initialization supplies the NI class and VID `17cc` / PID `1940`. |
| `FX2Device::OSImpl::connect`, `0x1002ef650`; `getDeviceInfoForOSHandle`, `0x1002f25a0` | User-client open type 0. |
| `getCaiaqUSBDeviceInfo`, `0x1002efd50` | Synchronous selector 3, 110-byte output buffer. |
| `getCaiaqUSBDeviceSpec`, `0x1002f0310` | Synchronous selector 18, 14-byte output buffer. |
| `subscribeToEvents`, `0x1002f0d20`; `asyncConnect`, `0x1002f1020` | Timestamped digital/analog subscriptions 8/7 with older-selector fallbacks; Mach-port/run-loop callback dispatch and reporting enable. |
| `FX2Device::sendMidiEvents`, `0x1002f23a0` | Struct selector 4 or 12 with a 72-byte input buffer, according to port type. |

Configuration, LEDs, calibration, mapping and ownership behavior remain in
[ControllerMapping.md](ControllerMapping.md). DIN MIDI and controller-message
conversion remain distinct in [MIDIDriver.md](MIDIDriver.md).

## What the four-body proof establishes

The four entries were selected from the saved original symbol index before
new extraction. Each original `LC_FUNCTION_STARTS` entry and the following
start bounded an analysis cap within `__TEXT.__text`; those caps are limits,
not proof of logical function ends or a complete control-flow graph.
The table reports only selected-cap assembly-backed facts. Offset notation
refers to NI's in-process message object, not a claimed complete wire schema.

| Selected original body | Fact and its exact limit |
| --- | --- |
| `ConnectionPort::ConnectionPort`, `0x1000203a0` | Reconstruction identifies `NIHWMainHandler`; assembly at `0x1000203ac–0x1000203ca` passes its address and a 64-bit length 15 to `IPCReceivePort` construction at `0x1002ec340`. The callee's actual registration, naming transformations and lifecycle are unexamined. |
| `IPCSendPort::sendMessage(Message&)`, `0x1002ec270` | Assembly `0x1002ec277–0x1002ec28f` prepares an outgoing span at message+12 with a 32-bit count from +0. It prepares an optional reply pointer as message+the unsigned 32-bit offset at +4, controlled by the unsigned 32-bit count at +8. It tail-jumps to `NI::IPC::SendPort::sendMessage` at `0x1002eb940`; transmission, CFData packing and CFMessagePort `msgid` are not established by this wrapper. |
| `IPCReceivePort::callback`, `0x1002ec650` | Assembly `0x1002ec663–0x1002ec6b6` stores the incoming scalar's low 32 bits at message+0, clears the 32-bit fields +4/+8, invokes a virtual handler, then exposes the handler's optional reply pointer/count from +4/+8. It does not establish how the lower transport fills the input buffer, whether that scalar is its data length, or the callback object's full layout. |
| `DeviceManager::handleRequest(Message&)`, `0x10001dc60` | Assembly first requires the 32-bit word at +0 to be at least 4, then masks the command word at +12 to 24 bits. For discriminator `0x536756`, `0x10001dd01–0x10001dd35` caches a module version shifted right eight bits and sets reply offset 16, count 4 and a 32-bit reply value. It also dispatches device connection/subscription requests to outside callees whose layouts and handshake are not recovered here. |

These facts narrow the compatibility work: there are explicit 32-bit counts,
offsets, command and reply values to preserve independently of native C++
object layout. They do not prove that the 12-byte internal control prefix is
sent, that pointers or C++ container bytes cross IPC, that all messages share
one layout, or that cross-architecture compatibility is already achieved.

The C exporter reports four completed decompilations and blank diagnostic
columns, but **that is not four fully verified function reconstructions**.
The send wrapper's C folds behavior beyond its assembly tail jump; receive
and request C contain exception/cleanup continuations not grounded in the
selected-cap instructions. The receive C also includes an outside-cap
jump-table warning. No claims above rely on those continuations. The exact
outside send boundary and missing transport/lifecycle bodies stopped further
analysis; no callee body or extra frontend code was followed.

Earlier selected `GuitarRig::Controller::onDisplayMessage` at `0x100040980`
also examines a low-24-bit discriminator and a 32-bit value, while
`FX2Controller::onPedalMessage` at `0x10003a0c0` contains calibration/pedal-option
operations. They reinforce that original functionality includes structured
NI messages. They are not independent proof of the CFMessagePort envelope.

## Transport, dependencies and remaining contract

The saved dependency metadata lists six CFMessagePort imports and named
`NI::IPC::Port::OSPortImpl`, send/receive-port, server connection and device
manager methods. Apple documents CFMessagePort as a named local
inter-thread/process transport carrying CFData requests and optional replies;
that public API does not supply NI's message IDs, field validation, endpoint
creation policy or handshake.
[Apple CFMessagePort contract](https://developer.apple.com/documentation/corefoundation/cfmessageport?language=objc).

The smallest further transport proof is source-only inspection of the exact
unresolved boundaries below, **after a new bounded admission in beads**; this
report does not authorize or perform it:

| Exact boundary, resolved from saved symbols only | Missing proof |
| --- | --- |
| `NI::IPC::SendPort::sendMessage`, `0x1002eb940` | The selected wrapper's actual tail target and handoff to the OS port. |
| `NI::IPC::Port::OSPortImpl::sendMessage`, `0x1002eda40` | CFMessagePort request ID, byte span/CFData construction, reply handling and errors. |
| `NI::IPC::Port::OSPortImpl::callback`, `0x1002ee910` | Input size/validation, dispatch framing and reply ownership. |
| `NI::NHL2::IPCReceivePort` constructor, `0x1002ec340` | Whether the observed name and construction arguments establish the expected receive endpoint; registration callees remain a stop if unresolved. |

This bounded packet alone would not prove the complete frontend lifecycle.
Connection identifiers, per-client ports, version/role acceptance, device
subscriptions, focus/ownership, invalidation and shutdown remain open.
Agent startup, process identity, signature and launch-path expectations are
also unproved compatibility requirements. Reusing NI's bundle ID or a
matching signing/provisioning identity must not be assumed. Frontends may
also access the driver directly. These limits prevent promising that an agent
port alone satisfies original NI recognition.

The dependency strand found a thin x86_64 PIE executable, minimum macOS 10.8 /
SDK 10.9. Dynamic load commands list eleven Apple frameworks plus libc++,
libSystem and libobjc; no separate vendor or Boost dylib is shown. Boost-named
local symbols do not establish its version or original source availability.
FSRef/Gestalt imports need individual availability/behavior checks; age alone
is not proof of ARM unavailability. `socketpair`/`mmap` imports do not prove a
shared-memory frontend protocol. This inventory is not a native build result.

## Audit packet and decision gate

The original temporary project was absent after reboot. Stage 2 therefore
made one no-analysis Mach-O import. A hidden-path rejection occurred before
load; a nonhidden-path correction performed the single actual import. Its
four-target filter found zero function entries and exported no C/assembly.
That count was not the entire program's function population. The first Stage
3 guard incorrectly expected a globally empty local-function population and
stopped before mutation. The separately reviewed correction preserved the
loader's baseline instead of deleting or analyzing it.

The corrected Stage 3 processed a fresh project copy with `-noanalysis`,
`-readOnly`, explicit `enableCodeAnalysis(false)` and only four restricted
entry-seeded caps. Before mutation it checked all four entries absent and all
caps disjoint from existing functions. Baseline local/external function
metadata stayed unchanged: **861 → 865**, exactly four additions. The zero
baseline instructions became **564** new instructions, each full span inside
its own cap. Four C/four assembly files were emitted, with cancellation false;
source binary and imported project hashes stayed unchanged. Independent
packet review passed before the body interpretation above. No whole-program
analyzer, guessed types or prototype repair ran, and no additional function
was selected for decompilation. The C continuations described above remain
unverified.

Private evidence is preserved under ignored
`.build/ni/agent-feasibility/20261025-ipc-four-stage3-delta/` alongside prior
attempts. Useful packet SHA256 values:

| Evidence | SHA256 |
| --- | --- |
| Existing 149-export manifest | `363963051f649a044bf0f88d3fac64540526352cec9187da125ed0194b5c8c5a` |
| Existing 39-export manifest | `a128f64745db3ee9d792c0880218691e1a7266fa1ca573693ccd30d97ee12058` |
| Dependency `metadata-summary.json` | `6bdd0f86121cd18e887e284957dd109de6719202550fb9b36b4c873d069fb0f0` |
| Paired `frontend-reference/controller174-18f35bcf/frontend-metadata.json` | `39d836e1729ac891f5a7c568c48325f4ae2f21b921ce3a42ad495bef8d9dd374` |
| Original four-cap bounds JSON | `7848dbc26a33d0fc07dc82eff24c977a0a229f6748e34218fa5dc1ddc82e69c1` |
| Corrected Stage 3 `evidence.json` | `80b0b6db5ba3ea84bd28bef2656b69b0b9b65cdbccf2f157e68c047fa69b984c` |
| Corrected four-export manifest | `23089b8e49d01755c90b924618cbd90f637bf894f485bb5b2ffa7ad8fe40f38c` |

Private reconstructed C/assembly, binary files and full symbol listings are
not redistributed here. The implementation gate remains sufficient original
protocol/lifecycle proof, a typed ARM build preserving those contracts, driver
boundary checks, and actual unchanged NI frontend acceptance. Missing proof
is recorded as unknown; it is neither a universal impossibility claim nor
permission to invent replacement behavior.
