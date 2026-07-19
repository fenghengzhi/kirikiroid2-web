#include "ncbind.hpp"
#include "XP3Archive.h"
#include "SystemIntf.h"
#include "tjsNative.h"
#include <assert.h>
#include <list>
#include "tjsDebug.h"
#include "xp3filter.h"
#include "ThreadIntf.h"
#include <memory>
#include <thread>
#include <spdlog/spdlog.h>

#include "TextStream.h"

#define NCB_MODULE_NAME TJS_W("xp3filter.dll")

static bool _ManagedDecoderInited = false;
static bool _ManagedFilterInited = false;

struct XP3FilterDecoder {
    tTJS *ScriptEngine = new tTJS();
    tTJSVariantClosure ManagedDecoder;
    tTJSVariantClosure ManagedFilter;
    XP3FilterDecoder() : ManagedDecoder(nullptr), ManagedFilter(nullptr) {}
    ~XP3FilterDecoder() {
        if(ScriptEngine)
            ScriptEngine->Release();
    }
};

// static std::vector<XP3FilterDecoder*> sTVPScriptEngineStack;
// static std::mutex sTVPScriptEngineStackLock;
static ttstr sXP3FilterScript;

class XP3FilterRegister : public tTJSDispatch {
    typedef tTJSDispatch inherited;

protected:
    XP3FilterDecoder *Decoder;

public:
    XP3FilterRegister(XP3FilterDecoder *decoder) {
        Decoder = decoder;
        if(TJSObjectHashMapEnabled())
            TJSAddObjectHashRecord(this);
    }
    ~XP3FilterRegister() {
        if(TJSObjectHashMapEnabled())
            TJSRemoveObjectHashRecord(this);
    }

    tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, const tjs_char *classname,
                           iTJSDispatch2 *objthis) {
        if(membername == nullptr) {
            if(!TJS_strcmp(classname, TJS_W("Function")))
                return TJS_S_TRUE;
        }

        return inherited::IsInstanceOf(flag, membername, hint, classname,
                                       objthis);
    }
    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(membername)
            return inherited::FuncCall(flag, membername, hint, result,
                                       numparams, param, objthis);
        if(!objthis)
            return TJS_E_NATIVECLASSCRASH;

        if(result)
            result->Clear();

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(Decoder->ManagedDecoder.Object)
            Decoder->ManagedDecoder.Release();
        Decoder->ManagedDecoder = param[0]->AsObjectClosure();
        _ManagedDecoderInited = true;
        return TJS_S_OK;
    }
};

class XP3ContentFilterRegister : public XP3FilterRegister {
    typedef XP3FilterRegister inherited;

public:
    XP3ContentFilterRegister(XP3FilterDecoder *decoder) :
        XP3FilterRegister(decoder) {}
    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(membername)
            return inherited::FuncCall(flag, membername, hint, result,
                                       numparams, param, objthis);
        if(!objthis)
            return TJS_E_NATIVECLASSCRASH;

        if(result)
            result->Clear();

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(Decoder->ManagedFilter.Object)
            Decoder->ManagedFilter.Release();
        Decoder->ManagedFilter = param[0]->AsObjectClosure();
        _ManagedFilterInited = true;
        return TJS_S_OK;
    }
};

static XP3FilterDecoder *AddXP3Decoder() {
    XP3FilterDecoder *decoder = new XP3FilterDecoder;
    tTJSVariant val;
    iTJSDispatch2 *dsp;
    iTJSDispatch2 *global = decoder->ScriptEngine->GetGlobalNoAddRef();
#define REGISTER_OBJECT(classname, instance)                                   \
    dsp = (instance);                                                          \
    val = tTJSVariant(dsp /*, dsp*/);                                          \
    dsp->Release();                                                            \
    global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W(#classname),      \
                    nullptr, &val, global);
    REGISTER_OBJECT(Debug, TVPCreateNativeClass_Debug());
    REGISTER_OBJECT(System, TVPCreateNativeClass_System());
    tTJSNativeClass *cls = TVPCreateNativeClass_Storages();
    TJSNativeClassRegisterNCM(cls, TJS_W("setXP3ArchiveExtractionFilter"),
                              new XP3FilterRegister(decoder),
                              cls->GetClassName().c_str(), nitMethod,
                              TJS_STATICMEMBER);
    TJSNativeClassRegisterNCM(cls, TJS_W("setXP3ArchiveContentFilter"),
                              new XP3ContentFilterRegister(decoder),
                              cls->GetClassName().c_str(), nitMethod,
                              TJS_STATICMEMBER);
    REGISTER_OBJECT(Storages, cls);

    decoder->ScriptEngine->ExecScript(sXP3FilterScript);
    //	sTVPScriptEngineStack.emplace_back(decoder);
    return decoder;
}

static std::map<std::thread::id, XP3FilterDecoder *> _thread_decoders;
#ifdef __EMSCRIPTEN__
static std::mutex _decoders_mtx;
static XP3FilterDecoder *_shared_decoder = nullptr;
static std::unique_lock<std::mutex> FetchXP3DecoderLocked(XP3FilterDecoder *&out) {
    std::unique_lock<std::mutex> lk(_decoders_mtx);
    if(!_shared_decoder) {
        _shared_decoder = AddXP3Decoder();
    }
    out = _shared_decoder;
    return lk;
}
static XP3FilterDecoder *FetchXP3Decoder() {
    std::lock_guard<std::mutex> lk(_decoders_mtx);
    if(!_shared_decoder) {
        _shared_decoder = AddXP3Decoder();
    }
    return _shared_decoder;
}
#elif 1 || (defined(_MSC_VER) /*&& _MSC_VER <= 1800*/) ||                      \
    defined(CC_TARGET_OS_IPHONE)
static std::mutex _decoders_mtx;
static std::vector<XP3FilterDecoder *> _cached_decoders;
static XP3FilterDecoder *FetchXP3Decoder() {
    std::lock_guard<std::mutex> lk(_decoders_mtx);
    auto it = _thread_decoders.find(std::this_thread::get_id());
    if(it != _thread_decoders.end()) {
        XP3FilterDecoder *ret = it->second;
        return ret;
    }
    static bool Inited = false;
    if(!Inited) {
        Inited = true;
        TVPAddOnThreadExitEvent([]() {
            std::lock_guard<std::mutex> lk(_decoders_mtx);
            auto it = _thread_decoders.find(std::this_thread::get_id());
            if(it != _thread_decoders.end()) {
                _cached_decoders.emplace_back(it->second);
                _thread_decoders.erase(it);
            }
        });
    }
    XP3FilterDecoder *ret;
    if(!_cached_decoders.empty()) {
        ret = _cached_decoders.back();
        _cached_decoders.pop_back();
    } else {
        ret = AddXP3Decoder();
    }
    _thread_decoders[std::this_thread::get_id()] = ret;
    return ret;
}
#else
static XP3FilterDecoder *FetchXP3Decoder() {
    thread_local std::auto_ptr<XP3FilterDecoder> ret;
    if(!ret)
        ret = AddXP3Decoder();
    return ret.get();
}
#endif
tjs_int TVPXP3ArchiveContentFilterWrapper(const ttstr &filepath,
                                          const ttstr &archivename,
                                          tjs_uint64 filesize,
                                          tTJSVariant *ctx) {
    if(!_ManagedFilterInited)
        return 0;

    XP3FilterDecoder *decoder;
#ifdef __EMSCRIPTEN__
    auto lock = FetchXP3DecoderLocked(decoder);
#else
    decoder = FetchXP3Decoder();
#endif
    if(!decoder->ManagedFilter.Object)
        return 0;
    tTJSVariant FilePath(filepath);
    tTJSVariant ArcName(archivename);
    tTJSVariant FileSize((tjs_int64)filesize);
    tTJSVariant *vars[] = { &FilePath, &ArcName, &FileSize };
    tTJSVariant result;
    decoder->ManagedFilter.FuncCall(0, nullptr, nullptr, &result,
                                    sizeof(vars) / sizeof(vars[0]), vars,
                                    nullptr);
    tjs_int ret = 0;
    if(result.Type() == tvtObject) {
        iTJSDispatch2 *arr = result.AsObjectNoAddRef();
        ncbPropAccessor a(arr);
        ret = a.GetValue(0, ncbTypedefs::Tag<tjs_int>());
        *ctx = a.GetValue(1, ncbTypedefs::Tag<tTJSVariant>());
    }
    return ret;
}

void TVP_tTVPXP3ArchiveExtractionFilter_CONVENTION
TVPXP3ArchiveExtractionFilterWrapper(tTVPXP3ExtractionFilterInfo *info,
                                     tTJSVariant *ctx) {
    if(info->SizeOfSelf != sizeof(tTVPXP3ExtractionFilterInfo))
        TVPThrowExceptionMessage(
            TJS_W("Incompatible tTVPXP3ExtractionFilterInfo size"));
    XP3FilterDecoder *decoder;
#ifdef __EMSCRIPTEN__
    auto lock = FetchXP3DecoderLocked(decoder);
#else
    decoder = FetchXP3Decoder();
#endif
    if(decoder->ManagedDecoder.Object) {
        tTJSVariant FileHash = (tjs_int64)info->FileHash;
        tTJSVariant Offset = (tjs_int64)info->Offset;
        CBinaryAccessor *buf = new CBinaryAccessor(
            (unsigned char *)info->Buffer, info->BufferSize);
        tTJSVariant Buffer(buf);
        buf->Release();
        tTJSVariant BufferSize((tjs_int64)info->BufferSize);
        tTJSVariant FileName(info->FileName);
        tTJSVariant *vars[] = { &FileHash,   &Offset,   &Buffer,
                                &BufferSize, &FileName, ctx };
        decoder->ManagedDecoder.FuncCall(0, nullptr, nullptr, nullptr,
                                         sizeof(vars) / sizeof(vars[0]), vars,
                                         nullptr);
    }
}

void TVPSetXP3FilterScript(ttstr content) {
    if(sXP3FilterScript != content) {
        for(auto it : _thread_decoders) {
            delete it.second;
        }
        _thread_decoders.clear();
#ifdef __EMSCRIPTEN__
        delete _shared_decoder;
        _shared_decoder = nullptr;
#endif
    }
    if(content.IsEmpty()) {
        TVPSetXP3ArchiveExtractionFilter(nullptr);
        TVPSetXP3ArchiveContentFilter(nullptr);
    } else {
        TVPSetXP3ArchiveExtractionFilter(TVPXP3ArchiveExtractionFilterWrapper);
        TVPSetXP3ArchiveContentFilter(TVPXP3ArchiveContentFilterWrapper);
    }
    sXP3FilterScript = content;
}

static void PostRegistCallback() {
    ttstr path = TVPGetAppPath() + TJS_W("xp3filter.tjs");
    bool exists = TVPIsExistentStorageNoSearch(path);
    spdlog::info("xp3filter: PostRegistCallback path='{}', exists={}",
                 path.AsStdString(), exists);
    if(exists) {
        iTJSTextReadStream *stream = TVPCreateTextStreamForRead(path, "");
        try {
            stream->Read(sXP3FilterScript, 0);
        } catch(...) {
            stream->Destruct();
            throw;
        }
        stream->Destruct();
        spdlog::info("xp3filter: Script loaded, len={}", sXP3FilterScript.GetLen());
        TVPSetXP3ArchiveExtractionFilter(TVPXP3ArchiveExtractionFilterWrapper);
        TVPSetXP3ArchiveContentFilter(TVPXP3ArchiveContentFilterWrapper);
    }
}

NCB_POST_REGIST_CALLBACK(PostRegistCallback);
