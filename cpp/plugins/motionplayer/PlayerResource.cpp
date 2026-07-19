// PlayerResource.cpp — Resource management: unload, findMotion, layerId
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "RenderManager.h"
#include "SourceCache.h"
#include "../../core/visual/ogl/imagepacker.h"

#include <algorithm>
#include <memory>

using namespace motion::internal;

extern unsigned int TVPMaxTextureSize;

namespace motion {

    namespace {
        struct WinSourceKeyLike_0x6948E8 {
            std::string group;
            std::string icon;
            std::string path;
        };

        WinSourceKeyLike_0x6948E8 splitWinSourceKeyLike_0x6948E8(
            const detail::MotionNode::ClipSlot &slot) {
            WinSourceKeyLike_0x6948E8 key;
            std::string src = detail::narrow(slot.srcValue);
            const std::string icon = detail::narrow(slot.iconValue);
            if(src.rfind("src/", 0) == 0) {
                src.erase(0, 4);
            }
            const auto slash = src.find('/');
            if(slash == std::string::npos) {
                key.group = src;
                key.icon = icon;
            } else {
                key.group = src.substr(0, slash);
                key.icon =
                    icon.empty() ? src.substr(slash + 1) : icon;
            }
            if(!key.group.empty() && !key.icon.empty()) {
                key.path = "src/" + key.group + "/" + key.icon;
            }
            return key;
        }

        bool findWinSourceGroupLike_0x6948E8(
            detail::LoadedResourceRecord &loadedResource,
            const std::string &group, PSB::PSBRawNode &groupNode) {
            const PSB::PSBRawNode sourceRoot =
                loadedResource.file.GetRoot()
                    .GetDictionaryValueStrict("source");
            // 0x694AEC..0x694B44 uses the non-throwing group lookup and
            // transfers control to the spec-1/fallback route on a miss.
            return sourceRoot.GetDictionaryValue(group, groupNode);
        }

        iTVPTexture2D *loadWinAtlasTextureLike_0x6948E8(
            const PSB::PSBRawNode &groupNode,
            detail::LoadedResourceRecord &loadedResource,
            const std::string &group) {
            const ttstr groupKey = detail::widen(group);
            if(const auto it = loadedResource.winSourceTextures.find(groupKey);
               it != loadedResource.winSourceTextures.end()) {
                return it->second.texture;
            }

            const PSB::PSBRawNode textureNode =
                groupNode.GetDictionaryValueStrict("texture");
            // 0x694C8C..0x694D14 performs these two conversions and discards
            // both results; width/height below do not fall back to them.
            (void)textureNode.GetDictionaryValueStrict("truncated_width")
                .GetInt();
            (void)textureNode.GetDictionaryValueStrict("truncated_height")
                .GetInt();
            const int width = textureNode.GetDictionaryValueStrict("width")
                                  .GetInt();
            const int height = textureNode.GetDictionaryValueStrict("height")
                                   .GetInt();
            const char *type =
                textureNode.GetDictionaryValueStrict("type").GetString();
            const PSB::PSBRawNode pixelNode =
                textureNode.GetDictionaryValueStrict("pixel");
            std::uint32_t sourceSize = 0;
            const std::uint8_t *sourcePixels =
                pixelNode.GetResource(sourceSize);

            const size_t destinationSize =
                static_cast<size_t>(4) * static_cast<size_t>(width) *
                static_cast<size_t>(height);
            // sub_A0DE48 in 0x694E64 returns an uninitialised allocation. The
            // binary transforms only the raw resource byte count, so do not
            // zero-fill the unused tail here.
            std::unique_ptr<std::uint8_t[]> bgra(
                new std::uint8_t[destinationSize]);
            if(type != nullptr && std::strcmp(type, "RGBA8") == 0) {
                TVPReverseRGB(
                              reinterpret_cast<tjs_uint32 *>(bgra.get()),
                              reinterpret_cast<const tjs_uint32 *>(sourcePixels),
                              static_cast<tjs_int>(sourceSize / 4u));
            } else if(type != nullptr && std::strcmp(type, "A8L8") == 0) {
                // 0x694EBC..0x694F10 reads [alpha,luminance] and writes
                // [luminance,luminance,luminance,alpha].
                size_t destinationOffset = 0;
                for(std::uint32_t sourceOffset = 0;
                    sourceOffset < sourceSize; sourceOffset += 2) {
                    const std::uint8_t alpha = sourcePixels[sourceOffset];
                    const std::uint8_t luminance =
                        sourcePixels[sourceOffset + 1];
                    bgra[destinationOffset + 0] = luminance;
                    bgra[destinationOffset + 1] = luminance;
                    bgra[destinationOffset + 2] = luminance;
                    bgra[destinationOffset + 3] = alpha;
                    destinationOffset += 4;
                }
            } else {
                TVPThrowExceptionMessage(
                    TJS_W("MotionPlayer.findSource: Unsupported texture format '%1'"),
                    ttstr(type != nullptr ? type : ""));
            }

            auto *texture = TVPGetRenderManager()->CreateTexture2D(
                bgra.get(), width * 4, width, height, TVPTextureFormat::RGBA,
                RENDER_CREATE_TEXTURE_FLAG_ANY);
            if(!texture) {
                return nullptr;
            }
            auto [cached, inserted] =
                loadedResource.winSourceTextures.try_emplace(groupKey);
            (void)inserted;
            cached->second.setTexture(texture);
            // 0x694FAC retains the map value; 0x694FBC releases the texture's
            // construction reference, leaving the nested map as sole owner.
            texture->Release();
            return cached->second.texture;
        }

        struct KrkrAtlasRecordLike_0x695DE8 : ImagePacker::rect_xywhf {
            std::string iconName;
            std::string sourceKey;
            PSB::PSBRawNode iconNode;
            std::uint8_t *bgra = nullptr;
            int contentWidth = 0;
            int contentHeight = 0;

            ~KrkrAtlasRecordLike_0x695DE8() { delete[] bgra; }
        };

        void decodeKrkrRL8Like_0x696E40(
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

        void decodeKrkrRL32Like_0x696D00(
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

        void decodeKrkrAtlasRecordLike_0x695DE8(
            const std::string &group, const std::string &iconName,
            const PSB::PSBRawNode &iconNode,
            KrkrAtlasRecordLike_0x695DE8 &record) {
            record.iconName = iconName;
            record.sourceKey = "src/" + group + "/" + iconName;
            record.iconNode = iconNode;

            const int width = iconNode.GetDictionaryValueStrict("width").GetInt();
            const int height =
                iconNode.GetDictionaryValueStrict("height").GetInt();
            record.contentWidth = width;
            record.contentHeight = height;
            record.x = 0;
            record.y = 0;
            record.w = width + 1;
            record.h = height + 1;

            const size_t pixelCount = static_cast<size_t>(record.contentWidth) *
                static_cast<size_t>(record.contentHeight);
            record.bgra = new std::uint8_t[pixelCount * sizeof(tjs_uint32)];

            PSB::PSBRawNode compressNode;
            const bool compressed =
                iconNode.GetDictionaryValue("compress", compressNode) &&
                std::strcmp(compressNode.GetString(), "RL") == 0;
            const PSB::PSBRawNode pixelNode =
                iconNode.GetDictionaryValueStrict("pixel");
            std::uint32_t pixelSize = 0;
            const std::uint8_t *pixelData = pixelNode.GetResource(pixelSize);

            if(iconNode.ContainsDictionaryKey("pal")) {
                auto *indexes = new std::uint8_t[pixelCount];
                if(compressed) {
                    decodeKrkrRL8Like_0x696E40(indexes, pixelData, pixelSize);
                } else {
                    // 0x6970A8 copies the PSB resource length rather than the
                    // destination pixel count.
                    std::memcpy(indexes, pixelData, pixelSize);
                }

                const PSB::PSBRawNode paletteNode =
                    iconNode.GetDictionaryValueStrict("pal");
                std::uint32_t paletteSize = 0;
                const std::uint8_t *paletteData =
                    paletteNode.GetResource(paletteSize);
                std::vector<tjs_uint32> palette(paletteSize / 4u);
                TVPReverseRGB(
                    palette.data(),
                    reinterpret_cast<const tjs_uint32 *>(paletteData),
                    static_cast<tjs_int>(paletteSize / 4u));
                TVPBLExpand8BitTo32BitPal(
                    reinterpret_cast<tjs_uint32 *>(record.bgra), indexes,
                    static_cast<tjs_int>(pixelCount), palette.data());
                delete[] indexes;
            } else if(compressed) {
                decodeKrkrRL32Like_0x696D00(record.bgra, pixelData, pixelSize);
                TVPReverseRGB(
                    reinterpret_cast<tjs_uint32 *>(record.bgra),
                    reinterpret_cast<const tjs_uint32 *>(record.bgra),
                    static_cast<tjs_int>(pixelCount));
            } else {
                TVPReverseRGB(
                    reinterpret_cast<tjs_uint32 *>(record.bgra),
                    reinterpret_cast<const tjs_uint32 *>(pixelData),
                    static_cast<tjs_int>(pixelCount));
            }

            bool anyAlpha = false;
            for(size_t i = 0; i < pixelCount; ++i) {
                if(record.bgra[i * 4u + 3u] != 0) {
                    anyAlpha = true;
                    break;
                }
            }
            if(!anyAlpha) {
                // 0x697210..0x697248: an entirely transparent image drops its
                // pixel buffer and replaces the padded rectangle with 2x2.
                delete[] record.bgra;
                record.bgra = nullptr;
                record.contentWidth = 1;
                record.contentHeight = 1;
                record.w = 2;
                record.h = 2;
            }
        }

        bool buildKrkrAtlasGroupLike_0x695DE8(
            detail::LoadedResourceRecord &loadedResource,
            const std::string &requestedGroup,
            const std::string &requestedIcon) {
            const PSB::PSBRawNode sourceRoot =
                loadedResource.file.GetRoot()
                    .GetDictionaryValueStrict("source");
            PSB::PSBRawNode requestedGroupNode;
            if(!sourceRoot.GetDictionaryValue(requestedGroup,
                                              requestedGroupNode)) {
                return false;
            }
            const PSB::PSBRawNode requestedIconRoot =
                requestedGroupNode.GetDictionaryValueStrict("icon");
            PSB::PSBRawNode requestedIconNode;
            if(!requestedIconRoot.GetDictionaryValue(requestedIcon,
                                                     requestedIconNode)) {
                return false;
            }

            std::vector<std::unique_ptr<KrkrAtlasRecordLike_0x695DE8>> records;
            for(const auto &group : sourceRoot.GetDictionaryKeys()) {
                const PSB::PSBRawNode groupNode =
                    sourceRoot.GetDictionaryValueStrict(group);
                const PSB::PSBRawNode iconRoot =
                    groupNode.GetDictionaryValueStrict("icon");
                for(const auto &iconName : iconRoot.GetDictionaryKeys()) {
                    const PSB::PSBRawNode iconNode =
                        iconRoot.GetDictionaryValueStrict(iconName);
                    auto record =
                        std::make_unique<KrkrAtlasRecordLike_0x695DE8>();
                    decodeKrkrAtlasRecordLike_0x695DE8(
                        group, iconName, iconNode, *record);
                    records.push_back(std::move(record));
                }
            }

            if(records.empty()) {
                return true;
            }

            std::vector<ImagePacker::rect_xywhf *> recordPointers;
            recordPointers.reserve(records.size());
            for(const auto &record : records) {
                recordPointers.push_back(record.get());
            }

            std::vector<ImagePacker::bin> bins;
            const int maxSide = static_cast<int>(TVPMaxTextureSize);
            const bool packed = ImagePacker::pack(
                recordPointers.data(), static_cast<int>(recordPointers.size()),
                maxSide, bins);
            if(!packed) {
                return false;
            }

            for(auto &bin : bins) {
                if(bin.size.w <= 0 || bin.size.h <= 0) {
                    continue;
                }
                const size_t atlasStride = static_cast<size_t>(bin.size.w) * 4u;
                std::vector<std::uint8_t> atlas(
                    atlasStride * static_cast<size_t>(bin.size.h), 0);
                for(auto *baseRect : bin.rects) {
                    auto *record =
                        static_cast<KrkrAtlasRecordLike_0x695DE8 *>(baseRect);
                    if(record->bgra == nullptr) {
                        continue;
                    }
                    const size_t sourceStride =
                        static_cast<size_t>(record->contentWidth) * 4u;
                    for(int y = 0; y < record->contentHeight; ++y) {
                        const size_t destinationOffset =
                            (static_cast<size_t>(record->y + y) *
                                 static_cast<size_t>(bin.size.w) +
                             static_cast<size_t>(record->x)) *
                            4u;
                        std::memcpy(atlas.data() + destinationOffset,
                                    record->bgra +
                                        static_cast<size_t>(y) * sourceStride,
                                    sourceStride);
                    }
                }

                // Android sub_695DE8 creates an owned empty page, then uploads
                // each packed sub-rect through iTVPTexture2D::Update. The Web
                // software texture cannot update a non-zero left offset, so
                // assemble the identical page in CPU memory and perform one
                // full-page Update. Passing atlas.data() to CreateTexture2D
                // would retain a dangling external pointer after this return.
                auto *texture = TVPGetRenderManager()->CreateTexture2D(
                    nullptr, static_cast<int>(atlasStride),
                    static_cast<unsigned int>(bin.size.w),
                    static_cast<unsigned int>(bin.size.h),
                    TVPTextureFormat::RGBA, RENDER_CREATE_TEXTURE_FLAG_ANY);
                if(texture) {
                    texture->Update(atlas.data(), TVPTextureFormat::RGBA,
                                    static_cast<int>(atlasStride),
                                    tTVPRect(0, 0, bin.size.w, bin.size.h));
                }
                for(auto *baseRect : bin.rects) {
                    auto *record =
                        static_cast<KrkrAtlasRecordLike_0x695DE8 *>(baseRect);
                    detail::PackedSourceAtlasEntry entry;
                    entry.setTexture(texture);
                    entry.originX = record->iconNode
                                        .GetDictionaryValueStrict("originX")
                                        .GetInt();
                    entry.originY = record->iconNode
                                        .GetDictionaryValueStrict("originY")
                                        .GetInt();
                    // 0x696A18..0x696A30 stores padded width/height followed
                    // by inclusive right/bottom in the four-int descriptor.
                    entry.textureRect = {
                        record->w,
                        record->h,
                        record->x + record->w - 1,
                        record->y + record->h - 1,
                    };
                    PSB::PSBRawNode clipNode;
                    if(record->iconNode.GetDictionaryValue("clip", clipNode)) {
                        entry.clip = {
                            clipNode.GetDictionaryValueStrict("left").GetDouble(),
                            clipNode.GetDictionaryValueStrict("top").GetDouble(),
                            clipNode.GetDictionaryValueStrict("right").GetDouble(),
                            clipNode.GetDictionaryValueStrict("bottom").GetDouble(),
                        };
                    }
                    loadedResource.krkrSourceEntries.insert_or_assign(
                        detail::widen(record->sourceKey), std::move(entry));
                }
                // Each cached icon entry took its own AddRef above.  Release
                // the page-construction reference like 0x696C20.
                if(texture) {
                    texture->Release();
                }
            }
            return true;
        }

        const detail::PackedSourceAtlasEntry *
        findKrkrAtlasSourceLike_0x695DE8(
            detail::LoadedResourceRecord &loadedResource,
            const std::string &group, const std::string &icon) {
            const ttstr sourceKey =
                detail::widen("src/" + group + "/" + icon);
            auto sourceIt = loadedResource.krkrSourceEntries.find(sourceKey);
            if(sourceIt == loadedResource.krkrSourceEntries.end()) {
                if(!buildKrkrAtlasGroupLike_0x695DE8(
                       loadedResource, group, icon)) {
                    return nullptr;
                }
                sourceIt = loadedResource.krkrSourceEntries.find(sourceKey);
            }
            return sourceIt == loadedResource.krkrSourceEntries.end()
                ? nullptr
                : &sourceIt->second;
        }
    } // namespace

    void Player::findSourceForNodeLike_0x6948E8(detail::MotionNode &node) {
        auto &dst = node.source;
        dst.clear();

        ResourceManager *resourceManager = nativeRM();
        const ttstr motionContext =
            static_cast<ttstr>(_findMotionContextVariant);
        detail::LoadedResourceRecord *loadedResource = nullptr;
        if(resourceManager) {
            loadedResource = resourceManager->findLoadedResourceRecord(
                motionContext);
        }
        const int sourceSpec = resourceManager ? resourceManager->_spec : 0;
        const std::string tracePath = motionContext.AsStdString();

        const auto key = splitWinSourceKeyLike_0x6948E8(node.activeSlot());
        if(sourceSpec == 2 && !key.group.empty() &&
           !key.icon.empty() && loadedResource) {
            PSB::PSBRawNode groupNode;
            if(findWinSourceGroupLike_0x6948E8(
                   *loadedResource, key.group, groupNode)) {
                const PSB::PSBRawNode iconNode =
                    groupNode.GetDictionaryValueStrict("icon")
                        .GetDictionaryValueStrict(key.icon);
                dst.texture = loadWinAtlasTextureLike_0x6948E8(
                    groupNode, *loadedResource, key.group);
                dst.width = static_cast<double>(
                    iconNode.GetDictionaryValueStrict("width").GetInt());
                dst.height = static_cast<double>(
                    iconNode.GetDictionaryValueStrict("height").GetInt());
                dst.originX = static_cast<double>(
                    iconNode.GetDictionaryValueStrict("originX").GetInt());
                dst.originY = static_cast<double>(
                    iconNode.GetDictionaryValueStrict("originY").GetInt());
                const int left =
                    iconNode.GetDictionaryValueStrict("left").GetInt();
                const int top =
                    iconNode.GetDictionaryValueStrict("top").GetInt();
                dst.textureRect = {
                    left, top, left + static_cast<int>(dst.width),
                    top + static_cast<int>(dst.height)
                };
                dst.clipLeft = dst.clipTop = 0.0;
                dst.clipRight = dst.clipBottom = 1.0;
                dst.path = key.path;
                dst.valid = true;
                dst.blank = false;
                detail::logoChainTraceLogf(
                    tracePath, "player.findSource", "0x6948E8",
                    _clampedEvalTime,
                    "spec=win group={} icon={} valid=1 atlas={} "
                    "size={}x{} rect=[{},{},{},{}]",
                    key.group, key.icon,
                    static_cast<const void *>(dst.texture), dst.width,
                    dst.height, dst.textureRect[0], dst.textureRect[1],
                    dst.textureRect[2], dst.textureRect[3]);
                return;
            }
        }

        const std::string path = !key.path.empty()
            ? key.path
            : detail::narrow(node.activeSlot().srcValue);
        if(path.empty()) {
            return;
        }
        dst.path = path;

        // sub_6948E8 @0x694BA0: the krkr atlas route is gated by player+909
        // (D3D draw mode) and returns immediately on success. It is mutually
        // exclusive with the ResourceManager.findSource fallback below.
        if(sourceSpec == 1 && _d3dDrawMode && loadedResource &&
           !key.group.empty() && !key.icon.empty()) {
            if(const auto *packed = findKrkrAtlasSourceLike_0x695DE8(
                   *loadedResource, key.group, key.icon)) {
                dst.texture = packed->texture;
                dst.originX = packed->originX;
                dst.originY = packed->originY;
                dst.textureRect = packed->textureRect;
                dst.width = static_cast<double>(packed->textureRect[0]);
                dst.height = static_cast<double>(packed->textureRect[1]);
                dst.clipLeft = packed->clip[0];
                dst.clipTop = packed->clip[1];
                dst.clipRight = packed->clip[2];
                dst.clipBottom = packed->clip[3];
                dst.blank = false;
                dst.valid = true;
                detail::logoChainTraceLogf(
                    tracePath, "player.findSource", "0x6948E8",
                    _clampedEvalTime,
                    "spec=krkr-atlas group={} icon={} valid=1 atlas={} "
                    "size={}x{} rect=[{},{},{},{}]",
                    key.group, key.icon, static_cast<const void *>(dst.texture),
                    dst.width, dst.height, dst.textureRect[0],
                    dst.textureRect[1], dst.textureRect[2], dst.textureRect[3]);
                return;
            }
        }

        // 0x6952DC..0x695720: only an atlas miss/non-D3D route calls the TJS
        // ResourceManager.findSource facade, then reads every descriptor field
        // from the returned object rather than mixing it with PSB icon data.
        dst.object = findSource(detail::widen(path));
        if(dst.object.Type() != tvtObject || !dst.object.AsObjectNoAddRef()) {
            return;
        }
        auto readDispatchNumber = [](iTJSDispatch2 *object,
                                     const tjs_char *name, double fallback) {
            if(!object) {
                return fallback;
            }
            tTJSVariant value;
            return TJS_SUCCEEDED(
                       object->PropGet(0, name, nullptr, &value, object))
                ? static_cast<double>(value)
                : fallback;
        };
        iTJSDispatch2 *sourceObject = dst.object.AsObjectNoAddRef();
        dst.width = readDispatchNumber(sourceObject, TJS_W("width"), 0.0);
        dst.height = readDispatchNumber(sourceObject, TJS_W("height"), 0.0);
        dst.originX = readDispatchNumber(sourceObject, TJS_W("originX"), 0.0);
        dst.originY = readDispatchNumber(sourceObject, TJS_W("originY"), 0.0);
        dst.blank =
            readDispatchNumber(sourceObject, TJS_W("blank"), 0.0) != 0.0;
        dst.textureRect = { 0, 0, static_cast<int>(dst.width),
                            static_cast<int>(dst.height) };
        tTJSVariant clipValue;
        if(TJS_SUCCEEDED(sourceObject->PropGet(0, TJS_W("clip"), nullptr,
                                               &clipValue, sourceObject)) &&
           clipValue.Type() == tvtObject && clipValue.AsObjectNoAddRef()) {
            iTJSDispatch2 *clipObject = clipValue.AsObjectNoAddRef();
            dst.clipLeft = readDispatchNumber(clipObject, TJS_W("left"), 0.0);
            dst.clipTop = readDispatchNumber(clipObject, TJS_W("top"), 0.0);
            dst.clipRight = readDispatchNumber(clipObject, TJS_W("right"), 1.0);
            dst.clipBottom =
                readDispatchNumber(clipObject, TJS_W("bottom"), 1.0);
        }
        dst.valid = true;
        detail::logoChainTraceLogf(
            tracePath, "player.findSource", "0x6948E8",
            _clampedEvalTime, "spec={} path={} valid=1 blank={} size={}x{}",
            sourceSpec, path, dst.blank ? 1 : 0, dst.width,
            dst.height);
    }

    bool Player::isExistMotion(ttstr name) {
        // Player_isExistMotion @0x6D07F4 calls the Player+992 ResourceManager
        // dispatch with (+1012 project key,
        // "motion/<Player+968>/<name>"). It does not probe storage paths or
        // populate a Player-local cache.
        iTJSDispatch2 *rm = _resourceManager.Type() == tvtObject
            ? _resourceManager.AsObjectNoAddRef()
            : nullptr;
        if(!rm) {
            return false;
        }

        tTJSVariant project = _findMotionContextVariant;
        tTJSVariant path(
            TJS_W("motion/") + _stealthChara + TJS_W("/") + name);
        tTJSVariant *args[] = { &project, &path };
        tTJSVariant result;
        static tjs_uint32 hint = 0;
        if(TJS_FAILED(rm->FuncCall(0, TJS_W("isExistMotion"), &hint,
                                   &result, 2, args, rm))) {
            return false;
        }
	return result.operator bool();
}

    // P3-B (c) (2026-06-05): removed the port-invented by-name
    //   `Player::requireLayerId(ttstr name)` (node-name reuse + by-name alloc).
    //   binary has NO by-name layer-id path anywhere ("requireLayerIdForName"
    //   string: 0 hits; "requireLayerId" only ever called numparams=0). The
    //   render path (emitRenderItem@0x6C4E28 LABEL_28) allocates a FRESH id via
    //   the no-arg RM dispatch FuncCall, gated only by the item+20 latch — it
    //   does not look up or reuse a node's layerId by name. Render now calls
    //   dispatchRequireLayerId() directly (PlayerRenderExecute.cpp).

    void Player::releaseLayerId(tjs_int id) { dispatchReleaseLayerId(id); }

    // P3-B (d): layer-id alloc/release via the Player+992 RM dispatch FuncCall
    //   (see Player.h note). FuncCall routes to the NCB-registered native
    //   ResourceManager::requireLayerId/releaseLayerId, so the result is the
    //   same id — only the call chain matches the binary (3 require sites + 1
    //   release site all go through the dispatch, never a direct native call).
    tjs_int Player::dispatchRequireLayerId() const {
        iTJSDispatch2 *rm = _resourceManager.Type() == tvtObject
            ? _resourceManager.AsObjectNoAddRef()
            : nullptr;
        if(!rm) {
            return 0;
        }
        // Per-callsite member-hint cache (binary passes a tjs_uint32* hint;
        //   0xFFFFFFFF / 0 = uncached, filled on first lookup).
        static tjs_uint32 hint = 0;
        tTJSVariant result;
        // FuncCall(flag, membername, hint, result, numparams=0, params=NULL,
        //   objthis) — aligned to requireLayerId@0x6B4A6C/0x6C4E28/0x6DE738.
        if(TJS_FAILED(rm->FuncCall(0, TJS_W("requireLayerId"), &hint, &result,
                                   0, nullptr, rm))) {
            return 0;
        }
        return static_cast<tjs_int>(result.AsInteger());
    }

    void Player::dispatchReleaseLayerId(tjs_int id) const {
        iTJSDispatch2 *rm = _resourceManager.Type() == tvtObject
            ? _resourceManager.AsObjectNoAddRef()
            : nullptr;
        if(!rm) {
            return;
        }
        static tjs_uint32 hint = 0;
        tTJSVariant idVar(static_cast<tjs_int>(id));
        tTJSVariant *args[1] = { &idVar };
        // numparams=1 {id}, result discarded — aligned to releaseLayerId via
        //   resetAndReleaseNodes@0x6B56F8.
        rm->FuncCall(0, TJS_W("releaseLayerId"), &hint, nullptr, 1, args, rm);
    }


} // namespace motion
