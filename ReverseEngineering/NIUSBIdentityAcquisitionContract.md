# Physical USB identity acquisition contract

Date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.25**, under `.2`.

**The physical-identity producer is not implementation-ready.** The existing
NI evidence proves the required calls and consumers. Primary Apple source
supplies a concrete older reader implementation and matching relative method
order, but does not yet prove the six absolute NI slot identities or bridge
that implementation to the NI build's historical runtime. These are explicit
source gates, not permission to invent descriptor decoding or an identity.

This report records source research only. No production/test code, build,
executable fixture, USB/device/registry operation, NI application, installed
bundle or security setting changed. SIP and NI audio timing/recovery stay fixed.
[Selected owner](NIARMRecognitionBindingDecision.md),
[NI field/serial proof](NIIdentitySpecificationContract.md),
[completed specification operation](NativeUSB.md#original-specification-snapshot-spec14).

## What the existing NI evidence establishes

The original S3 writer obtains two low16 words through receiver slots
`+0x8f0/+0x8f8`. Its serial/product/vendor sources are provider buffers
`+0x178/+0x138/+0x158`, copied with `strncpy32`; S3's four gaps remain unwritten.
The selected `initUSB` window obtains product/vendor/serial indices at
`+0x918/+0x910/+0x920`, then calls `+0x958(index,destination,31,0x0409)` in that
order and ignores all three returns. The window contains no explicit NUL or
zeroing operation. [Original calls and coverage](NIIdentitySpecificationContract.md#selector3-all110-output-bytes).

The actual instance-query string comes from this serial storage through the
retained agent's proved chain; native UID, fixed labels and registry IDs are
not substitutes. Named but unrecovered GP/string conversion leaves remain
separate. [Original serial chain](NIIdentitySpecificationContract.md#concrete-real-serial-query-chain).

Root's reused NI package evidence identifies SDK `macosx10.9`/`13C64`, build
`13F34`, RK3 `IOProviderClass=IOUSBDevice`, and an IOUSBFamily minimum of `1.8`.
The latter is a minimum dependency, not an exact runtime or source-version pin.
Original `audio-Info.plist` SHA256:
`0e6ba261d8d6884bdcd848c940025815a887547a1bc287f37f86ab9080e2fb44`.
The reviewed root packet links receiver field`+0xe0` to a nonnull
`safeMetaCast` against `IOUSBDevice::metaClass`. This establishes the checked
base-class relation; subclasses remain possible. It supplies neither an
absolute vtable-slot layout nor proof that the actual runtime uses an
unoverridden base implementation. The bounded head is reused existing ASM,
with the report's original coverage qualifications. Root's separately admitted
packet resolves only the selected original relocation associations; this
research strand performed no fresh NI binary/metadata/instruction extraction.

## Primary Apple evidence and the remaining ABI gate

Apple's version-pinned IOUSBDevice declaration has this exact relative sequence:

| NI raw slot | Apple declaration candidate; not an absolute-slot proof |
| --- | --- |
| `8f0` | `UInt16 GetVendorID()` |
| `8f8` | `UInt16 GetProductID()` |
| `910` | `UInt8 GetManufacturerStringIndex()` |
| `918` | `UInt8 GetProductStringIndex()` |
| `920` | `UInt8 GetSerialNumberStringIndex()` |
| `958` | `IOReturn GetStringDescriptor(UInt8,char*,int,UInt16)` |

Between these declarations are exactly the intervening methods implied by
those eight-byte spacings: release, configuration count, pipe zero, MakePipe,
two DeviceRequest overloads, configuration and device status. This is a
**relative order/signature match**, not an inferred inherited-vtable origin.
[540 declarations](https://github.com/apple-oss-distributions/IOUSBFamily/blob/21a163aef2192f568a11bcd2eded73dff6ed3290/IOUSBFamily/Headers/IOUSBDevice.h#L271-L346),
[630 declarations](https://github.com/apple-oss-distributions/IOUSBFamily/blob/1398331b04a6bb9ea9b9f76248b8b584811ebcd0/IOUSBFamily/Headers/IOUSBDevice.h#L272-L347).

The separately admitted declaration-prefix inventory uses IOUSBFamily630.4.5
and XNU2422.115.4, with the actual chain IOUSBDevice→IOUSBNub→IOService→
IORegistryEntry→OSObject→OSMetaClassBase. It counts **265+D** inherited function
entries, where D is the unproved number of callable destructor components,
plus19 new IOUSBDevice declarations before GetVendorID. Only the conditional
D=2/function-index0 address-point model yields indices286/287/290/291/292/299,
matching the recorded operands. The fit is not independent proof of that
compiler ABI or address point. This XNU tag is a declaration model, not proof
of the exact SDK header set used for NI. No compiler, generated vtable or
ancestor implementation was used. [Inherited declaration](https://github.com/apple-oss-distributions/xnu/blob/xnu-2422.115.4/iokit/IOKit/IOService.h),
[macros and base declaration](https://github.com/apple-oss-distributions/xnu/blob/xnu-2422.115.4/libkern/libkern/c%2B%2B/OSMetaClass.h).

The complete inspected implementation is **IOUSBFamily-540.4.1**, commit
`21a163aef2192f568a11bcd2eded73dff6ed3290`. Apple's later **630.4.5**, commit
`1398331b04a6bb9ea9b9f76248b8b584811ebcd0`, retains the selected declarations
but publishes an empty `IOUSBDevice.cpp`. The official Git tree also records
that file as zero bytes. Declaration stability does not prove reader-body
stability. The already-fetched 458.4.1 reference reader differs from540 only by
a log statement; that likewise does not prove630 or NI-runtime behavior.
[Official tags](https://github.com/apple-oss-distributions/IOUSBFamily/tags),
[630 empty source](https://github.com/apple-oss-distributions/IOUSBFamily/blob/1398331b04a6bb9ea9b9f76248b8b584811ebcd0/IOUSBFamily/Classes/IOUSBDevice.cpp).

In540, the candidate word getters read cached descriptor fields and apply
`USBToHostWord`; index getters return the descriptor's `iManufacturer`,
`iProduct` and `iSerialNumber` as UInt8. No USB request appears in those getter
bodies. [540 getter implementations](https://github.com/apple-oss-distributions/IOUSBFamily/blob/21a163aef2192f568a11bcd2eded73dff6ed3290/IOUSBFamily/Classes/IOUSBDevice.cpp#L4673-L4725).

## Exact540 reader behavior, conditionally applicable

The following facts apply to the pinned540 implementation; they are **not yet
asserted as the implementation behind NI's raw `+0x958` call**.
[Reader body](https://github.com/apple-oss-distributions/IOUSBFamily/blob/21a163aef2192f568a11bcd2eded73dff6ed3290/IOUSBFamily/Classes/IOUSBDevice.cpp#L3206-L3375).

- The count is destination **bytes**. Below6 returns `kIOReturnBadArgument`
  before clearing it. Otherwise it clears exactly that count before I/O.
  Thus count31 clears bytes0–30; byte31 lies outside this operation.
- It sends standard IN/device GET_DESCRIPTOR for the supplied string index
  and language. The initial request is two bytes, with those descriptor bytes
  zeroed and `wLenDone=0`. `DeviceRequest(request,5000,0)` is synchronous:
  no-data timeout5000ms, completion timeout0, omitted callback.
  [Declared timeout/callback contract](https://github.com/apple-oss-distributions/IOUSBFamily/blob/21a163aef2192f568a11bcd2eded73dff6ed3290/IOUSBFamily/Headers/IOUSBDevice.h#L365-L386).
- Initial success or Overrun proceeds. Any other initial result selects a
  zeroed256-byte fallback request; fallback success or Underrun proceeds,
  otherwise that result returns. These are original reader branches, not a
  proposed new retry policy.
- `len=desc[0]`; zero returns success with the cleared destination. A non-string
  type returns DeviceError. An odd length clears the low bit of `desc[0]`,
  while the separate `len` used for the next request remains unchanged.
- It clears `len` descriptor bytes, requests `len`, and accepts only success
  on this final request. Short-success counts are logged rather than rejected.
  It checks type again and makes `desc[0]` even again. There is no separate
  special case for index0, missing strings, or a language-list descriptor.
- It endian-converts each little-endian UInt16 and encodes code units
  independently into1/2/3 bytes. Zero code unit stops. It does not compose or
  reject surrogate pairs. The header's ASCII description is less precise than
  this body. [Exact helpers](https://github.com/apple-oss-distributions/IOUSBFamily/blob/21a163aef2192f568a11bcd2eded73dff6ed3290/IOUSBFamily/Classes/IOUSBDevice.cpp#L3599-L3668).
- Whole encoded units are copied only while the cumulative size is at most
  count−1. For count31, at most30 bytes are written and the prior clearing
  leaves a NUL. Truncation still returns success. After the size guard, for
  count31, failure before conversion retains the cleared destination,
  including on a reused buffer.

There is no general guarantee for arbitrary inconsistent/malformed descriptors:
conversion trusts the second descriptor's own length, not a new bound based on
`wLenDone`. This research does not invent deterministic bytes for unsafe or
uninitialized paths, normalize malformed input, or replace code-unit conversion
with a standards-compliant Unicode codec. Upstream destination allocation and
state outside the exact cleared range remain distinct from this method. On
valid540 count31 paths, the NUL within bytes0–30 means S3's later `strncpy32`
need not inspect provider byte31. Any outside-write state is a further
prerequisite only if a later required observation actually depends on it.

## Source-derived reference cases, not executed tests

These cases specify the540 reference only, pending the ABI/runtime bridge.

| Case | Reference observation |
| --- | --- |
| Count5, destination prefilledA5 | BadArgument; destination unchanged. |
| Count31, returned initial length0 | Success; bytes0–30 zero, byte31 outside count unchanged. |
| Initial non-success/non-Overrun, fallback error | Requests2 then256; returns fallback error; destination0–30 remains zero. |
| Initial/final success, short `wLenDone` | Logging alone does not create an error; content-dependent branches remain authoritative. |
|29 ASCII `A` units followed by U+00E9 |29 `41` bytes; U+00E9 cannot fit whole; bytes29–30 remain zero; success. |
|15 U+00E9 units followed by another |30 bytes `c3 a9` repeated15 times, byte30 zero; success. |
| U+D83D then U+DE00 | Independent-unit bytes `ed a0 bd ed b8 80`, not a four-byte scalar encoding. |
| Embedded U+0000 | Conversion stops there; remaining counted destination retains zeros. |

No vector supplies a fabricated original GP conversion result or repairs the
later S3 writer's untouched gap bytes.

## Current binding disposition and stops

The current source supplies one noncompeting capture interval: on the existing
worker, immediately **after successful USBDeviceOpen and before the existing
SetConfiguration**. Finish acquisition there, then retain the current
SetConfiguration→USBDeviceClose→readback/check→child-discovery/open order.
NI's existing string window precedes its SetConfiguration call; an earlier
post-configuration candidate is withdrawn. No extra parent open, seizure,
parent/child simultaneous claim, or moved EP1/audio operation is selected.
[Current parent interval](../Driver/NativeUSB.c:707),
[original ordering](AudioDriver.md#usb-configuration-and-command-pipe-toggle-synchronization),
[child ownership](../Driver/NativeHostUSB.h:37).

Pinned SDK macosx27.0, Xcode27.1/27A9275, declares Interface300's UInt16 word
outputs, UInt8 string-index outputs and synchronous `DeviceRequestTO`; it has
no GetStringDescriptor decoder member. Its raw request has host-endian UInt16
request scalars, bus-endian caller data, UInt32 actual-byte count and two UInt32
millisecond timeouts. It can represent the540 reference's exact2/fallback256/
length sequence, language0409 and no-data5000/completion0, preserving explicit
scratch/destination handling. This is representability, not legacy equivalence.
No NSString/registry-string substitution or single-GET255 follows.
[Interface300 declaration][SDK-lib], [raw request fields][SDK-USB].

The installed declaration permits closed-device requests except possibly
state-changing ones; Apple's web and released630 declaration require open.
The selected already-open interval satisfies both and does not rely on closed
parent control access. Raw-request documentation supplies no Unicode, NUL,
zero-fill or untouched-on-error promise. Retain parent/buffers through the
synchronous return; do not infer a finite cancellation bound, add retries, or
normalize current errors into NI results. Acquisition adds initialization work;
only later cached GETs can promise zero USB requests. Live transfer/permission/
error/latency acceptance remains untested.
[Current Apple request API](https://developer.apple.com/documentation/iokit/iousbdeviceinterface182/1559217-devicerequestto),
[released open requirement](https://github.com/apple-oss-distributions/IOUSBFamily/blob/1398331b04a6bb9ea9b9f76248b8b584811ebcd0/IOUSBFamily/Headers/IOUSBLib.h#L1131).

The remaining essential source gates are an authoritative absolute
slot/receiver ABI association and applicability of the shared reader to the
selected supported original runtime/version. SDK10.9 does not itself select
runtime630; minimum1.8 is not a runtime pin. The required evidence is a
version-matched reader plus its two helpers and six-slot layout, or an explicit
validated reference-target disposition. No540 equivalence is inferred. Later
original/native error translation also remains required. Current declarations
do not close these gates.

The one bounded availability check used exactly two focused official-source
search queries and returned no package result. Apple's old tarball index
redirects to the official tags; the Apple download page was inaccessible via
the web tool, so package availability and authentication requirements remain
**unverified**, not disproved. No login, archive/binary download or further
source search occurred. [Source index](https://opensource.apple.com/tarballs/IOUSBFamily/),
[unchecked download catalogue](https://developer.apple.com/download/all/?q=IOUSBFamily).
If either remains missing, stop at that exact source boundary and record the
minimum additional evidence required in Beads before action. Do not broaden
into an inherited-framework survey, another backend, guessed conversion,
production capture/schema, new readiness gate or application stub. A later
capture/publication plan must preserve original unchecked/error behavior,
generation coherence, teardown/drain and cached-read zero-I/O behavior without
changing audio configuration, activation, mapping, calibration or LEDs.

## Private evidence and review

Ignored packet root: `.build/ni/usb-identity-contract/historical/`.
`fetch-manifest.json` records official URLs, versions, byte lengths and hashes;
`selected-source-evidence.json` hashes the selected line spans and limitations.
Root's unchanged NI packets are retained under
`.build/ni/usb-identity-contract/ni-metadata/`; its `manifest.json` SHA256 is
`ee5e0e0355708b687578c8a3bf8d9d57d7f61640dda94ea4a08bd8b76321ad21`.
The separate `absolute-prefix/` packet preserves the conditional ledger and
its source pins; it is not an emitted vtable or an absolute-slot proof.

| Artifact | SHA256 |
| --- | --- |
|540 header | `c058dbf2df51ca70bcf45bb31782ee199d608774a94fc6c07f57c6e4170bc8e6` |
|540 implementation | `2a144c076a5ce852c0a2940a50339bcccb07eba04592d69479f5eba68736e9aa` |
|630 header | `a1a90c783a850679317cccbb5d7106e0dfbd8523e492c0bfe204b0b653e2149d` |
|630 tree metadata | `c5e89538dda317372aede74431051cb239abc19b88714da0e786e9cb386a2ce4` |
|Selected span packet | `2fe23178e8228685731f8fcfc7973aac7e346d460f0da77582777bbb062d8eda` |
|Root selected relocation packetB | `201e2babc5f44a3483a299ba6b483337592918c3326097ea23e0ed2b4fac244a` |
|Root qualified reused receiver head | `b8961454d80c6bacb16f717648b36133e0d588ba3e8827ecb89f66f27a236b7f` |
|Conditional declaration-prefix evidence | `d4f033390beb0c8af0d734287c8f1db1cc6efc599f6f40de22c1cb2d770eb49a` |
|Declaration-count ledger | `c35ff7063a725dcfc6dd51e1f34b6780dd6dc693d2e874b7e5e07212d517d9a1` |

Current API packet `.build/ni/usb-identity-contract/current-api/` records
static SDK/version sources and selected excerpts. Its evidence JSON SHA256 is
`63975eda0c5376056f398d8be40148c7386c2d54a53e21852de86fec6e3ed745`.
Installed IOUSBLib.h SHA256:
`16acc674ebc759d00904e8ae1cd2137b5f814d54fe237c5193cf6549131d542a`;
USB.h SHA256:
`db2fd1e52d669e4367e30bef6a099d6a3708c3a7b615cc1ece791daff46376d5`.
Apple's released IOUSBDeviceClass.cpp is also empty; it supplies no current ARM
implementation proof. [Released empty implementation](https://github.com/apple-oss-distributions/IOUSBFamily/blob/1398331b04a6bb9ea9b9f76248b8b584811ebcd0/IOUSBLib/Classes/IOUSBDeviceClass.cpp).

Primary-source provenance, selected spans and the root receiver packets have
independent technical review. Current-API integration has a separate read-only
review. The bounded run ends with the required absolute-ABI and selected-runtime
reader-applicability gates **still unproved**; `.25` remains incomplete. The540
reference source, current raw primitive and conditional declaration fit do not
supply the missing NI-target proof. No physical-identity parity, capture-code
readiness or NI recognition is claimed.

[SDK-lib]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOKit.framework/Headers/usb/IOUSBLib.h:1618>
[SDK-USB]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOKit.framework/Headers/usb/USB.h:687>
