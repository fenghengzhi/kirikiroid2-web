// PlayerLayerQuery.cpp — viewport, layer query, hit-test, selector, misc
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "HitTestInternal.h"
#include "SourceCache.h"
#include "ncbind.hpp"

#include <cstdint>
#include <cstring>

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

    tTJSVariant buildLayerGetterVariant(motion::detail::MotionNode &node) {
        using LayerGetterAdaptor = ncbInstanceAdaptor<motion::LayerGetter>;

        // Player_getLayerGetter @0x6D38F4 and getLayerGetterList @0x6D4F88
        // allocate only a raw node-pointer wrapper.  The adaptor owns this
        // wrapper, but neither wrapper nor adaptor owns the MotionNode.
        auto *getter = new motion::LayerGetter(&node);

        if(auto *dispatch = LayerGetterAdaptor::CreateAdaptor(getter)) {
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }
        // The Android failure path returns Void without deleting the freshly
        // allocated wrapper; preserve that observable allocation boundary.
        return {};
    }

} // anonymous namespace

namespace motion {
    int LayerGetter::getType() const { return _node->nodeType; }
    ttstr LayerGetter::getLabel() const { return _node->layerName; }
    ttstr LayerGetter::getSrc() const { return _node->activeSlot().srcValue; }
    bool LayerGetter::getVisible() const { return _node->accumulated.visible; }
    bool LayerGetter::getBranchVisible() const {
        return _node->accumulated.active;
    }
    bool LayerGetter::getLayerVisible() const {
        return _node->accumulated.visible && _node->accumulated.active;
    }
    double LayerGetter::getX() const { return _node->accumulated.posX; }
    double LayerGetter::getY() const { return _node->accumulated.posY; }
    double LayerGetter::getLeft() const { return _node->accumulated.posX; }
    double LayerGetter::getTop() const { return _node->accumulated.posY; }

    tTJSVariant LayerGetter::getCoord() const {
        auto result = detail::createTJSArrayWithItems_guess();
        result.items->emplace_back(_node->accumulated.posX);
        result.items->emplace_back(_node->accumulated.posY);
        result.items->emplace_back(_node->accumulated.posZ);
        return result.value;
    }

    bool LayerGetter::getFlipX() const { return _node->accumulated.flipX; }
    bool LayerGetter::getFlipY() const { return _node->accumulated.flipY; }
    double LayerGetter::getZoomX() const { return _node->accumulated.scaleX; }
    double LayerGetter::getZoomY() const { return _node->accumulated.scaleY; }
    double LayerGetter::getAngleDeg() const { return _node->accumulated.angle; }
    double LayerGetter::getAngleRad() const {
        return _node->accumulated.angle * 3.14159265358979323846 / 180.0;
    }
    double LayerGetter::getSlantX() const { return _node->accumulated.slantX; }
    double LayerGetter::getSlantY() const { return _node->accumulated.slantY; }
    double LayerGetter::getOriginX() const { return _node->activeSlot().ox; }
    double LayerGetter::getOriginY() const { return _node->activeSlot().oy; }
    int LayerGetter::getOpacity() const { return _node->accumulated.opacity; }

    tTJSVariant LayerGetter::getMtx() const {
        auto result = detail::createTJSArrayWithItems_guess();
        result.items->emplace_back(_node->accumulated.m11);
        result.items->emplace_back(_node->accumulated.m12);
        result.items->emplace_back(_node->accumulated.m21);
        result.items->emplace_back(_node->accumulated.m22);
        return result.value;
    }

    tTJSVariant LayerGetter::getVtx() const {
        auto result = detail::createTJSArrayWithItems_guess();
        for(size_t i = 0; i < 4; ++i) {
            ncbDictionaryAccessor point;
            point.SetValue(TJS_W("x"), _node->vertices[i * 2],
                           TJS_MEMBERENSURE, &detail::xMemberHint_guess);
            point.SetValue(TJS_W("y"), _node->vertices[i * 2 + 1],
                           TJS_MEMBERENSURE, &detail::yMemberHint_guess);
            result.items->emplace_back(point.GetDispatch(),
                                       point.GetDispatch());
        }
        return result.value;
    }

    tTJSVariant LayerGetter::getColor() const {
        auto result = detail::createTJSArrayWithItems_guess();
        for(size_t i = 0; i < 4; ++i) {
            std::uint32_t packed;
            std::memcpy(&packed, _node->colorBytes + i * sizeof(packed),
                        sizeof(packed));
            result.items->emplace_back(static_cast<tjs_int64>(packed));
        }
        return result.value;
    }

    tTJSVariant LayerGetter::getBezierPatch() const {
        if(_node->meshType != 1) {
            return {};
        }
        auto result = detail::createTJSArrayWithItems_guess();
        for(const auto &point : _node->meshControlPoints) {
            result.items->emplace_back(static_cast<double>(point.x));
            result.items->emplace_back(static_cast<double>(point.y));
        }
        return result.value;
    }

    tTJSVariant LayerGetter::getShape() const {
        return buildShapeVariantLike_0x691EE0(*_node);
    }

    tTJSVariant LayerGetter::getMotion() const {
        return _node->nodeType == 3 ? _node->childPlayerVar : tTJSVariant{};
    }

    tTJSVariant LayerGetter::getParticle() const {
        return _node->nodeType == 4 ? _node->particleArrayVar : tTJSVariant{};
    }

    tTJSVariant Quad::getP() const {
        // Quad_p @0x691CF4: each point dictionary and the outer Array are
        // materialised in one pass, using the same process-wide x/y hint
        // slots as LayerGetter_vtx @0x69C4B4.
        auto result = detail::createTJSArrayWithItems_guess();
        for(size_t i = 0; i < 4; ++i) {
            ncbDictionaryAccessor point;
            point.SetValue(TJS_W("x"), verts[i * 2], TJS_MEMBERENSURE,
                           &detail::xMemberHint_guess);
            point.SetValue(TJS_W("y"), verts[i * 2 + 1], TJS_MEMBERENSURE,
                           &detail::yMemberHint_guess);
            result.items->emplace_back(point.GetDispatch(),
                                       point.GetDispatch());
        }
        return result.value;
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
        auto result = detail::createTJSArrayWithItems_guess();
        // 0x6D1134 tests the ttstr handle itself. A null/empty handle emits
        // every key; otherwise the raw UTF-16 key is filtered in place.
        const bool hasFilter = filter != nullptr && !filter->IsEmpty();
        // std::map iteration is key-ascending = the binary's in-order RB-tree
        // walk; _nodeLabelMap keys are raw labels (M5-1).
        for(const auto &[ttLabel, _] : _nodeLabelMap) {
            if(hasFilter) {
                // 0x6D114C: push only when ttstr_indexOf(key, args[0]) >= 0,
                // i.e. the raw ttstr key contains the raw ttstr filter.
                if(ttLabel.IndexOf(*filter, 0) < 0) {
                    continue;
                }
            }
            // 0x6D1150..0x6D12A8 writes a type-2 Variant holding this exact
            // map-key string handle into tTJSArrayNI::Items.
            result.items->emplace_back(ttLabel);
        }
        return result.value;
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

    detail::MotionNode *Player::findNodeByRawLabelLike_0x6B5AD8(
        const ttstr &name, const bool recursive) {
        // 0x6B5B14..0x6B5B50: search this Player's raw-label map first.
        const auto it = _nodeLabelMap.find(name);
        if(it != _nodeLabelMap.end()) {
            return &_nodes[static_cast<size_t>(it->second)];
        }

        // 0x6B5B58 gate: callers choose whether descendants participate.
        if(!recursive) {
            return nullptr;
        }

        // Player_visitChildPlayerDispatches @0x6B601C walks the node deque in
        // order. Type 4 enumerates its TJS Array from index 0; type 3 visits
        // its single child dispatch. The callback @0x6F230C recursively calls
        // 0x6B5AD8 with the same key/flag and stops traversal on first hit.
        for(auto &node : _nodes) {
            if(node.nodeType == 4) {
                const int count = node.getParticleCount();
                for(int i = 0; i < count; ++i) {
                    if(auto *child = node.getParticleChild(i)) {
                        if(auto *found =
                               child->findNodeByRawLabelLike_0x6B5AD8(
                                   name, recursive)) {
                            return found;
                        }
                    }
                }
            } else if(node.nodeType == 3) {
                if(auto *child = node.getChildPlayer()) {
                    if(auto *found = child->findNodeByRawLabelLike_0x6B5AD8(
                           name, recursive)) {
                        return found;
                    }
                }
            }
        }
        return nullptr;
    }

    tTJSVariant Player::getLayerMotion(ttstr name) {
        // Player_getLayerMotion @0x6D3998: copy node+1912 from the recursively
        // resolved node. For non-motion nodes that field is the default void
        // variant; this entry performs no implicit load/update call.
        if(auto *node = findNodeByRawLabelLike_0x6B5AD8(name, true)) {
            return node->childPlayerVar;
        }
        return {};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        // Player_getLayerGetter @0x6D38F4.  The old IDB names for 0x6D38F4
        // and 0x6D3998 were swapped; the 0x6D69C8 literal registrations are
        // authoritative and the IDB has been corrected.
        auto *resolvedNode = findNodeByRawLabelLike_0x6B5AD8(name, true);
        if(!resolvedNode) {
            return {};
        }
        return buildLayerGetterVariant(*resolvedNode);
    }

    tTJSVariant Player::getLayerGetterList() {
        // Aligned to libkrkr2.so sub_6D4F88 (getLayerGetterList): walks the
        // flat node container (Player+200 deque) in nodeIndex order and emits
        // a getter per non-root node. Duplicates are NOT collapsed — every
        // node maps to its own getter, unlike getLayerNames.
        auto result = detail::createTJSArrayWithItems_guess();
        for(size_t i = 1; i < _nodes.size(); ++i) {
            // 0x6D505C..0x6D5104 appends even a Void result when the adaptor
            // could not be created; do not filter the element.
            result.items->emplace_back(buildLayerGetterVariant(_nodes[i]));
        }
        return result.value;
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

        if(const auto *node = findNodeByRawLabelLike_0x6B5AD8(ttKey, true)) {
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
    // --- Misc ---
    tTJSVariant Player::getCommandList() {
        // getCommandList @0x6D3A4C: sub_6C2334 builds persistent node-owned
        // items plus borrowed main/aux pointer lists. Only main is serialized.
        detail::PreparedRenderItemList mainList;
        detail::PreparedRenderItemList auxList;
        prepareRenderItems(mainList, auxList);

        const auto makeNumberArray = [](std::initializer_list<double> values) {
            auto array = detail::createTJSArrayWithItems_guess();
            for(const double value : values) {
                array.items->emplace_back(value);
            }
            return array.value;
        };
        const auto makeIntegerArray = [](const auto &values) {
            auto array = detail::createTJSArrayWithItems_guess();
            for(const auto value : values) {
                array.items->emplace_back(static_cast<tjs_int64>(value));
            }
            return array.value;
        };
        const auto makeMeshPointArray = [](const auto &values) {
            // sub_6C715C @0x6C715C, with the caller's offset={0,0}.
            auto array = detail::createTJSArrayWithItems_guess();
            for(const auto &point : values) {
                array.items->emplace_back(static_cast<double>(point.x));
                array.items->emplace_back(static_cast<double>(point.y));
            }
            return array.value;
        };

        const auto buildCommand = [&](detail::PreparedRenderItem &item) {
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
            if(item.viewport[2] >= item.viewport[0] &&
               item.viewport[3] >= item.viewport[1]) {
                ncbDictionaryAccessor clip;
                clip.SetValue(TJS_W("left"),
                              static_cast<tjs_real>(item.viewport[0]),
                              TJS_MEMBERENSURE, &detail::leftMemberHint_guess);
                clip.SetValue(TJS_W("top"),
                              static_cast<tjs_real>(item.viewport[1]),
                              TJS_MEMBERENSURE, &detail::topMemberHint_guess);
                clip.SetValue(TJS_W("right"),
                              static_cast<tjs_real>(item.viewport[2]),
                              TJS_MEMBERENSURE, &detail::rightMemberHint_guess);
                clip.SetValue(TJS_W("bottom"),
                              static_cast<tjs_real>(item.viewport[3]),
                              TJS_MEMBERENSURE, &detail::bottomMemberHint_guess);
                clip.SetValue(
                    TJS_W("width"),
                    static_cast<tjs_real>(item.viewport[2] - item.viewport[0]),
                    TJS_MEMBERENSURE, &detail::widthMemberHint_guess);
                clip.SetValue(
                    TJS_W("height"),
                    static_cast<tjs_real>(item.viewport[3] - item.viewport[1]),
                    TJS_MEMBERENSURE, &detail::heightMemberHint_guess);
                clipRect = tTJSVariant(clip.GetDispatch(), clip.GetDispatch());
            }

            ncbDictionaryAccessor command;
            command.SetValue(TJS_W("key"), item.commandKey,
                             TJS_MEMBERENSURE,
                             &detail::commandKeyMemberHint_guess);
            command.SetValue(TJS_W("id"), item.layerId1,
                             TJS_MEMBERENSURE,
                             &detail::commandIdMemberHint_guess);
            command.SetValue(TJS_W("src"), item.commandSrc,
                             TJS_MEMBERENSURE,
                             &detail::commandSrcMemberHint_guess);
            command.SetValue(TJS_W("coordinate"), item.coordinateMode,
                             TJS_MEMBERENSURE,
                             &detail::coordinateMemberHint_guess);
            command.SetValue(TJS_W("opacity"), item.opacity,
                             TJS_MEMBERENSURE,
                             &detail::opacityMemberHint_guess);
            command.SetValue(TJS_W("blendMode"), item.blendMode,
                             TJS_MEMBERENSURE,
                             &detail::blendModeMemberHint_guess);
            command.SetValue(TJS_W("coord"), coord, TJS_MEMBERENSURE,
                             &detail::coordMemberHint_guess);
            command.SetValue(TJS_W("mtx"), mtx, TJS_MEMBERENSURE,
                             &detail::mtxMemberHint_guess);
            command.SetValue(TJS_W("color"), color, TJS_MEMBERENSURE,
                             &detail::colorMemberHint_guess);
            command.SetValue(TJS_W("originX"), item.originX,
                             TJS_MEMBERENSURE,
                             &detail::originXMemberHint_guess);
            command.SetValue(TJS_W("originY"), item.originY,
                             TJS_MEMBERENSURE,
                             &detail::originYMemberHint_guess);
            command.SetValue(TJS_W("triPriority"), item.objTriPriority,
                             TJS_MEMBERENSURE,
                             &detail::triPriorityMemberHint_guess);
            // 0x6D40C8 writes clipRect even when its value is Void.
            command.SetValue(TJS_W("clipRect"), clipRect,
                             TJS_MEMBERENSURE,
                             &detail::clipRectMemberHint_guess);
            command.SetValue(TJS_W("meshTransform"), item.meshType,
                             TJS_MEMBERENSURE,
                             &detail::meshTransformMemberHint_guess);

            if(item.meshType <= 1) {
                ncbDictionaryAccessor bezier;
                const auto patch = makeMeshPointArray(
                    item.commandBezierPatchPoints);
                bezier.SetValue(TJS_W("patch"), patch, TJS_MEMBERENSURE,
                                &detail::patchMemberHint_guess);
                const double scaledDivision =
                    getMeshDivisionRatio() *
                    static_cast<double>(item.commandPatchDivision);
                const tjs_int64 division = scaledDivision >= 50.0
                    ? 50
                    : static_cast<tjs_int64>(scaledDivision);
                bezier.SetValue(TJS_W("division"), division,
                                TJS_MEMBERENSURE,
                                &detail::divisionMemberHint_guess);
                command.SetValue(TJS_W("bezierPatch"), bezier.GetDispatch(),
                                 TJS_MEMBERENSURE,
                                 &detail::bezierPatchMemberHint_guess);
            } else if(item.meshType == 2) {
                ncbDictionaryAccessor composite;
                const auto vtx = makeMeshPointArray(
                    item.commandCompositeMeshPoints);
                composite.SetValue(TJS_W("vtx"), vtx, TJS_MEMBERENSURE,
                                   &detail::vtxMemberHint_guess);
                composite.SetValue(TJS_W("divx"), item.meshDivX,
                                   TJS_MEMBERENSURE,
                                   &detail::divxMemberHint_guess);
                composite.SetValue(TJS_W("divy"), item.meshDivY,
                                   TJS_MEMBERENSURE,
                                   &detail::divyMemberHint_guess);
                command.SetValue(TJS_W("compositeMesh"),
                                 composite.GetDispatch(), TJS_MEMBERENSURE,
                                 &detail::compositeMeshMemberHint_guess);
            }

            // sub_A0FCC0 @0xA0FCC0 replaces item+284 in place, retaining the
            // same dispatch as both Object and objthis.
            item.commandVariant =
                tTJSVariant(command.GetDispatch(), command.GetDispatch());
        };

        // First pass: every main item gets item+284 before any output filter.
        for(auto *item : mainList) {
            if(item) {
                buildCommand(*item);
            }
        }

        auto result = detail::createTJSArrayWithItems_guess();
        for(auto *itemPtr : mainList) {
            if(!itemPtr) {
                continue;
            }
            auto &item = *itemPtr;
            // 0x6D4810..0x6D4820: rawFlag17 || rawFlag16 || opacity==0
            // command stays in item+284 but is omitted from
            // the returned Array.
            if(item.skipFlag0 || item.rawFlag16 || item.opacity == 0) {
                continue;
            }

            tTJSVariant stencilChain;
            if(item.parentItem) {
                auto chain = detail::createTJSArrayWithItems_guess();
                for(const auto *parent = item.parentItem; parent;
                    parent = parent->parentItem) {
                    ncbDictionaryAccessor link;
                    link.SetValue(TJS_W("type"), parent->stencilComposite,
                                  TJS_MEMBERENSURE,
                                  &detail::typeMemberHint_guess);
                    if((parent->stencilComposite & 4) != 0) {
                        auto childMeshes =
                            detail::createTJSArrayWithItems_guess();
                        for(const auto *child : parent->childItems) {
                            childMeshes.items->emplace_back(
                                child->commandVariant);
                        }
                        link.SetValue(TJS_W("mesh"), childMeshes.value,
                                      TJS_MEMBERENSURE,
                                      &detail::meshMemberHint_guess);
                    } else {
                        link.SetValue(TJS_W("mesh"), parent->commandVariant,
                                      TJS_MEMBERENSURE,
                                      &detail::meshMemberHint_guess);
                    }
                    chain.items->emplace_back(link.GetDispatch(),
                                              link.GetDispatch());
                }
                stencilChain = chain.value;
            }

            ncbPropAccessor command(item.commandVariant);
            command.SetValue(TJS_W("stencilChain"), stencilChain,
                             TJS_MEMBERENSURE,
                             &detail::stencilChainMemberHint_guess);
            result.items->emplace_back(item.commandVariant);
        }
        return result.value;
    }

    // getD3DAvailable / doAlphaMaskOperation relocated to Motion namespace-level
    // free functions (motion_getD3DAvailable / motion_doAlphaMaskOperation in
    // main.cpp). libkrkr2.so registers them on the Motion namespace object, not
    // on Motion.Player (motionplayer_ncb_register @0x6D9B08, 0x6da1f0/0x6da260).

    void Player::emoteEdit(tTJSVariant args) {
        _directEdit = true;
        _tags = args;
    }

} // namespace motion
