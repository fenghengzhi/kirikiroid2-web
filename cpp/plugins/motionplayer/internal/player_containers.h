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
// HM3 (PerNodeLayerState 688B value) now lives in value_structs.h with all
// dtor-referenced ttstr / dispatch / heap slots declared in ascending binary
// offset; reverse declaration-order destruction handles the descending-offset
// release sequence libkrkr2.so's Player_HM3_value_destroy @0x6DD06C uses.
//
#pragma once

#include <deque>
#include <unordered_map>

#include "ttstr_hash.h"
#include "value_structs.h"

namespace motion::detail {

    // HM1 — libkrkr2.so Player+264. ttstr → EvalCascadeState (cascade
    // PropGet result cache). Owns refcounts on the embedded main dispatch
    // and every dispatch in chainDispatches; releases them in destructor
    // order matching Player_HM1_value_destroy @0x6DD1A0.
    using EvalCascadeMap =
        std::unordered_map<ttstr, EvalCascadeState, ttstr_hash, ttstr_equal>;

    // HM2 — libkrkr2.so Player+320. ttstr → double, raw double @entry+16
    // (NOT tTJSVariant). Used by setVariable / getVariable label→value path.
    using LabelValueMap =
        std::unordered_map<ttstr, double, ttstr_hash, ttstr_equal>;

    // HM3 — libkrkr2.so Player+1184. ttstr (node-path key built by
    // Player_buildNodePathKey @0x6B5C1C) → PerNodeLayerState. The value type
    // owns its 8 ttstr / 5 dispatch / 2 heap slots and releases them in the
    // libkrkr2.so dtor order via reverse declaration-order destruction.
    using PerNodeLayerStateMap =
        std::unordered_map<ttstr, PerNodeLayerState, ttstr_hash, ttstr_equal>;

    // HM4 — libkrkr2.so Player+1240. ttstr → double, raw double @entry+16.
    // (Same value-slot layout as HM2 — clearHM3_HM4 @0x6B80E4 only Releases the
    // ttstr key and op-deletes the 32B entry; value slot is never AddRef'd or
    // Release'd. Spike-grounded R-M4: prior `iTJSDispatch2 *` annotation was
    // an audit misread; binary writer @0x6B2D40 stores a raw 8B controller
    // snapshot, reader @0x6CD304 loads it directly to a NEON D-reg as double.)
    //
    // Semantics: variable-snapshot cache populated by
    // Player_resetMotionState_clearAndRebuild's second loop (each controller
    // writes its current value snapshot keyed by controller-name ttstr); the
    // first stop of Player::getVariable's cascade (HM4 → HM2 → HM1).
    using VariableSnapshotMap =
        std::unordered_map<ttstr, double, ttstr_hash, ttstr_equal>;

    // Player+1296 std::deque<VariableLabelScope>. Populated by
    // Player_initVariables @0x6CD750 once per motion load with (variable,
    // label, scope) triples used by HM1/HM2 cascade evaluation.
    //
    // Distinct from EmotePlayer's 5 controller animator deques at +256 /
    // +336 / +416 / +576 / +656 (per-nodeType animator state); this is a
    // lookup table, those are state machines.
    using VariableLabelScopeDeque = std::deque<VariableLabelScope>;

    // The Player+184 deque carries motion::MotionNode (2632B in the binary).
    // The alias is intentionally not declared here to keep this header free
    // of the MotionNode dependency surface; consumers should spell it
    // directly as std::deque<motion::MotionNode>.

} // namespace motion::detail
