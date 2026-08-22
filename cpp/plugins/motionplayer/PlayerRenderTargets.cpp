// PlayerRenderTargets.cpp — Layer/SLA/D3D render targets and post-draw update
// Split from PlayerRender.cpp for maintainability.
//
#include "PlayerRenderInternal.h"
#include "MotionTraceWeb.h"
#include "MotionRenderBackend.h"
#include "PrivateMotionGLL.h"
#include "RenderManager.h"
#include "ResourceManager.h"
#include "SourceCache.h"
#include "ncbind.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace motion::internal;
using namespace motion::internal::render_detail;

extern bool TVPWindowUpdateEventsDelivering;

namespace motion {
    namespace {
        using PreparedRenderItem = detail::PreparedRenderItem;

        struct DispatchReleaseGuard_guess {
            iTJSDispatch2 *dispatch = nullptr;

            DispatchReleaseGuard_guess() = default;
            DispatchReleaseGuard_guess(
                const DispatchReleaseGuard_guess &) = delete;
            DispatchReleaseGuard_guess &operator=(
                const DispatchReleaseGuard_guess &) = delete;

            ~DispatchReleaseGuard_guess() {
                if(dispatch) {
                    dispatch->Release();
                }
            }
        };

        std::array<tTVPPointD, 6> makeTextureQuad(
            const std::array<int, 4> &rect) {
            const double l = rect[0], t = rect[1];
            const double r = rect[2], b = rect[3];
            return {{
                {l, t}, {r, t}, {l, b}, {r, t}, {l, b}, {r, b},
            }};
        }

        std::array<tTVPPointD, 6> makeAffineTargetQuad(
            const PreparedRenderItem &item,
            float xOffset,
            float yOffset) {
            const tTVPPointD p0{
                static_cast<double>(item.corners[0] + xOffset),
                static_cast<double>(item.corners[1] + yOffset)};
            const tTVPPointD p1{
                static_cast<double>(item.corners[2] + xOffset),
                static_cast<double>(item.corners[3] + yOffset)};
            const tTVPPointD p2{
                static_cast<double>(item.corners[6] + xOffset),
                static_cast<double>(item.corners[7] + yOffset)};
            const tTVPPointD p3{
                p1.x - p0.x + p2.x,
                p1.y - p0.y + p2.y};
            return {{p0, p1, p2, p1, p2, p3}};
        }

        std::vector<tTVPPointD> buildOffsetMeshPoints(
            const std::vector<detail::MeshPoint> &points,
            float xOffset,
            float yOffset) {
            std::vector<tTVPPointD> out;
            out.reserve(points.size());
            for(const auto &point : points) {
                out.push_back({
                    static_cast<double>(point.x + xOffset),
                    static_cast<double>(point.y + yOffset)});
            }
            return out;
        }

        bool shouldQueuePrivateMotionGLLRenderItem_guess(
            const PreparedRenderItem &item,
            bool priorDraw) {
            if((item.blendMode & 0xF) == 6 || item.skipFlag0 ||
               item.rawFlag16 || item.opacity == 0) {
                return false;
            }
            if(priorDraw && !item.skipFlag1) {
                return false;
            }
            return !item.sourceState->blank;
        }

        int privateMotionGLLOpacity_guess(
            const PreparedRenderItem &item,
            bool priorDraw) {
            int opacity = item.opacity;
            if(priorDraw) {
                opacity = opacity >= 0 ? opacity / 2 : (opacity + 1) / 2;
            }
            return opacity;
        }

        std::vector<detail::MeshPoint> *
        populatePrivateMotionGLLPoints_guess(
            PreparedRenderItem &item,
            PrivateMotionGLLRenderItemInput_guess &queueItem) {
            if(item.meshType == 0) {
                queueItem.affinePoints =
                    std::array<detail::MeshPoint, 3>{ {
                        { item.corners[0], item.corners[1] },
                        { item.corners[2], item.corners[3] },
                        { item.corners[6], item.corners[7] },
                    } };
                return nullptr;
            }
            if(item.meshType == 1) {
                return &item.meshPoints;
            }
            if(item.meshType == 2) {
                return &item.commandCompositeMeshPoints;
            }
            return nullptr;
        }

        unsigned int d3dPackedColorWithOpacity(
            const PreparedRenderItem &item,
            int opacity) {
            const auto base = item.packedColors[0];
            const auto rgb = base == 0xFF808080u ? 0x00FFFFFFu
                                                 : (base & 0x00FFFFFFu);
            return rgb |
                (static_cast<unsigned int>(static_cast<std::uint8_t>(opacity))
                 << 24u);
        }

        tTVPLayerType accurateSlaLayerType_guess(int rawBlendMode) {
            switch(rawBlendMode & 0x0F) {
                case 1: return ltPsAdditive;
                case 2:
                case 5: return ltPsSubtractive;
                case 3: return ltPsMultiplicative;
                case 4: return ltPsScreen;
                default: return ltAlpha;
            }
        }

        bool accurateSlaSkipsMaskBuffer_guess(int rawBlendMode) {
            return (rawBlendMode & 0x0F) == 6;
        }

        bool shouldRenderAccurateSlaItem_guess(
            const PreparedRenderItem &item) {
            return !item.skipFlag0 && !item.rawFlag16 && item.opacity != 0;
        }

        bool computeAccurateSlaClip_guess(
            const PreparedRenderItem &item,
            int canvasWidth,
            int canvasHeight,
            RenderClipRect &out) {
            float clipLeft = std::fmax(item.paintBox[0], 0.0f);
            float clipTop = std::fmax(item.paintBox[1], 0.0f);
            const float width = static_cast<float>(canvasWidth);
            const float height = static_cast<float>(canvasHeight);
            float clipRight = item.paintBox[2] < width
                ? item.paintBox[2]
                : width;
            float clipBottom = item.paintBox[3] < height
                ? item.paintBox[3]
                : height;

            if(item.hasViewport && item.viewport[2] >= item.viewport[0] &&
               item.viewport[3] >= item.viewport[1]) {
                const float viewportLeft = std::floor(item.viewport[0]);
                const float viewportTop = std::floor(item.viewport[1]);
                const float viewportRight = std::ceil(item.viewport[2]);
                const float viewportBottom = std::ceil(item.viewport[3]);
                clipLeft = viewportLeft < clipLeft
                    ? clipLeft
                    : viewportLeft;
                clipTop = viewportTop < clipTop
                    ? clipTop
                    : viewportTop;
                clipRight = clipRight < viewportRight
                    ? clipRight
                    : viewportRight;
                clipBottom = clipBottom < viewportBottom
                    ? clipBottom
                    : viewportBottom;
            }

            // Native uses the MI condition after FCMP: equality and unordered
            // (NaN) survive, while only a strictly reversed edge is rejected.
            if(clipRight < clipLeft || clipBottom < clipTop) {
                return false;
            }

            out.left = clipLeft;
            out.top = clipTop;
            out.right = clipRight;
            out.bottom = clipBottom;
            return true;
        }

        void setInternalWorkspaceLayerSize_guess(iTJSDispatch2 *layerObject,
                                                 int width,
                                                 int height) {
            tTJSVariant widthArg(width);
            tTJSVariant heightArg(height);
            tTJSVariant *args[] = { &widthArg, &heightArg };
            (void)layerObject->FuncCall(
                0, TJS_W("setSize"), &detail::setSizeMemberHint_guess,
                nullptr, 2, args, layerObject);
        }

        bool markD3DStencilMaskChain_guess(PreparedRenderItem *item,
                                           std::uint8_t ref) {
            bool hasDrawableMaskTarget = false;
            for(auto *ancestor = item; ancestor; ancestor = ancestor->parentItem) {
                ancestor->stencilMaskRef = ref;
                if(!ancestor->rawFlag16 && !ancestor->skipFlag0) {
                    hasDrawableMaskTarget = true;
                }
                for(auto *child : ancestor->childItems) {
                    child->stencilMaskRef = ref;
                    if(!child->rawFlag16 && !child->skipFlag0 &&
                       child->opacity != 0) {
                        hasDrawableMaskTarget = true;
                    }
                }
            }
            return hasDrawableMaskTarget;
        }

        int assignD3DStencilRefs_guess(
            std::vector<PreparedRenderItem *> &items) {
            for(auto *itemPtr : items) {
                auto &item = *itemPtr;
                item.stencilMaskRef = 0;
                item.stencilWriteRef = 0;
            }

            int stencilCount = 0;
            static bool stencilOverflowLogged = false;
            for(auto *itemPtr : items) {
                auto &item = *itemPtr;
                if((item.blendMode & 0xF) == 6 || !item.drawFlag ||
                   item.rawFlag16 || item.opacity == 0 || !item.parentItem) {
                    continue;
                }
                const int previousStencilCount = stencilCount++;
                if(previousStencilCount >= 255 && !stencilOverflowLogged) {
                    stencilOverflowLogged = true;
                    if(auto logger = LOGGER) {
                        logger->warn(
                            "MMotionPlayer: StencilCount overflow(256)");
                    }
                }
                const auto ref = static_cast<std::uint8_t>(stencilCount);
                item.stencilWriteRef = ref;
                if(!markD3DStencilMaskChain_guess(item.parentItem, ref)) {
                    item.stencilWriteRef = 0;
                }
            }
            return stencilCount;
        }

        bool computeD3DClip_guess(
            const PreparedRenderItem &item,
            int canvasWidth,
            int canvasHeight,
            std::array<float, 4> &out) {
            float clipLeft = std::fmax(item.paintBox[0], 0.0f);
            float clipTop = std::fmax(item.paintBox[1], 0.0f);
            const float width = static_cast<float>(canvasWidth);
            const float height = static_cast<float>(canvasHeight);
            // Keep the native compare/select operand order explicit. Besides
            // selecting the canvas bound for an unordered paint edge, this
            // makes equality select the second operand (including its zero
            // sign) rather than std::min's first argument.
            float clipRight = item.paintBox[2] < width
                ? item.paintBox[2]
                : width;
            float clipBottom = item.paintBox[3] < height
                ? item.paintBox[3]
                : height;

            if(item.viewport[2] >= item.viewport[0] &&
               item.viewport[3] >= item.viewport[1]) {
                const float viewportLeft = std::floor(item.viewport[0]);
                const float viewportTop = std::floor(item.viewport[1]);
                const float viewportRight = std::ceil(item.viewport[2]);
                const float viewportBottom = std::ceil(item.viewport[3]);
                clipLeft = viewportLeft < clipLeft
                    ? clipLeft
                    : viewportLeft;
                clipTop = viewportTop < clipTop
                    ? clipTop
                    : viewportTop;
                clipRight = clipRight < viewportRight
                    ? clipRight
                    : viewportRight;
                clipBottom = clipBottom < viewportBottom
                    ? clipBottom
                    : viewportBottom;
            }

            out = {clipLeft, clipTop, clipRight, clipBottom};
            // The native ordered >= tests reject equality and reversed edges.
            // An unordered edge would survive this gate, although the normal
            // construction above replaces or rejects every input NaN first.
            return !(clipLeft >= clipRight || clipTop >= clipBottom);
        }

        int prepareD3DRenderItems_guess(
            std::vector<PreparedRenderItem *> &items,
            int width,
            int height,
            bool priorDraw) {
            if(priorDraw) {
                return 0;
            }

            const int stencilCount = assignD3DStencilRefs_guess(items);
            for(auto *item : items) {
                if(!item->drawFlag) {
                    continue;
                }
                std::array<float, 4> clip{};
                const bool hasClip = computeD3DClip_guess(
                    *item, width, height, clip);
                if(!hasClip || item->rawFlag16) {
                    item->rawFlag21 = false;
                    continue;
                }
                item->rawFlag21 = true;
                item->clipRect = clip;
                item->leafLayer.Clear();
            }
            return stencilCount;
        }

        void appendD3DAffine_guess(
            motion::render_backend_guess::TriangleBatch_guess &batch,
            iTVPRenderMethod *method,
            iTVPTexture2D *target,
            const tTVPRect &targetRect,
            const PreparedRenderItem &item,
            iTVPTexture2D *sourceTexture,
            const std::array<int, 4> &sourceRect,
            float xOffset,
            float yOffset,
            std::uint32_t packedColor) {
            const auto dst = makeAffineTargetQuad(
                item, xOffset, yOffset);
            const auto src = makeTextureQuad(sourceRect);
            (void)method->IsBlendTarget();
            batch.appendTriangles_guess(
                method, sourceTexture, target, target, targetRect,
                src.data(), dst.data(), dst.size(), packedColor);
        }

        void appendD3DMesh_guess(
            motion::render_backend_guess::TriangleBatch_guess &batch,
            iTVPRenderMethod *method,
            iTVPTexture2D *target,
            const tTVPRect &targetRect,
            iTVPTexture2D *sourceTexture,
            const std::array<int, 4> &sourceRect,
            const std::vector<tTVPPointD> &boundsPoints,
            const std::vector<tTVPPointD> &meshPoints,
            int meshDivX,
            int meshDivY,
            std::uint32_t packedColor) {
            tTVPRect computedBounds(targetRect);
            motion::render_backend_guess::buildAndSubmitMeshTriangles_guess(
                computedBounds, sourceTexture,
                tTVPRect(sourceRect[0], sourceRect[1],
                         sourceRect[2], sourceRect[3]),
                boundsPoints, meshPoints, meshDivX, meshDivY,
                [&](iTVPTexture2D *submittedSourceTexture,
                    const std::vector<tTVPPointD> &sourceVertices,
                    const std::vector<tTVPPointD> &destinationVertices) {
                    (void)method->IsBlendTarget();
                    batch.appendTriangles_guess(
                        method, submittedSourceTexture, target, target,
                        targetRect, sourceVertices.data(),
                        destinationVertices.data(),
                        destinationVertices.size(), packedColor);
                });
        }

        bool shouldSkipD3DRenderItem_guess(
            const PreparedRenderItem &item,
            bool priorDraw) {
            if((item.blendMode & 0xF) == 6) {
                return true;
            }
            if(item.skipFlag0 || item.rawFlag16) {
                return true;
            }
            return priorDraw && !item.skipFlag1;
        }
    } // namespace

    bool internal::render_detail::computeD3DClipForDifferentialTest_guess(
        const detail::PreparedRenderItem &item,
        int canvasWidth,
        int canvasHeight,
        std::array<float, 4> &out) {
        return computeD3DClip_guess(item, canvasWidth, canvasHeight, out);
    }

    tjs_int internal::render_detail::getInternalWorkspaceDimension_guess(
        ncbPropAccessor &object,
        const tjs_char *member,
        tjs_uint32 *hint) {
        if(!object.HasValue(member, hint)) {
            return 0;
        }
        return object.GetValue(
            member, ncbTypedefs::Tag<tjs_int>(), 0, hint);
    }

    void Player::materializeInternalRenderLayers_guess(
        const tTJSVariant &target) {
        // All four references gate only on the primary internal Layer Variant.
        // Its assignment is published before probing dimensions, sizing it,
        // and creating the work Layer. A later call therefore does not repair
        // a missing or incompletely-sized work Layer after partial failure.
        if(_internalRenderLayer.Type() != tvtVoid) {
            return;
        }

        ncbPropAccessor targetAccessor{tTJSVariant(target)};
        tTJSVariant owner = targetAccessor.GetValue(
            TJS_W("window"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::windowMemberHint_guess);

        ncbPropAccessor internal{tTJSVariant(
            _internalRenderLayer =
                detail::createLayerVariant_guess(owner, target))};

        const tjs_int height = getInternalWorkspaceDimension_guess(
            targetAccessor, TJS_W("height"),
            &detail::heightMemberHint_guess);
        const tjs_int width = getInternalWorkspaceDimension_guess(
            targetAccessor, TJS_W("width"),
            &detail::widthMemberHint_guess);
        setInternalWorkspaceLayerSize_guess(
            internal.GetDispatch(), width, height);

        ncbPropAccessor work{tTJSVariant(
            _internalSourceWorkLayer_guess =
                detail::createLayerVariant_guess(owner, target))};
        setInternalWorkspaceLayerSize_guess(work.GetDispatch(), width, height);
    }

    void Player::renderViaSharedD3DAdaptor(
        const tTJSVariant &targetVariant,
        detail::PreparedRenderItemList &mainList) {
        // Construction precedes target selection. A later SLA resolver,
        // Invalidate, dispatch call, or capture failure therefore leaves the
        // process-global adaptor published for subsequent Players.
        auto *sharedAdaptor = ensureSharedD3DAdaptor();

        // The inline native branch default-constructs this owner, then assigns
        // exactly one branch result. In particular, an SLA draw never first
        // retains the original adaptor Variant. The ordinary branch copies the
        // complete Variant pair, preserving an objthis distinct from object.
        tTJSVariant selectedTarget;
        iTJSDispatch2 *targetObject = targetVariant.AsObjectNoAddRef();
        if(auto *sla =
               ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                   targetObject, false)) {
            // The native sticky route treats an SLA specially: rotate its two
            // trees, resolve ordinal zero without a payload/sequence advance,
            // invalidate the unused retired tree, and then clear the private
            // target plus the newly active tree. selectedTarget deliberately
            // keeps an owning reference across those invalidations.
            sla->beginLayerPass_guess();
            selectedTarget = sla->resolveLayerOrdinal_guess(0);
            sla->endLayerPass_guess();
            sla->clear();
        } else {
            selectedTarget = targetVariant;
        }

        // This path stays on the TJS dispatch boundary. It does not unwrap a
        // native Layer, branch on either HRESULT, or repair an invalidated SLA
        // Layer. Width is the first argument and height the second.
        ncbPropAccessor target{selectedTarget};
        iTJSDispatch2 *targetDispatch = target.GetDispatch();
        tTJSVariant widthArg(sharedAdaptor->getWidth());
        tTJSVariant heightArg(sharedAdaptor->getHeight());
        tTJSVariant *sizeArgs[] = {&widthArg, &heightArg};
        (void)targetDispatch->FuncCall(
            0, TJS_W("setSize"), &detail::setSizeMemberHint_guess,
            nullptr, 2, sizeArgs, targetDispatch);
        (void)target.SetValue(
            TJS_W("visible"), static_cast<tjs_int>(1), TJS_MEMBERENSURE,
            &detail::visibleMemberHint_guess);

        sharedAdaptor->renderFromPlayer_guess(this, mainList);
        sharedAdaptor->captureCanvas(selectedTarget);
    }


    int Player::buildPrivateMotionGLLCommands_guess(
        iTJSDispatch2 *renderTargetObject,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        detail::PreparedRenderItemList &mainList,
        detail::PreparedRenderItemList &auxList) {
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        bool traceForPath = false;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
            traceForPath =
                detail::logoChainTraceEnabledForPath(motionPath);
        }
        if(traceForPath) {
            detail::logoChainTraceLogf(
                motionPath, "sla.renderMotionFrame",
                "buildPrivateMotionGLLCommands_guess",
                _clampedEvalTime,
                "target={} canvas={}x{}",
                static_cast<const void *>(renderTargetObject),
                canvasWidth, canvasHeight);
        }

        int stencilCount = 0;
        if(!_priorDraw) {
            stencilCount = assignD3DStencilRefs_guess(mainList);
            for(auto *itemPtr : mainList) {
                auto &item = *itemPtr;
                if(!item.drawFlag) {
                    continue;
                }
                std::array<float, 4> clip{};
                const bool hasClip = computeD3DClip_guess(
                    item, canvasWidth, canvasHeight, clip);
                if(!hasClip || item.rawFlag16) {
                    item.rawFlag21 = false;
                    continue;
                }
                item.rawFlag21 = true;
                item.clipRect = clip;
                item.leafLayer.Clear();
                if(!item.rawFlag20) {
                    item.renderLayerId = dispatchRequireLayerId(
                        &detail::nodeRequireLayerIdMemberHint_guess);
                    item.rawFlag20 = true;
                }
            }
        }
        (void)auxList;
        clearPrivateMotionGLLRenderQueue_guess(renderTargetObject);
        for(auto *itemPtr : mainList) {
            auto &item = *itemPtr;
            if(!shouldQueuePrivateMotionGLLRenderItem_guess(
                   item, _priorDraw)) {
                continue;
            }
            auto *sourceTexture =
                nativeRM()->loadRenderSourceTextureFromItem_guess(
                    *this, item);
            PrivateMotionGLLRenderItemInput_guess queueItem;
            queueItem.opacity =
                privateMotionGLLOpacity_guess(item, _priorDraw);
            queueItem.stencilMaskRef = item.stencilMaskRef;
            queueItem.stencilWriteRef = item.stencilWriteRef;
            queueItem.blendMode = item.blendMode;
            queueItem.geometryType = item.meshType;
            if(item.meshType == 1) {
                const auto cellDivisions =
                    renderBezierPatchCellDivisions_guess(
                        item.commandPatchDivision,
                        item.sourceState->width,
                        item.sourceState->height);
                queueItem.meshDivX = cellDivisions[0];
                queueItem.meshDivY = cellDivisions[1];
            } else if(item.meshType == 2) {
                queueItem.meshDivX = item.meshDivX;
                queueItem.meshDivY = item.meshDivY;
            }
            queueItem.packedColors = item.packedColors;
            queueItem.sourceRect = item.sourceState->textureRect;
            queueItem.sourceTexture = sourceTexture;
            auto *pointsToSwap =
                populatePrivateMotionGLLPoints_guess(item, queueItem);
            appendPrivateMotionGLLRenderItem_guess(renderTargetObject,
                                                   queueItem,
                                                   pointsToSwap);
        }
        if(traceForPath) {
            detail::logoChainTraceLogf(
                motionPath, "sla.renderMotionFrame.queue",
                "buildPrivateMotionGLLCommands_guess",
                _clampedEvalTime,
                "queuedItems={}",
                privateMotionGLLRenderQueueSize_guess(
                    renderTargetObject));
        }
        return stencilCount;
    }

    void Player::computeParticleOutsideRect_guess(
        const std::array<float, 4> &targetRect) {
        _particleOutsideRect = internal::computeParticleOutsideRect_guess(
            targetRect,
            _drawAffineM11, _drawAffineM12,
            _drawAffineM21, _drawAffineM22,
            _drawAffineM14, _drawAffineM24,
            _cameraOffsetX, _cameraOffsetY,
            _outsideFactor);
    }

    void Player::renderAccurateSeparateLayerAdaptor_guess(
        SeparateLayerAdaptor *sla,
        detail::PreparedRenderItemList &mainList,
        detail::PreparedRenderItemList &auxList) {
        // The target Variant is materialized before the Layer class accessor.
        // Width and height are then read through TJS in that order, with no
        // native-instance fallback or positive-size admission gate.
        ncbPropAccessor targetLayerOwner{sla->getTargetLayer()};
        iTJSDispatch2 *targetLayerObject = targetLayerOwner.GetDispatch();
        ncbPropAccessor layerClass{TJS_W("Layer")};
        iTJSDispatch2 *layerClassObject = layerClass.GetDispatch();
        const auto readTargetDimension =
            [layerClassObject, targetLayerObject](
                const tjs_char *member, tjs_uint32 *hint) {
                tTJSVariant value;
                (void)layerClassObject->PropGet(
                    0, member, hint, &value, targetLayerObject);
                return static_cast<tjs_int>(value.AsInteger());
            };
        const tjs_int canvasWidth = readTargetDimension(
            TJS_W("width"), &detail::widthMemberHint_guess);
        const tjs_int canvasHeight = readTargetDimension(
            TJS_W("height"), &detail::heightMemberHint_guess);

        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        bool traceForPath = false;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
            traceForPath =
                detail::logoChainTraceEnabledForPath(motionPath);
        }

        const std::array<float, 4> targetClip{
            0.0f, 0.0f,
            static_cast<float>(canvasWidth),
            static_cast<float>(canvasHeight)
        };
        computeParticleOutsideRect_guess(targetClip);

        // The external SLA pass starts before command construction. Its normal
        // tail clears retired Layers; an exception deliberately leaves them for
        // the next pass swap.
        sla->beginLayerPass_guess();
        buildRenderCommands(mainList, auxList, targetClip);

        int renderedItems = 0;
        for(auto *itemPtr : mainList) {
            if(!shouldRenderAccurateSlaItem_guess(*itemPtr)) {
                continue;
            }
            auto &item = *itemPtr;

            RenderClipRect clip;
            if(!computeAccurateSlaClip_guess(
                   item, static_cast<int>(canvasWidth),
                   static_cast<int>(canvasHeight), clip)) {
                continue;
            }

            const tjs_real clipWidth = static_cast<tjs_real>(
                clip.right - clip.left);
            const tjs_real clipHeight = static_cast<tjs_real>(
                clip.bottom - clip.top);
            const auto layerType =
                accurateSlaLayerType_guess(item.blendMode);
            const float offsetX = -0.5f - static_cast<float>(clip.left);
            const float offsetY = -0.5f - static_cast<float>(clip.top);

            SeparateLayerPayload_guess payload;
            payload.completionType = _completionType;
            payload.hasOutlineOrMeshline =
                _outline.Type() != tvtVoid || _meshline.Type() != tvtVoid;
            payload.commandSrc = item.commandSrc;
            payload.blendMode = item.blendMode;
            payload.packedColors = item.packedColors;
            payload.paintAndViewport = {
                item.paintBox[0], item.paintBox[1], item.paintBox[2],
                item.paintBox[3], item.viewport[0], item.viewport[1],
                item.viewport[2], item.viewport[3]
            };
            if(item.meshType == 2) {
                payload.compositeMeshPoints =
                    item.commandCompositeMeshPoints;
            } else if(item.meshType == 1) {
                payload.bezierPatchPoints =
                    item.commandBezierPatchPoints;
            }
            payload.corners = item.corners;

            bool createdOrChanged = false;
            tTJSVariant baseLayerVariant =
                sla->resolveLayerNode_guess(
                    static_cast<tjs_uint32>(item.layerId1), payload,
                    createdOrChanged);
            // A temporary Variant CopyRef obtains one Object-only raw retain,
            // then dies before any later callback. That raw owner spans the
            // rest of the item. Masked, debug and publication phases repeat
            // the same temporary-copy acquisition independently below.
            DispatchReleaseGuard_guess baseLayerObjectOwner;
            baseLayerObjectOwner.dispatch =
                retainObjectFromVariantCopy_guess(baseLayerVariant);
            auto *baseLayerObject = baseLayerObjectOwner.dispatch;

            // The shipped payload comparator currently reports refresh for
            // every comparison, but this caller-side gate is present in all
            // four references and remains observable for compatible builds.
            if(createdOrChanged) {
                tTJSVariant sourceObject =
                    nativeRM()->loadRenderSourceLayerFromItem_guess(
                        *this, item);
                auto *sourceLayerObject = sourceObject.AsObjectNoAddRef();
                const auto readSourceDimension =
                    [sourceLayerObject](const tjs_char *member,
                                        tjs_uint32 *hint) {
                        tTJSVariant value;
                        (void)sourceLayerObject->PropGet(
                            0, member, hint, &value, sourceLayerObject);
                        return static_cast<tjs_int>(value.AsInteger());
                    };
                const tjs_int sourceWidth = readSourceDimension(
                    TJS_W("width"), &detail::widthMemberHint_guess);
                const tjs_int sourceHeight = readSourceDimension(
                    TJS_W("height"), &detail::heightMemberHint_guess);

                (void)callLayerSetSizeReal_guess(
                    baseLayerObject, clipWidth, clipHeight);

                const tTVPRect sourceRect(
                    0, 0, sourceWidth, sourceHeight);
                const auto completionType =
                    static_cast<tTVPBBStretchType>(_completionType);
                if(item.meshType == 0) {
                    const auto localPts = buildAffineTrianglePoints(
                        item.corners, offsetX, offsetY);
                    (void)callLayerAffineCopy_guess(
                        baseLayerObject, localPts.data(), sourceObject,
                        sourceRect, completionType, true);
                } else if(item.meshType == 1) {
                    const auto cellDivisions =
                        renderBezierPatchCellDivisions_guess(
                            item.commandPatchDivision,
                            item.sourceState->width,
                            item.sourceState->height);
                    tTJSVariant meshArray =
                        buildMeshPointTJSArrayVariant_guess(
                            item.meshPoints, offsetX, offsetY);
                    (void)callLayerBezierPatchCopy_guess(
                        baseLayerObject, sourceObject, sourceRect, meshArray,
                        cellDivisions[0], cellDivisions[1], completionType,
                        true);
                } else if(item.meshType == 2) {
                    tTJSVariant meshArray =
                        buildMeshPointTJSArrayVariant_guess(
                            item.commandCompositeMeshPoints, offsetX, offsetY);
                    (void)callLayerMeshCopy_guess(
                        baseLayerObject, sourceObject, sourceRect, meshArray,
                        item.meshDivX, item.meshDivY, completionType, true);
                }
            }

            tTJSVariant finalLayerVariant(baseLayerVariant);
            if(!accurateSlaSkipsMaskBuffer_guess(item.blendMode) &&
               item.parentItem) {
                tTJSVariant hiddenValue(static_cast<tjs_int>(0));
                (void)baseLayerObject->PropSet(
                    TJS_MEMBERENSURE, TJS_W("visible"),
                    &detail::visibleMemberHint_guess, &hiddenValue,
                    baseLayerObject);

                finalLayerVariant = sla->resolveLayerOrdinal_guess(
                    static_cast<tjs_uint32>(item.layerId2));
                DispatchReleaseGuard_guess maskedLayerObjectOwner;
                maskedLayerObjectOwner.dispatch =
                    retainObjectFromVariantCopy_guess(finalLayerVariant);
                auto *maskedLayerObject =
                    maskedLayerObjectOwner.dispatch;

                tTJSVariant baseLayerArg(baseLayerVariant);
                tTJSVariant *assignImagesArgs[] = {&baseLayerArg};
                (void)maskedLayerObject->FuncCall(
                    0, TJS_W("assignImages"),
                    &detail::assignImagesMemberHint_guess, nullptr, 1,
                    assignImagesArgs, maskedLayerObject);
                (void)callLayerSetSizeReal_guess(
                    maskedLayerObject, clipWidth, clipHeight);

                for(auto *ancestor = item.parentItem; ancestor;
                    ancestor = ancestor->parentItem) {
                    if(ancestor->rawFlag21 && !ancestor->rawFlag16) {
                        const tTJSVariant &selectedMask =
                            (ancestor->stencilComposite & 4) != 0
                                ? ancestor->composedLayer
                                : ancestor->leafLayer;
                        applyMotionAlphaMask_guess(
                            finalLayerVariant,
                            floatToSignedIntTowardZeroSaturated_guess(
                                ancestor->clipRect[0] -
                                static_cast<float>(clip.left)),
                            floatToSignedIntTowardZeroSaturated_guess(
                                ancestor->clipRect[1] -
                                static_cast<float>(clip.top)),
                            selectedMask, 0, 0,
                            floatToSignedIntTowardZeroSaturated_guess(
                                ancestor->clipRect[2] -
                                ancestor->clipRect[0]),
                            floatToSignedIntTowardZeroSaturated_guess(
                                ancestor->clipRect[3] -
                                ancestor->clipRect[1]),
                            64, _maskMode, ancestor->stencilComposite);
                    } else if((ancestor->stencilComposite & 3) == 1) {
                        // This deliberate argc=4 dispatch is rejected by Layer;
                        // the native caller ignores the result and stops walking.
                        (void)callLayerFillRect4_guess(
                            maskedLayerObject, clipWidth, clipHeight);
                        break;
                    }
                }
            }

            if(payload.hasOutlineOrMeshline &&
               (createdOrChanged || item.parentItem)) {
                DispatchReleaseGuard_guess debugLayerObjectOwner;
                debugLayerObjectOwner.dispatch =
                    retainObjectFromVariantCopy_guess(finalLayerVariant);
                auto *debugLayerObject =
                    debugLayerObjectOwner.dispatch;
                drawRenderItemFrame_guess(
                    debugLayerObject, debugLayerObject, item,
                    _outline, _meshline, offsetX, offsetY);
            }

            {
                DispatchReleaseGuard_guess publishLayerObjectOwner;
                publishLayerObjectOwner.dispatch =
                    retainObjectFromVariantCopy_guess(finalLayerVariant);
                auto *publishLayerObject =
                    publishLayerObjectOwner.dispatch;

                tTJSVariant leftArg(
                    static_cast<tjs_real>(clip.left));
                tTJSVariant topArg(
                    static_cast<tjs_real>(clip.top));
                tTJSVariant *positionArgs[] = {&leftArg, &topArg};
                (void)publishLayerObject->FuncCall(
                    0, TJS_W("setPos"), &detail::setPosMemberHint_guess,
                    nullptr, 2, positionArgs, publishLayerObject);

                tTJSVariant typeValue(
                    static_cast<tjs_int>(layerType));
                (void)publishLayerObject->PropSet(
                    TJS_MEMBERENSURE, TJS_W("type"),
                    &detail::typeMemberHint_guess, &typeValue,
                    publishLayerObject);

                tTJSVariant visibleValue(static_cast<tjs_int>(1));
                (void)publishLayerObject->PropSet(
                    TJS_MEMBERENSURE, TJS_W("visible"),
                    &detail::visibleMemberHint_guess, &visibleValue,
                    publishLayerObject);

                tTJSVariant opacityValue(
                    static_cast<tjs_int>(item.opacity));
                (void)publishLayerObject->PropSet(
                    TJS_MEMBERENSURE, TJS_W("opacity"),
                    &detail::opacityMemberHint_guess, &opacityValue,
                    publishLayerObject);
            }
            ++renderedItems;

#if defined(KRKR2_WASMTIME_HEADLESS)
            detail::motionTraceRecordPostDrawLayerCandidate(
                this, finalLayerVariant.AsObjectNoAddRef(),
                "Player::renderAccurateSeparateLayerAdaptor_guess.item.afterCopy");
#endif
            if(traceForPath) {
                detail::logoChainTraceLogf(
                    motionPath, "sla.accurate.item",
                    "renderAccurateSeparateLayerAdaptor_guess",
                    _clampedEvalTime,
                    "nodeIndex={} layerId={} clip=[{},{},{},{}] meshType={} type={} opacity={} source={}",
                    item.nodeIndex, item.layerId1,
                    clip.left, clip.top, clip.right, clip.bottom,
                    item.meshType, static_cast<int>(layerType), item.opacity,
                    item.sourceKey);
            }
        }

        sla->endLayerPass_guess();
        if(traceForPath) {
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.rendered",
                "renderAccurateSeparateLayerAdaptor_guess",
                _clampedEvalTime,
                "targetLayer={} canvas={}x{} renderedItems={}",
                static_cast<const void *>(targetLayerObject),
                canvasWidth, canvasHeight, renderedItems);
        }
    }

    void Player::renderToD3DAdaptor(D3DAdaptor *adaptor) {
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        bool traceForPath = false;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
            traceForPath =
                detail::logoChainTraceEnabledForPath(motionPath);
        }
        if(traceForPath) {
            detail::logoChainTraceLogf(
                motionPath, "draw.d3d", "Player::renderToD3DAdaptor",
                _clampedEvalTime,
                "adaptorSize={}x{} route=D3DAdaptor_renderFromPlayer",
                adaptor->getWidth(), adaptor->getHeight());
        }

        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        if(!prepareRenderItems(mainList, auxList)) {
            return;
        }
        applyPreparedRenderItemProjection_guess(mainList);
        adaptor->renderFromPlayer_guess(this, mainList);
    }

    // Unlike the script-facing typed draw route, D3DLayer supplies the
    // compositor's current native texture and transformed origin directly; no
    // TJS Layer or D3DAdaptor is constructed.
    void Player::drawToTexture_guess(iTVPTexture2D *target, float x, float y) {
        auto *resourceManager = nativeRM();
        const ttstr motionContext =
            static_cast<ttstr>(_findMotionContextVariant);
        const auto loadedIt =
            resourceManager->_loadedModules.find(motionContext);
        if(loadedIt == resourceManager->_loadedModules.end()) {
            return;
        }
        auto *loadedResource = &loadedIt->second;

        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        if(!prepareRenderItems(mainList, auxList)) {
            return;
        }
        applyPreparedRenderItemProjection_guess(mainList);
        // The native path passes a type-erased source getter that returns the
        // persistent descriptor's current texture without the atlas retry.
        const D3DSourceTextureGetter_guess sourceTextureGetter =
            [loadedResource](detail::PreparedRenderItem &item) {
                (void)loadedResource;
                return item.sourceState->texture;
            };
        TVPGetRenderManager()->SetRenderTarget(target);
        renderPreparedItemsToD3DTexture_guess(
            target, static_cast<tjs_int>(target->GetWidth()),
            static_cast<tjs_int>(target->GetHeight()), x, y,
            mainList, sourceTextureGetter);
    }

    void D3DAdaptor::renderFromPlayer_guess(
        Player *player,
        detail::PreparedRenderItemList &mainList) {
        // The four references gate the whole texture pipeline on the adaptor's
        // canvasCaptureEnabled byte.
        if(!getCanvasCaptureEnabled()) {
            return;
        }
        player->renderPreparedItemsToD3DTexture_guess(this, mainList);
    }

    void Player::renderPreparedItemsToD3DTexture_guess(
        D3DAdaptor *adaptor,
        detail::PreparedRenderItemList &mainList) {
        auto *targetTexture = adaptor->targetTexture();

        // The adaptor owns one static software copy per generic Layer-fallback
        // texture. KRKR atlas borrows bypass that map in both the initial and
        // retry-success branches, even when the process renderer is software.
        const D3DSourceTextureGetter_guess sourceTextureGetter =
            [this, adaptor](detail::PreparedRenderItem &item) {
                return nativeRM()->loadRenderSourceTextureForItem_guess(
                    *this, *adaptor, item);
            };
        TVPGetRenderManager()->SetRenderTarget(targetTexture);
        renderPreparedItemsToD3DTexture_guess(
            targetTexture, adaptor->getWidth(), adaptor->getHeight(),
            0.5f, 0.5f, mainList,
            sourceTextureGetter);
    }

    void Player::renderPreparedItemsToD3DTexture_guess(
        iTVPTexture2D *targetTexture,
        tjs_int width,
        tjs_int height,
        float xOffset,
        float yOffset,
        detail::PreparedRenderItemList &mainList,
        const D3DSourceTextureGetter_guess &sourceTextureGetter) {
        const tTVPRect targetRect(0, 0, width, height);
        const int stencilRefs = prepareD3DRenderItems_guess(
            mainList, width, height, _priorDraw);
        const bool stencilEnabled = stencilRefs > 0;
        motion::render_backend_guess::TriangleBatch_guess batch(
            TVPGetRenderManager());
        motion::render_backend_guess::beginStencil_guess(
            targetTexture, stencilEnabled);

        if(detail::logoChainTraceEnabled()) {
            const auto motionPath = matchedMotionPath();
            if(detail::logoChainTraceEnabledForPath(motionPath)) {
                detail::logoChainTraceLogf(
                    motionPath, "draw.d3d.renderItemsToTexture",
                    "Player_renderPreparedItemsToD3DTexture_guess",
                    _clampedEvalTime,
                    "target={} targetRect=[0,0,{},{}] items={} priorDraw={} stencilRefs={}",
                    static_cast<const void *>(targetTexture),
                    width, height, mainList.size(),
                    _priorDraw ? 1 : 0, stencilRefs);
            }
        }

        for(auto *itemPtr : mainList) {
            auto &item = *itemPtr;
            if(shouldSkipD3DRenderItem_guess(item, _priorDraw)) {
                continue;
            }
            if(item.sourceState->blank) {
                continue;
            }

            int opacity = item.opacity;
            if(_priorDraw) {
                opacity /= 2;
            }
            if(opacity <= 0 && item.stencilMaskRef == 0) {
                continue;
            }

            auto *sourceTexture = sourceTextureGetter(item);
            // The descriptor is reread only after its texture callback
            // returns, so render-time atlas writes are visible immediately.
            const auto sourceRect = item.sourceState->textureRect;
            if(sourceRect[2] <= sourceRect[0] ||
               sourceRect[3] <= sourceRect[1]) {
                continue;
            }

            const auto packedColor =
                d3dPackedColorWithOpacity(item, opacity);
            batch.setStencilState_guess(
                item.stencilMaskRef, item.stencilWriteRef);
            // All four native shared renderers pass literal true here. The
            // D3DAdaptor alphaOpAdd property is stored for script readback but
            // is not propagated into this method-selection key.
            auto *method = batch.selectMethod_guess(
                item.blendMode & 0xF,
                packedColor,
                true,
                item.stencilMaskRef != 0);
            if(!method) {
                continue;
            }

            if(item.meshType == 0) {
                appendD3DAffine_guess(
                    batch, method, targetTexture, targetRect, item,
                    sourceTexture, sourceRect, xOffset, yOffset,
                    packedColor);
            } else if(item.meshType == 1) {
                const auto cellDivisions =
                    renderBezierPatchCellDivisions_guess(
                        item.commandPatchDivision,
                        item.sourceState->width,
                        item.sourceState->height);
                const auto boundsPoints = buildOffsetMeshPoints(
                    item.meshPoints, xOffset, yOffset);
                const auto meshPoints = motion::render_backend_guess::
                    tessellateBezierPatch_guess(
                        boundsPoints, cellDivisions[0], cellDivisions[1]);
                appendD3DMesh_guess(
                    batch, method, targetTexture, targetRect,
                    sourceTexture, sourceRect, boundsPoints, meshPoints,
                    cellDivisions[0], cellDivisions[1], packedColor);
            } else if(item.meshType == 2) {
                const auto meshPoints =
                    buildOffsetMeshPoints(item.commandCompositeMeshPoints,
                                          xOffset, yOffset);
                appendD3DMesh_guess(
                    batch, method, targetTexture, targetRect,
                    sourceTexture, sourceRect, meshPoints, meshPoints,
                    item.meshDivX, item.meshDivY, packedColor);
            }
        }

        batch.flush_guess();
        motion::render_backend_guess::endStencil_guess(stencilEnabled);
    }

    void Player::renderToCanvas_guess(
        tTJSVariant target,
        detail::PreparedRenderItemList &mainList,
        detail::PreparedRenderItemList &auxList) {
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
        }
        const bool traceForPath =
            logoTraceEnabled &&
            detail::logoChainTraceEnabledForPath(motionPath);

        // One ncbPropAccessor for the global Layer class is constructed before
        // coercing the target. It owns the dispatch across target-size reads,
        // the submit loop and final setClip(argc=0), and dies after the target
        // object owner.
        ncbPropAccessor layerClass{TJS_W("Layer")};
        iTJSDispatch2 *layerClassObject = layerClass.GetDispatch();

        // Convert the supplied Variant directly to one raw object owner. The
        // native canvas path does not probe NativeInstanceSupport first.
        ncbPropAccessor renderTargetOwner{target};
        iTJSDispatch2 *resolvedLayerObject = renderTargetOwner.GetDispatch();

        // Non-priorDraw rendering clears the complex region before collecting
        // this frame's submitted paint boxes. Player.clear has already consumed
        // the previous frame's bound before this draw begins.
        if(!_priorDraw) {
            _drawRegion.Clear();
        }

        // Query through the class dispatch with target as objthis: width
        // strictly precedes height, and there is no positive-size gate here.
        const int canvasWidth = callLayerPropGetInt_guess(
            layerClassObject, resolvedLayerObject, TJS_W("width"),
            &detail::widthMemberHint_guess);
        const int canvasHeight = callLayerPropGetInt_guess(
            layerClassObject, resolvedLayerObject, TJS_W("height"),
            &detail::heightMemberHint_guess);

        if(traceForPath) {
            detail::logoChainTraceLogf(
                motionPath, "draw.renderToCanvas",
                "Player.renderToCanvas", _clampedEvalTime,
                "targetLayerCanvas={}x{} needsInternalAssignImages={} route=callerTarget",
                canvasWidth, canvasHeight,
                _needsInternalAssignImages ? 1 : 0);
        }

        iTJSDispatch2 *renderLayerObject = resolvedLayerObject;

        // Build leaf/composed state only when priorDraw is false. Prior-draw
        // submission consumes the retained item state directly.
        if(!_priorDraw) {
            buildRenderCommands(
                mainList, auxList,
                {0.0f, 0.0f,
                 static_cast<float>(canvasWidth),
                 static_cast<float>(canvasHeight)});
        }
        executeLayerRenderCommands(
            layerClassObject, renderLayerObject, canvasWidth, canvasHeight,
            true, mainList);
        if(traceForPath) {
            detail::logoChainTraceSummary(
                motionPath, "Player.renderToCanvas", _clampedEvalTime,
                "callerTarget=1");
        }
    }

    void Player::renderToSeparateLayerAdaptor(SeparateLayerAdaptor *sla) {
        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        if(!prepareRenderItems(mainList, auxList)) {
            return;
        }
        applyPreparedRenderItemProjection_guess(mainList);

        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        bool traceForPath = false;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
            traceForPath =
                detail::logoChainTraceEnabledForPath(motionPath);
        }
        const bool accurateSla = isAccurateSlaRenderEnabled();

        if(accurateSla) {
            iTJSDispatch2 *diagnosticTargetLayerObject = nullptr;
            if(traceForPath) {
                diagnosticTargetLayerObject =
                    tryResolveLayerDispatch(sla->getTargetLayer());
            }
#if defined(KRKR2_WASMTIME_HEADLESS)
            if(!diagnosticTargetLayerObject) {
                diagnosticTargetLayerObject =
                    tryResolveLayerDispatch(sla->getTargetLayer());
            }
            struct AccurateSlaRenderTraceScope {
                Player *player = nullptr;
                iTJSDispatch2 *target = nullptr;
                AccurateSlaRenderTraceScope(Player *p, iTJSDispatch2 *t)
                    : player(p), target(t) {
                    detail::motionTraceBeginAccurateSlaRender(player, target);
                }
                ~AccurateSlaRenderTraceScope() {
                    detail::motionTraceEndAccurateSlaRender(player, target);
                }
            } accurateSlaRenderTrace{
                this, diagnosticTargetLayerObject};
            detail::MotionTraceRenderExecuteScope renderTrace(
                this, diagnosticTargetLayerObject, false, mainList);
#endif
            renderAccurateSeparateLayerAdaptor_guess(
                sla, mainList, auxList);
            if(traceForPath) {
                detail::logoChainTraceLogf(
                    motionPath, "sla.accurate.begin", "renderAccurateSla",
                    _clampedEvalTime, "target={}",
                    static_cast<const void *>(diagnosticTargetLayerObject));
            }
            updateAccurateSLAAfterDraw(sla->getTargetLayer());
            if(traceForPath) {
                detail::logoChainTraceLogf(
                    motionPath, "sla.accurate.end",
                    "updateAccurateSLAAfterDraw",
                    _clampedEvalTime, "target={}",
                    static_cast<const void *>(diagnosticTargetLayerObject));
            }
#if defined(KRKR2_WASMTIME_HEADLESS)
            renderTrace.setResult(true);
#endif
        } else {
            iTJSDispatch2 *renderTarget =
                ensurePrivateMotionGLL_guess(*sla);
            auto *renderLayer =
                resolvePrivateMotionGLLNative_guess(renderTarget);
            const tjs_int canvasWidth =
                static_cast<tjs_int>(renderLayer->GetWidth());
            const tjs_int canvasHeight =
                static_cast<tjs_int>(renderLayer->GetHeight());
            const int stencilCount = buildPrivateMotionGLLCommands_guess(
                renderTarget, canvasWidth, canvasHeight,
                mainList, auxList);
            setPrivateMotionGLLStencilCount_guess(
                renderTarget, stencilCount);
            if(!TVPWindowUpdateEventsDelivering) {
                renderLayer->Update(false);
            }
            if(traceForPath) {
                detail::logoChainTraceLogf(
                    motionPath, "sla.updateRect",
                    "privateMotionGLL.Update", _clampedEvalTime,
                    "renderTarget={} size={}x{} stencilCount={} update={}",
                    static_cast<const void *>(renderTarget),
                    canvasWidth, canvasHeight, stencilCount,
                    TVPWindowUpdateEventsDelivering ? 0 : 1);
            }
        }

        if(traceForPath) {
            detail::logoChainTraceSummary(
                motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
                accurateSla ? "accurate=1" : "accurate=0");
        }
    }

    void Player::updateLayerAfterDrawRecovered_guess(
        const tTJSVariant &target) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        iTJSDispatch2 *rawProbeLayerObject =
            tryResolveLayerDispatch(target);
        if(!rawProbeLayerObject && target.Type() == tvtObject) {
            rawProbeLayerObject = target.AsObjectNoAddRef();
        }
        detail::motionTraceRenderImageCheckpoint(
            this, rawProbeLayerObject, "updateLayerAfterDraw_pre",
            "Player::updateLayerAfterDraw.enter.after-target-resolve");
        detail::motionTraceLayerRawProbe(
            this, rawProbeLayerObject, "updateLayerAfterDraw.enter");
        struct UpdateLayerAfterDrawTraceLeave {
            Player *player;
            iTJSDispatch2 *layerObject;
            ~UpdateLayerAfterDrawTraceLeave() {
                detail::motionTraceRenderImageCheckpoint(
                    player, layerObject, "updateLayerAfterDraw_post",
                    "Player::updateLayerAfterDraw.leave.before-return");
                detail::motionTraceLayerRawProbe(
                    player, layerObject,
                    "updateLayerAfterDraw.leave");
            }
        } updateLayerAfterDrawTraceLeave{this, rawProbeLayerObject};
#endif
        // All four references first snapshot the producer flag, even when it
        // is clear. Anchor type 10 reads this on the next frame to gate use of
        // the internal render Layer.
        _internalRenderLayerReady = _needsInternalAssignImages;
        if(!_needsInternalAssignImages) {
            return;
        }
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
        }
        const bool traceForPath =
            logoTraceEnabled &&
            detail::logoChainTraceEnabledForPath(motionPath);

        materializeInternalRenderLayers_guess(target);

        ncbPropAccessor internal{tTJSVariant(_internalRenderLayer)};
        (void)internal.FuncCall(
            0, TJS_W("assignImages"),
            &detail::assignImagesMemberHint_guess, nullptr, target);
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRecordPostDrawLayerCandidate(
            this, internal.GetDispatch(),
            "Player::updateLayerAfterDraw.afterAssignImages");
#endif
        if(traceForPath) {
            detail::logoChainTraceCheck(
                motionPath, "post.assignImages",
                "Player.updateLayerAfterDraw", _clampedEvalTime,
                "materialize internal/work Layers, then internal.assignImages(original target)",
                "assignImages(target)", true,
                "native internal Layer snapshot dispatched");
        }
    }

    void Player::updateAccurateSLAAfterDraw(const tTJSVariant &target) {
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        std::string motionPath;
        if(logoTraceEnabled) {
            motionPath = matchedMotionPath();
        }
        const bool traceForPath =
            logoTraceEnabled &&
            detail::logoChainTraceEnabledForPath(motionPath);

        // The accurate-SLA counterpart likewise snapshots the producer flag
        // unconditionally and leaves the producer untouched.
        _internalRenderLayerReady = _needsInternalAssignImages;
        if(!_needsInternalAssignImages) {
            if(traceForPath) {
                detail::logoChainTraceLogf(
                    motionPath, "post.sla.accurate",
                    "Player.updateAccurateSLAAfterDraw",
                    _clampedEvalTime, "needsInternalAssignImages=0");
            }
            return;
        }

        ncbPropAccessor targetAccessor{tTJSVariant(target)};
        materializeInternalRenderLayers_guess(target);
        ncbPropAccessor internal{tTJSVariant(_internalRenderLayer)};

        const tjs_int height = getInternalWorkspaceDimension_guess(
            targetAccessor, TJS_W("height"),
            &detail::heightMemberHint_guess);
        const tjs_int width = getInternalWorkspaceDimension_guess(
            targetAccessor, TJS_W("width"),
            &detail::widthMemberHint_guess);
        (void)internal.FuncCall(
            0, TJS_W("piledCopy"), &detail::piledCopyMemberHint_guess,
            nullptr, tTJSVariant(0), tTJSVariant(0), target,
            tTJSVariant(0), tTJSVariant(0), tTJSVariant(width),
            tTJSVariant(height));
        if(traceForPath) {
            detail::logoChainTraceCheck(
                motionPath, "post.sla.accurate",
                "Player.updateAccurateSLAAfterDraw", _clampedEvalTime,
                fmt::format("internal.piledCopy(0,0,target,0,0,{},{})",
                            width, height),
                "piledCopy", true,
                "native accurate-SLA post-copy dispatched");
        }
    }

} // namespace motion
