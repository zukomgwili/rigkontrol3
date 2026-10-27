# M1: NI application support for Rig Kontrol 3

**Current scope, 2026-10-27:** the user explicitly chose original native
recognition in Controller Editor and standard MIDI in Guitar Rig 6. The GR6
native-integration requirement and its blocker below are historical. Original
driver/controller/audio functionality and SIP remain required.

Publication research verified 2026-10-27. That research used first-party NI
publications only and performed no application, installer, driver, binary
recovery or device operation. A separate local UI inspection is recorded below.

**Finding: NI removed Rig Kontrol hardware integration when Guitar Rig 6
launched. A working ARM audio/MIDI driver is not evidence of native RK3
recognition in unchanged GR6.** NI's own distinction between native integration
and MIDI control matters to the retained M1 acceptance requirement.

## What NI establishes

1. **The GR6 limitation is application-side, not just an obsolete Mac driver.**
   NI's published interview identifies Tobias Fricke as Guitar Rig's Lead
   Product Manager. Discussing GR5's Rig Kontrol integration, he says:
   “we decided to remove this part when launching Guitar Rig 6.” This is a
   product-team statement on NI's official site, not a forum user's workaround.
   [NI Product Manager Q&A](https://community.native-instruments.com/discussion/16529/guitar-rig-7-pro-product-manager-answers-your-questions/p1).

2. **NI explicitly separates unsupported RK3 integration from standard MIDI.**
   NI community administrator Jeremy_NI states: “It's not supported on Guitar
   Rig 6. You should be able to use it as a standard MIDI controller.” Only
   the administrator's statement is evidence here; other users' reports on
   that page are excluded.
   [NI administrator's RK3 answer](https://community.native-instruments.com/discussion/12972/rig-kontrol-3-midi-issues),
   [administrator identity](https://community.native-instruments.com/profile/Jeremy_NI).

3. **Controller Editor 1.7.4 is the relevant legacy RK3 editor.** NI's support
   article lists RK3 among devices removed from Controller Editor starting
   with 1.8.0 and directs owners to 1.7.4 for editing MIDI properties. That
   establishes the historical software pairing; it does not establish that
   the original Intel application or its service works on this Mac.
   [NI Controller Editor compatibility guidance](https://support.native-instruments.com/support/solutions/articles/69000880035-my-native-instruments-controller-does-not-appear-in-controller-editor).

4. **Automatic MIDI-mode switching is not automatic native GR6 integration.**
   NI describes its native protocol as providing automatic integration and
   advanced operation distinct from standard MIDI. Its RK3 table specifies
   automatic MIDI-mode switching through Controller Editor. The article
   describes using MIDI mode with other NI or third-party applications; it
   does not claim GR6's removed RK3 integration is restored.
   [NI native/MIDI-mode distinction](https://support.native-instruments.com/support/solutions/articles/69000880031-native-instruments-switching-your-controller-to-midi-mode).

5. **The published downloads are legacy compatibility evidence.** NI lists
   Controller Editor 1.7.4 for Mac as a legacy-products installer dated
   2014-12-15, and RK3 driver 2.8.0 for Mac OS X 10.9–10.11 dated 2015-09-23.
   These entries do not certify Apple Silicon or current macOS support.
   [NI driver/download catalog](https://support.native-instruments.com/support/solutions/articles/69000882238-native-instruments-drivers-other-files).

## Consequence for the lift-and-shift

**Inference from the sources:** restoring NI's driver/agent behavior can target
the original compatible client contracts, but it cannot be assumed to restore
an application feature NI says it removed. The retained requirement for
unchanged GR6 native automatic recognition therefore has a concrete support
conflict. CE recognition, a visible MIDI endpoint, MIDI Learn, audio-device
selection or an older Guitar Rig succeeding cannot satisfy that GR6 criterion.
No fallback or requirement waiver is authorized by this report.

Publication research alone did not verify the exact installed GR6 build,
actual discovery/session behavior, CE 1.7.4 launch/bootstrap/trust behavior,
or recognition through our ARM implementation. The subsequent local section
records GR6 version/UI observations; native discovery/session and ARM-agent
recognition remain **unverified**. This research is not proof that every GR6 binary
lacks every latent legacy code path; it identifies NI's explicit removal and
unsupported status, not a binary audit. M1 must record the real unchanged-app
result separately and must not report native recognition from MIDI control.

No new issue, source implementation, runtime test or security change resulted
from the publication research. The local app inspection below is separate.

## Local early-recognition gate — 2026-10-27

The following observations were performed by the primary agent after recording
the gate in existing Bead `rigkontrol3-40h.2`. They are separate from the
publication-only research above.

- `csrutil status` reports SIP enabled. A property-bearing USB registry read
  finds `RigKontrol3` as an `IOUSBHostDevice`, vendor 6092 (`0x17cc`), product
  6464 (`0x1940`), with the actual NI product/manufacturer/serial properties.
  No diagnostic USB transfer, device reconfiguration or explicit service
  restart was requested. Launching GR6 can start its existing audio/MIDI
  clients; their streaming behavior was not measured in this UI inspection.
- Installed `/Applications/Native Instruments/Guitar Rig 6/Guitar Rig 6.app`
  reports **6.4.0**, build **6.4.0 (R0), Copyright © 2023 Native Instruments
  GmbH**. It contains `x86_64` and `arm64` slices. Deep/strict signature
  verification succeeds; its signer is Native Instruments GmbH,
  TeamIdentifier `83K5EG6Z9V`, signing timestamp 2023-06-20.
- The unchanged GR6 standalone launches. Its Preferences has General, Audio,
  MIDI and Library tabs. The observed MIDI page lists **Rig Kontrol 3 (DIN
  Input)**, **Native Controller Mapping (Controller)** and **Guitar Rig 6
  Virtual Input**. The first two checkboxes are off; Virtual Input is on.
  No checkbox, mapping, preset or audio setting was changed. These observed
  CoreMIDI endpoints are not evidence of NI's former native integration.
  No connected native RK3 identity/session was observed. This UI observation
  does not prove all possible binary discovery paths absent; the product's
  removed/unsupported functionality is established by NI's statements above.
- No Controller Editor installation was found under `/Applications/Native
  Instruments`. The unchanged reference app is present at
  `.build/ni/agent-feasibility/frontend-reference/controller174-18f35bcf/expanded/Controller Editor Application Update.pkg/Payload/Controller Editor.app`.
  Its public metadata reports **1.7.4 (R199)** and `x86_64`; deep/strict
  verification succeeds with the same NI team and a 2014-11-25 timestamp.
  The computer-use launch request was refused with **“Computer Use was not
  approved to use Controller Editor”**. Therefore CE launch and recognition
  are **not tested**, not failed or passed. A manual launch/result has been
  requested from the user; no shell-launch workaround bypassed that refusal.
- An existing NIHardwareAgent process was already running before GR6 was
  launched. The production ARM supporting agent is still unimplemented;
  the completed typed info/serial/message modules remain unlinked. No claim
  is made that this was a test of a completed replacement agent.

**Historical first-gate disposition:** the then-required GR6 native recognition
conflicted with NI's removal of that feature. The user subsequently resolved
this conflict explicitly as recorded at the top: native CE recognition plus
standard GR6 MIDI. That GR6 blocker is lifted; M1 is still incomplete. The GR6
process opened for the inspection was quit afterward; process inventory at
that checkpoint confirmed no GR6 or CE process remained. No new planning issue,
security change, driver installation, USB owner, timing change or recovery
change was introduced.

## Subsequent Controller Editor launch checks

The user's first Terminal attempt split the unquoted application path at a
space. The path exists; this was not an application failure. The quoted
`open -a` attempt then returned Launch Services error `-10669`. A narrowly
bounded system-log read identified the exact cause at 01:02:22.890 UTC:
Launch Services reported that Rosetta was not installed. Public SDK error
declarations did not define that number; its meaning was established by the
actual diagnostic, not guessed from a nearby error constant. Execute permission
was present, and the inspected app/executable xattrs contained provenance but
no quarantine attribute. No permission, quarantine or signature change was made.

The user then installed Rosetta following Apple's normal installation path.
[Apple's instructions](https://support.apple.com/en-us/102527). The subsequent
crash report records CE **1.7.4 (R199)** running as **X86-64 (Translated)** on
macOS **27.0.1 (26A434)** with SIP enabled. At 01:06:49 UTC it crashed with
`EXC_BAD_ACCESS`, address `0x18`, during a dyld initializer. Its selected stack
is `CoreGraphics: create_colorspace_with_colorSync_profile` →
`Controller Editor: _GLOBAL__I_a + 42` → dyld initializer calls. This is a
pre-main startup failure; the supplied stack does not implicate driver/agent
IPC. It does not identify the deeper ColorSync cause or prove that every
possible runtime environment fails.

One reversible packaging discriminator was prepared: a copy at
`~/Applications/RK3 Legacy Acceptance/Controller Editor.app`, outside the
expanded installer package. All **459** regular-file/symlink entries and modes
match the immutable reference, including executable SHA256
`ef1cee113d431ccd6946ee92631c55a6dedeb2b9a5d8ea12b150f91acc2cfad9`.
NI deep/strict signature verification passes for source and copy. Private copy
manifest SHA256:
`f62f10c3e2cf64c62e72d3d1651a29662a3d3b66e8aee137122dd51bca467145`.
The computer-use tool again refused CE access, so a single manual launch of
this copy was requested. **Its result is pending.** If the same startup crash
recurs, dependent recognition work stops at this application-runtime gate;
frontend patches, injection, security changes and repeated unchanged launches
are outside this check. No driver code has been changed.
