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
        // header-inline helper, hence the explicit _guess suffix.
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

        // The same-game Win32 psbfile.dll independently materializes this
        // four-DWORD record before calling its packed-array operator[].  The
        // stripped Android build scalar-replaces the corresponding values in
        // FindNameIndex @ 0x59641C, FindDictionaryValueOffset @ 0x59659C and
        // DecodeName @ 0x597B1C.  The cross-build evidence supports the
        // four-field source topology, but not the exact Android type name.
        struct PsbArray_guess {
            std::uint32_t nBytes;
            std::uint32_t nElementCount;
            std::uint32_t nSizeOf;
            const std::uint8_t *pCode;

            explicit PsbArray_guess(const std::uint8_t *code) {
                const auto l =
                    static_cast<std::ptrdiff_t>(code[0]) - 0x0b;
                nElementCount = ReadPackedCount_guess(code);
                nSizeOf = static_cast<std::uint32_t>(code[l]) - 0x0cu;
                pCode = code + l + 1;
                // The Android consumers form the complete relative
                // displacement in W before adding it to the original table
                // base with UXTW.  Do not derive the end from pCode.
                nBytes = nElementCount * nSizeOf +
                    static_cast<std::uint32_t>(l + 1);
            }

            [[nodiscard]] std::uint32_t
            operator[](std::uint32_t index) const {
                // Android accepts widths 1..5. Width 5 deliberately follows
                // AArch64's register-shift modulo rule and keeps the low byte;
                // invalid widths return zero without forming the value
                // address. The index product remains W32 before UXTW.
                if(nSizeOf - 1u >= 5u) {
                    return 0;
                }
                const auto shift = (8u * (4u - nSizeOf)) & 31u;
                return ReadUnaligned_guess<std::uint32_t>(
                           pCode + index * nSizeOf) &
                    (0xffffffffu >> shift);
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
