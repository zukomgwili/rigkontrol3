# Combined audio/controller interface, version 1

The HAL owns interface 0, alternate setting 1. The app never opens USB in shared
audio mode. Existing CoreMIDI endpoint names/UIDs, CCs, parser, calibration and
host-authoritative LED behavior remain in the app.

Device global/main properties `rk3i` (get) and `rk3o` (set) are advertised in
`cust` as CFPropertyList. Their payload is a CFDictionary; `rk3i` also accepts a
CFDictionary qualifier. The property and qualifier sizes are each
`sizeof(CFPropertyListRef)`, not serialized data lengths. Raw report bytes are
CFData. All numbers are integral CFNumbers except the boolean fields.

`rk3i` qualifier: `version: 1`, optionally `boot: UUID string`, `epoch: UInt64`,
`after: UInt64`. An initial read omits boot/epoch and obtains a baseline; it
returns no historical packets. Subsequent reads require both identities.

Response: `version: 1`, `boot`, `epoch`, `state` (`waiting`, `ready`, `failed`,
`gap`, or `stale`), `error: Int32`, `oldest: UInt64`, `newest: UInt64`,
`packets: [{sequence: UInt64, time: UInt64, bytes: CFData}]`,
`completions: [{client: UUID string, id: UInt64, epoch: UInt64,
status: Int32, actual: UInt32}]`.

`time` is acquisition mach_absolute_time. A bounded 256-packet ring retains
ordered raw EP1 messages (maximum 512 bytes each); each response returns at most
64 packets after the cursor. Advance the cursor to the last returned packet,
not `newest`. A cursor older than the retained ring returns `gap`; identity
mismatch returns `stale`. Either requires a new baseline and the existing
AUTO_MSG/READ_IO snapshot handshake. No packet from an old epoch is a new press.
USB removal/read failure invalidates ready state and pending commands. Each
successful USB reclaim starts a new epoch; HAL reload changes boot identity.

`rk3o` request: `version: 1`, `boot`, `epoch`, `client: UUID string`,
`id: UInt64` (nonzero, unique within client), `timeoutMS: UInt32` (1…200),
`bytes: CFData`. Allow only complete LED frames (`05` plus 63 payload bytes),
AUTO_MSG exactly `0b 01 0a 00`, and the payload-free READ_IO `04`.
Validate shape, identity, ready state and a bounded 16-command queue before
acceptance. Set success means enqueued, never hardware success.

The engine drains requests on its runloop and serializes asynchronous
WritePipeAsyncTO operations. Completion records contain the actual IOReturn and
written-byte count from the USB completion. Never drop a LED request merely
because another write is active. An expired queued request is failed, not sent.
The app input pump polls on its background thread and wakes the writer for its
matching client/id/epoch completion; a failed/short/timed-out write fails the
session. An audio command acknowledgement is handled by the same persistent EP1
reader, without a second reader or cosmetic LED/display writes.

The control module exposes `rk3_control_initialize`, `rk3_control_begin_epoch`,
`rk3_control_set_state`, `rk3_control_publish_input`,
`rk3_control_take_command`, `rk3_control_complete_command`,
`rk3_control_copy_input`, and `rk3_control_enqueue_output` through
`Control/rk3_control.h`. Its independent mutex protects only bounded copies and
queue operations. Property callbacks never take the audio lifecycle mutex or
perform USB work. DoIOOperation never touches the control mutex or properties.

First audio milestone is 48 kHz, stereo. StopIO stops audio chains while the
control reader remains armed. Abort and drain callbacks before buffers or USB
handles are released. No legacy firmware upload or NI private ABI is required.
