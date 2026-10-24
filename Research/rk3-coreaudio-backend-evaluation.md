# rk3-coreaudio controller-backend evaluation

Evaluated on 2026-10-24, on macOS 27.0.1 (26A434), arm64, with Apple Clang
21.0.0. The selected upstream revision is
[`mihaimetal/rk3-coreaudio@d0679238ad06842f382ef89b995823e978caec9b`](https://github.com/mihaimetal/rk3-coreaudio/tree/d0679238ad06842f382ef89b995823e978caec9b).

## Decision and observed failure

Reuse the upstream **IOUSBLib transport only**, adapted for controller access.
The app's live session now calls `CoreAudioUSBEventReader`, which uses the C
target `CRK3CoreAudioUSB`. The existing Swift session, continuous input pump,
control mapping, calibration and CoreMIDI endpoints remain in place. No upstream
audio engine, HAL plug-in, codec or MIDI/UDP helper is incorporated into this
controller path. See the [live dependency](../Sources/RigKontrol3Core/Bridge/RigKontrolControllerSession.swift),
[native reader](../Sources/RigKontrol3Core/Hardware/CoreAudioUSBEventReader.swift)
and [provenance notice](../ThirdParty/rk3-coreaudio/NOTICE.md).

The previous controller became unresponsive during a held-switch USB recovery
test. Its captured process sample shows the session worker in
`libusb_exit → darwin_exit → pthread_cond_wait`, while the libusb hotplug thread
is in `darwin_devices_detached` waiting for a mutex. This is evidence of blocked
transport teardown; it does not establish that MainStage mappings were erased
or that a running NIHardwareAgent caused the failure. The evidence is retained
locally in [.build/diagnostics/held-switch/controller.sample.txt](../.build/diagnostics/held-switch/controller.sample.txt).

The new controller path creates no libusb context and does not enter that
context's teardown lifecycle. The backend migration's reproduced held-switch
stall acceptance check has passed, followed by working switch/pedal input.
Stop/start and physical sleep/wake have also passed. Sustained use and broader
host-context synchronization remain outside this verified checkpoint. The old
libusb diagnostic implementations remain in the core target, so libusb may
remain a linked and bundled dependency. See [Package.swift](../Package.swift)
and [CoreAudioUSBEventReader.swift](../Sources/RigKontrol3Core/Hardware/CoreAudioUSBEventReader.swift).

## Minimum adaptation

Upstream `usb_iokit.c` and `usb_iokit.h` depend on Apple IOKit/CoreFoundation,
not libusb. Their original open path selects alternate setting 1 and requires
both bulk and audio pipes. The copied files and license are under
`Sources/CRK3CoreAudioUSB`; the controller adaptation adds the following behavior.
See [upstream C](https://github.com/mihaimetal/rk3-coreaudio/blob/d0679238ad06842f382ef89b995823e978caec9b/usb_iokit.c),
[upstream header](https://github.com/mihaimetal/rk3-coreaudio/blob/d0679238ad06842f382ef89b995823e978caec9b/usb_iokit.h),
and the [adapted C source](../Sources/CRK3CoreAudioUSB/usb_iokit.c).

- `rk3_usb_open_controls` selects interface 0, alternate setting 0, and requires
  only bulk EP1 OUT/IN. The upstream audio open path is preserved.
- Configuration is changed only when necessary. Interface selection checks its
  number rather than accepting the first matching interface.
- Cleanup closes only successfully opened device/interface handles. Open
  diagnostics remain available after failure cleanup.
- Bulk calls reject invalid sizes and zero timeouts, retain native errors and
  initialize failed read lengths to zero. The Swift adapter uses a 100 ms input
  timeout and bounds output timeouts to 1…200 ms.
- The existing independent input pump continues draining during startup,
  operation and final LED writes; its owner joins it before closing the native
  connection. See [the C contract](../Sources/CRK3CoreAudioUSB/include/usb_iokit.h),
  [Swift adapter](../Sources/RigKontrol3Core/Hardware/CoreAudioUSBEventReader.swift)
  and [input pump](../Sources/RigKontrol3Core/Hardware/LibUSBBulkInputPump.swift).

CoreMIDI names remain **RigKontrol3 Bridge** in both directions, with source UID
`0x524B3301` and destination UID `0x524B3302`. Channel 16, switch CC20…27,
expression CC11, press-only MainStage input and authoritative host-feedback
LEDs retain their existing session behavior. Unknown host states remain dark;
physical presses do not establish LED state. See the [session](../Sources/RigKontrol3Core/Bridge/RigKontrolControllerSession.swift)
and [setup guide](../Documentation/Controller-Setup.md).

## Why the full HAL/UDP path was not adopted

This is a fit assessment for the controller's current requirements, rather than
a claim that the upstream audio driver is unusable. The upstream engine forwards
raw EP1 report bytes over UDP, and its helper dispatches each received datagram.
That interface does not convey this app's connection epochs or a fresh snapshot
handshake. Adopting it would require an additional lifecycle protocol so a lost
release or an old datagram cannot become a new press after recovery. See
[engine forwarding](https://github.com/mihaimetal/rk3-coreaudio/blob/d0679238ad06842f382ef89b995823e978caec9b/rk3_engine.c#L536-L568)
and [helper reception](https://github.com/mihaimetal/rk3-coreaudio/blob/d0679238ad06842f382ef89b995823e978caec9b/tools/rk3midihelper.c#L128-L145).

The upstream LED listener drops a received state when a write is already busy.
Its boot display animation also clears the shared display/LED payload. These
paths would need reconciliation with authoritative effect-state LEDs; sending
one datagram would not prove the hardware reached the requested state. See
[LED/display writes](https://github.com/mihaimetal/rk3-coreaudio/blob/d0679238ad06842f382ef89b995823e978caec9b/rk3_engine.c#L624-L747)
and [LED reception](https://github.com/mihaimetal/rk3-coreaudio/blob/d0679238ad06842f382ef89b995823e978caec9b/rk3_engine.c#L778-L827).

The full engine's close path performs a `pthread_join` without a timeout. It
does not provide this app's bounded shutdown contract by itself. This is a
source-level limitation, not an observed upstream hang. The existing installed
experimental HAL driver was left untouched during this migration. See
[engine close](https://github.com/mihaimetal/rk3-coreaudio/blob/d0679238ad06842f382ef89b995823e978caec9b/rk3_engine.c#L1218-L1230).

## Validation and limits

| Check | Evidence and status |
| --- | --- |
| Upstream compilation | `make test all driver midihelper rk3midi rk3led rk3monitor iotest` exited 0 with no warnings. Tools and HAL bundle were arm64; inspected link dependencies were Apple frameworks/system libraries. No install target or hardware executable was run. [Build log](../.build/upstream/rk3-coreaudio-build-eval.log). |
| Upstream codec | Three hardware-free tests passed: capture byte recovery, corrupt check-byte detection, and playback layout. [Test source](https://github.com/mihaimetal/rk3-coreaudio/blob/d0679238ad06842f382ef89b995823e978caec9b/tools/codectest.c). |
| Final controller test suites | The clean controller source snapshot executed 107 tests, with two native opt-in skips and zero failures. The full working-tree run executed 116 tests, with the same skips and zero failures; its additional nine tests belong to pre-existing experimental audio work excluded from the clean snapshot. Both native CoreMIDI opt-in checks also passed separately outside the sandbox; they perform no USB testing. |
| Additional stream tests | All five passed: input draining during writes, initial-read failure, join-before-close, session recovery/port lifetime, and native error translation. They exercise the actual stream with a fake bounded connection. The session's `operationFailed` catch now routes through `usbFailure`, preserving native removal/busy behavior. [Tests](../Tests/RigKontrol3CoreTests/CoreAudioUSBEventReaderTests.swift), [session](../Sources/RigKontrol3Core/Bridge/RigKontrolControllerSession.swift). |
| Attached native app smoke | The final rebuilt app passed its three-second session: it logged backend/revision, reached **Connected — MIDI ready**, emitted no switch CC20…27, and ended with `clean: true`. It did emit expression CC11 value 127. The final clean-built package also passed the same check: [clean-package log](../.build/controller-smoke.pc5o1j). The [earlier rebuilt app](../.build/controller-smoke.GY0A47) also passed. |
| Active transport sample | The version 0.3.0 process sample shows the input pump in `NativeCoreAudioUSBConnection.readPacket → rk3_bulk_read → IOUSBLib ReadPipe`. No libusb hotplug thread appears in that sample, although the legacy-linked dylib is loaded. This corroborates the active native path, not all future recovery behavior. [Local sample](../.build/diagnostics/native-backend.sample.txt). |
| Physical acceptance | The user confirmed four switch-1 presses each toggled once and retained the state after release, smooth pedal response, and actual Cry Wah mouse-power changes updating the physical LED. After reconnecting, all eight switches produced their respective CC20…27 press messages, and expression reached 0…127. Only switch 1 has a mapped host effect; the other seven effect/LED mappings are not validated. [Live log](../.build/controller-rk3coreaudio-native.jsonl), [setup guide](../Documentation/Controller-Setup.md). |
| Held-switch USB recovery | Passed one physical cycle. The log shows a read error at 13:22:24 UTC, waiting at 13:22:25 and ready at 13:22:39, with no switch command during recovery. The deliberate post-reconnect CC20 press at 13:24:11 received host feedback setting switch 1 false; subsequent switches and pedal worked. This corroborates the user's successful held-switch test. [Live log](../.build/controller-rk3coreaudio-native.jsonl). |
| Stop/start and quit | Setup UI stop/start logged **Controller stopped** at 13:25:13 UTC and ready at 13:25:19 without switch commands. Final quit at 13:29:53 logged `clean: true`. [Live log](../.build/controller-rk3coreaudio-native.jsonl). |
| Sleep/wake | Passed physically. The log shows **Paused while Mac sleeps** at 13:27:03 UTC, then connecting/ready at 13:27:33 without switch output during wake. Subsequent CC20 presses at 13:27:46 and 113 expression messages spanning 0…127 confirm responding controls. The user confirmed effect/LED retention and working controls. [Live log](../.build/controller-rk3coreaudio-native.jsonl). |
| Clean package | Clean source snapshot tree `82b5188a` built and passed the 107-test check above. Version 0.3.0/build 3 passed arm64, plist, code-signature and bundled-source verification. |
| Remaining physical/host scope | Sustained sessions, all eight actual effect/LED mappings, and host initial/patch/concert synchronization remain unverified. Only switch 1 is mapped to a host effect. |

## Packaging and source

The native-backend controller app is version **0.3.0**, build **3**. Its package
includes GPL-2.0 licensing/provenance and the adapted C/header plus Swift backend
source snapshot. That snapshot is a subset; full corresponding controller-app
source and build instructions are maintained on
[`zukomgwili/rigkontrol3`, branch `codex/rk3-effects-controller`](https://github.com/zukomgwili/rigkontrol3/tree/codex/rk3-effects-controller).
The notice does not relicense unrelated pre-existing experimental audio work.
Packaging retains libusb only when the executable still links it and validates
its bundled dependencies/signature in that case. See the [build script](../Scripts/build-rk3controller.sh)
and [notice](../ThirdParty/rk3-coreaudio/NOTICE.md).
