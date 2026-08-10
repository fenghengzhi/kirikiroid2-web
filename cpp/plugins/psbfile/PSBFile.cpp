#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

#include "MsgIntf.h"
#include "PSBMediaRegistry.h"
#include "PSBDispatch.h"
#include "PSBPackedInternal.h"
#include "PSBRawFile.h"
#include "ncbind.hpp"
#include "tjs.h"
#include "tjsNative.h"

#define NCB_MODULE_NAME TJS_W("PSBFile.dll")

using PSB::PSBFile;
using PSB::PSBRawNode;

namespace PSB {
    PSBValueDispatch::PSBValueDispatch(
        const PSBFile &file, const std::uint8_t *node) :
        // All four reference constructors copy/AddRef the standalone file
        // owner, store node separately, initialize refCount to one and set
        // valid to true. See the four-binary audit for the qualified map.
        value_(file, node) {}

    void PSBValueDispatch::decodeName_guess(
        std::string &name, std::uint32_t nameIndex) const {
        // Only the Android builds retain this member wrapper. Their live
        // EnumMembers paths call the shared decoder directly, while both iOS
        // builds emit no standalone wrapper boundary.
        detail::DecodeName_guess(name, value_.GetOwner(), nameIndex);
    }

    const char *
    PSBValueDispatch::getString_guess(const std::uint8_t *node) const {
        // All four references use the same tag-to-index decoder and packed
        // string-offset lookup. Android arm64 also inlines the complete body
        // into CreateVariant.
        const PSBRawHeader *header = value_.GetOwner()->GetHeader();
        const detail::PsbArray_guess offsets(header->strings);
        std::uint32_t index = 0;
        switch(node[0]) {
            case 0x15:
                index = node[1];
                break;
            case 0x16:
                index = detail::ReadUnaligned_guess<std::uint16_t>(node + 1);
                break;
            case 0x17:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node + 1) &
                    0xffffffu;
                break;
            case 0x18:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node + 1);
                break;
            default:
                break;
        }
        return reinterpret_cast<const char *>(
            header->stringsData + offsets[index]);
    }

    const std::uint8_t *PSBValueDispatch::getResource_guess(
        const std::uint8_t *node, std::uint32_t &size) const {
        // All four references decode the resource index before looking it up
        // in the packed offset/length tables. Each leaves size untouched when
        // chunkData is null.
        const PSBRawHeader *header = value_.GetOwner()->GetHeader();
        if(header->chunkData == nullptr) {
            return nullptr;
        }
        const detail::PsbArray_guess offsets(header->chunkOffsets);
        const detail::PsbArray_guess lengths(header->chunkLengths);
        std::uint32_t index = 0;
        switch(node[0]) {
            case 0x19:
                index = node[1];
                break;
            case 0x1a:
                index = detail::ReadUnaligned_guess<std::uint16_t>(node + 1);
                break;
            case 0x1b:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node + 1) &
                    0xffffffu;
                break;
            case 0x1c:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node + 1);
                break;
            default:
                break;
        }
        size = lengths[index];
        return header->chunkData + offsets[index];
    }

    tjs_uint PSBValueDispatch::AddRef() {
        // Every reference uses a plain non-atomic 32-bit pre-increment.
        return ++refCount_;
    }

    tjs_uint PSBValueDispatch::Release() {
        // Every reference uses a plain non-atomic 32-bit pre-decrement and
        // destroys the dispatch exactly when the result reaches zero.
        const tjs_uint count = --refCount_;
        if(count == 0) {
            delete this;
        }
        return count;
    }

    tjs_error PSBValueDispatch::FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *, tjs_int, tTJSVariant **,
                       iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::FuncCallByNum(tjs_uint32, tjs_int, tTJSVariant *, tjs_int,
                            tTJSVariant **, iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tTJSVariant *PSBValueDispatch::CreateVariant(
        tTJSVariant *result, const std::uint8_t *node) {
        // All four references decode scalars on demand and create a fresh
        // owner-sharing dispatch only for list/dictionary nodes.
        // It is a dispatch member: owner reads come from this->value_
        // rather than an owner argument.  It also assumes a non-null
        // output and dereferences it on every normal value-producing tag;
        // an unknown-type throw helper that unexpectedly returns reaches the
        // common pointer return without dereferencing it.
        // Every normal branch returns that same output address. Android folds
        // the category result into a raw-tag jump table; iOS retains the
        // classifier call. Pointer versus reference source spelling remains
        // ABI-indistinguishable.
        switch(detail::GetTypeCategory_guess(node[0])) {
            case 0:
                result->Clear();
                break;
            case 1: {
                // The iOS builds preserve the complete numeric-to-bool
                // decoder here. Android constant-propagates category 1 and
                // removes its unreachable numeric arms.
                *result = detail::DecodeNumberAsBoolean_guess(node);
                break;
            }
            case 2: {
                // Android arm64 contains complete inline copies of
                // the shared narrow/wide integer decoders.  The float/double
                // arms are source-level dead under category 2 but survive in
                // the Android body and therefore remain explicit here.
                tjs_int64 integer = 0;
                switch(node[0]) {
                    case 0x04:
                    case 0x05:
                    case 0x06:
                    case 0x07:
                    case 0x08:
                        integer = detail::DecodeInteger32_guess(node);
                        break;
                    case 0x09:
                    case 0x0a:
                    case 0x0b:
                    case 0x0c:
                        integer = detail::DecodeInteger64_guess(node);
                        break;
                    case 0x1d:
                        break;
                    case 0x1e:
                        integer = static_cast<tjs_int64>(
                            detail::ReadUnaligned_guess<float>(node + 1));
                        break;
                    case 0x1f:
                        integer = static_cast<tjs_int64>(
                            detail::ReadUnaligned_guess<double>(node + 1));
                        break;
                    default:
                        TVPThrowExceptionMessage(
                            TJS_W("psb: can't convert value to long int."));
                        break;
                }
                *result = integer;
                break;
            }
            case 3:
                // Android retains the category-constrained inline residual of
                // the same raw double decoder used by the public raw wrapper.
                *result = detail::DecodeNumberAsDouble_guess(node);
                break;
            case 4: {
                // Android arm64 calls the narrow-string Variant
                // assignment directly, releasing the old result before the
                // new string allocation. getString_guess is the
                // source-level member whose body the optimizer inlined here;
                // no ttstr temporary is introduced.
                *result = getString_guess(node);
                break;
            }
            case 5: {
                // All four leave size uninitialized when chunkData is null.
                // getResource_guess is the source-level member whose complete
                // body the Android arm64 optimizer inlines here.
                std::uint32_t size;
                const std::uint8_t *data = getResource_guess(node, size);
                // For non-null, non-empty data the temporary starts at ref=1.
                // Variant CopyRef retains it into result before the temporary
                // destructor drops its reference, leaving result as the sole
                // non-null Octet owner in every build.
                // Android arm64 never reads either packed table
                // or size on the null path.  Passing plain `size` here is C++
                // UB even though the inlined constructor ignores it when data
                // is null; current Clang -O3 exploited that UB and erased the
                // gate.  The zero is an unavoidable compiler-boundary operand,
                // not evidence that the original source initialized `size`.
                *result = tTJSVariant(data, data != nullptr ? size : 0);
                break;
            }
            case 6:
            case 7: {
                auto *dispatch =
                    new PSBValueDispatch(value_.GetFile_guess(), node);
                // All four start at ref=1, install the same
                // dispatch in Object and ObjThis (ref=3), CopyRefs both slots
                // into result (ref=5), destroys the temporary (ref=3), then
                // releases the construction reference below (ref=2).  Those
                // final two references belong exactly to result's closure.
                *result = tTJSVariant(dispatch, dispatch);
                dispatch->Release();
                break;
            }
            default:
                // The classifier already emitted the internal-type diagnostic.
                // If that helper unexpectedly returns -1, all four reach the
                // common return without touching the existing result.
                break;
        }
        // Both iOS builds retain this source assertion (PSBFile.cpp:400);
        // Android release flags compile it out.
        if(result->Type() == tvtObject) {
            assert(result->AsObjectNoAddRef());
        }
        return result;
    }

    tjs_error PSBValueDispatch::PropGetByNum(tjs_uint32 flag, tjs_int num,
                           tTJSVariant *result, iTJSDispatch2 *) {
        // The four references agree on the signed bounds behavior and the
        // 32-bit modulo arithmetic used to normalize a negative index.
        if(!valid_ || value_.GetOwner() == nullptr) {
            return TJS_E_INVALIDOBJECT;
        }
        // Android inlines the category-6 test; iOS retains the shared call.
        if(detail::GetTypeCategory_guess(value_.GetNode()[0]) != 6) {
            return TJS_E_MEMBERNOTFOUND;
        }
        const std::uint8_t *packed = value_.GetNode() + 1;
        // Every target expands this first count decoder at the call site.
        tjs_int signedCount;
        switch(packed[0]) {
            case 0x0d:
                signedCount = packed[1];
                break;
            case 0x0e:
                signedCount = detail::ReadUnaligned_guess<std::uint16_t>(
                    packed + 1);
                break;
            case 0x0f:
                signedCount = detail::ReadUnaligned_guess<std::uint32_t>(
                                  packed + 1) &
                    0xffffffu;
                break;
            case 0x10:
                signedCount = static_cast<tjs_int>(
                    detail::ReadUnaligned_guess<std::uint32_t>(packed + 1));
                break;
            default:
                signedCount = 0;
                break;
        }
        // All four mask/add in 32 bits. Preserve modulo-2^32
        // normalization before interpreting the result as signed for the
        // two bounds comparisons; signed C++ += would overflow for corrupt
        // counts such as 0x80000000.
        std::uint32_t indexBits = static_cast<std::uint32_t>(num);
        if(num < 0) {
            indexBits += static_cast<std::uint32_t>(signedCount);
        }
        std::int32_t index;
        std::memcpy(&index, &indexBits, sizeof(index));
        if(index < 0 || index >= signedCount) {
            if((flag & TJS_MEMBERMUSTEXIST) != 0) {
                return TJS_E_MEMBERNOTFOUND;
            }
            // The output boundary is unguarded for a non-throwing miss.
            result->Clear();
        } else {
            // Packed-array construction occurs only after bounds validation.
            const detail::PsbArray_guess offsets(packed);
            const std::uint32_t relativeOffset =
                offsets.nBytes + offsets[static_cast<std::uint32_t>(index)];
            CreateVariant(
                result, packed + static_cast<std::int32_t>(relativeOffset));
        }
        // Both iOS builds retain this caller-level postcondition in addition
        // to CreateVariant's own assertion; Android release strips it.
        if(result->Type() == tvtObject) {
            assert(result->AsObjectNoAddRef());
        }
        return TJS_S_OK;
    }

    tjs_error PSBValueDispatch::PropGet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *, tTJSVariant *result,
                      iTJSDispatch2 *) {
        // The four references agree on the ordering of the member-name,
        // validity, category and miss checks below.
        if(membername == nullptr) {
            return TJS_E_NOTIMPL;
        }
        if(!valid_ || value_.GetOwner() == nullptr) {
            return TJS_E_INVALIDOBJECT;
        }

        // Android inlines the classifier; iOS retains the shared call before
        // selecting Array/Dictionary.
        switch(detail::GetTypeCategory_guess(value_.GetNode()[0])) {
            case 6:
                if(TJS_strcmp(membername, TJS_W("count")) == 0) {
                    // Every target writes through result without a null guard.
                    // Android expands this decoder; iOS keeps the same cases.
                    const std::uint8_t *packed = value_.GetNode() + 1;
                    std::uint32_t count;
                    switch(packed[0]) {
                        case 0x0d:
                            count = packed[1];
                            break;
                        case 0x0e:
                            count = detail::ReadUnaligned_guess<std::uint16_t>(
                                packed + 1);
                            break;
                        case 0x0f:
                            count = detail::ReadUnaligned_guess<std::uint32_t>(
                                        packed + 1) &
                                0xffffffu;
                            break;
                        case 0x10:
                            count = detail::ReadUnaligned_guess<std::uint32_t>(
                                packed + 1);
                            break;
                        default:
                            count = 0;
                            break;
                    }
                    // Assignment is specifically tjs_int32, preserving the
                    // signed interpretation of a high-bit packed count.
                    *result = static_cast<tjs_int32>(count);
                    return TJS_S_OK;
                }
                break;
            case 7: {
                // Every target constructs exactly one narrow holder; the same
                // Buf feeds both lookups and is destroyed on hit and miss.
                tTJSNarrowStringHolder key(membername);
                std::uint32_t nameIndex;
                if(detail::FindNameIndex_guess(
                       value_.GetOwner()->GetHeader()->names, key.Buf,
                       nameIndex)) {
                    std::uint32_t valueOffset;
                    if(detail::FindDictionaryValueOffset_guess(
                           value_.GetNode() + 1, nameIndex, valueOffset)) {
                        CreateVariant(
                            result, value_.GetNode() + 1 + valueOffset);
                        // iOS retains this caller-level postcondition in
                        // addition to CreateVariant's own assertion.
                        if(result->Type() == tvtObject) {
                            assert(result->AsObjectNoAddRef());
                        }
                        return TJS_S_OK;
                    }
                }
                break;
            }
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            default:
                break;
        }

        if((flag & TJS_MEMBERMUSTEXIST) != 0) {
            return TJS_E_MEMBERNOTFOUND;
        }
        // A non-throwing miss clears the output unconditionally.
        result->Clear();
        return TJS_S_OK;
    }

    tjs_error PSBValueDispatch::PropSet(tjs_uint32, const tjs_char *, tjs_uint32 *,
                      const tTJSVariant *, iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::PropSetByNum(tjs_uint32, tjs_int, const tTJSVariant *,
                           iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::GetCount(tjs_int *result, const tjs_char *membername,
                       tjs_uint32 *, iTJSDispatch2 *) {
        // All four references expose the raw 32-bit packed count through the
        // signed tjs_int boundary without clamping high-bit values.
        if(!valid_ || value_.GetOwner() == nullptr) {
            return TJS_E_INVALIDOBJECT;
        }
        if(membername != nullptr) {
            return TJS_E_NOTIMPL;
        }
        // Android inlines the category-6 classifier residual; iOS retains the
        // shared call.
        // A classifier helper-return of -1 still maps to TJS_E_NOTIMPL.
        if(detail::GetTypeCategory_guess(value_.GetNode()[0]) != 6) {
            return TJS_E_NOTIMPL;
        }
        // Every target expands this decoder here and maps an unknown tag to 0.
        const std::uint8_t *packed = value_.GetNode() + 1;
        tjs_int count;
        switch(packed[0]) {
            case 0x0d:
                count = packed[1];
                break;
            case 0x0e:
                count = detail::ReadUnaligned_guess<std::uint16_t>(packed + 1);
                break;
            case 0x0f:
                count = detail::ReadUnaligned_guess<std::uint32_t>(packed + 1) &
                    0xffffffu;
                break;
            case 0x10:
                count = static_cast<tjs_int>(
                    detail::ReadUnaligned_guess<std::uint32_t>(packed + 1));
                break;
            default:
                count = 0;
                break;
        }
        *result = count;
        return TJS_S_OK;
    }

    tjs_error PSBValueDispatch::GetCountByNum(tjs_int *, tjs_int, iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::PropSetByVS(tjs_uint32, tTJSVariantString *,
                          const tTJSVariant *, iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::IsInstanceOf(tjs_uint32, const tjs_char *membername,
                           tjs_uint32 *, const tjs_char *classname,
                           iTJSDispatch2 *) {
        // All four references bypass valid_ and the owner slot here, and only
        // recognize the four object-like PSB categories below.
        if(membername != nullptr) {
            return TJS_E_NOTIMPL;
        }

        // Android inlines the classifier; iOS retains its shared call.
        const tjs_char *expected = nullptr;
        switch(detail::GetTypeCategory_guess(value_.GetType())) {
            case 4:
                expected = TJS_W("String");
                break;
            case 5:
                expected = TJS_W("Octet");
                break;
            case 6:
                expected = TJS_W("Array");
                break;
            case 7:
                expected = TJS_W("Dictionary");
                break;
            default:
                return TJS_S_FALSE;
        }
        return TJS_strcmp(classname, expected) == 0 ? TJS_S_TRUE
                                                    : TJS_S_FALSE;
    }

    tjs_error PSBValueDispatch::EnumMembers(tjs_uint32 flag, tTJSVariantClosure *callback,
                          iTJSDispatch2 *) {
        // All four references construct the output variant before the
        // category gate and ignore the callback's return value.
        if(!valid_ || value_.GetOwner() == nullptr) {
            return TJS_E_INVALIDOBJECT;
        }
        // Android inlines the classifier; iOS retains the shared call. A
        // classifier helper-return of -1 still reaches the non-container
        // TJS_E_NOTIMPL result.
        int category =
            detail::GetTypeCategory_guess(value_.GetNode()[0]);

        tTJSVariant name;
        tTJSVariant memberFlags;
        tTJSVariant memberValue;
        // All four default all three member Variants before assigning flags.
        memberFlags = static_cast<tjs_int32>(0);
        tTJSVariant callbackResult;
        tTJSVariant *params[3] = { &name, &memberFlags, &memberValue };
        const bool noValue = (flag & TJS_ENUM_NO_VALUE) != 0;

        // All four construct all variants before the
        // non-container return; their reverse destruction is observable
        // in the original object-lifetime chain.
        if(category != 6 && category != 7) {
            return TJS_E_NOTIMPL;
        }

        if(category == 6) {
            const std::uint8_t *packed = value_.GetNode() + 1;
            const detail::PsbArray_guess offsets(packed);
            const tjs_int count =
                static_cast<tjs_int>(offsets.nElementCount);
            for(tjs_int index = 0; index < count; ++index) {
                name = ttstr(index);
                if(!noValue) {
                    CreateVariant(
                        &memberValue,
                        packed + offsets.nBytes +
                            offsets[static_cast<std::uint32_t>(index)]);
                }
                callback->FuncCall(0, nullptr, nullptr, &callbackResult,
                                   noValue ? 2 : 3, params, this);
            }
        } else {
            const std::uint8_t *packed = value_.GetNode() + 1;
            const detail::PsbArray_guess keys(packed);
            const detail::PsbArray_guess offsets(packed + keys.nBytes);
            std::string key;
            for(std::uint32_t index = 0; index < keys.nElementCount; ++index) {
                detail::DecodeName_guess(key, value_.GetOwner(), keys[index]);
                // The decoded narrow buffer is assigned directly; no ttstr
                // temporary participates in this lifetime.
                name = key.c_str();
                if(!noValue) {
                    // Table-end displacement and entry offset are combined in
                    // uint32_t before reloading self->node after the callback.
                    const std::uint32_t relativeOffset = keys.nBytes +
                        offsets.nBytes + offsets[index];
                    CreateVariant(
                        &memberValue, value_.GetNode() + relativeOffset + 1);
                }
                callback->FuncCall(0, nullptr, nullptr, &callbackResult,
                                   noValue ? 2 : 3, params, this);
            }
        }
        return TJS_S_OK;
    }

    tjs_error PSBValueDispatch::DeleteMember(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::DeleteMemberByNum(tjs_uint32, tjs_int,
                                iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::NativeInstanceSupport(tjs_uint32 flag, tjs_int32 classid,
                                    iTJSNativeInstance **pointer) {
        // Every reference lazily registers PSBValueClass and exposes the
        // adjusted secondary iTJSNativeInstance base only for GETINSTANCE.
        if(flag != TJS_NIS_GETINSTANCE) {
            return TJS_E_NOTIMPL;
        }
        static tjs_int32 valueClassId{};
        if(valueClassId == 0) {
            valueClassId =
                TJS::TJSRegisterNativeClass(TJS_W("PSBValueClass"));
        }
        if(classid != valueClassId) {
            return TJS_E_FAIL;
        }
        *pointer = static_cast<iTJSNativeInstance *>(this);
        return TJS_S_OK;
    }

    tjs_error PSBValueDispatch::Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        // Both main/secondary vtable entries return zero unconditionally in
        // every reference.
        return TJS_S_OK;
    }

    void PSBValueDispatch::Invalidate() {
        // Both main/secondary native-instance entries are empty in every
        // reference.
    }

    void PSBValueDispatch::Destruct() {
        // Both main/secondary native-instance entries are empty in every
        // reference.
    }

    tjs_error PSBValueDispatch::IsValid(tjs_uint32, const tjs_char *, tjs_uint32 *,
                      iTJSDispatch2 *) {
        // Only valid_ is consulted in every reference.
        return valid_ ? TJS_S_TRUE : TJS_S_FALSE;
    }

    tjs_error PSBValueDispatch::Invalidate(tjs_uint32, const tjs_char *membername,
                         tjs_uint32 *, iTJSDispatch2 *) {
        // Every reference clears only valid_; owner and node remain retained
        // until final Release.
        if(membername != nullptr) {
            return TJS_E_NOTIMPL;
        }
        if(!valid_) {
            return TJS_E_INVALIDOBJECT;
        }
        valid_ = false;
        return TJS_S_OK;
    }

    tjs_error PSBValueDispatch::InvalidateByNum(tjs_uint32, tjs_int,
                              iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::IsValidByNum(tjs_uint32, tjs_int, iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::CreateNew(tjs_uint32, const tjs_char *, tjs_uint32 *,
                        iTJSDispatch2 **, tjs_int, tTJSVariant **,
                        iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::CreateNewByNum(tjs_uint32, tjs_int, iTJSDispatch2 **, tjs_int,
                             tTJSVariant **, iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::Reserved1() { return TJS_E_NOTIMPL; }

    tjs_error PSBValueDispatch::IsInstanceOfByNum(tjs_uint32, tjs_int, const tjs_char *,
                                iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::Operation(tjs_uint32, const tjs_char *, tjs_uint32 *,
                        tTJSVariant *, const tTJSVariant *,
                        iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::OperationByNum(tjs_uint32, tjs_int, tTJSVariant *,
                             const tTJSVariant *,
                             iTJSDispatch2 *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::ClassInstanceInfo(tjs_uint32, tjs_uint,
                                tTJSVariant *) {
        return TJS_E_NOTIMPL;
    }

    tjs_error PSBValueDispatch::Reserved2() { return TJS_E_NOTIMPL; }

    tjs_error PSBValueDispatch::Reserved3() { return TJS_E_NOTIMPL; }

    iTJSDispatch2 *PSBFile::GetRootDispatch() const {
        // All four check the one-pointer holder before reading the raw root,
        // then construct the dispatch directly and retain the owner once.
        PSBRawOwner *owner = GetOwner();
        if(owner == nullptr) {
            return nullptr;
        }
        // Returning the fresh reference lets ncbind's dispatch convertor
        // create the variant and release that initial reference.
        return new PSBValueDispatch(*this, owner->GetHeader()->entries);
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
    // All four construct and publish an empty one-pointer holder first and,
    // when present, copy only the first constructor argument into a temporary
    // variant and forward it to load().
    auto *file = new PSBFile();
    *result = file;
    try {
        if(count >= 1) {
            (void)file->Load(*params[0]);
        }
    } catch(...) {
        // Their DWARF/SJLJ landing pads all destroy the already-published
        // native holder and rethrow without clearing the result slot.
        delete file;
        throw;
    }
    return TJS_S_OK;
}

NCB_REGISTER_CLASS(PSBFile) {
    Factory(PSBFileFactory);
    Property(TJS_W("root"), &PSBFile::GetRootDispatch, 0);
    Method(TJS_W("load"), &PSBFile::Load);
}

NCB_PRE_REGIST_CALLBACK(initPsbFile);
