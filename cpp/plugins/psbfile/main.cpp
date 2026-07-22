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

namespace PSB::detail {
    static void throwUnknownType() {
        TVPThrowExceptionMessage(
            TJS_W("psb: internal error: unknown internal type detected.\n"));
    }
} // namespace PSB::detail

namespace PSB {
    PSBValueDispatch::PSBValueDispatch(
        PSB::PSBRawOwner *const *ownerSlot, const std::uint8_t *node) :
        // sub_597AD4 @ 0x597AD4 first dereferences X1 as an owner slot, then
        // copies that owner and performs AddRef inside the constructor.
        value_(*ownerSlot, node) {}

    void PSBValueDispatch::decodeName_guess(
        std::string &name, std::uint32_t nameIndex) const {
        // sub_5975C0 @ 0x5975C0 is a distinct zero-xref member wrapper.  The
        // live EnumMembers path calls sub_597B1C directly and must not be
        // redirected through this otherwise-unused boundary.
        detail::DecodeName_guess(name, value_.GetOwner(), nameIndex);
    }

    const char *
    PSBValueDispatch::getString_guess(const std::uint8_t *node) const {
        // sub_596BC4 @ 0x596BC4 is a distinct dispatch member even though no
        // code xref survives in libkrkr2.so.  It reads owner from this+24.
        const PSBRawHeader *header = value_.GetOwner()->GetHeader();
        const detail::PackedArrayView_guess offsets(header->strings);
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
        const detail::PackedArrayView_guess offsets(header->chunkOffsets);
        const detail::PackedArrayView_guess lengths(header->chunkLengths);
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

        // sub_597854 @ 0x597894 reads the raw node tag and owns this
        // switch; GetTypeCategory @ 0x599554 is not in its call chain.
        switch(value_.GetNode()[0]) {
            case 0x20:
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
                    *result = static_cast<tjs_int64>(count);
                    return TJS_S_OK;
                }
                break;
            case 0x21: {
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
                        assign(result,
                               value_.GetNode() + 1 + valueOffset);
                        return TJS_S_OK;
                    }
                }
                break;
            }
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
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
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
            case 0x2c:
            case 0x2d:
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
                break;
            default:
                detail::throwUnknownType();
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
        // sub_5976C4 @ 0x5976DC performs its own raw-tag switch.
        switch(value_.GetNode()[0]) {
            case 0x20:
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
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x21:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2c:
            case 0x2d:
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
                return TJS_E_MEMBERNOTFOUND;
            default:
                detail::throwUnknownType();
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
        // sub_5976C4 @ 0x5977AC repeats the count-tag decoder after the
        // bounds check rather than carrying the first decoded count through.
        tjs_int elementCount;
        switch(packed[0]) {
            case 0x0d:
                elementCount = packed[1];
                break;
            case 0x0e:
                elementCount = detail::ReadUnaligned_guess<std::uint16_t>(
                    packed + 1);
                break;
            case 0x0f:
                elementCount = detail::ReadUnaligned_guess<std::uint32_t>(
                                   packed + 1) &
                    0xffffffu;
                break;
            case 0x10:
                elementCount = static_cast<tjs_int>(
                    detail::ReadUnaligned_guess<std::uint32_t>(packed + 1));
                break;
            default:
                elementCount = 0;
                break;
        }
        // 0x5977F0..0x5977F4 reads the element tag only after the second
        // count decoder, preserving the first-fault order for truncated data.
        const std::uint8_t valueTag =
            packed[static_cast<std::ptrdiff_t>(packed[0]) - 0x0b];
        const int width = static_cast<int>(valueTag) - 0x0c;
        const std::uint8_t *values =
            packed + static_cast<std::ptrdiff_t>(packed[0]) - 0x0a;
        // sub_5976C4 @ 0x597810..0x597814 multiplies in W and then
        // zero-extends that product for the entry-table address.
        const std::uint32_t entryOffset =
            static_cast<std::uint32_t>(index) *
            static_cast<std::uint32_t>(width);
        const std::uint32_t offset = detail::ReadPackedValue_guess(
            values + entryOffset, valueTag);
        // sub_5976C4 @ 0x597800..0x597848 forms this relative address in W9
        // (including 32-bit wraparound), then adds it to the packed-array base
        // with SXTW.  Do not zero-extend corrupt/high-bit packed offsets here.
        const std::uint32_t relativeOffset =
            static_cast<std::uint32_t>(packed[0]) - 0x0au +
            static_cast<std::uint32_t>(elementCount) *
                static_cast<std::uint32_t>(width) +
            offset;
        assign(result, packed + static_cast<std::int32_t>(relativeOffset));
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
        // sub_5975E0 @ 0x59760C likewise switches on the raw tag here.
        switch(value_.GetNode()[0]) {
            case 0x20:
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
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x21:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2c:
            case 0x2d:
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
                return TJS_E_NOTIMPL;
            default:
                detail::throwUnknownType();
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
                detail::throwUnknownType();
                // sub_596E24 @ 0x596EAC returns TJS_S_FALSE if the
                // exception helper unexpectedly returns.
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
        // PSBValueDispatch_EnumMembers @ 0x596F98 classifies the
        // raw tag in this function.  category is initialized to null's
        // category so the throw-helper-return boundary keeps value zero.
        int category = 0;
        switch(value_.GetNode()[0]) {
            case 0x01:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x3f:
                break;
            case 0x02:
            case 0x03:
            case 0x27:
            case 0x2f:
            case 0x33:
            case 0x37:
            case 0x3b:
                category = 1;
                break;
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
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
                category = 2;
                break;
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x2e:
            case 0x41:
                category = 3;
                break;
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x2c:
                category = 4;
                break;
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x2d:
                category = 5;
                break;
            case 0x20:
                category = 6;
                break;
            case 0x21:
                category = 7;
                break;
            default:
                detail::throwUnknownType();
                break;
        }

        tTJSVariant name;
        tTJSVariant memberFlags(static_cast<tjs_int>(0));
        tTJSVariant memberValue;
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
            const detail::PackedArrayView_guess offsets(value_.GetNode() + 1);
            const tjs_int count = static_cast<tjs_int>(offsets.count);
            for(tjs_int index = 0; index < count; ++index) {
                name = ttstr(index);
                if(!noValue) {
                    assign(
                        &memberValue,
                        offsets.end +
                            offsets[static_cast<std::uint32_t>(index)]);
                }
                callback->FuncCall(0, nullptr, nullptr, &callbackResult,
                                   noValue ? 2 : 3, params, this);
            }
        } else {
            const detail::PackedArrayView_guess keys(value_.GetNode() + 1);
            const detail::PackedArrayView_guess offsets(keys.end);
            std::string key;
            for(std::uint32_t index = 0; index < keys.count; ++index) {
                detail::DecodeName_guess(key, value_.GetOwner(), keys[index]);
                name = ttstr(key);
                if(!noValue) {
                    // PSBValueDispatch_EnumMembers @ 0x597388..0x59739C
                    // adds the table-end displacement and entry offset in W8,
                    // then zero-extends that wrapped value from node + 1.
                    const std::uint32_t relativeOffset =
                        static_cast<std::uint32_t>(
                            offsets.end - (value_.GetNode() + 1)) +
                        offsets[index];
                    assign(&memberValue,
                           value_.GetNode() + 1 + relativeOffset);
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
        // sub_597A38 @ 0x597A38.
        return TJS_S_OK;
    }

    void PSBValueDispatch::Invalidate() {
        // nullsub_259 @ 0x596F3C.
    }

    void PSBValueDispatch::Destruct() {
        // nullsub_261 @ 0x597A2C.
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

    void PSBValueDispatch::assign(tTJSVariant *result, const std::uint8_t *node) {
        // sub_59673C @ 0x59673C decodes scalars on demand and creates a
        // fresh owner-sharing dispatch only for list/dictionary nodes.
        // It is a dispatch member: owner reads come from this->value_
        // rather than an owner argument.  It also assumes a non-null
        // output and dereferences it on every tag.
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
            case 0x0c: {
                // sub_59673C @ 0x5967B4..0x596A10 owns this nested integer
                // decoder; it does not call the raw-node decoder at 0x5992E8.
                tjs_int64 integer = 0;
                switch(node[0]) {
                    case 0x04:
                        break;
                    case 0x05:
                        integer = static_cast<std::int8_t>(node[1]);
                        break;
                    case 0x06:
                        integer =
                            detail::ReadUnaligned_guess<std::int16_t>(node + 1);
                        break;
                    case 0x07: {
                        const std::uint32_t raw =
                            detail::ReadUnaligned_guess<std::uint16_t>(
                                node + 1) |
                            (static_cast<std::uint32_t>(node[3]) << 16);
                        integer = static_cast<std::int32_t>(raw << 8) >> 8;
                        break;
                    }
                    case 0x08:
                        integer =
                            detail::ReadUnaligned_guess<std::int32_t>(node + 1);
                        break;
                    case 0x09: {
                        std::uint64_t raw =
                            detail::ReadUnaligned_guess<std::uint32_t>(
                                node + 1) |
                            (static_cast<std::uint64_t>(node[5]) << 32);
                        if((node[5] & 0x80u) != 0) {
                            raw |= 0xffffff0000000000ull;
                        }
                        integer = static_cast<tjs_int64>(raw);
                        break;
                    }
                    case 0x0a: {
                        std::uint64_t raw =
                            detail::ReadUnaligned_guess<std::uint32_t>(
                                node + 1) |
                            (static_cast<std::uint64_t>(
                                 detail::ReadUnaligned_guess<std::uint16_t>(
                                     node + 5))
                             << 32);
                        if((node[6] & 0x80u) != 0) {
                            raw |= 0xffff000000000000ull;
                        }
                        integer = static_cast<tjs_int64>(raw);
                        break;
                    }
                    case 0x0b:
                        // The Android decoder deliberately leaves its
                        // seven-byte integer zero-extended.
                        integer = static_cast<tjs_int64>(
                            detail::ReadUnaligned_guess<std::uint32_t>(
                                node + 1) |
                            (static_cast<std::uint64_t>(
                                 detail::ReadUnaligned_guess<std::uint16_t>(
                                     node + 5))
                             << 32) |
                            (static_cast<std::uint64_t>(node[7]) << 48));
                        break;
                    case 0x0c:
                        integer =
                            detail::ReadUnaligned_guess<tjs_int64>(node + 1);
                        break;
                    default:
                        break;
                }
                *result = integer;
                return;
            }
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x2c: {
                const PSBRawHeader *header = value_.GetOwner()->GetHeader();
                const detail::PackedArrayView_guess offsets(header->strings);
                std::uint32_t index = 0;
                switch(node[0]) {
                    case 0x15:
                        index = node[1];
                        break;
                    case 0x16:
                        index = detail::ReadUnaligned_guess<std::uint16_t>(
                            node + 1);
                        break;
                    case 0x17:
                        index = detail::ReadUnaligned_guess<std::uint32_t>(
                                    node + 1) &
                            0xffffffu;
                        break;
                    case 0x18:
                        index = detail::ReadUnaligned_guess<std::uint32_t>(
                            node + 1);
                        break;
                    default:
                        break;
                }
                *result = ttstr(reinterpret_cast<const char *>(
                    header->stringsData + offsets[index]));
                return;
            }
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x2d: {
                // sub_59673C leaves size uninitialized when chunkData is
                // null; tTJSVariant's null-octet constructor does not consume
                // it.  Preserve that source-level boundary.
                const PSBRawHeader *header = value_.GetOwner()->GetHeader();
                const std::uint8_t *data;
                std::uint32_t size;
                if(header->chunkData != nullptr) {
                    const detail::PackedArrayView_guess offsets(
                        header->chunkOffsets);
                    const detail::PackedArrayView_guess lengths(
                        header->chunkLengths);
                    std::uint32_t index = 0;
                    switch(node[0]) {
                        case 0x19:
                            index = node[1];
                            break;
                        case 0x1a:
                            index =
                                detail::ReadUnaligned_guess<std::uint16_t>(
                                    node + 1);
                            break;
                        case 0x1b:
                            index =
                                detail::ReadUnaligned_guess<std::uint32_t>(
                                    node + 1) &
                                0xffffffu;
                            break;
                        case 0x1c:
                            index =
                                detail::ReadUnaligned_guess<std::uint32_t>(
                                    node + 1);
                            break;
                        default:
                            break;
                    }
                    data = header->chunkData + offsets[index];
                    size = lengths[index];
                } else {
                    data = nullptr;
                }
                *result = tTJSVariant(data, size);
                return;
            }
            case 0x1d:
                *result = static_cast<tjs_real>(0.0);
                return;
            case 0x1e:
                *result = static_cast<tjs_real>(
                    detail::ReadUnaligned_guess<float>(node + 1));
                return;
            case 0x1f:
                *result = detail::ReadUnaligned_guess<double>(node + 1);
                return;
            case 0x20:
            case 0x21: {
                auto *dispatch = new PSBValueDispatch(
                    value_.GetOwnerSlotAddress_guess(), node);
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
                // sub_59673C @ 0x5968C8..0x5968D4 preserves the
                // helper-return continuation as Boolean false.
                *result = false;
                return;
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
                // sub_59673C @ 0x59698C..0x596A18 preserves the
                // helper-return continuation as Integer zero.
                *result = static_cast<tjs_int64>(0);
                return;
            case 0x2e:
            case 0x41:
                TVPThrowExceptionMessage(
                    TJS_W("psb: can't convert value to double."));
                // sub_59673C @ 0x59696C..0x596A38 preserves the
                // helper-return continuation as Real zero.
                *result = static_cast<tjs_real>(0.0);
                return;
            default:
                detail::throwUnknownType();
                return;
        }
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
        return new PSBValueDispatch(GetOwnerSlotAddress_guess(),
                                    owner->GetHeader()->entries);
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
