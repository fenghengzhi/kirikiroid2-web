#include "SourceCache.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <unordered_set>
#include <vector>

#include "BitmapIntf.h"
#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "PlayerInternal.h"
#include "RenderManager.h"
#include "ResourceManager.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "ncbind.hpp"

namespace {

    bool getObjectProperty(const tTJSVariant &object,
                           const tjs_char *name,
                           tTJSVariant &out) {
        if(object.Type() != tvtObject || !object.AsObjectNoAddRef()) {
            return false;
        }
        return TJS_SUCCEEDED(object.AsObjectNoAddRef()->PropGet(
            0, name, nullptr, &out, object.AsObjectNoAddRef()));
    }

    std::optional<ttstr> sourceNameFromVariant(const tTJSVariant &value) {
        if(value.Type() == tvtVoid) {
            return std::nullopt;
        }
        if(value.Type() == tvtObject && value.AsObjectNoAddRef()) {
            for(const auto *name : { TJS_W("src"), TJS_W("key") }) {
                tTJSVariant prop;
                if(getObjectProperty(value, name, prop) && prop.Type() != tvtVoid) {
                    return ttstr(prop);
                }
            }
            return std::nullopt;
        }
        return ttstr(value);
    }

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
                    255, tintA * static_cast<int>(dst[3]) / colorDivisor));
            }
        }
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

        static const char *exts[] = {
            ".png", ".webp", ".jpg", ".jpeg", ".bmp", ".tlg", ".pimg", ".psb"
        };
        for(const auto *ext : exts) {
            candidates.emplace_back(base + ttstr{ ext });
        }
    }

    ttstr resolveMotionSourcePathLike_0x6948E8(
        const motion::detail::MotionSnapshot &snapshot,
        const std::string &source) {
        if(source.empty() || motion::internal::isMotionCrossReference(source)) {
            return {};
        }

        std::vector<ttstr> candidates;
        const auto sourcePath = motion::detail::widen(source);
        pushGraphicCandidates(candidates, sourcePath);
        motion::detail::appendEmbeddedSourceCandidates(snapshot, source, candidates);
        for(const auto &alias : snapshot.resourceAliases) {
            const auto embeddedBase = ttstr{ TJS_W("psb://") } +
                motion::detail::widen(alias) + TJS_W("/") + sourcePath;
            pushGraphicCandidates(candidates, embeddedBase);
        }

        const auto lastSlash = source.rfind('/');
        const auto baseName =
            lastSlash == std::string::npos ? source : source.substr(lastSlash + 1);
        for(const auto &[resPath, ignored] : snapshot.resourcesByPath) {
            (void)ignored;
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
            if(const auto placed = TVPGetPlacedPath(candidate); !placed.IsEmpty()) {
                return placed;
            }
        }
        return {};
    }

    std::shared_ptr<tTVPBaseBitmap> loadGraphicBitmap(const ttstr &path) {
        if(path.IsEmpty()) {
            return nullptr;
        }

        ttstr loadPath = path;
        const auto pathString = motion::detail::narrow(path);
        if(pathString.rfind('.') == std::string::npos ||
           pathString.rfind('.') < pathString.rfind('/')) {
            loadPath = path + TJS_W(".png");
        }

        try {
            auto bmp = std::make_shared<tTVPBaseBitmap>(1, 1, 32);
            TVPLoadGraphic(bmp.get(), loadPath, TVP_clNone, 0, 0,
                           glmNormal, nullptr, nullptr);
            if(bmp->GetWidth() > 0 && bmp->GetHeight() > 0) {
                return bmp;
            }
        } catch(...) {
        }
        return nullptr;
    }

    std::shared_ptr<tTVPBaseBitmap> loadPsbBitmap(
        const motion::detail::MotionSnapshot &snapshot,
        const std::string &sourceKey) {
        int width = 0;
        int height = 0;
        double originX = 0.0;
        double originY = 0.0;
        std::vector<std::uint8_t> decodedPixels;
        bool decodedPixelsAreBgra = false;
        const auto *resource = motion::internal::findPSBResourceBySourceName(
            snapshot, sourceKey, width, height, decodedPixels, originX, originY,
            &decodedPixelsAreBgra);
        const bool sourceDiag =
            sourceKey.find("yuzu") != std::string::npos ||
            sourceKey.find("logo") != std::string::npos;
        if(sourceDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG SourceCache::loadPsbBitmap path='{}' source='{}' found={} width={} height={} resourceBytes={} decodedBytes={} decodedBgra={} root={} resourceCount={}",
                snapshot.path, sourceKey, resource ? 1 : 0, width, height,
                resource ? resource->data.size() : 0u, decodedPixels.size(),
                decodedPixelsAreBgra ? 1 : 0, snapshot.root ? 1 : 0,
                snapshot.resourcesByPath.size());
        }
        motion::detail::logoChainTraceLogf(
            snapshot.path, "sourceCache.loadPsbBitmap", "0x6948E8", 0.0,
            "source='{}' found={} width={} height={} resourceBytes={} decodedBytes={} decodedBgra={} root={} resourceCount={}",
            sourceKey, resource ? 1 : 0, width, height,
            resource ? resource->data.size() : 0u, decodedPixels.size(),
            decodedPixelsAreBgra ? 1 : 0, snapshot.root ? 1 : 0,
            snapshot.resourcesByPath.size());
        if(!resource || width <= 0 || height <= 0 || resource->data.empty()) {
            return nullptr;
        }

        const auto &pixelData =
            decodedPixels.empty() ? resource->data : decodedPixels;
        auto bmp = std::make_shared<tTVPBaseBitmap>(
            static_cast<tjs_uint>(width), static_cast<tjs_uint>(height), 32);
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
                auto *dst = row + static_cast<size_t>(x) * 4u;
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
        return bmp;
    }

    tTJSVariant loadPsbSourceFacadeLike_0x6948E8(
        const motion::detail::MotionSnapshot &snapshot,
        const std::string &sourceKey) {
        if(sourceKey.rfind("src/", 0) != 0 || snapshot.moduleValue.Type() != tvtObject) {
            return {};
        }

        const auto afterSrc = sourceKey.substr(4);
        const auto slash = afterSrc.find('/');
        if(slash == std::string::npos) {
            return {};
        }
        const auto group = afterSrc.substr(0, slash);
        const auto name = afterSrc.substr(slash + 1);
        if(group.empty() || name.empty()) {
            return {};
        }

        tTJSVariant sourceDict;
        tTJSVariant groupDict;
        tTJSVariant iconHolder;
        tTJSVariant iconEntry;
        const ttstr groupKey = motion::detail::widen(group);
        const ttstr nameKey = motion::detail::widen(name);
        const bool found =
            getObjectProperty(snapshot.moduleValue, TJS_W("source"), sourceDict) &&
            getObjectProperty(sourceDict, groupKey.c_str(), groupDict) &&
            getObjectProperty(groupDict, TJS_W("icon"), iconHolder) &&
            getObjectProperty(iconHolder, nameKey.c_str(), iconEntry) &&
            iconEntry.Type() == tvtObject;

        const bool sourceDiag =
            sourceKey.find("yuzu") != std::string::npos ||
            sourceKey.find("logo") != std::string::npos;
        if(sourceDiag && LOGGER) {
            int width = 0;
            int height = 0;
            if(found) {
                tTJSVariant value;
                if(getObjectProperty(iconEntry, TJS_W("width"), value) &&
                   value.Type() != tvtVoid) {
                    width = static_cast<int>(value);
                }
                if(getObjectProperty(iconEntry, TJS_W("height"), value) &&
                   value.Type() != tvtVoid) {
                    height = static_cast<int>(value);
                }
            }
            LOGGER->info(
                "PRTDIAG SourceCache::loadPsbSourceFacade path='{}' source='{}' group='{}' icon='{}' found={} size={}x{}",
                snapshot.path, sourceKey, group, name, found ? 1 : 0,
                width, height);
        }
        motion::detail::logoChainTraceLogf(
            snapshot.path, "sourceCache.loadPsbSourceFacade", "0x6948E8", 0.0,
            "source='{}' group='{}' icon='{}' found={}", sourceKey, group,
            name, found ? 1 : 0);

        if(!found) {
            return {};
        }

        using ObjSourceAdaptor = ncbInstanceAdaptor<motion::ObjSource>;
        auto *src = new motion::ObjSource(iconEntry);
        if(iTJSDispatch2 *dispatch = ObjSourceAdaptor::CreateAdaptor(src)) {
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }
        delete src;
        return {};
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

    bool getLayerClassVariant(tTJSVariant &layerClassVar) {
        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global) {
            return false;
        }
        const bool ok = TJS_SUCCEEDED(global->PropGet(
            0, TJS_W("Layer"), nullptr, &layerClassVar, global)) &&
            layerClassVar.Type() == tvtObject &&
            layerClassVar.AsObjectNoAddRef();
        global->Release();
        return ok;
    }

    iTJSDispatch2 *createLayerObject(const tTJSVariant &owner,
                                     iTJSDispatch2 *parentLayerObject) {
        if(owner.Type() != tvtObject || !owner.AsObjectNoAddRef()) {
            return nullptr;
        }

        tTJSVariant layerClassVar;
        if(!getLayerClassVariant(layerClassVar)) {
            return nullptr;
        }

        iTJSDispatch2 *created = nullptr;
        tTJSVariant ownerArg(owner);
        tTJSVariant parentArg =
            parentLayerObject ? tTJSVariant(parentLayerObject, parentLayerObject)
                              : tTJSVariant();
        tTJSVariant *args[] = { &ownerArg, &parentArg };
        if(TJS_FAILED(layerClassVar.AsObjectNoAddRef()->CreateNew(
               0, nullptr, nullptr, &created, 2, args,
               layerClassVar.AsObjectNoAddRef()))) {
            return nullptr;
        }
        return created;
    }

    iTJSDispatch2 *ensureLayerObject(tTJSVariant &slot,
                                     const tTJSVariant &owner,
                                     iTJSDispatch2 *parentLayerObject,
                                     bool visible) {
        iTJSDispatch2 *layerObject =
            slot.Type() == tvtObject ? slot.AsObjectNoAddRef() : nullptr;
        if(!layerObject) {
            layerObject = createLayerObject(owner, parentLayerObject);
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
        layer->SetType(ltAlpha);
        layer->SetVisible(visible);
        layer->SetAbsoluteOrderMode(false);
        return layerObject;
    }

    bool assignBitmapToLayerLike_0x6948E8(tTJSNI_BaseLayer *sourceLayer,
                                          const iTVPBaseBitmap &src) {
        if(!sourceLayer || src.GetWidth() <= 0 || src.GetHeight() <= 0) {
            return false;
        }
        if(!sourceLayer->GetHasImage()) {
            sourceLayer->SetHasImage(true);
        }
        sourceLayer->SetType(ltAlpha);
        sourceLayer->AssignMainImageWithUpdate(
            const_cast<iTVPBaseBitmap *>(&src));
        sourceLayer->SetSize(src.GetWidth(), src.GetHeight());
        sourceLayer->SetClip(0, 0, src.GetWidth(), src.GetHeight());
        return true;
    }

} // namespace

namespace motion {

    SourceCache::SourceCache() = default;

    SourceCache::SourceCache(tTJSVariant owner, tjs_int layerType) {
        setLayerOwner(std::move(owner), layerType);
    }

    SourceCache::~SourceCache() {
        clearCache();
    }

    void SourceCache::bindPlayer(Player *player,
                                 ResourceManager *resourceManager) {
        _player = player;
        _resourceManager = resourceManager;
    }

    void SourceCache::setSelfObject(tTJSVariant selfObject) {
        _selfObject = std::move(selfObject);
    }

    void SourceCache::setLayerOwner(tTJSVariant owner, tjs_int layerType) {
        _owner = std::move(owner);
        _layerType = layerType;
        _primaryLayer.Clear();

        if(_owner.Type() == tvtObject && _owner.AsObjectNoAddRef()) {
            tTJSVariant primary;
            if(getObjectProperty(_owner, TJS_W("primaryLayer"), primary) &&
               primary.Type() == tvtObject && primary.AsObjectNoAddRef()) {
                _primaryLayer = primary;
            } else if(resolveNativeLayer(_owner.AsObjectNoAddRef())) {
                _primaryLayer = _owner;
            }
        }

        iTJSDispatch2 *parentLayer =
            _primaryLayer.Type() == tvtObject ? _primaryLayer.AsObjectNoAddRef()
                                              : nullptr;
        const tTJSVariant &layerOwner = _owner;
        if(layerOwner.Type() == tvtObject) {
            ensureLayerObject(_bufLayer, layerOwner, parentLayer, false);
        }
    }

    tTJSVariant SourceCache::loadSource(tTJSVariant keyOrSource,
                                        tTJSVariant currentSource) {
        auto name = sourceNameFromVariant(keyOrSource);
        if(!name || name->IsEmpty()) {
            name = sourceNameFromVariant(currentSource);
        }
        if(!name || name->IsEmpty()) {
            return {};
        }
        return loadSourceByName(*name, currentSource);
    }

    tTJSVariant SourceCache::loadSourceByName(
        const ttstr &name,
        const tTJSVariant &currentSource) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return {};
        }

        if(auto *entry = findEntryByKey(key)) {
            if(entry->sourceObject.Type() != tvtVoid) {
                return entry->sourceObject;
            }
            return entry->rawSource;
        }

        std::string resolvedKey;
        tTJSVariant rawSource;
        if(_player && _player->_activeMotion) {
            // Player_findSource @0x6948E8 resolves PSB-backed "src/..."
            // entries through the active module's source/icon dictionary before
            // falling back to ResourceManager.findSource/storage paths.
            rawSource =
                loadPsbSourceFacadeLike_0x6948E8(*_player->_activeMotion, key);
        }
        if(rawSource.Type() == tvtVoid) {
            rawSource =
                currentSource.Type() != tvtVoid
                    ? currentSource
                    : loadRawSourceVariant(name, resolvedKey);
        }
        Entry entry;
        entry.key = key;
        entry.resolvedKey = resolvedKey.empty() ? key : resolvedKey;
        entry.rawSource = rawSource;
        // 不要把 raw findSource 结果(ObjSource，非 Layer)塞进 entry.sourceObject。
        // sourceObject 是烘焙后的 Layer 槽位(对齐 libkrkr2.so loadSource@0x6A7BA8：
        // 缓存节点 +36 永远是 baked Layer，命中即返回可渲染 Layer）。脚本 loadSource
        // 这条 facade 路径不烘焙，若在此把 ObjSource 写入 sourceObject，会污染共享
        // _entries：render 路径 loadRenderSourceByName 的缓存命中(findEntry 按
        // key+blendMode 匹配，blendMode 默认 0)会早返回该 ObjSource，而
        // PlayerRenderExecute 对它做 resolveNativeLayer 失败 → 源贴图为空 → logo
        // 渲染全白。留 sourceObject 为空，使 render 命中时落到烘焙路径
        // (ensureEntryBackingBitmap + ensureLayerObject 新建真 Layer +
        // assignBitmapToLayer)。脚本 loadSource 仍由 rawSource 返回 ObjSource。
        _entries.push_front(std::move(entry));
        return rawSource;
    }

    tTJSVariant SourceCache::loadRenderSourceByName(
        const ttstr &name,
        const tTJSVariant &currentSource,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors,
        iTJSDispatch2 *layerTreeOwnerObject,
        iTJSDispatch2 *parentLayerObject) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return {};
        }

        if(layerTreeOwnerObject &&
           (_owner.Type() != tvtObject || _primaryLayer.Type() != tvtObject)) {
            setLayerOwner(tTJSVariant(layerTreeOwnerObject, layerTreeOwnerObject),
                          _layerType);
        }
        if(parentLayerObject && _primaryLayer.Type() != tvtObject) {
            _primaryLayer = tTJSVariant(parentLayerObject, parentLayerObject);
        }

        // Aligned with loadSource @0x6A7BA8: a cached node may be reused only
        // when its stored color (node+68..+80) still matches the requested
        // color. A color change must NOT short-circuit here — the binary always
        // reaches the 0x6a80d4 color comparison and re-bakes on mismatch — so we
        // only fast-return when the stored color is unchanged.
        if(auto *entry = findEntry(key, blendMode, packedColors)) {
            if(entry->packedColors == packedColors &&
               entry->sourceObject.Type() == tvtObject &&
               entry->sourceObject.AsObjectNoAddRef()) {
                return entry->sourceObject;
            }
        }

        std::string resolvedKey;
        auto rawSource =
            currentSource.Type() != tvtVoid ? currentSource
                                            : loadRawSourceVariant(name, resolvedKey);
        auto &entry = ensureEntry(
            key, resolvedKey.empty() ? key : resolvedKey, blendMode, packedColors);
        entry.rawSource = rawSource;

        if(!ensureEntryBackingBitmap(entry, key, blendMode, packedColors)) {
            return entry.rawSource;
        }

        iTJSDispatch2 *parentLayer =
            parentLayerObject ? parentLayerObject
                              : (_primaryLayer.Type() == tvtObject
                                     ? _primaryLayer.AsObjectNoAddRef()
                                     : nullptr);
        const tTJSVariant owner =
            _owner.Type() == tvtObject
                ? _owner
                : (layerTreeOwnerObject ? tTJSVariant(layerTreeOwnerObject,
                                                      layerTreeOwnerObject)
                                        : tTJSVariant());
        auto *sourceLayerObject =
            ensureLayerObject(entry.sourceObject, owner, parentLayer, false);
        auto *sourceLayer = resolveNativeLayer(sourceLayerObject);
        if(!sourceLayerObject || !sourceLayer || !entry.backingBitmap ||
           !assignBitmapToLayerLike_0x6948E8(sourceLayer, *entry.backingBitmap)) {
            entry.sourceObject.Clear();
            return entry.rawSource;
        }

        return entry.sourceObject;
    }

    iTVPTexture2D *SourceCache::loadRenderSourceTextureByName(
        const ttstr &name,
        const tTJSVariant &currentSource,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return nullptr;
        }

        // (key, blendMode) single mutable node; a color change invalidates the
        // baked texture so it is rebuilt below (aligned with 0x6A7BA8 hit path).
        if(auto *entry = findEntry(key, blendMode, packedColors)) {
            if(entry->packedColors == packedColors && entry->sourceTexture) {
                return entry->sourceTexture;
            }
        }

        std::string resolvedKey;
        auto rawSource =
            currentSource.Type() != tvtVoid ? currentSource
                                            : loadRawSourceVariant(name, resolvedKey);
        auto &entry = ensureEntry(
            key, resolvedKey.empty() ? key : resolvedKey, blendMode, packedColors);
        entry.rawSource = rawSource;

        if(!ensureEntryBackingBitmap(entry, key, blendMode, packedColors)) {
            return nullptr;
        }
        if(entry.sourceTexture) {
            return entry.sourceTexture;
        }

        const auto width = entry.backingBitmap->GetWidth();
        const auto height = entry.backingBitmap->GetHeight();
        const auto pitch = entry.backingBitmap->GetPitchBytes();
        const auto *pixels = entry.backingBitmap->GetScanLine(0);
        if(!pixels || pitch <= 0 || width <= 0 || height <= 0) {
            return nullptr;
        }

        // D3DAdaptor_renderFromPlayer @ 0x6ADE24 passes a source texture
        // getter into 0x6ADFBC, so this path returns texture data directly
        // instead of materializing an intermediate SourceCache Layer.
        entry.sourceTexture = TVPGetRenderManager()->CreateTexture2D(
            pixels, pitch, width, height,
            entry.backingBitmap->Is8BPP() ? TVPTextureFormat::Gray
                                          : TVPTextureFormat::RGBA,
            RENDER_CREATE_TEXTURE_FLAG_ANY);
        return entry.sourceTexture;
    }

    tTJSVariant SourceCache::findSource(ttstr name) {
        return loadSourceByName(name, {});
    }

    void SourceCache::clearCache() {
        for(auto &entry : _entries) {
            if(entry.sourceObject.Type() == tvtObject &&
               entry.sourceObject.AsObjectNoAddRef()) {
                if(auto *layer = resolveNativeLayer(entry.sourceObject.AsObjectNoAddRef())) {
                    layer->SetHasImage(false);
                }
            }
            releaseEntryTexture(entry);
        }
        _entries.clear();
    }

    void SourceCache::eraseSource(ttstr name) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return;
        }

        for(auto it = _entries.begin(); it != _entries.end();) {
            if(it->key == key || it->resolvedKey == key) {
                releaseEntryTexture(*it);
                it = _entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    tTJSVariant SourceCache::getBufLayer() const {
        return _bufLayer;
    }

    std::size_t SourceCache::size() const {
        return _entries.size();
    }

    const SourceCache::Entry *SourceCache::findEntry(
        const std::string &key,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors) const {
        // (key, blendMode) match only — see non-const overload / 0x6A7BA8.
        (void)packedColors;
        for(const auto &entry : _entries) {
            if((entry.key == key || entry.resolvedKey == key) &&
               entry.blendMode == blendMode) {
                return &entry;
            }
        }
        return nullptr;
    }

    SourceCache::Entry *SourceCache::findEntry(
        const std::string &key,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors) {
        // Aligned with loadSource @0x6A7BA8 (match loop 0x6a8004-0x6a8074):
        // the binary matches a cache node by (key @node+16, src @node+56,
        // blendMode @node+64) only — color is NOT part of the match key. Each
        // (key, blendMode) therefore has exactly ONE node; color (node+68..+80)
        // is mutable. We mirror that by matching (key, blendMode) here and
        // updating color in-place on hit (see below). packedColors is no longer
        // a match dimension; it is carried so the hit path can detect a change.
        (void)packedColors;
        for(auto it = _entries.begin(); it != _entries.end(); ++it) {
            if((it->key == key || it->resolvedKey == key) &&
               it->blendMode == blendMode) {
                // re-splice to head (0x6a8100-0x6a8114 clone-to-front / LRU)
                _entries.splice(_entries.begin(), _entries, it);
                return &_entries.front();
            }
        }
        return nullptr;
    }

    SourceCache::Entry *SourceCache::findEntryByKey(const std::string &key) {
        for(auto it = _entries.begin(); it != _entries.end(); ++it) {
            if(it->key == key || it->resolvedKey == key) {
                _entries.splice(_entries.begin(), _entries, it);
                return &_entries.front();
            }
        }
        return nullptr;
    }

    SourceCache::Entry &SourceCache::ensureEntry(
        const std::string &key,
        const std::string &resolvedKey,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors) {
        if(auto *entry = findEntry(key, blendMode, packedColors)) {
            // Aligned with loadSource @0x6A7BA8 hit path (else branch
            // 0x6a8098): each (key, blendMode) keeps ONE mutable node. When the
            // requested color differs from the node's stored color
            // (node+68..+80 vs v61..v64 at 0x6a80d4), the binary writes the new
            // color in-place (0x6a80d8), re-bakes the source bitmap via
            // sub_6A6BE0 (copyRect/fillRect/operateRect, per-pixel color bake),
            // then clone-replaces the node at the list head. We reproduce the
            // semantics: update the stored color and invalidate the baked
            // image so ensureEntryBackingBitmap re-bakes it with the new color.
            // (The binary clone+delete of the std::list node is an ABI detail of
            // its container; we keep the same Entry via std::list and just
            // refresh its fields — same data flow, no per-color entry growth.)
            if(entry->packedColors != packedColors) {
                entry->packedColors = packedColors;
                entry->backingBitmap.reset();
                entry->sourceObject.Clear();
                releaseEntryTexture(*entry);
            }
            return *entry;
        }

        Entry entry;
        entry.key = key;
        entry.resolvedKey = resolvedKey.empty() ? key : resolvedKey;
        entry.blendMode = blendMode;
        entry.packedColors = packedColors;
        _entries.push_front(std::move(entry));
        return _entries.front();
    }

    bool SourceCache::ensureEntryBackingBitmap(
        Entry &entry,
        const std::string &key,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors) {
        if(entry.backingBitmap) {
            return entry.backingBitmap->GetWidth() > 0 &&
                entry.backingBitmap->GetHeight() > 0;
        }

        std::shared_ptr<tTVPBaseBitmap> baseBitmap;
        if(_player && _player->_activeMotion) {
            if(key.rfind("src/", 0) == 0) {
                // Player_findSource @0x6948E8 resolves "src/..." entries from
                // the loaded PSB source/texture/icon dictionaries before any
                // ResourceManager.findSource storage fallback.
                baseBitmap = loadPsbBitmap(*_player->_activeMotion, key);
                if(LOGGER) {
                    LOGGER->info(
                        "PRTDIAG SourceCache::ensure psbFirst path='{}' key='{}' hit={} size={}x{}",
                        _player->_activeMotion->path, key,
                        baseBitmap ? 1 : 0,
                        baseBitmap ? baseBitmap->GetWidth() : 0,
                        baseBitmap ? baseBitmap->GetHeight() : 0);
                }
                detail::logoChainTraceLogf(
                    _player->_activeMotion->path,
                    "sourceCache.ensure.psbFirst", "0x6948E8",
                    _player->_clampedEvalTime,
                    "key='{}' hit={} size={}x{}", key, baseBitmap ? 1 : 0,
                    baseBitmap ? baseBitmap->GetWidth() : 0,
                    baseBitmap ? baseBitmap->GetHeight() : 0);
            }
            if(!baseBitmap) {
                const auto path = resolveMotionSourcePathLike_0x6948E8(
                    *_player->_activeMotion, key);
                baseBitmap = loadGraphicBitmap(path);
                if((key.find("yuzu") != std::string::npos ||
                    key.find("logo") != std::string::npos) &&
                   LOGGER) {
                    LOGGER->info(
                        "PRTDIAG SourceCache::ensure storageFallback path='{}' key='{}' storage='{}' hit={} size={}x{}",
                        _player->_activeMotion->path, key,
                        detail::narrow(path), baseBitmap ? 1 : 0,
                        baseBitmap ? baseBitmap->GetWidth() : 0,
                        baseBitmap ? baseBitmap->GetHeight() : 0);
                }
                detail::logoChainTraceLogf(
                    _player->_activeMotion->path,
                    "sourceCache.ensure.storageFallback", "0x6948E8",
                    _player->_clampedEvalTime,
                    "key='{}' path='{}' hit={} size={}x{}", key,
                    detail::narrow(path), baseBitmap ? 1 : 0,
                    baseBitmap ? baseBitmap->GetWidth() : 0,
                    baseBitmap ? baseBitmap->GetHeight() : 0);
            }
            if(!baseBitmap) {
                baseBitmap = loadPsbBitmap(*_player->_activeMotion, key);
                if((key.find("yuzu") != std::string::npos ||
                    key.find("logo") != std::string::npos) &&
                   LOGGER) {
                    LOGGER->info(
                        "PRTDIAG SourceCache::ensure psbFallback path='{}' key='{}' hit={} size={}x{}",
                        _player->_activeMotion->path, key,
                        baseBitmap ? 1 : 0,
                        baseBitmap ? baseBitmap->GetWidth() : 0,
                        baseBitmap ? baseBitmap->GetHeight() : 0);
                }
                detail::logoChainTraceLogf(
                    _player->_activeMotion->path,
                    "sourceCache.ensure.psbFallback", "0x6948E8",
                    _player->_clampedEvalTime,
                    "key='{}' hit={} size={}x{}", key, baseBitmap ? 1 : 0,
                    baseBitmap ? baseBitmap->GetWidth() : 0,
                    baseBitmap ? baseBitmap->GetHeight() : 0);
            }
        }
        if(!baseBitmap || baseBitmap->GetWidth() <= 0 ||
           baseBitmap->GetHeight() <= 0) {
            return false;
        }

        const bool useHalfAlphaTint = (blendMode & 0xF0) == 0x10;
        const bool needsTint =
            !packedColorsAreDefault(packedColors[0], packedColors[1],
                                    packedColors[2], packedColors[3]) &&
            !packedColorsAreOpaqueWhite(packedColors[0], packedColors[1],
                                        packedColors[2], packedColors[3]);
        if(needsTint) {
            entry.backingBitmap = cloneBitmap32(*baseBitmap);
            applyPackedCornerTintLike_0x6A7518(*entry.backingBitmap,
                                              packedColors,
                                              useHalfAlphaTint);
        } else {
            entry.backingBitmap = baseBitmap;
        }
        return true;
    }

    void SourceCache::releaseEntryTexture(Entry &entry) {
        if(entry.sourceTexture) {
            entry.sourceTexture->Release();
            entry.sourceTexture = nullptr;
        }
    }

    tTJSVariant SourceCache::loadRawSourceVariant(
        const ttstr &name,
        std::string &resolvedKey) const {
        resolvedKey.clear();
        if(!_player || !_resourceManager) {
            return {};
        }

        ttstr resolved;
        if(!detail::resolveExistingPath(
               internal::buildSourceCandidates(*_player, name), resolved)) {
            return {};
        }

        resolvedKey = detail::narrow(resolved);
        return _resourceManager->load(resolved);
    }

} // namespace motion
