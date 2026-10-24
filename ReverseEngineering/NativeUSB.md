# Native USB transport

`Driver/NativeUSB.h/.c` supplies the reusable native owner of the RK3 USB
interface. It derives from the fresh diagnostic probe and the NI evidence
recorded in `AudioDriver.md` and `MIDIDriver.md`. It does not use the earlier
bridge application or a public RK3 driver implementation.

`rk3_usb_open` creates one detached worker and returns immediately. The
worker enumerates only VID `17cc` / PID `1940`, refuses ambiguous accessible
devices, and retries absent/failed devices. Like original NI `initUSB` at
`0x8e0`, it reapplies advertised USB configuration1 even when already
configured, closes the device and verifies readback before claiming the
newly instantiated interface. It then opens interface0 without
seizure, selects advertised alt1, validates bulk EP1 IN/OUT and isochronous
EP2 IN / EP6 OUT, and starts its persistent 64-byte command reader before
querying device information. Connection is published only after a real
command1 reply validates stereo input/output and wire mode2.

The verified reply is:

```
01 0d 00 06 00 03 09 29 02 02 00 00 01 01 02
```

Its observed topology is audio 2-in / 2-out, MIDI 1-in / 1-out, mode2. The
first audio backend supports 48 kHz stereo Float32 host data only, through
`NativeAudioISO` and the independently reconstructed codec/scheduler.
After device-info validation and before creating the audio engine, the
worker sends the exact RK3-specific NI initialization
`05 00 40 40 00 00 00` (`setupTimerCallback`, `0x365d`..`0x3691`). Its purpose
remains unknown. Actual write completion is required; the original has no
command5 acknowledgement branch, so none is invented.
The original controller setup then supplies the exact 33-byte
`0f 02 <31 zero bytes>` configuration, subscribes with `0b 01 02 05`, and
queries current digital inputs with command04. Connection generation is
published only after a bounded report4 of at least three bytes arrives.
The low nine raw switch bits are cached; `NativeInfo.digital_snapshot`
contains those bits and bit31 indicates validity. Reconnection clears the
known state and obtains a new baseline before publishing the connection.

## Control and report contract

All interface operations and callback delivery belong to the worker's
CFRunLoop. Public control calls enqueue heap-owned bounded requests and
wait for actual completion. Caller timeouts do not free requests still
owned by the worker. Expired commands/start requests not yet begun are
cancelled; active startup writes and ACK waits honor the outer deadline.
If startup finishes after cancellation, its ISO work is stopped and the
session fails rather than reporting successful audio start.

EP1 writes use `WritePipeAsyncTO` with one session-owned 64-byte buffer.
Waiting for their completion pumps the same CFRunLoop, so the persistent
input callback can rearm while firmware accepts output. The callback must
confirm the entire byte count. Timeout or cancellation aborts OUT and marks
the transport failed; the buffer remains owned until a late callback drains.
Both command directions participate in close/quiescence checks.

The exposed command API accepts NI command7 MIDI output and only the exact
LED frame `05 00 40 40 00 <mask> <toe 0|1> 00 00`. It validates
port/count/length or every fixed LED field and uses bounded 200 ms USB writes. Generation-qualified
commands keep their lease in the queued request; the worker rechecks it
immediately before the actual write. Old MIDI requests therefore cannot
cross a disconnect/reconnect. Other initialization/configuration commands
stay private to the worker. Arbitrary firmware, IO and report configuration
writes are not exposed. Controller-to-MIDI mapping belongs to its separate
native policy component.

Internal command9 retains strict ACK=1 for the real sample rate. Its rate-ff
stop sentinel accepts a bounded ACK, including the observed zero result,
after a successful write: NI `performAudioEngineStart` ignores that initial
stop result before configuring the real rate. A missing ACK or failed write
still fails; other startup requirements and ISO scheduling are unchanged.

The last 256 incoming command reports form a non-destructive history. Each
has a session-wide monotonic sequence and a `mach_absolute_time()` receipt
timestamp. Separate clients keep independent cursors through the fixed
`NativeControl.h` ABI. Reconnect clears report contents before arming the
new reader, increments connection generation after validation, and keeps
the global sequence increasing. A new generation cannot replay old MIDI.
History overrun is explicit in each returned batch.

## Audio and lifetime

Audio start succeeds only after NI parameter acknowledgement **and a real
completed-output clock sample**. Until then the session remains STARTING.
The clock exposes completed device frames, matching host ticks and a seed
combining connection generation with the ISO clock generation. The snapshot
rechecks both engine pointer and generation around the read. It never
manufactures a running clock while callbacks have not begun.

HAL render/capture/clock paths use the ISO engine's bounded atomic queues;
they allocate no memory, take no mutex and issue no USB calls. Capture
zero-fills unavailable frames while returning the actual frame count.
Errors retain their code/status and trigger safe disconnect/retry. A
previously running audio stream is restarted after reconnect only when
startup and the first valid clock succeed; an explicit stop revokes that
automatic restart even while disconnected.

Close relinquishes the client's reference and requests worker cleanup.
`rk3_usb_close_wait` additionally waits a bounded interval for CLOSED before
relinquishing that reference; false retains the worker-owned pending state.
The worker stops ISO work, prevents EP1 rearming, aborts pending I/O and
drains callbacks. If cancellation is delayed, it retains the worker and all
callback storage rather than exiting the host process or freeing pending
buffers. USB low-latency buffers are detached after quiescence and before
the final interface close. Real-time calls atomically pin the session before
loading the current engine. Retired CPU queue objects are reclaimed only
when those readers quiesce; each engine retains its COM interface reference.
Retirement is capped at four engines, so repeated failed reconnects cannot
grow memory without bound if real-time readers never quiesce.
A forced cancellation close is followed, after
quiescence, by a non-seizing reopen for buffer detach/alternate restoration
where the device remains available. Failures remain observable.

## Verification

The full transport/ISO/codec/scheduler library builds as ARM64 with strict
C11 warnings treated as errors. Clang static analysis passes.
`Driver/Tests/NativeUSBTests.c` runs without USB enumeration or hardware
access, with AddressSanitizer and UndefinedBehaviorSanitizer. It covers
history overflow and independent cursors, reconnect without old-report
replay, sequence continuity, cancellation without rearming, queued request
ownership after timeout, stale command leases, command/port/length policy,
capture zero-fill, no write after active-request expiration/cancellation,
and valid-clock startup gating versus timeout/cancellation/engine errors.
It also verifies pinned retirement and successful/timed-out close waiting.

These checks establish software invariants. Real audio start, signal flow,
DIN MIDI, unplug/reconnect and host integration require separate hardware
validation by the owning integration session.
