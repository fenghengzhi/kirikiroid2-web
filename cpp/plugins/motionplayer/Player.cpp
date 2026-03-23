//
// Created by LiDon on 2025/9/15.
// Stub implementations reverse-engineered from libkrkr2.so MMotionPlayer
//

#include "Player.h"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("Player::" #name "() stub called")

namespace motion {

// --- Core methods ---
void Player::initPhysics() { STUB_WARN(initPhysics); }
void Player::unserialize(tTJSVariant data) { STUB_WARN(unserialize); }
void Player::setRotate(double rot) { STUB_WARN(setRotate); }
void Player::setMirror(bool mirror) { STUB_WARN(setMirror); }
void Player::setHairScale(double s) { STUB_WARN(setHairScale); }
void Player::setPartsScale(double s) { STUB_WARN(setPartsScale); }
void Player::setBustScale(double s) { STUB_WARN(setBustScale); }

void Player::setDrawAffineTranslateMatrix(tTJSVariant m) {
    STUB_WARN(setDrawAffineTranslateMatrix);
}

tTJSVariant Player::getCameraOffset() {
    STUB_WARN(getCameraOffset);
    return tTJSVariant();
}

void Player::setCameraOffset(tTJSVariant offset) {
    STUB_WARN(setCameraOffset);
}

void Player::modifyRoot(tTJSVariant data) { STUB_WARN(modifyRoot); }
void Player::debugPrint() { STUB_WARN(debugPrint); }

// --- Resource management ---
void Player::unload(ttstr name) { STUB_WARN(unload); }
void Player::unloadAll() { STUB_WARN(unloadAll); }
bool Player::isExistMotion(ttstr name) { return false; }

tTJSVariant Player::findMotion(ttstr name) {
    return tTJSVariant();
}

tjs_int Player::requireLayerId(ttstr name) {
    STUB_WARN(requireLayerId);
    return 0;
}

void Player::releaseLayerId(tjs_int id) { STUB_WARN(releaseLayerId); }

// --- Drawing/rendering ---
void Player::setClearColor(tjs_int color) { STUB_WARN(setClearColor); }
void Player::setResizable(bool v) { STUB_WARN(setResizable); }
void Player::removeAllTextures() { STUB_WARN(removeAllTextures); }
void Player::removeAllBg() { STUB_WARN(removeAllBg); }
void Player::removeAllCaption() { STUB_WARN(removeAllCaption); }
void Player::registerBg(tTJSVariant bg) { STUB_WARN(registerBg); }
void Player::registerCaption(tTJSVariant caption) { STUB_WARN(registerCaption); }
void Player::unloadUnusedTextures() { STUB_WARN(unloadUnusedTextures); }
tjs_int Player::alphaOpAdd() { return 0; }

tTJSVariant Player::captureCanvas() {
    STUB_WARN(captureCanvas);
    return tTJSVariant();
}

tTJSVariant Player::findSource(ttstr name) {
    STUB_WARN(findSource);
    return tTJSVariant();
}

void Player::loadSource(ttstr name) { STUB_WARN(loadSource); }
void Player::clearCache() { STUB_WARN(clearCache); }
void Player::setSize(tjs_int w, tjs_int h) { STUB_WARN(setSize); }
void Player::copyRect(tTJSVariant args) { STUB_WARN(copyRect); }
void Player::adjustGamma(tTJSVariant args) { STUB_WARN(adjustGamma); }
void Player::draw() { STUB_WARN(draw); }
void Player::frameProgress(double dt) { STUB_WARN(frameProgress); }

// --- Viewport/display ---
void Player::setFlip(bool v) { STUB_WARN(setFlip); }
void Player::setOpacity(double v) { STUB_WARN(setOpacity); }
void Player::setVisible(bool v) { STUB_WARN(setVisible); }
void Player::setSlant(double v) { STUB_WARN(setSlant); }
void Player::setZoom(double v) { STUB_WARN(setZoom); }

tTJSVariant Player::getLayerNames() {
    return tTJSVariant();
}

void Player::releaseSyncWait() { STUB_WARN(releaseSyncWait); }
void Player::calcViewParam() { STUB_WARN(calcViewParam); }

tTJSVariant Player::getLayerMotion(ttstr name) {
    return tTJSVariant();
}

tTJSVariant Player::getLayerGetter(ttstr name) {
    return tTJSVariant();
}

tTJSVariant Player::getLayerGetterList() {
    return tTJSVariant();
}

void Player::skipToSync() { STUB_WARN(skipToSync); }

void Player::setStereovisionCameraPosition(double x, double y, double z) {
    STUB_WARN(setStereovisionCameraPosition);
}

// --- Timeline/variable queries ---
bool Player::getTimelinePlaying(ttstr label) { return false; }

tTJSVariant Player::getVariableRange(ttstr label) {
    return tTJSVariant();
}

tTJSVariant Player::getVariableFrameList(ttstr label) {
    return tTJSVariant();
}

tTJSVariant Player::getMainTimelineLabelList() {
    return tTJSVariant();
}

tTJSVariant Player::getDiffTimelineLabelList() {
    return tTJSVariant();
}

bool Player::getLoopTimeline(ttstr label) { return false; }

tTJSVariant Player::getPlayingTimelineInfoList() {
    return tTJSVariant();
}

// --- Selector ---
bool Player::isSelectorTarget(ttstr name) { return false; }
void Player::deactivateSelectorTarget(ttstr name) { STUB_WARN(deactivateSelectorTarget); }

// --- Misc ---
tTJSVariant Player::getCommandList() {
    return tTJSVariant();
}

bool Player::getD3DAvailable() { return false; }
void Player::doAlphaMaskOperation() { STUB_WARN(doAlphaMaskOperation); }
void Player::onFindMotion(ttstr name) { STUB_WARN(onFindMotion); }

tTJSVariant Player::motionList() {
    return tTJSVariant();
}

void Player::emoteEdit(tTJSVariant args) { STUB_WARN(emoteEdit); }

} // namespace motion
