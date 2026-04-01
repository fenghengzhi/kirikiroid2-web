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
            // Let the SLA object be created (so entryOwner doesn't throw),
            // but return it normally. In drawAffine, if _motionSeparateAdaptor
            // is defined, TJS passes it to _player.draw(). The drawCompat SLA
            // path then renders to the owner (AffineLayer) via renderToLayer.
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

    private:
        tTJSVariant _owner;
    };
} // namespace motion
