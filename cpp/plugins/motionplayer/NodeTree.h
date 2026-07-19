//
// Build persistent node tree from PSB layer hierarchy.
// Aligned to libkrkr2.so sub_6B4A6C (0x6B4A6C): recursive tree walk
// that appends into the Player-owned node deque with parentIndex.
//
#pragma once

#include <string>
#include "tjs.h"

namespace motion {
    class Player;
    class ResourceManager;
}

namespace motion::detail {

    struct MotionNode;
    struct PlayerRuntime;

    // Walk motionContent.layer through raw TJS Array dispatches and append
    // nodes after the persistent root node.
    // Index 0 is the constructor-created root; each real PSB layer points to
    // its parent node index, with top-level layers using parentIndex=0.
    // P3-B (d): no longer takes a native ResourceManager* — layer-id allocation
    //   now routes through the Player+992 RM dispatch FuncCall (see Player.h
    //   dispatchRequireLayerId), matching binary buildNodeTree_recursive@0x6B4A6C.
    void buildNodeTree(
        motion::Player &player,
        const tTJSVariant &motionContent,
        int parentPreview = 0);

} // namespace motion::detail
