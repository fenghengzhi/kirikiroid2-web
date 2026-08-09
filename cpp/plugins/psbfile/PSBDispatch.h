#pragma once

#include <cstdint>

#include "PSBRawFile.h"
#include "tjs.h"
#include "tjsNative.h"

namespace PSB {
    // Raw collection dispatch reconstructed from sub_597AD4 @ 0x597AD4.
    // Its constructor has two distinct source arguments: X1 refers to the
    // one-pointer PSBFile holder and X2 supplies the node. Android exposes the
    // scalarized slot. Same-lineage iOS independently preserves one standalone
    // holder caller and two raw-node-first-subobject callers with a separate
    // node argument. A single PSBRawNode parameter remains ruled out.
    // valid_ is the independent byte toggled by sub_596F0C @ 0x596F0C.
    class PSBValueDispatch final : public iTJSDispatch2,
                                   public iTJSNativeInstance {
    public:
        PSBValueDispatch(const PSBFile &file, const std::uint8_t *node);

        tjs_uint AddRef() override;

        tjs_uint Release() override;

        // Unsupported one-instruction vtable entries in
        // 0x596D78..0x597A20 return TJS_E_NOTIMPL unconditionally.  Construct
        // returns TJS_S_OK, while the native Invalidate/Destruct slots are
        // nullsubs; all differ from tTJSDispatch's member-sensitive defaults.
        tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           tTJSVariant *, tjs_int, tTJSVariant **,
                           iTJSDispatch2 *) override;

        tjs_error FuncCallByNum(tjs_uint32, tjs_int, tTJSVariant *, tjs_int,
                                tTJSVariant **, iTJSDispatch2 *) override;

        tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                          tjs_uint32 *, tTJSVariant *result,
                          iTJSDispatch2 *) override;

        tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num,
                               tTJSVariant *result, iTJSDispatch2 *) override;

        tjs_error PropSet(tjs_uint32, const tjs_char *, tjs_uint32 *,
                          const tTJSVariant *, iTJSDispatch2 *) override;

        tjs_error PropSetByNum(tjs_uint32, tjs_int, const tTJSVariant *,
                               iTJSDispatch2 *) override;

        tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                           tjs_uint32 *, iTJSDispatch2 *) override;

        tjs_error GetCountByNum(tjs_int *, tjs_int, iTJSDispatch2 *) override;

        tjs_error PropSetByVS(tjs_uint32, tTJSVariantString *,
                              const tTJSVariant *, iTJSDispatch2 *) override;

        tjs_error IsInstanceOf(tjs_uint32, const tjs_char *membername,
                               tjs_uint32 *, const tjs_char *classname,
                               iTJSDispatch2 *) override;

        tjs_error EnumMembers(tjs_uint32 flag, tTJSVariantClosure *callback,
                              iTJSDispatch2 *) override;

        tjs_error DeleteMember(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               iTJSDispatch2 *) override;

        tjs_error DeleteMemberByNum(tjs_uint32, tjs_int,
                                    iTJSDispatch2 *) override;

        tjs_error NativeInstanceSupport(tjs_uint32 flag, tjs_int32 classid,
                                        iTJSNativeInstance **pointer) override;

        tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override;

        void Invalidate() override;

        void Destruct() override;

        tjs_error IsValid(tjs_uint32, const tjs_char *, tjs_uint32 *,
                          iTJSDispatch2 *) override;

        tjs_error Invalidate(tjs_uint32, const tjs_char *membername,
                             tjs_uint32 *, iTJSDispatch2 *) override;

        tjs_error InvalidateByNum(tjs_uint32, tjs_int,
                                  iTJSDispatch2 *) override;

        tjs_error IsValidByNum(tjs_uint32, tjs_int, iTJSDispatch2 *) override;

        tjs_error CreateNew(tjs_uint32, const tjs_char *, tjs_uint32 *,
                            iTJSDispatch2 **, tjs_int, tTJSVariant **,
                            iTJSDispatch2 *) override;

        tjs_error CreateNewByNum(tjs_uint32, tjs_int, iTJSDispatch2 **, tjs_int,
                                 tTJSVariant **, iTJSDispatch2 *) override;

        tjs_error Reserved1() override;

        tjs_error IsInstanceOfByNum(tjs_uint32, tjs_int, const tjs_char *,
                                    iTJSDispatch2 *) override;

        tjs_error Operation(tjs_uint32, const tjs_char *, tjs_uint32 *,
                            tTJSVariant *, const tTJSVariant *,
                            iTJSDispatch2 *) override;

        tjs_error OperationByNum(tjs_uint32, tjs_int, tTJSVariant *,
                                 const tTJSVariant *,
                                 iTJSDispatch2 *) override;

        tjs_error ClassInstanceInfo(tjs_uint32, tjs_uint,
                                    tTJSVariant *) override;

        tjs_error Reserved2() override;

        tjs_error Reserved3() override;

    private:
        ~PSBValueDispatch() = default;

        tTJSVariant *CreateVariant_guess(tTJSVariant *result,
                                         const std::uint8_t *node);
        void decodeName_guess(std::string &name,
                              std::uint32_t nameIndex) const; // 0x5975C0
        [[nodiscard]] const char *
        getString_guess(const std::uint8_t *node) const; // 0x596BC4
        [[nodiscard]] const std::uint8_t *
        getResource_guess(const std::uint8_t *node,
                          std::uint32_t &size) const; // 0x596C70

        tjs_uint refCount_{ 1 };
        PSBRawNode value_;
        bool valid_{ true };
    };
} // namespace PSB
