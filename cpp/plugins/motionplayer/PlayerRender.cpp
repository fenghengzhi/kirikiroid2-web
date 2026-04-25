// PlayerRender.cpp — Drawing/rendering: renderToLayer, draw
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "ConfigManager/IndividualConfigManager.h"

using namespace motion::internal;

namespace {

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject);

    bool packedColorsAreDefault(std::uint32_t c0, std::uint32_t c1,
                                std::uint32_t c2, std::uint32_t c3) {
        return c0 == 0xFF808080u && c1 == 0xFF808080u && c2 == 0xFF808080u &&
            c3 == 0xFF808080u;
    }

    bool packedColorsAreOpaqueWhite(std::uint32_t c0, std::uint32_t c1,
                                    std::uint32_t c2, std::uint32_t c3) {
        return (c0 & c1 & c2 & c3) == 0xFFFFFFFFu;
    }

    std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    std::shared_ptr<tTVPBaseBitmap> cloneBitmap32(const tTVPBaseBitmap &src) {
        auto copy = std::make_shared<tTVPBaseBitmap>(
            static_cast<tjs_uint>(src.GetWidth()),
            static_cast<tjs_uint>(src.GetHeight()), 32);
        for(tjs_uint y = 0; y < src.GetHeight(); ++y) {
            const auto *srcRow = static_cast<const std::uint8_t *>(
                src.GetScanLine(y));
            auto *dstRow = static_cast<std::uint8_t *>(
                copy->GetScanLineForWrite(y));
            std::memcpy(dstRow, srcRow,
                        static_cast<size_t>(src.GetWidth()) * 4u);
        }
        return copy;
    }

    void applyPackedCornerTintLike_0x6A7518(
        tTVPBaseBitmap &bitmap,
        const std::array<std::uint32_t, 4> &packedColors,
        bool halfAlphaBlend) {
        const auto c0 = packedColors[0];
        const auto c1 = packedColors[1];
        const auto c2 = packedColors[2];
        const auto c3 = packedColors[3];
        if(packedColorsAreDefault(c0, c1, c2, c3) ||
           packedColorsAreOpaqueWhite(c0, c1, c2, c3)) {
            return;
        }

        const auto topLeft = unpackPackedRgba(c0);
        const auto topRight = unpackPackedRgba(c1);
        const auto bottomRight = unpackPackedRgba(c2);
        const auto bottomLeft = unpackPackedRgba(c3);
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if(width <= 0 || height <= 0) {
            return;
        }

        const int colorDivisor = halfAlphaBlend ? 128 : 255;
        const int spanX = std::max(width - 1, 1);
        const int spanY = std::max(height - 1, 1);
        const auto lerpChannel = [](int a, int b, int pos, int span) -> int {
            if(span <= 0) {
                return a;
            }
            return a + (pos * (b - a)) / span;
        };

        for(int y = 0; y < height; ++y) {
            auto *row = static_cast<std::uint8_t *>(
                bitmap.GetScanLineForWrite(static_cast<tjs_uint>(y)));
            const int rowLeftR =
                lerpChannel(topLeft[0], bottomLeft[0], y, spanY);
            const int rowLeftG =
                lerpChannel(topLeft[1], bottomLeft[1], y, spanY);
            const int rowLeftB =
                lerpChannel(topLeft[2], bottomLeft[2], y, spanY);
            const int rowLeftA =
                lerpChannel(topLeft[3], bottomLeft[3], y, spanY);
            const int rowRightR =
                lerpChannel(topRight[0], bottomRight[0], y, spanY);
            const int rowRightG =
                lerpChannel(topRight[1], bottomRight[1], y, spanY);
            const int rowRightB =
                lerpChannel(topRight[2], bottomRight[2], y, spanY);
            const int rowRightA =
                lerpChannel(topRight[3], bottomRight[3], y, spanY);

            for(int x = 0; x < width; ++x) {
                auto *dst = row + static_cast<size_t>(x) * 4u;
                const int tintR =
                    lerpChannel(rowLeftR, rowRightR, x, spanX);
                const int tintG =
                    lerpChannel(rowLeftG, rowRightG, x, spanX);
                const int tintB =
                    lerpChannel(rowLeftB, rowRightB, x, spanX);
                const int tintA =
                    lerpChannel(rowLeftA, rowRightA, x, spanX);
                dst[2] = static_cast<std::uint8_t>(std::min(
                    255, tintR * static_cast<int>(dst[2]) / colorDivisor));
                dst[1] = static_cast<std::uint8_t>(std::min(
                    255, tintG * static_cast<int>(dst[1]) / colorDivisor));
                dst[0] = static_cast<std::uint8_t>(std::min(
                    255, tintB * static_cast<int>(dst[0]) / colorDivisor));
                dst[3] = static_cast<std::uint8_t>(std::min(
                    255, tintA * static_cast<int>(dst[3]) / 255));
    }

}
    }

    iTJSDispatch2 *resolveLayerTreeOwnerObject(iTJSDispatch2 *object) {
        if(!object) {
            return nullptr;
        }

        tTJSVariant objectVar(object, object);
        tTJSVariant value;
        if(getObjectProperty(objectVar, TJS_W("layerTreeOwnerInterface"), value) &&
           value.Type() != tvtVoid) {
            return object;
        }

        if(getObjectProperty(objectVar, TJS_W("window"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef()) {
            return value.AsObjectNoAddRef();
        }

        if(auto *resolvedLayer = tryResolveLayerDispatch(objectVar);
           resolvedLayer && resolvedLayer != object) {
            return resolveLayerTreeOwnerObject(resolvedLayer);
        }

        return nullptr;
    }

    iTJSDispatch2 *resolvePrimaryLayerObject(iTJSDispatch2 *layerTreeOwnerObject) {
        if(!layerTreeOwnerObject) {
            return nullptr;
        }

        tTJSVariant ownerVar(layerTreeOwnerObject, layerTreeOwnerObject);
        tTJSVariant primaryVar;
        if(!getObjectProperty(ownerVar, TJS_W("primaryLayer"), primaryVar) ||
           primaryVar.Type() != tvtObject || !primaryVar.AsObjectNoAddRef()) {
            return nullptr;
        }

        if(auto *resolved = tryResolveLayerDispatch(primaryVar)) {
            return resolved;
        }
        return primaryVar.AsObjectNoAddRef();
    }

    iTJSDispatch2 *resolveMainWindowOwnerObject() {
        if(!TVPMainWindow) {
            return nullptr;
        }
        auto *owner = TVPMainWindow->GetOwnerNoAddRef();
        if(owner) {
            return owner;
        }

        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global) {
            return nullptr;
        }

        tTJSVariant windowClassVar;
        tTJSVariant mainWindowVar;
        iTJSDispatch2 *resolved = nullptr;
        if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("Window"), nullptr,
                                         &windowClassVar, global)) &&
           windowClassVar.Type() == tvtObject &&
           windowClassVar.AsObjectNoAddRef() &&
           TJS_SUCCEEDED(windowClassVar.AsObjectNoAddRef()->PropGet(
               0, TJS_W("mainWindow"), nullptr, &mainWindowVar,
               windowClassVar.AsObjectNoAddRef())) &&
           mainWindowVar.Type() == tvtObject &&
           mainWindowVar.AsObjectNoAddRef()) {
            resolved = mainWindowVar.AsObjectNoAddRef();
        }

        global->Release();
        return resolved;
    }

    iTJSDispatch2 *resolveMainWindowPrimaryLayerObject() {
        return resolvePrimaryLayerObject(resolveMainWindowOwnerObject());
    }

    void pushGraphicCandidates(std::vector<ttstr> &candidates,
                               const ttstr &base) {
        if(base.IsEmpty()) {
            return;
        }

        candidates.push_back(base);
        const auto raw = motion::detail::narrow(base);
        if(raw.find('.') != std::string::npos) {
            return;
        }

        static const char *exts[] = { ".png",  ".webp", ".jpg", ".jpeg",
                                      ".bmp",  ".tlg",  ".pimg", ".psb" };
        for(const auto *ext : exts) {
            candidates.emplace_back(base + ttstr{ ext });
        }
    }

    // Try to resolve a source image path for the given source name in the
    // motion snapshot. Uses the same candidate generation logic as
    // loadMotionSourceImage but without OpenCV.
    ttstr resolveMotionSourcePath(
        const motion::detail::MotionSnapshot &snapshot,
        const std::string &source) {
        if(source.empty() || isMotionCrossReference(source)) {
            return {};
        }

        std::vector<ttstr> candidates;
        const auto sourcePath = motion::detail::widen(source);
        candidates.push_back(sourcePath);
        pushGraphicCandidates(candidates, sourcePath);
        motion::detail::appendEmbeddedSourceCandidates(snapshot, source, candidates);
        for(const auto &alias : snapshot.resourceAliases) {
            const auto embeddedBase = ttstr{ TJS_W("psb://") } +
                motion::detail::widen(alias) + TJS_W("/") + sourcePath;
            pushGraphicCandidates(candidates, embeddedBase);
        }

        // PSB motion resources are stored in a tree like:
        //   source/<group>/<subgroup>/<name>/pixel
        // but motion layers reference them as:
        //   src/<group>/<name>
        // Scan resourcesByPath for matching resource paths.
        {
            const auto lastSlash = source.rfind('/');
            const auto baseName = (lastSlash != std::string::npos)
                ? source.substr(lastSlash + 1) : source;

            for(const auto &[resPath, _] : snapshot.resourcesByPath) {
                const auto targetSuffix = "/" + baseName + "/pixel";
                if(resPath.size() >= targetSuffix.size() &&
                   resPath.compare(resPath.size() - targetSuffix.size(),
                                   targetSuffix.size(), targetSuffix) == 0) {
                        for(const auto &alias : snapshot.resourceAliases) {
                            const auto psbPath = ttstr{ TJS_W("psb://") } +
                            motion::detail::widen(alias) + TJS_W("/") +
                            motion::detail::widen(resPath);
                        pushGraphicCandidates(candidates, psbPath);
                    }
                }
            }
        }

        std::unordered_set<std::string> seen;
        for(const auto &candidate : candidates) {
            const auto candidateKey = motion::detail::narrow(candidate);
            if(!seen.insert(candidateKey).second || candidate.IsEmpty()) {
                continue;
            }
            if(candidateKey.rfind("psb://", 0) == 0) {
                if(TVPIsExistentStorage(candidate)) {
                    return candidate;
                }
                continue;
            }
            if(const auto placed = TVPGetPlacedPath(candidate);
               !placed.IsEmpty()) {
                return placed;
            }
        }
        return {};
    }

    iTJSDispatch2 *createLayerObject(iTJSDispatch2 *layerTreeOwnerObject,
                                     iTJSDispatch2 *parentLayerObject) {
        if(!layerTreeOwnerObject) {
            return nullptr;
        }

        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global) {
            return nullptr;
        }

        tTJSVariant layerClassVar;
        iTJSDispatch2 *created = nullptr;
        const bool haveLayerClass =
            TJS_SUCCEEDED(global->PropGet(
                0, TJS_W("Layer"), nullptr, &layerClassVar, global))
            && layerClassVar.Type() == tvtObject
            && layerClassVar.AsObjectNoAddRef();
        if(haveLayerClass) {
            tTJSVariant ownerVar(layerTreeOwnerObject, layerTreeOwnerObject);
            tTJSVariant parentVar =
                parentLayerObject ? tTJSVariant(parentLayerObject, parentLayerObject)
                                  : tTJSVariant();
            tTJSVariant *args[] = { &ownerVar, &parentVar };
            if(TJS_FAILED(layerClassVar.AsObjectNoAddRef()->CreateNew(
                   0, nullptr, nullptr, &created, 2, args,
                   layerClassVar.AsObjectNoAddRef()))) {
                created = nullptr;
            }
        }

        global->Release();
        return created;
    }

    bool configureReusableLayerObject(iTJSDispatch2 *layerObject,
                                      iTJSDispatch2 *parentLayerObject,
                                      tTVPLayerType layerType,
                                      bool visible,
                                      bool absoluteOrderMode) {
        auto *layer = resolveNativeLayer(layerObject);
        if(!layer) {
            return false;
        }

        if(parentLayerObject) {
            if(auto *parentLayer = resolveNativeLayer(parentLayerObject);
               parentLayer && layer->GetParent() != parentLayer) {
                layer->SetParent(parentLayer);
            }
        }

        layer->SetType(layerType);
        layer->SetAbsoluteOrderMode(absoluteOrderMode);
        layer->SetVisible(visible);
        return true;
    }

    iTJSDispatch2 *ensureReusableLayerObject(tTJSVariant &slot,
                                             iTJSDispatch2 *layerTreeOwnerObject,
                                             iTJSDispatch2 *parentLayerObject,
                                             tTVPLayerType layerType,
                                             bool visible,
                                             bool absoluteOrderMode = false) {
        if(!layerTreeOwnerObject && parentLayerObject) {
            layerTreeOwnerObject = resolveLayerTreeOwnerObject(parentLayerObject);
        }
        if(!parentLayerObject && layerTreeOwnerObject) {
            parentLayerObject = resolvePrimaryLayerObject(layerTreeOwnerObject);
        }

        iTJSDispatch2 *layerObject =
            slot.Type() == tvtObject ? slot.AsObjectNoAddRef() : nullptr;
        if(!layerObject) {
            layerObject = createLayerObject(layerTreeOwnerObject, parentLayerObject);
            if(!layerObject) {
                return nullptr;
            }
            slot = tTJSVariant(layerObject, layerObject);
            layerObject->Release();
            layerObject = slot.AsObjectNoAddRef();
        }

        if(!configureReusableLayerObject(layerObject, parentLayerObject,
                                         layerType, visible,
                                         absoluteOrderMode)) {
            return nullptr;
        }
        return layerObject;
    }

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject) {
        if(!layerObject) {
            return nullptr;
        }
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(layerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return nullptr;
        }
        return layer;
    }

    bool queryLayerCanvasSize(iTJSDispatch2 *layerObject, int &width, int &height) {
        width = 0;
        height = 0;
        if(auto *layer = resolveNativeLayer(layerObject)) {
            width = static_cast<int>(layer->GetWidth());
            height = static_cast<int>(layer->GetHeight());
            if(width <= 0 || height <= 0) {
                width = static_cast<int>(layer->GetImageWidth());
                height = static_cast<int>(layer->GetImageHeight());
            }
        }
        return width > 0 && height > 0;
    }

    bool setObjectIntProperty(iTJSDispatch2 *object, const tjs_char *name,
                              tjs_int value) {
        if(!object) {
            return false;
        }
        tTJSVariant var(value);
        return TJS_SUCCEEDED(
            object->PropSet(TJS_MEMBERENSURE, name, nullptr, &var, object));
    }

    bool prepareLayerForRender(iTJSDispatch2 *layerObject,
                               int width, int height,
                               tjs_uint32 clearColor) {
        auto *layer = resolveNativeLayer(layerObject);
        if(!layer || width <= 0 || height <= 0) {
            return false;
        }

        if(!layer->GetHasImage()) {
            layer->SetHasImage(true);
        }
        layer->SetImageSize(static_cast<tjs_uint>(width),
                            static_cast<tjs_uint>(height));
        layer->SetSize(width, height);
        layer->SetClip(0, 0, width, height);
        tTVPRect rect(0, 0, width, height);
        layer->FillRect(rect, clearColor);
        return true;
    }

    std::string summarizeLayerChildren(tTJSNI_BaseLayer *layer, int maxChildren = 12) {
        if(!layer) {
            return "<null-layer>";
        }
        int visibleChildren = 0;
        const auto totalChildren = static_cast<int>(layer->GetCount());
        for(int i = 0; i < totalChildren; ++i) {
            auto *child = layer->GetChildren(i);
            if(child && child->GetVisible() && child->GetOpacity() != 0) {
                ++visibleChildren;
            }
        }
        std::string summary = fmt::format(
            "children={} visibleChildren={} selfVisible={} opacity={}",
            totalChildren, visibleChildren,
            layer->GetVisible() ? 1 : 0, layer->GetOpacity());
        const int count = std::min(totalChildren, maxChildren);
        for(int i = 0; i < count; ++i) {
            auto *child = layer->GetChildren(i);
            if(!child) {
                continue;
            }
            summary += fmt::format(
                " | [{}] ptr={} name={} vis={} nodeVis={} opacity={} size={}x{}",
                i,
                static_cast<const void *>(child),
                child->GetName().AsStdString(),
                child->GetVisible() ? 1 : 0,
                child->GetNodeVisible() ? 1 : 0,
                child->GetOpacity(),
                child->GetWidth(),
                child->GetHeight());
        }
        return summary;
    }

    tTVPBlendOperationMode resolveBlendOperationModeLike_0x6C7440(
        int rawBlendMode) {
        // libkrkr2.so 0x6C7440 does not pass the raw item blend flag through to
        // operateRect. It first maps the low 4 bits to the final TVP blend
        // operation mode: 1->0xE, 2/5->0xF, 3->0x10, 4->0x11, and the raw 0 /
        // default path ultimately composites with mode 2 in the common case.
        switch(rawBlendMode & 0x0F) {
            case 1:
                return omPsAdditive;       // 0xE
            case 2:
            case 5:
                return omPsSubtractive;    // 0xF
            case 3:
                return omPsMultiplicative; // 0x10
            case 4:
                return omPsScreen;         // 0x11
            case 0:
            default:
                return omAlpha;            // 0x2
        }
    }

    bool shouldUseDirectRenderPathLike_0x6C7440(
        const motion::detail::PlayerRuntime::RenderCommand &command) {
        const unsigned lowNibble =
            static_cast<unsigned>(command.blendMode) & 0x0Fu;
        return !command.clearEnabled &&
            command.visibleAncestorIndex < 0 &&
            (lowNibble == 0u || lowNibble > 5u);
    }

    std::array<tTVPPointD, 3> buildAffineTrianglePoints(
        const std::array<float, 8> &corners,
        float xOffset,
        float yOffset) {
        return {{
            { static_cast<double>(corners[0] + xOffset),
              static_cast<double>(corners[1] + yOffset) },
            { static_cast<double>(corners[2] + xOffset),
              static_cast<double>(corners[3] + yOffset) },
            { static_cast<double>(corners[6] + xOffset),
              static_cast<double>(corners[7] + yOffset) },
        }};
    }

    std::vector<tTVPPointD> buildMeshPoints(
        const std::vector<float> &points,
        float xOffset,
        float yOffset) {
        std::vector<tTVPPointD> result;
        result.reserve(points.size() / 2u);
        for(size_t i = 0; i + 1 < points.size(); i += 2) {
            result.push_back({
                static_cast<double>(points[i] + xOffset),
                static_cast<double>(points[i + 1] + yOffset),
            });
        }
        return result;
    }

    motion::D3DAdaptor *ensureSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject) {
        static std::unique_ptr<motion::D3DAdaptor> s_sharedAdaptor;
        if(!s_sharedAdaptor) {
            s_sharedAdaptor = std::make_unique<motion::D3DAdaptor>();
        }

        int width = 0;
        int height = 0;
        if(auto *layer = resolveNativeLayer(targetLayerObject)) {
            width = static_cast<int>(layer->GetWidth());
            height = static_cast<int>(layer->GetHeight());
            if(width <= 0 || height <= 0) {
                width = static_cast<int>(layer->GetImageWidth());
                height = static_cast<int>(layer->GetImageHeight());
            }
        }

        if(width > 0 && height > 0) {
            if(s_sharedAdaptor->getWidth() != width ||
               s_sharedAdaptor->getHeight() != height) {
                s_sharedAdaptor->setSize(width, height);
            }
        }
        s_sharedAdaptor->setVisible(true);
        return s_sharedAdaptor.get();
    }

    struct RenderClipRect {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    bool computeRenderClipRect(
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry,
                               int canvasWidth, int canvasHeight,
                               RenderClipRect &out,
                               std::string *failureReason = nullptr) {
        float clipLeft = std::max(0.0f, entry.paintBox[0]);
        float clipTop = std::max(0.0f, entry.paintBox[1]);
        float clipRight = std::min(static_cast<float>(canvasWidth), entry.paintBox[2]);
        float clipBottom = std::min(static_cast<float>(canvasHeight), entry.paintBox[3]);

        if(entry.hasViewport && entry.viewport[2] >= entry.viewport[0]
           && entry.viewport[3] >= entry.viewport[1]) {
            clipLeft = std::max(clipLeft, floorf(entry.viewport[0]));
            clipTop = std::max(clipTop, floorf(entry.viewport[1]));
            clipRight = std::min(clipRight, ceilf(entry.viewport[2]));
            clipBottom = std::min(clipBottom, ceilf(entry.viewport[3]));
        }

        if(!(clipLeft < clipRight && clipTop < clipBottom)) {
            if(failureReason) {
                *failureReason = fmt::format(
                    "invalid_intersection paintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] viewport={} canvas=[0,0,{},{}]",
                    entry.paintBox[0], entry.paintBox[1], entry.paintBox[2],
                    entry.paintBox[3],
                    entry.hasViewport
                        ? fmt::format("[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                      entry.viewport[0], entry.viewport[1],
                                      entry.viewport[2], entry.viewport[3])
                        : std::string("<invalid default>"),
                    canvasWidth, canvasHeight);
            }
            return false;
        }

        out.left = static_cast<int>(clipLeft);
        out.top = static_cast<int>(clipTop);
        out.right = static_cast<int>(clipRight);
        out.bottom = static_cast<int>(clipBottom);
        if(failureReason) {
            failureReason->clear();
        }
        return out.left < out.right && out.top < out.bottom;
    }

    bool isAccurateSlaRenderEnabled() {
        auto *config = IndividualConfigManager::GetInstance();
        if(!config) {
            return false;
        }
        return config->GetValue<bool>("ogl_accurate_render", false);
    }

    tTVPRect localRectFromCommand(
        const motion::detail::PlayerRuntime::RenderCommand &command) {
        return tTVPRect(0, 0,
                        command.clipRect[2] - command.clipRect[0],
                        command.clipRect[3] - command.clipRect[1]);
    }

    bool clearLayerAlphaOutsideRect(tTJSNI_BaseLayer *layer,
                                    const tTVPRect &outerRect,
                                    const tTVPRect &innerRect) {
        if(!layer || !layer->GetMainImage()) {
            return false;
        }
        auto *bmp = layer->GetMainImage();
        if(outerRect.left >= outerRect.right || outerRect.top >= outerRect.bottom) {
            return true;
        }

        auto clearMask = [&](const tTVPRect &rect) {
            if(rect.left < rect.right && rect.top < rect.bottom) {
                bmp->FillMask(rect, 0);
            }
        };

        clearMask(tTVPRect(outerRect.left, outerRect.top,
                           innerRect.left, outerRect.bottom));
        clearMask(tTVPRect(innerRect.right, outerRect.top,
                           outerRect.right, outerRect.bottom));
        clearMask(tTVPRect(std::max(outerRect.left, innerRect.left),
                           outerRect.top,
                           std::min(outerRect.right, innerRect.right),
                           innerRect.top));
        clearMask(tTVPRect(std::max(outerRect.left, innerRect.left),
                           innerRect.bottom,
                           std::min(outerRect.right, innerRect.right),
                           outerRect.bottom));
        return true;
    }

    bool applyMotionAlphaMaskLike_0x6AF104(
        iTJSDispatch2 *dstLayerObject,
        int dstX,
        int dstY,
        iTJSDispatch2 *srcLayerObject,
        int srcX,
        int srcY,
        int width,
        int height,
        int threshold,
        int playerStencilType,
        int itemFlags,
        const std::string &motionPath,
        double frameTime,
        int dstNodeIndex,
        int srcNodeIndex) {
        auto *dstLayer = resolveNativeLayer(dstLayerObject);
        auto *srcLayer = resolveNativeLayer(srcLayerObject);
        if(!dstLayer || !srcLayer || !dstLayer->GetHasImage() ||
           !srcLayer->GetHasImage() || !dstLayer->GetMainImage() ||
           !srcLayer->GetMainImage()) {
            return false;
        }

        auto *dstBmp = dstLayer->GetMainImage();
        auto *srcBmp = srcLayer->GetMainImage();
        const auto &dstClip = dstLayer->GetClip();
        const int dstImageWidth = static_cast<int>(dstLayer->GetImageWidth());
        const int dstImageHeight = static_cast<int>(dstLayer->GetImageHeight());
        const int srcImageWidth = static_cast<int>(srcLayer->GetImageWidth());
        const int srcImageHeight = static_cast<int>(srcLayer->GetImageHeight());

        const int requestedLeft = dstX;
        const int requestedTop = dstY;
        const int requestedRight = dstX + width;
        const int requestedBottom = dstY + height;

        if(dstClip.left > dstX) {
            srcX += dstClip.left - dstX;
            width -= dstClip.left - dstX;
            dstX = dstClip.left;
        }
        if(dstClip.top > dstY) {
            srcY += dstClip.top - dstY;
            height -= dstClip.top - dstY;
            dstY = dstClip.top;
        }
        if(srcX < 0) {
            dstX -= srcX;
            width += srcX;
            srcX = 0;
        }
        if(srcY < 0) {
            dstY -= srcY;
            height += srcY;
            srcY = 0;
        }

        const int dstLimitRight =
            std::min(dstClip.right, dstImageWidth);
        const int dstLimitBottom =
            std::min(dstClip.bottom, dstImageHeight);
        if(dstX + width > dstLimitRight) {
            width = dstLimitRight - dstX;
        }
        if(dstY + height > dstLimitBottom) {
            height = dstLimitBottom - dstY;
        }
        if(srcX + width > srcImageWidth) {
            width = srcImageWidth - srcX;
        }
        if(srcY + height > srcImageHeight) {
            height = srcImageHeight - srcY;
        }

        const tTVPRect requestedRect(
            std::max(requestedLeft, dstClip.left),
            std::max(requestedTop, dstClip.top),
            std::min(requestedRight, dstLimitRight),
            std::min(requestedBottom, dstLimitBottom));
        const tTVPRect overlapRect(dstX, dstY, dstX + width, dstY + height);

        if((itemFlags & 3) == 1) {
            clearLayerAlphaOutsideRect(dstLayer, requestedRect, overlapRect);
        }

        if(width <= 0 || height <= 0) {
            return true;
        }

        const bool thresholdMaskMode = playerStencilType == 0;
        for(int y = 0; y < height; ++y) {
            auto *dstRow =
                static_cast<std::uint8_t *>(dstBmp->GetScanLineForWrite(dstY + y));
            const auto *srcRow =
                static_cast<const std::uint8_t *>(srcBmp->GetScanLine(srcY + y));
            for(int x = 0; x < width; ++x) {
                auto *dstPixel = dstRow + (dstX + x) * 4;
                const auto *srcPixel = srcRow + (srcX + x) * 4;
                const auto srcAlpha = static_cast<int>(srcPixel[3]);
                auto &dstAlpha = dstPixel[3];
                switch(itemFlags) {
                    case 1:
                        if(thresholdMaskMode) {
                            if(srcAlpha < threshold) {
                                dstAlpha = 0;
                            }
                        } else {
                            dstAlpha = static_cast<std::uint8_t>(
                                (static_cast<int>(dstAlpha) * srcAlpha) / 255);
                        }
                        break;
                    case 2:
                        if(thresholdMaskMode) {
                            if(srcAlpha >= threshold) {
                                dstAlpha = 0;
                            }
                        } else {
                            dstAlpha = static_cast<std::uint8_t>(
                                ((255 - srcAlpha) * static_cast<int>(dstAlpha)) / 255);
                        }
                        break;
                    case 5:
                    case 6:
                        if(thresholdMaskMode) {
                            if(srcAlpha >= threshold) {
                                dstAlpha = 255;
                            }
                        } else {
                            dstAlpha = static_cast<std::uint8_t>(
                                srcAlpha +
                                ((255 - srcAlpha) * static_cast<int>(dstAlpha)) / 255);
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        motion::detail::logoChainTraceLogf(
            motionPath, "execute.mask", "0x6AF104", frameTime,
            "dstNode={} srcNode={} itemFlags={} playerStencilType={} threshold={} requested=[{},{},{},{}] overlap=[{},{},{},{}]",
            dstNodeIndex, srcNodeIndex, itemFlags, playerStencilType, threshold,
            requestedRect.left, requestedRect.top,
            requestedRect.right, requestedRect.bottom,
            overlapRect.left, overlapRect.top,
            overlapRect.right, overlapRect.bottom);
        return true;
    }

} // namespace

namespace motion {

    // --- Drawing/rendering ---
    void Player::setClearColor(tjs_int color) { _runtime->clearColor = color; }

    void Player::setResizable(bool v) { _runtime->resizable = v; }

    void Player::removeAllTextures() { _runtime->sourcesByKey.clear(); }

    void Player::removeAllBg() { _runtime->backgrounds.clear(); }

    void Player::removeAllCaption() { _runtime->captions.clear(); }

    void Player::registerBg(tTJSVariant bg) { _runtime->backgrounds.push_back(bg); }

    void Player::registerCaption(tTJSVariant caption) {
        _runtime->captions.push_back(caption);
    }

    void Player::unloadUnusedTextures() {}

    tjs_int Player::alphaOpAdd() { return ++_runtime->alphaOpCounter; }

    tTJSVariant Player::captureCanvas() {
        if(_runtime->lastCanvas.Type() == tvtVoid) {
            draw();
        }
        return _runtime->lastCanvas;
    }

    // Aligned to libkrkr2.so Player_initVariables (0x6CD750). Called
    // synchronously from the play path after Player_buildNodeTree (0x6B51F0)
    // and before the (flags & Chain) playback-state gate. Reads the PSB
    // "variable" array (from Player+528 == activeMotion->root) and appends
    // one VariableLabelEntry per dict entry:
    //   name  <- entry["scope"] split by ':' (right half), or empty
    //   label <- entry["label"]
    //   flag68/flag124 <- 1 (binary default; semantics not yet reversed)
    void Player::initVariables() {
        if(!_runtime) {
            return;
        }
        _runtime->variableLabelEntries.clear();
        if(!_runtime->activeMotion || !_runtime->activeMotion->root) {
            return;
        }

        const auto &root = _runtime->activeMotion->root;
        const auto variableList = std::dynamic_pointer_cast<PSB::PSBList>(
            (*root)["variable"]);
        if(!variableList) {
            return;
        }

        for(const auto &item : *variableList) {
            const auto entryDic =
                std::dynamic_pointer_cast<PSB::PSBDictionary>(item);
            if(!entryDic) {
                continue;
            }

            detail::VariableLabelEntry entry;

            if(const auto scopeVal = (*entryDic)["scope"]) {
                if(const auto scopeStr = std::dynamic_pointer_cast<
                       PSB::PSBString>(scopeVal)) {
                    const auto &scope = scopeStr->value;
                    const auto colon = scope.find(':');
                    if(colon != std::string::npos) {
                        entry.name = detail::widen(scope.substr(colon + 1));
                    }
                }
            }
            if(const auto labelVal = (*entryDic)["label"]) {
                if(const auto labelStr = std::dynamic_pointer_cast<
                       PSB::PSBString>(labelVal)) {
                    entry.label = detail::widen(labelStr->value);
                }
            }

            _runtime->variableLabelEntries.push_back(std::move(entry));
        }
    }

    // Aligned to libkrkr2.so Player_buildNodeTree (0x6B51F0). The binary calls
    // this unconditionally from Player_initNonEmoteMotion (0x6B365C) after
    // Player_loadMotion succeeds — there is no lazy gate. The caller is
    // responsible for having loaded the motion first; we keep a minimal null
    // check so calls on a Player without a loaded motion become a no-op
    // instead of crashing, but we do NOT call ensureMotionLoaded here.
    void Player::buildNodeTree() {
        if(!_runtime || !_runtime->activeMotion) {
            return;
        }

        std::string clipLabel;
        const auto *clip = selectActiveClip();
        loadParameterEntriesForClipLike_0x6B365C(clip);
        if(clip != nullptr) {
            clipLabel = clip->label;
        }

        if(_runtime->activeMotion &&
           detail::logoSnapshotMarkEnabledForPath(_runtime->activeMotion->path) &&
           _runtime->activeMotion->path.find("m2logo.mtn") != std::string::npos) {
            std::fprintf(
                stderr,
                "SNAPCLIP motion=%s motionKey=%s clipLabel=%s playing=%s clipCount=%zu\n",
                _runtime->activeMotion->path.c_str(),
                detail::narrow(_motionKey).c_str(),
                clipLabel.empty() ? "<none>" : clipLabel.c_str(),
                _runtime->playingTimelineLabels.empty()
                    ? "<none>"
                    : _runtime->playingTimelineLabels.front().c_str(),
                _runtime->activeMotion->clipList.size());
        }

        _runtime->nodes = detail::buildNodeTree(*_runtime->activeMotion, clipLabel,
                                                &_resourceManagerNative,
                                                _completionType);
        for(const auto &node : _runtime->nodes) {
            if(node.parameterizeIndex >= 0) {
                (void)resolveNodeParameterEntry(*_runtime, node);
            }
        }

        // Aligned to Player_initNodeFields case 3 (0x6B43C0..0x6B4688):
        // child motion players inherit the parent's resource manager context
        // (ctor arg a1+124) and random generator state (copy from a1+1012).
        for(auto &node : _runtime->nodes) {
            if(node.nodeType != 3) {
                continue;
            }
            if(auto *child = node.getChildPlayer()) {
                child->_resourceManagerNative = _resourceManagerNative;
                child->_tjsRandomGenerator = _tjsRandomGenerator;
                child->_project = _project.Type() == tvtObject
                    ? _project
                    : (_runtime->activeMotion
                           ? _runtime->activeMotion->moduleValue
                           : tTJSVariant{});
            }
        }

        if(!_runtime->nodes.empty()) {
            auto &root = _runtime->nodes[0];
            // Aligned to libkrkr2.so Player_setRootFlipX/X/Y
            // (0x6CD028/0x6CD048/0x6CD068): these setters write the delta block
            // at node+1584..+1660, not the local post-interpolation mirror.
            root.delta.flipX = _rootFlipX;
            if(_hasPendingRootPos) {
                root.delta.posX = _pendingRootX;
                root.delta.posY = _pendingRootY;
            }
            root.delta.dirty = true;
        }

        _runtime->nodeLabelMap.clear();
        for(size_t ni = 0; ni < _runtime->nodes.size(); ++ni) {
            const auto &label = _runtime->nodes[ni].layerName;
            if(!label.empty()) {
                _runtime->nodeLabelMap.emplace(label, static_cast<int>(ni));
            }
        }

        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
            const auto &motionPath = _runtime->activeMotion->path;
            detail::logoChainTraceLogf(
                motionPath, "buildNodeTree", "0x6B51F0", _clampedEvalTime,
                "clipLabel={} rootLayers={} nodeCount={}",
                clipLabel.empty() ? std::string("<root>") : clipLabel,
                _runtime->activeMotion->layerList.size(), _runtime->nodes.size());
            for(const auto &node : _runtime->nodes) {
                const bool hasStencilTypeKey =
                    node.psbNode && static_cast<bool>((*node.psbNode)["stencilType"]);
                detail::logoChainTraceLogf(
                    motionPath, "buildNodeTree.node", "0x6B51F0",
                    _clampedEvalTime,
                    "nodeIndex={} label={} type={} parent={} hasSource={} meshType={} inheritFlags=0x{:x} parameterizeIndex={} objTriPriority={} parentClipIndex={} stencilType={} hasStencilTypeKey={}",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType, node.parentIndex, node.hasSource ? 1 : 0,
                    node.meshType, node.inheritFlags, node.parameterizeIndex,
                    node.objTriPriority,
                    node.parentClipIndex,
                    node.stencilType, hasStencilTypeKey ? 1 : 0);
            }
        }
    }

    bool Player::renderViaSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject) {
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

        if(!renderToD3DAdaptor(sharedAdaptor)) {
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
        _runtime->lastCanvas = tTJSVariant(resolvedTarget, resolvedTarget);
        return true;
    }

    bool Player::buildRenderCommands(tjs_int canvasWidth, tjs_int canvasHeight) {
        // Equivalent to sub_6D5164 @ 0x6D5178's `player+544` null gate —
        // the port has no explicit +544 mirror, so an absent runtime is
        // the canonical "no render list yet" signal.
        if(!_runtime) {
            return false;
        }

        _runtime->renderCommands.clear();
        _runtime->renderCommandsTopLevel.clear();
        _runtime->renderCommandsGroup.clear();
        const auto motionPath =
            _runtime->activeMotion ? _runtime->activeMotion->path : std::string{};
        for(const auto &entry : _runtime->preparedRenderItems) {
            // Render gate — aligned to sub_6C7440 @ 0x6C7440 (libkrkr2.so).
            // Path A main iteration does NOT read drawFlag (node+1960 /
            // item+19); that field is a Path B product (sub_6BD8DC) used by
            // Player_calcBounds and type3 propagate. Membership in mainList
            // (i.e. being present in preparedRenderItems, gated by nodeType
            // mask 0x1441/0x1449 upstream) is the render decision.
            if(entry.opacity <= 0) {
                continue;
            }

            detail::PlayerRuntime::RenderCommand command;
            command.nodeIndex = entry.nodeIndex;
            command.srcRef = entry.srcRef;
            command.sourceKey = entry.sourceKey;
            command.hasOwnSource = entry.hasOwnSource;
            command.groupOnly = entry.groupOnly;
            command.topLevelList = entry.topLevelList;
            command.groupList = entry.groupList;
            command.rawFlag16 = entry.rawFlag16;
            command.rawFlag17 = entry.skipFlag0;
            command.rawFlag18 = !entry.skipFlag1;
            command.rawFlag19 = entry.drawFlag;
            command.rawFlag20 = false;
            command.rawFlag21 = false;
            command.blendMode = entry.blendMode;
            command.contextVariant = entry.contextVariant;
            command.opacity = entry.opacity;
            command.itemFlags = entry.stencilComposite;
            command.parentNodeIndex = entry.visibleAncestorIndex;
            command.preparedItem = &entry;
            command.packedColors = entry.packedColors;
            command.visibleAncestorIndex = entry.visibleAncestorIndex;
            command.clearEnabled = _clearEnabled;
            command.meshType = entry.meshType;
            command.meshDivX = entry.meshDivX;
            command.meshDivY = entry.meshDivY;
            command.layerId = entry.layerId;
            command.layerId2 = entry.layerId2;
            command.worldCorners = entry.corners;
            command.worldMeshPoints = entry.meshPoints;
            RenderClipRect clipRect;
            std::string clipFailureReason;
            if(!computeRenderClipRect(entry, canvasWidth, canvasHeight, clipRect,
                                      &clipFailureReason)) {
                detail::logoChainTraceCheck(
                    motionPath, "renderCommand.clip", "0x6C4E28",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox∩viewport∩canvas exp paintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] viewport={} canvas=[0,0,{},{}]",
                        entry.paintBox[0], entry.paintBox[1], entry.paintBox[2],
                        entry.paintBox[3],
                        entry.hasViewport
                            ? fmt::format("[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                          entry.viewport[0], entry.viewport[1],
                                          entry.viewport[2], entry.viewport[3])
                            : std::string("<invalid default>"),
                        canvasWidth, canvasHeight),
                    fmt::format("nodeIndex={} act=<invalid:{}>",
                                entry.nodeIndex, clipFailureReason),
                    false,
                    "sub_6C4E28 produced an invalid local clip rect");
            } else {
                command.rawFlag21 = true;
                command.clipRect = {
                    clipRect.left, clipRect.top, clipRect.right, clipRect.bottom
                };
                command.dirtyRect = command.clipRect;

                for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                    command.localCorners[ci] =
                        entry.corners[ci] - 0.5f - static_cast<float>(clipRect.left);
                    command.localCorners[ci + 1] =
                        entry.corners[ci + 1] - 0.5f - static_cast<float>(clipRect.top);
                }

                command.localMeshPoints.reserve(entry.meshPoints.size());
                for(size_t pi = 0; pi + 1 < entry.meshPoints.size(); pi += 2) {
                    command.localMeshPoints.push_back(
                        entry.meshPoints[pi] - 0.5f -
                        static_cast<float>(clipRect.left));
                    command.localMeshPoints.push_back(
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
                                 command.localCorners[ci]) > 0.01f ||
                       std::fabs(expectedLocalCorners[ci + 1] -
                                 command.localCorners[ci + 1]) > 0.01f) {
                        cornersOk = false;
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "renderCommand.clip", "0x6C4E28",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox∩viewport∩canvas exp=[{},{},{},{}]",
                        clipRect.left, clipRect.top, clipRect.right,
                        clipRect.bottom),
                    fmt::format(
                        "nodeIndex={} act=[{},{},{},{}]",
                        entry.nodeIndex, command.clipRect[0],
                        command.clipRect[1], command.clipRect[2],
                        command.clipRect[3]),
                    true,
                    "sub_6C4E28 clip rect diverged from expected intersection");
                detail::logoChainTraceCheck(
                    motionPath, "renderCommand.localCorners", "0x6C4E28",
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
                        command.localCorners[0], command.localCorners[1],
                        command.localCorners[2], command.localCorners[3],
                        command.localCorners[4], command.localCorners[5],
                        command.localCorners[6], command.localCorners[7]),
                    cornersOk,
                    "sub_6C4E28 local corner translation diverged from clip-local expectation");
            }

            _runtime->renderCommands.push_back(std::move(command));
        }

        std::unordered_map<const detail::PlayerRuntime::PreparedRenderItem *,
                           detail::PlayerRuntime::RenderCommand *>
            commandByPreparedItem;
        commandByPreparedItem.reserve(_runtime->renderCommands.size());
        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            _runtime->renderCommands[i].childCommandPtrs.clear();
            _runtime->renderCommands[i].parentCommand = nullptr;
            _runtime->renderCommands[i].leafBuilt = false;
            _runtime->renderCommands[i].composedBuilt = false;
            _runtime->renderCommands[i].executedDirect = false;
            _runtime->renderCommands[i].builtRect = {0, 0, 0, 0};
            commandByPreparedItem.emplace(_runtime->renderCommands[i].preparedItem,
                                          &_runtime->renderCommands[i]);
        }
        _runtime->renderCommandsTopLevel.reserve(_runtime->renderCommands.size());
        _runtime->renderCommandsGroup.reserve(_runtime->renderCommands.size());
        for(auto &command : _runtime->renderCommands) {
            if(command.groupList) {
                _runtime->renderCommandsGroup.push_back(&command);
            }
            if(command.topLevelList) {
                _runtime->renderCommandsTopLevel.push_back(&command);
            }
        }
        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            const auto *preparedItem = _runtime->renderCommands[i].preparedItem;
            if(!preparedItem || !preparedItem->parentItem) {
                continue;
            }
            if(const auto it =
                   commandByPreparedItem.find(preparedItem->parentItem);
               it != commandByPreparedItem.end()) {
                auto &parentCommand = *it->second;
                auto &childCommand = _runtime->renderCommands[i];
                parentCommand.childCommandPtrs.push_back(&childCommand);
                childCommand.parentCommand = &parentCommand;
                childCommand.hasRenderParent = true;
                childCommand.parentNodeIndex = parentCommand.nodeIndex;
            }
        }
        for(auto &command : _runtime->renderCommands) {
            const auto *preparedItem = command.preparedItem;
            if(!preparedItem) {
                continue;
            }
            command.childCommandPtrs.clear();
            for(auto *preparedChild : preparedItem->childItems) {
                if(!preparedChild) {
                    continue;
                }
                const auto it = commandByPreparedItem.find(preparedChild);
                if(it == commandByPreparedItem.end() || !it->second) {
                    continue;
                }
                if(it->second == &command) {
                    continue;
                }
                command.childCommandPtrs.push_back(it->second);
            }
        }
        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           _clampedEvalTime >= 43.0 && _clampedEvalTime <= 50.0) {
            for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
                const auto &command = _runtime->renderCommands[i];
                if(!(command.nodeIndex == 14 || command.nodeIndex == 15 ||
                     command.nodeIndex == 19 ||
                     (command.nodeIndex >= 20 && command.nodeIndex <= 29))) {
                    continue;
                }
                std::fprintf(
                    stderr,
                    "SNAPCMD frame=%.3f order=%zu nodeIndex=%d source=%s groupOnly=%d topLevel=%d groupList=%d rawFlags=[%d,%d,%d,%d,%d,%d] parentNodeIndex=%d hasRenderParent=%d childCount=%zu layerId=(%d,%d) clipRect=[%d,%d,%d,%d] opacity=%d blend=%d\n",
                    _clampedEvalTime,
                    i,
                    command.nodeIndex,
                    command.sourceKey.empty() ? "<none>" : command.sourceKey.c_str(),
                    command.groupOnly ? 1 : 0,
                    command.topLevelList ? 1 : 0,
                    command.groupList ? 1 : 0,
                    command.rawFlag16 ? 1 : 0,
                    command.rawFlag17 ? 1 : 0,
                    command.rawFlag18 ? 1 : 0,
                    command.rawFlag19 ? 1 : 0,
                    command.rawFlag20 ? 1 : 0,
                    command.rawFlag21 ? 1 : 0,
                    command.parentNodeIndex,
                    command.hasRenderParent ? 1 : 0,
                    command.childCommandPtrs.size(),
                    command.layerId,
                    command.layerId2,
                    command.clipRect[0], command.clipRect[1],
                    command.clipRect[2], command.clipRect[3],
                    command.opacity,
                    command.blendMode);
            }
        }

        detail::logoChainTraceLogf(
            motionPath, "renderCommand.count", "0x6C4E28",
            _clampedEvalTime,
            "canvas={}x{} preparedItems={} renderCommands={} topLevelList={} groupList={}",
            canvasWidth, canvasHeight, _runtime->preparedRenderItems.size(),
            _runtime->renderCommands.size(),
            _runtime->renderCommandsTopLevel.size(),
            _runtime->renderCommandsGroup.size());
        return !_runtime->renderCommands.empty();
    }

    bool Player::executeLayerRenderCommands(iTJSDispatch2 *renderLayerObject,
                                            bool skipUpdate) {
        if(!renderLayerObject || !_runtime || !_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        auto *renderLayer = resolveNativeLayer(renderLayerObject);
        iTJSDispatch2 *scratchOwner = resolveMainWindowOwnerObject();
        iTJSDispatch2 *scratchParent = resolveMainWindowPrimaryLayerObject();
        if(!scratchOwner) {
            scratchOwner = resolveLayerTreeOwnerObject(renderLayerObject);
        }
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
            "renderCommands={} renderLayer={} scratchOwner={} scratchParent={} skipUpdate={}",
            _runtime->renderCommands.size(),
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

        std::unordered_map<std::string, std::shared_ptr<tTVPBaseBitmap>>
            baseSourceCache;
        std::unordered_map<std::string, std::shared_ptr<tTVPBaseBitmap>>
            preparedSourceCache;
        auto resolveBaseSourceBitmap =
            [&](const detail::PlayerRuntime::RenderCommand &command)
                -> std::shared_ptr<tTVPBaseBitmap> {
            if(command.sourceKey.empty()) {
                return nullptr;
            }
            if(auto it = baseSourceCache.find(command.sourceKey);
               it != baseSourceCache.end()) {
                return it->second;
            }

            std::shared_ptr<tTVPBaseBitmap> srcBmp;
            std::string sourceOrigin("unresolved");
            const auto resolvedPath =
                resolveMotionSourcePath(*_runtime->activeMotion, command.sourceKey);
            if(!resolvedPath.IsEmpty()) {
                ttstr loadPath = resolvedPath;
                const auto pathString = detail::narrow(resolvedPath);
                if(pathString.rfind('.') == std::string::npos ||
                   pathString.rfind('.') < pathString.rfind('/')) {
                    loadPath = resolvedPath + TJS_W(".png");
                }
                try {
                    auto bmp = std::make_shared<tTVPBaseBitmap>(1, 1, 32);
                    TVPLoadGraphic(bmp.get(), loadPath, TVP_clNone, 0, 0,
                                   glmNormal, nullptr, nullptr);
                    if(bmp->GetWidth() > 0 && bmp->GetHeight() > 0) {
                        srcBmp = bmp;
                        sourceOrigin = detail::narrow(loadPath);
                    }
                } catch(...) {
                }
            }

            if(!srcBmp) {
                int width = 0;
                int height = 0;
                double originX = 0.0;
                double originY = 0.0;
                std::vector<std::uint8_t> decodedPixels;
                bool decodedPixelsAreBgra = false;
                const auto *resource = findPSBResourceBySourceName(
                    *_runtime->activeMotion, command.sourceKey, width, height,
                    decodedPixels, originX, originY, &decodedPixelsAreBgra);
                if(resource && width > 0 && height > 0 && !resource->data.empty()) {
                    const auto &pixelData =
                        decodedPixels.empty() ? resource->data : decodedPixels;
                    auto bmp = std::make_shared<tTVPBaseBitmap>(
                        static_cast<tjs_uint>(width),
                        static_cast<tjs_uint>(height), 32);
                    tTVPRect fillRect(0, 0, width, height);
                    bmp->Fill(fillRect, 0x00000000);
                    const auto *src = pixelData.data();
                    for(int y = 0; y < height; ++y) {
                        auto *row = static_cast<std::uint8_t *>(
                            bmp->GetScanLineForWrite(static_cast<tjs_uint>(y)));
                        for(int x = 0; x < width; ++x) {
                            const size_t sourceIndex =
                                (static_cast<size_t>(y) * width + x) * 4u;
                            if(sourceIndex + 3 >= pixelData.size()) {
                                break;
                            }
                            auto *dst = row + x * 4;
                            if(decodedPixelsAreBgra) {
                                dst[0] = src[sourceIndex + 0];
                                dst[1] = src[sourceIndex + 1];
                                dst[2] = src[sourceIndex + 2];
                            } else {
                                dst[0] = src[sourceIndex + 2];
                                dst[1] = src[sourceIndex + 1];
                                dst[2] = src[sourceIndex + 0];
                            }
                            dst[3] = src[sourceIndex + 3];
                        }
                    }
                    srcBmp = bmp;
                    sourceOrigin = fmt::format(
                        "psb:{}:{}x{}:origin=({:.3f},{:.3f}):bgra={}",
                        command.sourceKey, width, height, originX, originY,
                        decodedPixelsAreBgra ? 1 : 0);
                }
            }

            detail::logoChainTraceLogf(
                motionPath, "execute.source", "0x6C7440", _clampedEvalTime,
                "source={} resolve={} bitmap={}x{}",
                command.sourceKey.empty() ? std::string("<none>")
                                          : command.sourceKey,
                sourceOrigin,
                srcBmp ? srcBmp->GetWidth() : 0,
                srcBmp ? srcBmp->GetHeight() : 0);

            baseSourceCache.emplace(command.sourceKey, srcBmp);
            return srcBmp;
        };
        auto resolveSourceBitmap =
            [&](const detail::PlayerRuntime::RenderCommand &command)
                -> std::shared_ptr<tTVPBaseBitmap> {
            if(command.sourceKey.empty()) {
                return nullptr;
            }

            const bool useHalfAlphaTint =
                (command.blendMode & 0xF0) == 0x10;
            const auto tintKey = fmt::format(
                "{}|{:08x}|{:08x}|{:08x}|{:08x}|{}",
                command.sourceKey, command.packedColors[0],
                command.packedColors[1], command.packedColors[2],
                command.packedColors[3], useHalfAlphaTint ? 1 : 0);
            if(auto it = preparedSourceCache.find(tintKey);
               it != preparedSourceCache.end()) {
                return it->second;
            }

            auto srcBmp = resolveBaseSourceBitmap(command);
            if(!srcBmp) {
                preparedSourceCache.emplace(tintKey, nullptr);
                return nullptr;
            }

            const bool needsTint =
                !packedColorsAreDefault(command.packedColors[0],
                                        command.packedColors[1],
                                        command.packedColors[2],
                                        command.packedColors[3]) &&
                !packedColorsAreOpaqueWhite(command.packedColors[0],
                                            command.packedColors[1],
                                            command.packedColors[2],
                                            command.packedColors[3]);
            if(!needsTint) {
                preparedSourceCache.emplace(tintKey, srcBmp);
                return srcBmp;
            }

            auto tinted = cloneBitmap32(*srcBmp);
            applyPackedCornerTintLike_0x6A7518(*tinted, command.packedColors,
                                              useHalfAlphaTint);
            detail::logoChainTraceLogf(
                motionPath, "execute.sourceTint", "0x6C1B70/0x6A7518",
                _clampedEvalTime,
                "source={} halfAlphaTint={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] bitmap={}x{}",
                command.sourceKey, useHalfAlphaTint ? 1 : 0,
                command.packedColors[0], command.packedColors[1],
                command.packedColors[2], command.packedColors[3],
                tinted->GetWidth(), tinted->GetHeight());
            preparedSourceCache.emplace(tintKey, tinted);
            return tinted;
        };

        const int playerStencilType = _maskMode;
        auto ensureLeafCommandLayer =
            [&](detail::PlayerRuntime::RenderCommand &command) -> iTJSDispatch2 * {
            const tjs_int stateLayerId = command.layerId;
            if(stateLayerId == 0) {
                return ensureReusableLayerObject(
                    command.leafLayer,
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
                if(command.nodeIndex >= 0 &&
                   command.nodeIndex < static_cast<int>(_runtime->nodes.size())) {
                    const auto &node = _runtime->nodes[command.nodeIndex];
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
            command.rawFlag20 = true;

            setObjectIntProperty(layerObject, TJS_W("absolute"), state.absolute);
            setObjectIntProperty(layerObject, TJS_W("hitThreshold"),
                                 state.hitThreshold);

            state.clipRect = {
                static_cast<float>(command.clipRect[0]),
                static_cast<float>(command.clipRect[1]),
                static_cast<float>(command.clipRect[2]),
                static_cast<float>(command.clipRect[3])
            };
            state.worldRect = {
                command.worldCorners[0], command.worldCorners[1],
                command.worldCorners[4], command.worldCorners[5]
            };
            state.localRect = {
                command.localCorners[0], command.localCorners[1],
                command.localCorners[4], command.localCorners[5]
            };
            state.packedColors = command.packedColors;
            state.isDirty = true;

            command.leafLayer = state.layerObject;
            return layerObject;
        };
        auto ensureComposedCommandLayer =
            [&](detail::PlayerRuntime::RenderCommand &command) -> iTJSDispatch2 * {
            return ensureReusableLayerObject(
                command.composedLayer,
                scratchOwner,
                scratchParent,
                static_cast<tTVPLayerType>(ltAlpha),
                false);
        };
        auto renderCommandSourceToLayer =
            [&](detail::PlayerRuntime::RenderCommand &command,
                iTJSDispatch2 *targetLayerObject,
                tTJSNI_BaseLayer *targetLayer,
                const std::shared_ptr<tTVPBaseBitmap> &srcBmp,
                const tTVPRect &sourceRect,
                const char *branch) -> bool {
            if(!targetLayerObject || !targetLayer) {
                return false;
            }
            const int clipWidth = command.clipRect[2] - command.clipRect[0];
            const int clipHeight = command.clipRect[3] - command.clipRect[1];
            if(clipWidth <= 0 || clipHeight <= 0) {
                return false;
            }
            if(!prepareLayerForRender(targetLayerObject, clipWidth, clipHeight,
                                      0x00000000)) {
                return false;
            }
            if(!srcBmp || srcBmp->GetWidth() <= 0 || srcBmp->GetHeight() <= 0) {
                return true;
            }
            if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
               motionPath.find("m2logo.mtn") != std::string::npos &&
               _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                std::fprintf(
                    stderr,
                    "SNAPGEOM phase=leafSource frame=%.3f nodeIndex=%d source=%s meshType=%d layerSize=%dx%d sourceRect=[%d,%d,%d,%d] clipRect=[%d,%d,%d,%d] worldCorners=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f] localCorners=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]\n",
                    _clampedEvalTime,
                    command.nodeIndex,
                    command.sourceKey.empty() ? "<none>" : command.sourceKey.c_str(),
                    command.meshType,
                    clipWidth,
                    clipHeight,
                    sourceRect.left,
                    sourceRect.top,
                    sourceRect.right,
                    sourceRect.bottom,
                    command.clipRect[0],
                    command.clipRect[1],
                    command.clipRect[2],
                    command.clipRect[3],
                    command.worldCorners[0],
                    command.worldCorners[1],
                    command.worldCorners[2],
                    command.worldCorners[3],
                    command.worldCorners[4],
                    command.worldCorners[5],
                    command.worldCorners[6],
                    command.worldCorners[7],
                    command.localCorners[0],
                    command.localCorners[1],
                    command.localCorners[2],
                    command.localCorners[3],
                    command.localCorners[4],
                    command.localCorners[5],
                    command.localCorners[6],
                    command.localCorners[7]);
            }
            if(command.meshType == 0) {
                const auto localPts =
                    buildAffineTrianglePoints(command.localCorners, 0.0f, 0.0f);
                targetLayer->AffineCopy(localPts.data(), srcBmp.get(),
                                        sourceRect, stNearest,
                                        command.clearEnabled);
            } else {
                if(command.localMeshPoints.empty() || command.meshDivX < 2 ||
                   command.meshDivY < 2) {
                    return false;
                }
                auto localMeshPoints =
                    buildMeshPoints(command.localMeshPoints, 0.0f, 0.0f);
                if(command.meshType == 1) {
                    targetLayer->BezierPatchCopy(
                        localMeshPoints.data(), command.meshDivX,
                        command.meshDivY, srcBmp.get(), sourceRect, stNearest,
                        command.clearEnabled);
                } else if(command.meshType == 2) {
                    targetLayer->MeshCopy(localMeshPoints.data(),
                                          command.meshDivX, command.meshDivY,
                                          srcBmp.get(), sourceRect, stNearest,
                                          command.clearEnabled);
                } else {
                    return false;
                }
            }
            detail::logoChainTraceLogf(
                motionPath, "execute.layerSource", "0x6C7440", _clampedEvalTime,
                "branch={} nodeIndex={} clipRect=[{},{},{},{}] layer={}x{} clearEnabled={}",
                branch, command.nodeIndex,
                command.clipRect[0], command.clipRect[1],
                command.clipRect[2], command.clipRect[3],
                clipWidth, clipHeight, command.clearEnabled ? 1 : 0);
            return true;
        };
        auto chooseCommandOutputLayerObject =
            [&](detail::PlayerRuntime::RenderCommand &command) -> iTJSDispatch2 * {
            const bool preferLeafLayer = (command.itemFlags & 4) == 0;
            if(!preferLeafLayer &&
               command.composedLayer.Type() == tvtObject) {
                return command.composedLayer.AsObjectNoAddRef();
            }
            if(command.leafLayer.Type() == tvtObject) {
                return command.leafLayer.AsObjectNoAddRef();
            }
            if(command.composedLayer.Type() == tvtObject) {
                return command.composedLayer.AsObjectNoAddRef();
            }
            return nullptr;
        };

        auto buildCommandOutput = [&](auto &&self, size_t commandIndex) -> bool {
            auto &command = _runtime->renderCommands[commandIndex];
            if(command.executedDirect || command.leafBuilt || command.composedBuilt) {
                return true;
            }
            if(!command.rawFlag21) {
                return false;
            }

            const int clipWidth = command.clipRect[2] - command.clipRect[0];
            const int clipHeight = command.clipRect[3] - command.clipRect[1];
            if(clipWidth <= 0 || clipHeight <= 0) {
                return false;
            }

            auto srcBmp = resolveSourceBitmap(command);
            const bool hasSourceBitmap =
                srcBmp && srcBmp->GetWidth() > 0 && srcBmp->GetHeight() > 0;
            if(!hasSourceBitmap && command.childCommandPtrs.empty()) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.source", "0x6C7440",
                    _clampedEvalTime,
                    "resolved bitmap should exist with positive size",
                    fmt::format("nodeIndex={} source={} bitmap={}x{}",
                                command.nodeIndex, command.sourceKey,
                                srcBmp ? srcBmp->GetWidth() : 0,
                                srcBmp ? srcBmp->GetHeight() : 0),
                    false,
                    "sub_6C7440 could not resolve a drawable source bitmap");
                return false;
            }

            const tTVPRect sourceRect(
                0, 0,
                hasSourceBitmap ? static_cast<tjs_int>(srcBmp->GetWidth()) : 0,
                hasSourceBitmap ? static_cast<tjs_int>(srcBmp->GetHeight()) : 0);
            if(hasSourceBitmap) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.srcRect", "0x6C7440",
                    _clampedEvalTime,
                    fmt::format("full texture rect exp=[0,0,{},{}]",
                                srcBmp->GetWidth(), srcBmp->GetHeight()),
                    fmt::format("nodeIndex={} act=[{},{},{},{}]",
                                command.nodeIndex, sourceRect.left,
                                sourceRect.top, sourceRect.right,
                                sourceRect.bottom),
                    true,
                    "sub_6C7440 source rect was not the full texture bounds");
            }

            const bool hasChildren = !command.childCommandPtrs.empty();
            const bool useDirectRenderPath =
                shouldUseDirectRenderPathLike_0x6C7440(command) &&
                !hasChildren && command.parentCommand == nullptr;
            if(useDirectRenderPath) {
                command.executedDirect = true;
                command.builtRect = command.clipRect;
                return true;
            }

            iTJSDispatch2 *leafLayerObject = ensureLeafCommandLayer(command);
            auto *leafLayer = resolveNativeLayer(leafLayerObject);
            if(!leafLayerObject || !leafLayer) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.workLayer", "0x6C7440",
                    _clampedEvalTime,
                    "leaf layer should resolve for buffered item path",
                    fmt::format("nodeIndex={} leafLayer={}",
                                command.nodeIndex,
                                static_cast<const void *>(leafLayer)),
                    false,
                    "sub_6C7440 could not allocate the per-item leaf layer");
                return false;
            }

            if(!renderCommandSourceToLayer(command, leafLayerObject, leafLayer,
                                           srcBmp, sourceRect,
                                           "item.leaf.affineCopy")) {
                return false;
            }
            command.leafBuilt = true;
            command.builtRect = command.clipRect;

            bool hasBuiltChildren = false;
            for(auto *childCommand : command.childCommandPtrs) {
                if(!childCommand) {
                    continue;
                }
                const auto childCommandIndex = static_cast<size_t>(
                    childCommand - _runtime->renderCommands.data());
                if(childCommandIndex >= _runtime->renderCommands.size()) {
                    continue;
                }
                hasBuiltChildren =
                    self(self, childCommandIndex) || hasBuiltChildren;
            }

            if(!hasBuiltChildren) {
                return true;
            }

            iTJSDispatch2 *composedLayerObject =
                ensureComposedCommandLayer(command);
            auto *composedLayer = resolveNativeLayer(composedLayerObject);
            if(!composedLayerObject || !composedLayer) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.workLayer", "0x6C7440",
                    _clampedEvalTime,
                    "composed layer should resolve for parent item path",
                    fmt::format("nodeIndex={} composedLayer={}",
                                command.nodeIndex,
                                static_cast<const void *>(composedLayer)),
                    false,
                    "sub_6C7440 could not allocate the composed output layer");
                return false;
            }

            if(!prepareLayerForRender(composedLayerObject, clipWidth, clipHeight,
                                      0x00000000)) {
                return false;
            }
            if(command.leafBuilt) {
                const auto localRect = localRectFromCommand(command);
                composedLayer->CopyRect(0, 0, leafLayer->GetMainImage(),
                                        nullptr, localRect);
            }

            for(auto *childPtr : command.childCommandPtrs) {
                if(!childPtr) {
                    continue;
                }
                auto &child = *childPtr;
                if(!child.rawFlag21 || child.rawFlag16) {
                    continue;
                }
                if((command.itemFlags & 4) != 0) {
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
                        child.builtRect[0] - command.clipRect[0],
                        child.builtRect[1] - command.clipRect[1],
                        childMaskLayerObject,
                        0,
                        0,
                        childWidth,
                        childHeight,
                        64,
                        playerStencilType,
                        command.itemFlags,
                        motionPath,
                        _clampedEvalTime,
                        command.nodeIndex,
                        child.nodeIndex);
                    continue;
                }

                auto *childOutputLayerObject = chooseCommandOutputLayerObject(child);
                auto *childOutputLayer = resolveNativeLayer(childOutputLayerObject);
                if(!childOutputLayerObject || !childOutputLayer) {
                    continue;
                }
                const auto childLocalRect = localRectFromCommand(child);
                const auto childBlendMode =
                    resolveBlendOperationModeLike_0x6C7440(child.blendMode);
                const auto childOpacity = static_cast<tjs_int>(
                    std::clamp(child.opacity, 0, 255));
                if(childOpacity <= 0) {
                    continue;
                }
                composedLayer->OperateRect(
                    child.builtRect[0] - command.clipRect[0],
                    child.builtRect[1] - command.clipRect[1],
                    childOutputLayer->GetMainImage(),
                    childLocalRect,
                    childBlendMode,
                    childOpacity);
            }

            command.composedBuilt = true;
            return true;
        };

        for(auto *commandPtr : _runtime->renderCommandsTopLevel) {
            if(!commandPtr) {
                continue;
            }
            auto &command = *commandPtr;

                const auto blendMode =
                    resolveBlendOperationModeLike_0x6C7440(command.blendMode);
                const auto effectiveColor =
                    unpackPackedRgba(command.packedColors[0]);
                const auto opa = static_cast<tjs_int>(
                    std::clamp(command.opacity, 0, 255));
                if(opa <= 0) {
                    continue;
                }

                const auto commandIndex = static_cast<size_t>(
                    &command - _runtime->renderCommands.data());
                if(commandIndex >= _runtime->renderCommands.size()) {
                    continue;
                }
                if(!command.rawFlag21) {
                    continue;
                }
                // libkrkr2.so 0x6C7440 reads item+17/item+16/item+18 in the
                // top-level walk before it enters the per-item output path.
                // Keep the local order the same instead of eagerly building a
                // command that the native loop would have skipped.
                if(command.rawFlag17) {
                    continue;
                }
                if(command.rawFlag16) {
                    continue;
                }
                if(_preview && !command.rawFlag18) {
                    continue;
                }
                if(command.hasRenderParent) {
                    continue;
                }
                if(!buildCommandOutput(buildCommandOutput, commandIndex)) {
                    continue;
                }

                try {
                    if(command.executedDirect) {
                        auto srcBmp = resolveSourceBitmap(command);
                        if(!srcBmp || srcBmp->GetWidth() <= 0 ||
                           srcBmp->GetHeight() <= 0) {
                            continue;
                        }
                    const tTVPRect sourceRect(
                        0, 0, static_cast<tjs_int>(srcBmp->GetWidth()),
                        static_cast<tjs_int>(srcBmp->GetHeight()));
                    std::string branch("direct.operateAffine");
                    if(command.meshType == 0) {
                        const auto worldPts =
                            buildAffineTrianglePoints(command.worldCorners,
                                                     -0.5f, -0.5f);
                        renderLayer->OperateAffine(worldPts.data(), srcBmp.get(),
                                                   sourceRect, blendMode, opa,
                                                   stNearest);
                    } else {
                        if(command.worldMeshPoints.empty() ||
                           command.meshDivX < 2 || command.meshDivY < 2) {
                            continue;
                        }
                        auto worldMeshPoints =
                            buildMeshPoints(command.worldMeshPoints,
                                            -0.5f, -0.5f);
                        if(command.meshType == 1) {
                            branch = "direct.operateBezierPatch";
                            renderLayer->OperateBezierPatch(
                                worldMeshPoints.data(), command.meshDivX,
                                command.meshDivY, srcBmp.get(), sourceRect,
                                blendMode, opa, stNearest,
                                command.clearEnabled);
                        } else if(command.meshType == 2) {
                            branch = "direct.operateMesh";
                            renderLayer->OperateMesh(
                                worldMeshPoints.data(), command.meshDivX,
                                command.meshDivY, srcBmp.get(), sourceRect,
                                blendMode, opa, stNearest,
                                command.clearEnabled);
                        } else {
                            continue;
                        }
                    }
                    detail::logoChainTraceLogf(
                        motionPath, "execute.copy", "0x6C7440",
                        _clampedEvalTime,
                        "branch={} nodeIndex={} clipRect=[{},{},{},{}] dirtyRect=[{},{},{},{}] blendMode={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] visibleAncestorIndex={} clearEnabled={} renderPath=direct workLayer=0x0 renderLayer={}x{}",
                        branch, command.nodeIndex,
                        command.clipRect[0], command.clipRect[1],
                        command.clipRect[2], command.clipRect[3],
                        command.dirtyRect[0], command.dirtyRect[1],
                        command.dirtyRect[2], command.dirtyRect[3],
                        command.blendMode, opa,
                        command.packedColors[0], command.packedColors[1],
                        command.packedColors[2], command.packedColors[3],
                        effectiveColor[0], effectiveColor[1],
                        effectiveColor[2], effectiveColor[3],
                        command.visibleAncestorIndex,
                        command.clearEnabled ? 1 : 0,
                        renderLayer->GetWidth(), renderLayer->GetHeight());
                    if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                       motionPath.find("m2logo.mtn") != std::string::npos &&
                       _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                        std::fprintf(stderr,
                                     "SNAPCOPY order=%d frame=%.3f nodeIndex=%d source=%s branch=%s clipRect=[%d,%d,%d,%d] opacity=%d blend=%d\n",
                                     snapshotCopyOrder++, _clampedEvalTime,
                                     command.nodeIndex,
                                     command.sourceKey.empty()
                                         ? "<none>"
                                         : command.sourceKey.c_str(),
                                     branch.c_str(),
                                     command.clipRect[0], command.clipRect[1],
                                     command.clipRect[2], command.clipRect[3],
                                     opa, command.blendMode);
                    }
                        continue;
                    }

                    auto *outputLayerObject = chooseCommandOutputLayerObject(command);
                    auto *outputLayer = resolveNativeLayer(outputLayerObject);
                    if(!outputLayerObject || !outputLayer) {
                        continue;
                    }

                    const auto localRect = localRectFromCommand(command);
                    renderLayer->OperateRect(command.clipRect[0], command.clipRect[1],
                                             outputLayer->GetMainImage(), localRect,
                                             blendMode, opa);
                    detail::logoChainTraceLogf(
                        motionPath, "execute.copy", "0x6C7440", _clampedEvalTime,
                        "branch={} nodeIndex={} clipRect=[{},{},{},{}] dirtyRect=[{},{},{},{}] blendMode={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] visibleAncestorIndex={} clearEnabled={} renderPath=buffered outputLayer={}x{} renderLayer={}x{} childCount={} phase={}",
                        command.composedBuilt ? "buffered.operateRect.composed"
                                              : "buffered.operateRect.leaf",
                        command.nodeIndex,
                        command.clipRect[0], command.clipRect[1],
                        command.clipRect[2], command.clipRect[3],
                        command.dirtyRect[0], command.dirtyRect[1],
                        command.dirtyRect[2], command.dirtyRect[3],
                        command.blendMode, opa,
                        command.packedColors[0], command.packedColors[1],
                        command.packedColors[2], command.packedColors[3],
                        effectiveColor[0], effectiveColor[1],
                        effectiveColor[2], effectiveColor[3],
                        command.visibleAncestorIndex,
                        command.clearEnabled ? 1 : 0,
                        localRect.get_width(), localRect.get_height(),
                        renderLayer->GetWidth(), renderLayer->GetHeight(),
                        command.childCommandPtrs.size(),
                        0);
                    if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                       motionPath.find("m2logo.mtn") != std::string::npos &&
                       _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
                        const char *snapBranch = command.composedBuilt
                            ? "buffered.operateRect.composed"
                            : "buffered.operateRect.leaf";
                        std::fprintf(stderr,
                                     "SNAPCOPY order=%d frame=%.3f nodeIndex=%d source=%s branch=%s clipRect=[%d,%d,%d,%d] opacity=%d blend=%d childCount=%zu phase=%d\n",
                                     snapshotCopyOrder++, _clampedEvalTime,
                                     command.nodeIndex,
                                     command.sourceKey.empty()
                                         ? "<none>"
                                         : command.sourceKey.c_str(),
                                     snapBranch,
                                     command.clipRect[0], command.clipRect[1],
                                     command.clipRect[2], command.clipRect[3],
                                     opa, command.blendMode,
                                     command.childCommandPtrs.size(),
                                     0);
                }
            } catch(const eTJS &) {
            } catch(...) {
            }
        }

        if(!skipUpdate) {
            renderLayer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "execute.update", "0x6C7440", _clampedEvalTime,
                "renderLayer.Update(false) size={}x{}",
                renderLayer->GetWidth(), renderLayer->GetHeight());
        }
        return true;
    }

    iTJSDispatch2 *Player::resolveSeparateLayerRenderTarget(
        SeparateLayerAdaptor *sla,
        iTJSDispatch2 *fallbackOwner,
        int &canvasWidth,
        int &canvasHeight) {
        canvasWidth = 0;
        canvasHeight = 0;
        if(!sla) {
            return nullptr;
        }

        iTJSDispatch2 *targetLayerObject = nullptr;
        if(auto *resolved = tryResolveLayerDispatch(sla->getTargetLayer())) {
            targetLayerObject = resolved;
        }
        if(!targetLayerObject) {
            targetLayerObject = fallbackOwner;
        }
        if(!targetLayerObject) {
            return nullptr;
        }

        sla->setTargetLayer(tTJSVariant(targetLayerObject, targetLayerObject));
        if(!queryLayerCanvasSize(targetLayerObject, canvasWidth, canvasHeight)) {
            return nullptr;
        }

        iTJSDispatch2 *renderTarget = ensureReusableLayerObject(
            sla->privateRenderTargetSlot(),
            resolveLayerTreeOwnerObject(targetLayerObject),
            targetLayerObject,
            static_cast<tTVPLayerType>(ltAlpha),
            true,
            sla->getAbsolute());
        if(!renderTarget) {
            return nullptr;
        }

        sla->setPrivateRenderTarget(tTJSVariant(renderTarget, renderTarget));
        if(auto *renderLayer = resolveNativeLayer(renderTarget)) {
            renderLayer->SetSize(canvasWidth, canvasHeight);
            renderLayer->SetVisible(true);
        }
        return renderTarget;
    }

    bool Player::renderMotionFrameToTarget(iTJSDispatch2 *renderTargetObject,
                                           tjs_int canvasWidth,
                                           tjs_int canvasHeight,
                                           const char *traceFunc) {
        if(!renderTargetObject || canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        if(!prepareLayerForRender(renderTargetObject, canvasWidth, canvasHeight,
                                  0x00000000)) {
            return false;
        }

        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};
        detail::logoChainTraceLogf(
            motionPath, "sla.renderMotionFrame", "0x6DE738",
            _clampedEvalTime,
            "target={} canvas={}x{} route={}",
            static_cast<const void *>(renderTargetObject),
            canvasWidth, canvasHeight,
            traceFunc ? traceFunc : "0x6DE738");

        buildRenderCommands(canvasWidth, canvasHeight);
        return executeLayerRenderCommands(renderTargetObject, true);
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
        if(!_runtime->activeMotion) return false;
        const auto motionPath = _runtime->activeMotion->path;
        detail::logoChainTraceLogf(
            motionPath, "draw.d3d", "0x6D5B90", _clampedEvalTime,
            "adaptorSize={}x{} route=D3DAdaptor_renderFromPlayer",
            adaptor->getWidth(), adaptor->getHeight());

        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();

        iTJSDispatch2 *renderLayerObject =
            ensureReusableLayerObject(_runtime->internalRenderLayer,
                                      adaptor->getWindowObject(),
                                      nullptr,
                                      static_cast<tTVPLayerType>(ltAlpha),
                                      false);
        if(!renderLayerObject) {
            return false;
        }
        if(!prepareLayerForRender(renderLayerObject, adaptor->getWidth(),
                                  adaptor->getHeight(), 0x00000000)) {
            return false;
        }

        buildRenderCommands(adaptor->getWidth(), adaptor->getHeight());
        executeLayerRenderCommands(renderLayerObject, true);

        // D3D backend still ends with copying pixels into the adaptor buffer,
        // but it now consumes prepared items directly instead of recursing into
        // renderToLayer().
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(renderLayerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return false;
        }

        const int w = adaptor->getWidth();
        const int h = adaptor->getHeight();
        const int layerW = static_cast<int>(layer->GetImageWidth());
        const int layerH = static_cast<int>(layer->GetImageHeight());
        const auto *srcBuf = reinterpret_cast<const std::uint8_t *>(
            layer->GetMainImagePixelBuffer());
        auto srcPitch = layer->GetMainImagePixelBufferPitch();

        if(!srcBuf || srcPitch <= 0 || layerW <= 0 || layerH <= 0) return false;

        // Resize adaptor buffer if needed
        if(w != layerW || h != layerH) {
            adaptor->setSize(layerW, layerH);
        }
        adaptor->clearBuffer();

        auto *dstBuf = adaptor->getBuffer();
        const auto dstPitch = adaptor->getBufferPitch();
        const int copyH = std::min(layerH, adaptor->getHeight());
        const int copyRowBytes = std::min(
            static_cast<int>(layerW * 4), dstPitch);

        for(int y = 0; y < copyH; ++y) {
            std::memcpy(dstBuf + dstPitch * y,
                        srcBuf + srcPitch * y,
                        static_cast<size_t>(copyRowBytes));
        }

        return true;
    }

    bool Player::renderToLayer(iTJSDispatch2 *layerObject, bool skipUpdate) {
        if(!layerObject) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        iTJSDispatch2 *resolvedLayerObject = layerObject;
        tTJSVariant wrapper(layerObject, layerObject);
        if(auto *resolved = tryResolveLayerDispatch(wrapper)) {
            resolvedLayerObject = resolved;
        }

        int canvasWidth = 0;
        int canvasHeight = 0;
        if(!queryLayerCanvasSize(resolvedLayerObject, canvasWidth, canvasHeight) &&
           _runtime->activeMotion) {
            canvasWidth = static_cast<int>(_runtime->activeMotion->width);
            canvasHeight = static_cast<int>(_runtime->activeMotion->height);
        }
        if(canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        detail::logoChainTraceLogf(
            motionPath, "draw.layer", "0x6C7440/0x6CE7D8", _clampedEvalTime,
            "targetLayerCanvas={}x{} skipUpdate={} needsInternalAssignImages={}",
            canvasWidth, canvasHeight, skipUpdate ? 1 : 0,
            _needsInternalAssignImages ? 1 : 0);

        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();

        iTJSDispatch2 *renderLayerObject = resolvedLayerObject;
        if(_needsInternalAssignImages && !skipUpdate) {
            renderLayerObject = ensureReusableLayerObject(
                _runtime->internalRenderLayer,
                resolveLayerTreeOwnerObject(resolvedLayerObject),
                resolvedLayerObject,
                static_cast<tTVPLayerType>(ltAlpha),
                false);
        }
        if(renderLayerObject != resolvedLayerObject) {
            if(!prepareLayerForRender(renderLayerObject, canvasWidth, canvasHeight,
                                      0x00000000)) {
                return false;
            }
        } else if(auto *targetLayer = resolveNativeLayer(resolvedLayerObject)) {
            if(targetLayer->GetWidth() != canvasWidth ||
               targetLayer->GetHeight() != canvasHeight) {
                targetLayer->SetSize(canvasWidth, canvasHeight);
            }
        } else {
            return false;
        }

        buildRenderCommands(canvasWidth, canvasHeight);
        if(!executeLayerRenderCommands(renderLayerObject, true)) {
            return false;
        }

        if(!skipUpdate) {
            if(renderLayerObject != resolvedLayerObject) {
                updateLayerAfterDraw(resolvedLayerObject);
            } else if(auto *layer = resolveNativeLayer(resolvedLayerObject)) {
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

        _runtime->lastCanvas =
            tTJSVariant(resolvedLayerObject, resolvedLayerObject);
        detail::logoChainTraceSummary(
            motionPath, "renderToLayer", _clampedEvalTime,
            skipUpdate ? "skipUpdate=1" : "skipUpdate=0");
        return true;
    }

    bool Player::renderToSeparateLayerAdaptor(iTJSDispatch2 *slaObject) {
        if(!slaObject || !_runtime) {
            return false;
        }

        SeparateLayerAdaptor *sla =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                slaObject, false);
        iTJSDispatch2 *ownerLayer = sla ? sla->getOwner() : nullptr;
        if(!ownerLayer) {
            ownerLayer = tryResolveSeparateAdaptorOwner(tTJSVariant(slaObject, slaObject));
        }
        if(!ownerLayer) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        int canvasWidth = 0;
        int canvasHeight = 0;
        iTJSDispatch2 *renderTarget =
            resolveSeparateLayerRenderTarget(sla, ownerLayer, canvasWidth,
                                             canvasHeight);
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
            isAccurateSlaRenderEnabled() ? 1 : 0,
            isAccurateSlaRenderEnabled()
                ? "0x6C9CA8 -> 0x6CE938"
                : "Player_RenderMotionFrame -> Layer_UpdateRect");
        detail::logoChainTraceLogf(
            motionPath, "sla.resolveTarget", "0x6D5948",
            _clampedEvalTime,
            "targetLayer={} privateTarget={} absolute={} canvas={}x{}",
            static_cast<const void *>(tryResolveLayerDispatch(sla->getTargetLayer())),
            static_cast<const void *>(renderTarget),
            sla->getAbsolute() ? 1 : 0,
            canvasWidth, canvasHeight);

        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();

        if(!renderMotionFrameToTarget(renderTarget, canvasWidth, canvasHeight,
                                      isAccurateSlaRenderEnabled()
                                          ? "0x6C9CA8"
                                          : "0x6DE738")) {
            detail::logoChainTraceSummary(
                motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
                "fail=renderMotionFrameToTarget");
            return false;
        }

        if(isAccurateSlaRenderEnabled()) {
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.begin", "0x6C9CA8",
                _clampedEvalTime,
                "target={} canvas={}x{}",
                static_cast<const void *>(renderTarget),
                canvasWidth, canvasHeight);
            updateAccurateSLAAfterDraw(renderTarget);
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.end", "0x6CE938",
                _clampedEvalTime,
                "target={}", static_cast<const void *>(renderTarget));
        } else if(auto *renderLayer = resolveNativeLayer(renderTarget)) {
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

        _runtime->lastCanvas = tTJSVariant(ownerLayer, ownerLayer);
        detail::logoChainTraceSummary(
            motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
            isAccurateSlaRenderEnabled() ? "accurate=1" : "accurate=0");
        return true;
    }

    bool Player::updateLayerAfterDraw(iTJSDispatch2 *targetLayerObject) {
        if(!_needsInternalAssignImages) {
            return true;
        }
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};

        _needsInternalAssignImages = false;
        if(!targetLayerObject) {
            return false;
        }

        iTJSDispatch2 *renderLayerObject =
            _runtime->internalRenderLayer.Type() == tvtObject
                ? _runtime->internalRenderLayer.AsObjectNoAddRef()
                : nullptr;
        if(!renderLayerObject) {
            return false;
        }

        try {
            tTJSVariant targetVar(targetLayerObject, targetLayerObject);
            tTJSVariant *args[] = { &targetVar };
            const bool ok = TJS_SUCCEEDED(renderLayerObject->FuncCall(
                0, TJS_W("assignImages"), nullptr, nullptr, 1, args,
                renderLayerObject));
            detail::logoChainTraceCheck(
                motionPath, "post.assignImages", "0x6CE7D8",
                _clampedEvalTime,
                "internal render layer assignImages(targetLayer)",
                ok ? "assignImages(targetLayer)" : "assignImages(failed)",
                ok,
                "sub_6CE7D8 failed to assign internal render layer to target");
            return ok;
        } catch(...) {
            detail::logoChainTraceCheck(
                motionPath, "post.assignImages", "0x6CE7D8",
                _clampedEvalTime,
                "internal render layer assignImages(targetLayer)",
                "assignImages(threw)", false,
                "sub_6CE7D8 threw while assigning internal render layer");
            return false;
        }
    }

    bool Player::updateAccurateSLAAfterDraw(iTJSDispatch2 *targetLayerObject) {
        if(!targetLayerObject) {
            return false;
        }
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};

        if(auto *layer = resolveNativeLayer(targetLayerObject)) {
            layer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "post.sla.accurate", "0x6C9CA8/0x6CE938",
                _clampedEvalTime,
                "route=renderTarget.Update(false) size={}x{}",
                layer->GetWidth(), layer->GetHeight());
            return true;
        }
        detail::logoChainTraceCheck(
            motionPath, "post.sla.accurate", "0x6C9CA8/0x6CE938",
            _clampedEvalTime,
            "accurate SLA should update the resolved render target",
            "no post-update target", false,
            "accurate SLA render finished without a target update");
        return false;
    }

    tTJSVariant Player::findSource(ttstr name) {
        loadSource(name);
        const auto key = detail::narrow(name);
        if(const auto it = _runtime->sourcesByKey.find(key);
           it != _runtime->sourcesByKey.end()) {
            return it->second;
        }
        return {};
    }

    void Player::loadSource(ttstr name) {
        const auto requestKey = detail::narrow(name);
        if(requestKey.empty() ||
           _runtime->sourcesByKey.find(requestKey) !=
               _runtime->sourcesByKey.end()) {
            return;
        }

        ttstr resolved;
        if(!detail::resolveExistingPath(buildSourceCandidates(*_runtime, name),
                                        resolved)) {
            return;
        }

        const auto resolvedKey = detail::narrow(resolved);
        if(const auto existing = _runtime->sourcesByKey.find(resolvedKey);
           existing != _runtime->sourcesByKey.end()) {
            _runtime->sourcesByKey.emplace(requestKey, existing->second);
            return;
        }

        const auto source = _resourceManagerNative.load(resolved);
        if(source.Type() == tvtVoid) {
            return;
        }

        _runtime->sourcesByKey.emplace(requestKey, source);
        _runtime->sourcesByKey.emplace(resolvedKey, source);
    }

    void Player::clearCache() {
        _runtime->sourcesByKey.clear();
        _runtime->lastCanvas.Clear();
    }

    void Player::setSize(tjs_int w, tjs_int h) {
        _runtime->width = w;
        _runtime->height = h;
    }

    void Player::copyRect(tTJSVariant) {}

    void Player::adjustGamma(tTJSVariant) {}

    void Player::draw() {
        // Keep the no-arg C++ method as a lightweight prepare pass. The real
        // libkrkr2.so draw dispatch happens in drawCompat based on argument type.
        if(!_runtime->visible) {
            _runtime->lastCanvas.Clear();
            return;
        }

        ensureMotionLoaded();
        calcViewParam();
        prepareRenderItems();
    }

} // namespace motion
