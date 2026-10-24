# NIUSBAudioDriver 2.8.0: controller reads, LED writes and shared EP1

Static research, 2026-10-24, for the parent `do5` investigation. **The strongest finding is a single EP1 receive dispatcher serving both controller reports and the audio-parameter acknowledgement.** Audio setup waits for a field updated by that dispatcher; it does not create a competing EP1 reader in the traced sequence. This is a concrete architecture reference for a future combined owner, not a tested fix or a driver change.

## Primary evidence and notation

Inspected [extracted x86_64 executable](/Users/zukomgwili/src/rigkontrol3/.build/tmp/legacy-rk3-drivers/280/NIUSBAudioDriver-Installer.pkg/Payload/Library/Extensions/NIUSBAudioDriver.kext/Contents/MacOS/NIUSBAudioDriver), version **2.8.0 (R36)**, SHA-256 `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`. It is byte-identical to the previously inspected installed executable. Artifact provenance is in [the three-version comparison](/Users/zukomgwili/src/rigkontrol3/Research/legacy-rk3-driver-comparison.md).

Sources: [demangled symbol table](/Users/zukomgwili/src/rigkontrol3/.build/tmp/legacy-rk3-drivers/280-kext-nm.txt) and [static disassembly](/Users/zukomgwili/src/rigkontrol3/.build/tmp/legacy-rk3-drivers/280-kext-disassembly.txt), reproduced with `nm -arch x86_64 -C/-n` and `otool -arch x86_64 -tvV`. Selected operands were rechecked with `otool -tv`, because `-V` sometimes renders a numeric structure offset as an unrelated symbol with the same numeric value. The command jump table was decoded directly from little-endian signed words in the executable.

Addresses below are **unslid instruction/symbol addresses in this binary**, not runtime addresses or a stable ABI. `D`, `U`, and `E` denote the device, user-client, and audio-engine objects. Field names are descriptive interpretations; numeric offsets are observed. Unresolved virtual calls are not assigned precise API names without supporting data flow. Nothing was executed, installed, loaded, or sent to hardware; no app/HAL code changed.

## Confirmed: one command receive path, including the audio ACK

On its non-update path, `initHardware` allocates separate 64-byte buffers at `0x0e78–0x0ea2`. A true result from `updateFirmware()` at `0x0e5f–0x0e69` instead takes an early return at `0x112c`. On the non-update path, `0x0fec–0x1013` builds a completion record containing the device, `staticCommandReplyDispatch`, and the input buffer; the initial pipe operation at `0x1038` requests `0x40` bytes. The static thunk at `0x1559–0x1564` computes `64 - remaining` and calls `commandReplyDispatch`.

On its normal successful path, `commandReplyDispatch` takes the device lock at `D+0x120` (`0x2179–0x2181`), selects a branch from packet byte 0 (`0x2186–0x21a2`), and eventually rearms the same input buffer for 64 bytes (`0x224c–0x227f`), unless `D+0x8ea` is set. `willTerminate` sets that flag at `0x1a3d`, supporting its interpretation as a shutdown gate.

The jump table starts at **`0x2560`**. Its signed relative entries establish these branches directly:

| Command | Target | Observed handling |
| --- | --- | --- |
| `0x03` | `0x23ae` | Skip command byte; iterate 32 analog-client slots at `D+0x1d0`; call `processAnalogInputData(payload, 16, true)` at `0x23db`. |
| `0x04` | `0x23fc` | Skip command byte; iterate 32 digital-client slots at `D+0x2d0`; call `processDigitalInputData(payload, 8, true)` at `0x2429`. |
| `0x09` | `0x24b5` | Read reply byte 1 and store it at **`D+0x8e0`**, instruction **`0x24ba`**, then return to the common rearm path. |

The fixed 16/8 forwarding counts are arguments used by this generic driver. They do not establish actual USB transfer lengths, RK3 axis count, toe-bit semantics, or validity of reserved bytes. Our captured packet lengths must remain the reference for our parser.

The other half of the `0x09` exchange is unambiguous:

1. `setAudioParams` writes **`-1` to `D+0x8e0` at `0x2e0c`**.
2. It prepares command `0x09`, five payload bytes, and calls `sendCommand` at **`0x2e28`**.
3. After successful send, `0x2e40–0x2e58` polls that same field, calling `IOSleep(1)` while it remains `-1`, bounded by an initial counter of `-1000`.
4. `0x2e63–0x2e6c` accepts acknowledgement value `1`; other outcomes return an error.

Thus the existing dispatcher delivers the acknowledgement while the command caller waits. No additional bulk-IN operation occurs inside this traced `setAudioParams` routine.

## Confirmed: user clients subscribe to buffers, rather than opening USB

`NIUSBUserClient::start` casts its provider to `NIUSBAudioDevice` and stores it at `U+0x1970` (`0xa3c1–0xa3d6`). The following read methods use that provider and mapped task memory:

| Method | Relevant instructions |
| --- | --- |
| `analogInputRead`, `0x9da0` | Takes `U+0x708` lock; creates an `IOMemoryDescriptor::withAddressRange` at `0x9e15`; copies the async reference at `0x9e3b`; registers the client at `0x9e52`. |
| `digitalInputRead`, `0x9ea0` | Creates the mapped destination at `0x9f23`; allocates **512** queue buffers in `0x9f50–0x9f7a`; copies the async reference at `0x9f8d`; registers at `0x9fc4`. |
| Registration | Analog `0x3950` and digital `0x39b0` search their respective 32-slot arrays under the same `D+0x120` lock used by receive dispatch. |

`processAnalogInputData` (`0xaec0`) bounds its copy against destination capacity and supplied count, optionally prepends two timestamp words using `clock_get_system_nanotime` (`0xaf57`), and calls `IOUserClient::sendAsyncResult64` at `0xb00e`.

The digital path (`0xb030`) also delivers through mapped memory and `sendAsyncResult64` (`0xb1cf`). Its queued mode advances a producer index and masks it with **`0x1ff`** (`0xb0a5–0xb0c2`) under the client lock at `U+0x710`. `digitalInputArm` (`0x9ad0`) checks producer/consumer indexes, drains one queued buffer with the same wrap mask (`0x9b0e–0x9b25`), and notifies at `0x9c03`; if empty, it records a waiting flag instead. The exact overflow policy and all mode transitions were not reconstructed.

`clientClose` deregisters analog and digital clients at `0xa573/0xa582`, deregisters other subscriptions, then clears the provider pointer at `0xa5df`. This supports a subscription lifecycle separate from the physical device lifetime.

## Confirmed: writes use another lock, with an important success-reporting flaw

`writeDigital` takes client lock `U+0x708` at `0x92bf`, then calls `D::writeIO` at `0x92ea`. `writeIO` selects command **`0x05`** (`0x27aa`) and delegates to `sendCommand`.

`sendCommand` rejects payloads over 63 bytes (`0x25d8–0x25e4`), takes the **different device lock `D+0x128`** (`0x25e9–0x25f0`), writes command and payload into the shared output buffer (`0x2645–0x2675`), and performs the output operation at `0x26a2`, with both timeout arguments set to **200**. It unlocks at `0x26e8` and returns whether the underlying status was zero (`0x26ed–0x26f0`). This separates output-buffer serialization from normal receive/list locking.

However, **`writeDigital` ignores that boolean**: immediately after the call, `0x92ef` zeros its return-status register without testing the result. A caller can therefore receive success even when `sendCommand` failed. The separately named `setLEDs` user-client method does test its provider result (`0x9568–0x956f`); its generic device path uses command `0x0c`, so it must not replace the attached-unit-verified RK3 `0x05` path merely because of its name.

## Audio shares the provider; implementation implications

`createAudioEngine` passes `this` device into `AudioEngine::init` at `0x1b68–0x1b74`; the engine stores that pointer at **`E+0xb938`**, instruction `0x4869`. Audio start calls this provider's `setAudioParams` at `0x5fd6` and `0x63bc`. The traced audio-stop routine (`0x6800–0x68be`) operates on audio-side timer/pipe objects, including provider fields `D+0x930/0x940`, rather than explicitly closing the command-pipe fields `D+0x920/0x928`. Detailed audio lifecycle evidence is in [the companion report](/Users/zukomgwili/src/rigkontrol3/Research/legacy-rk3-audio-lifecycle.md).

**Inference:** the intended architecture is one physical USB owner, persistent command reception, per-client subscriptions, and audio layered over that owner. This is stronger than a conclusion drawn from method names alone, because the shared provider pointer, command dispatcher and ACK field are directly traced.

Original behavioral pseudocode for that pattern:

```text
device owner starts persistent EP1 input
on successful packet:
    03 -> distribute analog payload to registered clients
    04 -> distribute digital payload to registered clients
    09 -> publish the audio-command acknowledgement
    rearm EP1 unless shutting down
send command:
    serialize shared output buffer; perform bounded write
configure audio:
    mark ACK pending; send 09; wait for dispatcher-published ACK
client closes:
    unsubscribe; retain physical owner for other clients/audio
```

The useful design consequence is to keep **one input demultiplexer** when future audio and controls share USB, instead of aborting control reads to synchronously fetch an ACK with a second reader. It also supports preserving independent input progress during bounded command writes and reporting the actual transfer result.

**Unknown / not demonstrated:** the complete private user-client ABI, all indirect virtual-call/error paths, kernel scheduling guarantees, overflow recovery, firmware backpressure cause, or a hardware-validated remedy for the current audio/controller ownership conflict. These findings do not make the Intel kext ARM-compatible, establish Guitar Rig 6 effect-state output, or justify uploading firmware. No replacement driver was implemented or hardware-tested in this research.
