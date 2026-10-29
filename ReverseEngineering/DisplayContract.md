# Original RK3 display operation and ARM binding

The four-byte display setter is part of NI's controller policy. It shares the
same digital-output report as the nine switch LEDs. The ARM implementation
keeps one controller state and one output writer; it does not introduce another
MIDI mapping, display encoding, device owner or recovery policy.

## Original contract

The bounded pre-read plan is recorded in existing `rigkontrol3-40h.2` on
29 October 2026. Only five pinned cached ASM exports were read (528 bytes of
address bounds, 143 export lines); no callee or other overload was recovered.

| Operation | Recovered behavior |
| --- | --- |
| `setLEDDisplayContent`, `0x100040810` | Unsigned index 0–3; store byte at `3-index`. An invalid index or unchanged byte returns false without setting dirty. A changed byte sets dirty and returns true. |
| `updateLEDs`, `0x100040750` | Require connected and either forced or dirty. Clear dirty **before** constructing and sending the report. |
| Display serialization | Eight-byte zeroed payload starts with stored bytes `[3], [2], [1], [0]`, so the setter's logical index corresponds to that payload position. |
| Remaining payload | Byte 4 packs nonzero assignment values 0–7; byte 5 bit 0 is assignment 8 (toe); bytes 6–7 stay zero. |
| `writeDigitalIO`, `0x1002f1d70` | Synchronous selector 2 with one scalar length and an eight-byte structure; returns selector success. |
| Kernel `writeDigital`, `0x92a0`; `writeIO`, `0x27a0` | Guard owner/input, send command `05` with the payload, ignore the USB send result at the digital wrapper. `updateLEDs` does not re-dirty on a hardware failure. |

The previous native default `00 40 40 00` is symmetric, so it concealed a
serialization gap: directly writing stored bytes 0–3 cannot prove the original
reversal. The new indexed setter and reversed writer preserve the recovered
contract with asymmetric values too. No segment, numeral, text or mode meaning
is inferred from these raw bytes.

## Necessary native binding

NI originally called the setter inside its controller process. That private
agent/editor interface is outside the user's current scope. Native CoreMIDI
already owns the reconstructed controller policy, so the replacement exposes a
small CoreMIDI Data-property mailbox on that same controller device. It uses the
existing server-main discovery cycle and I/O cycle, with no new timer or thread.

`Driver/Display.h` defines an exact 48-byte indexed request and 56-byte status.
The keys are `org.rigkontrol3.ni-arm.display.request` and
`org.rigkontrol3.ni-arm.display.status`. The request carries version, index, byte
value, request ID and a lease comprising driver-instance session, lifecycle
epoch, audio-provider object and physical generation. The status reports the
same lease and applied request ID, plus four logical content bytes in index
order. Reserved fields must be zero. This versioned native envelope is not NI's
old external-method ABI.

A fresh nonzero session token prevents CoreMIDI's cached data from replaying
requests into another driver instance. The complete lease is checked again at
the I/O mutation boundary. Stop/start, disconnect and provider replacement
invalidate pending commands. The existing controller object's in-process
content retention remains; there is no new saved display preference or
restoration from a cached request. A fresh controller starts with NI-derived
`00 40 40 00`.

The mailbox contains one latest request, not a command FIFO. A competing writer
can supersede a request before application; the CLI detects replacement rather
than reporting success for the wrong command. Status success acknowledges only
application to the **logical controller state**, including an unchanged-byte
setter. It is not USB completion or physical display readback. The writer
continues to combine current display content with current switch LED state.

The existing native callback means queue admission. Its admission failure still
retains dirty as before; once accepted, a later USB failure does not add an
acknowledgement or retry mechanism. NI's original dirty clearing does not justify
inventing a hardware retry policy. Generation-qualified native output continues
through the existing sole output worker.

The HAL and native USB command validators admit arbitrary first four display
bytes only in the exact nine-byte `05` report. They retain the toe-bit limit,
trailing zeros, length, provider/generation and existing bounded-write checks.
There is no generic raw USB escape.

## Use and validation boundaries

`rk3-display --show` reports runtime logical content. The indexed operation is
`rk3-display --index 0..3 --value 0..255`. It parses help and invalid arguments
before making system calls, obtains a current lease and waits a bounded interval
for the matching logical acknowledgement. It neither opens USB nor invents a
MIDI CC/SysEx protocol. It does not restore a saved value automatically.

Offline checks cover asymmetric byte ordering, unchanged and invalid setters,
force/reset behavior, combined switch/toe output, native command validation,
mailbox lifetime and competing requests, and the CLI's actual parsing and
lease/acknowledgement path with mocked services. Results and source pins are
recorded in `Evidence/DisplayImplementation.json` and existing M1.

Physical display observation is still required after installing the reviewed
combined package. Source tests cannot establish the device's display appearance.
The existing direct-monitor work is packaged together but its routing acceptance
is separate. NI's audio ring, timing, recovery and SIP settings are unchanged.
Apple case 23183489's output-lifetime question remains separate and unresolved.
