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

The previously installed **0.1.3 (build 4)** failed acceptance. Its unity-gain
guitar check produced crackling, dropouts and intermittent controls. A separate
silent check reproduced repeated `kIOReturnIsoTooOld` errors and USB reconnections,
despite successful audio callbacks. That build used a FIFO snapshot and an
invented 4 ms output calendar; neither preserves NI's original shared ring.

The now installed **0.2.0 (build 5)** restores the original data flow. Host output
writes by sample position into the same mapped wire ring referenced by queued
USB requests. Capture polls mapped completion state and reads the requested
ring positions without consuming a FIFO. The original 64-input/128-output
calendar, exact mode-2 byte placement, USB-anchor timestamp arithmetic and
audio-engine recovery triggers are retained. The 64 ms USB queue horizon is
not a measurement of guitar signal latency. The necessary current macOS
bindings and their limits are recorded in
[the audio reconstruction](ReverseEngineering/AudioDriver.md) and
[USB transport](ReverseEngineering/NativeUSB.md).

This source requires **macOS 26.4 or later** for explicitly kernel/user-shared
transaction lists; the test Mac runs macOS 27.0.1. All 12 installed
build-5 files match the frozen manifest, and both bundle signatures verify.
A 20-second standard CoreAudio run with silent output and a verified 128-frame
buffer completed 7526 callbacks and 963328 input frames, then stopped cleanly.
A parallel 25-second, 10 Hz status watch reported generation 1 and error 0 at
every sample through idle, running and idle transitions.

A subsequent dry guitar check at unity gain and 128 frames passed the user's
first home-practice listening check: clear, comfortably audible guitar,
acceptable delay and no heard crackles or dropouts. All eight switch LEDs
worked reliably while playing; incoming feedback also produced the requested
odd/even LED pattern. The full status log nevertheless revealed **five
unsolicited audio-engine restarts with `kIOReturnIsoTooOld` before the deliberate
USB unplug**, while USB generation stayed 1. Sustained acceptance remains open
as P1 `rigkontrol3-40h.11`; clear listening and successful callbacks do not make
that run error-free. This is a subjective physical pass, not a measured
round-trip latency or proof of zero signal loss. No effects host was tested.

The native MIDI bundle retains its separate Controller source and LED Feedback
destination, alongside the hardware DIN endpoints. The build-5 combined check
recorded all eight switch CCs at 0 and 127 and the pedal across MIDI 0–127.
Saved calibration **88–648**, measured earlier from 555 samples, was independently
read back from the Controller device. Incoming feedback remains explicit;
applications must send their effect states to synchronize LEDs.

These checks use standard CoreAudio and CoreMIDI clients, without the retired
app or MainStage. Held-switch USB recovery passed without an extra toggle,
and status confirmed a new connection generation 2. Resolving the steady-state
restarts, longer operation and sleep/wake remain acceptance work. Physical DIN
loopback, toe/external inputs and other sample rates remain separately tracked in Beads
under `rigkontrol3-40h`. See [hardware validation](ReverseEngineering/HardwareValidation.md)
for actual installed outcomes and [the MIDI component](Driver/MIDI/README.md)
for mappings and calibration.

The **0.2.1 (build 6) source** corrects a deterministic completion-order
bug in the modern USB binding. Accepted requests now deliver their ISO
callbacks in order for each pipe, matching the order required by NI's ring
traversal. Raw callbacks may arrive out of order; the binding retains their
storage until ordered delivery. A production-engine regression demonstrates
the previous stall and sustained progress with the ordered contract. This
changes neither the NI queue calendar nor sample placement. Temporary,
failure-only timing observations will distinguish the remaining live
`IsoTooOld` cause; **build 6 is not installed or physically validated yet**.
The ordering fix is not claimed to resolve that live error without testing.

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
The first dry listening check passed with these settings; effects-host integration remains untested.
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
