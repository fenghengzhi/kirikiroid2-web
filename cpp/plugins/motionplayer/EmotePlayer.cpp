//
// Created by LiDon on 2025/9/15.
// Minimal runtime implementation reverse-engineered from libkrkr2.so.
//

#include "EmotePlayer.h"

#include <algorithm>

#include "RuntimeSupport.h"
#include "ncbind.hpp"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("EmotePlayer::" #name "() stub called")

namespace motion {

    namespace {

        std::string resolveTimelineLabel(
            const detail::EmotePlayerRuntime &runtime, const ttstr &label) {
            const auto requested = detail::narrow(label);
            if(!requested.empty()) {
                return requested;
            }
            if(runtime.snapshot && !runtime.snapshot->mainTimelineLabels.empty()) {
                return runtime.snapshot->mainTimelineLabels.front();
            }
            return {};
        }

        std::vector<const detail::TimelineState *>
        playingTimelines(const detail::EmotePlayerRuntime &runtime) {
            std::vector<const detail::TimelineState *> result;
            for(const auto &[_, state] : runtime.timelines) {
                if(state.playing) {
                    result.push_back(&state);
                }
            }
            return result;
        }

        const detail::TimelineState *
        nthPlayingTimeline(const detail::EmotePlayerRuntime &runtime, tjs_int idx) {
            const auto active = playingTimelines(runtime);
            if(idx < 0 || static_cast<size_t>(idx) >= active.size()) {
                return nullptr;
            }
            return active[static_cast<size_t>(idx)];
        }

    } // namespace

    EmotePlayer::EmotePlayer(ResourceManager) :
        _runtime(detail::makeEmotePlayerRuntime()) {}

    EmotePlayer::~EmotePlayer() = default;

    bool EmotePlayer::getAnimating() const {
        return std::any_of(_runtime->timelines.begin(), _runtime->timelines.end(),
                           [](const auto &entry) {
                               return entry.second.playing;
                           });
    }

    void EmotePlayer::setModule(tTJSVariant v) {
        _module = v;
        _runtime->snapshot = detail::lookupModuleSnapshot(_module);
        _runtime->timelines.clear();
        if(_runtime->snapshot) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->snapshot);
        }
    }

    tTJSVariant EmotePlayer::getModule() const { return _module; }

    void EmotePlayer::create() {
        _runtime->snapshot.reset();
        _runtime->timelines.clear();
        _module.Clear();
        _modified = true;
    }

    void EmotePlayer::load(tTJSVariant data) {
        _runtime->snapshot = detail::lookupModuleSnapshot(data);
        _runtime->timelines.clear();
        if(_runtime->snapshot) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->snapshot);
        }
        _module = data;
        _modified = true;
    }

    tTJSVariant EmotePlayer::clone() {
        typedef ncbInstanceAdaptor<EmotePlayer> AdaptorT;

        auto *copy = new EmotePlayer(ResourceManager{});
        *copy = *this;
        copy->_runtime = detail::makeEmotePlayerRuntime();
        copy->_runtime->snapshot = _runtime->snapshot;
        copy->_runtime->timelines = _runtime->timelines;

        tTJSVariant result;
        if(iTJSDispatch2 *adaptor = AdaptorT::CreateAdaptor(copy)) {
            result = tTJSVariant(adaptor, adaptor);
            adaptor->Release();
        } else {
            delete copy;
        }
        return result;
    }

    void EmotePlayer::show() { _visible = true; }
    void EmotePlayer::hide() { _visible = false; }

    void EmotePlayer::assignState() { STUB_WARN(assignState); }

    void EmotePlayer::initPhysics() { STUB_WARN(initPhysics); }

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

    ttstr EmotePlayer::getVariableLabelAt(tjs_int) {
        STUB_WARN(getVariableLabelAt);
        return TJS_W("");
    }

    tjs_int EmotePlayer::countVariableFrameAt(tjs_int) {
        STUB_WARN(countVariableFrameAt);
        return 0;
    }

    ttstr EmotePlayer::getVariableFrameLabelAt(tjs_int, tjs_int) {
        STUB_WARN(getVariableFrameLabelAt);
        return TJS_W("");
    }

    double EmotePlayer::getVariableFrameValueAt(tjs_int, tjs_int) {
        STUB_WARN(getVariableFrameValueAt);
        return 0.0;
    }

    void EmotePlayer::setVariable(ttstr label, double value) {
        _variables[detail::narrow(label)] = value;
        _modified = true;
    }

    double EmotePlayer::getVariable(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _variables.find(key); it != _variables.end()) {
            return it->second;
        }
        if(_runtime->snapshot) {
            if(const auto it = _runtime->snapshot->variableFrames.find(key);
               it != _runtime->snapshot->variableFrames.end() &&
               !it->second.empty()) {
                return it->second.front().value;
            }
            if(const auto it = _runtime->snapshot->variableRanges.find(key);
               it != _runtime->snapshot->variableRanges.end()) {
                return it->second.first;
            }
        }
        return 0.0;
    }

    void EmotePlayer::startWind(double a, double b, double c) {
        _outerForceX = a + c;
        _outerForceY = b;
    }

    void EmotePlayer::stopWind() {
        _outerForceX = 0.0;
        _outerForceY = 0.0;
    }

    tjs_int EmotePlayer::countMainTimelines() {
        return _runtime->snapshot
            ? static_cast<tjs_int>(_runtime->snapshot->mainTimelineLabels.size())
            : 0;
    }

    ttstr EmotePlayer::getMainTimelineLabelAt(tjs_int idx) {
        if(!_runtime->snapshot || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->snapshot->mainTimelineLabels.size()) {
            return TJS_W("");
        }
        return detail::widen(
            _runtime->snapshot->mainTimelineLabels[static_cast<size_t>(idx)]);
    }

    tjs_int EmotePlayer::countDiffTimelines() {
        return _runtime->snapshot
            ? static_cast<tjs_int>(_runtime->snapshot->diffTimelineLabels.size())
            : 0;
    }

    ttstr EmotePlayer::getDiffTimelineLabelAt(tjs_int idx) {
        if(!_runtime->snapshot || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->snapshot->diffTimelineLabels.size()) {
            return TJS_W("");
        }
        return detail::widen(
            _runtime->snapshot->diffTimelineLabels[static_cast<size_t>(idx)]);
    }

    tjs_int EmotePlayer::countPlayingTimelines() {
        return static_cast<tjs_int>(playingTimelines(*_runtime).size());
    }

    ttstr EmotePlayer::getPlayingTimelineLabelAt(tjs_int idx) {
        if(const auto *timeline = nthPlayingTimeline(*_runtime, idx)) {
            return detail::widen(timeline->label);
        }
        return TJS_W("");
    }

    tjs_int EmotePlayer::getPlayingTimelineFlagsAt(tjs_int idx) {
        if(const auto *timeline = nthPlayingTimeline(*_runtime, idx)) {
            return timeline->flags;
        }
        return 0;
    }

    bool EmotePlayer::isLoopTimeline(ttstr label) {
        if(!_runtime->snapshot) {
            return false;
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->snapshot->loopTimelines.find(key);
           it != _runtime->snapshot->loopTimelines.end()) {
            return it->second;
        }
        return false;
    }

    tjs_int EmotePlayer::getTimelineTotalFrameCount(ttstr label) {
        if(!_runtime->snapshot) {
            return 0;
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->snapshot->timelineTotalFrames.find(key);
           it != _runtime->snapshot->timelineTotalFrames.end()) {
            return static_cast<tjs_int>(it->second);
        }
        return 0;
    }

    void EmotePlayer::playTimeline(ttstr label, tjs_int flags) {
        const auto key = resolveTimelineLabel(*_runtime, label);
        if(key.empty()) {
            return;
        }

        if(_runtime->snapshot) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->snapshot);
        }

        auto &state = _runtime->timelines[key];
        state.label = key;
        state.flags = flags;
        state.playing = true;
        state.currentTime = 0.0;
        state.loop = isLoopTimeline(detail::widen(key));
        state.totalFrames = getTimelineTotalFrameCount(detail::widen(key));
        _modified = true;
    }

    bool EmotePlayer::isTimelinePlaying(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.playing;
        }
        return false;
    }

    void EmotePlayer::stopTimeline(ttstr label) {
        const auto key = detail::narrow(label);
        if(key.empty()) {
            for(auto &[_, state] : _runtime->timelines) {
                state.playing = false;
            }
            return;
        }

        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            it->second.playing = false;
        }
    }

    void EmotePlayer::setTimelineBlendRatio(ttstr label, double ratio) {
        _timelineBlendRatios[detail::narrow(label)] = ratio;
    }

    double EmotePlayer::getTimelineBlendRatio(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _timelineBlendRatios.find(key);
           it != _timelineBlendRatios.end()) {
            return it->second;
        }
        return isTimelinePlaying(label) ? 1.0 : 0.0;
    }

    void EmotePlayer::fadeInTimeline(ttstr label, double, tjs_int flags) {
        setTimelineBlendRatio(label, 1.0);
        playTimeline(label, flags);
    }

    void EmotePlayer::fadeOutTimeline(ttstr label, double, tjs_int) {
        setTimelineBlendRatio(label, 0.0);
        stopTimeline(label);
    }

    void EmotePlayer::setTimeline(ttstr label, bool loop) {
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return;
        }

        auto &state = _runtime->timelines[key];
        state.label = key;
        state.loop = loop;
        state.totalFrames = getTimelineTotalFrameCount(label);
        _modified = true;
    }

    void EmotePlayer::addPlayCallback() {
        _playCallback = true;
    }

    void EmotePlayer::skip() {
        for(auto &[_, state] : _runtime->timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames;
            }
            if(!state.loop) {
                state.playing = false;
            }
        }
    }

    void EmotePlayer::pass(double dt) {
        _progress += dt;
        detail::stepTimelines(_runtime->timelines, dt);
        _modified = true;
    }

    void EmotePlayer::progress(double dt) {
        pass(dt);
    }

    void EmotePlayer::setOuterForce(double x, double y) {
        _outerForceX = x;
        _outerForceY = y;
    }

    tTJSVariant EmotePlayer::getOuterForce() {
        STUB_WARN(getOuterForce);
        return tTJSVariant();
    }

    bool EmotePlayer::contains(double x, double y) {
        if(!_visible) {
            return false;
        }

        const auto width = _runtime->snapshot ? _runtime->snapshot->width : 0.0;
        const auto height =
            _runtime->snapshot ? _runtime->snapshot->height : 0.0;
        if(width <= 0.0 || height <= 0.0) {
            return false;
        }

        const auto scaledWidth = width * _scale;
        const auto scaledHeight = height * _scale;
        return x >= _coordX && x <= (_coordX + scaledWidth) && y >= _coordY &&
            y <= (_coordY + scaledHeight);
    }

} // namespace motion
