# NI USB MIDI driver reconstruction

## Inputs and method

This note describes NI's **2.8.0 Intel MIDI plug-in** and the matching NI USB audio kernel driver's MIDI/user-client paths. It is a behavioral reconstruction for a new ARM implementation. No previous bridge implementation or third-party RK3 implementation was used.

| Input | SHA-256 / identity |
| --- | --- |
| `NIUSBMIDIDriver.x86_64` | `84fa1a6d75c7e478af82fe4f7a38ddd633d60754d63d65d6090f0b2517152ae1` |
| MIDI bundle version | `2.8.0`; built against macOS 10.9 SDK |
| Factory symbol / UUID | `NewCaiaqUSBMIDIDriver`; `3CA12C35-1708-4F74-B74F-24A02243E197` |

Ghidra **12.2** imported the original Mach-O bundle using `x86:LE:64:default` / `gcc`, performed analysis, and exported **132/132 initialized executable functions** successfully (**59 implementation functions plus 73 native import trampolines**, excluding synthetic external-memory entries); `cancelled=false`. The private export and manifest are in ignored `.build/ni/decompiled/midi280-text/`. The independent Ghidra database is `/private/tmp/ni-rk3-midi/ghidra-project/midi280`. `nm -an` and `otool -tvV` independently verified call arguments and offsets where Ghidra recovered imprecise pointer types. All addresses below are unslid addresses in the **2.8.0 x86_64 slice**, not file offsets in the original universal executable.

The generic exporter is `Tools/DecompileDriver.java`. A headless reproduction is:

```sh
JAVA_HOME=/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless \
  /private/tmp/ni-rk3-midi/ghidra-project midi280 \
  -import "$PWD/.build/ni/reference/2.8.0/NIUSBMIDIDriver.x86_64" \
  -analysisTimeoutPerFile 300 -scriptPath "$PWD/Tools" \
  -postScript DecompileDriver.java "$PWD/.build/ni/decompiled/midi280-text"
```

Use a new project name for a new import or Ghidra's explicit process mode for an existing import. The proprietary binary, complete C-like export, assembly listings, and Ghidra databases remain local and ignored. This document records facts, interfaces, and original-symbol addresses rather than redistributing the reconstructed proprietary implementation.

## Main finding: CoreMIDI traffic and controller reports are separate

The MIDI plug-in transports existing MIDI byte streams between CoreMIDI and the audio driver's user-client. It **does not convert footswitch or expression reports to MIDI**. Its complete IOKit method usage is synchronous selector **3** (device information), synchronous selector **4** (MIDI output), and asynchronous selector **0** (MIDI input). It does not subscribe to the kernel's analog or digital controller interfaces and contains no CC, note, program-change, pedal-calibration, or controller-template mapping.

The matching kernel driver confirms three different input paths:

| USB command/report | Audio-driver branch | Destination |
| --- | --- | --- |
| `3` | `commandReplyDispatch` `0x23ae` → `0x23db` | `processAnalogInputData(report + 1, 16, true)` on analog subscribers |
| `4` | `commandReplyDispatch` `0x23fc` → `0x2429` | `processDigitalInputData(report + 1, 8, true)` on digital subscribers |
| `6` | `commandReplyDispatch` `0x244a` → `0x2483` | `processMIDIData(report[1], report + 3, report[2])` on MIDI subscribers; byte count must be at most `61` |

These command numbers come from the signed-relative jump table at audio-driver `0x2560`, referenced by `commandReplyDispatch` `0x2194`. The analog, digital, and MIDI subscriber tables are respectively at device offsets `0x1d0`, `0x2d0`, and `0x5d0`; no mapping from the first two tables to the third occurs in these branches.

The kernel also has a deliberately separate **MIDI injection interface**: synchronous selector **12**, `MIDIWriteFake`, injects bytes into subscribed MIDI clients without sending USB data. The call chain is audio-driver `genericMethodDispatchAction` `0x8f9d` → `0x8fe5` → device `MIDIWriteFake` `0x2a70` → `processMIDIData` `0x2ab4`. This is an available mechanism for another process to translate controller reports and publish the result through the device's CoreMIDI source. It is **not called by the MIDI plug-in**.

Therefore replacing the two Intel driver binaries alone does not recover a controller-to-MIDI mapping that lives in another process. An application, a replacement native control service, or a native mapping component must consume analog/digital reports and perform that conversion. These two binaries establish the separation and injection mechanism; they **do not establish whether NIHardwareAgent, Controller Editor, Guitar Rig, or a combination supplied that policy**. Naming one of those as the proven mapper requires inspecting that executable or observing its API calls. Likewise, the exact RK3 footswitch bit positions, pedal calibration, defaults, and profile behavior are not established by this generic MIDI plug-in.

## CoreMIDI plug-in contract

| Behavior | Original symbol / address | Evidence |
| --- | --- | --- |
| Creation | `NewCaiaqUSBMIDIDriver` `0x20e0` | Accepts `kMIDIDriverTypeID` `ECDE9574-0FE4-11D4-BB1A-0050E4CEA526`; returns a COM-style `MIDIDriverRef` |
| Interface negotiation | `MIDIDriverQueryInterface` `0x1000` | Accepts `kMIDIDriverInterface2ID` `43C98C3C-306C-11D5-AF73-003065A8301E` and `IUnknown`; rejects other interfaces with `0x80000004` |
| Function table | `MIDIDriverInterfaceFtbl` `0x43e0` | Reserved, QueryInterface, AddRef, Release, FindDevices, Start, Stop, Configure, Send, EnableSource, Flush, Monitor |
| Start | `CaiaqUSBMIDIDriver::Start` `0x2490` | Observes the kernel provider service; marks remembered but absent devices offline |
| Dynamic device discovery | `listenToService` `0x2380` | `IOServiceMatching("de_caiaq_driver_NIUSBAudioDevice")`; first-match and terminate notifications on the server's current run loop |
| Hotplug addition | `deviceAdded` `0x19a0` | Opens user-client type `0`, fetches device info, creates/reconnects MIDI entities, starts async reads |
| Source listener gate | `EnableSource` `0x2640` | Sets per-port enabled flag used before parsing/delivering incoming data; does not issue a USB enable command |
| Send | `Send` `0x2720` | Immediately copies MIDI packet bytes into eight-byte chunks, then calls user-client selector `4` |
| Input delivery | `CaiaqMIDIParser::EmitPacketList` `0x28a0` | Calls `MIDIReceived` on that parser's source endpoint |
| Removal | `deviceRemoved` `0x2040`; `removeCaiaqMIDIDevice` `0x1760` | Finds the service's state, sets the MIDI device offline, frees per-device state |
| No-op interfaces | base `FindDevices` `0x11c0`, `Configure` `0x11f0`, `Flush` `0x1220`, `Monitor` `0x1230` | Return success; no schedule-ahead queue exists |

The system SDK's `CoreMIDI/MIDIDriver.h` confirms the COM function-table definitions and main-thread / MIDI I/O-thread rules. The original plug-in implements the version 2, MIDI 1.0 byte-stream interface; it has no UMP or MIDI 2.0 parser.

## Device information and endpoint topology

`deviceAdded` calls `IOServiceOpen(provider, mach_task_self(), 0, ...)` at `0x1a70`, then `IOConnectCallMethod(connection, 3, ..., structureOutputSize=0x6e)` at `0x1ab5`. The matching kernel method `getDeviceInfo` is at audio-driver `0x9310` and requires exactly `0x6e` bytes.

| Offset | Size | Recovered field / origin |
| --- | --- | --- |
| `0x00` | 2 | USB vendor ID |
| `0x02` | 2 | USB product ID |
| `0x04` | 33 | Serial-string storage; kernel copies at most 32 bytes |
| `0x25` | 33 | Product-string storage; kernel copies at most 32 bytes |
| `0x46` | 33 | Vendor-string storage; kernel copies at most 32 bytes |
| `0x67` | 1 | Padding / not assigned by this method |
| `0x68` | 2 | Word copied from device-info descriptor offset `0` |
| `0x6a` | 2 | Zero-extended byte from device-info descriptor offset `2` |
| `0x6c` | 1 | CoreMIDI input/source port count, from device offset `0x990` |
| `0x6d` | 1 | CoreMIDI output/destination port count, from device offset `0x98f` |

The plug-in sets device name and model from the USB product string and manufacturer from the vendor string. It creates **one separate input-only entity per input port**, followed by **one separate output-only entity per output port**; there is not one bidirectional entity per port. Input endpoint `refCon1` is its zero-based port index; `refCon2` is per-device state. Output endpoints receive the corresponding output index and device pointer. One byte-stream parser belongs to each source.

The generic driver reads port counts from the device. The CoreMIDI binary contains **no RK3-specific port-count constant**. Do not conclude that two ports mean “DIN plus pedals,” or that any exposed MIDI source inherently represents the foot controller. There is one count-adjustment exception in the kernel's device-info report path: at audio-driver `0x22d3`–`0x22f8`, **VID `0x17cc` / PID `0x2305`** gets one extra input and output. This condition must not be generalized to RK3 unless the actual device identity matches. The physical-versus-synthetic role of any port must follow the RK3 descriptor, USB routing, and/or observed external mapper.

`getPortNameFromPlist` (`0x1860`) looks up `/Library/Preferences/com.caiaq.<vendor>.MIDI.plist`, then a dictionary keyed by product string, then `MIDI input port <index>` or `MIDI output port <index>`. When no override is available, those are the names. This plist supplies labels, not MIDI conversion rules. Image lookup uses `/Library/Audio/MIDI Devices/<vendor>/Images/<product-with-slashes-replaced-by-underscores>.tiff`.

Remembered CoreMIDI-device reuse is based on name (`findOfflineDeviceByName` `0x1500`), not serial number. A new implementation should use a stable serial/location identity to avoid confusing two devices with the same product name.

## User-client MIDI ABI

Synchronous and asynchronous methods have **separate selector spaces**: synchronous `0` is not MIDI read.

| Space / selector | Input | Output / effect | Original evidence |
| --- | --- | --- | --- |
| Sync `3` | No scalar or structure input | `0x6e`-byte device-info structure | MIDI `0x1ab5`; audio `0x9310` |
| Sync `4` | One `0x48`-byte MIDI event structure | Sends bytes to hardware; original plug-in uses at most 8 payload bytes per call | MIDI `0x26f0`, `0x2720`; audio `0x9450` / inlined dispatch `0x8de5` |
| Async `0` | Two 64-bit scalars: user event-array address, event capacity; eight-word async reference | One-shot completion, one scalar = number of filled event records | MIDI `0x1240`, `0x1330`; audio `MIDIRead` `0x9c50`, `sendAsyncMIDI` `0xab60` |
| Async `6` | Same request shape | Timestamped event records of `0x50` bytes | audio `MIDIReadTimestamped` `0xa190`; dispatch table at `0x91d8`; not used by MIDI plug-in |
| Sync `12` | MIDI event structure | Injects host-produced MIDI input; no USB send | audio `0x97a0`, `0x2a70`; not used by MIDI plug-in |

The plain event wire structure is fixed-width, not native C pointer-sized:

```c
struct MidiEvent72 {
    uint8_t port;
    uint8_t reserved[3];
    uint32_t byteCount;
    uint8_t bytes[64];
}; /* sizeof = 72; count at +4; payload at +8 */
```

All numeric fields observed here are little-endian. Do not cast this ABI to an ARM-dependent C++ layout. A timestamped kernel event appends two 32-bit values (`seconds`, `nanoseconds`) at offsets `0x48` and `0x4c`, for size `0x50`; `sendAsyncMIDI` obtains those using `clock_get_system_nanotime`. The original MIDI plug-in does not consume those values.

### Input buffer and callback lifecycle

The original per-device allocation is `0x4a0` bytes. Its eight-event array starts at state offset `0x24`; the IOKit connection is at `0x268`, receive port at `0x26c`, source endpoints at `0x274`, destinations at `0x2f4`, parsers at `0x378`, and callback context `{driverPointer, devicePointer}` at `0x478`. These are reverse-engineering anchors, not a layout to reproduce in new code.

1. `deviceAdded` creates an IOKit receive port and a `CFMachPort` wrapper using `IODispatchCalloutFromMessage`. Its run-loop source is attached to `MIDIGetDriverIORunLoop`.
2. `triggerAsyncRead` calls async selector `0` with scalars `[eventArrayAddress, 8]`; the async reference contains `staticMIDIReadCallback` and a stable context pointer. Immediate submission errors are retried up to three times.
3. The kernel's `MIDIRead` requires nonzero pointer/capacity, rejects a second outstanding request with `0xe00002d5`, creates an `IOMemoryDescriptor` covering `capacity × 72` bytes in the calling task, saves the async reference, then attempts to drain buffered MIDI.
4. `processMIDIData` (`audio 0xade0`) queues bytes by logical port under a lock. It accepts port values below `128`; output monitoring uses `port + 0x40`. Each port has a 255-byte backing buffer; if adding a packet would reach/exceed 256 bytes, existing buffered data is discarded before appending the new packet.
5. `sendAsyncMIDI` (`audio 0xab60`) scans ports from `0` to `127`, fills up to the requested capacity, and copies at most `64` queued bytes per record. It sends a successful async completion with **one scalar argument: record count**, releases the user buffer descriptor, and clears the outstanding request. With no queued data, the request stays pending.
6. The callback `staticMIDIReadCallback` (`MIDI 0x1330`) handles at most eight records, gates input on source enablement and connected-device state, parses bytes, and **rearms one async read after successful completion**, including a zero-count completion. It stops rearming after a nonzero completion status. The completion count arrives as the third callback argument by value through `IODispatchCalloutFromMessage`; Ghidra's recovered `void **` type and the mangled original declaration are misleading here. The kernel sends one completion scalar, not a pointer to an event array.
7. Each delivered event is timestamped with userspace `mach_absolute_time` immediately before byte parsing. The kernel-timestamp variant is unused.
8. Removal marks the CoreMIDI device offline and releases strings/parsers, destroys the receive port, closes the IOKit connection, and frees state (`0x16c0`). The kernel's `clientClose` (`audio 0xa550`) unregisters all data subscriptions; `finalize` (`0xa620`) releases an outstanding MIDI descriptor and clears its saved callback reference.

A replacement must make cancellation and context lifetime explicit, wait for callbacks to quiesce before freeing device state, and retain/remove its run-loop sources correctly. Do not reproduce the original's source-lifetime omissions or its off-by-one comparisons (`port <= count` in `0x1330` / `0x28c0` and `destinationIndex <= outputCount` in `0x2720`). Valid zero-based indices satisfy **`index < count`**, and event lengths must be independently bounded to 64.

### Output behavior

`Send` (`0x2720`) walks every `MIDIPacket` and concatenates its bytes into chunks of **8**, retaining a partial chunk across packet boundaries. Every full chunk and final nonempty remainder goes through sync selector `4` with the 72-byte event structure. The packet's timestamp is ignored; output is immediate. It aborts on the first user-client error and returns that error; empty lists succeed.

The kernel's `MIDIWrite` (`0x2990`) caps payload length to 63, sends command **7** with payload `{port, count, bytes}`, and feeds a monitoring copy into MIDI subscribers under logical port **`port + 0x40`**. The CoreMIDI plug-in's input endpoint bounds ordinarily reject these monitoring ports, so this echo is not an additional public CoreMIDI source. The new transport must serialize command writes and preserve byte order across chunks. Although `MIDIWrite` caps its count at 63, `sendCommand` (`audio 0x25c0`) accepts at most 63 payload bytes including the port/count prefix, so a safe individual command-7 message contains at most **61 MIDI bytes**. The original plug-in's eight-byte chunks avoid that inconsistent legacy upper bound.

## Byte parsing

`MIDIParser::FeedBytes` (`0x29f0`) is a stateful MIDI 1.0 parser; `CaiaqMIDIParser::EmitPacketList` simply forwards completed packet lists to `MIDIReceived`. Parser state persists across transport-event boundaries. It handles channel-message running status, variable-size messages, interleaved realtime messages, and fragmented SysEx. The recovered system-common switch is at MIDI `0x2cec`.

The legacy parser passes realtime `F8`, `FA`, `FB`, `FC`, and `FF`; it suppresses `F9`, `FD`, and `FE` active sensing. With its constructor flag enabled, `F5` consumes the following byte as an internal cable value rather than forwarding the pair. The subclass ignores that cable argument when emitting. These details are byte-parser behavior, not evidence of RK3 foot-controller CC mapping. Compatibility decisions for reserved-status handling and active sensing should be explicit and covered by byte-stream tests.

## Native ARM implementation specification

The replacement can implement the original behavior without loading Intel code or preserving private class layouts:

- Build an ARM64 CoreMIDI CFPlugIn against the current SDK's `MIDIDriverInterface`, using its declared COM ABI and a new bundle/factory identity. Implement interface version 2 and reference counting. Keep `FindDevices`, `Configure`, `Flush`, and `Monitor` successful no-ops until an actual scheduling/monitoring feature exists.
- Feed that plug-in from the new native transport/provider contract. Preserve logical port indices and the distinction between hardware MIDI and controller reports; expose the verified source/destination topology rather than guessed DIN/pedal ports.
- Create entities and stable endpoint identities on discovery; mark them offline on disconnect; gate input delivery using `EnableSource`. All setup/property calls follow CoreMIDI's server-thread rules; realtime delivery uses the supported I/O context.
- Use explicit, bounded message structures and one outstanding asynchronous receive per device. Keep buffers and callback state alive until completion/cancellation. Prevent a disconnect/reconnect from routing an old completion into a new device instance.
- Parse MIDI independently per source, preserving parser state across received chunks. Use current `MIDIPacketList` helpers instead of carrying the Intel binary's packed-packet pointer arithmetic into ARM code. Timestamp delivery using the native host clock.
- Send in order and report transport errors. Immediate output matches the original; do not claim timestamp scheduling while `Flush` is a no-op. Eight-byte output chunks provide direct behavioral parity; larger batches need separate device validation.
- Add the controller-to-MIDI policy as a documented native component only after the RK3 analog/digital report layout, calibration, selected MIDI messages, channels, toggling/hold behavior, and LED feedback are established. A fresh in-process mapper can replace the legacy external conversion role, but a DIN-only driver is not complete pedal support.

Meaningful verification should exercise fragmented/running-status channel messages, realtime inserted during SysEx, long SysEx across transport events, ordered multi-packet output, partial eight-byte final output, source enable/disable, invalid port/length rejection, one-request receive semantics, disconnect with pending input, and identical-device reconnect identity. Hardware validation must separately demonstrate DIN MIDI and each footswitch/expression input; observing a CoreMIDI endpoint alone is insufficient.

## Implemented portable boundary

`Driver/MIDIEvent.h` and `Driver/MIDIEvent.c` implement the fixed-width 72/80-byte user-client event serialization, hardware MIDI command-6 validation, command-7 encoding, original eight-byte output chunking, and seconds/nanoseconds/host-tick conversions. Integer fields are explicitly serialized little-endian; static assertions document native structure sizes and offsets. Report lengths, logical hardware ports, event lengths, timestamp fractions, and arithmetic overflow are rejected without silently truncating or wrapping. The MIDI path explicitly returns `RK3_MIDI_NOT_MIDI` for analog/digital commands, leaving controller policy to a separate component.

`Driver/Tests/MIDIEventTests.c` exercises actual wire bytes, fragmented transport bounds, invalid 32-bit lengths, exact port-count boundaries, controller/MIDI separation, 61-byte report capacity, eight-byte output ordering/partial tails, write-error short circuiting, timestamp ABI units, non-unit ARM host timebases, and overflow. It passes a strict C11 build with AddressSanitizer and UndefinedBehaviorSanitizer. The new ARM64 CoreMIDI CFPlugIn is implemented in `Driver/MIDI/RK3MIDIDriver.c`; its native HAL transport, lifecycle/queue behavior, stable identities, limits, and hardware-free validation are documented in [Driver/MIDI/README.md](../Driver/MIDI/README.md). Its stateful `MIDIStream` parser reconstructs complete channel/system-common messages, expands running status, emits realtime separately, and keeps SysEx fragments isolated. Parser and plug-in tests pass strict ARM64 builds with AddressSanitizer/UndefinedBehaviorSanitizer; the built bundle passes a real CFPlugIn factory/COM smoke test without calling Start or opening hardware. Actual MIDIServer Start/hotplug/DIN loopback validation remains a hardware check.

The subsequent clean hardware query confirms RK3 stereo audio input/output, wire mode 2, and one hardware MIDI input/output; see [HardwareValidation.md](HardwareValidation.md). That establishes the port counts used by the native DIN component; physical DIN operation remains untested. Separate original Controller Editor/NIHardwareAgent analysis now identifies the controller mapping policy in [ControllerMapping.md](ControllerMapping.md); it supplements the two-driver separation evidence above rather than treating the foot controls as a second hardware DIN port.

The native CoreMIDI bundle now integrates the separately reconstructed `Driver/Controller.c` in process. A distinct Controller MIDI device exposes mapped CC input and an explicit LED Feedback destination alongside the verified DIN device. Persistent built-in-pedal calibration, activation snapshots, physical switch baselines, local toggle LEDs and feedback generation checks are covered by the software-only integration tests described in [Driver/MIDI/README.md](../Driver/MIDI/README.md). These software checks do not establish physical footswitch/pedal delivery or automatic effect-state feedback from an application.
