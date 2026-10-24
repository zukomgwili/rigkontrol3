# Native system-driver installation

`Tools/install-native-drivers.sh` installs exactly these two freshly built
bundles from `.build/native/drivers/manifest.json`:

| Bundle / identity | Destination |
| --- | --- |
| `RigKontrol3Audio.driver` / `org.rigkontrol3.ni-arm.audio` | `/Library/Audio/Plug-Ins/HAL` |
| `RigKontrol3MIDI.plugin` / `org.rigkontrol3.ni-arm.midi` | `/Library/Audio/MIDI Drivers` |

It does not touch the legacy-driver quarantine, NI applications, licensing,
Intel kernel extensions, firmware or macOS security settings.

Build and inspect without administrator rights:

```sh
sh Tools/build-drivers.sh
sh Tools/install-native-drivers.sh --check
```

The build selects a single valid Apple-issued signing identity from the local
keychain, preferring Developer ID Application then Apple Development. Set
`RK3_CODE_SIGN_IDENTITY` to choose explicitly. An Apple Development identity is
used for this Mac’s local build; its actual MIDIServer acceptance is tested
after installation. The observed MIDI host rejected ad-hoc signing before
calling Start. No library entitlements or provisioning profile are added. The installer verifies exact
bundle/executable identifiers, BNDL type, arm64-only Mach-O architecture,
strict code-signature integrity, and the manifest's exact file set and
SHA-256 hashes. It refuses symlinks and special files. It copies both
bundles into a private staging session and repeats those checks before
moving either active path. Installed ownership is root:wheel, directories
0755, regular resources 0644, and the executable 0755.
New production MIDI artifacts require an Apple-issued certificate chain.
Historical same-namespace active/backup versions retain strict signature
integrity, identity and architecture checks without that new-artifact
certificate requirement, so the initial ad-hoc version remains recoverable.
Protected backup and destination directory ancestors must be root-owned
and must disallow group/other writes before privileged mutations proceed.

After the concrete built artifacts and installer are reviewed, close audio
and MIDI applications and run this one administrator step:

```sh
sudo /bin/sh Tools/install-native-drivers.sh --install --restart-services
```

Only explicit real installation permits `--restart-services`. It signals
CoreAudio and the console user's MIDIServer to reload; it does not signal
other users' MIDI servers. Without that flag, the installer prints a reload
reminder. The user enters any administrator password directly; the tool
does not collect or store credentials.

Every install prints a `SESSION` path under
`/Library/Application Support/RigKontrol3/NativeDriverBackups/`. Its immutable
manifest and checksum record the new bundle snapshots and any previously
installed versions in the same two namespaces. Atomic same-filesystem
renames preserve previous ownership, permissions, ACLs and extended
attributes. A process lock prevents overlapping installs/restores.

Verify or restore using that exact printed path:

```sh
sudo /bin/sh Tools/install-native-drivers.sh --verify-session "/Library/Application Support/RigKontrol3/NativeDriverBackups/<session>"
sudo /bin/sh Tools/install-native-drivers.sh --restore "/Library/Application Support/RigKontrol3/NativeDriverBackups/<session>"
```

Restore validates both destinations and all backups before its first move.
It preserves the new bundles under the session's `removed/` directory and
restores previous versions, or returns a fresh installation to absence.
It refuses changed/colliding active bundles and never overwrites a later
version. Restore can recover an interrupted mixed-state installation and
can be repeated. No backup is deleted. Reload the audio/MIDI servers or
restart macOS before checking the restored runtime.

## Fixture verification

```sh
/usr/bin/python3 Tools/test-install-native-drivers.py
```

This creates minimal, locally signed ARM64 fixture bundles in `/private/tmp`
and passes an existing `--staging-root` to the installer. It does not use or
install the real driver binaries. The staging root prefixes every system
destination and backup path, bypasses administrator ownership only inside
that fixture, and rejects service restart requests.

The checks cover fresh install, signature/architecture/identity/content
validation, normalized ownership and modes, repeated restore, upgrades and
reverse-order restore, interrupted upgrade recovery, journal corruption,
concurrent mutation refusal, changed active drivers, content-hash tampering,
invalid signature despite a refreshed manifest, foreign signed identity,
symlink source/destination refusal, writable-ancestor refusal, missing
retained backups, and legacy quarantine preservation. Passing these
tests establishes installer behavior; working system audio/MIDI requires
separate hardware and host validation after the real installation.
The suite also requires nonfixture read-only `--check` to reject an ad-hoc
MIDI artifact and checks that the Apple-certificate rule applies to new
source/stage/target validation while historical versions remain restorable.
