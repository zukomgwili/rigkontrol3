# Shared audio/controller HAL

This is the new, independent shared-owner driver. It adapts the pinned native
`rk3-coreaudio` sources described in [NOTICE.md](NOTICE.md); the older `HAL/`
experiment and its install scripts remain separate. First audio support is
48 kHz stereo. Hardware audio quality, long sessions and recovery require their
own validation; a successful build does not establish those results.

The HAL owns RK3 USB interface 0, alternate setting 1. The menu app uses the
CoreAudio property interface in [CONTROL-PROTOCOL.md](CONTROL-PROTOCOL.md), so
audio and controls share one USB owner. The app's existing CoreMIDI source and
feedback destination remain `RigKontrol3 Bridge`, with their stable IDs and
channel-16 CC mappings. No upstream MIDI helper is needed. Direct USB mode is
reserved for the app's explicit `--controller-only` diagnostic option and must
not run alongside the shared driver.

## Build without installing

From the repository root on an Apple silicon Mac with Xcode command-line tools:

```sh
Scripts/build-rk3sharedhal.sh
```

The output is `.build/RigKontrol3SharedHAL/RigKontrol3.driver`, version
0.4.0/build 4, bundle ID `com.rigkontrol3.sharedhal`, executable
`RigKontrol3SharedHAL`. It is arm64, targets macOS 13 or later, links only
system frameworks/libraries and is signed ad hoc. The build validates those
properties and includes the exact adapted sources, license, notice and scripts
inside `Contents/Resources/SharedHALSource`. Nothing is installed or launched.

Quit the controller app before rebuilding its app bundle:

```sh
Scripts/build-rk3controller.sh
```

That builder preserves its previous output under `.build/ControllerBackups/`
before replacing `.build/Rig Kontrol 3.app`. Keep that backup with the HAL
transaction if you need to return to the previous controller-only setup.

Run the hardware-free C validation separately:

```sh
Scripts/check-rk3sharedhal.sh
```

It exercises the actual HAL property hooks, bounded control queues, fake-USB
engine lifecycle and pure audio codec with address/undefined-behavior sanitizers.

## Reviewed installation and rollback

Build, inspect and test the artifact before installation. Quit the controller
app and close any audio session using the RK3. The explicit command below
replaces the RK3 HAL and restarts `coreaudiod`, interrupting active Mac audio:

```sh
sudo Scripts/install-rk3sharedhal.sh install
```

The installer accepts only a signed arm64 RK3 bundle. It refuses another RK3
bundle in the same HAL directory. Other HAL plug-ins and Native Instruments
software are preserved. There is only one installed RK3 path:
`/Library/Audio/Plug-Ins/HAL/RigKontrol3.driver`.

Before replacing that path, the installer copies and verifies the existing
bundle under
`/Library/Application Support/RigKontrol3/DriverBackups/<UTC-time-and-ID>/`.
The backup is outside HAL, where CoreAudio will not load a second copy. Its
manifest records the original path, identity and executable hash. A failed
replacement restores that backup automatically; a failed audio-service restart
is reported separately. Backups are retained rather than overwritten.
Install and rollback share one temporary lock at the HAL destination, even
when different backup directories are requested.

The installer prints an exact rollback command. After quitting the controller
app and audio sessions, run it with the recorded transaction directory:

```sh
sudo Scripts/install-rk3sharedhal.sh rollback --backup '/Library/Application Support/RigKontrol3/DriverBackups/<UTC-time-and-ID>'
```

Rollback first preserves the current bundle in a new transaction, then restores
the original signed bundle and restarts `coreaudiod`. If that transaction had
no previous RK3 bundle, rollback removes the newly installed one. Restore the
previous controller app from its separate backup if returning to direct USB
control. Never leave a backup, renamed RK3 bundle or second RK3 driver inside
the HAL directory.

## Installer tests without changing system audio

Create separate temporary directories and use them explicitly:

```sh
test_root="$(mktemp -d /private/tmp/rk3sharedhal-install.XXXXXX)"
mkdir -p "$test_root/HAL" "$test_root/Backups"
Scripts/install-rk3sharedhal.sh install --staging-root "$test_root/HAL" --backup-root "$test_root/Backups"
```

Use the printed rollback command to test restoration. Staging mode never
changes ownership or restarts audio services, and refuses the system HAL path.
The build and these copy/rollback checks do not open USB or render audio.

## Explicit native status and silent audio probe

The build also produces `.build/RigKontrol3SharedHAL/rk3sharedprobe`. Its default
operation finds the existing RK3 audio device UID, reads input/output formats
and performs a baseline `rk3i` property read. It prints an XML property list.
It does not open USB directly, start audio or change a system default:

```sh
.build/RigKontrol3SharedHAL/rk3sharedprobe
```

After the installed shared driver has been reviewed, its explicit audio option
can exercise StartIO/StopIO for one to ten seconds:

```sh
.build/RigKontrol3SharedHAL/rk3sharedprobe --audio-seconds 3
```

The probe requires 48 kHz Float32 stereo, writes silence, and reports callback
and frame counts plus the input peak. It reads the shared control property
before audio, while audio runs and after stopping, requiring `ready` state,
a positive epoch and unchanged boot/epoch identities across those reads. It
produces no tone or recording file and
does not change the default audio device. Nonzero frame counts establish a
callback path; they do not establish audible sound quality or sustained audio
reliability. Building the probe does not run it.
