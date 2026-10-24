# Rig Kontrol 3 effects controller

This personal-use Apple Silicon app connects the Rig Kontrol 3 switches and expression pedal to CoreMIDI. Version 0.4 uses the combined RK3 audio driver, which owns USB once for audio and controls. The experimental setup hosts Guitar Rig 6 in MainStage so the host can return actual effect states for the hardware LEDs. Guitar Rig 6 standalone remains an option for basic MIDI control, but its effect-state feedback is not established. See the [shared driver guide](../SharedHAL/README.md) for installation, rollback and native validation commands.

## Build and open

From the repository, run:

```bash
Scripts/build-rk3controller.sh
open ".build/Rig Kontrol 3.app"
```

Quit the previous controller before building. The script builds a native arm64 app and signs it locally. Version **0.4.0**, build **4**, defaults to the shared CoreAudio transport. It creates `.build/Rig Kontrol 3.app` and preserves the previous app under `.build/ControllerBackups`; it does not install or launch anything. Build and install the matching shared HAL as described in its guide before opening the new app. Keep the app in a stable location before enabling launch at login, and rebuild it at that same location when updating.

The shared HAL uses native IOKit access; the app exchanges controller reports and acknowledged USB write results through CoreAudio properties. Neither path creates a libusb context. Legacy diagnostics remain in the core target, so the app may still link libusb; the script bundles it only when needed. A bundled Homebrew libusb may require a newer macOS version than the Swift package's declared minimum. This is a build for the current Mac; older-Mac compatibility is not established by a successful local build.

The app and driver include GPL-2.0 notices, attribution and adapted source snapshots. The shared engine uses the pinned rk3-coreaudio audio/codec implementation with a persistent command reader, separate asynchronous command writes and safe callback draining, guided by the [legacy driver reconstruction](../Research/legacy-rk3-driver-comparison.md). It does not install the upstream MIDI/UDP helper. The shared installer replaces the previous experimental HAL with a verified backup and rollback path; the old source remains separate under `HAL/`.

Use the menu-bar item to check the connection, start or stop the bridge, quit, and optionally enable launch at login. **Setup & Test…** opens assignment instructions, live switch counts, pedal range, and received host LED states. Start manually until the playing setup has been verified. The app requests automatic reconnection after USB reattachment or waking the Mac when enabled. Held-switch recovery, menu stop/start and physical sleep/wake passed on version 0.3's direct transport; the combined transport requires its own hardware checks. USB reconnection alone does not establish fresh host effect states in a changed concert or patch.

Run only one RK3 controller app at a time and quit the legacy `rk3bridge` CLI. The shared HAL retains USB while audio is idle so switches remain available. Start/Stop Controller attaches or detaches the app without stopping guitar audio. The explicit `--controller-only` diagnostic option uses direct USB and must not run while the shared HAL owns the device; restore the previous HAL/app pair before returning to that setup. There is no automatic fallback that would create competing USB owners.

## MIDI controls

All controls use **MIDI channel 16**.

The app publishes a MIDI source and a separate feedback destination, both named **RigKontrol3 Bridge**. Select that name as the host's input for pedal commands and as its output for returned state. Matching the two endpoint names enabled MainStage's **Send Value to** selection; they retain different stable CoreMIDI IDs.

| Physical control | MIDI output | Intended assignment |
| --- | --- | --- |
| Switches 1–8 | CC20–CC27 respectively; values depend on Control Mode below | One effect on/off per switch |
| Expression pedal | CC11, values 0–127 | Wah sweep |

Choose the input behavior from the app's **Control Mode** menu:

- **MainStage (toggle buttons)** is the default. Each physical press sends value **127**; releasing sends no MIDI. It also sends no neutral reset before the next press. Configure MainStage's button as **Single Value** so the host can toggle its actual parameter on each trigger. The guided eight-effect test verified all eight physical switch/LED mappings, with one press message per switch and retained states after release.
- **Guitar Rig standalone / other hosts** sends momentary values: **127 on press, 0 on release**. Use this for the standalone Macro-button instructions below or another host that expects press/release pairs.

Changing modes clears cached host LED states and establishes a new USB switch baseline without sending effect commands. Release a switch held during the change before using it again. Both modes use the same authoritative host-feedback LED path; the mode changes input messages, not the meaning of an LED.

Use one of the eight switches for wah on/off. Toe-down switching is not part of this version.

The current menu-bar app changes switch LEDs **only from host feedback**: channel 16, CC20–CC27, value 127 for on and 0 for off. Physical presses never light or locally toggle an LED. Other channels, controllers, and intermediate values do not establish LED state. Controls without a received host state remain dark and appear as **unknown** in Setup & Test; darkness alone is not confirmation that an effect is off. The legacy CLI's LED behavior is separate from the menu-bar app.

At each connection, the app queries the current switch state and establishes a baseline without sending switch commands. Wait for **Connected — MIDI ready** before playing; no manual arming press is needed. If a switch is already held at connection time, release it before its next press can control an effect. Recovery does not replay previous toggles.

In **Guitar Rig standalone / other hosts** mode only, if a release was lost during disconnection, the next deliberate press sends value 0 before value 127 on that switch's CC to restore the host's press detection. This happens only on a new press; reconnecting alone sends no switch commands. The live test view labels value 0 as release/reset, since these recovery zeroes are not additional physical releases. **MainStage (toggle buttons)** mode sends neither release values nor these neutral reset messages.

## Calibrate the expression pedal

Open **Setup & Test**, click **Calibrate Pedal**, sweep fully heel-to-toe and back through the travel you normally use, then click **Finish Calibration**. Sweep again and confirm the observed MIDI range reaches **0…127**. The app saves the measured raw endpoints for future launches and allows a small margin at each end so sensor noise does not prevent full effect travel. Calibrating updates expression scaling without reconnecting USB or changing switch states; a disconnected session cancels an unfinished calibration.

The current Mac stores measured raw endpoints **88…633**. The 2% end margin gives effective calibrated travel **98…623**. Calibrate again if this unit's usable travel changes.

## Experimental MainStage setup for effect-state LEDs

Start the controller app before MainStage so both MIDI directions exist when the concert opens. Select **Control Mode → MainStage (toggle buttons)** in the controller app. With the combined driver installed, choose **RigKontrol3 Audio (Shared)** as MainStage's input and output, **48 kHz**, and Audio 3 input 1 for the guitar. Connect headphones or speakers to the RK3. The starter concert combines the NI Cry Wah with seven built-in MainStage effects on **Audio 3**.

**Reload limitation under investigation:** after File Revert, the actual NI wah power parameter was on while MainStage's latest feedback was off, leaving its LED off. The frozen trace contains one CC20 value 127 followed by sixteen values 0; the bridge correctly applied the latest message. Warm patch changes and the physical mappings passed, but saved-concert reload is not yet reliable for this wah indication. This remains **tw5.1.2**; the concert retains only the two playing patches.

1. In **Layout**, use a group of eight Button screen controls. Assign them to **RigKontrol3 Bridge**, channel **16**, CC**20…27**, Type **Single Value**, MIDI Thru **Do not pass through**. This group replaces the old lone wah button; the **WahSweep** control remains separate.
2. For every button, set **Send Value to → RigKontrol3 Bridge**. Commit the destination selection with **Return** and recheck it afterward; Escape cancels the selection.
3. In **Edit**, map each button to the actual power/bypass parameter in the table below. Set **Show Input Value off** and **On Patch Change → Reset** explicitly. Save each patch's intended effect values before leaving it. Reset also applies when switching to Layout mode.
4. The NI Cry Wah target uses **Button On = 1, Button Off = 0**. The seven native plug-in **Bypass** targets use **Button On = — / not bypassed (0)** and **Button Off = Bypassed (1)**, with the mapping's **Range Inverted**. This makes a lit button/LED mean the effect is active. Verify the actual bypass state rather than relying on its numerical value alone.
5. Keep **WahSweep** assigned to channel **16**, **CC11**, Type **Absolute**, MIDI Thru **Do not pass through**, mapped to **Guitar Rig 6 FX → P018**. The power target is **P017** in this rack; NI parameter IDs depend on rack contents and must be checked in other presets.

| Switch | CC | Actual target | 01 Wah & Space | 02 Drive & Pulse |
| --- | --- | --- | --- | --- |
| 1 | 20 | NI Cry Wah power, P017 | On | Off |
| 2 | 21 | Overdrive Bypass | Off | On |
| 3 | 22 | Compressor Bypass | On | On |
| 4 | 23 | Chorus Bypass | Off | Off |
| 5 | 24 | Phaser Bypass | On | Off |
| 6 | 25 | Tape Delay Bypass | Off | On |
| 7 | 26 | ChromaVerb Bypass | On | Off |
| 8 | 27 | Tremolo Bypass | Off | On |

The saved fixture states are **0x55** and **0xA6**, respectively, with bit 0 representing switch 1. Compressor stays on and Chorus stays off across both patches so the test includes unchanged values.

The actual Audio 3 signal chain is **ChromaVerb → Tape Delay → Phaser → Chorus → Compressor → Overdrive → Guitar Rig → Tremolo**, as confirmed in the UI and by the user.

The user-approved mute of the original parallel **Brit and Clean** path is applied and saved in both playing patches.

The saved fixture retains **Master fader −∞ (value 0)** and **Master Mute on (value 1)**; Audio 3 is unmuted. Earlier tests validated controller/LED feedback, not audible audio output. After verifying the shared audio route, set a low listening level and unmute for the guitar test.

All eight physical switch/effect/LED mappings and the pedal have passed the guided test. The log recorded exactly one CC127 press for each switch, zero switch releases, a final host mask of **0xAA** after toggling all eight from patch A, and expression values **0…127**.

After **Clear Host State**, selecting **01 Wah & Space** alone produced fresh feedback for all eight controls and a complete cache matching **0x55**. Selecting **02 Drive & Pulse** in a separate cleared-state test likewise produced complete fresh **0xA6** feedback. Both offline checks passed, including the unchanged Compressor-on and Chorus-off values. Ordinary warm A/B switching and physical LED patterns also passed without clearing cached state. Saved reload is a separate failed check: at 15:38:04 UTC, CC20 value 127 was followed by sixteen zeroes, so its last value produced **0x54** while the actual NI wah parameter remained 1.00/on. Patch A should have been **0x55**. This is a host parameter/notification mismatch under investigation, not evidence that the bridge lost the initial message. See [patch-A evidence](../.build/diagnostics/eight-switch-mapping/patch-a-feedback.jsonl), [patch-B evidence](../.build/diagnostics/eight-switch-mapping/patch-b-feedback.jsonl), [reload evidence](../.build/diagnostics/eight-switch-mapping/saved-reload-feedback.jsonl), and [the mapping research note](../Research/mainstage-eight-switch-patch-feedback.md).

Earlier native-backend checks also passed the wah mouse-to-LED behavior, held-switch USB recovery, stop/start and sleep/wake. See [the native-session log](../.build/controller-rk3coreaudio-native.jsonl).

The permanent saved artifact is [Concerts/Rig Kontrol 3 Effects.concert](<../Concerts/Rig Kontrol 3 Effects.concert>). Saving and auditing the artifact did not establish correct feedback after reload; the wah mismatch above remains open.

The saved concert contains only the two playing patches above, with **02 Drive & Pulse** selected when saved. The user chose to keep this setup and track the reload issue as **tw5.1.2**, without adding a startup workflow. Ordinary warm patch selection/reselection refreshed feedback to match the actual effect states in the tests; initial loading remains unreliable for the wah indication. MainStage documents that reopening selects the patch selected when last saved; this does not guarantee correct initial parameter feedback. See [Apple's opening behavior](https://support.apple.com/guide/mainstage/open-and-close-concerts-mstg5ac61e78/mac) and [the concert usage guide](../Concerts/README.md).

Apple documents [parameter mapping](https://support.apple.com/guide/mainstage/map-to-channel-strip-and-plug-in-parameters-mstgf1477b4c/mac), [Send Value to](https://support.apple.com/guide/mainstage/common-screen-control-parameters-mstge3da7da4/mac), [mapped values versus Show Input Value](https://support.apple.com/guide/mainstage/set-screen-controls-show-hardware-mstg02e21cb5/mac), and [Reset/Keep/Match behavior](https://support.apple.com/guide/mainstage/set-parameter-change-behavior-screen-controls-mstgad08f9ca/mac).

### Host-state lifecycle limits

The app does not automatically identify the active concert, patch, or mapping revision. Ordinary changes between the included **01 Wah & Space** and **02 Drive & Pulse** patches need **no manual Clear Host State**; that bank passed warm A/B testing. For unverified patches, concert replacement/closure or remapping, use **Clear Host State** and check fresh feedback against the actual effect parameters. Clearing turns unknown LEDs off but does not request a new snapshot from MainStage. A previously known state must not be treated as evidence for an unmapped control in another patch.

The app invalidates cached host state when MainStage quits. Closing a concert while MainStage stays open does not trigger that application-termination handling. Both patches' complete cleared-state and warm-switch feedback is observed for this concert, not an Apple guarantee for arbitrary patches. The saved-reload wah mismatch is an observed failure; bridge startup after an already-open concert, missing mappings and concert replacement need separate verification. Stop/start and USB recovery retain received host states, so they depend on the host context still being valid and feedback continuing to update. Clear cached state and compare fresh feedback with actual effect parameters when testing a new context.

## Guitar Rig 6 standalone: basic MIDI control

This route keeps the Macro-based toggle setup below, but does not provide verified effect-state feedback to the app. LEDs therefore remain unknown/dark unless an explicitly configured host sends valid feedback. It does not satisfy the accurate-LED requirement on its own.

Select **Control Mode → Guitar Rig standalone / other hosts** in the controller app before following these steps. This provides the momentary 127/0 input pair expected by the Macro setup.

1. In Guitar Rig's audio preferences, select the separate audio interface. Confirm that the guitar can be heard through Guitar Rig before assigning pedals.
2. In **Preferences → MIDI**, enable **RigKontrol3 Bridge** under MIDI inputs and choose channel **16**, or **Omni** while testing.
3. Load a simple rack containing a wah and the effects you want to control.
4. Open **Show Macros**, the first Rack Tool icon before Tapedeck Pre. Right-click Macro 1 and select **Toggle Knob/Button Mode** to make it a button. Keep it in Toggle mode: an **Enable Hold Mode** menu entry means Hold is currently off.
5. Right-click Macro 1, choose **Learn MIDI Control**, and press switch 1. Drag the Macro's numbered badge or adjacent target icon onto the effect's on/off control. Clear any direct MIDI assignment on that effect's power control to avoid a parallel momentary mapping. Test several complete press/release cycles; each press must toggle once and release must leave the new state unchanged.
6. Repeat with Macros 2–8 and switches 2–8 for other effects. Use one Macro button for wah on/off. A component's outer power control may not offer Hold/Toggle mode; configure the Macro button instead.
7. Use **Learn MIDI Control** directly on the wah's continuous position parameter, then sweep the expression pedal slowly through its full travel. The software control should reach both ends and remain stable when held.
8. Save the Guitar Rig preset and any applicable controller defaults. Reload it and restart Guitar Rig to verify assignments are retained. Do not assume a component assignment automatically applies to every preset. Start the controller before Guitar Rig so the MIDI source exists when the host enumerates inputs.

In this mode, the bridge emits momentary CC values and leaves effect toggling to the Macro button. Directly learning the component power can make it follow press/release instead. Check Macro/target synchronization after mouse edits before relying on that control workflow. The app has no physical-press LED fallback and does not guess effect state from the Macro assignment.

The [Guitar Rig 6 June 2023 manual, pp. 42–43](https://www.native-instruments.com/fileadmin/ni_media/downloads/manuals/gr6/GUITAR_RIG_6_Manual_English_20_06_2023.pdf) documents Macro button Toggle/Hold modes, learning, and target assignment. The [online automation and MIDI guide](https://docs.native-instruments.com/ni-tech-manuals/guitar-rig-manual/en/automation-and-midi-control) describes assignment scopes, but is updated for newer Guitar Rig releases.

## Joint playing verification

Use the following for joint verification and repeat them after transport or mapping changes. All eight physical effect/LED mappings, full expression range, held-switch recovery, stop/start and sleep/wake have passed. Both patches returned complete fresh feedback after clearing host state, and warm A/B round trips passed without clearing. Saved reload currently fails the wah-state comparison; its fix/workaround, other host contexts and sustained sessions remain open:

- Press every switch repeatedly and rapidly; each physical press toggles only its assigned effect, once. Releases and simultaneous switches behave correctly.
- Confirm each LED matches its actual effect state after a footswitch press and remains correct after release. Change the actual effect with the mouse and require the LED to follow without a pedal press. Unknown controls must remain labelled unknown, rather than being inferred from presses.
- Sweep heel to toe and back; confirm the wah reaches both endpoints, moves smoothly, and stays stable while held.
- Save and reload the MainStage concert and hosted Guitar Rig rack, then restart the controller app and host and require fresh effect-state feedback. Verify the standalone Macro assignments separately if using that basic-control option.
- For unverified patches, concert replacement/closure or remapping, clear host state and compare fresh feedback with actual effects. The included A/B bank needs no manual clear during ordinary changes. Test missing mappings and MainStage quit/relaunch separately.
- Unplug and reconnect USB while idle and while holding a switch. Confirm status changes and recovery without an unintended effect toggle; release a held switch before pressing again.
- Stop and start the bridge, then sleep and wake the Mac. Confirm that there is one controller source, its paired feedback destination, and one responding bridge, with no stuck LEDs or unwanted toggles. Recheck feedback freshness as well as USB connectivity.
- Play for a sustained session, listening for missed, doubled, or delayed controls.

Other VST hosts can receive the same CoreMIDI controls when their MIDI routing and learn modes are configured. Accurate LEDs additionally require real parameter-state feedback and lifecycle verification for that host; a MIDI echo is insufficient. Combined RK3 audio/control, preset banks, toe-switch support, and a public installer remain outside this milestone.

## Developer checks

Run `swift test` for parsing, controller state, session lifecycle, expression endpoints, feedback decoding/state, USB input backpressure and native stream ownership tests. The live session uses `CoreAudioUSBEventReader` and the `CRK3CoreAudioUSB` C transport, with interface 0/alternate setting 0 for controls. Build and sign the local app with `Scripts/build-rk3controller.sh`. The source and destination use distinct stable IDs, `0x524B3301` and `0x524B3302`, despite their shared display name.

The clean controller source snapshot at `82b5188a` executed **107 tests, with two native opt-in checks skipped and zero failures**. Its version 0.3.0/build 3 package passed arm64, plist, code-signature and bundled-source checks. The full working-tree run executed 116 tests with the same skips and no failures; its additional nine tests cover pre-existing experimental audio work excluded from that snapshot. Both native CoreMIDI opt-in checks also passed separately outside the sandbox; they do not access USB. All five native-stream tests passed, including removal/busy error translation and simulated recovery through the actual stream. The rebuilt app passed its attached three-second readiness check with no switch commands and clean shutdown; see [the final clean-package smoke log](../.build/controller-smoke.pc5o1j). Physical checks separately passed for all eight current effect/LED mappings, full expression range, held-switch recovery, stop/start and sleep/wake. The remaining joint checks retain their own acceptance limits.

With the RK3 attached, its audio idle, and the controller app quit, run:

```bash
Scripts/check-rk3controller.sh
```

This opens a three-second test session and checks USB/CoreMIDI readiness, absence of idle switch messages, and clean shutdown. It keeps the event log in `.build/`. Physical controls, host toggle behavior, and unplug/sleep recovery use the joint playing checks above.

The transport keeps EP1 input reads running during startup commands and LED writes. Real-device testing exposed OUT timeouts when input was left unread, including between consecutive initialization writes. A single initial read was insufficient. Keep the continuous reader and its join-before-release ownership when changing the transport.
