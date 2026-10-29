# USB completion registration and delivery comparison

2026-10-25 · Beads `rigkontrol3-40h.11` · offline evidence only

The bounded comparison identifies a **public timing-contract gap**, not a proven incorrect registration or a fix. NI submits eight-entry low-latency ISO requests with `updateFrequency` values **0 for IN and 1 for OUT**. The current transaction-list API has no documented equivalent frequency parameter or primary-interrupt publication/delivery deadline. Its only declared transfer option is `None`; transaction `Wrap` describes memory wrapping. None of these proves equivalent timing, but the inspected material identifies no missing available public frequency/interrupt flag. [NI-IN] [NI-OUT] [Legacy-signatures] [SDK-pipe] [SDK-definitions]

This report stops at entry into the current framework completion block, called **R**. Callback bodies and post-R marshalling are covered by the earlier [callback/wait comparison][Earlier] and are not audited again here. The researcher changed only this report. Root separately owns Beads planning/evidence capture. No driver/utility changes, redecompilation, framework-binary analysis, new test, runtime probe, installation or security action occurred.

## Identity and mapping evidence

Assessment HEAD is `a4db45d03a1c475468d4d792cb3d416671ebba9f`; build 8 source is `4e23f8bce00f1399f3096fc34244b3d557a4e1cb`. The cited current Host/ISO files are unchanged between them. Original NI audio 2.8.0 x86_64 SHA256 is `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`; export index SHA256 is `c27c00c954e0ca08e4f725e8f3898b942e77c2a430578c3d9ff3115a9a4966e8`. The [manifest][Manifest] and [241-entry ledger][Ledger] identify the exports; ledger SHA256 is `87835acb6a226640b4928b4cebf83adb262db531f28a60a9034a441ced502372`. Root revalidated the selected 16 C/ASM hashes for eight ISO registration/callback entries. Addresses below are image-relative instruction evidence; decompiler casts do not establish API identity.

NI's calls match the legacy header's adjacent bulk Read/Write, status, clear, policy and low-latency Read/Write declarations (`+0x1d0` through `+0x200`). Both low-latency overloads end in `updateFrequency`, in milliseconds. This identification combines existing ABI evidence with declarations; it does not reconstruct the historical framework implementation. [Legacy-order] [Legacy-signatures] [NI-IN] [NI-OUT] [NI-EP1-init] [NI-command]

Root supplied the narrowly verified legacy header at `/private/tmp/rk3-legacy-IOUSBPipe.h`, SHA256 `87d221ce72bf7e1ba8ac1d9ada35bac31ece5e7d8af30c7c84d69d55606acfa3`. It was a single existing-source API-identity verification, recorded by root as the exception to the local-only evidence set; the researcher opened no additional network path.

## Four registrations

| Registration | NI record, API and arguments | Current selected binding | Result / remaining contract |
| --- | --- | --- | --- |
| **ISO IN** | `allocIsocIrps@0x7122–0x713e` selects `staticReadHandler@0x74f0` for direction 1 and writes action at IRP `+0xc8@0x7220`. `clearIsocIrps` sets target=`this` at `+0xc0@0x6507`, scalar slot index at `+0xd0@0x650e`; stride is `0xf0`. `queueInIrp@0x66c1–0x6728` passes the descriptor at `+0x30`, absolute start frame, **8** frame entries at `+0x40`, completion at `+0xc0`, and **updateFrequency=0** to the low-latency Read overload. Requested count is 512 per entry (`init@0x4896`, copies at `0x6685–0x66b8`). [NI-alloc] [NI-clear] [NI-init] [NI-IN] | `NativeAudioISO.c:225–241` prepares eight 512-byte transactions with fixed slot/microframe offsets and no Wrap; Host `NativeHostUSB.m:641–643` submits pointer+first, count, nonzero scheduled absolute frame and `TransferOptionsNone`, with a completion block. [ISO-IN] [Host-enqueue] | Eight-entry/request-size/absolute-calendar representation is present. The modern API omits NI's frequency argument. Its per-transaction completion fields and the current all-eight-ready decoder guard do not establish the original primary-publication cadence. [SDK-pipe] [SDK-definitions] |
| **ISO OUT** | The allocator selects `staticWriteHandler@0x7500` and stores it at IRP `+0xc8@0x7300`; target/index come from the same clear routine. `queueOutIrp@0x78af–0x790b` saves the START ring position at `+0xd8`, passes its live descriptor, absolute frame, **8** entries/completion pointer, and **updateFrequency=1** to low-latency Write. `1` specifies frame-list update frequency, not a callback deadline. [NI-alloc] [NI-clear] [NI-OUT] [Legacy-signatures] | `NativeAudioISO.c:265–284` supplies eight input-led byte counts, including zeros, into the same mapped output ring; public transaction Wrap is set only when one transaction crosses the backing descriptor's end. The Host uses the same transaction-list enqueue form/transfer option. [ISO-OUT] [Host-enqueue] | Low-latency Write identity is established. Output count/callback/ring representation exists, while frequency processing and delivery timing equivalence remain unproved. Wrap is not a substitute for `updateFrequency=1`. [SDK-definitions] |
| **EP1 IN** | Device initialization sets target=device at `+0x198@0x0ff4`, action=`staticCommandReplyDispatch` at `+0x1a0@0x1003`, parameter=owned input descriptor at `+0x1a8@0x1013`; it passes that record to the **normal bulk Read** at `0x1038` with two zero timeout arguments and 64 requested bytes. The shim at `0x1559–0x1564` converts remaining bytes to actual length before the device handler. The same embedded record is used for rearm at `0x2262–0x227f`. [NI-EP1-init] [NI-EP1-shim] [NI-EP1-rearm] | Host `bulk` creates retained payload/job storage and calls the explicitly asynchronous bulk/interrupt enqueue at `NativeHostUSB.m:468–486`; its completion supplies status and actual byte count. `NativeUSB.c:437–441` supplies persistent session context/read storage and timeout 0. [Host-bulk] [EP1-current] | This is not a low-latency ISO call. The API/byte-count ABI translation is explicit. Original callback-domain/latency and complete inherited teardown equivalence are not established by the embedded record or a stable address alone. [SDK-bulk] |
| **EP1 OUT** | `sendCommand@0x2681–0x26a2` calls **normal bulk Write** using its owned descriptor, two 200 ms timeout arguments, requested command length and **null completion**. This is the synchronous caller form; it does not register an ISO callback. [NI-command] [Legacy-bulk] | The provider snapshots command bytes into retained payload storage and uses asynchronous bulk enqueue with a total completion timeout and a completion block. The native command caller waits cooperatively as already documented in the earlier comparison. [Host-bulk] [Earlier] | A selected-binding structural translation exists; a synchronous caller is not evidence that the current framework acceptance call waits for transfer completion. No timeout is a demonstrated callback-delivery bound. [SDK-bulk] |

### Completion and backing lifetime

NI's ISO completion is an embedded three-pointer-sized target/action/parameter record at `+0xc0/+0xc8/+0xd0`; its parameter is a slot index, not a temporary stack object. Accepted IN increments outstanding count and retains the engine at `0x6745–0x6752`; the read callback's common exit releases it at `0x7acc`. Accepted OUT retains the engine at `0x7922–0x7928` and its callback releases at `0x80f9`. Descriptor preparation/retention and completion/release surround OUT submissions (`0x7857–0x7870`, `0x7f01–0x7f21`). These are source-backed lifetime provisions, not a blanket proof of every historical cancellation/thread behavior. [NI-alloc] [NI-clear] [NI-IN] [NI-OUT] [NI-read-exit] [NI-write-exit]

The current C job retains interface, mapped data and list before enqueue; the framework block captures its stable context. Rejection follows the no-callback path; accepted work retires mappings/resources at its existing completion lifecycle. The target/action/index representation becomes typed C/Objective-C callbacks and contexts. Compiling against the chosen current APIs requires these type/layout translations; raw Intel vtable offsets and old struct bytes cannot be used as the ARM API. This does not prove every adapter ownership/dispatch choice unavoidable for ARM. [Host-enqueue] [Host-lifetime] [Binding]

The old frame array has status/request/actual/timestamp at 16-byte strides (`0x6480–0x6501`). The current packed transaction ABI has status, 32-bit request/offset/complete fields, an AbsoluteTime timestamp and options. `KernelUserShared` allocation supplies mapped storage; it does not promise the old interrupt-time frame-list update frequency, a publication memory-order protocol or physical DMA timing. [NI-clear] [SDK-definitions] [SDK-memory] [Host-list]

## Delivery upstream of R

The legacy header specifies interrupt-time Read frame-list processing and millisecond update frequency. It does not define the zero special case, callback thread or delivery deadline. Those properties cannot be inferred from the low-latency name. [Legacy-primary]

The current interface is initialized with a supplied **serial I/O queue** and ownership options `None` (`NativeHostUSB.m:255,277–278`). The SDK says underlying asynchronous I/O is serviced on that queue; interest notifications use a separate internal serial queue. The first source-level statement in the completion block captures the R entry mark at `NativeHostUSB.m:644`; this is not a machine-prologue timing claim. The selected SDK provides no callback-delivery deadline or documented equivalent of the old frequency argument. No undocumented/private interrupt option is considered. [Host-open] [Host-enqueue] [SDK-object] [SDK-definitions]

Current transaction status/complete counts are documented as updated on completion. Its timestamp is an **observed** completion AbsoluteTime; interrupt latency/system load can make multiple transactions share a stamp. There is no specified order between shared result visibility and entering the completion block that proves NI's primary-publication cadence. Allocating shared lists or waiting for eight completed entries preserves representation/denominators, not that missing timing guarantee. [SDK-definitions] [SDK-memory] [Host-list]

The saved build 8 event records **95.374 ms** from the transaction timestamp to R and **34.916667 µs** from R to worker entry. The former is not a physical wire-completion latency, nor attribution to a particular kernel/framework queue, interrupt or thread. It places this event's large recorded span upstream of our raw block entry; it does not reproduce the earlier 72.7 ms post-R interval. This report adds no new timing observation or causal hypothesis. [Build8]

## Disposition

No concrete wrong target/action, missing frame count or wrong **available documented** completion option was identified. The concrete surface difference is the loss of the explicit `updateFrequency` input/output argument, with primary-publication and upstream delivery timing still unproved through the selected API. **Stop at that contract gap.** This report authorizes no code, test, flag change, QoS/lead adjustment, backend or recovery proposal.

The accepted `.17` public baseline is a deployment choice under the user's firm **Keep SIP enabled** constraint. Typed API/ABI translation is necessary for that selected binding, and the SDK specifies serial async-I/O delivery (creating a queue when none is supplied). This application's particular supplied queue and extra CFRunLoop marshalling are not thereby shown universally ARM-mandatory or Apple-required. Original NI functionality/timing requirements remain, including the +64 requeue calendar, live ring, 128 ms/eight-tick clock maintenance and recovered TooOld recovery. The primary lift-and-shift remains incomplete. [Binding] [Contracts]

## Pinned sources

Current source hashes: Host `c0261c598c58daa4466349acafe48fa190bc84d8a661c4b37e38867df4f94805`; ISO `e827592152d6c80e30194a4f022347c52ced6daef99b89140c006019037314bc`. Selected original ASM hashes: allocator `c2ff657c63ce5767122801cf39db8800dba4483e716a96d17be6659dc09ae119`; clear `7acb689b1995da19b0e084b7c97449e2b689b73e09ea8965463851005bf8af41`; queue IN `a3eed13617daebe1a85a4b21e9963c02af3dafdf258f4f02aa38be9c4641986e`; queue OUT `9ea27c3d9d8026d5871e8b775f098ebedb3e7633dccfaea9f1b3c6350064be0d`. Other cited export hashes are in the pinned ledger.

SDK IOUSBHostObject.h SHA256 `49acd363ecf659d22653dc533c36d81b7ca59db86a92adbc9d904cb003be093d`.

SDK IOUSBHostPipe.h SHA256 `1638832f66cf90b6170ff64cce1c992c4de29d94f15d6ae633e3f3504e2314d5`.

SDK IOUSBHostDefinitions.h SHA256 `c1d90c000b98c1ea75ba338103f03e3d972692df59ac9b2830b534e04ae1fa79`.

[Manifest]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/manifest.txt>
[Ledger]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/ReverseEngineering/Audit/audio280-function-coverage.json>
[NI-alloc]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/000070e0_de_caiaq_driver_NIUSBAudioEngine__allocIsocIrps.asm>
[NI-clear]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00006460_de_caiaq_driver_NIUSBAudioEngine__clearIsocIrps.asm>
[NI-init]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00004170_de_caiaq_driver_NIUSBAudioEngine__init.asm>
[NI-IN]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00006640_de_caiaq_driver_NIUSBAudioEngine__queueInIrp.asm>
[NI-OUT]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00007620_de_caiaq_driver_NIUSBAudioEngine__queueOutIrp.asm>
[NI-read-exit]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00007a80_de_caiaq_driver_NIUSBAudioEngine__readHandler.asm>
[NI-write-exit]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00007ec0_de_caiaq_driver_NIUSBAudioEngine__writeHandler.asm>
[NI-EP1-init]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00000db0_de_caiaq_driver_NIUSBAudioDevice__initHardware.asm>
[NI-EP1-shim]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/00001550_staticCommandReplyDispatch.asm>
[NI-EP1-rearm]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/000020e0_de_caiaq_driver_NIUSBAudioDevice__commandReplyDispatch.asm>
[NI-command]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/.build/ni/decompiled/audio280-text/000025c0_de_caiaq_driver_NIUSBAudioDevice__sendCommand.asm>
[Legacy-order]: <https://github.com/apple-oss-distributions/IOUSBFamily/blob/main/IOUSBFamily/Headers/IOUSBPipe.h#L260-L357>
[Legacy-signatures]: <https://github.com/apple-oss-distributions/IOUSBFamily/blob/main/IOUSBFamily/Headers/IOUSBPipe.h#L339-L357>
[Legacy-bulk]: <https://github.com/apple-oss-distributions/IOUSBFamily/blob/main/IOUSBFamily/Headers/IOUSBPipe.h#L260-L284>
[Legacy-primary]: <https://github.com/apple-oss-distributions/IOUSBFamily/blob/main/IOUSBFamily/Headers/IOUSBPipe.h#L326-L337>
[ISO-IN]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeAudioISO.c:225>
[ISO-OUT]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeAudioISO.c:265>
[Host-open]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:255>
[Host-enqueue]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:615>
[Host-bulk]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:468>
[Host-list]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:395>
[Host-lifetime]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeHostUSB.m:501>
[EP1-current]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/Driver/NativeUSB.c:437>
[SDK-pipe]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostPipe.h:323>
[SDK-bulk]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostPipe.h:223>
[SDK-object]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostObject.h:26>
[SDK-definitions]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostDefinitions.h:49>
[SDK-memory]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOUSBHost.framework/Headers/IOUSBHostObject.h:398>
[Earlier]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/ReverseEngineering/CallbackWaitComparison.md>
[Binding]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/ReverseEngineering/KernelBindingDecision.md>
[Contracts]: </Users/zukomgwili/.codex/worktrees/ni-arm-driver/rigkontrol3/ReverseEngineering/SelectedBindingContracts.md>
[Build8]: </private/tmp/rk3-build8-publication-yytwxpo1/state.json>
