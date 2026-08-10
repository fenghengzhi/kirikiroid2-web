//
// Persistent per-node state for MotionPlayer rendering pipeline.
// Aligned to libkrkr2.so 2632-byte node structure in std::deque.
//
// PSB key → node offset mapping (from IDA decompilation of sub_6B3C78 at 0x6B3C78):
//   "label"            → node+0   (name)
//   "type"             → node+28  (nodeType)
//   "coordinate"       → node+24  (coordinateMode)
//   "inheritMask"      → node+40  (inheritFlags, bits 2-8, default 0x1FC)
//   "groundCorrection" → node+47  (bool)
//   "transformOrder"   → node+84..96 (4 ints, default [0,1,2,3])
//   "frameList"        → node+64  (PSB variant for keyframes)
//   "meshTransform"    → node+2000 (meshType)
//   "stencilType"      → node+52
//   parentIndex        → node+36  (set during tree walk)
//   node+344, node+880 → clip slot done flags (initialized to 1=done)
//
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "MeshPoint.h"
#include "tjs.h"  // tTJSVariant, iTJSDispatch2 for TJS↔Native bridge (node+1912, node+2296)

class iTVPTexture2D;

namespace motion {
    class Player;
}

namespace motion::detail {

    struct MotionParameterEntry;
    struct PreparedRenderItem;

    struct MotionNode {
        MotionNode() = default;
        ~MotionNode();
        // MotionNode_copy@0x6F468C deliberately preserves the destination's
        // persistent render-item owner while assigning every other member.
        MotionNode(const MotionNode &) = delete;
        MotionNode &operator=(const MotionNode &);

        // Identity (from PSB, set once during tree build)
        int index = 0;
        int parentIndex = -1;          // node+36
        int layerId1 = 0;              // node+16: first requireLayerId result
        int layerId2 = 0;              // node+20: second requireLayerId result
        int nodeType = 0;              // node+28
        int coordinateMode = 0;        // node+24
        int inheritFlags = 0x1FC;      // node+40. Player_updateLayers (0x6BB33C)
                                        // also tests byte(node+42)&0x40, i.e.
                                        // inheritMask bit 0x00400000.
        uint8_t flags = 0;             // node+44 (sub_6BE0C0 at 0x6BE37C, sub_6BF0DC at 0x6BF310)
        // node+46 — PSB "joinTarget" bool. The ONLY writer is
        // Player_initNodeFields @0x6b3ef0 (`*(BYTE)(node+46) =
        // Motion_propGetBool("joinTarget",default 0) & 1`), set once at
        // tree-build. NOT a visibility/active byte (the prior MEMORY annotation
        // calling node+46 "visible" was falsified by the 0x6b3ef0 decompile).
        // Gates HM3 populate (resetMotionState loop3 @0x6b2dcc:
        // `if(!node+46) continue`) and HM3 restore (pruneHM3 loop2 @0x6b855c:
        // `if(*(BYTE)(node+46))` before the nodeType match).
        bool joinTarget = false;       // node+46
        bool groundCorrection = false; // node+47
        // TJS layer dispatch object for callbacks (sub_6BAA10 onGroundCorrection).
        // In libkrkr2.so this is at *(node+0)+16 (the layer's iTJSDispatch2*).
        // Stored as void* to avoid iTJSDispatch2 header dependency here;
        // cast to iTJSDispatch2* in Player.cpp where tjs.h is included.
        void *tjsLayerObject = nullptr;  // non-owning reference
        int transformOrder[4] = {0, 1, 2, 3}; // node+84..96
        // Player_initNodeFields @0x6B3C78 stores Motion_propGetString("label")
        // directly at node+0. Player_buildNodeTree_recursive @0x6B4A6C reuses
        // this same ttstr as the Player+24 map key and event payload source.
        ttstr layerName;
        int meshType = 0;             // "meshTransform" from PSB (node+2000)
        int meshFlags = 0;            // "meshSyncChildMask" from PSB (node+2004)
        int meshDivision = 0;         // "meshDivision" from PSB (node+2008)
        int meshDivX = 0;             // node+2012: horizontal cell count
        int meshDivY = 0;             // node+2016: vertical cell count
        int objTriPriority = 0;       // node+2136: "objTriPriority" for type==0
        // Aligned to libkrkr2.so Player_initNodeFields (0x6B3C78):
        // node+8 points to an entry selected from the player's 56-byte
        // parameter table using the PSB "parameterize" index.
        int parameterizeIndex = -1;
        MotionParameterEntry *parameterEntry = nullptr;
        // Mesh inverse matrix for sub_69AE74 child deformation (node+2096..2132)
        double meshInvM11 = 0, meshInvM12 = 0;  // node+2096, node+2104
        double meshInvM21 = 0, meshInvM22 = 0;  // node+2112, node+2120
        float meshInvOffX = 0, meshInvOffY = 0;  // node+2128, node+2132
        // Computed mesh flags (sub_6BC4F0 at 0x6BC6E4..0x6BC818)
        bool hasMeshData = false;        // node+1962: has active mesh data
        bool stencilCompositeMaskReferenced = false; // node+1961: post-build mask-layer reference
        bool meshCombineEnabled = false; // node+1963: mesh combines with children
        // libkrkr2.so seeds node+52 from PSB "stencilType" in
        // Player_initNodeFields (0x6B3C78) and later runtime stages only read
        // the field; they do not rebuild it from frame state.
        int stencilTypeBase = 0;      // raw PSB "stencilType"
        int stencilType = 0;          // runtime node+52, init-time owned
        int currentFrameType = 0;     // current frameList type (0/2/3), for trace
        // REMOVED 2026-06-21: hasLastActivePayload / lastActiveFrameIndex /
        // lastActiveSrc / lastActiveMotionFlags / lastActiveMotionDtgt — these
        // backed the invented markNodePayloadDirtyFromState node+44 dirtying
        // channel (no libkrkr2.so counterpart; see PlayerUpdateLayerEval.cpp).

        // Three independent std::vector<MeshPoint> owners copied by
        // MotionNode_copy@0x6F468C (0x6F47F4..0x6F480C) and destroyed in
        // reverse order by MotionNode_destroy_guess@0x6F4C8C
        // (0x6F4CFC..0x6F4D1C).  Player_updateLayers@0x6BC4F0 keeps the raw
        // 4x4 patch at +2024, builds a composite grid at +2048, and writes the
        // own-affine-transformed 4x4 patch at +2072; none is a "previous
        // frame" alias of another.
        std::vector<MeshPoint> meshControlPoints;             // node+2024
        std::vector<MeshPoint> compositeMeshPoints;           // node+2048
        std::vector<MeshPoint> transformedMeshControlPoints;  // node+2072

        // Raw TJS owners copied directly from the layer dispatch by
        // Player_initNodeFields @0x6B3C78. These mirror the source-level
        // tTJSVariant members at node+64/+1980/+2200/+2576; their independent
        // CopyRef lifetimes must not be replaced by a decoded PSB tree owner.
        tTJSVariant frameListVariant;                     // node+64
        tTJSVariant emoteEditVariant;                     // node+1980
        tTJSVariant particleMotionListVariant;            // node+2200
        tTJSVariant stencilCompositeMaskLayerListVariant; // node+2576

        // Prior draw flag (node+48, from PSB emoteEdit "priorDraw")
        // sub_6636D4 collapses the property to bool and 0x6BC6C4 masks & 1.
        int priorDraw = 0;

        // ========== Dual Clip Slot Architecture ==========
        // Aligned to libkrkr2.so's two 536-byte clip slots per node:
        //   slot0 @ node+320, slot1 @ node+856, activeSlotIndex @ node+1392.
        // When a new motion is played, data is written to the inactive slot,
        // then activeSlotIndex ^= 1 flips. The old slot is preserved for
        // crossfade blending.
        //
        struct ClipSlot {
            // Player_parseFrame @0x6926B4 / Player_mergeFrameContent
            // @0x692AB0 state. `merged` is the byte at slot+26; parse clears it
            // and merge sets it before the invisible early-return.
            bool done = true;
            bool crossfading = false;      // slot+25: currently blending with other slot
            bool merged = false;           // slot+26
            int frameIndex = -1;            // cached frameList index for this slot
            int frameType = 0;              // frame["type"]: 0 invisible, 2 static, 3 interpolate
            std::uint32_t ti = 0;            // slot+16, mask 0x04000000

            // Source. Player_mergeFrameContent @0x692AB0 stores two distinct
            // ttstr owners: icon at slot+28 and src at slot+36. All native
            // consumers read these owners directly; narrow strings are created
            // only at Web/PSB raw-node API boundaries.
            ttstr iconValue;
            ttstr srcValue;

            // Position (slot+96..112)
            double x = 0, y = 0, z = 0;
            double ox = 0, oy = 0;

            // Transform
            double width = 0, height = 0;
            int opacity = 255;
            double angle = 0.0;
            double scaleX = 1.0, scaleY = 1.0;
            double slantX = 0.0, slantY = 0.0;
            bool flipX = false, flipY = false;
            int blendMode = 16;
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };

            // Raw curve owners consumed through TJS dispatch at evaluation
            // time, matching 0x69A754/0x698454/0x69A4D4.
            tTJSVariant cccVariant;          // slot+168
            tTJSVariant occVariant;          // slot+188
            tTJSVariant accVariant;          // slot+208
            tTJSVariant zccVariant;          // slot+228
            tTJSVariant sccVariant;          // slot+248
            tTJSVariant cpVariant;           // slot+268
            tTJSVariant meshCurveVariant;    // slot+296, cc/mcc

            // Time (slot+328)
            double clipStartTime = 0.0;

            // Raw frame content mask (slot+340 = parseSlot+20). parseFrame
            // @0x6926E8 writes content["mask"] (Motion_propGetInt) here; bit
            // 0x40000 gates the "act" action field. resetFrameSlot defaults it
            // to 0. Snapshotted into the HM3 PerNodeLayerState (V+28) by
            // Player_HM3_initValueFromNode @0x699654 and restored from V by
            // Player_HM3_restoreValueToNode @0x6998b8 (slot+340 <- V[7]).
            int contentMask = 0;            // slot+340

            // Mesh control-point vector (slot+640). Per-slot mesh source read by
            // Player_evaluateTimeline @0x699c08 (copyVector node+2024 <- slot+640
            // when node+2000==meshType==1), snapshotted node+2024 -> V+568 by
            // Player_HM3_initValueFromNode @0x699588, and restored slot+640 <-
            // V+568 by Player_HM3_restoreValueToNode @0x699828. The element is
            // the binary's exact 8B {float x, float y} source-level point.
            std::vector<MeshPoint> meshControlPoints; // slot+640

            // Motion sub-object (mask 0x80000)
            int motionDt = 0, motionFlags = 0;
            double motionDofst = 0.0;
            bool motionDocmpl = false;
            double motionTimeOffset = 0.0;
            ttstr motionDtgtValue;

            // Model/camera/anchor/feedback blocks occupy the tail of the same
            // parsed-frame slot and are written by 0x693AE8..0x6941E4.
            double modelTimeOffset = 0.0;
            bool modelLoop = false;
            int modelDt = 0;
            ttstr modelDtgt;
            double cameraFov = 0.0;
            ttstr cameraTarget;
            ttstr anchorTarget;
            double feedbackTimespan = 0.0;

            // Particle sub-object (mask 0x100000). slot+416..488 PSB prt block,
            // written by Player_mergeFrameContent @0x693c64 (gate frame mask
            // 0x100000, sub-mask PSB "prt.mask"). Reset defaults @0x693d20:
            // {trigger=0, fmin=fmax=10.0, vmin=vmax=0, amin=amax=0, zmin=zmax=1.0,
            // range=0}. Field offsets byte-verified: trigger slot+416(slot[104]),
            // fmin slot+424, fmax slot+432, vmin slot+440, vmax slot+448, amin
            // slot+456, amax slot+464, zmin slot+472, zmax slot+480, range slot+488.
            //
            // This slot+424..488 9-double block is the SINGLE physical particle
            // block for type-4 nodes. It has TWO writers:
            //   (1) mergeFrameContent @0x693d98..0x693ecc — main per-frame writer
            //       during normal playback (PSB prt fields).
            //   (2) Player_HM3_restoreValueToNode @0x699890 — HM3 restore path only
            //       (memcpy V+600..664 -> slot+744, 0x48) when V.nodeType==4 &&
            //       V+32==0.
            // It has TWO readers, both in Player_evaluateTimeline's type-4 case:
            //   - COPY branch @0x699c2c (single-slot / no-crossfade): node+2224..2288
            //     <- slot+744..808 (v11[93..101], v11=node+536*idx).
            //   - INTERP branch @0x69a0f8 (crossfade): per-field lerp of active/other
            //     slots' slot+424..488 into node+2224..2288.
            // ALIAS (self-disassembled, see PlayerUpdateLayerEval.cpp): the COPY
            // offset 744 is slot-base 320: node+536*idx+744 == node+320+536*idx+424
            // == slot+424. So slot+744 IS slot+424; COPY reads the SAME prt block
            // INTERP reads and merge/restore write. There is NO separate result
            // region — prtFmin..prtRange below cover the whole block.
            int prtTrigger = 0;
            double prtFmin = 10.0, prtF = 10.0;
            double prtVmin = 0.0, prtV = 0.0;
            double prtAmin = 0.0, prtA = 0.0;
            double prtZmin = 1.0, prtZ = 1.0;
            double prtRange = 0.0;

            // TransformOrder
            bool hasTransformOrder = false;
            int transformOrder[4] = {0,1,2,3};
            ttstr actionValue;               // slot+288, content.act
            bool hasSync = false;
        };

        ClipSlot slots[2];
        int activeSlotIndex = 0;       // node+1392
        ClipSlot& activeSlot() { return slots[activeSlotIndex]; }
        const ClipSlot& activeSlot() const { return slots[activeSlotIndex]; }
        ClipSlot& otherSlot() { return slots[activeSlotIndex ^ 1]; }
        const ClipSlot& otherSlot() const { return slots[activeSlotIndex ^ 1]; }
        // Player_evaluateTimeline (0x699AE4) stores exactly one previous blend
        // ratio at node+56. A parameterized node reads parameterEntry->value
        // directly; there is no secondary override/cache-validity state.
        double timelineEvalRatio = 0.0;

        // TJS setter / camera velocity override block.
        // Aligned to libkrkr2.so node+1584..+1660: delta block consumed by
        // Player_updateLayers phase2 (0x6BB630..0x6BB700). Written by root
        // TJS setters (setX/setY/setFlipX @ 0x6CD028/0x6CD048/0x6CD068) and
        // camera velocity @ 0x6BB378..0x6BB3DC.
        struct DeltaState {
            bool dirty = true;               // node+1584
            bool activeOverride = true;      // node+1585
            bool visibleOverride = true;     // node+1586
            bool flipX = false;              // node+1587
            bool flipY = false;              // node+1588
            double posX = 0.0;               // node+1592
            double posY = 0.0;               // node+1600
            double posZ = 0.0;               // node+1608
            double angle = 0.0;              // node+1616
            double scaleX = 1.0;             // node+1624
            double scaleY = 1.0;             // node+1632
            double slantX = 0.0;             // node+1640
            double slantY = 0.0;             // node+1648
            int opacity = 255;               // node+1656
        } delta;

        // Working/evaluated state (built during updateLayers inheritance loop)
        // Aligned to libkrkr2.so node+0x5E0..0x628 block written by
        // Player_evaluateTimeline (0x699AE4) and further composed by
        // Player_updateLayers (0x6BB33C).
        struct AccumulatedState {
            bool visible = true;
            bool active = true;
            bool dirty = true;      // node+1504
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
            int opacity = 255;         // 0-255 integer, matching libkrkr2.so int math
            int blendMode = 16;        // node+1656: accumulated blend mode (default 0x10)
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

        // MotionNode_destroy_guess @0x6F4C8C owns and deletes node+1904.
        // sub_6C2334 @0x6C32D0 reuses this raw render item across calls and
        // allocates it only when the pointer is null.  The caller's main/aux
        // vectors contain borrowed pointers to this object; they never own it.
        PreparedRenderItem *preparedRenderItem = nullptr;

        // Path B visibility flag (node+1960), written by sub_6BD8DC @
        // 0x6BD958. Consumed by: the visibleAncestor chain walk in the
        // same sub_6BD8DC pass (PlayerUpdateLayerEval.cpp), copied to
        // PreparedRenderItem::drawFlag (item+19) in sub_6C2334's item
        // build, and exposed to TJS via the layerVisible getter
        // (PlayerLayerQuery.cpp). NOT read by Player_calcBounds — that
        // function gates on nodeType mask + source.valid instead
        // (see PlayerRenderItems.cpp comment). NOT the Path A main
        // render gate either.
        bool drawFlag = false;

        // node+1944 in libkrkr2.so sub_6C2334 — set to 1 when a node
        // enters the Path A render list (mainList), cleared at the top
        // of the outer loop. Mirrors a real native field for parity but
        // currently has no port consumer; kept for the Phase 4
        // motion_playback differential oracle.
        bool drawnThisFrame = false;

        int forceVisible = 0;              // node+1996
        int visibleAncestorIndex = -1;     // replaces pointer at node+1952

        // node+2600 std::vector<MotionNode *> populated by the type-12
        // post-pass in Player_buildNodeTree @0x6B51F0.
        std::vector<MotionNode *> stencilCompositeMaskNodes;

        // Child Player for nodeType=3 (Motion).
        // Aligned to libkrkr2.so node+1912: tTJSVariant holding iTJSDispatch2-wrapped
        // Player object, created by sub_6B3C78 case 3 via sub_6F1794 (NCB CreateAdaptor).
        // Use getChildPlayer() helper to extract native Player*.
        tTJSVariant childPlayerVar;

        // Particle children for nodeType=4 (Particle).
        // Aligned to libkrkr2.so node+2296: tTJSVariant holding TJS Array of
        // iTJSDispatch2-wrapped Player objects, created by sub_6B3C78 case 4 via
        // sub_704CB8 (TJSCreateArrayObject).
        // Use getParticleCount()/getParticleChild(i)/addParticleChild()/eraseParticleChild()
        // helpers for Array operations matching sub_56C694/sub_6C1678.
        tTJSVariant particleArrayVar;

        // Shape type for nodeType=1 (from PSB "shape" key, sub_6B3C78 case 1)
        int shapeType = 0;             // node+32: 0=point, 1=circle, 2=rect, 3=quad

        // Shape AABB for nodeType=7 (sub_6BDCC0 at 0x6BDCC0)
        float shapeAABB[4] = {};       // node+2144: minX, minY, maxX, maxY

        // Shape geometry for nodeType=1 (sub_6BDE94 at 0x6BDE94)
        int shapeGeomType = 0;         // node+1664: stored shape type
        double shapeVertices[16] = {}; // node+1672..1784: shape geometry

        // Vertex-computed position (sub_6BC4F0 at 0x6BC4F0)
        double vertexPosX = 0.0;       // node+152
        double vertexPosY = 0.0;       // node+160
        double vertexPosZ = 0.0;       // node+168

        // Vertex output (sub_6BC4F0)
        float vertices[8] = {};        // node+1856..1884: 4 corners x 2 floats

        // Bounding box output (Player_calcBounds, 0x6C3D04)
        float bounds[4] = { 1.0f, 1.0f, -1.0f, -1.0f }; // node+1888..1900

        // Player::findSourceForNode_guess writes this persistent node-level
        // descriptor. The texture pointer is non-owning: the loaded module's
        // group-atlas cache owns it, so MotionNode destruction/copy never
        // Release/AddRef it (MotionNode_destroy @0x6F4C8C, copy @0x6F468C).
        struct SourceState {
            bool valid = false;             // node+200
            bool blank = false;             // node+201
            tTJSVariant object;              // node+204
            iTVPTexture2D *texture = nullptr; // node+224, non-owning
            double width = 0.0;              // node+232
            double height = 0.0;             // node+240
            double originX = 0.0;            // node+248
            double originY = 0.0;             // node+256
            double clipLeft = 0.0;           // node+264
            double clipTop = 0.0;            // node+272
            double clipRight = 1.0;          // node+280
            double clipBottom = 1.0;         // node+288
            std::array<int, 4> textureRect{0, 0, 0, 0}; // node+296..308
            std::string path;                // node+312 (ttstr in Android)

            void clear() {
                valid = false;
                blank = false;
                object.Clear();
                texture = nullptr;
                width = height = originX = originY = 0.0;
                clipLeft = clipTop = 0.0;
                clipRight = clipBottom = 1.0;
                textureRect = {0, 0, 0, 0};
                path.clear();
            }
        } source;

        // Per-frame type-specific eval outputs written by
        // Player_evaluateTimeline @0x699AE4.
        // MotionNode_initFields @0x6F19B4 deliberately leaves both evaluator
        // output channels uninitialized; Player_evaluateTimeline writes them
        // before their type-specific consumers run.
        double cameraFov;             // node+2368: camera.fov (nodeType=5)

        // Anchor node data for nodeType=10 (sub_6C0528 at 0x6C0528)
        int anchorType = 0;            // node+2376: "anchor" from PSB
        double feedbackTimespan;      // node+2432: feedback.timespan
        double anchorOpaScale = 1.0;   // node+2440: opacity damping scale
        // Anchor color channel scales (4 channels × gamma factor, node+2448..2504)
        double anchorColorScale[16] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};

        // Camera constraint for nodeType=9 (sub_6BC000 at 0x6BC000)
        int cameraConstraintType = 0;  // node+2376: "anchor" type

        // Particle eval-output mirror node+2224..2288 (9 doubles, the type-4
        // analog of the +1512.. transform mirror). WRITTEN by
        // Player_evaluateTimeline @0x699AE4 type-4 branches:
        //   - COPY branch @0x699c2c (single slot / no crossfade): node+2224..2288
        //     <- active slot prt block slot+744..808 (== slot+424..488, same bytes);
        //   - INTERP branch @0x69a0f8 (crossfade): node+2224..2288 <- lerp(
        //     activeSlot prt block slot+424..488, otherSlot prt block, ratio).
        // READ by the particle emitter Player_particleEmitterPass @0x6BF0DC
        // (node4[139..143] == node+2224..2288: fmin/fmax velocity/accel/zoom/range)
        // and snapshotted into HM3 V+600..664 by Player_HM3_initValueFromNode
        // @0x6995dc. Field map (binary slot+424..488 / prt names):
        //   [0] node+2224 fmin (prtFmin)   [1] node+2232 fmax (prtF)
        //   [2] node+2240 vmin (prtVmin)   [3] node+2248 vmax (prtV)
        //   [4] node+2256 amin (prtAmin)   [5] node+2264 amax (prtA)
        //   [6] node+2272 zmin (prtZmin)   [7] node+2280 zmax (prtZ)
        //   [8] node+2288 range (prtRange)
        double particleInterp[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};  // node+2224..2288

        // (particleChildren replaced by particleArrayVar above — TJS Array)
        int particleType = 0;          // node+2164: particle subtype
        int particleMaxNum = 0;        // node+2168: max particle count
        // Binary: node+2192 is a SINGLE field used as both "accel ratio" (decay
        // factor in exponential velocity mode) and "camera damping" (copied to
        // child player). PSB key: "particleAccelRatio". See sub_6BF0DC.
        double particleAccelRatio = 0; // node+2192 — also used as cameraDamping
        bool particleInheritAngle = false; // node+2172
        int particleInheritVelocity = 0;   // node+2176
        int particleFlyDirection = 0;      // node+2180
        int particleApplyZoomToVelocity = 0; // node+2184
        bool particleDeleteOutside = false;  // node+2188
        bool particleTriVolume = false;    // node+2189: PSB "particleTriVolume", 3D particle flag
        // Previous frame matrix for change detection (node+2320..2336)
        double prevM11 = 1.0, prevM21 = 0.0;
        double prevM12 = 0.0, prevM22 = 1.0;
        double prevParticleAngle = 0.0;        // node+2352
        double emitterTimerAccum = 0.0;        // node+2360: frequency timer
        bool particleEmitterFlagActive = false; // v8[135].u8[0]

        // Particle emitter state for nodeType=6 (sub_6BEDD0 at 0x6BEDD0)
        bool emitterActive = false;    // node+2380
        double emitterTimer = 0.0;     // node+2392
        ttstr emitterDtgt;             // node+2384, CopyRef of active slot src
        bool emitterOffsetActive = false; // node+2400
        double emitterOffsetX = 0.0;   // node+2408
        double emitterOffsetY = 0.0;   // node+2416
        double emitterOffsetZ = 0.0;   // node+2424

        // Delta position (post-loop: accumulated - prev, node+176/184/192)
        double deltaPosX = 0.0;
        double deltaPosY = 0.0;
        double deltaPosZ = 0.0;

        // Clip origin offsets (from clip slot+376/384, used by sub_6BC4F0)
        double clipOriginX = 0.0;      // node+248 local
        double clipOriginY = 0.0;      // node+256 local

        // sub_6BDCC0 @0x6BDCC0: node+1936 points directly to the inherited
        // type-7 clip AABB (node+2144).  This pointer may cross Player node
        // containers through child-motion propagation at 0x6BE278.
        const float *clipAABB = nullptr;

        // sub_6BC4F0 @0x6BC4F0: node+1968 is the independent mesh-transform
        // ancestor chain.  Keep it separate from node+1936 exactly as the
        // child-motion propagation at 0x6BE278/0x6BE290 does.
        MotionNode *meshAncestor = nullptr;

        bool anchorEnabled = false;

        // Color bytes for anchor damping (node+100..115: 4 sets of RGBA)
        uint8_t colorBytes[16] = {
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF
        };

        // Particle trigger type used by sub_6BEDD0; refreshed with timeline eval.
        int prtTrigger = 0;

        // === TJS↔Native bridge helpers ===
        // These are implemented in MotionNodeBridge.cpp to avoid circular
        // dependency (MotionNode.h cannot include Player.h or ncbind.hpp).

        // nodeType=3: Get native Player* from childPlayerVar (node+1912).
        // Aligned to sub_6BE0C0 NativeInstanceSupport pattern.
        // Returns nullptr if childPlayerVar is void/invalid.
        Player* getChildPlayer() const;

        // nodeType=4: Get particle count from TJS Array (node+2296).
        // Aligned to sub_56C694: Array.count.
        int getParticleCount() const;

        // nodeType=4: Get native Player* for particle child at index.
        // Aligned to sub_6C1678: Array[i] + NativeInstanceSupport.
        Player* getParticleChild(int index) const;

        // nodeType=4: Get iTJSDispatch2* for particle child at index.
        // Returns the TJS dispatch object (for passing to sub_6B29C0 etc).
        iTJSDispatch2* getParticleChildDispatch(int index) const;

        // nodeType=4: Add a TJS-wrapped Player to the particle Array.
        // Aligned to TJS Array.add.
        void addParticleChild(const tTJSVariant &playerVar);

        // nodeType=4: Erase particle child at index from TJS Array.
        // Aligned to TJS Array.erase(index).
        void eraseParticleChild(int index);
    };

} // namespace motion::detail
