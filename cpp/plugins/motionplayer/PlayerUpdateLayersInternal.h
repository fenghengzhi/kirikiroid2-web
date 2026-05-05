#pragma once

#include "PlayerInternal.h"
#include "MotionTraceWeb.h"
#include "ncbind.hpp"    // ncbInstanceAdaptor<Player>::CreateAdaptor for TJS bridge
#include "tjsArray.h"    // TJSCreateArrayObject, TJSGetArrayElementCount
#ifdef __EMSCRIPTEN__
#include <wasm_simd128.h>
#endif

#if defined(__clang__) || defined(__GNUC__)
#define MOTIONPLAYER_NOINLINE __attribute__((noinline))
#else
#define MOTIONPLAYER_NOINLINE
#endif

using namespace motion::internal;

namespace {
    inline void copyPackedColorsToBytes(
        uint8_t (&colorBytes)[16],
        const std::array<std::uint32_t, 4> &packedColors) {
        std::memcpy(colorBytes, packedColors.data(), sizeof(std::uint32_t) * 4u);
    }

    inline std::array<std::uint32_t, 4> copyPackedColorsFromBytes(
        const uint8_t (&colorBytes)[16]) {
        std::array<std::uint32_t, 4> packedColors{};
        std::memcpy(packedColors.data(), colorBytes,
                    sizeof(std::uint32_t) * packedColors.size());
        return packedColors;
    }

    inline std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    template <typename StateT>
    inline void populateTimelinePayloadFromFrameState(
        StateT &localState,
        const motion::internal::FrameContentState &state) {
        localState.flipX = state.flipX;
        localState.flipY = state.flipY;
        localState.posX = state.x;
        localState.posY = state.y;
        localState.posZ = state.z;
        localState.angle = state.angle;
        localState.scaleX = state.scaleX;
        localState.scaleY = state.scaleY;
        localState.slantX = state.slantX;
        localState.slantY = state.slantY;
        localState.opacity = static_cast<int>(
            std::clamp(state.opacity * 255.0, 0.0, 255.0));
        localState.blendMode = state.blendMode;
    }

    template <typename StateT>
    inline void populateTransformStateFromFrameState(
        StateT &localState,
        const motion::internal::FrameContentState &state) {
        localState.visible = state.visible;
        localState.active = state.visible;
        populateTimelinePayloadFromFrameState(localState, state);
    }

    inline void populateDeltaStateFromFrameState(
        motion::detail::MotionNode::DeltaState &delta,
        const motion::internal::FrameContentState &state) {
        delta.activeOverride = state.visible;
        delta.visibleOverride = state.visible;
        delta.flipX = state.flipX;
        delta.flipY = state.flipY;
        delta.posX = state.x;
        delta.posY = state.y;
        delta.posZ = state.z;
        delta.angle = state.angle;
        delta.scaleX = state.scaleX;
        delta.scaleY = state.scaleY;
        delta.slantX = state.slantX;
        delta.slantY = state.slantY;
        delta.opacity = static_cast<int>(
            std::clamp(state.opacity * 255.0, 0.0, 255.0));
    }

    inline void neutralizeDeltaTransformOverrides(
        motion::detail::MotionNode::DeltaState &delta) {
        delta.flipX = false;
        delta.flipY = false;
        delta.posX = 0.0;
        delta.posY = 0.0;
        delta.posZ = 0.0;
        delta.angle = 0.0;
        delta.scaleX = 1.0;
        delta.scaleY = 1.0;
        delta.slantX = 0.0;
        delta.slantY = 0.0;
        delta.opacity = 255;
    }

    inline void copyDeltaBlockToAccum(
        motion::detail::MotionNode::AccumulatedState &accum,
        const motion::detail::MotionNode::DeltaState &delta) {
        accum.dirty = delta.dirty;
        accum.active = delta.activeOverride;
        accum.visible = delta.visibleOverride;
        accum.flipX = delta.flipX;
        accum.flipY = delta.flipY;
        accum.posX = delta.posX;
        accum.posY = delta.posY;
        accum.posZ = delta.posZ;
        accum.angle = delta.angle;
        accum.scaleX = delta.scaleX;
        accum.scaleY = delta.scaleY;
        accum.slantX = delta.slantX;
        accum.slantY = delta.slantY;
        accum.opacity = delta.opacity;
    }

    inline void refreshSourceGeometryFromSourceName(
        motion::detail::MotionNode &node,
        const std::shared_ptr<motion::detail::MotionSnapshot> &snapshot,
        const std::string &sourceName) {
        if (!snapshot || sourceName.empty()) {
            return;
        }
        int srcW = 0;
        int srcH = 0;
        double srcOX = 0.0;
        double srcOY = 0.0;
        std::vector<std::uint8_t> decomp;
        findPSBResourceBySourceName(*snapshot, sourceName, srcW, srcH, decomp,
                                    srcOX, srcOY);
        node.clipW = srcW;
        node.clipH = srcH;
        node.originX = srcOX;
        node.originY = srcOY;
    }

    // Populate a ClipSlot from a FrameContentState.
    // Cannot be a ClipSlot method because FrameContentState is defined in
    // PlayerInternal.h (motion::internal namespace) which MotionNode.h cannot include.
    inline void populateSlotFromState(
        motion::detail::MotionNode::ClipSlot &slot,
        const motion::internal::FrameContentState &s) {
        slot.done = !s.visible;
        slot.frameIndex = s.debugActiveIndex;
        slot.frameType = s.frameType;
        slot.src = s.src;
        slot.srcList = s.srcList;
        slot.x = s.x; slot.y = s.y; slot.z = s.z;
        slot.ox = s.ox; slot.oy = s.oy;
        slot.width = s.width; slot.height = s.height;
        slot.opacity = s.opacity; slot.angle = s.angle;
        slot.scaleX = s.scaleX; slot.scaleY = s.scaleY;
        slot.slantX = s.slantX; slot.slantY = s.slantY;
        slot.flipX = s.flipX; slot.flipY = s.flipY;
        slot.blendMode = s.blendMode;
        slot.packedColors = s.packedColors;
        slot.ccc.x = s.ccc.x; slot.ccc.y = s.ccc.y;
        slot.acc.x = s.acc.x; slot.acc.y = s.acc.y;
        slot.zcc.x = s.zcc.x; slot.zcc.y = s.zcc.y;
        slot.scc.x = s.scc.x; slot.scc.y = s.scc.y;
        slot.occ.x = s.occ.x; slot.occ.y = s.occ.y;
        slot.cc.x = s.cc.x; slot.cc.y = s.cc.y;
        slot.cp.x = s.cp.x; slot.cp.y = s.cp.y; slot.cp.t = s.cp.t;
        slot.hasCpRotation = !s.cp.empty();
        slot.clipStartTime = s.clipStartTime;
        slot.motionDt = s.motionDt; slot.motionFlags = s.motionFlags;
        slot.motionDofst = s.motionDofst; slot.motionDocmpl = s.motionDocmpl;
        slot.motionTimeOffset = s.motionTimeOffset; slot.motionDtgt = s.motionDtgt;
        slot.prtTrigger = s.prtTrigger;
        slot.prtFmin = s.prtFmin; slot.prtF = s.prtF;
        slot.prtVmin = s.prtVmin; slot.prtV = s.prtV;
        slot.prtAmin = s.prtAmin; slot.prtA = s.prtA;
        slot.prtZmin = s.prtZmin; slot.prtZ = s.prtZ;
        slot.prtRange = s.prtRange;
        slot.hasTransformOrder = s.hasTransformOrder;
        std::copy(s.transformOrder, s.transformOrder + 4, slot.transformOrder);
        slot.action = s.action; slot.hasSync = s.hasSync;
        // hasEasing derived from acc curve presence
        slot.hasEasing = !s.acc.empty();
    }

    // Flatten a PSB layer node tree into a list of render nodes.
    // Aligned to libkrkr2.so sub_6C4E28: converts tree into flat list
    // with pre-computed positions for the sub_6C7440 render loop.
    // Aligned to libkrkr2.so: full 2x3 affine [m11,m21,m12,m22,tx,ty]
    using Affine2x3 = std::array<double, 6>;

    // Compose: result = parent * Translate(lx, ly)
    inline Affine2x3 affineTranslate(const Affine2x3 &p, double lx, double ly) {
        return {p[0], p[1], p[2], p[3],
                p[0]*lx + p[2]*ly + p[4],
                p[1]*lx + p[3]*ly + p[5]};
    }

    // Compose: result = a * Scale(sx, sy)
    inline Affine2x3 affineScale(const Affine2x3 &a, double sx, double sy) {
        return {a[0]*sx, a[1]*sx, a[2]*sy, a[3]*sy, a[4], a[5]};
    }

    // Compose: result = a * Rotate(angleDeg)
    // Aligned to libkrkr2.so Player_updateLayers 2x2 matrix multiply
    inline Affine2x3 affineRotate(const Affine2x3 &a, double angleDeg) {
        if(angleDeg == 0.0) return a;
        const double rad = angleDeg * 3.14159265358979323846 / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        // Rotation matrix R = [c -s; s c]
        // A * R: new_m11 = a.m11*c + a.m12*s, new_m12 = -a.m11*s + a.m12*c
        return {a[0]*c + a[2]*s, a[1]*c + a[3]*s,
                -a[0]*s + a[2]*c, -a[1]*s + a[3]*c,
                a[4], a[5]};
    }

    // Build local 2x2 matrix and right-multiply into affine.
    // Exactly replicates libkrkr2.so sub_699940 (0x699940):
    //   Starts from identity, LEFT-multiplies transforms in order
    //   [0=Flip, 1=Angle, 2=Zoom, 3=Slant] (default transformOrder).
    //   Then composes: affine = affine × local_2x2
    //
    // sub_699940 variable mapping (verified from decompilation):
    //   v5→m11(+120), v6→m12(+128), v4→m21(+136), v7→m22(+144)
    //   case 0 flipX: negate v5,v6 (row1) = left-multiply [-1,0;0,1]
    //   case 0 flipY: negate v4,v7 (row2) = left-multiply [1,0;0,-1]
    //   case 1 angle: left-multiply [cos,-sin;sin,cos]
    //   case 2 zoom:  left-multiply [zoomX,0;0,zoomY]
    //   case 3 slant: left-multiply [1,slantX;slantY,1]
    inline void applyLocalTransform(
        Affine2x3 &a,
        bool flipX,
        bool flipY,
        double angle,
        double scaleX,
        double scaleY,
        double slantX,
        double slantY,
        const int (&transformOrder)[4]) {
        // Build local 2x2 from identity via left-multiplication.
        // Exactly replicates sub_699940 (0x699940): iterates
        // transformOrder[0..3] and applies each transform case.
        // Default order [0,1,2,3] = [Flip, Angle, Zoom, Slant].
        double l11 = 1.0, l12 = 0.0, l21 = 0.0, l22 = 1.0;

        for(int step = 0; step < 4; step++) {
            const int op = transformOrder[step];
            switch(op) {
                case 0: // Flip (left-multiply [-1,0;0,1] / [1,0;0,-1])
                    if(flipX) { l11 = -l11; l12 = -l12; }
                    if(flipY) { l21 = -l21; l22 = -l22; }
                    break;
                case 1: // Angle (left-multiply [c,-s;s,c])
                    if(angle != 0.0) {
                        const double rad =
                            angle * 2.0 * 3.14159265358979323846 / 360.0;
                        const double c = std::cos(rad);
                        const double s = std::sin(rad);
                        const double t11 = c*l11 - s*l21;
                        const double t12 = c*l12 - s*l22;
                        const double t21 = s*l11 + c*l21;
                        const double t22 = s*l12 + c*l22;
                        l11 = t11; l12 = t12; l21 = t21; l22 = t22;
                    }
                    break;
                case 2: // Zoom (left-multiply [zx,0;0,zy]) — 0x699A50
                    if(scaleX != 1.0 || scaleY != 1.0) {
                        l11 *= scaleX; l12 *= scaleX;
                        l21 *= scaleY; l22 *= scaleY;
                    }
                    break;
                case 3: // Slant (left-multiply [1,sx;sy,1]) — 0x699A7C
                    if(slantX != 0.0 || slantY != 0.0) {
                        const double t12 = l22*slantX + l12;
                        const double t21 = l11*slantY + l21;
                        const double t22 = l22 + l12*slantY;
                        const double t11 = l11 + slantX*l21;
                        l11 = t11; l12 = t12; l21 = t21; l22 = t22;
                    }
                    break;
            }
        }

        // Right-multiply local 2x2 into affine: A_new = A × L
        // (tx,ty unchanged; only 2x2 part is affected)
        const double m11 = a[0]*l11 + a[2]*l21;
        const double m21 = a[1]*l11 + a[3]*l21;
        const double m12 = a[0]*l12 + a[2]*l22;
        const double m22 = a[1]*l12 + a[3]*l22;
        a[0] = m11; a[1] = m21; a[2] = m12; a[3] = m22;
    }

    inline void applyLocalTransform(Affine2x3 &a,
                                    const FrameContentState &state) {
        applyLocalTransform(a,
                            state.flipX, state.flipY,
                            state.angle,
                            state.scaleX, state.scaleY,
                            state.slantX, state.slantY,
                            state.transformOrder);
    }

    // sub_699940 (0x699940) rebuilds the local 2x2 from node fields
    // after Player_updateLayers has already applied inheritFlags.
    inline void applyLocalTransform(Affine2x3 &a,
                                    const motion::detail::MotionNode &node) {
        applyLocalTransform(a,
                            node.accumulated.flipX,
                            node.accumulated.flipY,
                            node.accumulated.angle,
                            node.accumulated.scaleX,
                            node.accumulated.scaleY,
                            node.accumulated.slantX,
                            node.accumulated.slantY,
                            node.transformOrder);
    }

    // sub_69AE74 @ 0x69AE74 — mesh-surface deformation of child position
    // and optional angle/scale from the gradient/jacobian of the parent's
    // Bezier patch. Called from Player_updateLayers at 0x6BB714 when
    // parent.meshType != 0. The patch is a 4×4 grid of control points stored
    // at parent+2024 (32 floats). Operates in parent's normalized (u,v)
    // coordinates then maps back to world pixel space.
    inline void sub_69AE74_meshDeform(
        const motion::detail::MotionNode &parent,
        motion::detail::MotionNode &node) {
        if (parent.meshType != 1 || (parent.meshFlags & 1) == 0
            || !parent.accumulated.active || !parent.hasSource
            || parent.meshControlPoints.empty())
            return;
        const double slotOX = parent.activeSlot().ox;
        const double slotOY = parent.activeSlot().oy;
        const double totalOX = slotOX + parent.originX;
        const double totalOY = slotOY + parent.originY;
        const double pw = parent.clipW > 0.0 ? parent.clipW : 1.0;
        const double ph = parent.clipH > 0.0 ? parent.clipH : 1.0;
        const double childSecondary =
            parent.coordinateMode != 0
                ? node.accumulated.posZ
                : node.accumulated.posY;
        const double normX = (node.accumulated.posX + totalOX) / pw;
        const double normY = (childSecondary + totalOY) / ph;

        auto evalBezierPatch = [](const float *mesh, float u, float v,
                                  float &outX, float &outY) {
            const float su = 1.0f - u, sv = 1.0f - v;
            const float bu[4] = {
                su*su*su, 3.0f*su*su*u, 3.0f*su*u*u, u*u*u
            };
            const float bv[4] = {
                sv*sv*sv, 3.0f*sv*sv*v, 3.0f*sv*v*v, v*v*v
            };
            outX = 0;
            outY = 0;
            for (int i = 0; i < 16; ++i) {
                float w = bv[i >> 2] * bu[i & 3];
                outX += mesh[i * 2] * w;
                outY += mesh[i * 2 + 1] * w;
            }
        };

        float defX = static_cast<float>(normX);
        float defY = static_cast<float>(normY);
        if (parent.meshControlPoints.size() >= 32) {
            evalBezierPatch(parent.meshControlPoints.data(), defX, defY,
                            defX, defY);
        }
        node.accumulated.posX = static_cast<double>(defX) * pw - totalOX;
        if (parent.coordinateMode != 0) {
            node.accumulated.posZ = static_cast<double>(defY) * ph - totalOY;
        } else {
            node.accumulated.posY = static_cast<double>(defY) * ph - totalOY;
        }

        if ((parent.meshFlags & 2) != 0
            && (node.inheritFlags & 0x10) != 0
            && parent.meshControlPoints.size() >= 32) {
            const float eps = 0.0001f;
            const float *mp = parent.meshControlPoints.data();
            float x1, y1, x2, y2, x3, y3, x4, y4;
            evalBezierPatch(mp, defX - eps, defY, x1, y1);
            evalBezierPatch(mp, defX + eps, defY, x2, y2);
            evalBezierPatch(mp, defX, defY - eps, x3, y3);
            evalBezierPatch(mp, defX, defY + eps, x4, y4);
            double a1 = std::atan2(static_cast<double>(y3 - y4),
                                   static_cast<double>(x4 - x3));
            double a2 = std::atan2(static_cast<double>(x2 - x1),
                                   static_cast<double>(y2 - y1));
            node.accumulated.angle +=
                (a1 + a2) * 0.5 * 360.0 / 6.28318531;
        }

        if ((parent.meshFlags & 4) != 0
            && (node.inheritFlags & 0x60) != 0
            && parent.meshControlPoints.size() >= 32) {
            const float eps = 0.0001f;
            const float *mp = parent.meshControlPoints.data();
            float x1, y1, x2, y2, x3, y3, x4, y4;
            evalBezierPatch(mp, defX - eps, defY, x1, y1);
            evalBezierPatch(mp, defX + eps, defY, x2, y2);
            evalBezierPatch(mp, defX, defY - eps, x3, y3);
            evalBezierPatch(mp, defX, defY + eps, x4, y4);
            double dx1 = x2 - x1;
            double dy1 = y2 - y1;
            double area1 =
                std::fabs(dx1 * (y4 - y1) - dy1 * (x4 - x1)) * 0.5;
            double area2 =
                std::fabs(dx1 * (y3 - y1) - dy1 * (x3 - x1)) * 0.5;
            double scaleFactor =
                std::sqrt(area1 + area2 + area2 + area1) / 0.0002;
            if (node.inheritFlags & 0x020)
                node.accumulated.scaleX *= scaleFactor;
            if (node.inheritFlags & 0x040)
                node.accumulated.scaleY *= scaleFactor;
        }
    }

    // sub_6BAA10 @ 0x6BAA10 — onGroundCorrection TJS callback.
    inline void sub_6BAA10_groundCorrection(
        motion::detail::MotionNode &node,
        const motion::detail::MotionNode &parent) {
        if (!node.groundCorrection || !node.tjsLayerObject) {
            return;
        }
        auto *tjsObj = static_cast<iTJSDispatch2 *>(node.tjsLayerObject);
        try {
            iTJSDispatch2 *parentArr = TJSCreateArrayObject();
            tTJSVariant pxv(parent.accumulated.posX);
            tTJSVariant pyv(parent.accumulated.posY);
            tTJSVariant pzv(parent.accumulated.posZ);
            tTJSVariant *pargs[] = { &pxv };
            parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs,
                                parentArr);
            pargs[0] = &pyv;
            parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs,
                                parentArr);
            pargs[0] = &pzv;
            parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs,
                                parentArr);

            iTJSDispatch2 *childArr = TJSCreateArrayObject();
            tTJSVariant cxv(node.accumulated.posX);
            tTJSVariant cyv(node.accumulated.posY);
            tTJSVariant czv(node.accumulated.posZ);
            tTJSVariant *cargs[] = { &cxv };
            childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs,
                               childArr);
            cargs[0] = &cyv;
            childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs,
                               childArr);
            cargs[0] = &czv;
            childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs,
                               childArr);

            tTJSVariant parentVar(parentArr, parentArr);
            tTJSVariant childVar(childArr, childArr);
            tTJSVariant *callArgs[] = { &parentVar, &childVar };
            tTJSVariant result;
            tjsObj->FuncCall(0, TJS_W("onGroundCorrection"), nullptr, &result,
                             2, callArgs, tjsObj);

            if (result.Type() == tvtObject) {
                iTJSDispatch2 *resObj = result.AsObjectNoAddRef();
                if (resObj) {
                    tTJSVariant rv;
                    resObj->PropGetByNum(0, 0, &rv, resObj);
                    node.accumulated.posX = static_cast<double>(rv);
                    resObj->PropGetByNum(0, 1, &rv, resObj);
                    node.accumulated.posY = static_cast<double>(rv);
                    resObj->PropGetByNum(0, 2, &rv, resObj);
                    node.accumulated.posZ = static_cast<double>(rv);
                }
            }
            parentArr->Release();
            childArr->Release();
        } catch (...) {
            // TJS callback failure — silently ignore
        }
    }
} // anonymous namespace

    // Helper: find node by label in the node tree (sub_6F2228 equivalent)
    // Aligned to sub_6F2228: std::map<ttstr,int> lookup at player+24.
    // Binary uses red-black tree traversal with wcscmp; we use std::map::find.
    static int findNodeByLabel(const std::map<std::string, int> &labelMap,
                               const std::string &label) {
        auto it = labelMap.find(label);
        return (it != labelMap.end()) ? it->second : -1;
    }
