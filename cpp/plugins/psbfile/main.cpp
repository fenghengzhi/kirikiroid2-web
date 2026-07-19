#include <cstdint>
#include <string>
#include <utility>

#include "MsgIntf.h"
#include "PSBMediaRegistry.h"
#include "PSBDispatch.h"
#include "PSBRawFile.h"
#include "ncbind.hpp"
#include "tjs.h"
#include "tjsNative.h"

#define NCB_MODULE_NAME TJS_W("PSBFile.dll")

using PSB::PSBFile;
using PSB::PSBRawNode;

void initPsbFile() { PSB::initPSBMedia(); }

namespace {
    void throwUnknownType() {
        TVPThrowExceptionMessage(
            TJS_W("psb: internal error: unknown internal type detected.\n"));
    }

    // Raw collection dispatch reconstructed from sub_597AD4 @ 0x597AD4.
    // PSBRawNode is exactly the owner+node pair retained by that constructor;
    // valid_ is the independent byte toggled by sub_596F0C @ 0x596F0C.
    class PSBValueDispatch final : public iTJSDispatch2,
                                   public iTJSNativeInstance {
    public:
        explicit PSBValueDispatch(PSBRawNode value) :
            value_(std::move(value)) {}

        tjs_uint AddRef() override {
            // sub_597AC0 @ 0x597AC0.
            return ++refCount_;
        }

        tjs_uint Release() override {
            // sub_597A40 @ 0x597A40.
            const tjs_uint count = --refCount_;
            if(count == 0) {
                delete this;
            }
            return count;
        }

        // The one-instruction vtable entries in 0x596D78..0x597A20 return
        // TJS_E_NOTIMPL unconditionally.  This differs from tTJSDispatch's
        // member-name-sensitive defaults.
        tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           tTJSVariant *, tjs_int, tTJSVariant **,
                           iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error FuncCallByNum(tjs_uint32, tjs_int, tTJSVariant *, tjs_int,
                                tTJSVariant **, iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *, tTJSVariant *result,
                          iTJSDispatch2 *) override {
            // sub_597854 @ 0x597854.
            if(membername == nullptr) {
                return TJS_E_NOTIMPL;
            }
            if(!valid_ || value_.GetOwner() == nullptr) {
                return TJS_E_INVALIDOBJECT;
            }

            const int category = value_.GetTypeCategory();
            if(category == 6) {
                if(TJS_strcmp(membername, TJS_W("count")) == 0) {
                    std::uint32_t count{};
                    (void)value_.GetArrayCount(count);
                    if(result != nullptr) {
                        *result = static_cast<tjs_int64>(count);
                    }
                    return TJS_S_OK;
                }
            } else if(category == 7) {
                if(const auto *child = value_.FindDictionaryValue(
                       ttstr(membername).AsStdString())) {
                    assign(result, value_.GetOwner(), child);
                    return TJS_S_OK;
                }
            }

            if((flag & TJS_MEMBERMUSTEXIST) != 0) {
                return TJS_E_MEMBERNOTFOUND;
            }
            if(result != nullptr) {
                result->Clear();
            }
            return TJS_S_OK;
        }

        tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num,
                               tTJSVariant *result, iTJSDispatch2 *) override {
            // sub_5976C4 @ 0x5976C4.
            if(!valid_ || value_.GetOwner() == nullptr) {
                return TJS_E_INVALIDOBJECT;
            }
            if(value_.GetTypeCategory() != 6) {
                return TJS_E_MEMBERNOTFOUND;
            }
            std::uint32_t count{};
            (void)value_.GetArrayCount(count);

            const tjs_int64 index = num < 0
                ? static_cast<tjs_int64>(num) + count
                : static_cast<tjs_int64>(num);
            if(index < 0 || index >= count) {
                if((flag & TJS_MEMBERMUSTEXIST) != 0) {
                    return TJS_E_MEMBERNOTFOUND;
                }
                if(result != nullptr) {
                    result->Clear();
                }
                return TJS_S_OK;
            }

            assign(result, value_.GetOwner(),
                   value_.FindArrayElement(static_cast<std::uint32_t>(index)));
            return TJS_S_OK;
        }

        tjs_error PropSet(tjs_uint32, const tjs_char *, tjs_uint32 *,
                          const tTJSVariant *, iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error PropSetByNum(tjs_uint32, tjs_int, const tTJSVariant *,
                               iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                           tjs_uint32 *, iTJSDispatch2 *) override {
            // sub_5975E0 @ 0x5975E0.
            if(!valid_ || value_.GetOwner() == nullptr) {
                return TJS_E_INVALIDOBJECT;
            }
            if(membername != nullptr) {
                return TJS_E_NOTIMPL;
            }
            if(value_.GetTypeCategory() != 6) {
                return TJS_E_NOTIMPL;
            }
            std::uint32_t count{};
            (void)value_.GetArrayCount(count);
            *result = static_cast<tjs_int>(count);
            return TJS_S_OK;
        }

        tjs_error GetCountByNum(tjs_int *, tjs_int, iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error PropSetByVS(tjs_uint32, tTJSVariantString *,
                              const tTJSVariant *, iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error IsInstanceOf(tjs_uint32, const tjs_char *membername,
                               tjs_uint32 *, const tjs_char *classname,
                               iTJSDispatch2 *) override {
            // sub_596E24 @ 0x596E24 intentionally does not consult valid_.
            if(membername != nullptr) {
                return TJS_E_NOTIMPL;
            }

            const tjs_char *expected = nullptr;
            switch(value_.GetType()) {
                case 0x15:
                case 0x16:
                case 0x17:
                case 0x18:
                case 0x2c:
                    expected = TJS_W("String");
                    break;
                case 0x19:
                case 0x1a:
                case 0x1b:
                case 0x1c:
                case 0x2d:
                    expected = TJS_W("Octet");
                    break;
                case 0x20:
                    expected = TJS_W("Array");
                    break;
                case 0x21:
                    expected = TJS_W("Dictionary");
                    break;
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
                default:
                    throwUnknownType();
            }
            return TJS_strcmp(classname, expected) == 0 ? TJS_S_TRUE
                                                        : TJS_S_FALSE;
        }

        tjs_error EnumMembers(tjs_uint32 flag, tTJSVariantClosure *callback,
                              iTJSDispatch2 *) override {
            // PSBValueDispatch_EnumMembers_guess @ 0x596F50.
            if(!valid_ || value_.GetOwner() == nullptr) {
                return TJS_E_INVALIDOBJECT;
            }
            const int category = value_.GetTypeCategory();
            if(category != 6 && category != 7) {
                return TJS_E_NOTIMPL;
            }

            tTJSVariant name;
            tTJSVariant memberFlags(static_cast<tjs_int>(0));
            tTJSVariant memberValue;
            tTJSVariant callbackResult;
            tTJSVariant *params[3] = { &name, &memberFlags, &memberValue };
            const bool noValue = (flag & TJS_ENUM_NO_VALUE) != 0;

            if(category == 6) {
                std::uint32_t count{};
                (void)value_.GetArrayCount(count);
                for(std::uint32_t index = 0; index < count; ++index) {
                    name = ttstr(static_cast<tjs_int>(index));
                    if(!noValue) {
                        assign(&memberValue, value_.GetOwner(),
                               value_.FindArrayElement(index));
                    }
                    callback->FuncCall(0, nullptr, nullptr, &callbackResult,
                                       noValue ? 2 : 3, params, this);
                }
            } else {
                std::uint32_t count{};
                (void)value_.GetDictionaryCount(count);
                std::string key;
                const std::uint8_t *child{};
                for(std::uint32_t index = 0; index < count; ++index) {
                    (void)value_.GetDictionaryEntry(index, key, child);
                    name = ttstr(key);
                    if(!noValue) {
                        assign(&memberValue, value_.GetOwner(), child);
                    }
                    callback->FuncCall(0, nullptr, nullptr, &callbackResult,
                                       noValue ? 2 : 3, params, this);
                }
            }
            return TJS_S_OK;
        }

        tjs_error DeleteMember(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error DeleteMemberByNum(tjs_uint32, tjs_int,
                                    iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error NativeInstanceSupport(tjs_uint32 flag, tjs_int32 classid,
                                        iTJSNativeInstance **pointer) override {
            // sub_596D90 @ 0x596D90 lazily registers PSBValueClass and exposes
            // the embedded raw-node view only for GETINSTANCE.
            if(flag != TJS_NIS_GETINSTANCE) {
                return TJS_E_NOTIMPL;
            }
            if(classid != PSB::GetPSBValueClassID()) {
                return TJS_E_FAIL;
            }
            *pointer = static_cast<iTJSNativeInstance *>(this);
            return TJS_S_OK;
        }

        tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
            // sub_597A38 @ 0x597A38.
            return TJS_S_OK;
        }

        void Invalidate() override {
            // nullsub_259 @ 0x596F3C.
        }

        void Destruct() override {
            // nullsub_261 @ 0x597A2C.
        }

        tjs_error IsValid(tjs_uint32, const tjs_char *, tjs_uint32 *,
                          iTJSDispatch2 *) override {
            // sub_596EF0 @ 0x596EF0.
            return valid_ ? TJS_S_TRUE : TJS_S_FALSE;
        }

        tjs_error Invalidate(tjs_uint32, const tjs_char *membername,
                             tjs_uint32 *, iTJSDispatch2 *) override {
            // sub_596F0C @ 0x596F0C.
            if(membername != nullptr) {
                return TJS_E_NOTIMPL;
            }
            if(!valid_) {
                return TJS_E_INVALIDOBJECT;
            }
            valid_ = false;
            return TJS_S_OK;
        }

        tjs_error InvalidateByNum(tjs_uint32, tjs_int,
                                  iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error IsValidByNum(tjs_uint32, tjs_int, iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error CreateNew(tjs_uint32, const tjs_char *, tjs_uint32 *,
                            iTJSDispatch2 **, tjs_int, tTJSVariant **,
                            iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error CreateNewByNum(tjs_uint32, tjs_int, iTJSDispatch2 **, tjs_int,
                                 tTJSVariant **, iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error Reserved1() override { return TJS_E_NOTIMPL; }

        tjs_error IsInstanceOfByNum(tjs_uint32, tjs_int, const tjs_char *,
                                    iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error Operation(tjs_uint32, const tjs_char *, tjs_uint32 *,
                            tTJSVariant *, const tTJSVariant *,
                            iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error OperationByNum(tjs_uint32, tjs_int, tTJSVariant *,
                                 const tTJSVariant *,
                                 iTJSDispatch2 *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error ClassInstanceInfo(tjs_uint32, tjs_uint,
                                    tTJSVariant *) override {
            return TJS_E_NOTIMPL;
        }

        tjs_error Reserved2() override { return TJS_E_NOTIMPL; }

        tjs_error Reserved3() override { return TJS_E_NOTIMPL; }

    private:
        ~PSBValueDispatch() = default;

        static void assign(tTJSVariant *result, PSB::PSBRawOwner *owner,
                           const std::uint8_t *node) {
            // sub_59673C @ 0x59673C decodes scalars on demand and creates a
            // fresh owner-sharing dispatch only for list/dictionary nodes.
            if(result == nullptr) {
                return;
            }
            switch(node[0]) {
                case 0x01:
                case 0x23:
                case 0x24:
                case 0x25:
                case 0x26:
                case 0x3f:
                    result->Clear();
                    return;
                case 0x02:
                    *result = true;
                    return;
                case 0x03:
                    *result = false;
                    return;
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                case 0x08:
                case 0x09:
                case 0x0a:
                case 0x0b:
                case 0x0c:
                    *result = PSBRawNode::DecodeInteger(node);
                    return;
                case 0x15:
                case 0x16:
                case 0x17:
                case 0x18:
                case 0x2c:
                    *result = ttstr(owner->GetString(node));
                    return;
                case 0x19:
                case 0x1a:
                case 0x1b:
                case 0x1c:
                case 0x2d: {
                    const std::uint8_t *data{};
                    std::uint32_t size{};
                    data = owner->GetResource(node, size);
                    *result = tTJSVariant(data, size);
                    return;
                }
                case 0x1d:
                case 0x1e:
                case 0x1f:
                    *result = PSBRawNode::DecodeReal(node);
                    return;
                case 0x20:
                case 0x21: {
                    auto *dispatch =
                        new PSBValueDispatch(PSBRawNode(owner, node));
                    *result = tTJSVariant(dispatch, dispatch);
                    dispatch->Release();
                    return;
                }
                case 0x27:
                case 0x2f:
                case 0x33:
                case 0x37:
                case 0x3b:
                    TVPThrowExceptionMessage(
                        TJS_W("psb: can't convert value to bool."));
                case 0x28:
                case 0x29:
                case 0x30:
                case 0x31:
                case 0x34:
                case 0x35:
                case 0x38:
                case 0x39:
                case 0x3c:
                case 0x3d:
                    TVPThrowExceptionMessage(
                        TJS_W("psb: can't convert value to long int."));
                case 0x2e:
                case 0x41:
                    TVPThrowExceptionMessage(
                        TJS_W("psb: can't convert value to double."));
                default:
                    throwUnknownType();
            }
        }

        tjs_uint refCount_{ 1 };
        PSBRawNode value_;
        bool valid_{ true };
    };

} // namespace

// Android's typed NCB binding passes PSBFile::Load its variant argument by
// const reference.  This ncbind port lacks the corresponding borrow-only
// converter, so restore that converter without copying/refcounting the variant.
struct PSBVariantReferenceConvertor {
    void operator()(const tTJSVariant *&destination,
                    const tTJSVariant &source) const {
        destination = &source;
    }
};
NCB_SET_TOVALUE_CONVERTOR(const tTJSVariant *, PSBVariantReferenceConvertor);

namespace PSB {
    tjs_int32 GetPSBValueClassID() {
        // sub_596D90 @ 0x596D90 uses a zero-initialized integer and an
        // explicit lazy branch, without a C++ local-static guard.
        static tjs_int32 valueClassId{};
        if(valueClassId == 0) {
            valueClassId = TJS::TJSRegisterNativeClass(TJS_W("PSBValueClass"));
        }
        return valueClassId;
    }

    iTJSDispatch2 *CreatePSBValueDispatch(PSBRawNode value) {
        return value.GetOwner() != nullptr
            ? new PSBValueDispatch(std::move(value))
            : nullptr;
    }

    tTJSVariant CreatePSBValueVariant(PSBRawNode value) {
        iTJSDispatch2 *dispatch = CreatePSBValueDispatch(std::move(value));
        if(dispatch == nullptr) {
            return {};
        }
        tTJSVariant result(dispatch, dispatch);
        dispatch->Release();
        return result;
    }

    iTJSDispatch2 *PSBFile::GetRootDispatch() const {
        // sub_5981F8 @ 0x5981F8 is the typed root getter.  Returning the fresh
        // reference lets ncbind's dispatch convertor create the variant and
        // release that initial reference.
        return CreatePSBValueDispatch(GetRoot());
    }
} // namespace PSB

template <typename T>
class PSBFileConvertor {
    using ClassT = typename ncbTypeConvertor::Stripper<PSBFile>::Type;
    using AdaptorT = ncbInstanceAdaptor<ClassT>;

public:
    void operator()(T *&destination, const tTJSVariant &source) {
        if(source.Type() == tvtObject) {
            destination =
                AdaptorT::GetNativeInstance(source.AsObjectNoAddRef());
        }
    }

    void operator()(tTJSVariant &destination, const T *&source) {
        if(source == nullptr) {
            destination.Clear();
            return;
        }
        if(iTJSDispatch2 *object = AdaptorT::CreateAdaptor(source)) {
            destination = tTJSVariant(object, object);
            object->Release();
        }
    }
};

NCB_SET_CONVERTOR(PSBFile, PSBFileConvertor<PSBFile>);
NCB_SET_CONVERTOR(const PSBFile *, PSBFileConvertor<const PSBFile>);

static tjs_error PSBFileFactory(PSBFile **result, tjs_int count,
                                tTJSVariant **params, iTJSDispatch2 *) {
    // sub_5980F4 @ 0x5980F4: always construct an empty one-pointer holder and,
    // when present, forward only the first constructor argument to load().
    auto *file = new PSBFile();
    *result = file;
    if(count >= 1) {
        (void)file->Load(*params[0]);
    }
    return TJS_S_OK;
}

NCB_REGISTER_CLASS(PSBFile) {
    Factory(PSBFileFactory);
    Property(TJS_W("root"), &PSBFile::GetRootDispatch, 0);
    Method(TJS_W("load"), &PSBFile::Load);
}

NCB_PRE_REGIST_CALLBACK(initPsbFile);
