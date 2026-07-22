#include "SourceCache.h"

#include <algorithm>
#include <cstring>
#include <optional>
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
#include "tjsUtils.h"

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

    void decodeObjSourceRL8Like_0x6DA454(
        std::uint8_t *destination, const std::uint8_t *source,
        std::uint32_t sourceSize) {
        const std::uint8_t *const sourceEnd = source + sourceSize;
        while(source < sourceEnd) {
            const std::uint8_t marker = *source++;
            if((marker & 0x80u) != 0) {
                const size_t count = (marker & 0x7fu) + 3u;
                std::memset(destination, *source++, count);
                destination += count;
            } else {
                const size_t count = static_cast<size_t>(marker) + 1u;
                std::memcpy(destination, source, count);
                destination += count;
                source += count;
            }
        }
    }

    void decodeObjSourceRL32Like_0x6DA454(
        std::uint8_t *destination, const std::uint8_t *source,
        std::uint32_t sourceSize) {
        const std::uint8_t *const sourceEnd = source + sourceSize;
        auto *output = reinterpret_cast<tjs_uint32 *>(destination);
        while(source < sourceEnd) {
            const std::uint8_t marker = *source++;
            if((marker & 0x80u) != 0) {
                tjs_uint32 pixel;
                std::memcpy(&pixel, source, sizeof(pixel));
                source += sizeof(pixel);
                const size_t count = (marker & 0x7fu) + 3u;
                std::fill_n(output, count, pixel);
                output += count;
            } else {
                const size_t count = static_cast<size_t>(marker) + 1u;
                const size_t byteCount = count * sizeof(tjs_uint32);
                std::memcpy(output, source, byteCount);
                output += count;
                source += byteCount;
            }
        }
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

    ObjSource::~ObjSource() {
        if(_texture) {
            _texture->Release();
        }
    }

    tTJSVariant ObjSource::getClip() const {
        // ObjSource_getClip @0x69D35C: category-gate and try-get only `clip`;
        // once present, all four child reads are strict raw-node operations.
        PSB::PSBRawNode clip;
        if(_source.GetTypeCategory() != 7 ||
           !_source.GetDictionaryValue("clip", clip)) {
            return {};
        }
        ncbDictionaryAccessor dictionary;
        dictionary.SetValue(TJS_W("left"),
                            clip.GetDictionaryValueStrict("left").GetDouble(),
                            TJS_MEMBERENSURE, &detail::leftMemberHint_guess);
        dictionary.SetValue(TJS_W("top"),
                            clip.GetDictionaryValueStrict("top").GetDouble(),
                            TJS_MEMBERENSURE, &detail::topMemberHint_guess);
        dictionary.SetValue(TJS_W("right"),
                            clip.GetDictionaryValueStrict("right").GetDouble(),
                            TJS_MEMBERENSURE, &detail::rightMemberHint_guess);
        dictionary.SetValue(
            TJS_W("bottom"),
            clip.GetDictionaryValueStrict("bottom").GetDouble(),
            TJS_MEMBERENSURE, &detail::bottomMemberHint_guess);
        return tTJSVariant(dictionary.GetDispatch(), dictionary.GetDispatch());
    }

    void ObjSource::ensureTextureLike_0x6DA454() {
        // ObjSource_ensureTexture @0x6DA454 returns immediately once qword[2]
        // owns a texture. Every following read is a strict raw-node read.
        if(_texture) {
            return;
        }

        const tjs_uint width = static_cast<tjs_uint>(
            _source.GetDictionaryValueStrict("width").GetInt());
        const tjs_int height =
            _source.GetDictionaryValueStrict("height").GetInt();
        const tjs_uint pixelCount = width * static_cast<tjs_uint>(height);

        bool compressed = false;
        if(_source.ContainsDictionaryKey("compress")) {
            compressed = std::strcmp(
                _source.GetDictionaryValueStrict("compress").GetString(),
                "RL") == 0;
        }
        std::uint32_t pixelSize{};
        const std::uint8_t *pixelData = nullptr;
        std::uint8_t *decoded = nullptr;
        const std::uint8_t *sourcePixels = nullptr;
        if(compressed) {
            // 0x6DA708 checks `pal` here to select the RL element width. The
            // common palette branch checks it again at 0x6DA5E8.
            const bool compressedHasPalette =
                _source.ContainsDictionaryKey("pal");
            pixelData = _source.GetDictionaryValueStrict("pixel")
                            .GetResource(pixelSize);
            const tjs_uint decodedBytes =
                (compressedHasPalette ? 1u : 4u) * pixelCount;
            decoded = static_cast<std::uint8_t *>(
                TJSAlignedAlloc(decodedBytes, 4));
            if(compressedHasPalette) {
                decodeObjSourceRL8Like_0x6DA454(decoded, pixelData, pixelSize);
            } else {
                decodeObjSourceRL32Like_0x6DA454(decoded, pixelData, pixelSize);
                TVPReverseRGB(
                    reinterpret_cast<tjs_uint32 *>(decoded),
                    reinterpret_cast<const tjs_uint32 *>(decoded),
                    static_cast<tjs_int>(pixelCount));
            }
            sourcePixels = decoded;
        } else {
            pixelData = _source.GetDictionaryValueStrict("pixel")
                            .GetResource(pixelSize);
            sourcePixels = pixelData;
        }

        const bool hasPalette = _source.ContainsDictionaryKey("pal");
        std::uint8_t *bgra = nullptr;
        if(hasPalette) {
            std::uint32_t paletteSize{};
            const auto *paletteData =
                _source.GetDictionaryValueStrict("pal").GetResource(paletteSize);
            const std::size_t paletteCount =
                paletteSize / sizeof(tjs_uint32);
            std::vector<tjs_uint32> palette(paletteCount);
            TVPReverseRGB(
                palette.data(),
                reinterpret_cast<const tjs_uint32 *>(paletteData),
                static_cast<tjs_int>(paletteCount));
            bgra = static_cast<std::uint8_t *>(
                TJSAlignedAlloc(4u * pixelCount, 4));
            TVPBLExpand8BitTo32BitPal(
                reinterpret_cast<tjs_uint32 *>(bgra), sourcePixels,
                static_cast<tjs_int>(pixelCount), palette.data());
            if(decoded) {
                TJSAlignedDealloc(decoded);
            }
        } else if(decoded) {
            bgra = decoded;
        } else {
            bgra = static_cast<std::uint8_t *>(
                TJSAlignedAlloc(4u * pixelCount, 4));
            TVPReverseRGB(
                reinterpret_cast<tjs_uint32 *>(bgra),
                reinterpret_cast<const tjs_uint32 *>(pixelData),
                static_cast<tjs_int>(pixelCount));
        }

        auto *bitmap = new tTVPBitmap(width, static_cast<tjs_uint>(height), 32);
        const tjs_int pitch = bitmap->GetPitch();
        auto *destination = static_cast<std::uint8_t *>(bitmap->GetScanLine(0));
        const tjs_int rowBytes = static_cast<tjs_int>(4u * width);
        if(rowBytes == pitch) {
            std::memcpy(destination, bgra,
                        static_cast<std::size_t>(height * pitch));
        } else if(height >= 1) {
            const std::uint8_t *source = bgra;
            for(tjs_int row = 0; row < height; ++row) {
                std::memcpy(destination, source,
                            static_cast<std::size_t>(rowBytes));
                destination += pitch;
                source += rowBytes;
            }
        }
        _texture = TVPGetRenderManager()->CreateTexture2D(bitmap);
        bitmap->Release();
        TJSAlignedDealloc(bgra);
    }

    void ObjSource::drawLayer(tTJSVariant target) {
        // ObjSource_drawLayer @0x69D6D8 gates only on the raw source category.
        if(_source.GetTypeCategory() != 7) {
            return;
        }
        ensureTextureLike_0x6DA454();
        iTJSDispatch2 *targetObject = target.AsObjectNoAddRef();
        tTJSNI_BaseLayer *layer = nullptr;
        if(targetObject && TJS_FAILED(targetObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer)))) {
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        layer->AssignTexture(_texture);
        layer->SetSize(_texture->GetWidth(), _texture->GetHeight());
    }

    SourceCache::SourceCache() = default;

    SourceCache::SourceCache(tTJSVariant owner, tjs_int cacheSize) :
        _cacheLimitBytes(static_cast<std::uint32_t>(cacheSize)) {
        setLayerOwner(std::move(owner));
    }

    SourceCache::~SourceCache() {
        clearCache();
    }

    void SourceCache::setLayerOwner(tTJSVariant owner) {
        _owner = std::move(owner);
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
        return loadSourceByName(nullptr, *name, currentSource);
    }

    tTJSVariant SourceCache::loadSourceByName(
        const Player *player,
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
        tTJSVariant rawSource =
            currentSource.Type() != tvtVoid
                ? currentSource
                : loadRawSourceVariant(player, name, resolvedKey);
        Entry entry;
        entry.key = key;
        entry.resolvedKey = resolvedKey.empty() ? key : resolvedKey;
        entry.rawSource = rawSource;
        // This is a legacy by-name Web facade, not SourceCache_loadSource
        // @0x6A7BA8: the Android NCB method receives (source, descriptor) and
        // returns a baked Layer. Keep its raw ObjSource out of sourceObject so
        // the legacy render helpers cannot mistake it for that baked Layer.
        // The production prepared-item route below restores the exact
        // (key, src, blendMode) identity and object-to-bake data flow.
        trimCacheBeforeInsertLike_0x6A6B08();
        _entries.push_front(std::move(entry));
        return rawSource;
    }

    tTJSVariant SourceCache::loadRenderSourceByName(
        const Player &player,
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
            setLayerOwner(
                tTJSVariant(layerTreeOwnerObject, layerTreeOwnerObject));
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
                                            : loadRawSourceVariant(&player, name, resolvedKey);
        bool inserted = false;
        auto &entry = ensureEntry(
            key, resolvedKey.empty() ? key : resolvedKey, blendMode,
            packedColors, inserted);
        entry.rawSource = rawSource;

        const bool hasBitmap = ensureEntryBackingBitmap(
            entry, &player, key, blendMode, packedColors, nullptr, true);
        if(inserted) {
            _currentCacheBytes += entry.byteWeight;
        }
        if(!hasBitmap) {
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

    iTVPTexture2D *
    SourceCache::loadRenderSourceTextureFromItemLike_0x6C1B70(
        const Player &player,
        detail::PreparedRenderItem &item) {
        auto &source = *item.sourceState;
        const std::string key = detail::narrow(item.commandKey);
        const std::string src = detail::narrow(item.commandSrc);

        // sub_6C1B70 passes the descriptor and source object as independent
        // arguments to SourceCache_loadSource @0x6A7BA8.  Empty src is a real
        // cache-key value, not permission to discard a valid source object.
        // The cache node does not retain the incoming object: 0x6A823C and
        // 0x6A80FC pass it directly to the bake only on miss/color change.
        return loadSourceLike_0x6A7BA8(
            player, source.object, key, src, item.blendMode,
            item.packedColors);
    }

    iTVPTexture2D *SourceCache::loadRenderSourceTextureForItemLike_0x6F1060(
        const Player &player,
        detail::PreparedRenderItem &item) {
        auto &source = *item.sourceState;
        // sub_6F1060 @0x6F1094 observes the persistent descriptor first.
        if(source.texture) {
            return source.texture;
        }

        bool atlasLoaded;
        {
            // 0x6F112C..0x6F1160 materializes a temporary ttstr from the
            // Player-owned motion-context Variant, calls the shared helper,
            // then destroys the temporary before testing its result/texture.
            const ttstr moduleKey =
                static_cast<ttstr>(player._findMotionContextVariant);
            atlasLoaded = Player::loadKrkrAtlasSourceLike_0x695DE8(
                source, player.nativeRM(), moduleKey);
        }
        if(atlasLoaded && source.texture) {
            return source.texture;
        }

        // The helper may have cleared source.object before failing.  Pass the
        // post-call object onward without consulting source.path; 0x6F1174 ->
        // sub_6C1B70 receives only that object plus the prepared descriptor.
        return loadRenderSourceTextureFromItemLike_0x6C1B70(player, item);
    }

    iTVPTexture2D *SourceCache::loadRenderSourceTextureByName(
        const Player &player,
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
                                            : loadRawSourceVariant(&player, name, resolvedKey);
        bool inserted = false;
        auto &entry = ensureEntry(
            key, resolvedKey.empty() ? key : resolvedKey, blendMode,
            packedColors, inserted);
        entry.rawSource = rawSource;

        auto *texture = ensureRenderTextureForEntry(
            entry, &player, key, blendMode, packedColors, nullptr, true);
        if(inserted) {
            _currentCacheBytes += entry.byteWeight;
        }
        return texture;
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
        _currentCacheBytes = 0;
    }

    void SourceCache::eraseSource(ttstr name) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return;
        }

        for(auto it = _entries.begin(); it != _entries.end();) {
            if(it->key == key || it->resolvedKey == key) {
                _currentCacheBytes -= it->byteWeight;
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
        // Legacy by-name/storage alias matching. The Android descriptor route
        // must use loadSourceLike_0x6A7BA8 instead.
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
        // Legacy by-name/storage alias matching. It intentionally accepts the
        // requested key or the placed storage key and therefore is not the
        // Android 0x6A7BA8 tuple matcher. packedColors is carried only so this
        // compatibility route can detect a mutable color change.
        (void)packedColors;
        for(auto it = _entries.begin(); it != _entries.end(); ++it) {
            if((it->key == key || it->resolvedKey == key) &&
               it->blendMode == blendMode) {
                // Compatibility route: keep its historic alias-hit promotion.
                // The production 0x6A7BA8 route promotes only on color change.
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
        const std::array<std::uint32_t, 4> &packedColors,
        bool &inserted) {
        if(auto *entry = findEntry(key, blendMode, packedColors)) {
            inserted = false;
            // This legacy alias node still keeps color mutable. The exact
            // descriptor route implements the same invalidation independently
            // in loadSourceLike_0x6A7BA8.
            if(entry->packedColors != packedColors) {
                entry->packedColors = packedColors;
                entry->backingBitmap.reset();
                entry->sourceObject.Clear();
                releaseEntryTexture(*entry);
            }
            return *entry;
        }

        trimCacheBeforeInsertLike_0x6A6B08();
        Entry entry;
        entry.key = key;
        entry.resolvedKey = resolvedKey.empty() ? key : resolvedKey;
        entry.blendMode = blendMode;
        entry.packedColors = packedColors;
        _entries.push_front(std::move(entry));
        inserted = true;
        return _entries.front();
    }

    iTVPTexture2D *SourceCache::loadSourceLike_0x6A7BA8(
        const Player &player,
        const tTJSVariant &rawSource,
        const std::string &key,
        const std::string &src,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors) {
        for(auto it = _entries.begin(); it != _entries.end(); ++it) {
            if(it->key == key && it->resolvedKey == src &&
               it->blendMode == blendMode) {
                auto &entry = *it;
                if(entry.packedColors == packedColors) {
                    // 0x6A8098..0x6A80D4 returns the cached Layer directly:
                    // no source callback, retry, or list promotion occurs.
                    return entry.sourceTexture;
                }

                entry.packedColors = packedColors;
                entry.backingBitmap.reset();
                entry.sourceObject.Clear();
                releaseEntryTexture(entry);
                // 0x6A80D8..0x6A8140 invokes the source exactly once, then
                // replaces the old list node at the front.
                const bool baked = ensureEntryBackingBitmap(
                    entry, &player, src, blendMode, packedColors, &rawSource,
                    false);
                _entries.splice(_entries.begin(), _entries, it);
                if(!baked) {
                    return nullptr;
                }
                return ensureRenderTextureForEntry(
                    entry, &player, src, blendMode, packedColors, &rawSource,
                    false);
            }
        }

        // 0x6A8148 calls the byte-budget trim before creating and baking the
        // new Layer. The new node is inserted only after its byte weight is
        // known, so an oversized insertion is trimmed by the next miss.
        trimCacheBeforeInsertLike_0x6A6B08();
        Entry entry;
        entry.key = key;
        entry.resolvedKey = src;
        entry.blendMode = blendMode;
        entry.packedColors = packedColors;
        const bool baked = ensureEntryBackingBitmap(
            entry, &player, src, blendMode, packedColors, &rawSource, false);
        _currentCacheBytes += entry.byteWeight;
        _entries.push_front(std::move(entry));
        auto &inserted = _entries.front();
        if(!baked) {
            return nullptr;
        }
        return ensureRenderTextureForEntry(
            inserted, &player, src, blendMode, packedColors, &rawSource,
            false);
    }

    void SourceCache::trimCacheBeforeInsertLike_0x6A6B08() {
        if(_currentCacheBytes <= _cacheLimitBytes) {
            return;
        }

        const std::uint32_t threshold =
            (_cacheLimitBytes * std::uint32_t{99}) / std::uint32_t{100};
        std::uint32_t keptBytes = 0;
        for(auto it = _entries.begin(); it != _entries.end();) {
            const std::uint32_t sum = keptBytes + it->byteWeight;
            if(static_cast<std::int32_t>(sum) <=
               static_cast<std::int32_t>(threshold)) {
                keptBytes = sum;
                ++it;
                continue;
            }

            _currentCacheBytes -= it->byteWeight;
            releaseEntryTexture(*it);
            it = _entries.erase(it);
        }
    }

    iTVPTexture2D *SourceCache::ensureRenderTextureForEntry(
        Entry &entry,
        const Player *player,
        const std::string &fallbackSource,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors,
        const tTJSVariant *rawSourceOverride,
        bool allowStorageFallback) {
        if(!ensureEntryBackingBitmap(
               entry, player, fallbackSource, blendMode, packedColors,
               rawSourceOverride, allowStorageFallback)) {
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

        entry.sourceTexture = TVPGetRenderManager()->CreateTexture2D(
            pixels, pitch, width, height,
            entry.backingBitmap->Is8BPP() ? TVPTextureFormat::Gray
                                          : TVPTextureFormat::RGBA,
            RENDER_CREATE_TEXTURE_FLAG_ANY);
        return entry.sourceTexture;
    }

    bool SourceCache::ensureEntryBackingBitmap(
        Entry &entry,
        const Player *player,
        const std::string &key,
        int blendMode,
        const std::array<std::uint32_t, 4> &packedColors,
        const tTJSVariant *rawSourceOverride,
        bool allowStorageFallback) {
        if(entry.backingBitmap) {
            return entry.backingBitmap->GetWidth() > 0 &&
                entry.backingBitmap->GetHeight() > 0;
        }

        entry.byteWeight = 0;
        std::shared_ptr<tTVPBaseBitmap> baseBitmap;
        const tTJSVariant &rawSource =
            rawSourceOverride ? *rawSourceOverride : entry.rawSource;
        // SourceCache_loadSource @0x6A7BA8 calls the raw source facade's
        // drawLayer(bufLayer). ObjSource_drawLayer @0x69D6D8 owns PSB pixel
        // decoding; SourceCache only snapshots that temporary Layer before the
        // 0x6A6BE0 color bake.
        if(rawSource.Type() == tvtObject &&
           rawSource.AsObjectNoAddRef() &&
           _bufLayer.Type() == tvtObject && _bufLayer.AsObjectNoAddRef()) {
            tTJSVariant bufferArg(_bufLayer);
            tTJSVariant *args[] = { &bufferArg };
            if(TJS_SUCCEEDED(rawSource.AsObjectNoAddRef()->FuncCall(
                   0, TJS_W("drawLayer"), nullptr, nullptr, 1, args,
                   rawSource.AsObjectNoAddRef()))) {
                if(auto *bufferLayer =
                       resolveNativeLayer(_bufLayer.AsObjectNoAddRef())) {
                    if(auto *image = bufferLayer->GetMainImage();
                       image && image->GetWidth() > 0 &&
                       image->GetHeight() > 0) {
                        baseBitmap = cloneBitmap32(*image);
                    }
                }
            }
        }

        // ResourceManager_findSource @0x6AAB3C returns a plain dictionary for
        // blank/W:H:X:Y. It intentionally has no ObjSource.drawLayer method.
        if(!baseBitmap && rawSource.Type() == tvtObject) {
            tTJSVariant blankValue;
            tTJSVariant widthValue;
            tTJSVariant heightValue;
            if(getObjectProperty(rawSource, TJS_W("blank"), blankValue) &&
               static_cast<tjs_int>(blankValue) != 0 &&
               getObjectProperty(rawSource, TJS_W("width"), widthValue) &&
               getObjectProperty(rawSource, TJS_W("height"), heightValue)) {
                const auto width = static_cast<tjs_int>(widthValue);
                const auto height = static_cast<tjs_int>(heightValue);
                if(width > 0 && height > 0) {
                    baseBitmap = std::make_shared<tTVPBaseBitmap>(
                        static_cast<tjs_uint>(width),
                        static_cast<tjs_uint>(height), 32);
                    baseBitmap->Fill(tTVPRect(0, 0, width, height), 0);
                }
            }
        }

        // Non-PSB source names remain ordinary storage graphics. This is the
        // platform storage boundary after the raw ResourceManager lookup, not
        // a decoded MotionSnapshot alias graph.
        if(!baseBitmap && allowStorageFallback) {
            const ttstr path = detail::widen(
                entry.resolvedKey.empty() ? key : entry.resolvedKey);
            baseBitmap = loadGraphicBitmap(path);
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
        entry.byteWeight = std::uint32_t{4} *
            static_cast<std::uint32_t>(entry.backingBitmap->GetWidth()) *
            static_cast<std::uint32_t>(entry.backingBitmap->GetHeight());
        return true;
    }

    void SourceCache::releaseEntryTexture(Entry &entry) {
        if(entry.sourceTexture) {
            entry.sourceTexture->Release();
            entry.sourceTexture = nullptr;
        }
    }

    tTJSVariant SourceCache::loadRawSourceVariant(
        const Player *player,
        const ttstr &name,
        std::string &resolvedKey) const {
        resolvedKey.clear();
        if(!player || !player->nativeRM()) {
            return {};
        }

        // Player_findSource @0x6948E8 passes Player+1012 and the requested
        // source path directly to ResourceManager_findSource @0x6AAB3C.
        tTJSVariant raw = player->nativeRM()->findSource(
            static_cast<ttstr>(player->_findMotionContextVariant), name);
        if(raw.Type() != tvtVoid) {
            resolvedKey = detail::narrow(name);
            return raw;
        }

        // External graphic fallback uses the storage normalizer only. The
        // former decoded-tree source-candidate cache was removed because the
        // Android chain resolves through Player+1012/RM directly.
        const ttstr placed = TVPGetPlacedPath(name);
        if(!placed.IsEmpty() && TVPIsExistentStorage(placed)) {
            resolvedKey = detail::narrow(placed);
        }
        return {};
    }

} // namespace motion
