# Fresh native hardware validation

The source candidate0.2.2/build7 restores clock maintenance and software input
gain after the recorded contract checks. It has not replaced installed0.2.1/build6
or received a new physical acceptance result. Historical passes/failures below
remain version-specific; source tests do not establish the candidate's audio
continuity, gain, controls, LED feedback or recovery on hardware.

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

## Installed version 0.1.2 (build 3)

The user installed both fresh bundles using the reversible native installer.
The reported installation session is:

`/Library/Application Support/RigKontrol3/NativeDriverBackups/20261024T222056Z-8acc1adb`

Both installed ARM64 bundles have valid signatures and match all 12 files in the
build manifest. CoreAudio exposes the native RK3 device as 2 inputs and 2 outputs
at 48 kHz. The Apple Development-signed MIDI plug-in now actually loads in
MIDIServer, and the native Controller source is present. This demonstrates
local host acceptance on this Mac; it is not a distribution-signing result.

The first custom-property implementation used raw structures with a no-data
type declaration, which did not survive CoreAudio's process boundary. The
corrected properties marshal their fixed-width messages inside CFData values.
An installed-client query now returns the native device UID, connected state,
controller snapshot, generation, and 2/2 audio plus 1/1 MIDI counts. This verifies
the actual cross-process path rather than only a direct plug-in-table test.

A standard CoreAudio client exercised the installed audio device twice:

| Duration | Callbacks | Input1 frames | RMS | Stop status |
| --- | ---: | ---: | ---: | ---: |
| 3 seconds | 283 | 144896 | 0.002870 | 0 |
| 5 seconds | 469 | 240128 | 0.002229 | 0 |

These runs sent silence to the outputs. The five-second run took place while
the native controller calibration client was active. Neither run establishes
that guitar playback is audible, clean, or acceptably low-latency.

## Installed controller and calibration

The user swept the built-in pedal through its full travel three times and left
it at heel. The native calibration utility measured 555 samples and saved a
fresh raw range of **88–648** on the Controller MIDI device. This measurement
supersedes the retired app's calibration; no old calibration was imported.

The first physical switch test was interrupted when the 45-second monitor
expired. Only CC80 and CC81 were recorded twice, and MIDIServer then exited with
no MIDI client remaining. The user observed controls stopping partway through
the sequence. Repeating with a persistent `--live` CoreMIDI client removed the
test's lifetime limit; no driver change was made for that symptom.

In the repeated test, the user pressed and released switches 1–8 twice and swept
the pedal twice, then reported completion without faults. The native monitor
recorded:

| Controller CC | Count | Minimum | Maximum | Last |
| --- | ---: | ---: | ---: | ---: |
| 76–83, each | 2 | 0 | 127 | 0 |
| 4 (built-in pedal) | 55 | 0 | 127 | 0 |

Thus all eight NI-profile switch assignments and the full calibrated pedal
range reached CoreMIDI. The Controller LED Feedback destination was then sent
CC80=127 and CC80=0. The user confirmed that LED 1 lit and extinguished,
respectively. This feedback check does not establish automatic effect-state
output from Guitar Rig or another host.

All installed acceptance checks above use standard CoreAudio and CoreMIDI
clients. The former controller app and MainStage are not involved. For a guided
physical test, `rk3-midi-monitor --live` and
`rk3-audio-client --monitor --live` keep their clients open until stopped.
Each prints its PID; SIGUSR1 requests statistics, while SIGINT or SIGTERM ends
the test cleanly. The audio monitor defaults to 0.1 gain and a ±0.2 output clamp.

## Audible guitar check on installed build 3

A live standard CoreAudio monitor and the native CoreMIDI client ran together
for approximately 207 seconds. The audio client stopped cleanly with:

```text
19431 callbacks; 9948672 input1 frames; RMS0.007939; stop0
```

The user reported that guitar was audible and controls continued responding,
but the guitar was very soft and the delay was noticeable. The client applied
0.1 gain, a 20 dB attenuation, so this test's output level does not establish
normal driver output level. Acceptable unity-gain audio remains unproven; the later build-4 listening
check below failed.
This run establishes simultaneous audible audio and controller operation, not
acceptable sound quality or round-trip latency.

A subsequent one-second silent-output check used the standard CoreAudio
buffer-size API. The installed build 3 accepted a request for 128 frames, and
the client verified the actual size before starting:

```text
389 callbacks; 49792 input1 frames; RMS0.006867; input peak0.017783; stop0
```

The diagnostic now supports `--gain 1 --buffer-frames 128 --live`, prints the
selected gain/limit and actual buffer size, and reports input peak. Explicit
gain uses a ±0.95 clamp; the unchanged default is 0.1 gain with a ±0.2 clamp.
No acceptable unity-gain result was established on build 3; build 4 was
subsequently heard and failed as recorded below.

## Installed version 0.1.3 (build 4): failed acceptance

The user installed build 4 in session:

`/Library/Application Support/RigKontrol3/NativeDriverBackups/20261024T225037Z-08832e7b`

The build changed the native adapter's output calendar to 4 ms, retained FIFO
sample transport, gated I/O on PCM readiness and trimmed capture backlog. These
were new adaptations, not a faithful transfer of NI's shared-ring behavior.

With unity gain and a verified 128-frame host buffer, the user reported:
“Audio glitches remain (crackling, dropping in and out), controls intermittently work”.
The client nevertheless ended with 80715 callbacks, 10331520 input frames,
RMS 0.010865, peak 0.223881 and stop status 0. A separate 15-second silent run
also exited successfully (5552 callbacks, 710656 frames, RMS 0.002916,
peak 0.011254), while the owner generation changed from 99 to 105 with
`0xe00002ee` (`kIOReturnIsoTooOld`). Successful client exit, RMS and callback
counts therefore do not prove transport continuity. Detailed logs, hashes and
chronology are preserved in Beads `rigkontrol3-40h.3` and the checkpoint `.4`.

Audio test clients were stopped at that checkpoint. No rollback was performed;
build 4 remained installed until the build-5 installation below. All 12 installed
files matched the build-4 manifest after installation, which establishes
content integrity rather than audio quality.

## Installed version 0.2.0 (build 5): physical checks and unresolved restarts

The user directed preserving NI's original audio ring, timing and recovery.
The source replaces the FIFO/snapshot adapter with the sample-addressed live
mapped ring and restores the original input-led calendar. It uses primary
completion polling, actual USB-frame anchors and byte-position zero timestamps,
NI latency/sample-offset values 68/136/136 at 48 kHz, and host-mediated audio-only
panic recovery. The protocol, original controller profile, measured calibration
and incoming LED feedback are retained. See `AudioDriver.md` and `NativeUSB.md`
for original addresses and explicit current-platform substitutions.

The user installed both build-5 bundles in session:

`/Library/Application Support/RigKontrol3/NativeDriverBackups/20261025T003945Z-82467aa5`

The root-only session journal was not readable by the verification process.
Independent read-only checks nevertheless confirmed both expected bundle
identities, ARM64 executables, version 0.2.0/build 5, valid signatures and all
12 installed file hashes against the frozen manifest. Its SHA-256 is:

`35977658b0bc13636cb990f15841788bdc8dc0e8504e022821e0f82da6ffb01e`

A standard CoreAudio client then ran for 20 seconds with silent output and a
verified actual buffer size of 128 frames:

```text
7526 callbacks; 963328 input1 frames; RMS 0.000247; peak 0.003749; stop 0
```

A parallel 25-second status watch sampled at 10 Hz. Audio device 173 retained
USB generation 1 and error 0; flags moved from 1 (connected/idle) to 3
(connected/audio running) and back to 1. These observations establish only
this silent run's sampled status and clean client stop. They do not establish
full signal continuity, audible sound quality or guitar latency.

At the silent-run checkpoint, the fresh persistent CoreMIDI monitor was PID
83847 and MIDIServer was PID 83848; the old monitor PID 51213 had been stopped.

### Dry guitar/controller physical pass with residual audio restarts

The user subsequently played dry guitar through the standard monitor at unity
gain with the actual 128-frame buffer. They confirmed clear, comfortably audible
guitar, acceptable delay for home practice, no heard crackles or dropouts, and
reliable operation of all eight switch LEDs. This is a subjective first
listening pass, not a measured round-trip latency or proof of zero signal loss.
No effects host was tested on build 5.

The concurrent persistent CoreMIDI monitor recorded:

| Control | Messages | Minimum | Maximum | Last value |
| --- | ---: | ---: | ---: | ---: |
| Each switch CC76–83 | 2 | 0 | 127 | 0 |
| Built-in pedal CC4 | 162 | 0 | 127 | 127 |

Independent CoreMIDI readback of the saved 16-byte calibration Data property
returned `{version=1, axis=2, raw_min=88, raw_max=648}`. The user also confirmed
incoming LED feedback while guitar continued: LEDs 1/3/5/7 on and 2/4/6/8 off.
All eight feedback values were then cleared to zero before the recovery test.

At approximately 172 seconds, an intermediate live audio snapshot reported:

```text
64421 callbacks; 8245888 input1 frames; RMS 0.014331; peak 0.257267
```

A point-in-time status read showed generation 1 and error 0, but later review
of the **full status log superseded any error-free interpretation**. Before the
deliberate unplug, it recorded five unsolicited transitions from flags 3/error 0
to flags 1/error `0xe00002ee` (`kIOReturnIsoTooOld`) and back to flags 3/error 0.
USB generation remained 1 throughout all five:

| TooOld / not-running tick | Running again tick |
| ---: | ---: |
| 303 | 304 |
| 1392 | 1394 |
| 2086 | 2087 |
| 2585 | 2587 |
| 2749 | 2750 |

The watcher sampled at a nominal 10 Hz. Flags 1 retain the USB connection but
lack AUDIO_RUNNING; flags 3 restore it. This proves sampled audio-running
loss/restoration, consistent with host-mediated pause/start. It is not a direct
host-call trace or a measured interruption duration. The five audio-only
restarts remain a P1 failure under `rigkontrol3-40h.11`, even though the user
reported clear guitar and reliable controls. The sustained-run failure
criterion is any unsolicited nonzero error or loss of AUDIO_RUNNING before
intentional unplug; unchanged USB generation alone is insufficient.

### Held-switch USB recovery and final client stop

The user confirmed all requested recovery behavior: reconnecting with switch 1
held preserved the state without an extra toggle, release did not toggle, the
next press worked once, and guitar/pedal operation returned. The deliberate
unplug began at tick 3114. At tick 3324, generation 2 was connected with held
snapshot `80000010`; audio was running at tick 3326. Release appeared at tick
3540 as snapshot `80000000`. These are a separate intentional recovery check,
not the five earlier steady-state failures.

After recovery, CC80 had four messages total and last value 0, consistent with
the held press and next press without an extra held/release event. The pedal
CC4 count reached 448 across the full 0–127 range. The guitar client then
stopped cleanly:

```text
182667 callbacks; 23381376 input1 frames; RMS 0.013495; peak 0.257267; stop 0
```

The audio helper PID 84504 and status watcher PID 84498 were stopped; the
persistent MIDI monitor PID 83847 remained active at this checkpoint. Logs are
`.build/native/test/installed-v5-guitar-status.log`,
`.build/native/test/installed-v5-guitar-audio.log` and
`.build/native/test/installed-v5-midi.log`. Final callback/RMS/stop statistics do
not negate the five observed audio-engine restarts. No sleep/wake or effects-host
pass is claimed.

Hardware-free regressions model late callbacks and future ring writes,
whole-IRP primary publication, exact timestamp fractions, wrapped host blocks,
stop/drain and transient EP1/MIDI failures. They cannot establish real DMA
prefetch timing, audible latency, sound continuity or actual host power behavior.

Residual steady-state TooOld diagnosis is tracked in `rigkontrol3-40h.11`.
Further acceptance uses the installed owner through ordinary CoreAudio and
CoreMIDI clients; direct diagnostic USB claims must not compete with it.
Physical DIN loopback, toe/external inputs, sleep/wake, effects-host integration
and rates beyond stereo 48 kHz remain unvalidated.

## Follow-up silent reproduction and build 6

A focused unattended checker reproduced the installed build-5 fault with
silent output and the persistent MIDI client: at status tick 257 (approximately
25.7 seconds), generation 2 retained its identity while the owner reported
`IsoTooOld` and lost AUDIO_RUNNING. The client stopped cleanly with 9881
callbacks and 1264768 frames. Existing unified logs independently show XHCI
request-link rejection and remote RK3 enqueue errors at
00:53:25.309 UTC, followed by the host stop/restart handshake. Scheduled/current
USB frames, endpoint and request identity are not visible in those logs.

A controlled audio-only run, with MIDI client and server absent, then completed
120 seconds without a new error: 45022 callbacks, 5762816 frames, RMS 0.000102,
peak 0.003777, stop 0. A restored-MIDI repeat also completed 120.028 observed
stable seconds: 44886 callbacks, 5745408 frames, RMS 0.000102, peak 0.003783,
stop 0. A subsequent identical silent run failed after 15.646 observed seconds
at tick 148. Thus MIDI alone is not established as the cause, and successful
short repeats do not establish sustained reliability.

The diagnostic checker ignores retained idle errors only before the first
AUDIO_RUNNING observation. Once running, transport errors, unsolicited state
loss or generation changes fail the check. Offline process fixtures verify
full-duration acceptance, premature observer/client exit, stalled output,
deadlines and cleanup. Debug files remain clearly labeled under
`.build/native/debug/`; none opens USB directly.

Separately, a production-ISO simulation proved a callback-order dependency
cycle with only a one-millisecond delay on slot 0. An earlier callback needed
to advance the read cursor could sit behind a newer, blocked FIFO head. After
128 further simulated milliseconds, all 64 input requests were stalled with
no diagnostic or panic. Delivering the same raw completions in acceptance
order sustains 20 simulated seconds. This is a proven current-binding bug,
not proof of the physical `IsoTooOld` cause.

Version **0.2.1/build 6** restores per-pipe ISO callback delivery order in the
Host binding and retains mappings through ordered retirement. It also adds
first-rejection-only `[DEBUG-rk3-timing]` observations: requested/current USB
frames, submission timing, kernel transaction timestamp, framework/worker/
ordered-delivery times, and engine cursor/queue state. These observations do
not alter NI ring placement, queue depth, calendar or recovery policy. They
are temporary and must be removed after the causal correction is verified.
Build 6 was subsequently installed as recorded below. Beads `.11` tracks
the live fault and `.12` the corrected ordering bug.

## Installed 0.2.1/build 6 and planning gate

The user installed build 6 in session:

`/Library/Application Support/RigKontrol3/NativeDriverBackups/20261025T013609Z-bde94829`

Independent checks confirmed the exact 12-file installed inventory, expected
identities, ARM64 slices, version 0.2.1/build 6 and valid signatures. The frozen
manifest SHA-256 is
`e2ee623f97af89c24d94e38d7061b460c3e1bd51dd13052d57b9937ecaa400e6`.
The root-only session journal itself remained unreadable. CoreAudio restarted;
the RK3 remote driver process was PID 98416 and audio device 237, generation 1.

The first 128-frame silent run passed 120.012 observed seconds: 44881 callbacks,
5744768 input frames, RMS 0.009566, peak 0.108374, stop 0. A longer silent run
with MIDI active failed after 227.593 observed seconds at status tick 2154,
with `IsoTooOld` and loss of AUDIO_RUNNING while USB generation stayed 1.
It stopped cleanly with 85222 callbacks, 10908416 frames, RMS 0.007900,
peak 0.136124 and stop 0. Short green intervals do not establish stability.

The tagged trace at **01:44:26.829676 UTC** identifies IN endpoint 0x82,
slot 44, requeue target 3342140 and post-rejection current frame 3342160.
The target equals source request 3342076 plus NI's original 64-frame horizon.
With independently read Mach timebase 125/3, the intervals are:

| Observation | Elapsed |
| --- | ---: |
| Observed transaction timestamp to framework callback | 11.395792 ms |
| Framework callback entry to worker block entry | 54.539625 ms |
| Worker entry to ordered delivery | 0.000083 ms |
| Ordered delivery to enqueue entry | 0.003208 ms |
| Enqueue entry to return | 17.702292 ms |

The request was 20 frames late at the post-rejection query. The ready FIFO was
empty and the decoder guard available; consumed epochs showed the input had
been processed. This event does not exhibit the corrected FIFO dependency
cycle. The transaction timestamp is an observed completion timestamp, not
proof of exact wire time. The 54.54 ms interval includes raw-handler execution,
publication and worker servicing; it does not distinguish blocking, CPU work
or descheduling. The 17.70 ms API interval is wall time, not measured CPU time.
A nonprivileged stack-sampling attempt was denied; no administrator sampling,
QoS change, queue tuning or new driver change followed.

After the user's instruction to prioritize the lift-and-shift plan, diagnostic
MIDI client 99411 and timing-log stream 98726 were identified and stopped.
No audible helper is running. The installed driver remains in place.
Beads `rigkontrol3-40h.13` now gates further implementation and runtime paths
with an original-to-ARM inventory and written platform-necessity decision.
Full parity and stability remain incomplete.
