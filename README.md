# Rig Kontrol 3 — native ARM drivers

Native ARM64 CoreAudio and CoreMIDI drivers for Native Instruments Rig Kontrol 3,
reconstructing the behavior of the original Intel **NIUSBAudioDriver 2.8.0** and
**NIUSBMIDIDriver 2.8.0**. The implementation retains the recovered NI USB
protocol, audio ring, timing and recovery contracts through current macOS
interfaces. It uses one USB owner and works with **SIP enabled**.

The target is standard audio and MIDI operation in **Guitar Rig 6 and other
hosts**. Controller Editor and NI's private automatic application-recognition
interface are outside the current scope. The earlier controller app and
MainStage setup are retired; their committed history remains in Git.

## Current status

**Current package: 0.2.13 / build 18.** On **29 October 2026**, all twelve installed
files matched the frozen signed package. Both bundles' identities, versions,
ARM64 architecture and signatures verified; SIP remained enabled. The full
strict and address/undefined-behavior sanitizer checks passed before packaging.

This verifies the installed files. **Build 18's loaded-image identity, live
property readback and physical audio/controller checks remain pending.** Its
new programmable-display and direct-monitor operations have offline coverage;
physical display observation and direct-monitor routing still need validation.
See [package evidence](ReverseEngineering/Evidence/Build18CombinedControls.json)
and [installed-file verification](ReverseEngineering/Evidence/Build18InstalledAcceptance.json).

Earlier builds have these recorded results; they do not substitute for build 18
qualification:

| Tested build | Recorded result |
| --- | --- |
| 13 | Seven 600-second silent runs at 128 frames across all five sample rates, including three runs at 48 kHz, passed the bounded clock/transport checks. [Evidence](ReverseEngineering/Evidence/Build13StabilityRatesValidation.json) |
| 15 MIDI / 13 audio | One actual sleep/wake comparison retained Guitar Rig's wah control without restarting the host, MIDI services or USB. [Evidence](ReverseEngineering/Evidence/Build15SleepRecoveryValidation.json) |
| 16 | Clear Guitar Rig audio, audible wah, all eight switches and explicit incoming LED feedback passed. Released- and held-switch USB recovery passed. [Physical checks](ReverseEngineering/Evidence/Build16PhysicalValidation.json), [recovery](ReverseEngineering/Evidence/Build16USBRecoveryValidation.json) |
| 16 | A bounded physical DIN loopback, including fragmented SysEx, and two built-in toe-switch actions passed. The toe test ran with Guitar Rig closed and audio stopped. [DIN](ReverseEngineering/Evidence/Build16DINValidation.json), [toe](ReverseEngineering/Evidence/Build16ToeSwitchValidation.json) |

**Full NI functionality and whole-rig qualification are still incomplete.**
Remaining work includes final build 18 qualification, external pedal/footswitch
inputs, direct-monitor routing, measured latency and the original output-cleanup
contract's modern event binding. Apple support case **23183489** tracks that
binding question; no technical resolution is recorded yet. A silent transport
pass is not waveform-continuity proof or measured round-trip latency.

The existing Beads milestones track native audio (`rigkontrol3-40h.1`),
MIDI/controller operation (`.2`) and installed qualification (`.3`). The
[parity audit](ReverseEngineering/ParityAudit.md) and
[output-event binding analysis](ReverseEngineering/NIOutputBindingResolution.md)
retain the detailed limitations and historical findings.

## What the driver provides

- Two audio inputs and two outputs, with **44.1, 48, 88.2, 96 and 192 kHz**
  sample rates. The original default is **44.1 kHz**.
- Separate physical DIN MIDI input/output and a mapped **Controller** source
  with an **LED Feedback** destination.
- Eight press-latched footswitches, built-in expression pedal and toe switch,
  plus the original external pedal configuration and calibration operations.
- Original software input-gain, raw direct-monitor and indexed display-byte
  operations through the same native USB owner.

The current owner supports **one connected RK3**; multiple matching devices are
rejected. The audio bundle
uses AudioServerPlugIn and IOUSBHost; the MIDI bundle uses CoreMIDI. No kernel
extension or controller bridge is needed. Original Intel executables are not
loaded by these drivers.

## Build and install

Requirements:

- An Apple Silicon Mac running **macOS 26.4 or later**. The development/test Mac
  runs macOS 27.0.1; other Macs are not yet qualified.
- Xcode command-line tools with a macOS 26.4+ SDK providing the IOUSBHost
  shared-transaction APIs, and Python 3.
- An Apple-issued code-signing identity. The builder selects a local Developer
  ID Application or Apple Development identity; set `RK3_CODE_SIGN_IDENTITY`
  to select one explicitly. The test Mac's MIDI server rejected ad-hoc signing.

This is a personal-use development build. From the repository root, first
build and check the exact output you intend to install. Pause playback and
quit audio/MIDI applications before the final administrator command:

```sh
set -e
sh Tools/build-native.sh test
sh Tools/build-drivers.sh
rk3_driver_source="$PWD/.build/native/drivers"
sh Tools/install-native-drivers.sh --check --source "$rk3_driver_source"
sudo /bin/sh Tools/install-native-drivers.sh --install --restart-services --source "$rk3_driver_source"
```

Use freshly built output; an existing `.build/native/drivers` directory can
contain an older package. Frozen candidates mentioned in historical evidence
are local artifacts, not files supplied by a fresh clone.

The installer checks both bundles, preserves rollback copies, briefly restarts
CoreAudio/MIDI services and prints a **SESSION** path. Enter administrator
credentials in Terminal. See [installation and rollback](ReverseEngineering/NativeDriverInstall.md)
for destinations, session verification and restoration.

## Set up Guitar Rig or another host

1. Select **Rig Kontrol 3 (NI ARM)** as the audio input/output. Guitar checks
   have used input 1, RK3 headphones, 48 kHz and a 128-frame host buffer.
2. Enable the **Controller** MIDI source on **Rig Kontrol 3 Controller**.
   The separate DIN endpoints serve the five-pin sockets.
3. Use the host's MIDI Learn to assign the switches and expression pedal.
   For Cry Wah, learn its sweep from the pedal and its power control from
   switch 1. Reopen the host after a driver installation.

All mapped controls use MIDI channel **1**:

| Physical control | MIDI CC |
| --- | --- |
| Switches 1–4 | 80, 81, 82, 83 |
| Switches 5–8 | 76, 77, 78, 79 |
| Built-in expression pedal, axis 2 | 4 |
| Built-in toe switch | 64 |
| External pedal inputs, axes 0 and 1 | 16, 17 |

The eight switches toggle between 0 and 127 on each press; release does not
send another toggle. Their LEDs follow the driver's current assignment values.
Matching CC values sent to **LED Feedback** can update those values and LEDs.
A host must actually send effect states for mouse or preset changes to stay
synchronized; the driver does not add that output to Guitar Rig.

## Pedal settings and calibration

Read saved settings or measure a pedal's actual travel:

```sh
.build/native/test/rk3-pedals --show
.build/native/test/rk3-calibrate --axis 2 --seconds 20
```

Axis 2 is the built-in pedal; axes 0 and 1 are external inputs. Sweep through
full travel during measurement. **88–648** is the recorded calibration of the
development unit, not a universal default or a fresh build 18 readback.

Select the original tip/ring and continuous/switch options for a named input:

```sh
.build/native/test/rk3-pedals --axis 0 --ring --switch
.build/native/test/rk3-pedals --axis 0 --tip --continuous
```

Only that input's options change; its calibration endpoints are retained.
External input settings are implemented, but their physical qualification is
pending. The utilities require matching application status for the current
connection rather than treating a saved CoreMIDI property as success. If saving
succeeds but application fails, the requested settings may remain cached and
the utility reports failure. See [pedal contracts](ReverseEngineering/TipSwitchCalibrationContract.md).

## Display and direct monitor

Build 18 adds the original indexed display-byte setter through the existing
controller and LED writer:

```sh
.build/native/test/rk3-display --show
.build/native/test/rk3-display --index 0 --value 0
```

Indices are 0–3 and values are raw bytes 0–255; no text or segment encoding is
assumed. The result is **logical controller state**, not hardware readback or
USB completion. Requests are transient and bound to the live driver session
and connection. See [display contract](ReverseEngineering/DisplayContract.md).

The original direct-monitor query is available as:

```sh
.build/native/test/rk3-direct-monitor get
```

This sends one query through the installed owner. A returned **255 / 0xff** is
ambiguous: it may be an actual reply or NI's timeout value. Raw mode meanings
and physical routing are not yet verified, so no mode-setting example is given.
See [direct-monitor contract](ReverseEngineering/DirectMonitorContract.md).

## Guided hardware checks

Keep the diagnostic client open through the physical actions and final state
restoration. A short recording that expires cannot verify later actions:

```sh
.build/native/test/rk3-midi-monitor --live
```

For dry guitar monitoring, turn the RK3 headphone level low first, then run in
a separate terminal and raise the level gradually:

```sh
.build/native/test/rk3-audio-client --monitor --gain 1 --buffer-frames 128 --live
```

This applies no effects. Explicit `--gain 1` uses unity gain with a ±0.95 clamp;
the client prints the actual host buffer size. Without `--gain`, its deliberately
quiet monitor uses 0.1 gain and a ±0.2 clamp. Each client prints its PID;
Control-C stops it cleanly and SIGUSR1 requests cumulative statistics.
With no MIDI application or client open, MIDIServer may exit normally.

Keep one MIDI client alive across display baseline, changes and restoration;
separate CLI invocations otherwise may span different driver sessions. Save
LED state separately: display status contains only its four logical bytes.

## Reproduce the checks and research

The hardware-free native tests use mocked device/host seams. On Apple Silicon:

```sh
sh Tools/build-native.sh test
sh Tools/build-native.sh sanitize
```

The cached-descriptor diagnostic takes no exclusive device access:

```sh
.build/native/test/rk3-device-probe --descriptors
```

Read [its scope](ReverseEngineering/DeviceProbe.md) before using
`--query-device`; an unconfigured device requires explicit `--configure`.

The original drivers were decompiled with Ghidra 12.2 and cross-checked against
instructions and SHA-256 provenance. Original binaries and complete proprietary
exports remain local under ignored `.build/ni/`. To reproduce those exports,
place your original x86_64 slices in `.build/ni/reference/2.8.0/` as
`NIUSBAudioDriver.x86_64` and `NIUSBMIDIDriver.x86_64`; the tool checks their
pinned hashes. With Ghidra 12.2 and a compatible JDK installed:

```sh
sh Tools/decompile-drivers.sh
```

`GHIDRA_HOME` and `JAVA_HOME` override the local paths. The tool refuses existing
output directories to avoid mixing exports. No NI binaries or complete
proprietary decompilations are committed.

Further technical evidence:

- [Audio reconstruction](ReverseEngineering/AudioDriver.md)
- [MIDI reconstruction](ReverseEngineering/MIDIDriver.md)
- [USB transport](ReverseEngineering/NativeUSB.md)
- [Controller mapping and threading](Driver/MIDI/README.md)
- [Hardware validation history](ReverseEngineering/HardwareValidation.md)
- [SIP-enabled deployment decision](ReverseEngineering/KernelBindingDecision.md)
