# RK3 external footswitch compatibility: Orange FS-1 Mini

Date: 2026-10-28. Scope: manufacturer documentation for the user's **Orange FS-1 Mini**, RK3 Pedal 1/2, and the remaining physical validation. No device connection, setting change, electrical measurement, or driver change was performed for this note.

## Verified manufacturer facts

| Item | Documented fact | Source |
| --- | --- | --- |
| Exact Orange model | The FS-1 **Mini** uses a latching on/off mechanism. This finding is for the Mini, not an assumed equivalence with another FS-1 variant. | [Orange FS-1 Mini product page](https://orangeamps.com/products/fs-1-mini-footswitch), product description and latching section. |
| Power | Orange describes the Mini as passive, requiring neither a battery nor an external power supply. | [Orange](https://orangeamps.com/products/fs-1-mini-footswitch), passive operation and detailed description. |
| Cable | Orange specifies a mono quarter-inch jack cable; it is not supplied with the switch. Calling this a TS cable is the conventional interpretation of that connector description, rather than an internal wiring diagram supplied by Orange. | [Orange](https://orangeamps.com/products/fs-1-mini-footswitch), detailed description. |
| Orange LED | The Mini has a status LED. Orange says it obtains power through the amplifier connection when that connection supports it. This does not establish LED operation from an RK3. | [Orange](https://orangeamps.com/products/fs-1-mini-footswitch), LED and power sections. |
| RK3 external connectors | Pedal 1 and Pedal 2 accept external pedals, footswitches, or switchboxes. NI specifies stereo quarter-inch phone plugs for these sockets, separately from the five-pin DIN MIDI ports. | NI-authored [GUITAR RIG KONTROL manual](https://manuals.plus/m/ba6cc1149f251ba75a837c8ff793e5d95a21972cc4b4226f2730f86394a296d0.pdf#page=48), §9.1, printed p48. |
| RK3 switch configuration | NI describes matching each input mode to an opening/closing switch or tip/ring-active pedal, using the connected device's specification. | [NI manual](https://manuals.plus/m/ba6cc1149f251ba75a837c8ff793e5d95a21972cc4b4226f2730f86394a296d0.pdf#page=41), §7.3, printed pp41–42. |
| Original controller settings | NI's Controller Editor documentation explicitly offers pedal/footswitch and active-tip/active-ring choices for RK3 external inputs, and requires calibration for the expression/external pedals. | Original [Controller Editor manual](</Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/agent-feasibility/frontend-reference/controller174-18f35bcf/expanded/Controller Editor Documentation Update.pkg/Payload/Documentation/Controller Editor Manual English.pdf>), version1.7.1 (10/2014), printed p247. |

## Compatibility conclusion and limits

The Mini is a plausible **latching switch** test device for RK3's documented footswitch inputs. It is not an expression pedal and cannot validate an external continuous expression sweep. The manufacturer documents do not explicitly certify this particular pair.

The sources examined do **not** disclose the Mini's contact schematic, LED polarity/circuit, voltage/current requirements, or RK3's matching electrical limits. Do not infer a plain tip-to-sleeve short or treat the Mini's LED as evidence of RK3 MIDI feedback. An unlit Mini LED alone would not establish failed switch detection.

## Connection admission

NI's exact specification is: “(Pedal 1, Pedal 2) for stereo 6.3mm (1/4 inch) phone plugs” (§9.1, printed p48). The nearby §7.3 instructions say to match the connected manufacturer's switch/pedal characteristics; they do not supply a mono-switch exception. No explicit prohibition using the word mono, or separate electrical warning about such a plug, appears on the examined pp41–42/48. That absence is **not** affirmative compatibility evidence. The local Controller Editor manual p247 documents modes/calibration but does not specify a jack or cable type.

**Documentation alone does not admit either a mono lead or a substituted TRS lead for this particular Mini/RK3 pairing.** A TRS lead follows the RK3 connector specification but does not establish how the Mini's mono socket, switch and LED treat the ring conductor. Tip mode is only a possible software selection after the electrical connection contract is established. Compatibility remains **unverified**; this note does not authorize connecting the Mini or propose an adapter.

Once connection compatibility is established, a bounded check could verify two stable switch states and raw/MIDI values using the existing tools. That belongs to the existing hardware-validation bead and does not change the driver plan.

## Source provenance

- Orange's source is its current official page for the exact Mini model. No retailer or forum claims were used.
- The Controller Editor manual is the existing documentation payload from the NI installer already extracted in this worktree. It names Native Instruments GmbH as author. SHA-256: `87fd40636b66d5d9a4db2095951e850fdd4c3737dc8434765949a64ebc35af9b`.
- The RK3 manual is an NI-authored 07/2011 document for controller version3; its front matter identifies the author. The cited online copy is hosted by Manuals.plus, **not delivered from NI's domain**. Candidate NI-hosted legacy PDF URLs were inaccessible through the web tool. Only the manufacturer's document content, not mirror commentary, supports the socket/mode claims above.
