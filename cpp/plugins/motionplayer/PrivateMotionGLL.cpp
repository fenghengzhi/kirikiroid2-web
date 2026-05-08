#include "PrivateMotionGLL.h"

#include "MsgIntf.h"
#include "PlayerInternal.h"
#include "PlayerRenderInternal.h"
#include "SeparateLayerAdaptor.h"

using namespace motion::internal;
using namespace motion::internal::render_detail;

namespace {

    struct OwnerResolutionLike_0x800438 {
        tTJSVariantClosure closure;
        iTVPLayerTreeOwner *layerTreeOwner = nullptr;
    };

    OwnerResolutionLike_0x800438 requireOwnerClosureLike_0x800438(
        const tTJSVariant &ownerVariant) {
        if(ownerVariant.Type() != tvtObject || !ownerVariant.AsObjectNoAddRef()) {
            TVPThrowExceptionMessage(
                TJS_W("Please specify layerTreeOwnerInterface object"));
        }

        auto closure = ownerVariant.AsObjectClosureNoAddRef();
        if(!closure.Object) {
            TVPThrowExceptionMessage(
                TJS_W("Please specify layerTreeOwnerInterface object"));
        }

        tTJSVariant ownerInterface;
        iTJSDispatch2 *objthis = closure.ObjThis ? closure.ObjThis
                                                 : closure.Object;
        const tjs_error hr = closure.Object->PropGet(
            0, TJS_W("layerTreeOwnerInterface"), nullptr, &ownerInterface,
            objthis);
        if(TJS_FAILED(hr)) {
            TVPThrowExceptionMessage(
                TJS_W("Cannot Retrive Layer Tree Owner Interface."));
        }
        auto *layerTreeOwner = reinterpret_cast<iTVPLayerTreeOwner *>(
            static_cast<tjs_intptr_t>(static_cast<tjs_int64>(ownerInterface)));
        if(!layerTreeOwner) {
            TVPThrowExceptionMessage(
                TJS_W("Cannot Retrive Layer Tree Owner Interface."));
        }
        return { closure, layerTreeOwner };
    }

    tTJSNI_BaseLayer *requireTargetLayerNativeLike_0x800438(
        const tTJSVariant &targetLayerVariant,
        iTJSDispatch2 *targetLayerObject) {
        if(targetLayerVariant.Type() != tvtObject ||
           !targetLayerVariant.AsObjectNoAddRef() || !targetLayerObject) {
            TVPThrowExceptionMessage(TJS_W("Please specify Layer object."));
        }

        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(targetLayerVariant.AsObjectNoAddRef()->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            TVPThrowExceptionMessage(TJS_W("Please specify Layer object."));
        }
        return layer;
    }

    class tTJSNI_PrivateMotionGLLLayerLike_0x800438 final : public tTJSNI_Layer {
    public:
        tjs_error Construct(tjs_int numparams,
                            tTJSVariant **param,
                            iTJSDispatch2 *tjs_obj) override {
            if(numparams < 2) {
                return TJS_E_BADPARAMCOUNT;
            }

            auto owner =
                requireOwnerClosureLike_0x800438(*param[0]);
            iTJSDispatch2 *targetObject =
                param[1]->Type() == tvtObject ? param[1]->AsObjectNoAddRef()
                                               : nullptr;
            auto *parentLayer =
                requireTargetLayerNativeLike_0x800438(*param[1], targetObject);

            if(parentLayer == this) {
                TVPThrowExceptionMessage(TVPCannotSetParentSelf);
            }
            if(parentLayer->GetLayerTreeOwner() != owner.layerTreeOwner) {
                TVPThrowExceptionMessage(TVPCannotMoveToUnderOtherPrimaryLayer);
            }

            // PrivateMotionGLL @ 0x800438 resolves the owner LTO and target
            // native Layer before the raw child-layer attach. This bypasses
            // the public Layer constructor's script-side lookup while keeping
            // the resulting object registered as a normal Layer native class.
            const tjs_error hr = ConstructResolvedTreeOwnerLike_0x800438(
                owner.layerTreeOwner, parentLayer, tjs_obj, owner.closure);
            if(TJS_FAILED(hr)) {
                return hr;
            }

            // sub_8361A8 binds the child owner and immediately applies
            // visible=true and opacity=255 to the new layer.
            SetVisible(true);
            SetOpacity(255);
            return TJS_S_OK;
        }
    };

    class tTJSNC_PrivateMotionGLLLayerLike_0x800438 final : public tTJSNC_Layer {
    protected:
        tTJSNativeInstance *CreateNativeInstance() override {
            return new tTJSNI_PrivateMotionGLLLayerLike_0x800438();
        }
    };

    iTJSDispatch2 *createPrivateLayerObjectWithNativeClassLike_0x800438(
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant) {
        tTJSNC_PrivateMotionGLLLayerLike_0x800438 layerClass;
        iTJSDispatch2 *created = nullptr;
        tTJSVariant ownerArg(ownerVariant);
        tTJSVariant targetArg(targetLayerVariant);
        tTJSVariant *args[] = { &ownerArg, &targetArg };
        const tjs_error hr = layerClass.CreateNew(0, nullptr, nullptr, &created,
                                                  2, args, &layerClass);
        if(TJS_FAILED(hr) || !created) {
            TVPThrowExceptionMessage(TJS_W("Cannot create PrivateMotionGLL."));
        }
        return created;
    }

    iTJSDispatch2 *createPrivateLayerObjectLike_0x800438(
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant,
        iTJSDispatch2 *targetLayerObject) {
        requireOwnerClosureLike_0x800438(ownerVariant);
        requireTargetLayerNativeLike_0x800438(targetLayerVariant,
                                             targetLayerObject);

        return createPrivateLayerObjectWithNativeClassLike_0x800438(
            ownerVariant, targetLayerVariant);
    }

    void invalidateObjectVariantLike_0x6AC27C(tTJSVariant &value) {
        if(value.Type() == tvtObject && value.AsObjectNoAddRef()) {
            auto closure = value.AsObjectClosureNoAddRef();
            if(closure.Object) {
                closure.Invalidate(0, nullptr, nullptr, nullptr);
            }
        }
        value.Clear();
    }

} // namespace

namespace motion {

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

        if(sla._privateTarget.Type() != tvtObject) {
            if(sla._privateTarget.Type() != tvtVoid) {
                sla._privateTarget.Clear();
            }

            iTJSDispatch2 *created = createPrivateLayerObjectLike_0x800438(
                ownerVariant, targetLayerVariant, targetLayerObject);
            sla._privateTarget = tTJSVariant(created, created);
            created->Release();

            if(auto *layer =
                   resolveNativeLayer(sla._privateTarget.AsObjectNoAddRef())) {
                // Player_ResolveSLATarget @ 0x6D5948 performs these writes
                // immediately after the newly created object is stored in
                // SLA+40, before the per-frame SetSize call.
                layer->SetType(static_cast<tTVPLayerType>(ltAlpha));
                layer->SetAbsoluteOrderMode(sla.getAbsolute());
                layer->SetVisible(true);
            }
            sla.trackManagedTargetLike_0x6AC410(sla._privateTarget, 0);
        }

        iTJSDispatch2 *layerObject = sla.getPrivateRenderTargetObject();
        if(!layerObject || !resolveNativeLayer(layerObject)) {
            invalidateObjectVariantLike_0x6AC27C(sla._privateTarget);
            return nullptr;
        }

        // The native reuse path always applies Layer_SetSize_thunk after the
        // PrivateMotionGLL native instance is fetched from SLA+40.
        resolveNativeLayer(layerObject)->SetSize(canvasWidth, canvasHeight);
        return layerObject;
    }

} // namespace motion
