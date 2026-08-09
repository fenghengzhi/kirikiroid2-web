// motion::Player container hash specialisation.
//
// libkrkr2.so embeds four std::unordered_map<ttstr, V> instances at fixed
// offsets inside motion::Player. All four share the same KiriKiri-specific
// ttstr hash, reverse-engineered from the binary at sub_6F4D40-ish region
// (called from every Player_HM*_find_node / Player_HM*_upsert_*). Using
// std::hash<std::string> (or the default for any other key) would change the
// bucket distribution and therefore the iteration order; that breaks any
// script that walks these maps in insertion-relative order.
//
// Reference: .claude/agent-memory/ida-deep-analyzer/player_containers_libstdcxx_spec.md
//
#pragma once

#include <cstddef>
#include <cstdint>

#include "tjs.h"
#include "tjsString.h"

namespace motion::detail {

    // Compute the KiriKiri UTF-16 payload hash. A null C-string pointer and an
    // empty payload both reach the non-zero sentinel here; the ttstr functor
    // below handles a null ttstr backing pointer before calling this helper,
    // as the Android unordered_map instances do.
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
            // Player HM1/HM2/HM3/HM4 @0x6F52AC/0x686944/0x6F2674,
            // ResourceManager @0x6A96F8/0x6AAB3C and the Emote maps/sets all
            // distinguish a null ttstr from a non-null zero hash and share the
            // tTJSVariantString Hint cache.
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
            return a == b;
        }
    };

    // UTF-16 code-unit lexicographic comparator, byte-for-byte matching the
    // libkrkr2.so std::map<ttstr,int> (Player+24 node-index map) comparator
    // sub_9B1ED0 @0x9B1ED0. That function compares two `unsigned __int16 *`
    // (tjs_char*) one code unit at a time: it loops while the chars are equal
    // and the right operand is non-zero, then returns sign(a[i] - b[i]). The
    // std::map "less" predicate is therefore (compare < 0). std::string byte
    // order (UTF-8) would reorder any non-ASCII label; this reproduces the
    // binary's RB-tree key ordering exactly.
    struct ttstr_utf16_less {
        bool operator()(const ttstr &a, const ttstr &b) const noexcept {
            const tjs_char *pa = a.c_str();
            const tjs_char *pb = b.c_str();
            // sub_9B1ED0: v2 = a[0] - b[0]; while (b[i] != 0 && v2 == 0) advance.
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
