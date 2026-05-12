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
    bool Player::buildRenderCommands(tjs_int canvasWidth, tjs_int canvasHeight) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderBuildCommandsEnter(
            this, static_cast<int>(canvasWidth), static_cast<int>(canvasHeight));
#endif
        // Equivalent to sub_6D5164 @ 0x6D5178's `player+544` null gate —
        // the port has no explicit +544 mirror, so an absent runtime is
        // the canonical "no render list yet" signal.
        if(!_runtime) {
#if defined(KRKR2_WASMTIME_HEADLESS)
            detail::motionTraceRenderBuildCommandsLeave(
                this, static_cast<int>(canvasWidth),
                static_cast<int>(canvasHeight));
#endif
            return false;
        }

        _runtime->preparedRenderItemsTopLevel.clear();
        _runtime->preparedRenderItemsGroup.clear();
        const auto motionPath =
            _runtime->activeMotion ? _runtime->activeMotion->path : std::string{};
        for(auto &entry : _runtime->preparedRenderItems) {
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
                    clipRect.left, clipRect.top, clipRect.right, clipRect.bottom
                };
                entry.dirtyRect = entry.clipRect;

                for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                    entry.localCorners[ci] =
                        entry.corners[ci] - 0.5f - static_cast<float>(clipRect.left);
                    entry.localCorners[ci + 1] =
                        entry.corners[ci + 1] - 0.5f - static_cast<float>(clipRect.top);
                }

                entry.localMeshPoints.clear();
                entry.localMeshPoints.reserve(entry.meshPoints.size());
                for(size_t pi = 0; pi + 1 < entry.meshPoints.size(); pi += 2) {
                    entry.localMeshPoints.push_back(
                        entry.meshPoints[pi] - 0.5f -
                        static_cast<float>(clipRect.left));
                    entry.localMeshPoints.push_back(
                        entry.meshPoints[pi + 1] - 0.5f -
                        static_cast<float>(clipRect.top));
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
            }

            persistNativeRenderItemFieldLifetimeLike_0x6C4E28(entry);
            if(entry.groupList) {
                _runtime->preparedRenderItemsGroup.push_back(&entry);
            }
            if(entry.topLevelList) {
                _runtime->preparedRenderItemsTopLevel.push_back(&entry);
            }
        }
        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           _clampedEvalTime >= 43.0 && _clampedEvalTime <= 50.0) {
            for(size_t i = 0; i < _runtime->preparedRenderItems.size(); ++i) {
                const auto &item = _runtime->preparedRenderItems[i];
                if(!(item.nodeIndex == 14 || item.nodeIndex == 15 ||
                     item.nodeIndex == 19 ||
                     (item.nodeIndex >= 20 && item.nodeIndex <= 29))) {
                    continue;
                }
                std::fprintf(
                    stderr,
                    "SNAPCMD frame=%.3f order=%zu nodeIndex=%d source=%s groupOnly=%d topLevel=%d groupList=%d rawFlags=[%d,%d,%d,%d,%d,%d] parentNodeIndex=%d hasRenderParent=%d childCount=%zu layerId=(%d,%d) clipRect=[%d,%d,%d,%d] opacity=%d blend=%d\n",
                    _clampedEvalTime,
                    i,
                    item.nodeIndex,
                    item.sourceKey.empty() ? "<none>" : item.sourceKey.c_str(),
                    item.groupOnly ? 1 : 0,
                    item.topLevelList ? 1 : 0,
                    item.groupList ? 1 : 0,
                    item.rawFlag16 ? 1 : 0,
                    item.skipFlag0 ? 1 : 0,
                    item.skipFlag1 ? 0 : 1,
                    item.drawFlag ? 1 : 0,
                    item.rawFlag20 ? 1 : 0,
                    item.rawFlag21 ? 1 : 0,
                    item.parentItem ? item.parentItem->nodeIndex
                                    : item.visibleAncestorIndex,
                    item.parentItem ? 1 : 0,
                    item.childItems.size(),
                    item.layerId,
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
            "canvas={}x{} preparedItems={} topLevelList={} groupList={}",
            canvasWidth, canvasHeight, _runtime->preparedRenderItems.size(),
            _runtime->preparedRenderItemsTopLevel.size(),
            _runtime->preparedRenderItemsGroup.size());
        const bool ok = !_runtime->preparedRenderItems.empty();
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderBuildCommandsLeave(
            this, static_cast<int>(canvasWidth), static_cast<int>(canvasHeight));
#endif
        return ok;
    }


    bool Player::executeLayerRenderCommands(iTJSDispatch2 *renderLayerObject,
                                            bool skipUpdate) {
        if(!renderLayerObject || !_runtime || !_runtime->activeMotion) {
            return false;
        }
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::MotionTraceRenderExecuteScope renderTrace(
            this, renderLayerObject, skipUpdate);
#endif
        const auto motionPath = _runtime->activeMotion->path;

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
            "renderItems={} topLevelItems={} groupItems={} renderLayer={} scratchOwner={} scratchParent={} skipUpdate={}",
            _runtime->preparedRenderItems.size(),
            _runtime->preparedRenderItemsTopLevel.size(),
            _runtime->preparedRenderItemsGroup.size(),
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

        using PreparedRenderItem = detail::PlayerRuntime::PreparedRenderItem;
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
            if(item.sourceKey.empty() || !_runtime->sourceCacheNative) {
                return resolved;
            }

            resolved.object = _runtime->sourceCacheNative->loadRenderSourceByName(
                detail::widen(item.sourceKey), item.srcRef, item.blendMode,
                item.packedColors, scratchOwner, scratchParent);
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

        const int playerStencilType = _maskMode;
        auto ensureLeafItemLayer =
            [&](PreparedRenderItem &item) -> iTJSDispatch2 * {
            const tjs_int stateLayerId = item.layerId;
            if(stateLayerId == 0) {
                return ensureReusableLayerObject(
                    item.leafLayer,
                    scratchOwner,
                    scratchParent,
                    static_cast<tTVPLayerType>(ltAlpha),
                    false);
            }

            auto &state = _runtime->renderLayerStates[stateLayerId];
            if(!state.initialized) {
                state.layerId = stateLayerId;
                state.absolute = _runtime->nextLayerAbsolute++;
                state.hitThreshold = 256;
                state.initialized = true;
                if(item.nodeIndex >= 0 &&
                   item.nodeIndex < static_cast<int>(_runtime->nodes.size())) {
                    const auto &node = _runtime->nodes[item.nodeIndex];
                    state.layerGetter = getLayerGetter(detail::widen(node.layerName));
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
            item.rawFlag20 = true;
            persistNativeRenderItemFieldLifetimeLike_0x6C4E28(item);

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
        auto ensureComposedItemLayer =
            [&](PreparedRenderItem &item) -> iTJSDispatch2 * {
            return ensureReusableLayerObject(
                item.composedLayer,
                scratchOwner,
                scratchParent,
                static_cast<tTVPLayerType>(ltAlpha),
                false);
        };
        auto renderItemSourceToLayer =
            [&](PreparedRenderItem &item,
                iTJSDispatch2 *targetLayerObject,
                tTJSNI_BaseLayer *targetLayer,
                iTVPBaseBitmap *srcImage,
                const tTVPRect &sourceRect,
                const char *branch) -> bool {
            if(!targetLayerObject || !targetLayer) {
                return false;
            }
            const int clipWidth = item.clipRect[2] - item.clipRect[0];
            const int clipHeight = item.clipRect[3] - item.clipRect[1];
            if(clipWidth <= 0 || clipHeight <= 0) {
                return false;
            }
            if(!prepareLayerForRender(targetLayerObject, clipWidth, clipHeight,
                                      0x00000000)) {
                return false;
            }
            if(!srcImage || srcImage->GetWidth() <= 0 || srcImage->GetHeight() <= 0) {
                return true;
            }
            if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
               motionPath.find("m2logo.mtn") != std::string::npos &&
               _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                std::fprintf(
                    stderr,
                    "SNAPGEOM phase=leafSource frame=%.3f nodeIndex=%d source=%s meshType=%d layerSize=%dx%d sourceRect=[%d,%d,%d,%d] clipRect=[%d,%d,%d,%d] worldCorners=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f] localCorners=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]\n",
                    _clampedEvalTime,
                    item.nodeIndex,
                    item.sourceKey.empty() ? "<none>" : item.sourceKey.c_str(),
                    item.meshType,
                    clipWidth,
                    clipHeight,
                    sourceRect.left,
                    sourceRect.top,
                    sourceRect.right,
                    sourceRect.bottom,
                    item.clipRect[0],
                    item.clipRect[1],
                    item.clipRect[2],
                    item.clipRect[3],
                    item.corners[0],
                    item.corners[1],
                    item.corners[2],
                    item.corners[3],
                    item.corners[4],
                    item.corners[5],
                    item.corners[6],
                    item.corners[7],
                    item.localCorners[0],
                    item.localCorners[1],
                    item.localCorners[2],
                    item.localCorners[3],
                    item.localCorners[4],
                    item.localCorners[5],
                    item.localCorners[6],
                    item.localCorners[7]);
            }
            if(item.meshType == 0) {
                const auto localPts =
                    buildAffineTrianglePoints(item.localCorners, 0.0f, 0.0f);
                targetLayer->AffineCopy(localPts.data(), srcImage,
                                        sourceRect, stNearest, _clearEnabled);
#if defined(KRKR2_WASMTIME_HEADLESS)
                recordPostDrawCandidate(
                    targetLayerObject,
                    "Player::executeLayerRenderCommands.item.afterAffineCopy");
#endif
            } else {
                if(item.localMeshPoints.empty() || item.meshDivX < 2 ||
                   item.meshDivY < 2) {
                    return false;
                }
                auto localMeshPoints =
                    buildMeshPoints(item.localMeshPoints, 0.0f, 0.0f);
                if(item.meshType == 1) {
                    targetLayer->BezierPatchCopy(
                        localMeshPoints.data(), item.meshDivX,
                        item.meshDivY, srcImage, sourceRect, stNearest,
                        _clearEnabled);
#if defined(KRKR2_WASMTIME_HEADLESS)
                    recordPostDrawCandidate(
                        targetLayerObject,
                        "Player::executeLayerRenderCommands.item.afterBezierPatchCopy");
#endif
                } else if(item.meshType == 2) {
                    targetLayer->MeshCopy(localMeshPoints.data(),
                                          item.meshDivX, item.meshDivY,
                                          srcImage, sourceRect, stNearest,
                                          _clearEnabled);
#if defined(KRKR2_WASMTIME_HEADLESS)
                    recordPostDrawCandidate(
                        targetLayerObject,
                        "Player::executeLayerRenderCommands.item.afterMeshCopy");
#endif
                } else {
                    return false;
                }
            }
            detail::logoChainTraceLogf(
                motionPath, "execute.layerSource", "0x6C7440", _clampedEvalTime,
                "branch={} nodeIndex={} clipRect=[{},{},{},{}] layer={}x{} clearEnabled={}",
                branch, item.nodeIndex,
                item.clipRect[0], item.clipRect[1],
                item.clipRect[2], item.clipRect[3],
                clipWidth, clipHeight, _clearEnabled ? 1 : 0);
            return true;
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
            // libkrkr2.so sub_6C9CA8 sizes the SLA item layer to the clipped
            // draw bounds and clears it transparent before the affine copy.
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
                   0x00000000)) {
                return false;
            }

            const float offsetX = -0.5f - clipLeft;
            const float offsetY = -0.5f - clipTop;
            if(item.meshType == 0) {
                const auto localPts =
                    buildAffineTrianglePoints(item.corners, offsetX, offsetY);
                candidateLayer->AffineCopy(localPts.data(), source.image,
                                           sourceRect, stNearest, true);
                recordPostDrawCandidate(
                    candidateLayerObject,
                    "Player::executeLayerRenderCommands.accurateSla.item.afterAffineCopy");
                return true;
            }
            if(item.meshPoints.empty() || item.meshDivX < 2 ||
               item.meshDivY < 2) {
                return false;
            }
            auto localMeshPoints =
                buildMeshPoints(item.meshPoints, offsetX, offsetY);
            if(item.meshType == 1) {
                candidateLayer->BezierPatchCopy(
                    localMeshPoints.data(), item.meshDivX, item.meshDivY,
                    source.image, sourceRect, stNearest, true);
                recordPostDrawCandidate(
                    candidateLayerObject,
                    "Player::executeLayerRenderCommands.accurateSla.item.afterBezierPatchCopy");
                return true;
            }
            if(item.meshType == 2) {
                candidateLayer->MeshCopy(
                    localMeshPoints.data(), item.meshDivX, item.meshDivY,
                    source.image, sourceRect, stNearest, true);
                recordPostDrawCandidate(
                    candidateLayerObject,
                    "Player::executeLayerRenderCommands.accurateSla.item.afterMeshCopy");
                return true;
            }
            return false;
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

            // libkrkr2.so Player_renderToCanvas_guess @ 0x6C77C4..0x6C78DC:
            // set target Layer clip before both direct and composed output. The
            // later operateAffine call still receives the full source rect.
            if(hasViewportClip) {
                renderLayer->SetClip(outRect.left, outRect.top,
                                     outRect.right - outRect.left,
                                     outRect.bottom - outRect.top);
            } else {
                renderLayer->ResetClip();
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
                !(_preview && item.skipFlag1) && item.opacity > 0;

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

            iTJSDispatch2 *leafLayerObject = ensureLeafItemLayer(item);
            auto *leafLayer = resolveNativeLayer(leafLayerObject);
            if(!leafLayerObject || !leafLayer) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.workLayer", "0x6C7440",
                    _clampedEvalTime,
                    "leaf layer should resolve for buffered item path",
                    fmt::format("nodeIndex={} leafLayer={}",
                                item.nodeIndex,
                                static_cast<const void *>(leafLayer)),
                    false,
                    "sub_6C7440 could not allocate the per-item leaf layer");
                return false;
            }

            if(!renderItemSourceToLayer(item, leafLayerObject, leafLayer,
                                        source.image, sourceRect,
                                        "item.leaf.affineCopy")) {
                return false;
            }
            item.leafBuilt = true;
            item.builtRect = item.clipRect;

            bool hasBuiltChildren = false;
            for(auto *childItem : item.childItems) {
                hasBuiltChildren =
                    self(self, childItem) || hasBuiltChildren;
            }

            if(!hasBuiltChildren) {
                return true;
            }

            iTJSDispatch2 *composedLayerObject = ensureComposedItemLayer(item);
            auto *composedLayer = resolveNativeLayer(composedLayerObject);
            if(!composedLayerObject || !composedLayer) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.workLayer", "0x6C7440",
                    _clampedEvalTime,
                    "composed layer should resolve for parent item path",
                    fmt::format("nodeIndex={} composedLayer={}",
                                item.nodeIndex,
                                static_cast<const void *>(composedLayer)),
                    false,
                    "sub_6C7440 could not allocate the composed output layer");
                return false;
            }

            if(!prepareLayerForRender(composedLayerObject, clipWidth, clipHeight,
                                      0x00000000)) {
                return false;
            }
            if(item.leafBuilt) {
                const auto localRect = localRectFromItem(item);
                composedLayer->CopyRect(0, 0, leafLayer->GetMainImage(),
                                        nullptr, localRect);
            }

            for(auto *childPtr : item.childItems) {
                if(!childPtr) {
                    continue;
                }
                auto &child = *childPtr;
                if(!child.rawFlag21 || child.rawFlag16) {
                    continue;
                }
                if((item.stencilComposite & 4) != 0) {
                    auto *childMaskLayerObject =
                        child.leafLayer.Type() == tvtObject
                            ? child.leafLayer.AsObjectNoAddRef()
                            : nullptr;
                    if(!childMaskLayerObject) {
                        continue;
                    }
                    const int childWidth = child.builtRect[2] - child.builtRect[0];
                    const int childHeight = child.builtRect[3] - child.builtRect[1];
                    if(childWidth <= 0 || childHeight <= 0) {
                        continue;
                    }
                    applyMotionAlphaMaskLike_0x6AF104(
                        composedLayerObject,
                        child.builtRect[0] - item.clipRect[0],
                        child.builtRect[1] - item.clipRect[1],
                        childMaskLayerObject,
                        0,
                        0,
                        childWidth,
                        childHeight,
                        64,
                        playerStencilType,
                        item.stencilComposite,
                        motionPath,
                        _clampedEvalTime,
                        item.nodeIndex,
                        child.nodeIndex);
                    continue;
                }

                auto *childOutputLayerObject = chooseItemOutputLayerObject(child);
                auto *childOutputLayer = resolveNativeLayer(childOutputLayerObject);
                if(!childOutputLayerObject || !childOutputLayer) {
                    continue;
                }
                const auto childLocalRect = localRectFromItem(child);
                const auto childBlendMode =
                    resolveBlendOperationModeLike_0x6C7440(child.blendMode);
                const auto childOpacity = static_cast<tjs_int>(
                    std::clamp(child.opacity, 0, 255));
                if(childOpacity <= 0) {
                    continue;
                }
                composedLayer->OperateRect(
                    child.builtRect[0] - item.clipRect[0],
                    child.builtRect[1] - item.clipRect[1],
                    childOutputLayer->GetMainImage(),
                    childLocalRect,
                    childBlendMode,
                    childOpacity);
            }

            item.composedBuilt = true;
            return true;
        };

        for(auto *itemPtr : _runtime->preparedRenderItemsTopLevel) {
            if(!itemPtr) {
                continue;
            }
            auto &item = *itemPtr;

            const auto blendMode =
                resolveBlendOperationModeLike_0x6C7440(item.blendMode);
            const auto effectiveColor = unpackPackedRgba(item.packedColors[0]);
            const auto opa = static_cast<tjs_int>(
                std::clamp(item.opacity, 0, 255));
            if(opa <= 0) {
                continue;
            }

            // libkrkr2.so 0x6C7440 reads item+17/item+16 first, then updates
            // target Layer clip, and only then applies the preview item+18 gate.
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
            if(_preview && item.skipFlag1) {
                continue;
            }
            if(item.parentItem) {
                continue;
            }
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
                        if(item.meshPoints.empty() ||
                           item.meshDivX < 2 || item.meshDivY < 2) {
                            continue;
                        }
                        auto worldMeshPoints =
                            buildMeshPoints(item.meshPoints, -0.5f, -0.5f);
                        if(item.meshType == 1) {
                            branch = "direct.operateBezierPatch";
#if defined(KRKR2_WASMTIME_HEADLESS)
                            emitDirectProbe(
                                "Player::executeLayerRenderCommands.direct.beforeOperateBezierPatch",
                                "before");
#endif
                            renderLayer->OperateBezierPatch(
                                worldMeshPoints.data(), item.meshDivX,
                                item.meshDivY, source.image, sourceRect,
                                blendMode, opa, stNearest, _clearEnabled);
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
                            renderLayer->OperateMesh(
                                worldMeshPoints.data(), item.meshDivX,
                                item.meshDivY, source.image, sourceRect,
                                blendMode, opa, stNearest, _clearEnabled);
#if defined(KRKR2_WASMTIME_HEADLESS)
                            emitDirectProbe(
                                "Player::executeLayerRenderCommands.direct.afterOperateMesh",
                                "after");
#endif
                        } else {
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
                                     "SNAPCOPY order=%d frame=%.3f nodeIndex=%d source=%s branch=%s clipRect=[%d,%d,%d,%d] opacity=%d blend=%d\n",
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
                renderLayer->OperateRect(item.clipRect[0], item.clipRect[1],
                                         outputLayer->GetMainImage(), localRect,
                                         blendMode, opa);
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
                                 "SNAPCOPY order=%d frame=%.3f nodeIndex=%d source=%s branch=%s clipRect=[%d,%d,%d,%d] opacity=%d blend=%d childCount=%zu phase=%d\n",
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

        // libkrkr2.so Player_renderToCanvas_guess @ 0x6C8FCC resets the target
        // Layer clip once the top-level render-item walk is complete.
        renderLayer->ResetClip();
        if(!skipUpdate) {
            renderLayer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "execute.update", "0x6C7440", _clampedEvalTime,
                "renderLayer.Update(false) size={}x{}",
                renderLayer->GetWidth(), renderLayer->GetHeight());
        }
#if defined(KRKR2_WASMTIME_HEADLESS)
        renderTrace.setResult(true);
#endif
        return true;
    }

} // namespace motion
