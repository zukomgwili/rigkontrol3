# Native CoreMIDI system plug-in

`RK3MIDIDriver.c` implements a new ARM64 `MIDIDriverInterface2` CFPlugIn.
The bundle is `RigKontrol3MIDI.plugin`, executable `RK3MIDIDriver`, factory
`RK3MIDIDriverFactory`, identifier `org.rigkontrol3.ni-arm.midi`, factory UUID
`B43A1D48-9139-48B3-8357-6143807BB410`. Its static factory registration follows
Apple's SDK and [CFPlugInFactories contract](https://developer.apple.com/documentation/bundleresources/information-property-list/cfpluginfactories).

The clean RK3 reply verified **one hardware MIDI input and one output**. This
component exposes two unidirectional DIN entities under one hardware device,
matching the original driver's entity arrangement. Device/source/destination
IDs are deterministic hashes in a new namespace, keyed by the native HAL UID.
A separate **Rig Kontrol 3 Controller** MIDI device exposes a Controller
source and LED Feedback destination. These are native mapping endpoints, not a
claim that the hardware has a second DIN port. Their persistent identity is
`org.rigkontrol3.ni-arm.usb.17cc1940.controller`, under the shared
`org.rigkontrol3.ni-arm.midi.identity` key. The DIN identity remains the HAL UID.

The current owner contract deliberately supports the first RK3 unit only;
serial-based multiunit identity remains future work. Saved devices are reused
by a private persistent identity property and marked offline on disconnect.

The native AudioServerPlugIn owns USB. The MIDI bundle accesses its versioned
`NativeControl.h` properties on the audio device object. It has no controller
application, GUI bridge, socket, daemon, or Intel driver dependency. Analog and digital reports are handled by the freshly reconstructed
`Controller.c` mapping, based on the original Controller Editor/NIHardwareAgent
analysis. Nine switches use CC76..83 and CC64; the three pedals use CC16/17/4,
channel 1. Presses toggle 0/127; releases do not toggle. No old application or
mapping implementation is loaded.

## Thread and delivery contract

- Factory creation captures the server main run loop. Start, Stop, device/entity
  creation, persistent properties, endpoint refCons, and offline notifications
  run on that thread. A 500 ms timer reconciles HAL discovery and hotplug.
- A 5 ms timer on `MIDIGetDriverIORunLoop()` reads the report history with an
  independent cursor. Bounded command-6 records feed the DIN parser, while
  raw command03/04 reports feed the separate Controller mapping.
  Native Mach host timestamps are preserved; zero timestamps use the current
  host clock. A per-source stateful parser reconstructs complete messages and expands running
  status across records. Realtime messages are separate packets; each SysEx
  packet contains only a SysEx fragment (at most 256 bytes). No incomplete
  channel/system-common messages or running-status-only packets reach CoreMIDI.
- Enabling a source samples the current history sequence on the server main
  thread before activating delivery, capturing the physical snapshot for a
  Controller source enabled before its first poll. A second history read after
  the snapshot forms a delivery barrier: intervening reports may update state
  silently, and pre-activation transitions do not replay as new CCs. New
  post-enable presses are preserved. While disabled, the history cursor and
  controller physical state still advance. A fresh connection uses the owner's
  valid digital snapshot to seed the raw button baseline without phantom presses. An overrun resets running status, partial messages and SysEx. Orphan data
  bytes are discarded until a new status establishes a message boundary.
- `Send` performs bounded atomics and copying without a blocking lock, heap
  allocation, CoreAudio IPC, or USB operation. At most 256 packets / 2048 bytes
  per call are accepted, and the entire list must fit the 256-entry output
  queue. Contention/full/offline errors return immediately. Eight-byte chunks
  span packet boundaries, matching the recovered NI packetization; packet
  timestamps are not scheduled into the future.
- A private worker inside this plug-in drains that queue. Controller feedback
  bytes use a distinct typed entry and bounded feedback queue to the I/O loop;
  they are parsed as CC input to the controller state and never written to DIN.
  Controller LED flushes enqueue the exact leased nine-byte command05, preserving
  the original four startup display bytes. This is not a separate
  service. Every command carries the captured audio-device ID and generation,
  plus the required generation lease qualifier. Both plug-in and hardware owner
  discard stale work across reconnect. A separate lifecycle epoch also rejects
  a Send which entered before Stop but acquired the producer slot after a new
  Start, including when the USB generation stayed unchanged. The owner validates the lease while
  serializing the actual hardware write.
- Successful Send means **accepted for delivery**, not a hardware completion.
  A later write/property failure takes the source/destination offline until the
  owner reports a different transport generation (or the driver is restarted).
  CoreMIDI Flush is a successful no-op because no future timestamp scheduling is
  provided; the bounded immediate delivery backlog may still be in flight.
- Timers and the worker retain the COM object. Stop first disables I/O, invalidates
  timers, joins the bounded worker, and waits for an in-flight bounded producer
  before queue reuse. It clears refCons and marks the saved device offline.
  MIDIReceived is called after releasing the driver's state mutex, avoiding
  CoreMIDI reentrancy/lock inversion. A separate listener epoch cancels an
  already-dequeued input batch if the source is disabled and re-enabled before
  delivery; output queue entries are unaffected by listener changes.

`MIDIStream.h/.c` is a new allocation-free MIDI 1.0 parser. It retains partial
channel/system-common messages and timestamps, expands running status, streams
bounded SysEx-only fragments, and emits interleaved realtime bytes separately.
For NI compatibility it suppresses F9, FD and FE and consumes F5 plus its following
cable byte. Undefined F4/orphan F7 are discarded safely; interrupted SysEx is
closed with F7 before processing a new status, matching the recovered NI policy.
History loss and generation/activation changes reset all parser state.

The source uses current SDK packet helpers, not the old Intel packed-structure
pointer arithmetic. No proprietary decompiled implementation is embedded.

## Hardware-free validation

`RK3MIDIDriverTests.c` includes the exact production implementation with mocked
CoreAudio transport and MIDI delivery. It checks COM negotiation/reference
counts, cross-packet output chunking, all-or-nothing queue pressure, wrap,
contention, endpoint/offline errors, generation leases/stale-work rejection,
DIN/controller separation, input timestamps, activation baselines, history
loss recovery, malformed records, complete-message reconstruction, Stop/Start entrant cancellation, listener reactivation races, and USB failure/offline behavior. It never
calls Start or real AudioObject transport functions.

`MIDIStreamTests.c` verifies every split point, explicit running status, partial
system-common messages, realtime interleaving, long SysEx, interrupted SysEx,
compatibility filtering, history resets and invalid state/emit failures. An
integrated test first failed against the old raw-forwarding input callback,
then passed with the parser: `[90,60] / [100,61] / [110]` produces exactly
`[90,60,100]` and `[90,61,110]`.

`FactorySmokeTest.c` loads the **built bundle** through `CFPlugInCreate`, looks
up the exported factory, and verifies interface2 / IUnknown negotiation and
reference counting. It does not call Start, open an audio device, install the
plug-in, or access USB. Keep the test executable outside any directory with an
`Info.plist`; otherwise CoreFoundation can interpret that plist as the test's
main bundle and pre-register its factories.

Compile/link the bundle with `Driver/MIDIEvent.c`, `Driver/Controller.c`, and `Driver/MIDI/MIDIStream.c`, the `Driver` include directory,
CoreMIDI, CoreAudio and CoreFoundation frameworks, `-bundle`, `-arch arm64`, and
minimum macOS 13. Strict C11 builds and ASan/UBSan mock tests pass. The built
bundle's real factory smoke test also passes. Installation and actual MIDIServer
Start/hotplug/DIN I/O validation are separate hardware checks.

## Controller state, calibration and LEDs

Controller state has one serialized owner: the MIDI I/O loop. It processes raw
reports even without listeners, preserving physical baselines and local toggle
assignments. Transport/lifecycle changes reset input baselines; calibration and
assignments survive disconnect. History loss resets the baseline from the owner's
current digital snapshot. Stale feedback and LED writes are rejected by device,
generation and lifecycle tokens. The feedback queue is discarded safely on Stop.

The persistent Controller-device Data property
`org.rigkontrol3.ni-arm.calibration` contains exactly four little-endian u32 values:
`{version=1, axis=2, raw_min, raw_max}`. Endpoints must be in 0..1023 with a span
of at least 103. The main discovery timer validates this property and publishes
one atomic configuration word; the I/O loop applies normalized endpoints without
CoreMIDI property calls or a blocking calibration lock. Invalid data preserves
the last valid endpoints. Initial built-in full-range calibration is a documented
fallback until a physical sweep is stored; external inputs retain the recovered
auto-calibration behavior.

Local LEDs follow the reconstructed toggle assignment. A host can send channel-1
CC feedback to **LED Feedback** to override that assignment and LED state; values
below 64 mean off, 64 and above mean on. This is explicit host feedback, not a claim
that Guitar Rig or another application automatically sends effect state. The
feedback destination does not echo CCs into the Controller source.

Additional sanitizer tests cover physical snapshot baselines, local toggle/release
behavior, processing while listeners are disabled, source enable before the first
poll, interleaved snapshot/history activation reads, the first post-enable press,
overrun baselines,
controller/DIN separation, feedback LED override, leased LED05 queue entries,
feedback generation rejection, default and invalid calibration properties, and
atomic calibration application.

## System-host signing

A successful factory smoke test does not establish MIDIServer acceptance. The
installed system host rejected the initial ad-hoc signature before `Start`.
Apple Development signing is the local-development test; actual MIDIServer
loading must confirm its host-specific policy. The plug-in does not require
DriverKit entitlements, and entitlements should not be attached to library code.
See [CoreMIDISigning.md](../../ReverseEngineering/CoreMIDISigning.md) for primary
Apple sources, provisioning-profile limits and the separate Developer ID
distribution path.
