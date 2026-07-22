// PlayerRender.cpp — render state, canvas/source/cache helpers, no-arg draw
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "SourceCache.h"

using namespace motion::internal;

namespace motion {
    // --- Drawing/rendering ---
    void Player::setClearColor(tjs_int color) { _clearColor = color; }

    void Player::setResizable(bool v) { _resizable = v; }

    void Player::removeAllTextures() {
        if(_sourceCacheNative) {
            _sourceCacheNative->clearCache();
        }
    }

    void Player::removeAllBg() { _backgrounds.clear(); }

    void Player::removeAllCaption() { _captions.clear(); }

    void Player::registerBg(tTJSVariant bg) { _backgrounds.push_back(bg); }

    void Player::registerCaption(tTJSVariant caption) {
        _captions.push_back(caption);
    }

    void Player::unloadUnusedTextures() {}

    tjs_int Player::alphaOpAdd() { return ++_alphaOpCounter; }

    tTJSVariant Player::captureCanvas() {
        if(_lastCanvas.Type() == tvtVoid) {
            draw();
        }
        return _lastCanvas;
    }

    tTJSVariant Player::findSource(ttstr name) {
        ResourceManager *resourceManager = nativeRM();
        if(!resourceManager) {
            return {};
        }
        // Motion_Player_findSource @0x6948E8 copies Player+1012 as argument 0
        // and the requested source path as argument 1. Player_playImpl
        // @0x6B2284 writes findMotion result[1] into +1012; RM_findMotion
        // @0x6A9ED4 builds result[1] from the matched module-map node key.
        return resourceManager->findSource(
            static_cast<ttstr>(_findMotionContextVariant), std::move(name));
    }

    void Player::loadSource(ttstr name) {
        if(_sourceCacheNative) {
            _sourceCacheNative->loadSourceByName(this, name, {});
        }
    }

    void Player::clearCache() {
        if(_sourceCacheNative) {
            _sourceCacheNative->clearCache();
        }
        _lastCanvas.Clear();
    }

    void Player::setSize(tjs_int w, tjs_int h) {
        _width = w;
        _height = h;
    }

    void Player::copyRect(tTJSVariant) {}

    void Player::adjustGamma(tTJSVariant) {}

    void Player::draw() {
        // Keep the no-arg C++ method as a lightweight prepare pass. The real
        // libkrkr2.so draw dispatch happens in drawCompat based on argument type.
        if(!_visible) {
            _lastCanvas.Clear();
            return;
        }

        ensureMotionLoaded();
        calcViewParam();
        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        prepareRenderItems(mainList, auxList);
    }

} // namespace motion
