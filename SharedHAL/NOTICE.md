# Rig Kontrol 3 shared audio/controller HAL

This community driver adapts the native USB transport, caiaq protocol, mode-2
audio codec and audio engine, and the AudioServerPlugIn implementation from
[mihaimetal/rk3-coreaudio](https://github.com/mihaimetal/rk3-coreaudio), pinned to
[`d0679238ad06842f382ef89b995823e978caec9b`](https://github.com/mihaimetal/rk3-coreaudio/tree/d0679238ad06842f382ef89b995823e978caec9b).
The upstream copyright and **GPL-2.0** license are retained; see [LICENSE](LICENSE).
The upstream project credits the Linux `snd-usb-caiaq` driver for protocol
knowledge. The copied `rk3_midi.h` supplies protocol definitions; the upstream
MIDI helper, UDP bridge and standalone tools are not included in this driver.

Local adaptations provide one persistent owner for interface 0, alternate
setting 1, and a shared EP1 command/input path for audio and controls. They add
bounded control queues, boot/epoch identities, raw report history and command
completions exposed through CoreAudio properties. The audio lifecycle is
adapted to keep the control reader available when audio stops and to drain
callbacks before releasing owned USB resources. The plug-in fixes the first
audio milestone at 48 kHz stereo and retains this project's existing audio
device UID. The controller application retains its MIDI mapping, calibration
and host-authoritative LED behavior and communicates through these properties.
See [CONTROL-PROTOCOL.md](CONTROL-PROTOCOL.md) for the interface contract.

The shared driver and controller application incorporating GPL source are
distributed under **GPL-2.0**. This notice does not relicense unrelated,
pre-existing experimental audio work elsewhere in this repository. No legacy
Native Instruments driver binary or firmware image is included.

The driver bundle contains the exact adapted shared-HAL sources, public and
internal headers, property contract, license, notice and build/install scripts
under `Contents/Resources/SharedHALSource`. Its build provenance and file hashes
identify the snapshot even when built from an uncommitted checkout. Full
corresponding project source, including the controller application, is at
[zukomgwili/rigkontrol3, branch `codex/rk3-effects-controller`](https://github.com/zukomgwili/rigkontrol3/tree/codex/rk3-effects-controller).
The app also includes its own source snapshots; a backend subset alone is not
the full corresponding app source.

Rig Kontrol and Native Instruments identify compatible hardware. This project
is independent of Native Instruments.
