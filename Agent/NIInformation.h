#ifndef RK3_NI_INFORMATION_H
#define RK3_NI_INFORMATION_H
#include <CoreAudio/CoreAudio.h>
#include <cstdint>
#include <string>

namespace rk3 { namespace ni {
// Borrowed current-owner locator, not persistent identity across owner restart.
struct OwnerLease { AudioObjectID device; std::uint64_t generation; };
struct DeviceInformation {
    std::uint16_t vendor_id{}, product_id{}, spec_word0{};
    std::u16string vendor, product, serial;
};
// Unavailable input returns false without output writes. Once acquired, header
// and strings update in order; allocation exceptions propagate with prior writes.
bool get_device_info(OwnerLease, DeviceInformation&, bool strings = true);
}}
#endif
