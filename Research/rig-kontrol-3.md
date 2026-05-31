# Rig Kontrol 3 Research Log

Date: 2026-05-30

## Local Platform

- macOS: 26.5 (`25F71`)
- Architecture: `arm64`
- Xcode: 26.5 (`17F42`)
- Swift: 6.3.2, target `arm64-apple-macosx26.0`

## Attached Device Evidence

The attached device appears in IORegistry under the USB plane as `RigKontrol3@00100000`.

Verified properties:

- Vendor: Native Instruments
- Product: RigKontrol3
- `idVendor`: `6092` decimal, `0x17cc` hex
- `idProduct`: `6464` decimal, `0x1940` hex
- `bcdDevice`: `13`
- `bcdUSB`: `512` decimal, USB 2.0
- `UsbLinkSpeed`: `480000000`
- `bDeviceClass`: `255`
- `bDeviceSubClass`: `255`
- `bDeviceProtocol`: `255`
- `bNumConfigurations`: `1`
- Serial: `SN-s6w3bsru`
- `IOServiceDEXTEntitlements`: `com.apple.developer.driverkit.transport.usb`

No USB interface or endpoint children were visible in the captured IOUSB subtree. That likely means the device is present at the USB device level but not configured by a loaded matching driver on this system. The next implementation step is a native probe that can repeatedly verify this and prepare for app-level USB access.

## Visibility Checks

- `system_profiler SPUSBDataType` did not show a matching Rig Kontrol 3 block, even though IORegistry did.
- `ioreg -r -c IOHIDDevice` found no Rig Kontrol 3 / Native Instruments match.
- `ioreg -r -c IOMIDIDevice` found no Rig Kontrol 3 / Native Instruments match.
- `ioreg -r -c IOAudioDevice` found no Rig Kontrol 3 / Native Instruments match.
- `systemextensionsctl list` found no Native Instruments / Rig Kontrol system extension match.

These checks support an app-level vendor USB path as the likely next access route. CoreMIDI-only and IOHID-only paths are not supported by the current observed system state.

## Installed Native Instruments Kext

`/Library/Extensions/NIUSBAudioDriver.kext` is installed.

Important metadata:

- `CFBundleIdentifier`: `com.caiaq.driver.NIUSBHardwareDriver`
- `CFBundleShortVersionString`: `2.8.0 (R36)`
- `DTSDKName`: `macosx10.9`
- `IOKitPersonalities.RK3.idVendor`: `6092`
- `IOKitPersonalities.RK3.idProduct`: `6464`
- `IOKitPersonalities.RK3.IOProviderClass`: `IOUSBDevice`
- Binary architecture: `Mach-O 64-bit kext bundle x86_64`
- `kmutil showloaded` did not show a Native Instruments / NIUSB / Rig Kontrol match.

This verifies that the installed NI driver knows about Rig Kontrol 3 but is not an Apple Silicon driver.

## Public Precedent Used For Behavior Only

- Native Instruments driver downloads list Rig Kontrol 3 Mac drivers only for old Mac OS X versions up to `10.9 - 10.11`.
- Native Instruments support identifies Rig Kontrol as a legacy device requiring specific drivers.
- A Native Instruments community post describes a macOS control-only app that handles Rig Kontrol 3 switches and pedal as MIDI, explicitly without audio-interface support.
- `Timebutt/rig-kontrol-web` describes a browser driver that reads Rig Kontrol 3 and maps buttons/expression to MIDI output.
- `rstets/rigkontrol3-linux-midicontroller` maps Rig Kontrol 3 footswitches to MIDI notes and expression pedal data to CC 11. Its udev rule also identifies `17cc:1940`.

Implementation in this repository should be original unless a compatible license is confirmed for any external code.

## Current Access Decision

1. CoreMIDI-only: not viable from current checks because no existing MIDI device was visible.
2. IOHID-only: not viable from current checks because no HID device was visible.
3. App-level vendor USB: current route. libusb can open the device, enumerate descriptors, claim interface 0 alt 0, and read packets from bulk IN endpoint `0x81`.
4. DriverKit: fallback only if app-level USB cannot reliably read the control data or cannot be shipped with acceptable permissions.

## SDK And Dependency Notes

- Swift can import `IOKit`, `IOKit.usb`, and `IOUSBHost` on this system.
- `libusb` is installed and discoverable with `pkg-config` as version `1.0.30`.
- Current compile/link flags from `pkg-config --cflags --libs libusb-1.0`: `-I/opt/homebrew/Cellar/libusb/1.0.30/include/libusb-1.0 -L/opt/homebrew/Cellar/libusb/1.0.30/lib -lusb-1.0`.

The implementation adds a libusb descriptor probe first because it can enumerate device configurations/interfaces/endpoints from app-level code without committing to DriverKit.

## libusb Descriptor Probe Result

`swift run rk3probe` can open the attached device with libusb and enumerate descriptors.

Observed descriptor details:

- Open status: `LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED`
- Speed: high
- Configuration 1: attributes `128`, max power `250mA`
- Interface 0 alternate setting 0: vendor-specific `255/255/0`
	- Endpoint `0x01`: OUT bulk, max packet `512`, interval `0`
	- Endpoint `0x81`: IN bulk, max packet `512`, interval `0`
- Interface 0 alternate setting 1: vendor-specific `255/255/0`
	- Endpoint `0x01`: OUT bulk, max packet `512`, interval `0`
	- Endpoint `0x81`: IN bulk, max packet `512`, interval `0`
	- Endpoint `0x82`: IN isochronous, max packet `512`, interval `1`
	- Endpoint `0x06`: OUT isochronous, max packet `512`, interval `1`

Working assumption for the next probe: control-surface events are most likely on the bulk IN endpoint `0x81`; the isochronous endpoints are likely audio-related and remain out of scope for the first milestone.

## libusb Bulk Read Probe Result

`swift run rk3probe --read-bulk --seconds 3` successfully performed a bounded read from endpoint `0x81`.

Operation statuses:

- Open: `LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED`
- Set configuration 1: `LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED`
- Claim interface 0: `LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED`
- Set alternate setting 0: `LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED`
- Release interface 0: `LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED`

Baseline packets received without a controlled switch/pedal test:

```text
03 00 00 00 00 00 64 04 00 00 a2 af 7b ff cf 9c df fd ed ff 1d 9f 59 7f ff 13 5c 5d de be 09 ff 56
03 00 00 00 00 00 54 04 00 00 a2 af 7b ff cf 9c df fd ed ff 1d 9f 59 7f ff 13 5c 5d de be 09 ff 56
03 00 00 00 00 00 53 04 00 00 a2 af 7b ff cf 9c df fd ed ff 1d 9f 59 7f ff 13 5c 5d de be 09 ff 56
03 00 00 00 00 00 54 04 00 00 a2 af 7b ff cf 9c df fd ed ff 1d 9f 59 7f ff 13 5c 5d de be 09 ff 56
03 00 00 00 00 00 54 04 00 00 a2 af 7b ff cf 9c df fd ed ff 1d 9f 59 7f ff 13 5c 5d de be 09 ff 56
```

The only observed byte variation in this uncontrolled capture was around byte offset 6 (`0x64`, `0x54`, `0x53`, `0x54`, `0x54`). This must not be treated as a final expression-pedal mapping until a controlled capture is done.

`rk3probe --read-bulk` now prints each packet with a packet number, relative timestamp, byte length, and `changes:` line. Change offsets are zero-based and compare each packet to the immediately previous packet.

`rk3probe --guided-capture --step-seconds 4` runs labeled capture windows for baseline, switches 1 through 9, the pedal toe switch, and an expression sweep. Each window uses the same packet and `changes:` output as `--read-bulk`. In guided mode, the first packet in a labeled window compares against the last packet from the previous window when one exists. Hold each switch through its full labeled window and sweep expression slowly through the full expression window.

Add `--capture-log Captures/rk3-guided.jsonl` to write one JSON object per packet as JSON Lines. Each record includes schema version, step index/number/count, step label/instruction, packet index/number, timestamp, relative time, endpoint, length, byte values, hex bytes, and changed offsets with previous/current values.

`rk3probe --analyze-capture Captures/rk3-guided.jsonl` reads the JSONL capture log and summarizes packet counts, changed packet counts, offsets, value transitions, candidate offsets, switch-report masks, missing expected guided steps, and decoded switch/expression events by label. The analyzer recognizes the full guided capture plan, the focused remaining-control verification plan, and the stricter switch 2/9 verification plan.

First full guided capture result on 2026-05-30: `Captures/rk3-guided.jsonl` contained 26 records, all 12 expected steps, and no missing windows. It was not sufficient to decode controls because switch, toe-switch, and expression steps showed either no changes or only offset `6`, which also changed during baseline.

Second guided capture result on 2026-05-30: `Captures/rk3-guided.jsonl` contains 343 records, all 12 expected steps, and no missing windows. Packet format evidence from this capture:

- 8-byte packets beginning with `0x04` carry switch state in byte offset `1`.
- Verified switch-mask bits: switch 1 `0x20`, switch 3 `0x40`, switch 4 `0x80`, switch 5 `0x01`, switch 6 `0x02`, switch 7 `0x04`, switch 8 `0x08`.
- Bit `0x10` was observed during the baseline window and is likely a switch bit, but it was not verified during the labeled switch 2 window and is not enabled in the default parser.
- No switch-mask evidence verified switch 9 or the toe switch in this capture.
- 33-byte packets beginning with `0x03` carry expression state in bytes offsets `5...6`, interpreted as a big-endian raw value.
- Expression movement observed raw values around `76...628`; later focused expression captures widened this range.

Focused remaining-control capture result on 2026-05-30: `Captures/rk3-remaining-controls.jsonl` contains 103 records across all 4 focused steps. Findings:

- Switch 2 window emitted one 8-byte switch report with mask `0x20`, which is already verified as switch 1. It did not emit bit `0x10`.
- Switch 9 window emitted one 8-byte switch report with mask `0x00`; expression raw values moved from `83...682`. No switch-mask bit was verified.
- Toe-switch window emitted no 8-byte switch report; expression raw values moved from `611...679`. No switch-mask bit was verified.
- No new default switch mappings should be added from this capture. Keep bit `0x10`, switch 9, and toe switch unmapped unless a later focused capture produces clear switch-mask evidence.

Switch 2 and switch 9 are now the next blocking hardware-verification task before live bridge work. Use:

```bash
swift run rk3probe --switch-2-9-capture --step-seconds 4 --capture-log Captures/rk3-switch-2-9.jsonl
swift run rk3probe --analyze-capture Captures/rk3-switch-2-9.jsonl
```

This plan captures baseline, a switch 1 reference, release windows, two switch 2 passes, and two switch 9 passes. Treat a mapping as verified only if the same target switch produces a repeatable non-zero switch-report mask in both target windows and does not conflict with the switch 1 reference.

For a clean all-switch rerun, use the release-window capture:

```bash
swift run rk3probe --all-switches-capture --step-seconds 4 --capture-log Captures/rk3-all-switches-clean.jsonl
swift run rk3probe --analyze-capture Captures/rk3-all-switches-clean.jsonl
```

This captures switch 1 through switch 8, then switch 9 as expression-pedal toe-down closed, with a release window after every switch.

Clean all-switch release-window capture result on 2026-05-30: `Captures/rk3-all-switches-clean.jsonl` contains 276 records, all 19 expected steps, and no missing windows. Switch-report evidence:

- Switch 1 emitted mask `0x10`.
- Switch 2 emitted mask `0x20`.
- Switch 3 emitted mask `0x40`.
- Switch 4 emitted mask `0x80`.
- Switch 5 emitted mask `0x01`.
- Switch 6 emitted mask `0x02`.
- Switch 7 emitted mask `0x04`.
- Switch 8 did not emit a switch-report mask in this clean all-switch pass, so it was rerun in `Captures/rk3-switch-8.jsonl`; the focused capture verified mask `0x08` with press and release.
- Switch 9 toe-down emitted no switch-report mask; it produced expression movement only. Do not add a switch 9 mapping from current evidence.

Parser decision from the clean and focused passes: default switch map is now switch 1 `0x10`, switch 2 `0x20`, switch 3 `0x40`, switch 4 `0x80`, switch 5 `0x01`, switch 6 `0x02`, switch 7 `0x04`, switch 8 `0x08`; switch 9 remains unmapped.

For a focused expression-pedal rerun, use:

```bash
swift run rk3probe --expression-capture --step-seconds 6 --capture-log Captures/rk3-expression.jsonl
swift run rk3probe --analyze-capture Captures/rk3-expression.jsonl
```

This plan captures baseline, heel hold, toe hold, heel-to-toe sweep, and toe-to-heel sweep. Use the analyzer's expression report raw min/max output to decide whether `ExpressionCalibration` should be updated.

Focused expression capture results on 2026-05-30:

- `Captures/rk3-expression.jsonl` recorded 121 expression reports, but the toe-hold window was missing after a transient `LIBUSB_ERROR_IO`; observed raw range was `64...660`.
- `Captures/rk3-expression-clean.jsonl` recorded 221 expression reports across all 5 expected windows with no missing steps; observed raw range was `71...656`.
- Default `ExpressionCalibration` now uses the combined observed focused range `64...660` mapped to MIDI `0...127`.

## Live MIDI Bridge Smoke Test

`rk3bridge` is the live controller bridge command. It creates a CoreMIDI virtual source, opens USB interface `0` alt setting `0`, reads bulk endpoint `0x81`, parses packets through `RigKontrolPacketParser`, maps verified events through `RigKontrolMapping`, and publishes MIDI with `CoreMIDIVirtualSource`.

Launch command:

```bash
swift run rk3bridge
```

The default switch mode is `latch`: a physical switch press toggles the MIDI note state, and the physical release sends no MIDI. This is intended for Guitar Rig 6 learned effect toggles. Use `--switch-mode momentary` for the original press `noteOn` / release `noteOff` behavior, or `--switch-mode trigger` for press-only `noteOn` output.

LED protocol evidence from the Linux `snd-usb-caiaq` driver:

- Rig Kontrol 3 is supported under Native Instruments VID/PID `0x17cc:0x1940`.
- EP1 command `0x05` is `WRITE_IO`.
- Commands are sent to USB bulk OUT endpoint `0x01` as `[command] + payload`.
- Rig Kontrol 3 raw switch LED controls are bit positions `32...40` in the output control state. On the attached unit, physical switch labels 1...4 map to payload byte `4` bits `4...7`, physical switch labels 5...8 map to payload byte `4` bits `0...3`, and the pedal LED maps to payload byte `5` bit `0`.
- The Linux startup path writes payload bytes `00 40 40 00` for the Rig Kontrol 3 seven-segment display dashes; this bridge preserves those display bytes in its LED state payload.

The bridge now sends `WRITE_IO` LED state packets by default. Normal bridge shutdown and bounded LED tests send a final all-off switch LED state before releasing USB, so latched effect LEDs do not remain lit after the bridge exits. Use `--leds off` to disable LED writes, `--led-test 1 --seconds 3` to light one switch LED briefly, and `--led-test off --seconds 1` to clear switch LEDs after an interrupted run.

Hardware LED verification on 2026-05-30 with the attached Rig Kontrol 3:

- Before physical-label remapping, `swift run rk3bridge --led-test 1 --seconds 10 --verbose` sent command `0x05` with switch LED payload byte `0x01`; the user confirmed physical switch LED 5 lit. This proved Linux control bit 0 does not correspond to printed switch label 1 on this unit.
- Before physical-label remapping, `swift run rk3bridge --led-test 5 --seconds 30 --verbose` sent command `0x05` with switch LED payload byte `0x10`; the user confirmed physical switch LED 1 lit.
- The bridge now translates printed switch labels to the verified hardware LED bit order, so `--led-test 1` sends payload byte `0x10` and live switch 1 LED follow uses the physical switch 1 LED.
- `swift run rk3bridge --led-test off --seconds 1 --verbose` sent command `0x05` with switch LED payload byte `0x00` and cleared the switch LED state successfully.

Bounded smoke-test command:

```bash
swift run rk3bridge --seconds 2 --verbose
```

Smoke-test result on 2026-05-30 with the attached Rig Kontrol 3:

- CoreMIDI source created as `RigKontrol3 Bridge`.
- USB setup succeeded: open, set configuration, claim interface, set alt, and release all returned `LIBUSB_SUCCESS / LIBUSB_TRANSFER_COMPLETED`.
- The bridge read 4 packets from endpoint `0x81` and sent 2 expression CC outputs (`bf 0b 74`, `bf 0b 72`).
- The bounded run exited cleanly after 2 seconds and released the USB interface.

## DriverKit Fallback Evaluation

Current decision: do not start DriverKit for the controller-only milestone. The app-level USB path opens the attached device, claims the vendor-specific interface, receives packets from endpoint `0x81`, and now drives a CoreMIDI virtual source through `rk3bridge`.

Verified Apple platform facts:

- DriverKit drivers are user-space app extensions delivered inside a macOS app and installed/upgraded through System Extensions.
- USBDriverKit is the DriverKit family for custom or non-class-compliant USB devices and provides USB device, interface, and endpoint access.
- A DriverKit extension requires `com.apple.developer.driverkit`, which Apple says must be requested from Apple.
- USB matching requires the `com.apple.developer.driverkit.transport.usb` entitlement, an array of dictionaries identifying supported USB devices.
- Apple documents `idVendor` and `idProduct` as integer keys for the USB transport entitlement; local SDK headers also expose `idVendor`, `idProduct`, `idProductMask`, `idProductArray`, and `bInterfaceNumber` matching property names.
- The host app needs the System Extension capability / `com.apple.developer.system-extension.install` entitlement to activate or deactivate DriverKit extensions.
- DriverKit extensions must be packaged in the app bundle under `Contents/Library/SystemExtensions`; distribution requires valid signing and notarization.
- Local Xcode contains `USBDriverKit.framework` under the DriverKit platform SDK at `DriverKit.platform/Developer/SDKs/DriverKit.sdk/System/DriverKit/System/Library/Frameworks/USBDriverKit.framework`.
- `systemextensionsctl list` currently reports `0 extension(s)`, so there is no active Native Instruments or Rig Kontrol system extension on this machine.

Trigger DriverKit work only if one of these blockers is observed and recorded:

1. `libusb_open`, `libusb_set_configuration`, or `libusb_claim_interface` fails on the target macOS configuration with an access, busy, or policy error that cannot be solved in an app-level process.
2. Controlled captures prove endpoint `0x81` is unreliable from libusb, for example missed switch transitions, repeatable non-timeout transfer failures, or loss of data after hotplug/sleep that cannot be recovered in the app.
3. The final MIDI bridge needs a system-level hardware service that must bind to the USB device before the user launches the bridge app.
4. Distribution requirements reject the app-level USB/libusb approach, and the accepted shipping route requires a DriverKit dext.

If triggered, the likely DriverKit plan is:

1. Create a macOS host app target with System Extension capability.
2. Add a DriverKit dext target using the DriverKit SDK and USBDriverKit.
3. Request an Apple entitlement group containing at least `com.apple.developer.driverkit` and `com.apple.developer.driverkit.transport.usb` for Native Instruments VID/PID `0x17cc:0x1940` (`6092:6464`).
4. Configure the dext `Info.plist` IOKit personality for the Rig Kontrol 3 USB provider and package it under `Contents/Library/SystemExtensions`.
5. Implement only the controller data path first: bind to interface 0, read bulk endpoint `0x81`, and pass decoded control events to the app/MIDI bridge. Do not add audio-interface support in this milestone.