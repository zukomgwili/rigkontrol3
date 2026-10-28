# NI pedal configuration and calibration contract

Scope: `rigkontrol3-40h.2`, 2026-10-28. This is the remaining native
audio/MIDI functionality check, not a return to Controller Editor or NI IPC.
The baseline is installed MIDI 0.2.10/build 15 and audio 0.2.8/build 13.
This is the preimplementation snapshot; the implementation disposition below
records later work separately. All read bounds were recorded in the existing bead before original reads.
No live settings, services, drivers, USB access or security settings were changed.

## Authority and bounds

Controller policy comes from NIHardwareAgent 1.7.4/R199, SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
Hardware selector behavior comes from the previously pinned 2.8.0 x86_64
NIUSBAudioDriver; see [AudioDriver.md](AudioDriver.md) for its artifact identity.
The exact existing assembly/decompiler exports are pinned in
[TipSwitchCalibrationContract.json](Evidence/TipSwitchCalibrationContract.json).
Decompiled types are not declarations of NI's original source.

The first tranche admitted at most 474 assembly lines and 266 decompiled C
lines. The second admitted 487 and 379 respectively, explicitly expanding
the second C allowance before reading. `calculate` was read as assembly only.
No new decompilation or broad binary search was performed. This local proof
does not establish the registry parser, on-disk domain or direct vtable entry.
Independent review verified all 33 reference hashes and the contract, including
the stored continuous-value clarification below.

## Tip, ring and switch configuration

`FX2Pedal::setSampleTip` at `0x100020650` stores byte `+7`;
`setAsSwitch` at `0x100020660` stores byte `+0xc`. These are separate
options from the physical toe-switch input, currently mapped to CC64.
`getPedalConfiguration` at `0x100038b20` produces a ring mask (bit i set
when sampleTip is false) and a switch-mode mask (bit i set when asSwitch
is true). Modes do not select alternate pairs in the host: `processPedals`
at `0x100039a20` consumes one two-byte value per pedal and never reads
the sampleTip flag.

`getDeviceConfiguration` at `0x100038a90` fills a zero-initialized 32-byte
configuration: byte 0 is `02`, byte 1 the ring mask and byte 2 the switch
mask. `configureDevice` at `0x100038bb0` requires the connected flag,
fills this complete configuration through its virtual getter and writes
only when that succeeds. It ignores the write result. `writeConfiguration`
at `0x1002f1e00` calls selector 10 with scalar input `[32]`, a 32-byte
structure and no outputs, returning whether IOKit returned zero.

Kernel `NIUSBUserClient::setThresholds` at `0x9700` holds the existing
optional lock, returns `0xe00002d9` without a provider or `0xe0000001`
without the input pointer, otherwise forwards the scalar's low 32-bit length
and returns zero. Device `setThresholds` at `0x2930` forwards the payload
to `sendCommand` with command `0x0f`. Thus the three-pedal configuration
wire operation is `[0f,02,ringMask,switchMask,00...00]`, 33 bytes total.
The wrapper's zero result does not prove USB completion succeeded.

`onPedalMessage` at `0x10003a0c0` switches on the low 24 opcode bits:
`706774` get tip, `706753` get switch, `707374` set tip, `707353` set
switch, `707200` recalibrate, `705800` stop calibration. A valid unsigned
axis selects one pedal; an invalid index affects all pedals for setters,
recalibrate and stop. Invalid-index getters return Tip=true/Switch=false.
Only scalar `0x74727565` means true to the message setters. Recognized
operations return true, unknown operations false. Setters do not reset
calibration. Connected mode changes invoke virtual `+0x1e8`; the existing
[ownership proof](NIEventOwnershipContract.md#original-activation-and-configuration)
identifies the configure/write chain, but this tranche did not newly read
the actual vtable entry.

## Conversion and calibration lifecycle

`calculate` at `0x100020680` takes one raw integer, clamps it to its
configured maximum and ignores a repeated raw input. The first changed
sample after initialization establishes state and emits no event. The common
minimum span is 0.05. Continuous mode additionally requires span >=0.1;
its existing native endpoint margin, snap and 0.005 deadband agree with NI.

The missing switch-mode branch at `0x10002088f..0x100020968` uses
threshold `0.5*(high-low)`, without adding low. It learns inversion on the
first switch evaluation, then emits normalized 0/1 on the first evaluation
or a changed switch result. It is not the normal physical toe-switch path.

`recalibrate` at `0x100020a60` seeds both endpoints from the previous
stored continuous/baseline value at `+0x18`, clears initialization and switch polarity/state,
enables explicit collection and retains the raw cache. Switch events do not update
that stored double, so it need not equal the last emitted switch value. The first changed
sample establishes a silent baseline without expanding existing endpoints;
later changed samples expand them. Explicit collection suppresses output.
`stopCalibration` at `0x100020a90` only clears collection. It does not
validate, roll back endpoints or reset the baseline/raw cache.
`setLowHighPosition` at `0x100020a50` simply stores two doubles.

The native portable calibration API currently seeds from last raw input,
expands the first collection sample, validates and rolls back an insufficient
range, and resets its baseline on stop/set. Those are extra host semantics,
not NI's recovered lifecycle. The existing calibration utility independently
measures raw data and saves only a usable range; that utility safeguard must
not be presented as NI's underlying setter or recalibration behavior.

## Persistence

`readPedalValues` at `0x100039d60` reads Tip, Switch, Low, High, then assigns
the endpoints. Option reads occur only if its third argument is true;
registry defaults are Tip=1, Switch=0. Successful integer results normalize
nonzero to true; failed reads retain prior flags. Endpoints pass current
double values as Float32 defaults, and successful Float32 results widen to
double. Failed reads retain the corresponding previous value.

`savePedalValues` at `0x100039f60` saves Low, High, Tip, Switch, the last
two conditionally. Keys are `<controller name> Pedal <index> Low/High/Tip/Switch`.
Endpoints are stored as Float32; results are ignored. There is no transaction
or rollback in these local functions. This does not prove what malformed
values the registry parser accepts or how it persists them on disk.

## Native gap disposition

The current USB owner sends the all-tip/continuous default configuration.
The controller mapper has no switch-mode branch or sampleTip/asSwitch state.
Its runtime calibration property and utility expose only built-in axis 2;
the portable API has all three axes, with the lifecycle differences above.
Actual nondefault hardware mode selection, switch-mode conversion and their
native configuration interface are genuine gaps. Toe/external/DIN physical
qualification remains a separate M3 gate, not proof of missing code.

Any implementation must retain one USB owner, current mappings, saved built-in
calibration, input baselines, incoming LED feedback, NI timing/recovery and
SIP. The minimum native binding must send only the recovered fixed configuration
and expose the three existing pedal objects. No editor, agent, private NI IPC,
new polling cadence, audio algorithm or guessed pedal polarity is justified.


## Implementation disposition

The reviewed source restores the recovered pedal modes and calibration lifecycle.
A native CoreMIDI settings snapshot and applied-status property expose three axes;
the original built-in calibration is imported once. The supplied safe raw sweep
utility now supports all three axes. It remains a host utility, rather than an
emulation of NI's private recalibration messages or preference registry.

Configuration uses the existing output worker and leased HAL command queue.
Only the fixed recovered 33-byte configuration is additionally admitted by the
HAL and USB owner. Mapper changes wait for the matching current-lifecycle
completion, provider identity and a post-write analog history boundary. Digital history, DIN MIDI,
timing, audio rings and recovery policy retain their existing paths. The source
acceptance and physical acceptance are recorded separately in Beads; no physical
nondefault-mode result follows from this offline proof. Applied status confirms
the native USB write and mapper update; the recovered path has no device-side
settings readback reply, so physical mode selection still needs verification.
