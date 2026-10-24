# rk3-coreaudio native USB transport

The controller's native IOKit USB backend adapts `usb_iokit.c` and
`usb_iokit.h` from [mihaimetal/rk3-coreaudio](https://github.com/mihaimetal/rk3-coreaudio),
at upstream commit
[`d0679238ad06842f382ef89b995823e978caec9b`](https://github.com/mihaimetal/rk3-coreaudio/tree/d0679238ad06842f382ef89b995823e978caec9b).
The upstream project is licensed under **GPL-2.0**; its license text is preserved
in [LICENSE](LICENSE). Upstream documents protocol provenance from the Linux
`snd-usb-caiaq` driver.

The adapted files live in `Sources/CRK3CoreAudioUSB/`. Local changes made on
2026-10-24 support the controller-only session:

- Add a controller open path for interface 0 and alternate setting 0, resolving
  its bulk pipes without requiring audio endpoints; preserve the upstream
  alternate-setting-1 audio open path.
- Keep an already active configuration instead of resetting it on every open.
- Preserve native error statuses and read lengths, reject invalid transfer
  sizes/timeouts, and expose startup diagnostics to the Swift session.
- Close only device/interface resources successfully opened here. The session
  joins its independent input reader before closing the shared interface.

The existing Swift session continues to own control mapping, calibration,
input draining, reconnection and host-feedback LEDs. This adaptation does not
reuse the upstream audio engine, sample codec, HAL plug-in or MIDI helper.

Because it incorporates this upstream source, the controller application
distributed with this native backend is supplied under **GPL-2.0**. This notice
does not relicense pre-existing, unrelated experimental audio work elsewhere
in the repository.

The app bundle includes a `NativeUSBSource` snapshot of the adapted C source,
public header, Swift adapter, shared transport types and input pump. This is
only a backend source subset, not the full corresponding source of the app.
The full corresponding controller-app source and build instructions are
maintained at [zukomgwili/rigkontrol3, branch
`codex/rk3-effects-controller`](https://github.com/zukomgwili/rigkontrol3/tree/codex/rk3-effects-controller).

Rig Kontrol and Native Instruments identify the compatible hardware. This
community project is independent of Native Instruments.
