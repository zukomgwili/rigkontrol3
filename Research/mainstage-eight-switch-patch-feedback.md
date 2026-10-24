# MainStage eight-switch mapping and patch feedback

Recorded 2026-10-24. This evaluation extends the existing native RK3 controller
to eight real effect mappings in the current MainStage concert. The controller
app code and its authoritative host-feedback LED path are unchanged.

## Configured controls

The user approved the starter effect categories. A group of eight screen
buttons replaces the old lone wah button. All use **RigKontrol3 Bridge**, MIDI
channel 16, CC20…27, **Single Value**, **Do not pass through**, and feedback to
**RigKontrol3 Bridge**. **Show Input Value is off** and **On Patch Change is
explicitly Reset**. The existing WahSweep uses CC11 Absolute, no MIDI thru,
and NI P018; stored measured raw endpoints remain 88…633, giving effective
calibrated travel 98…623 after the 2% end margin.

| Switch | CC | Audio 3 target | Patch A | Patch B |
| --- | --- | --- | --- | --- |
| 1 | 20 | Guitar Rig 6 Cry Wah power, P017 | On | Off |
| 2 | 21 | Overdrive Bypass | Off | On |
| 3 | 22 | Compressor Bypass | On | On |
| 4 | 23 | Chorus Bypass | Off | Off |
| 5 | 24 | Phaser Bypass | On | Off |
| 6 | 25 | Tape Delay Bypass | Off | On |
| 7 | 26 | ChromaVerb Bypass | On | Off |
| 8 | 27 | Tremolo Bypass | Off | On |

The NI target uses Button On = 1 / Off = 0. Native **Bypass** targets use
Button On = not bypassed (0), Button Off = Bypassed (1), with **Range Inverted**.
The LED's meaning is effect enabled, not the raw bypass bit. These are saved
MainStage mappings; the bridge still accepts only actual host CC feedback
0/127 and does not infer state from physical presses. See the
[session implementation](../Sources/RigKontrol3Core/Bridge/RigKontrolControllerSession.swift)
and [setup guide](../Documentation/Controller-Setup.md).

The actual Audio 3 insert order, confirmed by the user and the UI, is
**ChromaVerb → Tape Delay → Phaser → Chorus → Compressor → Overdrive →
Guitar Rig → Tremolo**.

The user-approved mute of the original parallel **Brit and Clean** path is
applied and saved in both playing patches.

Retained test audio settings are Master fader **−∞ (value 0)** and toolbar
**Master Mute 1**, with Audio 3 unmuted. These levels were not changed during
mapping. The validation establishes controller/LED feedback, not audible audio
output.

## Patch fixture and observed evidence

The two saved patches are **01 Wah & Space** (A, mask **0x55**) and
**02 Drive & Pulse** (B, mask **0xA6**). Bit 0 is switch 1. Compressor stays on
and Chorus stays off in both patches, allowing the test to distinguish a full
refresh from feedback limited to changed values. The permanent artifact is
[Concerts/Rig Kontrol 3 Effects.concert](<../Concerts/Rig Kontrol 3 Effects.concert>).
Saving and auditing it is separate from successful reload validation.
The final source audit confirmed two patches with masks A=0x55/B=0xA6 and B
selected when saved. Its 15 files total 331,663 bytes; the audit found no
private paths, credentials or copied audio assets.

After Clear Host State, a log boundary was established at line 3667. Selecting
patch A alone then produced fresh channel-16 CC20…27 values and a complete
eight-control cache matching 0x55. This included CC22 = 127 for the unchanged
Compressor and CC23 = 0 for the unchanged Chorus. The offline checker passed.
The line boundary matters: earlier cached/setup messages do not prove a fresh
patch-selection refresh. Primary local evidence:
[patch-a-feedback.jsonl](../.build/diagnostics/eight-switch-mapping/patch-a-feedback.jsonl).

The user then confirmed all eight physical switches changed their assigned
effects with matching LEDs and that the pedal worked. The log recorded exactly
one CC127 press per switch, zero release messages, final host mask **0xAA**
after toggling all eight from patch A, and expression **0…127**. This completes
the eight physical mappings check.

Patch B also passed a separate fresh-feedback test after Clear Host State,
using line boundary 4090 through 4115: all eight values reconstructed the
expected **0xA6** state, and the checker passed. Primary local evidence:
[patch-b-feedback.jsonl](../.build/diagnostics/eight-switch-mapping/patch-b-feedback.jsonl).

Ordinary warm A/B changes and physical LED patterns passed without Clear Host
State: A = 1/3/5/7 and B = 2/3/6/8, with LED 3 remaining on and LED 4
remaining off. Native USB recovery and sleep/wake evidence is recorded separately in the
[backend evaluation](rk3-coreaudio-backend-evaluation.md).

## Saved-reload counterexample

Saved reload has **not passed**. File Revert of the permanent concert was tested
after log boundary 4294. The actual NI Generic Controls P017 value read
100 / 1.00 (wah on).
The full frozen trace contains **one CC20 value 127 followed by sixteen values
0**, all at 15:38:04 UTC. The last zero correctly became the bridge's latest
state, producing **0x54** rather than expected patch-A **0x55**; the other seven
states were correct. The initial 127 was not lost. The actual parameter was
read without changing it and remained 1.00. Evidence:
[saved-reload-feedback.jsonl](../.build/diagnostics/eight-switch-mapping/saved-reload-feedback.jsonl).

The observed gap is between the actual restored NI parameter and the latest
host feedback. Host initialization/notification timing, including possible
late NI restoration, is a hypothesis, not an established cause. The bridge
applies the latest received value correctly. It must not fabricate CC20=127 or infer it from the
saved patch mask to conceal this failure. Warm A/B success does not validate
reload.

The user chose to retain **only the two playing patches** and track the reload
failure as **tw5.1.2**. The permanent artifact was saved with patch B selected.
No additional startup patch or startup workflow is retained. Ordinary warm
selection/reselection of A/B refreshed feedback to match actual states in the
verified tests; that does not validate initial concert loading.

An additional candidate feedback source was observed: an orphan screen control
named **Button 1** retains a configured CC20 feedback output to RigKontrol3 Bridge alongside the active
**1 Wah** control. Its configuration is verified, but no cleanup or causal
test was applied. It is not established as the cause of the reload sequence.

Apple documents that reopening selects the patch selected when last saved.
General Startup Action selects a concert and mode; no independent initial-patch
selector is documented there. Neither behavior guarantees correct initial AU
parameter feedback. See
[open/close behavior](https://support.apple.com/guide/mainstage/open-and-close-concerts-mstg5ac61e78/mac)
and [General settings](https://support.apple.com/guide/mainstage/general-settings-mstgaa975f69/mac).

## Documented behavior and practical limits

Apple's Send Value to description promises the current screen-control value.
It does not specify a complete batch, message ordering, or retransmission of
unchanged values on every patch selection. The complete cleared-state A/B
results above are therefore measured properties of this configured concert, not a generic
MainStage guarantee. Screen controls normally show their mapped parameter;
Show Input Value would instead show physical input, so it remains off.
[Feedback setting](https://support.apple.com/guide/mainstage/common-screen-control-parameters-mstge3da7da4/mac),
[displayed value](https://support.apple.com/guide/mainstage/set-screen-controls-show-hardware-mstg02e21cb5/mac).

Reset/Keep/Match determine actual parameter behavior rather than requesting a
feedback snapshot. Reset restores saved values, Keep retains edits, and Match
uses the last hardware value. Reset also applies when entering Layout mode.
Each patch's intended values must be saved deliberately; the selected patch's
mapped values are saved when saving the concert. Relative is a separate
hardware-matching response, not a refresh mode.
[Patch-change behavior](https://support.apple.com/guide/mainstage/set-parameter-change-behavior-screen-controls-mstgad08f9ca/mac),
[saving values](https://support.apple.com/guide/mainstage/how-saving-affects-parameter-values-mstg2ab24b77/mac),
[hardware matching](https://support.apple.com/guide/mainstage/set-hardware-matching-behavior-screen-mstg539c021a/mac).

The bridge does not automatically identify patch/concert changes. Before
testing a new context, clear its cached host state and require fresh feedback;
an absent mapping must not inherit an old state. Closing a concert without
quitting MainStage does not invoke the existing application-quit invalidation.
No generic external refresh request is established by this experiment.

Apple documents per-patch program/bank output and MIDI-file sending through an
External Instrument strip. These could carry explicitly configured context
markers, but ordering against feedback and concert-close signaling would need
separate validation. Static MIDI-file values cannot substitute for current
effect-state feedback. No such adapter is required or adopted at this
checkpoint.
[External MIDI instruments](https://support.apple.com/guide/mainstage/use-external-midi-instruments-mstg52ccce25/mac).
