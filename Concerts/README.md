# Rig Kontrol 3 Effects

Open [Rig Kontrol 3 Effects.concert](<Rig Kontrol 3 Effects.concert>) in
MainStage after starting the **Rig Kontrol 3** controller app. Use
**Control Mode → MainStage (toggle buttons)** and a separate audio interface.
The MIDI input and feedback output are both **RigKontrol3 Bridge**, channel 16.

The concert contains two playing patches. The saved selection is **02 Drive &
Pulse**. All eight physical switch/effect/LED mappings, the wah pedal, and two
warm A/B round trips without clearing host state passed the guided test.

| Switch | CC | Effect | 01 Wah & Space | 02 Drive & Pulse |
| --- | --- | --- | --- | --- |
| 1 | 20 | NI Cry Wah | On | Off |
| 2 | 21 | Overdrive | Off | On |
| 3 | 22 | Compressor | On | On |
| 4 | 23 | Chorus | Off | Off |
| 5 | 24 | Phaser | On | Off |
| 6 | 25 | Tape Delay | Off | On |
| 7 | 26 | ChromaVerb | On | Off |
| 8 | 27 | Tremolo | Off | On |

Expected LED groups are **1/3/5/7** for Wah & Space and **2/3/6/8** for Drive &
Pulse. LED 3 stays on and LED 4 stays off when changing between them. Each
press sends 127; release sends no switch MIDI. LEDs follow actual host feedback.
The pedal sends **CC11**, controlling NI wah P018 over **0…127**. This unit's
stored measured raw endpoints are **88…633**; the 2% end margin gives effective
calibrated travel **98…623**.

Audio 3's actual signal chain is **ChromaVerb → Tape Delay → Phaser → Chorus →
Compressor → Overdrive → Guitar Rig → Tremolo**. The original parallel
**Brit and Clean** path is muted in both patches as requested.

Retained test levels are **Master fader −∞ (value 0)** and **Master Mute on
(value 1)**; Audio 3 is unmuted. These tests verified controller/LED feedback,
not audible audio output. Choose the separate audio interface and set the
listening level when ready to play.

## Known reload limitation

A saved-concert reload test failed the wah-state comparison. File Revert restored
actual NI wah P017 to 1.00/on, but its feedback ended at off: one CC20 value 127 was
followed by sixteen zeros. The bridge correctly displayed the latest value,
leaving mask 0x54 instead of patch A's 0x55. This remains **tw5.1.2**.

Ordinary warm patch selection/reselection refreshed feedback to match actual
effect states in the tests. Do not treat that as a guarantee for initial load
or other concerts. The user chose to retain these two patches and track the
issue without an additional startup workflow.

See [Controller Setup](../Documentation/Controller-Setup.md) for configuration
and recovery details, and the [feedback evaluation](../Research/mainstage-eight-switch-patch-feedback.md)
for the recorded evidence and remaining limits.
