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
            if(!result) {
                return TJS_E_INVALIDPARAM;
            }
            const tTJSVariant owner =
                (numparams > 0 && param[0]) ? *param[0] : tTJSVariant{};
            *result = new SeparateLayerAdaptor(owner);
            if(objthis != nullptr && owner.Type() != tvtVoid) {
                tTJSVariant value = owner;
                objthis->PropSet(TJS_MEMBERENSURE, TJS_W("owner"), nullptr,
                                 &value, objthis);
                objthis->PropSet(TJS_MEMBERENSURE, TJS_W("_owner"), nullptr,
                                 &value, objthis);
            }
            return TJS_S_OK;
        }

        iTJSDispatch2 *getOwner() const {
            return _owner.Type() == tvtObject ? _owner.AsObjectNoAddRef() : nullptr;
        }

    private:
        tTJSVariant _owner;
    };
} // namespace motion
