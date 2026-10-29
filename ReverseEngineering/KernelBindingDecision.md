# Kernel binding admission: policy and signing gate

Research date: **2026-10-25 UTC**. Authority: **rigkontrol3-40h.17** in Beads.
Scope: one bounded, read-only primary-source pass before coding or security
changes. This note supplements [the parity audit](ParityAudit.md). The final
decision below incorporates the user's subsequent firm security constraint.

**Reopened decision — original NI automatic recognition is required.** The
user explicitly confirmed this on2026-10-25. The current AudioServerPlugIn /
IOUSBHost / CoreMIDI baseline does not publish NI's IOKit service/UserClient ABI
and cannot satisfy that requirement by itself. `.17` has completed a bounded DriverKit class probe, with the production binding still unadmitted; the earlier
public-baseline decision below is historical and superseded for completion.
SIP remains enabled. No new kernel/DriverKit provider, signing, installation,
account enrollment or security action is admitted merely by reopening it.

The most direct structural candidate is the retained NI kernel device/engine
and actual named service/UserClient with modern kernel USB. Its SIP-enabled
build/deployment prerequisites are not currently satisfied. DriverKit has
useful RPC mechanisms, but a supported unchanged NI class-discovery path is
not established. Neither a user-class name nor generic MIDI success settles
that requirement. See the current assessment at the end of this report.

## Availability and admission are separate

The completed static audit found real arm64e IOAudioFamily code in this Mac's
boot collection and defined matches for 184 of 186 original audio-family
imports. Retaining IOAudioDevice/IOAudioEngine while replacing legacy USB with
kernel IOUSBHostFamily remains the smallest structural candidate identified;
that is not a loading, signing or stability guarantee. Exact hashes and
comparison limits remain in the parity audit and `.13`.

Apple classifies legacy **IOUSBFamily as unsupported since macOS 11**, whereas
**IOAudioFamily is deprecated since 12.3**. Its published policy says deprecated
KPI kexts do not load by default; its footnote exempts SIP-disabled or
MDM-approved kexts from deprecation-policy enforcement. Thus the deprecated
category is neither an ARM availability proof nor an unrestricted admission
promise. Applying that published default to this new IOAudioFamily driver is
a policy inference, not an observed macOS 27.0.1 load attempt.
[Apple deprecated-kext policy](https://developer.apple.com/support/kernel-extensions)

The same page's USB vendor-specific IPC restriction is conditional on the
driver not tying into an IOFamily subsystem. The original audio driver does
tie into IOAudioFamily. That prevents automatically treating it as a standalone
USB IPC case, but does not establish approval for a newly reconstructed driver.
[Apple USB policy](https://developer.apple.com/support/kernel-extensions)

## Four independent gates

| Gate | What the primary evidence establishes | What it does not establish |
| --- | --- | --- |
| Native API/ABI | Original kernel code needs arm64e/PAC and a replacement for legacy USB. | That an x86 binary, old vtable layout or decompiler casts can be transplanted. |
| Deprecated-KPI policy | Default enforcement and the published SIP/MDM exceptions are separate from API presence. | That ordinary manual approval or Reduced Security alone overrides deprecation policy. |
| Signing and distribution | A suitable kext-enabled signing identity and the relevant distribution checks are distinct requirements. | That an Apple Development or ordinary Developer ID identity is sufficient. |
| Startup/user authorization | Apple Silicon kext installation uses explicit authorization, an auxiliary collection and a restart. | Consent to alter this Mac's security or proof that its present startup policy permits the candidate. |

Apple requires arm64e for Apple Silicon kexts and limits kernel extensions
where an equivalent system-extension solution exists. These are constraints
on the candidate, not proof that this particular NI implementation has an
approved equivalent or that a particular replacement is the minimum change.
[Apple implementation guidance](https://developer.apple.com/documentation/systemextensions/implementing-drivers-system-extensions-and-kexts)

## Security modes and policy exceptions

For ordinary signed third-party kext installation, Apple documents enabling
kexts through recoveryOS, Reduced Security, authorization and an AuxKC rebuild
and restart. With SIP enabled, signatures are checked before inclusion; with
SIP disabled, that signature check is not enforced. An authorized list can
still contain kexts excluded from the resulting collection.
[Apple kernel-extension security](https://support.apple.com/en-gb/guide/security/sec8e454101b/web)

**Reduced Security and SIP disabled are different conditions.** Apple's
current startup-policy guide distinguishes Full, Reduced and Permissive
Security. The personal-development route must not be described as
ordinary Reduced Security. An MDM exception to deprecation policy likewise
does not manufacture a valid signing identity, working native code or user
consent. No MDM enrollment/profile is being proposed or applied here.
[Apple startup security, published 2026-09-17](https://support.apple.com/en-ca/guide/deployment/dep5810e849c/web)

## Signing: distributable and personal paths

Apple's signing guidance identifies a kext-enabled Developer ID Application
certificate by its kext extension OID **1.2.840.113635.100.6.1.18**, in addition
to the Developer ID Application extension **1.2.840.113635.100.6.1.13**.
Ordinary app signing is not equivalent. This older DTS reference is corroborated by the July 2026
staff answer below; no account request or certificate operation occurred.
[Apple DTS signing reference](https://developer.apple.com/forums/thread/52353),
[Apple Developer ID certificate profile, v3.2 §4.11.3](https://images.apple.com/certificateauthority/pdf/Apple_Developer_ID_CPS_v3.2.pdf).
The profile is a published historical certificate specification, corroborated
by the current staff guidance; it is not presented as the latest CPS.

Apple's notarization documentation requires new or updated kexts to be
notarized for its distributable path and specifies the appropriate Developer
ID certificate types. Notarization is not a replacement for the right signing
identity or for startup/loading approval.
[Apple notarization requirements](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[Apple kext certificate diagnostics](https://developer.apple.com/documentation/security/resolving-common-notarization-issues)

In **July 2026**, Apple DTS CoreOS/Hardware engineer Kevin Elliott addressed
personal-use Apple Silicon kexts directly: the testing path requires SIP
disabled and Permissive Security. He reported that kext-signing approval had
been limited to corporate accounts and new requests were unlikely. Quinn
separately distinguished distributable signing/notarization from personal
testing. These are current, attributable staff statements, not a guarantee
about this user's account or future decisions. Only the Apple staff replies,
not the questioner's reported experiment, support this conclusion.
[Apple DTS personal-use clarification](https://developer.apple.com/forums/thread/837391)

## Technical minimum and remaining contract gates

The installed **macOS27.0 Kernel.framework headers**, compared against the
pinned original NI2.8.0 mappings, can represent the ring and calendar model.
This is a static source/API finding, not a compile, load or stability result.
Full header hashes, line ranges and original method addresses are in the
technical evidence packet in Beads `.17`.

| Necessary adaptation | Original behavior to retain | Unproved boundary |
| --- | --- | --- |
| Generate typed arm64e classes/callbacks against current IOAudio/IOUserClient declarations | NI fields, selector schemas and audio hooks | PAC, changed reserved slots, linkage and caller authorization |
| Replace IOUSBDevice/Interface/Pipe with kernel IOUSBHost equivalents | VID/PID, device information, startup/failure ordering and sole owner | Exact session, termination and callback lifetime |
| Preserve controller-compatible non-bounced backing (documented `createIOBuffer` path); use current ISO lists, offsets and Wrap | Shared live wire backing, sample positions, input-led counts and 64/128 calendar | DMA visibility/prefetch, whole-eight-result primary publication and queue acceptance |
| Map command, clear and power calls to supported methods | True/false clear distinction, no replay, original error policy, sleep/clock resets | Modern abort side effects, idling/PM order and lock composition |

`IOUSBHostPipe.h` documents the legacy migration and absolute-frame ISO I/O;
`IOUSBHostInterface.h` promises controller-compatible non-bounced allocation.
The API accepts a suitable IOMemoryDescriptor; `createIOBuffer` is the
documented non-bounced path, not the only descriptor factory it permits.
Those declarations do not guarantee NI's primary-interrupt publication cadence
or arbitrary late CPU-write timing. Internal interrupt markers are explicitly
outside the public interface and are not a proposed solution.

The audio family retains timestamp, sample-position, pause/resume and stream
client roles. The USB family also imposes a per-port workloop. Their composition,
callback ordering and lock hierarchy must be established; remaining in the
kernel is not a demonstrated fix for the current TooOld fault. Typed original
user-client contracts are representable, but actual access and compatibility
remain unverified. No FIFO, new queue lead, new clock algorithm or new private
transport is forced by these inspected kernel APIs.

The selected SDK contains the relevant audio/modern USB headers; legacy USB
headers are absent. Xcode/clang metadata supports AArch64. No local KDK was
found, but that observation alone proves neither a universal KDK requirement
nor inability to build. No compiler/linker test or toolchain installation was
performed. These bounds prevent a claim that a usable ARM kext already exists.

## This Mac: observed facts and exact unknowns

The root agent supplied these scoped read-only observations on 2026-10-25:

- macOS **27.0.1 / 26A434**, Xcode **27.1 / 27A9275**, SDK **27.0**, clang21.
- `csrutil status` reported **SIP enabled**.
- `bputil -d` required administrator access; `sudo -n` required a password.
  Actual Full/Reduced/Permissive startup policy therefore remains **unknown**.
- The normal-user, unsandboxed identity query found **one valid Apple
  Development identity and zero Developer ID Application/Installer identities**.
  An earlier sandbox query falsely returned zero identities and is not used.
  No current valid kext-signing identity was found by the scoped check.
- `profiles status -type enrollment` reported **Enrolled via DEP: No** and
  **MDM enrollment: No**. No current managed exception is demonstrated.
- Account-level kext-signing eligibility is **not established**. The user has
  now required **SIP enabled** and rejected the personal kernel-testing path.

These are local observations, not assertions that signing authorization can
never be acquired. No private key was read/exported, no identity requested,
and no signing, notarization or load operation was performed.

## Historical constrained public-baseline decision (superseded)

The user's answer is authoritative: **“Keep SIP enabled; do not pursue that
testing path.”** The SIP-disabled/Permissive personal-development route is
excluded and must not be proposed again unless the user changes that constraint.
No other security change is authorized.

The retained-kernel-audio candidate remains structurally plausible, but is not
admitted for this current personal-Mac deployment. No locally usable kext-signing
identity or managed deprecation-policy exception was found. A hypothetical
properly signed/managed route is not declared impossible; pursuing certificates,
account approvals, MDM enrollment or startup changes is outside this decision.

Select the **existing public AudioServerPlugIn / user IOUSBHost / native
CoreMIDI binding** for the next planned parity work. Actual installed loading
under the observed SIP-enabled conditions supports this limited selection.
There is no automatic DriverKit project, kernel build, new backend, editor or
compatibility-layer effort. Kernel-to-user adaptation is a choice justified by
the recorded deployment constraint and available route, not proof that ARM
lacks IOAudioEngine or that this is the only possible SIP-enabled architecture.

### Continuing entry criteria in existing Beads cards

`.17` resolves architecture/deployment selection only. It does not certify the
selected binding's timing, DMA, lifecycle or NI-client equivalence, and does not
waive any of the parity matrix's missing functions.

- **`.6` owns the selected-API contract disposition.** Before a dependent change
  touches a ring, publication, clock, queue, callback or power contract, its card
  must identify the original behavior, public counterpart, supporting evidence,
  remaining uncertainty and bounded test/stop condition. Distinguish a contract
  adequate for a specific implementation from final hardware/stability acceptance.
  Do not create a circular dependency between that disposition and `.11`'s final
  stability result.
- **`.2` owns required NI client/control mapping.** Before changing an affected
  interface, identify the confirmed original consumer/operation/schema, retained
  functionality, selected public transport and exact lifetime/error behavior.
  Missing legacy NI service/selector compatibility stays unresolved; generic MIDI
  success does not waive it. Applicable settings, gains, display, assignment and
  feedback functionality cannot be discarded as optional GUI work.
- **Every implementation/runtime card retains its own admission rule.** Use the
  existing original-parity plan and precise red/green/stop criteria. No guessed
  queue lead, private interrupt constants, speculative priority tuning or new
  recovery policy follows from this selection. If a required original contract
  cannot be supported through the selected public binding, stop its owner card
  and record the exact blocker. Do not narrow functionality or start another
  architecture implicitly.

The primary lift-and-shift remains incomplete. The current build's TooOld fault
and all missing/unverified NI capabilities remain open. Closing this decision
starts no code, build, signing, loading or runtime action by itself.

This pass changed research/status documentation and Beads only. Driver source,
installed bundles, firmware, toolchains and security settings were not changed.
Official-source findings above retain their evidence date and limits; no load
experiment of a reconstructed kernel driver occurred.

## Current recognition requirement and eligibility assessment

The confirmed target includes automatic recognition by original NI software,
not only manually mapped CoreMIDI controls. Original MIDI `Start@0x2490` passes
`de_caiaq_driver_NIUSBAudioDevice` to `listenToService@0x2380`, which uses
`IOServiceMatching` and first-match/termination notifications. `deviceAdded`
opens the returned service with type0 and asks selector3 for110-byte device
information; its async MIDI subscription uses selector0. These C/ASM exports
remain local and are pinned by the [MIDI ledger](Audit/midi280-function-coverage.json).
The agent's selected `OSImpl::connect` exports establish type0 open, selectors3
and18 (110-byte info/14-byte spec), configuration, raw-event subscriptions and
LED output. They do not separately establish that agent's discovery class, or
modern Guitar Rig's complete automatic-recognition chain. Those need actual
unmodified-client acceptance; the report does not infer them from old MIDI code.

DriverKit can implement `IOServiceOpen`/`NewUserClient`, external methods,
retained async actions and client-memory descriptors. The problem is therefore
not blanket absence of IOKit RPC. Its documented kernel `IOClass` and user
implementation `IOUserClass` are distinct; `SetName` changes a registry name.
The public XNU reference uses `metaCast` for ordinary class matching. No
supported alias from the old NI kernel class to an identically named user
implementation was established. This is a concrete unresolved compatibility
contract, not proof that DriverKit can never provide one.
[Apple IOClass](https://developer.apple.com/documentation/bundleresources/information-property-list/ioclass),
[Apple IOUserClass](https://developer.apple.com/documentation/bundleresources/information-property-list/iouserclass),
[Apple XNU class matching](https://github.com/apple-oss-distributions/xnu/blob/main/iokit/Kernel/IOService.cpp#L7346-L7352).

Unchanged NI clients also cannot acquire a new entitlement merely because we
create a dext. DriverKit's broad user-client-access entitlement has distinct
development and distribution paths; valid profiles/capabilities and client
checks matter. Ordinary Apple Development signing alone proves none of those
capabilities. This does not reopen an unapproved compatibility-provider project.
[Apple DriverKit entitlement process](https://developer.apple.com/documentation/driverkit/requesting-entitlements-for-driverkit-development),
[Apple DTS user-client access clarification](https://developer.apple.com/forums/thread/774297).

Earlier local preflight, before DriverKit provisioning: macOS27.0.1/build26A434, arm64, SIP enabled; one valid
Apple Development identity, no Developer ID identity found by the normal-user
unsandboxed query; DEP and MDM enrollment both No. Five existing provisioning profiles decoded
successfully using normal macOS trust services; none grants base DriverKit,
USB DriverKit or AudioDriverKit capabilities. A sandbox-only decode failure
was discarded and is not evidence about profile contents. The user explicitly
confirmed that neither a kext-enabled signing identity nor access to an
MDM-managed test deployment is available. No KDK directory is installed.
The current SDK does contain IOAudio and kernel IOUSBHost headers; their
presence is not a successful build or loading approval. No key was exported
and no profile/security setting changed.

For the retained-kernel candidate, Apple's published rules distinguish
kext-enabled signing, startup authorization/AuxKC/restart and deprecated-KPI
admission. With SIP retained, the published deprecation-policy exception is
MDM approval. Reduced Security alone supplies neither the certificate nor that
exception. Future account eligibility is not declared impossible, but the user confirmed
that the signing and managed-test capabilities are currently unavailable.
[Apple deprecated-KPI policy](https://developer.apple.com/support/kernel-extensions),
[Apple signed-kext startup requirements](https://support.apple.com/en-gb/guide/security/sec8e454101b/web),
[Apple kext certificate requirement](https://developer.apple.com/forums/thread/52353).

### Compile-only preflight result

The [declaration probe](Audit/ni-kernel-declaration-probe/declarations.cpp)
defines no function bodies or driver personality. It tests retained class
shapes and typed modern kernel USB calls using arm64e, the installed27.0 SDK,
`KERNEL`, `-mkernel`, `-fapple-kext` and `-fsyntax-only`. The invocation failed:
12 missing `IOPMDriverAssertionID/Type/Level` declarations in the selected
headers, with104 warnings retained. No object, link, package, signature or load
result exists. No substitute types, private macro, downloaded header or
suppressed checks were used. The installed35-line assertion-types header does
not declare those types, although RootDomain/IOUSBHostDevice require them.
[Exact invocation and header identities](Audit/ni-kernel-declaration-probe/evidence.json).

This result establishes a particular installed-header completeness failure.
It does not retract the actual ARM IOAudio symbol inventory, establish global
ARM impossibility, prove a matching KDK could not resolve it, or certify any
partial class/runtime/PAC equivalence. Further source/build preparation needs
a coherent kernel development header set and a separately recorded disposition.

### Current work status

The interrupted native direct-monitor extension is preserved on
`codex/rk3-native-monitor-checkpoint-20261025` at
`711d76cdf6228186cf44a359950c834d5a758965`. It is explicitly unaccepted: strict
focused compilation and the HAL fixture passed, but the expanded fake-USB
fixture hung and was terminated. It is not installed, independently accepted,
or the NI private interface. Its seven files were restored exactly to the
reviewed main baseline; the original wire/selector evidence remains in Beads.
Further HAL power/display extension implementation stopped before source edits.

No remaining card is closed on these audits. That earlier assessment blocked
`.17` and the parent. Subsequent DriverKit development preparation below has
resumed them; no production backend or NI compatibility is admitted. Native functionality is not an accepted substitute for those
gates, and unavailable credentials or security changes are not inferred as consent. All audio, control, settings, display, rate,
power, physical DIN/pedal and stable-operation requirements remain in scope.

## DriverKit development probe: subsequent progress

The user confirmed active Apple Developer Program membership and permission to
create development profiles. Normal Xcode provisioning succeeded after the
current Mac was registered for development. Both the containing app and base
DriverKit service now have matching Apple Development signatures and real
profiles granting their required capabilities. Strict/deep verification and
Apple-anchor checks pass. No kext certificate, MDM enrollment or SIP change
was involved. This supersedes the earlier absence-of-DriverKit-profile snapshot;
it does not resolve the separate retained-kernel deployment restrictions.

The [software-only discovery probe](../Tools/DriverKitDiscoveryProbe/README.md)
builds unsigned and development-signed. Its fixtures pass strict compilation
and ASan/UBSan; independent source reviews cover service scope, bundle layout,
identity matching and inconclusive results. The qualified live class-match result is recorded below; build and
signing success alone establish no NI application acceptance. No RK3, audio, MIDI or NI selector
implementation is present in this probe.

The original MIDI driver is being ported and may require a necessary lookup
adaptation. The unchanged NI agent has its own matching constraint: bounded
read-only exports of NIHardwareAgent1.7.4/R199
(SHA256 `f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`)
show its RK3 filter initialized with type1, VID17cc, PID1940 and
`de_caiaq_driver_NIUSBAudioDevice`. The initializer loads the exact RK3 global
at `1002f7f84` and calls the filter constructor at `1002f7fa9`. That constructor
stores the supplied class string at filter+0x20 (`1002f7559` through
`1002f7575`); the factory reads that field and calls `IOServiceMatching` at
`1002f5b0b`. No property or name-only alternative was found in that factory.
The inspection covered seven functions and one 50-instruction initializer
window; full DeviceClass activation and current Guitar Rig recognition are
still unproved. Private exports and exact hashes are retained in `.17`/`.2`.

A probe miss therefore directly tests retaining this legacy class lookup for
the original agent's declared RK3 filter. It does not establish that every
DriverKit route or every NI application is impossible. A positive result
likewise proves no user-client ABI or actual application acceptance. The user
still requires actual original NI automatic recognition.

## Development activation and device eligibility

Normal user approval completed for both software-probe attempts, and
`systemextensionsctl` reported activated and enabled with SIP enabled. That
state did not establish that the DriverKit executable launched.

The first diagnostic returned `INCONCLUSIVE / PROBE_QUERY_ERROR` before NI's
class query. The installed SDK permits successful empty searches to return a
null iterator; our diagnostic incorrectly converted that case into an error.
An actual-adapter regression with fake IOKit calls reproduced the bug. Its
one-line correction passed strict compilation, ASan/UBSan and independent
review. The original output does not identify whether that bug caused the first
result. Iterator-validity checks remain, consistent with
[Apple's IOKit implementation](https://github.com/apple-oss-distributions/IOKitUser/blob/main/IOKitLib.c).

After the user rebooted, a fresh corrected client passed its offline fixtures.
The single second attempt returned `INCONCLUSIVE / NO_PROBE`, with no API error;
it also stopped before NI's class query. A separately planned, narrow startup
log inspection identified the actual launch failure: macOS rejected the dext's
profile as not provisioned for this device (`CPProfileManager -212`, then AMFI
`-413` and spawn failure). The extra registry snapshot was unusable because its
output failed plist parsing; it supplies no registry conclusion.

Our earlier validation checked cryptographic trust, team, App IDs, capabilities
and expiry, but omitted device eligibility. Private comparison confirmed that
the host profile included this Mac's provisioning UDID, while the dext's
three-device profile included neither this Mac's provisioning UDID nor hardware
UUID. This was our signing validation omission, not evidence against NI's class
contract or the ARM instruction set.

Following [Apple's profile refresh guidance](https://developer.apple.com/help/account/provisioning-profiles/edit-download-or-delete-profiles/),
we preserved and removed only the exact ineligible cached profile, then built
the unchanged probe for the physical Mac using normal automatic development
signing. The refreshed dext profile includes four devices, including this Mac;
the host profile remains unchanged and eligible. Both actual embedded profiles
now pass the current-device, team, App ID, capability and expiry checks. Both
bundles pass strict Apple signature and architecture checks. No new capability,
NI code, timing or recovery behavior was introduced.

The second probe was deactivated. Its callback again requires a reboot, while
the subsequent extension list contains zero entries. Full teardown remains a
normal-reboot prerequisite before another activation. The refreshed app was copied to `/Applications/RK3DiscoveryProbeHost.app`
after the old verified app was backed up. Its nine hashes, strict Apple
signatures and default inspection verify; no fresh activation occurred.
The build and its nine-file manifest are preserved under ignored
`.build/ni/driverkit-discovery-evidence/20261025-postreboot-e980ae5`; Beads `.17`
records the exact hashes, failed and corrected eligibility checks, independent
reviews and installation boundary. The prior original-agent exports and timing
measurement evidence remain preserved separately. No legacy class-match,
user-client ABI or actual NI application recognition result is claimed.

## Qualified live result and next admission boundary

With the corrected device-eligible profiles, normal user approval completed and
the service was actually published. The one diagnostic returned exit 1,
`FAIL / NAME_ONLY_MATCH`: one valid independently marked registry entry
(`4294971845`), kernel class `IOUserService`, NI's string as `IOUserClass` and
registry name, zero entries from the exact NI class query, and the same entry
from name matching. Both API error codes were zero. The evaluator rechecked the
unique marked identity before retaining the negative verdict. Independent
readback confirms the result.

This rejects the tested naming mechanism for the class query in the modern
ARM diagnostic. The original agent uses matching notifications; its actual
notification operation and Rosetta process were not exercised. This does not
reject all DriverKit implementations or establish NI application acceptance. The [bounded primary-source comparison](DriverKitClassMatching.md)
found no documented driver-side matching alias. Its examined server helper
also supplies no automatic legacy compatibility flag. Published-source version
limits and untested modern NI application behavior remain explicit. The name
`IOServiceDEXTMatching`, previously suggested as a possible matching contract,
was not found in the inspected public headers or IOKitUser source and must not
be treated as an available API.

The production audio/user-client port remains unadmitted. Original NI automatic
recognition is still required. The user authorized a bounded feasibility study of an ARM port of NI's
supporting hardware agent in `rigkontrol3-40h.18`, preserving its protocol and
keeping Guitar Rig and Controller Editor unchanged. This permits investigating
a necessary lookup adaptation in the supporting agent; it does not waive
original functionality or admit implementation. The tested unchanged-agent
target must not silently become a requirement that every support binary remain
immutable. No agent port, guessed compatibility layer, new provider code or
runtime retry was performed.

There is also a version boundary for acceptance: an
[NI community administrator states that RK3 is not supported in Guitar Rig 6 and
can be used as a standard MIDI controller](https://community.native-instruments.com/discussion/12972/rig-kontrol-3-midi-issues).
That first-party support statement does not prove hidden implementation details
or authorize dropping the user's recognition requirement. Legacy NI recognition
and Guitar Rig 6 MIDI behavior need distinct, named acceptance targets before
claiming original-functionality parity.

Probe cleanup initially failed macOS authorization (error 13). The user disabled
it through normal System Settings authentication; the subsequent extension list
contained zero entries and SIP remained enabled. The waiting carrier exited
with a requires-reboot callback, so complete teardown remains pending a normal
reboot before any future activation. No additional restart is requested merely
for offline planning. The signed app and verified rollback copies are retained;
no owned removal carrier remains. All evidence is saved under ignored
`.build/ni/driverkit-discovery-evidence/20261025-eligible-probe-5a25a43` and detailed
in Beads `.17`/`.2`. NI audio ring, timing, recovery and installed drivers remain
unchanged.

## Supporting-agent feasibility checkpoint

The user-approved assessment in `rigkontrol3-40h.18` is complete and
independently reviewed. The [feasibility report](NIHardwareAgentFeasibility.md)
identifies a conditional adaptation path, not an implementation-ready agent.
The exact Controller Editor1.7.4/R199 frontend and its matching agent were
identified from the supplied installer without running either. Four original
agent functions establish cap-local endpoint, message-wrapper and version-query
facts; they do not establish the complete wire ABI or unchanged-app acceptance.
Decompiler continuations beyond the selected assembly are explicitly excluded
from the findings.

The next source-only proof is captured in `rigkontrol3-40h.19`: four exact
transport boundaries, with assembly-first framing, reply ownership and endpoint
checks before code. Startup identity, full connection lifecycle, direct
frontend driver access, signing, typed ARM builds and actual NI acceptance
remain gates in `.2`/`.17`. Closing the bounded assessment does not select a
production backend or complete the primary driver port. No NI driver or agent
implementation was changed.
