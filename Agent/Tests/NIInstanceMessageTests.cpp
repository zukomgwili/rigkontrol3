#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include "../NIInstanceMessage.cpp"

namespace fixture {
bool armed = false;
unsigned allocations = 0, public_calls = 0, prefix_calls = 0;
void* allocate(std::size_t size) {
    if (armed) ++allocations;
    void* pointer = std::malloc(size ? size : 1);
    if (!pointer) throw std::bad_alloc();
    return pointer;
}
}

void* operator new(std::size_t size) { return fixture::allocate(size); }
void* operator new[](std::size_t size) { return fixture::allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace fixture {
void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
std::uint32_t word(const rk3::ni::InstanceMessageStorage& storage,
                   std::size_t offset) {
    std::uint32_t value;
    std::memcpy(&value, storage.data() + offset, sizeof(value));
    return value;
}
std::uint16_t unit(const rk3::ni::InstanceMessageStorage& storage,
                   std::size_t index) {
    std::uint16_t value;
    std::memcpy(&value, storage.data() + 28 + index * 2, sizeof(value));
    return value;
}
struct Frame {
    std::array<std::uint8_t, 16> before;
    rk3::ni::InstanceMessageStorage storage;
    std::array<std::uint8_t, 16> after;
    Frame() {
        before.fill(0x3c);
        storage.fill(0xa5);
        after.fill(0xc3);
        for (std::size_t i = 16; i < 24; ++i)
            storage[i] = static_cast<std::uint8_t>(0x80 + i);
    }
    void preserved() const {
        for (auto byte : before) check(byte == 0x3c, "leading canary");
        for (auto byte : after) check(byte == 0xc3, "trailing canary");
        for (std::size_t i = 16; i < 24; ++i)
            check(storage[i] == 0x80 + i, "opaque caller words unchanged");
    }
};
void metadata(const Frame& frame, std::uint32_t declared,
              std::uint32_t payload, std::uint32_t type = 0x02444e2b) {
    const auto actual_payload = word(frame.storage, 0);
    // Check before constructing any pointer based on the advertised count,
    // including in a compiled wrong-count control.
    check(actual_payload >= 16 && actual_payload <= 1040,
          "bounded payload before wire view");
    check(actual_payload == payload, "declared payload count");
    check(word(frame.storage, 4) == 16, "reply offset 16");
    check(word(frame.storage, 8) == 0, "reply count zero");
    check(word(frame.storage, 12) == type, "full message type");
    check(word(frame.storage, 24) == declared, "declared unit count");
    const auto* wire = frame.storage.data() + 12;
    const auto* wire_end = wire + actual_payload;
    check(wire_end <= frame.storage.data() + 1052, "wire stays in storage");
    frame.preserved();
}
void zeros_from(const Frame& frame, std::size_t first, const char* message) {
    for (std::size_t i = first; i < 512; ++i)
        check(unit(frame.storage, i) == 0, message);
}
void q_prefix(const Frame& frame, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        check(unit(frame.storage, i) == 0x0051, "literal Q prefix");
}
void call(Frame& frame, const std::u16string& source,
          std::uint32_t type = 0x00444e2b) {
    const auto snapshot = source;
    const auto* data = source.data();
    const auto capacity = source.capacity();
    allocations = 0;
    armed = true;
    rk3::ni::write_instance_message(frame.storage, type, source);
    armed = false;
    ++public_calls;
    check(allocations == 0, "public writer allocates no native heap");
    check(source == snapshot && source.data() == data && source.capacity() == capacity,
          "public source value and storage unchanged");
}
void ascii() {
    Frame frame;
    const std::u16string source = u"RK3";
    call(frame, source);
    check(word(frame.storage, 0) == 24, "ASCII payload 24");
    metadata(frame, 4, 24);
    check(unit(frame.storage, 0) == 0x0052, "ASCII R");
    check(unit(frame.storage, 1) == 0x004b, "ASCII K");
    check(unit(frame.storage, 2) == 0x0033, "ASCII 3");
    zeros_from(frame, 3, "ASCII terminator and cleared tail");
    const auto* wire = frame.storage.data() + 12; // Count was validated above.
    check(wire[0] == 0x2b && wire[1] == 0x4e && wire[2] == 0x44 && wire[3] == 0x02,
          "wire starts at full type, not internal control words");
    check(wire[4] == 0x90 && wire[11] == 0x97, "wire includes caller prestate");
    check(wire[12] == 4 && wire[16] == 0x52 && wire[22] == 0 && wire[23] == 0,
          "literal ASCII wire offsets and final NUL");
}
void empty() {
    Frame frame;
    call(frame, u"");
    metadata(frame, 1, 18);
    zeros_from(frame, 0, "empty clears whole character region");
}
void raw_types() {
    Frame already, high;
    call(already, u"", 0x02000000);
    metadata(already, 1, 18, 0x02000000);
    call(high, u"", 0x81444e2b);
    metadata(high, 1, 18, 0x83444e2b);
}
void embedded_nul() {
    Frame frame;
    const std::u16string source = {u'A', u'\0', u'B'};
    call(frame, source);
    metadata(frame, 4, 24);
    check(unit(frame.storage, 0) == 0x0041, "embedded NUL keeps A");
    zeros_from(frame, 1, "embedded NUL stops before B but retains declared size");
}
void length_edges() {
    struct Case { std::size_t length; std::uint32_t count, payload; std::size_t kept; };
    const Case cases[] = {{510, 511, 1038, 510}, {511, 512, 1040, 511},
                          {512, 512, 1040, 511}, {700, 512, 1040, 511}};
    for (const auto& value : cases) {
        Frame frame;
        const std::u16string source(value.length, u'Q');
        call(frame, source);
        metadata(frame, value.count, value.payload);
        q_prefix(frame, value.kept);
        zeros_from(frame, value.kept, "length edge terminator and cleared tail");
    }
}
void natural_surrogates() {
    Frame short_frame, edge_frame;
    const std::u16string short_source = {u'Q', static_cast<char16_t>(0xd800)};
    call(short_frame, short_source);
    metadata(short_frame, 3, 22);
    check(unit(short_frame.storage, 0) == 0x0051 && unit(short_frame.storage, 1) == 0xd800,
          "natural unpaired high surrogate retained");
    zeros_from(short_frame, 2, "natural short terminator");
    std::u16string edge_source(511, u'Q');
    edge_source[510] = static_cast<char16_t>(0xd800);
    call(edge_frame, edge_source);
    metadata(edge_frame, 512, 1040);
    q_prefix(edge_frame, 510);
    check(unit(edge_frame.storage, 510) == 0xd800,
          "natural end at cap retains preceding high surrogate");
    zeros_from(edge_frame, 511, "natural edge terminator");
}
void capacity_surrogates() {
    struct Case { std::uint16_t previous, last; bool backs_off; };
    const Case cases[] = {{0xd800, 0xdc00, true}, {0xdbff, 0x005a, true},
                          {0xd7ff, 0xdc00, false}, {0xdc00, 0xd800, false}};
    for (const auto& value : cases) {
        Frame frame;
        std::u16string source(512, u'Q');
        source[510] = static_cast<char16_t>(value.previous);
        source[511] = static_cast<char16_t>(value.last);
        call(frame, source);
        metadata(frame, 512, 1040);
        q_prefix(frame, 510);
        if (value.backs_off) {
            check(unit(frame.storage, 510) == 0, "capacity terminator backs over previous high surrogate");
            check(unit(frame.storage, 511) == value.last,
                  "capacity residual after earlier NUL retained");
        } else {
            check(unit(frame.storage, 510) == value.previous, "non-high previous unit retained");
            check(unit(frame.storage, 511) == 0, "non-high previous unit does not back off");
        }
    }
}
void leading_nul_and_raw_units() {
    Frame leading, units;
    std::u16string leading_source(700, u'Q');
    leading_source[0] = u'\0';
    call(leading, leading_source);
    metadata(leading, 512, 1040);
    zeros_from(leading, 0, "leading NUL leaves clipped declared count but all data zero");
    const std::u16string raw_source = {static_cast<char16_t>(0xffc3),
        static_cast<char16_t>(0xffa9), static_cast<char16_t>(0xffff)};
    call(units, raw_source);
    metadata(units, 4, 24);
    check(unit(units.storage, 0) == 0xffc3 && unit(units.storage, 1) == 0xffa9 &&
          unit(units.storage, 2) == 0xffff, "raw U16 values not decoded or converted");
    zeros_from(units, 3, "raw-unit terminator and tail");
}
void reuse() {
    Frame frame;
    std::u16string source(512, u'Q');
    source[510] = static_cast<char16_t>(0xd800);
    source[511] = static_cast<char16_t>(0xdc00);
    call(frame, source);
    metadata(frame, 512, 1040);
    check(unit(frame.storage, 511) == 0xdc00, "reuse witness starts with residual");
    call(frame, u"A");
    metadata(frame, 2, 20);
    check(unit(frame.storage, 0) == 0x0041, "reuse short A");
    zeros_from(frame, 1, "reuse clears old long tail and residual");
}
// Internal arithmetic/prefix oracles only: these are not giant native strings
// or claimed actual serial-producer outputs. Every source has 512 valid units.
void private_prefix_cases() {
    struct Case { std::uint64_t length; std::uint32_t count, payload; std::size_t kept; };
    const Case cases[] = {
        {0, 1, 18, 0}, {1, 2, 20, 1}, {510, 511, 1038, 510},
        {511, 512, 1040, 511}, {512, 512, 1040, 511},
        {UINT64_C(0xfffffffe), 512, 1040, 511},
        {UINT64_C(0xffffffff), 0, 16, 0},
        {UINT64_C(0x100000000), 1, 18, 0},
        {UINT64_C(0x1ffffffff), 0, 16, 0},
        {std::numeric_limits<std::size_t>::max(), 0, 16, 0}
    };
    for (const auto& value : cases) {
        for (bool leading_zero : {false, true}) {
            Frame frame;
            std::array<char16_t, 512> prefix;
            prefix.fill(u'Q');
            if (leading_zero) prefix[0] = u'\0';
            const auto snapshot = prefix;
            allocations = 0;
            armed = true;
            rk3::ni::write_counted_instance_message(frame.storage, 0x00444e2b,
                                                   prefix.data(), value.length);
            armed = false;
            ++prefix_calls;
            check(allocations == 0, "private writer allocates no native heap");
            check(prefix == snapshot, "private bounded source unchanged");
            metadata(frame, value.count, value.payload);
            if (leading_zero) {
                zeros_from(frame, 0, "private leading NUL stops without count repair");
            } else {
                q_prefix(frame, value.kept);
                zeros_from(frame, value.kept, "private wrapped/clipped terminator and tail");
            }
        }
    }
}
}

int main() {
    fixture::ascii();
    fixture::empty();
    fixture::raw_types();
    fixture::embedded_nul();
    fixture::length_edges();
    fixture::natural_surrogates();
    fixture::capacity_surrogates();
    fixture::leading_nul_and_raw_units();
    fixture::reuse();
    fixture::private_prefix_cases();
    std::printf("PASS: %u public writer calls, %u private bounded-prefix cases\n",
                fixture::public_calls, fixture::prefix_calls);
}
