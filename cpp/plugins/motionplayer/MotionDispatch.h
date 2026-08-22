#pragma once

#include <utility>
#include <vector>

#include "tjs.h"

namespace motion::detail {

    // Process-wide mutable TJS member-hint slots. These are distinct plugin
    // globals shared by the four-reference dispatch sites that use each slot;
    // they are not per-function caches.
    extern tjs_uint32 widthMemberHint_guess;   // "width"
    extern tjs_uint32 heightMemberHint_guess;  // "height"
    extern tjs_uint32 originXMemberHint_guess; // "originX"
    extern tjs_uint32 originYMemberHint_guess; // "originY"
    // ResourceManager's blank-descriptor publisher and MotionNode's source
    // fallback reader reuse this exact word.
    extern tjs_uint32 blankMemberHint_guess;   // "blank"
    extern tjs_uint32 clipMemberHint_guess;    // "clip"
    extern tjs_uint32 leftMemberHint_guess;    // "left"
    extern tjs_uint32 topMemberHint_guess;     // "top"
    extern tjs_uint32 rightMemberHint_guess;   // "right"
    extern tjs_uint32 bottomMemberHint_guess;  // "bottom"
    // Player::getBounds reuses all six geometry slots above; it does not own
    // bounds-private duplicates.
    extern tjs_uint32 xMemberHint_guess;       // "x"
    extern tjs_uint32 yMemberHint_guess;       // "y"
    // Position-control t/s are independent plugin-wide slots immediately
    // following the shared x/y pair in every reference image.
    extern tjs_uint32 positionControlTMemberHint_guess; // "t"
    extern tjs_uint32 positionControlSMemberHint_guess; // "s"
    // Controller-state schema. These slots are shared across the state
    // serializers/restorers; `length` is also reused by the bust-chain spring
    // metadata constructor.
    extern tjs_uint32 controllerPhaseHint_guess;        // "phase"
    extern tjs_uint32 controllerFrameHint_guess;        // "frame"
    extern tjs_uint32 controllerVHint_guess;            // "v"
    extern tjs_uint32 controllerTargetHint_guess;       // "target"
    extern tjs_uint32 controllerLengthHint_guess;       // "length"
    extern tjs_uint32 controllerLengthDoneHint_guess;   // "lengthDone"
    extern tjs_uint32 controllerExponentHint_guess;     // "exponent"
    extern tjs_uint32 controllerSpeedHint_guess;        // "speed"
    extern tjs_uint32 controllerRequestQueueHint_guess; // "rq"
    extern tjs_uint32 controllerP0Hint_guess;           // "p0"
    extern tjs_uint32 controllerP1Hint_guess;           // "p1"
    extern tjs_uint32 controllerMouthHint_guess;        // "mouth"
    extern tjs_uint32 controllerPrevHint_guess;         // "prev"
    extern tjs_uint32 controllerTickHint_guess;         // "tick"

    // Shared metadata schema used by the Eye/Blink, Eyebrow and Mouth
    // controller constructors. Eye consumes all eight slots; Eyebrow reuses
    // beginFrame/edge/node and Mouth reuses beginFrame.
    extern tjs_uint32 emoteControllerBeginFrameHint_guess;
    extern tjs_uint32 emoteControllerEndFrameHint_guess;
    extern tjs_uint32 emoteControllerBlinkIntervalMinHint_guess;
    extern tjs_uint32 emoteControllerBlinkIntervalMaxHint_guess;
    extern tjs_uint32 emoteControllerBlinkFrameCountHint_guess;
    extern tjs_uint32 emoteControllerBlinkEnabledHint_guess;
    extern tjs_uint32 emoteControllerEdgeHint_guess;
    extern tjs_uint32 emoteControllerNodeHint_guess;

    // Clamp metadata reads and the EmotePlayer HM5 range-wrapper writes reuse
    // these exact process-wide min/max slots in all four references.
    extern tjs_uint32 emoteVariableRangeMinHint_guess;
    extern tjs_uint32 emoteVariableRangeMaxHint_guess;

    // Spring metadata schema. The simple spring owns this five-slot family;
    // BustChain reuses gravity/scale_x/scale_y and has seven additional scalar
    // slots. Its nested length property reuses controllerLengthHint_guess.
    extern tjs_uint32 emoteSpringGravityHint_guess;
    extern tjs_uint32 emoteSpringCoefficientHint_guess;
    extern tjs_uint32 emoteSpringFrictionHint_guess;
    extern tjs_uint32 emoteSpringScaleXHint_guess;
    extern tjs_uint32 emoteSpringScaleYHint_guess;
    extern tjs_uint32 emoteBustChainFrictionXHint_guess;
    extern tjs_uint32 emoteBustChainFrictionYHint_guess;
    extern tjs_uint32 emoteBustChainBRateHint_guess;
    extern tjs_uint32 emoteBustChainVBoundHint_guess;
    extern tjs_uint32 emoteBustChainUdEftHint_guess;
    extern tjs_uint32 emoteBustChainBendSpdHint_guess;
    extern tjs_uint32 emoteBustChainBendVolHint_guess;

    // These keys use process-wide mutable member-hint slots. The render-source
    // descriptor "key" word is shared by its flags-0 SourceCache read and all
    // five publisher families (normal/canvas/accurate/command-list/private-
    // GLL); it is not a per-call or per-descriptor cache. In particular, type
    // is shared across the frame parser and the later Layer-publication paths;
    // it is not calc-local.  Its apparent BSS successor in the four references
    // is an ABI-aligned process-global PSB OwnerFilter std::function, not a
    // further tjs_uint32 hint; physical discovery order does not define this
    // portable declaration block.
    extern tjs_uint32 commandKeyMemberHint_guess;          // "key"
    extern tjs_uint32 mtxMemberHint_guess;                 // "mtx"
    extern tjs_uint32 triPriorityMemberHint_guess;         // "triPriority"
    extern tjs_uint32 clipRectMemberHint_guess;            // "clipRect"
    extern tjs_uint32 meshTransformMemberHint_guess;       // "meshTransform"
    extern tjs_uint32 compositeMeshMemberHint_guess;       // "compositeMesh"
    extern tjs_uint32 stencilChainMemberHint_guess;        // "stencilChain"
    extern tjs_uint32 vtxMemberHint_guess;                 // "vtx"
    extern tjs_uint32 divxMemberHint_guess;                // "divx"
    extern tjs_uint32 divyMemberHint_guess;                // "divy"
    // One contiguous native hint group backs raw timeline-frame parsing.
    // Each slot is process-wide: time/content/mask are also reused by the
    // skip/merge paths, while type has the broader users listed above.
    extern tjs_uint32 timeMemberHint_guess;                // "time"
    extern tjs_uint32 typeMemberHint_guess;                // "type"
    extern tjs_uint32 contentMemberHint_guess;             // "content"
    extern tjs_uint32 maskMemberHint_guess;                // "mask"
    extern tjs_uint32 actMemberHint_guess;                 // "act"

    // The four references place this complete 53-slot family contiguously,
    // immediately after the time/type/content/mask/act frame-parser group.
    // Repeated nested names (dt, dtgt, timeOffset and target) deliberately
    // reuse one slot. src/coord/color/angle/mesh/bezierPatch/motion are also
    // shared by the non-merge call sites that use the same native addresses.
    // `icon` is the sole adjacent merge property without a member-hint slot.
    extern tjs_uint32 srcMemberHint_guess;                  // "src"
    extern tjs_uint32 nodeFrameOxMemberHint_guess;          // "ox"
    extern tjs_uint32 nodeFrameOyMemberHint_guess;          // "oy"
    extern tjs_uint32 coordMemberHint_guess;                // "coord"
    extern tjs_uint32 nodeFrameBmMemberHint_guess;          // "bm"
    extern tjs_uint32 colorMemberHint_guess;                // "color"
    extern tjs_uint32 nodeFrameOpaMemberHint_guess;         // "opa"
    extern tjs_uint32 nodeFrameFxMemberHint_guess;          // "fx"
    extern tjs_uint32 nodeFrameFyMemberHint_guess;          // "fy"
    extern tjs_uint32 angleMemberHint_guess;                // "angle"
    extern tjs_uint32 nodeFrameZxMemberHint_guess;          // "zx"
    extern tjs_uint32 nodeFrameZyMemberHint_guess;          // "zy"
    extern tjs_uint32 nodeFrameSxMemberHint_guess;          // "sx"
    extern tjs_uint32 nodeFrameSyMemberHint_guess;          // "sy"
    extern tjs_uint32 nodeFrameTiMemberHint_guess;          // "ti"
    extern tjs_uint32 nodeFrameCccMemberHint_guess;         // "ccc"
    extern tjs_uint32 nodeFrameOccMemberHint_guess;         // "occ"
    extern tjs_uint32 nodeFrameAccMemberHint_guess;         // "acc"
    extern tjs_uint32 nodeFrameZccMemberHint_guess;         // "zcc"
    extern tjs_uint32 nodeFrameSccMemberHint_guess;         // "scc"
    extern tjs_uint32 nodeFrameCpMemberHint_guess;          // "cp"
    extern tjs_uint32 meshMemberHint_guess;                 // "mesh"
    extern tjs_uint32 nodeFrameObjMemberHint_guess;         // "obj"
    extern tjs_uint32 nodeFrameCcMemberHint_guess;          // "cc"
    extern tjs_uint32 nodeFrameMccMemberHint_guess;         // "mcc"
    extern tjs_uint32 nodeFrameBpMemberHint_guess;          // "bp"
    extern tjs_uint32 bezierPatchMemberHint_guess;          // "bezierPatch"
    extern tjs_uint32 motionMemberHint_guess;               // "motion"
    extern tjs_uint32 nodeFrameFlagsMemberHint_guess;       // "flags"
    extern tjs_uint32 nodeFrameDtMemberHint_guess;          // "dt"
    extern tjs_uint32 nodeFrameDocmplMemberHint_guess;      // "docmpl"
    extern tjs_uint32 nodeFrameDofstMemberHint_guess;       // "dofst"
    extern tjs_uint32 nodeFrameDtgtMemberHint_guess;        // "dtgt"
    extern tjs_uint32 nodeFrameTimeOffsetMemberHint_guess;  // "timeOffset"
    extern tjs_uint32 nodeFrameModelMemberHint_guess;       // "model"
    extern tjs_uint32 nodeFrameLoopMemberHint_guess;        // "loop"
    extern tjs_uint32 nodeFramePrtMemberHint_guess;         // "prt"
    extern tjs_uint32 nodeFrameTriggerMemberHint_guess;     // "trigger"
    extern tjs_uint32 nodeFrameFminMemberHint_guess;        // "fmin"
    extern tjs_uint32 nodeFrameFmaxMemberHint_guess;        // "fmax"
    extern tjs_uint32 nodeFrameVminMemberHint_guess;        // "vmin"
    extern tjs_uint32 nodeFrameVmaxMemberHint_guess;        // "vmax"
    extern tjs_uint32 nodeFrameAminMemberHint_guess;        // "amin"
    extern tjs_uint32 nodeFrameAmaxMemberHint_guess;        // "amax"
    extern tjs_uint32 nodeFrameZminMemberHint_guess;        // "zmin"
    extern tjs_uint32 nodeFrameZmaxMemberHint_guess;        // "zmax"
    extern tjs_uint32 nodeFrameRangeMemberHint_guess;       // "range"
    extern tjs_uint32 nodeFrameCameraMemberHint_guess;      // "camera"
    extern tjs_uint32 nodeFrameFovMemberHint_guess;         // "fov"
    extern tjs_uint32 nodeFrameTargetMemberHint_guess;      // "target"
    extern tjs_uint32 nodeFrameAnchorMemberHint_guess;      // "anchor"
    extern tjs_uint32 nodeFrameFeedbackMemberHint_guess;    // "feedback"
    extern tjs_uint32 nodeFrameTimespanMemberHint_guess;    // "timespan"

    // The four references place this complete 21-slot Player family in the
    // following order. It crosses source-level helper boundaries: loadMotion,
    // parameter append, the Emote play branch, node initialization/rendering,
    // the modified-node prepass and ground-correction callback all reuse the
    // listed process-wide slots rather than owning TU-local duplicates.
    extern tjs_uint32 requestCharaMemberHint_guess;          // "chara"
    extern tjs_uint32 onFindMotionMemberHint_guess;          // "onFindMotion"
    extern tjs_uint32 findMotionMemberHint_guess;            // "findMotion"
    extern tjs_uint32 commandIdMemberHint_guess;             // "id"
    extern tjs_uint32 playerParameterDiscretizationHint_guess; // "discretization"
    extern tjs_uint32 playerParameterRangeBeginHint_guess;   // "rangeBegin"
    extern tjs_uint32 playerParameterRangeEndHint_guess;     // "rangeEnd"
    extern tjs_uint32 divisionMemberHint_guess;              // "division"
    extern tjs_uint32 motionListMemberHint_guess;            // "motionList"
    extern tjs_uint32 nodeEmoteEditMemberHint_guess;         // "emoteEdit"
    extern tjs_uint32 nodeLabelMemberHint_guess;             // "label"
    extern tjs_uint32 parameterizeMemberHint_guess;          // "parameterize"
    extern tjs_uint32 coordinateMemberHint_guess;            // "coordinate"
    extern tjs_uint32 nodeJoinTargetMemberHint_guess;        // "joinTarget"
    extern tjs_uint32 nodeGroundCorrectionMemberHint_guess;  // "groundCorrection"
    extern tjs_uint32 nodeFrameListMemberHint_guess;         // "frameList"
    extern tjs_uint32 nodeInheritMaskMemberHint_guess;       // "inheritMask"
    extern tjs_uint32 nodeTransformOrderMemberHint_guess;    // "transformOrder"
    extern tjs_uint32 nodeRequireLayerIdMemberHint_guess;    // "requireLayerId"
    extern tjs_uint32 emoteEditModifiedHint_guess;           // "modified"
    extern tjs_uint32 onGroundCorrectionMemberHint_guess;    // "onGroundCorrection"

    // updateLayers vertex computation owns this immediately following
    // six-slot EmoteEdit family. priorDraw is read before the mesh-dirty gate;
    // the five transform members are written by the force-visible mirror.
    // Its final "angle" write deliberately reuses angleMemberHint_guess from
    // the earlier node-frame family rather than allocating an adjacent slot.
    extern tjs_uint32 emoteEditPriorDrawMemberHint_guess; // "priorDraw"
    extern tjs_uint32 emoteEditFlipXMemberHint_guess;     // "flipX"
    extern tjs_uint32 emoteEditFlipYMemberHint_guess;     // "flipY"
    extern tjs_uint32 emoteEditZoomXMemberHint_guess;     // "zoomX"
    extern tjs_uint32 emoteEditZoomYMemberHint_guess;     // "zoomY"
    extern tjs_uint32 emoteEditSlantXMemberHint_guess;    // "slantX"

    // The immediately following two-slot particle Array family. The outer
    // type-4 pass uses add and erase; the two-pass child worker reuses erase.
    extern tjs_uint32 particleArrayAddMemberHint_guess;   // "add"
    extern tjs_uint32 particleArrayEraseMemberHint_guess; // "erase"

    // The immediately following three slots belong to render-source
    // resolution. loadSource is resolver-only; blendMode and assignImages
    // are shared by the other render call families using the same caches.
    extern tjs_uint32 loadSourceMemberHint_guess;   // "loadSource"
    extern tjs_uint32 blendModeMemberHint_guess;    // "blendMode"
    extern tjs_uint32 assignImagesMemberHint_guess; // "assignImages"

    // Pending-event dispatch owns the next two slots. A single callback-result
    // Variant is reused while the loop reloads the live event-vector end.
    extern tjs_uint32 onSyncMemberHint_guess;   // "onSync"
    extern tjs_uint32 onActionMemberHint_guess; // "onAction"

    // The immediately following renderer-primitive family has twelve
    // independent process-global slots. Copy primitives dispatch on the
    // Layer instance; setClip/operate*/draw* dispatch through the Layer class
    // with the render Layer as objthis. Both setClip call shapes share the
    // same slot.
    extern tjs_uint32 meshCopyMemberHint_guess; // "meshCopy"
    extern tjs_uint32 bezierPatchCopyMemberHint_guess; // "bezierPatchCopy"
    extern tjs_uint32 affineCopyMemberHint_guess; // "affineCopy"
    extern tjs_uint32 setClipMemberHint_guess; // "setClip"
    extern tjs_uint32 bufLayerMemberHint_guess; // "bufLayer"
    extern tjs_uint32 operateMeshMemberHint_guess; // "operateMesh"
    extern tjs_uint32 operateBezierPatchMemberHint_guess; // "operateBezierPatch"
    extern tjs_uint32 operateAffineMemberHint_guess; // "operateAffine"
    extern tjs_uint32 drawMeshFrameMemberHint_guess; // "drawMeshFrame"
    extern tjs_uint32 drawBezierPatchMeshFrameMemberHint_guess; // "drawBezierPatchMeshFrame"
    extern tjs_uint32 drawBezierPatchFrameMemberHint_guess; // "drawBezierPatchFrame"
    extern tjs_uint32 drawLineMemberHint_guess; // "drawLine"

    // The four references place these eight slots immediately after the twelve
    // renderer-primitive slots. visible is shared by SLA assign,
    // accurate rendering, calcViewParam and sticky shared-D3D draw; opacity is
    // shared by SLA assign, accurate rendering, calcViewParam and
    // getCommandList. setPos belongs only to accurate rendering; isValid is
    // used by all three Player::getBounds result branches. parameter has only
    // Player::initNonEmoteMotion as a consumer but remains the next global
    // cache word in this exact process-wide sequence. Old-tree reset owns the
    // releaseLayerId slot; internal-workspace materialization and accurate-SLA
    // post-draw own the following window and piledCopy slots respectively.
    // This proven contiguous family ends at piledCopy: Player::isExistMotion
    // owns a separate function-local static hint whose physical placement
    // differs among the four reference targets.
    extern tjs_uint32 visibleMemberHint_guess; // "visible"
    extern tjs_uint32 setPosMemberHint_guess;  // "setPos"
    extern tjs_uint32 opacityMemberHint_guess; // "opacity"
    extern tjs_uint32 isValidMemberHint_guess; // "isValid"
    extern tjs_uint32 parameterMemberHint_guess; // "parameter"
    extern tjs_uint32 releaseLayerIdMemberHint_guess; // "releaseLayerId"
    extern tjs_uint32 windowMemberHint_guess; // "window"
    extern tjs_uint32 piledCopyMemberHint_guess; // "piledCopy"

    // Player::random and ResourceManager::random share this exact process-wide
    // cache word in all four references, including Player's inlined copies.
    // It is not two source-function-local statics. Its declaration here does
    // not imply membership in the proven contiguous family above.
    extern tjs_uint32 randomMemberHint_guess; // "random"

    // calcViewParam has only this six-slot contiguous private family. `patch`
    // is also reused by getCommandList. Its other output names deliberately
    // reuse the broader blend/source, geometry, node and render hint families.
    extern tjs_uint32 calcMbpMemberHint_guess;       // "mbp"
    extern tjs_uint32 calcInvOffsetMemberHint_guess; // "invOffset"
    extern tjs_uint32 calcInvMatrixMemberHint_guess; // "invMatrix"
    extern tjs_uint32 patchMemberHint_guess;         // "patch"
    extern tjs_uint32 calcCmeshMemberHint_guess;     // "cmesh"
    extern tjs_uint32 calcMatrixMemberHint_guess;    // "matrix"

    // The four references place this complete five-slot SourceCache bake
    // family contiguously in the exact order below. setSize is additionally
    // shared by SeparateLayerAdaptor assign and Player render/materialization;
    // operateRect is additionally shared by Player canvas rendering. The
    // next word is primaryLayer and therefore closes this family boundary.
    extern tjs_uint32 drawLayerMemberHint_guess;   // "drawLayer"
    extern tjs_uint32 setSizeMemberHint_guess;     // "setSize"
    extern tjs_uint32 copyRectMemberHint_guess;    // "copyRect"
    extern tjs_uint32 operateRectMemberHint_guess; // "operateRect"
    extern tjs_uint32 adjustGammaMemberHint_guess; // "adjustGamma"
    // The following descriptor-bridging words start at the proven boundary.
    extern tjs_uint32 primaryLayerMemberHint_guess; // "primaryLayer"
    extern tjs_uint32 fillRectMemberHint_guess;    // "fillRect"
    extern tjs_uint32 neutralColorMemberHint_guess; // "neutralColor"
    // MotionLayer mesh submission and the alpha-mask compositor share this
    // exact word; it is not one function-local cache per translation unit.
    extern tjs_uint32 updateMemberHint_guess;      // "update"
    // Position-control p is an independent plugin-wide slot immediately
    // preceding (and distinct from) the shared Layer-class slot.
    extern tjs_uint32 positionControlPMemberHint_guess; // "p"
    extern tjs_uint32 layerClassMemberHint_guess;  // "Layer"
    // SeparateLayerAdaptor's three Layer-publication paths share this exact
    // word immediately following the Layer-class factory hint.
    extern tjs_uint32 absoluteMemberHint_guess;    // "absolute"
    // Only the payload and payload-free SeparateLayer resolvers share this
    // target-publication word. Its linked distance from `absolute` varies by
    // target because unrelated globals are interleaved between them.
    extern tjs_uint32 hitThresholdMemberHint_guess; // "hitThreshold"

    // The input is copied into a mutable remainder. Each separator-delimited
    // prefix is pushed as an independently owning ttstr and the final
    // remainder is always pushed, including an empty one. A canonical empty
    // ttstr separator is not found, so it produces a one-element result. The
    // original source name is stripped, hence `_guess`.
    inline std::vector<ttstr> splitTtstr_guess(
        ttstr remainder, const ttstr &separator) {
        std::vector<ttstr> pieces;
        for(;;) {
            const int index = remainder.IndexOf(separator, 0);
            if(index < 0) {
                break;
            }
            pieces.push_back(remainder.SubString(
                0, static_cast<unsigned int>(index)));
            remainder = remainder.SubString(
                static_cast<unsigned int>(index) + separator.GetLen(),
                remainder.GetLen() - static_cast<unsigned int>(index) -
                    separator.GetLen());
        }
        pieces.push_back(remainder);
        return pieces;
    }

    inline std::vector<ttstr> splitTtstr_guess(
        ttstr remainder, tjs_char separator) {
        return splitTtstr_guess(
            std::move(remainder), ttstr(separator));
    }

    // Shared four-reference reconstruction of the Motion_propGet* helper
    // family (the Variant-returning form is inlined in Android arm64 at the
    // find-source clip read). Each helper deliberately calls the accessor's
    // retained dispatch with that same dispatch as objthis, forwards flags and
    // the process-wide member-hint pointer, ignores ordinary getter failures,
    // and destroys its result Variant after any requested conversion.
    inline tTJSVariant motionPropGet(const tTJSVariant &holder,
                                     const tjs_char *member,
                                     tjs_uint32 flags = 0,
                                     tjs_uint32 *hint = nullptr) {
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        // The standalone native Variant getter copy-constructs the returned
        // value before destroying the PropGet temporary.
        tTJSVariant result(value);
        return result;
    }

    inline tTJSVariant motionPropGet(iTJSDispatch2 *dispatch,
                                     const tjs_char *member,
                                     tjs_uint32 flags = 0,
                                     tjs_uint32 *hint = nullptr) {
        // The caller's retained accessor owns the receiver across both the
        // script-visible getter and the returned Variant copy.
        tTJSVariant value;
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        tTJSVariant result(value);
        return result;
    }

    inline bool motionTryPropGet(const tTJSVariant &holder,
                                 const tjs_char *member,
                                 tTJSVariant &value,
                                 tjs_uint32 flags = TJS_MEMBERMUSTEXIST,
                                 tjs_uint32 *hint = nullptr) {
        // The strict native helper probes into a temporary and commits the
        // caller's destination only after a successful HRESULT. In particular,
        // a failed or reentrant getter cannot partially replace `value`.
        tTJSVariant probe;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        if(TJS_FAILED(
               dispatch->PropGet(flags, member, hint, &probe, dispatch))) {
            return false;
        }
        // The native Variant-output probe has both this intermediate owner and
        // the PropGet temporary alive while it commits the caller destination.
        tTJSVariant committed(probe);
        value = committed;
        return true;
    }

    inline bool motionTryPropGet(iTJSDispatch2 *dispatch,
                                 const tjs_char *member,
                                 tTJSVariant &value,
                                 tjs_uint32 flags = TJS_MEMBERMUSTEXIST,
                                 tjs_uint32 *hint = nullptr) {
        tTJSVariant probe;
        if(TJS_FAILED(
               dispatch->PropGet(flags, member, hint, &probe, dispatch))) {
            return false;
        }
        tTJSVariant committed(probe);
        value = committed;
        return true;
    }

    inline tTJSVariant motionPropGetByNum(const tTJSVariant &holder,
                                          tjs_int index, tjs_uint32 flags = 0) {
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGetByNum(flags, index, &value, dispatch);
        tTJSVariant result(value);
        return result;
    }

    inline tTJSVariant motionPropGetByNum(iTJSDispatch2 *dispatch,
                                          tjs_int index,
                                          tjs_uint32 flags = 0) {
        tTJSVariant value;
        (void)dispatch->PropGetByNum(flags, index, &value, dispatch);
        tTJSVariant result(value);
        return result;
    }

    inline tjs_real motionPropGetDouble(const tTJSVariant &holder,
                                        const tjs_char *member,
                                        tjs_uint32 flags = 0,
                                        tjs_uint32 *hint = nullptr) {
        // Four-reference named-property read followed by Variant::AsReal.
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        return value.AsReal();
    }

    inline tjs_real motionPropGetDouble(iTJSDispatch2 *dispatch,
                                        const tjs_char *member,
                                        tjs_uint32 flags = 0,
                                        tjs_uint32 *hint = nullptr) {
        tTJSVariant value;
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        return value.AsReal();
    }

    inline tjs_int motionPropGetInt(const tTJSVariant &holder,
                                    const tjs_char *member,
                                    tjs_uint32 flags = 0,
                                    tjs_uint32 *hint = nullptr) {
        // Four-reference named-property read followed by integer conversion.
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        return static_cast<tjs_int>(value.AsInteger());
    }

    inline tjs_int motionPropGetInt(iTJSDispatch2 *dispatch,
                                    const tjs_char *member,
                                    tjs_uint32 flags = 0,
                                    tjs_uint32 *hint = nullptr) {
        tTJSVariant value;
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        return static_cast<tjs_int>(value.AsInteger());
    }

    inline bool motionPropGetBool(const tTJSVariant &holder,
                                  const tjs_char *member, tjs_uint32 flags = 0,
                                  tjs_uint32 *hint = nullptr) {
        // Four-reference named-property read followed by Boolean conversion.
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        return value.operator bool();
    }

    inline bool motionPropGetBool(iTJSDispatch2 *dispatch,
                                  const tjs_char *member,
                                  tjs_uint32 flags = 0,
                                  tjs_uint32 *hint = nullptr) {
        // A caller-owned accessor can keep the receiver alive across a series
        // of re-entrant property reads without reloading a Variant owner.
        tTJSVariant value;
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        return value.operator bool();
    }

    inline ttstr motionPropGetString(const tTJSVariant &holder,
                                     const tjs_char *member,
                                     tjs_uint32 flags = 0,
                                     tjs_uint32 *hint = nullptr) {
        // PropGet is followed by the ordinary Variant-to-ttstr conversion into
        // the caller's destination.
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        ttstr result(value);
        return result;
    }

    inline ttstr motionPropGetString(iTJSDispatch2 *dispatch,
                                     const tjs_char *member,
                                     tjs_uint32 flags = 0,
                                     tjs_uint32 *hint = nullptr) {
        tTJSVariant value;
        (void)dispatch->PropGet(flags, member, hint, &value, dispatch);
        ttstr result(value);
        return result;
    }

    inline tjs_int motionPropGetCount(const tTJSVariant &holder) {
        // All four helpers use PropGet("count"), not GetCount().
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGet(
            0, TJS_W("count"), nullptr, &value, dispatch);
        return static_cast<tjs_int>(value.AsInteger());
    }

    inline tjs_int motionPropGetCount(iTJSDispatch2 *dispatch) {
        // The caller owns the receiver lifetime across this call and any
        // adjacent numeric lookup. PropGet failure remains ignored.
        tTJSVariant value;
        (void)dispatch->PropGet(
            0, TJS_W("count"), nullptr, &value, dispatch);
        return static_cast<tjs_int>(value.AsInteger());
    }

    inline ttstr motionPropGetStringByNum(iTJSDispatch2 *dispatch,
                                          tjs_int index,
                                          tjs_uint32 flags = 0) {
        // Numeric getter followed by direct Variant-to-ttstr conversion. The
        // retained dispatch is both callee and objthis.
        tTJSVariant value;
        (void)dispatch->PropGetByNum(flags, index, &value, dispatch);
        return ttstr(value);
    }

    inline tjs_real motionPropGetDoubleByNum(const tTJSVariant &holder,
                                             tjs_int index,
                                             tjs_uint32 flags = 0) {
        // Numeric property read followed by real conversion.
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGetByNum(flags, index, &value, dispatch);
        return value.AsReal();
    }

    inline tjs_real motionPropGetDoubleByNum(iTJSDispatch2 *dispatch,
                                             tjs_int index,
                                             tjs_uint32 flags = 0) {
        tTJSVariant value;
        (void)dispatch->PropGetByNum(flags, index, &value, dispatch);
        return value.AsReal();
    }

    inline tjs_int motionPropGetIntByNum(const tTJSVariant &holder,
                                         tjs_int index, tjs_uint32 flags = 0) {
        // Numeric property read followed by integer conversion.
        tTJSVariant value;
        iTJSDispatch2 *dispatch = holder.AsObjectNoAddRef();
        (void)dispatch->PropGetByNum(flags, index, &value, dispatch);
        return static_cast<tjs_int>(value.AsInteger());
    }

    inline tjs_int motionPropGetIntByNum(iTJSDispatch2 *dispatch,
                                         tjs_int index,
                                         tjs_uint32 flags = 0) {
        tTJSVariant value;
        (void)dispatch->PropGetByNum(flags, index, &value, dispatch);
        return static_cast<tjs_int>(value.AsInteger());
    }

} // namespace motion::detail
