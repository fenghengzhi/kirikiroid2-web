// PlayerResource.cpp — Resource management: unload, findMotion, layerId
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "SourceCache.h"

using namespace motion::internal;

namespace motion {

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
        nativeRM()->clearCache();
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
