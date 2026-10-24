# Rig Kontrol 3 effects controller

This personal-use Apple Silicon app connects the Rig Kontrol 3 switches and expression pedal to CoreMIDI. The current experimental setup hosts Guitar Rig 6 in MainStage so the host can return effect states for the hardware LEDs. Guitar Rig 6 standalone remains an option for basic MIDI control, but its effect-state feedback is not established. Use another audio interface for the guitar and headphones or speakers.

## Build and open

From the repository, run:

```bash
Scripts/build-rk3controller.sh
open ".build/Rig Kontrol 3.app"
```

The script builds a native arm64 app, bundles libusb, and signs it locally. It creates `.build/Rig Kontrol 3.app`; it does not install or launch anything. Keep the app in a stable location before enabling launch at login, and rebuild it at that same location when updating.

This is a build for the current Mac. Its bundled Homebrew libusb may require a newer macOS version than the Swift package's declared minimum; compatibility with older Macs is not established by a successful local build.

Use the menu-bar item to check the connection, start or stop the bridge, quit, and optionally enable launch at login. **Setup & Test…** opens assignment instructions, live switch counts, pedal range, and received host LED states. Start manually until the playing setup has been verified. A disconnected device should reconnect automatically when plugged in; waking the Mac should restore the connection when the bridge is enabled. USB reconnection alone does not establish fresh host effect states.

Run only one RK3 controller bridge at a time. Quit the legacy `rk3bridge` CLI before opening the app. The installed experimental RK3 audio driver claims the same USB interface when its audio starts: select the separate interface in MainStage or Guitar Rig and ensure no other application or system audio is streaming through the RK3. An installed but idle RK3 audio driver does not need removal.

## MIDI controls

All controls use **MIDI channel 16**.

The app publishes a MIDI source and a separate feedback destination, both named **RigKontrol3 Bridge**. Select that name as the host's input for pedal commands and as its output for returned state. Matching the two endpoint names enabled MainStage's **Send Value to** selection; they retain different stable CoreMIDI IDs.

| Physical control | MIDI output | Intended assignment |
| --- | --- | --- |
| Switches 1–8 | CC20–CC27 respectively; values depend on Control Mode below | One effect on/off per switch |
| Expression pedal | CC11, values 0–127 | Wah sweep |

Choose the input behavior from the app's **Control Mode** menu:

- **MainStage (toggle buttons)** is the default. Each physical press sends value **127**; releasing sends no MIDI. It also sends no neutral reset before the next press. Configure MainStage's button as **Single Value** so the host can toggle its actual parameter on each trigger. This behavior has been physically verified for one Cry Wah power mapping: each press toggles once and the state remains after release.
- **Guitar Rig standalone / other hosts** sends momentary values: **127 on press, 0 on release**. Use this for the standalone Macro-button instructions below or another host that expects press/release pairs.

Changing modes clears cached host LED states and establishes a new USB switch baseline without sending effect commands. Release a switch held during the change before using it again. Both modes use the same authoritative host-feedback LED path; the mode changes input messages, not the meaning of an LED.

Use one of the eight switches for wah on/off. Toe-down switching is not part of this version.

The current menu-bar app changes switch LEDs **only from host feedback**: channel 16, CC20–CC27, value 127 for on and 0 for off. Physical presses never light or locally toggle an LED. Other channels, controllers, and intermediate values do not establish LED state. Controls without a received host state remain dark and appear as **unknown** in Setup & Test; darkness alone is not confirmation that an effect is off. The legacy CLI's LED behavior is separate from the menu-bar app.

At each connection, the app queries the current switch state and establishes a baseline without sending switch commands. Wait for **Connected — MIDI ready** before playing; no manual arming press is needed. If a switch is already held at connection time, release it before its next press can control an effect. Recovery does not replay previous toggles.

In **Guitar Rig standalone / other hosts** mode only, if a release was lost during disconnection, the next deliberate press sends value 0 before value 127 on that switch's CC to restore the host's press detection. This happens only on a new press; reconnecting alone sends no switch commands. The live test view labels value 0 as release/reset, since these recovery zeroes are not additional physical releases. **MainStage (toggle buttons)** mode sends neither release values nor these neutral reset messages.

## Calibrate the expression pedal

Open **Setup & Test**, click **Calibrate Pedal**, sweep fully heel-to-toe and back through the travel you normally use, then click **Finish Calibration**. Sweep again and confirm the observed MIDI range reaches **0…127**. The app saves the measured raw endpoints for future launches and allows a small margin at each end so sensor noise does not prevent full effect travel. Calibrating updates expression scaling without reconnecting USB or changing switch states; a disconnected session cancels an unfinished calibration.

## Experimental MainStage setup for effect-state LEDs

Start the controller app before MainStage so both MIDI directions exist when the concert opens. Select **Control Mode → MainStage (toggle buttons)** in the controller app. Select the separate audio interface in MainStage, add **Guitar Rig 6 FX** to an audio channel, and use a test concert with a known rack.

1. In **Layout**, add a Button screen control. Set its MIDI Port to **RigKontrol3 Bridge**, Channel to **16**, and Number to **20** for physical switch 1. Set Type to **Single Value** and MIDI Thru to **Do not pass through**. The app supplies press-only triggers in this mode; verify one retained toggle per complete physical press/release cycle before extending the layout. Selecting Single Value alone did not fix held-only behavior with the previous momentary input in this concert.
2. Set **Send Value to → RigKontrol3 Bridge**. Commit the destination selection with **Return**; pressing **Escape** cancelled the selection during the live setup. Recheck the displayed destination afterward.
3. In **Edit**, map the screen button to the actual Guitar Rig component's on/off AU parameter. Keep **Show Input Value off** so the screen control represents the parameter rather than the last pedal input. A Macro that can diverge from its target after direct mouse edits is not sufficient evidence of the target's state.
4. In the current test concert, the button is mapped to **Audio 3 → Guitar Rig 6 FX → P017**, the component power parameter with Guitar Rig automation ID 17. These identifiers depend on the rack; inspect the actual parameter rather than assuming P017 is always power in other presets. Do not confuse a bypass value with an enabled value: confirm the direction against the effect's power control.
5. Map the remaining switches to their own screen buttons and actual effect parameters using CC21–CC27. For the expression screen control, select **RigKontrol3 Bridge**, channel **16**, **CC11**, Type **Absolute**, and MIDI Thru **Do not pass through**. Map it to the actual wah position parameter. In the current test rack, **P018** is mapped and the user has confirmed that a full physical pedal sweep controls the wah through MainStage.
6. Save the concert, then verify the mappings and feedback again after reopening it.

The live setup has established one working Cry Wah power control and expression sweep. Committing the initial P017 mapping reported CC20 value 0, and changing AU P017 to 100% in Generic Controls produced CC20 value 127 at the app's feedback destination. With **MainStage (toggle buttons)** selected, the user verified that each physical press toggles the actual effect once, its state remains after release, and the physical LED matches that state. Clicking the actual Guitar Rig Cry Wah power control with the mouse also updates the LED and retains the correct on/off indication. The user has also confirmed the full physical expression sweep through P018 while the switch remains functional. This verifies press-only input, actual effect-to-LED feedback for that one mapping, and the wah sweep. The remaining switch mappings, complete initial state, patch/concert synchronization, and recovery are not yet verified.

The local test concert is saved at `.build/RK3 MainStage Feedback Test.concert`. It records this experimental setup; it is not a final concert prepared for distribution.

Apple documents [screen-control parameter mapping](https://support.apple.com/guide/mainstage/map-to-channel-strip-and-plug-in-parameters-mstgf1477b4c/mac), [Send Value to](https://support.apple.com/guide/mainstage/common-screen-control-parameters-mstge3da7da4/mac), and the distinction between [mapped parameter values and Show Input Value](https://support.apple.com/guide/mainstage/set-screen-controls-show-hardware-mstg02e21cb5/mac).

### Host-state lifecycle limits

The app does not automatically identify the active concert, patch, or mapping revision. Before changing patches, closing or replacing a concert, or changing mappings, use **Clear Host State**. This clears cached knowledge and turns unknown LEDs off; it does not request a new snapshot from MainStage. Require fresh feedback for the controls in the new context before trusting their LEDs. A previously known state must not be treated as evidence for an unmapped control in another patch.

The app invalidates cached host state when MainStage quits. Closing a concert while MainStage stays open does not trigger that application-termination handling. Starting the app after a concert is already open, receiving an initial complete snapshot, restoring all states after a patch change, and refreshing after app restart remain unproven. Stop/start and USB recovery retain received host states, so they also depend on the host context still being valid and its feedback continuing to update. These limits are part of the remaining MainStage verification, not a completed synchronization guarantee.

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

The following include remaining acceptance checks; the one verified Cry Wah control does not establish the full setup:

- Press every switch repeatedly and rapidly; each physical press toggles only its assigned effect, once. Releases and simultaneous switches behave correctly.
- Confirm each LED matches its actual effect state after a footswitch press and remains correct after release. Change the actual effect with the mouse and require the LED to follow without a pedal press. Unknown controls must remain labelled unknown, rather than being inferred from presses.
- Sweep heel to toe and back; confirm the wah reaches both endpoints, moves smoothly, and stays stable while held.
- Save and reload the MainStage concert and hosted Guitar Rig rack, then restart the controller app and host and require fresh effect-state feedback. Verify the standalone Macro assignments separately if using that basic-control option.
- Clear host state before changing patches or closing a concert. Test patches with different effect states and missing mappings, concert replacement, and MainStage quit/relaunch. Establish which actions actually produce a complete fresh snapshot.
- Unplug and reconnect USB while idle and while holding a switch. Confirm status changes and recovery without an unintended effect toggle; release a held switch before pressing again.
- Stop and start the bridge, then sleep and wake the Mac. Confirm that there is one controller source, its paired feedback destination, and one responding bridge, with no stuck LEDs or unwanted toggles. Recheck feedback freshness as well as USB connectivity.
- Play for a sustained session, listening for missed, doubled, or delayed controls.

Other VST hosts can receive the same CoreMIDI controls when their MIDI routing and learn modes are configured. Accurate LEDs additionally require real parameter-state feedback and lifecycle verification for that host; a MIDI echo is insufficient. Combined RK3 audio/control, preset banks, toe-switch support, and a public installer remain outside this milestone.

## Developer checks

Run `swift test` for parsing, controller state, session lifecycle, expression endpoints, feedback decoding/state, and USB input backpressure tests. Build and sign the local app with `Scripts/build-rk3controller.sh`. The source and destination use distinct stable IDs, `0x524B3301` and `0x524B3302`, despite their shared display name.

With the RK3 attached, its audio idle, and the controller app quit, run:

```bash
Scripts/check-rk3controller.sh
```

This opens a three-second test session and checks USB/CoreMIDI readiness, absence of idle switch messages, and clean shutdown. It keeps the event log in `.build/`. Physical controls, host toggle behavior, and unplug/sleep recovery use the joint playing checks above.

The transport keeps EP1 input reads running during startup commands and LED writes. Real-device testing exposed OUT timeouts when input was left unread, including between consecutive initialization writes. A single initial read was insufficient. Keep the continuous reader and its join-before-release ownership when changing the transport.
