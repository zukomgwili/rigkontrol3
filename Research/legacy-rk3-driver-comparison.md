# Rig Kontrol 3 legacy Mac drivers: 2.6.0, 2.7.0 and 2.8.0

Static inspection on 2026-10-24 for `rigkontrol3-tw5.4`. **These installers provide useful named protocol implementations, but no ARM driver or source project.** Their RK3 matching interface and three inspected controller commands remain consistent. The downloaded 2.8.0 kernel executable is byte-for-byte identical to the one already installed on this Mac. Reinstalling it would not supply missing ARM code or establish a fix for the current libusb stall.

## Artifacts and method

Original files are under `/Users/zukomgwili/Downloads/`:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `Rig_Kontrol_3_260_Mac_p.dmg` | 6,387,702 | `0b487380911ab4c283dcf72848b301fff7c72fd7a1717ba86ec15aeac4d0e371` |
| `Rig_Kontrol_3_270_Mac_p.dmg` | 6,297,243 | `37524a76c3d91de7699716580dbbb29115211500a3fb608728f921fd00b5d559` |
| `Rig_Kontrol_3_280_Mac_p.dmg` | 5,909,563 | `6d15a248eaab39ebcb5206409064b17e4521f56a4b506287e7571cdf2f64d001` |

Each image was mounted using `hdiutil attach -readonly -nobrowse -noautoopen`, and its installer expanded using `pkgutil --expand-full`. All images were then unmounted. **No installer, script, legacy executable, driver, or firmware was run or installed; no hardware or app UI was accessed.**

Evidence is retained under `/Users/zukomgwili/src/rigkontrol3/.build/tmp/legacy-rk3-drivers/` (**D** below): extracted packages in `260/`, `270/`, `280/`; `*-kext-nm.txt`, `*-kext-strings.txt`, `*-kext-disassembly.txt`; and `static-region-comparison.json`. Tools were `file`, plist/XML reads, `nm -arch x86_64 -C/-n`, `otool -L/-tvV`, SHA-256 and a bounded Mach-O section reader. `lsbom -s` found zero missing extracted payload paths in all eight component packages. No `.c/.cpp/.h/.swift` source project was found.

## Packaging and architecture changes

Versions below come from `PackageInfo` and each bundle's `Contents/Info.plist`; architectures come from `file` on the executables.

| Release | Installer minimum / variants | Kernel driver | MIDI driver plug-in | Preference pane |
| --- | --- | --- | --- | --- |
| 2.6.0 (R82) | Intel, OS X 10.6; pre-10.9 and 10.9 packages | Old variant i386 + x86_64; 10.9 variant x86_64 | i386 + x86_64 | i386 + x86_64 |
| 2.7.0 (R20) | Intel, OS X 10.8; 10.8 and 10.9 packages | Both x86_64 | i386 + x86_64 | x86_64 |
| 2.8.0 (R36) | Intel, OS X 10.9; single driver package | x86_64 | i386 + x86_64 | x86_64 |

The older OS variants place `NIUSBAudioDriver.kext` under `/System/Library/Extensions`; the 10.9 variants place it under `/Library/Extensions`. All supply `NIUSBMIDIDriver.plugin` under `/Library/Audio/MIDI Drivers` and `NIUSBAudio.prefPane` under `/Library/PreferencePanes`. The MIDI plug-in links CoreMIDI and IOKit and imports `IOServiceOpen` and `IOConnectCall*`: it relies on the kernel/user-client route, rather than being an independent USB driver.

This is a shared NI/caiaq stack, not three RK3-specific source releases. The RK3 personality is identical in every extracted kext:

- bundle ID `com.caiaq.driver.NIUSBHardwareDriver`;
- VID/PID `6092/6464` = `17cc:1940`;
- provider `IOUSBDevice`, class `de_caiaq_driver_NIUSBAudioDevice`;
- user-client class `de_caiaq_driver_NIUSBUserClient`;
- dependencies include `IOAudioFamily`, `IOUSBFamily` and kernel programming interfaces.

Primary local example: [2.8.0 kext Info.plist](/Users/zukomgwili/src/rigkontrol3/.build/tmp/legacy-rk3-drivers/280/NIUSBAudioDriver-Installer.pkg/Payload/Library/Extensions/NIUSBAudioDriver.kext/Contents/Info.plist). Installer OS/Intel checks are in each version's top-level `Distribution`.

## Concrete protocol evidence and limits of the comparison

The symbol tables retain named methods including `writeDigital`, `digitalInputArm`, `digitalInputRead`, `analogInputRead`, timestamped reads, `setAutoMessage`, `setLEDs` and `getDeviceSpec`. These identify useful seams for a focused interoperability investigation. They do not themselves document the complete external-method selector/argument contract.

Targeted static disassembly establishes these command builders in **all three inspected 10.9 kexts**:

| Method | Verified behavior | 2.6 / 2.7 / 2.8 function addresses |
| --- | --- | --- |
| `NIUSBAudioDevice::writeIO` | Passes command `0x05` and caller payload to `sendCommand` | `0x30d0 / 0x29f0 / 0x27a0` |
| `NIUSBAudioDevice::readIO` | Passes command `0x04`, null payload and zero length | `0x30f0 / 0x2a10 / 0x27c0` |
| `NIUSBAudioDevice::setAutoMsg` | Passes command `0x0b` with three one-byte arguments | `0x3790 / 0x30d0 / 0x2e80` |

These agree with our existing WRITE_IO, payload-free switch snapshot request and AUTO_MSG framing. In particular, the old native driver corroborates that `READ_IO 0x04` is intentionally payload-free. The generic method named `setLEDs` uses command `0x0c` on its ordinary path; it must **not** be substituted blindly for RK3's already hardware-verified `WRITE_IO 0x05` path.

No changed controller-command format was established in these three builders. The complete binaries and executable sections differ; identical method names do not prove that every algorithm or timing behavior is identical. Static analysis did not establish a fix for backpressure, reconnect stalls, toe-bit handling, ADC calibration or a specific transfer timeout. The kernel implementation's IOKit locking/completion environment differs from our libusb worker and input pump.

There is an RK3-specific data symbol, `_RK3_firmware`. The **10,016-byte interval from that symbol to the next data symbol** is byte-identical across the three 10.9 kexts:

- offsets `0x127b0` (2.6), `0x14a80` (2.7 and 2.8);
- SHA-256 `e3b69f0d222fe676e46c8c7974b7625cd96cc2d4fee1690de5cc29e74c5aaebe`;
- first three little-endian 32-bit words are `6092`, `6464`, `13`, consistent with RK3's VID/PID and this unit's previously observed device revision 13.

This comparison includes any alignment padding before the next symbol; it is not a reconstructed firmware format or proof that an update is needed. No firmware was extracted into product code or uploaded. The included English RK3 PDF manual is also identical in all three installers, SHA-256 `2b684fc402e85f3b9875835f32a78f818e069eec1f35e07109c76f48437b824b`.

## Installed components and practicable ARM reuse

The extracted 2.8.0 executable and [installed NIUSBAudioDriver](/Library/Extensions/NIUSBAudioDriver.kext/Contents/MacOS/NIUSBAudioDriver) share SHA-256 `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`. File presence does not prove that it is loaded or owns USB.

Controller Editor 1.7.4's helper imports the same caiaq service and IOConnect interfaces; see the [separate artifact analysis](/Users/zukomgwili/src/rigkontrol3/Research/controller-editor-1.7.4-analysis.md). The separately inspected installed NIHardwareAgent 2.8.2 is universal arm64/x86_64 and retains RK3 names and that service dependency. Thus an ARM user-space agent can exist while the required legacy kernel interface remains Intel-only. Strings or a running agent do not establish device recognition, ownership or the cause of the current stall.

Apple documents that [kernel extensions require native architecture and cannot use Rosetta](https://developer.apple.com/documentation/apple-silicon/porting-your-macos-apps-to-apple-silicon). These archives cannot be made into ARM drivers by changing installer checks, identifiers or bundle metadata. Reproducing the private user-client ABI would require a separate native implementation and contract tests; it is not a simple editor rebuild.

The useful next step is to preserve these artifacts as references for bounded questions—command framing, initialization, input buffering, device information—while diagnosing the current bridge using its actual transfer/lifecycle evidence. Prefer the [existing public ARM source survey](/Users/zukomgwili/src/rigkontrol3/Research/github-arm-survey-2026-10-24.md) before writing a replacement audio stack. Neither these drivers nor an ARM hardware transport make Guitar Rig 6 emit authoritative effect state; its [old native Rig Kontrol integration was removed](https://community.native-instruments.com/discussion/16529/guitar-rig-7-pro-product-manager-answers-your-questions/p1), so host feedback remains a separate requirement.
