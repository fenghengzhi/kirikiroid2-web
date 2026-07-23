// PlayerRenderExecute.cpp — render command build and execution
// Split from PlayerRender.cpp for maintainability.
//
#include "PlayerRenderInternal.h"
#include "MotionTraceWeb.h"
#include "PrivateMotionGLL.h"
#include "SourceCache.h"

using namespace motion::internal;
using namespace motion::internal::render_detail;

namespace motion {

    namespace {
        std::array<int, 4> integralClipRect(
            const std::array<float, 4> &rect) {
            return {
                static_cast<int>(rect[0]), static_cast<int>(rect[1]),
                static_cast<int>(rect[2]), static_cast<int>(rect[3])
            };
        }

        tjs_int propGetIntOnceLike_0x6635DC(ncbPropAccessor &accessor,
                                            const tjs_char *member,
                                            tjs_uint32 *hint) {
            tTJSVariant value;
            iTJSDispatch2 *dispatch = accessor.GetDispatch();
            (void)dispatch->PropGet(0, member, hint, &value, dispatch);
            return static_cast<tjs_int>(value.AsInteger());
        }
    }

    // libkrkr2.so sub_6C4E28 @0x6C5264..0x6C5D98 Loop A drawable body (J1/J7):
    // materialize the per-item LEAF layer (item+304) on the persistent
    // SeparateLayerAdaptor Rb_tree (player+760), keyed by the layerId latched at
    // item+424, then size it to the clip rect and affineCopy/meshCopy/
    // bezierPatchCopy the resolved source onto it. The points were already
    // pre-translated to be clip-local (corners/meshPoints had -0.5-clipOrigin
    // baked in by buildRenderCommands), so the copy uses a zero extra offset.
    //
    // This relocates the leaf-copy emit out of executeLayerRenderCommands
    // (where the port previously folded it into buildItemOutput) into the build
    // pass, matching the binary's two-function pipeline: 0x6C4E28 emits leaf
    // copies, 0x6C7440 only submits. The previous _renderLayerStates container
    // was a port invention (no HM correspondence in the 1384B Player binary);
    // the binary's leaf layers live ONLY on the SLA Rb_tree.
    bool Player::emitLeafLayerCopyLike_0x6C4E28(
        detail::PreparedRenderItem &item,
        iTJSDispatch2 *scratchOwner,
        iTJSDispatch2 *scratchParent,
        const std::string &motionPath) {
        if(!_renderSeparateLayerAdaptor) {
            return false;
        }
        const int clipWidth = item.clipRect[2] - item.clipRect[0];
        const int clipHeight = item.clipRect[3] - item.clipRect[1];
        if(clipWidth <= 0 || clipHeight <= 0) {
            return false;
        }

        const tjs_int leafBlendMode = 0;
        const std::array<std::uint32_t, 4> leafColors{
            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
        };

        // sub_6C6B48(player+760, item+424, ...) -> item+304 leaf layer. The SLA
        // resolve mirrors 0x6C6B48: Rb_tree get-or-create keyed by the layerId,
        // reuse-from-retired, create Layer via Window.mainWindow Layer ctor,
        // set absolute (= SLA+160 + SLA+164, then ++SLA+164 == J6) and
        // hitThreshold=256. The returned variant is item+304.
        iTJSDispatch2 *ownerObject =
            scratchParent ? scratchParent : scratchOwner;
        NativeSLAPayloadLike_0x6DCD0C payload =
            NativeSLAPayloadLike_0x6DCD0C::fromLayerVariant(
                item.leafLayer,
                static_cast<tjs_uint32>(item.renderLayerId));
        bool createdOrChanged = false;
        tTJSVariant leafVariant =
            _renderSeparateLayerAdaptor->resolveRenderLayerNodeLike_0x6C6B48(
                static_cast<tjs_uint32>(item.renderLayerId), payload,
                ownerObject,
                createdOrChanged);
        item.leafLayer = leafVariant;
        iTJSDispatch2 *leafLayerObject =
            leafVariant.Type() == tvtObject ? leafVariant.AsObjectNoAddRef()
                                            : nullptr;
        auto *leafLayer = resolveNativeLayer(leafLayerObject);
        if(!leafLayerObject || !leafLayer) {
            return false;
        }

        // 0x6C4E28 initializes a caller-local neutral descriptor payload before
        // resolving this leaf source: blendMode=0 @0x6C52AC and four opaque-white
        // colors @0x6C5300..0x6C5304.  It then writes key/src from the item and
        // calls the shared sub_6C1B70 resolver @0x6C5664.  The ordinary execute
        // caller instead writes the item's blend/colors, so do not mutate the
        // item or move this leaf policy into the shared resolver.
        ncbPropAccessor descriptor{tTJSVariant(_sourceDescriptor)};
        descriptor.SetValue(TJS_W("key"), item.commandKey,
                            TJS_MEMBERENSURE,
                            &detail::commandKeyMemberHint_guess);
        descriptor.SetValue(TJS_W("src"), item.commandSrc,
                            TJS_MEMBERENSURE,
                            &detail::commandSrcMemberHint_guess);
        descriptor.SetValue(TJS_W("blendMode"), leafBlendMode,
                            TJS_MEMBERENSURE,
                            &detail::blendModeMemberHint_guess);

        ncbPropAccessor color{tTJSVariant(_sourceColors)};
        for(tjs_int index = 0; index < 4; ++index) {
            color.SetValue(
                index,
                leafColors[static_cast<std::size_t>(index)],
                TJS_MEMBERENSURE);
        }

        const tTJSVariant sourceObject =
            resolveRenderSourceLike_0x6C1B70_guess(
                item.sourceState->object);
        ncbPropAccessor sourceAccessor{tTJSVariant(sourceObject)};
        const tjs_int srcW = propGetIntOnceLike_0x6635DC(
            sourceAccessor, TJS_W("width"),
            &detail::widthMemberHint_guess);
        const tjs_int srcH = propGetIntOnceLike_0x6635DC(
            sourceAccessor, TJS_W("height"),
            &detail::heightMemberHint_guess);

        // sub_6C4E28 sets neutralColor then setSize(clip) on the leaf layer
        // before the copy; prepareLayerForRender folds the size + transparent
        // clear used by the leaf path (J11 coarse equivalent of neutralColor=0
        // + affineCopy(clear) initialization).
        if(!prepareLayerForRender(leafLayerObject, clipWidth, clipHeight,
                                  0x00000000)) {
            return false;
        }
        const tTVPRect sourceRect(0, 0, srcW, srcH);

        if(item.meshType == 0) {
            // sub_6C4E28 @0x6c5968 affineCopy block, points already clip-local.
            const auto localPts =
                buildAffineTrianglePoints(item.localCorners, 0.0f, 0.0f);
            if(TJS_FAILED(callLayerAffineCopyLike_0x6C7440(
                   leafLayerObject, localPts.data(), sourceObject, sourceRect,
                   stNearest, _clearEnabled))) {
                return false;
            }
        } else {
            if(item.localMeshPoints.empty()) {
                return false;
            }
            std::array<tjs_int, 2> cellDivisions{
                item.meshDivX, item.meshDivY
            };
            if(item.meshType == 1) {
                cellDivisions = bezierPatchCellDivisionsLike_0x6C5C00(
                    item.commandPatchDivision,
                    item.sourceState
                        ? item.sourceState->width
                        : item.nativeNode->source.width,
                    item.sourceState
                        ? item.sourceState->height
                        : item.nativeNode->source.height);
            } else if(item.meshType == 2) {
                if(item.meshDivX < 1 || item.meshDivY < 1) {
                    return false;
                }
            } else {
                return false;
            }
            iTJSDispatch2 *meshArray =
                buildMeshPointTJSArrayLike_0x6C715C(item.localMeshPoints, 0.0f,
                                                    0.0f);
            if(!meshArray) {
                return false;
            }
            tjs_error meshResult = TJS_E_FAIL;
            if(item.meshType == 1) {
                // sub_6C4E28 @0x6c5ba0 bezierPatchCopy block.
                meshResult = callLayerBezierPatchCopyLike_0x6C7440(
                    leafLayerObject, sourceObject, sourceRect, meshArray,
                    cellDivisions[0], cellDivisions[1], stNearest,
                    _clearEnabled);
            } else if(item.meshType == 2) {
                // sub_6C4E28 @0x6c5810 meshCopy block.
                meshResult = callLayerMeshCopyLike_0x6C7440(
                    leafLayerObject, sourceObject, sourceRect, meshArray,
                    cellDivisions[0], cellDivisions[1], stNearest,
                    _clearEnabled);
            }
            meshArray->Release();
            if(TJS_FAILED(meshResult)) {
                return false;
            }
        }

        item.builtRect = integralClipRect(item.clipRect);
        detail::logoChainTraceLogf(
            motionPath, "buildCommands.leafCopy", "0x6C4E28", _clampedEvalTime,
            "nodeIndex={} layerId={} clipRect=[{},{},{},{}] meshType={}",
            item.nodeIndex, item.renderLayerId, item.clipRect[0],
            item.clipRect[1],
            item.clipRect[2], item.clipRect[3], item.meshType);
        return true;
    }

    // libkrkr2.so sub_6C4E28 @0x6C5E7C..0x6C63AC Loop B (group/composed compose).
    // For each group item: union the visible child clip rects (child+21 set),
    // intersect with the camera clip (a4) and the group paintBox. If the union
    // is empty, grp+21=0. Otherwise create/refresh the composed Layer (item+324)
    // via Window.mainWindow's Layer ctor, setSize/fillRect(0), then for each
    // visible child (child+21 && child+320) apply its leaf (child+304) as an
    // alpha mask via Motion_doAlphaMaskOperation, and write grp+21=1, grp+16=0,
    // grp+216..228=composed clip. Inert for the logo fixtures (no group item
    // reaches a non-empty child-clip union).
    void Player::composeGroupLayersLike_0x6C4E28(
        detail::PreparedRenderItemList &auxList,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        iTJSDispatch2 *scratchOwner,
        iTJSDispatch2 *scratchParent,
        const std::string &motionPath) {
        if(auxList.empty()) {
            return;
        }
        const float cameraLeft = 0.0f;
        const float cameraTop = 0.0f;
        const float cameraRight = static_cast<float>(canvasWidth);
        const float cameraBottom = static_cast<float>(canvasHeight);

        for(auto *grpPtr : auxList) {
            if(!grpPtr) {
                continue;
            }
            auto &grp = *grpPtr;
            // Seed the union with the group paintBox (item+184..196), then
            // accumulate each visible child's paintBox (child+184..196),
            // mirroring sub_6C4E28's `for child: if(child+21) min/max`.
            float unionLeft = grp.paintBox[0];
            float unionTop = grp.paintBox[1];
            float unionRight = grp.paintBox[2];
            float unionBottom = grp.paintBox[3];
            for(auto *childPtr : grp.childItems) {
                if(!childPtr || !childPtr->rawFlag21) {
                    continue;
                }
                auto &child = *childPtr;
                // sub_6C4E28 @0x6c5eb0 unions over each visible child's
                // paintBox (child+184..196 = v101[46..49]), gated on child+21,
                // NOT the child clipRect (+216..228): min(left/top),
                // max(right/bottom).
                unionLeft = std::min(unionLeft, child.paintBox[0]);
                unionTop = std::min(unionTop, child.paintBox[1]);
                unionRight = std::max(unionRight, child.paintBox[2]);
                unionBottom = std::max(unionBottom, child.paintBox[3]);
            }
            // sub_6C4E28 @0x6c5eec: clamp the union to the camera clip (a4):
            // max(a4.left, union.left) / max(a4.top, union.top) /
            // min(union.right, a4.right) / min(union.bottom, a4.bottom). These
            // camera-clamped values (v102/v97/v100/v105) drive the EMPTY test.
            const float camClampedLeft = std::max(cameraLeft, unionLeft);
            const float camClampedTop = std::max(cameraTop, unionTop);
            const float camClampedRight = std::min(unionRight, cameraRight);
            const float camClampedBottom = std::min(unionBottom, cameraBottom);

            // sub_6C4E28 @0x6c5f20: v109/v108/v107/v106 start as the
            // camera-clamped values, then (if the group has its OWN valid
            // viewport, grp+208>=grp+200 && grp+212>=grp+204) get narrowed by
            // floor(left/top)/ceil(right/bottom) of grp+200..212. These
            // viewport-narrowed values drive the composed SIZE and grp+216..228.
            float finalLeft = camClampedLeft;
            float finalTop = camClampedTop;
            float finalRight = camClampedRight;
            float finalBottom = camClampedBottom;
            if(grp.viewport[2] >= grp.viewport[0] &&
               grp.viewport[3] >= grp.viewport[1]) {
                const float vfLeft = floorf(grp.viewport[0]);
                const float vfTop = floorf(grp.viewport[1]);
                const float vcRight = ceilf(grp.viewport[2]);
                const float vcBottom = ceilf(grp.viewport[3]);
                if(vfLeft >= camClampedLeft) finalLeft = vfLeft;
                if(vfTop >= camClampedTop) finalTop = vfTop;
                if(camClampedRight >= vcRight) finalRight = vcRight;
                if(camClampedBottom >= vcBottom) finalBottom = vcBottom;
            }

            // sub_6C4E28 @0x6c5f90: empty test uses the CAMERA-clamped values
            // (v102>v100 || v97>v105), NOT the viewport-narrowed ones.
            if(camClampedLeft > camClampedRight ||
               camClampedTop > camClampedBottom) {
                // sub_6C4E28 @0x6c6000: empty union -> grp+21 = 0.
                grp.rawFlag21 = false;
                continue;
            }

            const float unionLeftF = finalLeft;
            const float unionTopF = finalTop;
            const float unionRightF = finalRight;
            const float unionBottomF = finalBottom;
            const int composedWidth =
                static_cast<int>(unionRightF - unionLeftF);
            const int composedHeight =
                static_cast<int>(unionBottomF - unionTopF);
            if(composedWidth <= 0 || composedHeight <= 0) {
                grp.rawFlag21 = false;
                continue;
            }

            // sub_6C4E28 @0x6c5f94: if(!grp+340) create composed Layer(item+324).
            // The composed-built gate is the local composedBuilt; create the
            // layer object lazily and size/clear it.
            iTJSDispatch2 *composedLayerObject = ensureReusableLayerObject(
                grp.composedLayer, scratchOwner, scratchParent,
                static_cast<tTVPLayerType>(ltAlpha), false);
            auto *composedLayer = resolveNativeLayer(composedLayerObject);
            if(!composedLayerObject || !composedLayer) {
                grp.rawFlag21 = false;
                continue;
            }
            if(!prepareLayerForRender(composedLayerObject, composedWidth,
                                      composedHeight, 0x00000000)) {
                grp.rawFlag21 = false;
                continue;
            }

            // sub_6C4E28 @0x6c62c8 child alpha-mask loop: for child in grp+24:
            //   if (child+21 && child+320): Motion_doAlphaMaskOperation(
            //       grp+324, (int)(child+216 - v109), (int)(child+220 - v108),
            //       child+304, 0,0, child+224-child+216, child+228-child+220,
            //       64, player+1148 stencilType, grp+244)
            // NOTE on the gate: item+320 is initialized to 0 in build
            // (0x6C2334 @0x6c2774..) and is NEVER written to a non-zero value
            // anywhere in build (0x6C2334), this emit (0x6C4E28), or execute
            // (0x6C7440) — verified by grep across all three. So `child+320` is
            // ALWAYS 0 in this shipped build, making the child alpha-mask loop
            // binary-inert. The local PreparedRenderItem has no item+320 field
            // (no producer to mirror); the loop is reproduced for structural
            // fidelity but its body is unreachable, matching the binary. The
            // child offset uses child clipRect (child+216..228 = item+216) minus
            // the viewport-narrowed origin (v109/v108 = finalLeft/finalTop).
            const int playerStencilType = _maskMode;
            for(auto *childPtr : grp.childItems) {
                if(!childPtr) {
                    continue;
                }
                auto &child = *childPtr;
                // child+21 && child+320; child+320 is always 0 (see note) so
                // this gate is always false — the binary never enters the body.
                const bool childHasLeafContent320 = false; // item+320 == 0 always
                if(!child.rawFlag21 || !childHasLeafContent320) {
                    continue;
                }
                auto *childMaskLayerObject =
                    child.leafLayer.Type() == tvtObject
                        ? child.leafLayer.AsObjectNoAddRef()
                        : nullptr;
                if(!childMaskLayerObject) {
                    continue;
                }
                const int childWidth = child.clipRect[2] - child.clipRect[0];
                const int childHeight = child.clipRect[3] - child.clipRect[1];
                if(childWidth <= 0 || childHeight <= 0) {
                    continue;
                }
                applyMotionAlphaMaskLike_0x6AF104(
                    composedLayerObject,
                    child.clipRect[0] - static_cast<int>(unionLeftF),
                    child.clipRect[1] - static_cast<int>(unionTopF),
                    childMaskLayerObject, 0, 0, childWidth, childHeight, 64,
                    playerStencilType, grp.stencilComposite, motionPath,
                    _clampedEvalTime, grp.nodeIndex, child.nodeIndex);
            }

            // sub_6C4E28 @0x6c6380: grp+21=1, grp+16=0, grp+216..228 =
            // viewport-narrowed union (v109/v108/v107/v106).
            grp.rawFlag21 = true;
            grp.rawFlag16 = false;
            grp.clipRect = {
                unionLeftF, unionTopF, unionRightF, unionBottomF
            };
            grp.composedBuilt = true;
            grp.builtRect = integralClipRect(grp.clipRect);
        }
    }

    bool Player::buildRenderCommands(
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        detail::PreparedRenderItemList &mainList,
        detail::PreparedRenderItemList &auxList) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderBuildCommandsEnter(
            this, static_cast<int>(canvasWidth), static_cast<int>(canvasHeight),
            mainList, auxList);
#endif
        const auto motionPath = matchedMotionPath();
        // Scratch owner/parent for the SLA leaf/composed Layer ctor — resolved
        // once for the whole build pass (mirrors 0x6C4E28's
        // Window.mainWindow.primaryLayer lookups). Only consumed by the
        // drawable-branch leaf emit and Loop B, both inert for the logo
        // fixtures (all mainList items have drawFlag19=0).
        iTJSDispatch2 *scratchOwner = resolveMainWindowOwnerObject();
        iTJSDispatch2 *scratchParent = resolveMainWindowPrimaryLayerObject();
        for(auto *entryPtr : mainList) {
            if(!entryPtr) {
                continue;
            }
            auto &entry = *entryPtr;
            // libkrkr2.so sub_6C4E28 works in-place on the render item list
            // built by sub_6C2334. It does not blanket-clear +20/+21 or
            // +216..228: item+19==0 leaves those fields untouched, and failed
            // intersections only write item+21=0. Local execution-only state
            // is reset here; native fields were restored in 0x6C2334 setup.
            entry.builtRect = {0, 0, 0, 0};
            entry.leafBuilt = false;
            entry.composedBuilt = false;
            entry.executedDirect = false;

            RenderClipRect clipRect;
            std::string clipFailureReason;
            const bool drawableGate = entry.drawFlag && !entry.rawFlag16;
            if(!entry.drawFlag) {
                // libkrkr2.so sub_6C4E28 only materializes item+21 and
                // item+216..228 for item+19 entries. Ordinary direct items are
                // clipped and submitted later by sub_6C7440 from item+184..212.
                // Because this branch skips the native writer entirely, keep
                // the restored +21/+216..228 values intact.
            } else if(!drawableGate ||
                      !computeRenderClipRect(entry, canvasWidth, canvasHeight,
                                             clipRect, &clipFailureReason)) {
                entry.rawFlag21 = false;
                detail::logoChainTraceCheck(
                    motionPath, "renderItem.clip", "0x6C4E28",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox∩viewport exp paintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] viewport={}",
                        entry.paintBox[0], entry.paintBox[1], entry.paintBox[2],
                        entry.paintBox[3],
                        entry.hasViewport
                            ? fmt::format("[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                          entry.viewport[0], entry.viewport[1],
                                          entry.viewport[2], entry.viewport[3])
                            : std::string("<invalid default>")),
                    fmt::format("nodeIndex={} act=<invalid:{}>",
                                entry.nodeIndex, clipFailureReason),
                    false,
                    "sub_6C4E28 produced an invalid local clip rect");
            } else {
                entry.rawFlag21 = true;
                entry.clipRect = {
                    static_cast<float>(clipRect.left),
                    static_cast<float>(clipRect.top),
                    static_cast<float>(clipRect.right),
                    static_cast<float>(clipRect.bottom)
                };
                entry.dirtyRect = integralClipRect(entry.clipRect);

                // libkrkr2.so sub_6C4E28 @0x6C5DBC: the drawable branch
                // (drawFlag19 && clip valid && !item+16) performs the
                // requireLayerId materialization HERE, in the build loop, not
                // in execute. Gate on player+760 (the persistent
                // SeparateLayerAdaptor) — create it lazily from
                // Window.mainWindow.primaryLayer when absent (the
                // `player+760==0` branch at 0x6c4fa8..0x6c5138), then reach the
                // LABEL_28 latch (0x6c514c): if item+20 is still 0, resolve the
                // layer id via requireLayerId, write item+424 (layerId), and
                // set item+20=1. The latch is once-only and never cleared in
                // the loop, so it persists across frames.
                if(!_renderSeparateLayerAdaptor) {
                    iTJSDispatch2 *primaryLayer =
                        resolveMainWindowPrimaryLayerObject();
                    tTJSVariant targetLayer =
                        primaryLayer ? tTJSVariant(primaryLayer, primaryLayer)
                                     : tTJSVariant();
                    _renderSeparateLayerAdaptor =
                        new SeparateLayerAdaptor(targetLayer);
                }
                if(!entry.rawFlag20) {
                    // P3-B (c): LABEL_28 @0x6c514c allocates a FRESH layer id via
                    //   the no-arg RM dispatch FuncCall (requireLayerId,
                    //   numparams=0, 0x6c51a4-c4) → item+424, then sets the
                    //   item+20 latch so it is emitted once. Binary does NOT look
                    //   up / reuse a node's layerId by name (the by-name reuse was
                    //   a port invention; "requireLayerId" is only ever called
                    //   numparams=0). Every drawable item that reaches this latch
                    //   gets its own fresh id, unconditionally.
                    entry.renderLayerId = dispatchRequireLayerId();
                    entry.rawFlag20 = true;
                }

                for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                    entry.localCorners[ci] =
                        entry.corners[ci] - 0.5f - static_cast<float>(clipRect.left);
                    entry.localCorners[ci + 1] =
                        entry.corners[ci + 1] - 0.5f - static_cast<float>(clipRect.top);
                }

                const auto &renderMeshPoints = entry.meshType == 2
                    ? entry.commandCompositeMeshPoints
                    : entry.meshPoints;
                entry.localMeshPoints.clear();
                entry.localMeshPoints.reserve(renderMeshPoints.size());
                for(const auto &point : renderMeshPoints) {
                    entry.localMeshPoints.push_back({
                        point.x - 0.5f - static_cast<float>(clipRect.left),
                        point.y - 0.5f - static_cast<float>(clipRect.top)
                    });
                }

                std::array<float, 8> expectedLocalCorners{};
                bool cornersOk = true;
                for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                    expectedLocalCorners[ci] =
                        entry.corners[ci] - 0.5f - static_cast<float>(clipRect.left);
                    expectedLocalCorners[ci + 1] =
                        entry.corners[ci + 1] - 0.5f - static_cast<float>(clipRect.top);
                    if(std::fabs(expectedLocalCorners[ci] -
                                 entry.localCorners[ci]) > 0.01f ||
                       std::fabs(expectedLocalCorners[ci + 1] -
                                 entry.localCorners[ci + 1]) > 0.01f) {
                        cornersOk = false;
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "renderItem.clip", "0x6C4E28",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox∩viewport exp=[{},{},{},{}]",
                        clipRect.left, clipRect.top, clipRect.right,
                        clipRect.bottom),
                    fmt::format(
                        "nodeIndex={} act=[{},{},{},{}]",
                        entry.nodeIndex, entry.clipRect[0],
                        entry.clipRect[1], entry.clipRect[2],
                        entry.clipRect[3]),
                    true,
                    "sub_6C4E28 clip rect diverged from expected intersection");
                detail::logoChainTraceCheck(
                    motionPath, "renderItem.localCorners", "0x6C4E28",
                    _clampedEvalTime,
                    fmt::format(
                        "corners-0.5-clipOrigin exp=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        expectedLocalCorners[0], expectedLocalCorners[1],
                        expectedLocalCorners[2], expectedLocalCorners[3],
                        expectedLocalCorners[4], expectedLocalCorners[5],
                        expectedLocalCorners[6], expectedLocalCorners[7]),
                    fmt::format(
                        "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex,
                        entry.localCorners[0], entry.localCorners[1],
                        entry.localCorners[2], entry.localCorners[3],
                        entry.localCorners[4], entry.localCorners[5],
                        entry.localCorners[6], entry.localCorners[7]),
                    cornersOk,
                    "sub_6C4E28 local corner translation diverged from clip-local expectation");

                // libkrkr2.so sub_6C4E28 Loop A drawable body (J1/J7): emit the
                // per-item leaf copy onto the SLA Rb_tree leaf layer (item+304)
                // HERE in the build pass, not in execute. Inert for the logo
                // fixtures (this drawable branch requires drawFlag19=1, which is
                // never set for any logo mainList item).
                entry.leafBuilt = emitLeafLayerCopyLike_0x6C4E28(
                    entry, scratchOwner, scratchParent, motionPath);
            }

        }

        // libkrkr2.so sub_6C4E28 Loop B (after Loop A): compose group items into
        // their composed layers (item+324). Inert for the logo fixtures (no
        // group item reaches a non-empty child-clip union).
        composeGroupLayersLike_0x6C4E28(
            auxList, canvasWidth, canvasHeight, scratchOwner, scratchParent,
            motionPath);
        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
            _clampedEvalTime >= 43.0 && _clampedEvalTime <= 50.0) {
            for(size_t i = 0; i < mainList.size(); ++i) {
                const auto *itemPtr = mainList[i];
                if(!itemPtr) {
                    continue;
                }
                const auto &item = *itemPtr;
                if(!(item.nodeIndex == 14 || item.nodeIndex == 15 ||
                     item.nodeIndex == 19 ||
                     (item.nodeIndex >= 20 && item.nodeIndex <= 29))) {
                    continue;
                }
                std::fprintf(
                    stderr,
                    "SNAPCMD frame=%.3f order=%zu nodeIndex=%d source=%s rawFlags=[%d,%d,%d,%d,%d,%d] parentNodeIndex=%d hasRenderParent=%d childCount=%zu layerId=(%d,%d) clipRect=[%.3f,%.3f,%.3f,%.3f] opacity=%d blend=%d\n",
                    _clampedEvalTime,
                    i,
                    item.nodeIndex,
                    item.sourceKey.empty() ? "<none>" : item.sourceKey.c_str(),
                    item.rawFlag16 ? 1 : 0,
                    item.skipFlag0 ? 1 : 0,
                    item.skipFlag1 ? 1 : 0,
                    item.drawFlag ? 1 : 0,
                    item.rawFlag20 ? 1 : 0,
                    item.rawFlag21 ? 1 : 0,
                    item.parentItem ? item.parentItem->nodeIndex
                                    : item.visibleAncestorIndex,
                    item.parentItem ? 1 : 0,
                    item.childItems.size(),
                    item.layerId1,
                    item.layerId2,
                    item.clipRect[0], item.clipRect[1],
                    item.clipRect[2], item.clipRect[3],
                    item.opacity,
                    item.blendMode);
            }
        }

        detail::logoChainTraceLogf(
            motionPath, "renderItem.count", "0x6C4E28",
            _clampedEvalTime,
            "canvas={}x{} mainList={} auxList={}",
            canvasWidth, canvasHeight, mainList.size(), auxList.size());
        const bool ok = !mainList.empty();
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderBuildCommandsLeave(
            this, static_cast<int>(canvasWidth), static_cast<int>(canvasHeight),
            mainList, auxList);
#endif
        return ok;
    }


    bool Player::executeLayerRenderCommands(
        iTJSDispatch2 *renderLayerObject,
        bool skipUpdate,
        detail::PreparedRenderItemList &mainList) {
        if(!renderLayerObject || !hasMotionContent()) {
            return false;
        }
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::MotionTraceRenderExecuteScope renderTrace(
            this, renderLayerObject, skipUpdate, mainList);
#endif
        const auto motionPath = matchedMotionPath();

        auto *renderLayer = resolveNativeLayer(renderLayerObject);
        if(!renderLayer) {
            renderLayer =
                resolvePrivateMotionGLLNativeLike_0x6DE24C(renderLayerObject);
        }
        iTJSDispatch2 *scratchOwner = resolveMainWindowOwnerObject();
        iTJSDispatch2 *scratchParent = resolveMainWindowPrimaryLayerObject();
        if(scratchParent && !resolveNativeLayer(scratchParent)) {
            if(auto *resolved =
                   tryResolveLayerDispatch(tTJSVariant(scratchParent, scratchParent))) {
                scratchParent = resolved;
            }
        }
        if(!scratchParent) {
            scratchParent = renderLayerObject;
        }
        if(scratchParent && !resolveNativeLayer(scratchParent)) {
            scratchParent = renderLayerObject;
        }
        detail::logoChainTraceLogf(
            motionPath, "execute.setup.pre", "0x6C7440", _clampedEvalTime,
            "renderLayer={} scratchOwner={} scratchParent={} renderLayerNative={} scratchParentNative={}",
            static_cast<const void *>(renderLayerObject),
            static_cast<const void *>(scratchOwner),
            static_cast<const void *>(scratchParent),
            static_cast<const void *>(renderLayer),
            static_cast<const void *>(resolveNativeLayer(scratchParent)));
        detail::logoChainTraceLogf(
            motionPath, "execute.begin", "0x6C7440", _clampedEvalTime,
            "mainItems={} renderLayer={} scratchOwner={} scratchParent={} skipUpdate={}",
            mainList.size(),
            static_cast<const void *>(renderLayer),
            static_cast<const void *>(scratchOwner),
            static_cast<const void *>(scratchParent), skipUpdate ? 1 : 0);
        int snapshotCopyOrder = 0;
        if(!renderLayer) {
            detail::logoChainTraceCheck(
                motionPath, "execute.setup", "0x6C7440", _clampedEvalTime,
                "renderLayer should resolve before executeLayerRenderCommands",
                fmt::format("renderLayer={}",
                            static_cast<const void *>(renderLayer)),
                false,
                "SLA/Layer backend could not resolve native layers before copy");
            return false;
        }

        using PreparedRenderItem = detail::PreparedRenderItem;
#if defined(KRKR2_WASMTIME_HEADLESS)
        const auto recordPostDrawCandidate =
            [&](iTJSDispatch2 *layerObject, const char *samplePoint) {
            detail::motionTraceRecordPostDrawLayerCandidate(
                this, layerObject, samplePoint);
        };
        const auto directItemCoversRenderTarget =
            [&](const PreparedRenderItem &item) {
            if(!renderLayer) return false;
            float minX = item.corners[0];
            float maxX = item.corners[0];
            float minY = item.corners[1];
            float maxY = item.corners[1];
            for(size_t i = 2; i + 1 < item.corners.size(); i += 2) {
                minX = std::min(minX, item.corners[i]);
                maxX = std::max(maxX, item.corners[i]);
                minY = std::min(minY, item.corners[i + 1]);
                maxY = std::max(maxY, item.corners[i + 1]);
            }
            return minX <= 0.0f && minY <= 0.0f &&
                maxX >= static_cast<float>(renderLayer->GetWidth()) &&
                maxY >= static_cast<float>(renderLayer->GetHeight());
        };
#endif

        tTJSVariant layerClassObject;
        if(!getLayerClassDispatchVariantLike_0x5CB08C(layerClassObject)) {
            detail::logoChainTraceCheck(
                motionPath, "execute.layerClass", "0x6C7440", _clampedEvalTime,
                "Layer class dispatch should resolve before operateAffine",
                "global.Layer unavailable", false,
                "sub_6C7440 could not resolve Layer class dispatch");
            return false;
        }

        struct ResolvedSourceObject {
            tTJSVariant object;
            iTJSDispatch2 *layerObject = nullptr;
            tTJSNI_BaseLayer *layer = nullptr;
            iTVPBaseBitmap *image = nullptr;
            tjs_int width = 0;
            tjs_int height = 0;
        };

        auto resolveSourceObjectLike_0x6C1B70 =
            [&](const PreparedRenderItem &item) -> ResolvedSourceObject {
            ResolvedSourceObject resolved;
            if(!item.sourceState || !_sourceCacheNative) {
                return resolved;
            }

            resolved.object =
                _sourceCacheNative
                    ->loadRenderSourceLayerFromItemLike_0x6C1B70(*this, item);
            if(resolved.object.Type() != tvtObject ||
               !resolved.object.AsObjectNoAddRef()) {
                return resolved;
            }

            resolved.layerObject = resolved.object.AsObjectNoAddRef();
            resolved.layer = resolveNativeLayer(resolved.layerObject);
            resolved.image = resolved.layer ? resolved.layer->GetMainImage()
                                            : nullptr;
            if(resolved.image) {
                resolved.width = static_cast<tjs_int>(resolved.image->GetWidth());
                resolved.height = static_cast<tjs_int>(resolved.image->GetHeight());
            }

            detail::logoChainTraceLogf(
                motionPath, "execute.source", "0x6C1B70/0x6A7BA8",
                _clampedEvalTime,
                "source={} sourceObject={} nativeLayer={} image={}x{}",
                item.sourceKey,
                static_cast<const void *>(resolved.layerObject),
                static_cast<const void *>(resolved.layer),
                resolved.width, resolved.height);
            return resolved;
        };

        // (playerStencilType / per-item alpha-mask compose moved to the build
        // pass: composeGroupLayersLike_0x6C4E28. The execute pass is now
        // submit-only, matching the binary's 0x6C7440 boundary.)
        auto ensureLeafItemLayer =
            [&](PreparedRenderItem &item) -> iTJSDispatch2 * {
            const tjs_int stateLayerId = item.renderLayerId;
            if(stateLayerId == 0) {
                return ensureReusableLayerObject(
                    item.leafLayer,
                    scratchOwner,
                    scratchParent,
                    static_cast<tTVPLayerType>(ltAlpha),
                    false);
            }

            auto &state = _renderLayerStates[stateLayerId];
            if(!state.initialized) {
                state.layerId = stateLayerId;
                state.absolute = _nextLayerAbsolute++;
                state.hitThreshold = 256;
                state.initialized = true;
                if(item.nativeNode) {
                    state.layerGetter = getLayerGetter(
                        item.nativeNode->layerName);
                }
            }

            auto *layerObject = ensureReusableLayerObject(
                state.layerObject,
                scratchOwner,
                scratchParent,
                static_cast<tTVPLayerType>(ltAlpha),
                false);
            if(!layerObject) {
                return nullptr;
            }
            // libkrkr2.so sub_6C4E28 @0x6C5DBC latches item+20 in the BUILD
            // loop (LABEL_28), never in execute. The build pass already
            // materialized rawFlag20/layerId under the oracle gate, so the
            // execute stage only consumes them here.

            setObjectIntProperty(layerObject, TJS_W("absolute"), state.absolute);
            setObjectIntProperty(layerObject, TJS_W("hitThreshold"),
                                 state.hitThreshold);

            state.clipRect = {
                static_cast<float>(item.clipRect[0]),
                static_cast<float>(item.clipRect[1]),
                static_cast<float>(item.clipRect[2]),
                static_cast<float>(item.clipRect[3])
            };
            state.worldRect = {
                item.corners[0], item.corners[1],
                item.corners[4], item.corners[5]
            };
            state.localRect = {
                item.localCorners[0], item.localCorners[1],
                item.localCorners[4], item.localCorners[5]
            };
            state.packedColors = item.packedColors;
            state.isDirty = true;

            item.leafLayer = state.layerObject;
            return layerObject;
        };
#if defined(KRKR2_WASMTIME_HEADLESS)
        auto renderAccurateSlaPostDrawCandidateLike_0x6C9CA8 =
            [&](PreparedRenderItem &item,
                const ResolvedSourceObject &source,
                const tTVPRect &sourceRect) -> bool {
            if(!detail::motionTraceIsAccurateSlaRenderActive() ||
               !renderLayer || !source.image) {
                return false;
            }

            // libkrkr2.so sub_6C9CA8 clips item+184..196 to the target
            // Layer, then sizes the tracked Layer to right-left/bottom-top
            // before calling affineCopy/meshCopy/bezierPatchCopy on it.
            float clipLeft = std::max(item.paintBox[0], 0.0f);
            float clipTop = std::max(item.paintBox[1], 0.0f);
            float clipRight = std::min(
                item.paintBox[2],
                static_cast<float>(renderLayer->GetWidth()));
            float clipBottom = std::min(
                item.paintBox[3],
                static_cast<float>(renderLayer->GetHeight()));
            if(!item.corners.empty()) {
                float minX = item.corners[0];
                float maxX = item.corners[0];
                float minY = item.corners[1];
                float maxY = item.corners[1];
                for(size_t i = 2; i + 1 < item.corners.size(); i += 2) {
                    minX = std::min(minX, item.corners[i]);
                    maxX = std::max(maxX, item.corners[i]);
                    minY = std::min(minY, item.corners[i + 1]);
                    maxY = std::max(maxY, item.corners[i + 1]);
                }
                clipLeft = std::max(clipLeft, std::floor(minX));
                clipTop = std::max(clipTop, std::floor(minY));
                clipRight = std::min(clipRight, std::ceil(maxX));
                clipBottom = std::min(clipBottom, std::ceil(maxY));
            }
            if(clipRight <= clipLeft || clipBottom <= clipTop) {
                return false;
            }
            const int clipWidth = static_cast<int>(clipRight - clipLeft);
            const int clipHeight = static_cast<int>(clipBottom - clipTop);
            // libkrkr2.so sub_6C9CA8 sizes the SLA item layer and invokes
            // affineCopy(clear=1) before this pass writes the final layer
            // type, so the checkpoint helper must start from the ltAlpha
            // transparent-white neutral color, not a stale reused type color.
            int layerWidth = clipWidth;
            int layerHeight = clipHeight;
            if(layerWidth <= 0 || layerHeight <= 0) {
                return false;
            }

            iTJSDispatch2 *candidateLayerObject = ensureLeafItemLayer(item);
            auto *candidateLayer = resolveNativeLayer(candidateLayerObject);
            if(!candidateLayerObject || !candidateLayer ||
               !prepareLayerForRender(
                   candidateLayerObject, layerWidth, layerHeight,
                   0x00FFFFFFu)) {
                return false;
            }

            (void)candidateLayer;
            const float offsetX = -0.5f - clipLeft;
            const float offsetY = -0.5f - clipTop;
            // Keep the SLA checkpoint copy consistent with the converted main
            // path: dispatch affineCopy/meshCopy/bezierPatchCopy through the
            // candidate Layer instance via FuncCall (clear=1, matching
            // sub_6C9CA8's affineCopy(clear=1) sizing pass).
            if(source.object.Type() != tvtObject ||
               !source.object.AsObjectNoAddRef()) {
                return false;
            }
            if(item.meshType == 0) {
                const auto localPts =
                    buildAffineTrianglePoints(item.corners, offsetX, offsetY);
                if(TJS_FAILED(callLayerAffineCopyLike_0x6C7440(
                       candidateLayerObject, localPts.data(), source.object,
                       sourceRect, stNearest, true))) {
                    return false;
                }
                recordPostDrawCandidate(
                    candidateLayerObject,
                    "Player::executeLayerRenderCommands.accurateSla.item.afterAffineCopy");
                return true;
            }
            const auto &renderMeshPoints = item.meshType == 2
                ? item.commandCompositeMeshPoints
                : item.meshPoints;
            if(renderMeshPoints.empty()) {
                return false;
            }
            std::array<tjs_int, 2> cellDivisions{
                item.meshDivX, item.meshDivY
            };
            if(item.meshType == 1) {
                cellDivisions = bezierPatchCellDivisionsU32Like_0x6C8E5C(
                    item.commandPatchDivision, source.width, source.height);
            } else if(item.meshType == 2) {
                if(item.meshDivX < 1 || item.meshDivY < 1) {
                    return false;
                }
            } else {
                return false;
            }
            iTJSDispatch2 *meshArray =
                buildMeshPointTJSArrayLike_0x6C715C(renderMeshPoints, offsetX,
                                                    offsetY);
            if(!meshArray) {
                return false;
            }
            bool ok = false;
            if(item.meshType == 1) {
                ok = TJS_SUCCEEDED(callLayerBezierPatchCopyLike_0x6C7440(
                    candidateLayerObject, source.object, sourceRect, meshArray,
                    cellDivisions[0], cellDivisions[1], stNearest, true));
                if(ok) {
                    recordPostDrawCandidate(
                        candidateLayerObject,
                        "Player::executeLayerRenderCommands.accurateSla.item.afterBezierPatchCopy");
                }
            } else if(item.meshType == 2) {
                ok = TJS_SUCCEEDED(callLayerMeshCopyLike_0x6C7440(
                    candidateLayerObject, source.object, sourceRect, meshArray,
                    cellDivisions[0], cellDivisions[1], stNearest, true));
                if(ok) {
                    recordPostDrawCandidate(
                        candidateLayerObject,
                        "Player::executeLayerRenderCommands.accurateSla.item.afterMeshCopy");
                }
            }
            meshArray->Release();
            return ok;
        };
#endif
        auto chooseItemOutputLayerObject =
            [&](PreparedRenderItem &item) -> iTJSDispatch2 * {
            const bool preferLeafLayer = (item.stencilComposite & 4) == 0;
            if(!preferLeafLayer &&
               item.composedLayer.Type() == tvtObject) {
                return item.composedLayer.AsObjectNoAddRef();
            }
            if(item.leafLayer.Type() == tvtObject) {
                return item.leafLayer.AsObjectNoAddRef();
            }
            if(item.composedLayer.Type() == tvtObject) {
                return item.composedLayer.AsObjectNoAddRef();
            }
            return nullptr;
        };
        auto computeTargetLayerClipLike_0x6C7440 =
            [&](const PreparedRenderItem &item, RenderClipRect &outRect,
                bool &hasViewportClip) -> bool {
            hasViewportClip = false;
            if(item.hasViewport && item.viewport[2] >= item.viewport[0] &&
               item.viewport[3] >= item.viewport[1]) {
                const float clipLeft =
                    std::max(item.paintBox[0], floorf(item.viewport[0]));
                const float clipTop =
                    std::max(item.paintBox[1], floorf(item.viewport[1]));
                const float clipRight =
                    std::min(item.paintBox[2], ceilf(item.viewport[2]));
                const float clipBottom =
                    std::min(item.paintBox[3], ceilf(item.viewport[3]));
                if(clipLeft > clipRight || clipTop > clipBottom) {
                    return false;
                }

                const int left = static_cast<int>(clipLeft);
                const int top = static_cast<int>(clipTop);
                const int width = static_cast<int>(clipRight - clipLeft);
                const int height = static_cast<int>(clipBottom - clipTop);
                outRect = {
                    left,
                    top,
                    left + width,
                    top + height,
                };
                hasViewportClip = true;
                return true;
            }

            outRect = {
                0,
                0,
                renderLayer ? static_cast<int>(renderLayer->GetWidth()) : 0,
                renderLayer ? static_cast<int>(renderLayer->GetHeight()) : 0,
            };
            return true;
        };
        auto applyTargetLayerClipLike_0x6C7440 =
            [&](const PreparedRenderItem &item, RenderClipRect &outRect) -> bool {
            bool hasViewportClip = false;
            if(!computeTargetLayerClipLike_0x6C7440(
                   item, outRect, hasViewportClip)) {
                return false;
            }

            // libkrkr2.so sub_6C7440 dispatches setClip on the target work-layer
            // (v370) via FuncCall, NOT a native Layer method:
            //   - 0x6c78dc: argc=4 [left, top, width, height] (viewport clip)
            //   - 0x6c7620: argc=0 (reset)
            // The later operateAffine call still receives the full source rect.
            if(hasViewportClip) {
                callLayerSetClipLike_0x6C7440(
                    renderLayerObject, outRect.left, outRect.top,
                    outRect.right - outRect.left,
                    outRect.bottom - outRect.top);
            } else {
                callLayerResetClipLike_0x6C7440(renderLayerObject);
            }

            const auto &actualClip = renderLayer->GetClip();
            outRect = {
                actualClip.left,
                actualClip.top,
                actualClip.right,
                actualClip.bottom,
            };
            return true;
        };

        auto buildItemOutput = [&](auto &&self, PreparedRenderItem *itemPtr) -> bool {
            if(!itemPtr) {
                return false;
            }
            auto &item = *itemPtr;
            if(item.executedDirect || item.leafBuilt || item.composedBuilt) {
                return true;
            }
            const bool hasChildren = !item.childItems.empty();
            const bool useDirectRenderPath =
                shouldUseDirectRenderPathLike_0x6C7440(item, _clearEnabled) &&
                !hasChildren && item.parentItem == nullptr &&
                !item.skipFlag0 && !item.rawFlag16 &&
                !(_priorDraw && !item.skipFlag1) && item.opacity > 0;

            const int clipWidth = item.clipRect[2] - item.clipRect[0];
            const int clipHeight = item.clipRect[3] - item.clipRect[1];
            if(!useDirectRenderPath) {
                if(item.rawFlag21 && (clipWidth <= 0 || clipHeight <= 0)) {
                    return false;
                }
                if(!item.rawFlag21) {
                    return false;
                }
            }

            auto source = resolveSourceObjectLike_0x6C1B70(item);
            const bool hasSourceBitmap =
                source.image && source.width > 0 && source.height > 0;
            if(!hasSourceBitmap && item.childItems.empty()) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.source", "0x6C7440",
                    _clampedEvalTime,
                    "resolved source object should exist with positive image size",
                    fmt::format("nodeIndex={} source={} object={} image={}x{}",
                                item.nodeIndex, item.sourceKey,
                                static_cast<const void *>(source.layerObject),
                                source.width, source.height),
                    false,
                    "sub_6C1B70 could not resolve a drawable source object");
                return false;
            }

            const tTVPRect sourceRect(
                0, 0,
                hasSourceBitmap ? source.width : 0,
                hasSourceBitmap ? source.height : 0);
            if(hasSourceBitmap) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.srcRect", "0x6C7440",
                    _clampedEvalTime,
                    fmt::format("full texture rect exp=[0,0,{},{}]",
                                source.width, source.height),
                    fmt::format("nodeIndex={} act=[{},{},{},{}]",
                                item.nodeIndex, sourceRect.left,
                                sourceRect.top, sourceRect.right,
                                sourceRect.bottom),
                    true,
                    "sub_6C7440 source rect was not the full texture bounds");
            }

            if(useDirectRenderPath) {
                RenderClipRect directTargetRect;
                bool hasViewportClip = false;
                if(!computeTargetLayerClipLike_0x6C7440(
                       item, directTargetRect, hasViewportClip)) {
                    return false;
                }
                item.executedDirect = true;
                item.builtRect = {
                    directTargetRect.left,
                    directTargetRect.top,
                    directTargetRect.right,
                    directTargetRect.bottom,
                };
                return true;
            }
            if(!item.rawFlag21) {
                return false;
            }

            // J1/J7: the leaf (item+304) and composed (item+324) layers are now
            // materialized in the BUILD pass (buildRenderCommands ->
            // emitLeafLayerCopyLike_0x6C4E28 / composeGroupLayersLike_0x6C4E28),
            // mirroring the binary's two-function pipeline (0x6C4E28 emits, this
            // 0x6C7440 counterpart only submits). The execute pass no longer
            // re-builds the leaf/composed copy; it just consumes the prebuilt
            // item+304/item+324 state for the buffered operateRect submit below.
            // (void)source/sourceRect/clip* keeps the direct-path source probe
            // above intact while the non-direct leaf rebuild is removed.)
            (void)sourceRect;
            (void)clipWidth;
            (void)clipHeight;
            item.builtRect = integralClipRect(item.clipRect);
            return item.leafBuilt || item.composedBuilt;
        };

        for(auto *itemPtr : mainList) {
            if(!itemPtr) {
                continue;
            }
            auto &item = *itemPtr;

            const auto blendMode =
                resolveBlendOperationModeLike_0x6C7440(item.blendMode);
            const auto effectiveColor = unpackPackedRgba(item.packedColors[0]);
            // libkrkr2.so sub_6C7440 @0x6c764c-0x6c7668 (J9): the submitted
            // opacity is item+232 (signed int). Under priorDraw (player+1096)
            // it is arithmetic-shifted right by 1, with a `+1` sign
            // adjustment so the shift rounds toward zero for negatives
            // (v23 = v20>=0 ? v20 : v20+1;
            //  v24 = priorDraw ? v23>>1 : item+232).
            // Non-priorDraw keeps the raw opacity.
            const int rawOpacity = item.opacity;
            const int signAdjustedOpacity =
                rawOpacity >= 0 ? rawOpacity : rawOpacity + 1;
            const int submittedOpacity =
                _priorDraw ? (signAdjustedOpacity >> 1) : rawOpacity;
            const auto opa = static_cast<tjs_int>(
                std::clamp(submittedOpacity, 0, 255));
            if(opa <= 0) {
                continue;
            }

            // libkrkr2.so 0x6C7440 reads item+17/item+16 first, then updates
            // target Layer clip, and only then applies the priorDraw item+18
            // gate.
            if(item.skipFlag0) {
                continue;
            }
            if(item.rawFlag16) {
                continue;
            }
            RenderClipRect targetLayerClip;
            if(!applyTargetLayerClipLike_0x6C7440(item, targetLayerClip)) {
                continue;
            }
            detail::logoChainTraceLogf(
                motionPath, "execute.setClip", "0x6C7440", _clampedEvalTime,
                "nodeIndex={} targetClip=[{},{},{},{}]",
                item.nodeIndex, targetLayerClip.left, targetLayerClip.top,
                targetLayerClip.right, targetLayerClip.bottom);
            if(_priorDraw && !item.skipFlag1) {
                continue;
            }
            if(item.parentItem) {
                continue;
            }

            // Player_renderToCanvas @0x6C7634..0x6C767C converts the four
            // paintBox floats to int with FCVTZS and ORs that rectangle into
            // player+864 before submitting the item. This region survives
            // until the next Player.clear call, so pixels occupied only by
            // the previous frame are still erased.
            _drawRegion.Or(tTVPRect(
                static_cast<tjs_int>(item.paintBox[0]),
                static_cast<tjs_int>(item.paintBox[1]),
                static_cast<tjs_int>(item.paintBox[2]),
                static_cast<tjs_int>(item.paintBox[3])));
            if(!buildItemOutput(buildItemOutput, &item)) {
                continue;
            }

            try {
                if(item.executedDirect) {
                    auto source = resolveSourceObjectLike_0x6C1B70(item);
                    if(!source.image || source.width <= 0 || source.height <= 0) {
                        continue;
                    }
                    const tTVPRect sourceRect(0, 0, source.width, source.height);
                    std::string branch("direct.operateAffine");
#if defined(KRKR2_WASMTIME_HEADLESS)
                    const auto emitDirectProbe =
                        [&](const char *samplePoint, const char *phase,
                            const char *executionMethod = "native-direct-call",
                            iTJSDispatch2 *sourceArgObject = nullptr,
                            tTJSNI_BaseLayer *sourceArgLayer = nullptr,
                            const char *sourceArgClass = nullptr) {
                        emitDirectExecuteDiagnostics(
                            this, samplePoint, phase, branch.c_str(),
                            executionMethod, item, renderLayer,
                            std::shared_ptr<tTVPBaseBitmap>{},
                            sourceArgObject, sourceArgLayer, sourceArgClass,
                            blendMode, opa, stNearest);
                    };
#endif
                    if(item.meshType == 0) {
                        if(!source.layerObject || !source.layer) {
                            detail::logoChainTraceCheck(
                                motionPath, "execute.directSourceLayer",
                                "0x6948E8/0x6C7440", _clampedEvalTime,
                                "direct affine source should be cached as Layer",
                                fmt::format("nodeIndex={} source={} object={} layer={}",
                                            item.nodeIndex, item.sourceKey,
                                            static_cast<const void *>(source.layerObject),
                                            static_cast<const void *>(source.layer)),
                                false,
                                "sub_6C1B70 direct affine source object setup failed");
                            continue;
                        }
#if defined(KRKR2_WASMTIME_HEADLESS)
#endif
                        const auto worldPts =
                            buildAffineTrianglePoints(item.corners,
                                                     -0.5f, -0.5f);
#if defined(KRKR2_WASMTIME_HEADLESS)
                        TVPResetSoftwareAffineDiagnosticsForWasmtime();
                        emitDirectProbe(
                            "Player::executeLayerRenderCommands.direct.beforeOperateAffine",
                            "before",
                            "tjs-funcall-operateAffine",
                            source.layerObject, source.layer, "Layer");
#endif
                        const tjs_error operateResult =
                            callLayerOperateAffineLike_0x6C7440(
                                layerClassObject, renderLayerObject,
                                worldPts.data(), source.object,
                                sourceRect, blendMode, opa, stNearest);
                        if(TJS_FAILED(operateResult)) {
                            detail::logoChainTraceCheck(
                                motionPath, "execute.directOperateAffine",
                                "0x6C7440", _clampedEvalTime,
                                "FuncCall(\"operateAffine\") should succeed",
                                fmt::format("nodeIndex={} hr={}",
                                            item.nodeIndex, operateResult),
                                false,
                                "sub_6C7440 direct affine dispatch failed");
                            continue;
                        }
#if defined(KRKR2_WASMTIME_HEADLESS)
                        if(detail::motionTraceIsAccurateSlaRenderActive()) {
                            if(!renderAccurateSlaPostDrawCandidateLike_0x6C9CA8(
                                   item, source, sourceRect)) {
                                recordPostDrawCandidate(
                                    directItemCoversRenderTarget(item)
                                        ? renderLayerObject
                                        : source.layerObject,
                                    "Player::executeLayerRenderCommands.direct.afterOperateAffine.accurateSlaCandidateFallback");
                            }
                        }
                        emitDirectProbe(
                            "Player::executeLayerRenderCommands.direct.afterOperateAffine",
                            "after",
                            "tjs-funcall-operateAffine",
                            source.layerObject, source.layer, "Layer");
#endif
                    } else {
                        const auto &renderMeshPoints = item.meshType == 2
                            ? item.commandCompositeMeshPoints
                            : item.meshPoints;
                        if(renderMeshPoints.empty()) {
                            continue;
                        }
                        std::array<tjs_int, 2> cellDivisions{
                            item.meshDivX, item.meshDivY
                        };
                        if(item.meshType == 1) {
                            cellDivisions =
                                bezierPatchCellDivisionsU32Like_0x6C8E5C(
                                    item.commandPatchDivision,
                                    item.sourceState
                                        ? item.sourceState->width
                                        : item.nativeNode->source.width,
                                    item.sourceState
                                        ? item.sourceState->height
                                        : item.nativeNode->source.height);
                        } else if(item.meshType == 2) {
                            if(item.meshDivX < 1 || item.meshDivY < 1) {
                                continue;
                            }
                        } else {
                            continue;
                        }
                        // libkrkr2.so sub_6C7440 operateMesh/operateBezierPatch
                        // blocks build the point array with a -0.5,-0.5 world
                        // offset (0xBF000000BF000000) and dispatch through the
                        // render-layer instance via FuncCall. The clear flag in
                        // those blocks is 0; the local _clearEnabled gate is
                        // already false on the direct path
                        // (shouldUseDirectRenderPathLike_0x6C7440 requires
                        // !clearEnabled), so it is preserved here verbatim.
                        iTJSDispatch2 *meshArray =
                            buildMeshPointTJSArrayLike_0x6C715C(
                                renderMeshPoints, -0.5f, -0.5f);
                        if(!meshArray) {
                            continue;
                        }
                        tjs_error meshResult = TJS_E_FAIL;
                        if(item.meshType == 1) {
                            branch = "direct.operateBezierPatch";
#if defined(KRKR2_WASMTIME_HEADLESS)
                            emitDirectProbe(
                                "Player::executeLayerRenderCommands.direct.beforeOperateBezierPatch",
                                "before");
#endif
                            meshResult =
                                callLayerOperateBezierPatchLike_0x6C7440(
                                    renderLayerObject, source.object,
                                    sourceRect, meshArray, cellDivisions[0],
                                    cellDivisions[1], blendMode, opa,
                                    _clearEnabled);
#if defined(KRKR2_WASMTIME_HEADLESS)
                            emitDirectProbe(
                                "Player::executeLayerRenderCommands.direct.afterOperateBezierPatch",
                                "after");
#endif
                        } else if(item.meshType == 2) {
                            branch = "direct.operateMesh";
#if defined(KRKR2_WASMTIME_HEADLESS)
                            emitDirectProbe(
                                "Player::executeLayerRenderCommands.direct.beforeOperateMesh",
                                "before");
#endif
                            meshResult = callLayerOperateMeshLike_0x6C7440(
                                renderLayerObject, source.object, sourceRect,
                                meshArray, cellDivisions[0], cellDivisions[1],
                                blendMode, opa, _clearEnabled);
#if defined(KRKR2_WASMTIME_HEADLESS)
                            emitDirectProbe(
                                "Player::executeLayerRenderCommands.direct.afterOperateMesh",
                                "after");
#endif
                        } else {
                            meshArray->Release();
                            continue;
                        }
                        meshArray->Release();
                        if(TJS_FAILED(meshResult)) {
                            continue;
                        }
                    }
                    detail::logoChainTraceLogf(
                        motionPath, "execute.copy", "0x6C7440",
                        _clampedEvalTime,
                        "branch={} nodeIndex={} clipRect=[{},{},{},{}] dirtyRect=[{},{},{},{}] blendMode={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] visibleAncestorIndex={} clearEnabled={} renderPath=direct workLayer=0x0 renderLayer={}x{}",
                        branch, item.nodeIndex,
                        item.clipRect[0], item.clipRect[1],
                        item.clipRect[2], item.clipRect[3],
                        item.dirtyRect[0], item.dirtyRect[1],
                        item.dirtyRect[2], item.dirtyRect[3],
                        item.blendMode, opa,
                        item.packedColors[0], item.packedColors[1],
                        item.packedColors[2], item.packedColors[3],
                        effectiveColor[0], effectiveColor[1],
                        effectiveColor[2], effectiveColor[3],
                        item.visibleAncestorIndex,
                        _clearEnabled ? 1 : 0,
                        renderLayer->GetWidth(), renderLayer->GetHeight());
                    if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                       motionPath.find("m2logo.mtn") != std::string::npos &&
                       _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                        std::fprintf(stderr,
                                     "SNAPCOPY order=%d frame=%.3f nodeIndex=%d source=%s branch=%s clipRect=[%.3f,%.3f,%.3f,%.3f] opacity=%d blend=%d\n",
                                     snapshotCopyOrder++, _clampedEvalTime,
                                     item.nodeIndex,
                                     item.sourceKey.empty()
                                         ? "<none>"
                                         : item.sourceKey.c_str(),
                                     branch.c_str(),
                                     item.clipRect[0], item.clipRect[1],
                                     item.clipRect[2], item.clipRect[3],
                                     opa, item.blendMode);
                    }
                    continue;
                }

                auto *outputLayerObject = chooseItemOutputLayerObject(item);
                auto *outputLayer = resolveNativeLayer(outputLayerObject);
                if(!outputLayerObject || !outputLayer) {
                    continue;
                }

                const auto localRect = localRectFromItem(item);
                (void)outputLayer;
                // libkrkr2.so sub_6C7440 submits the buffered work layer to the
                // render layer via FuncCall(L"operateRect") on the render-layer
                // instance, passing the work layer object as the source.
                if(TJS_FAILED(callLayerOperateRectLike_0x6C7440(
                       renderLayerObject, item.clipRect[0], item.clipRect[1],
                       tTJSVariant(outputLayerObject, outputLayerObject),
                       localRect, blendMode, opa))) {
                    continue;
                }
                detail::logoChainTraceLogf(
                    motionPath, "execute.copy", "0x6C7440", _clampedEvalTime,
                    "branch={} nodeIndex={} clipRect=[{},{},{},{}] dirtyRect=[{},{},{},{}] blendMode={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] visibleAncestorIndex={} clearEnabled={} renderPath=buffered outputLayer={}x{} renderLayer={}x{} childCount={} phase={}",
                    item.composedBuilt ? "buffered.operateRect.composed"
                                       : "buffered.operateRect.leaf",
                    item.nodeIndex,
                    item.clipRect[0], item.clipRect[1],
                    item.clipRect[2], item.clipRect[3],
                    item.dirtyRect[0], item.dirtyRect[1],
                    item.dirtyRect[2], item.dirtyRect[3],
                    item.blendMode, opa,
                    item.packedColors[0], item.packedColors[1],
                    item.packedColors[2], item.packedColors[3],
                    effectiveColor[0], effectiveColor[1],
                    effectiveColor[2], effectiveColor[3],
                    item.visibleAncestorIndex,
                    _clearEnabled ? 1 : 0,
                    localRect.get_width(), localRect.get_height(),
                    renderLayer->GetWidth(), renderLayer->GetHeight(),
                    item.childItems.size(),
                    0);
                if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                   motionPath.find("m2logo.mtn") != std::string::npos &&
                   _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                    const char *snapBranch = item.composedBuilt
                        ? "buffered.operateRect.composed"
                        : "buffered.operateRect.leaf";
                    std::fprintf(stderr,
                                 "SNAPCOPY order=%d frame=%.3f nodeIndex=%d source=%s branch=%s clipRect=[%.3f,%.3f,%.3f,%.3f] opacity=%d blend=%d childCount=%zu phase=%d\n",
                                 snapshotCopyOrder++, _clampedEvalTime,
                                 item.nodeIndex,
                                 item.sourceKey.empty()
                                     ? "<none>"
                                     : item.sourceKey.c_str(),
                                 snapBranch,
                                 item.clipRect[0], item.clipRect[1],
                                 item.clipRect[2], item.clipRect[3],
                                 opa, item.blendMode,
                                 item.childItems.size(),
                                 0);
                }
            } catch(const eTJS &) {
            } catch(...) {
            }
        }

        // libkrkr2.so sub_6C7440 @ 0x6c8fcc resets the target work-layer clip
        // once the top-level render-item walk is complete, via setClip(argc=0)
        // FuncCall on v370 (NOT a native Layer method), then releases v9/v370
        // and returns. J4: 0x6C7440 has NO Layer.Update() call (the whole
        // function contains zero "Update" dispatches; `L"Update"`=0 in the
        // decompile). Update belongs to the post-draw wrapper
        // (updateLayerAfterDraw 0x6CE7D8 / renderToLayer's own
        // layer->Update(false)). The previous execute-internal
        // renderLayer->Update(false) was a duplicate that fired Update twice
        // per draw; removed to align the function boundary with 0x6C7440.
        callLayerResetClipLike_0x6C7440(renderLayerObject);
        (void)skipUpdate;
#if defined(KRKR2_WASMTIME_HEADLESS)
        renderTrace.setResult(true);
#endif
        return true;
    }

} // namespace motion
