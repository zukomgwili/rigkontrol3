# Kernel binding admission: policy and signing gate

Research date: **2026-10-25 UTC**. Authority: **rigkontrol3-40h.17** in Beads.
Scope: one bounded, read-only primary-source pass before coding or security
changes. This note supplements [the parity audit](ParityAudit.md); it does not
close `.17` or select deployment preferences for the user.

**The original kernel audio model exists on this ARM Mac, but a usable loading
path for a newly reconstructed kext is not established under the present
constraints. Reduced Security alone is insufficient for personal kext development
without an approved kext-signing identity, and does not itself establish a deprecated-KPI exception. No fallback
architecture is automatically selected.**

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
- Account-level kext-signing eligibility and the user's acceptable security
  changes are **not established**.

These are local observations, not assertions that signing authorization can
never be acquired. No private key was read/exported, no identity requested,
and no signing, notarization or load operation was performed.

## Conditional disposition and stop

With **current observed SIP enabled**, no usable kext-signing identity found,
no MDM enrollment and no security consent, the kernel candidate
is **not admitted for implementation or installation** by this assessment.
Its ARM API availability remains valid; its loading route is the blocker.

There are two materially different deployment conditions to evaluate only if
the user authorizes the relevant path: a suitable signed/approved route with
a documented deprecation-policy exception, or personal development under the
stronger security changes identified above. Neither is chosen here. In
particular, this research does not request permission to disable SIP, create
an MDM profile, obtain a signing entitlement or install a kernel extension.

The bounded pass stops at the unresolved startup/account/authorization facts.
The user must state acceptable deployment/security conditions before `.17`
can admit a binding. If those conditions exclude the available kernel route,
record that concrete constraint before selecting a larger adapter. Do not
claim kernel audio is absent, silently resume the existing HAL, or select
DriverKit solely from deprecation. Any later feasibility, code, installation
or runtime work needs its own admitted Beads plan and red/green/stop criteria.

This pass changed research/status documentation and the Beads evidence only. Driver source, installed bundles, firmware, toolchains and security
settings were not changed. Official pages were consulted as of the research
date; no build/load experiment establishes this specific new driver's
admission on macOS 27.0.1.
