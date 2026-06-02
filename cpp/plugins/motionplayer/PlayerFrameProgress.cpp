// PlayerFrameProgress.cpp — frameProgress timeline/control stepping
// Split from PlayerRender.cpp for maintainability.
//
#include <cmath> // std::floor — var-track interp interval quantize

#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "MotionTraceWeb.h"
#include "ncbind.hpp"
#include "psbfile/PSBValue.h" // 砖5/洞3: read motion["tag"] frame dicts

using namespace motion::internal;

namespace {

    template <typename AnimatorState>
    bool stepQueuedAnimatorLike_0x67D01C(AnimatorState &state, double dt,
                                         double &outValue) {
        double remaining = std::max(dt, 0.0);

        while(remaining > 0.0) {
            if(!state.active) {
                if(state.queue.empty()) {
                    outValue = state.currentValue;
                    return false;
                }
                const auto frame = state.queue.front();
                state.queue.pop_front();
                state.startValue = state.currentValue;
                state.targetValue = frame.value;
                state.duration = std::max(frame.duration, 0.000001f);
                state.weight = frame.weight;
                state.progress = 0.0f;
                state.active = true;
            }

            const double remainingDuration =
                static_cast<double>(state.duration) *
                std::max(0.0f, 1.0f - state.progress);
            const double consume = std::min(remaining, remainingDuration);
            if(state.duration > 0.0f) {
                state.progress = static_cast<float>(std::min(
                    1.0, static_cast<double>(state.progress) +
                             consume / static_cast<double>(state.duration)));
            } else {
                state.progress = 1.0f;
            }

            const double ratio =
                std::pow(std::clamp(static_cast<double>(state.progress), 0.0,
                                    1.0),
                         static_cast<double>(state.weight));
            state.currentValue = static_cast<float>(
                state.startValue +
                (state.targetValue - state.startValue) * ratio);
            remaining -= consume;

            if(state.progress >= 1.0f) {
                state.currentValue = state.targetValue;
                state.active = false;
            }

            if(consume <= 0.0) {
                break;
            }
        }

        outValue = state.currentValue;
        return state.active || !state.queue.empty();
    }

    double timelineBlendEaseWeightLike_0x6735AC(double ease) {
        if(ease == 0.0) {
            return 1.0;
        }
        if(ease > 0.0) {
            return ease + 1.0;
        }
        return 1.0 / (1.0 - ease);
    }

} // anonymous namespace

namespace motion {
namespace internal {

    // A4: friend of Player so it can read _timelines / _playingTimelineLabels
    // for the active-clip evaluation path. Defined here because it has a
    // single caller in this translation unit.
    double activeClipTime(const Player &player, const detail::MotionClip *clip) {
        if(clip) {
            if(const auto it = player._timelines.find(clip->label);
               it != player._timelines.end()) {
                return it->second.currentTime;
            }
        }

        for(const auto &label : player._playingTimelineLabels) {
            if(const auto it = player._timelines.find(label);
               it != player._timelines.end()) {
                return it->second.currentTime;
            }
        }
        return 0.0;
    }

} // namespace internal


    void Player::scheduleTimelineControlAnimatorLike_0x671A50(
        detail::TimelineState &state, size_t trackIndex, float value,
        double transition, double easeWeight) {
        if(trackIndex >= state.controlTrackAnimators.size()) {
            state.controlTrackAnimators.resize(trackIndex + 1);
        }
        if(trackIndex >= state.controlTrackValues.size()) {
            state.controlTrackValues.resize(trackIndex + 1, 0.0f);
        }

        auto &animator = state.controlTrackAnimators[trackIndex];
        const float targetValue = value;
        if(transition <= 0.0) {
            animator.queue.clear();
            animator.active = false;
            animator.currentValue = targetValue;
            animator.startValue = targetValue;
            animator.targetValue = targetValue;
            animator.progress = 1.0f;
            animator.duration = 0.0f;
            animator.weight = static_cast<float>(easeWeight);
            state.controlTrackValues[trackIndex] = targetValue;
            return;
        }

        animator.queue.push_back(detail::TimelineControlKeyframe{
            targetValue,
            static_cast<float>(transition),
            static_cast<float>(easeWeight),
        });
        if(!animator.active && animator.queue.size() == 1 &&
           animator.progress >= 1.0f) {
            animator.startValue = animator.currentValue;
            animator.targetValue = animator.currentValue;
        }
    }

    void Player::setTimelineBlendLike_0x6735AC(const std::string &label,
                                               bool autoStop, double value,
                                               double transition,
                                               double ease) {
        if(label.empty()) {
            return;
        }

        auto timelineIt = _timelines.find(label);
        if(timelineIt == _timelines.end()) {
            return;
        }

        auto &state = timelineIt->second;
        state.label = label;
        state.blendAutoStop = autoStop;
        const float targetValue = static_cast<float>(value);
        const float easeWeight =
            static_cast<float>(timelineBlendEaseWeightLike_0x6735AC(ease));

        if(transition <= 0.0) {
            state.blendAnimator.queue.clear();
            state.blendAnimator.active = false;
            state.blendAnimator.currentValue = targetValue;
            state.blendAnimator.startValue = targetValue;
            state.blendAnimator.targetValue = targetValue;
            state.blendAnimator.progress = 1.0f;
            state.blendAnimator.duration = 0.0f;
            state.blendAnimator.weight = easeWeight;
            state.blendRatio = value;
            return;
        }

        state.blendAnimator.queue.push_back(detail::TimelineControlKeyframe{
            targetValue,
            static_cast<float>(transition),
            easeWeight,
        });
        if(!state.blendAnimator.active &&
           state.blendAnimator.queue.size() == 1 &&
           state.blendAnimator.progress >= 1.0f) {
            state.blendAnimator.startValue = state.blendAnimator.currentValue;
            state.blendAnimator.targetValue = state.blendAnimator.currentValue;
        }
        if (_engineBack) _engineBack->_dirty = true;
    }

    void Player::stepTimelineControlAnimatorsLike_0x67D01C(double dt) {
        for(const auto &label : _playingTimelineLabels) {
            const auto timelineIt = _timelines.find(label);
            if(timelineIt == _timelines.end()) {
                continue;
            }

            auto &state = timelineIt->second;
            for(size_t trackIndex = 0;
                trackIndex < state.controlTrackAnimators.size(); ++trackIndex) {
                double steppedValue =
                    trackIndex < state.controlTrackValues.size()
                    ? static_cast<double>(state.controlTrackValues[trackIndex])
                    : 0.0;
                const bool stillAnimating = stepQueuedAnimatorLike_0x67D01C(
                    state.controlTrackAnimators[trackIndex], dt, steppedValue);
                if(trackIndex >= state.controlTrackValues.size()) {
                    state.controlTrackValues.resize(trackIndex + 1, 0.0f);
                }
                state.controlTrackValues[trackIndex] =
                    static_cast<float>(steppedValue);
                if(stillAnimating) {
                    if (_engineBack) _engineBack->_dirty = true;
                }
            }
        }
    }

    void Player::stepTimelineBlendAnimatorsLike_0x67D01C(double dt) {
        for(const auto &label : _playingTimelineLabels) {
            const auto timelineIt = _timelines.find(label);
            if(timelineIt == _timelines.end()) {
                continue;
            }

            auto &state = timelineIt->second;
            double steppedBlend = state.blendRatio;
            const bool stillAnimating = stepQueuedAnimatorLike_0x67D01C(
                state.blendAnimator, dt, steppedBlend);
            state.blendRatio = steppedBlend;
            if(stillAnimating) {
                if (_engineBack) _engineBack->_dirty = true;
            }
        }
    }

    void Player::refreshFixedControllerEvalOutputsLike_0x67D01C() {
        const auto *activeMotion = _activeMotion.get();
        if(!activeMotion) {
            return;
        }

        for(const auto &binding : activeMotion->fixedControllerOutputs) {
            if(binding.label.empty()) {
                continue;
            }

            double value = 0.0;
            const auto *bucket =
                controllerAnimatorBucketLike_0x671228(binding.type);
            const auto *bucketEntry = [&]() -> const Player::VariableAnimatorState* {
                if(!bucket) return nullptr;
                for(const auto &e : *bucket) {
                    if(e.label == binding.label) return &e;
                }
                return nullptr;
            }();
            if(bucket != nullptr) {
                if(bucketEntry) {
                    value = static_cast<double>(bucketEntry->currentValue);
                } else if(const auto *state =
                              findControllerAnimatorStateLike_0x671228(
                                  binding.label)) {
                    value = static_cast<double>(state->currentValue);
                } else if(const auto it = _evalResultValues.find(binding.label);
                          it != _evalResultValues.end()) {
                    value = it->second;
                } else {
                    value = getVariable(detail::widen(binding.label));
                }
            } else if(const auto it = _evalResultValues.find(binding.label);
                      it != _evalResultValues.end()) {
                value = it->second;
            } else {
                value = getVariable(detail::widen(binding.label));
            }

            writeEvalResultValueLike_0x6C4668(binding.label, value);
        }
    }

    void Player::accumulateTimelineContributionLike_0x67C560(
        const std::string &label, double &value) {
        const auto *activeMotion = _activeMotion.get();
        if(!activeMotion || label.empty()) {
            return;
        }

        for(const auto &timelineLabel : _playingTimelineLabels) {
            const auto timelineIt = _timelines.find(timelineLabel);
            const auto controlIt =
                activeMotion->timelineControlByLabel.find(timelineLabel);
            if(timelineIt == _timelines.end() ||
               controlIt == activeMotion->timelineControlByLabel.end()) {
                continue;
            }

            const auto &state = timelineIt->second;
            if((state.flags & 2) == 0) {
                continue;
            }

            const auto &binding = controlIt->second;
            for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
                ++trackIndex) {
                const auto &track = binding.tracks[trackIndex];
                if(track.instantVariable || track.frames.empty() ||
                   track.label != label ||
                   trackIndex >= state.controlTrackValues.size()) {
                    continue;
                }
                value += static_cast<double>(state.controlTrackValues[trackIndex]) *
                    state.blendRatio;
            }
        }
    }

    void Player::applyClampControlsLike_0x67C8A8() {
        const auto *activeMotion = _activeMotion.get();
        if(!activeMotion) {
            return;
        }

        for(const auto &binding : activeMotion->clampControls) {
            if(binding.varLr.empty() || binding.varUd.empty()) {
                continue;
            }

            const double range = binding.maxValue - binding.minValue;
            if(std::abs(range) <= 0.0000001) {
                continue;
            }

            double lrValue = 0.0;
            double udValue = 0.0;
            if(const auto it = _evalResultValues.find(binding.varLr);
               it != _evalResultValues.end()) {
                lrValue = it->second;
            } else {
                lrValue = getVariable(detail::widen(binding.varLr));
            }

            if(const auto it = _evalResultValues.find(binding.varUd);
               it != _evalResultValues.end()) {
                udValue = it->second;
            } else {
                udValue = getVariable(detail::widen(binding.varUd));
            }

            double lrNorm =
                ((lrValue - binding.minValue) / range) * 2.0 - 1.0;
            double udNorm =
                ((udValue - binding.minValue) / range) * 2.0 - 1.0;

            if(lrNorm != 0.0 && udNorm != 0.0) {
                if(binding.type == 1) {
                    const double radius =
                        std::sqrt(lrNorm * lrNorm + udNorm * udNorm);
                    if(radius > 1.0) {
                        const double angle = std::atan2(udNorm, lrNorm);
                        lrNorm = std::cos(angle);
                        udNorm = std::sin(angle);
                    }
                } else {
                    double ratio = std::abs(lrNorm / udNorm);
                    if(ratio > 1.0) {
                        ratio = 1.0 / ratio;
                    }
                    const double invLen =
                        1.0 / std::sqrt(ratio * ratio + 1.0);
                    const double projX = lrNorm * invLen;
                    const double projY = udNorm * invLen;
                    const double projLen =
                        std::sqrt(projX * projX + projY * projY);
                    if(projLen > 0.0) {
                        const double scale =
                            (1.0 - std::cos(ratio * 1.57079633)) *
                                ((std::sin(projLen * 1.57079633) / projLen) -
                                 1.0) +
                            1.0;
                        lrNorm = projX * scale;
                        udNorm = projY * scale;
                    }
                }
            }

            double lrFinal = binding.minValue + range * (lrNorm + 1.0) * 0.5;
            const double udFinal =
                binding.minValue + range * (udNorm + 1.0) * 0.5;
            if(shouldMirrorEvalLabelLike_0x67C6B0(binding.varLr)) {
                lrFinal = -lrFinal;
            }
            writeEvalResultValueLike_0x6C4668(binding.varLr, lrFinal);
            writeEvalResultValueLike_0x6C4668(binding.varUd, udFinal);
        }
    }

    void Player::applyEvalResultPostProcessLike_0x67CC9C() {
        for(auto &entry : _evalResultList) {
            accumulateTimelineContributionLike_0x67C560(entry.label, entry.value);
            double outputValue = entry.value;
            if(shouldMirrorEvalLabelLike_0x67C6B0(entry.label)) {
                outputValue = -outputValue;
            }
            writeEvalResultValueLike_0x6C4668(entry.label, outputValue);
        }

        applyClampControlsLike_0x67C8A8();
    }

    void Player::preProgressPlayingTimelinesLike_0x671764(
        double dt, std::unordered_map<std::string, double> *prevTimes) {
        if(dt <= 0.0) {
            return;
        }

        const auto *activeMotion = _activeMotion.get();
        size_t writeIndex = 0;
        for(size_t readIndex = 0;
            readIndex < _playingTimelineLabels.size(); ++readIndex) {
            const std::string label = _playingTimelineLabels[readIndex];
            const auto it = _timelines.find(label);
            if(it == _timelines.end()) {
                continue;
            }

            auto &state = it->second;
            if(prevTimes != nullptr) {
                (*prevTimes)[label] = state.currentTime;
            }

            if(!state.playing) {
                continue;
            }

            state.wasPlaying = true;
            bool keepPlaying = true;

            const detail::TimelineControlBinding *binding = nullptr;
            if(activeMotion) {
                if(const auto controlIt =
                       activeMotion->timelineControlByLabel.find(label);
                   controlIt != activeMotion->timelineControlByLabel.end()) {
                    binding = &controlIt->second;
                }
            }

            if(!binding) {
                state.currentTime += dt;
                if(state.totalFrames > 0.0 &&
                   state.currentTime >= state.totalFrames) {
                    if(state.loopTime >= 0.0) {
                        while(state.currentTime >= state.totalFrames) {
                            state.currentTime =
                                state.currentTime + state.loopTime -
                                state.totalFrames;
                        }
                    } else {
                        state.currentTime = state.totalFrames;
                        state.playing = false;
                        keepPlaying = false;
                    }
                }
            } else {
                const auto stepInternalRoute =
                    [this, &state, binding](double routeDt) {
                        if((state.flags & 2) == 0 || routeDt <= 0.0) {
                            return;
                        }

                        double steppedBlend = state.blendRatio;
                        const bool blendAnimating =
                            stepQueuedAnimatorLike_0x67D01C(
                                state.blendAnimator, routeDt, steppedBlend);
                        state.blendRatio = steppedBlend;
                        if(blendAnimating) {
                            if (_engineBack) _engineBack->_dirty = true;
                        }

                        if(state.controlTrackValues.size() <
                           binding->tracks.size()) {
                            state.controlTrackValues.resize(
                                binding->tracks.size(), 0.0f);
                        }
                        if(state.controlTrackAnimators.size() <
                           binding->tracks.size()) {
                            state.controlTrackAnimators.resize(
                                binding->tracks.size());
                        }

                        for(size_t trackIndex = 0;
                            trackIndex < binding->tracks.size(); ++trackIndex) {
                            const auto &track = binding->tracks[trackIndex];
                            if(track.instantVariable || track.frames.empty()) {
                                continue;
                            }

                            double steppedValue =
                                static_cast<double>(
                                    state.controlTrackValues[trackIndex]);
                            const bool trackAnimating =
                                stepQueuedAnimatorLike_0x67D01C(
                                    state.controlTrackAnimators[trackIndex],
                                    routeDt, steppedValue);
                            state.controlTrackValues[trackIndex] =
                                static_cast<float>(steppedValue);
                            if(trackAnimating) {
                                if (_engineBack) _engineBack->_dirty = true;
                            }
                        }
                    };

                const double loopBegin = binding->loopBegin;
                const double loopEnd = binding->loopEnd;
                const double lastTime =
                    binding->lastTime >= 0.0 ? binding->lastTime
                                             : state.totalFrames;

                if(!state.controlInitialized ||
                   state.controlFrameCursor.size() != binding->tracks.size()) {
                    resetTimelineControlStateLike_0x671A50(
                        state, *binding, std::max(state.currentTime, 0.0));
                }

                if(loopBegin < 0.0) {
                    applyTimelineControlWindowLike_0x669E1C(
                        state, *binding, state.currentTime + dt, true);
                    stepInternalRoute(dt);

                    const bool blendAnimatorPending =
                        state.blendAnimator.active ||
                        !state.blendAnimator.queue.empty();
                    if(lastTime <= state.currentTime ||
                       (state.blendAutoStop && !blendAnimatorPending)) {
                        state.currentTime = lastTime;
                        state.playing = false;
                        keepPlaying = false;
                    }
                } else if(loopEnd > loopBegin) {
                    double remaining = dt;
                    while(remaining > 0.0 &&
                          state.currentTime + remaining >= loopEnd) {
                        const double currentTime = state.currentTime;
                        applyTimelineControlWindowLike_0x669E1C(
                            state, *binding, loopEnd, false);
                        remaining -= std::max(loopEnd - currentTime, 0.0);
                        resetTimelineControlStateLike_0x671A50(
                            state, *binding, loopBegin);
                    }
                    applyTimelineControlWindowLike_0x669E1C(
                        state, *binding, state.currentTime + remaining, true);
                    stepInternalRoute(remaining);

                    const bool blendAnimatorPending =
                        state.blendAnimator.active ||
                        !state.blendAnimator.queue.empty();
                    if(state.blendAutoStop && !blendAnimatorPending) {
                        state.playing = false;
                        keepPlaying = false;
                    }
                } else {
                    applyTimelineControlWindowLike_0x669E1C(
                        state, *binding, state.currentTime + dt, true);
                    stepInternalRoute(dt);

                    const bool blendAnimatorPending =
                        state.blendAnimator.active ||
                        !state.blendAnimator.queue.empty();
                    if(lastTime <= state.currentTime ||
                       (state.blendAutoStop && !blendAnimatorPending)) {
                        state.currentTime = lastTime;
                        state.playing = false;
                        keepPlaying = false;
                    }
                }
            }

            if(!keepPlaying && state.wasPlaying) {
                _pendingEvents.push_back({1, label, {}});
                state.wasPlaying = false;
            }

            if(state.playing && keepPlaying) {
                _playingTimelineLabels[writeIndex++] = label;
            }
        }
        _playingTimelineLabels.resize(writeIndex);
    }

    void Player::resetTimelineControlStateLike_0x671A50(
        detail::TimelineState &state,
        const detail::TimelineControlBinding &binding,
        double time) {
        state.controlFrameCursor.assign(binding.tracks.size(), -1);
        state.controlTrackValues.assign(binding.tracks.size(), 0.0f);
        state.controlTrackAnimators.assign(binding.tracks.size(), {});
        for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
            ++trackIndex) {
            const auto &track = binding.tracks[trackIndex];
            int cursor = -1;
            int lastNonTypeZero = -1;
            for(size_t frameIndex = 0; frameIndex < track.frames.size();
                ++frameIndex) {
                const auto &frame = track.frames[frameIndex];
                if(!frame.isTypeZero) {
                    lastNonTypeZero = static_cast<int>(frameIndex);
                }
                if(frame.time <= time) {
                    cursor = static_cast<int>(frameIndex);
                    continue;
                }
                break;
            }
            state.controlFrameCursor[trackIndex] = cursor;

            if(lastNonTypeZero < 0) {
                continue;
            }

            const auto &frame =
                track.frames[static_cast<size_t>(lastNonTypeZero)];
            const size_t nextIndex = static_cast<size_t>(lastNonTypeZero + 1);
            const double transition =
                nextIndex < track.frames.size()
                ? std::max(track.frames[nextIndex].time - time - 1.0, 0.0)
                : 0.0;
            if((state.flags & 2) != 0 && !track.instantVariable) {
                scheduleTimelineControlAnimatorLike_0x671A50(
                    state, trackIndex, frame.value, transition,
                    frame.easingWeight);
            } else {
                setVariableResolvedWeightLike_0x671228(
                    track.label, static_cast<double>(frame.value), transition,
                    frame.easingWeight);
            }
        }
        state.controlInitialized = true;
        state.controlLastAppliedTime = time;
    }

    void Player::applyTimelineControlWindowLike_0x669E1C(
        detail::TimelineState &state,
        const detail::TimelineControlBinding &binding,
        double targetTime,
        bool inclusiveEnd) {
        if(state.controlFrameCursor.size() != binding.tracks.size()) {
            state.controlFrameCursor.assign(binding.tracks.size(), -1);
        }
        if(state.controlTrackValues.size() < binding.tracks.size()) {
            state.controlTrackValues.resize(binding.tracks.size(), 0.0f);
        }
        if(state.controlTrackAnimators.size() < binding.tracks.size()) {
            state.controlTrackAnimators.resize(binding.tracks.size());
        }

        for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
            ++trackIndex) {
            const auto &track = binding.tracks[trackIndex];
            if(track.label.empty() || track.frames.empty()) {
                continue;
            }
            if((state.flags & 4) != 0 && track.instantVariable) {
                continue;
            }

            const bool internalRoute =
                (state.flags & 2) != 0 && !track.instantVariable;
            int cursor = state.controlFrameCursor[trackIndex];
            const int lastCursor =
                static_cast<int>(track.frames.size()) - 1;
            if(cursor >= lastCursor) {
                continue;
            }

            while(cursor + 1 < static_cast<int>(track.frames.size())) {
                const auto nextIndex = static_cast<size_t>(cursor + 1);
                const auto &nextFrame = track.frames[nextIndex];
                const bool crossed = inclusiveEnd
                    ? nextFrame.time <= targetTime
                    : nextFrame.time < targetTime;
                if(!crossed) {
                    break;
                }

                if(!nextFrame.isTypeZero &&
                   nextIndex + 1 < track.frames.size()) {
                    const auto &followingFrame = track.frames[nextIndex + 1];
                    const double transition = std::max(
                        followingFrame.time - targetTime - 1.0, 0.0);
                    if(internalRoute) {
                        scheduleTimelineControlAnimatorLike_0x671A50(
                            state, trackIndex, nextFrame.value, transition,
                            nextFrame.easingWeight);
                    } else {
                        setVariableResolvedWeightLike_0x671228(
                            track.label, static_cast<double>(nextFrame.value),
                            transition, nextFrame.easingWeight);
                    }
                }

                cursor = static_cast<int>(nextIndex);
            }

            state.controlFrameCursor[trackIndex] = cursor;
        }

        state.currentTime = targetTime;
        state.controlLastAppliedTime = targetTime;
    }

    void Player::applyTimelineControlFrameCrossingLike_0x67CD20(
        const std::unordered_map<std::string, double> &prevTimes) {
        const auto *activeMotion = _activeMotion.get();
        if(!activeMotion) {
            return;
        }

        for(const auto &label : _playingTimelineLabels) {
            const auto timelineIt = _timelines.find(label);
            const auto controlIt =
                activeMotion->timelineControlByLabel.find(label);
            if(timelineIt == _timelines.end() ||
               controlIt == activeMotion->timelineControlByLabel.end()) {
                continue;
            }

            auto &state = timelineIt->second;
            const auto &binding = controlIt->second;
            const auto prevIt = prevTimes.find(label);
            const double prevTime =
                prevIt != prevTimes.end() ? prevIt->second : state.currentTime;
            const bool rewound = !state.controlInitialized ||
                state.currentTime < prevTime ||
                state.controlFrameCursor.size() != binding.tracks.size();
            if(rewound) {
                // Aligned to sub_671A50: re-seek per-track cursors using the
                // timeline time before the current crossing scan.
                resetTimelineControlStateLike_0x671A50(
                    state, binding, std::max(prevTime, 0.0));
            }

            if((state.flags & 2) != 0 && (state.flags & 4) == 0) {
                // Aligned to sub_67CD20 + sub_6735AC:
                // crossed-frame entry into the internal route triggers a
                // timeline-level fade to 0 over 20 frames before the runtime
                // is marked as initialized.
                setTimelineBlendLike_0x6735AC(label, true, 0.0, 20.0, 0.0);
                state.flags |= 4;
            }

            for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
                ++trackIndex) {
                const auto &track = binding.tracks[trackIndex];
                if(track.label.empty() || track.frames.empty()) {
                    continue;
                }
                if((state.flags & 2) != 0 && !track.instantVariable) {
                    continue;
                }

                int cursor = trackIndex < state.controlFrameCursor.size()
                    ? state.controlFrameCursor[trackIndex]
                    : -1;
                size_t nextIndex = cursor >= 0
                    ? static_cast<size_t>(cursor + 1)
                    : 0;
                while(nextIndex < track.frames.size() &&
                      track.frames[nextIndex].time <= state.currentTime) {
                    const auto &frame = track.frames[nextIndex];
                    if(!frame.isTypeZero) {
                        setVariableResolvedWeightLike_0x671228(
                            track.label, static_cast<double>(frame.value),
                            frame.time, frame.easingWeight);
                    }
                    cursor = static_cast<int>(nextIndex);
                    ++nextIndex;
                }

                if(trackIndex >= state.controlFrameCursor.size()) {
                    state.controlFrameCursor.resize(trackIndex + 1, -1);
                }
                state.controlFrameCursor[trackIndex] = cursor;
            }

            state.controlLastAppliedTime = state.currentTime;
        }
    }

    // 砖5/洞3: faithful layer (motion["tag"]) event stream.
    // Bidirectional incremental cursor seek toward targetTime (= _clampedEvalTime),
    // porting the layer-stream loops of Player_advanceRootAndNodes (0x6B6ADC,
    // forward) + Player_rewindRootAndNodes (0x6B9A3C, backward). On each crossed
    // type==1 frame applies the advance/rewind gate (0x6B6DD8 / 0x6B9D0C):
    //   if (+1093 _speed): align -> _motionCompleted=1, snap _clampedEvalTime &
    //     _frameTickCount = curTime; sync -> _syncWaiting=1, same snap, onSync().
    //   (ungated) content["action"] -> onAction(void, actionName)  [§8.7].
    // NOTE vs binary: the binary runs this INSIDE advanceRootAndNodes, before the
    // node walk, so an align/sync snap propagates to the same-frame node seek. The
    // live port runs it once at end-of-frameProgress (see caller), so a snap takes
    // effect on the NEXT frame's node seek — documented 1-frame lag; the
    // cursor/event/gate semantics are otherwise 1:1.
    void Player::seekLayerEventStreamLike_0x6B6ADC(double targetTime) {
        if (!_activeMotion) {
            return;
        }
        const auto &frames = _activeMotion->tagFrames;
        if (!frames) {
            _layerStreamSource = nullptr;
            return;
        }
        const int count = static_cast<int>(frames->size());

        // Self-reset the cursor when the tag stream changes (motion (re)loaded).
        // Mirrors Player_reseekTimelineCursors resetting cursors on the firstFrame
        // seed, without coupling to the motion-load site.
        if (_layerStreamSource != static_cast<const void *>(frames.get())) {
            _layerStreamSource = static_cast<const void *>(frames.get());
            _layerFrameCursor = 0;
        }

        const auto frameAt =
            [&](int i) -> std::shared_ptr<PSB::PSBDictionary> {
            if (i < 0 || i >= count) {
                return nullptr;
            }
            return std::dynamic_pointer_cast<PSB::PSBDictionary>((*frames)[i]);
        };
        const auto numValue =
            [](const std::shared_ptr<PSB::IPSBValue> &v) -> double {
            auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v);
            if (!n) {
                return 0.0;
            }
            switch (n->numberType) {
                case PSB::PSBNumberType::Float:  return n->getValue<float>();
                case PSB::PSBNumberType::Double: return n->getValue<double>();
                case PSB::PSBNumberType::Int:
                    return static_cast<double>(n->getValue<int>());
                default:
                    return static_cast<double>(n->getValue<tjs_int64>());
            }
        };
        const auto frameTimeOf =
            [&](const std::shared_ptr<PSB::PSBDictionary> &f) -> double {
            return f ? numValue((*f)["time"]) : 0.0;
        };
        const auto frameTypeOf =
            [&](const std::shared_ptr<PSB::PSBDictionary> &f) -> int {
            if (!f) {
                return 0;
            }
            auto n = std::dynamic_pointer_cast<PSB::PSBNumber>((*f)["type"]);
            return n ? n->getValue<int>() : 0;
        };
        const auto contentBoolOf =
            [](const std::shared_ptr<PSB::PSBDictionary> &content,
               const char *key) -> bool {
            if (!content) {
                return false;
            }
            auto v = (*content)[key];
            if (auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                return n->getValue<int>() != 0;
            }
            if (auto b = std::dynamic_pointer_cast<PSB::PSBBool>(v)) {
                return b->value;
            }
            return false;
        };

        // type==1 frame gate (advance/rewind form: +1093-only, NOT time-gated;
        // action ungated). Aligned to 0x6B6DD8 (advance) / 0x6B9D0C (rewind).
        const auto gate =
            [&](const std::shared_ptr<PSB::PSBDictionary> &cf, double curTime) {
            auto content = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                (*cf)["content"]);
            if (!content) {
                return;
            }
            if (_speed) {                                   // +1093 stop gate
                if (contentBoolOf(content, "align")) {      // 0x6B6DD8
                    _motionCompleted = true;
                    _clampedEvalTime = curTime;
                    _frameTickCount = curTime;
                }
                if (_speed && contentBoolOf(content, "sync")) { // 0x6B6E14
                    _syncWaiting = true;
                    _clampedEvalTime = curTime;
                    _frameTickCount = curTime;
                    _pendingEvents.push_back({1, {}, {}, false}); // onSync()
                }
            }
            // 0x6B6E4C: content["action"] (ungated) -> onAction(void, actionName).
            if (auto actionStr = std::dynamic_pointer_cast<PSB::PSBString>(
                    (*content)["action"])) {
                if (!actionStr->value.empty()) {
                    detail::MotionEvent ev;
                    ev.type = 0;
                    ev.param2 = actionStr->value;  // record.b = action name
                    ev.voidParam1 = true;          // record.a = void [§8.7]
                    _pendingEvents.push_back(ev);
                }
            }
        };

        // Derive curTime/nextTime from the (persistent) cursor.
        _layerCurTime = frameTimeOf(frameAt(_layerFrameCursor));
        _layerNextTime = frameTimeOf(frameAt(_layerFrameCursor + 1));

        // Forward advance (0x6B6B80): while cursor<count-2 && target>=nextTime.
        while (_layerFrameCursor < count - 2) {
            if (targetTime < _layerNextTime) {
                break;
            }
            ++_layerFrameCursor;
            auto cf = frameAt(_layerFrameCursor);
            _layerCurTime = frameTimeOf(cf);
            _layerNextTime = frameTimeOf(frameAt(_layerFrameCursor + 1));
            if (frameTypeOf(cf) == 1) {
                gate(cf, _layerCurTime);
            }
        }
        // Backward rewind (0x6B9AE8): while count!=0 && curTime>target.
        // (cursor>0 guard: the binary relies on tag[0].time<=target; the guard
        // prevents underflow on data where that does not hold.)
        while (count != 0 && _layerCurTime > targetTime && _layerFrameCursor > 0) {
            --_layerFrameCursor;
            auto cf = frameAt(_layerFrameCursor);
            _layerCurTime = frameTimeOf(cf);
            _layerNextTime = frameTimeOf(frameAt(_layerFrameCursor + 1));
            if (frameTypeOf(cf) == 1) {
                gate(cf, _layerCurTime);
            }
        }
    }

    void Player::advanceVariableTracksLike_0x6B6ADC(double clampedEvalTime) {
        // libkrkr2.so Player_advanceRootAndNodes (0x6B6ADC) var-track loop
        // (0x6B7124..0x6B71C8) — stream ③. For each VariableLabelScope
        // (Player+1296 deque), advance its two 56B slots so they bracket
        // clampedEvalTime (+456) via the inlined step (sub_6B786C) + merge
        // (sub_6B7A70). Inert when frameSource is not a keyframe list (binary
        // PropGetCount ~0 → loop never runs); true for every currently-available
        // motion (no fixture exposes a populated "variable" list — see
        // analysis/Player_4_HashMaps_Container_Mapping.md §四之二).
        const auto numValue =
            [](const std::shared_ptr<PSB::IPSBValue> &v) -> double {
            auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v);
            if (!n) return 0.0;
            switch (n->numberType) {
                case PSB::PSBNumberType::Float:  return n->getValue<float>();
                case PSB::PSBNumberType::Double: return n->getValue<double>();
                case PSB::PSBNumberType::Int:
                    return static_cast<double>(n->getValue<int>());
                default:
                    return static_cast<double>(n->getValue<tjs_int64>());
            }
        };
        const auto intValue =
            [](const std::shared_ptr<PSB::IPSBValue> &v) -> int {
            auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v);
            return n ? n->getValue<int>() : 0;
        };

        for (auto &item : _variableLabelScopes) {
            const auto frames =
                std::dynamic_pointer_cast<PSB::PSBList>(item.frameSource);
            if (!frames) {
                continue;     // non-list → binary PropGetCount ~0 → no-op
            }
            const int count = static_cast<int>(frames->size());
            const auto frameAt =
                [&](int i) -> std::shared_ptr<PSB::PSBDictionary> {
                if (i < 0 || i >= count) return nullptr;
                return std::dynamic_pointer_cast<PSB::PSBDictionary>((*frames)[i]);
            };
            // step (sub_6B786C): slot.frameIndex=idx; slot.time=frame["time"];
            //   slot.merged=0.
            const auto step = [&](detail::VarTrackSlot &slot, std::uint32_t idx) {
                slot.frameIndex = idx;
                auto f = frameAt(static_cast<int>(idx));
                slot.time = f ? numValue((*f)["time"]) : 0.0;
                slot.merged = false;
            };
            // merge (sub_6B7A70): slot.merged=1; type=frame["type"]; type==0 ->
            //   typeZeroFlag=1 (0x6B7BB0 early return); else typeZeroFlag=0,
            //   interpFlag=(type==3), interval/value=content["interval"/"value"],
            //   easing=content["easing"].
            const auto merge = [&](detail::VarTrackSlot &slot) {
                slot.merged = true;
                auto f = frameAt(static_cast<int>(slot.frameIndex));
                const int type = f ? intValue((*f)["type"]) : 0;
                if (type == 0) {
                    slot.typeZeroFlag = true;
                    return;
                }
                slot.typeZeroFlag = false;
                if (type == 2)      slot.interpFlag = 0;
                else if (type == 3) slot.interpFlag = 1;
                auto content = f ? std::dynamic_pointer_cast<PSB::PSBDictionary>(
                                       (*f)["content"])
                                 : nullptr;
                if (content) {
                    slot.interval = static_cast<std::uint32_t>(
                        intValue((*content)["interval"]));
                    slot.value = numValue((*content)["value"]);
                    // easing (slot+32) = content["easing"] raw value — a bezier
                    // {x,y} dict consumed by applyBezierEasing (NOT a string).
                    slot.easing = (*content)["easing"];
                } else {
                    slot.easing = nullptr;
                }
            };

            // active=slot[cursor], other=slot[!cursor] (0x6B7264).
            int cursor = item.activeSlotCursor & 1;
            detail::VarTrackSlot *active = &item.slot[cursor];
            detail::VarTrackSlot *other = &item.slot[cursor ^ 1];
            const int limit = count - 2;
            // step loop (0x6B7274): until active.frameIndex>=count-2 OR
            //   clampedEvalTime < other.time.
            while (static_cast<int>(active->frameIndex) < limit
                   && clampedEvalTime >= other->time) {
                const std::uint32_t nextIdx = other->frameIndex + 1;
                item.activeSlotCursor =
                    (item.activeSlotCursor & 1) == 0;   // toggle (0x6B7150)
                step(*active, nextIdx);
                detail::VarTrackSlot *tmp = active;     // swap roles (0x6B7160)
                active = other;
                other = tmp;
            }
            // merge (0x6B7178, disasm-confirmed): merge slot[0] if !slot0.merged,
            // then slot[0] AGAIN if !slot1.merged — both BL sub_6B7A70(item+48,..).
            if (!item.slot[0].merged) merge(item.slot[0]);
            if (!item.slot[1].merged) merge(item.slot[0]);
        }
    }

    void Player::interpolateVarTrackValuesLike_0x6BBE20(double clampedEvalTime) {
        // libkrkr2.so Player_interpolateVarTrackValues @0x6BBE20. Writes item+16
        // (the value HM4 caches) for each VariableLabelScope. active=slot[cursor]
        // is prev (lower frame), other=slot[!cursor] is next. Gate (0x6BBF14):
        // skip if active.typeZeroFlag (type==0 → no value). Then HOLD vs LERP.
        const auto numValue =
            [](const std::shared_ptr<PSB::IPSBValue> &v) -> double {
            auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v);
            if (!n) return 0.0;
            switch (n->numberType) {
                case PSB::PSBNumberType::Float:  return n->getValue<float>();
                case PSB::PSBNumberType::Double: return n->getValue<double>();
                case PSB::PSBNumberType::Int:
                    return static_cast<double>(n->getValue<int>());
                default:
                    return static_cast<double>(n->getValue<tjs_int64>());
            }
        };
        // Player_applyBezierEasing @0x69A754: easing = {x:[...], y:[...]} control
        // points (count multiple of 3). Clamp t to [x[0], x[n-1]] → y end; else
        // stride-3 scan locates the segment and the curve uses t DIRECTLY:
        //   B(t) = (1-t)³y0 + 3(1-t)²t·y1 + 3(1-t)t²·y2 + t³y3
        // (the x values only locate the segment; they do not re-parameterise t —
        // faithful to the binary's discard of the xs reads).
        const auto applyBezierEasing =
            [&](const std::shared_ptr<PSB::IPSBValue> &easing,
                double t) -> double {
            auto dict = std::dynamic_pointer_cast<PSB::PSBDictionary>(easing);
            if (!dict) return t;
            auto xs = std::dynamic_pointer_cast<PSB::PSBList>((*dict)["x"]);
            auto ys = std::dynamic_pointer_cast<PSB::PSBList>((*dict)["y"]);
            if (!xs || !ys) return t;
            const int count = static_cast<int>(xs->size());
            if (count == 0 || static_cast<int>(ys->size()) < count) return t;
            const auto xAt = [&](int i) { return numValue((*xs)[i]); };
            const auto yAt = [&](int i) { return numValue((*ys)[i]); };
            if (xAt(0) >= t) return yAt(0);            // 0x69A938
            if (xAt(count - 1) <= t) return yAt(count - 1);  // 0x69A958
            int s = 0;
            do { s += 3; } while (s < count && xAt(s) < t);  // 0x69A960 stride-3
            const double y0 = yAt(s - 3), y1 = yAt(s - 2),
                         y2 = yAt(s - 1), y3 = yAt(s);
            const double u = 1.0 - t;                  // 0x69AA80 cubic bezier
            return u * u * u * y0 + 3.0 * u * u * t * y1
                 + 3.0 * u * t * t * y2 + t * t * t * y3;
        };

        for (auto &item : _variableLabelScopes) {
            const int cursor = item.activeSlotCursor & 1;
            detail::VarTrackSlot &active = item.slot[cursor];     // prev
            detail::VarTrackSlot &other = item.slot[cursor ^ 1];  // next
            if (active.typeZeroFlag) {
                continue;                       // 0x6BBF14 gate: type==0 → no value
            }
            double v;
            if (active.interpFlag == 0 || other.typeZeroFlag) {
                v = active.value;               // HOLD (0x6BBF40)
            } else {
                double d = clampedEvalTime - active.time;   // evalTime - prevTime
                if (active.interval != 0) {                 // 0x6BBF74 quantize
                    d = std::floor(d / active.interval) * active.interval;
                }
                const double Vp = active.value, Vo = other.value;
                if (Vo == Vp) {
                    v = Vp;                     // degenerate → hold
                } else {
                    double t = d / (other.time - active.time);  // 0x6BBFBC
                    if (active.easing) {        // slot+48 easingPresent ~ easing!=null
                        t = applyBezierEasing(active.easing, t); // 0x6BBFC8
                    }
                    v = Vo * t + Vp * (1.0 - t); // LERP (0x6BBFD8)
                }
            }
            item.value = v;                     // item+16 (0x6BBF54)
            // The binary then calls Player_bindParameterValue_writesHM1_HM2(
            //   player, item, 0, v) to populate HM1/HM2 — DEFERRED to the HM1/HM2
            //   bind port; item+16 alone feeds HM4 via resetMotionState loop2.
        }
    }

    void Player::frameProgress(double dt) {
        // Aligned to libkrkr2.so Player_progress_inner (0x6C106C):
        // _speed is a bool flag (play/pause). When false, skip progress entirely.
        if(!_speed) {
            return;
        }
        const double actualDelta = dt;
        _frameLastTime = actualDelta;

        _evalResultValues.clear();

        // 砖5/洞1: progress_inner's first step is Player_preProgressDirtyNodes
        // (0x6C10AC), before the firstFrame/cursor logic. Inert in the web port
        // (no "modified"-setter) but ported for call-chain restoration.
        preProgressDirtyNodesLike_0x6B6878();

        // Aligned to Player_progress_inner (0x6C106C): player+480 is a
        // one-shot first-frame gate. While it is set, progress records the
        // incoming delta but does not advance player+1120/player+456.
        if(_queuing) {
            _allplaying = !_playingTimelineLabels.empty();
            _syncActive = _syncWaiting && _allplaying;
            // M1/P7 step-1 fix: the firstFrame gate freezes cursor ADVANCE, but
            // the binary still seeks the node slots on this path (progress_inner
            // 0x6C106C firstFrame branch seeds +456 then calls
            // reseekTimelineCursors 0x6B86C8 — which seeks — before returning).
            // The collapsed model seeked inline in updateLayers every frame
            // regardless of _queuing, so without this the frame-0 slots stay
            // unseeded (active=false, scale=1.0 defaults). Seek at the held
            // _clampedEvalTime so updateLayers reads populated slots.
            if(!_nodes.empty()) {
                progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime);
            }
            return;
        }

        // R1.H2: removed `_frameLoopTime += actualDelta;` — port-invented
        // duplicate accumulator that collided with _frameTickCount on +1120.
        // Binary's progress_inner @0x6C106C maintains a single cursor at +1120
        // (= _frameTickCount); the previous _loopTime corruption stemmed from
        // this twin-accumulator pattern (commit e11ecef formally removed the
        // _loopTime side; R1.H2 deletes the field declaration itself).
        //
        // M1 P5/G3: Player_progress_inner @0x6C106C LABEL_48 advances the frame
        //   cursor by deltaTime(+592 = speedMul(+1168)*dt), gated by _queuing
        //   (+480) — when the gate's LSB is set the cursor is frozen. Mirror that:
        //     if (!_queuing) _frameTickCount += _speedMul * actualDelta;
        //   At P1 defaults (_speedMul=1.0, _queuing=false) this is exactly the
        //   previous `_frameTickCount += actualDelta`, so behaviour is preserved.
        _deltaTime = _speedMul * actualDelta;       // player+592
        if(!_queuing) {                              // player+480 LSB gate (LABEL_48)
            _frameTickCount += _deltaTime;           // player+1120 += player+592
        }

        // 砖6/Stage C (P7 teardown — 错位调用点 RE-confirmed, MIGRATION DEFERRED):
        // CORRECTION of the prior attribution. Player_preProgress @0x671764 is NOT
        // on the Player_progress_inner (0x6C106C) call chain — byte-verified via
        // xrefs_to(0x671764): its only callers are EmoteEngine_progress (0x530A5C
        //   -> 0x67D01C) and sub_675E40. progress_inner's own pre-progress step is
        // Player_preProgressDirtyNodes (0x6B6878), already invoked above (line ~952).
        // 0x671764 is a PLAYING-LIST controller stepper: it walks the playing-list
        // variant array (player[130..131] = _playingTimelineLabels), steps each
        // entry's EmoteVarController_step, and pops completed entries off the list
        // — i.e. it belongs to the EmoteEngine progress chain, not the non-emote
        // progress_inner port that frameProgress mirrors. The faithful fix is to
        // migrate this call out of frameProgress into the EmoteEngine_progress port
        // (cpp/plugins/motionplayer/EmoteEngine.cpp). That migration is a progress-
        // topology refactor (frameProgress is currently an emote/non-emote blend)
        // and is DEFERRED — see analysis/Player_progress_frame_stepping_M1_plan.md
        // §7 + module-alignment-driver memory. Left in place to preserve the green
        // logo differential; only the attribution comment is corrected this round.
        //
        // NOTE on _evalResultValues (frameProgress entry .clear() + this path's
        // writeEvalResultValueLike_0x6C4668): RE-checked 0x6C4668
        // (Player_bindParameterValue_writesHM1_HM2). Its LABEL_132 does
        // HM2_upsert(player+320, label) = value — so _evalResultValues is the
        // PORT MIRROR OF HM2 @+320 (a real binary container), NOT a port-invented
        // construct (M1_plan §3's "凭空多出" list is wrong for this entry; the
        // binary HM2 write is logo-differential-gated). The per-frame .clear()
        // (line ~947) IS port-invented (binary HM2 is persistent; progress_inner
        // entry clears only +1152/+483, never +320), but removing it converts HM2
        // to cross-frame persistence — a high-risk behavior change DEFERRED.
        preProgressPlayingTimelinesLike_0x671764(actualDelta, nullptr);

        double remainingControllerStep = actualDelta;
        const auto stepControllerBucket =
            [this](auto &bucket, double controllerDt) {
                for(auto &state : bucket) {
                    double steppedValue = state.currentValue;
                    const bool stillAnimating = stepQueuedAnimatorLike_0x67D01C(
                        state, controllerDt, steppedValue);
                    writeEvalResultValueLike_0x6C4668(state.label, steppedValue);
                    if(stillAnimating) {
                        if (_engineBack) _engineBack->_dirty = true;
                    }
                }
            };
        while(remainingControllerStep > 0.0) {
            const double controllerDt = std::min(remainingControllerStep, 1.1);
            // Aligned to 0x67D01C container order: type4 -> type5 -> type6
            // -> type8 -> type7, then generic eval animators.
            if(_engineBack) {
                stepControllerBucket(_engineBack->_type4ControllerAnimators, controllerDt);
                stepControllerBucket(_engineBack->_type5ControllerAnimators, controllerDt);
                stepControllerBucket(_engineBack->_type6ControllerAnimators, controllerDt);
                stepControllerBucket(_engineBack->_type8ControllerAnimators, controllerDt);
                stepControllerBucket(_engineBack->_type7ControllerAnimators, controllerDt);
            }
            refreshFixedControllerEvalOutputsLike_0x67D01C();
            remainingControllerStep -= controllerDt;
        }

        applyEvalResultPostProcessLike_0x67CC9C();

        // Camera velocity/friction moved to updateLayers pre-loop (0x6BB360..0x6BB42C)

        // M1 P5/G4 + P7 step-2: Player_progress_inner @0x6C106C LABEL_48.
        // After the gated cursor advance above (+1120 += +592; +456 =
        // min(+1120,+1128) when the +480 gate is clear), the binary branches on
        // the SIGN of deltaTime(+592) and on whether the cursor reached the end
        // / start, into forward-normal / forward-to-end(loop|stop) /
        // reverse-rewind / loop-wrap. Each terminal branch then re-seeks the
        // node slots (binary: Player_advanceRootAndNodes 0x6B6ADC /
        // Player_rewindRootAndNodes 0x6B9A3C, both via the per-node
        // advanceNodeFrames seek). In the port that re-seek is
        // progressSeekNodeSlotsLike_0x6C106C(+456): the live per-node seek
        // (advanceNodeFrameSelectionLike_0x6926B4) is direction-agnostic — it has
        // BOTH a forward AND a corrective-backward slot loop — so a single
        // seek-to-(+456) reproduces advanceRootAndNodes AND rewindRootAndNodes.
        //
        // The clamp `+456 = min(+1120,+1128)` was already applied above (the
        // gated-advance block). LABEL_48 below re-derives the SAME value for the
        // forward-not-at-end (= logo) path, so that path stays bit-identical to
        // P7 step-1; only the at-end / reverse / loop-wrap paths add behavior.
        // gate v23 = +480 snapshot (was sampled at the gated-advance block);
        // deltaTime v24 = +592.
        const bool gate = _queuing;              // v23 = (BYTE)player+480
        const double deltaTime = _deltaTime;     // v24 = player+592

        // Gated clamp (0x6C1340..0x6C1354): when the +480 gate is clear,
        //   v26 = +592 + +1120; +1120 = v26; if (v26 > +1128) v26 = +1128;
        //   +456 = v26;  (= min(advanced cursor, totalFrames))
        // The +1120 advance was already applied above (line ~831, the same
        // `!_queuing` gate). Here we mirror only the +456 clamp half so the
        // forward-not-at-end path keeps the P7 step-1 value bit-for-bit
        // (+456 = min(+1120,+1128); for not-at-end that is +1120 = _frameTickCount).
        if(!gate) {                              // 0x6C1330 !(BYTE)player+480
            double v26 = _frameTickCount;        // +1120 (already += +592 above)
            if(v26 > _cachedTotalFrames) {       // 0x6C1350
                v26 = _cachedTotalFrames;
            }
            _clampedEvalTime = v26;              // +456 = min(+1120,+1128) (0x6C1354)
        }

        bool reseekNodes = false;                // whether to seek slots this frame

        if(deltaTime >= 0.0) {                   // 0x6C135C: FORWARD
            const double totalFrames = _cachedTotalFrames; // v29 = player+1128
            if(totalFrames <= _frameTickCount) { // 0x6C13A0: reached/past end
                // M1 P7 step-2 (5553dd2): binary reads +1136 as fixed loop-wrap
                // target (set once at setMotion from clip->loopTime, see
                // PlayerCore.cpp:~557) and NEVER mutates it inside progress_inner.
                // R-M9 spike Q1/Q2 confirmed 5 reads + 0 writes of +1136 in
                // progress_inner. The earlier `_loopTime += actualDelta` port-
                // invented accumulator was removed in commit e11ecef so this
                // path is now numerically aligned with binary's loop-wrap math.
                const double loopTime = _loopTime; // v31 = player+1136
                _clampedEvalTime = totalFrames;    // +456 = v29 (= totalFrames)
                if(loopTime >= 0.0) {              // 0x6C13F0: LOOP
                    // advanceRootAndNodes at the clamped end (+456 = totalFrames)
                    // 砖6/Stage A (洞3 调用点重定位): Player_advanceRootAndNodes
                    // (0x6B6ADC) runs the layer event stream (+916 cursor) BEFORE
                    // the node-deque walk (LABEL_86), both keyed on the SAME +456.
                    // The layer pass may align/sync-snap +456/+1120 (0x6B6DD8,
                    // +1093-only gate), and that snap must reach this SAME advance
                    // point's node walk. So seek the layer stream here, immediately
                    // before progressSeekNodeSlotsLike (= the node walk), at each
                    // advanceRoot/rewind equivalent point — not once at end-of-frame.
                    seekLayerEventStreamLike_0x6B6ADC(_clampedEvalTime); // 0x6B6B80 layer pass
                    advanceVariableTracksLike_0x6B6ADC(_clampedEvalTime); // 0x6B7124 var-track ③
                    progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime); // 0x6C1468 node walk
                    if(!_syncWaiting && !_motionCompleted) {              // 0x6C1474
                        _clampedEvalTime = _loopTime; // +456 = player+1136 (0x6C1484)
                        progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime); // reseek (0x6C1488)
                        if(!_syncWaiting && !_motionCompleted) {          // 0x6C1498
                            // LABEL_22/23 loop-wrap (0x6C14AC..0x6C14CC):
                            //   v7 = +1120; if (+1128 > v7) goto LABEL_23;
                            //   do v7 += +1136 - +1128; while (+1128 <= v7);
                            //   +1120 = v7;  (LABEL_22)  +456 = v7; (LABEL_23)
                            double v7 = _frameTickCount;            // 0x6C14B0
                            if(_cachedTotalFrames > v7) {           // 0x6C14B8 -> LABEL_23
                                _clampedEvalTime = v7;              // LABEL_23 (0x6C119C)
                            } else {
                                do {
                                    v7 = v7 + _loopTime - _cachedTotalFrames; // 0x6C14C4
                                } while(_cachedTotalFrames <= v7);  // 0x6C14CC
                                _frameTickCount = v7;               // LABEL_22 (0x6C1198)
                                _clampedEvalTime = v7;              // LABEL_23 (0x6C119C)
                            }
                            // Stage A: layer pass before node walk at this 2nd
                            // advanceRoot (0x6C11B0, +456 = wrapped tick).
                            seekLayerEventStreamLike_0x6B6ADC(_clampedEvalTime);
                            advanceVariableTracksLike_0x6B6ADC(_clampedEvalTime); // var-track ③
                            progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime); // 0x6C11B0
                        }
                    }
                } else {                          // 0x6C13F4: NON-LOOP, hit end -> stop
                    _allplaying = false;          // player+1099 = 0 (0x6C13F4)
                    if(!gate) {                   // 0x6C13F8
                        reseekNodes = true;       // advanceRootAndNodes
                    }
                }
            } else if(!gate) {                    // 0x6C13A4: NOT at end (= logo path)
                reseekNodes = true;               // advanceRootAndNodes
            }
            // else: gate set & not-at-end -> no seek (binary returns result)
        } else {                                  // 0x6C1360: REVERSE (deltaTime < 0)
            const double frameTickCount = _frameTickCount; // v27 = player+1120
            const double loopTime = _loopTime;             // v28 = player+1136
            if(frameTickCount >= 0.0 && loopTime <= frameTickCount) { // 0x6C1374 -> LABEL_57
                if(!gate) {                       // 0x6C138C
                    reseekNodes = true;           // rewindRootAndNodes (LABEL_57)
                }
            } else if(loopTime < 0.0) {           // 0x6C137C: non-loop, hit start
                _clampedEvalTime = 0.0;           // player+456 = 0 (0x6C1380)
                _allplaying = false;              // player+1099 = 0 (0x6C1384)
                _frameTickCount = 0.0;            // player+1120 = 0 (0x6C1388)
                if(!gate) {                       // LABEL_57 (0x6C138C)
                    reseekNodes = true;           // rewindRootAndNodes
                }
            } else {                              // 0x6C1404: reverse loop-wrap
                _clampedEvalTime = loopTime;      // player+456 = v28 (0x6C1404)
                // Stage A: rewindRootAndNodes (0x6B9A3C) runs the BACKWARD layer
                // pass (0x6B9AE8: --+916 while curTime>+456, same +1093-only gate)
                // before its node walk. Layer pass before node walk here too.
                seekLayerEventStreamLike_0x6B6ADC(_clampedEvalTime);
                advanceVariableTracksLike_0x6B6ADC(_clampedEvalTime); // var-track ③
                progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime); // rewindRootAndNodes (0x6C1408)
                if(!_syncWaiting && !_motionCompleted) {              // 0x6C1414
                    _clampedEvalTime = _cachedTotalFrames; // +456 = player+1128 (0x6C1424)
                    progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime); // reseek (0x6C1428)
                    if(!_syncWaiting && !_motionCompleted) {          // 0x6C1434
                        // LABEL_27/28 reverse loop-wrap (0x6C143C..0x6C145C):
                        //   v7 = +1120; if (+1136 <= v7) goto LABEL_28;
                        //   do v7 += +1128 - +1136; while (+1136 > v7);
                        //   +1120 = v7;  (LABEL_27)  +456 = v7; (LABEL_28)
                        double v7 = _frameTickCount;            // 0x6C1440
                        if(_loopTime <= v7) {                   // 0x6C1448 -> LABEL_28
                            _clampedEvalTime = v7;              // LABEL_28 (0x6C11BC)
                        } else {
                            do {
                                v7 = v7 - _loopTime + _cachedTotalFrames; // 0x6C1454
                            } while(_loopTime > v7);            // 0x6C145C
                            _frameTickCount = v7;               // LABEL_27 (0x6C11B8)
                            _clampedEvalTime = v7;              // LABEL_28 (0x6C11BC)
                        }
                        // Stage A: layer pass before node walk at this 2nd rewind
                        // (0x6C11C0, +456 = reverse-wrapped tick).
                        seekLayerEventStreamLike_0x6B6ADC(_clampedEvalTime);
                        advanceVariableTracksLike_0x6B6ADC(_clampedEvalTime); // var-track ③
                        progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime); // 0x6C11C0
                    }
                }
            }
        }

        // M1/P7 step-1: progress-pass cursor stepping (forward-normal /
        // non-loop-end / reverse-normal terminal: advance|rewindRootAndNodes).
        // Aligned to Player_progress_inner (0x6C106C) LABEL_48 / the node-deque
        // walk at 0x6C1288: once +456 (clampedEvalTime) is established, the
        // binary advances every node's frame cursor HERE (Player_advanceNodeFrames
        // 0x6B7E44, reached at 0x6C1264/0x6C130C), filling the two parsed-frame
        // slots (node+320/+856). The SEPARATE Player_updateLayers pass (0x6BB33C)
        // then only interpolates those slots (Player_evaluateTimeline 0x699AE4).
        // The loop-wrap / reverse-wrap branches above already re-seeked inline
        // (matching the binary's terminal advance/rewindRootAndNodes calls); this
        // covers the forward-normal / reverse-normal / non-loop-end cases.
        if(reseekNodes) {
            // Stage A: this is the forward-not-at-end advanceRoot (0x6C13A4) OR
            // the reverse LABEL_57 rewind (0x6C138C) — the normal-playback (logo)
            // path. Both run the layer event stream before the node walk, keyed on
            // the same +456. The seek-direction is encoded by deltaTime sign, and
            // seekLayerEventStreamLike is bidirectional (forward 0x6B6B80 / backward
            // 0x6B9AE8) self-selecting on cursor vs target, so one call covers both.
            // Layer pass is NOT gated on _nodes.empty() (it walks motion["tag"], not
            // the node deque); only the node walk is.
            seekLayerEventStreamLike_0x6B6ADC(_clampedEvalTime);
            advanceVariableTracksLike_0x6B6ADC(_clampedEvalTime); // var-track ③ (not _nodes-gated)
            if(!_nodes.empty()) {
                progressSeekNodeSlotsLike_0x6C106C(_clampedEvalTime);
            }
        }

        // 砖6/Stage A (洞3 调用点重定位 — DONE): the faithful layer
        // (motion["tag"]) event stream is now driven INSIDE each advanceRoot /
        // rewind equivalent point above (each progressSeekNodeSlotsLike is now
        // preceded by seekLayerEventStreamLike on the SAME +456), matching the
        // binary where Player_advanceRootAndNodes (0x6B6ADC) / rewindRootAndNodes
        // (0x6B9A3C) run [layer stream -> root -> var-track -> node walk] as one
        // unit. This fixes the three defects of the old single end-of-frame call:
        //   (1) loop-wrap segments (totalFrames then loopTime) each now scan the
        //       layer stream, so align/sync/action inside a wrapped segment fire;
        //   (2) align/sync snaps of +456/+1120 now propagate to the SAME advance's
        //       node walk (was a 1-frame lag);
        //   (3) the gate-set not-at-end / firstFrame-queuing paths correctly do
        //       NOT scan the layer stream (binary returns without advanceRoot).
        // (Player_reseekTimelineCursors 0x6B86C8 — the firstFrame seed + the two
        // loop-wrap reseek points 0x6C1488/0x6C1428 — carries its OWN layer scan
        // with a DIFFERENT gate (+920==+456 precise-frame, 0x6B8AC0); that is
        // Stage B and intentionally NOT covered by these advance-form seeks.)
        // (Per-node frame actions — node mask 0x40000 from the node seek — remain
        // 洞2, already handled inside progressSeekNodeSlotsLike's _pendingEvents.)

        _allplaying = !_playingTimelineLabels.empty();
        _syncActive = _syncWaiting && _allplaying;
    }


    void Player::progressMsLike_0x6D2A54(double deltaMs) {
        ensureMotionLoaded();
        if(deltaMs < 0 || deltaMs > 60000) {
            deltaMs = 0;
        }

        if(true) {
            _pendingEvents.clear();
        }
        frameProgress(deltaMs * kMotionFramesPerMillisecond);
        if(!_nodes.empty()) {
            updateLayers();
        }
        calcBounds();
        if(true) {
            _pendingEvents.clear();
        }
    }

    tjs_error Player::progressCompatMethod(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        self->ensureMotionLoaded();
        detail::MotionTraceProgressScope motionTraceScope(self, objthis);

        double delta = 0.0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            delta = param[0]->AsReal();
        }
        // Clamp delta to sane range: TJS tick differences can overflow
        // when uint32 wraps (e.g. 4294967381 = 2^32 + 85)
        if(delta < 0 || delta > 60000) {
            delta = 0;
        }

        self->_pendingEvents.clear();
        self->frameProgress(delta * kMotionFramesPerMillisecond);
        const auto motionPath =
            self->_activeMotion
                ? self->_activeMotion->path
                : std::string{};
        detail::logoChainTraceCheck(
            motionPath, "progressCompat.dt", "0x6D2A98",
            self->_clampedEvalTime,
            fmt::format("dt_ms*60/1000={:.6f}", delta * kMotionFramesPerMillisecond),
            fmt::format("dt_frames={:.6f}", self->_frameLastTime),
            std::fabs(self->_frameLastTime - delta * kMotionFramesPerMillisecond) <
                0.000001,
            "progressCompat dt(ms)->frame conversion diverged from 0x6D2A98");

        // Aligned to libkrkr2.so Player_progressCompat (0x6D2A98):
        // progress_inner -> updateLayers -> calcBounds -> dispatchEvents.
        // The binary assumes the node tree is already built (it was built
        // eagerly inside play()/setMotion()), so there is no lazy build here.
        if(!self->_nodes.empty()) {
            detail::logoChainTraceLogf(
                motionPath, "progressCompat.update", "0x6D2A98",
                self->_clampedEvalTime,
                "timelineCurrentTime={:.3f} pendingEvents={} nodes={}",
                self->_clampedEvalTime, self->_pendingEvents.size(),
                self->_nodes.size());
            self->updateLayers();
        }
        self->calcBounds();

        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           self->_clampedEvalTime >= 0.0 && self->_clampedEvalTime <= 60.0) {
            std::fprintf(stderr,
                         "SNAPTIME motion=%s frame=%.3f playing=%d nodes=%zu\n",
                         motionPath.c_str(), self->_clampedEvalTime,
                         self->_allplaying ? 1 : 0,
                         self->_nodes.size());
        }

        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
           self->_clampedEvalTime >= 30.0 && self->_clampedEvalTime <= 50.0) {
            std::fprintf(stderr, "SHOTMARK motion=%s frame=%.3f\n",
                         motionPath.c_str(), self->_clampedEvalTime);
        }

        // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
        // After stepping timelines, dispatch queued onAction/onSync events.
        if(!self->_pendingEvents.empty()) {
            for(const auto &ev : self->_pendingEvents) {
                try {
                    if(ev.type == 0) {
                        // onAction(param1, param2). 砖5/洞3: the layer (tag)
                        // stream passes a void param1 (record.a, §8.7); the
                        // legacy per-node path uses a string param1.
                        tTJSVariant p1;
                        if(!ev.voidParam1) {
                            p1 = tTJSVariant(detail::widen(ev.param1));
                        }
                        tTJSVariant p2(detail::widen(ev.param2));
                        tTJSVariant *args[] = { &p1, &p2 };
                        objthis->FuncCall(0, TJS_W("onAction"),
                            nullptr, nullptr, 2, args, objthis);
                    } else if(ev.type == 1) {
                        // onSync()
                        objthis->FuncCall(0, TJS_W("onSync"),
                            nullptr, nullptr, 0, nullptr, objthis);
                    }
                } catch(...) {}
            }
            self->_pendingEvents.clear();
        }

        if(result) {
            *result = tTJSVariant(self->getProgressCompat());
        }
        return TJS_S_OK;
    }

} // namespace motion
