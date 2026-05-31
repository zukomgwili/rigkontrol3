# RigKontrol3 MIDI Bridge

Native macOS Apple Silicon work for bringing Native Instruments Rig Kontrol 3 pedal and switch control data back as MIDI.

The first milestone is a native macOS MIDI bridge app for the controller surface only. Audio interface support is intentionally out of scope until the control path is working and verified.

## Verified Starting Point

- Target platform: macOS Apple Silicon.
- Attached device verified in IORegistry as `RigKontrol3` with Native Instruments VID/PID `0x17cc:0x1940`.
- The device is vendor-specific at the USB device level (`bDeviceClass`, `bDeviceSubClass`, and `bDeviceProtocol` are all `255`).
- The current system does not expose the device in quick HID, MIDI, or CoreAudio registry checks.
- `/Library/Extensions/NIUSBAudioDriver.kext` is installed, matches `0x17cc:0x1940` as `RK3`, and is x86_64-only. It is not loaded on this Apple Silicon system.

See `Research/rig-kontrol-3.md` for the captured evidence and links to the public precedent used to guide the implementation.

## Current Implementation

- `RigKontrol3Core`: Swift library with USB/HID/MIDI probe helpers, raw packet parsing, MIDI message construction, default mappings, and expression-pedal calibration.
- `rk3bridge`: long-running CoreMIDI virtual-source bridge for verified Rig Kontrol 3 controls.
- `rk3probe`: command-line probe for re-checking device visibility from the native app layer.
- `rk3probe --read-bulk`: bounded raw packet capture from the vendor-specific bulk IN endpoint, including packet numbers, relative timestamps, lengths, and byte-offset deltas.
- `rk3probe --analyze-capture`: offline JSONL capture summaries plus decoded switch/expression events from the conservative parser.
- Unit tests for mapping, calibration, packet parsing, and bulk packet-diff behavior.

## Verified Packet Format

The current parser is intentionally conservative and only emits events for packet fields verified by the controlled capture evidence.

- Switch reports are 8-byte packets beginning with `0x04`; byte offset `1` is a switch bitmask.
- Verified switch bits: switch 1 `0x10`, switch 2 `0x20`, switch 3 `0x40`, switch 4 `0x80`, switch 5 `0x01`, switch 6 `0x02`, switch 7 `0x04`, switch 8 `0x08`.
- Switch 9 toe-down produced expression movement but no switch-report mask in the clean all-switch capture, so switch 9 remains unmapped.
- Expression reports are 33-byte packets beginning with `0x03`; bytes `5...6` form a big-endian raw expression value.
- The default expression calibration is based on focused expression captures, raw `64...660` to MIDI `0...127`.

The descriptor probe uses `libusb` through SwiftPM's `pkg-config` integration. On a clean machine, install it with Homebrew:

```bash
brew install libusb
```

## Commands

```bash
swift build
swift test
swift run rk3bridge
swift run rk3bridge --verbose
swift run rk3bridge --switch-mode momentary
swift run rk3bridge --led-test 1 --seconds 3
swift run rk3bridge --led-test off --seconds 1
swift run rk3bridge --seconds 10 --verbose
swift run rk3probe
swift run rk3probe --read-bulk --seconds 10
swift run rk3probe --guided-capture --step-seconds 4
swift run rk3probe --guided-capture --step-seconds 4 --capture-log Captures/rk3-guided.jsonl
swift run rk3probe --all-switches-capture --step-seconds 4 --capture-log Captures/rk3-all-switches-clean.jsonl
swift run rk3probe --remaining-controls-capture --step-seconds 6 --capture-log Captures/rk3-remaining-controls.jsonl
swift run rk3probe --switch-2-9-capture --step-seconds 4 --capture-log Captures/rk3-switch-2-9.jsonl
swift run rk3probe --switch-8-capture --step-seconds 6 --capture-log Captures/rk3-switch-8.jsonl
swift run rk3probe --expression-capture --step-seconds 6 --capture-log Captures/rk3-expression.jsonl
swift run rk3probe --analyze-capture Captures/rk3-guided.jsonl
```

Run the live MIDI bridge with:

```bash
swift run rk3bridge
```

It opens USB interface `0` alt setting `0`, reads bulk endpoint `0x81`, parses verified switch and expression packets, and publishes a CoreMIDI virtual source named `RigKontrol3 Bridge`. Stop it with `Ctrl-C`. Use `--source-name "Custom Name"` to change the CoreMIDI source name, `--verbose` to print decoded events as MIDI is sent, and `--seconds 10` for a bounded smoke test.

The default switch mode is `latch`, which is intended for Guitar Rig-style effect bypass toggles: a physical press flips the MIDI output state and the physical release sends no MIDI. Use `--switch-mode momentary` if a host needs traditional press `noteOn` and release `noteOff` behavior, or `--switch-mode trigger` if it should receive `noteOn` only on each press.

The physical Rig Kontrol LEDs follow switch output by default. The LED packet format comes from the Linux `snd-usb-caiaq` Rig Kontrol 3 support and is verified on the attached unit: send command `0x05` to bulk OUT endpoint `0x01`. The raw switch LED byte is payload byte `4`; physical switch labels 1...4 map to bits `4...7`, and physical switch labels 5...8 map to bits `0...3`. The pedal LED maps to payload byte `5` bit `0`. Normal bridge shutdown and bounded LED tests send a final all-off switch LED state before releasing USB. Use `--leds off` to disable LED writes, `--led-test 1 --seconds 3` to turn on physical switch LED 1 briefly for protocol checks, and `--led-test off --seconds 1` to clear switch LEDs after an interrupted run.

## Design Direction

The implementation uses an app-first architecture:

1. Probe and log what macOS exposes for the attached hardware.
2. Keep hardware access behind a small abstraction so the eventual input reader can use CoreMIDI, HID, or vendor USB without rewriting MIDI mapping.
3. Emit MIDI through a CoreMIDI virtual source once raw control events are verified.
4. Treat DriverKit as a fallback only if app-level access cannot read the control data reliably or cannot be shipped with acceptable permissions.

DriverKit is not part of the current implementation path. Apple documents DriverKit USB support through a system extension and USB transport entitlement, and local Xcode includes USBDriverKit in the DriverKit SDK. Because `rk3probe` can already claim the vendor USB interface and read bulk endpoint `0x81`, DriverKit work should only start after a recorded app-level blocker.

## Next Hardware Step

Run a controlled capture while pressing each switch and sweeping the expression pedal:

```bash
swift run rk3probe --guided-capture --step-seconds 4 --capture-log Captures/rk3-guided.jsonl
```

To verify only the still-unconfirmed controls, run the focused capture:

For the cleanest all-switch reading, use the release-window capture:

```bash
swift run rk3probe --all-switches-capture --step-seconds 4 --capture-log Captures/rk3-all-switches-clean.jsonl
swift run rk3probe --analyze-capture Captures/rk3-all-switches-clean.jsonl
```

This captures switch 1 through switch 8, then switch 9 as an expression-pedal toe-down closed press. It inserts a release window after every switch so delayed releases do not contaminate the next switch label.

To verify only the still-unconfirmed controls, run the focused capture:

```bash
swift run rk3probe --remaining-controls-capture --step-seconds 6 --capture-log Captures/rk3-remaining-controls.jsonl
swift run rk3probe --analyze-capture Captures/rk3-remaining-controls.jsonl
```

Before live bridge work continues, use the stricter switch 2/9 capture. It repeats the target controls and includes a switch 1 reference because one earlier switch 2 window emitted switch 1's `0x20` mask:

```bash
swift run rk3probe --switch-2-9-capture --step-seconds 4 --capture-log Captures/rk3-switch-2-9.jsonl
swift run rk3probe --analyze-capture Captures/rk3-switch-2-9.jsonl
```

The guided capture runs labeled windows for baseline, footswitches 1 through 9, the pedal toe switch, and an expression sweep. Each capture window starts as soon as its step label appears. Hold each switch until the next step label appears, and sweep the expression pedal slowly for the whole expression window. The optional `--capture-log` path writes one JSON object per captured packet as JSON Lines, including the step label, packet bytes, and changed offsets. The controlled capture is needed to map byte offsets to specific switches and pedal values without guessing.

After capture, summarize the JSONL log with:

```bash
swift run rk3probe --analyze-capture Captures/rk3-guided.jsonl
```

The analyzer also reports missing expected guided steps, candidate offsets, switch-report masks, and decoded events. It recognizes the full guided plan, the all-switch release-window plan, the focused remaining-controls plan, and the stricter switch 2/9 plan. If a control does not produce a decoded event, repeat the capture for that control with longer windows and hold the control through the full labeled window.

For expression-pedal calibration, run:

```bash
swift run rk3probe --expression-capture --step-seconds 6 --capture-log Captures/rk3-expression.jsonl
swift run rk3probe --analyze-capture Captures/rk3-expression.jsonl
```

This captures baseline, heel hold, toe hold, heel-to-toe sweep, and toe-to-heel sweep. The analyzer prints expression report counts and raw min/max values for each step.

Bulk capture output uses zero-based byte offsets in its `changes:` lines. During guided capture, the first packet in each labeled window is compared with the last packet from the previous window when one exists, so cross-step changes remain visible. Perform one physical action at a time and note which candidate offsets change between the baseline packet and the next packet burst.