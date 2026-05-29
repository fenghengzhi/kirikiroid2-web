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

    // Compute the KiriKiri UTF-16 string hash byte-for-byte matching the
    // libkrkr2.so algorithm. Empty / null strings collapse to (uint32_t)-1
    // (the binary's "non-zero sentinel" so a zero hash never escapes).
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
            return ttstr_hash_utf16(s.c_str());
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

} // namespace motion::detail
