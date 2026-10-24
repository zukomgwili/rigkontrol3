# Fresh native USB device-info probe

`Tools/rk3-device-probe.c` is new ARM64 diagnostic code based on the current
Apple SDK's `IOUSBLib.h`, `IOCFPlugIn.h`, and `IOKitLib.h`, plus the NI evidence
in `AudioDriver.md` and `MIDIDriver.md`. No previous RK3 implementation or
public RK3 driver source was consulted. It links the new bounded
`Driver/DeviceProtocol.c` parser.

Build from the repository root:

```sh
xcrun clang -arch arm64 -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -framework IOKit -framework CoreFoundation \
  Tools/rk3-device-probe.c Driver/DeviceProtocol.c -o /private/tmp/rk3-device-probe
```

This compiled to an ARM64 Mach-O executable with zero compiler warnings.
Clang static analysis also completed without findings:

```sh
xcrun clang -arch arm64 -std=c11 -Wall -Wextra -Wpedantic -Werror \
  --analyze Tools/rk3-device-probe.c -o /private/tmp/rk3-device-probe-analysis.plist
```

## Modes and scope

Default invocation, or explicit `--descriptors`, enumerates only VID `17cc` /
PID `1940` and prints cached configuration, alternate-interface and endpoint
descriptors. It does not call `USBDeviceOpen`, `USBInterfaceOpen`, or send a
device request. `--descriptors` and `--query-device` are mutually exclusive.

```sh
/private/tmp/rk3-device-probe --descriptors
```

Only the explicit query mode takes exclusive access to **interface 0**:

```sh
/private/tmp/rk3-device-probe --query-device
# If the clean device reports configuration0, explicitly select configuration1:
/private/tmp/rk3-device-probe --query-device --configure
# If more than one RK3 exists, select the printed location explicitly:
/private/tmp/rk3-device-probe --query-device --location 0x12340000
```

Run the hardware query only after the previous drivers/workaround are
quarantined and their running instances are stopped. The tool never seizes
an interface, resets/re-enumerates the device,
starts audio, enables automatic reports, writes LEDs, or changes firmware.
It refuses an ambiguous device selection or a failed descriptor inventory.

An unconfigured device is refused unless `--configure` accompanies
`--query-device`. This explicit option verifies advertised configuration1 /
interface0 / alt1, opens the device without seizure, selects configuration1
only when its current value is zero, verifies the readback, and closes the
device before opening the interface. Existing nonzero configuration is
preserved. Configuration1 is retained after the query; unplug/reconnect
returns the device to its fresh unconfigured state.

The default query selects alt 1 only when interface 0 / alt 1 is advertised
by the current configuration. `--query-device --current-alt` instead preserves
the observed alternate. It discovers pipe references from their endpoint
number, direction and bulk transfer type, and requires EP1 IN and OUT. It
restores a changed alternate after pending callbacks drain.

## NI evidence and deliberate limits

- NI `initUSB` (`0x8e0`) selects alt 1 after opening its first interface.
  Targeting **number 0** is the diagnostic scope requested here; NI's generic
  first-interface selection alone does not prove that number for every device.
- NI `initHardware` (`0xdb0`) posts its **64-byte** bulk reader at `0x1038`
  (length loaded at `0x1032`) before sending command 1 at `0x123f`
  (command loaded at `0x1233`).
- NI `sendCommand` (`0x25c0`) sends command 1 without payload as exactly
  **one byte `01`**, using **200 ms no-data / 200 ms completion** write limits.
- NI reply dispatch (`0x20e0`, case 1) copies 14 bytes after the command.
  The new parser requires a 15..64-byte command-1 reply and prints its raw
  payload, input/output channel sums, MIDI counts and returned wire mode.
  Unknown mode/count values are reported rather than relabeled as a known
  RK3 topology. MIDI-port roles are not inferred from their counts.
- EP1 is the requested command endpoint. The generic NI initialization's
  first-bulk-pipe selection does not independently prove endpoint number 1;
  the probe therefore validates the live descriptor before sending anything.

## Callback lifetime and cancellation

One heap-owned 64-byte read remains pending before the command is written.
Successful unrelated replies rearm that reader. A main-thread CFRunLoop
delivers callbacks; a three-second monotonic deadline bounds reply waiting.
There are no command retries or automatic stall/toggle recovery.

Shutdown prevents rearming, aborts the input pipe, and drains cancellation
callbacks for up to two seconds. If still pending it closes the interface
and allows a second two-second drain. If the kernel never completes the
request, the process exits while retaining its context, buffer, interface
and run-loop source; it never frees memory still owned by a pending request.
SIGINT/SIGTERM enter the same cleanup path. No async write buffer exists;
the one-byte write is synchronous and timeout-limited.

If closing the interface finally allows cancellation to finish, a changed
alternate is restored by reopening without seizure **after** pending state
has cleared. Failed close/reopen/restore operations are reported as failures.

Apple's SDK documents that `AbortPipe` still delivers outstanding callbacks
and that `ReadPipeAsyncTO` returns the received byte count by value in the
callback's third argument. See Apple's
[IOUSBLib declarations and API comments](https://github.com/apple-oss-distributions/IOUSBFamily/blob/main/IOUSBFamily/Headers/IOUSBLib.h).
These lifecycle rules were checked against the installed Xcode SDK too.

Only `--help` and invalid-argument cases were run during authoring; no
hardware enumeration or interface open was performed by the authoring
agent. Build/static checks do not establish device access or a successful
device-info exchange. Query/timeout/unplug behavior needs hardware validation.
