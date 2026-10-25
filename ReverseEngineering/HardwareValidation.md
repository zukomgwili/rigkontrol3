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

Audio test clients were stopped. No rollback was performed; build 4 remains the
installed version until the next explicit installation. All 12 installed files
matched the build manifest after installation, which establishes content
integrity rather than audio quality.

## Version 0.2.0 (build 5): faithful port awaiting installation

The user directed preserving NI's original audio ring, timing and recovery.
The source replaces the FIFO/snapshot adapter with the sample-addressed live
mapped ring and restores the original input-led calendar. It uses primary
completion polling, actual USB-frame anchors and byte-position zero timestamps,
NI latency/sample-offset values 68/136/136 at 48 kHz, and host-mediated audio-only
panic recovery. The protocol, original controller profile, measured calibration
and incoming LED feedback are retained. See `AudioDriver.md` and `NativeUSB.md`
for original addresses and explicit current-platform substitutions.

**This version has not been installed or tested on the device.** Hardware-free
regressions model late callbacks and future ring writes, whole-IRP primary
publication, exact timestamp fractions, wrapped host blocks, stop/drain and
transient EP1/MIDI failures. They cannot establish real DMA prefetch timing,
audible latency, sound continuity or actual host power behavior.

The next acceptance stage uses the installed owner through ordinary CoreAudio
and CoreMIDI clients: silent continuity with stable generation/error status,
then sustained unity-gain guitar with all eight switches, pedal and incoming
LED feedback, followed by recovery checks. Direct diagnostic USB claims must
not compete with the installed owner. Physical DIN loopback, toe/external
inputs and rates beyond stereo 48 kHz remain unvalidated.
