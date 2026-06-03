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
        _layerIdsByName.clear();
        _layerNamesById.clear();
        _resourceManagerNative.clearCache();
        _lastCanvas.Clear();
        _lastViewParam.Clear();
        _drawAffineMatrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        _variableKeys.Clear();
        if(_engineBack) _engineBack->_variableAnimators.clear();
        clearControllerAnimatorStateLike_0x671228();
        _evalResultValues.clear();
        _evalResultList.clear();
        _evalResultListIndex.clear();
        _motionKey.Clear();
    }

    bool Player::isExistMotion(ttstr name) {
        return static_cast<bool>(
            resolveMotion(*this, name, &_resourceManagerNative));
    }

    tTJSVariant Player::findMotion(ttstr name) {
        const auto snapshot =
            resolveMotion(*this, name, &_resourceManagerNative);
        if(!snapshot) {
            return {};
        }

        activateMotion(*this, snapshot);
        _motionKey = name;
        syncVariableKeysFromActiveMotion();
        return snapshot->moduleValue;
    }

    tjs_int Player::requireLayerId(ttstr name) {
        // Aligned to libkrkr2.so: after eager Player_buildNodeTree, the
        // label map is authoritative for any loaded motion; an empty
        // nodeLabelMap simply means no motion is loaded yet.
        if(true) {
            // Player+24 map is ttstr-keyed; look up by the raw ttstr name.
            if(const auto it = _nodeLabelMap.find(name);
               it != _nodeLabelMap.end()) {
                const auto nodeIndex = it->second;
                if(nodeIndex >= 0 &&
                   nodeIndex < static_cast<int>(_nodes.size()) &&
                   _nodes[nodeIndex].layerId1 != 0) {
                    return _nodes[nodeIndex].layerId1;
                }
            }
        }
        return _resourceManagerNative.requireLayerIdForName(name);
    }

    void Player::releaseLayerId(tjs_int id) {
        _resourceManagerNative.releaseLayerId(id);
    }


} // namespace motion
