//
// Created by lidong on 2025/1/31.
// TODO: implement psbfile.dll plugin
// ref: https://github.com/number201724/psbfile
// ref: https://github.com/UlyssesWu/FreeMote
//
#include <spdlog/spdlog.h>
#include <cassert>

#include "tjs.h"
#include "MsgIntf.h"
#include "ncbind.hpp"
#include "PSBFile.h"
#include "PSBHeader.h"
#include "PSBMediaRegistry.h"
#include "PSBValue.h"

#define NCB_MODULE_NAME TJS_W("psbfile.dll")

#define LOGGER spdlog::get("plugin")

using namespace PSB;

void initPsbFile() { initPSBMedia(); }

void deInitPsbFile() { deInitPSBMedia(); }

namespace {

// libkrkr2.so sub_5981F8 @ 0x5981F8 creates this dispatch object for
// PSBFile.root.  sub_59673C @ 0x59673C creates another object of the same
// class for every nested list/dictionary instead of materializing a TJS
// Array/Dictionary tree.
class PSBValueDispatch final : public TJS::tTJSDispatch {
public:
    explicit PSBValueDispatch(std::shared_ptr<const IPSBValue> value) :
        value_(std::move(value)) {}

    tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *, tTJSVariant *result,
                      iTJSDispatch2 *) override {
        // libkrkr2.so sub_597854 @ 0x597854.
        if(membername == nullptr) {
            return TJS_E_NOTIMPL;
        }

        if(const auto list = std::dynamic_pointer_cast<const PSBList>(value_)) {
            if(ttstr(membername) == TJS_W("count")) {
                if(result != nullptr) {
                    *result = static_cast<tjs_int64>(list->size());
                }
                return TJS_S_OK;
            }
        } else if(const auto dictionary =
                      std::dynamic_pointer_cast<const PSBDictionary>(value_)) {
            const auto key = ttstr(membername).AsStdString();
            const auto child = (*dictionary)[key];
            if(child != nullptr) {
                assign(result, child);
                return TJS_S_OK;
            }
        }

        if(flag & TJS_MEMBERMUSTEXIST) {
            return TJS_E_MEMBERNOTFOUND;
        }
        if(result != nullptr) {
            result->Clear();
        }
        return TJS_S_OK;
    }

    tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num,
                           tTJSVariant *result,
                           iTJSDispatch2 *) override {
        // libkrkr2.so sub_5976C4 @ 0x5976C4.
        const auto list = std::dynamic_pointer_cast<const PSBList>(value_);
        if(list == nullptr) {
            return TJS_E_NOTIMPL;
        }

        const auto count = static_cast<tjs_int>(list->size());
        const auto index = num < 0 ? num + count : num;
        if(index < 0 || index >= count) {
            if(flag & TJS_MEMBERMUSTEXIST) {
                return TJS_E_MEMBERNOTFOUND;
            }
            if(result != nullptr) {
                result->Clear();
            }
            return TJS_S_OK;
        }

        assign(result, (*list)[index]);
        return TJS_S_OK;
    }

    tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                       tjs_uint32 *, iTJSDispatch2 *) override {
        // libkrkr2.so sub_5975E0 @ 0x5975E0.
        if(membername != nullptr) {
            return TJS_E_NOTIMPL;
        }
        const auto list = std::dynamic_pointer_cast<const PSBList>(value_);
        if(list == nullptr) {
            return TJS_E_NOTIMPL;
        }
        *result = static_cast<tjs_int>(list->size());
        return TJS_S_OK;
    }

    tjs_error IsInstanceOf(tjs_uint32, const tjs_char *membername,
                           tjs_uint32 *, const tjs_char *classname,
                           iTJSDispatch2 *) override {
        // libkrkr2.so sub_596E24 @ 0x596E24.
        if(membername != nullptr) {
            return TJS_E_NOTIMPL;
        }

        const tjs_char *valueClass = nullptr;
        switch(static_cast<unsigned char>(value_->getType())) {
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2e:
            case 0x2f:
            case 0x30:
            case 0x31:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3b:
            case 0x3c:
            case 0x3d:
            case 0x3f:
            case 0x41:
                return TJS_S_FALSE;
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x2c:
                valueClass = TJS_W("String");
                break;
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x2d:
                valueClass = TJS_W("Octet");
                break;
            case 0x20:
                valueClass = TJS_W("Array");
                break;
            case 0x21:
                valueClass = TJS_W("Dictionary");
                break;
            default:
                TVPThrowExceptionMessage(TJS_W(
                    "psb: internal error: unknown internal type detected.\n"));
        }

        return TJS_strcmp(classname, valueClass) == 0 ? TJS_S_TRUE
                                                     : TJS_S_FALSE;
    }

private:
    static void assign(tTJSVariant *result,
                       const std::shared_ptr<const IPSBValue> &value) {
        if(result == nullptr) {
            return;
        }

        // libkrkr2.so sub_59673C @ 0x59673C converts scalar nodes directly,
        // but wraps type 0x20/0x21 collection nodes in another dispatch.
        if(std::dynamic_pointer_cast<const PSBList>(value) != nullptr ||
           std::dynamic_pointer_cast<const PSBDictionary>(value) != nullptr) {
            auto *dispatch = new PSBValueDispatch(value);
            *result = tTJSVariant(dispatch, dispatch);
            dispatch->Release();
        } else {
            *result = value != nullptr ? value->toTJSVal() : tTJSVariant{};
        }
    }

    // The current parser owns decoded nodes with shared_ptr.  This member is
    // the wasm-side owner reference corresponding to the PSB owner AddRef in
    // sub_5981F8/sub_59673C; it keeps the addressed node alive for the full
    // lifetime of the TJS dispatch object.
    std::shared_ptr<const IPSBValue> value_;
};

} // namespace

static tjs_error getRoot(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                         iTJSDispatch2 *obj) {
    auto *self = ncbInstanceAdaptor<PSB::PSBFile>::GetNativeInstance(obj);
    auto *root = new PSBValueDispatch(self->getObjects());
    *r = tTJSVariant(root, root);
    root->Release();
    return TJS_S_OK;
}

static tjs_error load(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                      iTJSDispatch2 *obj) {
    bool loadSuccess = true;
    auto *self = ncbInstanceAdaptor<PSB::PSBFile>::GetNativeInstance(obj);
    if(count != 1) {
        return TJS_E_BADPARAMCOUNT;
    }

    if((*p)->Type() == tvtString) {
        ttstr path{ **p };
        if(!self->loadPSBFile(path)) {
            LOGGER->info("cannot load psb file : {}", path.AsStdString());
            loadSuccess = false;
        }
        registerRootResources(path, *self);
    } else if((*p)->Type() == tvtOctet) {
        LOGGER->critical("PSBFile::load stream no implement!");
        loadSuccess = false;
    } else {
        return TJS_E_INVALIDPARAM;
    }

    if(r != nullptr)
        *r = tTJSVariant(loadSuccess);
    return TJS_S_OK;
}

// 因为有两种版本的psbfile插件调用方式不一样
// TODO: 第一种（新) 实现有问题, 可能忽略了某些东西
// var psbfile = new PSBFile();
// psbfile.load("xxxx.PIMG");
// 第二种（旧)
// new PSBFile("xxxx.PIMG");

template <typename T>
class PSBFileConvertor {
    typedef ncbTypeConvertor::Stripper<PSBFile>::Type ClassT;
    typedef ncbInstanceAdaptor<ClassT> AdaptorT;

public:
    PSBFileConvertor() = default;
    virtual ~PSBFileConvertor() = default;

    virtual void operator()(T *&dst, const tTJSVariant &src) {
        if(src.Type() == tvtObject) {
            dst = AdaptorT::GetNativeInstance(src.AsObjectNoAddRef());
        }
    }

    void operator()(tTJSVariant &dst, const T *&src) {
        if(src != nullptr) {
            if(iTJSDispatch2 *adpObj = AdaptorT::CreateAdaptor(src)) {
                dst = tTJSVariant(adpObj, adpObj);
                adpObj->Release();
            }
        } else {
            dst.Clear();
        }
    }
};

NCB_SET_CONVERTOR(PSBFile, PSBFileConvertor<PSBFile>);
NCB_SET_CONVERTOR(const PSBFile *, PSBFileConvertor<const PSBFile>);

static tjs_error PSBFileFactory(PSBFile **result, tjs_int count,
                                tTJSVariant **params, iTJSDispatch2 *_) {
    PSBFile *psbFile = nullptr;
    if(count == 0) {
        psbFile = new PSBFile();
    } else if(count == 1 && (*params)->Type() == tvtString) {
        ttstr path{ *params[0] };
        psbFile = new PSBFile();
        psbFile->loadPSBFile(path);
    } else {
        return TJS_E_INVALIDPARAM;
    }
    *result = psbFile;
    return TJS_S_OK;
}

NCB_REGISTER_CLASS(PSBFile) {
    Factory(PSBFileFactory);
    RawCallback(TJS_W("root"), &getRoot, 0, 0);
    RawCallback(TJS_W("load"), &load, 0);
}

NCB_PRE_REGIST_CALLBACK(initPsbFile);
NCB_POST_UNREGIST_CALLBACK(deInitPsbFile);
