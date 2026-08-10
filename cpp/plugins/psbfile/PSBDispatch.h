#pragma once

#include <cstdint>

#include "PSBRawFile.h"
#include "tjs.h"
#include "tjsNative.h"

namespace PSB {
    // The four reference constructors receive a one-pointer PSBFile holder
    // and a separate node pointer; every ABI preserves that factorization.
    // The per-binary function map belongs in the four-binary audit rather
    // than as unqualified addresses in compiled source comments.
    // valid_ is an independent byte, not part of the raw-node view. The four
    // member Invalidate functions clear only this byte and retain owner/node.
    class PSBValueDispatch final : public iTJSDispatch2,
                                   public iTJSNativeInstance {
    public:
        PSBValueDispatch(const PSBFile &file, const std::uint8_t *node);

        tjs_uint AddRef() override;

        tjs_uint Release() override;

        // Unsupported one-instruction vtable entries return TJS_E_NOTIMPL
        // unconditionally in all four references. Construct returns TJS_S_OK,
        // while the paired main/secondary native Invalidate and Destruct
        // entries are nullsubs; all differ from tTJSDispatch's
        // member-sensitive defaults.
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

        tTJSVariant *CreateVariant(tTJSVariant *result,
                                   const std::uint8_t *node);
        void decodeName_guess(std::string &name,
                              std::uint32_t nameIndex) const;
        [[nodiscard]] const char *
        getString_guess(const std::uint8_t *node) const;
        [[nodiscard]] const std::uint8_t *
        getResource_guess(const std::uint8_t *node,
                          std::uint32_t &size) const;

        tjs_uint refCount_{ 1 };
        PSBRawNode value_;
        bool valid_{ true };
    };
} // namespace PSB
