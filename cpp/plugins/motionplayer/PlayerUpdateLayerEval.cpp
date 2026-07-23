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
            state.frameType = slot.frameIndex >= 0 ? slot.frameType : frameType;
            state.x = slot.x; state.y = slot.y; state.z = slot.z;
            state.opacity = static_cast<double>(slot.opacity) / 255.0;
            state.scaleX = slot.scaleX; state.scaleY = slot.scaleY;
            return state;
        }

        void copyActiveTimelinePayloadLike_0x699B6C(
            detail::MotionNode &node) {
            const auto &slot = node.activeSlot();
            auto &dst = node.accumulated;
            dst.flipX = slot.flipX;
            dst.flipY = slot.flipY;
            dst.posX = slot.x;
            dst.posY = slot.y;
            dst.posZ = slot.z;
            dst.angle = slot.angle;
            dst.scaleX = slot.scaleX;
            dst.scaleY = slot.scaleY;
            dst.slantX = slot.slantX;
            dst.slantY = slot.slantY;
            dst.opacity = slot.opacity;
            dst.blendMode = slot.blendMode;
            copyPackedColorsToBytes(node.colorBytes, slot.packedColors);

            // Player_evaluateTimeline @0x699BE4..0x699C20 copies the active
            // slot vector directly into node+2024 when meshType==1.
            if(node.meshType == 1) {
                node.meshControlPoints = slot.meshControlPoints;
            }
        }

        void interpolateTimelinePayloadLike_0x699D80(
            detail::MotionNode &node,
            double ratio) {
            const auto &a = node.activeSlot();
            const auto &b = node.otherSlot();
            auto &dst = node.accumulated;
            auto lerp = [ratio](double lhs, double rhs) {
                return rhs * ratio + lhs * (1.0 - ratio);
            };

            dst.flipX = a.flipX;
            dst.flipY = a.flipY;

            const double srcPos[3] = {a.x, a.y, a.z};
            const double dstPos[3] = {b.x, b.y, b.z};
            double outPos[3] = {};
            interpolatePositionVariantLike_0x69A4D4(
                a.cccVariant, dstPos, srcPos, outPos,
                node.coordinateMode, a.cpVariant, ratio);
            dst.posX = outPos[0];
            dst.posY = outPos[1];
            dst.posZ = outPos[2];

            const double angleRatio = a.accVariant.Type() != tvtVoid
                ? evaluateBezierVariantLike_0x69A754(a.accVariant, ratio)
                : ratio;
            double angleA = a.angle;
            double angleB = b.angle;
            if(angleA != angleB) {
                if(angleA >= angleB) {
                    if(angleA - angleB > 180.0) angleB += 360.0;
                } else if(angleB - angleA > 180.0) {
                    angleB -= 360.0;
                }
                angleA = angleB * angleRatio + angleA * (1.0 - angleRatio);
                if(angleA < 0.0) angleA += 360.0;
                else if(angleA >= 360.0) angleA -= 360.0;
            }
            dst.angle = angleA;

            const double scaleRatio = a.zccVariant.Type() != tvtVoid
                ? evaluateBezierVariantLike_0x69A754(a.zccVariant, ratio)
                : ratio;
            dst.scaleX = a.scaleX == b.scaleX
                ? a.scaleX : b.scaleX * scaleRatio
                    + a.scaleX * (1.0 - scaleRatio);
            dst.scaleY = a.scaleY == b.scaleY
                ? a.scaleY : b.scaleY * scaleRatio
                    + a.scaleY * (1.0 - scaleRatio);

            const double slantRatio = a.sccVariant.Type() != tvtVoid
                ? evaluateBezierVariantLike_0x69A754(a.sccVariant, ratio)
                : ratio;
            dst.slantX = a.slantX == b.slantX
                ? a.slantX : b.slantX * slantRatio
                    + a.slantX * (1.0 - slantRatio);
            dst.slantY = a.slantY == b.slantY
                ? a.slantY : b.slantY * slantRatio
                    + a.slantY * (1.0 - slantRatio);

            double opacity = static_cast<double>(a.opacity);
            if(a.opacity != b.opacity) {
                opacity = lerp(opacity, static_cast<double>(b.opacity));
            }
            dst.opacity = opacity < 0.0
                ? static_cast<int>(std::ceil(opacity - 0.5))
                : static_cast<int>(std::floor(opacity + 0.5));
            dst.blendMode = a.blendMode;
            copyPackedColorsToBytes(node.colorBytes, a.packedColors);
        }

        // sub_69AC4C @0x69AC4C. Android stores each control point as one 8-byte
        // {float x,float y} vector element; MeshPoint preserves that source-level
        // element topology throughout the production interpolation chain.
        void interpolateMeshPointsLike_0x69AC4C(
            const tTJSVariant &curve,
            std::vector<detail::MeshPoint> &output,
            const std::vector<detail::MeshPoint> &source,
            const std::vector<detail::MeshPoint> &target,
            double ratio) {
            float meshRatio = static_cast<float>(ratio);
            if(curve.Type() != tvtVoid) {
                meshRatio = static_cast<float>(
                    evaluateBezierVariantLike_0x69A754(
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

        void writeTypeSpecificCopyLike_0x699C2C(
            detail::MotionNode &node) {
            const auto &slot = node.activeSlot();
            switch(node.nodeType) {
                case 5:
                    node.cameraFov = slot.cameraFov;             // node+2368
                    break;
                case 10:
                    node.feedbackTimespan = slot.feedbackTimespan; // node+2432
                    break;
                default:
                    break;
            }
        }

        void writeTypeSpecificLerpLike_0x69A0F8(
            detail::MotionNode &node,
            double ratio) {
            const auto &a = node.activeSlot();
            const auto &b = node.otherSlot();
            switch(node.nodeType) {
                case 5:
                    node.cameraFov = b.cameraFov * ratio
                        + a.cameraFov * (1.0 - ratio);            // node+2368
                    break;
                case 10:
                    node.feedbackTimespan = b.feedbackTimespan * ratio
                        + a.feedbackTimespan * (1.0 - ratio);    // node+2432
                    break;
                default:
                    break;
            }
        }

        void interpolateMeshPayloadLike_0x69A058(
            detail::MotionNode &node,
            double ratio) {
            if(node.meshType != 1) {
                return;
            }
            auto &a = node.activeSlot();
            const auto &b = node.otherSlot();
            static const std::vector<detail::MeshPoint> emptyMeshPoints;
            // At 0x69A058 both-empty only clears the already-empty active
            // vector and deliberately leaves node+2024 untouched.
            if(a.meshControlPoints.empty() && b.meshControlPoints.empty()) {
                a.meshControlPoints.clear();
                return;
            }
            const auto &source = a.meshControlPoints.empty()
                ? emptyMeshPoints : a.meshControlPoints;
            const auto &target = b.meshControlPoints.empty()
                ? emptyMeshPoints : b.meshControlPoints;
            interpolateMeshPointsLike_0x69AC4C(
                a.meshCurveVariant, node.meshControlPoints,
                source, target, ratio);
        }

        // Player_evaluateTimeline (0x699AE4) type-4 particle mirror.
        // The binary's nodeType==4 switch case writes the node+2224..2288 eval
        // mirror (MotionNode::particleInterp) from the active slot's particle block
        // via one of two branches:
        //   - COPY branch @0x699c2c (no crossfade / single slot): node+2224..2288
        //     <- v11[93..101] where v11 = node+536*idx, i.e. node+536*idx+744..808.
        //   - INTERP branch @0x69a0f8 (crossfade): per-field lerp of the active and
        //     other slots' blocks at node+320+536*idx+424..488 with ratio r, copying
        //     the active value when active==other (binary `if(a!=b) v=b*r+a*(1-r)`).
        // ALIAS (self-disassembled): node+536*idx+744 == node+320+536*idx+424, so
        // COPY's slot+744 and INTERP's slot+424 reference the SAME prt block. The
        // block is written every frame during normal playback by mergeFrameContent
        // (slot+424 = prtFmin) and on the HM3 path by restore @0x699890. The COPY
        // branch therefore reads the same prtFmin..prtRange fields INTERP reads
        // (its srcA), NOT a separate region. This mirror is what the particle
        // emitter @0x6BF0DC reads (node4[139..143]).
        void writeParticleInterpCopyLike_0x699c2c(detail::MotionNode &node) {
            const detail::MotionNode::ClipSlot &slot = node.activeSlot();
            // node+2224..2288 <- slot+424..488 (prt block, == COPY's slot+744..808).
            const double src[9] = {slot.prtFmin, slot.prtF, slot.prtVmin, slot.prtV,
                                   slot.prtAmin, slot.prtA, slot.prtZmin, slot.prtZ,
                                   slot.prtRange};
            for(int i = 0; i < 9; ++i) {
                node.particleInterp[i] = src[i]; // node+2224.. <- slot+424.. (==+744)
            }
        }

        void writeParticleInterpLerpLike_0x69a0f8(detail::MotionNode &node,
                                                  double ratio) {
            const auto &a = node.activeSlot();   // slot[active]+424..488
            const auto &b = node.otherSlot();    // slot[other]+424..488
            const double srcA[9] = {a.prtFmin, a.prtF, a.prtVmin, a.prtV,
                                    a.prtAmin, a.prtA, a.prtZmin, a.prtZ,
                                    a.prtRange};
            const double srcB[9] = {b.prtFmin, b.prtF, b.prtVmin, b.prtV,
                                    b.prtAmin, b.prtA, b.prtZmin, b.prtZ,
                                    b.prtRange};
            for(int i = 0; i < 9; ++i) {
                double v = srcA[i];
                if(srcA[i] != srcB[i]) {         // binary: lerp only when differ
                    v = srcB[i] * ratio + srcA[i] * (1.0 - ratio);
                }
                node.particleInterp[i] = v;       // node+2224..2288
            }
        }

        // REMOVED 2026-06-21: markNodePayloadDirtyFromState / markNodeNoActiveFrame.
        // These were an invented per-node payload-change detector that set node+44
        // (node.flags bit0x01) unconditionally at the seek-primitive tail, with no
        // counterpart in libkrkr2.so (node+44 is set 1 only inside the actual
        // cross-frame seek iteration bodies — 0x6B7FBC / 0x6B72C0 / 0x6BA28C — and
        // cleared unconditionally each frame by the post-loop at 0x6BBD2C). See the
        // detailed comment at the former call site in
        // advanceNodeFrameSelectionLike_0x6926B4. The port-local lastActive* cache
        // fields they maintained had no other consumer and were removed from
        // MotionNode.h.

        bool rawDispatchObject(const tTJSVariant &value) {
            return value.Type() == tvtObject && value.AsObjectNoAddRef() != nullptr;
        }

        // Player_resetFrameSlot @0x69260C. This deliberately does not assign a
        // fresh C++ object: Android releases only src/curve/act owners, zeros the
        // transform prefix and clears the mesh vector while retaining capacity.
        void resetClipSlotLike_0x69260C(
            detail::MotionNode::ClipSlot &slot) {
            slot.frameIndex = 0;
            slot.frameType = 0;
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
            slot.width = 0.0;
            slot.height = 0.0;
            slot.cccVariant.Clear();
            slot.occVariant.Clear();
            slot.accVariant.Clear();
            slot.zccVariant.Clear();
            slot.sccVariant.Clear();
            slot.cpVariant.Clear();
            slot.meshCurveVariant.Clear();
            slot.actionValue.Clear();
            slot.hasSync = false;
            slot.meshControlPoints.clear();
        }

        // Player_parseFrame @0x6926B4: raw frameList+index input, parse only.
        void parseFrameLike_0x6926B4(
            detail::MotionNode::ClipSlot &slot,
            const tTJSVariant &frameList,
            int frameIndex) {
            resetClipSlotLike_0x69260C(slot);
            slot.frameIndex = frameIndex;
            const tTJSVariant frame =
                detail::motionPropGetByNum(frameList, frameIndex);
            slot.clipStartTime =
                detail::motionPropGetDouble(frame, TJS_W("time"));
            const int type =
                detail::motionPropGetInt(frame, TJS_W("type"));
            slot.frameType = type;
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
            const tTJSVariant content =
                detail::motionPropGet(frame, TJS_W("content"));
            slot.contentMask =
                detail::motionPropGetInt(content, TJS_W("mask"));
            if((slot.contentMask & 0x40000) != 0) {
                slot.actionValue =
                    detail::motionPropGetString(content, TJS_W("act"));
            }
        }

        void copyRawCurveVariant(const tTJSVariant &content,
                                 const tjs_char *name,
                                 tTJSVariant &owner) {
            owner = detail::motionPropGet(content, name);
        }

        // Player_mergeFrameContent @0x692AB0. The third Android argument is the
        // raw frameList; content is intentionally fetched again by slot index.
        void mergeFrameContentLike_0x692AB0(
            detail::MotionNode &node,
            detail::MotionNode::ClipSlot &slot,
            const tTJSVariant &frameList) {
            slot.merged = true;
            if(slot.done) {
                return;
            }

            const tTJSVariant frame =
                detail::motionPropGetByNum(frameList, slot.frameIndex);
            const tTJSVariant content =
                detail::motionPropGet(frame, TJS_W("content"));
            const std::uint32_t mask =
                static_cast<std::uint32_t>(slot.contentMask);

            slot.packedColors = {0xFF808080u, 0xFF808080u,
                                 0xFF808080u, 0xFF808080u};
            slot.scaleX = 1.0;
            slot.scaleY = 1.0;
            slot.opacity = 255;
            slot.blendMode = 16;

            if(node.nodeType >= 0 && node.nodeType < 32 &&
               (((1u << static_cast<unsigned>(node.nodeType)) & 0x1849u) != 0)) {
                slot.srcValue =
                    detail::motionPropGetString(content, TJS_W("src"));
                tTJSVariant probe;
                if(detail::motionTryPropGet(
                       content, TJS_W("icon"), probe,
                       TJS_MEMBERMUSTEXIST)) {
                    slot.iconValue =
                        detail::motionPropGetString(content, TJS_W("icon"));
                } else {
                    slot.iconValue.Clear();
                }
            }

            if((mask & 0x1u) != 0) {
                slot.ox = detail::motionPropGetDouble(content, TJS_W("ox"));
                slot.oy = detail::motionPropGetDouble(content, TJS_W("oy"));
            }
            if((mask & 0x2u) != 0) {
                const tTJSVariant coord =
                    detail::motionPropGet(content, TJS_W("coord"));
                slot.x = detail::motionPropGetDoubleByNum(coord, 0);
                slot.y = detail::motionPropGetDoubleByNum(coord, 1);
                slot.z = detail::motionPropGetDoubleByNum(coord, 2);
            }

            if((mask & 0x20600u) != 0) {
                if((mask & 0x20000u) != 0) {
                    slot.blendMode =
                        detail::motionPropGetInt(content, TJS_W("bm"));
                }
                if((mask & 0x200u) != 0) {
                    const tTJSVariant color =
                        detail::motionPropGet(content, TJS_W("color"));
                    if(color.Type() == tvtObject) {
                        for(int i = 0; i < 4; ++i) {
                            slot.packedColors[static_cast<std::size_t>(i)] =
                                static_cast<std::uint32_t>(
                                    detail::motionPropGetIntByNum(color, i));
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
                        detail::motionPropGetInt(content, TJS_W("opa"));
                }
            }

            if((mask & 0x1FCu) != 0) {
                if((mask & 0xCu) != 0) {
                    slot.flipX =
                        detail::motionPropGetBool(content, TJS_W("fx"));
                    slot.flipY =
                        detail::motionPropGetBool(content, TJS_W("fy"));
                }
                if((mask & 0x10u) != 0) {
                    slot.angle =
                        detail::motionPropGetDouble(content, TJS_W("angle"));
                }
                if((mask & 0x60u) != 0) {
                    slot.scaleX =
                        detail::motionPropGetDouble(content, TJS_W("zx"));
                    slot.scaleY =
                        detail::motionPropGetDouble(content, TJS_W("zy"));
                }
                if((mask & 0x180u) != 0) {
                    slot.slantX =
                        detail::motionPropGetDouble(content, TJS_W("sx"));
                    slot.slantY =
                        detail::motionPropGetDouble(content, TJS_W("sy"));
                }
            }

            if(slot.crossfading) {
                if((mask & 0x04000000u) != 0) {
                    slot.ti = static_cast<std::uint32_t>(
                        detail::motionPropGetInt(content, TJS_W("ti")));
                }
                if((mask & 0x800u) != 0) {
                    copyRawCurveVariant(content, TJS_W("ccc"),
                                        slot.cccVariant);
                }
                if((mask & 0x8000u) != 0) {
                    copyRawCurveVariant(content, TJS_W("occ"),
                                        slot.occVariant);
                }
                if((mask & 0x1000u) != 0) {
                    copyRawCurveVariant(content, TJS_W("acc"),
                                        slot.accVariant);
                }
                if((mask & 0x2000u) != 0) {
                    copyRawCurveVariant(content, TJS_W("zcc"),
                                        slot.zccVariant);
                }
                if((mask & 0x4000u) != 0) {
                    copyRawCurveVariant(content, TJS_W("scc"),
                                        slot.sccVariant);
                }
                if((mask & 0x10000u) != 0) {
                    slot.cpVariant =
                        detail::motionPropGet(content, TJS_W("cp"));
                }
            }

            if((mask & 0x02000000u) != 0) {
                tTJSVariant mesh =
                    detail::motionPropGet(content, TJS_W("mesh"));
                if(mesh.Type() == tvtVoid) {
                    mesh = detail::motionPropGet(content, TJS_W("obj"));
                }
                slot.meshCurveVariant =
                    detail::motionPropGet(mesh, TJS_W("cc"));
                if(slot.meshCurveVariant.Type() == tvtVoid) {
                    slot.meshCurveVariant =
                        detail::motionPropGet(mesh, TJS_W("mcc"));
                }
                tTJSVariant points =
                    detail::motionPropGet(mesh, TJS_W("bp"));
                if(points.Type() == tvtVoid) {
                    points = detail::motionPropGet(
                        mesh, TJS_W("bezierPatch"));
                }
                if(points.Type() == tvtObject) {
                    if(detail::motionPropGetCount(points) != 32) {
                        TJS_eTJSError(
                            TJS_W("unexpected bezier patch point num."));
                    }
                    slot.meshControlPoints.reserve(16);
                    for(int i = 0; i < 32; i += 2) {
                        slot.meshControlPoints.push_back({
                            static_cast<float>(
                                detail::motionPropGetDoubleByNum(points, i)),
                            static_cast<float>(
                                detail::motionPropGetDoubleByNum(points, i + 1))
                        });
                    }
                }
            }

            if((mask & 0x80000u) != 0) {
                const tTJSVariant motion =
                    detail::motionPropGet(content, TJS_W("motion"));
                const int motionMask =
                    detail::motionPropGetInt(motion, TJS_W("mask"));
                slot.motionFlags = 0;
                slot.motionDt = 1;
                slot.motionDocmpl = false;
                slot.motionDofst = 0.0;
                slot.motionDtgtValue.Clear();
                if((motionMask & 0x1) != 0) {
                    slot.motionFlags =
                        detail::motionPropGetInt(motion, TJS_W("flags"));
                }
                if((motionMask & 0x2) != 0) {
                    slot.motionDt =
                        detail::motionPropGetInt(motion, TJS_W("dt"));
                }
                if((motionMask & 0x4) != 0) {
                    slot.motionDocmpl =
                        detail::motionPropGetBool(motion, TJS_W("docmpl"));
                }
                if((motionMask & 0x8) != 0) {
                    slot.motionDofst =
                        detail::motionPropGetDouble(motion, TJS_W("dofst"));
                }
                if((motionMask & 0x10) != 0) {
                    slot.motionDtgtValue =
                        detail::motionPropGetString(motion, TJS_W("dtgt"));
                }
                slot.motionTimeOffset =
                    detail::motionPropGetDouble(motion, TJS_W("timeOffset"));
            }

            if((mask & 0x01000000u) != 0) {
                const tTJSVariant model =
                    detail::motionPropGet(content, TJS_W("model"));
                slot.modelTimeOffset =
                    detail::motionPropGetDouble(model, TJS_W("timeOffset"));
                slot.modelLoop =
                    detail::motionPropGetBool(model, TJS_W("loop"));
                slot.modelDt =
                    detail::motionPropGetInt(model, TJS_W("dt"));
                slot.modelDtgt =
                    detail::motionPropGetString(model, TJS_W("dtgt"));
            }

            if((mask & 0x100000u) != 0) {
                const tTJSVariant particle =
                    detail::motionPropGet(content, TJS_W("prt"));
                const int particleMask =
                    detail::motionPropGetInt(particle, TJS_W("mask"));
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
                        detail::motionPropGetInt(particle, TJS_W("trigger"));
                }
                if((particleMask & 0x2) != 0) {
                    slot.prtFmin =
                        detail::motionPropGetDouble(particle, TJS_W("fmin"));
                    slot.prtF =
                        detail::motionPropGetDouble(particle, TJS_W("fmax"));
                }
                if((particleMask & 0x4) != 0) {
                    slot.prtVmin =
                        detail::motionPropGetDouble(particle, TJS_W("vmin"));
                    slot.prtV =
                        detail::motionPropGetDouble(particle, TJS_W("vmax"));
                }
                if((particleMask & 0x8) != 0) {
                    slot.prtAmin =
                        detail::motionPropGetDouble(particle, TJS_W("amin"));
                    slot.prtA =
                        detail::motionPropGetDouble(particle, TJS_W("amax"));
                }
                if((particleMask & 0x10) != 0) {
                    slot.prtZmin =
                        detail::motionPropGetDouble(particle, TJS_W("zmin"));
                    slot.prtZ =
                        detail::motionPropGetDouble(particle, TJS_W("zmax"));
                }
                if((particleMask & 0x20) != 0) {
                    slot.prtRange =
                        detail::motionPropGetDouble(particle, TJS_W("range"));
                }
            }

            if((mask & 0x200000u) != 0) {
                const tTJSVariant camera =
                    detail::motionPropGet(content, TJS_W("camera"));
                slot.cameraFov =
                    detail::motionPropGetDouble(camera, TJS_W("fov"));
                slot.cameraTarget =
                    detail::motionPropGetString(camera, TJS_W("target"));
            }
            if((mask & 0x800000u) != 0) {
                const tTJSVariant anchor =
                    detail::motionPropGet(content, TJS_W("anchor"));
                slot.anchorTarget =
                    detail::motionPropGetString(anchor, TJS_W("target"));
            }
            if((mask & 0x08000000u) != 0) {
                const tTJSVariant feedback =
                    detail::motionPropGet(content, TJS_W("feedback"));
                slot.feedbackTimespan =
                    detail::motionPropGetDouble(feedback, TJS_W("timespan"));
            }

            // Local evaluator compatibility: Android reads node+84 directly;
            // do not re-read a decoded layer dictionary.
            slot.hasTransformOrder = true;
            std::copy(node.transformOrder, node.transformOrder + 4,
                      slot.transformOrder);
        }

        int initialFrameIndexForTimeLike_0x6B64AC(
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
            return selected <= count - 2 ? selected : count - 2;
        }

        double frameSelectionTimeLike_0x6B7E44(
            const detail::MotionNode &node,
            double currentTime) {
            // sub_6B64AC/sub_6B7E44 read node+8->value when the node is
            // parameterized; otherwise they use the Player timeline time.
            if(node.parameterEntry != nullptr) {
                return node.parameterEntry->value;
            }
            return currentTime;
        }

        bool initializeNodeTimelineSlotsLike_0x6B64AC(
            detail::MotionNode &node,
            double currentTime,
            std::vector<detail::MotionEvent> *pendingEvents = nullptr) {
            if(!rawDispatchObject(node.frameListVariant)) {
                return false;
            }

            const double selectionTime =
                frameSelectionTimeLike_0x6B7E44(node, currentTime);
            const int activeIndex = initialFrameIndexForTimeLike_0x6B64AC(
                node.frameListVariant, selectionTime);
            node.activeSlotIndex = 0;
            parseFrameLike_0x6926B4(
                node.slots[0], node.frameListVariant, activeIndex);
            mergeFrameContentLike_0x692AB0(
                node, node.slots[0], node.frameListVariant);
            parseFrameLike_0x6926B4(
                node.slots[1], node.frameListVariant, activeIndex + 1);
            mergeFrameContentLike_0x692AB0(
                node, node.slots[1], node.frameListVariant);
            node.flags |= 0x01;

            // 砖5/洞2 (initNodeTimeline tail): Aligned with libkrkr2.so
            // Player_initNodeTimeline tail @0x6B674C. After parsing slot[0]
            // (node+320) and slot[1], the binary fires a per-node onAction when the
            // seed landed EXACTLY on slot[0]'s frame AND that frame carries an
            // action:
            //   if ( v8 == *(double*)(node+328)            // slot[0].time
            //        && (*(_BYTE*)(node+342) & 4) != 0 )   // slot[0].mask & 0x40000
            //     Player_pushActionEvent_guess(player, &node_label, node+608);
            // where v8 = frameSelectionTimeLike_0x6B7E44(node) (the seed target,
            // node+8 ? *(node+8)+40 : player+456), node+328 = slot[0]+8 = time
            // (= ClipSlot.clipStartTime), the (node+342 & 4) byte = slot[0].mask
            // & 0x40000 (parseFrame @0x6928EC sets slot+288='act' ONLY under that
            // bit), and node+608 = slot[0]+288 = the action variant. The pushed
            // event is {type=0(ACTION), param1=node label, param2=action} (same
            // contract as Player_pushActionEvent_guess @0x6B63C0 -> onAction). The
            // The binary tests the 0x40000 mask bit itself; actionValue is the
            // independent slot+288 ttstr copied into the queued variant.
            if(pendingEvents) {
                const detail::MotionNode::ClipSlot &slot0 = node.slots[0];
                if(selectionTime == slot0.clipStartTime &&
                   (slot0.contentMask & 0x40000) != 0) {
                    detail::MotionEvent event;
                    event.param1 = tTJSVariant(node.layerName);
                    event.param2 = tTJSVariant(slot0.actionValue);
                    pendingEvents->push_back(event);
                }
            }
            return true;
        }

        TimelineTraceState traceStateFromNodeSlots(
            const detail::MotionNode &node,
            double currentTime) {
            const auto &active = node.activeSlot();
            const auto &other = node.otherSlot();
            TimelineTraceState state =
                traceStateFromClipSlot(active, !active.done, active.frameType);
            state.debugEvaluated = active.frameIndex >= 0;
            state.debugActiveIndex = active.frameIndex;
            state.debugFrameATime = active.clipStartTime;
            state.debugFrameAType = active.frameType;
            state.debugFrameAInvisible = active.done;
            state.debugFrameAOpacity =
                static_cast<double>(active.opacity) / 255.0;
            state.debugFrameAScaleX = active.scaleX;
            state.debugFrameAScaleY = active.scaleY;
            state.debugFrameASrc = detail::narrow(active.srcValue);
            if(other.frameIndex >= 0) {
                state.debugNextIndex = other.frameIndex;
                state.debugFrameBTime = other.clipStartTime;
                state.debugFrameBType = other.frameType;
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

    // The shared per-node seek primitive behind THREE binary functions that all
    // run an identical forward / corrective-backward seek over node.slots[0/1]
    // and an identical state-establish tail, differing only in WHICH direction
    // loops run and whether per-node onAction fires:
    //   • Player_advanceNodeFrames  @0x6B7E44 (parameterized): forward + corrective
    //       backward, NO events  → doForward=true,  doBackward=true,  events=null
    //   • forward inline seek       @0x6B73DC (advanceRootAndNodes, non-param):
    //       forward only, WITH events → doForward=true, doBackward=false
    //   • backward inline seek      @0x6BA1CC (rewindRootAndNodes, non-param):
    //       backward only, WITH events → doForward=false, doBackward=true
    // The forward/backward loops below are gated by doForward/doBackward so each
    // binary variant gets exactly its loop set; the frames-empty handling, the
    // init seed, and the tail are shared (byte-identical across all three).
    // (doBackward off for the forward inline is the binary's reality — 0x6B73DC
    // has NO corrective-backward; its loop-wrap repositioning is done earlier by
    // Player_reseekTimelineCursors' node re-seed @0x6B91B0, ported as
    // reseekNodeTimelineSlotsLike_0x6B91B0. Empirically the backward loop never
    // fires for the logo cases, so the split is logo-identical.)
    MOTIONPLAYER_NOINLINE void
    advanceNodeFrameSelectionLike_0x6926B4(
        detail::MotionNode &node, double currentTime,
        std::vector<detail::MotionEvent> *pendingEvents,
        bool doForward, bool doBackward) {
        // 砖5/洞2: fire a per-node onAction(label, action) for each crossed
        // action frame (slot mask bit 0x40000 / content["act"], stored in
        // ClipSlot.actionValue). Aligned to sub_6B638C (0x6B638C) push
        // in Player_advanceRootAndNodes (0x6B6ADC, push @0x6B74E4, fires the
        // CROSSED `other` slot) and Player_rewindRootAndNodes (0x6B9A3C, push
        // @0x6BA26C, fires the just-entered prev slot).
        //
        // param1 = *(node+0). VERIFIED via Player_initNodeFields (0x6B3C78):
        // node+0 is seeded at 0x6B3DF4 with the refcounted ttstr pointer of the
        // PropGetString("label") result (sub_529524 @0x6B3DC8). The inline push
        // constructs a String tTJSVariant from that same pointer (advance 0x6B74BC:
        // `LDR X8,[X20]; STUR X8,[var_70]` then AddRef) copies exactly that
        // label variant as record.a. node.layerName is this same PSB "label"
        // (NodeTree.cpp:108). So param1 = node.layerName is the faithful port.
        // NOTE: param1 is NOT the layer dispatch object (node.tjsLayerObject /
        // *(node+0)+16); that misreading would diverge from the binary.
        // param2 = slot+288 (advance push arg `node+0x120`) = actionValue.
        const auto fireNodeAction =
            [&](const detail::MotionNode::ClipSlot &slot) {
            if(pendingEvents && (slot.contentMask & 0x40000) != 0) {
                detail::MotionEvent event;
                event.param1 = tTJSVariant(node.layerName);
                event.param2 = tTJSVariant(slot.actionValue);
                pendingEvents->push_back(event);
            }
        };

        if(!rawDispatchObject(node.frameListVariant)) {
            node.activeSlot().done = true;
            node.activeSlot().crossfading = false;
            node.otherSlot().done = true;
            return;
        }

        const int frameCount =
            detail::motionPropGetCount(node.frameListVariant);
        const double selectionTime =
            frameSelectionTimeLike_0x6B7E44(node, currentTime);
        bool seeked = false;
        const int lastForwardFrameIndex = frameCount - 2;
        while(doForward &&
              node.activeSlot().frameIndex < lastForwardFrameIndex &&
              selectionTime >= node.otherSlot().clipStartTime) {
            // 0x6B6ADC: fire the crossed frame (`other`) before the swap.
            fireNodeAction(node.otherSlot());
            node.activeSlotIndex ^= 1;
            const int nextIndex = node.activeSlot().frameIndex + 1;
            parseFrameLike_0x6926B4(
                node.otherSlot(), node.frameListVariant, nextIndex);
            seeked = true;
            node.flags |= 0x01;
        }

        while(doBackward &&
              selectionTime < node.activeSlot().clipStartTime) {
            const int previousIndex = node.activeSlot().frameIndex - 1;
            node.activeSlotIndex ^= 1;
            parseFrameLike_0x6926B4(
                node.activeSlot(), node.frameListVariant, previousIndex);
            // 0x6B9A3C: rewind fires the just-entered (previous) frame.
            fireNodeAction(node.activeSlot());
            seeked = true;
            node.flags |= 0x01;
        }

        // 0x6B7FC0/0x6B7FD4 and the two inline siblings merge only slots
        // invalidated by parseFrame. A tick that crosses no frame performs no
        // merge work at all.
        if(seeked) {
            if(!node.slots[0].merged) {
                mergeFrameContentLike_0x692AB0(
                    node, node.slots[0], node.frameListVariant);
            }
            if(!node.slots[1].merged) {
                mergeFrameContentLike_0x692AB0(
                    node, node.slots[1], node.frameListVariant);
            }
        }

        if(node.activeSlot().frameIndex < 0) {
            return;
        }
        node.currentFrameType = node.activeSlot().frameType;
        // NOTE (2026-06-21): the port previously called
        // markNodePayloadDirtyFromState(node, state) here — an INVENTED node+44
        // (node.flags bit0x01) dirtying channel that ran UNCONDITIONALLY at the
        // tail of the seek primitive (outside the forward/backward seek loops),
        // setting node.flags=1 whenever the active payload differed from a
        // port-local lastActive* cache. libkrkr2.so has NO such channel: node+44
        // is set 1 ONLY inside the actual cross-frame seek iteration bodies, each
        // guarded by an "did this frame actually step a frame" flag
        // (Player_advanceNodeFrames 0x6B7FBC via `else if((v9&1)==0) goto LABEL_25`;
        // Player_advanceRootAndNodes inline seek 0x6B72C0 via `if((v47&1)==0) goto
        // LABEL_98`; Player_rewindRootAndNodes 0x6BA28C same pattern). A node that
        // does NOT cross a frame this tick leaves node+44 at the 0 written by
        // Player_updateLayers' unconditional post-loop clear (0x6BBD2C). The
        // invented unconditional channel kept static type-3 child-Player nodes'
        // node+44 (and hence Player_evaluateTimeline's `internalDirty = a2 ||
        // node+44` at 0x699B1C → accumulated.dirty at node+1504) pinned at 1 every
        // frame, so they never settled → childMotionPass (0x6BE0C0) never took the
        // skip gate → the child motion was re-played every frame with _deltaTime=0
        // → the child Player's time never advanced → the type-3 subtree never
        // reached 'done'/destroy → unbounded recursion (DRACU title: 28000 nodes /
        // 1.9GB / blank). Removed to match the binary: node+44 settles via the
        // post-loop clear when no seek iteration runs this tick.
    }

    // ==================================================================
    // Player_advanceNodeFrames @ 0x6B7E44  (P7 convergence step 1 of 3)
    // ------------------------------------------------------------------
    // The binary's per-node 2-slot ping-pong frame seek for PARAMETERIZED
    // nodes (the caller branch `if (*(node+8)) advanceNodeFrames(node,player)`
    // at Player_advanceRootAndNodes 0x6B73B4/0x6B73D4, LABEL_104). It forward-
    // seeks (with corrective backward seek) the node's active parsed-frame slot
    // toward the node's CHILD eval time, then merges both slots + gated
    // findSource.
    //
    // D-A1 RESOLUTION (decompiled, NOT trusted from stale comments):
    //   The binary reads the seek target at 0x6B7E90 as
    //       v6 = *(double *)(*(node+8) + 40)
    //   node+8 is seeded by Player_initNodeFields (0x6B3EA0): when the PSB
    //   "parameterize" value has variant type 4 (integer), node+8 =
    //   player_paramTable[idx] (56-byte stride entry, 0x6B3E90/0x6B3EA0);
    //   otherwise node+8 = 0. So node+8 is a *parameter-table entry*, NOT a
    //   child Player. Offset +40 of that 56-byte entry is the interpolated
    //   parameter VALUE: sub_6B1718 (the param-entry builder) writes the
    //   entry's fields at base..base+48 and stores the eased value at
    //   `*(double*)(v6-16)` = entry+40 (0x6B19E4). Cross-checked against
    //   Player_initNodeTimeline (0x6B64AC, 0x6B6500):
    //       v7 = (*(node+8)) ? (double*)(*(node+8)+40) : (double*)(player+456)
    //   i.e. parameterized -> entry+40 value, else player+456 (clampedEvalTime).
    //   The live MotionParameterEntry maps entry+40 -> ::value (RuntimeSupport.h
    //   builds the 56-byte entry with `value` as the eased field). Therefore
    //   *(node+8)+40 == node.parameterEntry->value == exactly what
    //   frameSelectionTimeLike_0x6B7E44 returns for a parameterized node
    //   (PlayerUpdateLayerEval.cpp). CONFIRMED EQUAL: child+40 == the live
    //   parameterEntry->value. No behavior change for parameterized nodes.
    //
    // FAITHFUL BODY = the shared seek + state-establish tail, events suppressed.
    // ------------------------------------------------------------------
    // 0x6B7E44 and the NON-parameterized inline sibling path inside
    // Player_advanceRootAndNodes (0x6B73B0, LABEL_88 @0x6B72BC..0x6B7338) execute
    // the SAME source-level node-frame-advance: identical forward seek
    // (0x6B7F14 / break `cur.fi >= count-2 || target < other.time`), identical
    // corrective backward seek, and a field-for-field identical tail —
    // `node+44 = 1` (content-established) → 2× gated Player_mergeFrameContent
    // (node+346/+882) → gated Motion_Player_findSource(node+200, player,
    // activeSlot+356/src, +348/icon) which writes the active slot's
    // done(node+200 +0) / src(texture, +24). The ONLY binary difference: the
    // parameterized path (0x6B7E44) fires NO per-node onAction events; the
    // inline path pushes them in its seek loop (slot mask 0x40000). Verified by
    // decompile (0x6B7E44 full body + 0x6B72BC..0x6B7338 disasm) 2026-06-05.
    //
    // The live faithful reproduction of that shared seek+tail is
    // advanceNodeFrameSelectionLike_0x6926B4 (proven correct: logo 93/243).
    // So the faithful 0x6B7E44 = that shared helper with pendingEvents = nullptr.
    //
    // The earlier decoded parser needed invented non-negative guards because it
    // reset out-of-range frames to a synthetic invisible slot. That premise was
    // removed with the raw frameList parser: this shared loop now keeps the
    // binary's unguarded `other.fi+1` / `cur.fi-1` PropGetByNum calls and relies
    // on the same timeline-data invariant as 0x6B7E44.
    // ------------------------------------------------------------------
    MOTIONPLAYER_NOINLINE void
    advanceNodeFramesLike_0x6B7E44(detail::MotionNode &node, double currentTime) {
        // 0x6B7E90 seek target = *(node+8)+40 = parameterEntry->value. The shared
        // helper recomputes the selection time internally via
        // frameSelectionTimeLike_0x6B7E44. Player_initNodeFields @0x6B3EA0
        // guarantees parameterEntry is non-null exactly for integer
        // `parameterize`, so this matches the binary node+8 split directly.
        // pendingEvents = nullptr: the parameterized path fires no onAction.
        advanceNodeFrameSelectionLike_0x6926B4(node, currentTime, nullptr);
    }

    // M1/P7 step-1: read-only slot consumer for the updateLayers pass.
    // After progressSeekNodeSlotsLike_0x6C106C has positioned node.slots[0/1],
    // updateLayers reads them here — no seek. Mirrors the read half of the old
    // inline seek: uses the same per-node selection time
    // (frameSelectionTimeLike_0x6B7E44) so the debug interpolation ratio matches.
    // Aligned to libkrkr2.so: Player_updateLayers (0x6BB33C) feeds the slots to
    // Player_evaluateTimeline (0x699AE4) without cursor-stepping.
    MOTIONPLAYER_NOINLINE TimelineTraceState
    readNodeFrameSlotsForTrace(detail::MotionNode &node,
                               double currentTime) {
        const double selectionTime =
            frameSelectionTimeLike_0x6B7E44(node, currentTime);
        return traceStateFromNodeSlots(node, selectionTime);
    }

    MOTIONPLAYER_NOINLINE bool
    evaluateTimelineLike_0x699AE4(detail::MotionNode &node,
                                  bool dirtyArg,
                                  double currentTime) {
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
            copyActiveTimelinePayloadLike_0x699B6C(node);
            // type-4 COPY branch (@0x699c2c): node+2224..2288 <- slot+744..808.
            if(node.nodeType == 4) {
                writeParticleInterpCopyLike_0x699c2c(node);
            }
            writeTypeSpecificCopyLike_0x699C2C(node);
            return true;
        }

        if(node.parameterEntry != nullptr) {
            currentTime = node.parameterEntry->value;
        }

        double elapsed = currentTime - active.clipStartTime;
        if(active.ti != 0) {
            elapsed = static_cast<double>(active.ti) *
                static_cast<std::uint32_t>(
                    elapsed / static_cast<double>(active.ti));
        }
        const double ratio = elapsed /
            (other.clipStartTime - active.clipStartTime);
        const double oldRatio = node.timelineEvalRatio;

        if(std::fabs(ratio) < 1.0e-7) {
            node.timelineEvalRatio = ratio;
            if(!dirty && std::fabs(oldRatio - ratio) < 1.0e-7) {
                return false;
            }
            copyActiveTimelinePayloadLike_0x699B6C(node);
            if(node.nodeType == 4) {
                writeParticleInterpCopyLike_0x699c2c(node);
            }
            writeTypeSpecificCopyLike_0x699C2C(node);
            return true;
        }

        if(!dirty &&
           std::fabs(oldRatio - ratio) < std::numeric_limits<double>::epsilon()) {
            return false;
        }
        node.timelineEvalRatio = ratio;
        interpolateTimelinePayloadLike_0x699D80(node, ratio);
        interpolateMeshPayloadLike_0x69A058(node, ratio);
        // type-4 particle eval mirror (binary nodeType==4 switch case). When
        // the ratio is non-zero this is the crossfade INTERP branch @0x69a0f8.
        if(node.nodeType == 4) {
            writeParticleInterpLerpLike_0x69a0f8(node, ratio);
        }
        writeTypeSpecificLerpLike_0x69A0F8(node, ratio);
        return true;
    }
}

namespace motion {
    // M1/P7 step-1: progress-pass per-node frame seek (cursor stepping).
    //
    // Aligned to libkrkr2.so Player_progress_inner (0x6C106C). In the binary the
    // progress core walks the node-deque (player+200, 2632B stride) and, for
    // each node whose child-timeline pointer node+8 is non-null, calls
    // Player_advanceNodeFrames (0x6B7E44) at 0x6C1264 / 0x6C130C; the inline
    // loop bodies start at deque index 1 (`for(j=1; ...; ++j)`), i.e. the root
    // node (index 0) is NOT seeked here (it is reseeded by
    // Player_advanceRootAndNodes / the root-content snapshot path). The seek
    // FILLS each node's two parsed-frame slots (node+320 / node+856) that the
    // SEPARATE Player_updateLayers pass (0x6BB33C) then reads via
    // Player_evaluateTimeline (0x699AE4) at 0x6BB5F0.
    //
    // The live MotionNode::ClipSlot slots[2] ARE the binary's node+320/+856
    // slots, and advanceNodeFrameSelectionLike_0x6926B4 already seeks them using
    // the same per-node selection time (frameSelectionTimeLike_0x6B7E44 picks
    // node.parameterEntry->value for parameterized nodes, else clampedEvalTime =
    // player+456). So this driver is a faithful hoist: it runs the existing live
    // seek in the progress pass (before updateLayers), restoring the binary's
    // two-pass data flow with NO second slot copy.
    //
    // Both incremental directions and the absolute reseed are live:
    //   • forward: Player_advanceRootAndNodes @0x6B6ADC -> inline @0x6B73DC;
    //   • reverse: Player_rewindRootAndNodes @0x6B9A3C -> inline @0x6BA1CC;
    //   • first-frame/loop-wrap: Player_reseekTimelineCursors @0x6B86C8 ->
    //     Player_initNodeTimeline @0x6B64AC for every non-root node.
    // The incremental helpers therefore consume already-seeded slots exactly as
    // the binary does; there is deliberately no port-local lazy initialization.
    void Player::progressSeekNodeSlotsLike_0x6C106C(double clampedEvalTime,
                                                    bool forward) {
        auto &nodes = _nodes;
        if (nodes.empty()) {
            return;
        }
        // Player_progress_inner node-deque loop starts at index 1 (0x6C1288:
        // `for(j=1; ...)`). Root node (index 0) takes the root-stream path, not
        // the per-node advanceNodeFrames seek.
        //
        // UPPER BOUND = `i < nodes.size()` — DO NOT change to `i+1 < size()`,
        // and do NOT add a "sentinel" element to _nodes.
        // The binary exit is `dequeSize - 1 <= idx` (0x6C12D8 / disasm 0x6B7390
        // SUB X9,X9,#1 / 0x6B7398 B.LS). That `-1` is NOT a source-level
        // `size()-1` and there is NO trailing sentinel element. The node deque
        // holds exactly realNodeCount elements (root + N children); ctor
        // @0x6CED30 pushes 1 (root), buildNodeTree_recursive @0x6B4A6C pushes
        // each child — no extra push anywhere. The `-1` cancels a `+1` BIAS that
        // libstdc++ std::deque::size() emits when element>512B → 1-elem/block:
        // the binary computes size via `(start.last-start.cur)/T`, which for
        // 1-elem blocks == `size()+1` (magic 248037625 = 329⁻¹ mod 2³², 329 =
        // 2632/8). So `dequeSize - 1 == real size()`, and the source author
        // almost certainly wrote `i < _nodes.size()` — the local code IS the
        // faithful source, not a mere equivalent. Cross-checked vs
        // Player_buildNodeTree @0x6B51F0 (0x6B531C loop reads index 1..real-1,
        // no past-the-end read). RUNTIME (2026-06-06): `i+1 < size()` regressed
        // yuzulogo 468 mismatches (binary DOES seek the last real node);
        // baseline `i < size()` is byte-exact. Evidence: ida-deep-analyzer
        // project_node_deque_no_sentinel.md. The earlier "trailing sentinel /
        // _nodes not 1:1" comment was a misread of the size() inlining and is
        // corrected here.
        for (size_t i = 1; i < nodes.size(); ++i) {
            detail::MotionNode &node = nodes[i];
            const auto sourceGate = [&]() {
                const int mask = _preview ? 6153 : 6145;
                return node.forceVisible != 0 ||
                    (node.nodeType >= 0 && node.nodeType < 31 &&
                     ((1 << node.nodeType) & mask) != 0);
            };
            const int priorActiveFrame = node.activeSlot().frameIndex;
            const int priorOtherFrame = node.otherSlot().frameIndex;
            // Player_advanceNodeFrames (0x6B7E44) seeks this node's two slots to
            // the node's selection time. The live seek writes node.slots[0/1],
            // node.activeSlotIndex and node.flags |= 1; Player_evaluateTimeline
            // (0x699AE4) consumes those fields and its single node+56 ratio. Return value is only
            // used for tracing in the collapsed model; here we discard it (the
            // slots are the real output, mirroring the binary).
            // 砖5/洞2: per-node onAction(label, action) on crossed action frames
            // (slot mask bit 0x40000 -> ClipSlot.actionValue), matching the inline
            // sub_6B638C push inside Player_advanceRootAndNodes (0x6B6ADC,
            // LABEL_86 / push @0x6B74E4) and Player_rewindRootAndNodes (0x6B9A3C,
            // LABEL_76 / push @0x6BA26C).
            //
            // node+8 GATING (0x6B73B0/0x6B73D4 advance, 0x6BA1A8/0x6BA1C4 rewind):
            // both node loops branch
            //     if (*(node+8)) { Player_advanceNodeFrames(node, player); continue; }
            // i.e. PARAMETERIZED nodes (node+8 = parameterEntry != 0) take the
            // child-advance path (Player_advanceNodeFrames 0x6B7E44), which seeks
            // its two slots but contains NO action-mask check and NO
            // pushActionEvent call — so parameterized nodes fire NO per-node
            // onAction. Only NON-parameterized nodes (node+8 == 0) run the inline
            // seek that pushes the event. node+8 == parameterEntry (MotionNode.h:71,
            // node init 0x6B3EA0). Gate accordingly: parameterized -> no events.
            //
            // P7 convergence step 1: route the PARAMETERIZED branch through the
            // dedicated faithful reproduction of Player_advanceNodeFrames
            // (0x6B7E44) — the exact function the binary caller branches to at
            // LABEL_104 (`if (*(node+8)) { Player_advanceNodeFrames(node,player);
            // continue; }`). Non-parameterized nodes (node+8 == 0) keep the
            // inline 2-slot seek (0x6B73D0..0x6B7338) modelled by
            // advanceNodeFrameSelectionLike_0x6926B4, which fires the per-node
            // onAction events the inline path pushes. This mirrors the binary's
            // node+8 split precisely instead of conflating both into one helper
            // gated only by the selection time.
            if(node.parameterEntry != nullptr) {
                // node+8 != 0 -> Player_advanceNodeFrames (0x6B7E44). NO events.
                // Same forward+corrective-backward seek in both play directions.
                advanceNodeFramesLike_0x6B7E44(node, clampedEvalTime);
                if(sourceGate() &&
                   (node.activeSlot().frameIndex != priorActiveFrame ||
                    node.otherSlot().frameIndex != priorOtherFrame)) {
                    findSourceForNodeLike_0x6948E8(node);
                }
                continue;
            }
            // node+8 == 0 -> single-direction inline seek WITH events: forward
            // inline (0x6B73DC, advanceRootAndNodes) for forward playback,
            // backward inline (0x6BA1CC, rewindRootAndNodes) for reverse. The
            // binary runs exactly one direction's loop here; the forward inline
            // has NO corrective-backward (loop-wrap repositioning is done by
            // reseekNodeTimelineSlotsLike_0x6B91B0 in reseekTimelineCursors).
            if(forward) {
                advanceNodeFrameForwardInlineSeekLike_0x6B73DC(
                    node, clampedEvalTime, &_pendingEvents);
            } else {
                advanceNodeFrameBackwardInlineSeekLike_0x6BA1CC(
                    node, clampedEvalTime, &_pendingEvents);
            }
            if(sourceGate() &&
               (node.activeSlot().frameIndex != priorActiveFrame ||
                node.otherSlot().frameIndex != priorOtherFrame)) {
                findSourceForNodeLike_0x6948E8(node);
            }
        }
    }

    // Player_reseekTimelineCursors node-deque re-seed loop @0x6B91B0 (STEP 4).
    // ABSOLUTE two-slot re-seed of every non-root node to its target-bracketing
    // frame, independent of the prior cursor:
    //   for(m=1; m<dequeSize-1; ++m) Player_initNodeTimeline(player, node[m]);
    // Player_initNodeTimeline (0x6B64AC) parses slot[0]=frame(v19) +
    // slot[1]=frame(v19+1) (v19=min(scan(target),count-2)), merges both, sets
    // activeSlotIndex(+1392)=0 and seeded(+44)=1; selection target per-node is
    // (*(node+8)) ? *(node+8)+40 : player+456 — computed inside
    // initializeNodeTimelineSlotsLike_0x6B64AC via frameSelectionTimeLike_0x6B7E44.
    // This is what makes the loop-wrap path's subsequent FORWARD-ONLY inline seek
    // sufficient (no corrective-backward needed). The binary breaks at
    // m == dequeSize-1; this `-1` is a libstdc++ std::deque::size() inlining
    // BIAS (element>512B → 1-elem/block → size computed as `size()+1`), NOT a
    // trailing sentinel. The node deque holds exactly realNodeCount elements,
    // so `dequeSize - 1 == real size()` and the proven node-walk range is
    // `i < nodes.size()` (matches progressSeekNodeSlotsLike). UPPER BOUND
    // verified vs fresh-decompile 0x6B9200 (`... - 1 <= m` -> break) — same
    // size() inlining exit term as all node-walks; the `-1` cancels the +1 bias,
    // NOT a sentinel or the last real node. (See progressSeekNodeSlotsLike_0x6C106C
    // for the full mechanism; do NOT change to `i+1 < size()`, do NOT push a
    // sentinel.)
    // The 0x6B9234 pruneHM3 / 0x6B9650 aux-list tail is housekeeping (inert on
    // node slots) and stays DEFERRED. (Only reached at loop-wrap, which the logo
    // cases never hit — empirically reseekTimelineCursors is never called for
    // m2logo.)
    void Player::reseekNodeTimelineSlotsLike_0x6B91B0(double targetTime) {
        auto &nodes = _nodes;
        for (size_t i = 1; i < nodes.size(); ++i) {
            detail::MotionNode &node = nodes[i];
            // 砖5/洞2: Player_initNodeTimeline (0x6B64AC) fires a per-node onAction
            // in its tail @0x6B674C when the re-seed lands exactly on an action
            // frame; pass _pendingEvents so reseekTimelineCursors' node re-seed
            // (the binary's @0x6B91B0 loop) reproduces those onAction pushes.
            if(initializeNodeTimelineSlotsLike_0x6B64AC(
                   node, targetTime, &_pendingEvents)) {
                const int mask = _preview ? 6153 : 6145;
                if(node.forceVisible != 0 ||
                   (node.nodeType >= 0 && node.nodeType < 31 &&
                    ((1 << node.nodeType) & mask) != 0)) {
                    findSourceForNodeLike_0x6948E8(node);
                }
            }
        }
    }

    // 砖5/洞1: Player_preProgressDirtyNodes (0x6B6878) — progress_inner's first
    // step (called at 0x6C10AC, before the firstFrame/cursor logic). For each
    // node (deque idx >= 1) whose forceVisible (node+1996) != 0 and whose
    // emoteEdit dispatch (node+1980) has "modified" set: clear the flag and rebuild
    // the node's two timeline slots via initializeNodeTimelineSlotsLike_0x6B64AC
    // (= Player_initNodeTimeline_guess 0x6B64AC at 0x6B6A1C).
    //
    // The raw emoteEdit variant is mutable TJS state; 0x6B6A08 clears modified
    // with PropSet(TJS_MEMBERENSURE) before rebuilding the two slots.
    void Player::preProgressDirtyNodesLike_0x6B6878() {
        // UPPER BOUND = `i < _nodes.size()` — do NOT change to `i+1 < size()`,
        // do NOT push a sentinel. Fresh-decompile 0x6B6920 (`... - 1 <= v2` ->
        // return; v2 starts at 1) is the SAME libstdc++ std::deque::size()
        // inlining BIAS as the other 3 node-walks (progress_inner 0x6C12D8 /
        // advanceRootAndNodes 0x6B7398 / reseek 0x6B9200). The `-1` cancels the
        // `+1` bias that size() emits for >512B 1-elem/block deques — there is NO
        // trailing sentinel; dequeSize == realNodeCount. The walk covers all real
        // nodes [1, realNodeCount-1] == `i < _nodes.size()`. (Full mechanism +
        // runtime proof in progressSeekNodeSlotsLike_0x6C106C.)
        for (size_t i = 1; i < _nodes.size(); ++i) {
            detail::MotionNode &node = _nodes[i];
            if (node.forceVisible == 0 ||
                !rawDispatchObject(node.emoteEditVariant)) { // node+1996/+1980
                continue;
            }
            // sub_6636D4(emoteEdit, "modified") — 0x6B69C0.
            const bool modified = detail::motionPropGetBool(
                node.emoteEditVariant, TJS_W("modified"));
            if (!modified) {
                continue;
            }
            tTJSVariant zero(static_cast<tjs_int>(0));
            iTJSDispatch2 *emoteEdit =
                node.emoteEditVariant.AsObjectNoAddRef();
            (void)emoteEdit->PropSet(
                TJS_MEMBERENSURE, TJS_W("modified"), nullptr,
                &zero, emoteEdit);
            // Player_initNodeTimeline_guess(player, node) — 0x6B6A1C.
            // 砖5/洞2: the dirty-node rebuild is a direct Player_initNodeTimeline
            // (0x6B64AC) call, so its tail @0x6B674C onAction push fires here too;
            // pass _pendingEvents to reproduce it.
            if(initializeNodeTimelineSlotsLike_0x6B64AC(
                   node, _clampedEvalTime, &_pendingEvents)) {
                const int mask = _preview ? 6153 : 6145;
                if(node.forceVisible != 0 ||
                   (node.nodeType >= 0 && node.nodeType < 31 &&
                    ((1 << node.nodeType) & mask) != 0)) {
                    findSourceForNodeLike_0x6948E8(node);
                }
            }
        }
    }

    // Phase 1: Camera velocity, root evaluation, variable interpolation
    void Player::updateLayersPhase1_PreLoop(double currentTime) {
        auto &nodes = _nodes;
        // === PHASE 1: Pre-loop setup ===

        // Camera velocity → root delta block (0x6BB360..0x6BB3DC).
        // Writes node+1584 (delta.dirty) and node+1592/+1600/+1608 (delta pos).
        // Reads player+592 = _deltaTime (speedMul·dt): 0x6BB37C/3A4/3CC each
        // `LDR D1,[X19,#0x250]; FMUL D0,D1,velXYZ`. (Was _frameLastTime, a
        // port-invented raw-dt field with no binary backing — see Player.h.)
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
            // Camera friction (0x6BB3E0..0x6BB428): pow(damp, player+592/60.0).
            // Gate is ONLY `damp != 1.0` (0x6BB3EC FCMP D0,#1.0) — the binary has
            // no `>0` subgate; the former `&& _frameLastTime > 0.0` was a port
            // invention. Reads +592=_deltaTime (0x6BB3F4 LDR D1,[X19,#0x250]).
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
            // Player_buildNodeTree @0x6B51F0 always retains node 0 as the
            // synthetic root. Player_updateLayers @0x6BB4D4 copies its delta
            // block directly; no PSB layer/frame dispatch is evaluated here.
            root.delta.flipX = _rootFlipX;

            // Aligned to libkrkr2.so 0x6BB4E0..0x6BB4E8:
            //   memcpy(root+1504, root+1584, 0x50); *(root+1584) = 0;
            copyDeltaBlockToAccum(root.accumulated, root.delta);
            root.accumulated.blendMode = 16;
            root.delta.dirty = false;
            const std::array<std::uint32_t, 4> rootColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u};
            copyPackedColorsToBytes(root.colorBytes, rootColors);

        }

        // Player_updateLayers @0x6BB33C calls this unconditionally at
        // 0x6BB4EC, immediately after copying/clearing the root delta block.
        // Player_interpolateVarTrackValues @0x6BBE20 walks Player+1296's
        // VariableLabelScope deque, writes item+16 and binds each live value.
        interpolateVarTrackValuesLike_0x6BBE20(_clampedEvalTime);

        // 0x6BB4F0 calls sub_699940 only when Player+908 is zero.  A type-3
        // child's root 2x2 was already propagated by the parent motion pass;
        // particle children retain the ctor's zero flag and rebuild normally.
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
        const std::string motionPath = matchedMotionPath();
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

            if (detail::logoChainTraceEnabledForPath(motionPath)) {
                const auto &parentNode = nodes[parentIdx];
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.parent_lookup", "0x6BB598",
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

            // M1/P7 step-1: the per-node frame seek no longer runs here. In
            // libkrkr2.so Player_updateLayers (0x6BB33C) does NOT cursor-step;
            // the node's two parsed-frame slots (node+320/+856) were already
            // filled by the progress pass (Player_progress_inner 0x6C106C ->
            // Player_advanceNodeFrames 0x6B7E44, hoisted to
            // Player::progressSeekNodeSlotsLike_0x6C106C). Here we only READ the
            // already-positioned live ClipSlots — exactly what the binary's
            // Player_evaluateTimeline (0x699AE4) consumes. The following object
            // is a diagnostic-only projection built at this logging boundary.
            auto state = readNodeFrameSlotsForTrace(node, currentTime);
            if (detail::logoChainTraceEnabledForPath(motionPath)
                && state.debugEvaluated) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.framesel",
                    "0x6926B4", currentTime,
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

            const bool forceDirty = false;
            const bool needGround = node.groundCorrection;
            const bool parentDirty = parent.accumulated.dirty;
            const bool deltaDirty = node.delta.dirty;
            // Player_updateLayers @ 0x6BB5E0 passes only the explicit
            // dirty sources as a2; node+44 is folded in inside
            // Player_evaluateTimeline itself.
            const bool timelineDirtyArg =
                forceDirty || needGround || parentDirty || deltaDirty;

            const bool evalRet = evaluateTimelineLike_0x699AE4(
                    node, timelineDirtyArg, currentTime);
            if (!evalRet) {
                continue;
            }

            // Player_updateLayers clears node+1584 after evaluateTimeline but
            // keeps the active/visible override bytes intact.
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

            if (node.activeSlot().hasSync) {
                node.accumulated.dirty = parent.accumulated.dirty;
                node.accumulated.active = parent.accumulated.active;
                node.accumulated.visible = parent.accumulated.visible;
                node.accumulated.flipX = parent.accumulated.flipX;
                node.accumulated.flipY = parent.accumulated.flipY;
                node.accumulated.posX = parent.accumulated.posX;
                node.accumulated.posY = parent.accumulated.posY;
                node.accumulated.posZ = parent.accumulated.posZ;
                node.accumulated.angle = parent.accumulated.angle;
                node.accumulated.scaleX = parent.accumulated.scaleX;
                node.accumulated.scaleY = parent.accumulated.scaleY;
                node.accumulated.slantX = parent.accumulated.slantX;
                node.accumulated.slantY = parent.accumulated.slantY;
                node.accumulated.opacity = parent.accumulated.opacity;
                const bool postDirty = node.accumulated.dirty;
                const bool postVisible = node.accumulated.visible;
                node.accumulated.active = false;
                node.accumulated.dirty = postDirty ? true : (node.flags != 0);
                node.accumulated.visible =
                    postVisible ? node.delta.visibleOverride : false;
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
                sub_69AE74_meshDeform(parent, node);
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

            sub_6BAA10_groundCorrection(node, parent);

            {
                const int v46 = node.inheritFlags;
                if ((v46 & 0x400) != 0) {
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

            if (detail::logoChainTraceEnabledForPath(motionPath)) {
                detail::logoChainTraceLogf(
                    motionPath, "updateLayers.phase2.accum_final", "0x6BBB6C",
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
