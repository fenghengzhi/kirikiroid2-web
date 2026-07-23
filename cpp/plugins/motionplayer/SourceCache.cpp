#include "SourceCache.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "BitmapIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "MotionDispatch.h"
#include "PlayerInternal.h"
#include "PrivateMotionGLL.h"
#include "RenderManager.h"
#include "ResourceManager.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "ncbind.hpp"
#include "tjsUtils.h"

namespace {

    static_assert(sizeof(tjs_uint) == 4 && sizeof(tjs_int) == 4);

    struct TintRectLike_0x6A7518 {
        tjs_int x;
        tjs_int y;
        tjs_int width;
        tjs_int height;
    };

    constexpr tjs_int signedW32(tjs_uint bits) noexcept {
        return bits <= 0x7fffffffu
            ? static_cast<tjs_int>(bits)
            : -1 - static_cast<tjs_int>(~bits);
    }

    constexpr tjs_int addW32(tjs_int left, tjs_int right) noexcept {
        return signedW32(
            static_cast<tjs_uint>(left) + static_cast<tjs_uint>(right));
    }

    constexpr tjs_int subtractW32(tjs_int left, tjs_int right) noexcept {
        return signedW32(
            static_cast<tjs_uint>(left) - static_cast<tjs_uint>(right));
    }

    constexpr tjs_int multiplyW32(tjs_int left, tjs_int right) noexcept {
        return signedW32(
            static_cast<tjs_uint>(left) * static_cast<tjs_uint>(right));
    }

    tjs_int divideSignedW32LikeArm(tjs_int dividend,
                                    tjs_int divisor) noexcept {
        if(divisor == 0) {
            return 0;
        }
        if(dividend == signedW32(0x80000000u) && divisor == -1) {
            return dividend;
        }
        return dividend / divisor;
    }

    tjs_int lerpChannelLike_0x6A7518(tjs_int from,
                                     tjs_int to,
                                     tjs_int position,
                                     tjs_int span) noexcept {
        const tjs_int scaled = multiplyW32(
            position, subtractW32(to, from));
        return addW32(from, divideSignedW32LikeArm(scaled, span));
    }

    std::uint8_t multiplyTintChannelLike_0x6A7518(
        tjs_int tint,
        std::uint8_t pixel,
        tjs_uint divisor) noexcept {
        const tjs_uint product =
            static_cast<tjs_uint>(tint) * static_cast<tjs_uint>(pixel);
        const tjs_uint value = product / divisor;
        return static_cast<std::uint8_t>(value >= 255u ? 255u : value);
    }

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject);

    bool getObjectProperty(const tTJSVariant &object,
                           const tjs_char *name,
                           tTJSVariant &out) {
        if(object.Type() != tvtObject || !object.AsObjectNoAddRef()) {
            return false;
        }
        return TJS_SUCCEEDED(object.AsObjectNoAddRef()->PropGet(
            0, name, nullptr, &out, object.AsObjectNoAddRef()));
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

    bool packedColorsEqual(const tjs_int (&left)[4],
                           const tjs_int (&right)[4]) {
        for(std::size_t index = 0; index < 4; ++index) {
            if(left[index] != right[index]) {
                return false;
            }
        }
        return true;
    }

    void copyPackedColors(tjs_int (&destination)[4],
                          const tjs_int (&source)[4]) {
        std::copy_n(source, 4, destination);
    }

    std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    tjs_int propGetIntOnceLike_0x6635DC(iTJSDispatch2 *object,
                                        const tjs_char *member,
                                        tjs_uint32 flags,
                                        tjs_uint32 *hint) {
        tTJSVariant value;
        (void)object->PropGet(flags, member, hint, &value, object);
        return static_cast<tjs_int>(value.AsInteger());
    }

    tjs_int propGetIntAfterProbeLike_0x6C1B70(
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
        return propGetIntOnceLike_0x6635DC(object, member, 0, hint);
    }

    void applyPackedCornerTintLike_0x6A7518(
        const tTJSVariant &layer,
        const tjs_int (&colors)[4],
        const TintRectLike_0x6A7518 &rect,
        bool halfAlphaBlend) {
        const auto c0 = static_cast<std::uint32_t>(colors[0]);
        const auto c1 = static_cast<std::uint32_t>(colors[1]);
        const auto c2 = static_cast<std::uint32_t>(colors[2]);
        const auto c3 = static_cast<std::uint32_t>(colors[3]);
        if(packedColorsAreDefault(c0, c1, c2, c3) ||
           packedColorsAreOpaqueWhite(c0, c1, c2, c3)) {
            return;
        }

        if(!TVPGetRenderManager()->IsSoftware()) {
            // GPU branch performs this native-instance query and discards its
            // result; it does not run the software pixel loop.
            (void)motion::resolvePrivateMotionGLLNativeLike_0x6DE24C(
                layer.AsObjectNoAddRef());
            return;
        }

        auto *nativeLayer = resolveNativeLayer(layer.AsObjectNoAddRef());
        const tTVPRect clip = nativeLayer->GetClip();
        auto *pixelBuffer = static_cast<std::uint8_t *>(
            nativeLayer->GetMainImagePixelBufferForWrite());
        const tjs_int pitch = nativeLayer->GetMainImagePixelBufferPitch();

        const tjs_int left = std::max(clip.left, rect.x);
        const tjs_int top = std::max(clip.top, rect.y);
        const tjs_int right =
            std::min(clip.right, addW32(rect.x, rect.width));
        const tjs_int bottom =
            std::min(clip.bottom, addW32(rect.y, rect.height));
        const tjs_uint colorDivisor = halfAlphaBlend ? 128u : 255u;
        if(top >= bottom) {
            return;
        }

        const auto topLeft = unpackPackedRgba(c0);
        const auto topRight = unpackPackedRgba(c1);
        const auto bottomRight = unpackPackedRgba(c2);
        const auto bottomLeft = unpackPackedRgba(c3);
        const tjs_int spanX = subtractW32(rect.width, 1);
        const tjs_int spanY = subtractW32(rect.height, 1);
        const std::int64_t firstRowOffset =
            static_cast<std::int64_t>(multiplyW32(top, pitch)) +
            static_cast<std::int64_t>(multiplyW32(left, 4));
        auto *row = reinterpret_cast<std::uint8_t *>(
            reinterpret_cast<std::uintptr_t>(pixelBuffer) +
            static_cast<std::uintptr_t>(firstRowOffset) + 1u);

        tjs_int y = top;
        for(;;) {
            // 0x6A76FC skips only the per-pixel body when left >= right; the
            // outer row loop still advances until bottom.
            if(left < right) {
                const tjs_int rowPosition = subtractW32(y, rect.y);
                const tjs_int rowLeftR = lerpChannelLike_0x6A7518(
                    topLeft[0], bottomLeft[0], rowPosition, spanY);
                const tjs_int rowLeftG = lerpChannelLike_0x6A7518(
                    topLeft[1], bottomLeft[1], rowPosition, spanY);
                const tjs_int rowLeftB = lerpChannelLike_0x6A7518(
                    topLeft[2], bottomLeft[2], rowPosition, spanY);
                const tjs_int rowLeftA = lerpChannelLike_0x6A7518(
                    topLeft[3], bottomLeft[3], rowPosition, spanY);
                const tjs_int rowRightR = lerpChannelLike_0x6A7518(
                    topRight[0], bottomRight[0], rowPosition, spanY);
                const tjs_int rowRightG = lerpChannelLike_0x6A7518(
                    topRight[1], bottomRight[1], rowPosition, spanY);
                const tjs_int rowRightB = lerpChannelLike_0x6A7518(
                    topRight[2], bottomRight[2], rowPosition, spanY);
                const tjs_int rowRightA = lerpChannelLike_0x6A7518(
                    topRight[3], bottomRight[3], rowPosition, spanY);

                auto *dst = row;
                tjs_int x = left;
                do {
                    const tjs_int columnPosition = subtractW32(x, rect.x);
                    const tjs_int tintR = lerpChannelLike_0x6A7518(
                        rowLeftR, rowRightR, columnPosition, spanX);
                    const tjs_int tintG = lerpChannelLike_0x6A7518(
                        rowLeftG, rowRightG, columnPosition, spanX);
                    const tjs_int tintB = lerpChannelLike_0x6A7518(
                        rowLeftB, rowRightB, columnPosition, spanX);
                    const tjs_int tintA = lerpChannelLike_0x6A7518(
                        rowLeftA, rowRightA, columnPosition, spanX);
                    dst[1] = multiplyTintChannelLike_0x6A7518(
                        tintR, dst[1], colorDivisor);
                    dst[0] = multiplyTintChannelLike_0x6A7518(
                        tintG, dst[0], colorDivisor);
                    dst[-1] = multiplyTintChannelLike_0x6A7518(
                        tintB, dst[-1], colorDivisor);
                    dst[2] = multiplyTintChannelLike_0x6A7518(
                        tintA, dst[2], 255u);
                    x = addW32(x, 1);
                    dst += 4;
                } while(x < right);
            }

            y = addW32(y, 1);
            if(y >= bottom) {
                break;
            }
            row += static_cast<std::ptrdiff_t>(pitch);
        }
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
                                     const tTJSVariant &parent) {
        if(owner.Type() != tvtObject || !owner.AsObjectNoAddRef()) {
            return nullptr;
        }

        tTJSVariant layerClassVar;
        if(!getLayerClassVariant(layerClassVar)) {
            return nullptr;
        }

        iTJSDispatch2 *created = nullptr;
        tTJSVariant ownerArg(owner);
        tTJSVariant parentArg(parent);
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
            const tTJSVariant parent = parentLayerObject
                ? tTJSVariant(parentLayerObject, parentLayerObject)
                : tTJSVariant();
            layerObject = createLayerObject(owner, parent);
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

    iTVPTexture2D *textureFromLayerVariant(const tTJSVariant &value) {
        if(value.Type() != tvtObject || !value.AsObjectNoAddRef()) {
            return nullptr;
        }
        auto *layer = resolveNativeLayer(value.AsObjectNoAddRef());
        auto *image = layer ? layer->GetMainImage() : nullptr;
        return image ? image->GetTexture() : nullptr;
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

    tTJSVariant SourceCache::loadSource(iTJSDispatch2 *source,
                                        iTJSDispatch2 *descriptor) {
        // SourceCache_loadSource @0x6A7BA8 receives two borrowed dispatches.
        // ncbPropAccessor supplies the temporary AddRef/Release lifetime seen in
        // the binary while the cache entry itself never retains `source`.
        ncbPropAccessor descriptorAccessor(descriptor);

        tTJSVariant key;
        descriptor->PropGet(0, TJS_W("key"),
                            &detail::commandKeyMemberHint_guess, &key,
                            descriptor);
        const ttstr src = descriptorAccessor.getStrValue(TJS_W("src"), ttstr());
        const tjs_int blendMode = descriptorAccessor.getIntValue(
            TJS_W("blendMode"), 0);

        tTJSVariant colorValue;
        descriptor->PropGet(0, TJS_W("color"),
                            &detail::colorMemberHint_guess, &colorValue,
                            descriptor);
        // Deliberately default-initialized, not value-initialized.  The
        // color-void branch in 0x6A7BA8 writes only slot zero; slots 1..3 are
        // genuine uninitialized source behavior and must not be "fixed".
        tjs_int colors[4];
        if(colorValue.Type() != tvtVoid) {
            ncbPropAccessor colorAccessor(colorValue);
            for(tjs_int index = 0; index < 4; ++index) {
                colors[static_cast<std::size_t>(index)] =
                    colorAccessor.getIntValue(index, 0);
            }
        } else {
            colors[0] = (blendMode & 0xF0) != 0
                ? static_cast<tjs_int>(0xFF808080u)
                : static_cast<tjs_int>(0xFFFFFFFFu);
        }

        return loadSourceLike_0x6A7BA8(
            source, key, src, blendMode, colors);
    }

    tTJSVariant SourceCache::loadSourceByName(
        const Player *player,
        const ttstr &name,
        const tTJSVariant &currentSource) {
        // Web compatibility boundary for Player.loadSource(name).  It resolves
        // the raw object but intentionally does not create a second, by-name
        // cache topology beside Android's descriptor-keyed std::list.
        if(currentSource.Type() != tvtVoid) {
            return currentSource;
        }
        std::string resolvedKey;
        return loadRawSourceVariant(player, name, resolvedKey);
    }

    tTJSVariant Player::resolveRenderSourceLike_0x6C1B70_guess(
        const tTJSVariant &sourceObject) {
        tTJSVariant result;
        // sub_6C1B70 @0x6C1BAC compares only the two Variant Object dispatch
        // pointers.  Typed-null Objects therefore also compare equal; do not
        // add a non-null safety gate that the binary does not have.
        if(sourceObject.Type() == tvtObject &&
           _internalRenderLayer.Type() == tvtObject &&
           sourceObject.AsObjectNoAddRef() ==
               _internalRenderLayer.AsObjectNoAddRef()) {
            ncbPropAccessor descriptor{tTJSVariant(_sourceDescriptor)};
            const tjs_int blendMode = propGetIntOnceLike_0x6635DC(
                descriptor.GetDispatch(), TJS_W("blendMode"), 0,
                &detail::blendModeMemberHint_guess);

            ncbPropAccessor color{tTJSVariant(_sourceColors)};
            tjs_int colors[4];
            for(tjs_int index = 0; index < 4; ++index) {
                tTJSVariant value;
                (void)color.GetDispatch()->PropGetByNum(
                    0, index, &value, color.GetDispatch());
                colors[static_cast<std::size_t>(index)] =
                    static_cast<tjs_int>(value.AsInteger());
            }

            ncbPropAccessor work{
                tTJSVariant(_internalSourceWorkLayer_guess)};
            work.FuncCall(0, TJS_W("assignImages"),
                          &detail::assignImagesMemberHint_guess, &result,
                          _internalRenderLayer);
            const tjs_int height = propGetIntAfterProbeLike_0x6C1B70(
                work.GetDispatch(), TJS_W("height"),
                &detail::heightMemberHint_guess);
            const tjs_int width = propGetIntAfterProbeLike_0x6C1B70(
                work.GetDispatch(), TJS_W("width"),
                &detail::widthMemberHint_guess);
            applyPackedCornerTintLike_0x6A7518(
                _internalSourceWorkLayer_guess, colors,
                TintRectLike_0x6A7518{0, 0, width, height},
                (blendMode & 0xF0) == 0x10);
            return result;
        }

        ncbPropAccessor cache{tTJSVariant(_sourceCacheObject)};
        cache.FuncCall(0, TJS_W("loadSource"),
                       &detail::loadSourceMemberHint_guess, &result,
                       sourceObject, _sourceDescriptor);
        return result;
    }

    tTJSVariant SourceCache::loadRenderSourceLayerFromItemLike_0x6C1B70(
        Player &player,
        const detail::PreparedRenderItem &item) {
        // Player_ctor @0x6CED30 owns one persistent descriptor Dictionary and
        // one persistent color Dictionary.  Every 0x6C1B70 caller overwrites
        // these exact objects before entering the Player resolver; only its
        // fallback branch dispatches ResourceManager.loadSource.
        ncbPropAccessor descriptor{tTJSVariant(player._sourceDescriptor)};
        descriptor.SetValue(TJS_W("key"), item.commandKey, TJS_MEMBERENSURE,
                            &detail::commandKeyMemberHint_guess);
        descriptor.SetValue(TJS_W("src"), item.commandSrc, TJS_MEMBERENSURE,
                            &detail::commandSrcMemberHint_guess);
        descriptor.SetValue(TJS_W("blendMode"),
                            static_cast<tjs_int>(item.blendMode),
                            TJS_MEMBERENSURE,
                            &detail::blendModeMemberHint_guess);

        ncbPropAccessor color{tTJSVariant(player._sourceColors)};
        for(tjs_int index = 0; index < 4; ++index) {
            color.SetValue(
                index,
                // Player_renderToCanvas @0x6C7944..0x6C7A40 loads each
                // packed color through W8 then stores X8, i.e. zero-extended
                // uint32_t into the 64-bit TJS Integer payload.
                item.packedColors[static_cast<std::size_t>(index)],
                TJS_MEMBERENSURE);
        }

        auto &source = *item.sourceState;
        return player.resolveRenderSourceLike_0x6C1B70_guess(source.object);
    }

    iTVPTexture2D *
    SourceCache::loadRenderSourceTextureFromItemLike_0x6C1B70(
        Player &player,
        detail::PreparedRenderItem &item) {
        return textureFromLayerVariant(
            loadRenderSourceLayerFromItemLike_0x6C1B70(player, item));
    }

    iTVPTexture2D *SourceCache::loadRenderSourceTextureForItemLike_0x6F1060(
        Player &player,
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

    void SourceCache::clearCache() {
        for(auto &entry : _entries) {
            if(entry.layer.Type() == tvtObject &&
               entry.layer.AsObjectNoAddRef()) {
                auto *object = entry.layer.AsObjectNoAddRef();
                (void)object->Invalidate(0, nullptr, nullptr, object);
            }
        }
        _entries.clear();
        _currentCacheBytes = 0;
    }

    void SourceCache::eraseSource(ttstr name) {
        const tTJSVariant key(name);
        for(auto it = _entries.begin(); it != _entries.end();) {
            if(it->key.DiscernCompareStrictReal(key) || it->src == name) {
                _currentCacheBytes -=
                    static_cast<std::uint32_t>(it->byteWeight);
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

    tTJSVariant SourceCache::loadSourceLike_0x6A7BA8(
        iTJSDispatch2 *source,
        const tTJSVariant &key,
        const ttstr &src,
        tjs_int blendMode,
        const tjs_int (&colors)[4]) {
        for(auto it = _entries.begin(); it != _entries.end(); ++it) {
            if(it->key.DiscernCompareStrictReal(key) && it->src == src &&
               it->blendMode == blendMode) {
                tTJSVariant result(it->layer);
                if(packedColorsEqual(it->colors, colors)) {
                    return result;
                }

                copyPackedColors(it->colors, colors);
                bakeSourceLike_0x6A6BE0(source, *it);
                // 0x6A80D8..0x6A8140 is std::list::push_front(copy) followed
                // by erase(old), not splice: key/Layer/src all AddRef before
                // the old node's src -> Layer -> key destruction chain.
                _entries.push_front(*it);
                _entries.erase(it);
                return result;
            }
        }

        trimCacheBeforeInsertLike_0x6A6B08();
        Entry entry;
        entry.key = key;
        entry.src = src;
        entry.blendMode = blendMode;
        copyPackedColors(entry.colors, colors);
        if(iTJSDispatch2 *created = createLayerObject(_owner, _primaryLayer)) {
            entry.layer = tTJSVariant(created, created);
            created->Release();
        }
        tTJSVariant result(entry.layer);
        bakeSourceLike_0x6A6BE0(source, entry);
        _currentCacheBytes += static_cast<std::uint32_t>(entry.byteWeight);
        _entries.push_front(entry);
        return result;
    }

    void SourceCache::bakeSourceLike_0x6A6BE0(iTJSDispatch2 *source,
                                              Entry &entry) {
        {
            ncbPropAccessor sourceAccessor(source);
            sourceAccessor.FuncCall(0, TJS_W("drawLayer"),
                                    &detail::drawLayerMemberHint_guess,
                                    nullptr, entry.layer);
        }

        ncbPropAccessor layer(entry.layer);
        const tjs_int width = layer.getIntValue(TJS_W("width"), 0);
        const tjs_int height = layer.getIntValue(TJS_W("height"), 0);
        entry.byteWeight = 4 * width * height;

        applyPackedCornerTintLike_0x6A7518(
            entry.layer, entry.colors,
            TintRectLike_0x6A7518{0, 0, width, height},
            (entry.blendMode & 0xF0) != 0);

        const tjs_int lowBlend = entry.blendMode & 0x0F;
        if(static_cast<tjs_uint>(lowBlend - 1) < 2u) {
            const bool software = TVPGetRenderManager()->IsSoftware();
            if(software || !resolvePrivateMotionGLLNativeLike_0x6DE24C(
                               entry.layer.AsObjectNoAddRef())) {
                ncbPropAccessor buffer(_bufLayer);
                buffer.FuncCall(0, TJS_W("setSize"),
                                &detail::setSizeMemberHint_guess, nullptr,
                                tTJSVariant(width), tTJSVariant(height));
                buffer.FuncCall(0, TJS_W("copyRect"),
                                &detail::copyRectMemberHint_guess, nullptr,
                                tTJSVariant(0), tTJSVariant(0), entry.layer,
                                tTJSVariant(0), tTJSVariant(0),
                                tTJSVariant(width), tTJSVariant(height));
                layer.FuncCall(0, TJS_W("fillRect"),
                               &detail::fillRectMemberHint_guess, nullptr,
                               tTJSVariant(0), tTJSVariant(0),
                               tTJSVariant(width), tTJSVariant(height),
                               tTJSVariant(
                                   static_cast<tjs_int>(0xFF000000u)));
                layer.FuncCall(0, TJS_W("operateRect"),
                               &detail::operateRectMemberHint_guess, nullptr,
                               tTJSVariant(0), tTJSVariant(0), _bufLayer,
                               tTJSVariant(0), tTJSVariant(0),
                               tTJSVariant(width), tTJSVariant(height),
                               tTJSVariant(15));
                if(lowBlend == 2) {
                    layer.FuncCall(
                        0, TJS_W("adjustGamma"),
                        &detail::adjustGammaMemberHint_guess, nullptr,
                        tTJSVariant(1), tTJSVariant(255), tTJSVariant(0),
                        tTJSVariant(1), tTJSVariant(255), tTJSVariant(0),
                        tTJSVariant(1), tTJSVariant(255), tTJSVariant(0));
                }
            }
        }
    }

    void SourceCache::trimCacheBeforeInsertLike_0x6A6B08() {
        if(_currentCacheBytes <= _cacheLimitBytes) {
            return;
        }

        const std::uint32_t threshold =
            (_cacheLimitBytes * std::uint32_t{99}) / std::uint32_t{100};
        std::uint32_t keptBytes = 0;
        for(auto it = _entries.begin(); it != _entries.end();) {
            const std::uint32_t sum = keptBytes +
                static_cast<std::uint32_t>(it->byteWeight);
            if(static_cast<std::int32_t>(sum) <=
               static_cast<std::int32_t>(threshold)) {
                keptBytes = sum;
                ++it;
                continue;
            }

            _currentCacheBytes -=
                static_cast<std::uint32_t>(it->byteWeight);
            it = _entries.erase(it);
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
