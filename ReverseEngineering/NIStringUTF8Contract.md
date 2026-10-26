# NI StringUTF8 conversion and owned information strings

Date: **2026-10-26**. Beads authority: **rigkontrol3-40h.29**.
Baseline: `51c58ad`. This is a recovered source contract, not an ARM
implementation, runtime test or NI application-recognition result.

**Retain NI's CoreFoundation UTF-8 conversion calls and counted, owned UTF-16
copies.** The actual information getter copies the converted vendor, product
and serial into its existing fields, then destroys each temporary. A custom
Unicode decoder, borrowed string pointer or ownership transfer into those
fields would change the recovered behavior.

The selected conversion and destination-assignment chain is now bounded and
recovered. No further GP or allocator implementation recovery is required for
this operation. Native-to-NI status/generation mapping and its exact ARM code
plan remain separate prerequisites under `.2`. Outgoing `wstrToUTF8`, session,
events, policy and full recognition are not completed by this result.
[Selected binding](NIARMRecognitionBindingDecision.md),
[information provenance](NIIdentitySpecificationContract.md),
[pre-code decision](NIOwnerAdapterImplementationPlan.md).

## Original source and finite coverage

The source is NIHardwareAgent **1.7.4/R199**, original base `0x100000000`, SHA256
`f0ae68ad29ed57163a8be022f3e60ca5a08fbf1ae47621129eb86e52fa134258`.
Original `LC_FUNCTION_STARTS` contains 11,312 entries; its SHA256 is
`ccc1c7d6c1b95caddc11728c7953bb4380e288c0a0d6dd51ba28d51d4eb41253`.

Every packet had its metadata, exact bounds and tooling recorded in Beads and
independently reviewed before the selected bytes were decoded. LLVM decoded
only these disjoint original caps, with exact source-byte, contiguous-coverage
and full-instruction-span checks. A next-start cap is not a claim that padding
is part of the logical function. No decompiler, Ghidra analyzer/project change
or automatic callee expansion occurred.

| Packet | Original entry → next-start cap | Bytes | Decoded instructions |
| --- | --- | ---: | ---: |
| B1: StringUTF8 C1 | `1002ae6f0 → 1002ae700` | 16 | 5 |
| B2: StringUTF8 C2 | `1002ae700 → 1002ae7e0` | 224 | 60 |
| C3: StringCF C2 | `1002ae7f0 → 1002ae9d0` | 480 | 142 |
| D1: const-U16 iterator assignment | `1002af510 → 1002af6e0` | 464 | 131 |
| D2: mutable-U16 iterator assignment | `1002af6e0 → 1002af8b0` | 464 | 131 |
| E: actual information getter | `1002f0970 → 1002f0bb0` | 576 | 126 |
| F1: U16 pointer/count assignment | `100007f40 → 100008140` | 512 | 142 |
| G: required growth/replacement | `100008140 → 100008450` | 784 | 215 |
| **Total** | **8 functions** | **3,520** | **952** |

Selected data coverage is **188 unique bytes**: the two constructor cleanup
tables (32/44), the information-getter cleanup table (96), and the actual
16-byte initialization pattern. Original indirect-symbol/nlist metadata binds
16 required imports; no stub bodies, GOT pointer contents, type-information,
personality, unwinder or allocator implementations were decoded.

The old 149-function export was used only for the information-getter's source
and inventory provenance; its C output was not interpreted. The fresh bounded
LLVM packet establishes this caller's full selected instruction spans.

## C-string construction and CoreFoundation boundary

C1 forwards its arguments to C2. C2 initializes its destination representation,
loads the first pointer from `CharSequence<char>` and calls the original import:

`CFStringCreateWithCStringNoCopy(NULL, pointer, 0x08000100, kCFAllocatorNull)`.

It does **not load the CharSequence count**. The caller computed that count
using the original `strlen`, but it is not a new byte limit for this constructor.
The actual NI acquisition path supplies the in-range NUL proved for the
selected 686 reader; it is not permission to accept arbitrary invalid pointers.
[Original reader](NIUSBIdentityAcquisitionContract.md#exact-686-reader-including-errors-and-retained-writes).

Original import metadata proves the function and allocator symbol; the SDK
identifies `0x08000100` as `kCFStringEncodingUTF8`. Apple's API contract requires
a NUL-terminated C string and removes its terminator from the result. Creation
can return NULL. Passing `kCFAllocatorNull` prevents CF from freeing the caller's
buffer; the no-copy API can still make a copy. The input must remain alive and
unchanged through the CF object's lifetime.
[Apple constructor contract](https://developer.apple.com/documentation/corefoundation/cfstringcreatewithcstringnocopy%28_%3A_%3A_%3A_%3A%29).

A NULL result skips StringCF construction and leaves C2's initialized empty
representation. This constructor has no Boolean failure return. On normal
information-getter completion, such an empty conversion can therefore replace
an existing field while the getter still returns true. Do not turn it into a
new connection abort, substitute the native UID or introduce replacement text.

A nonnull result enters StringCF, transfers its resulting owned representation
into the fresh StringUTF8 destination, destroys the displaced temporary state,
and calls `CFRelease` on the CF object. This internal construction transfer is
distinct from the later **copy into the information fields**.

## UTF-16 materialization and ownership

The original StringCF path asks `CFStringGetLength` for the number of 16-bit
units. Its unsigned maximum-size check reaches the original length-error
import. Short storage is selected below 11 units; larger storage uses the
original allocation/rounding path. It initially fills the active units with
**U+0020 SPACE**, not zero, and writes a U16 terminator. The 16-byte constant and
`memset_pattern16` import were separately proved.

For a nonnull CF object it then calls `CFStringGetCharactersPtr`:

- A nonnull result is copied through the const-U16 iterator assignment.
- A NULL result with nonzero length uses `CFStringGetCharacters` over range
  `(0, length)`, followed by the mutable-U16 iterator assignment. The original
  temporary is on the stack through 65,536 units inclusive; larger lengths use
  an array allocation and normal-path array deletion.
- A zero-length fallback needs no character extraction. Empty storage still
  has its explicit terminator.

Both assignments copy U16 values into destination-owned storage, append their
own terminator and set length. They do not retain the CF pointer or temporary
buffer. Their selected preallocated-string path needs no further library
callee. `CharactersPtr` returning NULL is an ordinary storage/access choice,
not a new invalid-string error.
[Apple pointer-access contract](https://developer.apple.com/documentation/corefoundation/cfstringgetcharactersptr%28_%3A%29),
[Apple range-copy contract](https://developer.apple.com/documentation/corefoundation/cfstringgetcharacters%28_%3A_%3A_%3A%29).

The exact primary header pins are retained in local evidence. `CFIndex` is a
signed 64-bit type on the selected macOS LP64 ABI, `UniChar` is 16-bit, and the
actual instantiated destination type is `basic_string<char16_t>`. Preserve typed
unit copying; do not reinterpret NI's 24-byte x86 string representation as an
ARM wire record or assume unused object/padding bytes are initialized. A direct
NULL StringCF input only writes the observed empty tag/terminator, rather than
zeroing the whole object.

CF decoding and standard allocation remain the **same typed platform API
boundaries used by NI**. This proof does not decode their private implementations
or assert equality of every old/current framework error policy. It supplies no
invented malformed-UTF-8 output vector, normalization, surrogate repair or new
allocator implementation.

## Actual information-field replacement

The original getter returns false for handle 0 or a false S3-helper result,
before assigning the output. Success writes its three U16 header words first.
With strings disabled, existing string fields remain untouched. The actual
const getter requests strings enabled.

With strings enabled it processes **vendor → product → serial**, using output
offsets `+8`, `+0x20`, `+0x38`. Each stage constructs a StringUTF8 temporary,
extracts its U16 pointer/count, calls the same counted assignment and conditionally
deletes the temporary's owned allocation before beginning the next stage.
The destination remains valid after that temporary is destroyed.

When destination capacity suffices, counted assignment selects its own inline
or heap buffer, performs direction-aware copying, writes a U16 NUL and updates
length. Equal pointers skip copying; count zero still clears/terminates the
logical value. It does not borrow the conversion temporary. This operation's
alias handling is distinct from the forward iterator-copy helpers; no blanket
arbitrary-pointer or invalid-alias guarantee is inferred.

Growth is necessary on the actual path: an initially short destination has
capacity 10 while the acquired strings can require more units. The recovered
growth helper checks length, allocates a new buffer, copies the replacement
units, then deletes old heap storage and commits the new pointer/capacity/length
and terminator. For the selected counted replacement, old prefix and suffix
counts are zero. Standard allocation/length-error exceptions before commit
leave the **current field's old value** intact. The getter's header and any
earlier completed fields have already changed; it is not a transactional update.

The original x86 capacity/tag offsets and vectorized copies are provenance,
not bytes to transplant into an ARM process. A later code plan must retain
owned values, copy/error/mutation order and native typed calls while explicitly
justifying ABI representation changes. It cannot replace copying with a shared
pointer, add a Unicode policy, swallow an allocation exception or add a new
metadata-success connection gate.

## Exact cleanup coverage

Selected original compact-unwind metadata binds the cleanup tables to these
functions. The tables use four-byte unsigned call-site fields (encoding 3)
and cleanup action 0. No type-information target or runtime personality body
was needed.

| Function | Protected interval | Cleanup landing |
| --- | --- | --- |
| StringUTF8 C2 | `1002ae732–1002ae7b4` | `1002ae7bd` |
| StringCF C2 | `1002ae8ae–1002ae983` | `1002ae9af` |
| Getter vendor assignment | `1002f0a3d–1002f0a42` | `1002f0b5b` |
| Getter product assignment | `1002f0aab–1002f0ab0` | `1002f0b70` |
| Getter serial handoff/assignment | `1002f0b1c–1002f0b24` | `1002f0b85` |

Intervals are start-inclusive/end-exclusive. Surrounding no-landing intervals
are retained in the evidence. Constructor landings conditionally delete their
owned string destination and resume unwinding. Each getter landing destroys
only its corresponding conversion temporary and resumes. It does not restore
header/earlier fields or convert the exception into a false return.

The normal CF release and large temporary-array deletion are not additional
operations in those constructor cleanup blocks. Preserve the observed cleanup
placement; do not silently add CF/array rollback as an ARM-support change.
This is selected static call-site coverage, not a claim that hardware testing
observed an allocation failure or that all exception machinery was recovered.

## Source-derived acceptance cases for the later port

These are **planned oracles**, not tests implemented or run in `.29`. Use valid
backing storage and source/API-defined outcomes; do not fabricate private CF
malformed-input or allocation behavior.

| Case | Required observation |
| --- | --- |
| Valid C string `RK3`, including a deliberately different CharSequence count | Original pointer/encoding/allocator API call; counted GP value is `0052 004b 0033`. The constructor must not acquire a new count limit. |
| C string `A`, NUL, then `B` in valid backing storage | C-string boundary consumes the prefix; no explicit-count replacement that includes the suffix. |
| API-defined valid UTF-8 data such as `c3 a9` or `f0 9f 98 80` | Retain the real CF API and copy its 16-bit units; do not ASCII-widen bytes or normalize them independently. |
| CF creation returns NULL at the injected platform boundary | Empty conversion remains a successful value at the enclosing normal getter path; no fake identity, replacement text or new false result. |
| Same CF contents through direct-pointer and pointer-NULL copy paths | Equal owned U16 content after CF object/temporary teardown; no dangling borrow. |
| Empty, 10-unit and 11-unit values | Correct logical length, terminator, owned copying and allocation boundary; tests must not serialize native C++ object layouts. |
| Replace a previous long field with a short or empty value | The field remains independently owned; counted replacement/termination semantics hold after conversion temporary destruction. |
| Assignment allocation throws before growth commit | Current field unchanged; current temporary cleaned and exception propagated; earlier header/field writes remain. No transactional rollback or catch-to-false. |
| Later product/serial conversion or assignment throws | Preserve original stage order and the specific cleanup table's resource actions. Do not restore prior stages or invent extra CF/array cleanup. |
| Defined-cleared input after NI's ignored reader error versus undefined native metadata | Defined empty data must remain distinguishable from unavailable bytes. Exact native-to-NI disposition belongs to the separate binding plan. |

Literal API data tests, boundary fault injection and distinguishing semantic
mutations belong to a separately admitted ARM implementation. No test suite,
application, endpoint, audio/MIDI stream or device operation ran here.

## Evidence, qualifications and next boundary

Private evidence root:
`.build/ni/agent-information-binding/stringutf8/`.
The final manifest is `final-evidence.json`, SHA256
`782a2d45550134f3267c4ee9698f1e48599e1ee3175b4870ed721362cf7aa9c1`.
It pins 141 files, all admitted plans and successful packet outputs, metadata,
readers, primary API/header references and preserved failed artifacts.

The first data-reader attempt stopped at the original call-site encoding 3,
after exporting only the first 32-byte table. That failed evidence was retained.
A newly recorded/reviewed packet added bounded encoding-3 parsing and resumed
the same extents. Hence 188 **unique** data bytes, 220 exported data bytes across
attempts; no unrecorded retry or broader table sweep occurred. A pre-execution
review's escape-literal concern was independently disproved by AST inspection;
the source was not edited for that false-positive finding.

All eight code packets and both successful data packets passed their exact
source/range/output guards. Independent technical and scope/guard reviews are
recorded in Beads. Original image hashes stayed unchanged. No production/test
code, driver bundle, audio ring/calendar/clock/timing/recovery, mapping,
calibration, LED policy or security setting changed. SIP remains enabled.

`.29` closes only this selected source contract. Before coding, `.2` still
requires an exact reviewed information-binding plan for real `nusb`/`nspc`
values, native-versus-NI errors, cohort/lifetime and actual ARM owned types.
`wstrToUTF8` and serial-instance publication remain later source gates; no
outgoing converter, factory/event/policy or full runtime body was followed here.
Unchanged NI application recognition and separate full-driver/audio parity
remain unproved. Installed build 9 and its rollback state are preserved.
