# NI USB audio driver reconstruction

This records behavior recovered from Native Instruments' original Intel driver;
it is a specification for a new implementation, not a claim of hardware support.
No earlier driver implementation or public RK3 CoreAudio source was used.

## Evidence

Input: `NIUSBAudioDriver.x86_64`, NI 2.8.0 (R36), Mach-O x86_64 kext,
SHA-256 `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`.
Companion `audio-Info.plist` SHA-256
`0e6ba261d8d6884bdcd848c940025815a887547a1bc287f37f86ab9080e2fb44`.
The plist matches RK3 as vendor `0x17cc`, product `0x1940`.
Addresses below are original image-relative addresses, independently checked
with `nm -n` and `otool -tvV`; the kext's `__TEXT` vmaddr is zero.

Ghidra 12.2 imported the original as `x86:LE:64:default:gcc` and exported
C-like reconstructions and instruction listings to the ignored local
`.build/ni/decompiled/audio280-text` directory. Its manifest records 241
functions and 241 completed decompilations, all in the original `__text`
range `[0x710,0xe0fc)`. The exporter excludes synthetic EXTERNAL entries.
Ghidra and assembly agree on the branches below.
Some recovered method prototypes omit or mistype `this`, and indirect virtual
calls remain unresolved. Thus decompiled argument casts are not trusted ABI
definitions. Numeric offsets identify fields without inventing source names.

## Confirmed: format and command encoding

`NIUSBAudioDevice::setAudioParams(int,int,int)` at `0x2d50` sends command
`0x09` with five payload bytes:

```
[rate_code, sample_width_code, packet_capacity & 0xff,
 packet_capacity >> 8, 0x01]
```

| Sample rate | Rate code |
|---|---:|
| 44100 | 0 |
| 48000 | 1 |
| 96000 | 2 |
| 192000 | 3 |
| 88200 | 4 |
| other, including stop sentinel 255 | 255 |

16-bit maps to width code 1, 24-bit to 2, everything else to 255.
The method initializes device field `+0x8e0` to -1, sends, and polls at most
1000 times with `IOSleep(1)` while the field remains -1. Success requires its
value to become 1. A failed send or any other final value returns `0xe00002bc`.
`commandReplyDispatch` at `0x20e0`, reply case 9, copies reply byte 1 into
that field, completing the command/reply chain.
The initialized-device flag at `+0x8e8` gates this operation. Rate 255 skips
updating remembered device rate/width/capacity at `+0x914/+0x918/+0x91c`.

`createNewAudioStream` at `0x4d60` advertises stereo `lpcm`/`sint`, 24 valid
bits in 24 storage bits, mixable, with alignment discriminator 1 and byte-order
discriminator 0. The latter selects the swapped conversion on x86: the
advertised packed PCM is big endian.
For RK3 the rate branches explicitly add 44100, 48000, 96000, 192000 and
88200. These are driver-advertised formats; the hardware has not been tested.

`performFormatChange` at `0x68d0` computes:

```
frames_for_capacity = floor(sample_rate / 8000) + 11
packet_capacity = min(512, frames_for_capacity * 2 * wire_width * stereo_groups)
```

It configures both audio pipes with that capacity, sends the audio-parameter
command, and propagates the format to the other streams. Packet capacity
is an upper bound; actual outgoing microframe sizes follow received sizes.

### USB configuration and command-pipe toggle synchronization

`initUSB` (`0x8e0`) takes the first configuration descriptor's byte 5 at
`0x0a15` and calls `SetConfiguration` at `0x0a1f` without testing the current
configuration. After opening alternate interface 1 and discovering the pipes,
it invokes `IOUSBPipe::ClearPipeStall(true)` on command IN (`this+0x920`) at
`0x0d84`, then command OUT (`this+0x928`) at `0x0d99`. Both calls explicitly
pass 1 in ESI. These precede the persistent command read and command-1 query.

The virtual identity is anchored by the original bulk Read/Write slots
`+0x1d0/+0x1d8`, followed by `GetPipeStatus` (`+0x1e0`), `ClearPipeStall`
(`+0x1e8`), `SetPipePolicy` (`+0x1f0`) and low-latency Read/Write
(`+0x1f8/+0x200`), matching Apple's declared order. With `true`, this clears
the controller's halt/toggle state and sends `ClearFeature(ENDPOINT_HALT)` to
the device endpoint; the user-space equivalent is `ClearPipeStallBothEnds`.
`AbortPipe` does not reset the data toggle. See Apple's
[IOUSBPipe.h](https://github.com/apple-oss-distributions/IOUSBFamily/blob/main/IOUSBFamily/Headers/IOUSBPipe.h).
The error-recovery calls in `sendCommand` at `0x26c9/0x26db` pass **false**;
they are distinct from the startup synchronization.

### Observed current-host adaptation

The fresh user-space transport retains the already advertised bandwidth:
before audio start it validates actual pipe properties as ISO IN endpoint
`0x82`, ISO OUT endpoint `0x06`, each 512 bytes and interval 1. Current-host
`SetPipePolicy` returned `kIOReturnBadArgument` for both the desired 136-byte
capacity and descriptor-sized 512. Original calls at `0x6962/0x6988` ignore
their return values; the newly implemented engine omits this bandwidth
adjustment after validation, while command 9 still requests capacity 136.
Submission failures remain fatal.

Fresh tests then isolated an output geometry incompatibility: the current
host rejected a valid 272-byte output plan with trailing empty microframes,
logging an invalid transaction request-count/offset. Changing the frame lead
alone did not help. Trimming only trailing zero requests from the low-latency
write API call produced five seconds of completed audio/capture/clock progress;
nonempty requests, interior gaps, bytes and scheduled start frame were retained.
An all-empty plan is omitted and its schedule slot released without advancing
sample bytes or clock. Completion checks use only the submitted microframes.
This is a new user-space compatibility policy, not claimed NI behavior.

The installed macOS 27.0.1 `IOUSBLib` universal binary, SHA-256
`34571ff699fd416aec9030d13c18b7a800d9a1e04b928bae5584329d583d8201`,
provides supporting primary evidence: its arm64e
`LowLatencyWriteIsochPipeAsync` (`0x71f4`) sums requested counts at
`0x725c..0x7270`, supplies exactly that sum as its data-descriptor length at
`0x72a8`, and calls kernel selector `0x17` at `0x72e8`. Original NI output
uses duplicated memory-descriptor ranges, described below, so its backing
length differs. An end-offset bounds interaction is consistent with these
facts; the private kernel implementation was not recovered. Hardware-free
fake-interface regression reproduces the observed rejection before the fix
and verifies unchanged packet bytes/calendar plus no clock advance on an
empty plan afterward. See `HardwareValidation.md` for live validation scope.

### RK3-specific I/O initialization before engine creation

The original performs an additional initialization that is absent from a
command-1/command-9-only startup. `setupTimerCallback` (`0x3490`) checks vendor
`0x17cc` and selects product `0x1940` at `0x365d`. Instructions
`0x3668..0x367c` build the six-byte payload `00 40 40 00 00 00`, then
`0x3684/0x3689` select command 5 and length 6; `0x3691` calls `sendCommand`.
Thus the exact outgoing report is:

```
05 00 40 40 00 00 00
```

This happens after the device-info reply triggers setup and **before** engine
creation at `0x36a4..0x3717`. The exported `writeIO` method at `0x27a0`
independently identifies command 5 as the I/O-write family. The host binary
does not name the individual RK3 payload fields, so their gain/mute/routing
semantics are not asserted here. A faithful fresh startup sends these exact
device-specific defaults rather than assuming hardware power-on state is
already suitable for audio. Reply dispatch has no command-5 acknowledgement
case: successful USB write completion is the observable host-side result.

The inspected RK3 path through `initUSB`, `initHardware`, `setupTimerCallback`,
engine `initHardware`, `initCoreAudio`, `performFormatChange` and
`performAudioEngineStart` contains no additional sleep, direct-monitoring,
automatic-report or I2C command. Methods `setSleepMode` (`0x2eb0`, command 10)
and `setDirectMonitoringMode` (`0x2ee0`, command 14) exist, but no call to them
occurs on that recovered startup path. Initial channel gain values are 1.0 in
host memory (`0x590d`), not an extra USB gain command. `initHardware` also calls
`updateFirmware` at `0x1420`; that routine downloads only when a matching
embedded firmware target version exceeds the USB device-release value.
Static little-endian words at `_RK3_firmware` (`0x14a80`) are vendor `0x17cc`,
product `0x1940`, target release `0x000d`. The fresh query recorded in
`HardwareValidation.md` reports release `000d`, so the original update branch
is not taken for this verified device. No firmware binary was executed or
written during this analysis.

## Confirmed: rings and scheduling

`init` at `0x4170` records the input/output channel counts and device wire mode.
`initHardware` at `0x5320` derives
`stereo_groups = max(input_channels, output_channels) >> 1`. Wire width is
3 bytes for mode 0, 4 bytes for all nonzero modes. `initCoreAudio` at `0x4b20`
initializes the sample rate to 44100 and creates stereo streams.

Each stereo stream is spaced by `0x20000` bytes; the active packed-24 ring
wraps at `0x18000` bytes, exactly 16384 stereo frames. The interleaved USB
output ring has `wire_width * stereo_groups * 32768` bytes, also 16384 frames.

`performAudioEngineStart` at `0x5fb0` first sends the rate-255 stop sentinel,
aborts both audio pipes, waits up to 1000 x 1 ms for outstanding input work,
clears input and output IRPs and sample buffers, and fills check bytes. It
sets the initial input byte cursor to 0 for mode 3 and 2 otherwise. It starts
64 input IRPs, each containing eight microframe descriptors, beginning at
USB frame number +100. It then sends the desired rate/width/capacity. Failed
parameter commands are retried after 200 ms (four additional attempts).

The initial stop call is specifically unchecked: `0x5fd6` calls
`setAudioParams(255,0,capacity)`, and `0x5fdb` immediately loads an unrelated
timer pointer without testing EAX. Desired-rate calls later test the returned
status and retry. Therefore the original start path tolerates a negative stop
reply even though `setAudioParams` itself requires reply value 1. The fresh
device returned `[09,00]` for `[09,ff,ff,88,00,01]`; accepting that received
stop value while still rejecting write/timeout failures is a bounded new-host
policy consistent with the original's unchecked stop, not a general command-9
success rule. After stop, original ISO IN calls Abort/ClearPipeStall(true) at
`0x6016/0x6032`, and ISO OUT at `0x604e/0x606a`; those clear results are also
unchecked.

`queueInIrp` at `0x6640` requests the fixed 512-byte maximum for each of
eight microframes, increments the scheduled USB frame number before submission,
and retries submission at most six times. Its `0xe00002ee` failure path clears
sample output, cancels timestamp updates and schedules a restart timer after
100 ms. The initial first submitted frame is therefore current USB frame +101.

`readHandler` at `0x7a80` pairs each input IRP with a free slot from 128
output IRPs. For each
of its eight microframes it copies the received actual byte count into the
output requested count when the input frame status is success or
`0xe00002e7`; other statuses request zero bytes. It requeues input and then
queues the paired output. The output path therefore follows device input
cadence rather than synthesizing a 44.1-kHz fraction pattern.

`queueOutIrp` at `0x7620` sums those eight sizes and attaches one or two ring
segments to the output descriptor, splitting at the USB ring wrap. It submits
eight microframes at the saved USB frame number and advances its ring cursor
by the submitted byte total. `writeHandler` at `0x7ec0` advances the CoreAudio
sample-frame position from the IRP's recorded ring offset. `getCurrentSampleFrame`
at `0x68c0` returns that position.

### Exact input/output queue relationship

`isocIrp` occupies `0xf0` bytes. Its frame list begins at `+0x40` and has
eight 16-byte records (`status:+0`, requested count:`+4`, actual count:`+6`,
timestamp:`+8`). Its saved USB frame is at `+0xe0`; output ring sample position
is at `+0xd8`, and the output-busy flag is at `+0xe8`.

`allocIsocIrps` at `0x70e0` gives input slot `i` the memory range
`input_base + i*4096`, length 4096. `readFrameList` at `0x6f60` reads microframe
`j` at that base plus `512*j`, consuming only that frame's actual count.
Short received packets do **not** pack subsequent microframes together in the
input buffer. All 64 input slots occupy 262144 bytes. Output payloads instead
consume a continuous ring range whose length is the sum of actual counts;
their eight requested counts preserve the device's input packet boundaries.

The engine's `+0xb970` is a single queue counter. If startup observes USB
frame `T`, `performAudioEngineStart` stores `T+100`. Each `queueInIrp`
preincrements it and saves the result in that input IRP. Thus the 64 startup
reads are scheduled at `T+101` through `T+164`. In `readHandler`, these exact
operations occur under its queue lock:

1. Choose a free output slot from 128 slots.
2. Copy the eight accepted actual input counts into that output's requests.
3. Requeue the same input IRP, preincrementing the global counter and replacing
   its saved USB frame.
4. Queue output using the **current global counter**, copied into output
   `+0xe0` by `queueOutIrp`.

For ordered callbacks, completed input slot 0 originally saved `T+101`, then
requeues at `T+165` and generates output at `T+165`. This produces a 64 USB-frame
lead in steady state. There is no separate `old_input_frame + delay` expression
in the output submission. A native implementation must preserve the original
saved frame until received-data/timestamp processing finishes, before reusing
the input slot. Failed submissions in a new implementation should stop and
drain rather than expose a planned position as completed hardware progress.

`queueOutIrp` records `floor(output_byte_cursor/(2*wire_width*groups))` as the
output IRP's sample position before advancing the cursor by its total bytes,
modulo `wire_width*groups*32768`. `writeHandler` copies that recorded position
to the current CoreAudio position before handling completion status. The
original multi-descriptor construction repeats its one or two ring ranges
twice; the actual eight transfer requests still sum to the single payload
length. The repeated descriptor metadata does not imply doubled requested
audio bytes.

### Priming and error transitions

Startup sets running (`+0xb98d`), priming (`+0xb98f`) and first-unit (`+0xb990`)
to 1; clears restart (`+0xb992`), sync-error (`+0xb993`), device-error
(`+0xb994`) and timestamp-established (`+0xb98c`). In mode 2, the packed input
cursor begins at 2. `readFrameList` returns immediately throughout priming,
and `clipOutputSamples` returns without accepting mixed samples. Output check
patterns and silent payloads still flow during this phase.

The transition is tied specifically to **input slot 63's callback**, rather
than counting 64 arbitrary successful callbacks. After its input requeue and
output submission, `readHandler` resets the output-slot search cursor, input
IRP cursor and microframe cursor (`+0xba28/+0xba24/+0xba1c`) to zero, clears
priming, and calls `readFrameList` again. Mode-2 first-unit suppression is
cleared by its first processed byte group. Initial queued input is therefore
priming traffic, not immediately exposed capture PCM.

`readFrameList` advances across 64 slots and eight microframes in order,
stopping at status `0x6c6c6974` (`kUSBLowLatencyIsochTransferKey`, `'llit'`).
Apple's installed `USB.h` explicitly identifies this as the pending marker
written by the low-latency stack until the hardware transaction completes.
Statuses 0 and `0xe00002e7` permit decoding; other completed statuses advance
the cursor without audio. A sync or device error makes `readMicroframe` at
`0x8670` increment `USBstreamingErrors` and, with correction enabled, arm the
panic timer immediately and set restart. `panicTimerCallback` at `0x5950`
invokes the stop/start virtual slots and then clears restart. Lack of a free
output slot and `IsoTooOld` submission failures schedule that restart after
100 ms instead.

`Driver/AudioSchedule.h/.c` captures these original queue relationships as a
hardware-free state model: per-input saved frames, input-led requested counts,
free output slots, wrap segments, sample positions and the slot-63 priming
transition. It rejects oversized actual counts and arithmetic overflow.
Its clock helper preserves NI's extra one-frame interval and integer division
ordering, including the upward fractional rounding for a packet preceding
its anchor. Sanitizer tests exercise these cases, queue exhaustion and frame
overflow. USB submission and callback ownership remain outside that module.

## Confirmed: byte layouts and check bytes

The mode is not selected from the product ID in the engine.
`commandReplyDispatch` at `0x20e0`, case 1, copies the 14 bytes after the
command byte into device `+0x984`. Audio mode is therefore reply byte 14.
Input channel count is reply byte 9 + byte 11; output is byte 8 + byte 10
(`setupTimerCallback`, `0x3490`, inlined engine creation at `0x36a4`).
At `0x1b5d`, `createAudioEngine` passes device byte `+0x991` into engine
`init`, which stores it at `+0xb978`. Establishing RK3's actual returned
mode remains necessary. Although engine `init` receives the endpoint maximum
packet size, it sets its own maximum at `+0xba14` to 512.

Mode 0 (`readMicroframeMode0`, `0x81f0`) consumes one byte for each stereo
group at each ring-byte position and transposes into per-group rings. No
check byte is removed.

Mode 2 (`readMicroframeMode2`, `0x82b0`; check fill `0x87f0`) interleaves
group-wide byte planes. With `G` stereo groups, the output ring has check
planes at offsets `2*G` and `6*G` within each `8*G`-byte stereo-frame period.
Their tags are `2*g+1` and `2*g`, respectively, for group `g`.
The input compares a check byte when its packed-audio ring-byte cursor `b`
satisfies `b % 3 == 2`, expecting `(2*g | ((b/3)&1)) ^ 1` in the low six bits;
bit 7 independently
sets a second error flag. The check phase persists across packet boundaries
through the ring-byte cursor. The output packer at `0x6ab0` starts the six-byte
copy for frame `f` at source `(6*f-2) mod 0x18000`, with destination
`(8*G*f+g) mod (0x20000*G)`. After each byte it advances the destination by
`G`, plus another `G` after a source byte whose index modulo 3 is 2; source
wrap is `0x18000`. Thus its output period is
`[R1(previous), R2(previous), tagOdd, L0, L1, L2, tagEven, R0]`, each item a
`G`-byte plane. The two-byte phase displacement is significant and must not be
lost in a generic PCM24-plus-tag implementation. Input starts at cursor 2,
so its first check byte is consumed before writing the first received audio
plane at ring byte 3.

Mode 3 (`readMicroframeMode3`, `0x8430`; check fill `0x8890`) stores an
eight-byte stereo group as:

```
[L0, L1, L2, 2*g, R0, R1, R2, 2*g+1]
```

Its reader copies offsets 0,1,2,4,5,6 to the packed-24 stereo ring and checks
offsets 3 and 7. Its writer inserts the same gaps in `clipOutputSamples`.
This grouping is a confirmed mode-3 behavior; RK3 mode selection is still
unconfirmed. Both modes set an error flag at engine `+0xb993`; when that flag
and the correction-enabled flag `+0xb991` are both set, subsequent input
audio bytes are replaced with zero. Startup flags suppress or alter some
check comparisons, so a new implementation needs explicit startup state.

`AppleClipOutputSamples` at `0xc440` dispatches the advertised format to
`Float32ToSwapInt24_Portable` at `0xc6b0`. `AppleConvertInputSamples` at
`0xc550` uses `SwapInt24ToFloat32_Portable` at `0xca40`. Input conversion
sign-extends the three big-endian bytes and normalizes by `2^23`.
The portable output path scales by `2^31`, adds 128 (half a 24-bit LSB),
limits the upper result to `0x7fffff`, truncates to integer, and emits bits
31..8 most-significant-byte first. Constants at `0xfe28/0xfe30/0xfe40` are
128.0, 2147483519.0 and 2147483648.0. Native-byte-order discriminator 1 takes
the separate little-endian conversion path at `0xd600/0xc910`; its SSE rounding
is different. Undefined Intel conversions for NaN and out-of-range negative
values are not a portable compatibility contract.

## Confirmed: stopping and power

`performAudioEngineStop` at `0x6800` clears the running flag, disables and
cancels the panic, timestamp and statistic timers, and aborts both audio
pipes. It returns success. `tearDown` at `0x5e30` and `completeMemory` at
`0x5f20` handle later release; asynchronous ownership must be retained until
all pending callbacks finish in a fresh implementation.

`willTerminate` at `0x1830` resets the outstanding command result to zero,
disables and removes registered user clients, calls the engine-deactivation
virtual slot, clears its engine pointer and marks the device disconnected.
`didTerminate` at `0x1a70` closes/releases the interface and USB device before
calling its superclass. These are separate lifecycle stages.

`performPowerStateChange` at `0x1fe0` calls the base implementation first.
When changing from state 0 to state 1 or 2, it performs two USB device calls
and invokes engine `awakeFromSleep` if initialized. Entering state 0 invokes
`enterSleep`. Exact USB virtual call identities need relocation confirmation.

`enterSleep` at `0xbc50` cancels the timestamp timer and zeros four anchor
fields. `awakeFromSleep` at `0xbcb0` resets the timestamp refresh counter and
wall-clock interval, clears output sample streams, updates the USB-cycle
clock estimate and obtains an anchor, then rearms the timestamp timer for
128 ms when the device remains present.

`updateWallTimePerUSBCycle` at `0xb880` samples the USB frame number before
and after the uptime read until they agree. It computes the interval in
nanoseconds times 10000 and applies `(new + old*1023 + 512) >> 10` smoothing.
`getAnchorFrameAndTimeStamp` at `0xba80` waits at most 1.1 ms for the next USB
frame boundary. `takeSimpleTimeStamp` at `0x8100` reconstructs the ring-wrap
time from the IRP frame and fractional byte position and rejects backwards
timestamps. These formulas require explicit overflow-aware integer types in
an ARM implementation.

## Reconstructed fixtures

`Fixtures/audio-vectors.json` contains synthetic command and byte-layout
vectors derived from the instructions above. These are not captured device
traffic. They preserve the mode-2 phase displacement and mode-3 tag gaps for
future hardware-free tests without selecting either as RK3's observed mode.

`Driver/AudioCodec.h` and `.c` implement a fresh, allocation-free portable core
from this evidence. They preserve opaque PCM byte order while packing modes
2 and 3, snapshot output frames across short reads, accumulate partial input
planes/groups, preserve ring phase and expose latched sync/device error flags.
The caller supplies and clears per-group rings. Priming suppresses the original
conditional checks until the caller marks startup complete; explicit even
mode-3 tags remain checked. Optional correction zeros subsequent audio after
a sync error. A new stream resets state by initialization. The default original
ring size is 0x18000; smaller multiples of six support deterministic wrap tests.
PCM helpers support both explicit byte orders. The writer uses the recovered
portable finite-sample scaling, with deterministic clipping for infinities and
zero for NaN. It does not reproduce undefined legacy conversion behavior.

`Driver/Tests/AudioCodecTests.c` checks recovered layout vectors, every split
point across several frames, byte-at-a-time output, output snapshot stability,
receive phase, ring wrap, priming and corrupt check behavior, 32 stereo groups,
invalid state/capacity, and both PCM byte orders. It passes strict C11 warnings,
AddressSanitizer and UndefinedBehaviorSanitizer without USB or legacy execution.

## Unknowns and implementation boundaries

- A fresh device-info query during this session independently reported two
  audio inputs, two outputs, mode 2 and one MIDI input/output. The native first
  format therefore targets stereo 48000 Hz/mode 2. Fixed nominal output packet
  sizes remain inappropriate: the implementation follows actual input sizes.
- Exact cold-start acceptance/recovery thresholds, timestamp behavior after
  missed USB frames, and unplug during queued I/O need hardware captures.
- Sleep/wake and terminating callback races need a new implementation's
  concurrency model; original field offsets are evidence, not an ABI to copy.
- The portable core does not supply a USB service or CoreAudio plug-in. Mode 0
  is documented but not implemented in this core. No original binary was
  executed or installed.

## Fresh native ISO implementation

`Driver/NativeAudioISO.h/.c` is a new IOUSBLib300 adapter for the verified
stereo/mode-2 topology. All IOKit calls belong to the NativeUSB session worker.
It allocates both data buffers and frame-list buffers with
`LowLatencyCreateBuffer`, as required by the current Apple SDK, posts 64 input
requests, follows input counts for 128 available output slots, and preserves
the NI requeue-before-output ordering and priming transition. Startup command
9 requires acknowledgement, with the NI stop sentinel before queueing and
the recovered 48000/24-bit/capacity-136 payload afterward. Retry delays pump
the owning CFRunLoop so queued audio callbacks remain serviceable.

The HAL-facing Float32 stereo queues use lock-free atomic indices and bounded
single-producer/single-consumer storage. Contending HAL calls return zero
instead of blocking. Output starvation inserts silence at actual device
cadence; the enqueue API still returns only frames actually accepted.
Capture returns only decoded frames that have arrived. The clock publishes
completed output sample counts with USB frame-list timestamps, falling back
to the callback's actual host tick when the timestamp is absent; it does not
advance an audio counter from elapsed host time alone. That callback fallback
has additional scheduling jitter and still needs hardware timing validation.

Stop aborts both pipes and retains every buffer/context until all callbacks
drain. `release_usb` frees low-latency buffers before interface close while
retaining the CPU queues/object for late HAL calls; a retained COM reference
protects the interface object through final destruction. Pending callbacks
prevent release. Corrupt input, partial output, invalid status or a failed
ISO submission stops the stream and exposes an error to its owner.

`Driver/Tests/NativeAudioISOTests.c` uses a fake IOUSBLib vtable, rather than a
USB device, to verify startup command bytes, all 64 scheduled reads, priming
silence, real sample conversion, capture queues, completion clocks, failed
ACK/submission, corrupt input, abort/drain, detach and final COM ownership.
Strict ARM64 C11 warnings plus AddressSanitizer/UndefinedBehaviorSanitizer
pass. These checks establish software behavior; they do not establish audible
hardware output or stable device timing.
