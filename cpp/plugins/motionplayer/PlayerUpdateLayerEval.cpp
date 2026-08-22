// PlayerUpdateLayerEval.cpp — updateLayers phase1 and phase2 evaluation
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"
#include "MotionDispatch.h"

namespace motion::internal {

    namespace {
        // Diagnostic-only projection built at the logging boundary. It is not
        // part of Player_evaluateTimeline's production data flow and owns no
        // runtime state.
        struct TimelineTraceState {
            bool visible = false;
            bool debugEvaluated = false;
            bool debugFrameAInvisible = false;
            bool debugFrameBInvisible = false;
            bool debugInterpolated = false;
            int frameType = 0;
            int debugActiveIndex = -1;
            int debugNextIndex = -1;
            int debugFrameAType = 0;
            int debugFrameBType = 0;
            double x = 0.0, y = 0.0, z = 0.0;
            double opacity = 1.0;
            double scaleX = 1.0, scaleY = 1.0;
            double debugFrameATime = 0.0, debugFrameBTime = 0.0;
            double debugFrameAOpacity = 1.0, debugFrameBOpacity = 1.0;
            double debugFrameAScaleX = 1.0, debugFrameAScaleY = 1.0;
            double debugFrameBScaleX = 1.0, debugFrameBScaleY = 1.0;
            double debugInterpT = 0.0;
            std::string debugFrameASrc;
            std::string debugFrameBSrc;
        };

        TimelineTraceState traceStateFromClipSlot(
            const detail::MotionNode::ClipSlot &slot,
            bool visible,
            int frameType = 0) {
            TimelineTraceState state;
            state.visible = visible && !slot.done;
            state.frameType = slot.frameIndex >= 0
                ? (slot.done ? 0 : (slot.crossfading ? 3 : 2))
                : frameType;
            state.x = slot.x; state.y = slot.y; state.z = slot.z;
            state.opacity = static_cast<double>(slot.opacity) / 255.0;
            state.scaleX = slot.scaleX; state.scaleY = slot.scaleY;
            return state;
        }

        void copyActiveTimelinePayload_guess(
            detail::MotionNode &node) {
            const auto &slot = node.activeSlot();
            auto &dst = node.accumulated;
            // Keep the common four-target store sequence. All scalar stores
            // precede the potentially-throwing mesh vector assignment below.
            dst.flipX = slot.flipX;
            dst.flipY = slot.flipY;
            dst.angle = slot.angle;
            dst.scaleX = slot.scaleX;
            dst.scaleY = slot.scaleY;
            dst.slantX = slot.slantX;
            dst.slantY = slot.slantY;
            dst.posX = slot.x;
            dst.posY = slot.y;
            dst.posZ = slot.z;
            copyPackedColorsToBytes(node.colorBytes, slot.packedColors);
            dst.opacity = slot.opacity;

            // The active-copy branch copies the slot vector directly into the
            // node output when meshType==1.
            if(node.meshType == 1) {
                node.meshControlPoints = slot.meshControlPoints;
            }
        }

        // Each control point is one source-level {float x,float y} vector
        // element throughout the production interpolation chain.
        void interpolateMeshPoints_guess(
            const tTJSVariant &curve,
            std::vector<detail::MeshPoint> &output,
            const std::vector<detail::MeshPoint> &source,
            const std::vector<detail::MeshPoint> &target,
            double ratio) {
            float meshRatio = static_cast<float>(ratio);
            if(curve.Type() != tvtVoid) {
                meshRatio = static_cast<float>(
                    evaluateVariableTrackEasing_guess(
                        curve, static_cast<double>(meshRatio)));
            }
            if(source.size() != target.size()) {
                TJS_eTJSError(TJS_W("unmatched point list"));
            }

            output.clear();
            if(output.capacity() < source.size()) {
                output.reserve(source.size());
            }
            const float inverseRatio = 1.0f - meshRatio;
            for(size_t i = 0; i < source.size(); ++i) {
                detail::MeshPoint point;
                point.x = inverseRatio * source[i].x
                    + meshRatio * target[i].x;
                point.y = inverseRatio * source[i].y
                    + meshRatio * target[i].y;
                output.push_back(point);
            }
        }

        void writeTimelineTypeSpecificCopy_guess(
            detail::MotionNode &node) {
            const auto &slot = node.activeSlot();
            switch(node.nodeType) {
                case 5:
                    node.cameraFov = slot.cameraFov;
                    break;
                case 10:
                    node.feedbackTimespan = slot.feedbackTimespan;
                    break;
                default:
                    break;
            }
        }

        double interpolateScalarWithCurve_guess(
            double from, double to, const tTJSVariant &curve,
            double ratio) {
            if(from == to) {
                return from;
            }
            if(curve.Type() != tvtVoid) {
                ratio = evaluateVariableTrackEasing_guess(curve, ratio);
            }
            return to * ratio + from * (1.0 - ratio);
        }

        void writeTimelineTypeSpecificLerp_guess(
            detail::MotionNode &node,
            double ratio,
            const tTJSVariant &scalarCurve) {
            const auto &a = node.activeSlot();
            const auto &b = node.otherSlot();
            switch(node.nodeType) {
                case 5:
                    node.cameraFov = interpolateScalarWithCurve_guess(
                        a.cameraFov, b.cameraFov, scalarCurve, ratio);
                    break;
                case 10:
                    node.feedbackTimespan = interpolateScalarWithCurve_guess(
                        a.feedbackTimespan, b.feedbackTimespan,
                        scalarCurve, ratio);
                    break;
                default:
                    break;
            }
        }

        void interpolateTimelineMeshPayload_guess(
            detail::MotionNode &node,
            double ratio) {
            if(node.meshType != 1) {
                return;
            }
            auto &a = node.activeSlot();
            const auto &b = node.otherSlot();
            // Both-empty only clears the already-empty active vector and
            // deliberately leaves the node output vector untouched.
            if(a.meshControlPoints.empty() && b.meshControlPoints.empty()) {
                a.meshControlPoints.clear();
                return;
            }
            const auto &source = a.meshControlPoints.empty()
                ? defaultBezierPatchPoints_guess : a.meshControlPoints;
            const auto &target = b.meshControlPoints.empty()
                ? defaultBezierPatchPoints_guess : b.meshControlPoints;
            interpolateMeshPoints_guess(
                a.meshCurveVariant, node.meshControlPoints,
                source, target, ratio);
        }

        // The nodeType-4 copy and interpolation branches read the same nine-field
        // slot particle block. There is no separate copy-only region; the node
        // mirror is the downstream emitter input.
        void copyParticleInterpolationPayload_guess(detail::MotionNode &node) {
            const detail::MotionNode::ClipSlot &slot = node.activeSlot();
            // Copy the active slot's nine particle scalars into the node-level
            // evaluator output consumed by particle emission.
            const double src[9] = {slot.prtFmin, slot.prtF, slot.prtVmin, slot.prtV,
                                   slot.prtAmin, slot.prtA, slot.prtZmin, slot.prtZ,
                                   slot.prtRange};
            for(int i = 0; i < 9; ++i) {
                node.particleInterp[i] = src[i];
            }
        }

        void interpolateParticlePayload_guess(
            detail::MotionNode &node, double ratio,
            const tTJSVariant &scalarCurve) {
            const auto &a = node.activeSlot();
            const auto &b = node.otherSlot();
            const double srcA[9] = {a.prtFmin, a.prtF, a.prtVmin, a.prtV,
                                    a.prtAmin, a.prtA, a.prtZmin, a.prtZ,
                                    a.prtRange};
            const double srcB[9] = {b.prtFmin, b.prtF, b.prtVmin, b.prtV,
                                    b.prtAmin, b.prtA, b.prtZmin, b.prtZ,
                                    b.prtRange};
            for(int i = 0; i < 9; ++i) {
                node.particleInterp[i] = interpolateScalarWithCurve_guess(
                    srcA[i], srcB[i], scalarCurve, ratio);
            }
        }

        void interpolateTimelinePayload_guess(
            detail::MotionNode &node,
            double ratio) {
            const auto &a = node.activeSlot();
            const auto &b = node.otherSlot();
            auto &dst = node.accumulated;

            dst.flipX = a.flipX;
            dst.flipY = a.flipY;

            double angle = a.angle;
            double otherAngle = b.angle;
            if(angle != otherAngle) {
                if(angle >= otherAngle) {
                    if(angle - otherAngle > 180.0) {
                        otherAngle += 360.0;
                    }
                } else if(otherAngle - angle > 180.0) {
                    otherAngle -= 360.0;
                }
                angle = interpolateScalarWithCurve_guess(
                    angle, otherAngle, a.accVariant, ratio);
                if(angle < 0.0) {
                    angle += 360.0;
                } else if(angle >= 360.0) {
                    angle -= 360.0;
                }
            }
            dst.angle = angle;

            // The native source evaluates the same curve independently for
            // each unequal component. Dispatch count and exception timing are
            // therefore observable and must not be cached across X and Y.
            dst.scaleX = interpolateScalarWithCurve_guess(
                a.scaleX, b.scaleX, a.zccVariant, ratio);
            dst.scaleY = interpolateScalarWithCurve_guess(
                a.scaleY, b.scaleY, a.zccVariant, ratio);
            dst.slantX = interpolateScalarWithCurve_guess(
                a.slantX, b.slantX, a.sccVariant, ratio);
            dst.slantY = interpolateScalarWithCurve_guess(
                a.slantY, b.slantY, a.sccVariant, ratio);

            const double srcPos[3] = {a.x, a.y, a.z};
            const double dstPos[3] = {b.x, b.y, b.z};
            double outPos[3] = {};
            evaluatePositionInterpolation_guess(
                a.cccVariant, dstPos, srcPos, outPos,
                node.coordinateMode, a.cpVariant, ratio);
            dst.posX = outPos[0];
            dst.posY = outPos[1];
            dst.posZ = outPos[2];

            // All four targets construct this default-Void Variant after the
            // position call and keep it alive through opacity, mesh and the
            // type-specific switch. Its easing branches are source-real but
            // unreachable with the recovered initializer.
            const tTJSVariant scalarCurve;
            std::array<std::uint32_t, 4> colors{};
            for(std::size_t i = 0; i < colors.size(); ++i) {
                // The shipped source passes the active color as both endpoints.
                // Equality returns before cccVariant is inspected.
                colors[i] = interpolatePackedColor_guess(
                    a.cccVariant, a.packedColors[i], a.packedColors[i], ratio);
            }
            copyPackedColorsToBytes(node.colorBytes, colors);

            const double opacity = interpolateScalarWithCurve_guess(
                static_cast<double>(static_cast<std::uint32_t>(a.opacity)),
                static_cast<double>(static_cast<std::uint32_t>(b.opacity)),
                scalarCurve, ratio);
            const std::uint32_t opacityWord =
                timelineOpacityWordFromDouble_guess(opacity);
            static_assert(sizeof(dst.opacity) == sizeof(opacityWord));
            std::memcpy(&dst.opacity, &opacityWord, sizeof(opacityWord));

            interpolateTimelineMeshPayload_guess(node, ratio);
            if(node.nodeType == 4) {
                interpolateParticlePayload_guess(
                    node, ratio, scalarCurve);
            }
            writeTimelineTypeSpecificLerp_guess(
                node, ratio, scalarCurve);
        }

        // Dirty publishes only from an actual frame-crossing iteration or
        // absolute initialization, then the update pass consumes and clears it.

        // The four-reference slot reset deliberately does not assign a fresh
        // C++ object: it releases only the owned strings/Variants, zeros the
        // scalar prefix, and clears the mesh vector while retaining capacity.
        void resetClipSlot_guess(
            detail::MotionNode::ClipSlot &slot) {
            slot.frameIndex = 0;
            slot.clipStartTime = 0.0;
            slot.ti = 0;
            slot.contentMask = 0;
            slot.done = false;
            slot.crossfading = false;
            slot.merged = false;
            slot.srcValue.Clear();
            slot.blendMode = 0;
            slot.opacity = 0;
            slot.ox = 0.0;
            slot.oy = 0.0;
            slot.x = 0.0;
            slot.y = 0.0;
            slot.z = 0.0;
            slot.packedColors = {0u, 0u, 0u, 0u};
            slot.flipX = false;
            slot.flipY = false;
            slot.angle = 0.0;
            slot.scaleX = 0.0;
            slot.scaleY = 0.0;
            slot.slantX = 0.0;
            slot.slantY = 0.0;
            slot.cccVariant.Clear();
            slot.occVariant.Clear();
            slot.accVariant.Clear();
            slot.zccVariant.Clear();
            slot.sccVariant.Clear();
            slot.cpVariant.Clear();
            slot.meshCurveVariant.Clear();
            // The native reset intentionally preserves icon and action owners.
            // Their masks/type gates prevent stale values from being consumed;
            // later present fields overwrite and release them in place.
            slot.meshControlPoints.clear();
            // The two scalar words immediately after the native mesh vector
            // are also reset. Other motion-block fields deliberately retain
            // their previous bytes until a frame carrying the motion mask
            // initializes that block.
            slot.motionFlags = 0;
            slot.motionDt = 0;
        }

        void copyRawCurveVariant(iTJSDispatch2 *content,
                                 const tjs_char *name,
                                 tjs_uint32 *hint,
                                 tTJSVariant &owner) {
            owner = detail::motionPropGet(content, name, 0, hint);
        }

    }

    // Merge the content payload for an already parsed slot. The native routine
    // receives only the node-type byte plus the raw frame list; it does not
    // receive or retain a MotionNode pointer. Root, frame and content accessors
    // remain live across every selected nested payload block.
    void mergeNodeFrameContent_guess(
        detail::MotionNode::ClipSlot &slot,
        int nodeType,
        const tTJSVariant &frameList) {
            slot.merged = true;
            if(slot.done) {
                return;
            }

            ncbPropAccessor frameListObject{tTJSVariant(frameList)};
            ncbPropAccessor frameObject{frameListObject.GetValue(
                slot.frameIndex, ncbTypedefs::Tag<tTJSVariant>(), 0)};
            ncbPropAccessor contentObject{frameObject.GetValue(
                TJS_W("content"), ncbTypedefs::Tag<tTJSVariant>(), 0,
                &detail::contentMemberHint_guess)};
            iTJSDispatch2 *const content = contentObject.GetDispatch();
            const std::uint32_t mask =
                static_cast<std::uint32_t>(slot.contentMask);

            slot.packedColors = {0xFF808080u, 0xFF808080u,
                                 0xFF808080u, 0xFF808080u};
            slot.scaleX = 1.0;
            slot.scaleY = 1.0;
            slot.opacity = 255;
            slot.blendMode = 16;

            if(nodeType >= 0 && nodeType < 32 &&
               (((1u << static_cast<unsigned>(nodeType)) & 0x1849u) != 0)) {
                slot.srcValue =
                    detail::motionPropGetString(
                        content, TJS_W("src"), 0,
                        &detail::srcMemberHint_guess);
                // Four-reference behavior is deliberately two-phase. First
                // probe `icon` with MEMBERMUSTEXIST into a temporary Variant;
                // on success, discard it and read the property again with
                // flags=0 for string conversion. A reentrant getter can
                // therefore return a different value on the second read.
                tTJSVariant probe;
                if(detail::motionTryPropGet(
                       content, TJS_W("icon"), probe,
                       TJS_MEMBERMUSTEXIST, nullptr)) {
                    slot.iconValue =
                        detail::motionPropGetString(
                            content, TJS_W("icon"), 0, nullptr);
                } else {
                    slot.iconValue.Clear();
                }
            }

            if((mask & 0x1u) != 0) {
                slot.ox = detail::motionPropGetDouble(
                    content, TJS_W("ox"), 0,
                    &detail::nodeFrameOxMemberHint_guess);
                slot.oy = detail::motionPropGetDouble(
                    content, TJS_W("oy"), 0,
                    &detail::nodeFrameOyMemberHint_guess);
            }
            if((mask & 0x2u) != 0) {
                ncbPropAccessor coordObject{
                    detail::motionPropGet(
                        content, TJS_W("coord"), 0,
                        &detail::coordMemberHint_guess)};
                iTJSDispatch2 *const coord = coordObject.GetDispatch();
                slot.x = detail::motionPropGetDoubleByNum(coord, 0);
                slot.y = detail::motionPropGetDoubleByNum(coord, 1);
                slot.z = detail::motionPropGetDoubleByNum(coord, 2);
            }

            if((mask & 0x20600u) != 0) {
                if((mask & 0x20000u) != 0) {
                    slot.blendMode =
                        detail::motionPropGetInt(
                            content, TJS_W("bm"), 0,
                            &detail::nodeFrameBmMemberHint_guess);
                }
                if((mask & 0x200u) != 0) {
                    const tTJSVariant color =
                        detail::motionPropGet(
                            content, TJS_W("color"), 0,
                            &detail::colorMemberHint_guess);
                    if(color.Type() == tvtObject) {
                        ncbPropAccessor colorObject{color};
                        iTJSDispatch2 *const colorDispatch =
                            colorObject.GetDispatch();
                        for(int i = 0; i < 4; ++i) {
                            slot.packedColors[static_cast<std::size_t>(i)] =
                                static_cast<std::uint32_t>(
                                    detail::motionPropGetIntByNum(
                                        colorDispatch, i));
                        }
                    } else {
                        std::uint32_t packed = 0;
                        switch(color.Type()) {
                            case tvtString:
                            case tvtOctet:
                            case tvtInteger:
                            case tvtReal:
                                packed = static_cast<std::uint32_t>(
                                    color.AsInteger());
                                break;
                            default:
                                break;
                        }
                        slot.packedColors = {packed, packed, packed, packed};
                    }
                } else if((slot.blendMode & 0xF0) == 0) {
                    slot.packedColors = {0xFFFFFFFFu, 0xFFFFFFFFu,
                                         0xFFFFFFFFu, 0xFFFFFFFFu};
                }
                if((mask & 0x400u) != 0) {
                    slot.opacity =
                        detail::motionPropGetInt(
                            content, TJS_W("opa"), 0,
                            &detail::nodeFrameOpaMemberHint_guess);
                }
            }

            if((mask & 0x1FCu) != 0) {
                if((mask & 0xCu) != 0) {
                    slot.flipX =
                        detail::motionPropGetBool(
                            content, TJS_W("fx"), 0,
                            &detail::nodeFrameFxMemberHint_guess);
                    slot.flipY =
                        detail::motionPropGetBool(
                            content, TJS_W("fy"), 0,
                            &detail::nodeFrameFyMemberHint_guess);
                }
                if((mask & 0x10u) != 0) {
                    slot.angle =
                        detail::motionPropGetDouble(
                            content, TJS_W("angle"), 0,
                            &detail::angleMemberHint_guess);
                }
                if((mask & 0x60u) != 0) {
                    slot.scaleX =
                        detail::motionPropGetDouble(
                            content, TJS_W("zx"), 0,
                            &detail::nodeFrameZxMemberHint_guess);
                    slot.scaleY =
                        detail::motionPropGetDouble(
                            content, TJS_W("zy"), 0,
                            &detail::nodeFrameZyMemberHint_guess);
                }
                if((mask & 0x180u) != 0) {
                    slot.slantX =
                        detail::motionPropGetDouble(
                            content, TJS_W("sx"), 0,
                            &detail::nodeFrameSxMemberHint_guess);
                    slot.slantY =
                        detail::motionPropGetDouble(
                            content, TJS_W("sy"), 0,
                            &detail::nodeFrameSyMemberHint_guess);
                }
            }

            if(slot.crossfading) {
                if((mask & 0x04000000u) != 0) {
                    slot.ti = static_cast<std::uint32_t>(
                        detail::motionPropGetInt(
                            content, TJS_W("ti"), 0,
                            &detail::nodeFrameTiMemberHint_guess));
                }
                if((mask & 0x800u) != 0) {
                    copyRawCurveVariant(content, TJS_W("ccc"),
                                        &detail::nodeFrameCccMemberHint_guess,
                                        slot.cccVariant);
                }
                if((mask & 0x8000u) != 0) {
                    copyRawCurveVariant(content, TJS_W("occ"),
                                        &detail::nodeFrameOccMemberHint_guess,
                                        slot.occVariant);
                }
                if((mask & 0x1000u) != 0) {
                    copyRawCurveVariant(content, TJS_W("acc"),
                                        &detail::nodeFrameAccMemberHint_guess,
                                        slot.accVariant);
                }
                if((mask & 0x2000u) != 0) {
                    copyRawCurveVariant(content, TJS_W("zcc"),
                                        &detail::nodeFrameZccMemberHint_guess,
                                        slot.zccVariant);
                }
                if((mask & 0x4000u) != 0) {
                    copyRawCurveVariant(content, TJS_W("scc"),
                                        &detail::nodeFrameSccMemberHint_guess,
                                        slot.sccVariant);
                }
                if((mask & 0x10000u) != 0) {
                    slot.cpVariant =
                        detail::motionPropGet(
                            content, TJS_W("cp"), 0,
                            &detail::nodeFrameCpMemberHint_guess);
                }
            }

            if((mask & 0x02000000u) != 0) {
                tTJSVariant mesh =
                    detail::motionPropGet(
                        content, TJS_W("mesh"), 0,
                        &detail::meshMemberHint_guess);
                if(mesh.Type() == tvtVoid) {
                    mesh = detail::motionPropGet(
                        content, TJS_W("obj"), 0,
                        &detail::nodeFrameObjMemberHint_guess);
                }
                ncbPropAccessor meshObject{mesh};
                iTJSDispatch2 *const meshDispatch =
                    meshObject.GetDispatch();
                slot.meshCurveVariant =
                    detail::motionPropGet(
                        meshDispatch, TJS_W("cc"), 0,
                        &detail::nodeFrameCcMemberHint_guess);
                if(slot.meshCurveVariant.Type() == tvtVoid) {
                    slot.meshCurveVariant =
                        detail::motionPropGet(
                            meshDispatch, TJS_W("mcc"), 0,
                            &detail::nodeFrameMccMemberHint_guess);
                }
                tTJSVariant points =
                    detail::motionPropGet(
                        meshDispatch, TJS_W("bp"), 0,
                        &detail::nodeFrameBpMemberHint_guess);
                if(points.Type() == tvtVoid) {
                    points = detail::motionPropGet(
                        meshDispatch, TJS_W("bezierPatch"), 0,
                        &detail::bezierPatchMemberHint_guess);
                }
                if(points.Type() == tvtObject) {
                    ncbPropAccessor pointsObject{points};
                    iTJSDispatch2 *const pointsDispatch =
                        pointsObject.GetDispatch();
                    if(detail::motionPropGetCount(pointsDispatch) != 32) {
                        TJS_eTJSError(
                            TJS_W("unexpected bezier patch point num."));
                    }
                    slot.meshControlPoints.reserve(16);
                    for(int i = 0; i < 32; i += 2) {
                        slot.meshControlPoints.push_back({
                            static_cast<float>(
                                detail::motionPropGetDoubleByNum(
                                    pointsDispatch, i)),
                            static_cast<float>(
                                detail::motionPropGetDoubleByNum(
                                    pointsDispatch, i + 1))
                        });
                    }
                }
            }

            if((mask & 0x80000u) != 0) {
                ncbPropAccessor motionObject{
                    detail::motionPropGet(
                        content, TJS_W("motion"), 0,
                        &detail::motionMemberHint_guess)};
                iTJSDispatch2 *const motion = motionObject.GetDispatch();
                const int motionMask =
                    detail::motionPropGetInt(
                        motion, TJS_W("mask"), 0,
                        &detail::maskMemberHint_guess);
                slot.motionFlags = 0;
                slot.motionDt = 1;
                slot.motionDocmpl = false;
                slot.motionDofst = 0.0;
                slot.motionDtgtValue.Clear();
                if((motionMask & 0x1) != 0) {
                    slot.motionFlags =
                        detail::motionPropGetInt(
                            motion, TJS_W("flags"), 0,
                            &detail::nodeFrameFlagsMemberHint_guess);
                }
                if((motionMask & 0x2) != 0) {
                    slot.motionDt =
                        detail::motionPropGetInt(
                            motion, TJS_W("dt"), 0,
                            &detail::nodeFrameDtMemberHint_guess);
                }
                if((motionMask & 0x4) != 0) {
                    slot.motionDocmpl =
                        detail::motionPropGetBool(
                            motion, TJS_W("docmpl"), 0,
                            &detail::nodeFrameDocmplMemberHint_guess);
                }
                if((motionMask & 0x8) != 0) {
                    slot.motionDofst =
                        detail::motionPropGetDouble(
                            motion, TJS_W("dofst"), 0,
                            &detail::nodeFrameDofstMemberHint_guess);
                }
                if((motionMask & 0x10) != 0) {
                    slot.motionDtgtValue =
                        detail::motionPropGetString(
                            motion, TJS_W("dtgt"), 0,
                            &detail::nodeFrameDtgtMemberHint_guess);
                }
                slot.motionTimeOffset =
                    detail::motionPropGetDouble(
                        motion, TJS_W("timeOffset"), 0,
                        &detail::nodeFrameTimeOffsetMemberHint_guess);
            }

            if((mask & 0x01000000u) != 0) {
                ncbPropAccessor modelObject{
                    detail::motionPropGet(
                        content, TJS_W("model"), 0,
                        &detail::nodeFrameModelMemberHint_guess)};
                iTJSDispatch2 *const model = modelObject.GetDispatch();
                slot.modelTimeOffset =
                    detail::motionPropGetDouble(
                        model, TJS_W("timeOffset"), 0,
                        &detail::nodeFrameTimeOffsetMemberHint_guess);
                slot.modelLoop =
                    detail::motionPropGetBool(
                        model, TJS_W("loop"), 0,
                        &detail::nodeFrameLoopMemberHint_guess);
                slot.modelDt =
                    detail::motionPropGetInt(
                        model, TJS_W("dt"), 0,
                        &detail::nodeFrameDtMemberHint_guess);
                slot.modelDtgt =
                    detail::motionPropGetString(
                        model, TJS_W("dtgt"), 0,
                        &detail::nodeFrameDtgtMemberHint_guess);
            }

            if((mask & 0x100000u) != 0) {
                ncbPropAccessor particleObject{
                    detail::motionPropGet(
                        content, TJS_W("prt"), 0,
                        &detail::nodeFramePrtMemberHint_guess)};
                iTJSDispatch2 *const particle = particleObject.GetDispatch();
                const int particleMask =
                    detail::motionPropGetInt(
                        particle, TJS_W("mask"), 0,
                        &detail::maskMemberHint_guess);
                slot.prtTrigger = 0;
                slot.prtFmin = 10.0;
                slot.prtF = 10.0;
                slot.prtVmin = 0.0;
                slot.prtV = 0.0;
                slot.prtAmin = 0.0;
                slot.prtA = 0.0;
                slot.prtZmin = 1.0;
                slot.prtZ = 1.0;
                slot.prtRange = 0.0;
                if((particleMask & 0x1) != 0) {
                    slot.prtTrigger =
                        detail::motionPropGetInt(
                            particle, TJS_W("trigger"), 0,
                            &detail::nodeFrameTriggerMemberHint_guess);
                }
                if((particleMask & 0x2) != 0) {
                    slot.prtFmin =
                        detail::motionPropGetDouble(
                            particle, TJS_W("fmin"), 0,
                            &detail::nodeFrameFminMemberHint_guess);
                    slot.prtF =
                        detail::motionPropGetDouble(
                            particle, TJS_W("fmax"), 0,
                            &detail::nodeFrameFmaxMemberHint_guess);
                }
                if((particleMask & 0x4) != 0) {
                    slot.prtVmin =
                        detail::motionPropGetDouble(
                            particle, TJS_W("vmin"), 0,
                            &detail::nodeFrameVminMemberHint_guess);
                    slot.prtV =
                        detail::motionPropGetDouble(
                            particle, TJS_W("vmax"), 0,
                            &detail::nodeFrameVmaxMemberHint_guess);
                }
                if((particleMask & 0x8) != 0) {
                    slot.prtAmin =
                        detail::motionPropGetDouble(
                            particle, TJS_W("amin"), 0,
                            &detail::nodeFrameAminMemberHint_guess);
                    slot.prtA =
                        detail::motionPropGetDouble(
                            particle, TJS_W("amax"), 0,
                            &detail::nodeFrameAmaxMemberHint_guess);
                }
                if((particleMask & 0x10) != 0) {
                    slot.prtZmin =
                        detail::motionPropGetDouble(
                            particle, TJS_W("zmin"), 0,
                            &detail::nodeFrameZminMemberHint_guess);
                    slot.prtZ =
                        detail::motionPropGetDouble(
                            particle, TJS_W("zmax"), 0,
                            &detail::nodeFrameZmaxMemberHint_guess);
                }
                if((particleMask & 0x20) != 0) {
                    slot.prtRange =
                        detail::motionPropGetDouble(
                            particle, TJS_W("range"), 0,
                            &detail::nodeFrameRangeMemberHint_guess);
                }
            }

            if((mask & 0x200000u) != 0) {
                ncbPropAccessor cameraObject{
                    detail::motionPropGet(
                        content, TJS_W("camera"), 0,
                        &detail::nodeFrameCameraMemberHint_guess)};
                iTJSDispatch2 *const camera = cameraObject.GetDispatch();
                slot.cameraFov =
                    detail::motionPropGetDouble(
                        camera, TJS_W("fov"), 0,
                        &detail::nodeFrameFovMemberHint_guess);
                slot.cameraTarget =
                    detail::motionPropGetString(
                        camera, TJS_W("target"), 0,
                        &detail::nodeFrameTargetMemberHint_guess);
            }
            if((mask & 0x800000u) != 0) {
                ncbPropAccessor anchorObject{
                    detail::motionPropGet(
                        content, TJS_W("anchor"), 0,
                        &detail::nodeFrameAnchorMemberHint_guess)};
                iTJSDispatch2 *const anchor = anchorObject.GetDispatch();
                slot.anchorTarget =
                    detail::motionPropGetString(
                        anchor, TJS_W("target"), 0,
                        &detail::nodeFrameTargetMemberHint_guess);
            }
            if((mask & 0x08000000u) != 0) {
                ncbPropAccessor feedbackObject{
                    detail::motionPropGet(
                        content, TJS_W("feedback"), 0,
                        &detail::nodeFrameFeedbackMemberHint_guess)};
                iTJSDispatch2 *const feedback =
                    feedbackObject.GetDispatch();
                slot.feedbackTimespan =
                    detail::motionPropGetDouble(
                        feedback, TJS_W("timespan"), 0,
                        &detail::nodeFrameTimespanMemberHint_guess);
            }

    }

    namespace {

        int signedIndexFromBits32_guess(std::uint32_t bits) noexcept {
            static_assert(sizeof(int) == sizeof(std::uint32_t),
                          "motion timeline indices require a 32-bit int");
            std::int32_t signedBits = 0;
            std::memcpy(&signedBits, &bits, sizeof(signedBits));
            return static_cast<int>(signedBits);
        }

        int subtractTwoWrapping32_guess(int value) noexcept {
            return signedIndexFromBits32_guess(
                static_cast<std::uint32_t>(value) - UINT32_C(2));
        }

        int incrementWrapping32_guess(int value) noexcept {
            return signedIndexFromBits32_guess(
                static_cast<std::uint32_t>(value) + UINT32_C(1));
        }

        int decrementWrapping32_guess(int value) noexcept {
            return signedIndexFromBits32_guess(
                static_cast<std::uint32_t>(value) - UINT32_C(1));
        }

        int initialNodeFrameIndexForTime_guess(
            const tTJSVariant &frames,
            double currentTime) {
            const int count = detail::motionPropGetCount(frames);
            int selected = 0;
            if(count >= 1) {
                int index = 0;
                for(;;) {
                    const tTJSVariant frame =
                        detail::motionPropGetByNum(frames, index);
                    const double time =
                        detail::motionPropGetDouble(frame, TJS_W("time"));
                    if(currentTime == time) {
                        selected = index;
                        break;
                    }
                    if(currentTime < time) {
                        selected = index - 1;
                        break;
                    }
                    ++index;
                    if(index >= count) {
                        break;
                    }
                }
            }
            const int upper = subtractTwoWrapping32_guess(count);
            return selected <= upper ? selected : upper;
        }

        double nodeFrameSelectionTime_guess(
            const detail::MotionNode &node,
            double currentTime) {
            // Parameterized nodes select by their bound parameter value;
            // ordinary nodes select by the Player timeline time.
            if(node.parameterEntry != nullptr) {
                return node.parameterEntry->value;
            }
            return currentTime;
        }

        TimelineTraceState traceStateFromNodeSlots(
            const detail::MotionNode &node,
            double currentTime) {
            const auto &active = node.activeSlot();
            const auto &other = node.otherSlot();
            TimelineTraceState state =
                traceStateFromClipSlot(active, !active.done);
            state.debugEvaluated = active.frameIndex >= 0;
            state.debugActiveIndex = active.frameIndex;
            state.debugFrameATime = active.clipStartTime;
            state.debugFrameAType =
                active.done ? 0 : (active.crossfading ? 3 : 2);
            state.debugFrameAInvisible = active.done;
            state.debugFrameAOpacity =
                static_cast<double>(active.opacity) / 255.0;
            state.debugFrameAScaleX = active.scaleX;
            state.debugFrameAScaleY = active.scaleY;
            state.debugFrameASrc = detail::narrow(active.srcValue);
            if(other.frameIndex >= 0) {
                state.debugNextIndex = other.frameIndex;
                state.debugFrameBTime = other.clipStartTime;
                state.debugFrameBType =
                    other.done ? 0 : (other.crossfading ? 3 : 2);
                state.debugFrameBInvisible = other.done;
                state.debugFrameBOpacity =
                    static_cast<double>(other.opacity) / 255.0;
                state.debugFrameBScaleX = other.scaleX;
                state.debugFrameBScaleY = other.scaleY;
                state.debugFrameBSrc = detail::narrow(other.srcValue);
            }

            if(active.crossfading && other.frameIndex >= 0) {
                const double duration = other.clipStartTime - active.clipStartTime;
                if(duration > 0.0) {
                    state.debugInterpT = std::clamp(
                        (currentTime - active.clipStartTime) / duration,
                        0.0, 1.0);
                    state.debugInterpolated =
                        state.debugInterpT > 0.0 && !other.done;
                }
            }
            return state;
        }
    }

    // Shared two-slot seek primitive retained for the independently callable
    // parameterized-node helper. Ordinary nodes have different owner, live-time,
    // action and dirty-commit boundaries and are kept in the two native-shaped
    // incremental loops below instead of passing through this abstraction.
    MOTIONPLAYER_NOINLINE bool
    seekNodeFrameSelection_guess(
        detail::MotionNode &node, double currentTime,
        Player *eventOwner,
        bool doForward, bool doBackward) {
        // Each crossed action frame queues onAction(label, action). The first
        // argument is the node's raw PSB label, not its layer dispatch object;
        // the second is the action string owned by the crossed clip slot.
        const auto fireNodeAction =
            [&](const detail::MotionNode::ClipSlot &slot) {
            if(eventOwner && (slot.contentMask & 0x40000) != 0) {
                eventOwner->enqueueActionEvent_guess(
                    tTJSVariant(node.layerName), slot.actionValue);
            }
        };

        const int frameCount =
            detail::motionPropGetCount(node.frameListVariant);
        const double selectionTime =
            nodeFrameSelectionTime_guess(node, currentTime);
        bool seeked = false;
        const int lastForwardFrameIndex = frameCount - 2;
        while(doForward &&
              node.activeSlot().frameIndex < lastForwardFrameIndex &&
              selectionTime >= node.otherSlot().clipStartTime) {
            // Forward traversal fires the crossed `other` slot before swapping.
            fireNodeAction(node.otherSlot());
            node.activeSlotIndex ^= 1;
            const int nextIndex = node.activeSlot().frameIndex + 1;
            parseNodeFrame_guess(
                node.otherSlot(), node.frameListVariant, nextIndex);
            seeked = true;
            node.flags |= 0x01;
        }

        while(doBackward &&
              selectionTime < node.activeSlot().clipStartTime) {
            const int previousIndex = node.activeSlot().frameIndex - 1;
            node.activeSlotIndex ^= 1;
            parseNodeFrame_guess(
                node.activeSlot(), node.frameListVariant, previousIndex);
            // Reverse traversal fires the just-entered previous slot.
            fireNodeAction(node.activeSlot());
            seeked = true;
            node.flags |= 0x01;
        }

        // All three native paths merge only slots invalidated by parsing. A tick
        // that crosses no frame performs no merge work.
        if(seeked) {
            if(!node.slots[0].merged) {
                mergeNodeFrameContent_guess(
                    node.slots[0], node.nodeType, node.frameListVariant);
            }
            if(!node.slots[1].merged) {
                mergeNodeFrameContent_guess(
                    node.slots[1], node.nodeType, node.frameListVariant);
            }
        }

        if(node.activeSlot().frameIndex < 0) {
            return seeked;
        }
        // Do not synthesize dirty state from a port-local payload cache here.
        // Native code sets the node dirty bit only in iterations that actually
        // cross a frame; updateLayers clears it after consumption. Unconditional
        // dirtying prevents static child Players from settling and can repeatedly
        // replay a zero-delta child motion.
        return seeked;
    }

    // Parse one raw frame-list entry into a clip slot without merging the
    // referenced content payload. The retained source accessor outlives the
    // indexed getter, and the indexed result directly backs the frame
    // accessor. Nonzero frames add a content accessor; normal teardown is
    // content, frame, then frame-list root.
    void parseNodeFrame_guess(
        detail::MotionNode::ClipSlot &slot,
        const tTJSVariant &frameList,
        int frameIndex) {
        resetClipSlot_guess(slot);
        slot.frameIndex = frameIndex;

        ncbPropAccessor frameListObject{tTJSVariant(frameList)};
        ncbPropAccessor frameObject{frameListObject.GetValue(
            frameIndex, ncbTypedefs::Tag<tTJSVariant>(), 0)};
        slot.clipStartTime = frameObject.GetValue(
            TJS_W("time"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::timeMemberHint_guess);
        const tjs_int type = frameObject.GetValue(
            TJS_W("type"), ncbTypedefs::Tag<tjs_int>(), 0,
            &detail::typeMemberHint_guess);
        if(type == 0) {
            slot.done = true;
            return;
        }

        slot.done = false;
        if(type == 2) {
            slot.crossfading = false;
        } else if(type == 3) {
            slot.crossfading = true;
        }

        ncbPropAccessor contentObject{frameObject.GetValue(
            TJS_W("content"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::contentMemberHint_guess)};
        slot.contentMask = contentObject.GetValue(
            TJS_W("mask"), ncbTypedefs::Tag<tjs_int>(), 0,
            &detail::maskMemberHint_guess);
        if((slot.contentMask & 0x40000) != 0) {
            slot.actionValue = contentObject.GetValue(
                TJS_W("act"), ncbTypedefs::Tag<ttstr>(), 0,
                &detail::actMemberHint_guess);
        }
    }

    // Absolute initializer shared by full reseek and dirty emoteEdit rebuilds.
    // Its Player-first signature and complete tail match the independently
    // callable helper present in all four reference binaries.
    MOTIONPLAYER_NOINLINE void
    initializeNodeTimelineSlots_guess(Player &player,
                                      detail::MotionNode &node) {
        const double selectionTime =
            nodeFrameSelectionTime_guess(node, player._clampedEvalTime);
        // Native retains the frame-list dispatch before dynamic count and
        // keeps that owner through scan, slot rebuild, source refresh and the
        // exact-frame action tail. Parse/merge still re-read the persistent
        // node field, so a re-entrant clear remains observable there.
        const tTJSVariant frameListOwner = node.frameListVariant;
        const int activeIndex = initialNodeFrameIndexForTime_guess(
            frameListOwner, selectionTime);
        parseNodeFrame_guess(
            node.slots[0], node.frameListVariant, activeIndex);
        mergeNodeFrameContent_guess(
            node.slots[0], node.nodeType, node.frameListVariant);
        parseNodeFrame_guess(
            node.slots[1], node.frameListVariant, activeIndex + 1);
        mergeNodeFrameContent_guess(
            node.slots[1], node.nodeType, node.frameListVariant);
        // Both slot rebuilds publish before the active/dirty commits. A parser
        // or merger exception therefore preserves the prior cursor and flags.
        node.activeSlotIndex = 0;
        node.flags |= 0x01;

        const int sourceMask = player._preview ? 6153 : 6145;
        if(node.forceVisible != 0 ||
           ((1 << node.nodeType) & sourceMask) != 0) {
            player.findSourceForNode_guess(node);
        }

        // The action test follows source lookup and observes only slot 0.
        if(selectionTime == node.slots[0].clipStartTime &&
           (node.slots[0].contentMask & 0x40000) != 0) {
            player.enqueueActionEvent_guess(
                tTJSVariant(node.layerName), node.slots[0].actionValue);
        }
    }

    // Shared two-slot ping-pong seek for parameterized nodes. The node's
    // parameter pointer refers to a parameter-table entry, not a child Player;
    // its eased value is therefore the seek target. The native path performs a
    // forward pass plus corrective backward pass, merges invalidated slots and
    // emits no per-node onAction events. Raw frame indices intentionally rely on
    // the same timeline-data invariant as the native PropGetByNum calls.
    MOTIONPLAYER_NOINLINE void
    seekParameterizedNodeFrames_guess(detail::MotionNode &node,
                                      Player &player) {
        // The shared helper reads parameterEntry->value as its selection time.
        // A null event owner preserves the native parameterized-path behavior.
        const bool seeked = seekNodeFrameSelection_guess(
            node, 0.0, nullptr);
        if(!seeked) {
            return;
        }

        const int mask = player._preview ? 6153 : 6145;
        if(node.forceVisible != 0 ||
           ((1 << node.nodeType) & mask) != 0) {
            player.findSourceForNode_guess(node);
        }
    }

    // Diagnostic-only projection of the slots already positioned by progress.
    // It uses the production selection-time rule so trace interpolation labels
    // match the evaluator, but it is not a recovered native helper and must
    // never run when the Web trace sidecar is disabled.
    MOTIONPLAYER_NOINLINE TimelineTraceState
    readNodeFrameSlotsForTrace(detail::MotionNode &node,
                               double currentTime) {
        const double selectionTime =
            nodeFrameSelectionTime_guess(node, currentTime);
        return traceStateFromNodeSlots(node, selectionTime);
    }

    std::uint32_t doubleToUnsignedIntTowardZeroSaturated_guess(
        double value) {
        constexpr double kTwoToThe32 = 4294967296.0;
        if(std::isnan(value) || value <= 0.0) {
            return 0;
        }
        if(value >= kTwoToThe32) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        return static_cast<std::uint32_t>(value);
    }

    MOTIONPLAYER_NOINLINE bool
    evaluateTimeline_guess(detail::MotionNode &node,
                           double currentTime,
                           bool dirtyArg) {
        const bool dirty = dirtyArg || node.flags != 0;
        auto &active = node.activeSlot();
        auto &other = node.otherSlot();

        if(active.done) {
            return dirty;
        }

        if(!active.crossfading || other.done) {
            if(!dirty) {
                return false;
            }
            copyActiveTimelinePayload_guess(node);
            // Type 4 additionally publishes the active slot's particle block
            // into the node-level interpolation output.
            if(node.nodeType == 4) {
                copyParticleInterpolationPayload_guess(node);
            }
            writeTimelineTypeSpecificCopy_guess(node);
            return true;
        }

        if(node.parameterEntry != nullptr) {
            currentTime = node.parameterEntry->value;
        }

        double elapsed = currentTime - active.clipStartTime;
        if(active.ti != 0) {
            const std::uint32_t stepCount =
                doubleToUnsignedIntTowardZeroSaturated_guess(
                    elapsed / static_cast<double>(active.ti));
            // All four references multiply in a 32-bit W/core register before
            // converting the wrapped unsigned product back to double.
            const std::uint32_t quantizedElapsed = active.ti * stepCount;
            elapsed = static_cast<double>(quantizedElapsed);
        }
        const double ratio = elapsed /
            (other.clipStartTime - active.clipStartTime);
        const double oldRatio = node.timelineEvalRatio;

        constexpr double kNearZeroRatio = 1.0e-7;
        // All four Arm references branch to interpolation only on an ordered
        // abs(ratio) >= threshold comparison. An unordered NaN therefore falls
        // through to this active-copy branch; spelling the inverse as `<` would
        // incorrectly send NaN to interpolation in portable C++.
        const bool ratioAtOrBeyondNearZeroThreshold =
            std::fabs(ratio) >= kNearZeroRatio;
        if(!ratioAtOrBeyondNearZeroThreshold) {
            node.timelineEvalRatio = ratio;
            const bool changedEnough =
                std::fabs(oldRatio - ratio) >= kNearZeroRatio;
            // The native ordered-GE result is ORed with dirty. For an unordered
            // NaN difference the result is false, so a clean node returns after
            // storing the new (possibly NaN) ratio and leaves payload untouched.
            if(!dirty && !changedEnough) {
                return false;
            }
            copyActiveTimelinePayload_guess(node);
            if(node.nodeType == 4) {
                copyParticleInterpolationPayload_guess(node);
            }
            writeTimelineTypeSpecificCopy_guess(node);
            return true;
        }

        const bool changedEnough =
            std::fabs(oldRatio - ratio) >=
            std::numeric_limits<double>::epsilon();
        // This is likewise an ordered-GE gate. An unordered difference on the
        // normal-ratio side returns without writing ratio or payload.
        if(!dirty && !changedEnough) {
            return false;
        }
        node.timelineEvalRatio = ratio;
        interpolateTimelinePayload_guess(node, ratio);
        return true;
    }
}

namespace motion {
    // Extracted node phase of the native progress-pass cursor machine. The two
    // four-stream functions inline this walk after their layer, root and variable
    // phases. It starts at deque index 1: the root belongs to the root stream,
    // while every non-root node fills its two live ClipSlots here for the later
    // read-only updateLayers pass. Parameterized nodes select by their eased
    // parameter value; ordinary nodes select by Player's live evaluation field.
    // First-frame and loop-wrap paths seed the slots absolutely before entering
    // this phase, so no port-local lazy initialization is needed.
    void Player::seekNodeTimelineSlotsIncrementalPhase_guess(bool forward) {
        // The half-open range is [1, nodes.size()). There is no trailing sentinel:
        // the apparent subtraction in the Android deque size arithmetic cancels
        // the one-element-per-block implementation bias. Thus the last real node
        // must be included. The size expression is re-evaluated after every
        // node because dynamic frame access and source lookup can re-enter.
        for (size_t i = 1; i < _nodes.size(); ++i) {
            detail::MotionNode &node = _nodes[i];
            const auto finishOrdinarySeek = [&]() {
                // Native publishes the byte only after the complete crossing
                // loop. It overwrites stale bits rather than ORing bit zero.
                node.flags = 1;
                if(!node.slots[0].merged) {
                    mergeNodeFrameContent_guess(
                        node.slots[0], node.nodeType,
                        node.frameListVariant);
                }
                if(!node.slots[1].merged) {
                    mergeNodeFrameContent_guess(
                        node.slots[1], node.nodeType,
                        node.frameListVariant);
                }

                const int mask = _preview ? 6153 : 6145;
                // The four native tails feed nodeType directly to the target
                // shift instruction; there is no source-level range guard.
                if(node.forceVisible != 0 ||
                   (((1 << node.nodeType) & mask) != 0)) {
                    findSourceForNode_guess(node);
                }
            };
            // The seek updates the live slots, active-slot index and dirty bit;
            // those slots are the real output consumed by updateLayers. The
            // parameter pointer is also the native branch discriminator:
            // parameterized nodes use the event-free bidirectional helper, while
            // ordinary nodes use the direction-specific inline phase and emit
            // action events for crossed frames.
            if(node.parameterEntry != nullptr) {
                // Same forward-plus-corrective-backward seek in both directions;
                // the parameterized path emits no action events.
                seekParameterizedNodeFrames_guess(node, *this);
                continue;
            }

            const auto fireNodeAction = [&](
                const detail::MotionNode::ClipSlot &slot) {
                if((slot.contentMask & 0x40000) != 0) {
                    enqueueActionEvent_guess(
                        tTJSVariant(node.layerName), slot.actionValue);
                }
            };

            if(forward) {
                // The active selector is snapshotted before CopyRef/count. The
                // local owner is used only by count but remains alive through
                // parse, action, merge and source refresh. Those later helpers
                // deliberately re-read the persistent node field.
                const int cursor = node.activeSlotIndex;
                const tTJSVariant frameListOwner = node.frameListVariant;
                const int count =
                    detail::motionPropGetCount(frameListOwner);
                const int limit = subtractTwoWrapping32_guess(count);
                auto *active = &node.slots[cursor];
                auto *other = &node.slots[(cursor & 1) == 0];
                bool seeked = false;

                while(active->frameIndex < limit) {
                    // Ordered less-than break: NaN evaluation continues.
                    if(_clampedEvalTime < other->clipStartTime) {
                        break;
                    }
                    node.activeSlotIndex =
                        (node.activeSlotIndex & 1) == 0;
                    parseNodeFrame_guess(
                        *active, node.frameListVariant,
                        incrementWrapping32_guess(other->frameIndex));
                    // Forward action follows parse and observes the crossed
                    // old-other slot. An exception precedes dirty/merge/source.
                    fireNodeAction(*other);
                    seeked = true;
                    std::swap(active, other);
                }

                if(seeked) {
                    finishOrdinarySeek();
                }
                continue;
            }

            // Rewind has no dynamic count lookup and creates no frame-list
            // owner. It flips first, parses the newly entered previous frame,
            // then emits that frame's action before the next live-time test.
            const int cursor = node.activeSlotIndex;
            auto *active = &node.slots[cursor];
            if(!(active->clipStartTime > _clampedEvalTime)) {
                continue;
            }
            auto *other = &node.slots[(cursor & 1) == 0];
            bool seeked = false;
            for(;;) {
                node.activeSlotIndex =
                    (node.activeSlotIndex & 1) == 0;
                parseNodeFrame_guess(
                    *other, node.frameListVariant,
                    decrementWrapping32_guess(active->frameIndex));
                fireNodeAction(*other);
                seeked = true;
                // Ordered greater-than continuation: NaN stops rewind.
                if(!(other->clipStartTime > _clampedEvalTime)) {
                    break;
                }
                std::swap(active, other);
            }
            if(seeked) {
                finishOrdinarySeek();
            }
        }
    }

    // Independently callable in three references and emitted as an adjacent
    // function after boundary recovery in Android arm64. The loop reloads the
    // live node count because the stepper can re-enter script/resource code.
    void Player::refreshParameterizedNodeTimelines_guess() {
        for(size_t i = 1; i < _nodes.size(); ++i) {
            detail::MotionNode &node = _nodes[i];
            if(node.parameterEntry != nullptr) {
                seekParameterizedNodeFrames_guess(node, *this);
            }
        }
    }

    // Full-reseek STEP 4: absolutely seed both timeline slots for every non-root
    // node, independent of its previous cursor.  All four current references use
    // the real half-open range [1, nodeCount); Android's libstdc++ and iOS's
    // libc++ spell the deque arithmetic differently, but none has a tail sentinel.
    // The following STEP 5 is live as well: reseekTimelineCursors restores/prunes
    // HM3/HM4 and rebuilds every HM1 entry after this loop.  Current four-binary
    // addresses and container-layout details live in the analysis record, not in
    // this compiled source comment.
    void Player::reseedNodeTimelineSlots_guess() {
        auto &nodes = _nodes;
        for (size_t i = 1; i < nodes.size(); ++i) {
            detail::MotionNode &node = nodes[i];
            // The native helper reads the Player's committed evaluation time
            // and owns parse/merge, source refresh, and exact-frame onAction.
            initializeNodeTimelineSlots_guess(*this, node);
        }
    }

    // Pre-progress dirty-node pass. For every non-root force-visible node whose
    // emoteEdit object is marked modified, clear that flag and rebuild both
    // timeline slots. The emoteEdit variant is mutable TJS state, so the clear
    // uses PropSet(TJS_MEMBERENSURE) before rebuilding.
    void Player::refreshModifiedNodeTimelines_guess() {
        // This is the same half-open [1, size()) deque walk as the incremental
        // node phase. The native implementation has no trailing sentinel.
        for (size_t i = 1; i < _nodes.size(); ++i) {
            detail::MotionNode &node = _nodes[i];
            if (node.forceVisible == 0) {
                continue;
            }

            // Copy the persistent Variant first, then retain one independent
            // dispatch owner before releasing the copy. A re-entrant getter
            // may clear node.emoteEditVariant without invalidating the getter,
            // setter, or the following timeline rebuild.
            tTJSVariant emoteEditOwner(node.emoteEditVariant);
            ncbPropAccessor emoteEdit(emoteEditOwner);
            emoteEditOwner.Clear();
            iTJSDispatch2 *const emoteEditDispatch =
                emoteEdit.GetDispatch();
            const bool modified = detail::motionPropGetBool(
                emoteEditDispatch, TJS_W("modified"), 0,
                &detail::emoteEditModifiedHint_guess);
            if (!modified) {
                continue;
            }
            {
                // The setter's Integer temporary is destroyed before the
                // potentially-throwing absolute initializer begins.
                tTJSVariant zero(static_cast<tjs_int>(0));
                (void)emoteEditDispatch->PropSet(
                    TJS_MEMBERENSURE, TJS_W("modified"),
                    &detail::emoteEditModifiedHint_guess, &zero,
                    emoteEditDispatch);
            }
            // The dirty-node rebuild uses the complete absolute helper.
            initializeNodeTimelineSlots_guess(*this, node);
        }
    }

    // Phase 1: Camera velocity, root evaluation, variable interpolation
    void Player::updateLayersPhase1_PreLoop(double currentTime) {
        auto &nodes = _nodes;
        // === PHASE 1: Pre-loop setup ===

        // Integrate each nonzero camera-velocity component into root delta
        // position using the Player frame delta. Each active component publishes
        // root dirty before the multiply/add; signed zero is inactive, while NaN
        // follows the active path. The four ABI layouts are recorded in analysis/.
        {
            auto &rootNode = nodes[0];
            if (_cameraVelocityX != 0.0) {
                rootNode.delta.dirty = true;
                rootNode.delta.posX += _deltaTime * _cameraVelocityX;
            }
            if (_cameraVelocityY != 0.0) {
                rootNode.delta.dirty = true;
                rootNode.delta.posY += _deltaTime * _cameraVelocityY;
            }
            if (_cameraVelocityZ != 0.0) {
                rootNode.delta.dirty = true;
                rootNode.delta.posZ += _deltaTime * _cameraVelocityZ;
            }
            // Damping is applied after root integration. The sole gate is exact
            // equality with 1.0; otherwise pow(damping, frameDelta/60) scales all
            // three velocity components. There is no positive-delta/base guard.
            if (_cameraDamping != 1.0) {
                const double dampFactor = std::pow(_cameraDamping,
                                                    _deltaTime / 60.0);
                _cameraVelocityX *= dampFactor;
                _cameraVelocityY *= dampFactor;
                _cameraVelocityZ *= dampFactor;
            }
        }

        // Step 1: Save previous positions for delta calculation
        for (auto &n : nodes) {
            n.prevPosX = n.accumulated.posX;
            n.prevPosY = n.accumulated.posY;
            n.prevPosZ = n.accumulated.posZ;
        }

        // Step 2: Evaluate root node (index 0)
        auto &root = nodes[0];
        {
            // Root zero is retained across node-tree rebuilds. Its delta block
            // is copied directly; no PSB layer/frame dispatch or Player-level
            // transform shadow participates here.
            copyDeltaBlockToAccum(root.accumulated, root.delta);
            root.delta.dirty = false;
            const std::array<std::uint32_t, 4> rootColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u};
            copyPackedColorsToBytes(root.colorBytes, rootColors);

        }

        // All four reference targets call this unconditionally immediately
        // after copying/clearing the root delta block. The helper reads the
        // Player's current evaluation time; it has no explicit time argument.
        interpolateVarTrackValues_guess();

        // The marker is constructor-zero and set only while linking a type-3
        // child. Such a child's root 2x2 was already propagated by the parent
        // motion pass; ordinary Players and particle children retain false and
        // rebuild the root local matrix here.
        if(!_type3RootTransformAlreadyPropagated) {
            Affine2x3 rootAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            applyLocalTransform(rootAffine, root);
            root.accumulated.m11 = rootAffine[0];
            root.accumulated.m21 = rootAffine[1];
            root.accumulated.m12 = rootAffine[2];
            root.accumulated.m22 = rootAffine[3];
        }

    }

    // Phase 2: Main node evaluation loop (non-root nodes)
    void Player::updateLayersPhase2_MainLoop(double currentTime) {
        auto &nodes = _nodes;
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        bool logoTraceEnabledForPath = false;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
            logoTraceEnabledForPath =
                detail::logoChainTraceEnabledForPath(motionPath);
        }
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            const int origParentIdx = node.parentIndex;
            int parentIdx = node.parentIndex;
            int walkSteps = 0;
            while (parentIdx > 0 && parentIdx < static_cast<int>(nodes.size())) {
                if ((nodes[parentIdx].inheritFlags & 0x00400000) == 0) {
                    break;
                }
                parentIdx = nodes[parentIdx].parentIndex;
                ++walkSteps;
            }
            if (parentIdx < 0 || parentIdx >= static_cast<int>(nodes.size())) {
                parentIdx = 0;
            }
            const auto &parent = nodes[parentIdx];

            if (logoTraceEnabledForPath) {
                const auto &parentNode = nodes[parentIdx];
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.parent_lookup",
                    "Player::updateLayersPhase2_MainLoop",
                    currentTime,
                    "nodeIndex={} label={} type={} inheritFlags=0x{:x} origParentIdx={} resolvedParentIdx={} parentLabel={} parentType={} parentInheritFlags=0x{:x} walkSteps={} independentLayerInherit={}",
                    node.index,
                    node.layerName.IsEmpty() ? std::string("<root>")
                                             : detail::narrow(node.layerName),
                    node.nodeType,
                    static_cast<unsigned>(node.inheritFlags),
                    origParentIdx,
                    parentIdx,
                    parentNode.layerName.IsEmpty() ? std::string("<root>")
                        : detail::narrow(parentNode.layerName),
                    parentNode.nodeType,
                    static_cast<unsigned>(parentNode.inheritFlags),
                    walkSteps,
                    _independentLayerInherit ? 1 : 0);
            }

            // The per-node cursor seek already ran in the progress pass. Here
            // updateLayers only reads the positioned live ClipSlots; this trace
            // state is a diagnostic projection at that read boundary.
            std::optional<TimelineTraceState> traceState;
            if(logoTraceEnabledForPath) {
                traceState.emplace(
                    readNodeFrameSlotsForTrace(node, currentTime));
                const auto &state = *traceState;
                if(state.debugEvaluated) {
                    detail::logoChainTraceLogf(
                        motionPath, "updateLayers.phase2.framesel",
                        "seekNodeFrameSelection", currentTime,
                        "nodeIndex={} label={} type={} activeIndex={} nextIndex={} frameA[time={:.3f},type={},invisible={},src={},opacity={:.6f},scale=({:.6f},{:.6f})] frameB[time={:.3f},type={},invisible={},src={},opacity={:.6f},scale=({:.6f},{:.6f})] t={:.6f} interpolated={} final[src={},opacity={:.6f},scale=({:.6f},{:.6f})]",
                        node.index,
                        node.layerName.IsEmpty() ? std::string("<root>")
                                                 : detail::narrow(node.layerName),
                        node.nodeType,
                        state.debugActiveIndex,
                        state.debugNextIndex,
                        state.debugFrameATime,
                        state.debugFrameAType,
                        state.debugFrameAInvisible ? 1 : 0,
                        state.debugFrameASrc.empty() ? std::string("<none>")
                                                    : state.debugFrameASrc,
                        state.debugFrameAOpacity,
                        state.debugFrameAScaleX,
                        state.debugFrameAScaleY,
                        state.debugFrameBTime,
                        state.debugFrameBType,
                        state.debugFrameBInvisible ? 1 : 0,
                        state.debugFrameBSrc.empty() ? std::string("<none>")
                                                    : state.debugFrameBSrc,
                        state.debugFrameBOpacity,
                        state.debugFrameBScaleX,
                        state.debugFrameBScaleY,
                        state.debugInterpT,
                        state.debugInterpolated ? 1 : 0,
                        state.debugFrameASrc.empty()
                            ? std::string("<none>") : state.debugFrameASrc,
                        state.opacity,
                        state.scaleX,
                        state.scaleY);
                }
            }

            // A nonzero camera-constraint translation in the preceding frame
            // forces every node through evaluation once in the next frame.
            const bool forceDirty = _cameraConstraintDirty_guess;
            const bool needGround = node.groundCorrection;
            const bool parentDirty = parent.accumulated.dirty;
            const bool deltaDirty = node.delta.dirty;
            const bool timelineDirtyArg =
                forceDirty || needGround || parentDirty || deltaDirty;

            const bool evalRet = evaluateTimeline_guess(
                    node, currentTime, timelineDirtyArg);
            if (!evalRet) {
                continue;
            }

            // After a successful evaluation, neutralize the transient transform
            // overrides while preserving the active/visible override bytes.
            neutralizeDeltaTransformOverrides(node.delta);
            node.delta.dirty = false;

            if (node.activeSlot().done) {
                node.accumulated = parent.accumulated;
                const bool copiedDirty = node.accumulated.dirty;
                node.accumulated.active = false;
                node.accumulated.dirty = copiedDirty ? true : (node.flags != 0);
                node.accumulated.visible =
                    node.accumulated.visible && node.delta.visibleOverride;
                node.accumulated.m11 = parent.accumulated.m11;
                node.accumulated.m21 = parent.accumulated.m21;
                node.accumulated.m12 = parent.accumulated.m12;
                node.accumulated.m22 = parent.accumulated.m22;
                continue;
            }

            {
                const bool visResolved = node.delta.visibleOverride
                    ? parent.accumulated.visible
                    : false;
                node.accumulated.dirty = true;
                node.accumulated.flipX ^= node.delta.flipX;
                node.accumulated.flipY ^= node.delta.flipY;
                node.accumulated.visible = visResolved;
                node.accumulated.active =
                    visResolved && node.delta.activeOverride;
            }
            node.accumulated.scaleX *= node.delta.scaleX;
            node.accumulated.scaleY *= node.delta.scaleY;
            node.accumulated.slantX += node.delta.slantX;
            node.accumulated.slantY += node.delta.slantY;
            node.accumulated.opacity =
                node.delta.opacity * node.accumulated.opacity / 255;
            node.accumulated.posX += node.delta.posX;
            node.accumulated.posY += node.delta.posY;
            node.accumulated.posZ += node.delta.posZ;
            node.accumulated.angle += node.delta.angle;

            if (parent.meshType != 0) {
                deformChildByParentBezierPatch_guess(parent, node);
            }

            {
                const double localX = node.accumulated.posX;
                const double localY = node.accumulated.posY;
                const double localZ = node.accumulated.posZ;
                if (parent.coordinateMode != 0) {
                    const double worldX = parent.accumulated.m11 * localX
                        + parent.accumulated.m12 * localZ;
                    const double worldZ = parent.accumulated.m21 * localX
                        + parent.accumulated.m22 * localZ;
                    node.accumulated.posX = worldX + parent.accumulated.posX;
                    node.accumulated.posY = localY + parent.accumulated.posY;
                    node.accumulated.posZ = worldZ + parent.accumulated.posZ;
                } else {
                    const double worldX = parent.accumulated.m11 * localX
                        + parent.accumulated.m12 * localY;
                    const double worldY = parent.accumulated.m21 * localX
                        + parent.accumulated.m22 * localY;
                    node.accumulated.posX = worldX + parent.accumulated.posX;
                    node.accumulated.posY = worldY + parent.accumulated.posY;
                    node.accumulated.posZ = localZ + parent.accumulated.posZ;
                }
            }

            if(node.groundCorrection) {
                // The native worker follows Player.rootPlayer before reading
                // the raw, non-owning current-dispatch bridge slot.
                applyGroundCorrection_guess(
                    _rootPlayer->_currentDispatch, node, parent);
            }

            {
                const int opacityInheritFlags = node.inheritFlags;
                if ((opacityInheritFlags & 0x400) != 0) {
                    node.accumulated.opacity =
                        parent.accumulated.opacity * node.accumulated.opacity / 255;
                } else if (!_independentLayerInherit) {
                    const auto &rootNode = nodes[0];
                    node.accumulated.opacity =
                        rootNode.accumulated.opacity
                        * node.accumulated.opacity / 255;
                }
            }

            const int flags = node.inheritFlags;
            if ((~flags & 0x1FC) == 0) {
                Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(localAffine, node);
                const double lm11 = localAffine[0];
                const double lm21 = localAffine[1];
                const double lm12 = localAffine[2];
                const double lm22 = localAffine[3];
                node.accumulated.m11 =
                    parent.accumulated.m11 * lm11
                    + parent.accumulated.m12 * lm21;
                node.accumulated.m21 =
                    parent.accumulated.m21 * lm11
                    + parent.accumulated.m22 * lm21;
                node.accumulated.m12 =
                    parent.accumulated.m11 * lm12
                    + parent.accumulated.m12 * lm22;
                node.accumulated.m22 =
                    parent.accumulated.m21 * lm12
                    + parent.accumulated.m22 * lm22;
                node.accumulated.flipX ^= parent.accumulated.flipX;
                node.accumulated.flipY ^= parent.accumulated.flipY;
                node.accumulated.angle += parent.accumulated.angle;
                node.accumulated.scaleX *= parent.accumulated.scaleX;
                node.accumulated.scaleY *= parent.accumulated.scaleY;
                node.accumulated.slantX += parent.accumulated.slantX;
                node.accumulated.slantY += parent.accumulated.slantY;
            } else {
                if (flags & 0x004)
                    node.accumulated.flipX ^= parent.accumulated.flipX;
                if (flags & 0x008)
                    node.accumulated.flipY ^= parent.accumulated.flipY;
                if (flags & 0x010)
                    node.accumulated.angle += parent.accumulated.angle;
                if (flags & 0x020)
                    node.accumulated.scaleX *= parent.accumulated.scaleX;
                if (flags & 0x040)
                    node.accumulated.scaleY *= parent.accumulated.scaleY;
                if (flags & 0x080)
                    node.accumulated.slantX += parent.accumulated.slantX;
                if (flags & 0x100)
                    node.accumulated.slantY += parent.accumulated.slantY;

                if (_independentLayerInherit) {
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, node);
                    node.accumulated.m11 = localAffine[0];
                    node.accumulated.m21 = localAffine[1];
                    node.accumulated.m12 = localAffine[2];
                    node.accumulated.m22 = localAffine[3];
                } else {
                    const auto &rootNode = nodes[0];
                    if (flags & 0x004)
                        node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008)
                        node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010)
                        node.accumulated.angle -= rootNode.accumulated.angle;
                    if (flags & 0x020)
                        node.accumulated.scaleX /= rootNode.accumulated.scaleX;
                    if (flags & 0x040)
                        node.accumulated.scaleY /= rootNode.accumulated.scaleY;
                    if (flags & 0x080)
                        node.accumulated.slantX -= rootNode.accumulated.slantX;
                    if (flags & 0x100)
                        node.accumulated.slantY -= rootNode.accumulated.slantY;

                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, node);

                    if (flags & 0x004)
                        node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008)
                        node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010)
                        node.accumulated.angle += rootNode.accumulated.angle;
                    if (flags & 0x020)
                        node.accumulated.scaleX *= rootNode.accumulated.scaleX;
                    if (flags & 0x040)
                        node.accumulated.scaleY *= rootNode.accumulated.scaleY;
                    if (flags & 0x080)
                        node.accumulated.slantX += rootNode.accumulated.slantX;
                    if (flags & 0x100)
                        node.accumulated.slantY += rootNode.accumulated.slantY;

                    const double lm11 = localAffine[0];
                    const double lm21 = localAffine[1];
                    const double lm12 = localAffine[2];
                    const double lm22 = localAffine[3];
                    node.accumulated.m11 =
                        rootNode.accumulated.m11 * lm11
                        + rootNode.accumulated.m12 * lm21;
                    node.accumulated.m21 =
                        rootNode.accumulated.m21 * lm11
                        + rootNode.accumulated.m22 * lm21;
                    node.accumulated.m12 =
                        rootNode.accumulated.m11 * lm12
                        + rootNode.accumulated.m12 * lm22;
                    node.accumulated.m22 =
                        rootNode.accumulated.m21 * lm12
                        + rootNode.accumulated.m22 * lm22;
                }
            }

            if (logoTraceEnabledForPath) {
                const auto &state = *traceState;
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.accum_final",
                    "Player::updateLayersPhase2_MainLoop",
                    currentTime,
                    "nodeIndex={} label={} type={} parentIdx={} parentLabel={} state[visible={},evaluated={},opacity={:.3f},scale=({:.3f},{:.3f}),localPos=({:.3f},{:.3f},{:.3f})] parentAccum[pos=({:.3f},{:.3f},{:.3f}),m=({:.3f},{:.3f},{:.3f},{:.3f}),opacity={},visible={}] accum[pos=({:.3f},{:.3f},{:.3f}),m=({:.3f},{:.3f},{:.3f},{:.3f}),scale=({:.3f},{:.3f}),opacity={},visible={},active={}]",
                    node.index,
                    node.layerName.IsEmpty() ? std::string("<root>")
                                             : detail::narrow(node.layerName),
                    node.nodeType,
                    parentIdx,
                    parent.layerName.IsEmpty() ? std::string("<root>")
                                               : detail::narrow(parent.layerName),
                    state.visible ? 1 : 0,
                    state.debugEvaluated ? 1 : 0,
                    state.opacity,
                    state.scaleX, state.scaleY,
                    state.x, state.y, state.z,
                    parent.accumulated.posX, parent.accumulated.posY,
                    parent.accumulated.posZ,
                    parent.accumulated.m11, parent.accumulated.m21,
                    parent.accumulated.m12, parent.accumulated.m22,
                    parent.accumulated.opacity,
                    parent.accumulated.visible ? 1 : 0,
                    node.accumulated.posX, node.accumulated.posY,
                    node.accumulated.posZ,
                    node.accumulated.m11, node.accumulated.m21,
                    node.accumulated.m12, node.accumulated.m22,
                    node.accumulated.scaleX, node.accumulated.scaleY,
                    node.accumulated.opacity,
                    node.accumulated.visible ? 1 : 0,
                    node.accumulated.active ? 1 : 0);
            }
        }
    }


} // namespace motion
