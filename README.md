# Rig Kontrol 3 — native ARM driver reconstruction

Fresh Apple Silicon drivers reconstructed from Native Instruments’ original
Intel **NIUSBAudioDriver 2.8.0** and **NIUSBMIDIDriver 2.8.0**. The earlier
controller app, MainStage setup and third-party backend are retired on this
branch; their history remains on `codex/rk3-effects-controller`.

The native components use the current macOS AudioServerPlugIn and CoreMIDI
interfaces. The recovered NI USB protocol, audio packing and scheduling are
implemented independently in C. No Intel executable runs inside the new code.

## Evidence and current validation

Both original drivers were actually decompiled using Ghidra12.2, with
instruction-level cross-checks and SHA-256 provenance. The audio image contains
241 original implementation functions. The MIDI image contains 59 implementation
functions plus73 native import trampolines. Proprietary binaries and complete
C-like exports stay local under ignored `.build/ni/`.

- [Audio reconstruction](ReverseEngineering/AudioDriver.md)
- [MIDI reconstruction](ReverseEngineering/MIDIDriver.md)
- [Native USB diagnostic](ReverseEngineering/DeviceProbe.md)
- [Reversible removal of previous drivers](ReverseEngineering/InstalledBaseline.md)
- [Fresh hardware observations](ReverseEngineering/HardwareValidation.md)

The portable driver core passes strict ARM64 compilation, address/undefined
behavior sanitizers, NI-derived wire-format vectors, malformed-packet checks,
partial-transfer and ring-wrap checks. The native ARM diagnostic has configured
the cleaned device and validated its real device-info reply: **2 audio inputs,
2 outputs, wire mode2, 1 MIDI input and 1 MIDI output**.

The ARM64 audio and MIDI system-driver bundles are implemented and signed for
local installation. Real hardware tests passed five seconds of stereo48kHz
capture, silent output transfers, clock progression and clean shutdown. The
actual audio bundle also passed Initialize/StartIO/PCM/ZeroTimeStamp/StopIO
against the device. Both built plug-in factories pass COM smoke tests.

The MIDI bundle includes a separate Controller source and LED Feedback
destination, alongside the verified hardware DIN pair. Its profile follows NI's
original CC assignments and calibration formula. Source/feedback/calibration
and reconnect rules are documented in [the MIDI component](Driver/MIDI/README.md).
Physical MIDI/LED checks and installed-host acceptance remain to be performed.
Task status is tracked in Beads under `rigkontrol3-40h`.

## Build and install

```sh
sh Tools/build-native.sh test
sh Tools/build-drivers.sh
sh Tools/install-native-drivers.sh --check
sudo /bin/sh Tools/install-native-drivers.sh --install --restart-services
```

The two bundles are `RigKontrol3Audio.driver` and `RigKontrol3MIDI.plugin`.
The installer checks their identities, ARM64 code, signatures and content,
preserves previous versions, and prints a reversible session path. See
[installation and rollback](ReverseEngineering/NativeDriverInstall.md).
This personal-use build supports stereo48kHz. It uses no kernel extension,
menu-bar app, external controller bridge, or MainStage dependency.

After installation, the one-time pedal calibration utility measures fresh
endpoints and saves them on the Controller MIDI device:

```sh
.build/native/test/rk3-calibrate --seconds 20
```

Sweep through the full travel throughout the measurement. Insufficient movement
or reconnection prevents saving. Use the separate Controller MIDI source.
The original built-in pedal is CC4, switches1–4 CC80–83, and switches5–8 CC76–79,
channel1. LEDs follow the controller's toggle assignments; the feedback
destination can receive host CC values. This does not add effect-state output
to applications that do not send it.

## Reproduce the core checks

On an Apple Silicon Mac with Xcode command-line tools:

```sh
sh Tools/build-native.sh test
sh Tools/build-native.sh sanitize
```

The diagnostic defaults to cached descriptors and takes no exclusive access:

```sh
.build/native/test/rk3-device-probe --descriptors
```

Read its scope and cancellation rules before using `--query-device`. Configure
an otherwise unconfigured device only with explicit `--configure`.

## Reproduce the original-binary decompilation

Put your own original x86_64 slices in `.build/ni/reference/2.8.0/` as
`NIUSBAudioDriver.x86_64` and `NIUSBMIDIDriver.x86_64`. Their exact hashes are
checked by the tool. With Ghidra12.2 and its compatible JDK installed:

```sh
sh Tools/decompile-drivers.sh
```

`GHIDRA_HOME` and `JAVA_HOME` override the local Homebrew paths. This creates a
fresh private export with per-function C, assembly, an index and manifests.
Existing output directories are refused to avoid mixing old and new exports.
No NI binaries or full proprietary decompilations are committed to this repo.
