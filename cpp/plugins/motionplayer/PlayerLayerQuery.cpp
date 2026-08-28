// PlayerLayerQuery.cpp — viewport, layer query, hit-test, selector, misc
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "HitTestInternal.h"
#include "SourceCache.h"
#include "ncbind.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>

using namespace motion::internal;

namespace motion::internal {

    std::uint32_t calcViewMeshDivision_guess(
        double ratio, std::uint32_t meshDivision) {
        const double scaledDivision =
            ratio * static_cast<double>(meshDivision);
        std::uint32_t converted;
        constexpr double unsignedUpper = 0x1p32;

        if(std::isnan(scaledDivision) || scaledDivision <= 0.0) {
            converted = 0;
        } else if(scaledDivision >= unsignedUpper) {
            converted = std::numeric_limits<std::uint32_t>::max();
        } else {
            converted = static_cast<std::uint32_t>(scaledDivision);
        }
        return converted >= 50u ? 50u : converted;
    }

    tjs_int64 serializeBezierPatchDivision_guess(
        double scaledDivision) {
        tjs_int64 converted;
        constexpr double signedUpper = 0x1p63;
        constexpr double signedLower = -0x1p63;

        if(std::isnan(scaledDivision)) {
            converted = 0;
        } else if(scaledDivision >= signedUpper) {
            converted = std::numeric_limits<tjs_int64>::max();
        } else if(scaledDivision <= signedLower) {
            converted = std::numeric_limits<tjs_int64>::min();
        } else {
            converted = static_cast<tjs_int64>(scaledDivision);
        }

        // The native compare selects the converted value only for an ordered
        // product below 50.  Thus both >= 50 and unordered NaN publish 50.
        return scaledDivision < 50.0 ? converted : 50;
    }

}

namespace {
    template<typename Shape>
    tTJSVariant makeShapeVariant(Shape *shape) {
        using ShapeAdaptor = ncbInstanceAdaptor<Shape>;
        // The caller has already allocated the complete shape copy.
        // CreateAdaptor constructs the script instance with one Void sentinel
        // argument so its script constructor does not allocate a second native
        // shape.  A compatible non-sticky adaptor takes ownership on success;
        // the Variant retains the returned dispatch and the local Release
        // balances CreateNew's reference.  The reference implementation does
        // not reclaim shape when ClassInfo is absent, CreateNew fails or
        // throws, or the returned object has no compatible adaptor.  The last
        // case may still return the newly created script object while leaking
        // the unattached native copy.
        if(auto *dispatch = ShapeAdaptor::CreateAdaptor(shape)) {
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }
        return {};
    }

    tTJSVariant buildShapeVariant_guess(
        const motion::detail::MotionNode &node) {
        const auto &shape = node.shapeGeometry;
        switch(shape.type) {
            case 0:
                return makeShapeVariant(new motion::Point(shape));
            case 1:
                return makeShapeVariant(new motion::Circle(shape));
            case 2:
                return makeShapeVariant(new motion::Rect(shape));
            case 3:
                return makeShapeVariant(new motion::Quad(shape));
            default:
                return {};
        }
    }

    bool hitTestMotionNodeShape(const motion::detail::MotionNode &node,
                                double x, double y) {
        return motion::detail::hitTestHitData(node.shapeGeometry, x, y);
    }

    tTJSVariant buildLayerGetterVariant(motion::detail::MotionNode &node) {
        using LayerGetterAdaptor = ncbInstanceAdaptor<motion::LayerGetter>;

        // All four references allocate only a raw node-pointer facade.  A
        // compatible non-sticky adaptor deletes that facade after successful
        // attachment, but neither layer owns or retains the MotionNode.
        auto *getter = new motion::LayerGetter(&node);

        // CreateAdaptor constructs the script shell with one Void argument,
        // balances the shell dispatch on a normal return and looks up adaptor
        // metadata through LayerGetter's process-static ClassInfo ID.  The
        // returned Variant retains the dispatch before this local Release.
        if(auto *dispatch = LayerGetterAdaptor::CreateAdaptor(getter)) {
            tTJSVariant result(dispatch, dispatch);
            dispatch->Release();
            return result;
        }
        // Missing ClassInfo, failed/null CreateNew and thrown CreateNew do not
        // reclaim the facade.  Incompatible metadata also leaks it; with the
        // default error=false boundary CreateAdaptor can still return the
        // newly created script object, which the success branch above keeps.
        // A genuinely null return therefore becomes Void without cleanup.
        return {};
    }

    tTJSVariant makeCalcRealArray_guess(
        std::initializer_list<tjs_real> values) {
        auto result = motion::detail::createTJSArrayWithItems_guess();
        for(const tjs_real value : values) {
            result.items->emplace_back(value);
        }
        return result.value;
    }

    tTJSVariant makeCalcMeshPointArray_guess(
        const std::vector<motion::detail::MeshPoint> &points) {
        auto result = motion::detail::createTJSArrayWithItems_guess();
        for(const auto &point : points) {
            result.items->emplace_back(static_cast<tjs_real>(point.x));
            result.items->emplace_back(static_cast<tjs_real>(point.y));
        }
        return result.value;
    }

    void writeCalcRealArray_guess(const tTJSVariant &array,
                                  std::initializer_list<tjs_real> values) {
        ncbPropAccessor output(array);
        tjs_int index = 0;
        for(const tjs_real value : values) {
            (void)output.SetValue(index++, value, TJS_MEMBERENSURE);
        }
    }

    void writeCalcColorArray_guess(
        const tTJSVariant &array,
        const std::uint8_t (&colorBytes)[16]) {
        ncbPropAccessor output(array);
        for(tjs_int index = 0; index < 4; ++index) {
            std::uint32_t packed = 0;
            std::memcpy(&packed,
                        colorBytes + index * static_cast<tjs_int>(sizeof(packed)),
                        sizeof(packed));
            (void)output.SetValue(
                index, static_cast<tjs_int64>(packed), TJS_MEMBERENSURE);
        }
    }

} // anonymous namespace

namespace motion {
    int LayerGetter::getType() const { return _node->nodeType; }
    ttstr LayerGetter::getLabel() const { return _node->layerName; }
    ttstr LayerGetter::getSrc() const { return _node->activeSlot().srcValue; }
    bool LayerGetter::getVisible() const {
        return _node->layerGetterVisible_guess;
    }
    bool LayerGetter::getBranchVisible() const {
        return _node->layerGetterBranchVisible_guess;
    }
    bool LayerGetter::getLayerVisible() const {
        return _node->layerGetterVisible_guess &&
               _node->layerGetterBranchVisible_guess;
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
        // All four references preserve this operation order: multiply by the
        // exact binary64 pi literal, double that rounded product, then divide
        // by 360. The intermediate doubling can overflow even when the
        // algebraically equivalent product/180 result would remain finite.
        const double angleTimesPi =
            _node->accumulated.angle * 3.14159265358979323846;
        return (angleTimesPi + angleTimesPi) / 360.0;
    }
    double LayerGetter::getSlantX() const { return _node->accumulated.slantX; }
    double LayerGetter::getSlantY() const { return _node->accumulated.slantY; }
    double LayerGetter::getOriginX() const { return _node->activeSlot().ox; }
    double LayerGetter::getOriginY() const { return _node->activeSlot().oy; }
    int LayerGetter::getOpacity() const { return _node->accumulated.opacity; }

    tTJSVariant LayerGetter::getMtx() const {
        auto result = detail::createTJSArrayWithItems_guess();
        result.items->emplace_back(_node->matrix.m11);
        result.items->emplace_back(_node->matrix.m12);
        result.items->emplace_back(_node->matrix.m21);
        result.items->emplace_back(_node->matrix.m22);
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
        return buildShapeVariant_guess(*_node);
    }

    tTJSVariant LayerGetter::getMotion() const {
        return _node->nodeType == 3 ? _node->childPlayerVar : tTJSVariant{};
    }

    tTJSVariant LayerGetter::getParticle() const {
        return _node->nodeType == 4 ? _node->particleArrayVar : tTJSVariant{};
    }

    tTJSVariant Quad::getP() const {
        // Each point dictionary and the outer Array are materialised in one
        // pass, using the same process-wide x/y hint slots as LayerGetter.vtx.
        auto result = detail::createTJSArrayWithItems_guess();
        for(size_t i = 0; i < 4; ++i) {
            ncbDictionaryAccessor point;
            point.SetValue(TJS_W("x"), values[7 + i * 2], TJS_MEMBERENSURE,
                           &detail::xMemberHint_guess);
            point.SetValue(TJS_W("y"), values[8 + i * 2], TJS_MEMBERENSURE,
                           &detail::yMemberHint_guess);
            result.items->emplace_back(point.GetDispatch(),
                                       point.GetDispatch());
        }
        return result.value;
    }

    // --- Viewport/display ---
    bool Player::getFlipX() const {
        return _nodes[0].delta.flipX;
    }

    void Player::setFlipX(bool v) {
        auto &root = _nodes[0];
        if(root.delta.flipX != v) {
            root.delta.flipX = v;
            root.delta.dirty = true;
        }
    }

    bool Player::getFlipY() const {
        return _nodes[0].delta.flipY;
    }

    void Player::setFlipY(bool v) {
        auto &root = _nodes[0];
        if(root.delta.flipY != v) {
            root.delta.flipY = v;
            root.delta.dirty = true;
        }
    }

    void Player::setFlip(bool x, bool y) {
        auto &root = _nodes[0];
        if(root.delta.flipX != x || root.delta.flipY != y) {
            root.delta.flipX = x;
            root.delta.flipY = y;
            root.delta.dirty = true;
        }
    }

    int Player::getOpacity() const {
        return _nodes[0].delta.opacity;
    }

    void Player::setOpacity(int v) {
        auto &root = _nodes[0];
        if(root.delta.opacity != v) {
            root.delta.dirty = true;
            root.delta.opacity = v;
        }
    }

    bool Player::getVisible() const {
        return _nodes[0].delta.visibleOverride;
    }

    void Player::setVisible(bool v) {
        auto &root = _nodes[0];
        if(root.delta.visibleOverride != v) {
            root.delta.dirty = true;
            root.delta.visibleOverride = v;
        }
    }

    double Player::getSlantX() const {
        return _nodes[0].delta.slantX;
    }

    void Player::setSlantX(double x) {
        auto &root = _nodes[0];
        if(root.delta.slantX != x) {
            root.delta.dirty = true;
            root.delta.slantX = x;
        }
    }

    double Player::getSlantY() const {
        return _nodes[0].delta.slantY;
    }

    void Player::setSlantY(double y) {
        auto &root = _nodes[0];
        if(root.delta.slantY != y) {
            root.delta.dirty = true;
            root.delta.slantY = y;
        }
    }

    void Player::setSlant(double x, double y) {
        auto &root = _nodes[0];
        if(root.delta.slantX != x || root.delta.slantY != y) {
            root.delta.dirty = true;
            root.delta.slantX = x;
            root.delta.slantY = y;
        }
    }

    double Player::getZoomX() const {
        return _nodes[0].delta.scaleX;
    }

    void Player::setZoomX(double x) {
        auto &root = _nodes[0];
        if(root.delta.scaleX != x) {
            root.delta.dirty = true;
            root.delta.scaleX = x;
        }
    }

    double Player::getZoomY() const {
        return _nodes[0].delta.scaleY;
    }

    void Player::setZoomY(double y) {
        auto &root = _nodes[0];
        if(root.delta.scaleY != y) {
            root.delta.dirty = true;
            root.delta.scaleY = y;
        }
    }

    void Player::setZoom(double x, double y) {
        auto &root = _nodes[0];
        if(root.delta.scaleX != x || root.delta.scaleY != y) {
            root.delta.dirty = true;
            root.delta.scaleX = x;
            root.delta.scaleY = y;
        }
    }

    tTJSVariant Player::getLayerNames(ttstr filter) {
        // All four references create a fresh TJS Array and walk the raw-label
        // node-index std::map<ttstr, int> in order. Only each key is appended;
        // mapped node indices, node type, visibility and child-player recursion
        // do not participate in this query.
        auto result = detail::createTJSArrayWithItems_guess();
        const bool hasFilter = !filter.IsEmpty();
        for(const auto &[ttLabel, _] : _nodeLabelMap) {
            if(hasFilter) {
                if(ttLabel.IndexOf(filter, 0) < 0) {
                    continue;
                }
            }
            result.items->emplace_back(ttLabel);
        }
        return result.value;
    }

    void Player::releaseSyncWait() {
        _syncWaiting = false;
        // All four references leave syncActive unchanged. Its only writers are
        // construction from the class default and the public setter.
    }

    void Player::calcViewParam(double frame, tTJSVariant viewParams) {
        if(frame < 0.0) {
            frame = 0.0;
        }
        _frameTickCount = frame;
        if(frame > _cachedTotalFrames) {
            frame = _cachedTotalFrames;
        }
        _clampedEvalTime = frame;
        _queuing = true;
        _firstFrame = true;
        frameProgress(0.0);
        updateLayers();

        // The native body retains only the outer viewParams Object dispatch
        // for the complete traversal. Every numeric lookup uses that same
        // retained receiver even if script re-entry clears the caller's owner.
        ncbPropAccessor viewParamList(viewParams);
        for(std::size_t nodeIndex = 1; nodeIndex < _nodes.size(); ++nodeIndex) {
            auto &node = _nodes[nodeIndex];
            const tTJSVariant outputValue = detail::motionPropGetByNum(
                viewParamList.GetDispatch(),
                static_cast<tjs_int>(nodeIndex - 1));
            ncbPropAccessor output(outputValue);

            const bool exportable =
                (node.nodeType == 0 || node.nodeType == 6 ||
                 (node.nodeType == 3 && _preview)) &&
                node.accumulated.active && node.accumulated.visible;
            (void)output.SetValue(
                TJS_W("visible"), exportable, TJS_MEMBERENSURE,
                &detail::visibleMemberHint_guess);
            if(!exportable) {
                continue;
            }

            const auto &slot = node.activeSlot();
            (void)output.SetValue(
                TJS_W("src"), slot.srcValue, TJS_MEMBERENSURE,
                &detail::srcMemberHint_guess);
            (void)output.SetValue(
                TJS_W("blendMode"), slot.blendMode, TJS_MEMBERENSURE,
                &detail::blendModeMemberHint_guess);
            (void)output.SetValue(
                TJS_W("originX"), slot.ox, TJS_MEMBERENSURE,
                &detail::originXMemberHint_guess);
            (void)output.SetValue(
                TJS_W("originY"), slot.oy, TJS_MEMBERENSURE,
                &detail::originYMemberHint_guess);
            (void)output.SetValue(
                TJS_W("opacity"), node.accumulated.opacity,
                TJS_MEMBERENSURE, &detail::opacityMemberHint_guess);

            tTJSVariant meshBlendPoints;
            if(node.meshType == 1 && !node.meshControlPoints.empty()) {
                meshBlendPoints =
                    makeCalcMeshPointArray_guess(node.meshControlPoints);
            }
            (void)output.SetValue(
                TJS_W("mbp"), meshBlendPoints, TJS_MEMBERENSURE,
                &detail::calcMbpMemberHint_guess);

            auto compositeMesh = detail::createTJSArrayWithItems_guess();
            if(node.meshAncestor != nullptr) {
                ncbDictionaryAccessor separator;
                (void)separator.SetValue(
                    TJS_W("type"), ttstr(TJS_W("mesh.inherit.separator")),
                    TJS_MEMBERENSURE, &detail::typeMemberHint_guess);
                const tTJSVariant separatorValue(
                    separator.GetDispatch(), separator.GetDispatch());
                if(node.meshInheritanceSeparator_guess) {
                    compositeMesh.items->emplace_back(separatorValue);
                }

                for(const detail::MotionNode *mesh = node.meshAncestor;
                    mesh != nullptr; mesh = mesh->meshAncestor) {
                    if(mesh->meshInheritanceSeparator_guess) {
                        compositeMesh.items->emplace_back(separatorValue);
                    }
                    if(!mesh->hasMeshData) {
                        continue;
                    }

                    const float negOffsetX = -mesh->meshInvOffX;
                    const float negOffsetY = -mesh->meshInvOffY;
                    const float invOffsetX = static_cast<float>(
                        mesh->meshInvM11 * negOffsetX +
                        mesh->meshInvM12 * negOffsetY);
                    const float invOffsetY = static_cast<float>(
                        mesh->meshInvM21 * negOffsetX +
                        mesh->meshInvM22 * negOffsetY);

                    const auto invOffset = makeCalcRealArray_guess({
                        static_cast<tjs_real>(invOffsetX),
                        static_cast<tjs_real>(invOffsetY),
                    });
                    const auto invMatrix = makeCalcRealArray_guess({
                        mesh->meshInvM11, mesh->meshInvM12,
                        mesh->meshInvM21, mesh->meshInvM22,
                    });
                    const auto patch = makeCalcMeshPointArray_guess(
                        mesh->transformedMeshControlPoints);

                    const auto rawDivision = calcViewMeshDivision_guess(
                        getMeshDivisionRatio(),
                        static_cast<std::uint32_t>(mesh->meshDivision));
                    const tjs_int64 division =
                        static_cast<tjs_int64>(rawDivision);

                    ncbDictionaryAccessor meshParam;
                    (void)meshParam.SetValue(
                        TJS_W("type"), static_cast<tjs_int>(1),
                        TJS_MEMBERENSURE, &detail::typeMemberHint_guess);
                    (void)meshParam.SetValue(
                        TJS_W("division"), division, TJS_MEMBERENSURE,
                        &detail::divisionMemberHint_guess);
                    (void)meshParam.SetValue(
                        TJS_W("invOffset"), invOffset, TJS_MEMBERENSURE,
                        &detail::calcInvOffsetMemberHint_guess);
                    (void)meshParam.SetValue(
                        TJS_W("invMatrix"), invMatrix, TJS_MEMBERENSURE,
                        &detail::calcInvMatrixMemberHint_guess);
                    (void)meshParam.SetValue(
                        TJS_W("patch"), patch, TJS_MEMBERENSURE,
                        &detail::patchMemberHint_guess);
                    compositeMesh.items->emplace_back(
                        meshParam.GetDispatch(), meshParam.GetDispatch());
                }
            }
            (void)output.SetValue(
                TJS_W("cmesh"), compositeMesh.value, TJS_MEMBERENSURE,
                &detail::calcCmeshMemberHint_guess);

            tTJSVariant clipValue;
            if(node.clipAABB != nullptr) {
                const float *clipBounds = node.clipAABB;
                const float width = clipBounds[2] - clipBounds[0];
                const float height = clipBounds[3] - clipBounds[1];
                ncbDictionaryAccessor clip;
                (void)clip.SetValue(
                    TJS_W("left"), static_cast<tjs_real>(clipBounds[0]),
                    TJS_MEMBERENSURE, &detail::leftMemberHint_guess);
                (void)clip.SetValue(
                    TJS_W("top"), static_cast<tjs_real>(clipBounds[1]),
                    TJS_MEMBERENSURE, &detail::topMemberHint_guess);
                (void)clip.SetValue(
                    TJS_W("right"), static_cast<tjs_real>(clipBounds[2]),
                    TJS_MEMBERENSURE, &detail::rightMemberHint_guess);
                (void)clip.SetValue(
                    TJS_W("bottom"), static_cast<tjs_real>(clipBounds[3]),
                    TJS_MEMBERENSURE, &detail::bottomMemberHint_guess);
                (void)clip.SetValue(
                    TJS_W("width"), static_cast<tjs_real>(width),
                    TJS_MEMBERENSURE, &detail::widthMemberHint_guess);
                (void)clip.SetValue(
                    TJS_W("height"), static_cast<tjs_real>(height),
                    TJS_MEMBERENSURE, &detail::heightMemberHint_guess);
                clipValue = tTJSVariant(
                    clip.GetDispatch(), clip.GetDispatch());
            }
            (void)output.SetValue(
                TJS_W("clip"), clipValue, TJS_MEMBERENSURE,
                &detail::clipMemberHint_guess);

            const tTJSVariant coord = detail::motionPropGet(
                outputValue, TJS_W("coord"), 0,
                &detail::coordMemberHint_guess);
            writeCalcRealArray_guess(coord, {
                node.accumulated.posX,
                node.accumulated.posY,
                node.accumulated.posZ,
            });

            const tTJSVariant color = detail::motionPropGet(
                outputValue, TJS_W("color"), 0,
                &detail::colorMemberHint_guess);
            writeCalcColorArray_guess(color, node.colorBytes);

            const tTJSVariant matrix = detail::motionPropGet(
                outputValue, TJS_W("matrix"), 0,
                &detail::calcMatrixMemberHint_guess);
            writeCalcRealArray_guess(matrix, {
                node.matrix.m11,
                node.matrix.m12,
                node.matrix.m21,
                node.matrix.m22,
            });
        }
    }

    void Player::setZFactor(const double value) {
        if(_zFactor == value) {
            return;
        }

        _zFactor = value;
        _nodes.front().delta.dirty = true;
        visitChildPlayerDispatches_guess([value](Player *child) {
            child->setZFactor(value);
            return true;
        });
    }

    std::uint32_t Player::getProcessedMeshVerticesNum() const {
        std::uint32_t result = _processedMeshVerticesNum;
        visitChildPlayerDispatches_guess([&result](Player *child) {
            result += child->getProcessedMeshVerticesNum();
            return true;
        });
        return result;
    }

    void Player::visitChildPlayerDispatches_guess(
        const std::function<bool(Player *)> &visitor) const {
        // The end iterator is re-read from the deque at every condition check;
        // it is not the range-for snapshot form.
        for(auto it = _nodes.begin(); it != _nodes.end(); ++it) {
            const auto &node = *it;
            if(node.nodeType == 4) {
                // One retained Array dispatch spans the count read and every
                // callback. All four references repeatedly fetch numeric index
                // zero rather than the loop index; later particle children are
                // therefore never visited by this shared visitor.
                detail::ScopedParticleArrayDispatch_guess particleArray(
                    node.particleArrayVar);
                const int count = static_cast<int>(
                    detail::particleArrayCount_guess(particleArray.get()));
                for(int i = 0; i < count; ++i) {
                    Player *const child =
                        detail::particleArrayGetNativePlayerAt_guess(
                            particleArray.get(), 0);
                    if(!visitor(child)) {
                        return;
                    }
                }
            } else if(node.nodeType == 3) {
                if(!visitor(node.getChildPlayer())) {
                    return;
                }
            }
        }
    }

    detail::MotionNode *Player::findNodeByRawLabel_guess(
        const ttstr &name, const bool recursive) {
        const auto it = _nodeLabelMap.find(name);
        if(it != _nodeLabelMap.end()) {
            return &_nodes[static_cast<size_t>(it->second)];
        }

        if(!recursive) {
            return nullptr;
        }

        detail::MotionNode *found = nullptr;
        visitChildPlayerDispatches_guess([&](Player *child) {
            found = child->findNodeByRawLabel_guess(name, recursive);
            return found == nullptr;
        });
        return found;
    }

    tTJSVariant Player::getLayerMotion(tTJSVariant name) {
        detail::MotionNode *node = nullptr;
        {
            // The native method accepts a Variant, materializes one temporary
            // ttstr for recursive lookup, and releases it before copying the
            // node's child-player Variant to the return slot.
            const ttstr label(name);
            node = findNodeByRawLabel_guess(label, true);
        }
        if(node) {
            return node->childPlayerVar;
        }
        return {};
    }

    tTJSVariant Player::getLayerGetter(ttstr name) {
        auto *resolvedNode = findNodeByRawLabel_guess(name, true);
        if(!resolvedNode) {
            return {};
        }
        return buildLayerGetterVariant(*resolvedNode);
    }

    tTJSVariant Player::getLayerGetterList() {
        // The four references walk the flat node deque in node-index order and
        // emit one getter per non-root node. Duplicates are not collapsed;
        // unlike getLayerNames, every node contributes an element.
        auto result = detail::createTJSArrayWithItems_guess();
        for(size_t i = 1; i < _nodes.size(); ++i) {
            // Adaptor failure contributes a Void element; do not filter it.
            result.items->emplace_back(buildLayerGetterVariant(_nodes[i]));
        }
        return result.value;
    }

    tTJSVariant Player::getCameraTarget() const {
        auto result = detail::createTJSArrayWithItems_guess();
        result.items->emplace_back(static_cast<tjs_real>(_cameraTargetX));
        result.items->emplace_back(static_cast<tjs_real>(_cameraTargetY));
        result.items->emplace_back(static_cast<tjs_real>(_cameraTargetZ));
        return result.value;
    }

    tTJSVariant Player::getCameraPosition() const {
        auto result = detail::createTJSArrayWithItems_guess();
        result.items->emplace_back(static_cast<tjs_real>(_cameraPosX));
        result.items->emplace_back(static_cast<tjs_real>(_cameraPosY));
        result.items->emplace_back(static_cast<tjs_real>(_cameraPosZ));
        return result.value;
    }

    bool Player::getHasCamera() const {
        for(const auto &node : _nodes) {
            if(node.nodeType == 5) {
                return true;
            }
        }
        return false;
    }


    void Player::setStereovisionCameraPosition(double x, double y, double z) {
        _stereovisionCameraX_guess = x;
        _stereovisionCameraY_guess = y;
        _stereovisionCameraZ_guess = z;
    }


    bool Player::hitTestLayerByRawLabel_guess(
        const ttstr &name, const double x, const double y) {
        if(const auto *node = findNodeByRawLabel_guess(name, true)) {
            return hitTestMotionNodeShape(*node, x, y);
        }
        return false;
    }

    bool Player::contains(double x, double y) {
        // The constructor-created root at index 0 is excluded. Only shape
        // nodes participate in the direct local test.
        for(size_t i = 1; i < _nodes.size(); ++i) {
            const auto &node = _nodes[i];
            if(node.nodeType == 1 && hitTestMotionNodeShape(node, x, y)) {
                return true;
            }
        }

        bool found = false;
        visitChildPlayerDispatches_guess([&](Player *child) {
            if(child->contains(x, y)) {
                found = true;
                return false;
            }
            return true;
        });
        return found;
    }
    // --- Misc ---
    tTJSVariant Player::getCommandList() {
        // Build persistent node-owned items plus borrowed main/aux pointer
        // lists. Only the sorted main list is serialized.
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
            // The command-list serializer uses a zero translation offset.
            auto array = detail::createTJSArrayWithItems_guess();
            for(const auto &point : values) {
                array.items->emplace_back(static_cast<double>(point.x));
                array.items->emplace_back(static_cast<double>(point.y));
            }
            return array.value;
        };

        const auto buildCommand = [&](detail::PreparedRenderItem &item) {
            ncbDictionaryAccessor command;
            command.SetValue(TJS_W("key"), item.commandKey,
                             TJS_MEMBERENSURE,
                             &detail::commandKeyMemberHint_guess);
            command.SetValue(TJS_W("id"), item.layerId1,
                             TJS_MEMBERENSURE,
                             &detail::commandIdMemberHint_guess);
            command.SetValue(TJS_W("src"), item.commandSrc,
                             TJS_MEMBERENSURE,
                             &detail::srcMemberHint_guess);
            command.SetValue(TJS_W("coordinate"), item.coordinateMode,
                             TJS_MEMBERENSURE,
                             &detail::coordinateMemberHint_guess);
            command.SetValue(TJS_W("opacity"), item.opacity,
                             TJS_MEMBERENSURE,
                             &detail::opacityMemberHint_guess);
            command.SetValue(TJS_W("blendMode"), item.blendMode,
                             TJS_MEMBERENSURE,
                             &detail::blendModeMemberHint_guess);

            const auto coord = makeNumberArray({
                item.commandCoord[0], item.commandCoord[1],
                item.sortKey,
            });
            command.SetValue(TJS_W("coord"), coord, TJS_MEMBERENSURE,
                             &detail::coordMemberHint_guess);

            const auto mtx = makeNumberArray({
                item.commandMatrix[0], item.commandMatrix[1],
                item.commandMatrix[2], item.commandMatrix[3],
            });
            command.SetValue(TJS_W("mtx"), mtx, TJS_MEMBERENSURE,
                             &detail::mtxMemberHint_guess);

            const auto color = makeIntegerArray(item.packedColors);
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

            // clipRect is always written. Its Dictionary/Variant exists only
            // for this branch and is released before mesh payload creation.
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
                const tTJSVariant clipRect(
                    clip.GetDispatch(), clip.GetDispatch());
                command.SetValue(TJS_W("clipRect"), clipRect,
                                 TJS_MEMBERENSURE,
                                 &detail::clipRectMemberHint_guess);
            } else {
                const tTJSVariant clipRect;
                command.SetValue(TJS_W("clipRect"), clipRect,
                                 TJS_MEMBERENSURE,
                                 &detail::clipRectMemberHint_guess);
            }

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
                const tjs_int64 division =
                    serializeBezierPatchDivision_guess(scaledDivision);
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

            // Replace the persistent item Variant in place, retaining the same
            // command dispatch as both Object and objthis.
            item.commandVariant =
                tTJSVariant(command.GetDispatch(), command.GetDispatch());
        };

        // First pass: every main item gets a fresh command Variant before any
        // output filter. The borrowed main vector is dense and trusted; every
        // stored pointer is dereferenced by both native passes.
        for(auto *item : mainList) {
            buildCommand(*item);
        }

        auto result = detail::createTJSArrayWithItems_guess();
        for(auto *itemPtr : mainList) {
            auto &item = *itemPtr;
            // A filtered command stays on its persistent item but is omitted
            // from the returned Array.
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

    // All four registrars expose getD3DAvailable / doAlphaMaskOperation as
    // Motion namespace-level free functions, not Motion.Player methods. Their
    // local callbacks live in main.cpp.

} // namespace motion
