//
// Created by LiDon on 2025/9/15.
//
#pragma once

#include "tjs.h"

namespace motion {

    class SeparateLayerAdaptor {
    public:
        explicit SeparateLayerAdaptor(tTJSVariant owner = {}) : _owner(owner) {}

        static tjs_error factory(SeparateLayerAdaptor **result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
            // Return failure so TJS _motionSeparateAdaptor stays undefined.
            // This forces TJS drawAffine to use motionWorkLayer path instead
            // of SLA path. The motionWorkLayer path does proper assignImages
            // post-processing which is needed for the web port's rendering.
            if(result) *result = nullptr;
            return TJS_E_INVALIDPARAM;
        }

        iTJSDispatch2 *getOwner() const {
            return _owner.Type() == tvtObject ? _owner.AsObjectNoAddRef() : nullptr;
        }

        const tTJSVariant &getOwnerVariant() const {
            return _owner;
        }

    private:
        tTJSVariant _owner;
    };
} // namespace motion
