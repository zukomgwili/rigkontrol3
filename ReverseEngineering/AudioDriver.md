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

### Current-platform memory binding

The earlier native implementation used IOUSBLib low-latency transfers but
introduced its own per-transfer PCM snapshots. Its trailing-zero trimming,
empty-request omission and later 4 ms calendar were compatibility workarounds
in that implementation. They are superseded by the live-ring binding below;
they must not be treated as NI behavior.

Apple's current `IOUSBHostObject.h` documents controller-compatible mapped
`IOBufferMemoryDescriptor` allocation without DMA bouncing. The public
`IOUSBHostIsochronousTransaction` structure supplies an offset and `Wrap`
option, including a transfer crossing the end of its backing ring. Thus queued
requests can reference the same storage addressed by host sample writes,
without a software PCM copy. Explicit `KernelUserShared` allocation for shared
transaction lists is available on macOS 26.4+. The fresh audio engine requires
that API rather than guessing that an older fallback has equivalent visibility.
These declarations are in the selected SDK's `IOUSBHost.framework` headers;
`Driver/NativeHostUSB.m` implements only this platform seam.

The verified pipes are ISO IN `0x82` and OUT `0x06`, maximum 512 bytes,
interval 1. Command9 still requests NI's capacity 136. The old user-space
`SetPipePolicy` call rejected both 136 and 512 on this host; original NI calls
ignore its return. The new provider validates actual descriptors instead.
Mapped storage and transaction lists remain owned through callback delivery.
Mapped status is read using the SDK's transaction-completion publication
contract. The modern public contract does not establish NI's exact primary-
interrupt publication timing. `rk3_host_primary_lists_supported` checks only
shared-allocation API availability, not measured interrupt cadence. The public
transaction options are `None` and `Wrap`; no documented `Interrupt` option
was found, so no such flag is invented. Mocks establish
backing identity and safe ownership; actual hardware prefetch/deadline timing
still requires validation. Mapped memory alone does not prove arbitrary late
writes are safe.

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
sets the initial input byte cursor to 0 for mode 3 and 2 otherwise. It attempts
64 input IRPs, each containing eight microframe descriptors, beginning at
USB frame number +100. Individual initial IN failures are logged; the loop
continues through all slots before sending the desired rate/width/capacity
(`0x6342`..`0x639a`, then format call `0x63bc`). A restart flag suppresses
subsequent queueIn submissions after a TooOld panic. It then sends the desired rate/width/capacity. Failed
parameter commands are retried after 200 ms (four additional attempts).

The initial stop call is specifically unchecked: `0x5fd6` calls
`setAudioParams(255,0,capacity)`, and `0x5fdb` immediately loads an unrelated
timer pointer without testing EAX. Desired-rate calls later test the returned
status and retry. Therefore the original start path tolerates a negative stop
reply even though `setAudioParams` itself requires reply value 1. The fresh
device returned `[09,00]` for `[09,ff,ff,88,00,01]`; accepting that received
stop value is supported by the command worker, but the engine ignores the
initial stop callback's entire result, including failure, just as NI does.
Real-format acknowledgement still requires 1. Pending command storage remains
owned through retirement, and current owner cancellation/device-loss checks
still reject a cancelled or removed start. After stop, original ISO IN calls Abort/ClearPipeStall(true) at
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
the input slot. Planned positions are never reported as completed progress.
Error handling follows the distinct IN/OUT branches described below.

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
and `clipOutputSamples` marks the stream written but returns without copying PCM. Output check
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
invokes **IOAudioEngine pause/resume wrappers** and then clears restart. Vtable
relocations at `0x11640/0x11648` resolve the wrappers: pause stops the engine,
clears host buffers and notifies clients; resume notifies the host to restart.
This is not a direct call pair to performAudioEngineStop/Start. Lack of a free
output slot and **IN** `IsoTooOld` submission failures schedule that restart after
100 ms instead. OUT submission errors log and leave the chosen output slot
busy; they do not directly request panic. After six failed non-TooOld IN
submissions, `queueInIrp` returns the error without arming panic (`0x673f`..
`0x6757`). The TooOld branch at `0x6768` clears output and arms 100 ms.

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
mode was independently verified as 2 by the fresh command1 reply. Although engine `init` receives the endpoint maximum
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
This grouping is a confirmed mode-3 behavior; the attached RK3 reports mode 2. Both modes set an error flag at engine `+0xb993`; when that flag
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
`enterSleep`. The calls resolve to `SuspendDevice(false)` and `GetDeviceStatus`, checked
against known neighboring device methods and Apple's historical IOUSBDevice virtual order.

`enterSleep` at `0xbc50` cancels the timestamp timer and zeros four anchor
fields. `awakeFromSleep` at `0xbcb0` resets the timestamp refresh counter and
wall-clock interval, clears output sample streams, updates the USB-cycle
clock estimate and obtains an anchor, then rearms the timestamp timer for
128 ms when the device remains present. No EP1 read-rearm call was found in
this wake path. Original `commandReplyDispatch` handles NotResponding by
aborting command IN and OUT and returning without rearming. A power-triggered
EP1 retry would therefore be an invention unless further evidence establishes
it. Current host sleep/wake lifecycle equivalence remains a hardware check.

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

## Faithful native ring and timing port (0.2.0, build 5)

`Driver/AudioRing.h/.c` implements NI's sample-position-addressed mode-2/G1
output ring. `prepareMemory` (`0x59d0`, especially `0x5a5b..0x5a6b`) stores
the descriptor at `+0xb928` and its CPU mapping at `+0xb930`. `queueOutIrp`
references ranges of that descriptor; `clipOutputSamples` writes through the
same mapping. This identity is the key dataflow preserved by the port.

For frame `f`, wire lanes `{0,1,3,4,5,7}` receive packed bytes
`(6*f-2+k) mod 98304`, `k=0..5`. The wire ring is 131072 bytes / 16384 frames;
its check lanes 2 and 6 are untouched by clip. The original host framework
splits a wrapped block into tail conversion/clip followed by head
conversion/clip. Converting the entire wrapped block before clipping can
change the two-byte carry, so that order is tested explicitly. Priming marks
written state without copying PCM. NI's inactive clear is also literal: zero
wire lanes 0..5, leave lanes 6/7 and the packed ring untouched. The surprising
check-lane footprint is retained rather than corrected.

`NativeAudioISO` passes the same mapped output ring to every queued OUT
request, with each microframe's recovered offset and wrap option. It keeps all
eight transactions, including trailing or entirely empty requests. Initial IN submission errors remain diagnostic and do not invent an early
startup return; the owner still requires an actual valid clock before exposing
started audio. An empty
request retains its slot until completion and reports its saved start
position. Failed OUT submissions retain their reserved slot as NI does.
Input slots keep separate 4096-byte backing ranges and eight fixed 512-byte
microframe offsets. The original T+101..164 priming reads and paired T+165
requeue/OUT calendar are unchanged. The 64 ms queue horizon is not measured
audio latency: host writes future sample positions in already queued storage.

The HAL places output using `mOutputTime.mSampleTime` and captures using
`mInputTime.mSampleTime`, modulo the 16384-frame ring, including negative
initial sample times. Capture polls mapped transaction state before conversion,
then reads those exact sample positions repeatedly without consuming a FIFO.
NI submits input lists with updateFrequency zero, publishing the complete
eight-microframe IRP at primary completion. The modern provider exposes
per-transaction completion status, so the engine waits for all eight entries
before decoding the batch. This preserves the complete actual-byte denominator
used by NI timestamps even when the asynchronous callback is delayed. It does
not by itself prove that modern kernel updates occur at NI's primary-interrupt
time; that platform timing remains an explicit validation gap.

A bounded ownership flag serializes decoder state against list reset on the
modern worker/host threads. Atomic 64-bit stereo snapshots preserve coherent
packed-frame reads, including the currently partial frame. The capture path
allocates nothing, takes no mutex, makes no USB call and never drops a backlog
to make a FIFO appear current. The wire output uses a separate bounded render
ownership flag. These synchronization mechanisms are current thread/ARM
bindings, not new audio queue policies.

### Original-to-ARM binding account

| Original NI behavior | ARM/current-macOS binding | Evidence or remaining limit |
| --- | --- | --- |
| `prepareMemory` / `queueOutIrp`: same wired output backing, split ring ranges | IOUSBHost optimized mapped buffer, transaction offsets and `Wrap` | SDK documents no bouncing; CPU identity/wrap/lifetime tested; physical DMA timing pending. |
| `clipOutputSamples`: write host `firstSampleFrame` into ring | `mOutputTime` addressed `AudioRing` write, same queued backing | Exact mode-2 lane/carry/wrap/priming vectors and deferred-pointer tests. |
| `convertInputSamples` / `readFrameList`: primary polling then positioned conversion | `mInputTime` ring reads, complete-IRP primary gate, bounded decoder ownership and atomic stereo views | Shared storage and transaction-completion updates documented; exact primary timing unproved; delayed-callback and partial-batch tests. |
| `writeHandler` / `getCurrentSampleFrame`: completed OUT start position | Saved start position plus USB-frame time | Position and integer timestamp vectors; no completion-receipt clock fallback. |
| `takeSimpleTimeStamp` / timestamp timer | Exact input boundary fraction; USB/host anchors refreshed at 128 ms | Initial and variable-length ring-wrap vectors; hardware drift pending. |
| IOAudioEngine pause/resume panic | AudioServerPlugIn configuration handshake, preserving EP1/USB generation | SDK stop/Perform/restart ordering; host stop/restart observed in logs; sustained reliability remains open (.11). |
| Last-output-client inactive clear | Output `StreamIsActive`; original literal wire clear | Clear bytes tested; host last-client transition equivalence pending. |
| Kernel controller-only `ClearPipeStall(false)` | Public IOUSBHost both-end clear with interrupted-read retirement gate | Unavoidable API difference; no failed-command replay; error matrix tested. |
| Sleep/wake wire clear and clock reset | Host audio lifecycle expected to stop/restart engine | Physical verification required, especially if host keeps I/O active; no invented EP1 wake retry. |

### Exact positions and zero timestamps

`writeHandler` publishes the completed OUT request's **start** ring position,
not its end position or an elapsed-host-time sample counter. The port retains
that position and the corresponding USB-frame anchor. The HAL's zero timestamp
uses the input ring-boundary event directly; it is not back-extrapolated from
a nominal 48 kHz completion timestamp.

At the first post-priming data, `readFrameList` calls `takeSimpleTimeStamp`
with byte offset zero before payload consumption. With first post-priming
input saved at T+165, NI's extra frame gives zero time T+166. Mode2 then
records each packed-ring wrap immediately after its check byte, before the
next payload byte; bytewise processing therefore uses consumed offset `b+1`.
`takeSimpleTimeStamp` sums **all eight actual byte counts**, including their
prefix before the event. A variable-size example with counts
`40,40,40,40,48,48,48,48` totals 352 bytes. A wrap at consumed offset 125 in
USB frame 1506 gives zero time **1507 ms + 125/352 ms**, sample frame 16384.
Using nominal 16384/48000 to infer that time is incorrect.

The worker refreshes actual USB-frame/host-time anchors every 128 ms with NI's
bounded 1.1 ms boundary probe and `(new + old*1023 + 512) >> 10` smoothing.
RT capture records exact boundary coordinates without USB/time-anchor work.
Backwards timestamps are rejected. The public clock becomes valid only after
both real output progress and an input zero event. Original format metadata
uses `f=floor(rate/8000)+11`: input latency `4*f`, output latency `8*f`, sample
offset `8*f`, hence **68/136/136 frames at 48 kHz**. The ring period 16384 is a
clock boundary, not an audio batch size. The current HAL maps NI's sampleOffset
136 to `kAudioDevicePropertySafetyOffset` in both input and output scopes.
That is a host metadata binding, not the old IOAudioEngine ABI; actual host
write-ahead and latency behavior still require validation.

### Recovery and unavoidable platform substitutions

The original kernel IOAudioEngine/IOUSBPipe ABI cannot run inside an ARM user
process. Its pause/resume behavior maps to the current AudioServerPlugIn host
configuration handshake: `RequestDeviceConfigurationChange` causes the host
to stop I/O before `PerformDeviceConfigurationChange` and restart afterward
(the SDK documents this ordering). The notification is dispatched off the USB
worker, coalesced and checked against the current connection. Recoverable
audio panic preserves EP1, report history, controller baselines, assignments,
calibration and USB generation. It aborts/drains only audio work. A successful
restart changes the audio clock seed. Actual device termination still uses the
whole-device lifecycle.

Original panic delays and IN/OUT error distinctions above are retained.
A failed transfer remains diagnostic even if no immediate panic is specified.
The host's output `StreamIsActive` state is mapped to NI's inactive-output
clear. Its equivalence to NI's last-output-client transition while input
continues must still be checked against the actual host. Likewise, actual
sleep/wake must verify that host StopIO/StartIO restores the original wire
clear, clock-anchor reset and timer behavior. No speculative EP1 wake retry
was added.

Stop prevents new audio submission, aborts audio pipes and retains mappings,
transaction lists, callback contexts and interface ownership until callbacks
and RT readers drain. Modern completion blocks are marshalled to the owning
worker; they retain a cached run loop across publication so early retirement
cannot cause a use-after-free. Pending storage is not freed just because a
caller timed out. The public IOUSBHost clear-stall operation resets both ends,
whereas NI recovery sometimes used controller-only clearing; see
`NativeUSB.md` for that explicit necessary substitution.

Tests cover the production ISO/Host/HAL seams with advancing USB time, deferred
callbacks, future sample writes into queued memory, wrap, full-IRP primary
publication, exact zero-event fractions, empty requests, original retries and
panic delays, teardown and early completion retirement under ASan/UBSan.
These establish software invariants. Actual controller DMA visibility, clean
sustained guitar audio, latency and recovery are still installation/hardware
acceptance work. Build 4 failed those checks. Version 0.2.0/build 5 is now
installed; `HardwareValidation.md` records its limited silent run and subsequent
physical results. A silent callback/status check alone is not audible acceptance.
