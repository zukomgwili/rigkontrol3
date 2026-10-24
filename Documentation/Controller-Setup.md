# Rig Kontrol 3 effects controller

This personal-use Apple Silicon app connects the Rig Kontrol 3 switches and expression pedal to a standard CoreMIDI source named **RigKontrol3 Bridge**. The initial target is Guitar Rig 6 standalone, with another audio interface handling the guitar and headphones or speakers.

## Build and open

From the repository, run:

```bash
Scripts/build-rk3controller.sh
open ".build/Rig Kontrol 3.app"
```

The script builds a native arm64 app, bundles libusb, and signs it locally. It creates `.build/Rig Kontrol 3.app`; it does not install or launch anything. Keep the app in a stable location before enabling launch at login, and rebuild it at that same location when updating.

This is a build for the current Mac. Its bundled Homebrew libusb may require a newer macOS version than the Swift package's declared minimum; compatibility with older Macs is not established by a successful local build.

Use the menu-bar item to check the connection, start or stop the bridge, quit, and optionally enable launch at login. **Setup & Test…** opens the assignment instructions and live switch counts and pedal range. Start manually until the playing setup has been verified. A disconnected device should reconnect automatically when plugged in; waking the Mac should restore the connection when the bridge is enabled.

Run only one RK3 controller bridge at a time. Quit the legacy `rk3bridge` CLI before opening the app. The installed experimental RK3 audio driver claims the same USB interface when its audio starts: select the separate interface in Guitar Rig and ensure no other application or system audio is streaming through the RK3. An installed but idle RK3 audio driver does not need removal.

## MIDI controls

All controls use **MIDI channel 16**.

| Physical control | MIDI output | Intended assignment |
| --- | --- | --- |
| Switches 1–8 | CC20–CC27 respectively; 127 on press, 0 on release | One effect on/off per switch |
| Expression pedal | CC11, values 0–127 | Wah sweep |

Use one of the eight switches for wah on/off. Toe-down switching is not part of this version. Switch LEDs show physical presses, not Guitar Rig effect states: an LED stays on while its accepted switch press is held and turns off on release.

At each connection, the app queries the current switch state and establishes a baseline without sending switch commands. Wait for **Connected — MIDI ready** before playing; no manual arming press is needed. If a switch is already held at connection time, release it before its next press can control an effect. Recovery does not replay previous toggles.

If a release was lost during disconnection, the next deliberate press sends a neutral CC0 before CC127 to restore the host's press detection. This happens only on a new press; reconnecting alone sends no switch commands. The live test view labels CC0 as release/reset, since these recovery zeroes are not additional physical releases.

## Calibrate the expression pedal

Open **Setup & Test**, click **Calibrate Pedal**, sweep fully heel-to-toe and back through the travel you normally use, then click **Finish Calibration**. Sweep again and confirm the observed MIDI range reaches **0…127**. The app saves the measured raw endpoints for future launches. Calibrating updates expression scaling without reconnecting USB or changing switch states; a disconnected session cancels an unfinished calibration.

## Guitar Rig 6 standalone

1. In Guitar Rig's audio preferences, select the separate audio interface. Confirm that the guitar can be heard through Guitar Rig before assigning pedals.
2. In **Preferences → MIDI**, enable **RigKontrol3 Bridge** under MIDI inputs and choose channel **16**, or **Omni** while testing.
3. Load a simple rack containing a wah and the effects you want to control.
4. Right-click an effect's on/off control and choose **Learn MIDI Control**, then press and release the intended footswitch. Test several complete press/release cycles. The required outcome is one persistent effect toggle per physical press, without a second toggle on release.
5. If Guitar Rig requires two presses for a toggle, Native Instruments directs users to right-click that button and select **Enable Hold Mode**. Test the result in the installed version; do not assume that learning alone establishes correct toggle behavior.
6. Use **Learn MIDI Control** on the wah's continuous position parameter, then sweep the expression pedal slowly through its full travel. The software control should reach both ends and remain stable when the pedal is held.
7. Assign the remaining switches, then save the Guitar Rig preset and any applicable controller defaults. Reload it and restart Guitar Rig to verify that assignments are retained. Do not assume a component assignment automatically applies to every preset.

The bridge emits momentary CC values and leaves effect toggling to the host. The exact Guitar Rig mode and assignment persistence must be verified with the installed Guitar Rig 6 build. Preset changes and mouse clicks do not update physical LEDs; press-only LEDs remain valid regardless of the rack's effect states.

Native Instruments documents [MIDI Learn for on/off controls and its Hold Mode troubleshooting](https://support.native-instruments.com/support/solutions/articles/69000879964-native-instruments-using-midi-learn-to-control-guitar-rig-in-pro-tools). That article includes DAW routing steps; those routing steps are not needed for this standalone setup. The [Guitar Rig 6 manual](https://www.native-instruments.com/fileadmin/ni_media/downloads/manuals/gr6/GUITAR_RIG_6_Manual_English_22_04_2021.pdf) is the version-specific reference. The [online automation and MIDI guide](https://docs.native-instruments.com/ni-tech-manuals/guitar-rig-manual/en/automation-and-midi-control) describes assignment scopes, but is updated for newer Guitar Rig releases.

## Joint playing verification

The following are acceptance checks, not claims of completed hardware testing:

- Press every switch repeatedly and rapidly; each physical press toggles only its assigned effect, once. Releases and simultaneous switches behave correctly.
- Hold each switch and check that its corresponding physical LED lights only during the press.
- Sweep heel to toe and back; confirm the wah reaches both endpoints, moves smoothly, and stays stable while held.
- Save and reload the Guitar Rig assignments, then restart the controller app and Guitar Rig and recheck them.
- Unplug and reconnect USB while idle and while holding a switch. Confirm status changes and recovery without an unintended effect toggle; release a held switch before pressing again.
- Stop and start the bridge, then sleep and wake the Mac. Confirm that there is one MIDI source and one responding bridge, with no stuck LEDs or unwanted toggles.
- Play for a sustained session, listening for missed, doubled, or delayed controls.

Other VST hosts can receive the same CoreMIDI controls when their MIDI routing and learn modes are configured. They require separate host-specific verification. Combined RK3 audio/control, preset banks, toe-switch support, and a public installer remain outside this milestone.

## Developer checks

Run `swift test` for parsing, controller state, session lifecycle, expression endpoints, and USB input backpressure tests. Build and sign the local app with `Scripts/build-rk3controller.sh`.

With the RK3 attached, its audio idle, and the controller app quit, run:

```bash
Scripts/check-rk3controller.sh
```

This opens a three-second test session and checks USB/CoreMIDI readiness, absence of idle switch messages, and clean shutdown. It keeps the event log in `.build/`. Physical controls, host toggle behavior, and unplug/sleep recovery use the joint playing checks above.

The transport keeps EP1 input reads running during startup commands and LED writes. Real-device testing exposed OUT timeouts when input was left unread, including between consecutive initialization writes. A single initial read was insufficient. Keep the continuous reader and its join-before-release ownership when changing the transport.
