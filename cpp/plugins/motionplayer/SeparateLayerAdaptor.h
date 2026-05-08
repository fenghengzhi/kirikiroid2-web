//
// Created by LiDon on 2025/9/15.
//
#pragma once

#include <memory>
#include <vector>

#include "tjs.h"

namespace motion {

    class PrivateMotionGLL;

    class SeparateLayerAdaptor {
    public:
        explicit SeparateLayerAdaptor(tTJSVariant owner = {});
        ~SeparateLayerAdaptor();

        static tjs_error factory(SeparateLayerAdaptor **result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
            tTJSVariant owner;
            if(numparams > 0 && param[0]) {
                owner = *param[0];
            }
            if(result) *result = new SeparateLayerAdaptor(owner);
            return TJS_S_OK;
        }

        iTJSDispatch2 *getOwner() const {
            return _owner.Type() == tvtObject ? _owner.AsObjectNoAddRef() : nullptr;
        }

        const tTJSVariant &getOwnerVariant() const {
            return _owner;
        }

        // Aligned to libkrkr2.so SeparateLayerAdaptor_ncb_registerMembers (0x6ABFAC)
        bool getAbsolute() const { return _absolute; }
        void setAbsolute(bool v) { _absolute = v; }
        tTJSVariant getTargetLayer() const { return _targetLayer; }
        void setTargetLayer(tTJSVariant v) { _targetLayer = v; }

        tTJSVariant getPrivateRenderTarget() const;
        iTJSDispatch2 *getPrivateRenderTargetObject() const;
        void c();
        static tjs_error assignCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis);

    private:
        friend iTJSDispatch2 *ensurePrivateMotionGLLLike_0x6D5948(
            SeparateLayerAdaptor &sla,
            const tTJSVariant &ownerVariant,
            const tTJSVariant &targetLayerVariant,
            iTJSDispatch2 *targetLayerObject,
            int canvasWidth,
            int canvasHeight);

        void trackManagedTarget(const tTJSVariant &target);
        void clearPrivateRenderState();

        tTJSVariant _owner;
        bool _absolute = false;
        tTJSVariant _targetLayer;
        std::unique_ptr<PrivateMotionGLL> _privateMotionGLL;
        std::vector<tTJSVariant> _managedTargets;
    };
} // namespace motion
