// PlayerLayerQuery.cpp — viewport, layer query, hit-test, selector, misc
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "HitTestInternal.h"
#include "SourceCache.h"
#include "ncbind.hpp"

using namespace motion::internal;

namespace {
    bool hitTestMotionNodeShape(const motion::detail::MotionNode &node,
                                double x, double y) {
        motion::detail::HitData hit{};
        hit.type = node.shapeGeomType;
        for(size_t i = 0; i < std::size(node.shapeVertices) &&
                          i < hit.values.size();
            ++i) {
            hit.values[i] = node.shapeVertices[i];
        }
        return motion::detail::hitTestHitData(hit, x, y);
    }

    tTJSVariant buildLayerGetterVariant(motion::Player &player,
                                        const motion::detail::MotionNode &node) {
        using LayerGetterAdaptor = ncbInstanceAdaptor<motion::LayerGetter>;

        auto *getter = new motion::LayerGetter();
        getter->setType(node.nodeType);
        getter->setLabel(motion::detail::widen(node.layerName));
        getter->setVisible(node.accumulated.visible);
        getter->setBranchVisible(node.accumulated.active);
        getter->setLayerVisible(node.drawFlag);
        getter->setX(node.accumulated.posX);
        getter->setY(node.accumulated.posY);
        getter->setFlipX(node.accumulated.flipX);
        getter->setFlipY(node.accumulated.flipY);
        getter->setZoomX(node.accumulated.scaleX);
        getter->setZoomY(node.accumulated.scaleY);
        getter->setAngleRad(node.accumulated.angle);
        getter->setAngleDeg(
            node.accumulated.angle * 180.0 / 3.14159265358979323846);
        getter->setSlantX(node.accumulated.slantX);
        getter->setSlantY(node.accumulated.slantY);
        getter->setOriginX(node.originX);
        getter->setOriginY(node.originY);
        getter->setOpacity(node.accumulated.opacity);
        getter->setMtx(motion::detail::makeArray({
            tTJSVariant(node.accumulated.m11),
            tTJSVariant(node.accumulated.m12),
            tTJSVariant(node.accumulated.m21),
            tTJSVariant(node.accumulated.m22),
            tTJSVariant(node.accumulated.posX),
            tTJSVariant(node.accumulated.posY),
        }));
        getter->setVtx(motion::detail::makeArray({
            tTJSVariant(node.vertices[0]), tTJSVariant(node.vertices[1]),
            tTJSVariant(node.vertices[2]), tTJSVariant(node.vertices[3]),
            tTJSVariant(node.vertices[4]), tTJSVariant(node.vertices[5]),
            tTJSVariant(node.vertices[6]), tTJSVariant(node.vertices[7]),
        }));
        getter->setColor(motion::detail::makeArray({
            tTJSVariant(static_cast<tjs_int>(node.colorBytes[0])),
            tTJSVariant(static_cast<tjs_int>(node.colorBytes[1])),
            tTJSVariant(static_cast<tjs_int>(node.colorBytes[2])),
            tTJSVariant(static_cast<tjs_int>(node.colorBytes[3])),
        }));
        if(node.nodeType == 3) {
            getter->setMotion(node.childPlayerVar);
        } else if(node.nodeType == 4) {
            getter->setParticle(node.particleArrayVar);
        }

        if(auto *dispatch = LayerGetterAdaptor::CreateAdaptor(getter)) {
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }
        delete getter;
        return {};
    }

} // anonymous namespace

namespace motion {
    // --- Viewport/display ---
    void Player::setFlip(bool v) { _runtime->flip = v; }

    void Player::setOpacity(double v) { _runtime->opacity = v; }

    void Player::setVisible(bool v) { _runtime->visible = v; }

    void Player::setSlant(double v) { _runtime->slant = v; }

    void Player::setZoom(double v) { _runtime->zoom = v; }

    tTJSVariant Player::getLayerNames() {
        // Aligned to libkrkr2.so sub_6D1018 (getLayerNames NCB callback):
        // iterates Player+24 labelMap (std::map<ttstr,int>) and emits its keys.
        // That map is populated at 0x6B4CE4 during buildNodeTree_recursive with
        // operator[] — duplicates naturally collapse to one key per label.
        ensureMotionLoaded();
        if(!_runtime || !_runtime->activeMotion) {
            return detail::makeArray({});
        }
        std::vector<std::string> labels;
        labels.reserve(_runtime->nodeLabelMap.size());
        for(const auto &[label, _] : _runtime->nodeLabelMap) {
            labels.push_back(label);
        }
        return detail::makeArray(detail::stringsToVariants(labels));
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
            { "colorWeight", getColorWeight() },
        });
    }

    tTJSVariant Player::getLayerMotion(ttstr name) {
        // Aligned to libkrkr2.so sub_6D38F4 → sub_6B5AD8 (getLayerMotion):
        // queries Player+24 labelMap (last-wins) and returns the PSB dict of
        // the resolved node. For duplicate labels this yields the last layer
        // that wrote the key during buildNodeTree_recursive.
        ensureMotionLoaded();
        if(!_runtime) {
            return {};
        }

        const auto key = detail::narrow(name);
        const auto it = _runtime->nodeLabelMap.find(key);
        if(it == _runtime->nodeLabelMap.end()) {
            return {};
        }
        const auto nodeIndex = it->second;
        if(nodeIndex < 0 || nodeIndex >= static_cast<int>(_runtime->nodes.size())) {
            return {};
        }
        const auto &psb = _runtime->nodes[nodeIndex].psbNode;
        return psb ? psb->toTJSVal() : tTJSVariant{};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        ensureMotionLoaded();
        if(!_runtime) {
            return {};
        }
        const auto key = detail::narrow(name);
        const auto it = _runtime->nodeLabelMap.find(key);
        if(it == _runtime->nodeLabelMap.end()) {
            return {};
        }
        const auto nodeIndex = it->second;
        if(nodeIndex < 0 || nodeIndex >= static_cast<int>(_runtime->nodes.size())) {
            return {};
        }
        return buildLayerGetterVariant(*this, _runtime->nodes[nodeIndex]);
    }

    tTJSVariant Player::getLayerGetterList() {
        // Aligned to libkrkr2.so sub_6D4F88 (getLayerGetterList): walks the
        // flat node container (Player+200 deque) in nodeIndex order and emits
        // a getter per non-root node. Duplicates are NOT collapsed — every
        // node maps to its own getter, unlike getLayerNames.
        ensureMotionLoaded();
        if(!_runtime || !_runtime->activeMotion) {
            return detail::makeArray({});
        }

        std::vector<tTJSVariant> items;
        items.reserve(_runtime->nodes.size());
        for(size_t i = 1; i < _runtime->nodes.size(); ++i) {
            const auto &node = _runtime->nodes[i];
            auto getter = buildLayerGetterVariant(*this, node);
            if(getter.Type() != tvtVoid) {
                items.push_back(std::move(getter));
            }
        }
        return detail::makeArray(items);
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


    bool Player::hitTestLayer(ttstr name, double x, double y) {
        ensureMotionLoaded();
        if(!_runtime || !_runtime->activeMotion) {
            return false;
        }

        if(!_runtime->nodes.empty()) {
            updateLayers();
            calcBounds();
        }

        const auto key = detail::narrow(name);
        if(key.empty()) {
            return false;
        }

        auto findNodeRecursive =
            [&](auto &&self, Player *player) -> const detail::MotionNode * {
            if(!player || !player->_runtime) {
                return nullptr;
            }

            if(const auto it = player->_runtime->nodeLabelMap.find(key);
               it != player->_runtime->nodeLabelMap.end()) {
                const auto index = it->second;
                if(index >= 0 &&
                   index < static_cast<int>(player->_runtime->nodes.size())) {
                    return &player->_runtime->nodes[static_cast<size_t>(index)];
                }
            }

            for(auto &node : player->_runtime->nodes) {
                if(node.nodeType == 3) {
                    if(auto *child = node.getChildPlayer()) {
                        if(const auto *found = self(self, child)) {
                            return found;
                        }
                    }
                } else if(node.nodeType == 4) {
                    const int particleCount = node.getParticleCount();
                    for(int i = 0; i < particleCount; ++i) {
                        if(auto *child = node.getParticleChild(i)) {
                            if(const auto *found = self(self, child)) {
                                return found;
                            }
                        }
                    }
                }
            }

            return nullptr;
        };

        if(const auto *node = findNodeRecursive(findNodeRecursive, this)) {
            return hitTestMotionNodeShape(*node, x, y);
        }
        return false;
    }


    // --- Selector ---
    bool Player::isSelectorTarget(ttstr name) {
        // Aligned to libkrkr2.so sub_6823FC (EmotePlayer-level selector
        // target): checks membership in the "selectorControl" registry
        // parsed at PSB load time, NOT the layer list. Our snapshot already
        // populates selectorControls (RuntimeSupport.cpp:681) from the same
        // PSB "selectorControl" array. The previous implementation incorrectly
        // checked layer existence via layersByName, which conflated layer
        // tree membership with selector-target registration.
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto key = detail::narrow(name);
        const auto &selectors = _runtime->activeMotion->selectorControls;
        return selectors.find(key) != selectors.end() &&
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
