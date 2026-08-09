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
        // sub_597AD4 @ 0x597AD4 first dereferences X1 as an owner slot, then
        // copies that owner and performs AddRef; node arrives separately in
        // X2. iOS @0x1000EC248 independently preserves standalone-holder and
        // raw-node-first-subobject callers with the same factorization.
        value_(file, node) {}

    void PSBValueDispatch::decodeName_guess(
        std::string &name, std::uint32_t nameIndex) const {
        // sub_5975C0 @ 0x5975C0 is a distinct zero-xref member wrapper.  The
        // live EnumMembers path calls sub_597B1C directly and must not be
        // redirected through this otherwise-unused boundary.
        detail::DecodeName_guess(name, value_.GetOwner(), nameIndex);
    }

    const char *
    PSBValueDispatch::getString_guess(const std::uint8_t *node) const {
        // sub_596BC4 @ 0x596BC4 is the distinct dispatch member whose complete
        // body is inlined into CreateVariant @0x596834..0x596AB0.  Keep the
        // source member call even though no BL/xref survives the optimizer.
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
        // sub_596C70 @ 0x596C70 leaves size untouched when chunkData is null.
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
        // sub_597AC0 @ 0x597AC0.
        return ++refCount_;
    }

    tjs_uint PSBValueDispatch::Release() {
        // sub_597A40 @ 0x597A40.
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

    tjs_error PSBValueDispatch::PropGet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *, tTJSVariant *result,
                      iTJSDispatch2 *) {
        // sub_597854 @ 0x597854.
        if(membername == nullptr) {
            return TJS_E_NOTIMPL;
        }
        if(!valid_ || value_.GetOwner() == nullptr) {
            return TJS_E_INVALIDOBJECT;
        }

        // sub_597854 @ 0x597894 contains the complete inlined classifier
        // clone.  The same-source iOS build retains its call to the shared
        // classifier before selecting Array/Dictionary.
        switch(detail::GetTypeCategory_guess(value_.GetNode()[0])) {
            case 6:
                if(TJS_strcmp(membername, TJS_W("count")) == 0) {
                    // sub_597854 @ 0x5979F8 writes through result without
                    // a null guard, preserving the dispatch ABI boundary.
                    // The optimized Android body @0x597930..0x5979F4 expands
                    // this decoder; it has no shared count-helper BL boundary.
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
                    // PSBValueDispatch::PropGet @ 0x5979F8 calls the
                    // tjs_int32 Variant assignment (sub_A0FF28), whose
                    // SXTW preserves high-bit packed-count behavior.
                    *result = static_cast<tjs_int32>(count);
                    return TJS_S_OK;
                }
                break;
            case 7: {
                // sub_597854 @ 0x59795C constructs exactly one narrow holder;
                // the same Buf feeds both packed dictionary lookups and the
                // holder is destroyed on both the hit and miss paths.
                tTJSNarrowStringHolder key(membername);
                std::uint32_t nameIndex;
                if(detail::FindNameIndex_guess(
                       value_.GetOwner()->GetHeader()->names, key.Buf,
                       nameIndex)) {
                    std::uint32_t valueOffset;
                    if(detail::FindDictionaryValueOffset_guess(
                           value_.GetNode() + 1, nameIndex, valueOffset)) {
                        CreateVariant_guess(
                            result, value_.GetNode() + 1 + valueOffset);
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
        // sub_597854 @ 0x5978C0 clears the output unconditionally.
        result->Clear();
        return TJS_S_OK;
    }

    tjs_error PSBValueDispatch::PropGetByNum(tjs_uint32 flag, tjs_int num,
                           tTJSVariant *result, iTJSDispatch2 *) {
        // sub_5976C4 @ 0x5976C4.
        if(!valid_ || value_.GetOwner() == nullptr) {
            return TJS_E_INVALIDOBJECT;
        }
        // sub_5976C4 @ 0x5976DC contains the same classifier clone.  Its
        // same-source iOS counterpart retains the shared call and compares
        // the returned category with Array (6).
        if(detail::GetTypeCategory_guess(value_.GetNode()[0]) != 6) {
            return TJS_E_MEMBERNOTFOUND;
        }
        const std::uint8_t *packed = value_.GetNode() + 1;
        // sub_5976C4 @ 0x59770C..0x597778 expands the first count decoder.
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
        // 0x59777C..0x597780 masks/adds in W.  Preserve modulo-2^32
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
            // sub_5976C4 @ 0x5977C0 has the same unguarded output
            // boundary for a non-throwing out-of-range lookup.
            result->Clear();
            return TJS_S_OK;
        }
        // sub_5976C4 @ 0x5977AC repeats the packed-array construction only
        // after the bounds check. Its count decoder therefore still precedes
        // the element-tag read for the truncated-input first-fault boundary.
        const detail::PsbArray_guess offsets(packed);
        // 0x597800..0x597848 combines the complete table size and entry in W,
        // then adds the relative address to packed with SXTW.
        const std::uint32_t relativeOffset =
            offsets.nBytes + offsets[static_cast<std::uint32_t>(index)];
        CreateVariant_guess(
            result, packed + static_cast<std::int32_t>(relativeOffset));
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
        // sub_5975E0 @ 0x5975E0.
        if(!valid_ || value_.GetOwner() == nullptr) {
            return TJS_E_INVALIDOBJECT;
        }
        if(membername != nullptr) {
            return TJS_E_NOTIMPL;
        }
        // PSBValueDispatch_GetCount @ 0x59760C contains the category-6
        // specialized Android O3 inline classifier residual. Same-lineage
        // iOS arm64 @0x1000EC8E0 retains the shared call.
        // A classifier helper-return of -1 still maps to TJS_E_NOTIMPL.
        if(detail::GetTypeCategory_guess(value_.GetNode()[0]) != 6) {
            return TJS_E_NOTIMPL;
        }
        // sub_5975E0 @ 0x59763C..0x5976A4 expands this decoder at the
        // GetCount call site and maps an unknown count tag to zero.
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
        // sub_596E24 @ 0x596E24 intentionally does not consult valid_.
        if(membername != nullptr) {
            return TJS_E_NOTIMPL;
        }

        // sub_596E24 @ 0x596E3C..0x596EB4 contains the complete
        // category-specialized classifier residual. Same-lineage iOS arm64
        // @0x1000EC350 retains the shared classifier call.
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
        // PSBValueDispatch_EnumMembers @ 0x596F50.
        if(!valid_ || value_.GetOwner() == nullptr) {
            return TJS_E_INVALIDOBJECT;
        }
        // PSBValueDispatch_EnumMembers @ 0x596F98 contains the complete
        // Android O3 inline classifier clone. Same-lineage iOS arm64
        // @0x1000EC4AC retains the shared call. The
        // classifier's -1 continuation preserves @0x59749C if its throw
        // helper unexpectedly returns.
        int category =
            detail::GetTypeCategory_guess(value_.GetNode()[0]);

        tTJSVariant name;
        tTJSVariant memberFlags;
        tTJSVariant memberValue;
        // PSBValueDispatch::EnumMembers @ 0x596FF0..0x59701C defaults all
        // three member Variants before assigning flags through sub_A0FF28.
        memberFlags = static_cast<tjs_int32>(0);
        tTJSVariant callbackResult;
        tTJSVariant *params[3] = { &name, &memberFlags, &memberValue };
        const bool noValue = (flag & TJS_ENUM_NO_VALUE) != 0;

        // 0x596FF0..0x59701C constructs all four variants before the
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
                    CreateVariant_guess(
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
                // 0x597350..0x59735C assigns the decoded narrow buffer
                // directly; no ttstr temporary participates in this lifetime.
                name = key.c_str();
                if(!noValue) {
                    // PSBValueDispatch_EnumMembers @ 0x597388..0x59739C
                    // adds the table-end displacement and entry offset in W8,
                    // then reloads self->node after the preceding callback
                    // before adding the zero-extended wrapped offset and +1.
                    const std::uint32_t relativeOffset = keys.nBytes +
                        offsets.nBytes + offsets[index];
                    CreateVariant_guess(
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
        // sub_596D90 @ 0x596D90 lazily registers PSBValueClass and exposes
        // the embedded raw-node view only for GETINSTANCE.
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
        // sub_597A30 @ 0x597A30 is the main-vtable entry; sub_597A38 @
        // 0x597A38 is the secondary iTJSNativeInstance duplicate entry.
        return TJS_S_OK;
    }

    void PSBValueDispatch::Invalidate() {
        // nullsub_258 @ 0x596F38 is the main-vtable entry; nullsub_259 @
        // 0x596F3C is the secondary iTJSNativeInstance duplicate entry.
    }

    void PSBValueDispatch::Destruct() {
        // nullsub_260 @ 0x597A28 is the main-vtable entry; nullsub_261 @
        // 0x597A2C is the secondary iTJSNativeInstance duplicate entry.
    }

    tjs_error PSBValueDispatch::IsValid(tjs_uint32, const tjs_char *, tjs_uint32 *,
                      iTJSDispatch2 *) {
        // sub_596EF0 @ 0x596EF0.
        return valid_ ? TJS_S_TRUE : TJS_S_FALSE;
    }

    tjs_error PSBValueDispatch::Invalidate(tjs_uint32, const tjs_char *membername,
                         tjs_uint32 *, iTJSDispatch2 *) {
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

    tTJSVariant *PSBValueDispatch::CreateVariant_guess(
        tTJSVariant *result, const std::uint8_t *node) {
        // sub_59673C @ 0x59673C decodes scalars on demand and creates a
        // fresh owner-sharing dispatch only for list/dictionary nodes.
        // It is a dispatch member: owner reads come from this->value_
        // rather than an owner argument.  It also assumes a non-null
        // output and dereferences it on every normal value-producing tag;
        // an unknown-type throw helper that unexpectedly returns reaches the
        // common pointer return without dereferencing it.
        // Every normal branch returns that same output address through
        // X0 at 0x596B88.  Pointer versus reference source spelling is ABI-
        // indistinguishable; retaining the existing pointer parameter avoids
        // inventing a different caller shape.
        // sub_59673C's flattened raw-tag jump table is the Android -O3
        // residual of the same classifier used by sub_599554.  Restore the
        // source-level category call and keep every category-local default.
        switch(detail::GetTypeCategory_guess(node[0])) {
            case 0:
                result->Clear();
                break;
            case 1: {
                bool boolean = false;
                switch(node[0]) {
                    case 0x02:
                        boolean = true;
                        break;
                    case 0x03:
                        break;
                    default:
                        TVPThrowExceptionMessage(
                            TJS_W("psb: can't convert value to bool."));
                        break;
                }
                *result = boolean;
                break;
            }
            case 2: {
                // 0x5967B4..0x596A10 contains the complete inline copies of
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
                // 0x596A1C..0x596A38 is the category-constrained inline
                // residual of the same raw double decoder used by 0x5992E8.
                *result = detail::DecodeNumberAsDouble_guess(node);
                break;
            case 4: {
                // sub_59673C @ 0x596AB0 calls the narrow-string Variant
                // assignment directly, releasing the old result before the
                // new string allocation.  getString_guess @0x596BC4 is the
                // source-level member whose body the optimizer inlined here;
                // no ttstr temporary is introduced.
                *result = getString_guess(node);
                break;
            }
            case 5: {
                // sub_59673C leaves size uninitialized when chunkData is null.
                // getResource_guess @0x596C70 is the source-level member whose
                // complete body the Android optimizer inlined at
                // 0x59686C..0x596B5C.
                std::uint32_t size;
                const std::uint8_t *data = getResource_guess(node, size);
                // PSBValueDispatch_CreateVariant_guess @ 0x596B50..0x596B74 calls
                // TJSAllocVariantOctet_guess @ 0xA0E0F4; for non-null,
                // non-empty data it starts at ref=1.  Then
                // tTJSVariant_CopyRef_guess @ 0xA0FB64 retains it before
                // releasing the old result.  Destruction of the temporary
                // through 0xA0F778/0xA0F790 drops that extra reference,
                // leaving the copied result as the sole non-null Octet owner.
                // Android @0x596870..0x596978 never reads either packed table
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
                // 0x5968F8..0x596958 starts at ref=1, installs the same
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
                // If that helper unexpectedly returns -1, Android reaches the
                // common return without touching the existing result.
                break;
        }
        return result;
    }

    iTJSDispatch2 *PSBFile::GetRootDispatch() const {
        // sub_5981F8 @ 0x5981F8 checks the one-pointer holder before reading
        // the raw root, then constructs the dispatch directly.  It does not
        // call the lower-level unguarded sub_598A3C helper.
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
    // sub_5980F4 @ 0x5980F4: always construct an empty one-pointer holder and,
    // when present, forward only the first constructor argument to load().
    auto *file = new PSBFile();
    *result = file;
    try {
        if(count >= 1) {
            (void)file->Load(*params[0]);
        }
    } catch(...) {
        // sub_5980F4 @ 0x5981A0..0x5981EC destroys the already-published
        // native holder and rethrows without clearing the result slot.
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
