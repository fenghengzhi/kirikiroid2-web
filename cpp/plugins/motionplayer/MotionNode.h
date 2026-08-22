//
// Persistent per-node state for the MotionPlayer rendering pipeline.
//
// The portable declaration groups recovered source-level members. Exact
// per-target offsets and deque strides are documented under analysis/.
//
// The current four-reference comparison establishes the common logical field
// mapping below. ABI-specific offsets and deque strides belong in analysis/:
//   label -> layerName; type -> nodeType; coordinate -> coordinateMode;
//   inheritMask -> inheritFlags; frameList -> frameListVariant;
//   meshTransform -> meshType; stencilType -> stencilType.
// Ordinary value construction zeroes parentIndex, inheritFlags, and all four
// transformOrder entries, while both clip-slot done bytes start true.
//
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "HitTestInternal.h"
#include "MeshPoint.h"
#include "tjs.h"  // tTJSVariant and iTJSDispatch2 bridge ownership

class iTVPTexture2D;

namespace motion {
    class Player;
}

namespace motion::detail {

    struct MotionParameterEntry;
    struct PerNodeLayerState;
    struct PreparedRenderItem;

    // Native temporary object wrappers retain a Variant's dispatch once and
    // keep the same receiver across a multi-call operation. Type-4 uses one
    // for its child Array and a second one for the motion-source list. Exact
    // target layouts live in analysis/.
    class ScopedVariantObjectDispatch_guess final {
    public:
        explicit ScopedVariantObjectDispatch_guess(
            const tTJSVariant &arrayVariant);
        ~ScopedVariantObjectDispatch_guess();

        ScopedVariantObjectDispatch_guess(
            const ScopedVariantObjectDispatch_guess &) = delete;
        ScopedVariantObjectDispatch_guess &operator=(
            const ScopedVariantObjectDispatch_guess &) = delete;

        [[nodiscard]] iTJSDispatch2 *get() const { return dispatch_; }

    private:
        iTJSDispatch2 *dispatch_;
    };

    // Compatibility name retained for already-recovered child-Array callers.
    using ScopedParticleArrayDispatch_guess =
        ScopedVariantObjectDispatch_guess;

    tjs_int particleArrayCount_guess(iTJSDispatch2 *array);
    motion::Player *particleArrayGetNativePlayerAt_guess(
        iTJSDispatch2 *array, tjs_int index);
    void particleArrayAdd_guess(iTJSDispatch2 *array,
                                const tTJSVariant &playerVariant);
    void particleArrayErase_guess(iTJSDispatch2 *array, tjs_int index,
                                  tTJSVariant *result = nullptr);

    struct MotionNode {
        MotionNode() = default;
        ~MotionNode();
        // The four native deque range-erase instantiations expose the
        // compiler-generated memberwise copy assignment. In particular, the
        // raw preparedRenderItem owner is shallow-copied like every other
        // scalar member. The normal non-root suffix erase does not execute the
        // relocation branch, but deleting these operations changes which STL
        // paths
        // are well-formed and therefore does not match the native type.
        MotionNode(const MotionNode &) = default;
        MotionNode &operator=(const MotionNode &) = default;

        // Four-reference out-of-line MotionNode operations used by the Join
        // snapshot producer/consumer. Both sides transfer Variant ownership,
        // so neither the node nor the snapshot argument is const.
        void initJoinSnapshot_guess(PerNodeLayerState &snapshot);
        void restoreJoinSnapshot_guess(PerNodeLayerState &snapshot);

        // Identity (from PSB, set once during tree build)
        int index = 0;
        // The native value-constructor zeroes this field. Recursive tree build
        // overwrites it before reading the raw layer; the synthetic root keeps
        // zero because index zero terminates every parent walk.
        int parentIndex = 0;
        // Two independent ResourceManager.requireLayerId results acquired
        // before raw node-field initialization.
        int layerId1 = 0;
        int layerId2 = 0;
        int nodeType = 0;              // raw layer "type"
        int coordinateMode = 0;
        int inheritFlags = 0;
        uint8_t flags = 0;
        // Read once from the raw layer during node initialization. joinTarget
        // gates later per-node state snapshot/restore participation; it is not
        // a visibility or active byte.
        bool joinTarget = false;
        bool groundCorrection = false;
        // Ordinary node construction leaves all four entries zero. The Player
        // constructor alone overwrites the synthetic root with the class-level
        // default order; raw layer initialization overwrites every real node.
        int transformOrder[4] = {0, 0, 0, 0};
        // Recursive construction reads "label" once for the raw-label map, then
        // node initialization independently reads it again into this owner.
        ttstr layerName;
        int meshType = 0;              // raw "meshTransform" property
        int meshFlags = 0;             // raw "meshSyncChildMask" property
        int meshDivision = 0;          // raw "meshDivision" property
        // Persistent raw "meshCombine" property. This is distinct from the
        // per-frame inheritance separator derived below.
        bool meshCombine = false;
        int meshDivX = 0;              // current horizontal grid cell count
        int meshDivY = 0;              // current vertical grid cell count
        int objTriPriority = 0;        // raw "objTriPriority" for type 0
        // The four Player_initNodeFields_guess implementations store either a
        // pointer into the Player parameter vector (integer `parameterize`) or
        // null (every other variant type). Entry size is ABI-specific.
        int parameterizeIndex = -1;
        MotionParameterEntry *parameterEntry = nullptr;
        // Mesh inverse matrix and float inverse offsets used by child
        // deformation. Their target offsets differ across all four ABIs.
        double meshInvM11 = 0, meshInvM12 = 0;
        double meshInvM21 = 0, meshInvM22 = 0;
        float meshInvOffX = 0, meshInvOffY = 0;
        // Computed by the vertex/mesh pass after layer evaluation.
        bool hasMeshData = false;
        // Set when post-build stencil-mask resolution references this node.
        bool stencilCompositeMaskReferenced = false;
        // True when an inherited mesh chain exists and inheritMask bit 25 does
        // not request a separator. It is recomputed by the vertex pass and is
        // not the raw PSB "meshCombine" property.
        bool meshInheritanceSeparator_guess = false;
        // Dirty propagation local to the vertex/mesh pass. A node is processed
        // when its accumulated transform is dirty or its mesh ancestor carried
        // this state earlier in the same parent-first traversal.
        bool meshVertexPassDirty_guess = false;
        // The raw layer's optional stencilType property seeds this field;
        // absence writes zero. For a type-3 node, all four initializers then
        // read Player.preview live and clear bit 4 when preview is true.
        int stencilType = 0;
        // Three independent std::vector<MeshPoint> owners, destroyed in
        // reverse declaration order. Layer evaluation keeps the raw 4x4
        // patch, builds a composite grid, and writes the own-affine-transformed
        // 4x4 patch; none is a "previous frame" alias of another. Exact target
        // offsets live in analysis/.
        std::vector<MeshPoint> meshControlPoints;
        std::vector<MeshPoint> compositeMeshPoints;
        std::vector<MeshPoint> transformedMeshControlPoints;

        // Raw TJS owners copied independently from the layer dispatch. Their
        // CopyRef lifetimes must not be replaced by one decoded-tree owner.
        tTJSVariant frameListVariant;
        tTJSVariant emoteEditVariant;
        tTJSVariant particleMotionListVariant;
        tTJSVariant stencilCompositeMaskLayerListVariant;

        // One-byte Boolean derived later from the retained emoteEdit owner;
        // node initialization itself does not read the nested priorDraw member.
        bool priorDraw = false;

        // ========== Dual Clip Slot Architecture ==========
        // All four current references keep two parsed slots and one active-slot
        // selector. Exact offsets/strides differ by ABI and STL; see the
        // four-binary node-slot analysis. Incremental stepping overwrites the
        // inactive slot and flips the selector, preserving the old slot for
        // crossfade interpolation.
        //
        struct ClipSlot {
            // Header written by the frame parser. The merger sets `merged`
            // before its done-frame early return.
            int frameIndex = -1;
            double clipStartTime = 0.0;
            std::uint32_t ti = 0;
            std::uint32_t contentMask = 0;
            bool done = true;
            bool crossfading = false;
            bool merged = false;

            // The merger stores distinct icon/src owners. Consumers read these
            // directly; narrow strings exist only at Web/PSB API boundaries.
            ttstr iconValue;
            ttstr srcValue;

            // Base presentation and transform payload, in native declaration
            // order rather than evaluator-consumption order.
            int blendMode = 16;
            double ox = 0, oy = 0;
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            int opacity = 255;
            double x = 0, y = 0, z = 0;
            bool flipX = false, flipY = false;
            double angle = 0.0;
            double scaleX = 1.0, scaleY = 1.0;
            double slantX = 0.0, slantY = 0.0;

            // Raw curve/action owners. Their relative declaration order is
            // observable through reset and reverse destruction.
            tTJSVariant cccVariant;
            tTJSVariant occVariant;
            tTJSVariant accVariant;
            tTJSVariant zccVariant;
            tTJSVariant sccVariant;
            tTJSVariant cpVariant;
            ttstr actionValue;
            tTJSVariant meshCurveVariant;
            std::vector<MeshPoint> meshControlPoints;

            // motion sub-object (content mask 0x80000). Reset clears only the
            // first two words; merger writes the remaining defaults/fields.
            int motionFlags = 0;
            int motionDt = 0;
            bool motionDocmpl = false;
            double motionDofst = 0.0;
            ttstr motionDtgtValue;
            double motionTimeOffset = 0.0;

            // model sub-object (content mask 0x01000000).
            // The type-6 emitter uses model.{dt,dtgt,timeOffset} for mode,
            // mode-4 target label and timer offset. Its separately retained
            // source identity still comes from ClipSlot::srcValue.
            bool modelLoop = false;
            int modelDt = 0;
            ttstr modelDtgt;
            double modelTimeOffset = 0.0;

            // prt sub-object (content mask 0x100000). The merger initializes
            // this whole block before applying the nested prt.mask overrides.
            // The evaluator copies/interpolates these same nine doubles into
            // the node-level particle mirror; there is no second slot region.
            int prtTrigger = 0;
            double prtFmin = 10.0, prtF = 10.0;
            double prtVmin = 0.0, prtV = 0.0;
            double prtAmin = 0.0, prtA = 0.0;
            double prtZmin = 1.0, prtZ = 1.0;
            double prtRange = 0.0;

            // camera, anchor and feedback tail blocks. Their string owners are
            // replaced in place when the corresponding outer mask is present.
            double cameraFov = 0.0;
            ttstr cameraTarget;
            ttstr anchorTarget;
            double feedbackTimespan = 0.0;
        };

        ClipSlot slots[2];
        int activeSlotIndex = 0;
        ClipSlot& activeSlot() { return slots[activeSlotIndex]; }
        const ClipSlot& activeSlot() const { return slots[activeSlotIndex]; }
        ClipSlot& otherSlot() { return slots[activeSlotIndex ^ 1]; }
        const ClipSlot& otherSlot() const { return slots[activeSlotIndex ^ 1]; }
        // Timeline evaluation stores exactly one previous blend ratio. A
        // parameterized node reads parameterEntry->value directly; there is no
        // secondary override/cache-validity state.
        double timelineEvalRatio = 0.0;

        // TJS setter / camera velocity override block. Root position accessors
        // in all four references read/write this same logical delta state; the
        // exact per-target offsets live in analysis/.
        struct DeltaState {
            bool dirty = true;
            bool activeOverride = true;
            bool visibleOverride = true;
            bool flipX = false;
            bool flipY = false;
            double posX = 0.0;
            double posY = 0.0;
            double posZ = 0.0;
            double angle = 0.0;
            double scaleX = 1.0;
            double scaleY = 1.0;
            double slantX = 0.0;
            double slantY = 0.0;
            int opacity = 255;
        } delta;

        // Working/evaluated state built by the timeline evaluator and then
        // composed through the updateLayers inheritance pass in all four
        // current references.
        struct AccumulatedState {
            bool visible = true;
            bool active = true;
            bool dirty = true;
            bool flipX = false;
            bool flipY = false;
            double posX = 0.0;
            double posY = 0.0;
            double posZ = 0.0;
            double angle = 0.0;
            double scaleX = 1.0;
            double scaleY = 1.0;
            double slantX = 0.0;
            double slantY = 0.0;
            // This is the final member of the native 0x50-byte evaluated
            // transform block. Particle creation writes the parent particle
            // node's evaluated opacity into the child root's matching delta
            // member; no accumulated blend-mode member follows it.
            int opacity = 255;
            // 2x2 matrix (local × parent accumulated)
            double m11 = 1.0;
            double m12 = 0.0;
            double m21 = 0.0;
            double m22 = 1.0;
        } accumulated;

        // Previous position (for delta computation in post-loop)
        double prevPosX = 0.0;
        double prevPosY = 0.0;
        double prevPosZ = 0.0;

        // MotionNode owns and deletes this raw render item. The recursive
        // prepared-item builder reuses it across calls and allocates it only
        // when the pointer is null. The caller's main/aux
        // vectors contain borrowed pointers to this object; they never own it.
        PreparedRenderItem *preparedRenderItem = nullptr;

        // Path-B visibility flag. The visibility pass rewrites non-root nodes
        // and follows the previous nodes' values to build the visible-ancestor
        // chain; it deliberately leaves the constructor-zeroed root untouched.
        // Render-item construction consumes it. LayerGetter.layerVisible is a
        // separate conjunction of accumulated.visible and accumulated.active.
        bool drawFlag = false;

        // Per-frame render-admission publication byte. Priority selection
        // clears only the selected nodes; ordinary/type-3/particle admission
        // sets it at their native commit points. Main-list filtering and the
        // type-12 stencil-composite pass consume it later in the same build.
        bool drawnThisFrame = false;

        int forceVisible = 0;
        // Portable replacement for the native nullable MotionNode pointer.
        // Only -1 represents null. Consumers deliberately do not range-check
        // or exclude self before resolving every other value.
        int visibleAncestorIndex = -1;

        // Native std::vector<MotionNode *> populated by the type-12 tree-build
        // post-pass.
        std::vector<MotionNode *> stencilCompositeMaskNodes;

        // Child Player owner for nodeType=3 (Motion). The Variant receives the
        // NCB adaptor returned after the native child has been fully linked and
        // initialized. A null adaptor leaves this Variant void while leaking the
        // allocated native child, matching all four references.
        tTJSVariant childPlayerVar;

        // Particle children for nodeType=4 (Particle), held by a TJS Array.
        // Old-tree teardown deliberately fetches element zero once per reported
        // count, so later elements are not invalidated by that visitor.
        tTJSVariant particleArrayVar;

        // Shape type for nodeType=1, read from the PSB "shape" key.
        int shapeType = 0;             // 0=point, 1=circle, 2=rect, 3=quad

        // Shape AABB for nodeType=7. Native construction deliberately leaves
        // the four floats unwritten; an eligible shape-AABB pass publishes
        // them before clipAABB points here.
        float shapeAABB[4];

        // Shape geometry for nodeType=1.  This is the same complete record
        // copied into Motion.Point/Circle/Rect/Quad by LayerGetter.shape.
        // Construction leaves the whole record unwritten. Each eligible pass
        // writes type plus only the slots owned by that shape kind.
        HitData shapeGeometry;

        // Position output of the vertex-computation phase. CameraNode focus and
        // stereovision consume this block rather than `accumulated.pos*`.
        double vertexPosX = 0.0;
        double vertexPosY = 0.0;
        double vertexPosZ = 0.0;

        // Vertex/mesh pass output.
        // Four source/mesh output corners in TL, TR, BR, BL order, with an X/Y
        // float pair per corner.
        float vertices[8] = {};

        // Per-node float AABB written by Player::calcBounds.
        float bounds[4] = { 1.0f, 1.0f, -1.0f, -1.0f };

        // Player::findSourceForNode_guess writes this persistent node-level
        // descriptor. The texture pointer is non-owning: the loaded module's
        // group-atlas cache owns it, so MotionNode destruction never
        // Release/AddRef it.
        struct SourceState {
            // Native construction establishes only the lifetime-bearing
            // members and the validity gate. The remaining scalar payload is
            // deliberately dormant/indeterminate. Native writer paths publish
            // the subset used by their admitted node kind, and downstream
            // source consumers reach it only through the corresponding
            // valid/node-kind gates.
            SourceState() : valid(false), texture(nullptr) {}

            bool valid;
            bool blank;
            tTJSVariant object;
            iTVPTexture2D *texture; // non-owning
            double width;
            double height;
            double originX;
            double originY;
            double clipLeft;
            double clipTop;
            double clipRight;
            double clipBottom;
            std::array<int, 4> textureRect;
            // Retained native ttstr owner. The KRKR atlas loader copies this
            // owner for its entry split, but keeps reading this live field for
            // the cache probe/retry key across atlas-building callbacks.
            ttstr path;

            // Deterministic local reset helper used by the reconstruction
            // harness. Native construction and loader failures do not perform
            // this whole-record reset.
            void clear() {
                valid = false;
                blank = false;
                object.Clear();
                texture = nullptr;
                width = height = originX = originY = 0.0;
                clipLeft = clipTop = 0.0;
                clipRight = clipBottom = 1.0;
                textureRect = {0, 0, 0, 0};
                path.Clear();
            }
        } source;

        // Per-frame type-specific evaluator output for type-5 CameraNode.
        // Construction deliberately leaves it uninitialized; evaluation writes
        // it before an active camera node is consumed.
        double cameraFov;

        // One physical integer stores the raw "anchor" property used by the
        // type-9 camera-constraint pass. The exact source member name is not
        // present in the binaries.
        int anchorType_guess = 0;
        double feedbackTimespan;
        double anchorOpaScale = 1.0;
        // Four packed-color sets, each with four persistent damping residuals.
        double anchorColorScale[16] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};

        // Nine-double particle evaluator-output mirror. The type-4 timeline
        // branches either copy the active slot's prt block or interpolate it
        // against the other slot. The type-4 particle-system pass consumes
        // this mirror; the separate type-6 emitter pass does not. Indices are
        // fmin, fmax, vmin, vmax, amin, amax, zmin, zmax and range.
        double particleInterp[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

        // (particleChildren replaced by particleArrayVar above — TJS Array)
        int particleType = 0;
        int particleMaxNum = 0;
        // One field serves both as the exponential velocity decay ratio and as
        // the camera-damping value copied into each new child Player. Its PSB
        // key is "particleAccelRatio".
        double particleAccelRatio = 0;
        bool particleInheritAngle = false;
        int particleInheritVelocity = 0;
        int particleFlyDirection = 0;
        int particleApplyZoomToVelocity = 0;
        bool particleDeleteOutside = false;
        bool particleTriVolume = false;

        // Existing-child inheritance snapshots. A changed transform commits
        // all five values before the pass tests a positive child count or
        // unwraps an Array element, so empty arrays and thrown getters still
        // advance the snapshot prefix.
        double prevM11 = 1.0, prevM21 = 0.0;
        double prevM12 = 0.0, prevM22 = 1.0;
        double prevParticleAngle = 0.0;

        // Frequency-mode timer and its persistent activation latch. Only
        // accumulated inactivity clears the latch; completed slots and the
        // zero-minimum frequency shortcut leave its previous value intact.
        double emitterTimerAccum = 0.0;
        bool particleEmitterFlagActive = false;

        // Persistent state of the four-reference type-6 emitter pass.
        bool emitterActive = false;
        double emitterTimer = 0.0;
        ttstr emitterDtgt;             // CopyRef of active slot srcValue
        bool emitterOffsetActive = false;
        double emitterOffsetX = 0.0;
        double emitterOffsetY = 0.0;
        double emitterOffsetZ = 0.0;

        // Per-frame accumulated-position delta consumed by child inheritance.
        double deltaPosX = 0.0;
        double deltaPosY = 0.0;
        double deltaPosZ = 0.0;

        // Direct pointer to the nearest active type-7 ancestor's four-float
        // AABB. An active type-7 node publishes its own array; every other node
        // forwards its parent's pointer. The pointer may cross child Players.
        const float *clipAABB = nullptr;

        // Independent mesh-transform ancestor chain. Keep it separate from
        // the visibility/clip ancestor exactly as child-motion propagation
        // does in all four references.
        MotionNode *meshAncestor = nullptr;

        // Four opaque packed-color byte groups. Construction clears all bytes;
        // timeline evaluation later copies the active slot's packed values.
        uint8_t colorBytes[16] = {};

        // === TJS↔Native bridge helpers ===
        // These are implemented in MotionNodeBridge.cpp to avoid circular
        // dependency (MotionNode.h cannot include Player.h or ncbind.hpp).

        // nodeType=3: resolve the native Player from childPlayerVar. Invalid
        // non-object values throw; null/wrong-native objects return nullptr.
        Player* getChildPlayer() const;

        // nodeType=4: Get particle count from the retained TJS Array.
        int getParticleCount() const;

        // nodeType=4: Get native Player* for particle child at index.
        // Conversion and native-instance errors deliberately propagate.
        Player* getParticleChild(int index) const;

    };

} // namespace motion::detail
