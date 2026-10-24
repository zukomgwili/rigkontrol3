# Fresh native hardware validation

Observed2026-10-24 on this Apple Silicon Mac. The previous RK3 bundles were
quarantined by the user; all five original installation paths were then
independently confirmed absent. CoreAudio restarted from process90679 to5169.
The old RK3 controller and MainStage were not running. Other NI applications
and shared licensing services were preserved.

User-reported verified quarantine session:
`/Library/Application Support/RigKontrol3/Quarantine/20261024T203750Z-dee94a39`.
The apply script verifies every moved bundle. This session directory requires
administrator access; it was not separately reread by the agent.

## Descriptor exchange

Fresh `Tools/rk3-device-probe.c`, builtARM64 against the current Apple SDK:

```text
vendor17cc product1940 release000d location00100000
current configuration0; one advertised configuration1; total length69
interface0 alt0: bulk OUT01 / IN81, max512
interface0 alt1: bulk OUT01 / IN81, iso IN82 / OUT06, max512, interval1
```

No former driver configured the freshly reconnected device. Explicit
`--query-device --configure --location0x00100000` selected advertised
configuration1, closed the device handle, opened interface0 non-seizing,
selected alt1, and armed a64-byte persistent EP1-IN read before writing
exactly one command byte`01`.

Two unrelated33-byte command3 reports arrived first. The reader rearmed and
then received:

```text
01 0d 00 06 00 03 09 29 02 02 00 00 01 01 02
```

The NI-derived parser reports2audio inputs,2audio outputs, mode2,1MIDI input,
1MIDI output. Their DIN/controller roles are not inferred merely from counts.
The tool returned success and restored its original alternate after callback
cleanup. Configuration1 remains selected until reset/unplug, as documented.
No firmware write, audio stream, LED write, auto-report command, old app or
MainStage configuration was used in this exchange.

These observations verify fresh ARM USB access and command1, not audio
capture/playback, MIDI port operation or pedal conversion. System-driver and
physical tests must establish those separately.

## Native owner and real audio

Warm-start tests initially exposed a missed original startup operation.
Unconditional configuration alone still timed out; adding the exact NI
`ClearPipeStall(true)` equivalents to both command pipes made three consecutive
warm opens and callback-drained closes pass. Real stop command`09 ff ff 88 00 01`
returns`09 00` on this unit. The original engine ignores its return; the fresh
owner accepts a bounded stop acknowledgement while requiring`09 01` for the
actual48kHz start command`09 01 02 88 00 01`.

The current native USB adapter rejects legacy SetPipePolicy values. Validated
512-byte ISO endpoints with interval1 work without that optional operation.
The current output API also rejected an original eight-microframe plan with
trailing empty transactions; kernel logs reported an invalid transaction
request count/offset. Trimming only trailing empty transactions, retaining
all payload bytes, interior gaps and original calendar frames, resolved that
failure. These are observed current-host adaptations, with hardware-free
regressions; they are not attributed to the Intel implementation.

The production owner then passed a five-second actual hardware test:

```text
capture240006 frames; clock68..240074; peak0.011135; RMS0.002069
stop0; callback-drained close
```

After adding original controller configuration`0f 02 ...` (32-byte payload),
automatic reports`0b 01 02 05`, and current digital query`04`, synchronous
command bursts exposed EP1 backpressure. Asynchronous bounded writes let the
worker keep rearming input while waiting for output completion. The corrected
full startup and production audio test passed:

```text
digital baseline valid, raw000
capture240006 frames; clock68..240074; peak0.011101; RMS0.002067
stop0; callback-drained close
```

Finally the built ARM64 audio CFPlugIn was loaded into a diagnostic host and
used through its actual macOS driver interface, including Initialize,
StartIO, GetZeroTimeStamp, DoIOOperation and StopIO:

```text
48000Hz;2inputs/2outputs; snapshot80000000
145 I/O cycles; zero timestamp0..32768; RMS0.005260; I/O0; stop0
```

Both built bundles passed real CFPlugIn factory/COM smoke tests. These tests
sent silence. They demonstrate real capture and completed output transport,
not an audible listening result, installed-driver acceptance, DIN loopback,
or a physical switch/pedal/LED result. Those require the following live stage.
