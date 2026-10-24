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

Version **0.1.2 (build 3)** is installed on the test Mac. Both signed ARM64
bundles match the build's 12-file manifest. CoreAudio exposes the native device
with **2 inputs and 2 outputs at 48 kHz**. Standard CoreAudio clients passed
three- and five-second capture runs with completed callbacks and clean stops;
these runs sent silence. The installed MIDI plug-in, signed with an Apple-issued
development identity, loads in MIDIServer and exposes the Controller source.
The corrected CFData-based properties also pass actual cross-process
communication between the installed audio owner and its clients.

The MIDI bundle includes a separate Controller source and LED Feedback
destination, alongside the hardware DIN endpoints. Its profile follows NI's
original CC assignments and calibration formula. A persistent CoreMIDI client
recorded two presses of each of the eight switches, with each assignment
reaching 0 and 127. The pedal reached the full MIDI 0–127 range after fresh
calibration measured **88–648** from 555 samples. The user completed the repeated
switch/pedal test without reporting faults and confirmed that host CC80 values
of 127 and 0 respectively lit and extinguished LED 1.

A roughly 207-second listening run kept audio and MIDI active together. The
user heard guitar and reported that the controls continued responding, but the
guitar was very soft and had noticeable delay. The test client's 0.1 gain
attenuated the signal by 20 dB; normal output level has not yet been established
with unity gain. The installed driver also accepted a 128-frame CoreAudio
buffer request in a separate one-second silent-output check.

The **0.1.3 (build 4) source** prepares a latency adaptation: a 4 ms output
scheduling lead replaces the recovered 64 ms lead in the native adapter, while
the NI reference scheduler remains intact. Public I/O waits for PCM readiness,
and the capture consumer discards stale backlog. A regression using the actual
ISO adapter fails against the previous behavior and passes with this change.
**Build 4 is not installed or physically validated yet.** The 4 ms scheduling
lead is not a measured total round-trip latency.

These installed tests use standard CoreAudio and CoreMIDI clients, without the
retired app or MainStage. Acceptable guitar level and latency remain open under
`rigkontrol3-40h.3`; actual reconnect recovery and physical DIN loopback also
remain unvalidated. Source/feedback/calibration and reconnect rules are
documented in [the MIDI component](Driver/MIDI/README.md), with measured results
in [hardware validation](ReverseEngineering/HardwareValidation.md).
Task status is tracked in Beads under `rigkontrol3-40h`.

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

For guided tests, keep the diagnostic client open until the physical checks are
finished, rather than relying on a short recording window:

```sh
.build/native/test/rk3-midi-monitor --live
.build/native/test/rk3-audio-client --monitor --live
```

Run these in separate terminals when testing audio and controls together. The
audio client defaults to sending guitar input 1 to both outputs with 0.1 gain
and a ±0.2 clamp. That intentionally quiet test attenuates the signal by 20 dB.
For the next level and latency test, request unity gain and a smaller buffer:

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
