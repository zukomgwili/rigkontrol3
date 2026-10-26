#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include "../../Driver/NativeControl.h"

// All responses are synthetic. The only CoreAudio GET is bound to this fixture
// BEFORE NativeClient's inline bodies are first parsed.
namespace fixture {
constexpr AudioObjectID device = 91;
constexpr std::uint64_t generation = 7;
void check(bool value, const char *what) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
enum class Phase { none, conversion, copy };
struct Allocation {
    void *pointer;
    std::size_t size;
    unsigned field;
    Phase phase;
    bool deleted;
};
Allocation allocations[128]{};
unsigned allocation_count = 0;
bool armed = false;
Phase phase = Phase::none;
unsigned field = 0;
int fail_field = -1;
Phase fail_phase = Phase::none;
unsigned fault_hits = 0;
void *allocate(std::size_t size) {
    if (armed && phase != Phase::none && static_cast<int>(field) == fail_field && phase == fail_phase) {
        ++fault_hits;
        throw std::bad_alloc();
    }
    void *p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    if (armed && phase != Phase::none) {
        check(allocation_count < 128, "bounded allocation trace");
        allocations[allocation_count++] = {p,size,field,phase,false};
    }
    return p;
}
void deallocate(void *p) noexcept {
    if (armed && p) {
        // An allocator may reuse an earlier deleted address. Match the latest
        // live allocation, not a stale pointer identity.
        for (unsigned i = allocation_count; i > 0; --i) {
            if (allocations[i-1].pointer == p && !allocations[i-1].deleted) {
                allocations[i-1].deleted = true; break;
            }
        }
    }
    std::free(p);
}
struct Reply {
    UInt32 selector{};
    OSStatus transport{};
    bool wrong_type{};
    bool wrong_envelope_size{};
    int length_delta{};
    RK3NativeUSBIdentity identity{};
    RK3NativeSpec spec{};
};
Reply replies[8]{};
unsigned reply_count = 0, gets = 0, wait_count = 0;
std::int64_t waits[3]{};
char order[32]{};
unsigned order_count = 0;
void mark(char c) {
    check(order_count + 1 < sizeof(order), "bounded call-order trace");
    order[order_count++] = c;
}
struct Witness {
    CFStringRef object{};
    bool released{};
    char input[RK3_USB_IDENTITY_STRING_BYTES]{};
    UniChar units[RK3_USB_IDENTITY_STRING_BYTES]{};
};
Witness witnesses[3]{};
unsigned create_count = 0, pointer_count = 0, range_count = 0, release_count = 0;
bool direct = false;
int null_field = -1;
RK3NativeUSBIdentity identity() {
    RK3NativeUSBIdentity value{};
    value.version = 1;
    value.generation = generation;
    value.attempted_mask = 0xff;
    value.defined_mask = 0xff;
    value.vendor_id = 0x1234;
    value.product_id = 0x5678;
    value.product_index = 1;
    value.manufacturer_index = 2;
    value.serial_index = 3;
    std::strcpy(reinterpret_cast<char *>(value.manufacturer), "Vendor");
    std::strcpy(reinterpret_cast<char *>(value.product), "Product");
    std::strcpy(reinterpret_cast<char *>(value.serial), "Synthetic");
    return value;
}
void text(std::uint8_t (&target)[RK3_USB_IDENTITY_STRING_BYTES], const char *value) {
    check(std::strlen(value) < sizeof(target), "synthetic text fits native buffer");
    std::memset(target,0,sizeof(target));
    std::memcpy(target,value,std::strlen(value));
}
void pair(std::uint16_t word = 0x0201, RK3NativeUSBIdentity value = identity()) {
    check(reply_count + 2 <= 8, "at most four acquisition pairs");
    replies[reply_count].selector = RK3_PROPERTY_USB_IDENTITY;
    replies[reply_count++].identity = value;
    Reply &s = replies[reply_count++];
    s.selector = RK3_PROPERTY_SPEC;
    s.spec.version = 1;
    s.spec.generation = generation;
    s.spec.raw[0] = static_cast<std::uint8_t>(word);
    s.spec.raw[1] = static_cast<std::uint8_t>(word >> 8);
}
void cleanup() {
    armed = false;
    phase = Phase::none;
    for (Witness &w : witnesses) if (w.object) {
        // Each creation has one fixture retain. On exceptional paths NI does
        // not release the original reference: observe first, then clean both
        // references here with input still alive. This is fixture cleanup only.
        if (!w.released) CFRelease(w.object);
        CFRelease(w.object);
        w.object = nullptr;
    }
}
void reset() {
    cleanup();
    for (Reply &r : replies) r = {};
    for (Witness &w : witnesses) w = {};
    for (Allocation &a : allocations) a = {};
    allocation_count = 0;
    reply_count = gets = wait_count = order_count = 0;
    create_count = pointer_count = range_count = release_count = 0;
    std::memset(order,0,sizeof(order));
    for (std::int64_t &w : waits) w = 0;
    direct = false;
    null_field = fail_field = -1;
    fail_phase = Phase::none;
    fault_hits = 0;
    field = 0;
}
OSStatus get(AudioObjectID object, const AudioObjectPropertyAddress *address,
             UInt32 qualifier_size, const void *qualifier, UInt32 *size, void *out) {
    check(object == device, "exact owner object");
    check(address && address->mScope == kAudioObjectPropertyScopeGlobal &&
          address->mElement == kAudioObjectPropertyElementMain, "global/main property");
    check(qualifier_size == sizeof(CFDataRef) && qualifier && size && out &&
          *size == sizeof(CFPropertyListRef), "CFData transport envelope");
    CFDataRef query = *static_cast<const CFDataRef *>(qualifier);
    check(query && CFGetTypeID(query) == CFDataGetTypeID() && CFDataGetLength(query) == 16,
          "actual qualifier CFData16");
    RK3NativeSpecQuery q{};
    std::memcpy(&q, CFDataGetBytePtr(query), sizeof(q));
    check(q.version == 1 && q.reserved == 0 && q.generation == generation,
          "version/reserved/fixed generation");
    check(gets < reply_count, "no extra GET/discovery/lease reacquisition");
    const Reply &r = replies[gets++];
    check(address->mSelector == r.selector, "exact ordered nusb/nspc selector");
    mark(r.selector == RK3_PROPERTY_USB_IDENTITY ? 'I' : 'S');
    if (r.transport) return r.transport;
    CFPropertyListRef value;
    if (r.wrong_type) value = CFRetain(CFSTR("not a native record"));
    else {
        const void *bytes = r.selector == RK3_PROPERTY_USB_IDENTITY ?
            static_cast<const void *>(&r.identity) : static_cast<const void *>(&r.spec);
        CFIndex length = static_cast<CFIndex>(r.selector == RK3_PROPERTY_USB_IDENTITY ?
            sizeof(r.identity) : sizeof(r.spec));
        check(r.length_delta <= 0, "fixture never reads beyond record");
        value = CFDataCreate(nullptr,static_cast<const UInt8 *>(bytes),length + r.length_delta);
        check(value != nullptr, "fixture CFData allocation");
    }
    *static_cast<CFPropertyListRef *>(out) = value;
    *size = static_cast<UInt32>(sizeof(value) - (r.wrong_envelope_size ? 1 : 0));
    return 0;
}
void wait(std::chrono::nanoseconds duration) {
    check(wait_count < 3, "at most three waits");
    waits[wait_count++] = duration.count();
    mark('W');
}
CFStringRef make(CFAllocatorRef allocator, const char *bytes,
                 CFStringEncoding encoding, CFAllocatorRef deallocator) {
    check(create_count < 3 && bytes && allocator == nullptr &&
          encoding == kCFStringEncodingUTF8 && deallocator == kCFAllocatorNull,
          "original CF UTF8/no-copy/null allocator call");
    field = create_count++;
    Witness &w = witnesses[field];
    std::memcpy(w.input,bytes,sizeof(w.input));
    check(std::memchr(w.input,0,sizeof(w.input)) != nullptr, "in-range source NUL");
    phase = Phase::none;
    if (static_cast<int>(field) == null_field) {
        phase = Phase::copy;
        return nullptr;
    }
    w.object = CFStringCreateWithCStringNoCopy(allocator,w.input,encoding,deallocator);
    check(w.object != nullptr, "valid synthetic UTF8 accepted by actual CF");
    CFRetain(w.object);
    const CFIndex n = CFStringGetLength(w.object);
    check(n >= 0 && n < RK3_USB_IDENTITY_STRING_BYTES, "bounded real UTF16 units");
    if (n) CFStringGetCharacters(w.object,CFRangeMake(0,n),w.units);
    phase = Phase::conversion;
    return w.object;
}
const UniChar *pointer(CFStringRef object) {
    check(object == witnesses[field].object, "current CF conversion object");
    ++pointer_count;
    const CFIndex n = CFStringGetLength(object);
    for (unsigned i = allocation_count; i > 0; --i) {
        const Allocation &a = allocations[i-1];
        if (a.field == field && a.phase == Phase::conversion && !a.deleted) {
            check(a.size >= static_cast<std::size_t>(n + 1) * sizeof(char16_t),
                  "owned conversion storage includes terminator");
            const char16_t *units = static_cast<const char16_t *>(a.pointer);
            for (CFIndex j = 0; j < n; ++j)
                check(units[j] == u' ', "original conversion initializes U+0020 spaces");
            check(units[n] == 0, "conversion initial terminator");
            break;
        }
    }
    return direct ? witnesses[field].units : nullptr;
}
void characters(CFStringRef object, CFRange range, UniChar *buffer) {
    check(object == witnesses[field].object && range.location == 0 &&
          range.length == CFStringGetLength(object), "original whole UTF16 range");
    ++range_count;
    phase = Phase::none;
    CFStringGetCharacters(object,range,buffer);
    phase = Phase::conversion;
}
void release(CFTypeRef object) {
    check(object == witnesses[field].object && !witnesses[field].released,
          "only current CFString normal-path release");
    witnesses[field].released = true;
    ++release_count;
    phase = Phase::none;
    CFRelease(object);
    phase = Phase::copy;
}
}
void *operator new(std::size_t size) { return fixture::allocate(size); }
void *operator new[](std::size_t size) { return fixture::allocate(size); }
void operator delete(void *p) noexcept { fixture::deallocate(p); }
void operator delete[](void *p) noexcept { fixture::deallocate(p); }
void operator delete(void *p, std::size_t) noexcept { fixture::deallocate(p); }
void operator delete[](void *p, std::size_t) noexcept { fixture::deallocate(p); }
#define AudioObjectGetPropertyData fixture::get
#include "../../Driver/NativeClient.h"
#undef AudioObjectGetPropertyData
#define CFStringCreateWithCStringNoCopy fixture::make
#define CFStringGetCharactersPtr fixture::pointer
#define CFStringGetCharacters fixture::characters
#define CFRelease fixture::release
#define RK3_NI_INFORMATION_SLEEP_FOR fixture::wait
#include "../NIInformation.cpp"
#undef RK3_NI_INFORMATION_SLEEP_FOR
#undef CFRelease
#undef CFStringGetCharacters
#undef CFStringGetCharactersPtr
#undef CFStringCreateWithCStringNoCopy

namespace fixture {
using rk3::ni::DeviceInformation;
DeviceInformation old_output() {
    DeviceInformation value{};
    value.vendor_id = 0xaaaa;
    value.product_id = 0xbbbb;
    value.spec_word0 = 0xcccc;
    value.vendor = u"old-vendor";
    value.product = u"old-prod";
    value.serial = u"old-serial";
    return value;
}
bool equal(const DeviceInformation &a, const DeviceInformation &b) {
    return a.vendor_id == b.vendor_id && a.product_id == b.product_id &&
           a.spec_word0 == b.spec_word0 && a.vendor == b.vendor &&
           a.product == b.product && a.serial == b.serial;
}
bool call(DeviceInformation &out, bool strings = true) {
    armed = true;
    try {
        const bool result = strings ? rk3::ni::get_device_info({device,generation},out)
                                    : rk3::ni::get_device_info({device,generation},out,false);
        armed = false;
        phase = Phase::none;
        return result;
    } catch (...) { armed = false; phase = Phase::none; throw; }
}
void header(const DeviceInformation &out, std::uint16_t word = 0x0201) {
    check(out.vendor_id == 0x1234 && out.product_id == 0x5678 && out.spec_word0 == word,
          "supplied IDs and exact little-endian raw specification word");
}
void success() {
    for (unsigned mode = 0; mode < 2; ++mode) {
        reset(); pair(); direct = mode != 0;
        DeviceInformation out = old_output();
        check(call(out), "actual public strings=true operation succeeds");
        header(out);
        check(out.vendor == u"Vendor" && out.product == u"Product" && out.serial == u"Synthetic",
              "real CF conversion into independently owned fields");
        check(gets == 2 && wait_count == 0 && std::strcmp(order,"IS") == 0,
              "one ordered pair/no wait/no diagnostics");
        check(create_count == 3 && release_count == 3 && pointer_count == 3 &&
              range_count == (direct ? 0u : 3u), "direct/fallback exact CF calls");
        cleanup();
        check(out.vendor == u"Vendor" && out.serial == u"Synthetic", "owned after CF teardown");
    }
}
void invalid_lease() {
    for (unsigned which = 0; which < 3; ++which) {
        reset(); DeviceInformation out = old_output(), before = out;
        check(!rk3::ni::get_device_info({which == 1 ? device : 0,which == 0 ? generation : 0},out),
              "invalid lease returns false");
        check(equal(out,before) && gets == 0 && wait_count == 0 && create_count == 0,
              "invalid lease has no side effects");
    }
}
void rejected_reply() {
    // Run every defect in BOTH physical-identity and specification positions.
    for (unsigned position = 0; position < 2; ++position) {
        for (unsigned defect = 0; defect < 9; ++defect) {
            reset(); pair(); Reply &r = replies[position];
            switch (defect) {
                case 0: r.transport = kAudioHardwareUnspecifiedError; break;
                case 1: r.wrong_type = true; break;
                case 2: r.length_delta = -1; break;
                case 3: r.wrong_envelope_size = true; break;
                case 4: if (position) r.spec.version = 9; else r.identity.version = 9; break;
                case 5: if (position) r.spec.reserved[0] = 1; else r.identity.reserved0 = 1; break;
                case 6: if (position) ++r.spec.generation; else ++r.identity.generation; break;
                case 7:
                case 8:
                    if (position) { r.spec = {}; r.spec.version = 1; r.spec.status = defect == 7 ? ENODEV : ESTALE; }
                    else { r.identity = {}; r.identity.version = 1; r.identity.status = defect == 7 ? ENODEV : ESTALE; }
                    break;
            }
            DeviceInformation out = old_output(), before = out;
            check(!call(out), "transport/schema/native errors return false");
            check(equal(out,before) && gets == position + 1 && wait_count == 0 && create_count == 0,
                  "failed acquisition preserves all output and stops immediately");
        }
    }
}
void availability() {
    // Each required bit is independently necessary; zero storage is not data
    // when the corresponding defined bit is absent. Preserve NativeClient's
    // canonical getter/reader status rules so these reach the operation itself.
    const unsigned required[] = {RK3_USB_IDENTITY_VENDOR_ID,RK3_USB_IDENTITY_PRODUCT_ID,
        RK3_USB_IDENTITY_PRODUCT_READER,RK3_USB_IDENTITY_MANUFACTURER_READER,
        RK3_USB_IDENTITY_SERIAL_READER};
    for (unsigned missing : required) {
        for (unsigned strings = 0; strings < 2; ++strings) {
            if (!strings && missing >= RK3_USB_IDENTITY_PRODUCT_READER) continue;
            reset(); auto unavailable = identity();
            unavailable.defined_mask &= ~(1u << missing);
            if (missing < RK3_USB_IDENTITY_PRODUCT_READER) {
                unavailable.step_status[missing] = -91;
                if (missing == RK3_USB_IDENTITY_VENDOR_ID) unavailable.vendor_id = 0;
                else unavailable.product_id = 0;
            } else {
                std::uint8_t *bytes[] = {unavailable.product,unavailable.manufacturer,unavailable.serial};
                std::memset(bytes[missing - RK3_USB_IDENTITY_PRODUCT_READER],0,
                            RK3_USB_IDENTITY_STRING_BYTES);
            }
            pair(0x0201,unavailable);
            DeviceInformation unchanged = old_output(), prior = unchanged;
            check(!call(unchanged,strings != 0) && equal(unchanged,prior) && gets == 1 &&
                  create_count == 0 && wait_count == 0,
                  "each required VID/PID/reader bit is independently necessary");
        }
    }

    reset(); auto id = identity();
    DeviceInformation out = old_output(), before = out;
    std::memset(id.serial,0,sizeof(id.serial));
    id.step_status[RK3_USB_IDENTITY_SERIAL_READER] = -77;
    pair(0x0201,id); out = old_output();
    check(call(out) && out.serial.empty(), "defined cleared string survives ignored reader error");

    reset(); id = identity();
    id.defined_mask = 3; id.attempted_mask = RK3_USB_IDENTITY_GETTER_MASK;
    id.product_index = id.manufacturer_index = id.serial_index = 0;
    for (unsigned i = RK3_USB_IDENTITY_PRODUCT_INDEX; i <= RK3_USB_IDENTITY_SERIAL_INDEX; ++i)
        id.step_status[i] = -91;
    std::memset(id.product,0,sizeof(id.product));
    std::memset(id.manufacturer,0,sizeof(id.manufacturer));
    std::memset(id.serial,0,sizeof(id.serial));
    pair(0x0201,id); out = old_output(); before = out;
    check(call(out,false), "strings=false only requires real VID/PID"); header(out);
    check(out.vendor == before.vendor && out.product == before.product && out.serial == before.serial &&
          create_count == 0 && gets == 2, "strings=false retains existing strings without CF");

    reset(); id = identity(); id.vendor_id = id.product_id = 0;
    std::memset(id.product,0,sizeof(id.product)); std::memset(id.manufacturer,0,sizeof(id.manufacturer));
    std::memset(id.serial,0,sizeof(id.serial)); pair(0,id); pair(0,id); pair(0,id); pair(0,id);
    out = old_output();
    check(call(out) && out.vendor_id == 0 && out.product_id == 0 && out.spec_word0 == 0 &&
          out.vendor.empty() && out.product.empty() && out.serial.empty(), "defined all-zero data still succeeds");
}
void readiness() {
    for (unsigned zero_pairs = 0; zero_pairs <= 4; ++zero_pairs) {
        reset();
        for (unsigned i = 0; i < zero_pairs; ++i) pair(0);
        if (zero_pairs < 4) pair();
        DeviceInformation out = old_output();
        check(call(out), "zero readiness keeps original final-success rule");
        const unsigned n = zero_pairs < 4 ? zero_pairs : 3;
        check(wait_count == n && gets == 2 * (n + 1), "exact bounded pair/wait count");
        for (unsigned i = 0; i < n; ++i)
            check(waits[i] == static_cast<std::int64_t>(i + 1) * 100000000,
                  "original 100/200/300ms waits");
        check(std::strcmp(order + order_count - 2,"IS") == 0, "no wait after final successful pair");
        for (unsigned i = 0; i < n; ++i)
            check(order[i*3] == 'I' && order[i*3+1] == 'S' && order[i*3+2] == 'W',
                  "whole pair repeated only after original wait");
        header(out,zero_pairs == 4 ? 0 : 0x0201);
    }
    for (unsigned position = 0; position < 2; ++position) {
        reset(); pair(0); pair(); replies[2+position].transport = kAudioHardwareUnspecifiedError;
        DeviceInformation out = old_output(), before = out;
        check(!call(out) && equal(out,before) && gets == 3 + position && wait_count == 1,
              "failure after zero ends immediately without partial output");
    }
}
void unicode_and_replacement() {
    for (unsigned mode = 0; mode < 2; ++mode) {
        reset(); auto id = identity();
        text(id.manufacturer,"\xc3\xa9"); text(id.product,"\xf0\x9f\x98\x80");
        text(id.serial,"A"); id.serial[2] = 'B'; pair(0x0201,id); direct = mode != 0;
        DeviceInformation out = old_output();
        out.vendor.assign(80,u'x'); out.product.assign(80,u'x'); out.serial.assign(80,u'x');
        const char16_t *v = out.vendor.data(), *p = out.product.data(), *s = out.serial.data();
        check(call(out), "actual CF UTF8 conversion succeeds");
        check(out.vendor == u"\u00e9" && out.product == u"\U0001f600" && out.serial == u"A",
              "real UTF16 including surrogate pair and first NUL");
        check(out.vendor.data() == v && out.product.data() == p && out.serial.data() == s,
              "sufficient-capacity replacement keeps destination storage");
        check(out.vendor.c_str()[out.vendor.size()] == 0 && out.product.c_str()[out.product.size()] == 0,
              "counted owned values terminate");
    }
    for (unsigned target = 0; target < 3; ++target) {
        reset(); pair(); null_field = static_cast<int>(target); DeviceInformation out = old_output();
        check(call(out), "CF creation NULL remains normal getter success");
        const std::u16string *fields[] = {&out.vendor,&out.product,&out.serial};
        check(fields[target]->empty() && release_count == 2 && create_count == 3,
              "NULL conversion replaces selected field with empty");
    }
    for (unsigned length : {0u,10u,11u,30u}) {
        reset(); auto id = identity(); char data[31]{};
        std::memset(data,'L',length); text(id.manufacturer,data); text(id.product,data); text(id.serial,data);
        pair(0x0201,id); DeviceInformation out = old_output();
        check(call(out) && out.vendor == std::u16string(length,u'L') && out.product == out.vendor &&
              out.serial == out.vendor, "empty/10/11/native-limit owned lengths");
        check(pointer_count == 3 && range_count == (length ? 3u : 0u),
              "empty still asks for pointer but avoids fallback extraction");
    }
}
RK3NativeUSBIdentity long_identity() {
    auto id = identity();
    text(id.manufacturer,"VVVVVVVVVVVVVVVVVVVVVVVVVVVVVV");
    text(id.product,"PPPPPPPPPPPPPPPPPPPPPPPPPPPPPP");
    text(id.serial,"SSSSSSSSSSSSSSSSSSSSSSSSSSSSSS");
    return id;
}
void owned_copy_oracle() {
    reset(); pair(0x0201,long_identity()); DeviceInformation out = old_output();
    check(out.vendor.capacity() < 30 && out.product.capacity() < 30 && out.serial.capacity() < 30,
          "fixture starts below copy-growth capacity");
    check(call(out), "heap-size conversion and field copy succeeds");
    const char16_t *data[] = {out.vendor.data(),out.product.data(),out.serial.data()};
    for (unsigned f = 0; f < 3; ++f) {
        const Allocation *conversion = nullptr, *copy = nullptr;
        for (unsigned i = 0; i < allocation_count; ++i) if (allocations[i].field == f) {
            if (allocations[i].phase == Phase::conversion) conversion = &allocations[i];
            if (allocations[i].phase == Phase::copy) copy = &allocations[i];
        }
        check(conversion && copy && conversion->pointer != copy->pointer,
              "conversion and output use distinct owned heap allocations");
        check(conversion->deleted && !copy->deleted && copy->pointer == data[f],
              "temporary deleted after COPY; destination remains independently alive");
    }
    cleanup();
    check(out.vendor == std::u16string(30,u'V') && out.product == std::u16string(30,u'P') &&
          out.serial == std::u16string(30,u'S'), "field values survive all CF and conversion teardown");
}
void allocation_failures() {
    for (unsigned target = 0; target < 3; ++target) {
        for (Phase stage : {Phase::conversion,Phase::copy}) {
            reset(); pair(0x0201,long_identity());
            DeviceInformation out = old_output(), before = out;
            check(out.vendor.capacity() < 30 && out.product.capacity() < 30 && out.serial.capacity() < 30,
                  "fault targets actual growth beyond destination capacity");
            fail_field = static_cast<int>(target); fail_phase = stage;
            bool threw = false;
            try { (void)call(out); } catch (const std::bad_alloc &) { threw = true; }
            check(threw && fault_hits == 1, "actual string allocation exception propagates");
            header(out);
            check(create_count == target + 1 && release_count == target + (stage == Phase::copy ? 1u : 0u),
                  "original conversion order and exact exceptional CF cleanup");
            const std::u16string *after[] = {&out.vendor,&out.product,&out.serial};
            const std::u16string *old[] = {&before.vendor,&before.product,&before.serial};
            for (unsigned f = 0; f < 3; ++f) {
                if (f < target) check(*after[f] == std::u16string(30,f == 0 ? u'V' : u'P'),
                                      "earlier completed field stays updated on later failure");
                else check(*after[f] == *old[f], "current and later fields retain old values on failure");
            }
            for (unsigned i = 0; i < allocation_count; ++i) {
                const Allocation &a = allocations[i];
                if (a.phase == Phase::conversion)
                    check(a.deleted, "current and earlier conversion allocations destroyed on unwind");
            }
            cleanup();
        }
    }
}
}
int main() {
    fixture::success();
    fixture::invalid_lease();
    fixture::rejected_reply();
    fixture::availability();
    fixture::readiness();
    fixture::unicode_and_replacement();
    fixture::owned_copy_oracle();
    fixture::allocation_failures();
    fixture::cleanup();
    std::puts("NI information tests: PASS (actual public operation, synthetic offline transport)");
}
