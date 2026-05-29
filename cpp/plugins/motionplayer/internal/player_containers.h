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
// HM3 (PerNodeLayerState 688B value) is still deferred: its interior 8+ ttstr
// / 5+ iTJSDispatch2* slots are only partially reverse-engineered, and a
// premature implementation would leak Released refcounts. See
// value_structs.h's forward declaration and
// .claude/agent-memory/ida-deep-analyzer/player_value_structs_spec.md.
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

    // HM4 — libkrkr2.so Player+1240. ttstr → iTJSDispatch2* (non-owning).
    // Used by name → dispatch alias resolution.
    using DispatchAliasMap =
        std::unordered_map<ttstr, iTJSDispatch2 *, ttstr_hash, ttstr_equal>;

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
