# Installed driver baseline and reversible quarantine

Observed on this Mac on **2026-10-24 at 21:28 UTC**, before any quarantine. These
are filesystem observations, not a claim that the drivers are loaded or that
the machine is already clean. No system driver was moved during preparation.

## Observed active-installation candidates

| Absolute path | Identity / version | Executable architecture |
| --- | --- | --- |
| `/Library/Audio/Plug-Ins/HAL/RigKontrol3.driver` | `com.rigkontrol3.sharedhal`, `4` | arm64 |
| `/Library/Extensions/NIUSBAudioDriver.kext` | `com.caiaq.driver.NIUSBHardwareDriver`, `2.8.0` | x86_64 |
| `/Library/Audio/MIDI Drivers/NIUSBMIDIDriver.plugin` | No `CFBundleIdentifier`; `CFPlugInFactories` maps `3CA12C35-1708-4F74-B74F-24A02243E197` to `NewCaiaqUSBMIDIDriver`; `2.7.0` | i386 + x86_64 |
| `/Library/PreferencePanes/NIUSBAudio.prefPane` | `com.caiaq.NIUSBAudioPreferencePane`, `2.8.0 (R36)` | x86_64 |
| `/Users/zukomgwili/src/rigkontrol3/.build/Rig Kontrol 3.app` | `com.rigkontrol3.controller`, `4` (`0.4.0`) | arm64 |

The HAL and controller app are earlier local workarounds. The NIUSB audio, MIDI
and preferences bundles are original Intel-era components. Their NIUSB scope
can include other NI devices; quarantining them removes those shared legacy
drivers as well. It does not uninstall Native Instruments applications.

The script pins the exact observed Info.plist and executable SHA-256 hashes
in addition to their identities and versions. Executable hashes:

| Executable | SHA-256 |
| --- | --- |
| `RigKontrol3SharedHAL` | `e50519e1d0184eb9bdbc095215af0996c44899b0248e020c7144824bd8d193b3` |
| `NIUSBAudioDriver` | `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08` |
| `NIUSBMIDIDriver` | `e8eea627e252287d4ae5b82e5789d3a9f789cdabb6b0700639726e48a0e99c1d` |
| `NIUSBAudio` | `cae7605e94760e35da2eeb976721b82d2d3b4355687ed22bec37fe265b3860b3` |
| `rk3controller` | `4760d4f2936220470914b60cd636abe5531ec28790dafbc31e82bd85091b447b` |

## Shared components and remaining runtime uncertainty

- `/Library/Application Support/Native Instruments/Hardware/NIHardwareAgent.app`
  exists. It is a shared NI service and is preserved.
- No readable launch plist referring to `rigkontrol`, `rig kontrol`, `niusb`,
  or `nihardware` was found in `/Library/LaunchAgents`, `/Library/LaunchDaemons`
  or `/Users/zukomgwili/Library/LaunchAgents` during the inventory.
- Process enumeration was denied by the sandbox. `sfltool dumpbtm` could not
  obtain administrator authorization. Running processes, registered login items
  and loaded driver state therefore remain **unverified**. Quitting an app does
  not establish that its login item was removed.
- Earlier controller backups and extracted installers under the original
  repository's `.build/ControllerBackups`, `.build/tmp`, and
  `.build/shared-audio/clean-source-3a604a0/.build` are preserved as inactive
  research/backup artifacts. They must not be launched for clean-baseline tests.
- Native Access, Controller Editor, NI licensing/helper services and other
  vendors' components are outside this quarantine's scope.

## Reviewable administrator procedure

`Tools/quarantine-rk3-drivers.sh` is independently authored cleanup tooling.
It never executes earlier workaround code. Run the read-only preview first:

```sh
sh Tools/quarantine-rk3-drivers.sh --inventory
```

Quit the earlier RK3 controller and audio clients before the actual move. Once
the reviewed quarantine is approved, an administrator can run:

```sh
sudo /bin/sh Tools/quarantine-rk3-drivers.sh --apply
```

This only moves present, identity-matched allowlisted bundles. A mismatched
bundle aborts the preflight before any source is moved. Backups are outside
active plugin directories at
`/Library/Application Support/RigKontrol3/Quarantine/<timestamp>-<id>/bundles/`.
The printed `SESSION` directory contains an immutable `manifest.json` and its
SHA-256 checksum. Each entry records all file contents, symlink targets,
ownership and permission bits. Before and after each move the tree is checked
against that snapshot. Same-filesystem `rename` preserves extended attributes,
ACLs, signatures and timestamps; a cross-filesystem move is refused.

The script does not remove launch/login registrations, stop processes, restart
CoreAudio, unload kernel extensions, alter firmware, or change SIP or security
settings. Restart macOS after the quarantine, then separately verify the
running/login state before calling the runtime baseline clean. A pending
login item may still reference an absent earlier app path; inspect such an
entry explicitly instead of resetting all login items.

Verify or restore using the **exact session path printed by apply**, quoted
because its parent contains spaces:

```sh
sudo /bin/sh Tools/quarantine-rk3-drivers.sh --verify "/Library/Application Support/RigKontrol3/Quarantine/<timestamp>-<id>"
sudo /bin/sh Tools/quarantine-rk3-drivers.sh --restore "/Library/Application Support/RigKontrol3/Quarantine/<timestamp>-<id>"
```

Restore verifies the manifest, every bundle and every destination before the
first move. It refuses to overwrite an existing destination. An interrupted
apply/restore can use the same restore command: bundles already at their
original locations are verified and left there. No backup is deleted. A
restart is needed before judging restored runtime behavior too.

## Fixture verification

The script supports `--staging-root /absolute/fixture` on inventory, apply,
verify and restore. Every allowlisted source and the backup base is prefixed
with that root. This mode runs without administrator rights and performs no
process/login/service inspection or actions. It rejects `/`, paths resolving
to `/`, and symlinked roots/ancestors.

Verification used isolated copies of the five installed bundles in
`/private/tmp`, with no privileged writes. It exercised all-five quarantine,
content verification, restore, repeated restore, destination collision,
quarantined-content tampering, manifest tampering, changed binary, symlink
source, and normalized-root rejection. Shell syntax and read-only inventory
also passed. These tests establish filesystem reversibility; they do not
establish a clean running system or working new ARM driver.
