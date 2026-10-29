# Native CoreMIDI plug-in signing

Reviewed 2026-10-24 against Apple documentation and the installed macOS SDK.
This concerns `RigKontrol3MIDI.plugin`, an in-process `MIDIDriverInterface2`
CFPlugIn. It is separate from a MIDIDriverKit system extension.

## Local development versus distribution

An existing, valid **Apple Development** identity is the appropriate next local
development signing test after the observed ad-hoc-signature rejection. Apple
identifies Apple Development as a development certificate, and its signing guide
separates development signatures from Developer ID Application signatures used
for independent distribution. This is support for testing that identity, not a
CoreMIDI-specific promise of acceptance. [Certificates overview](https://developer.apple.com/help/account/create-certificates/certificates-overview),
[Creating distribution-signed code for macOS](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac/).

Apple's WWDC19 security presentation explicitly warns that library validation
rejects unsigned/ad-hoc code and recommends development certificates during
local development. The same presentation distinguishes local development from
the Developer ID/notarization release workflow. Its older notarization-tool
commands are not current instructions. [All About Notarization, library validation section](https://developer.apple.com/videos/play/wwdc2019/703/?time=1948).

For distribution outside the Mac App Store, Apple's documented plug-in path is
Developer ID Application signing and notarization. Apple Development and ad-hoc
signatures are not accepted by the notary service. Quarantined plug-ins are also
subject to Gatekeeper's notarization/approval policy; a local build test does not
establish distributability. [Signing Mac Software with Developer ID](https://developer.apple.com/developer-id/),
[Notarizing macOS software before distribution](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

## Entitlements and profiles

This CFPlugIn is library code loaded into another process. Apple says to apply
entitlements to main executables, not library code, because library entitlements
can prevent loading. In-process plug-ins inherit their host's entitlements.
Adding `com.apple.security.cs.disable-library-validation` to this plug-in would
not configure MIDIServer's policy. [Creating distribution-signed code for macOS](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac/),
[Notarizing macOS software before distribution, plug-ins](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

macOS does not generally require a provisioning profile for third-party code;
a profile is required to authorize restricted entitlement claims. No restricted
entitlement or provisioning-profile requirement was found for this ordinary
CoreMIDI CFPlugIn. The SDK `MIDIDriver.h` describes the CFPlugIn interface and
installation locations without requiring a signing entitlement/profile.
[TN3125: Inside Code Signing: Provisioning Profiles](https://developer.apple.com/documentation/technotes/tn3125-inside-code-signing-provisioning-profiles).

Apple's MIDI DriverKit sample requires DriverKit entitlements and provisioning
profiles for its **dext**. That requirement is specific to that extension model
and does not supply entitlements for `MIDIDriverInterface2` CFPlugIns.
[Creating a MIDI device driver](https://developer.apple.com/documentation/mididriverkit/creating-a-midi-device-driver).

## Host policy and the evidence still required

Default Hardened Runtime library validation accepts Apple-signed code or code
with the host executable's Team ID. A third-party Apple Development or Developer
ID certificate is Apple-issued; it does **not** make that third-party code
Apple-signed or match an Apple host's Team ID. Third-party plug-in loading depends
on the host's applicable plug-in policy. No current official source located in
this review guarantees that every MIDIServer version accepts Apple Development
signatures. [Disable Library Validation entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation).

The project owner observed native MIDIServer reject the ad-hoc plug-in before
`Start`, with an AMFI unknown-certificate-chain report. An earlier successful
`CFPlugInCreate` factory smoke used our own test executable; it verifies bundle
registration and COM, not MIDIServer's signing policy. A signature that passes
`codesign --verify` is similarly necessary but does not prove the system host
will load it. The next decisive evidence is the actual MIDIServer loading the
Apple Development-signed bundle and publishing the two saved native MIDI devices.
If that fails, retain the exact new loader/AMFI reason rather than assuming a
missing plug-in entitlement.

This research performed no signing, installation, service restart, entitlement
changes, profile creation or security-setting changes.
