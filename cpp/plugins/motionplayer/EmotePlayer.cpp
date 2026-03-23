//
// Created by LiDon on 2025/9/15.
// Stub implementations reverse-engineered from libkrkr2.so
//

#include "EmotePlayer.h"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("EmotePlayer::" #name "() stub called")

namespace motion {

tTJSVariant EmotePlayer::clone() {
    STUB_WARN(clone);
    return tTJSVariant();
}

void EmotePlayer::show() { _visible = true; }
void EmotePlayer::hide() { _visible = false; }

void EmotePlayer::assignState() {
    STUB_WARN(assignState);
}

void EmotePlayer::initPhysics() {
    STUB_WARN(initPhysics);
}

void EmotePlayer::setRot(double rot) { _rot = rot; }
double EmotePlayer::getRot() { return _rot; }

void EmotePlayer::setCoord(double x, double y) {
    _coordX = x;
    _coordY = y;
}

void EmotePlayer::setScale(double s) { _scale = s; }
double EmotePlayer::getScale() { return _scale; }

void EmotePlayer::setColor(tjs_int color) { _color = color; }
tjs_int EmotePlayer::getColor() { return _color; }

tjs_int EmotePlayer::countVariables() {
    STUB_WARN(countVariables);
    return 0;
}

ttstr EmotePlayer::getVariableLabelAt(tjs_int idx) {
    STUB_WARN(getVariableLabelAt);
    return TJS_W("");
}

tjs_int EmotePlayer::countVariableFrameAt(tjs_int idx) {
    STUB_WARN(countVariableFrameAt);
    return 0;
}

ttstr EmotePlayer::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
    STUB_WARN(getVariableFrameLabelAt);
    return TJS_W("");
}

double EmotePlayer::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
    STUB_WARN(getVariableFrameValueAt);
    return 0.0;
}

void EmotePlayer::setVariable(ttstr label, double value) {
    _variables[label.AsStdString()] = value;
}

double EmotePlayer::getVariable(ttstr label) {
    auto it = _variables.find(label.AsStdString());
    if (it != _variables.end()) return it->second;
    return 0.0;
}

void EmotePlayer::startWind(double a, double b, double c) {
    STUB_WARN(startWind);
}

void EmotePlayer::stopWind() {
    STUB_WARN(stopWind);
}

tjs_int EmotePlayer::countMainTimelines() { return 0; }

ttstr EmotePlayer::getMainTimelineLabelAt(tjs_int idx) {
    return TJS_W("");
}

tjs_int EmotePlayer::countDiffTimelines() { return 0; }

ttstr EmotePlayer::getDiffTimelineLabelAt(tjs_int idx) {
    return TJS_W("");
}

tjs_int EmotePlayer::countPlayingTimelines() { return 0; }

ttstr EmotePlayer::getPlayingTimelineLabelAt(tjs_int idx) {
    return TJS_W("");
}

tjs_int EmotePlayer::getPlayingTimelineFlagsAt(tjs_int idx) { return 0; }

bool EmotePlayer::isLoopTimeline(ttstr label) { return false; }

tjs_int EmotePlayer::getTimelineTotalFrameCount(ttstr label) { return 0; }

void EmotePlayer::playTimeline(ttstr label, tjs_int flags) {
    STUB_WARN(playTimeline);
}

bool EmotePlayer::isTimelinePlaying(ttstr label) { return false; }

void EmotePlayer::stopTimeline(ttstr label) {
    STUB_WARN(stopTimeline);
}

void EmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
    _timelineBlendRatios[label.AsStdString()] = ratio;
}

double EmotePlayer::getTimelineBlendRatio(ttstr label) {
    auto it = _timelineBlendRatios.find(label.AsStdString());
    if (it != _timelineBlendRatios.end()) return it->second;
    return 0.0;
}

void EmotePlayer::fadeInTimeline(ttstr label, double duration, tjs_int flags) {
    STUB_WARN(fadeInTimeline);
}

void EmotePlayer::fadeOutTimeline(ttstr label, double duration, tjs_int flags) {
    STUB_WARN(fadeOutTimeline);
}

void EmotePlayer::skip() {
    STUB_WARN(skip);
}

void EmotePlayer::pass(double dt) {
    STUB_WARN(pass);
}

void EmotePlayer::setOuterForce(double x, double y) {
    _outerForceX = x;
    _outerForceY = y;
}

tTJSVariant EmotePlayer::getOuterForce() {
    STUB_WARN(getOuterForce);
    return tTJSVariant();
}

bool EmotePlayer::contains(double x, double y) { return false; }

} // namespace motion
