// PlayerRenderTargets.cpp — Layer/SLA/D3D render targets and post-draw update
// Split from PlayerRender.cpp for maintainability.
//
#include "PlayerRenderInternal.h"
#include "MotionTraceWeb.h"
#include "PrivateMotionGLL.h"
#include "RenderManager.h"
#include "SourceCache.h"
#include "ncbind.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <GLES2/gl2.h>
#elif !defined(KRKR2_WASMTIME_HEADLESS)
#include "ogl/ogl_common.h"
#endif

using namespace motion::internal;
using namespace motion::internal::render_detail;

namespace motion {
    namespace {
        using PreparedRenderItem = detail::PreparedRenderItem;

        std::array<tTVPPointD, 6> makeTextureQuad(
            const std::array<int, 4> &rect) {
            const double l = rect[0], t = rect[1];
            const double r = rect[2], b = rect[3];
            return {{
                {l, t}, {r, t}, {l, b}, {r, t}, {l, b}, {r, b},
            }};
        }

        std::array<int, 4> sourceRectForItem(
            const PreparedRenderItem &item,
            const iTVPTexture2D *texture) {
            if(item.sourceState) {
                return item.sourceState->textureRect;
            }
            if(item.sourceTexture && item.sourceRect[2] > item.sourceRect[0] &&
               item.sourceRect[3] > item.sourceRect[1]) {
                return item.sourceRect;
            }
            return {0, 0, static_cast<int>(texture->GetWidth()),
                    static_cast<int>(texture->GetHeight())};
        }

        std::array<tTVPPointD, 6> makeAffineTargetQuad(
            const PreparedRenderItem &item,
            double xOffset,
            double yOffset) {
            return {{
                {item.corners[0] + xOffset, item.corners[1] + yOffset},
                {item.corners[2] + xOffset, item.corners[3] + yOffset},
                {item.corners[6] + xOffset, item.corners[7] + yOffset},
                {item.corners[2] + xOffset, item.corners[3] + yOffset},
                {item.corners[6] + xOffset, item.corners[7] + yOffset},
                {item.corners[4] + xOffset, item.corners[5] + yOffset},
            }};
        }

        std::vector<tTVPPointD> tessellateBezierPatch(
            const std::vector<detail::MeshPoint> &controlPoints,
            int divx,
            int divy,
            double xOffset,
            double yOffset) {
            std::vector<tTVPPointD> out;
            if(controlPoints.size() < 16u || divx < 1 || divy < 1) {
                return out;
            }
            const auto cubicBlend = [](double p0, double p1, double p2,
                                       double p3, double t) {
                const double mt = 1.0 - t;
                return mt * mt * mt * p0 + 3.0 * mt * mt * t * p1 +
                    3.0 * mt * t * t * p2 + t * t * t * p3;
            };
            const auto samplePatch = [&](double u, double v) {
                tTVPPointD curve[4];
                for(int row = 0; row < 4; ++row) {
                    const size_t base = static_cast<size_t>(row) * 4u;
                    curve[row].x = cubicBlend(
                        controlPoints[base + 0].x, controlPoints[base + 1].x,
                        controlPoints[base + 2].x, controlPoints[base + 3].x, u);
                    curve[row].y = cubicBlend(
                        controlPoints[base + 0].y, controlPoints[base + 1].y,
                        controlPoints[base + 2].y, controlPoints[base + 3].y, u);
                }
                return tTVPPointD{
                    cubicBlend(curve[0].x, curve[1].x, curve[2].x,
                               curve[3].x, v) + xOffset,
                    cubicBlend(curve[0].y, curve[1].y, curve[2].y,
                               curve[3].y, v) + yOffset,
                };
            };
            out.reserve(static_cast<size_t>(divx + 1) *
                        static_cast<size_t>(divy + 1));
            for(int y = 0; y <= divy; ++y) {
                const double v =
                    static_cast<double>(y) / static_cast<double>(divy);
                for(int x = 0; x <= divx; ++x) {
                    const double u =
                        static_cast<double>(x) / static_cast<double>(divx);
                    out.push_back(samplePatch(u, v));
                }
            }
            return out;
        }

        std::vector<tTVPPointD> buildOffsetMeshPoints(
            const std::vector<detail::MeshPoint> &points,
            double xOffset,
            double yOffset) {
            std::vector<tTVPPointD> out;
            out.reserve(points.size());
            for(const auto &point : points) {
                out.push_back({point.x + xOffset, point.y + yOffset});
            }
            return out;
        }

        bool shouldQueuePrivateMotionGLLRenderItemLike_0x6DE738(
            const PreparedRenderItem &item,
            bool priorDraw) {
            if((item.blendMode & 0xF) == 6 || item.skipFlag0 ||
               item.rawFlag16 || item.opacity == 0) {
                return false;
            }
            if(priorDraw && !item.skipFlag1) {
                return false;
            }
            return !item.sourceKey.empty();
        }

        int privateMotionGLLOpacityLike_0x6DE738(
            const PreparedRenderItem &item,
            bool priorDraw) {
            int opacity = item.opacity;
            if(priorDraw) {
                opacity = opacity >= 0 ? opacity / 2 : (opacity + 1) / 2;
            }
            return opacity;
        }

        std::vector<detail::MeshPoint> *
        populatePrivateMotionGLLPointsLike_0x6DE738(
            PreparedRenderItem &item,
            PrivateMotionGLLRenderItemInputLike_0x6DE738 &queueItem) {
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

        tTVPBBBltMethod softwareMethodForD3DBlend(int blendLowNibble) {
            switch(blendLowNibble) {
                case 1: return bmPsAdditive;
                case 2:
                case 5: return bmPsSubtractive;
                case 3: return bmPsMultiplicative;
                case 4: return bmPsScreen;
                default: return bmAlpha;
            }
        }

        tTVPLayerType accurateSlaLayerTypeLike_0x6C9CA8(int rawBlendMode) {
            switch(rawBlendMode & 0x0F) {
                case 1: return ltPsAdditive;
                case 2:
                case 5: return ltPsSubtractive;
                case 3: return ltPsMultiplicative;
                case 4: return ltPsScreen;
                default: return ltAlpha;
            }
        }

        bool shouldRenderAccurateSlaItemLike_0x6C9CA8(
            const PreparedRenderItem &item) {
            return !item.skipFlag0 && !item.rawFlag16 && item.opacity != 0 &&
                !item.sourceKey.empty();
        }

        bool computeAccurateSlaClipLike_0x6C9CA8(
            const PreparedRenderItem &item,
            int canvasWidth,
            int canvasHeight,
            RenderClipRect &out) {
            float clipLeft = std::max(item.paintBox[0], 0.0f);
            float clipTop = std::max(item.paintBox[1], 0.0f);
            float clipRight = std::min(item.paintBox[2],
                                       static_cast<float>(canvasWidth));
            float clipBottom = std::min(item.paintBox[3],
                                        static_cast<float>(canvasHeight));

            if(item.hasViewport && item.viewport[2] >= item.viewport[0] &&
               item.viewport[3] >= item.viewport[1]) {
                clipLeft = std::max(
                    clipLeft, static_cast<float>(std::floor(item.viewport[0])));
                clipTop = std::max(
                    clipTop, static_cast<float>(std::floor(item.viewport[1])));
                clipRight = std::min(
                    clipRight, static_cast<float>(std::ceil(item.viewport[2])));
                clipBottom = std::min(
                    clipBottom, static_cast<float>(std::ceil(item.viewport[3])));
            }

            if(!(clipLeft < clipRight && clipTop < clipBottom)) {
                return false;
            }

            out.left = static_cast<int>(clipLeft);
            out.top = static_cast<int>(clipTop);
            out.right = static_cast<int>(clipRight);
            out.bottom = static_cast<int>(clipBottom);
            return out.left < out.right && out.top < out.bottom;
        }

        bool trySetAccurateSlaLayerSize(iTJSDispatch2 *layerObject,
                                        int width,
                                        int height) {
            if(!layerObject) {
                return false;
            }
            tTJSVariant widthArg(width);
            tTJSVariant heightArg(height);
            tTJSVariant *args[] = { &widthArg, &heightArg };
            return TJS_SUCCEEDED(layerObject->FuncCall(
                0, TJS_W("setSize"), &detail::setSizeMemberHint_guess,
                nullptr, 2, args, layerObject));
        }

        void setLayerSizeLike_0x6CE19C(iTJSDispatch2 *layerObject,
                                       int width,
                                       int height) {
            tTJSVariant widthArg(width);
            tTJSVariant heightArg(height);
            tTJSVariant *args[] = { &widthArg, &heightArg };
            (void)layerObject->FuncCall(
                0, TJS_W("setSize"), &detail::setSizeMemberHint_guess,
                nullptr, 2, args, layerObject);
        }

        tTJSVariant createLayerVariantLike_0x6CE19C(
            const tTJSVariant &owner,
            const tTJSVariant &parent) {
            iTJSDispatch2 *global = TVPGetScriptDispatch();
            iTJSDispatch2 *created = nullptr;
            tTJSVariant *args[] = {
                const_cast<tTJSVariant *>(&owner),
                const_cast<tTJSVariant *>(&parent)
            };
            (void)global->CreateNew(
                0, TJS_W("Layer"), &detail::layerClassMemberHint_guess,
                &created, 2, args, global);
            tTJSVariant createdVariant(created, created);
            created->Release();
            global->Release();
            return createdVariant;
        }

        tjs_int propGetIntAfterProbeLike_0x6CE19C(
            iTJSDispatch2 *object,
            const tjs_char *member,
            tjs_uint32 *hint) {
            {
                tTJSVariant probe;
                if(TJS_FAILED(object->PropGet(
                       TJS_MEMBERMUSTEXIST, member, hint, &probe, object))) {
                    return 0;
                }
            }
            tTJSVariant value;
            (void)object->PropGet(0, member, hint, &value, object);
            return static_cast<tjs_int>(value.AsInteger());
        }

        iTJSDispatch2 *ensureAccurateSlaStateLayerLike_0x6C6B48(
            tTJSVariant &slot,
            iTJSDispatch2 *layerTreeOwnerObject,
            iTJSDispatch2 *parentLayerObject,
            tTVPLayerType layerType) {
            if(!parentLayerObject && layerTreeOwnerObject) {
                parentLayerObject =
                    resolvePrimaryLayerObject(layerTreeOwnerObject);
            }

            iTJSDispatch2 *layerObject =
                slot.Type() == tvtObject ? slot.AsObjectNoAddRef() : nullptr;
            if(!layerObject) {
                if(!layerTreeOwnerObject) {
                    return nullptr;
                }
                layerObject =
                    createLayerObject(layerTreeOwnerObject, parentLayerObject);
                if(!layerObject) {
                    return nullptr;
                }
                slot = tTJSVariant(layerObject, layerObject);
                layerObject->Release();
                layerObject = slot.AsObjectNoAddRef();
            }

            auto *layer = resolveNativeLayer(layerObject);
            if(!layer) {
                return nullptr;
            }
            if(parentLayerObject) {
                if(auto *parentLayer = resolveNativeLayer(parentLayerObject);
                   parentLayer && layer->GetParent() != parentLayer) {
                    layer->SetParent(parentLayer);
                }
            }
            layer->SetType(layerType);
            layer->SetAbsoluteOrderMode(false);
            return layerObject;
        }

        const char *gpuMethodNameForD3DBlend(int blendLowNibble,
                                             bool alphaOpAdd,
                                             bool alphaTest) {
            switch(blendLowNibble) {
                case 1:
                    return alphaTest ? "PsAddBlend_color_AlphaTest"
                                     : "PsAddBlend_color";
                case 2:
                case 5:
                    return alphaTest ? "PsSubBlend_color_AlphaTest"
                                     : "PsSubBlend_color";
                case 3:
                    return alphaTest ? "PsMulBlend_color_AlphaTest"
                                     : "PsMulBlend_color";
                case 4:
                    return alphaTest ? "PsScreenBlend_color_AlphaTest"
                                     : "PsScreenBlend_color";
                default:
                    if(alphaOpAdd) {
                        return alphaTest ? "AlphaBlend_color_a_AlphaTest"
                                         : "AlphaBlend_color_a";
                    }
                    return alphaTest ? "AlphaBlend_color_AlphaTest"
                                     : "AlphaBlend_color";
            }
        }

        iTVPRenderMethod *selectD3DRenderMethod(int blendLowNibble,
                                                unsigned int color,
                                                bool alphaOpAdd,
                                                bool alphaTest,
                                                int opacity) {
            auto *mgr = TVPGetRenderManager();
            if(mgr->IsSoftware()) {
                return mgr->GetRenderMethod(
                    opacity, false, softwareMethodForD3DBlend(blendLowNibble));
            }

            auto *method = mgr->GetRenderMethod(
                gpuMethodNameForD3DBlend(blendLowNibble, alphaOpAdd,
                                         alphaTest));
            if(!method) {
                return nullptr;
            }
            const int colorId = method->EnumParameterID("color");
            method->SetParameterColor4B(colorId, color);
            if(alphaTest) {
                const int thresholdId = method->EnumParameterID("alpha_threshold");
                method->SetParameterOpa(thresholdId, 64);
            }
            return method;
        }

        bool markStencilMaskChainLike_0x6ADFBC(PreparedRenderItem *item,
                                               std::uint8_t ref) {
            if(!item) {
                return false;
            }
            bool hasDrawableMaskTarget = false;
            for(auto *ancestor = item; ancestor; ancestor = ancestor->parentItem) {
                ancestor->stencilMaskRef = ref;
                if(!ancestor->rawFlag16 && !ancestor->skipFlag0) {
                    hasDrawableMaskTarget = true;
                }
                for(auto *child : ancestor->childItems) {
                    if(!child || child == ancestor) {
                        continue;
                    }
                    child->stencilMaskRef = ref;
                    if(!child->rawFlag16 && !child->skipFlag0 &&
                       child->opacity != 0) {
                        hasDrawableMaskTarget = true;
                    }
                }
            }
            return hasDrawableMaskTarget;
        }

        int assignStencilRefsLike_0x6ADFBC(
            std::vector<PreparedRenderItem *> &items) {
            for(auto *itemPtr : items) {
                if(!itemPtr) {
                    continue;
                }
                auto &item = *itemPtr;
                item.stencilMaskRef = 0;
                item.stencilWriteRef = 0;
            }

            int stencilCount = 0;
            static bool stencilOverflowLogged = false;
            for(auto *itemPtr : items) {
                if(!itemPtr) {
                    continue;
                }
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
                if(!markStencilMaskChainLike_0x6ADFBC(item.parentItem, ref)) {
                    item.stencilWriteRef = 0;
                }
            }
            return stencilCount;
        }

        bool computeD3DClipLike_0x6ADFBC(
            const PreparedRenderItem &item,
            int canvasWidth,
            int canvasHeight,
            std::array<float, 4> &out) {
            float clipLeft = std::max(item.paintBox[0], 0.0f);
            float clipTop = std::max(item.paintBox[1], 0.0f);
            float clipRight = std::min(
                item.paintBox[2], static_cast<float>(canvasWidth));
            float clipBottom = std::min(
                item.paintBox[3], static_cast<float>(canvasHeight));

            if(item.viewport[2] >= item.viewport[0] &&
               item.viewport[3] >= item.viewport[1]) {
                clipLeft = std::max(clipLeft, std::floor(item.viewport[0]));
                clipTop = std::max(clipTop, std::floor(item.viewport[1]));
                clipRight = std::min(clipRight, std::ceil(item.viewport[2]));
                clipBottom = std::min(clipBottom, std::ceil(item.viewport[3]));
            }

            out = {clipLeft, clipTop, clipRight, clipBottom};
            return clipLeft < clipRight && clipTop < clipBottom;
        }

        int prepareD3DRenderItemsLike_0x6ADFBC(
            std::vector<PreparedRenderItem *> &items,
            int width,
            int height,
            bool priorDraw) {
            if(priorDraw) {
                return 0;
            }

            const int stencilCount = assignStencilRefsLike_0x6ADFBC(items);
            for(auto *item : items) {
                if(!item || !item->drawFlag) {
                    continue;
                }
                std::array<float, 4> clip{};
                const bool hasClip = computeD3DClipLike_0x6ADFBC(
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

        void beginD3DStencilIfNeeded(iTVPTexture2D *target, bool enabled) {
            if(!enabled) {
                return;
            }
            auto *mgr = TVPGetRenderManager();
            mgr->SetRenderTarget(target);
            mgr->BeginStencil(target);
#if !defined(KRKR2_WASMTIME_HEADLESS)
            if(!mgr->IsSoftware()) {
                glDisable(GL_DEPTH_TEST);
                glStencilMask(255);
                glClearStencil(0);
                glClear(GL_STENCIL_BUFFER_BIT);
                glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);
                glDepthMask(GL_FALSE);
                glDisable(GL_STENCIL_TEST);
            }
#endif
        }

        void applyD3DStencilState(const PreparedRenderItem &item,
                                  bool enabled) {
            if(!enabled) {
                return;
            }
#if !defined(KRKR2_WASMTIME_HEADLESS)
            auto *mgr = TVPGetRenderManager();
            if(mgr->IsSoftware()) {
                return;
            }
            const auto maskRef = item.stencilMaskRef;
            const auto writeRef = item.stencilWriteRef;
            if(writeRef) {
                glEnable(GL_STENCIL_TEST);
                glStencilFunc(GL_LEQUAL, writeRef, 255);
                if(maskRef) {
                    glStencilMask(maskRef);
                    glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);
                } else {
                    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                }
            } else if(maskRef) {
                glEnable(GL_STENCIL_TEST);
                glStencilMask(maskRef);
                glStencilFunc(GL_ALWAYS, maskRef, 255);
                glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
            } else {
                glDisable(GL_STENCIL_TEST);
            }
#endif
        }

        void endD3DStencilIfNeeded(bool enabled) {
            if(!enabled) {
                return;
            }
#if !defined(KRKR2_WASMTIME_HEADLESS)
            if(!TVPGetRenderManager()->IsSoftware()) {
                glDepthMask(GL_TRUE);
            }
#endif
            TVPGetRenderManager()->EndStencil();
        }

        bool operateD3DAffine(iTVPRenderMethod *method,
                              iTVPTexture2D *target,
                              const tTVPRect &targetRect,
                              const PreparedRenderItem &item,
                              iTVPTexture2D *sourceTexture,
                              const std::array<int, 4> &sourceRect,
                              double xOffset,
                              double yOffset) {
            auto dst = makeAffineTargetQuad(
                item, xOffset + 0.5, yOffset + 0.5);
            auto src = makeTextureQuad(sourceRect);
            tRenderTexQuadArray::Element srcTex[] = {
                tRenderTexQuadArray::Element(sourceTexture, src.data())
            };
            TVPGetRenderManager()->OperateTriangles(
                method, 2, target, target, targetRect, dst.data(),
                tRenderTexQuadArray(srcTex));
            return true;
        }

        bool operateD3DMesh(iTVPRenderMethod *method,
                            iTVPTexture2D *target,
                            const tTVPRect &targetRect,
                            iTVPTexture2D *sourceTexture,
                            const std::array<int, 4> &sourceRect,
                            const std::vector<tTVPPointD> &meshPoints,
                            int meshDivX,
                            int meshDivY) {
            const auto pointColumns = static_cast<size_t>(meshDivX + 1);
            const auto pointRows = static_cast<size_t>(meshDivY + 1);
            if(meshDivX < 1 || meshDivY < 1 ||
               meshPoints.size() <
                   pointColumns * pointRows) {
                return false;
            }

            const double srcL = sourceRect[0];
            const double srcT = sourceRect[1];
            const double srcW = sourceRect[2] - sourceRect[0];
            const double srcH = sourceRect[3] - sourceRect[1];
            for(int y = 0; y < meshDivY; ++y) {
                const double v0 =
                    static_cast<double>(y) / static_cast<double>(meshDivY);
                const double v1 = static_cast<double>(y + 1) /
                    static_cast<double>(meshDivY);
                for(int x = 0; x < meshDivX; ++x) {
                    const double u0 =
                        static_cast<double>(x) / static_cast<double>(meshDivX);
                    const double u1 = static_cast<double>(x + 1) /
                        static_cast<double>(meshDivX);
                    const auto row = static_cast<size_t>(y) * pointColumns;
                    const auto nextRow = row + pointColumns;
                    const auto column = static_cast<size_t>(x);
                    const auto &p0 = meshPoints[row + column];
                    const auto &p1 = meshPoints[row + column + 1];
                    const auto &p2 = meshPoints[nextRow + column];
                    const auto &p3 = meshPoints[nextRow + column + 1];
                    std::array<tTVPPointD, 6> dst{{
                        p0, p1, p2, p1, p2, p3,
                    }};
                    std::array<tTVPPointD, 6> src{{
                        {srcL + std::floor(srcW * u0), srcT + std::floor(srcH * v0)},
                        {srcL + std::ceil(srcW * u1), srcT + std::floor(srcH * v0)},
                        {srcL + std::floor(srcW * u0), srcT + std::ceil(srcH * v1)},
                        {srcL + std::ceil(srcW * u1), srcT + std::floor(srcH * v0)},
                        {srcL + std::floor(srcW * u0), srcT + std::ceil(srcH * v1)},
                        {srcL + std::ceil(srcW * u1), srcT + std::ceil(srcH * v1)},
                    }};
                    tRenderTexQuadArray::Element srcTex[] = {
                        tRenderTexQuadArray::Element(sourceTexture, src.data())
                    };
                    TVPGetRenderManager()->OperateTriangles(
                        method, 2, target, target, targetRect, dst.data(),
                        tRenderTexQuadArray(srcTex));
                }
            }
            return true;
        }

        bool shouldSkipD3DRenderItemLike_0x6ADFBC(
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

    void Player::materializeInternalRenderLayersLike_0x6CE19C_guess(
        const tTJSVariant &target) {
        // Player_materializeRenderLayer_guess @0x6CE19C gates only on the
        // primary internal Layer Variant. Once it exists, the work Layer is
        // never repaired or resized independently.
        if(_internalRenderLayer.Type() != tvtVoid) {
            return;
        }

        ncbPropAccessor targetAccessor{tTJSVariant(target)};
        tTJSVariant owner = targetAccessor.GetValue(
            TJS_W("window"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::windowMemberHint_guess);

        ncbPropAccessor internal{tTJSVariant(
            _internalRenderLayer =
                createLayerVariantLike_0x6CE19C(owner, target))};

        const tjs_int height = propGetIntAfterProbeLike_0x6CE19C(
            targetAccessor.GetDispatch(), TJS_W("height"),
            &detail::heightMemberHint_guess);
        const tjs_int width = propGetIntAfterProbeLike_0x6CE19C(
            targetAccessor.GetDispatch(), TJS_W("width"),
            &detail::widthMemberHint_guess);
        setLayerSizeLike_0x6CE19C(internal.GetDispatch(), width, height);

        ncbPropAccessor work{tTJSVariant(
            _internalSourceWorkLayer_guess =
                createLayerVariantLike_0x6CE19C(owner, target))};
        setLayerSizeLike_0x6CE19C(work.GetDispatch(), width, height);
    }

    bool Player::renderViaSharedD3DAdaptor(
        iTJSDispatch2 *targetLayerObject,
        detail::PreparedRenderItemList &mainList) {
        if(!targetLayerObject) {
            return false;
        }

        auto *resolvedTarget = targetLayerObject;
        tTJSVariant wrapper(targetLayerObject, targetLayerObject);
        if(auto *resolved = tryResolveLayerDispatch(wrapper)) {
            resolvedTarget = resolved;
        }

        auto *targetLayer = resolveNativeLayer(resolvedTarget);
        if(!targetLayer) {
            return false;
        }

        auto *sharedAdaptor = ensureSharedD3DAdaptor(resolvedTarget);
        if(!sharedAdaptor) {
            return false;
        }

        if(!renderFromPlayerLike_0x6ADE24(sharedAdaptor, mainList)) {
            return false;
        }

        if(sharedAdaptor->getWidth() > 0 && sharedAdaptor->getHeight() > 0) {
            targetLayer->SetSize(sharedAdaptor->getWidth(),
                                 sharedAdaptor->getHeight());
        }
        targetLayer->SetVisible(true);

        tTJSVariant targetVar(resolvedTarget, resolvedTarget);
        tTJSVariant *args[] = { &targetVar };
        if(TJS_FAILED(sharedAdaptor->captureCanvas(nullptr, 1, args, nullptr))) {
            return false;
        }

        targetLayer->Update(false);
        _lastCanvas = tTJSVariant(resolvedTarget, resolvedTarget);
        return true;
    }


    iTJSDispatch2 *Player::resolveSeparateLayerRenderTarget(
        SeparateLayerAdaptor *sla,
        int &canvasWidth,
        int &canvasHeight) {
        canvasWidth = 0;
        canvasHeight = 0;
        const auto motionPath = matchedMotionPath();
        auto traceResolveFailure = [&](const char *reason,
                                       const tTJSVariant &target,
                                       iTJSDispatch2 *targetLayerObject) {
            iTJSDispatch2 *targetObject = nullptr;
            iTJSDispatch2 *targetObjThis = nullptr;
            if(target.Type() == tvtObject && target.AsObjectNoAddRef()) {
                const auto closure = target.AsObjectClosureNoAddRef();
                targetObject = closure.Object;
                targetObjThis = closure.ObjThis;
            }
            detail::logoChainTraceLogf(
                motionPath, "sla.resolveTarget.fail", "0x6D5948",
                _clampedEvalTime,
                "reason={} targetType={} targetObject={} targetObjThis={} targetLayer={} canvas={}x{}",
                reason ? reason : "<unknown>",
                static_cast<int>(target.Type()),
                static_cast<const void *>(targetObject),
                static_cast<const void *>(targetObjThis),
                static_cast<const void *>(targetLayerObject),
                canvasWidth, canvasHeight);
        };
        if(!sla) {
            return nullptr;
        }

        const auto originalOwnerLayer = sla->getOwnerVariant();
        const auto originalTargetLayer = sla->getTargetLayer();

        // libkrkr2.so Player_ResolveSLATarget @ 0x6D5948 constructs
        // PrivateMotionGLL(owner, targetLayer) from SLA+0 and SLA+20, then
        // stores it in SLA+40. targetLayer remains the original SLA+20
        // variant; only this local variable is reduced like sub_A7A050.
        iTJSDispatch2 *targetLayerObject =
            tryResolveLayerDispatch(originalTargetLayer);
        if(!targetLayerObject) {
            traceResolveFailure("no-target-layer", originalTargetLayer,
                                targetLayerObject);
            return nullptr;
        }

        if(!queryLayerCanvasSize(targetLayerObject, canvasWidth, canvasHeight)) {
            traceResolveFailure("no-canvas-size", originalTargetLayer,
                                targetLayerObject);
            return nullptr;
        }

        iTJSDispatch2 *renderTarget = ensurePrivateMotionGLLLike_0x6D5948(
            *sla,
            originalOwnerLayer,
            originalTargetLayer,
            targetLayerObject,
            canvasWidth,
            canvasHeight);
        if(!renderTarget) {
            traceResolveFailure("ensure-private-target-failed",
                                originalTargetLayer, targetLayerObject);
            return nullptr;
        }

        return renderTarget;
    }

    bool Player::renderMotionFrameToTarget(iTJSDispatch2 *renderTargetObject,
                                           tjs_int canvasWidth,
                                           tjs_int canvasHeight,
                                           const char *traceFunc,
                                           detail::PreparedRenderItemList &mainList,
                                           detail::PreparedRenderItemList &auxList) {
        if(!renderTargetObject || canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        auto *renderLayer =
            resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTargetObject);
        if(!renderLayer) {
            return false;
        }

        clearPrivateMotionGLLRenderQueueLike_0x6DE738(renderTargetObject);
        const auto motionPath = matchedMotionPath();
        detail::logoChainTraceLogf(
            motionPath, "sla.renderMotionFrame", "0x6DE738",
            _clampedEvalTime,
            "target={} canvas={}x{} route={}",
            static_cast<const void *>(renderTargetObject),
            canvasWidth, canvasHeight,
            traceFunc ? traceFunc : "0x6DE738");

        // Player_ResolveSLATarget @ 0x6D5948 owns PrivateMotionGLL sizing;
        // Player_RenderMotionFrame @ 0x6DE738 only emits render commands.
        buildRenderCommands(canvasWidth, canvasHeight, mainList, auxList);
        if(_sourceCacheNative) {
            for(auto *itemPtr : mainList) {
                if(!itemPtr) {
                    continue;
                }
                auto &item = *itemPtr;
                if(!shouldQueuePrivateMotionGLLRenderItemLike_0x6DE738(
                       item, _priorDraw)) {
                    continue;
                }
                auto *sourceTexture =
                    _sourceCacheNative
                        ->loadRenderSourceTextureFromItemLike_0x6C1B70(
                            *this, item);
                PrivateMotionGLLRenderItemInputLike_0x6DE738 queueItem;
                queueItem.opacity =
                    privateMotionGLLOpacityLike_0x6DE738(item, _priorDraw);
                queueItem.stencilMaskRef = item.stencilMaskRef;
                queueItem.stencilWriteRef = item.stencilWriteRef;
                queueItem.blendMode = item.blendMode;
                queueItem.geometryType = item.meshType;
                if(item.meshType == 1) {
                    const auto cellDivisions =
                        bezierPatchCellDivisionsU32Like_0x6C8E5C(
                            item.commandPatchDivision,
                            item.sourceState
                                ? item.sourceState->width
                                : item.nativeNode->source.width,
                            item.sourceState
                                ? item.sourceState->height
                                : item.nativeNode->source.height);
                    queueItem.meshDivX = cellDivisions[0];
                    queueItem.meshDivY = cellDivisions[1];
                } else if(item.meshType == 2) {
                    queueItem.meshDivX = item.meshDivX;
                    queueItem.meshDivY = item.meshDivY;
                }
                queueItem.packedColors = item.packedColors;
                if(sourceTexture) {
                    const auto rect = sourceRectForItem(item, sourceTexture);
                    queueItem.sourceRect = {rect[0], rect[1], rect[2], rect[3]};
                    queueItem.sourceTexture = sourceTexture;
                }
                auto *pointsToSwap =
                    populatePrivateMotionGLLPointsLike_0x6DE738(item, queueItem);
                appendPrivateMotionGLLRenderItemLike_0x6DE738(renderTargetObject,
                                                              queueItem,
                                                              pointsToSwap);
            }
            detail::logoChainTraceLogf(
                motionPath, "sla.renderMotionFrame.queue", "0x6DE738",
                _clampedEvalTime,
                "queuedItems={}",
                privateMotionGLLRenderQueueSizeLike_0x6DE738(
                    renderTargetObject));
        }
        // Player_DrawSLA @ 0x6D5658 calls Player_RenderMotionFrame @ 0x6DE738
        // only to populate the private +824 queue; Layer_UpdateRect @ 0x800F4C
        // later dispatches __Private_Motion_GLLayer::Draw_GPU @ 0x6DD56C.
        return true;
    }

    bool Player::renderAccurateSlaLike_0x6C9CA8(
        SeparateLayerAdaptor *sla,
        iTJSDispatch2 *slaObject,
        iTJSDispatch2 *targetLayerObject,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        detail::PreparedRenderItemList &mainList,
        detail::PreparedRenderItemList &auxList) {
        if(!sla || !slaObject || !targetLayerObject ||
           canvasWidth <= 0 || canvasHeight <= 0 ||
           !hasMotionContent() || !_sourceCacheNative) {
            return false;
        }

        const auto motionPath = matchedMotionPath();

        buildRenderCommands(canvasWidth, canvasHeight, mainList, auxList);

        iTJSDispatch2 *layerTreeOwner = resolveMainWindowOwnerObject();
        if(!layerTreeOwner) {
            layerTreeOwner = targetLayerObject;
        }

        struct AccurateSlaStateScope {
            SeparateLayerAdaptor *sla = nullptr;
            explicit AccurateSlaStateScope(SeparateLayerAdaptor *value)
                : sla(value) {
                if(sla) {
                    sla->beginAccurateRenderPassLike_0x6C9CA8();
                }
            }
            ~AccurateSlaStateScope() {
                if(sla) {
                    sla->endAccurateRenderPassLike_0x6C9CA8();
                }
            }
        } stateScope(sla);

        struct AccurateSlaItemLayer {
            iTJSDispatch2 *object = nullptr;
            bool createdOrChanged = true;
        };

        auto ensureAccurateSlaItemLayer =
            [&](PreparedRenderItem &item,
                tTVPLayerType layerType) -> AccurateSlaItemLayer {
            const tjs_int stateLayerId = item.renderLayerId;
            if(stateLayerId == 0) {
                return {
                    ensureReusableLayerObject(
                        item.leafLayer,
                        layerTreeOwner,
                        targetLayerObject,
                        layerType,
                        false),
                    true
                };
            }

            NativeSLAPayloadLike_0x6DCD0C payload;
            payload.type = static_cast<tjs_int>(layerType);
            payload.visible = true;
            payload.key = detail::widen(item.sourceKey);
            payload.flags = item.blendMode;
            payload.affine = {
                item.paintBox[0], item.paintBox[1],
                item.paintBox[2], item.paintBox[3],
                item.viewport[0], item.viewport[1],
                item.viewport[2], item.viewport[3]
            };
            bool createdOrChanged = false;
            tTJSVariant layerVariant =
                sla->resolveRenderLayerNodeLike_0x6C6B48(
                    static_cast<tjs_uint32>(stateLayerId), payload,
                    slaObject, createdOrChanged);

            auto *layerObject = tryResolveLayerDispatch(layerVariant);
            if(!layerObject) {
                return {};
            }

            // libkrkr2.so sub_6C4E28 @0x6C5DBC latches item+20 in the BUILD
            // loop (LABEL_28), never in the accurate-SLA execute path. The
            // build pass already materialized rawFlag20/layerId under the
            // oracle gate, so this path only consumes them.
            item.leafLayer = layerVariant;
            return { layerObject, createdOrChanged };
        };

        int renderedItems = 0;
        for(auto *itemPtr : mainList) {
            if(!itemPtr || !shouldRenderAccurateSlaItemLike_0x6C9CA8(*itemPtr)) {
                continue;
            }
            auto &item = *itemPtr;

            RenderClipRect clip;
            if(!computeAccurateSlaClipLike_0x6C9CA8(
                   item, static_cast<int>(canvasWidth),
                   static_cast<int>(canvasHeight), clip)) {
                continue;
            }

            const int clipWidth = clip.right - clip.left;
            const int clipHeight = clip.bottom - clip.top;
            const auto layerType =
                accurateSlaLayerTypeLike_0x6C9CA8(item.blendMode);
            const auto itemLayerResult =
                ensureAccurateSlaItemLayer(item, layerType);
            auto *itemLayerObject = itemLayerResult.object;
            auto *itemLayer = resolveNativeLayer(itemLayerObject);
            if(!itemLayerObject || !itemLayer) {
                continue;
            }

            if(itemLayerResult.createdOrChanged) {
                if(!item.sourceState) {
                    continue;
                }
                tTJSVariant sourceObject =
                    _sourceCacheNative
                        ->loadRenderSourceLayerFromItemLike_0x6C1B70(
                            *this, item);
                if(sourceObject.Type() != tvtObject ||
                   !sourceObject.AsObjectNoAddRef()) {
                    continue;
                }
                auto *sourceLayerObject = sourceObject.AsObjectNoAddRef();
                auto *sourceLayer = resolveNativeLayer(sourceLayerObject);
                auto *sourceImage = sourceLayer ? sourceLayer->GetMainImage()
                                                : nullptr;
                if(!sourceImage || sourceImage->GetWidth() <= 0 ||
                   sourceImage->GetHeight() <= 0 ||
                   !trySetAccurateSlaLayerSize(itemLayerObject, clipWidth,
                                               clipHeight)) {
                    continue;
                }

                const tTVPRect sourceRect(
                    0, 0,
                    static_cast<tjs_int>(sourceImage->GetWidth()),
                    static_cast<tjs_int>(sourceImage->GetHeight()));
                const float offsetX = -0.5f - static_cast<float>(clip.left);
                const float offsetY = -0.5f - static_cast<float>(clip.top);
                bool copied = false;
                if(item.meshType == 0) {
                    const auto localPts = buildAffineTrianglePoints(
                        item.corners, offsetX, offsetY);
                    itemLayer->AffineCopy(localPts.data(), sourceImage,
                                          sourceRect, stNearest, true);
                    copied = true;
                } else if(item.meshType == 1 && !item.meshPoints.empty()) {
                    auto localMeshPoints =
                        buildMeshPoints(item.meshPoints, offsetX, offsetY);
                    const auto cellDivisions =
                        bezierPatchCellDivisionsU32Like_0x6C8E5C(
                            item.commandPatchDivision,
                            static_cast<double>(sourceImage->GetWidth()),
                            static_cast<double>(sourceImage->GetHeight()));
                    itemLayer->BezierPatchCopy(
                        localMeshPoints.data(), cellDivisions[0],
                        cellDivisions[1],
                        sourceImage, sourceRect, stNearest, true);
                    copied = true;
                } else if(item.meshType == 2 && item.meshDivX >= 1 &&
                          item.meshDivY >= 1 &&
                          !item.commandCompositeMeshPoints.empty()) {
                    auto localMeshPoints =
                        buildMeshPoints(item.commandCompositeMeshPoints,
                                        offsetX, offsetY);
                    itemLayer->MeshCopy(localMeshPoints.data(), item.meshDivX,
                                        item.meshDivY, sourceImage, sourceRect,
                                        stNearest, true);
                    copied = true;
                }
                if(!copied) {
                    continue;
                }
            }

            itemLayer->SetPosition(clip.left, clip.top);
            itemLayer->SetType(layerType);
            itemLayer->SetVisible(true);
            itemLayer->SetOpacity(std::clamp(item.opacity, 0, 255));
            ++renderedItems;

#if defined(KRKR2_WASMTIME_HEADLESS)
            detail::motionTraceRecordPostDrawLayerCandidate(
                this, itemLayerObject,
                "Player::renderAccurateSla_0x6C9CA8.item.afterCopy");
#endif
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.item", "0x6C9CA8",
                _clampedEvalTime,
                "nodeIndex={} layerId={} clip=[{},{},{},{}] meshType={} type={} opacity={} source={}",
                item.nodeIndex, item.renderLayerId,
                clip.left, clip.top, clip.right, clip.bottom,
                item.meshType, static_cast<int>(layerType), item.opacity,
                item.sourceKey);
        }

        detail::logoChainTraceLogf(
            motionPath, "sla.accurate.rendered", "0x6C9CA8",
            _clampedEvalTime,
            "targetLayer={} canvas={}x{} renderedItems={}",
            static_cast<const void *>(targetLayerObject),
            canvasWidth, canvasHeight, renderedItems);
        return true;
    }

    bool Player::renderToD3DAdaptor(D3DAdaptor *adaptor) {
        if(!adaptor || adaptor->getWidth() <= 0 || adaptor->getHeight() <= 0) {
            return false;
        }
        // Guard against recursion: D3D capture can re-enter drawCompat.
        static bool s_inRenderToD3D = false;
        if(s_inRenderToD3D) return false;
        s_inRenderToD3D = true;
        struct Guard { ~Guard() { s_inRenderToD3D = false; } } guard;

        ensureMotionLoaded();
        if(!hasMotionContent()) return false;
        const auto motionPath = matchedMotionPath();
        detail::logoChainTraceLogf(
            motionPath, "draw.d3d", "0x6D5B90", _clampedEvalTime,
            "adaptorSize={}x{} route=D3DAdaptor_renderFromPlayer",
            adaptor->getWidth(), adaptor->getHeight());

        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        prepareRenderItems(mainList, auxList);
        applyPreparedRenderItemTranslateOffsets(mainList);
        return renderFromPlayerLike_0x6ADE24(adaptor, mainList);
    }

    // Player_drawToTexture @0x6D5C68. Unlike the script-facing drawCompat
    // route, D3DImage supplies the compositor's current native texture and the
    // transformed origin directly; no TJS Layer or D3DAdaptor is constructed.
    bool Player::drawToD3DImageLike_0x6D5C68(iTVPTexture2D *target,
                                              float x,
                                              float y) {
        if(!target) {
            return false;
        }
        ensureMotionLoaded();
        if(!hasMotionContent()) {
            return false;
        }
        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        if(!prepareRenderItems(mainList, auxList)) {
            return false;
        }
        applyPreparedRenderItemTranslateOffsets(mainList);
        // sub_6D5C68 @0x6D5E08..0x6D5EA0 passes a type-erased source getter
        // whose invoke boundary is sub_6F67CC: it returns the persistent
        // descriptor's current texture without the atlas retry in 0x6F1060.
        const D3DSourceTextureGetterLike_0x6ADFBC sourceTextureGetter =
            [](detail::PreparedRenderItem &item) {
                return item.sourceState->texture;
            };
        return renderItemsToD3DTextureLike_0x6ADFBC(
            target, static_cast<tjs_int>(target->GetWidth()),
            static_cast<tjs_int>(target->GetHeight()), false, x, y,
            mainList, sourceTextureGetter);
    }

    bool Player::renderFromPlayerLike_0x6ADE24(
        D3DAdaptor *adaptor,
        detail::PreparedRenderItemList &mainList) {
        if(!adaptor || adaptor->getWidth() <= 0 || adaptor->getHeight() <= 0) {
            return false;
        }
        // libkrkr2.so D3DAdaptor_renderFromPlayer @ 0x6ADE24 gates the whole
        // GPU texture pipeline on adaptor+21 canvasCaptureEnabled.
        if(!adaptor->getCanvasCaptureEnabled()) {
            return true;
        }
        if(!adaptor->ensureTargetTexture()) {
            return false;
        }
        if(adaptor->getClearEnabled()) {
            adaptor->clearTargetTexture();
        }
        return renderItemsToD3DTextureLike_0x6ADFBC(
            adaptor, mainList);
    }

    bool Player::renderItemsToD3DTextureLike_0x6ADFBC(
        D3DAdaptor *adaptor,
        detail::PreparedRenderItemList &mainList) {
        if(!adaptor || !hasMotionContent() ||
           !_sourceCacheNative) {
            return false;
        }
        auto *targetTexture = adaptor->targetTexture();
        if(!targetTexture) {
            return false;
        }

        // D3DAdaptor_renderFromPlayer @0x6ADE64..0x6ADF00 constructs the
        // other type-erased getter, whose invoke boundary is sub_6F1060.
        const D3DSourceTextureGetterLike_0x6ADFBC sourceTextureGetter =
            [this](detail::PreparedRenderItem &item) {
                return _sourceCacheNative
                    ->loadRenderSourceTextureForItemLike_0x6F1060(
                        *this, item);
            };
        return renderItemsToD3DTextureLike_0x6ADFBC(
            targetTexture, adaptor->getWidth(), adaptor->getHeight(),
            adaptor->getAlphaOpAdd(), 0.0f, 0.0f, mainList,
            sourceTextureGetter);
    }

    bool Player::renderItemsToD3DTextureLike_0x6ADFBC(
        iTVPTexture2D *targetTexture,
        tjs_int width,
        tjs_int height,
        bool alphaOpAdd,
        float xOffset,
        float yOffset,
        detail::PreparedRenderItemList &mainList,
        const D3DSourceTextureGetterLike_0x6ADFBC &sourceTextureGetter) {
        if(!targetTexture || !hasMotionContent() || !_sourceCacheNative ||
           width <= 0 || height <= 0) {
            return false;
        }

        const tTVPRect targetRect(0, 0, width, height);
        const int stencilRefs = prepareD3DRenderItemsLike_0x6ADFBC(
            mainList, width, height, _priorDraw);
        const bool stencilEnabled = stencilRefs > 0;
        beginD3DStencilIfNeeded(targetTexture, stencilEnabled);
        struct StencilGuard {
            bool enabled;
            ~StencilGuard() { endD3DStencilIfNeeded(enabled); }
        } stencilGuard{ stencilEnabled };

        const auto motionPath = matchedMotionPath();
        detail::logoChainTraceLogf(
            motionPath, "draw.d3d.renderItemsToTexture", "0x6ADFBC",
            _clampedEvalTime,
            "target={} targetRect=[0,0,{},{}] items={} priorDraw={} stencilRefs={}",
            static_cast<const void *>(targetTexture),
            width, height, mainList.size(),
            _priorDraw ? 1 : 0, stencilRefs);

        for(auto *itemPtr : mainList) {
            if(!itemPtr) {
                continue;
            }
            auto &item = *itemPtr;
            if(shouldSkipD3DRenderItemLike_0x6ADFBC(item, _priorDraw)) {
                continue;
            }

            int opacity = item.opacity;
            if(_priorDraw) {
                opacity = opacity >= 0 ? opacity / 2 : (opacity + 1) / 2;
            }
            if(opacity <= 0 && item.stencilMaskRef == 0) {
                continue;
            }

            auto *sourceTexture = sourceTextureGetter(item);
            if(!sourceTexture || sourceTexture->GetWidth() <= 0 ||
               sourceTexture->GetHeight() <= 0) {
                continue;
            }
            // sub_6ADFBC @0x6AE154..0x6AE188 rereads the descriptor only
            // after its texture callback returns, so render-time atlas writes
            // are visible here immediately.
            const auto sourceRect = sourceRectForItem(item, sourceTexture);
            if(sourceRect[2] <= sourceRect[0] ||
               sourceRect[3] <= sourceRect[1]) {
                continue;
            }

            applyD3DStencilState(item, stencilEnabled);
            auto *method = selectD3DRenderMethod(
                item.blendMode & 0xF,
                d3dPackedColorWithOpacity(item, opacity),
                alphaOpAdd,
                item.stencilMaskRef != 0,
                opacity);
            if(!method) {
                continue;
            }

            if(item.meshType == 0) {
                operateD3DAffine(method, targetTexture, targetRect, item,
                                 sourceTexture, sourceRect, xOffset, yOffset);
            } else if(item.meshType == 1) {
                const auto cellDivisions =
                    bezierPatchCellDivisionsU32Like_0x6C8E5C(
                        item.commandPatchDivision,
                        item.sourceState
                            ? item.sourceState->width
                            : item.nativeNode->source.width,
                        item.sourceState
                            ? item.sourceState->height
                            : item.nativeNode->source.height);
                const auto meshPoints =
                    tessellateBezierPatch(item.meshPoints, cellDivisions[0],
                                          cellDivisions[1], xOffset + 0.5,
                                          yOffset + 0.5);
                operateD3DMesh(method, targetTexture, targetRect,
                               sourceTexture, sourceRect, meshPoints,
                               cellDivisions[0], cellDivisions[1]);
            } else if(item.meshType == 2) {
                const auto meshPoints =
                    buildOffsetMeshPoints(item.commandCompositeMeshPoints,
                                          xOffset + 0.5, yOffset + 0.5);
                operateD3DMesh(method, targetTexture, targetRect,
                               sourceTexture, sourceRect, meshPoints,
                               item.meshDivX, item.meshDivY);
            }
        }

        return true;
    }

    bool Player::renderToCanvasLike_0x6C7440(
        tTJSVariant *target,
        detail::PreparedRenderItemList &mainList,
        detail::PreparedRenderItemList &auxList) {
        if(!target) {
            return false;
        }

        ensureMotionLoaded();
        if(!hasMotionContent()) {
            return false;
        }
        const auto motionPath = matchedMotionPath();

        iTJSDispatch2 *resolvedLayerObject = tryResolveLayerDispatch(*target);
        if(!resolvedLayerObject && target->Type() == tvtObject) {
            resolvedLayerObject = target->AsObjectNoAddRef();
        }
        if(!resolvedLayerObject) {
            detail::logoChainTraceCheck(
                motionPath, "draw.renderToCanvas", "0x6C7440",
                _clampedEvalTime,
                "target variant should resolve to a Layer object",
                "target did not resolve", false,
                "Player_renderToCanvas_guess could not resolve target variant");
            return false;
        }

        // Player_renderToCanvas @0x6C74E8: non-priorDraw rendering clears the
        // tTVPComplexRect at player+864 before collecting this frame's
        // submitted paint boxes. Player.clear has already consumed the
        // previous frame's bound before this draw begins.
        if(!_priorDraw) {
            _drawRegion.Clear();
        }

        int canvasWidth = 0;
        int canvasHeight = 0;
        queryLayerCanvasSize(resolvedLayerObject, canvasWidth, canvasHeight);
        if(canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }

        detail::logoChainTraceLogf(
            motionPath, "draw.renderToCanvas", "0x6C7440", _clampedEvalTime,
            "targetLayerCanvas={}x{} needsInternalAssignImages={} route=callerTarget",
            canvasWidth, canvasHeight,
            _needsInternalAssignImages ? 1 : 0);

        iTJSDispatch2 *renderLayerObject = resolvedLayerObject;
        if(auto *targetLayer = resolveNativeLayer(resolvedLayerObject)) {
            if(targetLayer->GetWidth() != canvasWidth ||
               targetLayer->GetHeight() != canvasHeight) {
                targetLayer->SetSize(canvasWidth, canvasHeight);
            }
        } else {
            return false;
        }

        buildRenderCommands(canvasWidth, canvasHeight, mainList, auxList);
        if(!executeLayerRenderCommands(renderLayerObject, true, mainList)) {
            return false;
        }

        _lastCanvas =
            tTJSVariant(resolvedLayerObject, resolvedLayerObject);
        detail::logoChainTraceSummary(
            motionPath, "renderToCanvasLike_0x6C7440", _clampedEvalTime,
            "callerTarget=1");
        return true;
    }

    bool Player::renderToLayer(iTJSDispatch2 *layerObject, bool skipUpdate) {
        if(!layerObject) {
            return false;
        }

        ensureMotionLoaded();
        if(!hasMotionContent()) {
            return false;
        }
        const auto motionPath = matchedMotionPath();

        tTJSVariant target(layerObject, layerObject);
        iTJSDispatch2 *resolvedLayerObject = layerObject;
        if(auto *resolved = tryResolveLayerDispatch(target)) {
            resolvedLayerObject = resolved;
        }

        int canvasWidth = 0;
        int canvasHeight = 0;
        queryLayerCanvasSize(resolvedLayerObject, canvasWidth, canvasHeight);
        if(canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        detail::logoChainTraceLogf(
            motionPath, "draw.layer", "0x6C7440/0x6CE7D8", _clampedEvalTime,
            "targetLayerCanvas={}x{} skipUpdate={} needsInternalAssignImages={}",
            canvasWidth, canvasHeight, skipUpdate ? 1 : 0,
            _needsInternalAssignImages ? 1 : 0);

        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        prepareRenderItems(mainList, auxList);
        applyPreparedRenderItemTranslateOffsets(mainList);

        const bool needsInternalAssignBeforeRender =
            _needsInternalAssignImages;
        if(!renderToCanvasLike_0x6C7440(&target, mainList, auxList)) {
            return false;
        }

        if(!skipUpdate) {
            if(!updateLayerAfterDrawLike_0x6CE7D8(target)) {
                return false;
            }
            if(!needsInternalAssignBeforeRender) {
                auto *layer = resolveNativeLayer(resolvedLayerObject);
                if(!layer) {
                    return false;
                }
                if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                   motionPath.find("m2logo.mtn") != std::string::npos &&
                   _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                    std::fprintf(stderr,
                                 "SNAPLAYER phase=beforeUpdate frame=%.3f %s\n",
                                 _clampedEvalTime,
                                 summarizeLayerChildren(layer).c_str());
                }
                layer->Update(false);
                detail::logoChainTraceLogf(
                    motionPath, "post.layer", "0x6CE7D8", _clampedEvalTime,
                    "targetLayer.Update(false) size={}x{}",
                    layer->GetWidth(), layer->GetHeight());
                if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                   motionPath.find("m2logo.mtn") != std::string::npos &&
                   _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                    std::fprintf(stderr,
                                 "SNAPLAYER phase=afterUpdate frame=%.3f %s\n",
                                 _clampedEvalTime,
                                 summarizeLayerChildren(layer).c_str());
                }
            }
        }

        detail::logoChainTraceSummary(
            motionPath, "renderToLayer", _clampedEvalTime,
            skipUpdate ? "skipUpdate=1" : "skipUpdate=0");
        return true;
    }

    bool Player::renderToSeparateLayerAdaptor(iTJSDispatch2 *slaObject) {
        if(!slaObject) {
            return false;
        }

        SeparateLayerAdaptor *sla =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                slaObject, false);
        if(!sla) {
            return false;
        }
        iTJSDispatch2 *ownerLayer =
            tryResolveLayerDispatch(sla->getOwnerVariant());

        ensureMotionLoaded();
        if(!hasMotionContent()) {
            return false;
        }
        const auto motionPath = matchedMotionPath();

        int canvasWidth = 0;
        int canvasHeight = 0;
        const bool accurateSla = isAccurateSlaRenderEnabled();
        iTJSDispatch2 *targetLayerObject =
            tryResolveLayerDispatch(sla->getTargetLayer());
        iTJSDispatch2 *renderTarget = nullptr;
        if(accurateSla) {
            if(targetLayerObject) {
                queryLayerCanvasSize(targetLayerObject, canvasWidth,
                                     canvasHeight);
                renderTarget = targetLayerObject;
            }
        } else {
            renderTarget =
                resolveSeparateLayerRenderTarget(sla, canvasWidth, canvasHeight);
            if(!targetLayerObject) {
                targetLayerObject = tryResolveLayerDispatch(sla->getTargetLayer());
            }
        }
        if(!renderTarget) {
            detail::logoChainTraceSummary(
                motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
                "fail=resolveSeparateLayerRenderTarget");
            return false;
        }
        detail::logoChainTraceLogf(
            motionPath, "draw.sla", "0x6D5658", _clampedEvalTime,
            "ownerLayer={} targetCanvas={}x{} accurate={} route={}",
            static_cast<const void *>(ownerLayer),
            canvasWidth, canvasHeight,
            accurateSla ? 1 : 0,
            accurateSla
                ? "0x6C9CA8 -> 0x6CE938"
                : "Player_RenderMotionFrame -> Layer_UpdateRect");
        detail::logoChainTraceLogf(
            motionPath, "sla.resolveTarget", "0x6D5948",
            _clampedEvalTime,
            "targetLayer={} privateTarget={} absolute={} canvas={}x{} "
            "targetName={} targetType={} targetFace={} targetChildren={} "
            "targetParent={} targetParentType={} "
            "privateType={} privateFace={} privateParent={} "
            "privateParentType={}",
            static_cast<const void *>(targetLayerObject),
            static_cast<const void *>(renderTarget),
            sla->getAbsolute(),
            canvasWidth, canvasHeight,
            resolveNativeLayer(targetLayerObject)
                ? resolveNativeLayer(targetLayerObject)->GetName().AsStdString()
                : std::string("<not-layer>"),
            resolveNativeLayer(targetLayerObject)
                ? static_cast<int>(resolveNativeLayer(targetLayerObject)->GetType())
                : -1,
            resolveNativeLayer(targetLayerObject)
                ? static_cast<int>(resolveNativeLayer(targetLayerObject)->GetFace())
                : -1,
            resolveNativeLayer(targetLayerObject)
                ? static_cast<int>(resolveNativeLayer(targetLayerObject)->GetCount())
                : -1,
            resolveNativeLayer(targetLayerObject)
                ? static_cast<const void *>(resolveNativeLayer(targetLayerObject)->GetParent())
                : nullptr,
            resolveNativeLayer(targetLayerObject) &&
                    resolveNativeLayer(targetLayerObject)->GetParent()
                ? static_cast<int>(
                      resolveNativeLayer(targetLayerObject)->GetParent()->GetType())
                : -1,
            resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)
                ? static_cast<int>(
                      resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)->GetType())
                : -1,
            resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)
                ? static_cast<int>(
                      resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)->GetFace())
                : -1,
            resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)
                ? static_cast<const void *>(
                      resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)->GetParent())
                : nullptr,
            resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget) &&
                    resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)->GetParent()
                ? static_cast<int>(
                      resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)
                          ->GetParent()
                          ->GetType())
                : -1);

        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        prepareRenderItems(mainList, auxList);
        applyPreparedRenderItemTranslateOffsets(mainList);

#if defined(KRKR2_WASMTIME_HEADLESS)
        struct AccurateSlaRenderTraceScope {
            Player *player = nullptr;
            iTJSDispatch2 *target = nullptr;
            bool active = false;
            AccurateSlaRenderTraceScope(Player *p, iTJSDispatch2 *t, bool enabled)
                : player(p), target(t), active(enabled) {
                if(active) {
                    detail::motionTraceBeginAccurateSlaRender(player, target);
                }
            }
            ~AccurateSlaRenderTraceScope() {
                if(active) {
                    detail::motionTraceEndAccurateSlaRender(player, target);
                }
            }
        } accurateSlaRenderTrace{
            this, renderTarget, accurateSla};
#endif

        if(accurateSla) {
#if defined(KRKR2_WASMTIME_HEADLESS)
            detail::MotionTraceRenderExecuteScope renderTrace(
                this, targetLayerObject, false, mainList);
#endif
            if(!renderAccurateSlaLike_0x6C9CA8(
                   sla, slaObject, targetLayerObject,
                   canvasWidth, canvasHeight, mainList, auxList)) {
                detail::logoChainTraceSummary(
                    motionPath, "renderToSeparateLayerAdaptor",
                    _clampedEvalTime,
                    "fail=renderAccurateSlaLike_0x6C9CA8");
                return false;
            }
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.begin", "0x6C9CA8",
                _clampedEvalTime,
                "target={} canvas={}x{}",
                static_cast<const void *>(targetLayerObject),
                canvasWidth, canvasHeight);
            updateAccurateSLAAfterDraw(sla->getTargetLayer());
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.end", "0x6CE938",
                _clampedEvalTime,
                "target={}", static_cast<const void *>(targetLayerObject));
#if defined(KRKR2_WASMTIME_HEADLESS)
            renderTrace.setResult(true);
#endif
        } else if(!renderMotionFrameToTarget(
                      renderTarget, canvasWidth, canvasHeight, "0x6DE738",
                      mainList, auxList)) {
            detail::logoChainTraceSummary(
                motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
                "fail=renderMotionFrameToTarget");
            return false;
        } else if(auto *renderLayer =
                      resolvePrivateMotionGLLNativeLike_0x6DE24C(renderTarget)) {
            renderLayer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "sla.updateRect", "0x800F4C", _clampedEvalTime,
                "renderTarget.Update(false) size={}x{} ownerLayer={}",
                renderLayer->GetWidth(), renderLayer->GetHeight(),
                static_cast<const void *>(ownerLayer));
        } else {
            detail::logoChainTraceCheck(
                motionPath, "sla.updateRect", "0x800F4C", _clampedEvalTime,
                "renderTarget should expose a native layer for Update(false)",
                "renderTarget native layer missing", false,
                "Player_RenderMotionFrame finished but SLA target lacked a native layer");
        }

        iTJSDispatch2 *lastCanvasObject = ownerLayer ? ownerLayer : renderTarget;
        _lastCanvas = tTJSVariant(lastCanvasObject, lastCanvasObject);
        detail::logoChainTraceSummary(
            motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
            accurateSla ? "accurate=1" : "accurate=0");
        return true;
    }

    bool Player::updateLayerAfterDrawLike_0x6CE7D8(
        const tTJSVariant &target) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        iTJSDispatch2 *rawProbeLayerObject =
            tryResolveLayerDispatch(target);
        if(!rawProbeLayerObject && target.Type() == tvtObject) {
            rawProbeLayerObject = target.AsObjectNoAddRef();
        }
        detail::motionTraceRenderImageCheckpoint(
            this, rawProbeLayerObject, "updateLayerAfterDraw_pre",
            "Player::updateLayerAfterDraw_0x6CE7D8.enter.after-target-resolve");
        detail::motionTraceLayerRawProbe(
            this, rawProbeLayerObject, "updateLayerAfterDraw_0x6CE7D8.enter");
        struct UpdateLayerAfterDrawTraceLeave {
            Player *player;
            iTJSDispatch2 *layerObject;
            ~UpdateLayerAfterDrawTraceLeave() {
                detail::motionTraceRenderImageCheckpoint(
                    player, layerObject, "updateLayerAfterDraw_post",
                    "Player::updateLayerAfterDraw_0x6CE7D8.leave.before-return");
                detail::motionTraceLayerRawProbe(
                    player, layerObject,
                    "updateLayerAfterDraw_0x6CE7D8.leave");
            }
        } updateLayerAfterDrawTraceLeave{this, rawProbeLayerObject};
#endif
        // 0x6CE7F4 first action: unconditionally snapshot the producer flag,
        // even when it is clear. Anchor type-10 (0x6C0528) reads this next frame
        // to gate on the internal render Layer being ready.
        _internalRenderLayerReady = _needsInternalAssignImages;
        if(!_needsInternalAssignImages) {
            return true;
        }
        const auto motionPath = matchedMotionPath();

        materializeInternalRenderLayersLike_0x6CE19C_guess(target);

        ncbPropAccessor internal{tTJSVariant(_internalRenderLayer)};
        (void)internal.FuncCall(
            0, TJS_W("assignImages"),
            &detail::assignImagesMemberHint_guess, nullptr, target);
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRecordPostDrawLayerCandidate(
            this, internal.GetDispatch(),
            "Player::updateLayerAfterDraw_0x6CE7D8.afterAssignImages");
#endif
        detail::logoChainTraceCheck(
            motionPath, "post.assignImages", "0x6CE7D8",
            _clampedEvalTime,
            "materialize internal/work Layers, then internal.assignImages(original target)",
            "assignImages(target)", true,
            "sub_6CE7D8 internal Layer snapshot dispatched");
        return true;
    }

    bool Player::updateLayerAfterDraw(iTJSDispatch2 *targetLayerObject) {
        if(!targetLayerObject) {
            return !_needsInternalAssignImages;
        }
        tTJSVariant target(targetLayerObject, targetLayerObject);
        return updateLayerAfterDrawLike_0x6CE7D8(target);
    }

    bool Player::updateAccurateSLAAfterDraw(const tTJSVariant &target) {
        const auto motionPath = matchedMotionPath();

        // sub_6CE938 @0x6CE938 mirrors 0x6CE7D8: snapshot the producer flag
        // unconditionally and leave the producer untouched.
        _internalRenderLayerReady = _needsInternalAssignImages;
        if(!_needsInternalAssignImages) {
            detail::logoChainTraceLogf(
                motionPath, "post.sla.accurate", "0x6CE938",
                _clampedEvalTime, "needsInternalAssignImages=0");
            return true;
        }

        ncbPropAccessor targetAccessor{tTJSVariant(target)};
        materializeInternalRenderLayersLike_0x6CE19C_guess(target);
        ncbPropAccessor internal{tTJSVariant(_internalRenderLayer)};

        const tjs_int height = propGetIntAfterProbeLike_0x6CE19C(
            targetAccessor.GetDispatch(), TJS_W("height"),
            &detail::heightMemberHint_guess);
        const tjs_int width = propGetIntAfterProbeLike_0x6CE19C(
            targetAccessor.GetDispatch(), TJS_W("width"),
            &detail::widthMemberHint_guess);
        (void)internal.FuncCall(
            0, TJS_W("piledCopy"), &detail::piledCopyMemberHint_guess,
            nullptr, tTJSVariant(0), tTJSVariant(0), target,
            tTJSVariant(0), tTJSVariant(0), tTJSVariant(width),
            tTJSVariant(height));
        detail::logoChainTraceCheck(
            motionPath, "post.sla.accurate", "0x6CE938",
            _clampedEvalTime,
            fmt::format("internal.piledCopy(0,0,target,0,0,{},{})",
                        width, height),
            "piledCopy", true,
            "sub_6CE938 accurate SLA post-copy dispatched");
        return true;
    }

} // namespace motion
