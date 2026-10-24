# Legacy RK3 reconstruction feasibility

Read-only static audit, 2026-10-24. The extracted NI 2.8.0 artifacts are useful
reverse-engineering inputs: both preserve meaningful C++ symbols. Static
analysis can reconstruct protocol and lifecycle behavior, but does not itself
produce a tested ARM implementation. No installer, legacy executable, driver,
USB operation or GUI was run for this audit.

## Available tools and symbol coverage

Verified locally: `/usr/bin/otool`, `/usr/bin/nm`, `/usr/bin/objdump`, and Xcode's
`llvm-objdump` at
`/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-objdump`.
`nm -arch x86_64 -n -C` demangles the retained names; LLVM disassembly works on
the x86_64 kext without executing it.

No ready-to-use Ghidra, Hopper, IDA, Binary Ninja, radare2 or Rizin was found in
PATH or `/Applications`. The default Python environment also lacked capstone,
angr, unicorn, lief and ghidra_bridge. This is a bounded local inventory, not
a claim about every directory or Python environment.

| Artifact | Verified architecture and text-symbol coverage |
| --- | --- |
| [NIUSBAudioDriver][audio] | x86_64 kext; 241 text symbols (219 `T`, 22 `t`), including 213 named C++ methods. |
| [NIUSBMIDIDriver][midi] | i386/x86_64 bundle; x86_64 slice has 59 text symbols (1 `T`, 58 `t`), including 43 named C++ methods. |

SHA-256 identities:

- Audio: `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`
- MIDI: `a86c4d02b16966a1fb3a06027a7c3dc3a6e9a617082e93c01e1065b8aef3353e`

Addresses below are **unslid x86_64 virtual addresses**, not runtime addresses.
External calls need Mach-O relocation resolution: bare disassembly can show a
call to the next instruction. `otool -rv` identifies, for example, relocations
at `0x14c1 → _IOMalloc`, `0x14d9 → _bcopy`, and `0x1500 → _IOFree` in [the kext][audio].

## Embedded firmware: verified and inferred

No standalone `.hex`, `.fw`, `.syx`, `.srec`, `.ihex` or `.bin` firmware file was
found in the extracted 2.8.0 package. Firmware is nevertheless embedded in the
kext: `nm` identifies `_RK3_firmware` at **0x14a80** and `_firmware_list` at
**0x32000**. The first four little-endian 32-bit fields at 0x14a80 are
`6092, 6464, 13, 5598`: matching RK3 VID **0x17cc**, PID **0x1940**, a version
threshold field **13**, and image-length field **5598**. The list contains the
pointer 0x14a80. These bytes were read from the artifact, not extracted into a
new firmware file. [Binary evidence][audio].

`updateFirmware()` at **0x1420–0x1533** scans the list. Instructions
**0x1490–0x149c** require matching first/second identity fields and a third
table field greater than the current device value before taking the update
path. **0x14bb–0x1506** allocates/copies a 0x2720-byte record, calls
`replaceSerial(firmware*)` at **0x1e70**, and conditionally calls
`downloadFirmware(firmware*)` at **0x1c80**. The exact interpretation of the
device-side comparison value still needs confirmation; do not equate it
automatically with a separately reported protocol firmware version.
[Instruction evidence][audio].

The download routine **0x1c80–0x1e6b** constructs USB vendor requests with
type **0x40**, request **0xa0**, first writing one byte 1 to address **0xe600**,
then payload chunks of at most 16 bytes, then one byte 0 to 0xe600.
**Inference:** this fits an FX2/8051 RAM-loading/reset sequence, supported by
the adjacent named `FX2Write` routine at 0x1bd0. It is not evidence of persistent
flash programming or a requirement to update this working unit. [Instructions][audio].

The current native controller was physically validated using its existing
firmware, and its [C transport](../Sources/CRK3CoreAudioUSB/usb_iokit.c) contains
no firmware-upload implementation. Thus no upload is a demonstrated
prerequisite for the controller path. Whether any older unit needs an update,
or whether a specific audio behavior depends on a firmware revision, remains
unproven. See the [native validation record](rk3-coreaudio-backend-evaluation.md).

## Concrete targets for unified ARM audio and controls

These named boundaries permit focused reconstruction rather than an attempt
to translate the whole Intel binary. Addresses come from the artifacts' own
symbol tables; this audit did not fully trace every listed routine.

| Remaining question | Kext targets |
| --- | --- |
| Shared USB command/reply ownership and serialization | `sendCommand` 0x25c0; `commandReplyDispatch` 0x20e0; `externalMethod` 0xa2a0. |
| Audio format, start/stop and packet cadence | `setAudioParams` 0x2d50; `performAudioEngineStart` 0x5fb0; stop 0x6800; format change 0x68d0; `queueInIrp` 0x6640; `queueOutIrp` 0x7620. |
| Completion timing, sample framing and drift handling | `readHandler` 0x7a80; `writeHandler` 0x7ec0; `takeSimpleTimeStamp` 0x8100; `readMicroframeMode2` 0x82b0; `fillErrorCorrectionPatternMode2` 0x87f0. |
| Buffer sizing, channel gain and direct monitoring | User-client `setHardwareBufferSize` 0x9690; engine `setChannelGain` 0x88f0; device direct-monitor set/get 0x2ee0/0x2f10. |
| Sleep/wake and teardown while controls remain active | Device `performPowerStateChange` 0x1fe0; engine `enterSleep` 0xbc50, `awakeFromSleep` 0xbcb0, `tearDown` 0x5e30. |

For the MIDI ABI, [NIUSBMIDIDriver's x86_64 slice][midi] retains
`triggerAsyncRead` **0x1240**, `deviceAdded` **0x19a0**, `Send` **0x2720** and
`MIDIReadCallback` **0x28c0**. Its imports include CoreMIDI device/entity creation
and IOKit user-client calls. It can clarify port identity, callback framing and
the old driver boundary; adopting its Intel binary is not required to implement
equivalent native CoreMIDI behavior.

Static work can establish constants, packet layouts, branch conditions,
error paths and candidate synchronization rules. It cannot establish actual
USB timing, jitter, race behavior, latency, audible correctness or reliable
hotplug on ARM. Kernel vtable calls, relocations and asynchronous callbacks
also require careful interpretation. A native implementation needs executable
tests and attached-unit audio/control validation; this audit did neither.

[audio]: <../.build/tmp/legacy-rk3-drivers/280/NIUSBAudioDriver-Installer.pkg/Payload/Library/Extensions/NIUSBAudioDriver.kext/Contents/MacOS/NIUSBAudioDriver>
[midi]: <../.build/tmp/legacy-rk3-drivers/280/NIUSBAudioDriver-Installer.pkg/Payload/Library/Audio/MIDI Drivers/NIUSBMIDIDriver.plugin/Contents/MacOS/NIUSBMIDIDriver>
