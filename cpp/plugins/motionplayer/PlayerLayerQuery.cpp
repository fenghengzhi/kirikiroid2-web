// PlayerLayerQuery.cpp — viewport, layer query, hit-test, selector, misc
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "HitTestInternal.h"
#include "SourceCache.h"
#include "ncbind.hpp"

using namespace motion::internal;

namespace {
    template<typename Shape>
    tTJSVariant makeShapeVariant(Shape *shape) {
        using ShapeAdaptor = ncbInstanceAdaptor<Shape>;
        if(auto *dispatch = ShapeAdaptor::CreateAdaptor(shape)) {
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }
        delete shape;
        return {};
    }

    tTJSVariant buildShapeVariantLike_0x691EE0(
        const motion::detail::MotionNode &node) {
        const double *v = node.shapeVertices;
        switch(node.shapeGeomType) {
            case 0: {
                auto *shape = new motion::Point();
                shape->x = v[0];
                shape->y = v[1];
                return makeShapeVariant(shape);
            }
            case 1: {
                auto *shape = new motion::Circle();
                shape->x = v[0];
                shape->y = v[1];
                shape->r = v[2];
                return makeShapeVariant(shape);
            }
            case 2: {
                auto *shape = new motion::Rect();
                shape->l = v[3];
                shape->t = v[4];
                shape->w = v[5] - v[3];
                shape->h = v[6] - v[4];
                return makeShapeVariant(shape);
            }
            case 3: {
                auto *shape = new motion::Quad();
                for(size_t i = 0; i < std::size(shape->verts); ++i) {
                    shape->verts[i] = v[7 + i];
                }
                return makeShapeVariant(shape);
            }
            default:
                return {};
        }
    }

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
        getter->setOriginX(node.source.originX);
        getter->setOriginY(node.source.originY);
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
        // LayerGetter_shape @ 0x69CB48 passes node+1664 to the four-way
        // shape factory sub_691EE0. Unknown types intentionally stay void.
        getter->setShape(buildShapeVariantLike_0x691EE0(node));
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
    tTJSVariant Quad::getP() const {
        std::vector<tTJSVariant> points;
        points.reserve(4);
        for(size_t i = 0; i < 4; ++i) {
            points.push_back(detail::makeDictionary({
                {"x", tTJSVariant(verts[i * 2])},
                {"y", tTJSVariant(verts[i * 2 + 1])},
            }));
        }
        return detail::makeArray(points);
    }

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
        // (B) Removed `_syncActive = false`: syncActive(+1093) is written only by
        // ctor(0x6CF11C) and setSyncActive(0x6D9698) in libkrkr2.so — this
        // function does not clear it.
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

    bool Player::contains(double x, double y) {
        // Player_contains @0x6D333C first scans the local node deque from index
        // 1 (the constructor-created root at index 0 is excluded). Only shape
        // nodes (nodeType == 1) participate in the direct hit test.
        for(size_t i = 1; i < _nodes.size(); ++i) {
            const auto &node = _nodes[i];
            if(node.nodeType == 1 && hitTestMotionNodeShape(node, x, y)) {
                return true;
            }
        }

        // The binary then calls Player_visitChildPlayerDispatches @0x6B601C.
        // Its callback recursively invokes Player_contains and stops at the
        // first hit. Preserve the visitor's node order and type-3/type-4 split.
        for(const auto &node : _nodes) {
            if(node.nodeType == 3) {
                if(Player *child = node.getChildPlayer();
                   child && child->contains(x, y)) {
                    return true;
                }
            } else if(node.nodeType == 4) {
                const int particleCount = node.getParticleCount();
                for(int i = 0; i < particleCount; ++i) {
                    if(Player *child = node.getParticleChild(i);
                       child && child->contains(x, y)) {
                        return true;
                    }
                }
            }
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

        // libkrkr2.so loc_6D3A4C (chunk owner sub_682520): build the same
        // per-call render-item vector as draw (sub_6C2334), stable-sort it, then
        // materialize a fresh TJS Array of command dictionaries.  The caller
        // compares this structure frame-to-frame to decide whether Layer.update
        // is necessary, so returning a static source-name list breaks the native
        // invalidation data flow.
        prepareRenderItems();

        const auto makeNumberArray = [](std::initializer_list<double> values) {
            std::vector<tTJSVariant> variants;
            variants.reserve(values.size());
            for(const double value : values) {
                variants.emplace_back(value);
            }
            return detail::makeArray(variants);
        };
        const auto makeIntegerArray = [](const auto &values) {
            std::vector<tTJSVariant> variants;
            variants.reserve(values.size());
            for(const auto value : values) {
                variants.emplace_back(static_cast<tjs_int64>(value));
            }
            return detail::makeArray(variants);
        };
        const auto makeRealArray = [](const auto &values) {
            std::vector<tTJSVariant> variants;
            variants.reserve(values.size());
            for(const auto value : values) {
                variants.emplace_back(static_cast<double>(value));
            }
            return detail::makeArray(variants);
        };

        const auto buildCommand = [&](const detail::PreparedRenderItem &item) {
            const auto coord = makeNumberArray({
                item.commandCoord[0], item.commandCoord[1],
                item.commandCoord[2],
            });
            const auto mtx = makeNumberArray({
                item.commandMatrix[0], item.commandMatrix[1],
                item.commandMatrix[2], item.commandMatrix[3],
            });
            const auto color = makeIntegerArray(item.packedColors);

            tTJSVariant clipRect;
            if(item.hasViewport && item.viewport[2] >= item.viewport[0] &&
               item.viewport[3] >= item.viewport[1]) {
                clipRect = detail::makeDictionary({
                    {"left", tTJSVariant(static_cast<double>(item.viewport[0]))},
                    {"top", tTJSVariant(static_cast<double>(item.viewport[1]))},
                    {"right", tTJSVariant(static_cast<double>(item.viewport[2]))},
                    {"bottom", tTJSVariant(static_cast<double>(item.viewport[3]))},
                    {"width", tTJSVariant(static_cast<double>(
                        item.viewport[2] - item.viewport[0]))},
                    {"height", tTJSVariant(static_cast<double>(
                        item.viewport[3] - item.viewport[1]))},
                });
            }

            std::vector<std::pair<std::string, tTJSVariant>> fields{
                {"key", item.contextVariant},
                {"id", tTJSVariant(item.layerId)},
                {"src", item.srcRef},
                {"coordinate", tTJSVariant(item.coordinateMode)},
                {"opacity", tTJSVariant(item.opacity)},
                {"blendMode", tTJSVariant(item.blendMode)},
                {"coord", coord},
                {"mtx", mtx},
                {"color", color},
                {"originX", tTJSVariant(item.originX)},
                {"originY", tTJSVariant(item.originY)},
                {"triPriority", tTJSVariant(item.objTriPriority)},
                {"clipRect", clipRect},
                {"meshTransform", tTJSVariant(item.meshType)},
            };
            if(item.meshType == 2) {
                fields.emplace_back("compositeMesh", detail::makeDictionary({
                    {"vtx", makeRealArray(
                        item.commandCompositeMeshPoints)},
                    {"divx", tTJSVariant(item.meshDivX)},
                    {"divy", tTJSVariant(item.meshDivY)},
                }));
            } else {
                int division = static_cast<int>(
                    getMeshDivisionRatio() *
                    static_cast<double>(item.commandPatchDivision));
                if(division >= 50) {
                    division = 50;
                }
                fields.emplace_back("bezierPatch", detail::makeDictionary({
                    {"patch", makeRealArray(
                        item.commandBezierPatchPoints)},
                    {"division", tTJSVariant(division)},
                }));
            }

            return detail::makeDictionary(fields);
        };

        // 0x6D3B84..0x6D45B0 constructs and stores item+284 for every item
        // before the visibility filter and stencil-chain pass.  Parent/child
        // stencil links therefore reference these exact dictionary objects,
        // including dictionaries belonging to items omitted from the result.
        std::vector<tTJSVariant> itemCommands;
        itemCommands.reserve(_preparedRenderItems.size());
        std::unordered_map<const detail::PreparedRenderItem *, size_t>
            commandIndex;
        commandIndex.reserve(_preparedRenderItems.size());
        for(size_t i = 0; i < _preparedRenderItems.size(); ++i) {
            const auto &item = _preparedRenderItems[i];
            commandIndex.emplace(&item, i);
            itemCommands.emplace_back(buildCommand(item));
        }

        std::vector<tTJSVariant> commands;
        commands.reserve(_preparedRenderItems.size());
        for(size_t i = 0; i < _preparedRenderItems.size(); ++i) {
            const auto &item = _preparedRenderItems[i];
            // 0x6D4810..0x6D4820: rawFlag17 || rawFlag16 || opacity==0
            // commands are retained as item+284 dictionaries but omitted from
            // the returned Array.
            if(item.skipFlag0 || item.rawFlag16 || item.opacity == 0) {
                continue;
            }

            tTJSVariant stencilChain;
            if(item.parentItem) {
                std::vector<tTJSVariant> links;
                for(const auto *parent = item.parentItem; parent;
                    parent = parent->parentItem) {
                    tTJSVariant mesh;
                    if((parent->stencilComposite & 4) != 0) {
                        std::vector<tTJSVariant> childMeshes;
                        childMeshes.reserve(parent->childItems.size());
                        for(const auto *child : parent->childItems) {
                            const auto childIt = commandIndex.find(child);
                            if(childIt != commandIndex.end()) {
                                childMeshes.push_back(
                                    itemCommands[childIt->second]);
                            }
                        }
                        mesh = detail::makeArray(childMeshes);
                    } else {
                        const auto parentIt = commandIndex.find(parent);
                        if(parentIt != commandIndex.end()) {
                            mesh = itemCommands[parentIt->second];
                        }
                    }
                    links.emplace_back(detail::makeDictionary({
                        {"type", tTJSVariant(parent->stencilComposite)},
                        {"mesh", mesh},
                    }));
                }
                stencilChain = detail::makeArray(links);
            }

            auto command = itemCommands[i];
            if(auto *dictionary = command.AsObjectNoAddRef()) {
                dictionary->PropSet(TJS_MEMBERENSURE, TJS_W("stencilChain"),
                                    nullptr, &stencilChain, dictionary);
            }
            commands.emplace_back(command);
        }
        return detail::makeArray(commands);
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
