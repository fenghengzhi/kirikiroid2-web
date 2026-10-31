#define NCB_MODULE_NAME TJS_W("KAGParserEx.dll")
#include "ncbind.hpp"
#include "KAGParser.h"
#include "ScriptMgnIntf.h"

namespace {
    // The native Ex module retains the class it replaces for its lifetime.
    iTJSDispatch2 *OriginalKAGParserClass = nullptr;

    void RegisterKAGParserEx() {
        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global)
            return;

        tTJSVariant original;
        if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("KAGParser"), nullptr,
                                        &original, global)))
            OriginalKAGParserClass = original.AsObject();

        iTJSDispatch2 *classobj = TVPCreateNativeClass_KAGParserEx();
        tTJSVariant value(classobj);
        classobj->Release();
        global->PropSet(TJS_MEMBERENSURE, TJS_W("KAGParser"), nullptr, &value,
                        global);
        global->Release();
    }
}

NCB_PRE_REGIST_CALLBACK(RegisterKAGParserEx);
