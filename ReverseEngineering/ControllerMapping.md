# Original RK3 controller mapping

This is a fresh analysis of Native Instruments Controller Editor 1.7.4 (R199),
not an implementation or a hardware calibration result. The original
NIHardwareAgent contains the controller-to-MIDI mapping; the original
NIUSBAudioMIDIDriver handles DIN MIDI separately. No earlier project source,
derived report, public RK3 implementation, or original executable was run.

## Evidence

Original installer: `Controller_Editor_174_Mac.dmg`, SHA-256
`18f35bcf7e35af5c30a54b99caa33868df47afb7e3f0f27eef5c97f0e8efcd9c`.
Only raw files from its previously extracted original package payloads were
read and copied into the ignored local
`.build/ni/reference/controller-editor-1.7.4` directory:

| Original artifact | SHA-256 |
|---|---|
| `NIHardwareAgent.app/Contents/MacOS/NIHardwareAgent` | `f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258` |
| Factory `Templates/Default.ncg` | `f6c7bb5edeac6d43c0914fdb5414c0dbe57a1ccde01bd8b2323cd5a83d926605` |
| Hardware `Devices/Rig Kontrol 3.ndd` | `5113d7d9de57b320b96b9442046e1f5894a313e0d88577432f9dcabf027bbab2` |

The agent Info.plist identifies `1.7.4 (R199)`. The `.ndd` contains an empty
version-1 `ni-hardware-controller` element; it supplies no mapping. The
factory `.ncg` is plain XML, map type `Guitar Rig`, name `Default`, port
`internal`. Original C++ symbols, `otool -tvV`, and Ghidra 12.2 agree on the
code paths below. Agent addresses are full original virtual addresses with
image base `0x100000000`. Selected private exports are
`.build/ni/decompiled/controller174` (149/149 functions) and
`controller174-extra` (39/39); both manifests verify the input hash and record
`cancelled=false`. Decompiled casts and indirect-call prototypes are not
treated as original source declarations.

## Confirmed: raw controls and default MIDI assignments

`RigControl3::RigControl3` (`0x100040f30`) constructs
`GuitarRig::Controller(false)` (`0x100040330`), which declares nine buttons,
nine LEDs and three pedals, and adds a `GuitarRig::MapHandler` mapping client.
The factory XML and `GuitarRig::Map::Map` (`0x100326620`) independently agree
on these assignments. All use MIDI Control Change, channel field 0
(status `B0`, the first MIDI channel), range 0–127.

| Raw input index | Original logical name | Default CC |
|---|---|---:|
| Analog 0 | `PedalPlug1` | 16 |
| Analog 1 | `PedalPlug2` | 17 |
| Analog 2 | `Pedal` | 4 |
| Digital bit 0 | `Fsw5` | 76 |
| Digital bit 1 | `Fsw6` | 77 |
| Digital bit 2 | `Fsw7` | 78 |
| Digital bit 3 | `Fsw8` | 79 |
| Digital bit 4 | `Fsw1` | 80 |
| Digital bit 5 | `Fsw2` | 81 |
| Digital bit 6 | `Fsw3` | 82 |
| Digital bit 7 | `Fsw4` | 83 |
| Digital bit 8 | `PedalSwitch` | 64 |

The nonsequential logical footswitch order is explicit: constructor table
`s_ControllerIds` at `0x1004dea10` is `{4,5,6,7,0,1,2,3}` for `Fsw1..Fsw8`.
It binds switches using controller IDs `0x10000000 | bit_index`, and pedals
using `0x30000000 | analog_index`. `ControllerBase::processButtons`
(`0x100031ef0`) reads byte `index/8`, bit `index%8`, compares cached state,
and publishes changes with that same index. `MapHandler::onSwitchEvents`
(`0x100310600`) looks up those controller IDs.

The XML specifies switch `off=0`, `on=127`, behavior literal `toogle`, reaction
`ondown`: each press toggles the output state; release does not toggle it.
The pedal XML default is 0. `Assignment::onSwitchDown/onSwitchAction`
(`0x1002fc840/0x1002fd4d0`) implement the press action and alternating state;
`Assignment::setValue` (`0x1002fc4f0`) clamps a pedal's normalized input and
rounds its scaled MIDI value. Its `MULSS` at `0x1002fc5c4` followed by
`CVTSS2SD/CVTSD2SI` at `0x1002fc5cb/0x1002fc5cf` establishes Float32 scaling
and the normal nearest-even integer tie rule. These are profile defaults,
not claims that the physical report itself contains MIDI bytes.

## Confirmed: report transport and pedal calibration

The original kernel `commandReplyDispatch` (`0x20e0`, audio binary and hash
identified in `AudioDriver.md`) sends command-3 payload bytes to the analog
user-client path and command-4 payload bytes to the digital path. It copies
16 analog bytes or eight digital bytes beginning after the command byte.
The agent's `FX2Controller::processAnalogInputs` (`0x1000394c0`) passes false
as the final argument to `processPedals` (`0x100039a20`), selecting big-endian
16-bit input pairs. Only the configured three pedal pairs are processed.
Changed calibrated results become indexed float events;
`MapHandler::onPedalEvents` (`0x100311080`) applies the profile assignment.

`FX2Pedal` construction (`0x1000205b0`) defaults to raw maximum 4095,
automatic calibration enabled, tip sampling enabled, switch mode disabled,
uninitialized endpoints -1 and normalized-change threshold 0.005. The RK3
constructor overrides analog index 2 to maximum 1023 and disables its
automatic calibration. External analog indices 0/1 retain maximum 4095 and
automatic calibration. Tip/switch options and saved endpoints are read from
user preferences; no preference override for those raw maxima was recovered.

`FX2Pedal::calculate` (`0x100020680`) clamps raw input to its maximum and
ignores repeated raw values. It divides by that maximum. The first changed
sample initializes missing endpoints/state and emits no event. Automatic or
explicit calibration expands the normalized minimum and maximum; explicit
calibration emits no pedal events until stopped. For continuous pedals, with
normalized input `x`, minimum `lo`, maximum `hi`, and `s=hi-lo`, the recovered
interior conversion is:

```
y = (x - lo - 0.025*s) / (0.95*s)
```

It snaps below 0.005 to 0 and above 0.995 to 1, suppresses interior changes
of at most 0.005, and requires span at least 0.1 to emit continuous output.
The routine also has a switch-mode branch, selected by the per-pedal option;
the default profile uses continuous pedals. Constants 0.05, 0.5, -0.025,
0.95, 1 and 0.1 at `0x100460418..0x100460440` were independently read from
the Mach-O data, rather than inferred from decompiler symbol names.

`recalibrate` (`0x100020a60`) resets endpoint/state tracking and enters
explicit calibration; `stopCalibration` (`0x100020a90`) clears that state.
`readPedalValues/savePedalValues` (`0x100039d60/0x100039f60`) load/store
per-index preference keys under `Rig Kontrol 3 Pedal <index> Low`, `High`,
`Tip`, and `Switch`. Endpoints are normalized floating-point values. No
particular user's saved endpoints were read or assumed.

Before event subscription, `GuitarRig::Controller::postConnect`
(`0x100040730`) calls configuration. `FX2Controller::getDeviceConfiguration`
(`0x100038a90`) starts from 32 zero bytes, sets byte 0 to 2, byte 1 to a mask
of pedals sampling ring rather than tip, and byte 2 to a switch-mode mask.
All-tip/non-switch defaults therefore produce `[02,00,00,...]`, 32 bytes.
`FX2Device::writeConfiguration` (`0x1002f1e00`) uses synchronous user-client
selector 10; kernel `genericMethodDispatchAction` (`0x8bc0`) routes it to
`setThresholds` (`0x2930`), USB command `0x0f` with the same payload.

`FX2Device::OSImpl::subscribeToEvents` (`0x1002f0d20`) registers timestamped
digital async selector 8 and analog async selector 7, then invokes synchronous
selector 5 with three scalars `[1, analog_threshold, ERP_threshold]`.
`FX2DeviceFeatures` construction (`0x1002ec860`) sets ERP threshold 5 at
`+0x21` and analog threshold 2 at `+0x23`; the RK3 path retains both defaults.
Kernel selector 5 calls `setAutoMsg` (`0x2e80`), whose instructions
`0x2e88..0x2ea0` send command `0x0b` and those three byte values. The exact
automatic-report enable is therefore `[0b,01,02,05]`, after the configuration
write and subscriptions. This additional command belongs to the controller
startup, not the audio-only startup recovered in `AudioDriver.md`.

## Confirmed: LEDs and displays

`GuitarRig::MapHandler::updateController` (`0x100328150`) looks up switch
assignments for raw indices 0–8 and sets the corresponding LED to 127 when
enabled and `Assignment::isButtonLEDOn` says on, otherwise 0. It requests an
update only when a level changes. Thus the LED policy follows assignment
state, including toggled state, rather than unconditionally mirroring each
physical press. `GuitarRig::Map` temporarily disables the generic
LED-on-physical-down default while constructing its assignments.

`GuitarRig::Controller::updateLEDs` (`0x100040750`) writes only when connected
and forced or dirty. It starts with an eight-byte zeroed payload: bytes 0–3
are display storage bytes in reverse order, byte 4 is LED bits 0–7, byte 5
bit 0 is LED 8, and bytes 6–7 stay zero. All nonzero LED levels become 1 bits.
`setLEDDisplayContent` (`0x100040810`) accepts display indices 0–3, stores at
`3-index`, and marks dirty only for a changed byte.

The write chain is `FX2Device::writeDigitalIO` (`0x1002f1d70`), synchronous
user-client selector 2 with eight struct bytes and scalar actual length 8,
kernel dispatch `0x8bc0`, `NIUSBAudioDevice::writeIO` (`0x27a0`), then USB
command **5** with that eight-byte payload. It is not the analog-I/O LED
method/command 12. This eight-byte controller update is distinct from the
six-byte RK3 audio initialization recorded in `AudioDriver.md`.

## Limits of the recovered specification

The profile names and raw indices above are confirmed. Physical switch
legends/orientation, pedal polarity and calibrated endpoints still need
fresh report observations on this device. The original preference-controlled
startup/focus policy can choose controller ownership. Calibration persistence
format and GUI behavior beyond the named original keys were not reconstructed.

## Fresh portable reconstruction boundary

`Driver/Controller.h/.c` implements the recovered default mappings and
continuous-pedal normalization with caller-owned state and synchronous MIDI/
LED callbacks. It has no USB, CoreMIDI, persistence or thread dependency.
Synthetic tests cover all assignments, down-only toggles, calibrated ranges,
collection, bounds, feedback, dirty LEDs and failed-output retries.

Explicit new-host policies are: first physical snapshots/reconnect snapshots
establish baselines without phantom presses or pedal events; built-in pedal
calibration starts at full normalized range 0–1 until fresh calibration is
applied; an insufficient explicit sweep restores prior endpoints; malformed
reports do not mutate state. External pedals retain automatic calibration.
Feedback on the profile's CCs updates assignment state; values at least 64
turn the associated switch LED on. LED command 5 retains startup display bytes
`00 40 40 00`, then appends the raw-index LED mask/toe bit and two zeros.
These choices are distinguished from original code behavior and are not
asserted as physical calibration results.
