#include "ncbind.hpp"
#include "TextStream.h"

#define NCB_MODULE_NAME TJS_W("kirikiroid2.dll")

static tjs_error krkr_str_ord(tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, iTJSDispatch2 *objthis) {
    if (numparams == 0)
        return TJS_E_FAIL;

    tTJSVariant type = *param[0];
    if (type.Type() == tvtString) {
        const tTJSVariantString *vs = type.AsStringNoAddRef();
        tjs_int ch = static_cast<tjs_int>(vs->ShortString[0]);
        *result = ch;
    } else {
        *result = type;
    }
    return TJS_S_OK;
}

NCB_REGISTER_FUNCTION(_str_ord, krkr_str_ord);

static tjs_error krkr_setTextEncoding(tTJSVariant *result, tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *objthis) {
    if (numparams == 0)
        return TJS_E_FAIL;

    tTJSVariant type = *param[0];
    if (type.Type() == tvtString) {
        TVPSetDefaultReadEncoding(type);
    }
    return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(setTextEncoding, Storages, krkr_setTextEncoding);
