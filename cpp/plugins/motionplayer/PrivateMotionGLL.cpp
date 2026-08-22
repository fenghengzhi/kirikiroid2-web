#include "PrivateMotionGLL.h"

#include "LayerBitmapIntf.h"
#include "MsgIntf.h"
#include "MotionTraceWeb.h"
#include "MotionRenderBackend.h"
#include "PlayerInternal.h"
#include "PlayerRenderInternal.h"
#include "RenderManager.h"
#include "SeparateLayerAdaptor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <GLES2/gl2.h>
#elif !defined(KRKR2_WASMTIME_HEADLESS)
#include "ogl/ogl_common.h"
#endif

using namespace motion::internal;
using namespace motion::internal::render_detail;

namespace {

    tjs_uint32 g_PrivateMotionGLLClassId_guess =
        static_cast<tjs_uint32>(-1);

    const tjs_char *privateMotionGLLClassName() {
        return TJS_W("__Private_Motion_GLLayer");
    }

    struct PrivateMotionGLLRenderItem_guess {
        std::vector<motion::detail::MeshPoint> points;
        std::uint32_t opacity;
        std::uint8_t stencilRefFromItem22;
        std::uint8_t stencilRefFromItem23;
        std::int32_t blendMode;
        std::int32_t geometryType;
        std::int32_t meshDivX;
        std::int32_t meshDivY;
        std::array<std::uint32_t, 4> packedColors;
        std::array<std::int32_t, 4> sourceRect;
        iTVPTexture2D *sourceTexture;

        explicit PrivateMotionGLLRenderItem_guess(
            const motion::PrivateMotionGLLRenderItemInput_guess &input)
            : opacity(static_cast<std::uint32_t>(input.opacity)),
              stencilRefFromItem22(input.stencilMaskRef),
              stencilRefFromItem23(input.stencilWriteRef),
              blendMode(input.blendMode),
              geometryType(input.geometryType),
              packedColors(input.packedColors),
              sourceRect(input.sourceRect),
              sourceTexture(nullptr) {
            if(input.sourceTexture) {
                input.sourceTexture->AddRef();
                sourceTexture = input.sourceTexture;
            }
        }
        PrivateMotionGLLRenderItem_guess(
            const PrivateMotionGLLRenderItem_guess &) = delete;
        PrivateMotionGLLRenderItem_guess &operator=(
            const PrivateMotionGLLRenderItem_guess &) = delete;

        ~PrivateMotionGLLRenderItem_guess() {
            if(sourceTexture) {
                sourceTexture->Release();
            }
        }
    };

    tTVPRect privateMotionGLLSourceRect_guess(
        const PrivateMotionGLLRenderItem_guess &item) {
        return tTVPRect(
            item.sourceRect[0], item.sourceRect[1],
            item.sourceRect[2], item.sourceRect[3]);
    }

    const motion::detail::MeshPoint *
    privateMotionGLLPointsBegin_guess(
        const PrivateMotionGLLRenderItem_guess &item) {
        return item.points.data();
    }

    std::size_t privateMotionGLLPointCount_guess(
        const PrivateMotionGLLRenderItem_guess &item) {
        return item.points.size();
    }

    unsigned int privateMotionGLLPackedColorWithOpacity_guess(
        const PrivateMotionGLLRenderItem_guess &item) {
        const auto rgb = item.packedColors[0] == 0xff808080u
            ? 0x00ffffffu
            : (item.packedColors[0] & 0x00ffffffu);
        return rgb | ((item.opacity & 0xffu) << 24u);
    }

    std::array<tTVPPointD, 6> privateMotionGLLAffineTargetQuad_guess(
        const PrivateMotionGLLRenderItem_guess &item,
        float xOffset,
        float yOffset) {
        std::array<tTVPPointD, 6> out{};
        const auto *points = privateMotionGLLPointsBegin_guess(item);
        const tTVPPointD p0{
            static_cast<double>(points[0].x + xOffset),
            static_cast<double>(points[0].y + yOffset)};
        const tTVPPointD p1{
            static_cast<double>(points[1].x + xOffset),
            static_cast<double>(points[1].y + yOffset)};
        const tTVPPointD p2{
            static_cast<double>(points[2].x + xOffset),
            static_cast<double>(points[2].y + yOffset)};
        const tTVPPointD p3{p1.x + p2.x - p0.x, p1.y + p2.y - p0.y};
        out = {{p0, p1, p2, p1, p2, p3}};
        return out;
    }

    std::array<tTVPPointD, 6> privateMotionGLLAffineSourceQuad_guess(
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

    std::vector<tTVPPointD> privateMotionGLLOffsetMeshPoints_guess(
        const PrivateMotionGLLRenderItem_guess &item,
        float xOffset,
        float yOffset) {
        std::vector<tTVPPointD> out;
        const auto *points = privateMotionGLLPointsBegin_guess(item);
        const auto count = privateMotionGLLPointCount_guess(item);
        out.reserve(count);
        if(xOffset == 0.0f && yOffset == 0.0f) {
            for(std::size_t i = 0; i < count; ++i) {
                out.push_back({
                    static_cast<double>(points[i].x),
                    static_cast<double>(points[i].y)});
            }
            return out;
        }
        for(std::size_t i = 0; i < count; ++i) {
            out.push_back({
                static_cast<double>(points[i].x + xOffset),
                static_cast<double>(points[i].y + yOffset)});
        }
        return out;
    }

    bool operatePrivateMotionGLLAffine_guess(
        iTVPRenderMethod *method,
        tTVPBaseTexture *targetBitmap,
        const tTVPRect &targetRect,
        const PrivateMotionGLLRenderItem_guess &item,
        iTVPTexture2D *sourceTexture,
        float xOffset,
        float yOffset) {
        const auto sourceRect = privateMotionGLLSourceRect_guess(item);
        if(sourceRect.is_empty()) {
            return false;
        }
        if(sourceRect.left < 0 || sourceRect.top < 0 ||
           sourceRect.right > static_cast<int>(sourceTexture->GetWidth()) ||
           sourceRect.bottom > static_cast<int>(sourceTexture->GetHeight())) {
            TVPThrowExceptionMessage(TVPOutOfRectangle);
        }

        auto *referenceTexture = targetBitmap->GetTexture();
        auto *targetTexture = targetBitmap->GetTextureForRender(
            method->IsBlendTarget(), &targetRect);
        tTVPRect clippedRect(targetRect);
        clippedRect.left = std::max(clippedRect.left, 0);
        clippedRect.top = std::max(clippedRect.top, 0);
        clippedRect.right = std::min(
            clippedRect.right, static_cast<int>(targetTexture->GetWidth()));
        clippedRect.bottom = std::min(
            clippedRect.bottom, static_cast<int>(targetTexture->GetHeight()));
        if(clippedRect.is_empty()) {
            return false;
        }

        auto dst = privateMotionGLLAffineTargetQuad_guess(
            item, xOffset, yOffset);
        auto src = privateMotionGLLAffineSourceQuad_guess(
            sourceRect);
        tRenderTexQuadArray::Element srcTex[] = {
            tRenderTexQuadArray::Element(sourceTexture, src.data())
        };
        TVPGetRenderManager()->OperateTriangles(
            method, 2, targetTexture, referenceTexture, clippedRect,
            dst.data(), tRenderTexQuadArray(srcTex));
        return true;
    }

    bool operatePrivateMotionGLLMesh_guess(
        iTVPRenderMethod *method,
        tTVPBaseTexture *targetBitmap,
        const tTVPRect &targetRect,
        const PrivateMotionGLLRenderItem_guess &item,
        iTVPTexture2D *sourceTexture,
        const std::vector<tTVPPointD> &boundsPoints,
        const std::vector<tTVPPointD> &meshPoints) {
        const auto sourceRect = privateMotionGLLSourceRect_guess(item);
        tTVPRect computedBounds(targetRect);
        return motion::render_backend_guess::
            buildAndSubmitMeshTriangles_guess(
                computedBounds, sourceTexture, sourceRect,
                boundsPoints, meshPoints, item.meshDivX, item.meshDivY,
                [&](iTVPTexture2D *submittedSourceTexture,
                    const std::vector<tTVPPointD> &sourceVertices,
                    const std::vector<tTVPPointD> &destinationVertices) {
                    auto *referenceTexture = targetBitmap->GetTexture();
                    auto *targetTexture = targetBitmap->GetTextureForRender(
                        method->IsBlendTarget(), &targetRect);
                    tRenderTexQuadArray::Element srcTex[] = {
                        tRenderTexQuadArray::Element(
                            submittedSourceTexture, sourceVertices.data())
                    };
                    TVPGetRenderManager()->OperateTriangles(
                        method,
                        static_cast<int>(destinationVertices.size() / 3u),
                        targetTexture, referenceTexture, targetRect,
                        destinationVertices.data(),
                        tRenderTexQuadArray(srcTex));
                });
    }

    class tTJSNI_PrivateMotionGLLayer_guess final : public tTJSNI_Layer {
    public:
        tTJSNI_PrivateMotionGLLayer_guess() {
            Type = ltAlpha;
        }

        ~tTJSNI_PrivateMotionGLLayer_guess() override = default;

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
            ParentRectToChildRect(rect);
            tTVPRect ignoredDrawTargetRect;
            UpdateBitmapForChild = target->GetDrawTargetBitmap(
                targetRect, ignoredDrawTargetRect);
            SetFace(dfAuto);

#if defined(KRKR2_WASMTIME_HEADLESS)
            motion::detail::motionTracePrivateMotionGLLDraw(
                this, static_cast<int>(_renderQueue.size()),
                rect.left, rect.top, rect.right, rect.bottom,
                targetRect.left, targetRect.top,
                targetRect.right, targetRect.bottom, visiblecheck);
#endif

            tTVPRect clipRect(ClipRect);
            clipRect.set_offsets(x, y);
            const bool stencilEnabled = _stencilCount >= 1;
            motion::render_backend_guess::beginStencil_guess(
                UpdateBitmapForChild->GetTexture(), stencilEnabled);
            const float xOffset = static_cast<float>(x) - 0.5f;
            const float yOffset = static_cast<float>(y) - 0.5f;

            for(const auto &item : _renderQueue) {
                auto *sourceTexture = item.sourceTexture;
                if(!sourceTexture) {
                    break;
                }
                const auto packedColor =
                    privateMotionGLLPackedColorWithOpacity_guess(item);
                auto *method = item.stencilRefFromItem22 != 0
                    ? motion::render_backend_guess::
                          selectAlphaTestRenderMethod_guess(
                              item.blendMode & 0x0f, packedColor, false)
                    : motion::render_backend_guess::selectRenderMethod_guess(
                          item.blendMode & 0x0f, packedColor, false);
                if(!method) {
                    continue;
                }

                motion::render_backend_guess::applyStencilState_guess(
                    item.stencilRefFromItem22,
                    item.stencilRefFromItem23);
                switch(item.geometryType) {
                    case 0:
                        operatePrivateMotionGLLAffine_guess(
                            method, UpdateBitmapForChild, clipRect, item,
                            sourceTexture, xOffset, yOffset);
                        break;
                    case 1: {
                        const auto controlPoints =
                            privateMotionGLLOffsetMeshPoints_guess(
                                item, xOffset, yOffset);
                        const auto meshPoints = motion::render_backend_guess::
                            tessellateBezierPatch_guess(
                                controlPoints, item.meshDivX, item.meshDivY);
                        operatePrivateMotionGLLMesh_guess(
                            method, UpdateBitmapForChild, clipRect, item,
                            sourceTexture, controlPoints, meshPoints);
                        break;
                    }
                    case 2: {
                        const auto meshPoints =
                            privateMotionGLLOffsetMeshPoints_guess(
                            item, xOffset, yOffset);
                        operatePrivateMotionGLLMesh_guess(
                            method, UpdateBitmapForChild, clipRect, item,
                            sourceTexture, meshPoints, meshPoints);
                        break;
                    }
                    default:
                        break;
                }
            }

            motion::render_backend_guess::endStencil_guess(stencilEnabled);
            ResetClip();
        }

        void clearRenderQueue_guess() {
            _renderQueue.clear();
        }

        void setStencilCount_guess(tjs_int value) {
            _stencilCount = value;
        }

        void appendRenderItem_guess(
            const motion::PrivateMotionGLLRenderItemInput_guess &input,
            std::vector<motion::detail::MeshPoint> *pointsToSwap) {
            _renderQueue.emplace_back(input);
            auto &item = _renderQueue.back();
            if(input.geometryType == 0) {
                item.points.push_back(input.affinePoints[0]);
                item.points.push_back(input.affinePoints[1]);
                item.points.push_back(input.affinePoints[2]);
            } else if(input.geometryType == 1 || input.geometryType == 2) {
                item.points.swap(*pointsToSwap);
                item.meshDivX = input.meshDivX;
                item.meshDivY = input.meshDivY;
            }
        }

        std::size_t renderQueueSize_guess() const {
            return _renderQueue.size();
        }

    private:
        tjs_int _stencilCount;
        std::deque<PrivateMotionGLLRenderItem_guess> _renderQueue;
    };

    tTJSNI_PrivateMotionGLLayer_guess *
    resolvePrivateMotionGLLNativeInternal_guess(iTJSDispatch2 *object) {
        if(!object ||
           g_PrivateMotionGLLClassId_guess ==
               static_cast<tjs_uint32>(-1)) {
            return nullptr;
        }
        tTJSNI_PrivateMotionGLLayer_guess *layer = nullptr;
        if(TJS_FAILED(object->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, g_PrivateMotionGLLClassId_guess,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return nullptr;
        }
        return layer;
    }

    tTJSNI_PrivateMotionGLLayer_guess *
    resolvePrivateMotionGLLNativeUnchecked_guess(iTJSDispatch2 *object) {
        tTJSNI_PrivateMotionGLLayer_guess *layer = nullptr;
        (void)object->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, g_PrivateMotionGLLClassId_guess,
            reinterpret_cast<iTJSNativeInstance **>(&layer));
        return layer;
    }

    tjs_error PrivateMotionGLL_constructor_guess(
        tTJSVariant * /*result*/,
        tjs_int numparams,
        tTJSVariant **param,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeUnchecked_guess(objthis);
        return self->Construct(numparams, param, objthis);
    }

    tjs_error PrivateMotionGLL_setSize_guess(
        tTJSVariant * /*result*/,
        tjs_int numparams,
        tTJSVariant **param,
        iTJSDispatch2 *objthis) {
        if(numparams < 2) {
            return TJS_E_BADPARAMCOUNT;
        }
        auto *self = resolvePrivateMotionGLLNativeUnchecked_guess(objthis);
        self->SetSize(static_cast<tjs_int>(*param[0]),
                      static_cast<tjs_int>(*param[1]));
        return TJS_S_OK;
    }

    tjs_error PrivateMotionGLL_getVisible_guess(
        tTJSVariant *result,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeUnchecked_guess(objthis);
        if(result) {
            *result = self->GetVisible();
        }
        return TJS_S_OK;
    }

    tjs_error PrivateMotionGLL_setVisible_guess(
        const tTJSVariant *param,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeUnchecked_guess(objthis);
        self->SetVisible(*param);
        return TJS_S_OK;
    }

    tjs_error PrivateMotionGLL_getAbsolute_guess(
        tTJSVariant *result,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeUnchecked_guess(objthis);
        if(result) {
            *result = static_cast<tjs_int>(self->GetAbsoluteOrderIndex());
        }
        return TJS_S_OK;
    }

    tjs_error PrivateMotionGLL_setAbsolute_guess(
        const tTJSVariant *param,
        iTJSDispatch2 *objthis) {
        auto *self = resolvePrivateMotionGLLNativeUnchecked_guess(objthis);
        self->SetAbsoluteOrderIndex(static_cast<tjs_int>(*param));
        return TJS_S_OK;
    }

    class tTJSNC_PrivateMotionGLLayer_guess final : public tTJSNativeClass {
    public:
        tTJSNC_PrivateMotionGLLayer_guess()
            : tTJSNativeClass(privateMotionGLLClassName()) {
            const tjs_char *className = privateMotionGLLClassName();
            g_PrivateMotionGLLClassId_guess =
                TJSRegisterNativeClass(className);
            SetClassID(g_PrivateMotionGLLClassId_guess);

            TJSNativeClassRegisterNCM(
                this, className,
                TJSCreateNativeClassConstructor(
                    PrivateMotionGLL_constructor_guess),
                className, nitMethod);
            TJSNativeClassRegisterNCM(
                this, TJS_W("setSize"),
                TJSCreateNativeClassMethod(PrivateMotionGLL_setSize_guess),
                className, nitMethod);
            TJSNativeClassRegisterNCM(
                this, TJS_W("visible"),
                TJSCreateNativeClassProperty(
                    PrivateMotionGLL_getVisible_guess,
                    PrivateMotionGLL_setVisible_guess),
                className, nitProperty);
            TJSNativeClassRegisterNCM(
                this, TJS_W("absolute"),
                TJSCreateNativeClassProperty(
                    PrivateMotionGLL_getAbsolute_guess,
                    PrivateMotionGLL_setAbsolute_guess),
                className, nitProperty);
        }

    protected:
        tTJSNativeInstance *CreateNativeInstance() override {
            return new tTJSNI_PrivateMotionGLLayer_guess();
        }
    };

    tTJSNC_PrivateMotionGLLayer_guess *
    privateMotionGLLClassDispatch_guess() {
        static auto *klass =
            new tTJSNC_PrivateMotionGLLayer_guess();
        return klass;
    }

    iTJSDispatch2 *createPrivateMotionGLLObject_guess(
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant) {
        auto *layerClass = privateMotionGLLClassDispatch_guess();
        iTJSDispatch2 *global = TVPGetScriptDispatch();
        iTJSDispatch2 *created = nullptr;
        tTJSVariant *args[] = {
            const_cast<tTJSVariant *>(&ownerVariant),
            const_cast<tTJSVariant *>(&targetLayerVariant),
        };
        (void)layerClass->CreateNew(0, nullptr, nullptr, &created,
                                    2, args, global);
        global->Release();
        return created;
    }

} // namespace

namespace motion {

    iTJSDispatch2 *ensurePrivateMotionGLL_guess(
        SeparateLayerAdaptor &sla) {
        auto *targetLayer = tTJSNI_Layer::FromVariant(sla._targetLayer);
        tTJSNI_PrivateMotionGLLayer_guess *privateLayer = nullptr;
        if(sla._privateTarget.Type() == tvtVoid) {
            iTJSDispatch2 *created =
                createPrivateMotionGLLObject_guess(
                    sla._owner, sla._targetLayer);
            sla._privateTarget = tTJSVariant(created, created);
            created->Release();
            privateLayer = resolvePrivateMotionGLLNativeUnchecked_guess(
                sla._privateTarget.AsObjectNoAddRef());
            privateLayer->SetAbsoluteOrderIndex(sla.getAbsolute());
            privateLayer->SetVisible(true);
        } else {
            privateLayer = resolvePrivateMotionGLLNativeUnchecked_guess(
                sla._privateTarget.AsObjectNoAddRef());
        }

        privateLayer->SetSize(targetLayer->GetWidth(), targetLayer->GetHeight());
        return sla._privateTarget.AsObjectNoAddRef();
    }

    tTJSNI_BaseLayer *resolvePrivateMotionGLLNative_guess(
        iTJSDispatch2 *object) {
        return resolvePrivateMotionGLLNativeInternal_guess(object);
    }

    tTJSNI_BaseLayer *queryPrivateMotionGLLNativeFromVariant_guess(
        const tTJSVariant &value) {
        // SourceCache's four Variant helpers perform a strict Object
        // conversion, initialize only the output slot, ignore the returned
        // tjs_error, and return whatever GETINSTANCE wrote.  Keep this
        // boundary separate from the raw-object resolver used by the private
        // Layer class's own callbacks.
        auto *object = value.AsObjectNoAddRef();
        tTJSNI_PrivateMotionGLLayer_guess *layer = nullptr;
        (void)object->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, g_PrivateMotionGLLClassId_guess,
            reinterpret_cast<iTJSNativeInstance **>(&layer));
        return layer;
    }

    void clearPrivateMotionGLLRenderQueue_guess(iTJSDispatch2 *object) {
        if(auto *layer = resolvePrivateMotionGLLNativeInternal_guess(
               object)) {
            layer->clearRenderQueue_guess();
        }
    }

    void setPrivateMotionGLLStencilCount_guess(
        iTJSDispatch2 *object,
        tjs_int value) {
        if(auto *layer = resolvePrivateMotionGLLNativeInternal_guess(
               object)) {
            layer->setStencilCount_guess(value);
        }
    }

    void appendPrivateMotionGLLRenderItem_guess(
        iTJSDispatch2 *object,
        const PrivateMotionGLLRenderItemInput_guess &item,
        std::vector<detail::MeshPoint> *pointsToSwap) {
        if(auto *layer = resolvePrivateMotionGLLNativeInternal_guess(
               object)) {
            layer->appendRenderItem_guess(item, pointsToSwap);
        }
    }

    std::size_t privateMotionGLLRenderQueueSize_guess(
        iTJSDispatch2 *object) {
        if(auto *layer = resolvePrivateMotionGLLNativeInternal_guess(
               object)) {
            return layer->renderQueueSize_guess();
        }
        return 0;
    }

} // namespace motion
