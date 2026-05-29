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
        if(_runtime && _sourceCacheNative) {
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
        if(!_runtime || !_sourceCacheNative) {
            return {};
        }
        return _sourceCacheNative->findSource(std::move(name));
    }

    void Player::loadSource(ttstr name) {
        if(_runtime && _sourceCacheNative) {
            _sourceCacheNative->loadSourceByName(name, {});
        }
    }

    void Player::clearCache() {
        if(_runtime && _sourceCacheNative) {
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
        prepareRenderItems();
    }

} // namespace motion
