# DriverKit and NI's unchanged class lookup

Research date: **2026-10-25 UTC**. Beads authority: **rigkontrol3-40h.17**.
This is a bounded source-only decision following the published software-probe
result. No provider, NI application, signing asset or security setting was
changed by this research.

**No supported driver-side alias satisfying NI's unchanged lookup was
established.** The tested `IOUserClass` and registry-name mechanism fails that
lookup. The inspected Apple source explains the distinction between names,
reported classes and kernel class matching. This is a decision to keep the
production binding unadmitted, not a proof that every DriverKit implementation
or NI application is incompatible.

The original NIHardwareAgent1.7.4 RK3 filter supplies
`de_caiaq_driver_NIUSBAudioDevice` to `IOServiceMatching`. The initializer,
filter-string storage and factory-call evidence are recorded in
[the binding decision](KernelBindingDecision.md). The ported MIDI driver's
similar lookup is a separate, changeable caller; adapting it alone does not
change the original agent's lookup.

The single device-eligible probe test returned the following, with no API
errors and a successful independent identity recheck:

| Observation | Recorded result |
| --- | --- |
| Independent marked service | One valid entry, ID 4294971845 |
| Kernel class | `IOUserService` |
| DriverKit user class and registry name | `de_caiaq_driver_NIUSBAudioDevice` |
| Exact legacy class query | Zero entries |
| Name query | The same marked entry |
| Diagnostic verdict | Exit 1, `FAIL / NAME_ONLY_MATCH` |

The result is preserved privately in
`.build/ni/driverkit-discovery-evidence/20261025-eligible-probe-5a25a43/live-result.json`
(SHA256 `fb8b8f0c1e5c69374c09589aff21ed892170b106584a806fd52fa0251167b549`).
It proves service publication and this exact lookup failure; it tests no NI
user-client selectors, device information, audio or application acceptance.

Apple's public personality documentation distinguishes `IOClass` from
`IOUserClass` and lists property and name matching as separate criteria. Naming
an entry or choosing a user-space entry-point class is therefore not a
documented promise to create the corresponding kernel metaclass.
[Apple IOKitPersonalities documentation](https://developer.apple.com/documentation/bundleresources/information-property-list/iokitpersonalities)

The inspected source contracts are:

| Path | Relevant behavior | Consequence for the fixed NI query |
| --- | --- | --- |
| `IOServiceMatching` | Constructs an `IOProviderClass` criterion. `IOServiceNameMatching` constructs a different name criterion. | A driver name does not rewrite the caller's criterion. |
| XNU `copyExistingServices` | Selects instances through `OSMetaClass::applyToInstancesOfClassName` when the criterion supplies a class. | Ordinary properties do not supply a new class for that selection. |
| XNU `matchInternal` / `matchPassive` | The ordinary class branch uses kernel `metaCast`; name and property branches are distinct. | A name/property match does not by itself satisfy the class branch. |

[Apple IOKitUser matching helpers](https://github.com/apple-oss-distributions/IOKitUser/blob/23a611d9d194b63465f6df2abfdda63d71c87e97/IOKitLib.c#L1065-L1076),
[Apple XNU existing-service selection](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOService.cpp#L6227),
[Apple XNU internal matching](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOService.cpp#L7773),
[Apple XNU passive matching](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOService.cpp#L8022).

XNU also has a compatibility-properties path, but both its extra compatibility
class enumeration and class comparison depend on an `IOCompatibilityMatch`
criterion in the **matching dictionary**. The inspected unchanged
`IOServiceMatching(oldClass)` helper does not add that criterion. This branch
is not evidence that adding a personality property to this dext would repair
the unchanged NI call. The initial pass did not trace server-entry compatibility transformations.
The bounded follow-up below resolves that specific entry-path question in the
published source, without claiming exhaustive platform behavior.
[Apple XNU compatibility gate](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOService.cpp#L6278),
[Apple IOKitUser helper](https://github.com/apple-oss-distributions/IOKitUser/blob/23a611d9d194b63465f6df2abfdda63d71c87e97/IOKitLib.c#L1065).

`IOClassNameOverride` is a different mechanism: IOKitUser's reporting wrapper
can return that string instead of the kernel class; its conformance wrapper
can accept the string after kernel conformance fails. The matching helper does
not call either wrapper. Consequently, those reporting overrides supply no
matching alias for this query.
[Apple class-report override](https://github.com/apple-oss-distributions/IOKitUser/blob/23a611d9d194b63465f6df2abfdda63d71c87e97/IOKitLib.c#L262-L284),
[Apple conformance override](https://github.com/apple-oss-distributions/IOKitUser/blob/23a611d9d194b63465f6df2abfdda63d71c87e97/IOKitLib.c#L389-L418).

The installed macOS27 public `IOKitLib.h` documents class/subclass matching
separately from name matching. The bounded public-header and pinned
`IOKitLib.c` inspection found no `IOServiceDEXTMatching` declaration or function;
that name must not be presented as an available public API on this evidence.
The public keys do expose `IOUserClass` and `IOUserClasses`, but a key declaration
alone establishes no legacy metaclass alias.
[Installed IOKitLib.h](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/IOKit.framework/Headers/IOKitLib.h:1322),
[Installed IOKitKeys.h](/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/IOKit.framework/Headers/IOKitKeys.h:78),
[Inspected Apple IOKitUser file](https://github.com/apple-oss-distributions/IOKitUser/blob/23a611d9d194b63465f6df2abfdda63d71c87e97/IOKitLib.c).

This source interpretation is limited deliberately. XNU was pinned at
`f6217f891ac0bb64f3d375211650a4c1ff8ca1ea` (commit dated 2025-10-16), not asserted to
be the exact macOS27.0.1 running kernel. Its `IOService.cpp` SHA256 is
`8791d87936ced84d6529a2becf6b78db31c0db7acd0aa6da4708a73b8cb50176`.
IOKitUser was checked at the immutable `IOKitUser-100231.120.3` file revision
`23a611d9d194b63465f6df2abfdda63d71c87e97`, SHA256
`6086e7b07aac1d7b489232b6997acac276e2da8917e077ba7365c2b5a58fdcd4`.
The installed header hashes are `553869588e8c0b162b232e294dea2579cfd739d83e772a0b8a79e2be91a5816e`
(`IOKitLib.h`) and `049915beaceaf5dfafb5223d98f00e7303be49d29d00e20394046ef183639c24`
(`IOKitKeys.h`). The actual runtime probe supplies the host-specific negative
result; the published source supplies an explanation and bounded admission
criteria, not exhaustive knowledge of private platform behavior.

Before any further provider code, admission requires a supported driver-side
mechanism shown to satisfy this same fixed caller, or an explicit change to the
required-client boundary. A different consumer matching dictionary is a client
adaptation, not proof that the unchanged NI agent works. No speculative alias
properties, kernel shim or full audio/user-client backend is justified by this
pass. Original NI automatic recognition remains required and unproved; the
full modern Guitar Rig/Controller Editor recognition chain also remains outside
this bounded check.

The bounded server-entry follow-up uses the same XNU commit's
`IOUserClient.cpp`. Its binary and out-of-line matching entry points call
`internal_io_service_get_matching_services`; the plain string entry point
returns unsupported. The common helper invokes
`IOTaskRegistryCompatibilityMatching(current_task(), dict)` before passing the
dictionary to `IOService::getMatchingServices`.
[Apple matching entry points and common helper](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOUserClient.cpp#L2732-L2812).

That compatibility helper always adds the user-notification criterion. It adds
`gIOCompatibilityMatchKey` only if `IOTaskRegistryCompatibility(task)` is true;
in this published version the predicate unconditionally returns false. It has
no SDK-version, architecture or translation branch. Therefore the proposed
automatic legacy-caller compatibility flag is not supplied at this inspected
boundary, including for the hypothesized old x86 NI caller. This removes that
specific source-level uncertainty; it does not establish the exact host's
macOS27/Rosetta implementation or prove full NI application acceptance.
[Apple compatibility predicate and flag helper](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOUserClient.cpp#L417-L430).

The added file's SHA256 is
`a09c525b144ecbf834bb2b69f0c78b93358601ee22045340609b30acc8e0a6e7`.
No further callee or unrelated registry path was expanded. The admission
conclusion remains unchanged: a documented supported driver-side mechanism
satisfying the fixed NI caller has not been established, and the name/user-class
candidate has an actual negative result. No provider-code change follows from
this source comparison.

The user subsequently authorized a bounded feasibility study of an ARM port of
NI's supporting hardware agent in `rigkontrol3-40h.18`. A necessary lookup
adaptation in that port may be investigated while preserving NI frontend
recognition, protocol and behavior. That is distinct from waiving original NI
functionality or claiming the original agent binary works. The class-test
result stays unchanged, and no agent or provider implementation is admitted by
the feasibility answer alone.
