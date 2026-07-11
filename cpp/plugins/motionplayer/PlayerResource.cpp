// PlayerResource.cpp — Resource management: unload, findMotion, layerId
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "RenderManager.h"
#include "SourceCache.h"

#include <spdlog/spdlog.h>

using namespace motion::internal;

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

        // The non-win branch of 0x6948E8 resolves the composed path through
        // ResourceManager and stores the returned variant in the same node
        // descriptor. Keep that routing and defaults; krkr-specific texture
        // cache materialization remains inside the existing source facade.
        const std::string path = !key.path.empty()
            ? key.path : node.activeSlot().src;
        if(path.empty()) {
            return;
        }
        dst.path = path;
        dst.object = findSource(detail::widen(path));
        if(dst.object.Type() != tvtObject || !dst.object.AsObjectNoAddRef()) {
            return;
        }
        const auto iconNode = !key.group.empty() && !key.icon.empty()
            ? navigatePSBPath(_activeMotion->root,
                              "source/" + key.group + "/icon/" + key.icon)
            : nullptr;
        if(iconNode) {
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
            if(const auto clip = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                   (*iconNode)["clip"])) {
                dst.clipLeft =
                    psbDictionaryNumber(clip, "left").value_or(0.0);
                dst.clipTop =
                    psbDictionaryNumber(clip, "top").value_or(0.0);
                dst.clipRight =
                    psbDictionaryNumber(clip, "right").value_or(1.0);
                dst.clipBottom =
                    psbDictionaryNumber(clip, "bottom").value_or(1.0);
            }
        }
        auto readObjectNumber = [&](const tjs_char *name, double fallback) {
            tTJSVariant value;
            iTJSDispatch2 *object = dst.object.AsObjectNoAddRef();
            return TJS_SUCCEEDED(object->PropGet(0, name, nullptr, &value,
                                                 object))
                ? static_cast<double>(value) : fallback;
        };
        dst.blank = readObjectNumber(TJS_W("blank"), 0.0) != 0.0;
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
