// PlayerUpdateLayers.cpp — updateLayers 3-phase pipeline + extracted sub-phase methods
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#ifdef __EMSCRIPTEN__
#include <wasm_simd128.h>
#endif

using namespace motion::internal;

namespace motion {

    // Phase 1: Camera velocity, root evaluation, variable interpolation
    void Player::updateLayersPhase1_PreLoop(double currentTime) {
        auto &nodes = _runtime->nodes;
        // === PHASE 1: Pre-loop setup ===

        // Camera velocity → root node position (0x6BB360..0x6BB42C)
        // In libkrkr2.so this modifies root node+1592/1600/1608 (posX/Y/Z) before
        // prevPos save. Applied here to root accumulated state.
        {
            auto &rootNode = nodes[0];
            if (_cameraVelocityX != 0.0)
                rootNode.accumulated.posX += _frameLastTime * _cameraVelocityX;
            if (_cameraVelocityY != 0.0)
                rootNode.accumulated.posY += _frameLastTime * _cameraVelocityY;
            if (_cameraVelocityZ != 0.0)
                rootNode.accumulated.posZ += _frameLastTime * _cameraVelocityZ;
            // Camera friction (0x6BB3E0..0x6BB428)
            if (_cameraDamping != 1.0 && _frameLastTime > 0.0) {
                const double dampFactor = std::pow(_cameraDamping,
                                                    _frameLastTime / 60.0);
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
            auto rootState = evaluateLayerContent(root.psbNode, currentTime);
            // slotDone: type=0 in evaluateLayerContent → visible=false → done
            root.slotDone = !rootState.visible;
            // Map to accumulated state
            root.accumulated.visible = rootState.visible;
            root.accumulated.flipX = rootState.flipX;
            root.accumulated.flipY = rootState.flipY;
            root.accumulated.posX = rootState.x + rootState.ox;
            root.accumulated.posY = rootState.y + rootState.oy;
            root.accumulated.posZ = 0.0;
            root.accumulated.angle = rootState.angle;
            root.accumulated.scaleX = rootState.scaleX;
            root.accumulated.scaleY = rootState.scaleY;
            root.accumulated.slantX = rootState.slantX;
            root.accumulated.slantY = rootState.slantY;
            root.accumulated.opacity = static_cast<int>(
                std::clamp(rootState.opacity * 255.0, 0.0, 255.0));
            root.accumulated.active = true;
            // Cache interpolated data for rendering
            root.interpolatedCache.src = rootState.src;
            root.interpolatedCache.width = rootState.width;
            root.interpolatedCache.height = rootState.height;
            root.interpolatedCache.opacity = rootState.opacity;
            root.interpolatedCache.x = rootState.x;
            root.interpolatedCache.y = rootState.y;
            root.interpolatedCache.ox = rootState.ox;
            root.interpolatedCache.oy = rootState.oy;
            root.interpolatedCache.angle = rootState.angle;
            root.interpolatedCache.scaleX = rootState.scaleX;
            root.interpolatedCache.scaleY = rootState.scaleY;
            root.interpolatedCache.slantX = rootState.slantX;
            root.interpolatedCache.slantY = rootState.slantY;
            root.interpolatedCache.flipX = rootState.flipX;
            root.interpolatedCache.flipY = rootState.flipY;
            root.interpolatedCache.blendMode = rootState.blendMode;
            root.interpolatedCache.colorR = rootState.colorR;
            root.interpolatedCache.colorG = rootState.colorG;
            root.interpolatedCache.colorB = rootState.colorB;
            root.interpolatedCache.colorA = rootState.colorA;
            root.interpolatedCache.hasTransformOrder = rootState.hasTransformOrder;
            if (rootState.hasTransformOrder) {
                std::copy(std::begin(rootState.transformOrder),
                          std::end(rootState.transformOrder),
                          root.interpolatedCache.transformOrder);
            }
            root.interpolatedCache.action = rootState.action;
            root.interpolatedCache.hasSync = rootState.hasSync;
            root.interpolatedCache.prtTrigger = rootState.prtTrigger;
            root.interpolatedCache.prtF = rootState.prtF;
            root.interpolatedCache.prtV = rootState.prtV;
            root.interpolatedCache.prtA = rootState.prtA;
            root.interpolatedCache.prtZ = rootState.prtZ;
            root.interpolatedCache.prtRange = rootState.prtRange;

            // Populate root clipW/clipH/originX/originY (sub_6BC4F0)
            root.clipW = rootState.width;
            root.clipH = rootState.height;
            if (!rootState.src.empty() && _runtime->activeMotion) {
                int srcW = 0, srcH = 0;
                double srcOX = 0, srcOY = 0;
                std::vector<std::uint8_t> decomp;
                findPSBResourceBySourceName(*_runtime->activeMotion, rootState.src,
                    srcW, srcH, decomp, srcOX, srcOY);
                root.originX = srcOX;
                root.originY = srcOY;
                if (root.clipW <= 0 && srcW > 0) root.clipW = srcW;
                if (root.clipH <= 0 && srcH > 0) root.clipH = srcH;
            }

            // Step 3: Build root local 2x2 matrix via sub_699940
            // Reuse applyLocalTransform logic but on raw 2x2
            Affine2x3 rootAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            applyLocalTransform(rootAffine, rootState);
            root.accumulated.m11 = rootAffine[0];
            root.accumulated.m21 = rootAffine[1];
            root.accumulated.m12 = rootAffine[2];
            root.accumulated.m22 = rootAffine[3];
        }

        // --- sub_6BBE20: Variable interpolation (pre-loop) ---
        // Aligned to 0x6BBE20. Interpolates variable values and binds to nodes.
        // In libkrkr2.so this operates on a 160-byte item deque (player+1312).
        // Each variable is interpolated then bound to nodes via sub_6C4668.
        //
        // sub_6C4668 binding: resolves variable name to a source entry in
        // player+264 map, then updates child Player timeline parameters for
        // nodeType=3 and nodeType=4 nodes. In our architecture, variable values
        // are stored in _variableValues and exposed via getVariable()/setVariable()
        // TJS API. The binding to child Players happens implicitly when child
        // Players re-evaluate their timelines.
        if (_runtime->activeMotion) {
            const auto &varFrames = _runtime->activeMotion->variableFrames;
            for (const auto &[label, frames] : varFrames) {
                if (frames.empty()) continue;
                // User-set value takes precedence
                if (_variableValues.find(label) != _variableValues.end()) continue;
                // Default: use first frame value
                _variableValues[label] = frames.front().value;
            }
            // Bind variable values to child Players (sub_6C4668 equivalent)
            // For nodeType=3/4 nodes with child Players, propagate variable values
            for (auto &vn : nodes) {
                if ((vn.nodeType == 3 || vn.nodeType == 4) && vn.childPlayer) {
                    for (const auto &[label, value] : _variableValues) {
                        vn.childPlayer->setVariable(detail::widen(label), value);
                    }
                }
            }
        }

    }

    // Phase 2: Main node evaluation loop (non-root nodes)
    void Player::updateLayersPhase2_MainLoop(double currentTime) {
        auto &nodes = _runtime->nodes;
        // === PHASE 2: Main loop — evaluate non-root nodes ===
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Find parent node — walk parentIndex chain, skip flag 0x40 nodes
            // Aligned to 0x6BB598..0x6BB5BC
            int parentIdx = node.parentIndex;
            while (parentIdx > 0 && parentIdx < static_cast<int>(nodes.size())) {
                if ((nodes[parentIdx].flags & 0x40) == 0) break;
                parentIdx = nodes[parentIdx].parentIndex;
            }
            if (parentIdx < 0 || parentIdx >= static_cast<int>(nodes.size()))
                parentIdx = 0;
            const auto &parent = nodes[parentIdx];

            // Evaluate this node's interpolated state
            auto state = evaluateLayerContent(node.psbNode, currentTime);

            // Cache interpolated data for rendering
            node.interpolatedCache.src = state.src;
            node.interpolatedCache.width = state.width;
            node.interpolatedCache.height = state.height;
            node.interpolatedCache.opacity = state.opacity;
            node.interpolatedCache.x = state.x;
            node.interpolatedCache.y = state.y;
            node.interpolatedCache.ox = state.ox;
            node.interpolatedCache.oy = state.oy;
            node.interpolatedCache.angle = state.angle;
            node.interpolatedCache.scaleX = state.scaleX;
            node.interpolatedCache.scaleY = state.scaleY;
            node.interpolatedCache.slantX = state.slantX;
            node.interpolatedCache.slantY = state.slantY;
            node.interpolatedCache.flipX = state.flipX;
            node.interpolatedCache.flipY = state.flipY;
            node.interpolatedCache.blendMode = state.blendMode;
            node.interpolatedCache.colorR = state.colorR;
            node.interpolatedCache.colorG = state.colorG;
            node.interpolatedCache.colorB = state.colorB;
            node.interpolatedCache.colorA = state.colorA;
            node.interpolatedCache.hasTransformOrder = state.hasTransformOrder;
            if (state.hasTransformOrder) {
                std::copy(std::begin(state.transformOrder),
                          std::end(state.transformOrder),
                          node.interpolatedCache.transformOrder);
            }
            node.interpolatedCache.action = state.action;
            node.interpolatedCache.hasSync = state.hasSync;
            // Motion sub-object data from FrameContentState (mask 0x80000)
            node.interpolatedCache.motionDt = state.motionDt;
            node.interpolatedCache.motionFlags = state.motionFlags;
            node.interpolatedCache.motionDofst = state.motionDofst;
            node.interpolatedCache.motionDocmpl = state.motionDocmpl;
            node.interpolatedCache.motionTimeOffset = state.motionTimeOffset;
            node.interpolatedCache.motionDtgt = state.motionDtgt;
            // Particle data from FrameContentState (mask 0x100000)
            node.interpolatedCache.prtTrigger = state.prtTrigger;
            node.interpolatedCache.prtF = state.prtF;
            node.interpolatedCache.prtV = state.prtV;
            node.interpolatedCache.prtA = state.prtA;
            node.interpolatedCache.prtZ = state.prtZ;
            node.interpolatedCache.prtRange = state.prtRange;
            node.prtTrigger = state.prtTrigger;

            // Populate clipW/clipH from interpolated state (sub_6BC4F0 at 0x6BCB14)
            node.clipW = state.width;
            node.clipH = state.height;

            // Populate originX/originY from PSB source icon (sub_6948E8).
            // findPSBResourceBySourceName reads originX/originY from PSB icon nodes.
            if (!state.src.empty() && _runtime->activeMotion) {
                int srcW = 0, srcH = 0;
                double srcOX = 0, srcOY = 0;
                std::vector<std::uint8_t> decomp;
                findPSBResourceBySourceName(*_runtime->activeMotion, state.src,
                    srcW, srcH, decomp, srcOX, srcOY);
                node.originX = srcOX;
                node.originY = srcOY;
                if (node.clipW <= 0 && srcW > 0) node.clipW = srcW;
                if (node.clipH <= 0 && srcH > 0) node.clipH = srcH;
            }

            // slotDone: type=0 in evaluateLayerContent → visible=false → done
            node.slotDone = !state.visible;

            if (!state.visible) {
                node.accumulated.visible = false;
                node.accumulated.active = false;
                node.accumulated.opacity = 0;
                node.drawFlag = false;
                continue;
            }

            // === Inheritance from parent ===
            // Aligned to libkrkr2.so 0x6BB630..0x6BBB6C (Player_updateLayers main loop)
            // Full inheritFlags system with 3-phase independentLayerInherit support.
            node.accumulated.visible = true;
            node.accumulated.active = true;

            // Flip XOR from interpolated → accumulated (0x6BB668)
            node.accumulated.flipX = state.flipX ^ parent.accumulated.flipX;
            node.accumulated.flipY = state.flipY ^ parent.accumulated.flipY;

            // Scale: multiply from parent interpolated (0x6BB6A4)
            node.accumulated.scaleX = state.scaleX * parent.accumulated.scaleX;
            node.accumulated.scaleY = state.scaleY * parent.accumulated.scaleY;

            // Slant: add from parent interpolated (0x6BB6B8)
            node.accumulated.slantX = state.slantX + parent.accumulated.slantX;
            node.accumulated.slantY = state.slantY + parent.accumulated.slantY;

            // Opacity: int multiplication (0x6BB6D4)
            // First multiply: parent.interpolated.opacity * child.interpolated.opacity / 255
            const int childOpa = static_cast<int>(
                std::clamp(state.opacity * 255.0, 0.0, 255.0));
            node.accumulated.opacity = parent.accumulated.opacity * childOpa / 255;

            // Position: add from interpolated offsets (0x6BB6EC)
            const double lx = state.x + state.ox;
            const double ly = state.y + state.oy;

            // Position transform: parent.matrix × child.pos + parent.pos
            // 3D/2D coordinate mode branching (0x6BB718..0x6BB7C4)
            if (node.coordinateMode != 0) {
                // 3D mode: X and Z through matrix, Y pass-through (0x6BB720)
                node.accumulated.posX = parent.accumulated.m11 * lx
                    + parent.accumulated.m12 * ly + parent.accumulated.posX;
                node.accumulated.posY = parent.accumulated.m21 * lx
                    + parent.accumulated.m22 * ly + parent.accumulated.posY;
                // posZ: pass-through from interpolated + parent
                node.accumulated.posZ = state.y + state.oy + parent.accumulated.posZ;
            } else {
                // 2D mode (default, 0x6BB794): X and Y through matrix, Z pass-through
                node.accumulated.posX = parent.accumulated.m11 * lx
                    + parent.accumulated.m12 * ly + parent.accumulated.posX;
                node.accumulated.posY = parent.accumulated.m21 * lx
                    + parent.accumulated.m22 * ly + parent.accumulated.posY;
                // posZ: add from interpolated + parent (0x6BB7E4)
                node.accumulated.posZ += parent.accumulated.posZ;
            }

            // sub_69AE74: Mesh position deformation (0x6BB714)
            // Aligned to 0x69AE74. Called when parent.meshType != 0.
            // Deforms child position based on parent mesh surface.
            // Condition: parent.meshType==1 && (parent.meshFlags & 1) &&
            //            child.active && child.hasSource && parent has mesh vertices.
            if (parent.meshType == 1 && (parent.meshFlags & 1) != 0
                && node.accumulated.active && node.hasSource) {
                // Normalize child position by parent clip dimensions (0x69AF24..0x69AF50)
                const double pw = parent.clipW > 0.0 ? parent.clipW : 1.0;
                const double ph = parent.clipH > 0.0 ? parent.clipH : 1.0;
                const double normX = (node.accumulated.posX + parent.originX) / pw;
                const double normY = (node.accumulated.posY + parent.originY) / ph;

                // sub_69B1E8 → sub_6990A0: 4×4 bicubic Bezier patch evaluation.
                // meshData = 16 control points × 2 floats (X,Y) = 128 bytes at node+2024.
                // Bernstein basis: bu[i] for u, bv[j] for v, sum(bu[i]*bv[j]*P[i*4+j])
                // When no mesh vertex data available, use identity (passthrough).
                auto evalBezierPatch = [](const float *mesh, float u, float v,
                                          float &outX, float &outY) {
                    const float su = 1.0f - u, sv = 1.0f - v;
                    const float bu[4] = {
                        su*su*su, 3.0f*su*su*u, 3.0f*su*u*u, u*u*u
                    };
                    const float bv[4] = {
                        sv*sv*sv, 3.0f*sv*sv*v, 3.0f*sv*v*v, v*v*v
                    };
                    outX = 0; outY = 0;
                    for (int i = 0; i < 16; ++i) {
                        float w = bv[i >> 2] * bu[i & 3];
                        outX += mesh[i * 2] * w;
                        outY += mesh[i * 2 + 1] * w;
                    }
                };

                // Evaluate at normalized coordinates
                float defX = static_cast<float>(normX);
                float defY = static_cast<float>(normY);
                // Evaluate mesh at normalized coordinates using parent's mesh data.
                // parent.meshControlPoints populated by sub_6BC4F0 vertex computation.
                if (parent.meshControlPoints.size() >= 32) {
                    // 16-point Bezier patch: evaluate via sub_6990A0
                    evalBezierPatch(parent.meshControlPoints.data(),
                                    defX, defY, defX, defY);
                }
                node.accumulated.posX = static_cast<double>(defX) * pw - parent.originX;
                node.accumulated.posY = static_cast<double>(defY) * ph - parent.originY;

                // Angle deformation from mesh gradient (0x69AFB4..0x69B0EC)
                if ((parent.meshFlags & 2) != 0
                    && (node.inheritFlags & 0x10) != 0
                    && parent.meshControlPoints.size() >= 32) {
                    const float eps = 0.0001f;
                    const float *mp = parent.meshControlPoints.data();
                    float x1, y1, x2, y2, x3, y3, x4, y4;
                    // Sample at 4 nearby points (0x69B030..0x69B094)
                    evalBezierPatch(mp, defX - eps, defY, x1, y1);
                    evalBezierPatch(mp, defX + eps, defY, x2, y2);
                    evalBezierPatch(mp, defX, defY - eps, x3, y3);
                    evalBezierPatch(mp, defX, defY + eps, x4, y4);
                    // Average of two orthogonal gradients (0x69B0AC..0x69B0EC)
                    double a1 = std::atan2(
                        static_cast<double>(y3 - y4),
                        static_cast<double>(x4 - x3));
                    double a2 = std::atan2(
                        static_cast<double>(x2 - x1),
                        static_cast<double>(y2 - y1));
                    node.accumulated.angle += (a1 + a2) * 0.5 * 360.0 / 6.28318531;
                }

                // Scale deformation from mesh jacobian (0x69B11C..0x69B1A8)
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
                    // Jacobian area from cross product (0x69B154..0x69B188)
                    double dx1 = x2 - x1, dy1 = y2 - y1;
                    double dx2 = x3 - x4, dy2 = y3 - y4;
                    double area1 = std::fabs(dx1 * (y4 - y1) - dy1 * (x4 - x1)) * 0.5;
                    double area2 = std::fabs(dx1 * (y3 - y1) - dy1 * (x3 - x1)) * 0.5;
                    double scaleFactor = std::sqrt(area1 + area2 + area2 + area1) / 0.0002;
                    if (node.inheritFlags & 0x020)
                        node.accumulated.scaleX *= scaleFactor;
                    if (node.inheritFlags & 0x040)
                        node.accumulated.scaleY *= scaleFactor;
                }
            }

            // sub_6BAA10: Ground correction TJS callback (0x6BB7F8)
            // Aligned to 0x6BAA10. Called when node+47 (groundCorrection) set.
            // Invokes TJS onGroundCorrection(parentPos, childPos) callback on
            // the node's TJS object. The callback can modify child position.
            // In libkrkr2.so, the TJS object is at *(node+0)+16 (the layer's
            // iTJSDispatch2 reference). In our architecture, MotionNode doesn't
            // hold a TJS dispatch pointer. This callback is used for specialized
            // ground-plane correction in E-mote animations.
            if (node.groundCorrection && node.tjsLayerObject) {
                auto *tjsObj = static_cast<iTJSDispatch2 *>(node.tjsLayerObject);
                // Aligned to sub_6BAA10 (0x6BAA10): invoke TJS onGroundCorrection.
                // Push parent pos [posX,posY,posZ] and child pos as TJS arrays,
                // call onGroundCorrection, read back corrected child position.
                try {
                    // Create parent position array
                    iTJSDispatch2 *parentArr = TJSCreateArrayObject();
                    tTJSVariant pxv(parent.accumulated.posX);
                    tTJSVariant pyv(parent.accumulated.posY);
                    tTJSVariant pzv(parent.accumulated.posZ);
                    tTJSVariant *pargs[] = { &pxv };
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);
                    pargs[0] = &pyv;
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);
                    pargs[0] = &pzv;
                    parentArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, pargs, parentArr);

                    // Create child position array
                    iTJSDispatch2 *childArr = TJSCreateArrayObject();
                    tTJSVariant cxv(node.accumulated.posX);
                    tTJSVariant cyv(node.accumulated.posY);
                    tTJSVariant czv(node.accumulated.posZ);
                    tTJSVariant *cargs[] = { &cxv };
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);
                    cargs[0] = &cyv;
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);
                    cargs[0] = &czv;
                    childArr->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, cargs, childArr);

                    // Call onGroundCorrection(parentPos, childPos)
                    tTJSVariant parentVar(parentArr, parentArr);
                    tTJSVariant childVar(childArr, childArr);
                    tTJSVariant *callArgs[] = { &parentVar, &childVar };
                    tTJSVariant result;
                    tjsObj->FuncCall(0, TJS_W("onGroundCorrection"),
                        nullptr, &result, 2, callArgs, tjsObj);

                    // Read back corrected position from result (0x6BAD48..0x6BAE00)
                    if (result.Type() == tvtObject) {
                        iTJSDispatch2 *resObj = result.AsObjectNoAddRef();
                        if (resObj) {
                            tTJSVariant rv;
                            tTJSVariant idx;
                            idx = 0; resObj->PropGetByNum(0, 0, &rv, resObj);
                            node.accumulated.posX = static_cast<double>(rv);
                            idx = 1; resObj->PropGetByNum(0, 1, &rv, resObj);
                            node.accumulated.posY = static_cast<double>(rv);
                            idx = 2; resObj->PropGetByNum(0, 2, &rv, resObj);
                            node.accumulated.posZ = static_cast<double>(rv);
                        }
                    }
                    parentArr->Release();
                    childArr->Release();
                } catch (...) {
                    // TJS callback failure — silently ignore
                }
            }

            // Opacity conditional second multiply (0x6BB808..0x6BB830):
            // Decompilation: if ((v46 & 0x400) != 0 || (v47 = v3, !*(a1+1097)))
            //   node.opacity = v47.opacity * node.opacity / 255
            // v47 = parent when 0x400 set; v47 = root (v3) when !independentLayerInherit
            {
                const auto *opaNode = &parent;
                if ((node.inheritFlags & 0x400) == 0 && _independentLayerInherit) {
                    // Neither 0x400 set nor independentLayerInherit=false: skip
                    // (no second multiply in this case)
                } else {
                    if ((node.inheritFlags & 0x400) != 0)
                        opaNode = &parent;
                    else
                        opaNode = &nodes[0];  // root
                    node.accumulated.opacity = opaNode->accumulated.opacity
                        * node.accumulated.opacity / 255;
                }
            }

            // Angle: add (0x6BB8C8)
            node.accumulated.angle = state.angle + parent.accumulated.angle;

            // === inheritFlags per-property control (0x6BB83C) ===
            // Decompilation evidence: Player_updateLayers 0x6BB83C..0x6BBB6C
            //   if ((~v46 & 0x1FC) == 0) → all bits set, simple path
            //   else:
            //     per-property inherit from parent for SET bits
            //     if (player+1097) → LABEL_68: sub_699940 only, NO matrix multiply
            //     else → LABEL_76: root undo → sub_699940 → root re-apply → matrix multiply
            const int flags = node.inheritFlags;
            const bool allInheritBitsSet = (~flags & 0x1FC) == 0;

            if (allInheritBitsSet) {
                // All bits set → simple path (0x6BB848): inherit from parent,
                // sub_699940, matrix multiply. Already inherited above.
                Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(localAffine, state);
                const double lm11 = localAffine[0], lm21 = localAffine[1];
                const double lm12 = localAffine[2], lm22 = localAffine[3];
                node.accumulated.m11 = parent.accumulated.m11 * lm11 + parent.accumulated.m12 * lm21;
                node.accumulated.m21 = parent.accumulated.m21 * lm11 + parent.accumulated.m22 * lm21;
                node.accumulated.m12 = parent.accumulated.m11 * lm12 + parent.accumulated.m12 * lm22;
                node.accumulated.m22 = parent.accumulated.m21 * lm12 + parent.accumulated.m22 * lm22;
            } else {
                // Some bits NOT set: per-property inherit from parent for SET bits only
                // (0x6BB8F4..0x6BB918)
                if (flags & 0x004) node.accumulated.flipX = state.flipX ^ parent.accumulated.flipX;
                else               node.accumulated.flipX = state.flipX;
                if (flags & 0x008) node.accumulated.flipY = state.flipY ^ parent.accumulated.flipY;
                else               node.accumulated.flipY = state.flipY;
                if (flags & 0x010) node.accumulated.angle = state.angle + parent.accumulated.angle;
                else               node.accumulated.angle = state.angle;
                if (flags & 0x020) node.accumulated.scaleX = state.scaleX * parent.accumulated.scaleX;
                else               node.accumulated.scaleX = state.scaleX;
                if (flags & 0x040) node.accumulated.scaleY = state.scaleY * parent.accumulated.scaleY;
                else               node.accumulated.scaleY = state.scaleY;
                if (flags & 0x080) node.accumulated.slantX = state.slantX + parent.accumulated.slantX;
                else               node.accumulated.slantX = state.slantX;
                if (flags & 0x100) node.accumulated.slantY = state.slantY + parent.accumulated.slantY;
                else               node.accumulated.slantY = state.slantY;

                if (_independentLayerInherit) {
                    // LABEL_68 (0x6BB918): independentLayerInherit=TRUE
                    // Only sub_699940, NO matrix multiply with parent.
                    // Node's matrix stays as its own local matrix (independent of parent).
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, state);
                    node.accumulated.m11 = localAffine[0];
                    node.accumulated.m21 = localAffine[1];
                    node.accumulated.m12 = localAffine[2];
                    node.accumulated.m22 = localAffine[3];
                } else {
                    // LABEL_76 (0x6BB9BC..0x6BBB6C): independentLayerInherit=FALSE
                    // 4-phase: undo root → sub_699940 → re-apply root → matrix multiply
                    const auto &rootNode = nodes[0];

                    // Phase A: For SET bits, UNDO root contribution (0x6BB9BC)
                    if (flags & 0x004) node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008) node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010) node.accumulated.angle -= rootNode.accumulated.angle;
                    if (flags & 0x020 && rootNode.accumulated.scaleX != 0.0)
                        node.accumulated.scaleX /= rootNode.accumulated.scaleX;
                    if (flags & 0x040 && rootNode.accumulated.scaleY != 0.0)
                        node.accumulated.scaleY /= rootNode.accumulated.scaleY;
                    if (flags & 0x080) node.accumulated.slantX -= rootNode.accumulated.slantX;
                    if (flags & 0x100) node.accumulated.slantY -= rootNode.accumulated.slantY;

                    // Phase B: sub_699940 (0x6BB9E8)
                    Affine2x3 localAffine = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                    applyLocalTransform(localAffine, state);

                    // Phase C: For SET bits, RE-APPLY root contribution (0x6BBA04)
                    if (flags & 0x004) node.accumulated.flipX ^= rootNode.accumulated.flipX;
                    if (flags & 0x008) node.accumulated.flipY ^= rootNode.accumulated.flipY;
                    if (flags & 0x010) node.accumulated.angle += rootNode.accumulated.angle;
                    if (flags & 0x020) node.accumulated.scaleX *= rootNode.accumulated.scaleX;
                    if (flags & 0x040) node.accumulated.scaleY *= rootNode.accumulated.scaleY;
                    if (flags & 0x080) node.accumulated.slantX += rootNode.accumulated.slantX;
                    if (flags & 0x100) node.accumulated.slantY += rootNode.accumulated.slantY;

                    // Phase D: Matrix multiply parent × local (0x6BBA24)
                    const double lm11 = localAffine[0], lm21 = localAffine[1];
                    const double lm12 = localAffine[2], lm22 = localAffine[3];
                    node.accumulated.m11 = parent.accumulated.m11 * lm11 + parent.accumulated.m12 * lm21;
                    node.accumulated.m21 = parent.accumulated.m21 * lm11 + parent.accumulated.m22 * lm21;
                    node.accumulated.m12 = parent.accumulated.m11 * lm12 + parent.accumulated.m12 * lm22;
                    node.accumulated.m22 = parent.accumulated.m21 * lm12 + parent.accumulated.m22 * lm22;
                }
            }
        }

    }

    void Player::updateLayersPhase3_CameraConstraint() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BC000: Camera constraint (nodeType=9) ---
        // Aligned to 0x6BC000..0x6BC4EC. Only when !isEmoteMode.
        // 9 cases at 0x6BC1B0..0x6BC358 based on flipX/flipY + constraintType (node+2376).
        if (!_runtime->isEmoteMode && nodes.size() >= 2) {
            double offsetX = 0, offsetY = 0, offsetZ = 0;
            // Track which axes have constraints and their types
            bool hasMinX = false, hasMaxX = false, hasTrackX = false;
            bool hasMinY = false, hasMaxY = false, hasTrackY = false;
            bool hasMinZ = false, hasMaxZ = false, hasTrackZ = false;
            double minX = 3.4e38, maxX = -3.4e38, trackX = 0;
            double minY = 3.4e38, maxY = -3.4e38, trackY = 0;
            double minZ = 3.4e38, maxZ = -3.4e38, trackZ = 0;

            for (size_t ci = 1; ci < nodes.size(); ++ci) {
                auto &cn = nodes[ci];
                if (cn.nodeType != 9 || cn.slotDone || !cn.accumulated.active) continue;

                // Target node: root (node 0). Full impl would look up dtgt.
                const auto &target = nodes[0];

                // Compute constraintType with flip adjustment (0x6BC1B0..0x6BC1FC)
                int ctype = cn.cameraConstraintType;
                if (cn.accumulated.flipX) {
                    if (ctype == 0) ctype = 2;
                    else if (ctype == 2) ctype = 0;
                }
                if (cn.accumulated.flipY) {
                    if (ctype == 3) ctype = 5;
                    else if (ctype == 5) ctype = 3;
                }

                // 9 cases (0x6BC224..0x6BC358)
                switch (ctype) {
                    case 0: { // X min constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d < 0 && d < minX) { minX = d; hasMinX = true; }
                        break;
                    }
                    case 1: { // X direct track
                        trackX = target.accumulated.posX - cn.accumulated.posX;
                        hasTrackX = true;
                        break;
                    }
                    case 2: { // X max constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d > 0 && d > maxX) { maxX = d; hasMaxX = true; }
                        break;
                    }
                    case 3: { // Y min constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d < 0 && d < minY) { minY = d; hasMinY = true; }
                        break;
                    }
                    case 4: { // Y direct track
                        trackY = target.accumulated.posY - cn.accumulated.posY;
                        hasTrackY = true;
                        break;
                    }
                    case 5: { // Y max constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d > 0 && d > maxY) { maxY = d; hasMaxY = true; }
                        break;
                    }
                    case 6: { // Z min constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d < 0 && d < minZ) { minZ = d; hasMinZ = true; }
                        break;
                    }
                    case 7: { // Z direct track
                        trackZ = target.accumulated.posZ - cn.accumulated.posZ;
                        hasTrackZ = true;
                        break;
                    }
                    case 8: { // Z max constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d > 0 && d > maxZ) { maxZ = d; hasMaxZ = true; }
                        break;
                    }
                    default: break;
                }
            }
            // Resolve final offset per axis (0x6BC398..0x6BC410)
            // Priority: track > max > min > 0
            if (hasTrackX) offsetX = trackX;
            else if (hasMaxX) offsetX = maxX;
            else if (hasMinX) offsetX = minX;
            if (hasTrackY) offsetY = trackY;
            else if (hasMaxY) offsetY = maxY;
            else if (hasMinY) offsetY = minY;
            if (hasTrackZ) offsetZ = trackZ;
            else if (hasMaxZ) offsetZ = maxZ;
            else if (hasMinZ) offsetZ = minZ;

            // Apply offset to all nodes (0x6BC450..0x6BC4BC)
            if (offsetX != 0 || offsetY != 0 || offsetZ != 0) {
                for (size_t ci = 1; ci < nodes.size(); ++ci) {
                    nodes[ci].accumulated.posX += offsetX;
                    nodes[ci].accumulated.posY += offsetY;
                    nodes[ci].accumulated.posZ += offsetZ;
                }
            }
        }

    }

    void Player::updateLayersPhase3_VertexComputation() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BC4F0: Vertex computation ---
        // Aligned to 0x6BC4F0. Full implementation matching decompilation.
        for (size_t vi = 1; vi < nodes.size(); ++vi) {
            auto &vn = nodes[vi];
            const int parentIdx = vn.parentIndex >= 0 ? vn.parentIndex : 0;
            auto &parentNode = nodes[parentIdx];
            const int slotIdx = 0;  // current slot index

            // priorDraw flag from emoteEdit (0x6BC648..0x6BC6C4)
            // priorDraw from emoteEdit (0x6BC648..0x6BC6C4)
            if (vn.forceVisible && vn.emoteEditDict) {
                // sub_6636D4: read bool "priorDraw" from emoteEdit dict
                auto pdVal = (*vn.emoteEditDict)["priorDraw"];
                if (auto num = std::dynamic_pointer_cast<PSB::PSBNumber>(pdVal))
                    vn.priorDraw = (num->getValue<int>() != 0);
                else if (auto bl = std::dynamic_pointer_cast<PSB::PSBBool>(pdVal))
                    vn.priorDraw = bl->value;
                else
                    vn.priorDraw = false;
            } else {
                vn.priorDraw = false;  // 0x6BC67C
            }

            // Parent clip chain: node+1962/1963 flags (0x6BC6E4..0x6BC818)
            // node+1962 = has mesh data, node+1963 = mesh combine enabled
            // parentClipIndex propagated by sub_6BDCC0 carries the ancestor chain

            // Check visible (0x6BC700..0x6BC74C)
            if (!vn.accumulated.visible) {
                // Walk parent for mesh flag
                goto bc4f0_next;
            }

            // Propagate clip origin
            vn.clipOriginX = vn.interpolatedCache.ox;
            vn.clipOriginY = vn.interpolatedCache.oy;

            // nodeType 1/5 special position via parent mesh chain (0x6BC828..0x6BC8D4)
            // if ((1 << nodeType) & 0x22) != 0 → nodeType 1 (shape) or 5 (camera)
            if (((1 << vn.nodeType) & 0x22) != 0) {
                double px = vn.accumulated.posX;
                double py = vn.accumulated.posY;
                // Walk parent clip chain, evaluate through each mesh (0x6BC838..0x6BC8B0)
                int clipWalk = vn.parentClipIndex;
                while (clipWalk >= 0 && clipWalk < static_cast<int>(nodes.size())) {
                    auto &cn = nodes[clipWalk];
                    if (cn.meshControlPointsPrev.size() >= 32) {
                        // Apply inverse matrix to get normalized coords (0x6BC858..0x6BC87C)
                        float tx = static_cast<float>(px) + cn.meshInvOffX;
                        float ty = static_cast<float>(py) + cn.meshInvOffY;
                        float ix = static_cast<float>(
                            cn.meshInvM11 * tx + cn.meshInvM12 * ty);
                        float iy = static_cast<float>(
                            cn.meshInvM21 * tx + cn.meshInvM22 * ty);
                        // Evaluate bezier patch at normalized coords (sub_69B1E8)
                        const float *mesh = cn.meshControlPointsPrev.data();
                        const float su = 1.f - ix, sv = 1.f - iy;
                        const float bu[4] = {su*su*su, 3.f*su*su*ix, 3.f*su*ix*ix, ix*ix*ix};
                        const float bv[4] = {sv*sv*sv, 3.f*sv*sv*iy, 3.f*sv*iy*iy, iy*iy*iy};
                        float ox = 0, oy = 0;
                        for (int bi = 0; bi < 16; ++bi) {
                            float w = bv[bi >> 2] * bu[bi & 3];
                            ox += mesh[bi * 2] * w;
                            oy += mesh[bi * 2 + 1] * w;
                        }
                        px = ox;
                        py = oy;
                    }
                    clipWalk = cn.parentClipIndex;
                }
                vn.vertexPosX = px;
                vn.vertexPosY = py;
                vn.vertexPosZ = vn.accumulated.posZ;
            }

            // Non slot-done path: vertex computation (0x6BC8DC..0x6BD730)
            if (!vn.slotDone) {
                // Second visibility bitmask check (0x6BCE2C..0x6BCE40)
                // Non-emote: 7233 = 0x1C41, Emote: 7241 = 0x1C49
                const int vbm = _runtime->isEmoteMode ? 7241 : 7233;
                const bool vertexEligible = vn.forceVisible
                    || ((vbm & (1 << vn.nodeType)) != 0);

                if (vertexEligible && vn.hasSource) {
                    const double m11 = vn.accumulated.m11, m12 = vn.accumulated.m12;
                    const double m21 = vn.accumulated.m21, m22 = vn.accumulated.m22;
                    const double posX = vn.accumulated.posX;
                    const double posY = vn.accumulated.posY
                        + vn.accumulated.posZ * _zFactor;

                    // Origin offset (0x6BCB58..0x6BCBA4)
                    const double totalOX = vn.originX + vn.clipOriginX;
                    const double totalOY = vn.originY + vn.clipOriginY;
                    const double orgX = posX - (m12 * totalOY + totalOX * m11);
                    const double orgY = posY - (totalOY * m22 + totalOX * m21);
                    vn.vertexPosX = orgX;
                    vn.vertexPosY = orgY;
                    vn.vertexPosZ = vn.accumulated.posZ;

                    // Save prev mesh (0x6BCB94..0x6BCBAC)
                    vn.meshControlPointsPrev = vn.meshControlPoints;

                    const double cw = vn.clipW;
                    const double ch = vn.clipH;

                    // Mesh vertex construction (0x6BCBBC..0x6BD060)
                    if (vn.meshType == 1
                        && !vn.meshControlPoints.empty()
                        && cw > 0 && ch > 0) {
                        // meshType=1: Bezier patch mesh
                        // Compute inverse matrix for mesh (0x6BCBF8..0x6BCC38)
                        // Compute and store inverse matrix (0x6BCBF8..0x6BCC38)
                        // det = m11*cw * m22*ch - m12*ch * m21*cw
                        const double mw11 = m11 * cw, mw12 = m12 * ch;
                        const double mw21 = m21 * cw, mw22 = m22 * ch;
                        const double det = mw11 * mw22 - mw12 * mw21;
                        if (std::fabs(det) > 1e-10) {
                            // node+2096..2120: inverse of [mw11,mw12;mw21,mw22]
                            vn.meshInvM11 = mw22 / det;   // 0x6BCC0C
                            vn.meshInvM12 = -(mw12 / det); // 0x6BCC20
                            vn.meshInvM21 = -(mw21 / det); // 0x6BCC34
                            vn.meshInvM22 = mw11 / det;    // 0x6BCC14
                            // node+2128/2132: negated origin as float (0x6BCC04/0x6BCC38)
                            vn.meshInvOffX = -static_cast<float>(orgX);
                            vn.meshInvOffY = -static_cast<float>(orgY);
                        }

                        // Build grid via sub_6BAF68 (0x6BCF6C)
                        // Grid dimensions: divX = meshDivision * cw/(cw+ch) + 1
                        int divTotal = vn.meshDivision;
                        if (divTotal > 50) divTotal = 50;
                        if (divTotal < 1) divTotal = 4;
                        const int divX = static_cast<int>(
                            static_cast<double>(divTotal) * cw / (cw + ch)) + 1;
                        const int divY = divTotal - divX + 2;
                        const int numPts = divX * divY;
                        // Store grid dimensions (node+2012/2016, 0x6BCF5C)
                        vn.meshDivX = divX;
                        vn.meshDivY = divY;

                        // sub_6BAF68: build bilinear grid (0x6BAF68)
                        // NEON version at 0x6BB030..0x6BB138 processes 4 points/iteration.
                        // Each row interpolates linearly between two edge points:
                        //   p0 = orgXY + m_col2*ch*tv, p1 = orgXY + m_col1*cw + m_col2*ch*tv
                        //   grid[gx] = lerp(p0, p1, gx/divX)
                        vn.meshControlPoints.resize(numPts * 2);
                        for (int gy = 0; gy < divY; ++gy) {
                            const double tv = (divY > 1) ? static_cast<double>(gy) / (divY - 1) : 0;
                            // Row edge points (0x6BB068..0x6BB09C)
                            const double rowBaseX = orgX + (m12 * ch) * tv;
                            const double rowBaseY = orgY + (m22 * ch) * tv;
                            const double rowEndX = rowBaseX + m11 * cw;
                            const double rowEndY = rowBaseY + m21 * cw;
                            float *rowPtr = &vn.meshControlPoints[gy * divX * 2];
#ifdef __EMSCRIPTEN__
                            // WASM SIMD: process 4 grid points per iteration
                            // Aligned to NEON at 0x6BB0CC..0x6BB138
                            // For each group of 4 gx values: tu = [gx, gx+1, gx+2, gx+3] / divX
                            // ptX = rowBaseX*(1-tu) + rowEndX*tu
                            // ptY = rowBaseY*(1-tu) + rowEndY*tu
                            const v128_t vBaseX = wasm_f64x2_splat(rowBaseX);
                            const v128_t vBaseY = wasm_f64x2_splat(rowBaseY);
                            const v128_t vEndX = wasm_f64x2_splat(rowEndX);
                            const v128_t vEndY = wasm_f64x2_splat(rowEndY);
                            const double invDivX = (divX > 1) ? 1.0 / (divX - 1) : 0.0;
                            int gx = 0;
                            const int simdEnd = divX & ~1;  // process 2 at a time (f64x2)
                            for (; gx < simdEnd; gx += 2) {
                                const double t0 = gx * invDivX;
                                const double t1 = (gx + 1) * invDivX;
                                const v128_t vt = wasm_f64x2_make(t0, t1);
                                const v128_t v1mt = wasm_f64x2_sub(wasm_f64x2_splat(1.0), vt);
                                // X = base*(1-t) + end*t
                                v128_t vx = wasm_f64x2_add(
                                    wasm_f64x2_mul(vBaseX, v1mt),
                                    wasm_f64x2_mul(vEndX, vt));
                                // Y = base*(1-t) + end*t
                                v128_t vy = wasm_f64x2_add(
                                    wasm_f64x2_mul(vBaseY, v1mt),
                                    wasm_f64x2_mul(vEndY, vt));
                                // Convert f64→f32 and store interleaved [x0,y0,x1,y1]
                                float fx0 = static_cast<float>(wasm_f64x2_extract_lane(vx, 0));
                                float fy0 = static_cast<float>(wasm_f64x2_extract_lane(vy, 0));
                                float fx1 = static_cast<float>(wasm_f64x2_extract_lane(vx, 1));
                                float fy1 = static_cast<float>(wasm_f64x2_extract_lane(vy, 1));
                                rowPtr[gx*2]   = fx0;
                                rowPtr[gx*2+1] = fy0;
                                rowPtr[gx*2+2] = fx1;
                                rowPtr[gx*2+3] = fy1;
                            }
                            // Scalar remainder
                            for (; gx < divX; ++gx) {
                                const double tu = (divX > 1) ? static_cast<double>(gx) / (divX-1) : 0;
                                rowPtr[gx*2]   = static_cast<float>(rowBaseX*(1-tu) + rowEndX*tu);
                                rowPtr[gx*2+1] = static_cast<float>(rowBaseY*(1-tu) + rowEndY*tu);
                            }
#else
                            for (int gx = 0; gx < divX; ++gx) {
                                const double tu = (divX > 1) ? static_cast<double>(gx) / (divX-1) : 0;
                                rowPtr[gx*2]   = static_cast<float>(rowBaseX*(1-tu) + rowEndX*tu);
                                rowPtr[gx*2+1] = static_cast<float>(rowBaseY*(1-tu) + rowEndY*tu);
                            }
#endif
                        }

                        // Evaluate each grid point through Bezier patch (0x6BCF80..0x6BCFBC)
                        // sub_69B1E8 evaluates bezier patch at each mesh point
                        // This transforms the bilinear grid into a deformed mesh
                        if (vn.meshControlPointsPrev.size() >= 32) {
                            auto evalBP = [](const float *mesh, float u, float v,
                                             float &outX, float &outY) {
                                const float su=1.f-u, sv=1.f-v;
                                const float bu[4]={su*su*su,3.f*su*su*u,3.f*su*u*u,u*u*u};
                                const float bv[4]={sv*sv*sv,3.f*sv*sv*v,3.f*sv*v*v,v*v*v};
                                outX=0; outY=0;
                                for(int i=0;i<16;++i){
                                    float w=bv[i>>2]*bu[i&3];
                                    outX+=mesh[i*2]*w; outY+=mesh[i*2+1]*w;
                                }
                            };
                            for (int pi = 0; pi < numPts; ++pi) {
                                float px = vn.meshControlPoints[pi*2];
                                float py = vn.meshControlPoints[pi*2+1];
                                evalBP(vn.meshControlPointsPrev.data(), px, py, px, py);
                                vn.meshControlPoints[pi*2] = px;
                                vn.meshControlPoints[pi*2+1] = py;
                            }
                        }

                        // Parent clip chain mesh cascade (0x6BD118..0x6BD380)
                        // Walk node+1968 (parentClipIndex), for each mesh-enabled
                        // ancestor: evaluate all mesh points + origin through its mesh
                        // Parent clip chain mesh cascade (0x6BD118..0x6BD380)
                        auto evalBPCascade = [](const float *mesh, float u, float v,
                                                float &outX, float &outY) {
                            const float su=1.f-u, sv=1.f-v;
                            const float bu[4]={su*su*su,3.f*su*su*u,3.f*su*u*u,u*u*u};
                            const float bv[4]={sv*sv*sv,3.f*sv*sv*v,3.f*sv*v*v,v*v*v};
                            outX=0; outY=0;
                            for(int i=0;i<16;++i){
                                float w=bv[i>>2]*bu[i&3];
                                outX+=mesh[i*2]*w; outY+=mesh[i*2+1]*w;
                            }
                        };
                        int clipWalk = vn.parentClipIndex;
                        double cascadeOrgX = orgX, cascadeOrgY = orgY;
                        while (clipWalk >= 0 && clipWalk < static_cast<int>(nodes.size())) {
                            auto &cn = nodes[clipWalk];
                            if (cn.meshControlPoints.size() >= 32) {
                                const float *cmesh = cn.meshControlPoints.data();
                                // Evaluate each mesh point through parent mesh (0x6BD148..0x6BD1E8)
                                for (size_t mi = 0; mi < vn.meshControlPoints.size() / 2; ++mi) {
                                    float mpx = vn.meshControlPoints[mi*2];
                                    float mpy = vn.meshControlPoints[mi*2+1];
                                    // Transform by parent inverse matrix + offset (0x6BD188)
                                    // Transform by parent inverse matrix + offset (0x6BD188)
                                    float tx = mpx + cn.meshInvOffX;  // node+2128
                                    float ty = mpy + cn.meshInvOffY;  // node+2132
                                    // Apply inverse matrix: [invM11,invM12;invM21,invM22] × (tx,ty)
                                    float ix = static_cast<float>(cn.meshInvM11 * tx + cn.meshInvM12 * ty);
                                    float iy = static_cast<float>(cn.meshInvM21 * tx + cn.meshInvM22 * ty);
                                    tx = ix; ty = iy;
                                    // Evaluate through parent bezier (sub_69B1E8)
                                    float rx, ry;
                                    evalBPCascade(cmesh, tx, ty, rx, ry);
                                    vn.meshControlPoints[mi*2] = rx;
                                    vn.meshControlPoints[mi*2+1] = ry;
                                }
                                // Evaluate origin through parent mesh (0x6BD218..0x6BD258)
                                float cox = static_cast<float>(cascadeOrgY) + cn.meshInvOffY;
                                float coy = static_cast<float>(cascadeOrgX) + cn.meshInvOffX;
                                float rox, roy;
                                evalBPCascade(cmesh, coy, cox, rox, roy);
                                cascadeOrgX = rox;
                                cascadeOrgY = roy;
                                _processedMeshVerticesNum += static_cast<int>(
                                    vn.meshControlPoints.size() / 2) + 1;
                            }
                            clipWalk = cn.parentClipIndex;
                        }
                        // Update origin if cascade changed it (0x6BD330..0x6BD380)
                        if (cascadeOrgX != orgX || cascadeOrgY != orgY) {
                            vn.vertexPosX = cascadeOrgX;
                            vn.vertexPosY = cascadeOrgY;
                            // Offset all mesh points by delta (0x6BD360..0x6BD380)
                            const float fdx = static_cast<float>(cascadeOrgX - orgX);
                            const float fdy = static_cast<float>(cascadeOrgY - orgY);
                            const size_t totalFloats = vn.meshControlPoints.size();
                            float *mp = vn.meshControlPoints.data();
#ifdef __EMSCRIPTEN__
                            // WASM SIMD: process 4 floats at a time (2 XY pairs)
                            // Aligned to NEON at 0x6BD360: vadd with delta vector
                            const v128_t vdelta = wasm_f32x4_make(fdx, fdy, fdx, fdy);
                            size_t fi = 0;
                            for (; fi + 4 <= totalFloats; fi += 4) {
                                v128_t pts = wasm_v128_load(&mp[fi]);
                                pts = wasm_f32x4_add(pts, vdelta);
                                wasm_v128_store(&mp[fi], pts);
                            }
                            // Scalar remainder
                            for (; fi < totalFloats; fi += 2) {
                                mp[fi] += fdx;
                                if (fi + 1 < totalFloats) mp[fi+1] += fdy;
                            }
#else
                            for (size_t mi = 0; mi < totalFloats / 2; ++mi) {
                                mp[mi*2] += fdx;
                                mp[mi*2+1] += fdy;
                            }
#endif
                        }
                    }

                    // 4-corner vertex output (0x6BCE44..0x6BCEC0)
                    {
                        const double fx = vn.vertexPosX;
                        const double fy = vn.vertexPosY;
                        vn.vertices[0] = static_cast<float>(fx);
                        vn.vertices[1] = static_cast<float>(fy);
                        vn.vertices[2] = static_cast<float>(fx + m11*cw);
                        vn.vertices[3] = static_cast<float>(fy + m21*cw);
                        vn.vertices[4] = static_cast<float>(fx + m11*cw + m12*ch);
                        vn.vertices[5] = static_cast<float>(fy + m21*cw + m22*ch);
                        vn.vertices[6] = static_cast<float>(fx + m12*ch);
                        vn.vertices[7] = static_cast<float>(fy + m22*ch);
                    }

                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // When node+1996 (forceVisible) is set, write node properties
                    // to a TJS dictionary for sub-motion evaluation.
                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // Write node properties to TJS dict for sub-motion evaluation.
                    if (vn.forceVisible && vn.tjsLayerObject) {
                        auto *tjsObj = static_cast<iTJSDispatch2 *>(vn.tjsLayerObject);
                        try {
                            // "c" array: [posX, posY] (0x6BD480..0x6BD494)
                            tTJSVariant posXv(vn.vertexPosX);
                            tTJSVariant posYv(vn.vertexPosY);
                            // "mtx" array: [m11,m12,m21,m22] (0x6BD534..0x6BD570)
                            tTJSVariant m11v(m11), m12v(m12), m21v(m21), m22v(m22);
                            // "width" (0x6BD590)
                            tTJSVariant wv(cw);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("width"),
                                nullptr, &wv, tjsObj);
                            // "height" (0x6BD5B0)
                            tTJSVariant hv(ch);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("height"),
                                nullptr, &hv, tjsObj);
                            // "originX" (0x6BD5E4)
                            tTJSVariant oxv(totalOX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originX"),
                                nullptr, &oxv, tjsObj);
                            // "originY" (0x6BD618)
                            tTJSVariant oyv(totalOY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originY"),
                                nullptr, &oyv, tjsObj);
                            // "flipX" (0x6BD638)
                            tTJSVariant fxv(static_cast<tjs_int>(vn.accumulated.flipX));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipX"),
                                nullptr, &fxv, tjsObj);
                            // "flipY" (0x6BD658)
                            tTJSVariant fyv(static_cast<tjs_int>(vn.accumulated.flipY));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipY"),
                                nullptr, &fyv, tjsObj);
                            // "zoomX" (0x6BD678)
                            tTJSVariant zxv(vn.accumulated.scaleX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomX"),
                                nullptr, &zxv, tjsObj);
                            // "zoomY" (0x6BD698)
                            tTJSVariant zyv(vn.accumulated.scaleY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomY"),
                                nullptr, &zyv, tjsObj);
                            // "slantX" (0x6BD6B8)
                            tTJSVariant sxv(vn.accumulated.slantX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("slantX"),
                                nullptr, &sxv, tjsObj);
                            // "angle" (0x6BD6D8)
                            tTJSVariant av(vn.accumulated.angle);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("angle"),
                                nullptr, &av, tjsObj);
                        } catch (...) {}
                    }
                }
            }
            bc4f0_next:;
        }

        // Delta position computation (0x6BBB74..0x6BBC54)
        // if playing (player+480): delta = 0; else: delta = currentPos - prevPos
        {
            bool anyPlaying = std::any_of(
                _runtime->timelines.begin(), _runtime->timelines.end(),
                [](const auto &e) { return e.second.playing; });
            for (size_t di = 1; di < nodes.size(); ++di) {
                auto &dn = nodes[di];
                if (anyPlaying) {
                    dn.deltaPosX = 0; dn.deltaPosY = 0; dn.deltaPosZ = 0;
                } else {
                    dn.deltaPosX = dn.accumulated.posX - dn.prevPosX;
                    dn.deltaPosY = dn.accumulated.posY - dn.prevPosY;
                    dn.deltaPosZ = dn.accumulated.posZ - dn.prevPosZ;
                }
            }
        }

    }

    void Player::updateLayersPhase3_Visibility() {
        auto &nodes = _runtime->nodes;
        // Visibility flags — aligned to sub_6BD8DC at 0x6BD8DC.
        // Root node (index 0) is always visible.
        if (!nodes.empty()) {
            nodes[0].drawFlag = nodes[0].accumulated.visible && nodes[0].hasSource;
        }
        // Visibility bitmask: which nodeTypes can render
        // Non-emote: 6145 = 0x1801 → nodeTypes 0, 11, 12
        // Emote:     6153 = 0x1809 → nodeTypes 0, 3, 11, 12
        // Aligned to sub_6BD8DC (0x6BD8DC): visibility bitmask depends on emote mode.
        const int visBitmask = _runtime->isEmoteMode ? 6153 : 6145;
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Find visible ancestor (walk parent chain, 0x6BD9D8)
            int pIdx = node.parentIndex;
            if (pIdx >= 0 && pIdx < static_cast<int>(nodes.size())) {
                if (!nodes[pIdx].drawFlag) {
                    node.visibleAncestorIndex = nodes[pIdx].visibleAncestorIndex;
                } else {
                    node.visibleAncestorIndex = pIdx;
                }
            }

            // Visibility logic — exact replica of sub_6BD8DC (0x6BD958..0x6BDA00):
            //   if (slotDone) { v9 = 0; }
            //   else { v9 = stencilType; if (v9) { v9 = active; if (v9) {
            //     if (forceVisible || (bitmask & (1<<nodeType))) v9 = hasSource; } } }
            //   drawFlag = v9;
            if (node.slotDone) {
                node.drawFlag = false;
            } else if (node.stencilType == 0) {
                // node+52 == 0 → invisible (0x6BD958)
                node.drawFlag = false;
            } else if (!node.accumulated.active) {
                node.drawFlag = false;
            } else if (node.forceVisible
                       || (visBitmask & (1 << node.nodeType)) != 0) {
                node.drawFlag = node.hasSource;
            } else {
                // Active node, not in renderable bitmask, not forceVisible:
                // v9 stays as active (non-zero) → drawFlag = true
                node.drawFlag = true;
            }
        }

    }

    void Player::updateLayersPhase3_CameraNode() {
        auto &nodes = _runtime->nodes;
        // Camera node processing — aligned to sub_6BDA28 (0x6BDA28).
        // Find first nodeType=5 (camera) that is active, compute cameraOffset.
        _hasCamera = false;
        for (size_t i = 1; i < nodes.size(); ++i) {
            const auto &camNode = nodes[i];
            if (camNode.nodeType != 5 || !camNode.accumulated.active) continue;
            _hasCamera = true;

            // Compute delta from root node position
            const auto &rootAcc = nodes[0].accumulated;
            const double dx = -(camNode.accumulated.posX - rootAcc.posX);
            const double dy = -(camNode.accumulated.posY * _zFactor
                + camNode.accumulated.posZ
                - (rootAcc.posY * _zFactor + rootAcc.posZ));

            // Transform by drawAffineMatrix (player+808..832)
            const auto &dam = _runtime->drawAffineMatrix;
            _cameraOffsetX = static_cast<float>(
                static_cast<int>(dam[0] * dx + dam[2] * dy + 0.5));
            _cameraOffsetY = static_cast<float>(
                static_cast<int>(dam[1] * dx + dam[3] * dy + 0.5));

            // Camera-to-target angle (0x6BDC04..0x6BDCB0)
            // When stereovisionActive (a1+1094): compute camera angle for 3D effect.
            if (_stereovisionActive) {
                // Store camera/target positions (a1+72..112)
                _cameraPosX = camNode.accumulated.posX;
                _cameraPosY = camNode.accumulated.posY;
                _cameraPosZ = camNode.accumulated.posZ;
                // Look up target node via clip slot action path
                // For now, target defaults to previous positions
                // Compute angle: atan2(camPosZ - targetZ, camPosX - targetX)
                double angleRad = std::atan2(
                    camNode.accumulated.posZ - _cameraTargetZ,
                    camNode.accumulated.posX - _cameraTargetX);
                double angleDeg = angleRad * -57.2957795 + 90.0;
                while (angleDeg < 0.0) angleDeg += 360.0;
                while (angleDeg >= 360.0) angleDeg -= 360.0;
                _cameraAngle = angleDeg;  // a1+472
                _cameraTargetX = _cameraPosX;
                _cameraTargetY = _cameraPosY;
                _cameraTargetZ = _cameraPosZ;
            }
            break;  // only first camera node
        }

    }

    void Player::updateLayersPhase3_ShapeAABB() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BDCC0: Shape AABB computation (nodeType=7) ---
        // Aligned to 0x6BDCC0. For nodeType=7 active nodes, compute AABB
        // from 2x2 matrix × 16-unit extent, origin offset, parent clip clamping.
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            // Propagate parent clip region (node+1936)
            if (sn.parentIndex >= 0 && sn.parentIndex < static_cast<int>(nodes.size())) {
                sn.parentClipIndex = nodes[sn.parentIndex].parentClipIndex;
            }
            if (sn.nodeType != 7 || !sn.accumulated.active) continue;

            const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
            const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
            const double px = sn.accumulated.posX, py = sn.accumulated.posY;
            const double pzs = sn.accumulated.posZ * _zFactor + py;
            const double ox = sn.clipOriginX, oy = sn.clipOriginY;
            const double oox = ox * m11 + oy * m12;
            const double ooy = ox * m21 + oy * m22;
            // Extent = matrix × 16
            const double ex1 = m11 * 16.0, ex2 = m12 * 16.0;
            const double ey1 = m21 * 16.0, ey2 = m22 * 16.0;
            double xMin = px - ex1 - ex2 - oox;
            double xMax = px + ex1 + ex2 - oox;
            double yMin = pzs - ey1 - ey2 - ooy;
            double yMax = pzs + ey1 + ey2 - ooy;
            if (xMin > xMax) std::swap(xMin, xMax);
            if (yMin > yMax) std::swap(yMin, yMax);
            sn.shapeAABB[0] = static_cast<float>(xMin);
            sn.shapeAABB[1] = static_cast<float>(yMin);
            sn.shapeAABB[2] = static_cast<float>(xMax);
            sn.shapeAABB[3] = static_cast<float>(yMax);
            // Clamp to parent clip (0x6BDE40..0x6BDE80)
            if (sn.parentClipIndex >= 0 &&
                sn.parentClipIndex < static_cast<int>(nodes.size())) {
                const auto &pc = nodes[sn.parentClipIndex];
                if (pc.shapeAABB[0] > sn.shapeAABB[0]) sn.shapeAABB[0] = pc.shapeAABB[0];
                if (pc.shapeAABB[1] > sn.shapeAABB[1]) sn.shapeAABB[1] = pc.shapeAABB[1];
                if (pc.shapeAABB[2] < sn.shapeAABB[2]) sn.shapeAABB[2] = pc.shapeAABB[2];
                if (pc.shapeAABB[3] < sn.shapeAABB[3]) sn.shapeAABB[3] = pc.shapeAABB[3];
            }
            sn.parentClipIndex = static_cast<int>(si);
        }

    }

    void Player::updateLayersPhase3_ShapeGeometry() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BDE94: Shape geometry computation (nodeType=1) ---
        // Aligned to 0x6BDE94. For nodeType=1 nodes with active slot,
        // compute shape vertices based on shapeType (0=point,1=circle,2=rect,3=quad).
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            if (sn.nodeType != 1 || sn.slotDone) continue;
            sn.shapeGeomType = sn.shapeType;
            switch (sn.shapeType) {
                case 0: // point (0x6BDF40)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    break;
                case 1: { // circle (0x6BDF50)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    sn.shapeVertices[2] = sn.accumulated.scaleX * 16.0 * 0.5;
                    break;
                }
                case 2: { // rect (0x6BDF70)
                    const double hw = sn.accumulated.scaleX * 16.0 * 0.5;
                    const double hh = sn.accumulated.scaleY * 16.0 * 0.5;
                    sn.shapeVertices[3] = sn.vertexPosX - hw;
                    sn.shapeVertices[4] = sn.vertexPosY - hh;
                    sn.shapeVertices[5] = sn.vertexPosX + hw;
                    sn.shapeVertices[6] = sn.vertexPosY + hh;
                    break;
                }
                case 3: { // quad (0x6BDFA8)
                    const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
                    const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
                    const double ox = sn.clipOriginX, oy = sn.clipOriginY;
                    const double oox = ox * m11 + oy * m12;
                    const double ooy = ox * m21 + oy * m22;
                    const double px = sn.vertexPosX, py = sn.vertexPosY;
                    const double ax = m11 * -8.0, bx = m12 * -8.0;
                    const double cx = m11 * 8.0,  dx = m12 * 8.0;
                    const double ay = m21 * -8.0, by = m22 * -8.0;
                    const double cy = m21 * 8.0,  dy = m22 * 8.0;
                    sn.shapeVertices[7]  = px + ax + bx - oox;
                    sn.shapeVertices[8]  = py + ay + by - ooy;
                    sn.shapeVertices[9]  = px + cx + bx - oox;
                    sn.shapeVertices[10] = py + cy + by - ooy;
                    sn.shapeVertices[11] = px + cx + dx - oox;
                    sn.shapeVertices[12] = py + cy + dy - ooy;
                    sn.shapeVertices[13] = px + ax + dx - oox;
                    sn.shapeVertices[14] = py + ay + dy - ooy;
                    break;
                }
                default: break;
            }
        }

    }

    // Helper: find node by label in the node tree (sub_6F2228 equivalent)
    static int findNodeByLabel(const std::vector<detail::MotionNode> &nodes,
                               const std::string &label) {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].layerName == label) return static_cast<int>(i);
        return -1;
    }

    void Player::updateLayersPhase3_MotionSubNode(double currentTime) {
        auto &nodes = _runtime->nodes;
        // Motion sub-node processing — aligned to sub_6BE0C0 (0x6BE0C0).
        // For each nodeType=3 (Motion) node, create/manage child Player instance.
        // Only runs when !isEmoteMode (0x6BE104).
        if (_runtime->isEmoteMode) return;

        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &mn = nodes[i];
            if (mn.nodeType != 3) continue;

            // Get parent's priorDraw flag as play trigger (v12, 0x6BE204..0x6BE214)
            // In libkrkr2.so: v12 = *(int*)(parentObj+48) where parentObj = node+8 or player+47*8
            int v12 = 0;
            if (mn.tjsLayerObject) {
                v12 = mn.priorDraw ? 1 : 0;
            } else {
                v12 = _priorDraw ? 1 : 0;
            }

            // Create child Player on demand (0x6BE220..0x6BE260)
            if (!mn.childPlayer) {
                mn.childPlayer = std::make_shared<Player>(_resourceManagerNative);
                mn.childPlayer->_tjsRandomGenerator = _tjsRandomGenerator;
                mn.childNeedsInit = true;
            }
            auto &child = *mn.childPlayer;

            // If no v12 flags and not visible → skip to LABEL_18 (0x6BE270)
            if (!v12 && !mn.accumulated.visible) {
                goto label_18;
            }

            // Check slotDone → clear child (0x6BE31C..0x6BE354)
            if (mn.slotDone) {
                child._allplaying = false;
                mn.childNeedsInit = true;
                goto label_18;
            }

            {
                // Get motion source from clip slot (0x6BE364)
                const auto &src = mn.interpolatedCache.src;
                if (!src.empty()) {
                    // Re-init gate: (v12 & 5) != 0 || mn.flags (0x6BE37C)
                    if ((v12 & 5) != 0 || (mn.flags & 0x01)) {
                        mn.flags |= 0x01; // mark as initialized (0x6BE388)

                        // Resolve motion and play (0x6BE3B4..0x6BE46C)
                        child.setChara(_chara);
                        child.onFindMotion(detail::widen(src));
                        mn.childNeedsInit = false;

                        // Time sync from parent loop time (0x6BE478..0x6BE4E8)
                        if (child._allplaying) {
                            double childTime = _frameLoopTime
                                - mn.interpolatedCache.motionTimeOffset
                                + mn.interpolatedCache.motionDofst;
                            if (_frameLastTime < 0.0) {
                                // Backward play: handle loop wrapping
                                double loopEnd = child._loopTime;
                                if (loopEnd >= 0.0) {
                                    double totalFrames = child._runtime ?
                                        child._runtime->timelines.begin()->second.totalFrames : 0;
                                    while (childTime >= totalFrames)
                                        childTime = childTime - totalFrames + loopEnd;
                                }
                            }
                            double totalFrames = 0;
                            if (child._runtime && !child._runtime->timelines.empty())
                                totalFrames = child._runtime->timelines.begin()->second.totalFrames;
                            childTime = std::max(childTime, 0.0);
                            if (childTime > totalFrames) childTime = totalFrames;
                            // Set child's current time directly
                            if (child._runtime) {
                                for (auto &[name, tl] : child._runtime->timelines) {
                                    tl.currentTime = childTime;
                                }
                                child._allplaying = true;
                                child._queuing = true;
                            }
                            if (!_allplaying) {
                                child._needsInternalAssignImages = true;
                            }
                        }
                    }
                }

                if (!child._runtime || !child._runtime->activeMotion) goto label_18;

                // === Angle interpolation (0x6BE534..0x6BEC9C) ===
                int angleMode = mn.interpolatedCache.motionDt;
                bool hasAngle = false;
                double computedAngle = 0.0;
                const double dofst = mn.interpolatedCache.motionDofst;

                if (angleMode != 0) {
                    switch (angleMode) {
                    case 1: // Direct angle (0x6BE5BC)
                        computedAngle = dofst + mn.accumulated.angle;
                        hasAngle = true;
                        break;
                    case 2: { // atan2 from delta position (0x6BE8C4)
                        double dy_comp, dx_comp;
                        if (mn.coordinateMode == 1) {
                            dy_comp = mn.deltaPosZ; // node+192
                            dx_comp = mn.deltaPosX; // node+176
                        } else {
                            dy_comp = mn.deltaPosY; // node+184
                            dx_comp = mn.deltaPosX; // node+176
                        }
                        computedAngle = dofst + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                        hasAngle = true;
                        break;
                    }
                    case 3: // Interpolated atan2 (0x6BE664..0x6BE79C)
                        // Simplified: use same as case 2 (full interpolation requires
                        // dual clip slot access which our architecture handles differently)
                        hasAngle = true;
                        computedAngle = dofst + mn.accumulated.angle;
                        break;
                    case 4: { // Target node lookup (0x6BE7B4)
                        const auto &dtgt = mn.interpolatedCache.motionDtgt;
                        if (!dtgt.empty()) {
                            int targetIdx = findNodeByLabel(nodes, dtgt);
                            if (targetIdx >= 0) {
                                const auto &target = nodes[targetIdx];
                                double dy_comp, dx_comp;
                                if (mn.coordinateMode == 1) {
                                    dy_comp = target.accumulated.posZ - mn.accumulated.posZ;
                                    dx_comp = target.accumulated.posX - mn.accumulated.posX;
                                } else {
                                    dy_comp = target.accumulated.posY - mn.accumulated.posY;
                                    dx_comp = target.accumulated.posX - mn.accumulated.posX;
                                }
                                computedAngle = dofst + std::atan2(dy_comp, dx_comp) * 360.0 / 6.28318531;
                            }
                        }
                        hasAngle = true;
                        break;
                    }
                    default: break;
                    }
                    // Normalize to [0, 360) (0x6BECA4..0x6BECB8)
                    while (computedAngle < 0.0) computedAngle += 360.0;
                    while (computedAngle >= 360.0) computedAngle -= 360.0;
                }

                // === Origin offset (0x6BE994..0x6BE9F4) ===
                double posX = mn.accumulated.posX;
                double posY = mn.accumulated.posY;
                double posZ = mn.accumulated.posZ;

                const double originX = mn.interpolatedCache.ox;
                const double originY = mn.interpolatedCache.oy;
                if (originX != 0.0 || originY != 0.0) {
                    const double negOY = -originY;
                    // v79 = m12*negOY - originX*m11 (0x6BE9E0)
                    const double vx = mn.accumulated.m12 * negOY - originX * mn.accumulated.m11;
                    // v80 = m22*negOY - originX*m21 (0x6BE9E4)
                    const double vy = mn.accumulated.m22 * negOY - originX * mn.accumulated.m21;
                    if (mn.coordinateMode == 1) {
                        posX += vx;
                        posZ += vy;
                    } else {
                        posX += vx;
                        posY += vy;
                    }
                }

                // === State propagation to child root node (0x6BEA18..0x6BEB74) ===
                if (child._runtime && !child._runtime->nodes.empty()) {
                    auto &cr = child._runtime->nodes[0];
                    cr.accumulated.posX = posX;
                    cr.accumulated.posY = posY;
                    cr.accumulated.posZ = posZ;
                    // Flip — only write if changed (0x6BEA28..0x6BEA54)
                    if (cr.accumulated.flipX != mn.accumulated.flipX ||
                        cr.accumulated.flipY != mn.accumulated.flipY) {
                        cr.accumulated.flipX = mn.accumulated.flipX;
                        cr.accumulated.flipY = mn.accumulated.flipY;
                    }
                    // Scale — only write if changed (0x6BEA5C..0x6BEA88)
                    if (cr.accumulated.scaleX != mn.accumulated.scaleX ||
                        cr.accumulated.scaleY != mn.accumulated.scaleY) {
                        cr.accumulated.scaleX = mn.accumulated.scaleX;
                        cr.accumulated.scaleY = mn.accumulated.scaleY;
                    }
                    // Slant (0x6BEB10..0x6BEB3C)
                    if (cr.accumulated.slantX != mn.accumulated.slantX ||
                        cr.accumulated.slantY != mn.accumulated.slantY) {
                        cr.accumulated.slantX = mn.accumulated.slantX;
                        cr.accumulated.slantY = mn.accumulated.slantY;
                    }
                    // Opacity (0x6BEB40..0x6BEB58)
                    cr.accumulated.opacity = mn.accumulated.opacity;
                    // Active — use node's own active, not accumulated (0x6BEB5C)
                    cr.accumulated.active = mn.accumulated.active;
                    // Blend mode (0x6BEB7C)
                    // node+100 = colorBytes[0..3] packed as int
                    cr.interpolatedCache.blendMode = mn.interpolatedCache.blendMode;

                    // zFactor propagation (0x6BEA94)
                    child._zFactor = _zFactor;

                    // === Angle → child (0x6BEAA8..0x6BEB08) ===
                    if (hasAngle) {
                        if (child._runtime->isEmoteMode) {
                            // Emote mode: set angle directly + reinit
                            // Player_initEmoteMotion(child, 2) equivalent
                        } else {
                            cr.accumulated.angle = computedAngle;
                        }
                    }

                    // === Matrix propagation (0x6BEB9C..0x6BEC4C) ===
                    if (hasAngle || computedAngle == mn.accumulated.angle ||
                        child._runtime->isEmoteMode) {
                        // Direct copy (0x6BEB9C)
                        cr.accumulated.m11 = mn.accumulated.m11;
                        cr.accumulated.m12 = mn.accumulated.m12;
                        cr.accumulated.m21 = mn.accumulated.m21;
                        cr.accumulated.m22 = mn.accumulated.m22;
                    } else {
                        // Rotate by (computedAngle - accumulated.angle) (0x6BEBC8..0x6BEC4C)
                        double delta = (computedAngle - mn.accumulated.angle)
                                       * 3.14159265 * 2.0 / 360.0;
                        if (mn.accumulated.flipX != mn.accumulated.flipY)
                            delta = -delta;
                        const double c = std::cos(delta);
                        const double s = std::sin(delta);
                        cr.accumulated.m11 = c * mn.accumulated.m11 + s * mn.accumulated.m12;
                        cr.accumulated.m12 = c * mn.accumulated.m12 - mn.accumulated.m11 * s;
                        cr.accumulated.m21 = c * mn.accumulated.m21 + s * mn.accumulated.m22;
                        cr.accumulated.m22 = c * mn.accumulated.m22 - mn.accumulated.m21 * s;
                    }

                    // Clip chain propagation (0x6BE278..0x6BE29C)
                    cr.parentClipIndex = mn.parentClipIndex;
                    cr.visibleAncestorIndex = mn.visibleAncestorIndex;
                }

                // === Step child (0x6BE2A4..0x6BE2AC) ===
                child._independentLayerInherit = _independentLayerInherit;
                child.frameProgress(_frameLastTime);  // NOT 0.0!
                if (child._runtime && !child._runtime->nodes.empty()) {
                    child.updateLayers(currentTime);
                }
            }
            continue;

        label_18:
            // Minimal propagation for inactive nodes (0x6BE278)
            if (mn.childPlayer && mn.childPlayer->_runtime &&
                !mn.childPlayer->_runtime->nodes.empty()) {
                auto &cr = mn.childPlayer->_runtime->nodes[0];
                cr.parentClipIndex = mn.parentClipIndex;
                cr.visibleAncestorIndex = mn.visibleAncestorIndex;
            }
        }

    }

    void Player::updateLayersPhase3_ParticleEmitter() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BEDD0: Particle emitter state (nodeType=6) ---
        // Aligned to 0x6BEDD0. Only when !isEmoteMode.
        // Manages emitter timer and trigger state for particle emitter nodes.
        if (!_runtime->isEmoteMode) {
            for (size_t ei = 1; ei < nodes.size(); ++ei) {
                auto &en = nodes[ei];
                if (en.nodeType != 6) continue;
                if (!en.accumulated.active || en.slotDone) {
                    // Clear emitter state (0x6BEEB0..0x6BEEC4)
                    en.emitterActive = false;
                    en.emitterDtgt.clear();
                    en.emitterTimer = 0.0;
                    continue;
                }
                // dtgt from interpolated cache (clip slot+356)
                const std::string &dtgt = en.interpolatedCache.src;
                if (dtgt.empty()) {
                    en.emitterActive = false;
                    en.emitterDtgt.clear();
                    en.emitterTimer = 0.0;
                    continue;
                }
                // Check if dtgt changed → reinit (0x6BEEF0..0x6BEF48)
                if (en.emitterActive && en.emitterDtgt != dtgt) {
                    en.emitterDtgt = dtgt;
                    en.emitterTimer = 0.0;
                } else if (!en.emitterActive) {
                    en.emitterActive = true;
                    en.emitterDtgt = dtgt;
                    en.emitterTimer = 0.0;
                }
                // Accumulate timer (0x6BEFAC..0x6BEFC0)
                en.emitterOffsetActive = false;
                en.emitterTimer += _frameLastTime;
                // Trigger type handling (0x6BEFC4..0x6BF0B8)
                const int triggerType = en.prtTrigger;
                if (triggerType == 4) {
                    // Target position mode: compute offset from target node
                    // Emission handled by sub_6BF0DC (particle system node)
                    en.emitterOffsetActive = false;
                } else if (triggerType == 3) {
                    // Interpolated emit: use delta position as offset
                    if (_frameLastTime != 0.0) {
                        en.emitterOffsetActive = true;
                        en.emitterOffsetX = en.deltaPosX;
                        en.emitterOffsetY = en.deltaPosY;
                        en.emitterOffsetZ = en.deltaPosZ;
                    }
                } else if (triggerType == 2) {
                    // Timer-based emission
                    // Emission handled by sub_6BF0DC (particle system node)
                }
                // Particle creation is handled by sub_6BF0DC (nodeType=4).
                // implemented. Emitter state is maintained for future use.
            }
        }

    }

    void Player::updateLayersPhase3_ParticleSystem(double currentTime) {
        auto &nodes = _runtime->nodes;
        // --- sub_6BF0DC: Particle system (nodeType=4) ---
        // Aligned to 0x6BF0DC. Only when !isEmoteMode.
        // Manages child Player instances per particle with physics stepping.
        if (!_runtime->isEmoteMode) {
            for (size_t pi = 1; pi < nodes.size(); ++pi) {
                auto &pn = nodes[pi];
                if (pn.nodeType != 4) continue;
                if (!pn.accumulated.active || pn.slotDone) {
                    // Inactive: clear all particles
                    pn.particleChildren.clear();
                    pn.particleStates.clear();
                    continue;
                }

                const double dt = _frameLastTime;

                // Find the emitter node (nodeType=6) that feeds this particle node.
                // In libkrkr2.so, the emitter's trigger drives particle creation.
                // Look for a nodeType=6 child of the same parent with matching dtgt.
                detail::MotionNode *emitter = nullptr;
                for (size_t ei = 1; ei < nodes.size(); ++ei) {
                    if (nodes[ei].nodeType == 6
                        && nodes[ei].parentIndex == pn.parentIndex
                        && nodes[ei].emitterActive) {
                        emitter = &nodes[ei];
                        break;
                    }
                }

                // Emit new particles when emitter triggers (0x6BF1F0..0x6BF3D0)
                if (emitter && emitter->emitterTimer > 0.0 && dt > 0.0) {
                    // Check if we should emit (frequency-based)
                    // prtF = emission frequency from interpolatedCache
                    const double freq = pn.interpolatedCache.prtF;
                    if (freq > 0.0 && pn.particleChildren.size()
                        < static_cast<size_t>(pn.particleMaxNum > 0 ? pn.particleMaxNum : 100)) {
                        // Create new particle child Player (0x6BF240..0x6BF390)
                        auto child = std::make_shared<Player>(_resourceManagerNative);
                        // Aligned to sub_6CED30: inherit parent's RandomGenerator
                        child->_tjsRandomGenerator = _tjsRandomGenerator;
                        child->setChara(_chara);
                        if (!emitter->emitterDtgt.empty()) {
                            child->onFindMotion(detail::widen(emitter->emitterDtgt));
                        }
                        child->_zFactor = _zFactor;
                        child->_independentLayerInherit = _independentLayerInherit;
                        child->_runtime->drawAffineMatrix = _runtime->drawAffineMatrix;

                        // Initialize particle state with random emission (0x6BF390..0x6BF5D0)
                        detail::MotionNode::ParticleState ps;
                        ps.posX = pn.accumulated.posX;
                        ps.posY = pn.accumulated.posY;
                        ps.posZ = pn.accumulated.posZ;
                        // Random velocity from prtV/prtVmin range
                        const double vel = pn.interpolatedCache.prtV;
                        // Random angle from prtRange
                        const double range = pn.interpolatedCache.prtRange;
                        const double emitAngle = pn.accumulated.angle
                            + (range > 0.0 ? (random() * 2.0 - 1.0) * range : 0.0);
                        const double rad = emitAngle * 3.14159265358979323846 / 180.0;
                        ps.velX = vel * std::cos(rad);
                        ps.velY = vel * std::sin(rad);
                        ps.angle = emitAngle;
                        ps.zoom = pn.interpolatedCache.prtZ;
                        ps.alive = true;

                        pn.particleChildren.push_back(std::move(child));
                        pn.particleStates.push_back(ps);
                    }
                }

                // Step all existing particles (0x6BF5D0..0x6BF9E0)
                for (size_t ci = 0; ci < pn.particleChildren.size(); ++ci) {
                    auto &child = pn.particleChildren[ci];
                    auto &ps = pn.particleStates[ci];
                    if (!ps.alive || !child) continue;

                    // Physics step: velocity + acceleration (0x6BF660..0x6BF750)
                    const double accel = pn.interpolatedCache.prtA;
                    ps.velX += accel * std::cos(ps.angle * 3.14159265358979323846 / 180.0) * dt;
                    ps.velY += accel * std::sin(ps.angle * 3.14159265358979323846 / 180.0) * dt;

                    // Position update
                    ps.posX += ps.velX * dt;
                    ps.posY += ps.velY * dt;

                    // Propagate to child Player root node (0x6BF750..0x6BF850)
                    if (child->_runtime && !child->_runtime->nodes.empty()) {
                        auto &cr = child->_runtime->nodes[0];
                        cr.accumulated.posX = ps.posX;
                        cr.accumulated.posY = ps.posY;
                        cr.accumulated.posZ = ps.posZ;
                        cr.accumulated.scaleX = pn.accumulated.scaleX * ps.zoom;
                        cr.accumulated.scaleY = pn.accumulated.scaleY * ps.zoom;
                        cr.accumulated.opacity = pn.accumulated.opacity;
                        cr.accumulated.visible = true;
                        cr.accumulated.active = true;
                    }

                    // Step child Player (0x6BF850..0x6BF8E0)
                    child->frameProgress(dt);
                    if (child->_runtime && !child->_runtime->nodes.empty()) {
                        child->updateLayers(currentTime);
                    }
                }

                // Enforce max particle count (0x6BF9E0..0x6BFA20)
                const int maxNum = pn.particleMaxNum > 0 ? pn.particleMaxNum : 100;
                while (static_cast<int>(pn.particleChildren.size()) > maxNum) {
                    pn.particleChildren.erase(pn.particleChildren.begin());
                    pn.particleStates.erase(pn.particleStates.begin());
                }
            }
        }

    }

    void Player::updateLayersPhase3_AnchorNode() {
        auto &nodes = _runtime->nodes;
        // --- sub_6C0528: Anchor node processing (nodeType=10) ---
        // Aligned to 0x6C0528. For each nodeType=10 active node,
        // apply exponential damping toward root node values.
        for (size_t ai = 1; ai < nodes.size(); ++ai) {
            auto &an = nodes[ai];
            if (an.nodeType != 10 || !an.accumulated.active) continue;
            _needsInternalAssignImages = true;
            if (_frameLastTime == 0.0) {
                an.anchorEnabled = false;
                continue;
            }
            an.anchorEnabled = true;
            // Read width/height (0x6C0790..0x6C0848)
            double cw = an.interpolatedCache.width;
            double ch = an.interpolatedCache.height;
            if (cw <= 0.0) cw = 32.0;
            if (ch <= 0.0) ch = 32.0;
            an.clipW = cw;
            an.clipH = ch;
            an.originX = cw * 0.5;
            an.originY = ch * 0.5;

            // Damping exponent (0x6C088C..0x6C08B8)
            // From decompilation: v28 = dt * (v27*dt/v27) / v27 / 60 / damping
            // where v27 = dt/fps. Simplifies to dt*fps/60/damping for dt~1 frame.
            const double dampPow = std::abs(_frameLastTime) / 60.0
                / std::max(an.anchorDamping, 0.001);

            // Angle damping (0x6C08C0..0x6C08E0)
            double angle = an.accumulated.angle;
            if (angle >= 180.0)
                angle = 360.0 - (360.0 - angle) * dampPow;
            else
                angle = angle * dampPow;
            an.accumulated.angle = angle;

            // Scale damping (0x6C08E0..0x6C0924)
            an.accumulated.scaleX = std::pow(
                an.accumulated.scaleX * 32.0 / cw, dampPow);
            an.accumulated.scaleY = std::pow(
                an.accumulated.scaleY * 32.0 / ch, dampPow);

            // Slant damping (0x6C0924..0x6C0938)
            an.accumulated.slantX *= dampPow;
            an.accumulated.slantY *= dampPow;

            // Rebuild local matrix via sub_699940 (0x6C0944)
            {
                FrameContentState tmpState;
                tmpState.angle = an.accumulated.angle;
                tmpState.scaleX = an.accumulated.scaleX;
                tmpState.scaleY = an.accumulated.scaleY;
                tmpState.slantX = an.accumulated.slantX;
                tmpState.slantY = an.accumulated.slantY;
                tmpState.flipX = an.accumulated.flipX;
                tmpState.flipY = an.accumulated.flipY;
                if (an.interpolatedCache.hasTransformOrder) {
                    std::copy(std::begin(an.interpolatedCache.transformOrder),
                              std::end(an.interpolatedCache.transformOrder),
                              tmpState.transformOrder);
                }
                Affine2x3 la = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(la, tmpState);
                an.accumulated.m11 = la[0]; an.accumulated.m21 = la[1];
                an.accumulated.m12 = la[2]; an.accumulated.m22 = la[3];
            }

            // If !independentLayerInherit: multiply with root (0x6C094C)
            if (!_independentLayerInherit && !nodes.empty()) {
                const auto &rn = nodes[0];
                const double nm11 = an.accumulated.m11, nm12 = an.accumulated.m12;
                const double nm21 = an.accumulated.m21, nm22 = an.accumulated.m22;
                an.accumulated.m11 = rn.accumulated.m11*nm11 + rn.accumulated.m12*nm21;
                an.accumulated.m21 = rn.accumulated.m21*nm11 + rn.accumulated.m22*nm21;
                an.accumulated.m12 = rn.accumulated.m11*nm12 + rn.accumulated.m12*nm22;
                an.accumulated.m22 = rn.accumulated.m21*nm12 + rn.accumulated.m22*nm22;
            }

            // Opacity damping (0x6C0994..0x6C09F8)
            {
                int opa = an.accumulated.opacity;
                double opaF = static_cast<double>(opa) / 255.0;
                if (opa == 0) opaF = 1.0 / 255.0;
                double newOpa = std::pow(opaF, dampPow) * 255.0 * an.anchorOpaScale;
                newOpa = std::clamp(newOpa, 0.0, 255.0);
                an.accumulated.opacity = static_cast<int>(newOpa);
                double denom = newOpa;
                if (static_cast<int>(newOpa) < 0) denom += 4294967296.0;
                if (denom != 0.0) an.anchorOpaScale = newOpa / denom;
            }

            // Position lerp toward root (0x6C0A04..0x6C0A4C)
            if (!nodes.empty()) {
                const auto &rn = nodes[0];
                an.accumulated.posX = rn.accumulated.posX
                    + dampPow * (an.accumulated.posX - rn.accumulated.posX);
                an.accumulated.posY = rn.accumulated.posY
                    + dampPow * (an.accumulated.posY - rn.accumulated.posY);
                an.accumulated.posZ = rn.accumulated.posZ
                    + dampPow * (an.accumulated.posZ - rn.accumulated.posZ);
            }

            // Color damping (0x6C0A68..0x6C0C58)
            // Per-channel pow(channel/base, dampPow)*base*colorScale
            {
                const bool isDefaultBlend =
                    (an.interpolatedCache.blendMode & 0xF0) == 0x10;
                const double base = isDefaultBlend ? 255.0 : 255.0;
                const int cR = an.interpolatedCache.colorR;
                const int cG = an.interpolatedCache.colorG;
                const int cB = an.interpolatedCache.colorB;
                const int cA = an.interpolatedCache.colorA;
                const bool allEqual = (cR == cG && cG == cB && cB == cA);
                if (!(allEqual && cR == 0x80 && cA == 0xFF)) {
                    int iters = (allEqual) ? 1 : 4;
                    for (int ci = 0; ci < iters && ci < 4; ++ci) {
                        for (int ch = 0; ch < 3; ++ch) {
                            double v = static_cast<double>(an.colorBytes[ci*4+ch]);
                            if (v == 0.0) v = 1.0;
                            double res = base * std::pow(v / base, dampPow)
                                * an.anchorColorScale[ci*4+ch];
                            res = std::clamp(res, 0.0, 255.0);
                            int ir = static_cast<int>(res);
                            double dr = static_cast<double>(ir);
                            if (dr != 0.0) an.anchorColorScale[ci*4+ch] = res / dr;
                            an.colorBytes[ci*4+ch] = static_cast<uint8_t>(ir);
                        }
                        // Alpha channel (0x6C0BA8..0x6C0BE0)
                        double av = static_cast<double>(an.colorBytes[ci*4+3]) / 255.0;
                        if (av == 0.0) av = 1.0 / 255.0;
                        double ares = std::pow(av, dampPow) * 255.0
                            * an.anchorColorScale[ci*4+3];
                        ares = std::clamp(ares, 0.0, 255.0);
                        int iar = static_cast<int>(ares);
                        double dar = static_cast<double>(iar);
                        if (dar != 0.0) an.anchorColorScale[ci*4+3] = ares / dar;
                        an.colorBytes[ci*4+3] = static_cast<uint8_t>(iar);
                    }
                    if (allEqual) {
                        std::memcpy(&an.colorBytes[4], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[8], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[12], &an.colorBytes[0], 4);
                    }
                }
            }
        }

    }

    // --- updateLayers: 3-phase pipeline ---
    // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C).
    // Operates on persistent MotionNode vector instead of re-walking PSB tree.
    void Player::updateLayers(double currentTime) {
        auto &nodes = _runtime->nodes;
        if (nodes.empty()) return;

        updateLayersPhase1_PreLoop(currentTime);
        updateLayersPhase2_MainLoop(currentTime);

        // === PHASE 3: Post-loop processing ===
        // Call order matches libkrkr2.so Player_updateLayers (0x6BBC60..0x6BBCA8):
        // sub_6BC000 → sub_6BC4F0 → sub_6BD8DC → sub_6BDA28 →
        // sub_6BDCC0 → sub_6BDE94 → sub_6BE0C0 → sub_6BEDD0 →
        // sub_6BF0DC → sub_6C0528
        updateLayersPhase3_CameraConstraint();
        updateLayersPhase3_VertexComputation();
        updateLayersPhase3_Visibility();
        updateLayersPhase3_CameraNode();
        updateLayersPhase3_ShapeAABB();
        updateLayersPhase3_ShapeGeometry();
        updateLayersPhase3_MotionSubNode(currentTime);
        updateLayersPhase3_ParticleEmitter();
        updateLayersPhase3_ParticleSystem(currentTime);
        updateLayersPhase3_AnchorNode();

        // === Post-loop cleanup ===
        // Aligned to 0x6BBCB4..0x6BBE1C: clear per-node flags and timeline state.

        // Clear node+44 (flags byte) and node+1504 (accumulated visible)
        // for all non-root nodes (0x6BBCFC..0x6BBD40).
        for (size_t ci = 1; ci < nodes.size(); ++ci) {
            nodes[ci].flags &= ~0x01;           // node+44
            nodes[ci].accumulated.visible = false; // node+1504
        }
    }

} // namespace motion
