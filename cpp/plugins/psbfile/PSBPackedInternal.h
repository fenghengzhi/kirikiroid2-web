#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "MsgIntf.h"
#include "tjs.h"

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

        // PSBRawNode_GetTypeCategory_guess @ 0x599554 is this tag classifier
        // after Android -O3 inlines it into the public raw-node wrapper.
        // PSBValueDispatch_CreateVariant_guess @ 0x59673C contains the same
        // category partition after its call is inlined and folded into the
        // outer switch. IsInstanceOf @ 0x596E24, EnumMembers @ 0x596F50,
        // GetCount @ 0x5975E0, PropGet @ 0x597854,
        // PropGetByNum @ 0x5976C4, GetString @ 0x598B58,
        // GetDictionaryKeys @ 0x598E64, ContainsDictionaryKey @ 0x5995D8 and
        // PSBMedia::GetListAt @ 0x5999F4 keep category-specialized inline residuals;
        // their same-lineage iOS counterparts retain calls to this shared
        // classifier.
        // The exact original name is stripped, hence the _guess suffix.
        inline int GetTypeCategory_guess(std::uint8_t tag) {
            switch(tag) {
                case 0x01:
                case 0x23:
                case 0x24:
                case 0x25:
                case 0x26:
                case 0x3f:
                    return 0;
                case 0x02:
                case 0x03:
                case 0x27:
                case 0x2f:
                case 0x33:
                case 0x37:
                case 0x3b:
                    return 1;
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                case 0x08:
                case 0x09:
                case 0x0a:
                case 0x0b:
                case 0x0c:
                case 0x28:
                case 0x29:
                case 0x30:
                case 0x31:
                case 0x34:
                case 0x35:
                case 0x38:
                case 0x39:
                case 0x3c:
                case 0x3d:
                    return 2;
                case 0x1d:
                case 0x1e:
                case 0x1f:
                case 0x2e:
                case 0x41:
                    return 3;
                case 0x15:
                case 0x16:
                case 0x17:
                case 0x18:
                case 0x2c:
                    return 4;
                case 0x19:
                case 0x1a:
                case 0x1b:
                case 0x1c:
                case 0x2d:
                    return 5;
                case 0x20:
                    return 6;
                case 0x21:
                    return 7;
                default:
                    TVPThrowExceptionMessage(TJS_W(
                        "psb: internal error: unknown internal type detected.\n"));
            }
            return -1;
        }

        // Android arm64 GetInt/GetDouble/CreateVariant contain complete
        // inlined copies of these two scalar decoders. iOS arm64 independently
        // preserves the same shared decoder boundaries and caller topology. Their
        // names/member-free tokens remain stripped.
        inline tjs_int DecodeInteger32_guess(const std::uint8_t *node) {
            switch(node[0]) {
                case 0x05:
                    return static_cast<std::int8_t>(node[1]);
                case 0x06:
                    return ReadUnaligned_guess<std::int16_t>(node + 1);
                case 0x07:
                    return static_cast<tjs_int>(
                        ReadUnaligned_guess<std::uint16_t>(node + 1) |
                        (static_cast<std::uint32_t>(
                             static_cast<std::int8_t>(node[3]))
                         << 16));
                case 0x08:
                    return ReadUnaligned_guess<std::int32_t>(node + 1);
                default:
                    return 0;
            }
        }

        inline tjs_int64 DecodeInteger64_guess(const std::uint8_t *node) {
            switch(node[0]) {
                case 0x09:
                    return static_cast<tjs_int64>(
                        ReadUnaligned_guess<std::uint32_t>(node + 1) |
                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                             static_cast<std::int8_t>(node[5])))
                         << 32));
                case 0x0a:
                    return static_cast<tjs_int64>(
                        ReadUnaligned_guess<std::uint32_t>(node + 1) |
                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                             ReadUnaligned_guess<std::int16_t>(node + 5)))
                         << 32));
                case 0x0b:
                    return static_cast<tjs_int64>(
                        ReadUnaligned_guess<std::uint32_t>(node + 1) |
                        (static_cast<std::uint64_t>(
                             ReadUnaligned_guess<std::uint16_t>(node + 5))
                         << 32) |
                        (static_cast<std::uint64_t>(node[7]) << 48));
                case 0x0c:
                    return ReadUnaligned_guess<tjs_int64>(node + 1);
                default:
                    return 0;
            }
        }

        // PSBRawNode_GetDouble_guess @ 0x5992E8 is a complete Android inline
        // copy of this raw-node decoder. iOS arm64 preserves a shared
        // dispatcher calling the two integer decoders; no exact source
        // identifier survives.
        inline tjs_real DecodeNumberAsDouble_guess(
            const std::uint8_t *node) {
            switch(node[0]) {
                case 0x02:
                    return 1.0;
                case 0x03:
                    return 0.0;
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                case 0x08:
                    return static_cast<tjs_real>(DecodeInteger32_guess(node));
                case 0x09:
                case 0x0a:
                case 0x0b:
                case 0x0c:
                    return static_cast<tjs_real>(DecodeInteger64_guess(node));
                case 0x1d:
                    return 0.0;
                case 0x1e:
                    return static_cast<tjs_real>(
                        ReadUnaligned_guess<float>(node + 1));
                case 0x1f:
                    return ReadUnaligned_guess<double>(node + 1);
                default:
                    TVPThrowExceptionMessage(
                        TJS_W("psb: can't convert value to double."));
            }
            return 0.0;
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

        // Android arm64 @0x59641C/@0x59659C/@0x597B1C proves the count, width,
        // data base and next-table displacement scalars but SROA hides their
        // object. iOS arm64 independently preserves a four-field constructor
        // and fixes the semantic order to nBytes/count/width/data. Only the
        // type/field/operator/member-free-inline tokens remain guessed.
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
