# Original direct-monitor control on ARM

Tracking: `rigkontrol3-40h.1`. This implements the original device-level
direct-monitor operations through the existing native USB owner. Physical
routing meanings and installed acceptance belong to the existing qualification
milestone. No routing labels, startup setting or persistence are inferred.

## Original contract

The reference is the NI audio 2.8.0 x86-64 slice, SHA-256
`41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`.
Only five previously exported ASM functions were read, under the bounds recorded
in Beads before reading. No new decompilation or callee-body recovery was needed.

| Operation | Recovered behavior |
| --- | --- |
| SET, `0x2ee0` and wrapper `0x97f0` | Use the scalar's low byte; send command `0x0e` with that one-byte payload once. Ignore the send result. Do not update the mode cache. The attached-device wrapper returns success. |
| GET, `0x2f10` and wrapper `0x9820` | Set the shared device DWORD at `+0x8d4` to `-1`, send command `0x12` without payload once, and ignore its send result. Check the cache before each of up to 1,000 `IOSleep(1)` calls. Return a received byte, or `0xff` after the final sleep **without another cache read**. The wrapper returns success and zero-extends the byte. |
| Reply, `commandReplyDispatch` at `0x20e0` | The command-`0x12` branch at `0x2524` copies unsigned reply byte 1 into the shared DWORD. It has no query token or local pending-query check. |

The four-byte jump-table entry at `0x25a8` is `c4 ff ff ff`, signed `-60`.
Adding it to table base `0x2560` yields `0x2524`. Import metadata identifies
the sleep call as `IOSleep`; its callee body was not read. The private proof
record pins the original reference, five exports and exact metadata/data reads.

All 256 byte values are preserved. No accepted mode subset or physical
stereo/mono/off routing labels were established. A hardware byte `0xff` and
poll exhaustion have the same original return value. A delayed same-device
reply can update the shared cache; a returned byte is not proof that a
particular query caused it. The original wrapper tests the device pointer,
without requiring a running audio engine.

## Native binding and necessary platform differences

The audio DEVICE exposes one new global/main CFData property, `ndmo`
(`0x6e646d6f`). Both the qualifier and result/data are property-list objects;
no raw process pointer or old NI selector ABI is transported.

| Structure | Layout |
| --- | --- |
| `RK3NativeMonitorQuery`, 16 bytes | `uint32_t version=1`, `uint32_t reserved=0`, `uint64_t generation` |
| `RK3NativeMonitorValue`, 16 bytes | `uint32_t version=1`, `uint32_t mode`, `uint64_t generation` |

Generation must be nonzero and current. SET truncates `mode` to its low byte;
GET returns a value from 0 through 255. NativeClient validates a temporary
reply before changing the caller's output. Existing property layouts and
the generic command whitelist are unchanged.

The owner queues only the cache-reset/query-send operation. The non-RT property
caller performs the original check/sleep loop, holding its own session reference.
No HAL/session lock is held across submission or sleep, and the USB worker
continues processing other commands and servicing existing audio callbacks,
clock maintenance and recovery. Interrupted sleeps retry their remaining
duration; interruption fragments do not consume extra poll iterations.
The count and requested sleep duration are preserved, not a hard wall-clock
one-second execution guarantee.

The modern native boundary adds explicit queue, schema and physical-cohort
errors. Queued cancellation/deadline checks precede any cache reset or write.
The existing synchronized committed USB generation supplies the lifetime
guard; neither raw specification availability nor an audio-engine pointer is
a monitor precondition. After the last sleep, a cohort-only check can reject
disconnect/close without reloading the cache. A failed native boundary call
leaves GET output unchanged. These errors are not claimed to be NI IOReturn
values. Normal original poll exhaustion remains a successful `0xff` result.

Unlike the old descriptor branch's unchecked second byte, the native callback
accepts a mode only when the modern completion reports at least two valid
bytes. It does not reinterpret an unreported buffer tail as a fresh reply.
This bounded framing difference and the native lease/error checks are explicit
platform bindings; valid original packets, polling and ignored send outcomes
are preserved. SET does not optimistically change the shared cache or issue a
hardware-state notification. No additional query, retry, reader, thread,
timer, reconnect write or audio Stop/Start behavior is introduced.

## Native tool

`Tools/build-native.sh` builds `rk3-direct-monitor`. Help and invalid arguments
return before any CoreAudio device lookup. The explicit device operations are:

```sh
rk3-direct-monitor get
rk3-direct-monitor set U32_MODE
```

SET sends one byte and performs no automatic GET. Its successful completion
retains NI's ignored-send-result semantics and does not confirm physical
routing. GET prints the raw byte; `255` retains the ambiguity described above.
The tool uses the installed sole owner, never a direct USB claim or audio start.
Do not sweep unknown mode values to invent routing labels.

Source/offline validation and its limits are recorded in
[DirectMonitorImplementation.json](Evidence/DirectMonitorImplementation.json).
Installation and physical routing are not implied by CPU or fake-host tests.
NI ring, calendar, clock, transfer, recovery, MIDI, calibration and LED policy
are preserved. SIP settings are untouched. Apple case `23183489` and the
output-buffer attachment/cleanup question are independent of this control.
