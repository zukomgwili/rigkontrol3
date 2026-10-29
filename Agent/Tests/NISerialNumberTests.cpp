#include <CoreFoundation/CoreFoundation.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <limits>
#include <new>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include "../NIInformation.h"
#include "../NISerialNumber.h"

extern char **environ;

// Only synthetic cached values and real data-only CF/std operations are used.
// SDK declarations and wrappers precede redirects into the actual module.
namespace fixture {
void check(bool value, const char *message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
enum class Phase { none, buffer, assignment, reserve, transfer, wide_construct, wide_copy };
enum class Fault { none, create, length, maximum, array, assignment, wide_construct, wide_copy };
enum class Child { none, null_next, release_throw, reserve_throw };
struct BoundaryFailure {};
struct Allocation {
    void *pointer{};
    std::size_t bytes{};
    Phase phase{};
    bool array{}, deleted{};
    unsigned deletion_order{};
};
Allocation allocations[64]{};
unsigned allocation_count = 0, cpp_allocations = 0, array_attempts = 0, fault_hits = 0;
bool armed = false;
Phase phase = Phase::none;
Fault fault = Fault::none;
Child child = Child::none;
const rk3::ni::DeviceInformation *expected_cache = nullptr;
std::string *current_output = nullptr;
CFStringRef source = nullptr;
bool source_released = false;
unsigned creates = 0, lengths = 0, maxima = 0, cstrings = 0, releases = 0, reserves = 0;
unsigned parent_calls = 0, utf16_parent_calls = 0, helper_cases = 0;
unsigned byte_productions = 0, wide_copies = 0, deletion_ordinal = 0;
bool utf16_call = false, expected_connected = false, oversized_helper = false;
std::u16string *wide_output = nullptr;
const void *byte_payload = nullptr, *wide_payload = nullptr;
std::size_t byte_count = 0, wide_count = 0;
unsigned release_throw_unwind_count = 99;
CFIndex last_maximum = 0, last_capacity = 0;
bool forced_maximum = false, false_after_valid_conversion = false;
CFIndex maximum_value = 0;
std::size_t last_array_request = 0;
const void *old_output_data = nullptr;
bool old_output_is_heap = false;
unsigned old_output_deletions = 0;
char order[32]{};
unsigned order_count = 0;
const char *executable = nullptr;
void mark(char value) {
    check(order_count + 1 < sizeof(order),"bounded CF/reserve trace");
    order[order_count++] = value;
}
void *allocate(std::size_t size, bool array) {
    // Guard-only helper cases allow bounded exception diagnostic storage, but
    // must never reach a real huge allocation even under a changed native guard.
    if (oversized_helper) check(size <= 4096,"oversized helper cannot allocate huge storage");
    if (armed) {
        ++cpp_allocations;
        if (array && phase == Phase::buffer) {
            ++array_attempts; last_array_request = size;
            if (forced_maximum && maximum_value == std::numeric_limits<CFIndex>::max()) {
                check(size == std::numeric_limits<std::size_t>::max(),
                      "overflow boundary must request exact SIZE_MAX before any real allocation");
                ++fault_hits;
                throw std::bad_alloc();
            }
        }
        if ((array && phase == Phase::buffer &&
             (size == std::numeric_limits<std::size_t>::max() || fault == Fault::array)) ||
            (!array && phase == Phase::assignment && fault == Fault::assignment) ||
            (!array && phase == Phase::wide_construct && fault == Fault::wide_construct) ||
            (!array && phase == Phase::wide_copy && fault == Fault::wide_copy)) {
            ++fault_hits;
            throw std::bad_alloc();
        }
    }
    void *pointer = std::malloc(size ? size : 1);
    if (!pointer) throw std::bad_alloc();
    if (armed && phase != Phase::none) {
        check(allocation_count < 64,"bounded allocation trace");
        allocations[allocation_count++] = {pointer,size,phase,array,false,0};
    }
    return pointer;
}
void deallocate(void *pointer, bool array) noexcept {
    if (armed && pointer) {
        if (old_output_is_heap && pointer == old_output_data) ++old_output_deletions;
        for (unsigned i = allocation_count; i > 0; --i) {
            Allocation &a = allocations[i-1];
            if (a.pointer == pointer && !a.deleted) {
                check(a.array == array,"matching scalar/array allocation boundary");
                a.deleted = true;
                check(deletion_ordinal < 64,"bounded scalar deletion trace");
                a.deletion_order = ++deletion_ordinal;
                break;
            }
        }
    }
    std::free(pointer);
}
void cleanup() {
    // Fixture policy only, after source-policy assertions. Never repair module
    // cleanup while recording it. A stopped mutant is not a leak-clean pass.
    armed = false;
    phase = Phase::none;
    if (source && !source_released) CFRelease(source);
    source = nullptr;
    for (Allocation &a : allocations) if (a.array && !a.deleted) {
        ::operator delete[](a.pointer);
        a.deleted = true;
    }
}
void reset() {
    cleanup();
    for (Allocation &a : allocations) a = {};
    allocation_count = cpp_allocations = array_attempts = fault_hits = 0;
    creates = lengths = maxima = cstrings = releases = reserves = 0;
    order_count = 0;
    std::memset(order,0,sizeof(order));
    source_released = false;
    forced_maximum = false_after_valid_conversion = false;
    maximum_value = last_maximum = last_capacity = 0;
    last_array_request = 0;
    old_output_data = nullptr;
    old_output_is_heap = false;
    old_output_deletions = 0;
    expected_cache = nullptr;
    current_output = nullptr;
    release_throw_unwind_count = 99;
    fault = Fault::none;
    child = Child::none;
    byte_productions = wide_copies = deletion_ordinal = 0;
    utf16_call = expected_connected = oversized_helper = false;
    wide_output = nullptr;
    byte_payload = wide_payload = nullptr;
    byte_count = wide_count = 0;
}
CFStringRef make(CFAllocatorRef allocator, const UniChar *units, CFIndex count,
                 CFAllocatorRef deallocator) {
    ++creates; mark('C'); phase = Phase::none;
    if (utf16_call) check(creates == 1,"UTF16 performs one real byte conversion");
    check(expected_cache && allocator == nullptr && deallocator == kCFAllocatorNull,
          "original CF allocators and cached input");
    check(static_cast<const void *>(units) == static_cast<const void *>(expected_cache->serial.data()) &&
          count == static_cast<CFIndex>(expected_cache->serial.size()),
          "exact borrowed serial pointer and explicit UTF16 count");
    check(count >= 0 && count <= 256,"bounded synthetic input witness");
    UniChar witness[256]{};
    if (count) std::memcpy(witness,units,static_cast<std::size_t>(count) * sizeof(UniChar));
    for (CFIndex i = 0; i < count; ++i)
        check(witness[i] == static_cast<UniChar>(expected_cache->serial[static_cast<std::size_t>(i)]),
              "typed witness preserves UTF16 units without alias dereference");
    if (fault == Fault::create) { ++fault_hits; throw BoundaryFailure{}; }
    if (child == Child::null_next) return nullptr;
    source = CFStringCreateWithCharactersNoCopy(allocator,units,count,deallocator);
    check(source != nullptr,"valid synthetic counted units accepted by real CF");
    return source;
}
CFIndex length(CFStringRef value) {
    ++lengths; mark('L');
    if (child == Child::null_next) {
        if (value == nullptr && creates == 1 && reserves == 0 && current_output &&
            *current_output == "old-output") {
            constexpr char marker[] = "NULL_NEXT\n";
            (void)::write(STDOUT_FILENO,marker,sizeof(marker)-1);
            std::_Exit(73);
        }
        std::_Exit(93);
    }
    check(value && value == source,"length uses current real CF object");
    if (fault == Fault::length) { ++fault_hits; throw BoundaryFailure{}; }
    return CFStringGetLength(value);
}
CFIndex maximum(CFIndex count, CFStringEncoding encoding) {
    ++maxima; mark('M');
    check(source && count == CFStringGetLength(source) && encoding == kCFStringEncodingUTF8,
          "original real CF length and UTF8 maximum query");
    if (fault == Fault::maximum) { ++fault_hits; throw BoundaryFailure{}; }
    last_maximum = forced_maximum ? maximum_value : CFStringGetMaximumSizeForEncoding(count,encoding);
    phase = Phase::buffer;
    return last_maximum;
}
Boolean cstring(CFStringRef value, char *buffer, CFIndex capacity, CFStringEncoding encoding) {
    ++cstrings; mark('G'); phase = Phase::none;
    check(value == source && buffer && last_maximum > 0 && capacity > 0 &&
          static_cast<std::uint64_t>(capacity) == static_cast<std::uint64_t>(last_maximum) + 1 &&
          encoding == kCFStringEncodingUTF8,
          "original positive maximum-plus-one CString conversion");
    last_capacity = capacity;
    const Boolean ok = CFStringGetCString(value,buffer,capacity,encoding);
    check(ok,"real conversion writes valid terminated synthetic bytes");
    phase = Phase::assignment;
    return false_after_valid_conversion ? false : ok;
}
void release(CFTypeRef value) {
    ++releases; mark('R'); phase = Phase::none;
    check(source && value == source && !source_released,"one current CF release");
    if (child == Child::release_throw) {
        release_throw_unwind_count = static_cast<unsigned>(std::uncaught_exceptions());
        throw BoundaryFailure{};
    }
    source_released = true;
    CFRelease(value);
    phase = Phase::transfer;
}
void reserve_zero(std::string &value) {
    ++reserves; mark('T');
    check(&value == current_output && value.empty(),"destination cleared before actual reserve(0)");
    check(source_released && releases == 1,"CF released before caller transfer stage");
    phase = Phase::reserve;
    if (child == Child::reserve_throw) throw BoundaryFailure{};
    value.reserve(0);
    phase = Phase::transfer;
}
const Allocation *payload_record(const void *pointer) {
    if (!pointer) return nullptr;
    for (unsigned i = allocation_count; i > 0; --i)
        if (allocations[i-1].pointer == pointer) return &allocations[i-1];
    return nullptr;
}
bool produce_bytes(const rk3::ni::DeviceInformation *cache, bool connected, std::string &value) {
    check(utf16_call && ++byte_productions == 1,"UTF16 forwards byte producer exactly once");
    check(cache == expected_cache && connected == expected_connected && value.empty(),
          "UTF16 forwards supplied snapshot into fresh real byte string");
    current_output = &value;
    old_output_data = value.data(); old_output_is_heap = false;
    const bool result = rk3::ni::get_serial_number(cache,connected,value);
    if (result) {
        check(source_released && releases == 1,"producer ends CF borrowing before widening");
        byte_payload = value.data(); byte_count = value.size();
        phase = Phase::wide_construct;
    }
    return result;
}
void assign_utf16(std::u16string &output, const std::u16string &widened) {
    check(utf16_call && ++wide_copies == 1,"UTF16 performs one counted output COPY");
    check(&output == wide_output && source_released && releases == 1 &&
          widened.size() == byte_count,"UTF16 COPY follows finished counted byte conversion");
    wide_payload = widened.data(); wide_count = widened.size();
    if (byte_count > std::string{}.capacity()) {
        const Allocation *bytes = payload_record(byte_payload);
        check(bytes && !bytes->array && !bytes->deleted,
              "byte payload remains owned until wide COPY completes");
    }
    if (wide_count > std::u16string{}.capacity()) {
        const Allocation *wide = payload_record(wide_payload);
        check(wide && !wide->array && !wide->deleted,"completed wide payload is live at COPY");
    }
    phase = Phase::wide_copy;
    output.assign(widened.data(),widened.size());
}

}
void *operator new(std::size_t size) { return fixture::allocate(size,false); }
void *operator new[](std::size_t size) { return fixture::allocate(size,true); }
void operator delete(void *pointer) noexcept { fixture::deallocate(pointer,false); }
void operator delete[](void *pointer) noexcept { fixture::deallocate(pointer,true); }
void operator delete(void *pointer,std::size_t) noexcept { fixture::deallocate(pointer,false); }
void operator delete[](void *pointer,std::size_t) noexcept { fixture::deallocate(pointer,true); }

#define CFStringCreateWithCharactersNoCopy fixture::make
#define CFStringGetLength fixture::length
#define CFStringGetMaximumSizeForEncoding fixture::maximum
#define CFStringGetCString fixture::cstring
#define CFRelease fixture::release
#define RK3_NI_SERIAL_RESERVE_ZERO fixture::reserve_zero
#define RK3_NI_SERIAL_PRODUCE_BYTES fixture::produce_bytes
#define RK3_NI_SERIAL_ASSIGN_UTF16 fixture::assign_utf16
#include "../NISerialNumber.cpp"
#undef RK3_NI_SERIAL_ASSIGN_UTF16
#undef RK3_NI_SERIAL_PRODUCE_BYTES
#undef RK3_NI_SERIAL_RESERVE_ZERO
#undef CFRelease
#undef CFStringGetCString
#undef CFStringGetMaximumSizeForEncoding
#undef CFStringGetLength
#undef CFStringCreateWithCharactersNoCopy

namespace fixture {
bool run(const rk3::ni::DeviceInformation *cache, bool connected, std::string &output) {
    expected_cache = cache;
    current_output = &output;
    old_output_data = output.data();
    old_output_is_heap = output.size() > std::string{}.capacity();
    ++parent_calls;
    armed = true;
    try {
        const bool result = rk3::ni::get_serial_number(cache,connected,output);
        armed = false; phase = Phase::none;
        return result;
    } catch (...) { armed = false; phase = Phase::none; throw; }
}
void guarded_false() {
    for (unsigned which = 0; which < 3; ++which) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"stored";
        std::string output = "old-output", before = output;
        const auto *input = which == 2 ? &cache : nullptr;
        check(!run(input,which == 1,output),"original absent/disconnected guards return false");
        check(output == before && creates == 0 && cpp_allocations == 0 && reserves == 0,
              "false guards retain output without CF/allocation/transfer");
        cleanup();
    }
}
void successful_values() {
    const std::u16string inputs[] = {u"RK3-Test",u"",u"\u00e9",u"\U0001f600",std::u16string(u"A\0B",3)};
    const std::string outputs[] = {"RK3-Test","","\xc3\xa9","\xf0\x9f\x98\x80","A"};
    for (unsigned i = 0; i < 5; ++i) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial = inputs[i];
        cache.vendor = cache.product = u"decoy"; cache.vendor_id = 0x2222; cache.product_id = 0x4444;
        std::string output(256,'x');
        check(run(&cache,true,output),"actual cached serial producer succeeds");
        check(output == outputs[i] && cache.serial == inputs[i],"real CF UTF8/C-string result from stored serial");
        check(creates == 1 && lengths == 1 && maxima == 1 && releases == 1 && reserves == 1,
              "one counted conversion and output transfer");
        check(std::strcmp(order,i == 1 ? "CLMRT" : "CLMGRT") == 0,
              "source conversion/release/transfer order including empty branch");
        check(output.c_str()[output.size()] == 0,"owned output terminates after CF teardown");
        cleanup();
    }
}
void nonpositive_maximum() {
    for (CFIndex value : {CFIndex(0),CFIndex(-1)}) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"nonempty";
        std::string output = "old-output"; forced_maximum = true; maximum_value = value;
        check(run(&cache,true,output) && output.empty(),"nonpositive maximum is successful empty value");
        check(cstrings == 0 && array_attempts == 0 && releases == 1 && reserves == 1 &&
              std::strcmp(order,"CLMRT") == 0,"nonpositive branch skips byte conversion only");
        cleanup();
    }
}
void buffer_thresholds() {
    for (CFIndex value : {CFIndex(65536),CFIndex(65537)}) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"short";
        std::string output = "old-output"; forced_maximum = true; maximum_value = value;
        check(run(&cache,true,output) && output == "short","threshold uses valid real conversion bytes");
        check(last_capacity == (value == 65536 ? 65537 : 65538),"literal source threshold capacities");
        check(array_attempts == (value == 65536 ? 0u : 1u),"stack through 65536, raw heap above it");
        for (unsigned i = 0; i < allocation_count; ++i) if (allocations[i].array)
            check(allocations[i].bytes == 65538 && allocations[i].deleted,
                  "normal raw byte array deletion after assignment");
        cleanup();
    }
}
void ignored_boolean() {
    reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"valid-buffer";
    std::string output = "old-output"; false_after_valid_conversion = true;
    check(run(&cache,true,output) && output == "valid-buffer",
          "conversion Boolean ignored with real valid terminated bytes");
    cleanup();
}
void boundary_throws() {
    const Fault cases[] = {Fault::create,Fault::length,Fault::maximum};
    const char *orders[] = {"C","CLR","CLMR"};
    for (unsigned i = 0; i < 3; ++i) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"stored";
        std::string output = "old-output", before = output; fault = cases[i];
        bool threw = false;
        try { (void)run(&cache,true,output); } catch (const BoundaryFailure &) { threw = true; }
        check(threw && fault_hits == 1 && output == before && reserves == 0,
              "conversion boundary exception propagates before output mutation");
        check(releases == (i ? 1u : 0u) && std::strcmp(order,orders[i]) == 0,
              "CF guard starts only after creation and releases on selected unwind");
        cleanup();
    }
}
void allocation_throws() {
    for (unsigned which = 0; which < 3; ++which) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial.assign(128,u'S');
        std::string output = "old-output", before = output;
        fault = which == 0 ? Fault::array : Fault::assignment;
        if (which != 1) { forced_maximum = true; maximum_value = 65537; }
        bool threw = false;
        try { (void)run(&cache,true,output); } catch (const std::bad_alloc &) { threw = true; }
        check(threw && fault_hits == 1 && output == before && releases == 1 && reserves == 0,
              "actual allocation fault preserves caller and releases CF");
        if (which == 2) {
            bool live_array = false;
            for (unsigned i = 0; i < allocation_count; ++i) if (allocations[i].array) {
                check(!allocations[i].deleted,"original array is not repaired during assignment unwind");
                live_array = true;
            }
            check(live_array,"assignment fault follows actual raw array allocation");
        }
        cleanup();
    }
}
void maximum_request() {
    reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"stored";
    std::string output = "old-output", before = output;
    forced_maximum = true; maximum_value = std::numeric_limits<CFIndex>::max();
    bool threw = false;
    try { (void)run(&cache,true,output); } catch (const std::bad_alloc &) { threw = true; }
    check(threw && fault_hits == 1 && array_attempts == 1 &&
          last_array_request == std::numeric_limits<std::size_t>::max(),
          "source overflow boundary attempts exact SIZE_MAX array allocation");
    check(cstrings == 0 && releases == 1 && reserves == 0 && output == before,
          "overflow allocation interception occurs before unsafe API/storage use");
    cleanup();
}
void transfer_ownership() {
    for (unsigned large_output = 0; large_output < 2; ++large_output) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial.assign(128,u'Q');
        std::string output(large_output ? 512 : 3,'x');
        check(cache.serial.size() > std::string{}.capacity(),"heap-sized source witness");
        const bool old_heap = output.size() > std::string{}.capacity();
        check(run(&cache,true,output) && output == std::string(128,'Q'),"heap conversion transfers value");
        const Allocation *converted = nullptr;
        unsigned transfer_allocations = 0;
        for (unsigned i = 0; i < allocation_count; ++i) {
            const Allocation &a = allocations[i];
            if (!a.array && a.phase == Phase::assignment) converted = &a;
            if (a.phase == Phase::transfer) ++transfer_allocations;
        }
        check(converted && converted->pointer == output.data() && !converted->deleted,
              "conversion allocation becomes independently owned output, not COPY");
        check(transfer_allocations == 0 && old_output_deletions == (old_heap ? 1u : 0u),
              "transfer adds no copy allocation and retires old ownership once");
        cleanup();
        check(output == std::string(128,'Q'),"output remains valid after conversion/CF teardown");
    }
}
bool run_utf16(const rk3::ni::DeviceInformation *cache, bool connected, std::u16string &output) {
    expected_cache = cache; expected_connected = connected; wide_output = &output;
    utf16_call = true;
    ++utf16_parent_calls;
    armed = true;
    try {
        const bool result = rk3::ni::get_serial_number(cache,connected,output);
        // The byte string was local to the overload and is now retired.
        current_output = nullptr;
        armed = utf16_call = false; phase = Phase::none;
        return result;
    } catch (...) {
        current_output = nullptr;
        armed = utf16_call = false; phase = Phase::none;
        throw;
    }
}
void check_output_live(const std::u16string &output) {
    const Allocation *record = payload_record(output.data());
    check(!record || !record->deleted,"UTF16 destination owns live storage before value observation");
}
void check_copy_completed(const std::u16string &output) {
    check(byte_productions == 1 && wide_copies == 1,"actual producer then counted UTF16 COPY");
    check_output_live(output);
    check(output.data() != wide_payload,"UTF16 output storage is independent of widened temporary");
}
void check_heap_temporaries_retired() {
    const Allocation *bytes = payload_record(byte_payload), *wide = payload_record(wide_payload);
    check(bytes && wide && !bytes->array && !wide->array && bytes->deleted && wide->deleted &&
          bytes->deletion_order > wide->deletion_order && wide->deletion_order > 0,
          "completed wide payload retires before byte payload");
}
void first_utf16_ascii() {
    reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"RK3";
    std::u16string output = u"old-output";
    check(run_utf16(&cache,true,output),"actual public UTF16 serial overload succeeds");
    check_copy_completed(output);
    check(output == u"RK3","actual public UTF16 ASCII value");
    cleanup();
}
void utf16_guarded_false() {
    for (unsigned which = 0; which < 3; ++which) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"stored";
        std::u16string output(128,u'x'), before = output;
        const char16_t *old = output.data(); const std::size_t capacity = output.capacity();
        const auto *input = which == 2 ? &cache : nullptr;
        check(!run_utf16(input,which == 1,output),"UTF16 absent/disconnected input returns false");
        check(byte_productions == 1 && wide_copies == 0 && creates == 0 &&
              cpp_allocations == 0 && byte_payload == nullptr && wide_payload == nullptr,
              "false UTF16 path still calls actual byte producer once without conversion or COPY");
        check_output_live(output);
        check(output == before && output.data() == old && output.capacity() == capacity,
              "false UTF16 path preserves destination value and ownership");
        cleanup();
    }
}
void utf16_successful_values() {
    const std::u16string inputs[] = {u"",u"\u00e9",u"\U0001f600",
        std::u16string(u"A\0B",3),std::u16string(u"\0\u00e9",2)};
    const std::u16string outputs[] = {u"",std::u16string({0xffc3,0xffa9}),
        std::u16string({0xfff0,0xff9f,0xff98,0xff80}),u"A",u""};
    for (unsigned i = 0; i < 5; ++i) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial = inputs[i];
        cache.vendor = cache.product = u"decoy"; cache.vendor_id = 0x2222; cache.product_id = 0x4444;
        std::u16string output(128,u'x');
        check(run_utf16(&cache,true,output),"real UTF16 composition returns true including empty");
        check_copy_completed(output);
        check(output == outputs[i] && output.c_str()[output.size()] == 0,
              "literal signed-byte UTF16 values preserve real producer first-NUL behavior");
        check(cache.serial == inputs[i] && cache.vendor == u"decoy" && cache.product == u"decoy" &&
              cache.vendor_id == 0x2222 && cache.product_id == 0x4444,
              "distinct UTF16 output leaves borrowed cache unchanged");
        check(creates == 1 && lengths == 1 && maxima == 1 && releases == 1 && reserves == 1,
              "UTF16 path uses one real counted CF producer");
        cleanup();
    }
}
void utf16_aliased_output() {
    reset(); rk3::ni::DeviceInformation cache{}; cache.serial = u"\u00e9";
    check(run_utf16(&cache,true,cache.serial),"UTF16 output may alias a mutable cached serial");
    check_copy_completed(cache.serial);
    check(cache.serial == std::u16string({0xffc3,0xffa9}) && source_released,
          "aliased field changes only after input/CF borrowing finished");
    cleanup();
}
void utf16_copy_ownership() {
    for (unsigned ample = 0; ample < 2; ++ample) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial.assign(128,u'Q');
        check(128 > std::string{}.capacity() && 128 > std::u16string{}.capacity(),
              "UTF16 ownership witness has real native heap-sized byte and wide payloads");
        std::u16string output(ample ? 512 : 3,u'x');
        const char16_t *old = output.data(); const std::size_t capacity = output.capacity();
        check((capacity >= 128) == (ample != 0),"UTF16 COPY witnesses distinguish old capacity");
        check(run_utf16(&cache,true,output),"heap UTF16 serial composition succeeds");
        check_copy_completed(output);
        check_heap_temporaries_retired();
        if (ample) check(output.data() == old && output.capacity() == capacity,
                         "ample old storage retains original in-place counted COPY");
        check(output == std::u16string(128,u'Q'),"UTF16 COPY survives both temporary destructions");
        cleanup();
    }
}
void utf16_producer_faults() {
    const Fault cases[] = {Fault::create,Fault::length,Fault::maximum,Fault::assignment};
    for (unsigned i = 0; i < 4; ++i) {
        reset(); rk3::ni::DeviceInformation cache{}; cache.serial.assign(128,u'S');
        std::u16string output(256,u'x'), before = output;
        const char16_t *old = output.data(); const std::size_t capacity = output.capacity();
        fault = cases[i]; bool expected = false, wrong = false;
        try { (void)run_utf16(&cache,true,output); }
        catch (const BoundaryFailure &) { expected = i < 3; wrong = !expected; }
        catch (const std::bad_alloc &) { expected = i == 3; wrong = !expected; }
        catch (...) { wrong = true; }
        check(expected && !wrong && fault_hits == 1 && byte_productions == 1 && wide_copies == 0,
              "real producer exceptions propagate without a UTF16 COPY or false conversion");
        check(byte_payload == nullptr && wide_payload == nullptr && reserves == 0 &&
              releases == (i == 0 ? 0u : 1u),
              "producer fault precedes completed outer byte/wide payloads");
        check_output_live(output);
        check(output == before && output.data() == old && output.capacity() == capacity,
              "producer exception preserves UTF16 destination before assignment");
        // The fresh outer byte string is normally empty on this real path;
        // no invented heap-byte destructor observation is claimed here.
        cleanup();
    }
}
void utf16_allocation_faults() {
    for (unsigned copy = 0; copy < 2; ++copy) {
        for (unsigned alias = 0; alias < 2; ++alias) {
            reset(); rk3::ni::DeviceInformation cache{};
            cache.serial.assign(128,alias ? u'\u00e9' : u'Q');
            std::u16string separate(3,u'x');
            std::u16string &output = alias ? cache.serial : separate;
            const std::u16string before = output;
            const char16_t *old = output.data(); const std::size_t capacity = output.capacity();
            const std::size_t expected_units = alias ? 256 : 128;
            check(expected_units > std::string{}.capacity() &&
                  expected_units > std::u16string{}.capacity() && capacity < expected_units,
                  "allocation faults use real heap payloads and insufficient old COPY capacity");
            fault = copy ? Fault::wide_copy : Fault::wide_construct;
            bool threw = false, wrong = false;
            try { (void)run_utf16(&cache,true,output); }
            catch (const std::bad_alloc &) { threw = true; }
            catch (...) { wrong = true; }
            check(threw && !wrong && fault_hits == 1 && byte_productions == 1 &&
                  wide_copies == (copy ? 1u : 0u),
                  "actual widening/COPY allocation exception propagates through UTF16 caller");
            const Allocation *bytes = payload_record(byte_payload);
            check(bytes && !bytes->array && bytes->deleted && bytes->deletion_order > 0,
                  "widening or COPY allocation failure retires actual owned byte payload");
            if (copy) check_heap_temporaries_retired();
            else check(wide_payload == nullptr,"construction failure has no completed wide payload");
            check_output_live(output);
            check(output == before && output.data() == old && output.capacity() == capacity,
                  "actual allocation failure preserves distinct or aliased UTF16 field");
            cleanup();
        }
    }
}
void counted_helper_cases() {
    // Internal counted conversion witnesses, not values the .35 producer can
    // publish after its separate C-string truncation stage.
    const char counted[] = {'A',static_cast<char>(0x80),0,static_cast<char>(0xff),'Z',0};
    const char heap[129] = {
        'Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q',
        'Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q',
        'Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q',
        'Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q',
        'Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q',
        'Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q',
        'Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q',
        'Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q','Q',0};
    const char *inputs[] = {"","RK3",counted,heap};
    const std::size_t counts[] = {0,3,5,128};
    const std::u16string expected[] = {u"",u"RK3",std::u16string({0x0041,0xff80,0,0xffff,0x005a}),
                                     std::u16string(128,u'Q')};
    for (unsigned i = 0; i < 4; ++i) {
        reset(); ++helper_cases; armed = true; phase = Phase::wide_construct;
        {
            const std::u16string value = rk3::ni::widen_serial_bytes(inputs[i],counts[i]);
            check(value == expected[i] && value.c_str()[value.size()] == 0,
                  "private counted helper preserves literal signed bytes and embedded NUL suffix");
        }
        armed = false; phase = Phase::none; cleanup();
    }
}
void counted_helper_limits() {
    const char sentinel[] = {0};
    const std::size_t counts[] = {std::size_t(UINT64_C(0x7ffffffffffffff0)),
                                 std::numeric_limits<std::size_t>::max()};
    for (std::size_t count : counts) {
        reset(); ++helper_cases; oversized_helper = true; armed = true; phase = Phase::wide_construct;
        bool length_error = false, wrong = false;
        try { (void)rk3::ni::widen_serial_bytes(sentinel,count); }
        catch (const std::length_error &) { length_error = true; }
        catch (...) { wrong = true; }
        armed = false; phase = Phase::none; oversized_helper = false;
        check(length_error && !wrong,"oversized counted helper propagates standard length_error");
        // Native construction can reject the same count. This alone does not
        // prove the explicit original guard; source review retains that guard.
        // Exception diagnostic allocations are allowed within the bounded gate.
        cleanup();
    }
}

[[noreturn]] void terminated() noexcept {
    if (child == Child::release_throw && releases == 1 && reserves == 0 && cstrings == 1 &&
        release_throw_unwind_count == 0 && current_output && *current_output == "old-output") {
        constexpr char marker[] = "TERM_CF_RELEASE\n";
        (void)::write(STDOUT_FILENO,marker,sizeof(marker)-1);
        std::_Exit(74);
    }
    if (child == Child::reserve_throw && releases == 1 && source_released && reserves == 1 &&
        current_output && current_output->empty()) {
        constexpr char marker[] = "TERM_RESERVE\n";
        (void)::write(STDOUT_FILENO,marker,sizeof(marker)-1);
        std::_Exit(75);
    }
    constexpr char marker[] = "WRONG_TERMINATE\n";
    (void)::write(STDOUT_FILENO,marker,sizeof(marker)-1);
    std::_Exit(94);
}
int child_mode(const char *name) {
    reset();
    if (std::strcmp(name,"null-next") == 0) child = Child::null_next;
    else if (std::strcmp(name,"release-throw") == 0) child = Child::release_throw;
    else if (std::strcmp(name,"reserve-throw") == 0) child = Child::reserve_throw;
    else return 97;
    std::set_terminate(terminated);
    rk3::ni::DeviceInformation cache{}; cache.serial = u"stored";
    std::string output = "old-output";
    try { (void)run(&cache,true,output); }
    catch (...) {
        constexpr char marker[] = "ESCAPED_EXCEPTION\n";
        (void)::write(STDOUT_FILENO,marker,sizeof(marker)-1);
        return 95;
    }
    constexpr char marker[] = "UNEXPECTED_RETURN\n";
    (void)::write(STDOUT_FILENO,marker,sizeof(marker)-1);
    return 96;
}
void reap_killed(pid_t pid) {
    // Confirm a still-owned, unreaped child before signalling. ECHILD or any
    // unknown wait error loses ownership; never signal a possibly reused PID.
    int status = 0;
    pid_t observed;
    do { observed = ::waitpid(pid,&status,WNOHANG); } while (observed < 0 && errno == EINTR);
    if (observed != 0) return;
    if (::kill(pid,SIGKILL) < 0 && errno != ESRCH) return;
    while (::waitpid(pid,&status,0) < 0 && errno == EINTR) {}
}
void expect_child(const char *mode, int expected_exit, const char *expected_marker) {
    check(executable && executable[0] == '/',"self-spawn uses pinned absolute executable path");
    struct sigaction child_policy{};
    check(::sigaction(SIGCHLD,nullptr,&child_policy) == 0 &&
          child_policy.sa_handler == SIG_DFL && !(child_policy.sa_flags & SA_NOCLDWAIT),
          "child policy must retain waitable children with no custom reaper");
    int descriptors[2]; check(::pipe(descriptors) == 0,"create own child marker pipe");
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) { ::close(descriptors[0]); ::close(descriptors[1]); check(false,"initialize spawn actions"); }
    error = posix_spawn_file_actions_adddup2(&actions,descriptors[1],STDOUT_FILENO);
    if (!error) error = posix_spawn_file_actions_addclose(&actions,descriptors[0]);
    if (!error) error = posix_spawn_file_actions_addclose(&actions,descriptors[1]);
    pid_t pid = -1;
    char *arguments[] = {const_cast<char *>(executable),const_cast<char *>("--child"),const_cast<char *>(mode),nullptr};
    if (!error) error = ::posix_spawn(&pid,executable,&actions,nullptr,arguments,environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(descriptors[1]);
    if (error) { ::close(descriptors[0]); check(false,"spawn only own finite child mode"); }
    check(pid > 0,"owned spawned child PID");
    const int flags = ::fcntl(descriptors[0],F_GETFL);
    if (flags < 0 || ::fcntl(descriptors[0],F_SETFL,flags | O_NONBLOCK) < 0) {
        ::close(descriptors[0]); reap_killed(pid); check(false,"nonblocking child marker pipe");
    }
    char marker[128]{}; std::size_t used = 0; int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool finished = false;
    while (!finished) {
        const ssize_t n = ::read(descriptors[0],marker + used,sizeof(marker)-1-used);
        if (n > 0) used += static_cast<std::size_t>(n);
        const pid_t result = ::waitpid(pid,&status,WNOHANG);
        if (result == pid) finished = true;
        else if (result < 0 && errno != EINTR) {
            // ECHILD (or another unknown error) does not affirm ownership.
            ::close(descriptors[0]); check(false,"lost wait ownership of child");
        }
        if (!finished && std::chrono::steady_clock::now() >= deadline) {
            ::close(descriptors[0]); reap_killed(pid); check(false,"child mode exceeded ten-second cap");
        }
        if (!finished) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (;;) {
        const ssize_t n = ::read(descriptors[0],marker + used,sizeof(marker)-1-used);
        if (n <= 0) break;
        used += static_cast<std::size_t>(n);
    }
    ::close(descriptors[0]); marker[used] = 0;
    check(WIFEXITED(status) && WEXITSTATUS(status) == expected_exit &&
          std::strcmp(marker,expected_marker) == 0,
          "child requires exact controlled marker and exit, not escaped exception/signal/crash");
}
void child_contracts() {
    expect_child("null-next",73,"NULL_NEXT\n");
    expect_child("release-throw",74,"TERM_CF_RELEASE\n");
    expect_child("reserve-throw",75,"TERM_RESERVE\n");
}
}

int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1],"--child") == 0) return fixture::child_mode(argv[2]);
    fixture::check(argc == 1,"only normal fixture or finite child modes accepted");
    fixture::executable = argv[0];
    fixture::guarded_false();
    fixture::successful_values();
    fixture::nonpositive_maximum();
    fixture::buffer_thresholds();
    fixture::ignored_boolean();
    fixture::boundary_throws();
    fixture::allocation_throws();
    fixture::maximum_request();
    fixture::transfer_ownership();
    fixture::first_utf16_ascii();
    fixture::utf16_guarded_false();
    fixture::utf16_successful_values();
    fixture::utf16_aliased_output();
    fixture::utf16_copy_ownership();
    fixture::utf16_producer_faults();
    fixture::utf16_allocation_faults();
    fixture::counted_helper_cases();
    fixture::counted_helper_limits();
    fixture::cleanup();
    fixture::child_contracts();
    std::printf("NI serial tests: PASS (%u byte public + %u UTF16 public + %u internal helper cases + 3 controlled child modes)\n",
                fixture::parent_calls,fixture::utf16_parent_calls,fixture::helper_cases);
}
