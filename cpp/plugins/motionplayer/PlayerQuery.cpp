// PlayerQuery.cpp — Viewport, timeline/variable queries, selector, misc, compat
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"

using namespace motion::internal;

namespace motion {

    // --- Viewport/display ---
    void Player::setFlip(bool v) { _runtime->flip = v; }

    void Player::setOpacity(double v) { _runtime->opacity = v; }

    void Player::setVisible(bool v) { _runtime->visible = v; }

    void Player::setSlant(double v) { _runtime->slant = v; }

    void Player::setZoom(double v) { _runtime->zoom = v; }

    tTJSVariant Player::getLayerNames() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(activeLayerNames()));
    }

    void Player::releaseSyncWait() {
        _syncWaiting = false;
        _syncActive = false;
    }

    void Player::calcViewParam() {
        _runtime->lastViewParam = detail::makeDictionary({
            { "flip", _runtime->flip },
            { "opacity", _runtime->opacity },
            { "visible", _runtime->visible },
            { "slant", _runtime->slant },
            { "zoom", _runtime->zoom },
            { "zFactor", _zFactor },
            { "colorWeight", _colorWeight },
        });
    }

    tTJSVariant Player::getLayerMotion(ttstr name) {
        const auto *layers = activeLayersByName();
        if(!layers) {
            return {};
        }

        const auto key = detail::narrow(name);
        if(const auto it = layers->find(key); it != layers->end()) {
            return it->second->toTJSVal();
        }

        return {};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        const auto layer = getLayerMotion(name);
        if(layer.Type() == tvtVoid) {
            return {};
        }

        const auto layerId = requireLayerId(name);
        return detail::makeDictionary({
            { "name", name },
            { "id", layerId },
            { "motion", layer },
        });
    }

    tTJSVariant Player::getLayerGetterList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        std::vector<tTJSVariant> items;
        for(const auto &layerName : activeLayerNames()) {
            const auto getter = getLayerGetter(detail::widen(layerName));
            if(getter.Type() != tvtVoid) {
                items.push_back(getter);
            }
        }
        return detail::makeArray(items);
    }

    void Player::skipToSync() {
        for(auto &[_, state] : _runtime->timelines) {
            if(state.totalFrames > 0.0) {
                state.currentTime = state.totalFrames;
            }
            if(!state.loop) {
                state.playing = false;
            }
        }
        _syncWaiting = false;
        _syncActive = false;
    }

    void Player::setStereovisionCameraPosition(double x, double y, double z) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tTJSVariant vx = x;
        tTJSVariant vy = y;
        tTJSVariant vz = z;
        static tjs_uint addHint = 0;
        tTJSVariant *argsX[] = { &vx };
        tTJSVariant *argsY[] = { &vy };
        tTJSVariant *argsZ[] = { &vz };
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsX, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsY, array);
        array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, argsZ, array);
        _cameraPosition = tTJSVariant(array, array);
        array->Release();
    }

    // --- Timeline/variable queries ---
    void Player::setVariable(ttstr label, double value) {
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return;
        }
        _variableValues[key] = value;
    }

    double Player::getVariable(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return 0.0;
        }

        if(const auto it = _variableValues.find(key); it != _variableValues.end()) {
            return it->second;
        }

        if(!_runtime->activeMotion) {
            return 0.0;
        }

        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it != _runtime->activeMotion->variableFrames.end() &&
           !it->second.empty()) {
            return it->second.front().value;
        }

        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return it->second.first;
        }

        return 0.0;
    }

    tjs_int Player::countVariables() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->variableLabels.size())
            : 0;
    }

    ttstr Player::getVariableLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >= _runtime->activeMotion->variableLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->variableLabels[idx]);
    }

    tjs_int Player::countVariableFrameAt(tjs_int idx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0;
        }
        const auto frames = getVariableFrameList(label);
        return getObjectCount(frames);
    }

    ttstr Player::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return {};
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return {};
        }
        return detail::widen(it->second[frameIdx].label);
    }

    double Player::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0.0;
        }

        const auto key = detail::narrow(label);
        if(!_runtime->activeMotion) {
            return 0.0;
        }
        const auto it = _runtime->activeMotion->variableFrames.find(key);
        if(it == _runtime->activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return 0.0;
        }
        return it->second[frameIdx].value;
    }

    bool Player::getTimelinePlaying(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.playing;
        }
        return false;
    }

    tTJSVariant Player::getVariableRange(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableRanges.find(key);
           it != _runtime->activeMotion->variableRanges.end()) {
            return detail::makeArray(
                { tTJSVariant(it->second.first), tTJSVariant(it->second.second) });
        }
        return {};
    }

    tTJSVariant Player::getVariableFrameList(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }

        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->variableFrames.find(key);
           it == _runtime->activeMotion->variableFrames.end()) {
            return detail::makeArray({});
        } else {
            std::vector<tTJSVariant> frames;
            for(const auto &frame : it->second) {
                frames.push_back(detail::makeDictionary({
                    { "label", detail::widen(frame.label) },
                    { "frame", frame.value },
                    { "value", frame.value },
                }));
            }
            return detail::makeArray(frames);
        }
    }

    tjs_int Player::countMainTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->mainTimelineLabels.size())
            : 0;
    }

    ttstr Player::getMainTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->mainTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->mainTimelineLabels[idx]);
    }

    tTJSVariant Player::getMainTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->mainTimelineLabels));
    }

    tjs_int Player::countDiffTimelines() {
        ensureMotionLoaded();
        return _runtime->activeMotion
            ? static_cast<tjs_int>(_runtime->activeMotion->diffTimelineLabels.size())
            : 0;
    }

    ttstr Player::getDiffTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >=
               _runtime->activeMotion->diffTimelineLabels.size()) {
            return {};
        }
        return detail::widen(_runtime->activeMotion->diffTimelineLabels[idx]);
    }

    tTJSVariant Player::getDiffTimelineLabelList() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->diffTimelineLabels));
    }

    bool Player::getLoopTimeline(ttstr label) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->activeMotion->loopTimelines.find(key);
           it != _runtime->activeMotion->loopTimelines.end()) {
            return it->second;
        }
        return false;
    }

    tjs_int Player::countPlayingTimelines() {
        ensureMotionLoaded();
        return static_cast<tjs_int>(timelineInfoVariants(*_runtime).size());
    }

    ttstr Player::getPlayingTimelineLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(const auto *state = nthPlayingTimeline(*_runtime, idx)) {
            return detail::widen(state->label);
        }
        return {};
    }

    tjs_int Player::getPlayingTimelineFlagsAt(tjs_int idx) {
        ensureMotionLoaded();
        if(const auto *state = nthPlayingTimeline(*_runtime, idx)) {
            return state->flags;
        }
        return 0;
    }

    tjs_int Player::getTimelineTotalFrameCount(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return static_cast<tjs_int>(it->second.totalFrames);
        }
        if(_runtime->activeMotion) {
            if(const auto it = _runtime->activeMotion->timelineTotalFrames.find(key);
               it != _runtime->activeMotion->timelineTotalFrames.end()) {
                return static_cast<tjs_int>(it->second);
            }
        }
        return 0;
    }

    void Player::playTimeline(ttstr label, tjs_int flags) {
        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return;
        }
        if(_runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto it = _runtime->timelines.find(key);
        if(it == _runtime->timelines.end()) {
            return;
        }

        it->second.flags = flags;
        it->second.playing = true;
        it->second.currentTime = 0.0;
        if(!label.IsEmpty()) {
            _motionKey = label;
        }
        _allplaying = true;
    }

    void Player::stopTimeline(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            it->second.playing = false;
        }

        _allplaying = std::any_of(
            _runtime->timelines.begin(), _runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
    }

    void Player::setTimelineBlendRatio(ttstr label, double ratio) {
        ensureMotionLoaded();
        if(_runtime->timelines.empty() && _runtime->activeMotion) {
            detail::primeTimelineStates(_runtime->timelines, *_runtime->activeMotion);
        }

        const auto key = detail::narrow(label);
        auto &state = _runtime->timelines[key];
        state.label = key;
        state.blendRatio = ratio;
    }

    double Player::getTimelineBlendRatio(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.blendRatio;
        }
        return 1.0;
    }

    void Player::fadeInTimeline(ttstr label, double, tjs_int flags) {
        playTimeline(label, flags);
        setTimelineBlendRatio(label, 1.0);
    }

    void Player::fadeOutTimeline(ttstr label, double, tjs_int) {
        setTimelineBlendRatio(label, 0.0);
        stopTimeline(label);
    }

    tTJSVariant Player::getPlayingTimelineInfoList() {
        ensureMotionLoaded();
        return detail::makeArray(timelineInfoVariants(*_runtime));
    }

    // --- Selector ---
    bool Player::isSelectorTarget(ttstr name) {
        const auto *layers = activeLayersByName();
        if(!layers) {
            return false;
        }
        const auto key = detail::narrow(name);
        return layers->find(key) != layers->end() &&
            _runtime->disabledSelectorTargets.find(key) ==
                _runtime->disabledSelectorTargets.end();
    }

    void Player::deactivateSelectorTarget(ttstr name) {
        _runtime->disabledSelectorTargets[detail::narrow(name)] = true;
    }

    // --- Misc ---
    tTJSVariant Player::getCommandList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(
            detail::stringsToVariants(activeSourceCandidates()));
    }

    bool Player::getD3DAvailable() { return true; }

    void Player::doAlphaMaskOperation() {}

    // Aligned to libkrkr2.so Player_playImpl (0x6B21E8):
    // Called from sub_6BE0C0 at 0x6BE46C with flags = motionFlags | v12.
    // flags: PlayFlagForce(1)=force reload, PlayFlagStealth(16)=set stealth fields only.
    void Player::onFindMotion(ttstr name, int flags) {
        // PlayFlagStealth (0x10): store as stealth motion, don't load
        // Binary: if ((flags & 0x10) && !player->project) { player->motionKey = name; return; }
        if ((flags & PlayFlagStealth) && _project.Type() == tvtVoid) {
            _stealthMotion = name;
            return;
        }

        // PlayFlagForce (0x01): force reload even if same motion is loaded
        // Binary: Player_setMotionImpl skips reload guard when force flag set
        if ((flags & PlayFlagForce) && _motionKey == name) {
            _motionKey = ttstr();  // clear to bypass same-motion guard in findMotion
        }

        // Load the motion (equivalent to Player_setMotionImpl → loadMotion)
        (void)findMotion(name);

        // After loading, prime timelines and start playback
        // (aligned to Player_setMotionImpl post-load behavior)
        if (_runtime->activeMotion && _runtime->timelines.empty()) {
            detail::primeTimelineStates(_runtime->timelines,
                                        *_runtime->activeMotion);
        }

        // Start all timelines playing (equivalent to playCompat's playOne loop)
        if (_runtime->activeMotion && !_runtime->timelines.empty()) {
            double maxTF = 0.0;
            for (auto &[tlName, state] : _runtime->timelines) {
                state.flags = flags & ~PlayFlagStealth;  // pass flags minus stealth
                state.playing = true;
                state.blendRatio = 1.0;
                if (state.totalFrames > maxTF) maxTF = state.totalFrames;
            }
            _cachedTotalFrames = maxTF;  // player+1128 cached value
            _allplaying = true;
        }

        // Handle pending stealth motion (0x6B226C..0x6B2280)
        if (!_stealthMotion.IsEmpty()) {
            _stealthChara = _chara;
            // stealthMotion is consumed — binary nulls it after use
            _stealthMotion = ttstr();
        }
    }

    tjs_error Player::setDrawAffineTranslateMatrixCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        std::array<double, 6> matrix{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        if(numparams >= 6) {
            for(size_t index = 0; index < matrix.size(); ++index) {
                if(!param[index] || param[index]->Type() == tvtVoid) {
                    return TJS_E_INVALIDPARAM;
                }
                matrix[index] = param[index]->AsReal();
            }
        } else if(numparams == 1 && param[0] && param[0]->Type() == tvtObject &&
                  param[0]->AsObjectNoAddRef() != nullptr) {
            const auto object = *param[0];
            tTJSVariant value;
            if(getObjectProperty(object, TJS_W("m11"), value) &&
               value.Type() != tvtVoid) {
                matrix[0] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m21"), value) &&
               value.Type() != tvtVoid) {
                matrix[1] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m12"), value) &&
               value.Type() != tvtVoid) {
                matrix[2] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m22"), value) &&
               value.Type() != tvtVoid) {
                matrix[3] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m14"), value) &&
               value.Type() != tvtVoid) {
                matrix[4] = value.AsReal();
            }
            if(getObjectProperty(object, TJS_W("m24"), value) &&
               value.Type() != tvtVoid) {
                matrix[5] = value.AsReal();
            }
        } else {
            return TJS_E_BADPARAMCOUNT;
        }

        nativeInstance->_runtime->drawAffineMatrix = matrix;
        return TJS_S_OK;
    }

    tjs_error Player::captureCanvasCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        if(numparams > 0 && param[0] && param[0]->Type() == tvtObject &&
           param[0]->AsObjectNoAddRef() != nullptr) {
            if(nativeInstance->renderToLayer(param[0]->AsObjectNoAddRef())) {
                if(result) {
                    *result = *param[0];
                }
                return TJS_S_OK;
            }
        }

        if(result) {
            *result = nativeInstance->captureCanvas();
        }
        return TJS_S_OK;
    }

    // drawCompat — aligned to libkrkr2.so sub_6D5FB8.
    // Logic:
    //   1. param is D3DAdaptor → set _d3dDrawMode flag, return (no render)
    //   2. param is SLA → mark for SLA processing, return
    //   3. param is Layer → if _d3dDrawMode, render via D3DAdaptor path;
    //      else render directly to Layer
    tjs_error Player::drawCompat(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, Player *nativeInstance) {
        if(result) {
            result->Clear();
        }
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        if(numparams < 1 || !param[0] || param[0]->Type() != tvtObject ||
           !param[0]->AsObjectNoAddRef()) {
            return TJS_S_OK;
        }

        iTJSDispatch2 *paramObj = param[0]->AsObjectNoAddRef();

        // Step 1: Check if param is D3DAdaptor (libkrkr2.so checks NIS with
        // D3DAdaptor classID). If so, set _d3dDrawMode and return immediately.
        {
            tTJSVariant testVar;
            bool isD3DAdaptor =
                ncbInstanceAdaptor<D3DAdaptor>::GetNativeInstance(paramObj, false) != nullptr;
            if(!isD3DAdaptor) {
                // Fallback: check for canvasCaptureEnabled property
                isD3DAdaptor = TJS_SUCCEEDED(paramObj->PropGet(
                    TJS_MEMBERMUSTEXIST, TJS_W("canvasCaptureEnabled"),
                    nullptr, &testVar, paramObj));
            }
            if(isD3DAdaptor) {
                nativeInstance->_d3dDrawMode = true;
                if(result) *result = *param[0];
                return TJS_S_OK;
            }
        }

        // Step 2: Check if param is SLA.
        // Aligned to libkrkr2.so Player_DrawSLA (0x6D5658):
        // SLA owner is the AffineLayer in the display tree.
        // Render directly to it — TJS drawAffine does NOT call assignImages
        // for SLA path (SLA is not Layer/D3DAdaptor), so the owner IS the
        // final display target. renderToLayer now uses identity translation
        // (drawAffineMatrix tx/ty stripped in renderToLayer) because the
        // AffineLayer's position in the display tree already handles screen
        // placement.
        {
            auto *sla = ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                paramObj, false);
            if(!sla) {
                iTJSDispatch2 *resolved = tryResolveSeparateAdaptorOwner(*param[0]);
                if(resolved && resolved != paramObj) {
                    sla = reinterpret_cast<SeparateLayerAdaptor*>(1);
                }
            }
            if(sla) {
                auto *realSla = ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                    paramObj, false);
                iTJSDispatch2 *ownerLayer = realSla ? realSla->getOwner() : nullptr;
                if(!ownerLayer) {
                    ownerLayer = tryResolveSeparateAdaptorOwner(*param[0]);
                }
                if(ownerLayer) {
                    // Aligned to libkrkr2.so Player_ResolveSLATarget_guess
                    // (0x6D5948): create PrivateMotionGLL (Layer subclass,
                    // type=ltAlpha) as child of ownerLayer (AffineLayer).
                    // The ownerLayer may be larger than the visible window
                    // (e.g. 1920×1440 vs 1920×1080 for exHeight support).
                    // The compositor + DrawDevice handle clipping/scaling.
                    static tTJSVariant slaChild;
                    iTJSDispatch2 *renderTarget = nullptr;
                    if(slaChild.Type() == tvtObject && slaChild.AsObjectNoAddRef())
                        renderTarget = slaChild.AsObjectNoAddRef();
                    if(!renderTarget) {
                        iTJSDispatch2 *global = TVPGetScriptDispatch();
                        if(global) {
                            tTJSVariant lcVar, kagVar;
                            tTJSVariant ownerVar(ownerLayer, ownerLayer);
                            global->PropGet(0, TJS_W("Layer"), nullptr, &lcVar, global);
                            global->PropGet(0, TJS_W("kag"), nullptr, &kagVar, global);
                            if(lcVar.Type() == tvtObject) {
                                tTJSVariant *args[] = { &kagVar, &ownerVar };
                                iTJSDispatch2 *newL = nullptr;
                                lcVar.AsObjectNoAddRef()->CreateNew(0, nullptr, nullptr,
                                    &newL, 2, args, lcVar.AsObjectNoAddRef());
                                if(newL) {
                                    slaChild = tTJSVariant(newL, newL);
                                    newL->Release();
                                    renderTarget = slaChild.AsObjectNoAddRef();
                                    tTJSNI_BaseLayer *cn = nullptr;
                                    renderTarget->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                        tTJSNC_Layer::ClassID,
                                        reinterpret_cast<iTJSNativeInstance **>(&cn));
                                    if(cn) {
                                        cn->SetVisible(true);
                                        tTJSNI_BaseLayer *ownerN = nullptr;
                                        ownerLayer->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                            tTJSNC_Layer::ClassID,
                                            reinterpret_cast<iTJSNativeInstance **>(&ownerN));
                                        if(ownerN)
                                            cn->SetSize(ownerN->GetWidth(), ownerN->GetHeight());
                                        cn->SetType(static_cast<tTVPLayerType>(2)); // ltAlpha
                                    }
                                }
                            }
                            global->Release();
                        }
                    }
                    if(renderTarget)
                        nativeInstance->renderToLayer(renderTarget);
                    if(result) *result = tTJSVariant(ownerLayer, ownerLayer);
                    return TJS_S_OK;
                }
            }
        }

        // Step 3: param is a Layer (or resolves to one)
        tTJSNI_BaseLayer *layer = nullptr;
        if(tryGetLayerObject(*param[0], layer)) {
            nativeInstance->renderToLayer(paramObj);
            // Aligned to libkrkr2.so Player_updateLayerAfterDraw_guess (0x6CE7D8):
            // If flag+613 is set, call assignImages on internal render layer.
            // In our implementation, renderToLayer renders directly to target,
            // so the flag check is for future internal-buffer rendering modes.
            if(nativeInstance->_needsInternalAssignImages) {
                nativeInstance->_needsInternalAssignImages = false;
                try {
                    tTJSVariant targetVar(paramObj, paramObj);
                    tTJSVariant *args[] = { &targetVar };
                    paramObj->FuncCall(0, TJS_W("assignImages"),
                        nullptr, nullptr, 1, args, paramObj);
                } catch(...) {}
            }
            if(result) *result = *param[0];
            return TJS_S_OK;
        }

        // Step 4: param resolves to a Layer via property chain
        {
            iTJSDispatch2 *resolved = tryResolveSeparateAdaptorOwner(*param[0]);
            if(resolved) {
                nativeInstance->renderToLayer(resolved);
                if(nativeInstance->_needsInternalAssignImages) {
                    nativeInstance->_needsInternalAssignImages = false;
                    try {
                        tTJSVariant targetVar(resolved, resolved);
                        tTJSVariant *args[] = { &targetVar };
                        resolved->FuncCall(0, TJS_W("assignImages"),
                            nullptr, nullptr, 1, args, resolved);
                    } catch(...) {}
                }
                if(result) *result = tTJSVariant(resolved, resolved);
                return TJS_S_OK;
            }
        }

        // Fallback: no SLA/Layer match
        nativeInstance->draw();
        if(result) {
            *result = nativeInstance->_runtime->lastCanvas;
        }
        return TJS_S_OK;
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

        if(!self->_runtime->activeMotion && self->_project.Type() == tvtObject) {
            if(const auto snapshot = detail::lookupModuleSnapshot(self->_project)) {
                activateMotion(*self->_runtime, snapshot);
                self->syncVariableKeysFromActiveMotion();
            }
        }

        self->ensureMotionLoaded();
        if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
            detail::primeTimelineStates(self->_runtime->timelines,
                                        *self->_runtime->activeMotion);
        }

        if(!label.IsEmpty() && !self->_runtime->activeMotion) {
            self->setMotion(label);
            self->ensureMotionLoaded();
            if(self->_runtime->activeMotion && self->_runtime->timelines.empty()) {
                detail::primeTimelineStates(self->_runtime->timelines,
                                            *self->_runtime->activeMotion);
            }
        }

        if(!self->_runtime->activeMotion) {
            if(result) {
                *result = tTJSVariant(false);
            }
            return TJS_S_OK;
        }

        const auto playOne = [&](const std::string &timelineLabel) {
            auto &state = self->_runtime->timelines[timelineLabel];
            state.label = timelineLabel;
            state.flags = flags;
            state.blendRatio = 1.0;
            state.playing = true;
            state.currentTime = 0.0;
            // Ensure totalFrames is set (may be 0 if timeline wasn't primed)
            if(state.totalFrames <= 0.0 && self->_runtime->activeMotion) {
                auto it = self->_runtime->activeMotion->timelineTotalFrames.find(timelineLabel);
                if(it != self->_runtime->activeMotion->timelineTotalFrames.end()) {
                    state.totalFrames = it->second;
                }
            }
        };

        bool started = false;
        if(!label.IsEmpty()) {
            const auto key = detail::narrow(label);
            if(self->_runtime->timelines.find(key) != self->_runtime->timelines.end()) {
                self->_motionKey = label;
                playOne(key);
                started = true;
            } else if(self->_runtime->activeMotion) {
                self->_motionKey = label;
            }
        }

        if(!started) {
            const auto &primary = !self->_runtime->activeMotion->mainTimelineLabels.empty()
                ? self->_runtime->activeMotion->mainTimelineLabels
                : self->_runtime->activeMotion->diffTimelineLabels;
            for(const auto &timelineLabel : primary) {
                playOne(timelineLabel);
                started = true;
            }
        }

        self->_allplaying = std::any_of(
            self->_runtime->timelines.begin(), self->_runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });

        if(result) {
            *result = tTJSVariant(started);
        }
        return TJS_S_OK;
    }

    tjs_error Player::progressCompatMethod(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        self->ensureMotionLoaded();

        double delta = 0.0;
        if(numparams > 0 && param[0] && param[0]->Type() != tvtVoid) {
            delta = param[0]->AsReal();
        }
        // Clamp delta to sane range: TJS tick differences can overflow
        // when uint32 wraps (e.g. 4294967381 = 2^32 + 85)
        if(delta < 0 || delta > 60000) {
            delta = 0;
        }

        self->_runtime->pendingEvents.clear();
        self->frameProgress(delta * kMotionFramesPerMillisecond);

        // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
        // After stepping timelines, dispatch queued onAction/onSync events.
        if(!self->_runtime->pendingEvents.empty()) {
            for(const auto &ev : self->_runtime->pendingEvents) {
                try {
                    if(ev.type == 0) {
                        // onAction(param1, param2)
                        tTJSVariant p1(detail::widen(ev.param1));
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
            self->_runtime->pendingEvents.clear();
        }

        if(result) {
            *result = tTJSVariant(self->getProgressCompat());
        }
        return TJS_S_OK;
    }

    tjs_error Player::isPlayingCompat(tTJSVariant *result, tjs_int,
                                      tTJSVariant **, iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }

        const bool playing = std::any_of(
            self->_runtime->timelines.begin(), self->_runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
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
        // Binary simply sets playing=false (offset 1099), nothing else.
        for(auto &[_, state] : self->_runtime->timelines) {
            state.playing = false;
        }
        self->_allplaying = false;

        if(result) {
            *result = tTJSVariant(true);
        }
        return TJS_S_OK;
    }

    tTJSVariant Player::motionList() {
        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;
        for(const auto &[_, snapshot] : _runtime->motionsByKey) {
            if(snapshot && seen.insert(snapshot->path).second) {
                paths.push_back(snapshot->path);
            }
        }
        return detail::makeArray(detail::stringsToVariants(paths));
    }

    void Player::emoteEdit(tTJSVariant args) {
        _directEdit = true;
        _tags = args;
    }

} // namespace motion
