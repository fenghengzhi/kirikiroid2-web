//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Plugins" class implementation / Service for plug-ins
//---------------------------------------------------------------------------
#include <set>
#include <algorithm>
#include <functional>

#include "tjsCommHead.h"

#include "ScriptMgnIntf.h"
#include "PluginImpl.h"

#include "StorageImpl.h"

#include "EventIntf.h"
#include "TransIntf.h"
#include "tjsArray.h"
#include "DebugIntf.h"

#include "tjs.h"
#include "tjsConfig.h"
#include "ncbind.hpp"

#ifdef TVP_SUPPORT_KPI
#include "kmp_pi.h"
#endif

#include "FilePathUtil.h"
#include "Application.h"
#include "SysInitImpl.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

void TVPLoadPlugin(const ttstr &name) {
    // Autoload and Plugins.link both pass their complete ttstr key directly to
    // the public NCB loader and ignore its bool.  The autoload scanner below
    // has one reference-platform split: iOS rewrites the discovered basename
    // from .tpm to .dll before it is joined to Path; Android retains .tpm.
    (void)ncbAutoRegister::LoadModule(name);
}

//---------------------------------------------------------------------------
bool TVPUnloadPlugin(const ttstr &name) {
    // Plugins.unlink is a true no-op in all four references. In particular,
    // it does not call the dormant ncbAutoRegister::AllUnregist traversal,
    // erase either NCB container, or inspect this name.
    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// plug-in autoload support
//---------------------------------------------------------------------------
struct tTVPFoundPlugin {
    std::string Path;
    std::string Name;

    bool operator<(const tTVPFoundPlugin &rhs) const { return Name < rhs.Name; }
};

// Four-reference state boundary: this is a zero-initialized signed 32-bit
// snapshot of the most recently completed discovery/sort phase.  tvpLoadPlugins
// does not reset it on entry.  An exception before the assignment below keeps
// the previous snapshot; an exception from logging/loading after publication
// leaves the new discovery count visible.  Android retains the exported getter
// below, while both final iOS images dead-strip that uncalled getter.
static tjs_int TVPAutoLoadPluginCount = 0;

static void TVPSearchPluginsAt(std::vector<tTVPFoundPlugin> &list,
                               std::string folder) {
    TVPListDir(folder, [&](const std::string &filename, int mask) {
        if(mask & S_IFREG) {
            if(!strcasecmp(filename.c_str() + filename.length() - 4, ".tpm")) {
                tTVPFoundPlugin fp;
                fp.Path = folder;
                fp.Name = filename;
#ifdef __APPLE__
                // Both iOS references perform this replacement while
                // materializing the found-plugin record.  Android stores the
                // original .tpm name.  Neither path later extracts the
                // basename from the complete Path/Name key.
                fp.Name = fp.Name.substr(0, fp.Name.length() - 4) + ".dll";
#endif
                list.emplace_back(fp);
            }
        }
    });
}

void TVPLoadInternalPlugins() {
    // The four current references first index all three NCB registration
    // lines, then eagerly load only xp3filter.dll.  motionplayer/emoteplayer
    // remain indexed in the internal map and are registered later by
    // Plugins.link or another module's PreRegist dependency callback. There
    // is deliberately no once guard: repeating this entry appends another
    // borrowed-pointer copy of every registrar to each internal module list.
    ncbAutoRegister::AllRegist();
    ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll"));
}

void tvpLoadPlugins() {
    TVPLoadInternalPlugins();
    // This function searches plugins which have an extension of
    // ".tpm" in the default path:
    //    1. a folder which holds kirikiri executable
    //    2. "plugin" folder of it
    // Plugin load order is to be decided using its name;
    // aaa.tpm is to be loaded before aab.tpm (sorted by ASCII order)

    // search plugins from path: (exepath), (exepath)\system,
    // (exepath)\plugin
    std::vector<tTVPFoundPlugin> list;

    std::string exepath = ExtractFileDir(TVPNativeProjectDir.AsStdString());

    TVPSearchPluginsAt(list, exepath);
    TVPSearchPluginsAt(list, exepath + "/system");
    TVPSearchPluginsAt(list, exepath + "/plugin");

    // sort by filename
    std::sort(list.begin(), list.end());

    // Publish before the empty check/load loop.  This exact ordering means an
    // empty successful discovery writes zero, while any later per-item failure
    // still exposes the complete discovered count rather than a loaded count.
    TVPAutoLoadPluginCount = (tjs_int)list.size();
    for(auto &i : list) {
        TVPAddImportantLog(ttstr(TJS_W("(info) Loading ")) +
                           ttstr(i.Name.c_str()));
        TVPLoadPlugin((i.Path + "/" + i.Name).c_str());
    }
}

//---------------------------------------------------------------------------
tjs_int TVPGetAutoLoadPluginCount() {
    // The Android references implement this as a raw 32-bit load with no
    // internal callers or side effects.
    return TVPAutoLoadPluginCount;
}

//---------------------------------------------------------------------------
// some service functions for plugin
//---------------------------------------------------------------------------
#include <zlib.h>

int ZLIB_uncompress(unsigned char *dest, unsigned long *destlen,
                    const unsigned char *source, unsigned long sourcelen) {
    return uncompress(dest, destlen, source, sourcelen);
}

//---------------------------------------------------------------------------
int ZLIB_compress(unsigned char *dest, unsigned long *destlen,
                  const unsigned char *source, unsigned long sourcelen) {
    return compress(dest, destlen, source, sourcelen);
}

//---------------------------------------------------------------------------
int ZLIB_compress2(unsigned char *dest, unsigned long *destlen,
                   const unsigned char *source, unsigned long sourcelen,
                   int level) {
    return compress2(dest, destlen, source, sourcelen, level);
}
//---------------------------------------------------------------------------
#include "md5.h"

static char TVP_assert_md5_state_t_size[(sizeof(TVP_md5_state_t) >=
                                         sizeof(md5_state_t))];

// if this errors, sizeof(TVP_md5_state_t) is not equal to
// sizeof(md5_state_t). sizeof(TVP_md5_state_t) must be equal to
// sizeof(md5_state_t).
//---------------------------------------------------------------------------
void TVP_md5_init(TVP_md5_state_t *pms) { md5_init((md5_state_t *)pms); }

//---------------------------------------------------------------------------
void TVP_md5_append(TVP_md5_state_t *pms, const tjs_uint8 *data, int nbytes) {
    md5_append((md5_state_t *)pms, (const md5_byte_t *)data, nbytes);
}

//---------------------------------------------------------------------------
void TVP_md5_finish(TVP_md5_state_t *pms, tjs_uint8 *digest) {
    md5_finish((md5_state_t *)pms, digest);
}

//---------------------------------------------------------------------------
bool TVPRegisterGlobalObject(const tjs_char *name, iTJSDispatch2 *dsp) {
    // This compatibility service survives in both Android references, but is
    // unreferenced and linker-dead-stripped from both final iOS images.  Keep
    // the recovered ordering: the nullable dispatch is first wrapped in an
    // object Variant (and AddRef'd when non-null), then the script global is
    // acquired as an owning reference.  Only the virtual PropSet belongs to
    // the catch region; Variant construction and global acquisition do not.
    tTJSVariant val(dsp);
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    tjs_error er;
    try {
        // The Android references deliberately have no global-null or name-null
        // guard here.  A missing engine therefore dereferences null, while a
        // null name is merely forwarded to PropSet.  The exact call uses
        // MEMBERENSURE, a null hint, and the global itself as objthis.
        er = global->PropSet(TJS_MEMBERENSURE, name, nullptr, &val, global);
    } catch(...) {
        // PropSet exceptions are swallowed only after balancing the owning
        // global reference.  The local Variant then releases its temporary
        // dispatch reference during the return cleanup.
        global->Release();
        return false;
    }
    global->Release();
    // Every nonnegative TJS status is true; failures are false.
    return TJS_SUCCEEDED(er);
}

//---------------------------------------------------------------------------
bool TVPRemoveGlobalObject(const tjs_char *name) {
    // Unlike registration, both Android references explicitly accept a
    // missing script engine and return false before the protected call.
    // TVPGetScriptDispatch still returns an owning reference when non-null.
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return false;
    tjs_error er;
    try {
        // No name validation or MEMBERENSURE flag is added: DeleteMember sees
        // flags=0, a null hint, and the global itself as objthis.
        er = global->DeleteMember(0, name, nullptr, global);
    } catch(...) {
        // Only DeleteMember exceptions are converted to false.  In both the
        // normal and exceptional paths the owning global reference is released
        // exactly once.
        global->Release();
        return false;
    }
    global->Release();
    // TJS_S_* statuses are true; negative deletion failures are false.
    return TJS_SUCCEEDED(er);
}

//---------------------------------------------------------------------------
void TVPDoTryBlock(tTVPTryBlockFunction tryblock,
                   tTVPCatchBlockFunction catchblock,
                   tTVPFinallyBlockFunction finallyblock, void *data) {
    // This dormant public compatibility service survives in both Android
    // references with no internal caller, but is linker-dead-stripped from
    // both final iOS images.  tryblock is intentionally mandatory.  The source
    // try region covers only this callback: the normal-path finally call below
    // is outside it, so an exception from finally propagates rather than being
    // routed through catchblock.
    try {
        tryblock(data);
    } catch(const eTJS &e) {
        // The exceptional finally callback runs first and exactly once.  It is
        // therefore also outside any protection by this handler: if it throws,
        // its replacement exception propagates and catchblock is never called.
        if(finallyblock)
            finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_W("eTJS");
        // Copy the virtual message into the stack description.  The callback
        // receives a const reference valid only for its invocation; both ttstr
        // members are destroyed when this handler leaves.
        desc.message = e.GetMessage();
        // catchblock is deliberately not nullable on an exceptional path.
        // false swallows the active exception; true rethrows that same original
        // exception after stack-description cleanup, without a second finally.
        if(catchblock(data, desc))
            throw;
        return;
    } catch(...) {
        // catch(...) has the same callback order.  It exposes no original
        // exception details: type is "unknown" and message stays default-empty.
        if(finallyblock)
            finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_W("unknown");
        if(catchblock(data, desc))
            throw;
        return;
    }
    // Nullable finally is the only callback with an explicit guard.  It runs
    // once after a normal tryblock return; there is no description/catch call.
    if(finallyblock)
        finallyblock(data);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Plugins
//---------------------------------------------------------------------------
tTJSNativeClass *TVPCreateNativeClass_Plugins() {
    auto *cls = new tTJSNC_Plugins();

    // setup some platform-specific members
    //---------------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ link) {

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr name = *param[0];

        // The public link surface accepts the module map key itself.  It does
        // not extract a path or rewrite .tpm to .dll, and false from an
        // already-loaded or missing module is not exposed to script.
        (void)ncbAutoRegister::LoadModule(name);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(
        /*object to register*/ cls,
        /*func. name*/ link)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ unlink) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr name = *param[0];

        bool res = TVPUnloadPlugin(name);

        if(result)
            *result = (tjs_int)res;

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(
        /*object to register*/ cls,
        /*func. name*/ unlink)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(getList) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        try {
            tjs_int idx = 0;
            for(const ttstr &name : TVPRegisteredPlugins) {
                tTJSVariant val(name);
                array->PropSetByNum(TJS_MEMBERENSURE, idx++, &val, array);
            }
            if(result)
                *result = tTJSVariant(array, array);
        } catch(...) {
            array->Release();
            throw;
        }
        array->Release();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(cls, getList)
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    return cls;
}
//---------------------------------------------------------------------------
