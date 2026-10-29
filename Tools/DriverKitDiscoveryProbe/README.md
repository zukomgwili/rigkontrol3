# DriverKit discovery probe

This software-only service tests the exact legacy query
`IOServiceMatching("de_caiaq_driver_NIUSBAudioDevice")`. It uses DriverKit's
normal `IOClass=IOUserService` and the NI name as `IOUserClass`. It has no USB
personality, audio engine, user client, NI selectors or fabricated device data.
It leaves the RK3 and installed audio/MIDI driver alone.

The diagnostic independently locates one service using both
`RK3DiscoveryProbe=true` and
`RK3DiscoveryProbeIdentifier=org.rigkontrol3.ni-discovery-probe`. A class match
must return that same registry entry with valid identity metadata. A name-only
match or another entry is insufficient. A disappearing probe, missing identity,
ambiguous result or failed query is inconclusive. A successful null iterator
is an empty result, as specified by the installed IOKitLib.h; the offline API
fixture exercises this boundary without registry calls.

## Build and software checks

These commands do not provision, sign, activate or query the live registry:

```sh
sh Tools/DriverKitDiscoveryProbe/build-client.sh test
sh Tools/DriverKitDiscoveryProbe/build-client.sh sanitize
sh Tools/DriverKitDiscoveryProbe/build-service.sh
sh Tools/DriverKitDiscoveryProbe/build-probe.sh
```

The two Xcode schemes build the DriverKit service alone or its complete
containing app. They use the installed DriverKit template and canonical IIG
build rule. The IIG filename matches its class. No header aliases or substituted
SDK types are needed. The service builds as arm64e for DriverKit27; the host and
IOKit client build as arm64 for macOS.

`build-probe.sh` produces `Build/Products/Debug/RK3DiscoveryProbeHost.app`
inside its derived-data directory. Its embedded extension is
`Contents/Library/SystemExtensions/org.rigkontrol3.ni-discovery-probe.driver.dext`.
An unsigned build, fixture success or bundle-layout inspection proves no OS
class match or NI application compatibility.

## Development signing and activation

The host needs the system-extension-install capability; this first-stage dext
needs only base DriverKit. It requests no USB, audio or broad user-client
capability. Development profiles and a registered test Mac are required. Kext
signing and MDM are separate kernel-route requirements, not prerequisites for
this development probe. Distribution is a separate capability decision.

Normal Xcode development provisioning can be requested explicitly:

```sh
xcodebuild -project Tools/DriverKitDiscoveryProbe/DiscoveryProbe.xcodeproj \
  -scheme DiscoveryProbe -configuration Debug \
  -derivedDataPath /private/tmp/rk3-discovery-probe-development \
  -destination "platform=macOS,id=$RK3_MAC_PROVISIONING_UDID,arch=arm64" \
  -allowProvisioningUpdates -allowProvisioningDeviceRegistration \
  DEVELOPMENT_TEAM="$RK3_DEVELOPMENT_TEAM" CODE_SIGN_STYLE=Automatic \
  'CODE_SIGN_IDENTITY=Apple Development' \
  CODE_SIGNING_ALLOWED=YES CODE_SIGNING_REQUIRED=YES build
```

Set `RK3_MAC_PROVISIONING_UDID` to this Mac's Provisioning UDID from System
Information, and `RK3_DEVELOPMENT_TEAM` to the existing signing team. Keep the
device identifier private. This command may register the current Mac and create
development App IDs and profiles. Before activation, verify both bundles'
signatures, matching team, App IDs, expiry and actual profile capabilities.
Both embedded profiles must include the current Mac's **Provisioning UDID**
(or legitimately permit all devices). Valid signatures and capabilities alone
do not establish device eligibility. If Xcode reuses an ineligible profile,
follow [Apple's profile refresh guidance](https://developer.apple.com/help/account/provisioning-profiles/edit-download-or-delete-profiles/)
to preserve and remove only that cached profile before rebuilding. Do not alter
security settings. The default checked-in build scripts explicitly
disable signing and never pass provisioning flags.

The containing host defaults to `--inspect`, which only checks its bundle
layout and reports `request_submitted=false`. Only explicit `--activate` or
`--deactivate` submits a request for this probe's fixed extension identifier.
Existing extension replacement is cancelled. Actual activation requires normal
macOS approval; exiting the host is not claimed to cancel a pending OS request.
No activation occurs merely by building or opening the default scheme.
An activated/enabled extension listing does not prove its executable launched;
a missing probe remains inconclusive, with any startup audit planned separately.

Once the signed app is installed through the recorded runtime plan and macOS
has approved activation, run the separately built `discovery-client --check`:

| Exit | Meaning |
| --- | --- |
| 0 | Exact legacy class query returned the independently marked probe entry. |
| 1 | The unique probe remained visible, but that exact class query missed it. |
| 2 | Probe absent, identity changed/unreadable/ambiguous, or a query failed. |

Record the JSON, exact app/profile/source identity and macOS activation result.
Deactivate this probe after collecting the result. If macOS reports that
deactivation requires a reboot, retain the exact app and verify teardown after
a normal reboot before another activation. A zero-entry extension list alone
does not override that callback result.

## Interpretation boundary

The legacy class query is proved in the original MIDI driver, which is itself
being ported. A miss establishes that this query cannot be retained unchanged;
it does not by itself rule out an adapted ARM driver client or NI application
recognition. The original NIHardwareAgent1.7.4 also declares the RK3 filter with that exact
class string; its constructor stores the string in the field used by the
factory's class query. Full activation wiring and current NI application
recognition remain unproved. A positive class result likewise proves no
NI selector/event ABI, real-device information, audio/MIDI behavior or actual
Guitar Rig/Controller Editor acceptance.

Beads `rigkontrol3-40h.17` owns the source evidence, plans, test results and
runtime admission. The primary lift-and-shift remains incomplete.
