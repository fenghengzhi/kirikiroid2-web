//
// Build persistent node tree from PSB layer hierarchy.
// Aligned to libkrkr2.so sub_6B4A6C (0x6B4A6C): recursive tree walk
// that creates flat vector of MotionNodes with parentIndex.
//
#pragma once

#include <string>
#include <vector>

namespace motion::detail {

    struct MotionSnapshot;
    struct MotionNode;

    // Walk the PSB layer tree for the given clip (or root layers if clipLabel
    // is empty/not found) and produce a flat vector of MotionNodes.
    // Root node is at index 0. Each node's parentIndex points to its parent.
    std::vector<MotionNode> buildNodeTree(
        const MotionSnapshot &snapshot,
        const std::string &clipLabel);

} // namespace motion::detail
