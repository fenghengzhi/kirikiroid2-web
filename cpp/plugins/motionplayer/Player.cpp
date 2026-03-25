//
// Created by LiDon on 2025/9/15.
// Minimal runtime implementation aligned to libkrkr2.so MMotionPlayer surface.
//

#include "Player.h"

#include <algorithm>
#include <unordered_set>

#include "RuntimeSupport.h"
#include "ResourceManager.h"
#include "StorageIntf.h"
#include "tjsArray.h"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("Player::" #name "() stub called")

namespace motion {

    namespace {

        std::string basenameWithoutExtension(const std::string &value) {
            const auto slash = value.find_last_of("/\\");
            const auto fileName =
                slash == std::string::npos ? value : value.substr(slash + 1);
            const auto dot = fileName.find_last_of('.');
            return dot == std::string::npos ? fileName : fileName.substr(0, dot);
        }

        std::shared_ptr<detail::MotionSnapshot>
        activateMotion(detail::PlayerRuntime &runtime,
                       const std::shared_ptr<detail::MotionSnapshot> &snapshot) {
            runtime.activeMotion = snapshot;
            runtime.timelines.clear();
            if(snapshot) {
                detail::primeTimelineStates(runtime.timelines, *snapshot);
            }
            return snapshot;
        }

        std::shared_ptr<detail::MotionSnapshot>
        resolveMotion(detail::PlayerRuntime &runtime, const ttstr &name) {
            const auto requestKey = detail::narrow(name);
            if(requestKey.empty()) {
                return nullptr;
            }

            if(const auto it = runtime.motionsByKey.find(requestKey);
               it != runtime.motionsByKey.end()) {
                return it->second;
            }

            ttstr resolved;
            if(!detail::resolveExistingPath(
                   detail::buildMotionLookupCandidates(name), resolved)) {
                return nullptr;
            }

            const auto resolvedKey = detail::narrow(resolved);
            if(const auto it = runtime.motionsByKey.find(resolvedKey);
               it != runtime.motionsByKey.end()) {
                runtime.motionsByKey.emplace(requestKey, it->second);
                return it->second;
            }

            const auto snapshot = detail::loadMotionSnapshot(
                resolved, ResourceManager::getEmotePSBDecryptSeed());
            if(!snapshot) {
                return nullptr;
            }

            runtime.motionsByKey.emplace(requestKey, snapshot);
            runtime.motionsByKey.emplace(resolvedKey, snapshot);
            return snapshot;
        }

        std::vector<ttstr> buildSourceCandidates(
            const detail::PlayerRuntime &runtime, const ttstr &name) {
            std::vector<ttstr> candidates;
            if(name.IsEmpty()) {
                return candidates;
            }

            candidates.push_back(name);
            const auto requestKey = detail::narrow(name);
            if(!runtime.activeMotion) {
                return candidates;
            }

            const auto baseDir = TVPExtractStoragePath(
                detail::widen(runtime.activeMotion->path));
            for(const auto &candidate : runtime.activeMotion->sourceCandidates) {
                if(candidate == requestKey ||
                   basenameWithoutExtension(candidate) == requestKey) {
                    candidates.emplace_back(detail::widen(candidate));
                    if(!baseDir.IsEmpty() &&
                       candidate.find('/') == std::string::npos &&
                       candidate.find('\\') == std::string::npos) {
                        candidates.emplace_back(baseDir + detail::widen(candidate));
                    }
                }
            }

            return candidates;
        }

        std::vector<tTJSVariant>
        timelineInfoVariants(const detail::PlayerRuntime &runtime) {
            std::vector<tTJSVariant> items;
            for(const auto &[label, state] : runtime.timelines) {
                if(!state.playing) {
                    continue;
                }

                items.push_back(detail::makeDictionary({
                    { "label", detail::widen(label) },
                    { "flags", static_cast<tjs_int>(state.flags) },
                    { "loop", state.loop },
                    { "playing", state.playing },
                    { "currentTime", state.currentTime },
                    { "totalFrames", state.totalFrames },
                }));
            }
            return items;
        }

    } // namespace

    Player::Player() : _runtime(detail::makePlayerRuntime()) {
        LOGGER->info("Motion.Player constructor called");
    }

    Player::~Player() = default;

    // --- Core methods ---
    void Player::initPhysics() { STUB_WARN(initPhysics); }
    void Player::unserialize(tTJSVariant) { STUB_WARN(unserialize); }
    void Player::setRotate(double rot) { STUB_WARN(setRotate); }
    void Player::setMirror(bool mirror) { setFlip(mirror); }
    void Player::setHairScale(double) { STUB_WARN(setHairScale); }
    void Player::setPartsScale(double) { STUB_WARN(setPartsScale); }
    void Player::setBustScale(double) { STUB_WARN(setBustScale); }

    void Player::setDrawAffineTranslateMatrix(tTJSVariant) {
        STUB_WARN(setDrawAffineTranslateMatrix);
    }

    tTJSVariant Player::getCameraOffset() { return _cameraPosition; }

    void Player::setCameraOffset(tTJSVariant offset) { _cameraPosition = offset; }

    void Player::modifyRoot(tTJSVariant data) { _project = data; }

    void Player::debugPrint() {
        LOGGER->info("motionKey={}, motions={}, sources={}, timelines={}",
                     _motionKey.AsStdString(), _runtime->motionsByKey.size(),
                     _runtime->sourcesByKey.size(), _runtime->timelines.size());
    }

    // --- Resource management ---
    void Player::unload(ttstr name) {
        const auto key = detail::narrow(name);
        if(key.empty()) {
            return;
        }

        for(auto it = _runtime->motionsByKey.begin();
            it != _runtime->motionsByKey.end();) {
            if(it->first == key || it->second->path == key) {
                if(_runtime->activeMotion == it->second) {
                    _runtime->activeMotion.reset();
                    _runtime->timelines.clear();
                }
                it = _runtime->motionsByKey.erase(it);
            } else {
                ++it;
            }
        }

        for(auto it = _runtime->sourcesByKey.begin();
            it != _runtime->sourcesByKey.end();) {
            if(it->first == key) {
                it = _runtime->sourcesByKey.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Player::unloadAll() {
        _runtime->motionsByKey.clear();
        _runtime->sourcesByKey.clear();
        _runtime->activeMotion.reset();
        _runtime->timelines.clear();
        _runtime->layerIdsByName.clear();
        _runtime->layerNamesById.clear();
        _runtime->lastCanvas.Clear();
        _runtime->lastViewParam.Clear();
        _variableKeys.Clear();
        _motionKey.Clear();
    }

    bool Player::isExistMotion(ttstr name) {
        return static_cast<bool>(resolveMotion(*_runtime, name));
    }

    tTJSVariant Player::findMotion(ttstr name) {
        const auto snapshot = resolveMotion(*_runtime, name);
        if(!snapshot) {
            return {};
        }

        activateMotion(*_runtime, snapshot);
        _motionKey = name;
        _variableKeys =
            detail::makeArray(detail::stringsToVariants(snapshot->variableLabels));
        return snapshot->moduleValue;
    }

    tjs_int Player::requireLayerId(ttstr name) {
        const auto key = detail::narrow(name);
        if(const auto it = _runtime->layerIdsByName.find(key);
           it != _runtime->layerIdsByName.end()) {
            return it->second;
        }

        const auto id = _runtime->nextLayerId++;
        _runtime->layerIdsByName[key] = id;
        _runtime->layerNamesById[id] = key;
        return id;
    }

    void Player::releaseLayerId(tjs_int id) {
        if(const auto it = _runtime->layerNamesById.find(id);
           it != _runtime->layerNamesById.end()) {
            _runtime->layerIdsByName.erase(it->second);
            _runtime->layerNamesById.erase(it);
        }
    }

    // --- Drawing/rendering ---
    void Player::setClearColor(tjs_int color) { _runtime->clearColor = color; }

    void Player::setResizable(bool v) { _runtime->resizable = v; }

    void Player::removeAllTextures() { _runtime->sourcesByKey.clear(); }

    void Player::removeAllBg() { _runtime->backgrounds.clear(); }

    void Player::removeAllCaption() { _runtime->captions.clear(); }

    void Player::registerBg(tTJSVariant bg) { _runtime->backgrounds.push_back(bg); }

    void Player::registerCaption(tTJSVariant caption) {
        _runtime->captions.push_back(caption);
    }

    void Player::unloadUnusedTextures() {}

    tjs_int Player::alphaOpAdd() { return ++_runtime->alphaOpCounter; }

    tTJSVariant Player::captureCanvas() {
        if(_runtime->lastCanvas.Type() == tvtVoid) {
            draw();
        }
        return _runtime->lastCanvas;
    }

    tTJSVariant Player::findSource(ttstr name) {
        loadSource(name);
        const auto key = detail::narrow(name);
        if(const auto it = _runtime->sourcesByKey.find(key);
           it != _runtime->sourcesByKey.end()) {
            return it->second;
        }
        return {};
    }

    void Player::loadSource(ttstr name) {
        const auto requestKey = detail::narrow(name);
        if(requestKey.empty() ||
           _runtime->sourcesByKey.find(requestKey) !=
               _runtime->sourcesByKey.end()) {
            return;
        }

        ttstr resolved;
        if(!detail::resolveExistingPath(buildSourceCandidates(*_runtime, name),
                                        resolved)) {
            return;
        }

        const auto resolvedKey = detail::narrow(resolved);
        if(const auto existing = _runtime->sourcesByKey.find(resolvedKey);
           existing != _runtime->sourcesByKey.end()) {
            _runtime->sourcesByKey.emplace(requestKey, existing->second);
            return;
        }

        const auto source = ResourceManager{}.load(resolved);
        if(source.Type() == tvtVoid) {
            return;
        }

        _runtime->sourcesByKey.emplace(requestKey, source);
        _runtime->sourcesByKey.emplace(resolvedKey, source);
    }

    void Player::clearCache() {
        _runtime->sourcesByKey.clear();
        _runtime->lastCanvas.Clear();
    }

    void Player::setSize(tjs_int w, tjs_int h) {
        _runtime->width = w;
        _runtime->height = h;
    }

    void Player::copyRect(tTJSVariant) {}

    void Player::adjustGamma(tTJSVariant) {}

    void Player::draw() {
        if(!_runtime->visible) {
            _runtime->lastCanvas = detail::makeDictionary({
                { "visible", false },
                { "tickCount", _tickCount },
            });
            return;
        }

        if(!_motionKey.IsEmpty() && !_runtime->activeMotion) {
            findMotion(_motionKey);
        }

        if(_runtime->width == 0 && _runtime->activeMotion) {
            _runtime->width = static_cast<tjs_int>(_runtime->activeMotion->width);
        }
        if(_runtime->height == 0 && _runtime->activeMotion) {
            _runtime->height = static_cast<tjs_int>(_runtime->activeMotion->height);
        }

        calcViewParam();

        const auto layerNames = getLayerNames();
        const auto sourceCount = static_cast<tjs_int>(_runtime->sourcesByKey.size());
        _processedMeshVerticesNum = _runtime->activeMotion
            ? static_cast<int>(_runtime->activeMotion->layerNames.size())
            : 0;

        std::vector<std::pair<std::string, tTJSVariant>> entries = {
            { "width", _runtime->width },
            { "height", _runtime->height },
            { "visible", _runtime->visible },
            { "opacity", _runtime->opacity },
            { "flip", _runtime->flip },
            { "slant", _runtime->slant },
            { "zoom", _runtime->zoom },
            { "clearColor", _runtime->clearColor },
            { "tickCount", _tickCount },
            { "frameTickCount", _frameTickCount },
            { "backgroundCount",
              static_cast<tjs_int>(_runtime->backgrounds.size()) },
            { "captionCount", static_cast<tjs_int>(_runtime->captions.size()) },
            { "sourceCount", sourceCount },
            { "layers", layerNames },
        };

        if(_runtime->activeMotion) {
            entries.emplace_back("motionPath",
                                 detail::widen(_runtime->activeMotion->path));
            entries.emplace_back(
                "layerCount",
                static_cast<tjs_int>(_runtime->activeMotion->layerNames.size()));
        }

        _runtime->lastCanvas = detail::makeDictionary(entries);
    }

    void Player::frameProgress(double dt) {
        _frameLastTime = dt;
        _frameLoopTime += dt;
        _loopTime += dt;
        _tickCount += dt;
        _frameTickCount += 1.0;
        detail::stepTimelines(_runtime->timelines, dt);

        _allplaying = std::any_of(
            _runtime->timelines.begin(), _runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
        _syncActive = _syncWaiting && _allplaying;
    }

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
        return detail::makeArray(
            detail::stringsToVariants(_runtime->activeMotion->layerNames));
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
        if(!_runtime->activeMotion) {
            return {};
        }

        const auto key = detail::narrow(name);
        if(const auto it = _runtime->activeMotion->layersByName.find(key);
           it != _runtime->activeMotion->layersByName.end()) {
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
        for(const auto &layerName : _runtime->activeMotion->layerNames) {
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
    bool Player::getTimelinePlaying(ttstr label) {
        const auto key = detail::narrow(label);
        if(const auto it = _runtime->timelines.find(key);
           it != _runtime->timelines.end()) {
            return it->second.playing;
        }
        return false;
    }

    tTJSVariant Player::getVariableRange(ttstr label) {
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
                    { "value", frame.value },
                }));
            }
            return detail::makeArray(frames);
        }
    }

    tTJSVariant Player::getMainTimelineLabelList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->mainTimelineLabels));
    }

    tTJSVariant Player::getDiffTimelineLabelList() {
        if(!_runtime->activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(detail::stringsToVariants(
            _runtime->activeMotion->diffTimelineLabels));
    }

    bool Player::getLoopTimeline(ttstr label) {
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

    tTJSVariant Player::getPlayingTimelineInfoList() {
        return detail::makeArray(timelineInfoVariants(*_runtime));
    }

    // --- Selector ---
    bool Player::isSelectorTarget(ttstr name) {
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto key = detail::narrow(name);
        return _runtime->activeMotion->layersByName.find(key) !=
                _runtime->activeMotion->layersByName.end() &&
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
            detail::stringsToVariants(_runtime->activeMotion->sourceCandidates));
    }

    bool Player::getD3DAvailable() { return _useD3D; }

    void Player::doAlphaMaskOperation() {}

    void Player::onFindMotion(ttstr name) { (void)findMotion(name); }

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
