# Controller Editor 1.7.4: static ARM feasibility analysis

Inspected 2026-10-24 for `rigkontrol3-tw5.3`. **Useful interoperability reference; not an ARM source tree or a binary that can acquire native ARM support by changing its device list.** Its documented MIDI feedback model supports our existing design. Restoring Guitar Rig 5's proprietary integration would be a different project and would not restore integration removed from Guitar Rig 6.

## Artifact and reproducibility

- Input: [Controller_Editor_174_Mac.dmg](/Users/zukomgwili/Downloads/Controller_Editor_174_Mac.dmg), SHA-256 `18f35bcf7e35af5c30a54b99caa33868df47afb7e3f0f27eef5c97f0e8efcd9c`.
- `hdiutil imageinfo` identifies a checksummed, unencrypted, zlib-compressed read-only UDIF image. Mounted using `hdiutil attach -readonly -nobrowse -noautoopen -mountpoint /private/tmp/rk3-controller-editor-174-image ...`.
- Inspected `Controller Editor 1.7.4 Installer Mac.pkg` with `xar -tf` and `pkgutil --expand-full`. The latter returned an extraction error, but an independent `lsbom -s` comparison found **zero missing paths** across all four payloads: 469 application, 188 documentation, 83 content, and 14 system-extension entries. Extracted files are retained at `/Users/zukomgwili/src/rigkontrol3/.build/tmp/controller-editor-1.7.4/extracted` (abbreviated **E** below); manual text and offset-indexed strings are in the parent directory.
- Inspected using `file`, `otool -L`, `nm -u`, `strings -a -t x`, plist/XML reads, and `pdftotext -layout`. No installer scripts, bundled executables, drivers, or apps were run; nothing was installed or sent to hardware. Extraction and text outputs are temporary research material.
- Signature inspection: `codesign -d --verbose=4` reports the DMG **unsigned**; `pkgutil --check-signature` reports the enclosed package **invalid signature** on this system. Both apps contain embedded SHA-1 signatures with team ID `83K5EG6Z9V`, but authority is unavailable. These observations do not establish why validation fails or authenticate the artifact; no signature bypass was attempted.

NI's [official downloads catalog](https://support.native-instruments.com/support/solutions/articles/69000882238-native-instruments-drivers-other-files) dates the Mac legacy installer to 2014-12-15. NI [specifically identifies 1.7.4 as the last editor for RK3](https://support.native-instruments.com/support/solutions/articles/69000880035-my-native-instruments-controller-does-not-appear-in-controller-editor); support was removed in 1.8.0.

## What is actually included

| Item within E | Verified finding |
| --- | --- |
| `Controller Editor Application Update.pkg/Payload/Controller Editor.app/Contents/MacOS/Controller Editor` | Compiled **x86_64-only** Mach-O, 17,005,280 bytes; Info.plist version `1.7.4 (R199)` and minimum OS `10.8.0`. |
| `Controller Editor System Extensions.pkg/Payload/Native Instruments/Hardware/NIHardwareAgent.app/Contents/MacOS/NIHardwareAgent` | Compiled **x86_64-only** Mach-O, 8,343,520 bytes; same version/minimum OS; background UI element. |
| `Scripts/pathhelper`, `Scripts/LoginItemHelper` and package copies | **i386/PowerPC** helper executables. They are not ARM or x86_64. |
| `Controller Editor Factory Content Update.pkg/Payload/Native Instruments/Hardware/Devices/Rig Kontrol 3.ndd` | 119-byte XML containing only an empty `ni-hardware-controller` root. It is not a useful USB protocol description. |
| `.../Native Instruments/Controller Editor/Templates/Default.ncg` and `Mackie Control.ncg` | Readable XML MIDI assignment templates, including a separate `PedalSwitch`. |
| `Controller Editor Documentation Update.pkg/Payload/Documentation/Controller Editor Manual English.pdf` | Included version-specific manual with an RK3 chapter. |

No source project, `.c/.cpp/.h/.swift` implementation, bundled `.kext`, or custom dynamic framework was found in these payloads. The package named “System Extensions” installs **NIHardwareAgent.app**, not a modern DriverKit extension. Its `Scripts/postinstall` lines 18–25 add that agent as a login item and launch it. The top-level `Distribution` also explicitly rejects non-Intel hardware via `system.gestalt('sysa') == '10'`.

## Where device communication lives

`otool -L` shows both applications link IOKit; the agent also links CoreMIDI. Its undefined imports include `IOServiceOpen`, `IOConnectCallMethod`, `IOConnectCallStructMethod`, `IOConnectMapMemory`, `CFMessagePortCreateLocal/Remote`, `CFMessagePortSendRequest`, `MIDISourceCreate`, `MIDIDestinationCreate`, and `MIDIReceived`.

These are concrete signs of a background hardware/MIDI service and interprocess communication, rather than an editor that merely changes USB IDs. Agent string **file offsets** include:

| Offset | String evidence |
| --- | --- |
| `0x461a60` | C++ type `NI::NHL2::SERVER::RigControl3` |
| `0x4de2d0`, `0x4de2f0` | `IPCReceivePort`, `IPCSendPort` types |
| `0x5550d4` | `Rig Kontrol 3 StartInMidiMode` |
| `0x5619da` | `de_caiaq_driver_NIUSBAudioDevice` |

The existing installed [NIUSBAudioDriver Info.plist](/Library/Extensions/NIUSBAudioDriver.kext/Contents/Info.plist) corroborates the last dependency: its `RK3` personality matches VID `6092`/PID `6464` (`17cc:1940`), uses that exact IOClass, and declares `de_caiaq_driver_NIUSBUserClient`. `file` identifies its executable as an **x86_64-only kext**. Static imports alone do not reconstruct every RK3 transport call, but together these are strong evidence that the legacy route expects the NI kernel/user-client interface. Our libusb route supplies a different, already tested transport.

## Useful behavior established by the included manual

Page numbers below are printed page numbers in the extracted English manual:

- **p37, §4.1:** the persistent background service handles hardware/MIDI communication after the editor closes. This matches our app/transport separation.
- **pp213–217, chapter 21:** RK3 has eight footswitches, the expression-pedal toe switch, the treadle, and two external pedal inputs. Pedals require calibration. MIDI mode and native Guitar Rig mode are distinct; native mode uses the old Virtual Rig Kontrol connection and shows a preset number, while MIDI mode displays `con`.
- **p216, §21.3.1:** incoming MIDI can update the switch/LED state. Toggle mode combines local actions and incoming state; Gate/Trigger/Increment can select the LED source.
- **pp290–291, §25.6.2:** `For MIDI In` makes the target control the LED, including changes made with the mouse. **The target must be configured to send the return message.** `For MIDI Out` follows local button actions. The editor does not discover arbitrary target parameters by itself.
- **p247:** calibration covers the treadle and external pedals; external inputs support pedal/footswitch and ring/tip choices.

`Default.ncg` independently confirms nine switch assignments and three pedals: eight switches use CC80–83/76–79, toe CC64, treadle CC4, external pedals CC16/17, all with XML channel `0`. Those are template choices, not mandatory USB protocol numbers. The template does **not** reveal the toe packet bit or ADC byte order. We should retain the app's explicit CC20–27/CC11 mapping and measured calibration unless requirements change.

## What an ARM implementation can realistically reuse

Apple's [porting documentation](https://developer.apple.com/documentation/apple-silicon/porting-your-macos-apps-to-apple-silicon) describes rebuilding source and dependencies for `arm64`; changing installer checks does not create that code. [Rosetta translates x86_64 user processes, but not kernel extensions](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment). The 32-bit installer helpers are another separate limitation: [macOS stopped supporting 32-bit apps in Catalina](https://support.apple.com/103076). These facts do not prove every legacy UI operation fails under Rosetta; they rule out treating editor launch as proof of working ARM hardware support.

NI's product manager [states that Rig Kontrol-related integration was removed when launching GR6](https://community.native-instruments.com/discussion/16529/guitar-rig-7-pro-product-manager-answers-your-questions/p1). Therefore implementing the old hardware service alone cannot supply missing GR6 rack-state output. The confirmed MainStage/AU feedback path remains appropriate: actual parameter → host MIDI destination → strict feedback parser → USB LED state.

Recommended work is to **reimplement selected observable functions** on the current native bridge: template editing/import if wanted, toe and external-pedal support after captures, calibration, and explicit feedback routing. Use this manual as the behavioral reference and the [public ARM implementation survey](/Users/zukomgwili/src/rigkontrol3/Research/github-arm-survey-2026-10-24.md) for reusable source. Reproducing legacy NI IPC/native GR5 support would require a separate interoperability investigation; no message schema or working GR6 connection was established here. The bundled EULA is proprietary and supplies no general source/reuse license; no NI code or assets were copied into the product.
