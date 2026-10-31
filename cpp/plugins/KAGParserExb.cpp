#define NCB_MODULE_NAME TJS_W("KAGParserExb.dll")
#include "ncbind.hpp"
#include "KAGParser.h"
#include "ScriptMgnIntf.h"

namespace {
    // Successful registration holds these for the module lifetime. Instances
    // borrow them; the native callback has no matching unregistration release.
    tTVPKAGParserMethodCache Methods = {};
}

const tTVPKAGParserMethodCache &TVPGetKAGParserExbMethodCache() {
    return Methods;
}

static void RegisterKAGParserExb() {
    {
        tTJSVariant dictionaryClass;
        TVPExecuteExpression(TJS_W("Dictionary"), &dictionaryClass);
        iTJSDispatch2 *dictclass = dictionaryClass.AsObjectNoAddRef();
        tTJSVariant arrayClass;
        TVPExecuteExpression(TJS_W("Array"), &arrayClass);
        iTJSDispatch2 *arrayclass = arrayClass.AsObjectNoAddRef();

        try {
            tTJSVariant value;
            tjs_error er;

            er = dictclass->PropGet(0, TJS_W("clear"), nullptr, &value,
                                    dictclass);
            if(TJS_FAILED(er))
                TVPThrowInternalError;
            Methods.DicClear = value.AsObject();

            er = dictclass->PropGet(0, TJS_W("assign"), nullptr, &value,
                                    dictclass);
            if(TJS_FAILED(er))
                TVPThrowInternalError;
            Methods.DicAssign = value.AsObject();

            er = arrayclass->PropGet(0, TJS_W("clear"), nullptr, &value,
                                     arrayclass);
            if(TJS_FAILED(er))
                TVPThrowInternalError;
            Methods.ArrayClear = value.AsObject();

            er = arrayclass->PropGet(0, TJS_W("assign"), nullptr, &value,
                                     arrayclass);
            if(TJS_FAILED(er))
                TVPThrowInternalError;
            Methods.ArrayAssign = value.AsObject();

            er = arrayclass->PropGet(0, TJS_W("push"), nullptr, &value,
                                     arrayclass);
            if(TJS_FAILED(er))
                TVPThrowInternalError;
            Methods.ArrayPush = value.AsObject();
        } catch(...) {
            if(Methods.DicClear)
                Methods.DicClear->Release();
            if(Methods.DicAssign)
                Methods.DicAssign->Release();
            if(Methods.ArrayClear)
                Methods.ArrayClear->Release();
            if(Methods.ArrayAssign)
                Methods.ArrayAssign->Release();
            if(Methods.ArrayPush)
                Methods.ArrayPush->Release();
            throw;
        }
    }

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    iTJSDispatch2 *classobj = TVPCreateNativeClass_KAGParserExb();
    tTJSVariant value(classobj);
    classobj->Release();
    global->PropSet(TJS_MEMBERENSURE, TJS_W("KAGParser"), nullptr, &value,
                    global);
    global->Release();
}

NCB_POST_REGIST_CALLBACK(RegisterKAGParserExb);
