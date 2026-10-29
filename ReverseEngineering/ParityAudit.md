# NI Rig Kontrol 3 → ARM parity audit

**2026-10-25 requirement update:** the user requires original NI automatic
recognition. `.17` is reopened; the current public HAL/CoreMIDI baseline does
not provide the NI IOKit discovery/UserClient contract. The historical selection
below is not the completion decision. See [current binding status](KernelBindingDecision.md).

Audit date: **2026-10-25 UTC**. Source checkpoint: **f540463**. Authoritative
record: **rigkontrol3-40h.13** in Beads; this report and the JSON ledgers are
reviewable evidence exports. They are not a second task tracker.

**The parity inventory and bounded architecture assessment are complete. The
primary driver lift-and-shift is incomplete.** The current user-space HAL is
not established as the smallest necessary ARM change. This audit authorizes
no driver coding or runtime experiment.

## Scope, evidence and completeness

The reference is the attached **Rig Kontrol 3**, not every NI product served
by the shared driver. Preserve its original behavior and callable capability
surface. An unknown applicable branch remains unresolved; it is not silently
removed from scope. Optional companion-application UI is distinguished from
the driver contract it consumes.

| Pinned input | SHA-256 |
| --- | --- |
| NIUSBAudioDriver 2.8.0 x86_64 | `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08` |
| NIUSBMIDIDriver 2.8.0 x86_64 | `84fa1a6d75c7e478af82fe4f7a38ddd633d60754d63d65d6090f0b2517152ae1` |
| Audio function index | `c27c00c954e0ca08e4f725e8f3898b942e77c2a430578c3d9ff3115a9a4966e8` |
| MIDI function index | `70b57a10fc1960a973707744b3db5e50fa8630fe875e6a7b7e2e2803d4dfd52f` |

The private references remain under `.build/ni/reference/2.8.0/`; the existing
Ghidra C/assembly pairs and indexes remain under
`.build/ni/decompiled/{audio280-text,midi280-text}/`. Decompiled types and
vtable offsets are evidence to check against instructions, not original
source headers. No proprietary binary or complete decompiled body is
redistributed by this report.

The evidence has two complementary levels:

- [67 capability/platform/package rows](Audit/parity-matrix.json): A1–A12,
  B1–B16, C1–C8, D1–D5, E1–E6, F1–F3, G1–G8 and P1–P9. Each preserves the
  original address/source, current counterpart, branch-specific status,
  evidence, uncertainty and completion/stop boundary in its packet text.
- [241 audio entries](Audit/audio280-function-coverage.json) and
  [132 MIDI entries](Audit/midi280-function-coverage.json): every address and
  symbol in the pinned indexes is classified. The MIDI total is **59
  implementation entries plus 73 import trampolines**, not 132 NI algorithms.
  Runtime wrappers, imported APIs, common utilities and proven other-product
  branches have reasons; they are not counted as missing RK3 features merely
  because no identically named ARM function exists.

Complete inventory means exhaustive classification against these indexes and
an explicit disposition for the capability surface. It does **not** mean all
runtime branches match or all unknowns have been experimentally resolved.
Applicability-unknown private controls remain with `.2`/`.15`, firmware and
audio identity with `.1`, and deployment admission with `.17`. Original
capability exclusions require explicit user acceptance before full parity.

## Architecture Necessity Decision

The original plist depends on **IOAudioFamily**, **IOUSBFamily** and kernel
KPIs. Apple lists the legacy IOUSBFamily APIs as unsupported since macOS 11;
IOAudioFamily is deprecated rather than identically classified as unsupported.
New deprecated-kext loading is constrained by current policy. Neither a missing
header nor deprecation alone proves that an audio class is absent from ARM.
[Apple kernel-extension policy](https://developer.apple.com/support/kernel-extensions)

The actual Mac is **macOS 27.0.1 / 26A434 / arm64**, with **Xcode 27.1 /
27A9275** and selected **macOS SDK 27.0**. The audit inspected the real boot
collection, rather than assuming a bundle directory contained executable code:

| Static observation | Result |
| --- | --- |
| Current signed IMG4 collection | UUID `DC19E614-3443-91F3-E063-6DD78DA31217` |
| Compressed collection SHA-256 | `a6780cfee14e2f1ce248a0942fa4415bbd32b27836a0857fb4e00d25943466e9` |
| In-memory decoded arm64e fileset SHA-256 | `06846c718a67715f540c7f5a5d5a183da6185d94b6fc01f1ce57a30b601be44d` |
| IOAudioFamily implementation | 142,284 bytes of `__TEXT_EXEC`; 1,849 symbol records |
| Original audio-family imports | 184 of 186 exact names have defined ARM symbols |
| Two unmatched names | Reserved IOAudioEngine slots 15/16, consumed by newer SDK methods |
| IOUSBHostFamily | Real arm64e code and public kernel USB interfaces present |
| Legacy IOUSBFamily | No matching fileset entry; original kernel headers absent |

The decoded collection was inspected in memory using installed libcompression;
no extracted kernel binary was saved or executed. The full boot path, metadata,
commands, SDK/header hashes and limitations are in the matrix's architecture
packet and Beads `.13`. Symbol presence does not establish ABI layout,
pointer-authentication correctness, current loading approval or stability.

**Required changes:** native execution/source ABI, including ARM replacements
for x86-specific code; and migration away from the unsupported legacy USB
family. Apple requires the kernel's arm64e/PAC ABI for Apple Silicon kexts;
user-process plug-ins use their documented native ABI.
[Apple driver implementation guidance](https://developer.apple.com/documentation/systemextensions/implementing-drivers-system-extensions-and-kexts)

**Minimum structural-change candidate:** retain reconstructed
`IOAudioDevice`/`IOAudioEngine`, its sample-positioned ring, workloop and
lifecycle, replacing the USB binding with **kernel IOUSBHostFamily** and the
required source/ABI changes. This is an engineering inference from the inspected
contracts, not a validated or policy-approved deployment recommendation.
The SDK's kernel `IOUSBHostPipe` documents legacy-pipe migration, ISO I/O using
caller-owned lists/storage, and `clearStall(bool withRequest)`; that Boolean
choice is closer to NI's calls than the current user API.

**Not justified as unavoidable:** replacing the kernel audio model with the
current AudioServerPlugIn, default pthread/dispatch handoff, new inter-bundle
CFData transport and approximated host activity/power events. Those changes
may require a deployment rationale, but “ARM has no IOAudioEngine” is false on
this inspected Mac. A user-API limitation does not prove that selecting that
API was necessary.

**Unresolved deployment gate:** a new driver using deprecated IOAudioFamily
must satisfy current loading policy, suitable signing authorization and the
user's security constraints. General Apple Silicon kext installation involves
approval, auxiliary collection rebuilding/restart and Reduced Security settings;
Reduced Security alone does not prove this specific deprecated-family driver
will be admitted. The audit did not inspect/change startup security, request
signing authorization, install a KDK or compile/load a kext. The absence of a
local KDK is a tooling observation, not an impossibility proof.
[Apple custom-kext installation](https://developer.apple.com/documentation/apple-silicon/installing-a-custom-kernel-extension),
[Apple deployment requirements](https://support.apple.com/en-gb/guide/deployment/depa5fb8376f/web)

**Original Stage2 disposition:** the bounded necessity assessment stopped here
and required an explicit deployment/binding decision in **`.17`** using the
actual constraints above. It did not admit silently continuing the HAL,
starting another backend, reducing security or choosing DriverKit solely
because the original family is deprecated.

The subsequent [.17 binding decision](KernelBindingDecision.md) incorporates
the user's firm SIP-enabled constraint and excludes personal kernel testing.
It selects the existing public user-space binding for this Mac, with `.6` and
`.2` retaining the relevant API/client entry criteria. This changes neither the
audit's original-functionality scope nor its finding that kernel audio exists
on ARM; it establishes no loadable kernel port or completed driver parity.

## Change register

“Contingent” below means justified only within a chosen interface; it is not
proof that the interface or architecture is required. No behavior difference
gets an ARM justification merely by being present in the prototype.

| Existing/proposed difference | Audit disposition and evidence | Beads owner |
| --- | --- | --- |
| x86 code, host pointer/layout and kernel PAC ABI | Native adaptation necessary. Preserve values, fixed-width wire/legacy-event layouts and behavior; do not transplant decompiler casts or old vtable offsets. | `.1`, `.2`, `.17` |
| Legacy USB device/interface/pipe classes | Replacement necessary. Kernel IOUSBHostFamily and user IOUSBHost are distinct candidates; mandatory USB migration does not select the whole architecture. | `.17` |
| IOAudioEngine → user-space HAL; kernel workloop → default pthread/dispatch | Global necessity unproved; real ARM audio classes exist. Current missed deadlines remain red. Numeric QoS/queue-lead tuning is not admitted. | `.17`, `.6`, `.11` |
| Wired live ring → optimized mapped storage/Wrap | Backing identity/ownership implemented in chosen API. Primary publication, physical DMA timing and equivalent scheduling bounds remain unproved. Preserve original positions/calendar. | `.6`, `.17` |
| Per-pipe acceptance-order callback adapter | Demonstrated necessary for current binding's valid reversed arrivals; real-seam regression fixed. This does not explain the later live TooOld fault. | `.12` closed; `.11` open |
| NI controller-only false clear → broader user clear/abort | Semantic difference. Lifetime/one-rearm discipline is tested, but kernel Host API retains a Boolean choice. No global necessity for losing it is established. | `.9`, `.17` |
| Kernel pause/resume → HAL configuration handshake | Installed audio-only recovery observed, contingent on current architecture. Original scope/delay retained; stable operation not proved by recovery. | `.8` closed; `.11` open |
| activeClientCount/power callbacks → StreamIsActive/host StopIO | Original-equivalent last-client and sleep behavior unverified. | `.10`, `.17` |
| NI private user-client → ninf/nrpt/ncmd CFData | New transport; actual CFData marshaling works in chosen HAL architecture. It does not implement the original NI service/selectors. | `.2`, `.17` |
| Separate HardwareAgent mapper → fixed native Controller device | Default profile behavior recovered, component consolidation is a choice. Original selectable settings/display/private contract remain accounted for; no automatic editor UI project. | `.2`, `.7`, `.16` |
| Fixed 48 kHz/default, unity-only capture, missing monitor controls | Original-functionality omissions, not ARM requirements. | `7vl`, `.15` |
| Every-128ms boundary anchor, earlier priming effects, stricter startup gates | Audit identified departures without ARM justification. Clock cadence restored in build7; `.16` source now corrects priming order and A5/A6/A8 nonfatal gates. A7 readiness/A9 activation and installed acceptance remain open; see `AudioDriver.md` and `NativeUSB.md`. | `.14`, `.16` |
| Firmware update branch absent | Original would skip on verified release000d; general gate/download is not ported. No firmware upload justified for this unit. | `.1` |
| Fixed/new audio/MIDI identities; new bundle/factory namespace | Choices aid separation/rollback but are not ISA requirements. Serial-derived identity/private discovery are not preserved by renaming. | `.1`, `.2` |
| Native signing and local Apple Development certificate | Rebuilt code needs valid current-host signing; actual MIDIServer rejection/acceptance proves this local case. It does not establish distribution or kext authorization. | `.1`, `.2`, `.17` |
| macOS26.4 minimum, new HAL package/installer paths | Consequences of selected shared-list API/package architecture, not general ARM minimum. Reversible install tooling is verified infrastructure, not NI behavior. | `.1`, `.17` |

## Functionality disposition

| Group | Current matched/implemented evidence | Blocking original gap or uncertainty |
| --- | --- | --- |
| A: discovery/startup/commands | Attached topology, commands, persistent reader, removal and lifetime checks | Failure/readiness gates differ; merged activation; general firmware branch absent |
| B: audio data/clock | Mode2 packing, sample-positioned shared ring, original calendar and timestamp formulas | Five-rate/default44100 surface, gain/monitor controls, 8-tick clock cadence, priming order, real publication/identity |
| C/P: timing/recovery/power | Scoped error policies; ordered callbacks; audio-only panic recovery | Live TooOld, scheduling-domain equivalence, inactive output, sleep/wake, original statistics semantics |
| D: DIN MIDI | Native CFPlugIn, byte parser, send/drop behavior and attached endpoint topology | Actual DIN loopback, original identity/discovery and receive-client/timestamped contract |
| E: controller | Default mapping provenance, saved88–648 calibration, eight switches/main pedal and explicit incoming LEDs | Selectable pedal modes, toe/external axes, programmable display, full assignment feedback; UI/profile application separated |
| F: clients/hosts | Generic native audio/MIDI endpoint access demonstrated | NI private selector/service contract, HardwareAgent consumers and fresh-native effects-host acceptance |
| G: deployment | Signed current bundles and reversible installer previously checked | Binding admission, clean final artifact, temporary DEBUG removal and final installed acceptance |

The original hardware-buffer get/set selectors are successful no-ops after
provider/engine checks; they do not establish a configurable USB queue. LCD,
EP4 and other product-specific paths have explicit other-PID evidence in the
function ledger. Shared I2C/user-data/encoder/sample interfaces whose RK3 use is
unproved remain applicability-unknown, rather than automatically missing or
out of scope. The controller/client ledger separately identifies confirmed
DIN plug-in and HardwareAgent selectors; direct Controller Editor/Guitar Rig
private consumption is not inferred from a symbol name.

Original profile-aware feedback is also distinct from automatic effect-state
discovery. The tested native feedback destination reacts to received MIDI;
it does not discover a host's effects by itself. Retired MainStage/app results
do not count as acceptance of this fresh driver.

## Validation boundary and implementation admission

Installed **0.2.1/build6** remains unchanged, matched before this audit to the
12-file manifest
`e2ee623f97af89c24d94e38d7061b460c3e1bd51dd13052d57b9937ecaa400e6`.
The prior v5 guitar/eight-switch/pedal/LED/held-USB physical checks passed,
but its status log contained unsolicited audio restarts. Build6 passed
**120.012s** and then failed a longer MIDI-active run at **227.593s**.
Neither signatures, callback counts nor subjective clarity establish full
parity or stability.

The failing trace preserves NI's target oldframe+64. It records raw-arrival
to worker **54.539625ms**, enqueue wall duration **17.702292ms**, negligible
ordered hold and a target20frames late at the subsequent query. These are
elapsed observations; they do not distinguish CPU work, blocking, descheduling
or publication latency, and do not justify a priority change.

Every remaining child card now has original-row references, a minimum plan,
red/green evidence and a stop criterion. **`.17` has now selected the public
binding under the user's SIP constraint; it grants no blanket coding or runtime
admission.** Relevant `.6` API-contract and `.2` client/control dispositions,
exact commands/access and one falsifiable hypothesis must be recorded in the
responsible card before any later experiment;
this report is not blanket execution authorization. The plan retains a bounded
three600s48k MIDI-active stability threshold, zero unsolicited restart/error/
generation-change predicate, separate intentional recovery windows, per-rate
acceptance, and guided physical controls/audio checks. Missing hardware stays
unvalidated. The next relevant step is the affected card's `.6`/`.2` contract
disposition and bounded original-parity plan; this record admits no new
sampling, QoS tuning or parallel backend effort.

Audit work was limited to static originals/current source/SDK/system artifacts,
primary Apple policy, evidence classification, documentation and Beads. No
driver code, installed bundle, firmware, scheduling policy or security setting
changed. No runtime experiment or build was performed. The parent remains open
until all applicable rows pass or the user explicitly accepts specific limits.
