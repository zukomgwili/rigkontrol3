# Offline NI IPC contract proof on ARM

Beads authority: **rigkontrol3-40h.20**. Original contract:
[NI IPC framing and OS-port lifecycle](../../ReverseEngineering/NIIPCTransport.md),
recovered from NIHardwareAgent 1.7.4/R199. This directory is an isolated proof;
it is not linked into the driver, application or installer.

From the repository root on an Apple Silicon Mac, run:

```sh
python3 Tools/NIIPCContract/check.py --negative-controls
```

The runner builds explicit arm64 executables with strict C11 warnings, then
ASan/UBSan, and executes both suites. It preserves source snapshots, command
outputs, binary architecture/import inventories, hashes and results in a new
ignored `.build/ni/ipc-arm-contract/` directory. It rejects an x86/Rosetta host
or binary. Existing evidence is never overwritten.

## What is reconstructed

`transport.c` translates the recovered NHL send wrapper and lower OS send and
receive callback boundaries. It uses Apple's native CoreFoundation types and
real CFData/CFString objects in the fixtures, with every message-port outcome
injected. There is no implementation that calls a live message port.

Literal vectors check the excluded 12-byte internal prefix, full first-word
message ID, unaligned fields, short sends without an invented four-byte guard,
reply selection, original 2-second timeouts, signed size comparisons,
copy/release ownership and empty CFData replies. Other fixtures check the
original stored-name path's `strlen`, signed byte widening, wrapping 32-bit
allocation-count arithmetic and send-time invalidation notification. Expected
bytes and branches come from the reviewed assembly contract, not a second
copy of the implementation algorithm.

`lifecycle.c` translates the observed public/OS guards and operation order into
**semantic state and typed commands**. An observer records commands; it never
executes them. Cases cover remote/local creation, startup acknowledgment before
listener scheduling, common run-loop modes, the two stop paths, invalidation,
active-close guards, owner detachment and qualified destructor dispatch.
GP calls, map locking and virtual slots remain opaque. The priority constructor
receives raw enum 4; its unread conversion result is a separate injected value.
Raw event-wait argument 5000 has no asserted units or scheduling guarantee.

Each suite uses literal independent cases at these agreed boundaries. Source
comments identify the original addresses. Test-first failing checks preceded
the translations; temporary-copy negative controls additionally ensure that
specific framing and lifecycle deviations cannot pass the suite. A mutant must
compile and fail an ordinary behavioral assertion; a compiler failure or crash
does not count as detecting it.

## Necessary representation changes

The proof rebuilds typed C code for ARM rather than executing x86 instructions
or copying NI's C++/Boost object layout. Compile-time checks require the
recovered little-endian LP64 widths. `memcpy` loads preserve unaligned 32-bit
fields and signed 64-bit bit patterns without undefined typed pointer access.
The semantic lifecycle records are deliberately separate from NI's object ABI.
These are proof representations, not changes to NI's wire format, timeouts,
retry policy, audio ring, timing or recovery.

## Verified result — 2026-10-25

| Check | Result |
| --- | --- |
| Strict native ARM64 | 49 transport scenarios, 18 lifecycle cases pass |
| ASan + UBSan | Same suites pass with no sanitizer report |
| Semantic negative controls | All nine compile, then fail the expected assertion |
| Binary inventory | All 13 binaries are arm64; forbidden direct imports absent |
| Source snapshot | All seven compiled/runner files unchanged during execution |
| Independent review | Fidelity and implementation/evidence reviews pass |

The accepted run is
`.build/ni/ipc-arm-contract/20261025T191947Z-fc78a356/summary.json`, SHA256
`e54ac593706cc755117dfa8fcf3d44a1ea66b9fd09abd020c60f65fc20d7c9b7`.
It records 69 commands with output hashes and supersedes the preliminary run.
Private test-first logs, including both review regressions, are preserved in
`.build/ni/ipc-arm-contract/tdd-evidence/`; its manifest SHA256 is
`2481c451bb2d2c72ae85355d4a2cb4513001301961d48f630620fcaf78cbe33f`.
The null-candidate RED reproduced a sanitizer fault; it is a regression
reproduction, not one of the nine assertion-based negative controls.

## Limits

No CFMessagePort registration/lookup/send, run-loop execution, actual thread
creation, NI application, USB/audio/MIDI operation, driver installation,
signing/profile or security change occurs. The runner checks direct undefined
symbols and records linked libraries; this is an import guard, not proof of
every internal operation performed by Apple's data/string framework.

Byte spans must have physically valid backing memory, as the original
unguarded boundary requires. Short sends have four readable ID bytes even
when fewer are transmitted. Allocation-count overflow is tested arithmetically
without allocating an invalid span. Allocation failures, C++ exception paths,
actual GP scheduling/deferred cleanup, map concurrency and exact virtual-slot
targets are outside this proof.

Receive coverage is the **OS callback**. The NHL receive wrapper's internal
length/reply-field dispatch and the constructor-to-registered-buffer connection
are not reconstructed here. Full NI agent source, application handshake,
startup identity/signature, driver access, actual NI recognition and live port
behavior remain required in **rigkontrol3-40h.2 / rigkontrol3-40h.17**.
SIP remains enabled; no production binding is admitted by this proof.
