#include "PrivateMotionGLL.h"

#include "PlayerInternal.h"
#include "PlayerRenderInternal.h"
#include "SeparateLayerAdaptor.h"

using namespace motion::internal;
using namespace motion::internal::render_detail;

namespace {

    bool hasLayerTreeOwnerInterfaceLike_0x800438(
        const tTJSVariant &ownerVariant) {
        tTJSVariant ownerInterface;
        return getObjectProperty(ownerVariant, TJS_W("layerTreeOwnerInterface"),
                                 ownerInterface) &&
            ownerInterface.Type() != tvtVoid;
    }

    iTJSDispatch2 *resolveLayerTreeOwnerObjectLike_0x800438(
        const tTJSVariant &ownerVariant) {
        if(ownerVariant.Type() != tvtObject || !ownerVariant.AsObjectNoAddRef()) {
            return nullptr;
        }
        if(!hasLayerTreeOwnerInterfaceLike_0x800438(ownerVariant)) {
            return nullptr;
        }

        // Native PrivateMotionGLL @ 0x800438 uses owner.ObjThis when present,
        // otherwise owner.Object, for layerTreeOwnerInterface. The Web port's
        // public Layer constructor needs that owner dispatch as its first arg.
        const auto closure = ownerVariant.AsObjectClosureNoAddRef();
        if(closure.ObjThis) {
            return closure.ObjThis;
        }
        if(closure.Object) {
            return closure.Object;
        }
        return ownerVariant.AsObjectNoAddRef();
    }

    iTJSDispatch2 *createLayerObjectForPrivateMotionGLLLike_0x800438(
        iTJSDispatch2 *layerTreeOwnerObject,
        iTJSDispatch2 *parentLayerObject) {
        if(!layerTreeOwnerObject) {
            return nullptr;
        }

        tTJSVariant layerClassVar;
        if(!getLayerClassDispatchVariantLike_0x5CB08C(layerClassVar)) {
            return nullptr;
        }

        iTJSDispatch2 *created = nullptr;
        tTJSVariant ownerVar(layerTreeOwnerObject, layerTreeOwnerObject);
        tTJSVariant parentVar =
            parentLayerObject ? tTJSVariant(parentLayerObject, parentLayerObject)
                              : tTJSVariant();
        tTJSVariant *args[] = { &ownerVar, &parentVar };
        const tjs_error hr = layerClassVar.AsObjectNoAddRef()->CreateNew(
            0, nullptr, nullptr, &created, 2, args,
            layerClassVar.AsObjectNoAddRef());
        if(TJS_FAILED(hr)) {
            return nullptr;
        }
        return created;
    }

    iTJSDispatch2 *createPrivateLayerObjectLike_0x800438(
        const tTJSVariant &ownerVariant,
        iTJSDispatch2 *targetLayerObject) {
        if(ownerVariant.Type() != tvtObject || !ownerVariant.AsObjectNoAddRef() ||
           !targetLayerObject) {
            return nullptr;
        }
        // libkrkr2.so PrivateMotionGLL constructor @ 0x800438 reads
        // owner.layerTreeOwnerInterface from the original owner closure, then
        // creates the backing layer from that layer-tree owner. The local Layer
        // public constructor needs the owner dispatch rather than the raw
        // interface pointer, so pass through the same owner closure boundary.
        auto *layerTreeOwnerObject =
            resolveLayerTreeOwnerObjectLike_0x800438(ownerVariant);
        if(!layerTreeOwnerObject) {
            return nullptr;
        }
        return createLayerObjectForPrivateMotionGLLLike_0x800438(
            layerTreeOwnerObject, targetLayerObject);
    }

} // namespace

namespace motion {

    PrivateMotionGLL::PrivateMotionGLL(const tTJSVariant &ownerVariant,
                                       const tTJSVariant &targetLayerVariant)
        : _ownerVariant(ownerVariant), _targetLayerVariant(targetLayerVariant) {}

    PrivateMotionGLL::~PrivateMotionGLL() { invalidate(); }

    iTJSDispatch2 *PrivateMotionGLL::ensureLayerObject(
        iTJSDispatch2 *targetLayerObject,
        bool absolute) {
        if(auto *existing = layerObject()) {
            return existing;
        }

        auto *created =
            createPrivateLayerObjectLike_0x800438(_ownerVariant,
                                                  targetLayerObject);
        if(!created) {
            return nullptr;
        }

        _layerObject = tTJSVariant(created, created);
        created->Release();

        // First creation mirrors Player_ResolveSLATarget @ 0x6D5948:
        // PrivateMotionGLL(owner, targetLayer), then absolute and visible=true.
        if(auto *layer = resolveNativeLayer(layerObject())) {
            layer->SetType(static_cast<tTVPLayerType>(ltAlpha));
            layer->SetAbsoluteOrderMode(absolute);
            layer->SetVisible(true);
        }
        return layerObject();
    }

    iTJSDispatch2 *PrivateMotionGLL::layerObject() const {
        return _layerObject.Type() == tvtObject
                   ? _layerObject.AsObjectNoAddRef()
                   : nullptr;
    }

    tTJSVariant PrivateMotionGLL::layerVariant() const { return _layerObject; }

    void PrivateMotionGLL::setSize(int width, int height) {
        if(auto *layer = resolveNativeLayer(layerObject())) {
            layer->SetSize(width, height);
        }
    }

    void PrivateMotionGLL::setVisible(bool visible) {
        if(auto *layer = resolveNativeLayer(layerObject())) {
            layer->SetVisible(visible);
        }
    }

    void PrivateMotionGLL::setAbsolute(bool absolute) {
        if(auto *layer = resolveNativeLayer(layerObject())) {
            layer->SetAbsoluteOrderMode(absolute);
        }
    }

    void PrivateMotionGLL::invalidate() {
        if(_layerObject.Type() == tvtObject && _layerObject.AsObjectNoAddRef()) {
            // SeparateLayerAdaptor.clear @ 0x6AC27C invalidates the private
            // object stored in SLA+40 before clearing the slot.
            auto closure = _layerObject.AsObjectClosureNoAddRef();
            if(closure.Object) {
                closure.Invalidate(0, nullptr, nullptr, nullptr);
            }
        }
        _layerObject.Clear();
    }

    iTJSDispatch2 *ensurePrivateMotionGLLLike_0x6D5948(
        SeparateLayerAdaptor &sla,
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant,
        iTJSDispatch2 *targetLayerObject,
        int canvasWidth,
        int canvasHeight) {
        if(!targetLayerObject || canvasWidth <= 0 || canvasHeight <= 0) {
            return nullptr;
        }

        if(!sla._privateMotionGLL) {
            auto privateLayer =
                std::make_unique<PrivateMotionGLL>(ownerVariant,
                                                   targetLayerVariant);
            auto *layerObject =
                privateLayer->ensureLayerObject(targetLayerObject,
                                                sla.getAbsolute());
            if(!layerObject) {
                return nullptr;
            }
            sla._privateMotionGLL = std::move(privateLayer);
            sla.trackManagedTarget(sla._privateMotionGLL->layerVariant());
        }

        auto *layerObject = sla._privateMotionGLL->layerObject();
        if(!layerObject || !resolveNativeLayer(layerObject)) {
            return nullptr;
        }

        // Reuse path matches 0x6D5948: no targetLayer/absolute rebind here;
        // only Layer_SetSize is applied after fetching the private layer.
        sla._privateMotionGLL->setSize(canvasWidth, canvasHeight);
        return layerObject;
    }

} // namespace motion
