// PlayerTimeline.cpp — timeline queries and playback raw callbacks
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "ncbind.hpp"
#include "tjsDebug.h"

using namespace motion::internal;

namespace motion {
    namespace {
        bool shouldEmitPlaybackDiag(std::uint32_t seq) {
            return seq <= 200 || (seq % 100) == 0;
        }

        const char *boolText(bool v) {
            return v ? "true" : "false";
        }

        std::string joinPlayingLabels(const std::vector<std::string> &labels) {
            std::string joined;
            for(const auto &timelineLabel : labels) {
                if(!joined.empty()) {
                    joined += ",";
                }
                joined += timelineLabel;
            }
            return joined.empty() ? std::string("<none>") : joined;
        }

        std::string shortTJSStackTrace(tjs_int limit = 8) {
            ttstr stack = TJSGetStackTraceString(limit, TJS_W(" <- "));
            return stack.AsStdString();
        }
    }

    void Player::skipToSync() {
        for(auto &[_, state] : _timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames;
            }
            if(!state.loop) {
                state.playing = false;
            }
        }
        if(const auto it = std::remove_if(_playingTimelineLabels.begin(),
                                          _playingTimelineLabels.end(),
                                          [this](const std::string &label) {
                                              const auto found =
                                                  _timelines.find(label);
                                              return found ==
                                                      _timelines.end() ||
                                                  !found->second.playing;
                                          });
           it != _playingTimelineLabels.end()) {
            _playingTimelineLabels.erase(
                it, _playingTimelineLabels.end());
        }
        _syncWaiting = false;
        // (B) Removed `_syncActive = false`: syncActive(+1093) is script-set only
        // (writers = ctor 0x6CF11C + setSyncActive 0x6D9698); not cleared here.
        _allplaying = !_playingTimelineLabels.empty();
    }

    bool Player::getTimelinePlaying(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _timelines.find(key);
           it != _timelines.end()) {
            return it->second.playing;
        }
        return false;
    }

    tjs_int Player::countMainTimelines() {
        ensureMotionLoaded();
        return _activeMotion
            ? static_cast<tjs_int>(_activeMotion->mainTimelineLabels.size())
            : 0;
    }

    ttstr Player::getMainTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _activeMotion->mainTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_activeMotion->mainTimelineLabels[idx]);
    }

    tTJSVariant Player::getMainTimelineLabelList() {
        ensureMotionLoaded();
        if(!_activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _activeMotion->mainTimelineLabels));
    }

    tjs_int Player::countDiffTimelines() {
        ensureMotionLoaded();
        return _activeMotion
            ? static_cast<tjs_int>(_activeMotion->diffTimelineLabels.size())
            : 0;
    }

    ttstr Player::getDiffTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _activeMotion->diffTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_activeMotion->diffTimelineLabels[idx]);
    }

    tTJSVariant Player::getDiffTimelineLabelList() {
        ensureMotionLoaded();
        if(!_activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _activeMotion->diffTimelineLabels));
    }

    bool Player::getLoopTimeline(ttstr label) {
        ensureMotionLoaded();
        if(!_activeMotion) {
            return false;
        }
        const auto key = detail::narrow(label);
        if(const auto it = _activeMotion->loopTimelines.find(key);
           it != _activeMotion->loopTimelines.end()) {
            return it->second;
        }
        return false;
    }

    tjs_int Player::countPlayingTimelines() {
        ensureMotionLoaded();
        return static_cast<tjs_int>(_playingTimelineLabels.size());
    }

    ttstr Player::getPlayingTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(idx >= 0 &&
           static_cast<size_t>(idx) < _playingTimelineLabels.size()) {
            return detail::widen(_playingTimelineLabels[idx]);
        }
        return {};
    }

    tjs_int Player::getPlayingTimelineFlagsAt(tjs_int idx) {
        ensureMotionLoaded();
        if(idx >= 0 &&
           static_cast<size_t>(idx) < _playingTimelineLabels.size()) {
            const auto &label = _playingTimelineLabels[idx];
            if(const auto it = _timelines.find(label);
               it != _timelines.end()) {
                return it->second.flags;
            }
        }
        return 0;
    }

    tjs_int Player::getTimelineTotalFrameCount(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _timelines.find(key);
           it != _timelines.end()) {
            return static_cast<tjs_int>(it->second.totalFrames);
        }
        if(_activeMotion) {
            if(const auto it = _activeMotion->timelineTotalFrames.find(key);
               it != _activeMotion->timelineTotalFrames.end()) {
                return static_cast<tjs_int>(it->second);
            }
        }
        return 0;
    }

    void Player::playTimeline(ttstr label, tjs_int flags) {
        ensureMotionLoaded();
        if(!_activeMotion) {
            return;
        }
        if(_timelines.empty()) {
            detail::primeTimelineStates(_timelines, *_activeMotion);
        }

        const auto key = detail::narrow(label);
        auto it = _timelines.find(key);
        if(it == _timelines.end()) {
            return;
        }

        // Aligned to libkrkr2.so Player_playTimeline (0x672F70):
        // parallel flag first clears the playing-timeline list.
        if((flags & 1) != 0) {
            stopTimeline(TJS_W(""));
        }

        if(!label.IsEmpty()) {
            if(std::find(_playingTimelineLabels.begin(),
                         _playingTimelineLabels.end(),
                         key) == _playingTimelineLabels.end()) {
                _playingTimelineLabels.push_back(key);
            }
        }

        it->second.flags = flags;
        it->second.playing = true;
        it->second.currentTime = 0.0;
        it->second.blendRatio = 1.0;
        it->second.blendAnimator = {};
        it->second.blendAutoStop = false;
        it->second.controlInitialized = false;
        it->second.controlLastAppliedTime = 0.0;
        it->second.controlFrameCursor.clear();
        it->second.controlTrackValues.clear();
        it->second.controlTrackAnimators.clear();
        if(const auto controlIt =
               _activeMotion->timelineControlByLabel.find(key);
           controlIt != _activeMotion->timelineControlByLabel.end()) {
            resetTimelineControlStateLike_0x671A50(
                it->second, controlIt->second, 0.0);
        }
        _allplaying = !_playingTimelineLabels.empty();
    }

    void Player::stopTimeline(ttstr label) {
        const auto key = detail::narrow(label);
        if(label.IsEmpty()) {
            for(auto &[_, state] : _timelines) {
                state.playing = false;
                state.blendRatio = 1.0;
                state.blendAnimator = {};
                state.blendAutoStop = false;
                state.controlInitialized = false;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
            }
            _playingTimelineLabels.clear();
        } else {
            if(const auto it = _timelines.find(key);
               it != _timelines.end()) {
                it->second.playing = false;
                it->second.blendRatio = 1.0;
                it->second.blendAnimator = {};
                it->second.blendAutoStop = false;
                it->second.controlInitialized = false;
                it->second.controlFrameCursor.clear();
                it->second.controlTrackValues.clear();
                it->second.controlTrackAnimators.clear();
            }
            if(const auto it = std::remove(_playingTimelineLabels.begin(),
                                           _playingTimelineLabels.end(),
                                           key);
               it != _playingTimelineLabels.end()) {
                _playingTimelineLabels.erase(
                    it, _playingTimelineLabels.end());
            }
        }

        _allplaying = !_playingTimelineLabels.empty();
    }

    void Player::setTimelineBlendRatio(ttstr label, double ratio) {
        ensureMotionLoaded();
        if(_timelines.empty() && _activeMotion) {
            detail::primeTimelineStates(_timelines, *_activeMotion);
        }

        const auto key = detail::narrow(label);
        auto &state = _timelines[key];
        state.label = key;
        state.blendRatio = ratio;
        state.blendAnimator = {};
        state.blendAutoStop = false;
    }

    double Player::getTimelineBlendRatio(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _timelines.find(key);
           it != _timelines.end()) {
            return it->second.blendRatio;
        }
        return 1.0;
    }

    void Player::fadeInTimeline(ttstr label, double duration, tjs_int flags) {
        const auto key = detail::narrow(label);
        const bool alreadyPlaying =
            std::find(_playingTimelineLabels.begin(),
                      _playingTimelineLabels.end(),
                      key) != _playingTimelineLabels.end();
        if(!alreadyPlaying) {
            playTimeline(label, 3);
            setTimelineBlendLike_0x6735AC(key, false, 0.0, 0.0, 0.0);
        }
        setTimelineBlendLike_0x6735AC(key, false, 1.0, duration, 0.0);
    }

    void Player::fadeOutTimeline(ttstr label, double duration, tjs_int) {
        setTimelineBlendLike_0x6735AC(detail::narrow(label), true, 0.0,
                                      duration, 0.0);
    }

    tTJSVariant Player::getPlayingTimelineInfoList() {
        ensureMotionLoaded();
        return detail::makeArray(timelineInfoVariants(*this));
    }

    bool Player::playMotionLike_0x6B2284(ttstr label, tjs_int flags) {
        static std::uint32_t s_diagSeq = 0;
        const auto diagSeq = ++s_diagSeq;
        const bool emitDiag = shouldEmitPlaybackDiag(diagSeq);
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playMotionLike enter seq={} this={} label='{}' flags=0x{:x} chara='{}' motionKey='{}' stealth='{}' active={} activePath='{}' timelines={} playingLabels={} allplaying={}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(label), static_cast<unsigned int>(flags),
                detail::narrow(_chara), detail::narrow(_motionKey),
                detail::narrow(_stealthMotion), _activeMotion != nullptr,
                _activeMotion ? _activeMotion->path : std::string("<none>"),
                _timelines.size(), _playingTimelineLabels.size(),
                boolText(_allplaying));
        }

        if(!_activeMotion && _project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(_project)) {
                activateMotion(*this, snapshot);
                syncVariableKeysFromActiveMotion();
                if(emitDiag && LOGGER) {
                    LOGGER->info(
                        "PRTDIAG Player::playMotionLike project-activate seq={} this={} activePath='{}'",
                        diagSeq, static_cast<const void *>(this),
                        _activeMotion ? _activeMotion->path
                                      : std::string("<none>"));
                }
            }
        }

        auto commitRequestedMotionLike_0x6B2380 = [&]() {
            if(label.IsEmpty() || !_activeMotion) {
                return;
            }
            if((flags & PlayFlagStealth) != 0) {
                _stealthMotion = label;
                return;
            }
            // Player_playImpl @ 0x6B2284 writes player+976 before
            // Player_initNonEmoteMotion @ 0x6B365C. Player_getMotion_ncb
            // @ 0x6D9544 then exposes that same slot to TJS.
            _motionKey = label;
        };

        // Player_playImpl @0x6B22E4: when (flags & 8 == PlayFlagJoin), rebuild
        // the var-track HM4 snapshot from the CURRENT motion before loading the
        // new one. Inert for content without a "variable" list (empty deque).
        if((flags & PlayFlagJoin) != 0) {
            resetMotionStateLike_0x6B2D3C();
        }

        const auto activeBeforeEnsure = _activeMotion;
        const bool ensureOk = ensureMotionLoaded();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playMotionLike after-ensure seq={} this={} ok={} activeChanged={} activePath='{}' motionKey='{}'",
                diagSeq, static_cast<const void *>(this), boolText(ensureOk),
                boolText(activeBeforeEnsure != _activeMotion),
                _activeMotion ? _activeMotion->path : std::string("<none>"),
                detail::narrow(_motionKey));
        }
        commitRequestedMotionLike_0x6B2380();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playMotionLike call-init seq={} this={} label='{}' flags=0x{:x} motionKey='{}' active={} sameLabelAfterCommit={}",
                diagSeq, static_cast<const void *>(this),
                detail::narrow(label), static_cast<unsigned int>(flags),
                detail::narrow(_motionKey), _activeMotion != nullptr,
                boolText(_motionKey == label));
        }
        initNonEmoteMotionLike_0x6B365C(
            static_cast<std::uint32_t>(flags));
        if(_activeMotion && _timelines.empty()) {
            detail::primeTimelineStates(_timelines,
                                        *_activeMotion);
        }

        if(!label.IsEmpty() && !_activeMotion) {
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::playMotionLike fallback-setMotion seq={} this={} label='{}'",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(label));
            }
            setMotion(label);
            const bool fallbackEnsureOk = ensureMotionLoaded();
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::playMotionLike fallback-after-ensure seq={} this={} ok={} activePath='{}' motionKey='{}'",
                    diagSeq, static_cast<const void *>(this),
                    boolText(fallbackEnsureOk),
                    _activeMotion ? _activeMotion->path
                                  : std::string("<none>"),
                    detail::narrow(_motionKey));
            }
            commitRequestedMotionLike_0x6B2380();
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::playMotionLike fallback-call-init seq={} this={} label='{}' flags=0x{:x}",
                    diagSeq, static_cast<const void *>(this),
                    detail::narrow(label), static_cast<unsigned int>(flags));
            }
            initNonEmoteMotionLike_0x6B365C(
                static_cast<std::uint32_t>(flags));
            if(_activeMotion && _timelines.empty()) {
                detail::primeTimelineStates(_timelines,
                                            *_activeMotion);
            }
        }

        if(!_activeMotion) {
            return false;
        }

        if((flags & PlayFlagForce) != 0) {
            stopTimeline(TJS_W(""));
        }

        const bool chainMode = (flags & PlayFlagChain) != 0;
        const auto playOne = [&](const std::string &timelineLabel) {
            auto &state = _timelines[timelineLabel];
            state.label = timelineLabel;
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            if(!chainMode) {
                state.currentTime = 0.0;
                state.controlInitialized = false;
                state.controlLastAppliedTime = 0.0;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
            }
            if(std::find(_playingTimelineLabels.begin(),
                         _playingTimelineLabels.end(),
                         timelineLabel) == _playingTimelineLabels.end()) {
                _playingTimelineLabels.push_back(timelineLabel);
            }
            if(state.totalFrames <= 0.0 && _activeMotion) {
                const auto it =
                    _activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != _activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            if(_timelines.find(key) != _timelines.end()) {
                playOne(key);
                started = true;
            }
        }

        if(!started) {
            const auto &primary =
                !_activeMotion->mainTimelineLabels.empty()
                    ? _activeMotion->mainTimelineLabels
                    : _activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel);
                started = true;
            }
        }

        _allplaying = !_playingTimelineLabels.empty();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playMotionLike exit seq={} this={} started={} activePath='{}' motionKey='{}' timelines={} playingLabels={} allplaying={}",
                diagSeq, static_cast<const void *>(this), boolText(started),
                _activeMotion ? _activeMotion->path : std::string("<none>"),
                detail::narrow(_motionKey), _timelines.size(),
                _playingTimelineLabels.size(), boolText(_allplaying));
        }
        return started;
    }

    tjs_error Player::playCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        ttstr label;
        tjs_int flags = 0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            if(param[0]->Type() == tvtInteger || param[0]->Type() == tvtReal) {
                flags = param[0]->AsInteger();
            } else {
                label = *param[0];
            }
        }
        if(numparams > 1 && param[1] && param[1]->Type() != tvtVoid) {
            flags = param[1]->AsInteger();
        }

        static std::uint32_t s_playCompatDiagSeq = 0;
        const auto diagSeq = ++s_playCompatDiagSeq;
        const bool emitDiag = shouldEmitPlaybackDiag(diagSeq);
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playCompat enter seq={} this={} label='{}' flags=0x{:x} chara='{}' motionKey='{}' active={} activePath='{}' timelines={} playingLabels={} allplaying={}",
                diagSeq, static_cast<const void *>(self),
                detail::narrow(label), static_cast<unsigned int>(flags),
                detail::narrow(self->_chara),
                detail::narrow(self->_motionKey),
                self->_activeMotion != nullptr,
                self->_activeMotion ? self->_activeMotion->path
                                    : std::string("<none>"),
                self->_timelines.size(), self->_playingTimelineLabels.size(),
                boolText(self->_allplaying));
        }

        if(!self->_activeMotion && self->_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(self->_project)) {
                activateMotion(*self, snapshot);
                self->syncVariableKeysFromActiveMotion();
                if(emitDiag && LOGGER) {
                    LOGGER->info(
                        "PRTDIAG Player::playCompat project-activate seq={} this={} activePath='{}'",
                        diagSeq, static_cast<const void *>(self),
                        self->_activeMotion ? self->_activeMotion->path
                                            : std::string("<none>"));
                }
            }
        }

        auto commitRequestedMotionLike_0x6B2380 = [&]() {
            if(label.IsEmpty() || !self->_activeMotion) {
                return;
            }
            if((flags & PlayFlagStealth) != 0) {
                self->_stealthMotion = label;
                return;
            }
            // Player_playImpl @ 0x6B2284 writes player+976 before
            // Player_initNonEmoteMotion @ 0x6B365C. Player_getMotion_ncb
            // @ 0x6D9544 then exposes that same slot to TJS.
            self->_motionKey = label;
        };

        // Aligned to libkrkr2.so Player_playImpl (0x6B2284) ->
        // Player_initNonEmoteMotion (0x6B365C): after loadMotion, the binary
        // synchronously calls Player_buildNodeTree (0x6B51F0) and
        // Player_initVariables (0x6CD750) before setting any playing state.
        // No lazy gate exists in the binary.
        const auto activeBeforeEnsure = self->_activeMotion;
        const bool ensureOk = self->ensureMotionLoaded();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playCompat after-ensure seq={} this={} ok={} activeChanged={} activePath='{}' motionKey='{}'",
                diagSeq, static_cast<const void *>(self), boolText(ensureOk),
                boolText(activeBeforeEnsure != self->_activeMotion),
                self->_activeMotion ? self->_activeMotion->path
                                    : std::string("<none>"),
                detail::narrow(self->_motionKey));
        }
        commitRequestedMotionLike_0x6B2380();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playCompat call-init seq={} this={} label='{}' flags=0x{:x} motionKey='{}' active={} sameLabelAfterCommit={}",
                diagSeq, static_cast<const void *>(self),
                detail::narrow(label), static_cast<unsigned int>(flags),
                detail::narrow(self->_motionKey),
                self->_activeMotion != nullptr,
                boolText(self->_motionKey == label));
        }
        self->initNonEmoteMotionLike_0x6B365C(
            static_cast<std::uint32_t>(flags));
        if(self->_activeMotion && self->_timelines.empty()) {
            detail::primeTimelineStates(self->_timelines,
                                        *self->_activeMotion);
        }

        if(!label.IsEmpty() && !self->_activeMotion) {
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::playCompat fallback-setMotion seq={} this={} label='{}'",
                    diagSeq, static_cast<const void *>(self),
                    detail::narrow(label));
            }
            self->setMotion(label);
            const bool fallbackEnsureOk = self->ensureMotionLoaded();
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::playCompat fallback-after-ensure seq={} this={} ok={} activePath='{}' motionKey='{}'",
                    diagSeq, static_cast<const void *>(self),
                    boolText(fallbackEnsureOk),
                    self->_activeMotion ? self->_activeMotion->path
                                        : std::string("<none>"),
                    detail::narrow(self->_motionKey));
            }
            commitRequestedMotionLike_0x6B2380();
            if(emitDiag && LOGGER) {
                LOGGER->info(
                    "PRTDIAG Player::playCompat fallback-call-init seq={} this={} label='{}' flags=0x{:x}",
                    diagSeq, static_cast<const void *>(self),
                    detail::narrow(label), static_cast<unsigned int>(flags));
            }
            self->initNonEmoteMotionLike_0x6B365C(
                static_cast<std::uint32_t>(flags));
            if(self->_activeMotion && self->_timelines.empty()) {
                detail::primeTimelineStates(self->_timelines,
                                            *self->_activeMotion);
            }
        }

        if(!self->_activeMotion) {
            if(result) {
                *result = tTJSVariant(false);
            }
            return TJS_S_OK;
        }

        if((flags & PlayFlagForce) != 0) {
            self->stopTimeline(TJS_W(""));
        }

        // Aligned to libkrkr2.so Player_initNonEmoteMotion (0x6B3A8C):
        //   if ((flags & 2) == 0) {                // non-Chain
        //       Player+456 = fmin(Player+1128, 0); // reset lastTime
        //       Player+1120 = 0;                   // reset time counter
        //       Player+480  = 257;                 // playing state
        //   }
        // Chain mode (bit 1) preserves the prior play position; non-Chain
        // resets time to the motion's origin. At the per-timeline level we
        // mirror this by gating currentTime / control cursor reset on
        // non-Chain. Label/flags/blendRatio/playing stay unconditional
        // because the binary always stores the new motion state.
        const bool chainMode = (flags & PlayFlagChain) != 0;
        const auto playOne = [&](const std::string &timelineLabel) {
            auto &state = self->_timelines[timelineLabel];
            state.label = timelineLabel;
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            if(!chainMode) {
                state.currentTime = 0.0;
                state.controlInitialized = false;
                state.controlLastAppliedTime = 0.0;
                state.controlFrameCursor.clear();
                state.controlTrackValues.clear();
                state.controlTrackAnimators.clear();
            }
            if(std::find(self->_playingTimelineLabels.begin(),
                         self->_playingTimelineLabels.end(),
                         timelineLabel) ==
               self->_playingTimelineLabels.end()) {
                self->_playingTimelineLabels.push_back(timelineLabel);
            }
            // Ensure totalFrames is set (may be 0 if timeline wasn't primed)
            if(state.totalFrames <= 0.0 && self->_activeMotion) {
                auto it = self->_activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != self->_activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            if(self->_timelines.find(key) != self->_timelines.end()) {
                playOne(key);
                started = true;
            }
        }

        if(!started) {
            const auto &primary = !self->_activeMotion->mainTimelineLabels.empty()
                ? self->_activeMotion->mainTimelineLabels
                : self->_activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel);
                started = true;
            }
        }

        self->_allplaying = !self->_playingTimelineLabels.empty();
        if(emitDiag && LOGGER) {
            LOGGER->info(
                "PRTDIAG Player::playCompat exit seq={} this={} started={} activePath='{}' motionKey='{}' timelines={} playingLabels={} allplaying={}",
                diagSeq, static_cast<const void *>(self), boolText(started),
                self->_activeMotion ? self->_activeMotion->path
                                    : std::string("<none>"),
                detail::narrow(self->_motionKey), self->_timelines.size(),
                self->_playingTimelineLabels.size(),
                boolText(self->_allplaying));
        }

        if(self->_activeMotion &&
           detail::logoChainTraceEnabled(self->_activeMotion)) {
            detail::logoChainTraceLogf(
                self->_activeMotion->path, "playCompat", "0x6B2284",
                self->_clampedEvalTime,
                "label={} flags={} started={} timelineCount={} playingLabels={} allplaying={} stack={}",
                detail::narrow(label), flags, started ? 1 : 0,
                self->_timelines.size(),
                joinPlayingLabels(self->_playingTimelineLabels),
                self->_allplaying ? 1 : 0, shortTJSStackTrace());
        }

        if(result) {
            *result = tTJSVariant(started);
        }
        return TJS_S_OK;
    }

    tjs_error Player::isPlayingCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        const bool playing = !self->_playingTimelineLabels.empty();
        self->_allplaying = playing;
        if(result) {
            *result = tTJSVariant(playing);
        }
        return TJS_S_OK;
    }

    tjs_error Player::stopCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        // Aligned to libkrkr2.so Player_stop (0x6D9A30):
        // Binary simply clears the Player-level playing flag (player+1099).
        // Timeline state is left intact; TJS polls `playing` for edge-triggered
        // stop detection and may still inspect the final motion pose afterward.
        if(self->_activeMotion &&
           detail::logoChainTraceEnabled(self->_activeMotion)) {
            detail::logoChainTraceLogf(
                self->_activeMotion->path, "stopCompat", "0x6D9A30",
                self->_clampedEvalTime,
                "numparams={} playingLabelsBefore={} allplayingBefore={} timelineCount={} stack={}",
                numparams, joinPlayingLabels(self->_playingTimelineLabels),
                self->_allplaying ? 1 : 0, self->_timelines.size(),
                shortTJSStackTrace());
        }
        self->_allplaying = false;

        if(result) {
            *result = tTJSVariant(true);
        }
        return TJS_S_OK;
    }

    // clear #72 — raw-callback entry, aligned with libkrkr2.so
    // Player_drawToLayerCompat @0x6D2D80. Despite the member name "clear", the
    // binary callback is a gated draw-to-layer routine: param[0] is the target
    // Layer object, param[1] is the fill value (a3 in the binary, switched on
    // its variant type at *(a3+16)). The whole body is gated on the player+544
    // dword, which is the type tag of the motion tTJSVariant at player+528, not the
    // independent clearEnabled property at player+1144. If no motion is
    // loaded, the binary returns immediately doing nothing.
    tjs_error Player::clearCompat(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        // Binary: `if (*(player+544))` — gate on player+528 motion variant's
        // non-void type tag. _activeMotion is the port counterpart.
        if(self->_activeMotion && numparams >= 1 && param[0]) {
            tTJSVariant fill =
                (numparams >= 2 && param[1]) ? *param[1] : tTJSVariant();
            self->drawToLayerCompat(*param[0], fill);
        }
        if(result) {
            *result = tTJSVariant();
        }
        return TJS_S_OK;
    }

    // Instance worker for clear — aligned with libkrkr2.so
    // Player_drawToLayerCompat @0x6D2D80 (recursive body).
    //
    // Binary structure (post-gate):
    //   1. FAST PATH: PropGetByNum(targetLayer, flags=2, num=dword_1AB8820) — a
    //      runtime-resolved member index that, when present with a non-null
    //      +8 sub-object, routes the fill through sub_6ADCAC(subObj, fillInt).
    //      dword_1AB8820 is a member-index cache (0xffffffff placeholder in the
    //      static .so), so the exact UTF-16 member name cannot be resolved
    //      statically. DOCUMENTED GAP: this fast path is not ported; the port
    //      always takes the general fillRect path below (observably equivalent
    //      for a plain Layer target, which is the common case).
    //   2. GENERAL PATH: builds a "Layer" class wrapper, lazily recomputes the
    //      tTVPComplexRect bound via sub_7E3ECC(player+864), reads it as
    //      {left=+884, top=+888, w=+892-+884, h=+896-+888}, then calls
    //      fillRect(left, top, w, h, fillValue) on the target layer instance.
    //   3. RECURSION: iterates _nodes[1..]; for each node with nodeType==3,
    //      resolves the child Player and recurses drawToLayerCompat with the
    //      same target layer + fill value.
    void Player::drawToLayerCompat(const tTJSVariant &targetLayer,
                                   const tTJSVariant &fillValue) {
        iTJSDispatch2 *layer = targetLayer.Type() == tvtObject
                                   ? targetLayer.AsObjectNoAddRef()
                                   : nullptr;
        if(layer) {
            // General path: FuncCall(L"fillRect", left, top, w, h, fillValue).
            const tTVPRect &bound = _drawRegion.GetBound();
            tTJSVariant left(bound.left);
            tTJSVariant top(bound.top);
            tTJSVariant w(bound.get_width());
            tTJSVariant h(bound.get_height());
            tTJSVariant fill(fillValue);
            tTJSVariant *args[] = {&left, &top, &w, &h, &fill};
            tjs_uint32 hint = 0;
            layer->FuncCall(0, TJS_W("fillRect"), &hint, nullptr, 5, args,
                            layer);
        }
        // Recurse over nodeType==3 children (binary loop i=1.. over _nodes).
        for(size_t ni = 1; ni < _nodes.size(); ++ni) {
            auto &node = _nodes[ni];
            if(node.nodeType == 3) {
                if(auto *child = node.getChildPlayer()) {
                    child->drawToLayerCompat(targetLayer, fillValue);
                }
            }
        }
    }

} // namespace motion
