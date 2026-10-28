# NI output-count identity and modern binding decision

NI's direct DWORD read at stream `+0xa8` is identified as `IOAudioStream::numClients` by the source-correlated x86-64 layout proof below. The automatic modern binding has a concrete limitation: the selected published AudioServerPlugIn interface supplies no persistent per-output client-buffer attachment notification. The field-identification gap is resolved; automatic attachment-count parity remains unsatisfied. [Evidence and compile results](Evidence/NIOutputBindingResolution.json).

For the selected one-device AudioServerPlugIn interface, **do not manufacture a stream-attachment counter from device-client Start/Stop, operation callbacks, PID, silence or elapsed time**. The inspected published contract provides positive device, stream-configuration and I/O-operation events, but no persistent per-stream client-buffer attach/detach notification. This is a bounded interface result, not a claim that every macOS mechanism is incapable of exposing ownership.

The minimal existing bindings have precise scopes: the actual output-stream `IsActive` setter is an explicit stream-disable event and already reaches the native output-active flag; the final device-client Stop stops aggregate device audio I/O. Neither becomes the original per-output attachment-zero condition merely by renaming it. Automatic independent output-attachment parity is unavailable through the inspected published interface alone. Preserve the requirement and existing NI ring/clear/timing/recovery behavior; this finding authorizes no replacement trigger or functional change.

## NI field identity: source-correlated x86-64 proof

The selected historical header is Apple's IOAudioFamily **195.4.2**, immutable commit `8cf084d3ccac29eeace2e5f3ec268c1cb9afa1d9` (Apple import 2014-03-11), close to NI's recorded SDK 10.9/Xcode 5.1.1 build. Its stream instance fields match 203.3 in order and type. Changes inside pointed-to expansion data do not alter the stream object. The source chain includes the actual XNU 2422.115.4 base-class fields, including IOService's trailing interrupt fields; no opaque base-size padding was assumed. [Historical stream fields](https://github.com/apple-oss-distributions/IOAudioFamily/blob/8cf084d3ccac29eeace2e5f3ec268c1cb9afa1d9/IOAudioStream.h#L63-L140), [IOService fields](https://github.com/apple-oss-distributions/xnu/blob/xnu-2422.115.4/iokit/IOKit/IOService.h#L287-L316), [trailing fields](https://github.com/apple-oss-distributions/xnu/blob/xnu-2422.115.4/iokit/IOKit/IOService.h#L1218-L1221).

| Layout fact | Derived and compiler-checked value |
| --- | --- |
| OSMetaClassBase / OSObject / IORegistryEntry sizes | `0x8` / `0x10` / `0x28` |
| Complete IOService size | `0x88` |
| Stream's audioEngine / workLoop / commandGate / streamIOLock | `0x88` / `0x90` / `0x98` / `0xa0` |
| Named UInt32 numClients | offset `0xa8`, width 4 |
| Final expansion pointer / complete IOAudioStream size | `0x148` / `0x150` |

The proof uses the actual primitive aliases, 24-byte stream format and 8-byte engine position. A 91-line private declaration model preserves the single inherited primary vptr and every instance data field, omitting method bodies and static/pointed-to internals. Apple Clang 21 compiled it explicitly for **x86_64-apple-darwin13**, with `-fapple-kext`, strict assertions and record-layout output. The positive case exits 0 with empty stderr. Changing the counter to UInt64 fails its width/full-size assertions; inserting eight bytes into the base fails the offset/full-size assertions. Root independently reviewed the declarations and repeated all three compile-only cases. No object was linked or executed.

This supplies two independent matches to NI's compiled operands: the DWORD load at `0x7f78` reads the named 32-bit count at `0xa8`, and the constructor allocation at `0x4d77` is the complete `0x150` stream size. The object provenance and clear-before-latch-reset sequence were already instruction-checked in [the original-trigger audit](NIOutputClearTrigger.md). Its field-name gap is superseded by this proof. The recovered predicate is therefore the previously-written latch plus **numClients == 0**, at the original eligible completion, followed by the original clear and latch reset.

This is source-correlated identification of the NI-compiled member under the recovered legacy LP64/kernel C++ layout. It does not claim byte-identical NI SDK headers, execution of NI's old compiler, or discovery of the exact loaded historical framework image. None of those stronger historical claims is used to infer a modern lifecycle event. The proof does not copy an Intel memory offset into the ARM driver. The counter's attached-buffer semantics remain those described in [the Apple lifecycle analysis](NIOutputClearTrigger.md).

## Compatibility matrix

| Mechanism | Positive fact the contract supplies | Relationship to output attachment/clear |
| --- | --- | --- |
| Legacy Apple `IOAudioStream::addClient/removeClient` | An attached client-buffer node enters/leaves one stream's list; its counter changes. Kernel user-client Start/Stop and registration/freeing can all change attachment. | This is the relevant legacy **attachment** model. The compiled count-field identity is resolved above; exact runtime revision remains unclaimed. [Legacy source analysis](NIOutputClearTrigger.md). |
| AudioServerPlugIn `AddDeviceClient/RemoveDeviceClient` | A host client begins/ends use of a **device**; metadata supplies native client ID and process ID as different fields. | Neither signature identifies a stream or registers/unregisters a stream buffer. It cannot establish zero output attachments while another direction remains active. SDK `AudioServerPlugIn.h:179–202, 698–732`; [published interface](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface). |
| Native `StartIO/StopIO` | A native client requests device I/O; the device remains running while at least one client has started. | Positive aggregate device lifetime. No direction argument or attachment count is supplied. Current native code implements this device-client count. SDK `948–984`; [StartIO](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface/startio), [StopIO](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface/stopio). |
| Explicit stream `IsActive` configuration | One public stream reports whether it is enabled and doing I/O. The native setter receives an explicit value for that stream. | A real stream-control event: output disable already forwards to the native flag. The contract does not equate its value with client-buffer attachment count or require a transition on one public IOProc Stop. `AudioHardwareBase.h:1077–1086`; [IsActive](https://developer.apple.com/documentation/coreaudio/kaudiostreampropertyisactive). |
| `IOProcStreamUsage` / stream configuration | Usage describes one public IOProc's selected streams; stream configuration describes buffers/channels presented to the IOProc. | These public descriptions do not supply a documented native attach/detach callback. Usage still specifies NULL for unused callback buffers; that separate unresolved requirement is retained. `AudioHardware.h:849–867, 966–979`; [IOProc usage](https://developer.apple.com/documentation/coreaudio/kaudiodevicepropertyioprocstreamusage). |
| `WillDoIOOperation` | During startup, the host asks which processing operations the plug-in will implement; sets may differ by native client. | Capability negotiation, explicitly outside the I/O cycle. A query or returned capability bit is not evidence that an output buffer became attached/detached. SDK `1017–1051`. |
| `ProcessOutput`, `MixOutput`, `ProcessMix`, `WriteMix` | A `DoIOOperation` invocation identifies a stream, native client and processing operation. ProcessOutput processes canonical output; ProcessMix handles all clients' mix; WriteMix supplies the device-ring transfer. | Positive **operation/data-path participation** at that invocation, not persistent attachment lifetime. MixOutput additionally terminates later output operations for that cycle; enabling it as a supposed observer would change the required processing path. SDK `319–338, 1083–1123`; [ProcessOutput](https://developer.apple.com/documentation/coreaudio/audioserverpluginiooperation/kaudioserverpluginiooperationprocessoutput). |
| Thread/Cycle `BeginIOOperation/EndIOOperation` | Thread marks an I/O thread's beginning/end; Cycle marks a cycle's beginning/end. These IDs are never passed to DoIOOperation. Begin/End carry device, client and operation identifiers. | Positive thread/cycle boundaries. They have no stream-object argument or documented per-output attachment meaning. Ending one processing phase/cycle is not a persistent stream detach. SDK `301–306, 1053–1081, 1126–1156`; [Thread](https://developer.apple.com/documentation/coreaudio/audioserverpluginiooperation/kaudioserverpluginiooperationthread), [Begin](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface/beginiooperation), [End](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface/endiooperation). |

## What the existing implementation actually receives

The inspected native source has one device with separate input/output stream objects. Its supported device-property list omits `IOProcStreamUsage`; public usage readbacks therefore do not prove that this handler received those masks. Its stream-configuration getter describes one stereo buffer. The actual per-stream `IsActive` setter updates a global Boolean and forwards output changes to `rk3_usb_set_output_active`. These are configuration facts, not recovered client ownership. [Supported properties](../Driver/Audio/RK3AudioDriver.c#L330-L375), [stream getters](../Driver/Audio/RK3AudioDriver.c#L450-L458), [explicit setter](../Driver/Audio/RK3AudioDriver.c#L566-L574).

The Start/Stop implementation tracks native UInt32 device-client IDs; only the first start and final stop operate the USB audio engine. The ordinary Add/Remove handler maintains no per-stream attachment list, and its documented client metadata has no direction field. WillDo offers ReadInput and WriteMix; Begin/End are currently no-ops, and DoIO implements only those two transfers. No current per-client stream-attachment state exists to repair by choosing a different counter name. [Client handling](../Driver/Audio/RK3AudioDriver.c#L264-L282), [device lifetime](../Driver/Audio/RK3AudioDriver.c#L586-L604), [operation handling](../Driver/Audio/RK3AudioDriver.c#L658-L704).

Offering ProcessOutput would require implementing that processing contract, rather than merely subscribing to a lifecycle notification. Even then, receiving an operation for a client would establish participation at that invocation; its later absence supplies no documented detach event. There is consequently no justified ProcessOutput/MixOutput/capability-counter patch from this review.

## Interpretation of the observed WriteMix continuation

The prior [build 17 capture](Evidence/Build17HostClientLifecycleValidation.json) recorded continued native WriteMix invocations after the controlled public output registration stopped, while the input registration continued. That is positive evidence of a continuing host output-transfer path in that measured cohort. It is **not evidence that the legacy attachment-zero predicate had been reached**, and it does not prove how the historical host attached buffers.

This is compatible with the old mixed-lifetime model: a shared kernel engine user client could retain registered buffers while online. Compatibility is not an assertion that this was the historical host's exact grouping or that current WriteMix is the old attachment counter. Public output Stop must therefore cease to be treated as an automatic positive control for NI's original gate. A sampled global activity value, a capability query, PID grouping or callback silence cannot replace that missing fact.

## Finite decision and preserved requirements

- Keep aggregate Start/Stop bound to actual native device-client requests; do not interpret it as a per-output count.
- Keep explicit output disable bound to the existing real stream setter, with its explicit-control scope. This is not a newly proved original automatic count binding.
- Do not add a synthetic attachment count from processing callbacks, Thread/Cycle boundaries, public IOProc masks, PID, silence or a timeout. The inspected interface does not provide their required persistent detach event.
- Do not enable a different processing operation, alter the device graph, change backend or run another diagnostic merely to manufacture this event. No such change is admitted by this contract finding.
- Preserve the independent output-attachment/clear requirement. Meeting it automatically would require an additional positively defined host attachment lifecycle or another explicitly justified compatibility contract; it is not implemented by the available inspected callbacks. The existing NULL-buffer, physical clear/no-stale-replay and NI parity gates are not waived or closed.

This resolves the interface-selection question: there is no scope-correct automatic attachment-counter source in the selected published contract, and therefore no faithful counter patch to implement from it. Existing positive events remain usable only within the scopes shown above. It does not declare whole-macOS impossibility or authorize a new backend, ownership policy, kernel/security change, trace campaign or functional driver modification.

## Primary-source bounds and provenance

A finite AudioDriverKit cross-check also found no published ownership-count binding in the inspected declarations. `IOUserAudioStream::HandleChangeStreamIsActive(bool)` is a real overridable active-state callback, whose default calls `SetStreamIsActive`; its contract supplies no last-client-detach/count guarantee. Device Start/Stop propagates to **all** attached streams. `AddStream/RemoveStream` changes stream-object topology, and the operation handler/current-client time describe processing/timing. Those distinctions supply no affirmative reason to migrate backends for this missing event. [Apple device StopIO](https://developer.apple.com/documentation/audiodriverkit/iouseraudiodevice/stopio).

The alternative check stopped after two official queries and **277 IIG declaration lines**, with no extension, USB, build, installation or security work:

| AudioDriverKit primary file | SHA-256 | Inclusive lines |
| --- | --- | --- |
| `IOUserAudioStream.iig` | `c595d900c3cf72a903b062025fd5714026b8bdc08f240cfeb4a1c8dd1681b2c6` | 145–224; 335–365 |
| `IOUserAudioDevice.iig` | `4f65e1da833271a7abf94f7d52544cc4500ab7804ee7a3d3575658ff5788f37f` | 175–213; 516–589 |
| `IOUserAudioDriver.iig` | `542eb1d9108884cdb7bc406331a0834735c9c07322900438855f7c17d47eabe6` | 185–237 |

These files are under the installed Xcode DriverKit SDK's `System/DriverKit/System/Library/Frameworks/AudioDriverKit.framework/Headers/`. The result is limited to the published declarations inspected; it does not assert impossibility across all modern macOS mechanisms.

The SDK root is `/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/CoreAudio.framework/Headers/`. Root recorded these exact SDK bounds in existing `rigkontrol3-40h.3` before body reads:

| Primary file | SHA-256 | Inclusive lines |
| --- | --- | --- |
| `AudioServerPlugIn.h` | `de0ecbec543ac46b051ea731968ded0706401991ebec6d7976063e19651947e8` | 174–202; 295–355; 698–738; 948–984; 1017–1159 |
| `AudioHardware.h` | `a699437248e079d9ebe47078ef3861492d8253fef1a6007e476031031d3535ca` | 849–867; 960–981 |
| `AudioHardwareBase.h` | `cbac54e8edb7ee99bf10c18a2db044e01c47645985b2f2824623f3f8371a7c4e` | 1075–1099 |

Those SDK selections total **377 lines**. The native source pin is `64bb7db40ee234c1f4fcdc328b5dff19954ce3d1ad2be419bec01f27156fbfd1`; reviewed native ranges for this note were 264–282, 330–375, 450–458, 560–616 and 658–704, within the separately recorded envelopes. It includes guarded diagnostic hooks; the normal functional path is identified separately above.

Four targeted official-Apple searches and eight explicitly opened Apple declaration pages were used for the AudioServerPlugIn comparison. The opened pages returned JavaScript shells; precise operation/lifecycle statements therefore rely on the pinned current Apple SDK declarations, with official URLs supplied for discovery. The field proof inspected 697 selected header lines after bounds were recorded in M3, with input hashes and all compile commands/results retained in the evidence. No secondary source, new NI function body, installed-driver query, live test, driver build or functional driver change was used. Only the private compile-only layout proof was coded. This is the existing M3 contract finding, not a new issue or completion of the overall ARM lift-and-shift. NI timing/recovery, mappings/calibration/LED policy and SIP settings are unchanged.
