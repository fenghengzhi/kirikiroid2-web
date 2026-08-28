//
// Persistent per-node state for the MotionPlayer rendering pipeline.
//
// Data members below follow the common source declaration order recovered
// from four reference binaries. ABI-specific offsets, padding, deque block
// sizes and the evidence for every owner boundary live under analysis/.
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "HitTestInternal.h"
#include "MeshPoint.h"
#include "tjs.h"

class iTVPTexture2D;

namespace motion {
    class Player;
}

namespace motion::detail {

    struct MotionParameterEntry;
    struct PerNodeLayerState;
    struct PreparedRenderItem;

    // Native temporary object wrappers retain a Variant's dispatch once and
    // keep the same receiver across a multi-call operation.
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
        // Nested declarations do not consume object storage. Keeping all value
        // types before the data-member ledger makes the recovered declaration
        // order below directly auditable.
        struct MatrixState {
            double m11 = 1.0;
            double m12 = 0.0;
            double m21 = 0.0;
            double m22 = 1.0;
        };

        struct ClipSlot {
            int frameIndex = -1;
            double clipStartTime = 0.0;
            std::uint32_t ti = 0;
            std::uint32_t contentMask = 0;
            bool done = true;
            bool crossfading = false;
            bool merged = false;

            ttstr iconValue;
            ttstr srcValue;

            int blendMode = 16;
            double ox = 0.0, oy = 0.0;
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            int opacity = 255;
            double x = 0.0, y = 0.0, z = 0.0;
            bool flipX = false, flipY = false;
            double angle = 0.0;
            double scaleX = 1.0, scaleY = 1.0;
            double slantX = 0.0, slantY = 0.0;

            tTJSVariant cccVariant;
            tTJSVariant occVariant;
            tTJSVariant accVariant;
            tTJSVariant zccVariant;
            tTJSVariant sccVariant;
            tTJSVariant cpVariant;
            ttstr actionValue;
            tTJSVariant meshCurveVariant;
            std::vector<MeshPoint> meshControlPoints;

            int motionFlags = 0;
            int motionDt = 0;
            bool motionDocmpl = false;
            double motionDofst = 0.0;
            ttstr motionDtgtValue;
            double motionTimeOffset = 0.0;

            bool modelLoop = false;
            int modelDt = 0;
            ttstr modelDtgt;
            double modelTimeOffset = 0.0;

            int prtTrigger = 0;
            double prtFmin = 10.0, prtF = 10.0;
            double prtVmin = 0.0, prtV = 0.0;
            double prtAmin = 0.0, prtA = 0.0;
            double prtZmin = 1.0, prtZ = 1.0;
            double prtRange = 0.0;

            double cameraFov = 0.0;
            ttstr cameraTarget;
            ttstr anchorTarget;
            double feedbackTimespan = 0.0;
        };

        struct AccumulatedState {
            bool dirty = true;
            bool active = true;
            bool visible = true;
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
        };

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
        };

        struct SourceState {
            // Native construction establishes only the lifetime-bearing
            // members and the validity gate. Other scalar payload stays
            // indeterminate until a gated writer publishes it.
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
            ttstr path;

            // Reconstruction-only deterministic reset helper. Native default
            // construction and loader failure do not clear the whole record.
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
        };

        // These are real cross-ABI source members, not target padding. Their
        // object representations are copied by compiler-generated assignment,
        // but no constructor, destructor or motionplayer consumer reveals a
        // unique semantic type or original name. Byte records preserve exactly
        // the observable uninitialized/copy-only boundary without inventing a
        // float/double/struct interpretation.
        struct DormantRecord64_guess {
            std::byte objectRepresentation[64];
        };
        struct CopiedOnlyTrailingWord_guess {
            std::byte objectRepresentation[4];
        };
        static_assert(sizeof(DormantRecord64_guess) == 64);
        static_assert(sizeof(CopiedOnlyTrailingWord_guess) == 4);

        // Must remain user-provided: allocator value-construction in the four
        // references does not zero the complete record before member defaults.
        MotionNode() {}
        ~MotionNode();
        MotionNode(const MotionNode &) = default;
        MotionNode &operator=(const MotionNode &) = default;

        void initJoinSnapshot_guess(PerNodeLayerState &snapshot);
        void restoreJoinSnapshot_guess(PerNodeLayerState &snapshot);

        // -----------------------------------------------------------------
        // Recovered source declaration order. Do not regroup by subsystem:
        // construction, copy assignment and reverse destruction observe it.
        // -----------------------------------------------------------------

        // Front identity owner and scalar cluster.
        ttstr layerName;
        MotionParameterEntry *parameterEntry = nullptr;
        int layerId1 = 0;
        int layerId2 = 0;
        int coordinateMode = 0;
        int nodeType = 0;
        int shapeType = 0;
        int parentIndex = 0;
        int inheritFlags = 0;
        std::uint8_t flags = 0;
        bool joinTarget = false;
        bool groundCorrection = false;
        bool priorDraw = false;
        int stencilType = 0;
        double timelineEvalRatio = 0.0;

        // First retained script owner.
        tTJSVariant frameListVariant;

        // Front transform/color state precedes SourceState on all four ABIs.
        int transformOrder[4] = {0, 0, 0, 0};
        std::uint8_t colorBytes[16] = {};
        MatrixState matrix;
        double vertexPosX = 0.0;
        double vertexPosY = 0.0;
        double vertexPosZ = 0.0;
        double prevPosX = 0.0;
        double prevPosY = 0.0;
        double prevPosZ = 0.0;

        SourceState source;

        // The parser keeps the old and new slot alive across crossfade.
        ClipSlot slots[2];
        int activeSlotIndex = 0;

        // A distinct std::string owner is present after the active selector.
        // No motionplayer path reads or writes its contents.
        std::string dormantString_guess;

        // Trivial pre-evaluation state between the dormant string and the two
        // independent LayerGetter publication bytes.
        double cameraFov;
        double deltaPosX = 0.0;
        double deltaPosY = 0.0;
        double deltaPosZ = 0.0;
        bool layerGetterVisible_guess;
        bool layerGetterBranchVisible_guess;

        AccumulatedState accumulated;
        DeltaState delta;

        // Type-1 shape result is a partially-written persistent record.
        HitData shapeGeometry;
        DormantRecord64_guess dormantRecord64_guess;
        float vertices[8] = {};
        float bounds[4] = {1.0f, 1.0f, -1.0f, -1.0f};

        // Raw owner deleted by MotionNode::~MotionNode before automatic reverse
        // member destruction. Compiler-generated copy assignment is shallow.
        PreparedRenderItem *preparedRenderItem = nullptr;

        // Child motion adaptor owner.
        tTJSVariant childPlayerVar;

        // Render admission and borrowed link cluster.
        bool drawFlag = false;
        bool drawnThisFrame = false;
        bool hasMeshData = false;
        bool stencilCompositeMaskReferenced = false;
        MotionNode *visibleAncestor = nullptr;

        // The live Variant type tag is the sole emote-edit/forced-visible gate.
        tTJSVariant emoteEditVariant;

        // Mesh configuration precedes the three vector owners.
        int meshType = 0;
        int meshFlags = 0;
        int meshDivision = 0;
        bool meshCombine = false;
        int meshDivX = 0;
        int meshDivY = 0;
        int objTriPriority = 0;
        bool meshInheritanceSeparator_guess = false;
        bool meshVertexPassDirty_guess = false;

        std::vector<MeshPoint> meshControlPoints;
        std::vector<MeshPoint> compositeMeshPoints;
        std::vector<MeshPoint> transformedMeshControlPoints;

        // Mesh inverse, clip and particle configuration cluster.
        double meshInvM11 = 0.0, meshInvM12 = 0.0;
        double meshInvM21 = 0.0, meshInvM22 = 0.0;
        float meshInvOffX = 0.0f, meshInvOffY = 0.0f;
        float shapeAABB[4];
        const float *clipAABB = nullptr;
        MotionNode *meshAncestor = nullptr;
        int particleType = 0;
        int particleMaxNum = 0;
        double particleAccelRatio = 0.0;
        bool particleInheritAngle = false;
        int particleInheritVelocity = 0;
        int particleFlyDirection = 0;
        int particleApplyZoomToVelocity = 0;
        bool particleDeleteOutside = false;
        bool particleTriVolume = false;

        tTJSVariant particleMotionListVariant;

        // Nine values: fmin/fmax, vmin/vmax, amin/amax, zmin/zmax, range.
        double particleInterp[9] = {0.0, 0.0, 0.0, 0.0, 0.0,
                                    0.0, 0.0, 0.0, 0.0};

        // Particle children are held by a retained TJS Array.
        tTJSVariant particleArrayVar;

        // Persistent particle/emitter state before its ttstr target owner.
        double prevM11 = 1.0, prevM21 = 0.0;
        double prevM12 = 0.0, prevM22 = 1.0;
        double prevParticleAngle = 0.0;
        double emitterTimerAccum = 0.0;
        bool particleEmitterFlagActive = false;
        bool emitterActive = false;
        double emitterTimer = 0.0;
        ttstr emitterDtgt;

        // Emitter offsets and anchor damping follow the target owner.
        bool emitterOffsetActive = false;
        double emitterOffsetX = 0.0;
        double emitterOffsetY = 0.0;
        double emitterOffsetZ = 0.0;
        int anchorType_guess = 0;
        double feedbackTimespan;
        double anchorOpaScale = 1.0;
        double anchorColorScale[16] = {
            1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0
        };

        tTJSVariant stencilCompositeMaskLayerListVariant;
        std::vector<MotionNode *> stencilCompositeMaskNodes;
        CopiedOnlyTrailingWord_guess copiedOnlyTrailingWord_guess;

        [[nodiscard]] bool hasEmoteEdit_guess() const {
            return emoteEditVariant.Type() != tvtVoid;
        }

        ClipSlot &activeSlot() { return slots[activeSlotIndex]; }
        const ClipSlot &activeSlot() const { return slots[activeSlotIndex]; }
        ClipSlot &otherSlot() { return slots[activeSlotIndex ^ 1]; }
        const ClipSlot &otherSlot() const { return slots[activeSlotIndex ^ 1]; }

        Player *getChildPlayer() const;
        int getParticleCount() const;
        Player *getParticleChild(int index) const;
    };

} // namespace motion::detail
