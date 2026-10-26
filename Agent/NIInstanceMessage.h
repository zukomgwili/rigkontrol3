#ifndef RK3_NI_INSTANCE_MESSAGE_H
#define RK3_NI_INSTANCE_MESSAGE_H
#include <array>
#include <cstdint>
#include <string>

namespace rk3 { namespace ni {
using InstanceMessageStorage = std::array<std::uint8_t, 1052>;
// Caller exclusively owns storage and keeps value alive/stable and separate
// from it. Bytes 16..23 retain caller prestate; establish it before observation
// or transport. This writer neither interprets those bytes nor sends a message.
void write_instance_message(InstanceMessageStorage& storage,
                            std::uint32_t raw_type,
                            const std::u16string& value);
}}
#endif
