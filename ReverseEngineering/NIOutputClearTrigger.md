# NI output-clear trigger and Apple stream-client lifecycle

The inspected Apple source supports a **mixed lifecycle**: an `IOAudioStream` counts attached client buffers. Buffer registration, user-client Start/Stop, and buffer removal can change that attachment count. It is neither a permanent registration count nor a count of public CoreAudio IOProc registrations. This establishes the framework model; it does not yet identify NI's direct stream-object field read or prove the exact framework binary used with NI 2.8.0.

## Source and version boundary

The primary source is Apple's `IOAudioFamily-203.3`, immutable commit [`bbc41d05fd968710c91fa7794c5ed575cf28cb88`](https://github.com/apple-oss-distributions/IOAudioFamily/commit/bbc41d05fd968710c91fa7794c5ed575cf28cb88). Its Apple import commit is dated 2015-06-05. The nearby `204.3` import is dated 2015-12-09; its implementation was **not** inspected or assumed identical. [Apple 204.3 import](https://github.com/apple-oss-distributions/IOAudioFamily/commit/165af8be6681d7dd8b7868468c34614ed3c14dad).

This is a pinned 2015-era source model, not proof of the original test Mac's OS release or its loaded IOAudioFamily binary. NI's recovered `audio-Info.plist` identifies driver **2.8.0 (R36)**, SDK `macosx10.9`/`13C64`, Xcode `0511`/`5B1008`, and build-machine OS `13F34`. Its IOAudioFamily dependency `1.1d25` is a minimum requirement, not the actual runtime revision. It therefore cannot select Apple 203.3 as NI's exact target implementation.

Exactly five Apple source files were downloaded; no framework clone or runtime API was used in the Apple analysis. Raw files and their download metadata remain in `.build/ni/apple-ioaudio/IOAudioFamily-203.3/`. The NI comparison used existing pinned x86-64 C reconstructions and instruction listings, with instructions taking precedence over inferred decompiler types. No legacy code was executed. [Machine-readable provenance and bounds](Evidence/NIOutputClearTriggerAudit.json).

| File | SHA-256 |
| --- | --- |
| `IOAudioStream.h` | `a0f508d0e507f78cfc7bd689858b938ac39b1ef47cb24cc9076ff00215530833` |
| `IOAudioStream.cpp` | `2a83f87f2e5aabb365fd1796c9151db382d3c31d31ff0c9759773757dd45f44b` |
| `IOAudioEngineUserClient.h` | `0d3bc899de39b5436527c6d73f817fffacee7d09a42795177f0d6eda309f4b88` |
| `IOAudioEngineUserClient.cpp` | `9476eb13021ec7e169273f5ab6bd122fe52c9b69726fe798e12b251b1d01468e` |
| `IOAudioEngine.cpp` | `293a0481858b6d31efe870bf689371ac74b10b17e1e665d0a848da6550f8965f` |

## NI's actual conditional clear

The reference `NIUSBAudioDriver.x86_64` has SHA-256 `41574699c507896cb3a51b4756143e8c445eb76994871eb13517a642f7adaf08`. In `writeHandler@0x7ec0`, the normal clear path is reached only after the restart bypass is excluded and the callback status is `0` or `0xe00002e7`. For each output group `g`, it checks the written latch at `engine + 0xb995 + g` and loads the object in odd stream slot `engine + 0xba40 + 8*(2*g+1)`.

| Instruction | Observed operation |
| --- | --- |
| `0x7f62` | Skip the clear when this group's written latch is zero. |
| `0x7f70`–`0x7f7f` | Load the odd-slot object and skip when DWORD `[object + 0xa8]` is nonzero. |
| `0x7f86` | Call `clearSampleBufferForStream@0x7510` with this group index. |
| `0x7f8b` | Reset the written latch **after** that call. |

There is no named `getNumClients()` call at this gate. The exact proved predicate is a direct zero field test; identifying the field symbolically requires the ABI proof below. This inspection does not re-prove the clear helper's previously recovered wire footprint.

Object provenance is narrower than a name inferred at the read site: `initCoreAudio@0x4b20` passes direction `0` and memory from the buffer NI labels `audioOutBuf` to `createNewAudioStream@0x4d60`, then stores the successful result in odd slots (`0x4cce`, `0x4cf0`). Direction `1` results go to even slots (`0x4c6e`, `0x4c94`). The creation function retains the allocated object in `R12`, calls the cached-symbol-labelled `IOAudioStream` constructor at `0x61d00`, passes the direction to initialization, and returns that same successful object at `0x5229`. The constructor stub body was not expanded. Thus the checked object is the original direction-0 `IOAudioStream` construction result; it is not a modern HAL device-client counter.

## What the stream counter counts

`IOAudioStream` derives from `IOService`. Its declared instance fields include four pointers followed by `UInt32 numClients`; its protected interface exposes the add/remove/getter operations. These declarations establish member order and names, not an absolute binary displacement. [Class and member declarations](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioStream.h#L65-L105), [protected operations](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioStream.h#L283-L289).

On successful admission of a buffer absent from that stream's list, `addClient` increments `numClients` and links the buffer into `userClientList`. Duplicate attachment does not increment it. `removeClient` decrements only when it finds that buffer in the list. The getter directly returns this same field, and `updateNumClients` publishes it as the stream's client-count property. The counter belongs to the individual stream; its admission logic distinguishes input and output directions. [Counter publication and admission](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioStream.cpp#L1293-L1373), [removal and getter](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioStream.cpp#L1381-L1459).

The counted unit is therefore an attached `IOAudioClientBuffer`, not a PID, application, public IOProc or aggregate engine client. One user client can retain buffer sets containing separate input and output lists. The source's connection-ID calculation also does not establish a public IOProc identity mapping. [Buffer-set registration](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngineUserClient.cpp#L1603-L1662), [connection-ID calculation](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngineUserClient.cpp#L2450-L2454).

## Registration and Start/Stop are distinct paths

The successful paths show:

- Registration stores the buffer in its direction's list; it attaches to the stream immediately only when the user client is online. [Registration](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngineUserClient.cpp#L1625-L1680).
- Starting an offline user client starts the engine client, then attaches its stored output and input buffers. An already-online start does not repeat that attachment traversal. [Start](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngineUserClient.cpp#L2578-L2641).
- Stopping an online user client detaches both lists, stops the engine client, and marks it offline. The registered buffer sets remain available for a later Start. [Stop](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngineUserClient.cpp#L2659-L2705).
- Unregistration finds/removes a buffer and frees it; freeing also asks its stream to detach it. Provider shutdown stops the client and frees its retained buffer sets. [Unregistration](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngineUserClient.cpp#L1719-L1806), [freeing and shutdown](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngineUserClient.cpp#L883-L966).

Thus buffer destruction is not required for a stream count to fall: stopping its owning **kernel engine user client** can detach buffers while retaining their registrations. Whether stopping one public output IOProc causes that kernel lifecycle operation, unregisters only selected buffers, or leaves a shared user client online is not established by these files. Error/partial-start paths also prevent treating this as an unconditional equivalence between online status and count.

The engine has a separate `numActiveUserClients` counter. Its first active user client starts the engine; when that engine-level count reaches zero while running, stopping is scheduled at a stream position. This is a different scope from one stream's `numClients`. [Engine client calls](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngine.cpp#L1197-L1220), [active count and engine transition](https://github.com/apple-oss-distributions/IOAudioFamily/blob/bbc41d05fd968710c91fa7794c5ed575cf28cb88/IOAudioEngine.cpp#L1224-L1270).

## Limit on interpreting NI's field access

The Apple accessor and lifecycle are proved for this pinned source revision. NI's direct DWORD read at stream-object `+0xa8` cannot be named `numClients` from source member order alone. An absolute x86-64 offset additionally requires the relevant base-class layout, packing/ABI and revision binding, or equivalent direct binary/type evidence. None was established by this bounded inspection. Identifying an allocated object as `IOAudioStream` narrows the object class but does not by itself settle that member displacement.

A bounded reuse check of the existing `absolute-prefix/prefix-disposition.md` and `prefix-evidence.json` did not close this gap: they establish declaration/vtable-slot contributions, not object data layout or `sizeof(IOService)`. Their XNU 2422 model is itself not an exact NI SDK binding. Inspection stopped there; no new XNU header recovery, layout program or production change followed.

Even if the NI field is independently bound to this counter, the source does not justify substituting any of these conditions for it: last public output IOProc stopped, same-process native client count zero, global output `IsActive` false, or whole-device Stop. Such a substitution would require its own demonstrated lifecycle correspondence. No functional driver change follows from this source analysis alone.

The NI direct gate and clear-before-latch-reset ordering above are instruction observations. Its literal clear footprint is retained from earlier evidence. This study does not qualify physical clearing, absence of stale replay, original timing/recovery parity or the existing unused-buffer NULL requirement. The [build 17 trace](Evidence/Build17HostClientLifecycleValidation.json) observed a shared native device-client lifetime while public output stopped and input continued; it did not witness the original per-output gate. These remain separate open obligations in existing M3, `rigkontrol3-40h.3`.

The bounded audit is complete. Before changing a functional trigger, the missing NI field binding and the modern output-ownership correspondence must be established. No new diagnostic run, timing adjustment, timeout or inferred client count is authorized by this result. Installed build 16, NI ring/timing/recovery, mappings, calibration and LED policy are unchanged; SIP stays enabled. No new bead was created.

## Bounded inspection record

Root recorded the bounds in existing `rigkontrol3-40h.3` before body reads. The initial 1,403 lines plus the approved 75-line extension total **1,478 unique Apple source lines**:

- `IOAudioStream.h`: 65–105, 280–295.
- `IOAudioStream.cpp`: 365–390, 1288–1465.
- `IOAudioEngineUserClient.h`: 140–162, 238–275.
- `IOAudioEngineUserClient.cpp`: 880–1070, 1288–1810, 2450–2755.
- `IOAudioEngine.cpp`: 1140–1275.

Exact-symbol web searches returned no results. Repository/tag/commit inventory and direct immutable Apple-source URLs supplied the evidence; no secondary source was used. No production code, test, package or runtime action was performed.

NI body reads were limited to the pinned `writeHandler` C 1–93 and ASM 1–124, `initCoreAudio` C 1–76 and ASM 1–149, and `createNewAudioStream` C 1–119 and ASM 1–64/261–276: **641 lines** total. NI plist/checksum metadata and the existing prefix disposition/evidence artifacts were separately admitted before reading. All paths, hashes and limits are retained in the [audit evidence](Evidence/NIOutputClearTriggerAudit.json).
