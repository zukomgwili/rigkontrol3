# Apple output lifecycle support request — draft only

This request has **not been sent**. The copyable request ends before the internal notes.

## Copyable request

**Subject:** AudioServerPlugIn: guaranteed last output-buffer detachment event while input continues

**Body:**

I am porting a legacy duplex USB audio driver to native Apple silicon using AudioServerPlugIn. I need to preserve an existing cleanup rule: clear previously written output when the last host client buffer attached to the output stream detaches, even if input I/O continues. A notification would update ownership state; the existing eligible hardware-completion path would still perform the clear. I am seeking clarification of the published lifecycle contract before changing the driver.

**Which documented event, if any, guarantees that the last output-stream client buffer has detached while input remains active?** In particular, is `kAudioStreamPropertyIsActive` associated with a host ownership transition, or solely stream configuration authored by the plug-in? Is another documented hook intended for this distinction? If none provides it, please clarify the intended output cleanup boundary when the host continues supplying a mixed-output transfer path. I am not assuming that stopping one public IOProc detaches the final output buffer.

We performed one bounded, counter-only comparison using two distinct `AudioDeviceIOProcID` registrations in one process. They used the same callback function with separate contexts. Registration A requested input enabled/output disabled; registration B requested input disabled/output enabled. The four `kAudioDevicePropertyIOProcStreamUsage` SET/readback values were respectively 1, 0, 0, 1. “Input” and “output” below identify these requested logical roles; they do not establish exclusive physical directions.

The device's nominal sample rate read back as 44,100 Hz. The public buffer size was set to, and verified as, 128 frames. The sequence was:

1. Create A and B, configure their usage, then start A and wait for progress.
2. Start B and wait for both registrations to progress.
3. Stop B and observe for approximately one second while A remains started.
4. Restart B, stop B again, then stop A and destroy both registrations.

During step 3, A's public callback progressed while B's public callback count remained at 1. The plug-in nevertheless received 362 additional `ReadInput` and 362 additional `WriteMix` operations. The plug-in's exposed output-stream `IsActive` value was 1 in all 18 samples; this was not independent ownership evidence. No plug-in `StartIO`/`StopIO` call or successful output-stream `IsActive` setter was recorded when B stopped or restarted. The plug-in `StartIO` occurred at the first A Start, and `StopIO` occurred after the final A Stop.

The plug-in observed one native device-client ID, correlated through its explicit client metadata to the collector process. This describes only the measured cohort; we do not assume that native client IDs map one-to-one to public IOProcIDs. Final counts were 370 native `ReadInput`, 370 native `WriteMix`, 370 public A callbacks and 4 public B callbacks. The collector's own API calls and checked cleanup succeeded.

The temporary instrumentation recorded lifecycle events and counters. Its public callbacks accessed no PCM, buffer contents or timestamps. Continued `WriteMix` is an observation of continued host output-transfer participation, not proof that zero output buffers were attached. We have not substituted callback silence, PID, elapsed time or aggregate device Start/Stop for an attachment count.

SIP remained enabled. The temporary measurement driver was removed and the preceding driver restored after this capture. Current environment verified on 28 October 2026: Mac14,2, Apple M2, macOS 27.0.1 (26A434), Xcode 27.1 (27A9275), macOS SDK 27.0. The saved capture does not independently record the capture-time OS/Xcode versions. Sanitized collector source, lifecycle logs and the trace schema can be supplied, with the instrumentation dependency explained.

## Internal notes — exclude from the submitted request

- This is a contract question and an existing-trace summary, not a claim of an Apple bug or a request to repair a third-party driver. Keep it to the one output-buffer lifecycle question. Do not add the separate unused-buffer NULL qualification failure.
- Existing tracking: `rigkontrol3-40h.3`. This draft creates no new issue and authorizes no code, installation, recapture, security change or external submission.
- The environment tuple above was verified **at draft time**, not independently by the saved capture; the read-only command results are retained in `.build/ni/apple-support-draft/environment.json`. Retain that distinction. If Apple requires capture-time attestation, obtain it explicitly rather than relabeling current metadata.
- The capture used temporary audio **0.2.12/build 17** with exact MIDI **0.2.11/build 16**, then restored audio/MIDI build 16. The validation records verified restoration of the installed files, signatures and calibration. No new runtime check was performed for this draft.

### Existing evidence and candidate attachments

These are internal repository locations, not text for an external form. Review and sanitize attachments before submission; do not include private paths, unrelated process metadata, authentication or certificate/account information.

| Item | Location / provenance | Submission consideration |
| --- | --- | --- |
| Capture and restoration validation | `ReverseEngineering/Evidence/Build17HostClientLifecycleValidation.json` | Primary summary of this single capture; not a standalone reproduction. |
| Contract analysis | `ReverseEngineering/NIOutputBindingResolution.md` and `ReverseEngineering/Evidence/NIOutputBindingResolution.json` | Internal reasoning and primary-source pins. Do not attach legacy binary/decompilation material. |
| Collector source | `.build/native/build17-audio-lifecycle/trace-client.c`; SHA-256 `3546bbe9e3a62a3ab86b0c943ca7b9083835152225d1aabf90c4740a926eec09` | Reviewed source lines 1–234. Requires the custom instrumentation described below. |
| Main stdout | `.build/native/build17-audio-lifecycle/live-1/main.stdout.log`; SHA-256 `576c2375c56ce620ad56d3eb5c696ab38ae3409a08dcb3bb3de58bedf1981d4b` | Reviewed lines 1–167. Sanitize before attachment; do not imply every historical journal event belonged to the collector. |
| Trace schema | `Driver/AudioLifecycleTrace.h`; SHA-256 `a941bc53a70b2fd279659ba5601e30e8a74da52567b5732674066de8b6228396` | Version 1/build 17; snapshot size 22,576 bytes. |
| Raw and decoded trace | Existing capture's binary snapshots and `decoded-trace.json`, as inventoried by its validation | These plus the validation's decoded counter matrix support the native +362/+362 deltas; main stdout alone does not. Retain existing hashes/provenance; prepare sanitized copies only if subsequently authorized. |

**Reproduction limitation:** the collector reads the custom `natr` trace property implemented by the temporary instrumented audio driver. The collector source alone cannot reproduce the native journal against an arbitrary AudioServerPlugIn. That measurement driver is no longer installed. Describe these materials as an existing trace and its collection method, not a ready-to-run standalone reproducer. No replacement measurement is proposed here.

The native ID/PID correlation and counter deltas support the reported cohort only. Public registration B stopping does not prove last-buffer detachment. Post-exit removal was observed by a later read; its precise trigger/time is not asserted. The journal contains unrelated historical activity; “successful calls” in the request refers to the collector's own checked API operations and cleanup.

No attachments have been assembled. The initial contract question needs the focused public call sequence, trace excerpt and collection method; proprietary NI material and unrelated system diagnostics are outside that scope. No support form, email or other external message has been opened or sent for it.

### Primary references for review

Precise contract statements were checked against the already pinned Apple SDK declarations below. The official web reference pages previously returned JavaScript shells; they are discovery links, not a claim of newly fetched page text.

- [AudioServerPlugInDriverInterface](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface), [StartIO](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface/startio), [StopIO](https://developer.apple.com/documentation/coreaudio/audioserverplugindriverinterface/stopio).
- [kAudioDevicePropertyIOProcStreamUsage](https://developer.apple.com/documentation/coreaudio/kaudiodevicepropertyioprocstreamusage), [kAudioStreamPropertyIsActive](https://developer.apple.com/documentation/coreaudio/kaudiostreampropertyisactive).
- [Apple technical support guidance](https://developer.apple.com/support/technical/). Verify the current submission instructions when submission is authorized; this draft makes no claim about eligibility, cost or process.

| Existing primary SDK pin | Relevant reviewed declarations |
| --- | --- |
| `AudioServerPlugIn.h` SHA-256 `de0ecbec543ac46b051ea731968ded0706401991ebec6d7976063e19651947e8` | Client ID/PID 179–202; operation definitions 301–338; device-client Start/Stop 948–984; Will/Begin/Do/End 1017–1159. |
| `AudioHardware.h` SHA-256 `a699437248e079d9ebe47078ef3861492d8253fef1a6007e476031031d3535ca` | Usage structure 849–867; public IOProc usage 966–979. |
| `AudioHardwareBase.h` SHA-256 `cbac54e8edb7ee99bf10c18a2db044e01c47645985b2f2824623f3f8371a7c4e` | Stream `IsActive` contract 1077–1086. |

The existing finding is limited to the inspected published interface: device-client lifecycle, explicit stream activity and processing operations have different scopes. No persistent per-output attachment event was identified there. The request seeks confirmation or a documented correction; it does not assert that every macOS mechanism is incapable of exposing this lifecycle, waive the original cleanup requirement or justify a backend change.
