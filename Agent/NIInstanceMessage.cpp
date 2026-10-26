#include "NIInstanceMessage.h"
#include <climits>
#include <cstddef>
#include <cstring>
#include <strings.h>

namespace rk3 { namespace ni {
namespace {
static_assert(CHAR_BIT == 8 && sizeof(std::uint8_t) == 1);
static_assert(sizeof(std::uint16_t) == 2 && sizeof(char16_t) == 2);
static_assert(sizeof(std::uint32_t) == 4 && sizeof(std::uint64_t) == 8);
static_assert(sizeof(std::size_t) == 8 && sizeof(InstanceMessageStorage) == 1052);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

void store_word(InstanceMessageStorage& storage, std::size_t offset,
                std::uint32_t value) {
    std::memcpy(storage.data() + offset, &value, sizeof(value));
}
void store_unit(InstanceMessageStorage& storage, std::size_t offset,
                std::uint16_t value) {
    std::memcpy(storage.data() + offset, &value, sizeof(value));
}
std::uint16_t load_unit(const InstanceMessageStorage& storage,
                        std::size_t offset) {
    std::uint16_t value;
    std::memcpy(&value, storage.data() + offset, sizeof(value));
    return value;
}

void write_counted_instance_message(InstanceMessageStorage& storage,
                                    std::uint32_t raw_type,
                                    const char16_t* source,
                                    std::uint64_t logical_length) {
    store_word(storage, 4, 16);
    store_word(storage, 8, 0);
    store_word(storage, 12, raw_type | 0x02000000U);
    ::bzero(storage.data() + 28, 1024);
    store_word(storage, 0, 16);
    std::uint32_t count = static_cast<std::uint32_t>(logical_length);
    ++count; // NI increments the low 32 bits before the unsigned clamp.
    if (count > 512) count = 512;
    store_word(storage, 24, count);
    store_word(storage, 0, 16 + 2 * count);

    std::size_t cursor = 28;
    std::size_t ordinal = 1;
    std::uint16_t current = source[0]; // Also read for wrapped count zero.
    while (current != 0) {
        store_unit(storage, cursor, current);
        if (ordinal >= count) {
            // Previous word is within the whole storage, even at +26 for n0/1.
            if ((load_unit(storage, cursor - 2) & 0xfc00U) == 0xd800U)
                cursor -= 2;
            // NI leaves the copied final unit after an earlier backoff NUL.
            break;
        }
        cursor += 2;
        current = source[ordinal];
        ++ordinal;
    }
    store_unit(storage, cursor, 0);
}
}

void write_instance_message(InstanceMessageStorage& storage,
                            std::uint32_t raw_type,
                            const std::u16string& value) {
    write_counted_instance_message(storage, raw_type, value.data(), value.size());
}
}}
