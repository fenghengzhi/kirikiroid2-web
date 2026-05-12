#include "PrivateMotionGLL.h"

#include "LayerBitmapIntf.h"
#include "MsgIntf.h"
#include "MotionTraceWeb.h"
#include "PlayerInternal.h"
#include "PlayerRenderInternal.h"
#include "RenderManager.h"
#include "SeparateLayerAdaptor.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <GLES2/gl2.h>
#elif !defined(KRKR2_WASMTIME_HEADLESS)
#include "ogl/ogl_common.h"
#endif

using namespace motion::internal;
using namespace motion::internal::render_detail;

namespace {

    tjs_uint32 g_PrivateMotionGLL_ClassID_Like_0x6DD284 =
        static_cast<tjs_uint32>(-1);

    const tjs_char *privateMotionGLLClassNameLike_0x6DD284() {
        return TJS_W("__Private_Motion_GLLayer");
    }

    struct OwnerResolutionLike_0x800438 {
        tTJSVariantClosure closure;
        iTVPLayerTreeOwner *layerTreeOwner = nullptr;
    };

    struct PrivateMotionGLLRenderItemLike_0x6DE738 {
        std::uint64_t pointsBegin = 0; // item+0; heap-owned in native
        std::uint64_t pointsEnd = 0; // item+8
        std::uint64_t pointsCapacity = 0; // item+16
        std::uint32_t opacity = 0; // item+24
        std::uint8_t stencilRefFromItem22 = 0; // item+28
        std::uint8_t stencilRefFromItem23 = 0; // item+29
        std::uint16_t reserved30 = 0;
        std::int32_t blendMode = 0; // item+32
        std::int32_t geometryType = 0; // item+36
        std::int32_t meshDivX = 0; // item+40
        std::int32_t meshDivY = 0; // item+44
        std::uint32_t color0 = 0; // item+48
        std::uint32_t color1 = 0; // item+52
        std::uint64_t color2And3 = 0; // item+56
        std::uint64_t sourceRect0 = 0; // item+64
        std::uint64_t sourceRect1 = 0; // item+72
        std::uint64_t sourceTexture = 0; // item+80; AddRef/Release in native
    };

    static_assert(sizeof(PrivateMotionGLLRenderItemLike_0x6DE738) == 88,
                  "PrivateMotionGLL render item must mirror native 88-byte items");

    static_assert(sizeof(motion::PrivateMotionGLLPackedPointLike_0x6DF33C) == 8,
                  "PrivateMotionGLL points mirror native packed float pairs");

    std::uint64_t storeNativePointerLike_0x6DE738(const void *ptr) {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ptr));
    }

    template <typename T>
    T *loadNativePointerLike_0x6DE738(std::uint64_t ptr) {
        return reinterpret_cast<T *>(static_cast<std::uintptr_t>(ptr));
    }

    void cleanupRenderItemLike_0x6DDE80(
        PrivateMotionGLLRenderItemLike_0x6DE738 &item) {
        if(auto *texture =
               loadNativePointerLike_0x6DE738<iTVPTexture2D>(item.sourceTexture)) {
            texture->Release();
            item.sourceTexture = 0;
        }
        if(auto *points =
               loadNativePointerLike_0x6DE738<void>(item.pointsBegin)) {
            operator delete(points);
            item.pointsBegin = 0;
            item.pointsEnd = 0;
            item.pointsCapacity = 0;
        }
    }

    tTVPRect sourceRectLike_0x6DD56C(
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item) {
        return tTVPRect(
            static_cast<std::int32_t>(item.sourceRect0 & 0xffffffffu),
            static_cast<std::int32_t>((item.sourceRect0 >> 32) & 0xffffffffu),
            static_cast<std::int32_t>(item.sourceRect1 & 0xffffffffu),
            static_cast<std::int32_t>((item.sourceRect1 >> 32) & 0xffffffffu));
    }

    const motion::PrivateMotionGLLPackedPointLike_0x6DF33C *
    pointsBeginLike_0x6DD56C(
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item) {
        return loadNativePointerLike_0x6DE738<
            motion::PrivateMotionGLLPackedPointLike_0x6DF33C>(
            item.pointsBegin);
    }

    std::size_t pointCountLike_0x6DD56C(
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item) {
        if(item.pointsEnd < item.pointsBegin) {
            return 0;
        }
        return static_cast<std::size_t>(
            (item.pointsEnd - item.pointsBegin) /
            sizeof(motion::PrivateMotionGLLPackedPointLike_0x6DF33C));
    }

    unsigned int packedColorWithOpacityLike_0x6DD56C(
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item) {
        const auto rgb = item.color0 == 0xff808080u
            ? 0x00ffffffu
            : (item.color0 & 0x00ffffffu);
        return rgb | ((item.opacity & 0xffu) << 24u);
    }

    tTVPBBBltMethod softwareMethodForPrivateMotionGLLLike_0x6DD56C(
        int blendLowNibble) {
        switch(blendLowNibble) {
            case 1: return bmPsAdditive;
            case 2:
            case 5: return bmPsSubtractive;
            case 3: return bmPsMultiplicative;
            case 4: return bmPsScreen;
            default: return bmAlpha;
        }
    }

    const char *gpuMethodNameForPrivateMotionGLLLike_0x6DD56C(
        int blendLowNibble,
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
                return alphaTest ? "AlphaBlend_color_AlphaTest"
                                 : "AlphaBlend_color";
        }
    }

    iTVPRenderMethod *selectPrivateMotionGLLRenderMethodLike_0x6DD56C(
        int blendLowNibble,
        unsigned int color,
        bool alphaTest,
        int opacity) {
        auto *mgr = TVPGetRenderManager();
        if(mgr->IsSoftware()) {
            return mgr->GetRenderMethod(
                opacity, false,
                softwareMethodForPrivateMotionGLLLike_0x6DD56C(
                    blendLowNibble));
        }

        auto *method = mgr->GetRenderMethod(
            gpuMethodNameForPrivateMotionGLLLike_0x6DD56C(
                blendLowNibble, alphaTest));
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

    bool privateMotionGLLStencilNeededLike_0x6DD56C(
        const std::deque<PrivateMotionGLLRenderItemLike_0x6DE738> &items) {
        for(const auto &item : items) {
            if(item.stencilRefFromItem22 || item.stencilRefFromItem23) {
                return true;
            }
        }
        return false;
    }

    void beginPrivateMotionGLLStencilLike_0x6DD56C(
        iTVPTexture2D *target,
        bool enabled) {
        if(!enabled || !target) {
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

    void applyPrivateMotionGLLStencilStateLike_0x6DD56C(
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item,
        bool enabled) {
        if(!enabled) {
            return;
        }
#if !defined(KRKR2_WASMTIME_HEADLESS)
        auto *mgr = TVPGetRenderManager();
        if(mgr->IsSoftware()) {
            return;
        }
        if(item.stencilRefFromItem23) {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_LEQUAL, item.stencilRefFromItem23, 255);
            if(item.stencilRefFromItem22) {
                glStencilMask(item.stencilRefFromItem22);
                glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);
            } else {
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            }
        } else if(item.stencilRefFromItem22) {
            glEnable(GL_STENCIL_TEST);
            glStencilMask(item.stencilRefFromItem22);
            glStencilFunc(GL_ALWAYS, item.stencilRefFromItem22, 255);
            glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
        } else {
            glDisable(GL_STENCIL_TEST);
        }
#endif
    }

    void endPrivateMotionGLLStencilLike_0x6DD56C(bool enabled) {
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

    std::array<tTVPPointD, 6> affineTargetQuadLike_0x6DD56C(
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item,
        double xOffset,
        double yOffset) {
        std::array<tTVPPointD, 6> out{};
        const auto *points = pointsBeginLike_0x6DD56C(item);
        if(!points || pointCountLike_0x6DD56C(item) < 3) {
            return out;
        }
        const tTVPPointD p0{points[0].x + xOffset, points[0].y + yOffset};
        const tTVPPointD p1{points[1].x + xOffset, points[1].y + yOffset};
        const tTVPPointD p2{points[2].x + xOffset, points[2].y + yOffset};
        const tTVPPointD p3{p1.x + p2.x - p0.x, p1.y + p2.y - p0.y};
        out = {{p0, p1, p2, p1, p2, p3}};
        return out;
    }

    std::array<tTVPPointD, 6> affineSourceQuadLike_0x6DD56C(
        const tTVPRect &sourceRect) {
        const double left = sourceRect.left;
        const double top = sourceRect.top;
        const double right = sourceRect.right;
        const double bottom = sourceRect.bottom;
        return {{
            {left, top},
            {right, top},
            {left, bottom},
            {right, top},
            {left, bottom},
            {right, bottom},
        }};
    }

    std::vector<tTVPPointD> offsetMeshPointsLike_0x6DD56C(
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item,
        double xOffset,
        double yOffset) {
        std::vector<tTVPPointD> out;
        const auto *points = pointsBeginLike_0x6DD56C(item);
        const auto count = pointCountLike_0x6DD56C(item);
        if(!points) {
            return out;
        }
        out.reserve(count);
        for(std::size_t i = 0; i < count; ++i) {
            out.push_back({points[i].x + xOffset, points[i].y + yOffset});
        }
        return out;
    }

    std::vector<tTVPPointD> tessellateBezierPatchLike_0x6DD56C(
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item,
        double xOffset,
        double yOffset) {
        std::vector<tTVPPointD> out;
        const auto *points = pointsBeginLike_0x6DD56C(item);
        const auto count = pointCountLike_0x6DD56C(item);
        if(!points || count < 16 || item.meshDivX < 2 || item.meshDivY < 2) {
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
                const auto *base = points + row * 4;
                curve[row].x = cubicBlend(base[0].x, base[1].x, base[2].x,
                                          base[3].x, u);
                curve[row].y = cubicBlend(base[0].y, base[1].y, base[2].y,
                                          base[3].y, u);
            }
            return tTVPPointD{
                cubicBlend(curve[0].x, curve[1].x, curve[2].x,
                           curve[3].x, v) + xOffset,
                cubicBlend(curve[0].y, curve[1].y, curve[2].y,
                           curve[3].y, v) + yOffset,
            };
        };

        out.reserve(static_cast<std::size_t>(item.meshDivX) *
                    static_cast<std::size_t>(item.meshDivY));
        for(int y = 0; y < item.meshDivY; ++y) {
            const double v =
                static_cast<double>(y) / static_cast<double>(item.meshDivY - 1);
            for(int x = 0; x < item.meshDivX; ++x) {
                const double u = static_cast<double>(x) /
                    static_cast<double>(item.meshDivX - 1);
                out.push_back(samplePatch(u, v));
            }
        }
        return out;
    }

    bool operatePrivateMotionGLLAffineLike_0x6DD56C(
        iTVPRenderMethod *method,
        iTVPTexture2D *targetTexture,
        const tTVPRect &targetRect,
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item,
        iTVPTexture2D *sourceTexture,
        double xOffset,
        double yOffset) {
        if(pointCountLike_0x6DD56C(item) < 3) {
            return false;
        }
        auto dst = affineTargetQuadLike_0x6DD56C(item, xOffset, yOffset);
        auto src = affineSourceQuadLike_0x6DD56C(sourceRectLike_0x6DD56C(item));
        tRenderTexQuadArray::Element srcTex[] = {
            tRenderTexQuadArray::Element(sourceTexture, src.data())
        };
        TVPGetRenderManager()->OperateTriangles(
            method, 2, targetTexture, targetTexture, targetRect, dst.data(),
            tRenderTexQuadArray(srcTex));
        return true;
    }

    bool operatePrivateMotionGLLMeshLike_0x6DD56C(
        iTVPRenderMethod *method,
        iTVPTexture2D *targetTexture,
        const tTVPRect &targetRect,
        const PrivateMotionGLLRenderItemLike_0x6DE738 &item,
        iTVPTexture2D *sourceTexture,
        const std::vector<tTVPPointD> &meshPoints) {
        if(item.meshDivX < 2 || item.meshDivY < 2 ||
           meshPoints.size() < static_cast<std::size_t>(item.meshDivX) *
                   static_cast<std::size_t>(item.meshDivY)) {
            return false;
        }

        const auto sourceRect = sourceRectLike_0x6DD56C(item);
        const double srcW = sourceRect.get_width();
        const double srcH = sourceRect.get_height();
        for(int y = 0; y < item.meshDivY - 1; ++y) {
            const double v0 =
                static_cast<double>(y) / static_cast<double>(item.meshDivY - 1);
            const double v1 = static_cast<double>(y + 1) /
                static_cast<double>(item.meshDivY - 1);
            for(int x = 0; x < item.meshDivX - 1; ++x) {
                const double u0 = static_cast<double>(x) /
                    static_cast<double>(item.meshDivX - 1);
                const double u1 = static_cast<double>(x + 1) /
                    static_cast<double>(item.meshDivX - 1);
                const auto &p0 = meshPoints[y * item.meshDivX + x];
                const auto &p1 = meshPoints[y * item.meshDivX + x + 1];
                const auto &p2 = meshPoints[(y + 1) * item.meshDivX + x];
                const auto &p3 =
                    meshPoints[(y + 1) * item.meshDivX + x + 1];
                std::array<tTVPPointD, 6> dst{{p0, p1, p2, p1, p2, p3}};
                std::array<tTVPPointD, 6> src{{
                    {sourceRect.left + std::floor(srcW * u0),
                     sourceRect.top + std::floor(srcH * v0)},
                    {sourceRect.left + std::ceil(srcW * u1),
                     sourceRect.top + std::floor(srcH * v0)},
                    {sourceRect.left + std::floor(srcW * u0),
                     sourceRect.top + std::ceil(srcH * v1)},
                    {sourceRect.left + std::ceil(srcW * u1),
                     sourceRect.top + std::floor(srcH * v0)},
                    {sourceRect.left + std::floor(srcW * u0),
                     sourceRect.top + std::ceil(srcH * v1)},
                    {sourceRect.left + std::ceil(srcW * u1),
                     sourceRect.top + std::ceil(srcH * v1)},
                }};
                tRenderTexQuadArray::Element srcTex[] = {
                    tRenderTexQuadArray::Element(sourceTexture, src.data())
                };
                TVPGetRenderManager()->OperateTriangles(
                    method, 2, targetTexture, targetTexture, targetRect,
                    dst.data(), tRenderTexQuadArray(srcTex));
            }
        }
        return true;
    }

    OwnerResolutionLike_0x800438 requireOwnerClosureLike_0x800438(
        const tTJSVariant &ownerVariant) {
        if(ownerVariant.Type() != tvtObject || !ownerVariant.AsObjectNoAddRef()) {
            TVPThrowExceptionMessage(
                TJS_W("Please specify layerTreeOwnerInterface object"));
        }

        auto closure = ownerVariant.AsObjectClosureNoAddRef();
        if(!closure.Object) {
            TVPThrowExceptionMessage(
                TJS_W("Please specify layerTreeOwnerInterface object"));
        }

        tTJSVariant ownerInterface;
        iTJSDispatch2 *objthis = closure.ObjThis ? closure.ObjThis
                                                 : closure.Object;
        const tjs_error hr = closure.Object->PropGet(
            0, TJS_W("layerTreeOwnerInterface"), nullptr, &ownerInterface,
            objthis);
        if(TJS_FAILED(hr)) {
            TVPThrowExceptionMessage(
                TJS_W("Cannot Retrive Layer Tree Owner Interface."));
        }
        auto *layerTreeOwner = reinterpret_cast<iTVPLayerTreeOwner *>(
            static_cast<tjs_intptr_t>(static_cast<tjs_int64>(ownerInterface)));
        if(!layerTreeOwner) {
            TVPThrowExceptionMessage(
                TJS_W("Cannot Retrive Layer Tree Owner Interface."));
        }
        return { closure, layerTreeOwner };
    }

    tTJSNI_BaseLayer *resolveTargetLayerNativeOrNullLike_0x800438(
        const tTJSVariant &targetLayerVariant,
        iTJSDispatch2 *targetLayerObject) {
        if(targetLayerVariant.Type() != tvtObject ||
           !targetLayerVariant.AsObjectNoAddRef() || !targetLayerObject) {
            return nullptr;
        }

        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(targetLayerVariant.AsObjectNoAddRef()->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer)))) {
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        return layer;
    }

    class tTJSNI_PrivateMotionGLLLayerLike_0x800438 final : public tTJSNI_Layer {
    public:
        ~tTJSNI_PrivateMotionGLLLayerLike_0x800438() override {
            ClearRenderQueueLike_0x6DE738();
        }

        tjs_error Construct(tjs_int numparams,
                            tTJSVariant **param,
                            iTJSDispatch2 *tjs_obj) override {
            if(numparams < 2) {
                return TJS_E_BADPARAMCOUNT;
            }

            auto owner =
                requireOwnerClosureLike_0x800438(*param[0]);
            iTJSDispatch2 *targetObject =
                param[1]->Type() == tvtObject ? param[1]->AsObjectNoAddRef()
                                               : nullptr;
            auto *parentLayer = resolveTargetLayerNativeOrNullLike_0x800438(
                *param[1], targetObject);

            if(parentLayer && parentLayer == this) {
                TVPThrowExceptionMessage(TVPCannotSetParentSelf);
            }
            if(parentLayer &&
               parentLayer->GetLayerTreeOwner() != owner.layerTreeOwner) {
                TVPThrowExceptionMessage(TVPCannotMoveToUnderOtherPrimaryLayer);
            }

            // PrivateMotionGLL @ 0x800438 resolves the owner LTO and target
            // native Layer before the raw child-layer attach. This bypasses
            // the public Layer constructor's script-side lookup while the TJS
            // object remains registered only as __Private_Motion_GLLayer.
            const tjs_error hr = ConstructResolvedTreeOwnerLike_0x800438(
                owner.layerTreeOwner, parentLayer, tjs_obj, owner.closure);
            if(TJS_FAILED(hr)) {
                return hr;
            }

            // sub_8361A8 binds the child owner and immediately applies
            // visible=true and opacity=255 to the new layer.
            SetVisible(true);
            SetOpacity(255);
            return TJS_S_OK;
        }

        void Draw_GPU(tTVPDrawable *target,
                      int x,
                      int y,
                      const tTVPRect &r,
                      bool visiblecheck = true) override {
            if(visiblecheck && !IsSeen()) {
                return;
            }

            tTVPRect rect;
            if(!TVPIntersectRect(&rect, r, Rect)) {
                return;
            }

            x += rect.left - r.left;
            y += rect.top - r.top;
            tTVPRect targetRect(rect);
            targetRect.set_offsets(x, y);

#if defined(KRKR2_WASMTIME_HEADLESS)
            motion::detail::motionTracePrivateMotionGLLDraw(
                this, static_cast<int>(_renderQueueLike_0x6DDBD8.size()),
                rect.left, rect.top, rect.right, rect.bottom,
                targetRect.left, targetRect.top,
                targetRect.right, targetRect.bottom, visiblecheck);
#endif

            CurrentDrawTarget = target;
            auto resetTarget = [&] { CurrentDrawTarget = nullptr; };
            tTVPRect clipRect;
            auto *targetBitmap = target ? target->GetDrawTargetBitmap(
                                              targetRect, clipRect)
                                        : nullptr;
            if(!targetBitmap) {
                resetTarget();
                return;
            }

            auto *targetTexture = targetBitmap->GetTextureForRender(
                true, &clipRect);
            if(!targetTexture) {
                resetTarget();
                return;
            }

            auto *mgr = TVPGetRenderManager();
            mgr->SetRenderTarget(targetTexture);
            const bool stencilEnabled =
                privateMotionGLLStencilNeededLike_0x6DD56C(
                    _renderQueueLike_0x6DDBD8);
            beginPrivateMotionGLLStencilLike_0x6DD56C(targetTexture,
                                                       stencilEnabled);
            const double xOffset = static_cast<double>(x) - 0.5;
            const double yOffset = static_cast<double>(y) - 0.5;

            for(const auto &item : _renderQueueLike_0x6DDBD8) {
                auto *sourceTexture =
                    loadNativePointerLike_0x6DE738<iTVPTexture2D>(
                        item.sourceTexture);
                if(!sourceTexture) {
                    break;
                }
                auto *method = selectPrivateMotionGLLRenderMethodLike_0x6DD56C(
                    item.blendMode & 0x0f,
                    packedColorWithOpacityLike_0x6DD56C(item),
                    item.stencilRefFromItem22 != 0,
                    static_cast<int>(item.opacity));
                if(!method) {
                    continue;
                }

                applyPrivateMotionGLLStencilStateLike_0x6DD56C(
                    item, stencilEnabled);
                switch(item.geometryType) {
                    case 0:
                        operatePrivateMotionGLLAffineLike_0x6DD56C(
                            method, targetTexture, clipRect, item,
                            sourceTexture, xOffset, yOffset);
                        break;
                    case 1: {
                        const auto meshPoints =
                            tessellateBezierPatchLike_0x6DD56C(
                                item, xOffset, yOffset);
                        operatePrivateMotionGLLMeshLike_0x6DD56C(
                            method, targetTexture, clipRect, item,
                            sourceTexture, meshPoints);
                        break;
                    }
                    case 2: {
                        const auto meshPoints = offsetMeshPointsLike_0x6DD56C(
                            item, xOffset, yOffset);
                        operatePrivateMotionGLLMeshLike_0x6DD56C(
                            method, targetTexture, clipRect, item,
                            sourceTexture, meshPoints);
                        break;
                    }
                    default:
                        break;
                }
            }

            endPrivateMotionGLLStencilLike_0x6DD56C(stencilEnabled);
            resetTarget();
        }

        void ClearRenderQueueLike_0x6DE738() {
            // Player_RenderMotionFrame @ 0x6DE738 clears the deque rooted at
            // native this+824 before appending the current frame's 88-byte
            // render commands.
            for(auto &item : _renderQueueLike_0x6DDBD8) {
                cleanupRenderItemLike_0x6DDE80(item);
            }
            _renderQueueLike_0x6DDBD8.clear();
        }

        void AppendRenderItemLike_0x6DE738(
            const motion::PrivateMotionGLLRenderItemInputLike_0x6DE738 &input) {
            PrivateMotionGLLRenderItemLike_0x6DE738 item;
            struct OperatorDelete {
                void operator()(void *ptr) const { operator delete(ptr); }
            };
            std::unique_ptr<void, OperatorDelete> pointsOwner;
            if(!input.points.empty()) {
                const auto bytes =
                    input.points.size() *
                    sizeof(motion::PrivateMotionGLLPackedPointLike_0x6DF33C);
                auto *points = static_cast<
                    motion::PrivateMotionGLLPackedPointLike_0x6DF33C *>(
                    operator new(bytes));
                std::memcpy(points, input.points.data(), bytes);
                pointsOwner.reset(points);
                item.pointsBegin = storeNativePointerLike_0x6DE738(points);
                item.pointsEnd = storeNativePointerLike_0x6DE738(
                    points + input.points.size());
                item.pointsCapacity = item.pointsEnd;
            }
            item.opacity = static_cast<std::uint32_t>(input.opacity);
            item.stencilRefFromItem22 = input.stencilMaskRef;
            item.stencilRefFromItem23 = input.stencilWriteRef;
            item.blendMode = input.blendMode;
            item.geometryType = input.geometryType;
            item.meshDivX = input.meshDivX;
            item.meshDivY = input.meshDivY;
            item.color0 = input.packedColors[0];
            item.color1 = input.packedColors[1];
            item.color2And3 =
                static_cast<std::uint64_t>(input.packedColors[2]) |
                (static_cast<std::uint64_t>(input.packedColors[3]) << 32);
            item.sourceRect0 =
                static_cast<std::uint32_t>(input.sourceRect[0]) |
                (static_cast<std::uint64_t>(
                     static_cast<std::uint32_t>(input.sourceRect[1])) << 32);
            item.sourceRect1 =
                static_cast<std::uint32_t>(input.sourceRect[2]) |
                (static_cast<std::uint64_t>(
                     static_cast<std::uint32_t>(input.sourceRect[3])) << 32);
            if(input.sourceTexture) {
                input.sourceTexture->AddRef();
                item.sourceTexture =
                    storeNativePointerLike_0x6DE738(input.sourceTexture);
            }
            try {
                _renderQueueLike_0x6DDBD8.push_back(item);
                pointsOwner.release();
            } catch(...) {
                if(input.sourceTexture) {
                    input.sourceTexture->Release();
                }
                throw;
            }
        }

        std::size_t RenderQueueSizeLike_0x6DE738() const {
            return _renderQueueLike_0x6DDBD8.size();
        }

    private:
        // sub_6DD430 zeroes native +824..+904 and initializes this deque via
        // sub_6DDBD8(..., 0). The default std::deque construction gives the
        // same source-level lifetime: constructed after base Layer, destroyed
        // before tTJSNI_Layer/tTJSNI_BaseLayer.
        std::deque<PrivateMotionGLLRenderItemLike_0x6DE738>
            _renderQueueLike_0x6DDBD8;
    };

    tTJSNI_PrivateMotionGLLLayerLike_0x800438 *
    resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(iTJSDispatch2 *object) {
        if(!object ||
           g_PrivateMotionGLL_ClassID_Like_0x6DD284 ==
               static_cast<tjs_uint32>(-1)) {
            return nullptr;
        }
        tTJSNI_PrivateMotionGLLLayerLike_0x800438 *layer = nullptr;
        if(TJS_FAILED(object->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, g_PrivateMotionGLL_ClassID_Like_0x6DD284,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return nullptr;
        }
        return layer;
    }

    tjs_error PrivateMotionGLL_constructorLike_0x6DE24C(
        tTJSVariant * /*result*/,
        tjs_int numparams,
        tTJSVariant **param,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(objthis);
        if(!self) {
            return TJS_E_NATIVECLASSCRASH;
        }
        return self->Construct(numparams, param, objthis);
    }

    tjs_error PrivateMotionGLL_setSizeLike_0x6DE2E0(
        tTJSVariant * /*result*/,
        tjs_int numparams,
        tTJSVariant **param,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(objthis);
        if(!self) {
            return TJS_E_NATIVECLASSCRASH;
        }
        if(numparams < 2) {
            return TJS_E_BADPARAMCOUNT;
        }
        self->SetSize(static_cast<tjs_int>(*param[0]),
                      static_cast<tjs_int>(*param[1]));
        return TJS_S_OK;
    }

    tjs_error PrivateMotionGLL_getVisibleLike_0x6DE46C(
        tTJSVariant *result,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(objthis);
        if(!self) {
            return TJS_E_NATIVECLASSCRASH;
        }
        if(result) {
            *result = self->GetVisible();
        }
        return TJS_S_OK;
    }

    tjs_error PrivateMotionGLL_setVisibleLike_0x6DE4EC(
        const tTJSVariant *param,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(objthis);
        if(!self) {
            return TJS_E_NATIVECLASSCRASH;
        }
        self->SetVisible(*param);
        return TJS_S_OK;
    }

    tjs_error PrivateMotionGLL_getAbsoluteLike_0x6DE5C8(
        tTJSVariant *result,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(objthis);
        if(!self) {
            return TJS_E_NATIVECLASSCRASH;
        }
        if(result) {
            *result = static_cast<tjs_int>(self->GetAbsoluteOrderIndex());
        }
        return TJS_S_OK;
    }

    tjs_error PrivateMotionGLL_setAbsoluteLike_0x6DE64C(
        const tTJSVariant *param,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(objthis);
        if(!self) {
            return TJS_E_NATIVECLASSCRASH;
        }
        self->SetAbsoluteOrderIndex(static_cast<tjs_int>(*param));
        return TJS_S_OK;
    }

    class tTJSNC_PrivateMotionGLLLayerLike_0x6DD284 final : public tTJSNativeClass {
    public:
        tTJSNC_PrivateMotionGLLLayerLike_0x6DD284()
            : tTJSNativeClass(privateMotionGLLClassNameLike_0x6DD284()) {
            const tjs_char *className = privateMotionGLLClassNameLike_0x6DD284();
            g_PrivateMotionGLL_ClassID_Like_0x6DD284 =
                TJSRegisterNativeClass(className);
            SetClassID(g_PrivateMotionGLL_ClassID_Like_0x6DD284);

            TJSNativeClassRegisterNCM(
                this, className,
                TJSCreateNativeClassConstructor(
                    PrivateMotionGLL_constructorLike_0x6DE24C),
                className, nitMethod);
            TJSNativeClassRegisterNCM(
                this, TJS_W("setSize"),
                TJSCreateNativeClassMethod(PrivateMotionGLL_setSizeLike_0x6DE2E0),
                className, nitMethod);
            TJSNativeClassRegisterNCM(
                this, TJS_W("visible"),
                TJSCreateNativeClassProperty(
                    PrivateMotionGLL_getVisibleLike_0x6DE46C,
                    PrivateMotionGLL_setVisibleLike_0x6DE4EC),
                className, nitProperty);
            TJSNativeClassRegisterNCM(
                this, TJS_W("absolute"),
                TJSCreateNativeClassProperty(
                    PrivateMotionGLL_getAbsoluteLike_0x6DE5C8,
                    PrivateMotionGLL_setAbsoluteLike_0x6DE64C),
                className, nitProperty);
        }

    protected:
        tTJSNativeInstance *CreateNativeInstance() override {
            return new tTJSNI_PrivateMotionGLLLayerLike_0x800438();
        }
    };

    tTJSNC_PrivateMotionGLLLayerLike_0x6DD284 *
    privateMotionGLLClassDispatchLike_0x6D5948() {
        // Player_ResolveSLATarget @ 0x6D5948 guards qword_1AB8578, initialized
        // by PrivateMotionGLL_CreateClass @ 0x6DD284, then reuses it forever.
        static auto *klass =
            new tTJSNC_PrivateMotionGLLLayerLike_0x6DD284();
        return klass;
    }

    iTJSDispatch2 *createPrivateLayerObjectWithNativeClassLike_0x800438(
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant) {
        auto *layerClass = privateMotionGLLClassDispatchLike_0x6D5948();
        iTJSDispatch2 *created = nullptr;
        tTJSVariant ownerArg(ownerVariant);
        tTJSVariant targetArg(targetLayerVariant);
        tTJSVariant *args[] = { &ownerArg, &targetArg };
        const tjs_error hr = layerClass->CreateNew(0, nullptr, nullptr, &created,
                                                   2, args, layerClass);
        if(TJS_FAILED(hr) || !created) {
            TVPThrowExceptionMessage(TJS_W("Cannot create PrivateMotionGLL."));
        }
        return created;
    }

    iTJSDispatch2 *createPrivateLayerObjectLike_0x800438(
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant,
        iTJSDispatch2 *targetLayerObject) {
        requireOwnerClosureLike_0x800438(ownerVariant);
        if(!resolveTargetLayerNativeOrNullLike_0x800438(targetLayerVariant,
                                                        targetLayerObject)) {
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }

        return createPrivateLayerObjectWithNativeClassLike_0x800438(
            ownerVariant, targetLayerVariant);
    }

    void invalidateObjectVariantLike_0x6AC27C(tTJSVariant &value) {
        if(value.Type() == tvtObject && value.AsObjectNoAddRef()) {
            auto closure = value.AsObjectClosureNoAddRef();
            if(closure.Object) {
                closure.Invalidate(0, nullptr, nullptr, nullptr);
            }
        }
        value.Clear();
    }

} // namespace

namespace motion {

    iTJSDispatch2 *ensurePrivateMotionGLLLike_0x6D5948(
        SeparateLayerAdaptor &sla,
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant,
        iTJSDispatch2 *targetLayerObject,
        int canvasWidth,
        int canvasHeight) {
        if(!targetLayerObject || canvasWidth <= 0 || canvasHeight <= 0) {
            return nullptr;
        }

        if(sla._privateTarget.Type() != tvtObject) {
            if(sla._privateTarget.Type() != tvtVoid) {
                sla._privateTarget.Clear();
            }

            iTJSDispatch2 *created = createPrivateLayerObjectLike_0x800438(
                ownerVariant, targetLayerVariant, targetLayerObject);
            sla._privateTarget = tTJSVariant(created, created);
            created->Release();

            if(auto *layer = resolvePrivateMotionGLLNativeLike_0x6DE24C(
                   sla._privateTarget.AsObjectNoAddRef())) {
                // Player_ResolveSLATarget @ 0x6D5948 performs these writes
                // immediately after the newly created object is stored in
                // SLA+40, before the per-frame SetSize call.
                layer->SetAbsoluteOrderIndex(
                    static_cast<tjs_int>(sla.getAbsolute()));
                layer->SetVisible(true);
            }
            sla.trackManagedTargetLike_0x6AC410(sla._privateTarget, 0);
        }

        iTJSDispatch2 *layerObject = sla.getPrivateRenderTargetObject();
        auto *privateLayer =
            resolvePrivateMotionGLLNativeLike_0x6DE24C(layerObject);
        if(!layerObject || !privateLayer) {
            invalidateObjectVariantLike_0x6AC27C(sla._privateTarget);
            return nullptr;
        }

        // The native reuse path always applies Layer_SetSize_thunk after the
        // PrivateMotionGLL native instance is fetched from SLA+40.
        privateLayer->SetSize(canvasWidth, canvasHeight);
        return layerObject;
    }

    tTJSNI_BaseLayer *resolvePrivateMotionGLLNativeLike_0x6DE24C(
        iTJSDispatch2 *object) {
        return resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(object);
    }

    void clearPrivateMotionGLLRenderQueueLike_0x6DE738(iTJSDispatch2 *object) {
        if(auto *layer = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(
               object)) {
            layer->ClearRenderQueueLike_0x6DE738();
        }
    }

    void appendPrivateMotionGLLRenderItemLike_0x6DE738(
        iTJSDispatch2 *object,
        const PrivateMotionGLLRenderItemInputLike_0x6DE738 &item) {
        if(auto *layer = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(
               object)) {
            layer->AppendRenderItemLike_0x6DE738(item);
        }
    }

    std::size_t privateMotionGLLRenderQueueSizeLike_0x6DE738(
        iTJSDispatch2 *object) {
        if(auto *layer = resolvePrivateMotionGLLNativeInternalLike_0x6DE24C(
               object)) {
            return layer->RenderQueueSizeLike_0x6DE738();
        }
        return 0;
    }

} // namespace motion
