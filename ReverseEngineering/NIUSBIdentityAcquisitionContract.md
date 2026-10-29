# Physical USB identity acquisition contract

Source-contract completion: **2026-10-26 UTC**. Beads: **rigkontrol3-40h.25**,
under `.2`. The 2026-10-25 partial assessment and plans remain historical evidence.

**The absolute six-slot ABI and reader-applicability gaps are resolved for one
validated original reference: OS X 10.9.5, Apple IOUSBFamily 686.4.1.** Primary
NI compatibility evidence selects the supported 10.9 family; Apple's signed
update supplies the actual component, emitted table and reader. The older 540
source is no longer used as a substitute for the target implementation.

This closes the bounded source gate, not physical capture code or live NI
recognition. It does not establish identical behavior on every old OS or
arbitrary subclass override. No driver/test code, build, installation, NI
application, endpoint, USB operation or security setting changed. The official
update was read-only mounted for static extraction and the owned mount was
detached; no installer scripts ran. SIP and NI audio timing/recovery stay fixed.
[Selected owner](NIARMRecognitionBindingDecision.md),
[NI field/serial proof](NIIdentitySpecificationContract.md),
[completed SPEC14 operation](NativeUSB.md#original-specification-snapshot-spec14).

## Supported reference and primary provenance

NI's download listing identifies RK3 Driver 2.8.0 for 10.9–10.11 and 2.7.0 for 10.8.
Its hardware table explicitly recommends 2.8.0 under 10.9/10.10 and requires
Controller Editor 1.7.4 or lower. We select 10.9.5 within that supported 10.9
family; the pages do not separately certify every patch or every runtime.
This is an explicit supported-reference choice, not SDK/date inference.
[NI downloads](https://support.native-instruments.com/support/solutions/articles/69000882238-native-instruments-drivers-other-files),
[NI compatibility](https://support.native-instruments.com/support/solutions/articles/69000879296-compatibility-of-native-instruments-products-on-macos).

Apple's first-party 10.9.5 Combo download contains package identifier
`com.apple.pkg.update.os.10.9.5.13F34.combo`. Its verified Apple Software
signature has a trusted 2019 timestamp. The selected component's Info.plist
identifies `com.apple.iokit.IOUSBFamily`, version **686.4.1**, compatible version
**1.8**; NI's previously reviewed dependency also requires 1.8. This directly
links a supported reference and compatible component, without equating a
minimum dependency with a runtime version. [Apple download](https://support.apple.com/en-us/106413).

| Exact artifact | Bytes / SHA256 |
| --- | --- |
| Official `OSXUpdCombo10.9.5.dmg` | 982148796 / `a1623a91524cf086cb7689fc0b4b0a1cc8311435662f8a5b9d701d0fd2ed2b35` |
| Signed package | 982693511 / `c3b9b560a46c7f9322913a624167a5f03bd3c4d12a28320edf19b8504a629519` |
| Exact bzip2 Payload entry | 979476117 / `def896fec4aa17c2ba46437cc5b70f0760aaa0238958e9f38f1a76d320cac82b` |
| Selected Info.plist | 1839 / `6288334b76405821f1d7b6c691786dc877dcaa5afce936b28e2eb5727c8493f9` |
| Selected IOUSBFamily executable | 494048 / `af352cc23b086367c16124d023d7cc0b10d0ceef44de0fa7ddb91a527786f773` |

Only those two component files were extracted: 495887 bytes total. Component
SDK 13F17/Xcode 0501, NI's SDK 10.9/13C64/Xcode 0511 and the update's OS identifier
13F34 are distinct facts. They are not proof of identical compiler patches.
The public 630 source's empty implementation and the 10.9 source-manifest
omission remain negative research history; neither is used to invent behavior.

## Original NI receiver and call order

Reused NI evidence links provider field `+0xe0` to a nonnull safeMetaCast against
`IOUSBDevice::metaClass`; subclasses remain possible. Original S3 takes two
low 16 bits returned at `+8f0/+8f8`. The `initUSB` string window obtains
product/vendor/serial indices at `+918/+910/+920` and calls `+958` in that order
with destination buffers `+138/+158/+178`, count **31**, language **0409**.
All three returns are ignored, and these calls precede SetConfiguration.
The real instance-query string ultimately derives from this serial storage,
not native UID, registry ID or a generated label.
[Original producer/consumer contract](NIIdentitySpecificationContract.md#selector3-all110-output-bytes),
[real serial chain](NIIdentitySpecificationContract.md#concrete-real-serial-query-chain).

## Absolute ABI and actual selected table

Primary Apple Clang 503.38 source establishes the 5.1 family rule: complete and
deleting destructor components, declaration/override indexing, an address
point after metadata, and an AppleKext final zero rather than an added slot at
every ancestor. The exact NI 5.1.1 patch is not proved from that source alone.
[Apple table rule](https://github.com/apple-oss-distributions/clang/blob/9cf9295528348ccacefcc90175be2c189d667f93/src/tools/clang/lib/AST/VTableBuilder.cpp#L1317),
[address-point construction](https://github.com/apple-oss-distributions/clang/blob/9cf9295528348ccacefcc90175be2c189d667f93/src/tools/clang/lib/CodeGen/ItaniumCXXABI.cpp#L967).

The selected emitted component supplies an independent absolute anchor:
C1 at `1434b` computes `14352+2715e=3b4b0`, then stores that pointer into saved
input `this` at `14352`. The original ZTV symbol is `3b4a0`, so the actual stored
address point is ZTV+16. At relative 0/8, the two cells target D1 `1429e` and
D0 `1427c`. This does not solve for destructor count from a desired numeric fit.
The constructor tail is bound to `OSMetaClass::instanceConstructed`; its body
and final side effects were not expanded.

| Offset from stored vptr | Actual cell → target VA | Method / declared type; selected body source |
| --- | --- | --- |
| `8f0` | `3bda0 → 1b154` | GetVendorID, UInt16; zero-extends object word `+a8`. |
| `8f8` | `3bda8 → 1b162` | GetProductID, UInt16; zero-extends word `+aa`. |
| `910` | `3bdc0 → 1b1c4` | GetManufacturerStringIndex, UInt8; zero-extends byte `+ae`. |
| `918` | `3bdc8 → 1b1d2` | GetProductStringIndex, UInt8; zero-extends byte `+af`. |
| `920` | `3bdd0 → 1b1e0` | GetSerialNumberStringIndex, UInt8; zero-extends byte `+b0`. |
| `958` | `3be08 → 1984e` | GetStringDescriptor(UInt8,char*,int,UInt16) → IOReturn. |

Original nlist identities, exact cells and constructor bytes agree. The five
getters perform cached descriptor reads without USB/callee operations or an
error-return branch. Their offsets describe the old object, not an ARM layout
to transplant. [Published typed declarations](https://github.com/apple-oss-distributions/IOUSBFamily/blob/1398331b04a6bb9ea9b9f76248b8b584811ebcd0/IOUSBFamily/Headers/IOUSBDevice.h#L272-L347).

This resolves the selected compatible IOUSBDevice ABI. NI's polymorphic call
still permits overrides; the proof does not claim a live object's exact class,
all-runtime implementation equality or exact NI compiler-patch identity.

## Exact 686 reader, including errors and retained writes

Reader `1984e..19b18` is 714 bytes. Four call relocations prove bzero; one proves
bcopy(source, destination, count). The three indirect request calls use actual
slot `+978`, cell `3be28 → 1a25c`, bound to
`DeviceRequest(IOUSBDevRequest*,unsigned int,unsigned int,IOUSBCompletion*)`.
The stack guard/failure imports are separately identified, not invented string
policy. No lower request, bzero, bcopy or guard body was expanded.

1. Signed destination count < 6 returns `e00002c2` before clearing. Otherwise
   clear exactly count bytes before I/O. NI's 31 therefore clears bytes 0–30;
   byte 31 is outside that operation. The count bounds **output bytes**, not
   the descriptor's USB request length.
2. Build standard IN/device string GET_DESCRIPTOR: type 80, request 06,
   value `0300|index`, language verbatim, scratch pointer and actual-count 0.
   First request length 2 follows scratch 2 clearing. Each request delegates
   with no-data timeout 5000 ms, completion timeout 0, null callback/synchronous
   operation. No finite overall/cancellation bound is inferred.
   [Typed timeout contract](https://github.com/apple-oss-distributions/IOUSBFamily/blob/1398331b04a6bb9ea9b9f76248b8b584811ebcd0/IOUSBFamily/Headers/IOUSBDevice.h#L366-L387).
3. First status 0 proceeds. Literal `e00002e8` proceeds **only if type byte 3**;
   every other nonzero case selects the 256-byte zeroed-scratch fallback.
   Fallback status 0 or `e00002e7` proceeds; another result returns unchanged.
   This type-dependent first-error branch differs from the older 540 source.
4. Read initial length L. L == 0 returns 0 before the common type check. Otherwise
   type != 3 returns `e00002e9`. An odd header length is masked in storage, while
   original L remains the next request length. Clear exactly L scratch bytes
   and request L; final request requires status 0 or returns its error unchanged.
5. Final type != 3 returns `e00002e9`. Make final length even. At `19a68..19a73`,
   signed `(even_length−2)<2` returns 0: lengths 0/2 need no conversion. Do not
   carry the older 540 empty/underflow behavior into this reference.
6. Otherwise visit `(even_length−2)/2` little-endian U16 units. The direct
   helper returns 0 for NUL, 1 for 1..007f, 2 for 0080..07ff and 3 for 0800..ffff.
   It neither composes nor rejects surrogate pairs. Append whole encoded units
   only while cumulative bytes <= count−1; NUL, truncation and normal end return 0.
   `SwapUniWords` was decoded but is **not called by this reader**; the x86
   reader loads units directly. No modern Unicode normalization is justified.

`wLenDone` is reset but never used by the actual reader for equality/length
validation or compiled logging. Index 0 has no special skip/empty-label branch.
For valid count 31, conversion copies at most 30 bytes, leaving an in-range NUL;
failure before conversion retains the cleared 31 bytes, including in reused
storage. Original S3's subsequent 32-byte strncpy need not observe provider byte 31;
its own four gap bytes remain untouched. No whole-object zeroing is inferred.

Inconsistent final descriptor lengths can reference scratch beyond the bytes
actually transferred/cleared. There is no new actual-span guard in this body.
Do not fabricate deterministic uninitialized/malformed outputs or add repairs
as if they were original behavior. Typed raw-transfer errors/partial writes,
invalid pointers and lower USB-stack behavior are not expanded into invented
vectors or a new retry/readiness/error policy.

## Source-derived conformance cases, not executed tests

Use valid allocated destination storage; count 31 fixtures start with at least
32 A5 bytes and supply the stated response data. E denotes the stated nonzero
request error. These are selected-reference oracles, not observed RK3 data.

| Supplied case | Required selected 686 observation |
| --- | --- |
| Count 5 | `e00002c2`; destination unchanged. |
| Initial status 0/header length 0, even type 0 | One 2-byte request; success; bytes 0–30 zero, byte 31 unchanged. |
| Initial `e00002e8`, header `04 03`; final success `04 03 41 00` | Requests 2 then 4; no 256 fallback; `41` then zeros. |
| Initial `e00002e8`, header `04 00`; fallback error E other than `e00002e7` | Requests 2 then 256; return E, destination bytes 0–30 zero. A 540-based immediate type error is wrong. |
| Initial status 0/nonzero length/type 0 | `e00002e9`; no 256/final request. |
| Initial result other than 0 or `e00002e8`; fallback `e00002e7`/zero length | Requests 2 then 256; return 0 from zero-length branch. |
| Initial valid L=5; final success/type 3/length 5, supplied bytes | Request 5, not 4; final conversion length 4 means one U16. |
| Initial valid L=4; final success/type 3/length 0 or 2 | Signed guard returns 0 without conversion. |
| Final request error E | Return E unchanged; counted destination remains zero. |
| Final success/count mismatch, fully supplied coherent data | No actual-count equality rejection; normal header/unit rules apply. |
| 29 ASCII A units followed by U+00e9 | 29 `41` bytes; whole next unit does not fit; bytes 29/30 zero; success. |
| 15 U+00e9 units followed by another | 30 bytes `c3 a9` repeated 15 times; byte 30 zero; success. |
| U+d83d then U+de00 | `ed a0 bd ed b8 80`, not a 4-byte scalar encoding. |
| Index 0, supplied `04 03 09 04` response | Same algorithm; U+0409 becomes `d0 89`, not an invented empty value. |
| Embedded U+0000 | Stops conversion; remaining counted destination stays zero. |

## One current ARM capture seam; implementation remains separate

Use the existing worker's retained IOUSBDeviceInterface300, immediately **after
successful existing USBDeviceOpen and before existing SetConfiguration**.
Acquire in product→vendor→serial order, then retain existing
SetConfiguration→USBDeviceClose→readback/check→child-discovery/open order.
No added open, seizure, competing parent/child claim, or moved EP1/audio work
is selected. [Owner interval](../Driver/NativeUSB.c:707),
[original ordering](AudioDriver.md#usb-configuration-and-command-pipe-toggle-synchronization),
[child ownership](../Driver/NativeHostUSB.h:37).

Pinned SDK macosx27.0/Xcode 27.1 declares UInt16 descriptor-word outputs,
UInt8 index outputs and synchronous DeviceRequestTO; there is no text-decoder
member. Its host-endian scalars, bus-endian caller buffer, actual count and
5000/0 timeout fields can represent the proved request sequence and byte
transformation. No NSString/registry-string/single-GET255 substitution follows.
[Interface300][SDK-lib], [raw request][SDK-USB].

Installed and released/web open requirements differ; the chosen already-open
interval satisfies both. Raw API documentation promises no automatic text
conversion, NUL, zero fill or untouched-on-error output. Preserve the proved
scratch/destination behavior explicitly, retain buffers/parent through return,
and do not invent an overall timeout, retry, abort or readiness gate.
[Apple request API](https://developer.apple.com/documentation/iokit/iousbdeviceinterface182/1559217-devicerequestto).

A later, separately recorded code plan must define physical-metadata
availability/partial/error shape, native-versus-NI status mapping, atomic
same-attempt generation publication and revoke/drain/lifetime rules. It must
preserve ignored string returns and original downstream connection behavior,
prevent mixed/stale metadata, and test the actual client→HAL→owner seam.
Acquisition adds initialization work; subsequent cached GETs add zero USB I/O.
Current transfer/permission/latency acceptance, GP conversion, agent/events/
policy and unchanged-NI application recognition remain open under `.2`.
No new property/schema or code is admitted automatically by this source proof.

## Evidence discipline and review

Private root: `.build/ni/usb-identity-contract/proof-resolution/`. The partial
report is retained at commit 3934861 and as an ignored hash-pinned copy. Only
prose/pins are committed; no proprietary listing or downloaded source dump is.

| Reviewed packet | SHA256 |
| --- | --- |
| NI support/reference evidence | `b732a5ba871f4bd955426ce1d25454ae93c4492bac29969fea1624ea557681c4` |
| Apple component provenance | `9276ad137946d9b71615f6c5d6204e4b1ed36305d9cbeb2bdb2e129803f045c1` |
| Apple 5.1-family ABI source supplement | `a7f4e0839a54d64cc0d52abf3d55319dbdd2e128d3589e270a75c0c48fdff947` |
| C exact raw/decode packet | `d35110930c19f2a3d1226a7f194f1828206e82459130afe851a7468981a9322b` |
| D exact call/data bindings | `0f9a00894096d83dbe526e7edba33494117da729f9c0112dc8b5123e033c864b` |
| Actual constructor/table interpretation | `d693670deaee6e77033131b1d8ce98ca002b9cbbbdc79ab816d08251ae10fa54` |
| Selected 686 semantic notes | `11b86f1abb8dbd16b6442cad1434f74ee161dc42ad78756be3f845f015a4d9e5` |

C covers exactly 10 qwords/80 bytes and 9 disjoint function intervals/988 bytes,
261 full-span instructions, with raw-byte/contiguous-coverage/source guards.
D adds only 8 selected relocation associations and one 8-byte request cell;
no new instruction body. This executable has no LC_FUNCTION_STARTS: caps use
original next-defined-symbol and file-backed-text containment, not fabricated
LC bounds or a whole-program function proof. Bounded LLVM only: no decompiler,
automatic callee expansion or compiler build. The initial unsupported raw-byte
flag failure is preserved; its corrected retry decoded the same admitted caps.

Independent technical and selected-reference semantic reviews passed for these
packets and all 15 source oracles. Source-gate closure applies to this validated
reference; it claims no installation, live capture or complete NI functionality.

[SDK-lib]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOKit.framework/Headers/usb/IOUSBLib.h:1618>
[SDK-USB]: </Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk/System/Library/Frameworks/IOKit.framework/Headers/usb/USB.h:687>
