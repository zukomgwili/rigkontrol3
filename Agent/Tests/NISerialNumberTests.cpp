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
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include "../NIInformation.h"

extern char **environ;

// Only synthetic cached values and real data-only CF/std operations are used.
// SDK declarations and wrappers precede redirects into the actual module.
namespace fixture {
void check(bool value, const char *message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
enum class Phase { none, buffer, assignment, reserve, transfer };
enum class Fault { none, create, length, maximum, array, assignment };
enum class Child { none, null_next, release_throw, reserve_throw };
struct BoundaryFailure {};
struct Allocation {
    void *pointer{};
    std::size_t bytes{};
    Phase phase{};
    bool array{}, deleted{};
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
unsigned parent_calls = 0;
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
            (!array && phase == Phase::assignment && fault == Fault::assignment)) {
            ++fault_hits;
            throw std::bad_alloc();
        }
    }
    void *pointer = std::malloc(size ? size : 1);
    if (!pointer) throw std::bad_alloc();
    if (armed && phase != Phase::none) {
        check(allocation_count < 64,"bounded allocation trace");
        allocations[allocation_count++] = {pointer,size,phase,array,false};
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
}
CFStringRef make(CFAllocatorRef allocator, const UniChar *units, CFIndex count,
                 CFAllocatorRef deallocator) {
    ++creates; mark('C'); phase = Phase::none;
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
#include "../NISerialNumber.cpp"
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
    fixture::cleanup();
    fixture::child_contracts();
    std::printf("NI serial producer tests: PASS (%u public parent calls + 3 controlled child modes)\n",
                fixture::parent_calls);
}
