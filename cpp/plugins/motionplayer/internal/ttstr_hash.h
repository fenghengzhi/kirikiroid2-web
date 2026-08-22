// motion::Player container hash specialisation.
//
// The four current references separate hashing into two layers. The core
// tTJSHashFunc<ttstr>::Make operation is a pure UTF-16 payload hash and never
// reads or writes tTJSVariantString::Hint. The motionplayer unordered-container
// wrappers first inspect that shared 32-bit Hint, accept any nonzero value
// verbatim, and otherwise call the pure payload hash and cache its result before
// bucket lookup. Using std::hash<std::string> (or omitting the wrapper cache)
// would change native bucket selection and alias-visible Hint state. Final
// unordered iteration order additionally remains specific to each STL ABI.
//
#pragma once

#include <cstddef>
#include <cstdint>

#include "tjs.h"
#include "tjsString.h"

namespace motion::detail {

    // Pure counterpart of the references' core tTJSHashFunc<ttstr>::Make
    // algorithm. It operates only on UTF-16 code units: a null C-string pointer
    // and an empty payload both produce the nonzero UINT32_MAX sentinel. The
    // ttstr overload below intercepts a null backing pointer and returns zero
    // before this layer, matching all four container wrappers.
    inline std::size_t ttstr_hash_utf16(const tjs_char *p) noexcept {
        std::uint32_t acc = 0;
        if (p) {
            std::uint32_t c = static_cast<std::uint32_t>(*p);
            if (c) {
                ++p;
                do {
                    const std::uint32_t mixed = acc + c;
                    c = static_cast<std::uint32_t>(*p++);
                    acc = (1025u * mixed) ^ ((1025u * mixed) >> 6);
                } while (c);
                acc = 9u * acc;
            }
        }
        std::uint32_t h = 32769u * (acc ^ (acc >> 11));
        if (!h) {
            h = static_cast<std::uint32_t>(-1);
        }
        return h;
    }

    struct ttstr_hash {
        using is_transparent = void;
        std::size_t operator()(const ttstr &s) const noexcept {
            // GetHint addresses the shared backing object, so copied ttstr
            // aliases share this cache word. A nonzero Hint is trusted exactly;
            // zero alone means "compute and publish before lookup". The 32-bit
            // result is naturally zero-extended when size_t is 64-bit.
            tjs_uint32 *hint = const_cast<ttstr &>(s).GetHint();
            if(!hint) {
                return 0;
            }
            if(*hint) {
                return *hint;
            }
            const auto hash = static_cast<tjs_uint32>(
                ttstr_hash_utf16(s.c_str()));
            *hint = hash;
            return hash;
        }
        std::size_t operator()(const tjs_char *s) const noexcept {
            return ttstr_hash_utf16(s);
        }
    };

    struct ttstr_equal {
        using is_transparent = void;
        bool operator()(const ttstr &a, const ttstr &b) const noexcept {
            // All four reference collision paths use ttstr's backing-aware
            // equality: identical backing pointers (including two nulls) are
            // equal; exactly one null is unequal; two nonnull backings compare
            // Length first and then UTF-16 payload. Consequently a default
            // null ttstr differs from an allocated empty string, while two
            // independently allocated empty strings compare equal.
            return a == b;
        }
    };

    // UTF-16 code-unit lexicographic comparator used by the four-reference
    // std::map<ttstr,int> node-index map. A null-backed ttstr sorts before every
    // non-null-backed ttstr, even a deliberately allocated zero-length buffer.
    // Two non-null values are compared one code unit at a time: loop while the
    // chars are equal and the right operand is non-zero, then use the sign of
    // a[i] - b[i]. std::string byte order (UTF-8) would reorder non-ASCII labels.
    struct ttstr_utf16_less {
        bool operator()(const ttstr &a, const ttstr &b) const noexcept {
            if(a.IsEmpty()) {
                return !b.IsEmpty();
            }
            if(b.IsEmpty()) {
                return false;
            }
            const tjs_char *pa = a.c_str();
            const tjs_char *pb = b.c_str();
            // v = a[0] - b[0]; while (b[i] != 0 && v == 0) advance.
            std::uint16_t ca = static_cast<std::uint16_t>(*pa);
            std::uint16_t cb = static_cast<std::uint16_t>(*pb);
            int diff = static_cast<int>(ca) - static_cast<int>(cb);
            while (cb != 0 && diff == 0) {
                ca = static_cast<std::uint16_t>(*++pa);
                cb = static_cast<std::uint16_t>(*++pb);
                diff = static_cast<int>(ca) - static_cast<int>(cb);
            }
            return diff < 0;
        }
    };

} // namespace motion::detail
