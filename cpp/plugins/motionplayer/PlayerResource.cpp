// PlayerResource.cpp — Resource management: unload, findMotion, layerId
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "RenderManager.h"
#include "SourceCache.h"
#include "../../core/visual/ogl/imagepacker.h"

#include <algorithm>

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
            std::string src = slot.src;
            if(src.rfind("src/", 0) == 0) {
                src.erase(0, 4);
            }
            const auto slash = src.find('/');
            if(slash == std::string::npos) {
                key.group = src;
                key.icon = slot.icon;
            } else {
                key.group = src.substr(0, slash);
                key.icon = slot.icon.empty() ? src.substr(slash + 1)
                                             : slot.icon;
            }
            if(!key.group.empty() && !key.icon.empty()) {
                key.path = "src/" + key.group + "/" + key.icon;
            }
            return key;
        }

        iTVPTexture2D *loadWinAtlasTextureLike_0x6948E8(
            detail::MotionSnapshot &snapshot,
            const std::string &group) {
            const ttstr groupKey = detail::widen(group);
            if(const auto it = snapshot.sourceAtlasTextures.find(groupKey);
               it != snapshot.sourceAtlasTextures.end()) {
                return it->second;
            }

            const std::string texturePath = "source/" + group + "/texture";
            const auto textureNode = navigatePSBPath(snapshot.root, texturePath);
            if(!textureNode) {
                return nullptr;
            }
            const int width = static_cast<int>(
                psbDictionaryNumber(textureNode, "width")
                    .value_or(psbDictionaryNumber(textureNode,
                                                   "truncated_width")
                                  .value_or(0.0)));
            const int height = static_cast<int>(
                psbDictionaryNumber(textureNode, "height")
                    .value_or(psbDictionaryNumber(textureNode,
                                                   "truncated_height")
                                  .value_or(0.0)));
            const auto resourceIt =
                snapshot.resourcesByPath.find(texturePath + "/pixel");
            if(width <= 0 || height <= 0 ||
               resourceIt == snapshot.resourcesByPath.end() ||
               !resourceIt->second || resourceIt->second->data.empty()) {
                return nullptr;
            }

            bool decodedIsBgra = false;
            std::vector<std::uint8_t> decoded;
            const bool decodedOk = decodePsbPixelResource(
                snapshot, texturePath, *resourceIt->second, width, height,
                psbDictionaryString(textureNode, "compress") == "RL",
                decoded, &decodedIsBgra);
            const auto &input = decodedOk ? decoded : resourceIt->second->data;
            const size_t pixelCount = static_cast<size_t>(width) * height;
            std::vector<std::uint8_t> bgra(pixelCount * 4u, 0);
            if(input.size() >= pixelCount * 4u) {
                for(size_t i = 0; i < pixelCount; ++i) {
                    const auto *src = input.data() + i * 4u;
                    auto *dst = bgra.data() + i * 4u;
                    dst[0] = decodedIsBgra ? src[0] : src[2];
                    dst[1] = src[1];
                    dst[2] = decodedIsBgra ? src[2] : src[0];
                    dst[3] = src[3];
                }
            } else if(input.size() >= pixelCount * 2u) {
                // A8L8 expansion in the win branch of 0x6948E8.
                for(size_t i = 0; i < pixelCount; ++i) {
                    const std::uint8_t l = input[i * 2u];
                    const std::uint8_t a = input[i * 2u + 1u];
                    bgra[i * 4u + 0u] = l;
                    bgra[i * 4u + 1u] = l;
                    bgra[i * 4u + 2u] = l;
                    bgra[i * 4u + 3u] = a;
                }
            } else {
                return nullptr;
            }

            auto *texture = TVPGetRenderManager()->CreateTexture2D(
                bgra.data(), width * 4, width, height,
                TVPTextureFormat::RGBA, RENDER_CREATE_TEXTURE_FLAG_ANY);
            if(texture) {
                snapshot.sourceAtlasTextures.emplace(groupKey, texture);
            }
            return texture;
        }

        struct KrkrAtlasRecordLike_0x695DE8 : ImagePacker::rect_xywhf {
            std::string iconName;
            std::shared_ptr<const PSB::PSBDictionary> iconNode;
            std::vector<std::uint8_t> bgra;
            int contentWidth = 0;
            int contentHeight = 0;
        };

        bool decodeKrkrAtlasRecordLike_0x695DE8(
            detail::MotionSnapshot &snapshot,
            const std::string &group,
            const std::string &iconName,
            const std::shared_ptr<const PSB::PSBDictionary> &iconNode,
            KrkrAtlasRecordLike_0x695DE8 &record) {
            record.iconName = iconName;
            record.iconNode = iconNode;

            int width = 0;
            int height = 0;
            double originX = 0.0;
            double originY = 0.0;
            bool decodedIsBgra = false;
            std::vector<std::uint8_t> decoded;
            const std::string sourceKey =
                "src/" + group + "/" + iconName;
            const auto *resource = findPSBResourceBySourceName(
                snapshot, sourceKey, width, height, decoded,
                originX, originY, &decodedIsBgra);

            record.contentWidth = std::max(0, width);
            record.contentHeight = std::max(0, height);
            record.x = 0;
            record.y = 0;
            record.w = record.contentWidth + 1;
            record.h = record.contentHeight + 1;

            if(!resource || record.contentWidth <= 0 ||
               record.contentHeight <= 0) {
                return true;
            }

            const auto &input = decoded.empty() ? resource->data : decoded;
            const size_t pixelCount =
                static_cast<size_t>(record.contentWidth) *
                static_cast<size_t>(record.contentHeight);
            record.bgra.assign(pixelCount * 4u, 0);
            if(input.size() >= pixelCount * 4u) {
                for(size_t i = 0; i < pixelCount; ++i) {
                    const auto *src = input.data() + i * 4u;
                    auto *dst = record.bgra.data() + i * 4u;
                    dst[0] = decodedIsBgra ? src[0] : src[2];
                    dst[1] = src[1];
                    dst[2] = decodedIsBgra ? src[2] : src[0];
                    dst[3] = src[3];
                }
            } else if(input.size() >= pixelCount * 2u) {
                for(size_t i = 0; i < pixelCount; ++i) {
                    const std::uint8_t luminance = input[i * 2u];
                    const std::uint8_t alpha = input[i * 2u + 1u];
                    record.bgra[i * 4u + 0u] = luminance;
                    record.bgra[i * 4u + 1u] = luminance;
                    record.bgra[i * 4u + 2u] = luminance;
                    record.bgra[i * 4u + 3u] = alpha;
                }
            } else {
                record.bgra.clear();
                return true;
            }

            bool anyAlpha = false;
            for(size_t i = 3; i < record.bgra.size(); i += 4) {
                if(record.bgra[i] != 0) {
                    anyAlpha = true;
                    break;
                }
            }
            if(!anyAlpha) {
                // 0x697210..0x697248: an entirely transparent image drops its
                // pixel buffer and replaces the padded rectangle with 2x2.
                record.bgra.clear();
                record.contentWidth = 1;
                record.contentHeight = 1;
                record.w = 2;
                record.h = 2;
            }
            return true;
        }

        bool buildKrkrAtlasGroupLike_0x695DE8(
            detail::MotionSnapshot &snapshot,
            const std::string &group,
            detail::PackedSourceAtlasGroup &result) {
            const auto iconRoot = navigatePSBPath(
                snapshot.root, "source/" + group + "/icon");
            if(!iconRoot) {
                return false;
            }

            std::vector<std::string> iconNames;
            iconNames.reserve(static_cast<size_t>(
                std::distance(iconRoot->begin(), iconRoot->end())));
            for(const auto &[name, value] : *iconRoot) {
                if(std::dynamic_pointer_cast<const PSB::PSBDictionary>(value)) {
                    iconNames.push_back(name);
                }
            }

            std::vector<std::unique_ptr<KrkrAtlasRecordLike_0x695DE8>> records;
            records.reserve(iconNames.size());
            std::vector<ImagePacker::rect_xywhf *> recordPointers;
            recordPointers.reserve(iconNames.size());
            for(const auto &iconName : iconNames) {
                const auto iconNode =
                    std::dynamic_pointer_cast<const PSB::PSBDictionary>(
                        (*iconRoot)[iconName]);
                auto record =
                    std::make_unique<KrkrAtlasRecordLike_0x695DE8>();
                decodeKrkrAtlasRecordLike_0x695DE8(
                    snapshot, group, iconName, iconNode, *record);
                recordPointers.push_back(record.get());
                records.push_back(std::move(record));
            }

            if(recordPointers.empty()) {
                return true;
            }

            std::vector<ImagePacker::bin> bins;
            const int maxSide = static_cast<int>(TVPMaxTextureSize);
            const bool packed = ImagePacker::pack(
                recordPointers.data(),
                static_cast<int>(recordPointers.size()), maxSide, bins);
            if(!packed) {
                return false;
            }

            for(auto &bin : bins) {
                if(bin.size.w <= 0 || bin.size.h <= 0) {
                    continue;
                }
                const size_t atlasStride =
                    static_cast<size_t>(bin.size.w) * 4u;
                std::vector<std::uint8_t> atlas(
                    atlasStride * static_cast<size_t>(bin.size.h), 0);
                for(auto *baseRect : bin.rects) {
                    auto *record = static_cast<KrkrAtlasRecordLike_0x695DE8 *>(
                        baseRect);
                    if(record->bgra.empty()) {
                        continue;
                    }
                    const size_t sourceStride =
                        static_cast<size_t>(record->contentWidth) * 4u;
                    for(int y = 0; y < record->contentHeight; ++y) {
                        const size_t destinationOffset =
                            (static_cast<size_t>(record->y + y) *
                                 static_cast<size_t>(bin.size.w) +
                             static_cast<size_t>(record->x)) * 4u;
                        std::memcpy(
                            atlas.data() + destinationOffset,
                            record->bgra.data() +
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
                    TVPTextureFormat::RGBA,
                    RENDER_CREATE_TEXTURE_FLAG_ANY);
                if(texture) {
                    texture->Update(
                        atlas.data(), TVPTextureFormat::RGBA,
                        static_cast<int>(atlasStride),
                        tTVPRect(0, 0, bin.size.w, bin.size.h));
                }
                for(auto *baseRect : bin.rects) {
                    auto *record = static_cast<KrkrAtlasRecordLike_0x695DE8 *>(
                        baseRect);
                    detail::PackedSourceAtlasEntry entry;
                    entry.setTexture(texture);
                    entry.originX = static_cast<int>(psbDictionaryNumber(
                        record->iconNode, "originX").value_or(0.0));
                    entry.originY = static_cast<int>(psbDictionaryNumber(
                        record->iconNode, "originY").value_or(0.0));
                    entry.textureRect = {
                        record->x, record->y,
                        record->x + record->contentWidth,
                        record->y + record->contentHeight,
                    };
                    if(const auto clip =
                           std::dynamic_pointer_cast<const PSB::PSBDictionary>(
                               (*record->iconNode)["clip"])) {
                        entry.clip = {
                            psbDictionaryNumber(clip, "left").value_or(0.0),
                            psbDictionaryNumber(clip, "top").value_or(0.0),
                            psbDictionaryNumber(clip, "right").value_or(1.0),
                            psbDictionaryNumber(clip, "bottom").value_or(1.0),
                        };
                    }
                    result.icons.emplace(
                        detail::widen(record->iconName), std::move(entry));
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
            detail::MotionSnapshot &snapshot,
            const std::string &group,
            const std::string &icon) {
            const ttstr groupKey = detail::widen(group);
            auto groupIt = snapshot.packedSourceAtlasGroups.find(groupKey);
            if(groupIt == snapshot.packedSourceAtlasGroups.end()) {
                detail::PackedSourceAtlasGroup built;
                if(!buildKrkrAtlasGroupLike_0x695DE8(
                       snapshot, group, built)) {
                    return nullptr;
                }
                groupIt = snapshot.packedSourceAtlasGroups.emplace(
                    groupKey, std::move(built)).first;
            }
            const auto iconIt = groupIt->second.icons.find(
                detail::widen(icon));
            return iconIt == groupIt->second.icons.end()
                ? nullptr : &iconIt->second;
        }
    }

    void Player::findSourceForNodeLike_0x6948E8(
        detail::MotionNode &node) {
        auto &dst = node.source;
        dst.clear();
        if(!_activeMotion) {
            return;
        }

        const auto key = splitWinSourceKeyLike_0x6948E8(node.activeSlot());
        if(_activeMotion->sourceSpec == 2 && !key.group.empty() &&
           !key.icon.empty()) {
            const auto iconNode = navigatePSBPath(
                _activeMotion->root,
                "source/" + key.group + "/icon/" + key.icon);
            if(!iconNode) {
                return;
            }
            dst.object = iconNode->toTJSVal();
            dst.texture =
                loadWinAtlasTextureLike_0x6948E8(*_activeMotion, key.group);
            dst.width = psbDictionaryNumber(iconNode, "width").value_or(0.0);
            dst.height = psbDictionaryNumber(iconNode, "height").value_or(0.0);
            dst.originX =
                psbDictionaryNumber(iconNode, "originX").value_or(0.0);
            dst.originY =
                psbDictionaryNumber(iconNode, "originY").value_or(0.0);
            const int left = static_cast<int>(
                psbDictionaryNumber(iconNode, "left").value_or(0.0));
            const int top = static_cast<int>(
                psbDictionaryNumber(iconNode, "top").value_or(0.0));
            dst.textureRect = {
                left, top, left + static_cast<int>(dst.width),
                top + static_cast<int>(dst.height)};
            dst.clipLeft = dst.clipTop = 0.0;
            dst.clipRight = dst.clipBottom = 1.0;
            dst.path = key.path;
            dst.valid = dst.texture != nullptr;
            dst.blank = false;
            detail::logoChainTraceLogf(
                _activeMotion->path, "player.findSource", "0x6948E8",
                _clampedEvalTime,
                "spec=win group={} icon={} valid={} atlas={} size={}x{} rect=[{},{},{},{}]",
                key.group, key.icon, dst.valid ? 1 : 0,
                static_cast<const void *>(dst.texture), dst.width, dst.height,
                dst.textureRect[0], dst.textureRect[1], dst.textureRect[2],
                dst.textureRect[3]);
            return;
        }

        const std::string path = !key.path.empty()
            ? key.path : node.activeSlot().src;
        if(path.empty()) {
            return;
        }
        dst.path = path;

        // sub_6948E8 @0x694BA0: the krkr atlas route is gated by player+909
        // (D3D draw mode) and returns immediately on success. It is mutually
        // exclusive with the ResourceManager.findSource fallback below.
        if(_activeMotion->sourceSpec == 1 && _d3dDrawMode &&
           !key.group.empty() && !key.icon.empty()) {
            if(const auto *packed = findKrkrAtlasSourceLike_0x695DE8(
                   *_activeMotion, key.group, key.icon)) {
                dst.texture = packed->texture;
                dst.originX = packed->originX;
                dst.originY = packed->originY;
                dst.textureRect = packed->textureRect;
                dst.width = static_cast<double>(
                    packed->textureRect[2] - packed->textureRect[0]);
                dst.height = static_cast<double>(
                    packed->textureRect[3] - packed->textureRect[1]);
                dst.clipLeft = packed->clip[0];
                dst.clipTop = packed->clip[1];
                dst.clipRight = packed->clip[2];
                dst.clipBottom = packed->clip[3];
                dst.blank = false;
                dst.valid = true;
                detail::logoChainTraceLogf(
                    _activeMotion->path, "player.findSource", "0x6948E8",
                    _clampedEvalTime,
                    "spec=krkr-atlas group={} icon={} valid=1 atlas={} size={}x{} rect=[{},{},{},{}]",
                    key.group, key.icon,
                    static_cast<const void *>(dst.texture), dst.width,
                    dst.height, dst.textureRect[0], dst.textureRect[1],
                    dst.textureRect[2], dst.textureRect[3]);
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
                                     const tjs_char *name,
                                     double fallback) {
            if(!object) {
                return fallback;
            }
            tTJSVariant value;
            return TJS_SUCCEEDED(object->PropGet(0, name, nullptr, &value,
                                                 object))
                ? static_cast<double>(value) : fallback;
        };
        iTJSDispatch2 *sourceObject = dst.object.AsObjectNoAddRef();
        dst.width = readDispatchNumber(sourceObject, TJS_W("width"), 0.0);
        dst.height = readDispatchNumber(sourceObject, TJS_W("height"), 0.0);
        dst.originX =
            readDispatchNumber(sourceObject, TJS_W("originX"), 0.0);
        dst.originY =
            readDispatchNumber(sourceObject, TJS_W("originY"), 0.0);
        dst.blank =
            readDispatchNumber(sourceObject, TJS_W("blank"), 0.0) != 0.0;
        dst.textureRect = {
            0, 0, static_cast<int>(dst.width), static_cast<int>(dst.height)};
        tTJSVariant clipValue;
        if(TJS_SUCCEEDED(sourceObject->PropGet(
               0, TJS_W("clip"), nullptr, &clipValue, sourceObject)) &&
           clipValue.Type() == tvtObject && clipValue.AsObjectNoAddRef()) {
            iTJSDispatch2 *clipObject = clipValue.AsObjectNoAddRef();
            dst.clipLeft =
                readDispatchNumber(clipObject, TJS_W("left"), 0.0);
            dst.clipTop =
                readDispatchNumber(clipObject, TJS_W("top"), 0.0);
            dst.clipRight =
                readDispatchNumber(clipObject, TJS_W("right"), 1.0);
            dst.clipBottom =
                readDispatchNumber(clipObject, TJS_W("bottom"), 1.0);
        }
        dst.valid = true;
        detail::logoChainTraceLogf(
            _activeMotion->path, "player.findSource", "0x6948E8",
            _clampedEvalTime,
            "spec={} path={} valid=1 blank={} size={}x{}",
            _activeMotion->sourceSpec, path, dst.blank ? 1 : 0,
            dst.width, dst.height);
    }

    // --- Resource management ---
    void Player::unload(ttstr name) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return;
        }

        for(auto it = _motionsByKey.begin();
            it != _motionsByKey.end();) {
            if(it->first == key || it->second->path == key) {
                if(_activeMotion == it->second) {
                    _activeMotion.reset();
                    _timelines.clear();
                    _playingTimelineLabels.clear();
                }
                it = _motionsByKey.erase(it);
            } else {
                ++it;
            }
        }

        if(_sourceCacheNative) {
            _sourceCacheNative->eraseSource(name);
        }
    }

    void Player::unloadAll() {
        _motionsByKey.clear();
        if(_sourceCacheNative) {
            _sourceCacheNative->clearCache();
        }
        _activeMotion.reset();
        _timelines.clear();
        _playingTimelineLabels.clear();
        // C-1 (2026-06-07): RM is now `class ResourceManager : public SourceCache`,
        //   so RM::clearCache() resolves to the inherited SourceCache::clearCache()
        //   (binary sub_6A8438) which clears ONLY the SourceCache base +72
        //   layer-list — it does NOT clear the module HashMap A. The prior
        //   `nativeRM()->clearCache()` here relied on the now-removed RM-own
        //   clearCache override that (deviantly) cleared loadedModules. To keep the
        //   module cache being released on full unload, route through
        //   RM::unloadAll() (binary unloadAll @0x6A8BBC DOES clear HashMap A +88 /
        //   lastLoaded / layer-id set), which is the faithful function for this
        //   intent (clearCache != unloadAll in the binary).
        nativeRM()->unloadAll();
        _lastCanvas.Clear();
        _lastViewParam.Clear();
        _drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();
        _motionKey.Clear();
    }

    bool Player::isExistMotion(ttstr name) {
        return static_cast<bool>(
            resolveMotion(*this, name, nativeRM()));
    }

    tTJSVariant Player::findMotion(ttstr name) {
        const auto snapshot =
            resolveMotion(*this, name, nativeRM());
        if(!snapshot) {
            return {};
        }

        activateMotion(*this, snapshot);
        _motionKey = name;
        syncVariableKeysFromActiveMotion();
        return snapshot->moduleValue;
    }

    // P3-B (c) (2026-06-05): removed the port-invented by-name
    //   `Player::requireLayerId(ttstr name)` (node-name reuse + by-name alloc).
    //   binary has NO by-name layer-id path anywhere ("requireLayerIdForName"
    //   string: 0 hits; "requireLayerId" only ever called numparams=0). The
    //   render path (emitRenderItem@0x6C4E28 LABEL_28) allocates a FRESH id via
    //   the no-arg RM dispatch FuncCall, gated only by the item+20 latch — it
    //   does not look up or reuse a node's layerId by name. Render now calls
    //   dispatchRequireLayerId() directly (PlayerRenderExecute.cpp).

    void Player::releaseLayerId(tjs_int id) {
        dispatchReleaseLayerId(id);
    }

    // P3-B (d): layer-id alloc/release via the Player+992 RM dispatch FuncCall
    //   (see Player.h note). FuncCall routes to the NCB-registered native
    //   ResourceManager::requireLayerId/releaseLayerId, so the result is the same
    //   id — only the call chain matches the binary (3 require sites + 1 release
    //   site all go through the dispatch, never a direct native call).
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
        if(TJS_FAILED(rm->FuncCall(0, TJS_W("requireLayerId"), &hint, &result, 0,
                                   nullptr, rm))) {
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
