#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace PSB {
    class PSBRawOwner;

    namespace detail {
        // The Android binary repeats these expressions inline in collection
        // consumers.  It cannot distinguish hand-written expressions from a
        // header-inline view type, hence the explicit _guess suffix.
        template <typename T>
        T ReadUnaligned_guess(const std::uint8_t *data) {
            T result;
            std::memcpy(&result, data, sizeof(result));
            return result;
        }

        inline std::uint32_t
        ReadPackedCount_guess(const std::uint8_t *data) {
            switch(data[0]) {
                case 0x0d:
                    return data[1];
                case 0x0e:
                    return ReadUnaligned_guess<std::uint16_t>(data + 1);
                case 0x0f:
                    return ReadUnaligned_guess<std::uint32_t>(data + 1) &
                        0xffffffu;
                case 0x10:
                    return ReadUnaligned_guess<std::uint32_t>(data + 1);
                default:
                    return 0;
            }
        }

        inline std::uint32_t
        ReadPackedValue_guess(const std::uint8_t *data, std::uint8_t tag) {
            // sub_59641C @ 0x596498 and sub_59659C @ 0x5966E4 accept
            // (tag - 0x0d) <= 4.  Tag 0x11 deliberately follows AArch64's
            // register-shift modulo rule.
            if(static_cast<std::uint32_t>(tag) - 0x0du > 4u) {
                return 0;
            }
            const auto shift =
                (8u * (0x10u - static_cast<std::uint32_t>(tag))) & 31u;
            return ReadUnaligned_guess<std::uint32_t>(data) &
                (0xffffffffu >> shift);
        }

        struct PackedArrayView_guess {
            const std::uint8_t *begin;
            std::uint32_t count;
            int width;
            std::uint8_t valueTag;
            const std::uint8_t *values;
            const std::uint8_t *end;

            explicit PackedArrayView_guess(const std::uint8_t *data) :
                begin(data), count(ReadPackedCount_guess(data)),
                width(static_cast<int>(
                          data[static_cast<std::ptrdiff_t>(data[0]) - 0x0b]) -
                      0x0c),
                valueTag(
                    data[static_cast<std::ptrdiff_t>(data[0]) - 0x0b]),
                values(data + static_cast<std::ptrdiff_t>(data[0]) - 0x0a),
                // FindNameIndex @ 0x596478..0x596480 and
                // FindDictionaryValueOffset @ 0x596608..0x59667C form the
                // complete relative displacement in W before UXTW pointer
                // addition.  Keeping the header displacement inside this
                // uint32_t expression preserves that wraparound boundary.
                end(data +
                    (static_cast<std::uint32_t>(data[0]) - 0x0au +
                     count * static_cast<std::uint32_t>(width))) {}

            [[nodiscard]] std::uint32_t
            operator[](std::uint32_t index) const {
                return ReadPackedValue_guess(values + index * width,
                                             valueTag);
            }
        };

        // These three out-of-line boundaries are independently present in
        // libkrkr2.so and shared by dispatch and raw-node consumers.
        [[nodiscard]] bool
        FindNameIndex_guess(const std::uint8_t *names, const char *name,
                            std::uint32_t &nameIndex); // 0x59641C

        [[nodiscard]] bool FindDictionaryValueOffset_guess(
            const std::uint8_t *dictionary, std::uint32_t nameIndex,
            std::uint32_t &valueOffset); // 0x59659C

        void DecodeName_guess(std::string &name, const PSBRawOwner *owner,
                              std::uint32_t nameIndex); // 0x597B1C
    } // namespace detail
} // namespace PSB
