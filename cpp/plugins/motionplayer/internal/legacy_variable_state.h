// Legacy VariableAnimatorState — transitional element type used by
// EmoteEngine's 5 backward-compat deques + 1 unordered_map.
//
// PLATFORM_BOUNDARY: NOT a libkrkr2.so type. The binary uses 5 distinct POD
// element types (16/16/24/24/48 bytes) for its 5 step-iterated deques, plus a
// double-valued HM2. Until those typed step functions (sub_663BDC,
// sub_665600, sub_666068, sub_666BF8, sub_668470) are ported in P2, the local
// build keeps this uniform "fat" record alongside the binary-typed deques
// in EmoteEngine.h.
//
// Extracted from Player.h::VariableAnimatorState so EmoteEngine.h can declare
// legacy storage without pulling Player.h (avoiding circular include).
#pragma once

#include <deque>
#include <string>

namespace motion::detail {

    struct LegacyVariableKeyframe {
        float value = 0.0f;
        float duration = 0.0f;
        float weight = 1.0f;
    };

    struct LegacyVariableAnimatorState {
        std::string label;
        std::deque<LegacyVariableKeyframe> queue;
        bool active = false;
        float currentValue = 0.0f;
        float startValue = 0.0f;
        float targetValue = 0.0f;
        float progress = 1.0f;
        float duration = 0.0f;
        float weight = 1.0f;
    };

} // namespace motion::detail
