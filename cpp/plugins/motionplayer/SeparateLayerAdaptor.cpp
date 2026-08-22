#include "SeparateLayerAdaptor.h"

#include <algorithm>
#include <utility>

#include "PlayerInternal.h"
#include "PlayerRenderInternal.h"

using namespace motion::internal;

namespace {

    void invalidateObjectVariant_guess(tTJSVariant &value) {
        if(value.Type() == tvtObject) {
            iTJSDispatch2 *object = value.AsObjectNoAddRef();
            object->Invalidate(0, nullptr, nullptr, object);
        }
        value.Clear();
    }

    tTJSVariant separateLayerOwner_guess(
        const tTJSVariant &targetLayer) {
        tTJSVariant targetCopy(targetLayer);
        tTJSVariant owner;
        iTJSDispatch2 *targetObject = targetCopy.AsObjectNoAddRef();
        targetObject->PropGet(0, TJS_W("window"), nullptr, &owner,
                              targetObject);
        return owner;
    }

    iTJSDispatch2 *resolveAssignableLayer(const tTJSVariant &value) {
        if(value.Type() != tvtObject || !value.AsObjectNoAddRef()) {
            return nullptr;
        }

        if(auto *adaptor =
               ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(
                   value.AsObjectNoAddRef(), false)) {
            if(auto *privateTarget = adaptor->getPrivateRenderTargetObject()) {
                return privateTarget;
            }
            if(auto *target = tryResolveLayerDispatch(adaptor->getTargetLayer())) {
                return target;
            }
            return adaptor->getOwner();
        }

        return tryResolveLayerDispatch(value);
    }

    iTJSDispatch2 *resolveAssignableLayerStrict_guess(
        const tTJSVariant &value) {
        // AsObjectNoAddRef preserves native Variant conversion failure for a
        // malformed payload instead of silently turning it into a skipped
        // map/sequence/property update.
        iTJSDispatch2 *object = value.AsObjectNoAddRef();
        if(auto *adaptor =
               ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(
                   object, false)) {
            if(auto *privateTarget = adaptor->getPrivateRenderTargetObject()) {
                return privateTarget;
            }
            if(auto *target = tryResolveLayerDispatch(adaptor->getTargetLayer())) {
                return target;
            }
            return adaptor->getOwner();
        }
        return tryResolveLayerDispatch(value);
    }

    bool getIntegerProperty_guess(iTJSDispatch2 *object,
                                  const tjs_char *name,
                                  tjs_int &out) {
        out = 0;
        if(!object) {
            return false;
        }

        // ncbPropAccessor::GetValue(default) first probes presence with a
        // disposable Variant. Only a successful probe performs the real
        // flags=0 read; that second status is deliberately ignored.
        tTJSVariant probe;
        const tjs_error probeHr = object->PropGet(
            TJS_MEMBERMUSTEXIST, name, nullptr, &probe, object);
        if(TJS_FAILED(probeHr)) {
            return false;
        }

        tTJSVariant value;
        (void)object->PropGet(0, name, nullptr, &value, object);
        out = static_cast<tjs_int>(value.AsInteger());
        return true;
    }

    void setIntegerProperty_guess(iTJSDispatch2 *object,
                                  const tjs_char *name,
                                  tjs_int value,
                                  tjs_uint32 *memberHint = nullptr) {
        if(!object) {
            return;
        }
        tTJSVariant variant(value);
        object->PropSet(TJS_MEMBERENSURE, name, memberHint, &variant, object);
    }

    void callSetSize_guess(iTJSDispatch2 *object,
                           tjs_int width,
                           tjs_int height) {
        if(!object) {
            return;
        }
        tTJSVariant widthVar(width);
        tTJSVariant heightVar(height);
        tTJSVariant *args[] = { &widthVar, &heightVar };
        object->FuncCall(0, TJS_W("setSize"),
                         &motion::detail::setSizeMemberHint_guess,
                         nullptr, 2, args, object);
    }

    void callAssignImages_guess(iTJSDispatch2 *target,
                                const tTJSVariant &sourceVariant) {
        if(!target) {
            return;
        }
        tTJSVariant sourceArg(sourceVariant);
        tTJSVariant *args[] = { &sourceArg };
        target->FuncCall(
            0, TJS_W("assignImages"),
            &motion::detail::assignImagesMemberHint_guess,
            nullptr, 1, args, target);
    }

} // namespace

namespace motion {

    bool SeparateLayerPayload_guess::requiresRefresh_guess(
        const SeparateLayerPayload_guess &other) const {
        const auto meshPointsEqual = [](const auto &lhs, const auto &rhs) {
            if(lhs.size() != rhs.size()) {
                return false;
            }
            for(std::size_t index = 0; index < lhs.size(); ++index) {
                if(lhs[index].x != rhs[index].x ||
                   lhs[index].y != rhs[index].y) {
                    return false;
                }
            }
            return true;
        };

        // The native comparator deliberately excludes layerVariant,
        // packedColors and corners. Every mismatch exit and the all-equal exit
        // return true; retain the short-circuit reads instead of turning this
        // shipped behavior into equality semantics.
        if(commandSrc != other.commandSrc) return true;
        if(completionType != other.completionType) return true;
        if(hasOutlineOrMeshline != other.hasOutlineOrMeshline) return true;
        if(blendMode != other.blendMode) return true;
        if(paintAndViewport != other.paintAndViewport) return true;
        if(!meshPointsEqual(compositeMeshPoints,
                            other.compositeMeshPoints)) return true;
        if(!meshPointsEqual(bezierPatchPoints,
                            other.bezierPatchPoints)) return true;
        return true;
    }

    SeparateLayerOrderedMap_guess::~SeparateLayerOrderedMap_guess() {
        clear(false);
    }

    SeparateLayerPayload_guess &
    SeparateLayerOrderedMap_guess::ensure(tjs_uint32 ordinal) {
        // Native insertion commits a zero/default payload node before the
        // resolver assigns the source payload.  Keep that ordering: an
        // exception from the later assignment does not roll the node back.
        return _nodes[ordinal];
    }

    void SeparateLayerOrderedMap_guess::erase(iterator it) {
        if(it != _nodes.end()) {
            _nodes.erase(it);
        }
    }

    void SeparateLayerOrderedMap_guess::clear(bool invalidateObjects) {
        if(invalidateObjects) {
            for(const auto &entry : _nodes) {
                auto payloadCopy = entry.second;
                invalidateObjectVariant_guess(payloadCopy.layerVariant);
            }
        }
        _nodes.clear();
    }

    void SeparateLayerOrderedMap_guess::swapWith(
        SeparateLayerOrderedMap_guess &other) {
        if(this == &other) {
            return;
        }
        _nodes.swap(other._nodes);
    }

    SeparateLayerAdaptor::SeparateLayerAdaptor(tTJSVariant targetLayer)
        : _owner(separateLayerOwner_guess(targetLayer)),
          _targetLayer(targetLayer) {}

    SeparateLayerAdaptor::~SeparateLayerAdaptor() { clear(); }

    iTJSDispatch2 *SeparateLayerAdaptor::getPrivateRenderTargetObject() const {
        return _privateTarget.Type() == tvtObject
                   ? _privateTarget.AsObjectNoAddRef()
                   : nullptr;
    }

    void SeparateLayerAdaptor::clearPrivateRenderState() {
        // clear invalidates the private target first. Active-map entries are
        // then copied into temporary payloads, invalidated through those
        // copies, and finally released when the complete tree is destroyed.
        // The sticky shared-D3D caller intentionally keeps a separate local
        // Layer Variant and continues dispatching through it after this pass.
        if(_privateTarget.Type() == tvtObject) {
            invalidateObjectVariant_guess(_privateTarget);
        } else {
            _privateTarget.Clear();
        }
        _activeLayers_guess.clear(true);
    }

    void SeparateLayerAdaptor::clear() { clearPrivateRenderState(); }

    void SeparateLayerAdaptor::beginLayerPass_guess() {
        // A pass swaps the complete active/retired trees and resets sequence.
        // An exceptional prior exit may intentionally leave retired state for
        // this next swap.
        _activeLayers_guess.swapWith(_retiredLayers_guess);
        _assignSequence = 0;
    }

    tTJSVariant SeparateLayerAdaptor::resolveLayerNode_guess(
        tjs_uint32 ordinal,
        const SeparateLayerPayload_guess &sourcePayload,
        bool &createdOrChanged) {
        return resolveLayerNodeInternal_guess(
            ordinal, sourcePayload, createdOrChanged);
    }

    tTJSVariant SeparateLayerAdaptor::resolveLayerOrdinal_guess(
        tjs_uint32 ordinal) {
        tTJSVariant layerVariant;
        auto retired = _retiredLayers_guess.find(ordinal);
        auto &active = _activeLayers_guess.ensure(ordinal);
        if(retired != _retiredLayers_guess.end()) {
            active.layerVariant = retired->second.layerVariant;
            layerVariant = active.layerVariant;
            _retiredLayers_guess.erase(retired);
        } else {
            active.layerVariant =
                detail::createLayerVariant_guess(_owner, _targetLayer);
            layerVariant = active.layerVariant;
        }

        iTJSDispatch2 *object =
            resolveAssignableLayerStrict_guess(layerVariant);
        setIntegerProperty_guess(
            object, TJS_W("absolute"),
            static_cast<tjs_int>(_absolute + _assignSequence),
            &detail::absoluteMemberHint_guess);
        // Unlike the payload overload, this path does not increment the
        // sequence after using it.
        setIntegerProperty_guess(
            object, TJS_W("hitThreshold"), 256,
            &detail::hitThresholdMemberHint_guess);
        return layerVariant;
    }

    void SeparateLayerAdaptor::endLayerPass_guess() {
        // Reused ordinals were erased during resolve; invalidate and destroy
        // only the Layer payloads that remain retired at the normal tail.
        _retiredLayers_guess.clear(true);
    }

    tTJSVariant SeparateLayerAdaptor::resolveLayerNodeInternal_guess(
        tjs_uint32 ordinal,
        const SeparateLayerPayload_guess &sourcePayload,
        bool &createdOrChanged) {
        tTJSVariant layerVariant;
        auto retired = _retiredLayers_guess.find(ordinal);
        if(retired != _retiredLayers_guess.end()) {
            createdOrChanged = sourcePayload.requiresRefresh_guess(
                retired->second);
            auto &active = _activeLayers_guess.ensure(ordinal);
            active = sourcePayload;
            active.layerVariant = retired->second.layerVariant;
            layerVariant = active.layerVariant;
            _retiredLayers_guess.erase(retired);
        } else {
            createdOrChanged = true;
            auto &active = _activeLayers_guess.ensure(ordinal);
            active = sourcePayload;
            active.layerVariant =
                detail::createLayerVariant_guess(_owner, _targetLayer);
            layerVariant = active.layerVariant;
        }

        iTJSDispatch2 *object =
            resolveAssignableLayerStrict_guess(layerVariant);
        setIntegerProperty_guess(
            object, TJS_W("absolute"),
            static_cast<tjs_int>(_absolute + _assignSequence),
            &detail::absoluteMemberHint_guess);
        ++_assignSequence;
        setIntegerProperty_guess(
            object, TJS_W("hitThreshold"), 256,
            &detail::hitThresholdMemberHint_guess);

        return layerVariant;
    }

    tjs_error SeparateLayerAdaptor::assignFromAdaptor_guess(
        const SeparateLayerAdaptor &source) {
        _activeLayers_guess.swapWith(_retiredLayers_guess);
        _assignSequence = 0;

        for(const auto &entry : source._activeLayers_guess) {
            const tjs_uint32 ordinal = entry.first;
            const auto &sourcePayload = entry.second;
            const tTJSVariant &sourceVariant = sourcePayload.layerVariant;

            bool createdOrChanged = false;
            tTJSVariant targetVariant = resolveLayerNodeInternal_guess(
                ordinal, sourcePayload, createdOrChanged);
            (void)createdOrChanged;
            iTJSDispatch2 *sourceLayerObject =
                resolveAssignableLayerStrict_guess(sourceVariant);
            iTJSDispatch2 *targetLayerObject =
                resolveAssignableLayerStrict_guess(targetVariant);

            callAssignImages_guess(targetLayerObject, sourceVariant);

            tjs_int width = 0;
            tjs_int height = 0;
            getIntegerProperty_guess(sourceLayerObject, TJS_W("height"),
                                     height);
            getIntegerProperty_guess(sourceLayerObject, TJS_W("width"),
                                     width);
            callSetSize_guess(targetLayerObject, width, height);

            tjs_int absolute = 0;
            tjs_int visible = 0;
            tjs_int opacity = 0;
            tjs_int type = 0;
            tjs_int left = 0;
            tjs_int top = 0;
            getIntegerProperty_guess(sourceLayerObject, TJS_W("absolute"),
                                     absolute);
            getIntegerProperty_guess(sourceLayerObject, TJS_W("visible"),
                                     visible);
            getIntegerProperty_guess(sourceLayerObject, TJS_W("opacity"),
                                     opacity);
            getIntegerProperty_guess(sourceLayerObject, TJS_W("type"), type);
            getIntegerProperty_guess(sourceLayerObject, TJS_W("left"), left);
            getIntegerProperty_guess(sourceLayerObject, TJS_W("top"), top);

            // assign overwrites the temporary sequence-based absolute with a
            // value rebased from the source adaptor, then preserves the native
            // property write order.
            setIntegerProperty_guess(
                targetLayerObject, TJS_W("absolute"),
                _absolute + absolute - source._absolute,
                &detail::absoluteMemberHint_guess);
            setIntegerProperty_guess(targetLayerObject, TJS_W("visible"),
                                     visible,
                                     &detail::visibleMemberHint_guess);
            setIntegerProperty_guess(targetLayerObject, TJS_W("opacity"),
                                     opacity,
                                     &detail::opacityMemberHint_guess);
            setIntegerProperty_guess(targetLayerObject, TJS_W("type"), type,
                                     &detail::typeMemberHint_guess);
            setIntegerProperty_guess(targetLayerObject, TJS_W("left"), left,
                                     &detail::leftMemberHint_guess);
            setIntegerProperty_guess(targetLayerObject, TJS_W("top"), top,
                                     &detail::topMemberHint_guess);
        }

        _retiredLayers_guess.clear(true);
        return TJS_S_OK;
    }

    tjs_error SeparateLayerAdaptor::assignCompat(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
        if(result) {
            result->Clear();
        }

        auto *nativeInstance =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(objthis, true);
        if(!nativeInstance) {
            return TJS_E_INVALIDOBJECT;
        }

        SeparateLayerAdaptor *sourceAdaptor = nullptr;
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject && param[0]->AsObjectNoAddRef()) {
            sourceAdaptor =
                ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                    param[0]->AsObjectNoAddRef(), false);
        }

        if(sourceAdaptor) {
            return nativeInstance->assignFromAdaptor_guess(*sourceAdaptor);
        }

        return TJS_S_OK;
    }

} // namespace motion
