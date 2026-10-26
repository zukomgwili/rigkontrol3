#include "NISerialNumber.h"
#include "NIInformation.h"
#include <CoreFoundation/CoreFoundation.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

#ifndef RK3_NI_SERIAL_RESERVE_ZERO
#define RK3_NI_SERIAL_RESERVE_ZERO(value) (value).reserve(0)
#define RK3_NI_SERIAL_DEFAULT_RESERVE
#endif

namespace rk3 { namespace ni {
namespace {
static_assert(sizeof(char16_t) == 2 && sizeof(UniChar) == 2, "UTF16 unit width");
static_assert(alignof(char16_t) >= alignof(UniChar), "Darwin CF input alignment");
static_assert(sizeof(CFIndex) == 8 && std::numeric_limits<CFIndex>::is_signed,
              "Signed 64-bit CF count");
static_assert(sizeof(std::size_t) == 8, "64-bit allocation request");

struct CFReleaseGuard {
    CFStringRef value;
    ~CFReleaseGuard() noexcept { CFRelease(value); }
};

std::string convert_serial(const std::u16string& serial) {
    std::string result;
    // Darwin CF ABI view only: preserve the borrowed pointer/count and lifetime.
    // No C++ access through the reinterpreted pointer or extra input allocation.
    CFStringRef source = CFStringCreateWithCharactersNoCopy(nullptr,
        reinterpret_cast<const UniChar*>(serial.data()),
        static_cast<CFIndex>(serial.size()), kCFAllocatorNull);
    CFReleaseGuard release{source};
    const CFIndex length = CFStringGetLength(source);
    const CFIndex maximum = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
    if (maximum > 0) {
        const std::uint64_t capacity = static_cast<std::uint64_t>(maximum) + 1;
        if (maximum <= 65536) {
            char* buffer = static_cast<char*>(__builtin_alloca(static_cast<std::size_t>(capacity)));
            CFStringGetCString(source, buffer, static_cast<CFIndex>(capacity), kCFStringEncodingUTF8);
            result.assign(buffer);
        } else {
            const std::size_t request = capacity <= static_cast<std::uint64_t>(std::numeric_limits<CFIndex>::max())
                ? static_cast<std::size_t>(capacity) : std::numeric_limits<std::size_t>::max();
            char* buffer = static_cast<char*>(::operator new[](request));
            CFStringGetCString(source, buffer, static_cast<CFIndex>(capacity), kCFStringEncodingUTF8);
            result.assign(buffer);
            // NI's selected exceptional path releases CF, but not this array.
            ::operator delete[](buffer);
        }
    }
    return result;
}

void transfer_serial(std::string& output, std::string&& converted) noexcept {
    output.clear();
    RK3_NI_SERIAL_RESERVE_ZERO(output);
    output = std::move(converted);
    converted.clear();
}
}

bool get_serial_number(const DeviceInformation* cached_info,
                       bool connected, std::string& output) {
    if (!cached_info || !connected) return false;
    auto converted = convert_serial(cached_info->serial);
    transfer_serial(output, std::move(converted));
    return true;
}
}}

#ifdef RK3_NI_SERIAL_DEFAULT_RESERVE
#undef RK3_NI_SERIAL_DEFAULT_RESERVE
#undef RK3_NI_SERIAL_RESERVE_ZERO
#endif
