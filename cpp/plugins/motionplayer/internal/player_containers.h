// Container aliases for motion::Player's libkrkr2.so-aligned hash maps.
//
// libkrkr2.so embeds four std::unordered_map<ttstr, V> at fixed offsets inside
// motion::Player (HM1@+264, HM2@+320, HM3@+1184, HM4@+1240). All four share
// the same key type (ttstr) and the same custom ttstr_hash; only the value
// type and inline node size differ.
//
// PLATFORM_BOUNDARY: sizeof(std::unordered_map) on the Web build (libc++) is
// ~32B while the Android build (libstdc++) is 56B. sizeof(Player) on Web
// therefore cannot equal libkrkr2.so's 1384B. The logical 1:1 contract we
// uphold is: same K/V semantics, same custom hash, same iteration / bucket
// distribution algorithm, same lifetime. Byte-level offset equality is not
// reachable inside Emscripten — see CLAUDE.md "明确标注且不可避免的平台边界".
//
// HM1 (EvalCascadeState 72B value) and HM3 (PerNodeLayerState 688B value) are
// deferred: their interior layout still has un-reversed regions, and adding
// them as aliases now would commit to potentially wrong field types. They land
// once those structs are fully verified.
//
#pragma once

#include <unordered_map>

#include "ttstr_hash.h"

namespace motion::detail {

    // HM2 — libkrkr2.so Player+320. ttstr → double, raw double @entry+16
    // (NOT tTJSVariant). Used by setVariable / getVariable label→value path.
    using LabelValueMap =
        std::unordered_map<ttstr, double, ttstr_hash, ttstr_equal>;

    // HM4 — libkrkr2.so Player+1240. ttstr → iTJSDispatch2* (non-owning).
    // Used by name → dispatch alias resolution.
    using DispatchAliasMap =
        std::unordered_map<ttstr, iTJSDispatch2 *, ttstr_hash, ttstr_equal>;

} // namespace motion::detail
