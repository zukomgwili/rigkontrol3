# Callback and wait comparison

2026-10-25 · Beads `rigkontrol3-40h.11` · admitted offline scope only

The comparison identifies a concrete binding region: the current input completion can execute due clock maintenance, including frame-query loops, on the serial USB owner **before** replenishing ISO requests. NI's exported `readHandler` instead calls its input scan and queue operations directly, with no direct clock-maintenance call in that local path. NI's clock timer is a separate registered entry point. This establishes a local call-order difference, not the original framework's complete thread relationship or a demonstrated cause of the observed 72.7 ms gap. The diagnostic conclusion is **source-inconclusive; stop before implementation or another runtime action**. [NI-read] [NI-timer] [ISO] [Clock]

## Scope and provenance

Only existing NI 2.8.0 C/ASM exports, the checked-in ARM source and the bundled macOS 27.0 SDK were read. The report contains summaries, not proprietary function bodies. The researcher's actions were source reads and this report's creation/correction; no redecompilation, build, probe, driver/utility change, installation or security action occurred. Root separately recorded the plan/provenance and captures findings in Beads. This report and Beads tracking are the changes for this comparison; the report is evidence for the existing issue, not a second task tracker.

| Material | Pinned identity |
| --- | --- |
| NI audio 2.8.0 x86_64 binary | SHA256 `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08` |
| Existing export index, `functions.tsv` | SHA256 `c27c00c954e0ca08e4f725e8f3898b942e77c2a430578c3d9ff3115a9a4966e8` |
| Per-entry C/ASM provenance ledger | SHA256 `87835acb6a226640b4928b4cebf83adb262db531f28a60a9034a441ced502372`; root verified all 482 listed C/ASM hashes against existing exports |
| Current assessment commit | `f7b785bd3114a6047f25aa6ba3a811846a5cec89` |
| Build 7 source commit | `a3750016649f48be6a6524af2664a875b36da610`; the four current source files below are unchanged between these commits |
| Existing guided-test timing log | SHA256 `f6fc56791d145ee5c6d8240c3c34e40f0a4c41b5c8cbbc30fc3750d8bb19f080` |

The [export manifest][Manifest] identifies Ghidra 12.2, the binary hash and all 241 exported functions. The [coverage ledger][Ledger] pins individual C/ASM files and prior symbol/virtual-call mappings; image-relative addresses below are stable instruction references, not decompiler type assumptions. Current source hashes: `NativeHostUSB.m` `ef0bedbe425ac527ebdd0bb37feb9d903dcfcc5f5f5b70adc97f76d8a1734122`; `NativeAudioISO.c` `114a4ee408c20ff65b29085e1c79434a279b7f1d9792c770d9ce2f9bcf362852`; `NativeUSB.c` `cb1207b44b4157a80f8947b77ef754c2e3e6d882b960dba87e29de7a2f83df8b`; `AudioClock.c` `17f2cd1d4f2c062b41ec4d6a364de3a5ee0f46bbbe2760496cb95d95d9b784ad`.

## Four execution paths

| Path | Original NI instruction evidence | Current binding, locks and waits | Established difference / limit |
| --- | --- | --- | --- |
| **ISO completion** | `staticReadHandler@0x74f5` tail-calls `readHandler@0x7a80`. Normal processing calls `readFrameList@0x7b40`, then acquires engine lock `+bc40@0x7b4c`; it queues IN at `0x7e35`, OUT at `0x7e40`, then releases that lock at `0x7e4c`. The input scan separately uses `+bc48@0x6f93/0x70d6`. `staticWriteHandler@0x7505` directly calls `writeHandler@0x7ec0`, which publishes its saved position and uses `+bc40` when releasing the OUT slot. [NI-shims] [NI-write-shim] [NI-read] [NI-scan] [NI-write] | Host completion captures raw arrival at line 490, posts a default-mode run-loop block at 496, and records worker entry at 497. The per-pipe accepted-order drain retires bookkeeping, records ordered delivery, and directly calls the ISO callback at 424–445. `read_complete` directly calls `service`; service checks clock maintenance at 292 **before** decode/requeue at 299/333–334. The decoder try-guard is acquired at 297 and released at 331, before Host enqueue; it is not held during clock maintenance. [Host-ISO] [Host-order] [ISO] | A user-process serial-queue → serial-worker handoff is explicit. The current input callback also contains an inline due-maintenance point absent from the original local handler. No claim follows that NI's controller callbacks and timer used independent threads, or that every modern async acceptance call is instantaneous. |
| **Timestamp maintenance** | Engine initialization obtains inherited `getWorkLoop@0x578a`, constructs the timestamp event source at `0x5802–0x5814`, and adds it at `0x582b`. Wrapper `0x4156` directly calls `timestampTimerCallback@0xbbb0`. The callback calls `updateWallTimePerUSBCycle@0xbbe9`, advances the byte counter, conditionally probes an anchor at `0xbc1b`, and rearms 128 ms at `0xbc36–0xbc42`. The stable frame/local-uptime/frame retry is `0xb8a0–0xb8fd`; boundary sampling is `0xba80`. [NI-init] [NI-timer-wrapper] [NI-timer] [NI-update] [NI-anchor] | `AudioClock.c:51–57` retains that stable-sample retry; its boundary loop is at 30–48. The default provider calls `frameNumberWithTime:` directly through `NativeAudioISO.c:118–121` and `NativeHostUSB.m:247–252`. There is no explicit run-loop pump in these application loops/provider. Due service has the preserved 128 ms guard and eight-callback boundary phase; it runs on the existing worker. [Clock] [Clock-provider] [Host-frame] | The retry algorithm is **retained**, not newly invented. Its provider and dispatch context changed. The SDK specifies current controller frames/system time but no maximum call duration or cooperative-pump guarantee. A 1.1 ms deadline checked between queries cannot bound one query's duration. Original workloop attachment alone does not establish the actual USB-versus-timer thread identity. [SDK-object] |
| **EP1 completion** | Device initialization installs `staticCommandReplyDispatch@0x0ffc–0x1003` and starts the read at `0x1038`. Shim `0x1564` directly calls `commandReplyDispatch@0x20e0`. Normal reply decoding acquires device lock `+120@0x2181`, dispatches reports, rearms the read at `0x227f`, and releases the lock at `0x228d`; error branches differ. [NI-device-init] [NI-EP1-shim] [NI-EP1] | `complete_bulk` posts the same worker at `NativeHostUSB.m:340–360`; that block copies data and directly invokes the command/report callback. `report_complete` takes the session mutex briefly around history/info/snapshot publication, then rearms through `arm_reader` at `NativeUSB.c:433–441`. Input recovery uses its separate drain gate, not an unconditional blocking sleep. [Host-bulk] [EP1] | EP1 and ISO retirement now share the explicit worker. These exports do not prove that the original EP1 and ISO provider callbacks were concurrent or on different workloops. Shared current ownership is a structural fact, not evidence that MIDI caused the recorded gap. |
| **Synchronous command caller** | `NIUSBUserClient::MIDIWrite@0x9450` uses its client lock `+708` and calls device `MIDIWrite@0x94a8`; that method calls `sendCommand@0x2a3a`. `sendCommand` acquires the separate device `+128` lock at `0x25f0`, issues a direct pipe Write at `0x26a2` with two 200 ms timeout arguments and a null completion, then unlocks at `0x26e8`. No NI dispatch hop or application-style run-loop pump is present in this exported path. Exact inherited caller/controller scheduling remains unknown. [NI-client-write] [NI-device-write] [NI-send] | External `submit` queues/wakes the owner and waits on a condition at `NativeUSB.c:246–270`; that wait releases the mutex. The worker releases its queue mutex at 931 before executing `command_worker`. The latter enqueues asynchronous EP1 OUT at 567–568, then explicitly pumps callbacks every requested 5 ms during write wait and 10 ms during reply wait. `pump_callbacks:177–184` runs the default run loop plus audio/recovery/panic service. ISO startup calls the worker command function directly rather than condition-waiting on itself. [Submit] [Command] [Pump] [Host-bulk] | Synchronous caller completion is separate from a cooperative worker wait. The current bulk API explicitly submits asynchronous I/O; it is **not** the SDK's synchronous `sendIORequest` variant. Thus a 200 ms command timeout does not establish a 200 ms period without ISO servicing. Neither async completion nor explicit pumping supplies a hard acceptance-call/dispatch latency bound. [SDK-pipe] |

The modern worker also calls connection/recovery/audio/panic services between run-loop entries (`NativeUSB.c:991–1025`). Both completion blocks and explicit pumps use default mode; a mode mismatch is not apparent in this code. The requested 5/10 ms run-loop intervals are not measured maximum handoff times. SDK declarations for `CFRunLoopPerformBlock`, `CFRunLoopWakeUp` and `CFRunLoopRunInMode` do not promise a scheduling deadline. No absence of *internal framework* pumping is inferred from the absence of an explicit application pump. [Worker] [Pump] [Host-ISO] [Host-bulk] [SDK-runloop]

## Concrete region, without a causal leap

The added non-pumping **application region** is the serial owner's due clock service before ISO replenishment: `read_complete → service → ni_audio_clock_service → update_interval/boundary → frameNumberWithTime:`. The stable-sample loop has no iteration deadline, and the selected frame getter's documented contract has no time bound. The original local `readHandler` calls the scan and queue functions without calling `updateWallTimePerUSBCycle` or `timestampTimerCallback`; its timer is separately registered. This is a concrete call-order/provider-binding candidate. It is **not** proof that the clock occupied the observed gap, that the original framework always avoided equivalent serialization, or that a move/reorder is a necessary or safe correction. Other direct framework methods and ordinary worker work likewise have no measured occupancy attribution here. [NI-read] [NI-timer] [NI-update] [ISO] [Clock] [Host-frame] [SDK-object]

The SDK specifies a serial queue for asynchronous I/O service. The additional CFRunLoop marshalling and inline clock placement are current application choices; the inspected contracts do not show either to be Apple-required or ARM-mandatory. Decision `.17` accepts the existing public baseline for this personal Mac under the firm SIP constraint; it does not waive preservation of NI's local ordering or prove every chosen adapter necessary. This comparison does not reopen that deployment decision or admit another backend. [SDK-object] [Host-ISO] [ISO] [Binding-decision] [Binding-contracts]

Command waits are an important negative finding: their explicit pumps prevent identifying the whole ACK/write timeout as a blind interval from source alone. Conversely, a pump does not make each inline API, mutex acquisition, decoder pass or nested callback bounded. Original engine/device/client locks are distinct, but that is insufficient to reconstruct all inherited scheduling domains or worst-case lock duration. [NI-read] [NI-send] [Command] [Pump]

## What the existing failure proves

The retained [guided-test log][Trace] reports IN `0x82`, old start `31635003`, rejected target `31635067`, and a post-rejection controller query of `31635100`. Target minus old start is exactly 64. Using the already-established Mach scale 125/3 ns per tick:

| Existing milestones | Elapsed |
| --- | ---: |
| Raw framework entry `6131589911587` → worker block entry `6131591656298` | 72.696292 ms |
| Worker entry → ordered delivery `6131591656311` | 0.000542 ms |
| Ordered delivery → enqueue entry `6131591656499` | 0.007833 ms |
| Enqueue entry → return `6131591878535` | 9.251500 ms |

The handoff interval alone exceeds the complete 64 ms requeue lead. Its raw tick is recorded **before** publication, however; the interval includes raw-handler execution/preemption, publication/wake, run-loop availability, earlier worker work, blocking and descheduling. The existing fields cannot separate those causes. The nearly immediate delivery-to-enqueue path means the failed callback itself did not spend the earlier 72.7 ms in clock maintenance after its worker entry. [Trace] [Host-ISO] [Host-order]

The saved last job is on the matching IN pipe and has the matching old start, supporting this particular correlation; `debug_last_iso` is interface-wide, not a universal per-request correlation guarantee. Ordered hold for this job was negligible. `ready=0`, `pending=127`, `decoderBusy=0` describe the rejection-time snapshot, not every moment of the earlier gap. The post-call controller frame being 33 frames beyond the target does not timestamp when it first became late. The enqueue measurement brackets the framework acceptance call, not USB transfer completion. [Trace] [Host-order] [Host-ISO] [ISO]

## Disposition and stop

**Region identified; actual-cause conclusion remains source-inconclusive.** The narrow hypothesis is that the added shared-owner binding kept ready ISO retirement from running during another non-pumping operation. The source establishes where this could occur but cannot say whether it occurred, versus raw pre-publication delay or worker descheduling. This admitted comparison has reached its stop condition: capture these findings and do not change code or run another test. A publication/occupancy discriminator would need its own recorded plan and admission; it is not performed or authorized by this report.

The user's firm constraint remains **Keep SIP enabled**; no security change is authorized.

Keep NI's 64-frame requeue calendar, 128 OUT slots, 16,384-frame ring, 128 ms maintenance cadence/eight-tick anchor phase and recovered 100 ms TooOld panic policy intact. No queue lead, QoS, backend, recovery, security or scheduling change is justified solely by this comparison. Existing original-functionality and physical/stability gates remain open. [NI-IN] [NI-read] [NI-timer] [ISO] [Clock]

## Primary source references

All current line references are pinned to the assessment commit above. Original addresses refer to the existing private exports; the ledger contains each C/ASM hash. Bundled SDK hashes: `IOUSBHostObject.h` `49acd363ecf659d22653dc533c36d81b7ca59db86a92adbc9d904cb003be093d`; `IOUSBHostPipe.h` `1638832f66cf90b6170ff64cce1c992c4de29d94f15d6ae633e3f3504e2314d5`; `CFRunLoop.h` `8eae64468e30c23a0e0e9a6a8dbf9d373644d0691ad3ca1613ebef3c42173742`. These headers specify the cited public interfaces, not the missing historical runtime implementation or a current scheduling guarantee.

[Manifest]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/manifest.txt>
[Ledger]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/ReverseEngineering/Audit/audio280-function-coverage.json>
[NI-shims]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/000074f0_staticReadHandler.asm>
[NI-write-shim]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00007500_staticWriteHandler.asm>
[NI-timer-wrapper]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00004130_de_caiaq_driver_NIUSBAudioEngine__staticTimestampTimerCallback.asm>
[NI-EP1-shim]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00001550_staticCommandReplyDispatch.asm>
[NI-read]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00007a80_de_caiaq_driver_NIUSBAudioEngine__readHandler.asm>
[NI-scan]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00006f60_de_caiaq_driver_NIUSBAudioEngine__readFrameList.asm>
[NI-write]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00007ec0_de_caiaq_driver_NIUSBAudioEngine__writeHandler.asm>
[NI-IN]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00006640_de_caiaq_driver_NIUSBAudioEngine__queueInIrp.asm>
[NI-init]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00005320_de_caiaq_driver_NIUSBAudioEngine__initHardware.asm>
[NI-timer]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/0000bbb0_de_caiaq_driver_NIUSBAudioEngine__timestampTimerCallback.asm>
[NI-update]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/0000b880_de_caiaq_driver_NIUSBAudioEngine__updateWallTimePerUSBCycle.asm>
[NI-anchor]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/0000ba80_de_caiaq_driver_NIUSBAudioEngine__getAnchorFrameAndTimeStamp.asm>
[NI-device-init]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00000db0_de_caiaq_driver_NIUSBAudioDevice__initHardware.asm>
[NI-EP1]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/000020e0_de_caiaq_driver_NIUSBAudioDevice__commandReplyDispatch.asm>
[NI-client-write]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00009450_de_caiaq_driver_NIUSBUserClient__MIDIWrite.asm>
[NI-device-write]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00002990_de_caiaq_driver_NIUSBAudioDevice__MIDIWrite.asm>
[NI-send]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/000025c0_de_caiaq_driver_NIUSBAudioDevice__sendCommand.asm>
[Host-ISO]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:463>
[Host-order]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:424>
[Host-bulk]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:340>
[Host-frame]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:247>
[ISO]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeAudioISO.c:289>
[Clock]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/AudioClock.c:30>
[Clock-provider]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeAudioISO.c:118>
[EP1]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeUSB.c:373>
[Submit]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeUSB.c:237>
[Command]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeUSB.c:539>
[Pump]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeUSB.c:177>
[Worker]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeUSB.c:991>
[SDK-object]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostObject.h:36>
[SDK-pipe]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostPipe.h:223>
[SDK-runloop]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/CoreFoundation.framework/Headers/CFRunLoop.h:73>
[Trace]: </private/tmp/rk3-build7-guided-ir1_nm64/system-timing.ndjson>
[Binding-decision]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/ReverseEngineering/KernelBindingDecision.md>
[Binding-contracts]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/ReverseEngineering/SelectedBindingContracts.md>
