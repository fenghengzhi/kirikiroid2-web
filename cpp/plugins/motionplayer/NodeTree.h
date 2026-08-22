//
// Build the persistent node tree from the raw PSB layer hierarchy.
//
#pragma once

#include <string>
#include "tjs.h"

struct ncbPropAccessor;

namespace motion {
    class Player;
    class ResourceManager;
}

namespace motion::detail {

    struct MotionNode;

    // Walk motionContent.layer through raw TJS Array dispatches and append
    // nodes after the persistent root node.
    // Index 0 is the constructor-created root; each real PSB layer points to
    // its parent node index, with top-level layers using parentIndex=0.
    // `motionContent` is an independently owning ncbPropAccessor.
    // Player::buildNodeTree_guess constructs it before old-tree teardown,
    // matching the reference lifetime and re-entrancy boundary.
    // Per-node preview handling reads Player state at the type-3 branch after
    // earlier property callbacks; tree entry does not capture a preview value.
    void buildNodeTree(
        motion::Player &player,
        ncbPropAccessor &motionContent);

} // namespace motion::detail
