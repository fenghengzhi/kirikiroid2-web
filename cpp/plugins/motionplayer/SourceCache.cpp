#include "SourceCache.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "BitmapIntf.h"
#include "D3DAdaptor.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "MotionDispatch.h"
#include "PlayerInternal.h"
#include "PrivateMotionGLL.h"
#include "RenderManager.h"
#include "ResourceManager.h"
#include "ScriptMgnIntf.h"
#include "ncbind.hpp"
#include "tjsUtils.h"

namespace {

    static_assert(sizeof(tjs_uint) == 4 && sizeof(tjs_int) == 4);

    struct TintRect_guess {
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

    tjs_int lerpTintChannel_guess(tjs_int from,
                                  tjs_int to,
                                  tjs_int position,
                                  tjs_int span) noexcept {
        const tjs_int scaled = multiplyW32(
            position, subtractW32(to, from));
        return addW32(from, divideSignedW32LikeArm(scaled, span));
    }

    std::uint8_t multiplyTintChannel_guess(
        tjs_int tint,
        std::uint8_t pixel,
        tjs_uint divisor) noexcept {
        const tjs_uint product =
            static_cast<tjs_uint>(tint) * static_cast<tjs_uint>(pixel);
        const tjs_uint value = product / divisor;
        return static_cast<std::uint8_t>(value >= 255u ? 255u : value);
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

    tjs_int getRenderSourceDimension_guess(
        ncbPropAccessor &object,
        const tjs_char *member,
        tjs_uint32 *hint) {
        if(!object.HasValue(member, hint)) {
            return 0;
        }
        return object.GetValue(
            member, ncbTypedefs::Tag<tjs_int>(), 0, hint);
    }

    void applyPackedCornerTint_guess(
        const tTJSVariant &layer,
        const tjs_int (&colors)[4],
        const TintRect_guess &rect,
        bool halfAlphaBlend) {
        const auto c0 = static_cast<std::uint32_t>(colors[0]);
        const auto c1 = static_cast<std::uint32_t>(colors[1]);
        const auto c2 = static_cast<std::uint32_t>(colors[2]);
        const auto c3 = static_cast<std::uint32_t>(colors[3]);
        if(packedColorsAreDefault(c0, c1, c2, c3) ||
           packedColorsAreOpaqueWhite(c0, c1, c2, c3)) {
            return;
        }

        if(!TVPIsSoftwareRenderManager()) {
            // GPU branch performs this native-instance query and discards its
            // result; it does not run the software pixel loop.
            (void)motion::queryPrivateMotionGLLNativeFromVariant_guess(layer);
            return;
        }

        // The software path uses the engine's strict Variant-to-Layer helper;
        // a failed native-instance query throws TVPSpecifyLayer before clip
        // and pixel-buffer access.
        auto *nativeLayer = tTJSNI_Layer::FromVariant(layer);
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
            // An empty horizontal intersection skips only the per-pixel body;
            // the outer row loop still advances until bottom.
            if(left < right) {
                const tjs_int rowPosition = subtractW32(y, rect.y);
                const tjs_int rowLeftR = lerpTintChannel_guess(
                    topLeft[0], bottomLeft[0], rowPosition, spanY);
                const tjs_int rowLeftG = lerpTintChannel_guess(
                    topLeft[1], bottomLeft[1], rowPosition, spanY);
                const tjs_int rowLeftB = lerpTintChannel_guess(
                    topLeft[2], bottomLeft[2], rowPosition, spanY);
                const tjs_int rowLeftA = lerpTintChannel_guess(
                    topLeft[3], bottomLeft[3], rowPosition, spanY);
                const tjs_int rowRightR = lerpTintChannel_guess(
                    topRight[0], bottomRight[0], rowPosition, spanY);
                const tjs_int rowRightG = lerpTintChannel_guess(
                    topRight[1], bottomRight[1], rowPosition, spanY);
                const tjs_int rowRightB = lerpTintChannel_guess(
                    topRight[2], bottomRight[2], rowPosition, spanY);
                const tjs_int rowRightA = lerpTintChannel_guess(
                    topRight[3], bottomRight[3], rowPosition, spanY);

                auto *dst = row;
                tjs_int x = left;
                do {
                    const tjs_int columnPosition = subtractW32(x, rect.x);
                    const tjs_int tintR = lerpTintChannel_guess(
                        rowLeftR, rowRightR, columnPosition, spanX);
                    const tjs_int tintG = lerpTintChannel_guess(
                        rowLeftG, rowRightG, columnPosition, spanX);
                    const tjs_int tintB = lerpTintChannel_guess(
                        rowLeftB, rowRightB, columnPosition, spanX);
                    const tjs_int tintA = lerpTintChannel_guess(
                        rowLeftA, rowRightA, columnPosition, spanX);
                    dst[1] = multiplyTintChannel_guess(
                        tintR, dst[1], colorDivisor);
                    dst[0] = multiplyTintChannel_guess(
                        tintG, dst[0], colorDivisor);
                    dst[-1] = multiplyTintChannel_guess(
                        tintB, dst[-1], colorDivisor);
                    dst[2] = multiplyTintChannel_guess(
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

    void decodeObjSourceRL8_guess(
        std::uint8_t *destination, const std::uint8_t *source,
        std::uint32_t sourceSize) {
        // All four references form sourceEnd from the signed low 32-bit size
        // before the signed <1 gate. The exact source identifier is stripped.
        const tjs_int signedSourceSize = signedW32(sourceSize);
        const std::uint8_t *const sourceEnd = source + signedSourceSize;
        if(signedSourceSize < 1) {
            return;
        }
        do {
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
        } while(source < sourceEnd);
    }

    void decodeObjSourceRL32_guess(
        std::uint8_t *destination, const std::uint8_t *source,
        std::uint32_t sourceSize) {
        // The four no-palette branches use the same signed low-word size gate.
        const tjs_int signedSourceSize = signedW32(sourceSize);
        const std::uint8_t *const sourceEnd = source + signedSourceSize;
        if(signedSourceSize < 1) {
            return;
        }
        auto *output = reinterpret_cast<tjs_uint32 *>(destination);
        do {
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
        } while(source < sourceEnd);
    }

    iTVPTexture2D *textureFromLayerVariant(const tTJSVariant &value) {
        // The render-source path uses the engine's strict Layer conversion,
        // then unconditionally follows Layer -> MainImage -> Texture.  Native
        // query failure throws TVPSpecifyLayer before either image call.
        auto *layer = tTJSNI_Layer::FromVariant(value);
        return layer->GetMainImage()->GetTexture();
    }

} // namespace

namespace motion {

    ObjSource::~ObjSource() {
        // ObjSource's explicit body runs before PSBRawNode's implicit member
        // destructor, so the retained texture is released before the PSB owner.
        if(_texture) {
            _texture->Release();
        }
    }

    tTJSVariant ObjSource::getClip() const {
        // All four clip wrappers category-gate and try-get only `clip`; once
        // present, every child read is a strict raw-node operation.
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

    void ObjSource::ensureTexture_guess() {
        // All four references return once the lazy texture is non-null. Every
        // following read is a strict raw-node read; the original source
        // identifier is stripped.
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
        // All four pass the same uninitialized uint32 stack slot to every
        // resource lookup.
        std::uint32_t resourceSize;
        const std::uint8_t *pixelData = nullptr;
        // These aligned buffers are deliberately raw. None of the four
        // references installs an unwind owner for decoded/BGRA storage, so an
        // exception after allocation preserves the native leak boundary.
        std::uint8_t *decoded = nullptr;
        const std::uint8_t *sourcePixels = nullptr;
        if(compressed) {
            // Each reference checks `pal` here to select the RL element width;
            // the common palette branch checks it again later.
            const bool compressedHasPalette =
                _source.ContainsDictionaryKey("pal");
            pixelData = _source.GetDictionaryValueStrict("pixel")
                            .GetResource(resourceSize);
            const tjs_uint decodedBytes =
                (compressedHasPalette ? 1u : 4u) * pixelCount;
            decoded = static_cast<std::uint8_t *>(
                TJSAlignedAlloc(decodedBytes, 4));
            if(compressedHasPalette) {
                decodeObjSourceRL8_guess(
                    decoded, pixelData, resourceSize);
            } else {
                decodeObjSourceRL32_guess(
                    decoded, pixelData, resourceSize);
                TVPReverseRGB(
                    reinterpret_cast<tjs_uint32 *>(decoded),
                    reinterpret_cast<const tjs_uint32 *>(decoded),
                    static_cast<tjs_int>(pixelCount));
            }
            sourcePixels = decoded;
        } else {
            pixelData = _source.GetDictionaryValueStrict("pixel")
                            .GetResource(resourceSize);
            sourcePixels = pixelData;
        }

        const bool hasPalette = _source.ContainsDictionaryKey("pal");
        std::uint8_t *bgra = nullptr;
        if(hasPalette) {
            const auto *paletteData =
                _source.GetDictionaryValueStrict("pal")
                    .GetResource(resourceSize);
            // All four use signed division by four for both the vector element
            // count and TVPReverseRGB length.
            const tjs_int paletteCount =
                signedW32(resourceSize) /
                static_cast<tjs_int>(sizeof(tjs_uint32));
            std::vector<tjs_uint32> palette(
                static_cast<std::size_t>(paletteCount));
            TVPReverseRGB(
                palette.data(),
                reinterpret_cast<const tjs_uint32 *>(paletteData),
                paletteCount);
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

        // Android arm64 and both iOS references delete a still-pending
        // new-expression allocation if this constructor unwinds; Android
        // armv7's merged body has no local landing pad. Once construction
        // returns, no reference has a guard for the bitmap. This raw pointer is
        // therefore load-bearing source structure.
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
        // The returned retained reference is published directly. If render
        // manager lookup or CreateTexture2D throws, the store has not happened:
        // the member remains null while the constructed bitmap and BGRA buffer
        // have no exception cleanup. Once the call returns, publication
        // precedes bitmap Release and aligned deallocation; a later failure
        // therefore leaves the retained member committed. There is no
        // temporary texture owner or later swap/commit step.
        _texture = TVPGetRenderManager()->CreateTexture2D(bitmap);
        bitmap->Release();
        TJSAlignedDealloc(bgra);
    }

    void ObjSource::drawLayer(tTJSVariant target) {
        // Each current drawLayer wrapper gates only on raw source category 7,
        // then materialises, assigns and sizes the texture.
        if(_source.GetTypeCategory() != 7) {
            return;
        }
        ensureTexture_guess();
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

    SourceCache::SourceCache(tTJSVariant owner, tjs_int cacheSize) :
        _owner(owner),
        _cacheLimitBytes(static_cast<std::uint32_t>(cacheSize)) {
        // The current four constructors perform a strict Object conversion,
        // read primaryLayer, then ask the global dispatch to CreateNew the
        // Layer member. There is no native-layer fallback or null recovery.
        ncbPropAccessor ownerAccessor{tTJSVariant(owner)};
        _primaryLayer = ownerAccessor.GetValue(
            TJS_W("primaryLayer"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::primaryLayerMemberHint_guess);
        tTJSVariant createdLayer =
            detail::createLayerVariant_guess(_owner, _primaryLayer);
        // Keep a closure owner distinct from both constructor inputs.  The
        // public getter returns ordinary CopyRef aliases of this same Layer.
        _bufLayer = createdLayer;
    }

    // Native destruction does not reuse the public clearCache callback: the
    // list destructor releases cached entry Variants directly, then the three
    // persistent Variants unwind in reverse member order.  In particular,
    // cached Layers receive no script-visible Invalidate call at this boundary.
    SourceCache::~SourceCache() = default;

    tTJSVariant SourceCache::loadSource(iTJSDispatch2 *source,
                                        iTJSDispatch2 *descriptor) {
        // All four current callbacks receive two borrowed dispatches.
        // ncbPropAccessor supplies temporary AddRef/Release while the cache
        // entry itself never retains `source`.
        ncbPropAccessor descriptorAccessor(descriptor);
        // Construct one complete candidate before reading the descriptor.
        // key/layer/src and byteWeight initialize; blendMode/colors do not.
        Entry entry;

        entry.key = descriptorAccessor.GetValue(
            TJS_W("key"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::commandKeyMemberHint_guess);
        entry.src = descriptorAccessor.getStrValue(TJS_W("src"), ttstr());
        entry.blendMode = descriptorAccessor.getIntValue(
            TJS_W("blendMode"), 0);

        tTJSVariant colorValue = descriptorAccessor.GetValue(
            TJS_W("color"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &detail::colorMemberHint_guess);
        if(colorValue.Type() != tvtVoid) {
            ncbPropAccessor colorAccessor(colorValue);
            for(tjs_int index = 0; index < 4; ++index) {
                entry.colors[static_cast<std::size_t>(index)] =
                    colorAccessor.getIntValue(index, 0);
            }
        } else {
            // Only slot zero is written on this branch.  The later cache
            // comparison, node copy and tint path still consume slots 1..3,
            // preserving the original source-level indeterminate-value bug.
            entry.colors[0] = (entry.blendMode & 0xF0) != 0
                ? static_cast<tjs_int>(0xFF808080u)
                : static_cast<tjs_int>(0xFFFFFFFFu);
        }

        tTJSVariant result;
        for(auto it = _entries.begin(); it != _entries.end(); ++it) {
            // The native 32-bit and iOS arm64 builds call a compact helper
            // comparing this exact triple; Android arm64 inlines the same
            // Variant-key, string and integer comparisons into the list walk.
            if(it->key.DiscernCompareStrictReal(entry.key) &&
               it->src == entry.src && it->blendMode == entry.blendMode) {
                result = it->layer;
                if(packedColorsEqual(it->colors, entry.colors)) {
                    // An unchanged hit returns in place; it is not promoted.
                    return result;
                }

                // A color-changing hit updates only the four packed colors.
                copyPackedColors(it->colors, entry.colors);
                bakeSource_guess(source, *it);
                // This is push_front(copy) followed by erase(old), not splice:
                // key/Layer/src AddRef before the old node is destroyed.
                _entries.push_front(*it);
                _entries.erase(it);
                return result;
            }
        }

        trimCacheBeforeInsert_guess();
        {
            tTJSVariant createdLayer =
                detail::createLayerVariant_guess(
                    _owner, _primaryLayer);
            entry.layer = createdLayer;
            result = entry.layer;
        }
        bakeSource_guess(source, entry);
        _currentCacheBytes += static_cast<std::uint32_t>(entry.byteWeight);
        _entries.push_front(entry);
        return result;
    }

    tTJSVariant Player::resolveRenderSource_guess(
        const tTJSVariant &sourceObject) {
        tTJSVariant result;
        // The resolver compares only the two Variant Object dispatch pointers.
        // Typed-null Objects therefore also compare equal; do not add a
        // non-null safety gate that the references do not have.
        if(sourceObject.Type() == tvtObject &&
           _internalRenderLayer.Type() == tvtObject &&
            sourceObject.AsObjectNoAddRef() ==
               _internalRenderLayer.AsObjectNoAddRef()) {
            ncbPropAccessor descriptor{tTJSVariant(_sourceDescriptor)};
            const tjs_int blendMode = descriptor.GetValue(
                TJS_W("blendMode"), ncbTypedefs::Tag<tjs_int>(), 0,
                &detail::blendModeMemberHint_guess);

            ncbPropAccessor color{tTJSVariant(_sourceColors)};
            tjs_int colors[4];
            for(tjs_int index = 0; index < 4; ++index) {
                colors[static_cast<std::size_t>(index)] =
                    color.GetValue(
                        index, ncbTypedefs::Tag<tjs_int>(), 0);
            }

            ncbPropAccessor work{
                tTJSVariant(_internalSourceWorkLayer_guess)};
            work.FuncCall(0, TJS_W("assignImages"),
                          &detail::assignImagesMemberHint_guess, &result,
                          _internalRenderLayer);
            const tjs_int height = getRenderSourceDimension_guess(
                work, TJS_W("height"),
                &detail::heightMemberHint_guess);
            const tjs_int width = getRenderSourceDimension_guess(
                work, TJS_W("width"),
                &detail::widthMemberHint_guess);
            applyPackedCornerTint_guess(
                _internalSourceWorkLayer_guess, colors,
                TintRect_guess{0, 0, width, height},
                (blendMode & 0xF0) == 0x10);
            return result;
        }

        ncbPropAccessor cache{tTJSVariant(_sourceCacheObject)};
        cache.FuncCall(0, TJS_W("loadSource"),
                       &detail::loadSourceMemberHint_guess, &result,
                       sourceObject, _sourceDescriptor);
        return result;
    }

    tTJSVariant SourceCache::loadRenderSourceLayerFromItem_guess(
        Player &player,
        const detail::PreparedRenderItem &item) {
        // Player owns one persistent descriptor Dictionary and one persistent
        // color Dictionary. Every caller overwrites these exact objects before
        // entering the resolver; only its fallback dispatches loadSource.
        ncbPropAccessor descriptor{tTJSVariant(player._sourceDescriptor)};
        descriptor.SetValue(TJS_W("key"), item.commandKey, TJS_MEMBERENSURE,
                            &detail::commandKeyMemberHint_guess);
        descriptor.SetValue(TJS_W("src"), item.commandSrc, TJS_MEMBERENSURE,
                            &detail::srcMemberHint_guess);
        descriptor.SetValue(TJS_W("blendMode"),
                            static_cast<tjs_int>(item.blendMode),
                            TJS_MEMBERENSURE,
                            &detail::blendModeMemberHint_guess);

        ncbPropAccessor color{tTJSVariant(player._sourceColors)};
        for(tjs_int index = 0; index < 4; ++index) {
            color.SetValue(
                index,
                // Every reference zero-extends the packed uint32_t into the
                // 64-bit TJS Integer payload.
                item.packedColors[static_cast<std::size_t>(index)],
                TJS_MEMBERENSURE);
        }

        auto &source = *item.sourceState;
        return player.resolveRenderSource_guess(source.object);
    }

    iTVPTexture2D *
    SourceCache::loadRenderSourceTextureFromItem_guess(
        Player &player,
        detail::PreparedRenderItem &item) {
        return textureFromLayerVariant(
            loadRenderSourceLayerFromItem_guess(player, item));
    }

    iTVPTexture2D *SourceCache::loadRenderSourceTextureForItem_guess(
        Player &player,
        D3DAdaptor &adaptor,
        detail::PreparedRenderItem &item) {
        auto &source = *item.sourceState;
        // The four-reference getter observes the persistent descriptor first
        // and returns an existing atlas borrow before even asking whether the
        // process renderer is software. Only the generic Layer fallback below
        // is eligible for the adaptor's software-copy map.
        if(source.texture) {
            return source.texture;
        }

        // Native-instance extraction precedes materializing the temporary
        // motion-context ttstr in every reference. This path calls strict
        // Variant::AsObject(), which AddRefs a nonnull dispatch, then asks for
        // the ResourceManager adaptor without ever releasing that new dispatch
        // reference. Preserve that per-retry leak instead of using nativeRM()'s
        // friendly, borrowed fast pointer.
        iTJSDispatch2 *resourceManagerDispatch =
            player._findSourceResourceManager.AsObject();
        ResourceManager *resourceManager =
            ncbInstanceAdaptor<ResourceManager>::GetNativeInstance(
                resourceManagerDispatch);
        bool atlasLoaded;
        {
            // Materialize a temporary ttstr from the Player-owned motion-context
            // Variant, call the shared helper, then destroy the temporary before
            // testing its result/texture.
            const ttstr moduleKey =
                static_cast<ttstr>(player._findMotionContextVariant);
            atlasLoaded = Player::loadKrkrAtlasSource_guess(
                source, resourceManager, moduleKey);
        }
        if(atlasLoaded && source.texture) {
            // A newly recovered atlas borrow takes the same pre-software return
            // as the initial fast path.
            return source.texture;
        }

        // The helper may have cleared source.object before failing.  Pass the
        // post-call object onward without consulting source.path; the fallback
        // receives only that object plus the prepared descriptor. Its Layer
        // main-image texture is the sole source passed through the software
        // renderer bridge/cache.
        return adaptor.getRenderTexture_guess(
            loadRenderSourceTextureFromItem_guess(player, item));
    }

    void SourceCache::clearCache() {
        // The persistent scratch Layer is not a cache entry.  Native clearCache
        // walks only this list, invalidates its entry Layers, releases the
        // nodes, and resets the accumulated byte count.
        for(auto &entry : _entries) {
            if(entry.layer.Type() == tvtObject) {
                // All four callbacks test only the Variant type tag, then
                // dereference Object without a typed-null recovery branch.
                // They also pass Object itself as objthis, not closure.ObjThis.
                auto *object = entry.layer.AsObjectNoAddRef();
                (void)object->Invalidate(0, nullptr, nullptr, object);
            }
        }
        _entries.clear();
        _currentCacheBytes = 0;
    }

    tTJSVariant SourceCache::getBufLayer() const {
        // Returning by value deliberately preserves the original closure's
        // Object and ObjThis identity while acquiring an independent owner.
        return _bufLayer;
    }

    std::size_t SourceCache::size() const {
        return _entries.size();
    }

    void SourceCache::bakeSource_guess(iTJSDispatch2 *source, Entry &entry) {
        // The reference constructs one Void result Variant before drawLayer
        // and reuses that same storage for every later scratch-layer call.
        // Ordinary HRESULT failures neither clear it nor stop the chain; its
        // final value is released only after the Layer accessor is destroyed.
        tTJSVariant dispatchResult;
        {
            ncbPropAccessor sourceAccessor(source);
            sourceAccessor.FuncCall(0, TJS_W("drawLayer"),
                                    &detail::drawLayerMemberHint_guess,
                                    &dispatchResult, entry.layer);
        }

        ncbPropAccessor layer(entry.layer);
        const tjs_int width = layer.getIntValue(TJS_W("width"), 0);
        const tjs_int height = layer.getIntValue(TJS_W("height"), 0);
        // All four targets perform one 32-bit width*height MUL followed by a
        // two-bit left shift; retain the low word without C++ signed-overflow
        // undefined behavior.
        entry.byteWeight = multiplyW32(multiplyW32(width, height), 4);

        applyPackedCornerTint_guess(
            entry.layer, entry.colors,
            TintRect_guess{0, 0, width, height},
            (entry.blendMode & 0xF0) != 0);

        const tjs_int lowBlend = entry.blendMode & 0x0F;
        if(static_cast<tjs_uint>(lowBlend - 1) < 2u) {
            const bool software = TVPIsSoftwareRenderManager();
            if(software ||
               !queryPrivateMotionGLLNativeFromVariant_guess(entry.layer)) {
                ncbPropAccessor buffer(_bufLayer);
                buffer.FuncCall(0, TJS_W("setSize"),
                                &detail::setSizeMemberHint_guess,
                                &dispatchResult,
                                tTJSVariant(width), tTJSVariant(height));
                buffer.FuncCall(0, TJS_W("copyRect"),
                                &detail::copyRectMemberHint_guess,
                                &dispatchResult,
                                tTJSVariant(0), tTJSVariant(0), entry.layer,
                                tTJSVariant(0), tTJSVariant(0),
                                tTJSVariant(width), tTJSVariant(height));
                layer.FuncCall(0, TJS_W("fillRect"),
                               &detail::fillRectMemberHint_guess,
                               &dispatchResult,
                               tTJSVariant(0), tTJSVariant(0),
                               tTJSVariant(width), tTJSVariant(height),
                               // The Integer payload is zero-extended from the
                               // packed 32-bit ARGB value on both 32/64-bit
                               // references, so this is +4278190080.
                               tTJSVariant(
                                   static_cast<tjs_int64>(0xFF000000u)));
                layer.FuncCall(0, TJS_W("operateRect"),
                               &detail::operateRectMemberHint_guess,
                               &dispatchResult,
                               tTJSVariant(0), tTJSVariant(0), _bufLayer,
                               tTJSVariant(0), tTJSVariant(0),
                               tTJSVariant(width), tTJSVariant(height),
                               tTJSVariant(15));
                if(lowBlend == 2) {
                    layer.FuncCall(
                        0, TJS_W("adjustGamma"),
                        &detail::adjustGammaMemberHint_guess,
                        &dispatchResult,
                        tTJSVariant(1), tTJSVariant(255), tTJSVariant(0),
                        tTJSVariant(1), tTJSVariant(255), tTJSVariant(0),
                        tTJSVariant(1), tTJSVariant(255), tTJSVariant(0));
                }
            }
        }
    }

    void SourceCache::trimCacheBeforeInsert_guess() {
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

} // namespace motion
