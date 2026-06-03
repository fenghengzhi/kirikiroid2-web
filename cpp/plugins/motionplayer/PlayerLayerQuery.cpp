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
    // M20 P1 (cluster H): binary root setters write root node delta + dirty,
    // not Player-level viewport scalars. Port now writes BOTH root delta
    // (1:1 with binary) AND Player scalar (legacy debug compat). When the
    // _flip/_opacity/_slant/_zoom scalars are confirmed unused, remove them.
    void Player::setFlip(bool v) {
        _flip = v;
        if (!_nodes.empty()) {
            auto &root = _nodes[0];
            root.delta.flipX = v;
            root.delta.flipY = v;
            root.delta.dirty = true;
        }
    }

    void Player::setOpacity(double v) {
        _opacity = v;
        // Port DeltaState.opacity is int 0..255; convert from TJS 0..1 double.
        if (!_nodes.empty()) {
            auto &root = _nodes[0];
            int op = static_cast<int>(v * 255.0);
            if (op < 0) op = 0;
            if (op > 255) op = 255;
            root.delta.opacity = op;
            root.delta.dirty = true;
        }
    }

    void Player::setVisible(bool v) {
        _visible = v;
        if (!_nodes.empty()) {
            auto &root = _nodes[0];
            root.delta.visibleOverride = v;
            root.delta.dirty = true;
        }
    }

    void Player::setSlant(double v) {
        _slant = v;
        if (!_nodes.empty()) {
            auto &root = _nodes[0];
            root.delta.slantX = v;
            root.delta.slantY = v;
            root.delta.dirty = true;
        }
    }

    void Player::setZoom(double v) {
        _zoom = v;
        if (!_nodes.empty()) {
            auto &root = _nodes[0];
            root.delta.scaleX = v;
            root.delta.scaleY = v;
            root.delta.dirty = true;
        }
    }

    tTJSVariant Player::collectLayerNames(const ttstr *filter) {
        // Aligned to libkrkr2.so Player_getLayerNames @0x6D10E0 (NCB-registered
        // as "getLayerNames" @0x6D88C8; IDA had merged it into sub_6D1018 —
        // sub_6B601C is the SEPARATE processedMeshVerticesNum visitor, unrelated
        // to layer names). The binary creates a TJS Array and walks the Player+24
        // node-index std::map<ttstr,int> in-order (leftmost @+48 then
        // _Rb_tree_increment @sub_1485230), emitting each KEY (raw PSB "label")
        // as a string variant — never the value (node index), no nodeType /
        // visible gating, no type3/type4 descent.
        ensureMotionLoaded();
        if(!_activeMotion) {
            return detail::makeArray({});
        }
        // 0x6D1134: `if (*a2 == 0)` — void/absent args[0] emits every key;
        // otherwise apply the substring filter below.
        std::string needle;
        const bool hasFilter = filter != nullptr;
        if(hasFilter) {
            needle = detail::narrow(*filter);
        }
        std::vector<std::string> labels;
        labels.reserve(_nodeLabelMap.size());
        // std::map iteration is key-ascending = the binary's in-order RB-tree
        // walk; _nodeLabelMap keys are raw labels (M5-1).
        for(const auto &[ttLabel, _] : _nodeLabelMap) {
            // Player+24 map is ttstr-keyed (UTF-16 comparator sub_9B1ED0);
            // narrow back to std::string for the substring filter / output list.
            const std::string label = detail::narrow(ttLabel);
            if(hasFilter) {
                // 0x6D114C: push only when ttstr_indexOf(key, args[0]) >= 0,
                // i.e. the key CONTAINS the filter (case-sensitive). CORRECTED
                // 2026-06-03 (fresh decompile of 0x6D10E0 + ttstr_indexOf
                // 0x9B1FF8): an empty (but present) filter string makes
                // ttstr_indexOf return 0 (empty needle matches at index 0 of
                // every key, wcsstr-like), so a present empty-string arg emits
                // ALL keys — same as the void/absent arg branch. (The prior
                // comment claiming it emits NOTHING was contradicted by the
                // decompile.) std::string::find("") also returns 0 (!= npos),
                // so dropping the needle.empty() guard reproduces this.
                if(label.find(needle) == std::string::npos) {
                    continue;
                }
            }
            labels.push_back(label);
        }
        return detail::makeArray(detail::stringsToVariants(labels));
    }

    tTJSVariant Player::getLayerNames() {
        // No-filter entry point (the no-arg TJS call path + C++ callers such as
        // the unit tests). Equivalent to the binary's void-args[0] branch.
        return collectLayerNames(nullptr);
    }

    tjs_error Player::getLayerNamesCompat(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        // 0x6D1134 void gate: a present, non-void args[0] enables the substring
        // filter; absence/void emits all keys.
        ttstr filter;
        const bool hasFilter =
            numparams > 0 && param && param[0] && param[0]->Type() != tvtVoid;
        if(hasFilter) {
            filter = *param[0];
        }
        if(result) {
            *result = self->collectLayerNames(hasFilter ? &filter : nullptr);
        }
        return TJS_S_OK;
    }

    void Player::releaseSyncWait() {
        _syncWaiting = false;
        _syncActive = false;
    }

    void Player::calcViewParam() {
        _lastViewParam = detail::makeDictionary({
            { "flip", _flip },
            { "opacity", _opacity },
            { "visible", _visible },
            { "slant", _slant },
            { "zoom", _zoom },
            { "zFactor", _zFactor },
            { "colorWeight", getColorWeight() },
        });
    }

    tTJSVariant Player::getLayerMotion(ttstr name) {
        // Aligned to libkrkr2.so sub_6D38F4 → sub_6B5AD8 (getLayerMotion):
        // calls Player_nodePathMap_find @0x6F2228 on the Player+24 node-index map
        // (0x6B5B14) with the raw TJS `name` and returns the PSB dict of the
        // resolved node. The map is keyed by the raw PSB "label", so `name` is a
        // raw label matched verbatim (no path transform).
        ensureMotionLoaded();
        if(false) {
            return {};
        }

        // Player+24 map is ttstr-keyed (UTF-16 comparator sub_9B1ED0); look up
        // by the raw ttstr name verbatim (matches binary sub_6F2228 feed).
        const auto it = _nodeLabelMap.find(name);
        if(it == _nodeLabelMap.end()) {
            return {};
        }
        const auto nodeIndex = it->second;
        if(nodeIndex < 0 || nodeIndex >= static_cast<int>(_nodes.size())) {
            return {};
        }
        const auto &psb = _nodes[nodeIndex].psbNode;
        return psb ? psb->toTJSVal() : tTJSVariant{};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        ensureMotionLoaded();
        if(false) {
            return {};
        }
        // Player+24 map is ttstr-keyed; look up by the raw ttstr name verbatim.
        const auto it = _nodeLabelMap.find(name);
        if(it == _nodeLabelMap.end()) {
            return {};
        }
        const auto nodeIndex = it->second;
        if(nodeIndex < 0 || nodeIndex >= static_cast<int>(_nodes.size())) {
            return {};
        }
        return buildLayerGetterVariant(*this, _nodes[nodeIndex]);
    }

    tTJSVariant Player::getLayerGetterList() {
        // Aligned to libkrkr2.so sub_6D4F88 (getLayerGetterList): walks the
        // flat node container (Player+200 deque) in nodeIndex order and emits
        // a getter per non-root node. Duplicates are NOT collapsed — every
        // node maps to its own getter, unlike getLayerNames.
        ensureMotionLoaded();
        if(!_activeMotion) {
            return detail::makeArray({});
        }

        std::vector<tTJSVariant> items;
        items.reserve(_nodes.size());
        for(size_t i = 1; i < _nodes.size(); ++i) {
            const auto &node = _nodes[i];
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
        if(!_activeMotion) {
            return false;
        }

        if(!_nodes.empty()) {
            updateLayers();
            calcBounds();
        }

        const auto key = detail::narrow(name);
        if(key.empty()) {
            return false;
        }
        // Player+24 map is ttstr-keyed (UTF-16 comparator sub_9B1ED0); use the
        // raw ttstr name for the lookup.
        const ttstr ttKey = name;

        auto findNodeRecursive =
            [&](auto &&self, Player *player) -> const detail::MotionNode * {
            if(!player || !true) {
                return nullptr;
            }

            if(const auto it = player->_nodeLabelMap.find(ttKey);
               it != player->_nodeLabelMap.end()) {
                const auto index = it->second;
                if(index >= 0 &&
                   index < static_cast<int>(player->_nodes.size())) {
                    return &player->_nodes[static_cast<size_t>(index)];
                }
            }

            for(auto &node : player->_nodes) {
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
        if(!_activeMotion) {
            return false;
        }
        const auto key = detail::narrow(name);
        const auto &selectors = _activeMotion->selectorControls;
        return selectors.find(key) != selectors.end() &&
            _disabledSelectorTargets.find(key) ==
                _disabledSelectorTargets.end();
    }

    void Player::deactivateSelectorTarget(ttstr name) {
        _disabledSelectorTargets[detail::narrow(name)] = true;
    }

    // --- Misc ---
    tTJSVariant Player::getCommandList() {
        if(!_activeMotion) {
            return detail::makeArray({});
        }
        return detail::makeArray(
            detail::stringsToVariants(activeSourceCandidates()));
    }

    // getD3DAvailable / doAlphaMaskOperation relocated to Motion namespace-level
    // free functions (motion_getD3DAvailable / motion_doAlphaMaskOperation in
    // main.cpp). libkrkr2.so registers them on the Motion namespace object, not
    // on Motion.Player (motionplayer_ncb_register @0x6D9B08, 0x6da1f0/0x6da260).

    tTJSVariant Player::motionList() {
        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;
        for(const auto &[_, snapshot] : _motionsByKey) {
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
