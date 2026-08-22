// Portable value types recovered from the four current motionplayer reference
// binaries. Native ttstr and standard-library container ABIs differ by target;
// exact sizes and offsets therefore live in analysis/. These definitions retain
// the semantic fields, reference ownership, non-owning links and destruction
// behavior that are observable across all four targets.
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../MotionNode.h"
#include "tjs.h"
#include "tjsInterface.h"
#include "tjsString.h"

namespace motion::detail {

    // EvalCascadeState — Player HM1 value type.
    //
    // Cached result of joined-label cascade lookup chains. The 64-bit layout
    // below is shared by the two 64-bit references; 32-bit layouts and exact
    // function mappings live in the four-binary binding analysis. Binary V
    // (value base after the hash-node header) layout:
    //   V+0  ttstr key copy (the joinedKey)
    //   V+8/+16/+24 std::vector<ttstr> chainSegments
    //              = the scope label split on '/'
    //   V+32 double writeVal  (updated by every bind)
    //   V+40 double weight    (seeded to 1.0 on first insert)
    //   V+48/+56/+64 std::vector<MotionNode*> heapResult
    //              = type3/4 nodes whose ancestor-label-chain matches
    //                chainSegments.
    //
    // Destruction releases heapResult backing, chainSegments strings/backing,
    // then the key. heapResult elements are non-owning node pointers.
    struct EvalCascadeState {
        ttstr keyCopy;
        std::vector<ttstr> chainSegments;
        // Binary V+32 / V+40. Every bind stores its `value` parameter in
        // writeVal; weight is seeded to 1.0 only on first insert and remains
        // untouched on later upserts.
        double writeVal = 0.0;
        double weight = 0.0;
        std::vector<MotionNode *> heapResult;

        EvalCascadeState() = default;
        EvalCascadeState(const EvalCascadeState &) = delete;
        EvalCascadeState &operator=(const EvalCascadeState &) = delete;

        EvalCascadeState(EvalCascadeState &&other) noexcept = default;
        EvalCascadeState &operator=(EvalCascadeState &&other) noexcept = default;
        // ~EvalCascadeState: vector<ttstr> releases each ttstr (mirrors binary
        // chainSegments element Release); vector<MotionNode*> just frees backing
        // (non-owning, matches binary heapResult dtor — no per-node Release);
        // ~ttstr handles keyCopy. Declaration order = ascending binary offset, so
        // reverse-order member destruction matches the binary's V+48→V+8→V+0.
        ~EvalCascadeState() = default;
    };

    // Per-frame variable-track slot. The four references use the same internal
    // field offsets; only the trailing Variant size changes the native stride
    // (56 bytes on 64-bit targets, 48 bytes on 32-bit targets). `step` writes
    // frameIndex/time and clears merged. `merge` sets merged first, then reads
    // type and, for nonzero types, content.interval/content.value/frame.easing.
    // A type-zero merge intentionally leaves interpFlag, interval, value and
    // easing stale.
    struct VarTrackSlot {
        std::uint32_t frameIndex = 0;  // +0
        double time = 0.0;             // +8
        std::uint32_t interval = 0;    // +16
        bool typeZeroFlag = false;     // +20; explicitly seeded by initVariables
        std::uint8_t interpFlag = 0;   // +21; type 2 -> 0, type 3 -> 1
        bool merged = false;           // +22
        double value = 0.0;            // +24
        // +32. A non-void {x:[...], y:[...]} Variant enables curve easing.
        tTJSVariant easing;
    };

    // One deque element per motion `variable` item. Native size is 160 bytes on
    // both 64-bit targets and 128 bytes on both 32-bit targets. The shared field
    // order is cascade key, active-slot cursor, interpolated value, retained raw
    // frame source, then two slots. Because cascadeKey is first, the native
    // variableKeys getter can pass each element address directly to its String-
    // Variant append path. Reverse member destruction therefore releases
    // slot1.easing, slot0.easing, frameSource and cascadeKey in native order.
    // This is a variable lookup/interpolation table, not one of EmotePlayer's
    // controller animator state machines.
    struct VariableLabelScope {
        ttstr cascadeKey;
        int activeSlotCursor = 0;
        double value = 0.0;
        // A second read of entry["label"], retained independently and indexed
        // with PropGetByNum by all cursor paths.
        tTJSVariant frameSource;
        VarTrackSlot slot[2];
    };

    // PerNodeLayerState — HM3's per-node join-snapshot value type.
    //
    // Per-node-path layer state snapshot keyed by the slash-joined node path
    // built by Player_buildNodePathKey_guess. Recovered from the four-reference
    // init/restore/reset/reseek family documented in
    // analysis/motionplayer_join_snapshot_four_binary_2026-08-11.md.
    //
    // All four references expose the same source-level member nesting despite
    // their different STL and Variant ABIs: nodeType, a complete ClipSlot,
    // child-Player Variant, mesh vector, nine particle doubles, then the
    // particle-array Variant. The native value constructor zero-initializes the
    // whole object. That differs from an ordinary MotionNode ClipSlot, whose
    // portable defaults represent the later node-reset state, so this wrapper
    // explicitly returns every nonzero ClipSlot default to zero.
    //
    // Ordinary reverse member destruction exactly matches the four native
    // chains: particle Variant, mesh vector, child Variant, then the embedded
    // ClipSlot's anchor/camera/model/motion strings, mesh vector, Variants and
    // icon/source strings. The native map clear has an earlier invalidation
    // pass for unconsumed type-3/type-4 children; it remains an explicit method
    // rather than a destructor side effect.
    struct PerNodeLayerState {
        int nodeType = 0;
        MotionNode::ClipSlot clipSlot;
        tTJSVariant childPlayerSnapshot;
        std::vector<MeshPoint> meshControlPoints;
        std::array<double, 9> particleInterp{};
        tTJSVariant particleArraySnapshot;

        PerNodeLayerState() {
            clipSlot.frameIndex = 0;
            clipSlot.done = false;
            clipSlot.blendMode = 0;
            clipSlot.packedColors.fill(0);
            clipSlot.opacity = 0;
            clipSlot.scaleX = 0.0;
            clipSlot.scaleY = 0.0;
            clipSlot.prtFmin = 0.0;
            clipSlot.prtF = 0.0;
            clipSlot.prtZmin = 0.0;
            clipSlot.prtZ = 0.0;
        }
        PerNodeLayerState(const PerNodeLayerState &) = delete;
        PerNodeLayerState &operator=(const PerNodeLayerState &) = delete;
        PerNodeLayerState(PerNodeLayerState &&) noexcept = default;
        PerNodeLayerState &operator=(PerNodeLayerState &&) noexcept = default;
        void invalidateRetainedChildrenBeforeClear_guess();
        ~PerNodeLayerState() = default;
    };

} // namespace motion::detail
