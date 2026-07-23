// PlayerRenderExecute.cpp — render command build and execution
// Split from PlayerRender.cpp for maintainability.
//
#include "PlayerRenderInternal.h"
#include "MotionTraceWeb.h"
#include "PrivateMotionGLL.h"
#include "SourceCache.h"

#include <limits>

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

        // AArch64 FCVTZS W saturates malformed/out-of-range floats instead of
        // entering C++'s undefined floating-to-integer conversion boundary.
        // Player_renderToCanvas @0x6C7634 and @0x6C8348 use this operation on
        // paint boxes and ancestor clip differences respectively.
        tjs_int fcvtzsWLike_0x6C7440(float value) {
            if(std::isnan(value)) {
                return 0;
            }
            constexpr float upper = 2147483648.0f;
            constexpr float lower = -2147483648.0f;
            if(value >= upper) {
                return std::numeric_limits<tjs_int>::max();
            }
            if(value <= lower) {
                return std::numeric_limits<tjs_int>::min();
            }
            return static_cast<tjs_int>(std::trunc(value));
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
        const tjs_real clipWidth = static_cast<tjs_real>(
            item.clipRect[2] - item.clipRect[0]);
        const tjs_real clipHeight = static_cast<tjs_real>(
            item.clipRect[3] - item.clipRect[1]);
        if(clipWidth < 0 || clipHeight < 0) {
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
        // The binary builds a caller-local command payload before 0x6C6B48; it
        // never probes type/visible/left/top/width/height on the previous Layer.
        // Keep the local SLA payload value-initialized until that command
        // payload type itself is fully reconstructed instead of introducing
        // those observably wrong TJS reads through fromLayerVariant().
        NativeSLAPayloadLike_0x6DCD0C payload;
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
        if(!leafLayerObject) {
            return false;
        }
        // Player_acquireLeafLayerById writes this out byte.  A false value reuses
        // the acquired item+304 layer without re-resolving/repainting its source.
        if(!createdOrChanged) {
            item.builtRect = integralClipRect(item.clipRect);
            return true;
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

        // 0x6C5714..0x6C5760 first writes Integer 0 to neutralColor with
        // TJS_MEMBERENSURE on the leaf instance.  0x6C57B4 then dispatches
        // setSize with two Real Variants.  All dispatch results are ignored.
        {
            tTJSVariant neutralColor(static_cast<tjs_int>(0));
            (void)leafLayerObject->PropSet(
                TJS_MEMBERENSURE, TJS_W("neutralColor"),
                &detail::neutralColorMemberHint_guess, &neutralColor,
                leafLayerObject);
        }
        (void)callLayerSetSizeRealLike_0x6C7440(
            leafLayerObject, clipWidth, clipHeight);
        const tTVPRect sourceRect(0, 0, srcW, srcH);
        const auto completionType =
            static_cast<tTVPBBStretchType>(_completionType);

        if(item.meshType == 0) {
            // sub_6C4E28 @0x6c5968 affineCopy block, points already clip-local.
            const auto localPts =
                buildAffineTrianglePoints(item.localCorners, 0.0f, 0.0f);
            (void)callLayerAffineCopyLike_0x6C7440(
                leafLayerObject, localPts.data(), sourceObject, sourceRect,
                completionType, true);
        } else if(item.meshType == 1 || item.meshType == 2) {
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
            }
            iTJSDispatch2 *meshArray =
                buildMeshPointTJSArrayLike_0x6C715C(item.localMeshPoints, 0.0f,
                                                    0.0f);
            if(item.meshType == 1) {
                // sub_6C4E28 @0x6c5ba0 bezierPatchCopy block.
                (void)callLayerBezierPatchCopyLike_0x6C7440(
                    leafLayerObject, sourceObject, sourceRect, meshArray,
                    cellDivisions[0], cellDivisions[1], completionType, true);
            } else if(item.meshType == 2) {
                // sub_6C4E28 @0x6c5810 meshCopy block.
                (void)callLayerMeshCopyLike_0x6C7440(
                    leafLayerObject, sourceObject, sourceRect, meshArray,
                    cellDivisions[0], cellDivisions[1], completionType, true);
            }
            meshArray->Release();
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
    // For each group item: union the visible child paint boxes (child+21 set),
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
                // 0x6C5EC0..0x6C5EDC uses FCSEL MI with the child as the
                // fallback operand.  Put child first so std::min/max also
                // preserve the child's NaN payload and signed zero on equal
                // or unordered inputs.
                unionLeft = std::min(child.paintBox[0], unionLeft);
                unionTop = std::min(child.paintBox[1], unionTop);
                unionRight = std::max(child.paintBox[2], unionRight);
                unionBottom = std::max(child.paintBox[3], unionBottom);
            }
            // sub_6C4E28 @0x6c5eec: clamp the union to the camera clip (a4):
            // max(a4.left, union.left) / max(a4.top, union.top) /
            // min(union.right, a4.right) / min(union.bottom, a4.bottom). These
            // camera-clamped values (v102/v97/v100/v105) drive the EMPTY test.
            const float camClampedLeft = cameraLeft < unionLeft
                ? unionLeft
                : cameraLeft;
            const float camClampedTop = cameraTop < unionTop
                ? unionTop
                : cameraTop;
            const float camClampedRight = unionRight < cameraRight
                ? unionRight
                : cameraRight;
            const float camClampedBottom = unionBottom < cameraBottom
                ? unionBottom
                : cameraBottom;

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
                finalLeft = vfLeft < camClampedLeft ? camClampedLeft : vfLeft;
                finalTop = vfTop < camClampedTop ? camClampedTop : vfTop;
                finalRight = camClampedRight < vcRight ? camClampedRight
                                                       : vcRight;
                finalBottom = camClampedBottom < vcBottom ? camClampedBottom
                                                          : vcBottom;
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
            const tjs_real composedWidth = static_cast<tjs_real>(
                unionRightF - unionLeftF);
            const tjs_real composedHeight = static_cast<tjs_real>(
                unionBottomF - unionTopF);

            // sub_6C4E28 @0x6c5f94: grp+340 is the type tag of the
            // tTJSVariant beginning at grp+324.  A void composedLayer Variant
            // therefore creates the Layer lazily before sizing/clearing it.
            if(grp.composedLayer.Type() == tvtVoid) {
                iTJSDispatch2 *created = createLayerObject(
                    scratchOwner, scratchParent);
                if(created) {
                    grp.composedLayer = tTJSVariant(created, created);
                    created->Release();
                }
            }
            ncbPropAccessor composedLayer{tTJSVariant(grp.composedLayer)};
            iTJSDispatch2 *composedLayerObject = composedLayer.GetDispatch();
            (void)callLayerSetSizeRealLike_0x6C7440(
                composedLayerObject, composedWidth, composedHeight);
            (void)callLayerFillRect5Like_0x6C4E28(
                composedLayerObject, composedWidth, composedHeight);

            // sub_6C4E28 @0x6c62c8 child alpha-mask loop: for child in grp+24:
            //   if (child+21 && child+320): Motion_doAlphaMaskOperation(
            //       grp+324, (int)(child+216 - v109), (int)(child+220 - v108),
            //       child+304, 0,0, child+224-child+216, child+228-child+220,
            //       64, player+1148 stencilType, grp+244)
            // child+320 is not a standalone field: it is the type tag of the
            // tTJSVariant beginning at child+304 (leafLayer).  0x6C533C writes
            // that Variant and 0x6C62E0 tests the tag before CopyRef'ing the same
            // child+304 Variant into Motion_doAlphaMaskOperation.
            const int playerStencilType = _maskMode;
            for(auto *childPtr : grp.childItems) {
                if(!childPtr) {
                    continue;
                }
                auto &child = *childPtr;
                if(!child.rawFlag21 || child.leafLayer.Type() == tvtVoid) {
                    continue;
                }
                // 0x6C62E8/0x6C62FC create scoped CopyRef owners for group+324
                // and child+304, then destroy mask before destination.
                tTJSVariant composedLayerCopy = grp.composedLayer;
                tTJSVariant childLeafCopy = child.leafLayer;
                auto *childMaskLayerObject = childLeafCopy.Type() == tvtObject
                    ? childLeafCopy.AsObjectNoAddRef()
                    : nullptr;
                auto *composedLayerCopyObject =
                    composedLayerCopy.Type() == tvtObject
                        ? composedLayerCopy.AsObjectNoAddRef()
                        : nullptr;
                const int childWidth = fcvtzsWLike_0x6C7440(
                    child.clipRect[2] - child.clipRect[0]);
                const int childHeight = fcvtzsWLike_0x6C7440(
                    child.clipRect[3] - child.clipRect[1]);
                applyMotionAlphaMaskLike_0x6AF104(
                    composedLayerCopyObject,
                    fcvtzsWLike_0x6C7440(
                        child.clipRect[0] - unionLeftF),
                    fcvtzsWLike_0x6C7440(
                        child.clipRect[1] - unionTopF),
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
        // Player_emitRenderItem_requireLayer @0x6C4E74..0x6C4F14 swaps the
        // active/retired SLA Rb_trees and resets the per-pass absolute sequence
        // before either item loop. Keep this as an explicit normal-flow pair:
        // the binary does not run its 0x6C63B8 retired-tree cleanup while an
        // exception unwinds out of the function.
        bool renderLayerPassStarted = false;
        if(_renderSeparateLayerAdaptor) {
            _renderSeparateLayerAdaptor
                ->beginRenderLayerPassLike_0x6C4E28();
            renderLayerPassStarted = true;
        }
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
                    // 0x6C5088..0x6C5128 repeats the same swap/reset sequence
                    // immediately after lazily constructing player+760.
                    _renderSeparateLayerAdaptor
                        ->beginRenderLayerPassLike_0x6C4E28();
                    renderLayerPassStarted = true;
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
        if(renderLayerPassStarted) {
            // Normal-only tail: 0x6C63B0..0x6C63B8 immediately follows Loop B
            // and invalidates/destroys retired entries that 0x6C6B48 did not
            // move back into the active Rb_tree. Keep diagnostics after this
            // call so they cannot insert an extra throwing boundary before the
            // binary's explicit cleanup point.
            _renderSeparateLayerAdaptor
                ->endRenderLayerPassLike_0x6C4E28();
        }
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
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *renderLayerObject,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        bool skipUpdate,
        detail::PreparedRenderItemList &mainList) {
        if(!layerClassObject || !renderLayerObject || !hasMotionContent()) {
            return false;
        }
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::MotionTraceRenderExecuteScope renderTrace(
            this, renderLayerObject, skipUpdate, mainList);
#endif
        const auto motionPath = matchedMotionPath();

#if defined(KRKR2_WASMTIME_HEADLESS)
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
#endif
        int snapshotCopyOrder = 0;
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

        struct ResolvedSourceObject {
            tTJSVariant object;
            iTJSDispatch2 *layerObject = nullptr;
#if defined(KRKR2_WASMTIME_HEADLESS)
            tTJSNI_BaseLayer *layer = nullptr;
            iTVPBaseBitmap *image = nullptr;
#endif
            tjs_int width = 0;
            tjs_int height = 0;
        };

#if defined(KRKR2_WASMTIME_HEADLESS)
        // HEADLESS-only diagnostic reconstruction; ordinary 0x6C7440 execution
        // has no native-layer/scratch-owner precondition or side query.
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
        auto applyTargetLayerClipLike_0x6C7440 =
            [&](const PreparedRenderItem &item,
                std::array<tjs_real, 4> &outRect) -> bool {
            // 0x6C75D8..0x6C75EC rejects only ordered right<left or
            // bottom<top.  Unordered comparisons therefore remain valid.
            if(!(item.viewport[2] < item.viewport[0]) &&
               !(item.viewport[3] < item.viewport[1])) {
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
                outRect = {
                    static_cast<tjs_real>(clipLeft),
                    static_cast<tjs_real>(clipTop),
                    static_cast<tjs_real>(clipRight),
                    static_cast<tjs_real>(clipBottom),
                };
                // Player_renderToCanvas @0x6C7820..0x6C78DC supplies four
                // Real Variants.  Layer.setClip owns the later integer
                // conversion; this caller performs no native GetClip readback.
                (void)callLayerSetClipLike_0x6C7440(
                    layerClassObject, renderLayerObject,
                    outRect[0], outRect[1],
                    outRect[2] - outRect[0], outRect[3] - outRect[1]);
                return true;
            }

            outRect = {
                0.0,
                0.0,
                static_cast<tjs_real>(canvasWidth),
                static_cast<tjs_real>(canvasHeight),
            };
            (void)callLayerResetClipLike_0x6C7440(
                layerClassObject, renderLayerObject);
            return true;
        };

        for(auto *itemPtr : mainList) {
            if(!itemPtr) {
                continue;
            }
            auto &item = *itemPtr;

            // Player_renderToCanvas @0x6C75C8 checks the raw item fields before
            // any clip/source work.  Only raw zero opacity is a gate; negative
            // and >255 values survive to the submitted Integer Variant.
            const tjs_int rawOpacity = item.opacity;
            if(item.skipFlag0 || item.rawFlag16 || rawOpacity == 0) {
                continue;
            }

            std::array<tjs_real, 4> targetLayerClip{};
            if(!applyTargetLayerClipLike_0x6C7440(item, targetLayerClip)) {
                continue;
            }
            detail::logoChainTraceLogf(
                motionPath, "execute.setClip", "0x6C7440", _clampedEvalTime,
                "nodeIndex={} targetClip=[{},{},{},{}]",
                item.nodeIndex, targetLayerClip[0], targetLayerClip[1],
                targetLayerClip[2], targetLayerClip[3]);
            if(_priorDraw && !item.skipFlag1) {
                continue;
            }

            // The AArch64 add-sign/asr sequence at 0x6C764C..0x6C7668 is the
            // compiler expansion of signed `/ 2`: C++ division preserves the
            // original source-level rounding-toward-zero boundary.
            const tjs_int opa = _priorDraw ? rawOpacity / 2 : rawOpacity;

            // Player_renderToCanvas @0x6C7634..0x6C767C converts the four
            // paintBox floats to int with FCVTZS and ORs that rectangle into
            // player+864 before submitting the item. This region survives
            // until the next Player.clear call, so pixels occupied only by
            // the previous frame are still erased.
            _drawRegion.Or(tTVPRect(
                fcvtzsWLike_0x6C7440(item.paintBox[0]),
                fcvtzsWLike_0x6C7440(item.paintBox[1]),
                fcvtzsWLike_0x6C7440(item.paintBox[2]),
                fcvtzsWLike_0x6C7440(item.paintBox[3])));

            // 0x6C7440 constructs these owners in descriptor -> color -> source
            // Variant -> source accessor order and keeps all four alive across
            // the selected direct/buffered copy. Ordinary reverse destruction
            // therefore releases accessor -> source -> color -> descriptor.
            ncbPropAccessor descriptor{tTJSVariant(_sourceDescriptor)};
            descriptor.SetValue(TJS_W("key"), item.commandKey,
                                TJS_MEMBERENSURE,
                                &detail::commandKeyMemberHint_guess);
            descriptor.SetValue(TJS_W("src"), item.commandSrc,
                                TJS_MEMBERENSURE,
                                &detail::commandSrcMemberHint_guess);
            descriptor.SetValue(TJS_W("blendMode"),
                                static_cast<tjs_int>(item.blendMode),
                                TJS_MEMBERENSURE,
                                &detail::blendModeMemberHint_guess);

            ncbPropAccessor color{tTJSVariant(_sourceColors)};
            for(tjs_int index = 0; index < 4; ++index) {
                color.SetValue(
                    index,
                    item.packedColors[static_cast<std::size_t>(index)],
                    TJS_MEMBERENSURE);
            }

            ResolvedSourceObject source;
            source.object = resolveRenderSourceLike_0x6C1B70_guess(
                item.sourceState->object);
            source.layerObject = source.object.AsObjectNoAddRef();
#if defined(KRKR2_WASMTIME_HEADLESS)
            // Native image inspection belongs only to the differential probe.
            // Player_renderToCanvas @0x6C7440 keeps the source as a TJS owner
            // and does not issue NativeInstanceSupport/GetMainImage queries.
            source.layer = resolveNativeLayer(source.layerObject);
            source.image = source.layer ? source.layer->GetMainImage() : nullptr;
#endif
            ncbPropAccessor sourceAccessor{source.object};
            source.width = propGetIntOnceLike_0x6635DC(
                sourceAccessor, TJS_W("width"),
                &detail::widthMemberHint_guess);
            source.height = propGetIntOnceLike_0x6635DC(
                sourceAccessor, TJS_W("height"),
                &detail::heightMemberHint_guess);
            detail::logoChainTraceLogf(
                motionPath, "execute.source", "0x6C1B70/0x6A7BA8",
                _clampedEvalTime,
                "source={} sourceObject={} image={}x{}",
                item.sourceKey,
                static_cast<const void *>(source.layerObject),
                source.width, source.height);

                // Exactly one 0x6C1B70 resolve per item.  Its Object owner must
                // span both direct and buffered branches.
                const tTVPRect sourceRect(0, 0, source.width, source.height);
                const auto blendMode =
                    resolveBlendOperationModeLike_0x6C7440(item.blendMode);
                const auto effectiveColor =
                    unpackPackedRgba(item.packedColors[0]);
                const bool useDirectRenderPath =
                    shouldUseDirectRenderPathLike_0x6C7440(
                        item, _completionType);
                item.executedDirect = useDirectRenderPath;
                item.builtRect = {
                    fcvtzsWLike_0x6C7440(
                        static_cast<float>(targetLayerClip[0])),
                    fcvtzsWLike_0x6C7440(
                        static_cast<float>(targetLayerClip[1])),
                    fcvtzsWLike_0x6C7440(
                        static_cast<float>(targetLayerClip[2])),
                    fcvtzsWLike_0x6C7440(
                        static_cast<float>(targetLayerClip[3])),
                };

                if(useDirectRenderPath) {
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
                            blendMode, opa,
                            static_cast<tTVPBBStretchType>(_completionType));
                    };
#endif
                    if(item.meshType == 0) {
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
                        (void)callLayerOperateAffineLike_0x6C7440(
                            layerClassObject, renderLayerObject,
                            worldPts.data(), source.object, sourceRect,
                            blendMode, opa);
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
                        std::array<tjs_int, 2> cellDivisions{
                            item.meshDivX, item.meshDivY
                        };
                        if(item.meshType == 1) {
                            cellDivisions =
                                bezierPatchCellDivisionsU32Like_0x6C8E5C(
                                    item.commandPatchDivision,
                                    item.sourceState
                                        ? item.sourceState->width
                                        : 0.0,
                                    item.sourceState
                                        ? item.sourceState->height
                                        : 0.0);
                        } else if(item.meshType != 2) {
                            continue;
                        }
                        // libkrkr2.so sub_6C7440 operateMesh/operateBezierPatch
                        // blocks build the point array with a -0.5,-0.5 world
                        // offset (0xBF000000BF000000) and dispatch through the
                        // Layer class accessor with the target render layer as
                        // objthis. The clear flag in both direct mesh blocks is
                        // the literal Integer 0.
                        iTJSDispatch2 *meshArray =
                            buildMeshPointTJSArrayLike_0x6C715C(
                                renderMeshPoints, -0.5f, -0.5f);
                        if(item.meshType == 1) {
                            branch = "direct.operateBezierPatch";
#if defined(KRKR2_WASMTIME_HEADLESS)
                            emitDirectProbe(
                                "Player::executeLayerRenderCommands.direct.beforeOperateBezierPatch",
                                "before");
#endif
                            (void)callLayerOperateBezierPatchLike_0x6C7440(
                                layerClassObject, renderLayerObject,
                                source.object, sourceRect,
                                meshArray, cellDivisions[0], cellDivisions[1],
                                blendMode, opa, false);
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
                            (void)callLayerOperateMeshLike_0x6C7440(
                                layerClassObject, renderLayerObject,
                                source.object, sourceRect,
                                meshArray, cellDivisions[0], cellDivisions[1],
                                blendMode, opa, false);
#if defined(KRKR2_WASMTIME_HEADLESS)
                            emitDirectProbe(
                                "Player::executeLayerRenderCommands.direct.afterOperateMesh",
                                "after");
#endif
                        }
                        meshArray->Release();
                    }
                    detail::logoChainTraceLogf(
                        motionPath, "execute.copy", "0x6C7440",
                        _clampedEvalTime,
                        "branch={} nodeIndex={} clipRect=[{},{},{},{}] dirtyRect=[{},{},{},{}] blendMode={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] visibleAncestorIndex={} completionType={} renderPath=direct workLayer=0x0 renderLayer={}x{}",
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
                        _completionType,
                        canvasWidth, canvasHeight);
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

                // 0x6C7BB0..0x6C7C90: preserve the three nested owners exactly:
                // RM raw dispatch owner -> bufLayer Variant -> buf raw dispatch
                // owner. Reverse destruction therefore matches 0x6C85B4..D8.
                ncbPropAccessor resourceManager{
                    tTJSVariant(_sourceCacheObject)};
                tTJSVariant bufLayer;
                {
                    tTJSVariant propertyResult;
                    iTJSDispatch2 *resourceManagerObject =
                        resourceManager.GetDispatch();
                    (void)resourceManagerObject->PropGet(
                        0, TJS_W("bufLayer"),
                        &detail::bufLayerMemberHint_guess, &propertyResult,
                        resourceManagerObject);
                    bufLayer.CopyRef(propertyResult);
                }
                ncbPropAccessor buffer{tTJSVariant(bufLayer)};
                iTJSDispatch2 *bufferObject = buffer.GetDispatch();

                // The target width/height are TJS property reads performed
                // after acquiring bufLayer; cached native dimensions are not
                // substituted for this observable dispatch sequence.
                const tjs_int targetWidthInteger =
                    callLayerPropGetIntLike_0x6C99B8(
                        layerClassObject, renderLayerObject, TJS_W("width"),
                        &detail::widthMemberHint_guess);
                const float targetWidth =
                    static_cast<float>(targetWidthInteger);
                const tjs_int targetHeightInteger =
                    callLayerPropGetIntLike_0x6C99B8(
                        layerClassObject, renderLayerObject, TJS_W("height"),
                        &detail::heightMemberHint_guess);
                const float targetHeight =
                    static_cast<float>(targetHeightInteger);

                // 0x6C7CBC..0x6C7D40 uses FMAXNM only for left/top and the
                // ordered compare/select form for right/bottom.  It has only
                // the right<left exit; a vertically inverted or zero extent is
                // deliberately allowed to reach Layer.setSize.
                const float bufferLeft =
                    std::fmax(item.paintBox[0], 0.0f);
                const float bufferTop =
                    std::fmax(item.paintBox[1], 0.0f);
                const float bufferRight = item.paintBox[2] < targetWidth
                    ? item.paintBox[2]
                    : targetWidth;
                const float bufferBottom = item.paintBox[3] < targetHeight
                    ? item.paintBox[3]
                    : targetHeight;
                if(bufferRight < bufferLeft) {
                    continue;
                }
                const tjs_real bufferWidth =
                    static_cast<tjs_real>(bufferRight - bufferLeft);
                const tjs_real bufferHeight =
                    static_cast<tjs_real>(bufferBottom - bufferTop);
                (void)callLayerSetSizeRealLike_0x6C7440(
                    bufferObject, bufferWidth, bufferHeight);

                const float pointOffsetX = -0.5f - bufferLeft;
                const float pointOffsetY = -0.5f - bufferTop;
                const auto completionType =
                    static_cast<tTVPBBStretchType>(_completionType);
                if(item.meshType == 0) {
                    const auto localPoints = buildAffineTrianglePoints(
                        item.corners, pointOffsetX, pointOffsetY);
                    (void)callLayerAffineCopyLike_0x6C7440(
                        bufferObject, localPoints.data(), source.object,
                        sourceRect, completionType, true);
                } else if(item.meshType == 1 || item.meshType == 2) {
                    const auto &renderMeshPoints = item.meshType == 2
                        ? item.commandCompositeMeshPoints
                        : item.meshPoints;
                    std::array<tjs_int, 2> cellDivisions{
                        item.meshDivX, item.meshDivY
                    };
                    if(item.meshType == 1) {
                        cellDivisions =
                            bezierPatchCellDivisionsU32Like_0x6C8E5C(
                                item.commandPatchDivision,
                                item.sourceState ? item.sourceState->width
                                                 : 0.0,
                                item.sourceState ? item.sourceState->height
                                                 : 0.0);
                    }
                    iTJSDispatch2 *meshArray =
                        buildMeshPointTJSArrayLike_0x6C715C(
                            renderMeshPoints, pointOffsetX, pointOffsetY);
                    if(item.meshType == 1) {
                        (void)callLayerBezierPatchCopyLike_0x6C7440(
                            bufferObject, source.object, sourceRect,
                            meshArray, cellDivisions[0], cellDivisions[1],
                            completionType, true);
                    } else {
                        (void)callLayerMeshCopyLike_0x6C7440(
                            bufferObject, source.object, sourceRect,
                            meshArray, cellDivisions[0], cellDivisions[1],
                            completionType, true);
                    }
                    meshArray->Release();
                }

                // 0x6C82DC..0x6C83C0 walks the parent pointer chain.  Each
                // helper invocation receives independently owning dst/src
                // Variant copies; declaration order makes src die before dst.
                for(auto *ancestor = item.parentItem; ancestor;
                    ancestor = ancestor->parentItem) {
                    if(ancestor->rawFlag21 && !ancestor->rawFlag16) {
                        tTJSVariant maskDestination(bufLayer);
                        const tTJSVariant &selectedMask =
                            (ancestor->stencilComposite & 4) != 0
                                ? ancestor->composedLayer
                                : ancestor->leafLayer;
                        tTJSVariant maskSource(selectedMask);
                        (void)applyMotionAlphaMaskLike_0x6AF104(
                            maskDestination.AsObjectNoAddRef(),
                            fcvtzsWLike_0x6C7440(
                                ancestor->clipRect[0] - bufferLeft),
                            fcvtzsWLike_0x6C7440(
                                ancestor->clipRect[1] - bufferTop),
                            maskSource.AsObjectNoAddRef(), 0, 0,
                            fcvtzsWLike_0x6C7440(
                                ancestor->clipRect[2] -
                                ancestor->clipRect[0]),
                            fcvtzsWLike_0x6C7440(
                                ancestor->clipRect[3] -
                                ancestor->clipRect[1]),
                            64, _maskMode, ancestor->stencilComposite,
                            motionPath, _clampedEvalTime, item.nodeIndex,
                            ancestor->nodeIndex);
                    } else if((ancestor->stencilComposite & 3) == 1) {
                        // Layer_fillRect_ncb @0x81D6E0 rejects this deliberate
                        // argc=4 call.  0x6C7440 ignores the error and still
                        // terminates the ancestor walk before operateRect.
                        (void)callLayerFillRect4Like_0x6C7440(
                            bufferObject, bufferWidth, bufferHeight);
                        break;
                    }
                }

                // Exact argv types: Real L/T, Object CopyRef(bufLayer), two
                // Integer zeros, Real W/H, Integer mode/opacity. Return ignored.
                (void)callLayerOperateRectLike_0x6C7440(
                    layerClassObject, renderLayerObject,
                    static_cast<tjs_real>(bufferLeft),
                    static_cast<tjs_real>(bufferTop), bufLayer, bufferWidth,
                    bufferHeight, blendMode, opa);
                item.builtRect = {
                    fcvtzsWLike_0x6C7440(bufferLeft),
                    fcvtzsWLike_0x6C7440(bufferTop),
                    fcvtzsWLike_0x6C7440(bufferRight),
                    fcvtzsWLike_0x6C7440(bufferBottom),
                };
                detail::logoChainTraceLogf(
                    motionPath, "execute.copy", "0x6C7440", _clampedEvalTime,
                    "branch=buffered.bufLayer.operateRect nodeIndex={} clipRect=[{},{},{},{}] dirtyRect=[{},{},{},{}] blendMode={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] completionType={} renderPath=buffered bufferRect=[{},{},{},{}] renderLayer={}x{} ancestor={}",
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
                    _completionType,
                    bufferLeft, bufferTop, bufferRight, bufferBottom,
                    canvasWidth, canvasHeight,
                    item.parentItem ? item.parentItem->nodeIndex : -1);
                if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                   motionPath.find("m2logo.mtn") != std::string::npos &&
                   _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                    std::fprintf(stderr,
                                 "SNAPCOPY order=%d frame=%.3f nodeIndex=%d source=%s branch=buffered.bufLayer.operateRect clipRect=[%.3f,%.3f,%.3f,%.3f] opacity=%d blend=%d ancestor=%d\n",
                                 snapshotCopyOrder++, _clampedEvalTime,
                                 item.nodeIndex,
                                 item.sourceKey.empty()
                                     ? "<none>"
                                     : item.sourceKey.c_str(),
                                 item.clipRect[0], item.clipRect[1],
                                 item.clipRect[2], item.clipRect[3],
                                 opa, item.blendMode,
                                 item.parentItem
                                     ? item.parentItem->nodeIndex
                                     : -1);
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
        callLayerResetClipLike_0x6C7440(
            layerClassObject, renderLayerObject);
        (void)skipUpdate;
#if defined(KRKR2_WASMTIME_HEADLESS)
        renderTrace.setResult(true);
#endif
        return true;
    }

} // namespace motion
