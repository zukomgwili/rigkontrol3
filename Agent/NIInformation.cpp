#include "NIInformation.h"
#include "../Driver/NativeClient.h"
#include <algorithm>
#include <chrono>
#include <thread>

#ifndef RK3_NI_INFORMATION_SLEEP_FOR
#define RK3_NI_INFORMATION_SLEEP_FOR(duration) std::this_thread::sleep_for(duration)
#define RK3_NI_INFORMATION_DEFAULT_SLEEP
#endif

namespace rk3 { namespace ni {
namespace {
static_assert(sizeof(UInt16) == 2 && sizeof(char16_t) == 2, "16-bit information units");
static_assert(sizeof(AudioObjectID) == 4 && sizeof(std::uint64_t) == 8, "Owner lease widths");

std::u16string copy_cf_string(CFStringRef source) {
    const auto length = static_cast<std::size_t>(CFStringGetLength(source));
    std::u16string value(length, u' ');
    const UniChar *characters = CFStringGetCharactersPtr(source);
    if (characters) {
        std::copy_n(characters, length, value.begin());
    } else if (length) {
        // Retain NI's temporary-storage threshold and normal-only array cleanup.
        UniChar *temporary = length <= 65536
            ? static_cast<UniChar *>(__builtin_alloca((length + 1) * sizeof(UniChar)))
            : new UniChar[length + 1];
        CFStringGetCharacters(source, CFRangeMake(0, static_cast<CFIndex>(length)), temporary);
        std::copy_n(temporary, length, value.begin());
        if (length > 65536) delete[] temporary;
    }
    return value;
}

std::u16string convert_string(const std::uint8_t *bytes) {
    CFStringRef source = CFStringCreateWithCStringNoCopy(nullptr,
        reinterpret_cast<const char *>(bytes), kCFStringEncodingUTF8, kCFAllocatorNull);
    if (!source) return {};
    // NI releases CF only after successful owned construction, not on unwind.
    std::u16string value = copy_cf_string(source);
    CFRelease(source);
    return value;
}

bool acquire(OwnerLease lease, bool strings, RK3NativeUSBIdentity& identity,
             RK3NativeSpec& spec) {
    const RK3NativeUSBIdentityQuery identity_query{RK3_CONTROL_VERSION, 0, lease.generation};
    if (rk3_native_copy_usb_identity(lease.device, &identity_query, &identity) || identity.status)
        return false;
    const std::uint32_t required = strings ? UINT32_C(0xe3) : UINT32_C(0x03);
    if ((identity.defined_mask & required) != required) return false;
    const RK3NativeSpecQuery spec_query{RK3_CONTROL_VERSION, 0, lease.generation};
    return rk3_native_copy_spec(lease.device, &spec_query, &spec) == 0 && spec.status == 0;
}
}

bool get_device_info(OwnerLease lease, DeviceInformation& output, bool strings) {
    if (!lease.device || !lease.generation) return false;
    RK3NativeUSBIdentity identity{};
    RK3NativeSpec spec{};
    for (unsigned attempt = 0; ; ++attempt) {
        if (!acquire(lease, strings, identity, spec)) return false;
        if (spec.raw[0] || spec.raw[1] || attempt == 3) break;
        RK3_NI_INFORMATION_SLEEP_FOR(std::chrono::nanoseconds(100000000LL * (attempt + 1)));
    }
    output.vendor_id = identity.vendor_id;
    output.product_id = identity.product_id;
    output.spec_word0 = static_cast<std::uint16_t>(spec.raw[0] | (std::uint16_t(spec.raw[1]) << 8));
    if (strings) {
        { const auto value = convert_string(identity.manufacturer);
          output.vendor.assign(value.c_str(), value.size()); }
        { const auto value = convert_string(identity.product);
          output.product.assign(value.c_str(), value.size()); }
        { const auto value = convert_string(identity.serial);
          output.serial.assign(value.c_str(), value.size()); }
    }
    return true;
}
}}

#ifdef RK3_NI_INFORMATION_DEFAULT_SLEEP
#undef RK3_NI_INFORMATION_DEFAULT_SLEEP
#undef RK3_NI_INFORMATION_SLEEP_FOR
#endif
