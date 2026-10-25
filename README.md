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

The currently installed **0.1.3 (build 4)** is unstable. Its unity-gain guitar
check produced crackling, dropouts and intermittent controls. A separate silent
check reproduced repeated `kIOReturnIsoTooOld` errors and USB reconnections,
despite successful audio callbacks. That build used a FIFO snapshot and an
invented 4 ms output calendar; neither preserves NI's original shared ring.

The **0.2.0 (build 5)** source restores the original data flow. Host output
writes by sample position into the same mapped wire ring referenced by queued
USB requests. Capture polls primary completion state and reads the requested
ring positions without consuming a FIFO. The original 64-input/128-output
calendar, exact mode-2 byte placement, USB-anchor timestamp arithmetic and
audio-engine recovery triggers are retained. The 64 ms USB queue horizon is
not a measurement of guitar signal latency. The necessary current macOS
bindings and their limits are recorded in
[the audio reconstruction](ReverseEngineering/AudioDriver.md) and
[USB transport](ReverseEngineering/NativeUSB.md).

This source requires **macOS 26.4 or later** for explicitly kernel/user-shared
primary transaction lists; the test Mac runs macOS 27.0.1. Version 0.2.0 has
**not been installed or physically validated**. Mocks establish sample
placement, live backing identity, clock arithmetic and buffer lifetime. They
do not establish hardware DMA timing, clean sound or reliable recovery.

The native MIDI bundle retains its separate Controller source and LED Feedback
destination, alongside the hardware DIN endpoints. Earlier installed controller
checks recorded two presses of all eight switches and full pedal MIDI 0–127.
Fresh calibration measured **88–648** from 555 samples; host CC80 on/off lit and
extinguished LED 1. Those results are retained evidence, not a new combined
0.2.0 acceptance result. Incoming feedback remains explicit; applications must
send their effect states to synchronize LEDs.

These checks use standard CoreAudio and CoreMIDI clients, without the retired
app or MainStage. Sustained guitar audio, simultaneous controls, reconnect and
sleep/wake on the faithful port remain acceptance work. Physical DIN loopback,
toe/external inputs and other sample rates remain separately tracked in Beads
under `rigkontrol3-40h`. See [hardware validation](ReverseEngineering/HardwareValidation.md)
for actual installed outcomes and [the MIDI component](Driver/MIDI/README.md)
for mappings and calibration.

## Build and install

```sh
sh Tools/build-native.sh test
sh Tools/build-drivers.sh
sh Tools/install-native-drivers.sh --check
sudo /bin/sh Tools/install-native-drivers.sh --install --restart-services
```

A valid Apple-issued code-signing identity is required for the MIDI bundle.
The build selects the local identity or accepts `RK3_CODE_SIGN_IDENTITY`.
The two bundles are `RigKontrol3Audio.driver` and `RigKontrol3MIDI.plugin`.
The installer checks their identities, ARM64 code, signatures and content,
preserves previous versions, and prints a reversible session path. See
[installation and rollback](ReverseEngineering/NativeDriverInstall.md).
This personal-use build requires macOS 26.4+ and supports stereo 48 kHz. It uses no kernel extension,
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

For guided tests, keep the diagnostic client open until the physical checks are
finished, rather than relying on a short recording window:

```sh
.build/native/test/rk3-midi-monitor --live
.build/native/test/rk3-audio-client --monitor --live
```

Run these in separate terminals when testing audio and controls together. The
audio client defaults to sending guitar input 1 to both outputs with 0.1 gain
and a ±0.2 clamp. That intentionally quiet test attenuates the signal by 20 dB.
For the next listening check, lower the hardware headphone level first, then
request unity gain and a smaller buffer:

```sh
.build/native/test/rk3-audio-client --monitor --gain 1 --buffer-frames 128 --live
```

Explicit `--gain` accepts a finite decimal from 0 to 1 and uses a ±0.95 clamp;
the client prints the selected gain, limit and actual CoreAudio buffer size.
The driver has not yet passed a listening test with these settings.
Each client prints its PID. `kill -USR1 <PID>` prints cumulative statistics
without stopping; Control-C stops the client cleanly. The timed `--seconds`
option remains available. With no MIDI applications or test clients open,
MIDIServer may exit; a test client closing is not evidence of USB failure.

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
