#include "xp3filter.h"

#include <cstdint>

#include "ncbind.hpp"

tjs_error CBinaryAccessor::OperationByNum(
    /* operation with member by index number */ tjs_uint32 flag,
    /* calling flag */ tjs_int num,
    /* index number */ tTJSVariant *result,
    /* result ( can be nullptr ) */ const tTJSVariant *param,
    /* parameters */ iTJSDispatch2 *objthis /* object as "this" */) {
    num += m_curPos;
    if(num < 0 || num >= m_length)
        return TJS_E_MEMBERNOTFOUND;
    unsigned char opnum = param->AsInteger();
    switch(flag & TJS_OP_MASK) {
        case TJS_OP_BAND:
            m_buff[num] &= opnum;
            break;
        case TJS_OP_BOR:
            m_buff[num] |= opnum;
            break;
        case TJS_OP_BXOR:
            m_buff[num] ^= opnum;
            break;
        case TJS_OP_SUB:
            m_buff[num] -= opnum;
            break;
        case TJS_OP_ADD:
            m_buff[num] += opnum;
            break;
        case TJS_OP_MOD:
            m_buff[num] %= opnum;
            break;
        case TJS_OP_DIV:
            m_buff[num] /= (signed char)opnum;
            break;
        case TJS_OP_IDIV:
            m_buff[num] /= opnum;
            break;
        case TJS_OP_MUL:
            m_buff[num] *= opnum;
            break;
        case TJS_OP_LOR:
            m_buff[num] = m_buff[num] || opnum;
            break;
        case TJS_OP_LAND:
            m_buff[num] = m_buff[num] && opnum;
            break;
        case TJS_OP_SAR:
            m_buff[num] >>= opnum;
            break;
        case TJS_OP_SAL:
            m_buff[num] <<= opnum;
            break;
        case TJS_OP_SR:
            m_buff[num] >>= opnum;
            break;
        case TJS_OP_INC:
            m_buff[num]++;
            break;
        case TJS_OP_DEC:
            m_buff[num]--;
            break;
        default:
            return TJS_E_NOTIMPL;
    }

    return TJS_S_OK;
}
tjs_error CBinaryAccessor::Operation(
    /* operation with member */ tjs_uint32 flag,
    /* calling flag */ const tjs_char *membername,
    /* member name ( nullptr for a default member ) */
    tjs_uint32 *hint,
    /* hint for the member name (in/out) */ tTJSVariant *result,
    /* result ( can be nullptr ) */ const tTJSVariant *param,
    /* parameters */ iTJSDispatch2 *objthis /* object as "this" */) {
    if(membername) {
        static const ttstr str_ptr(TJS_W("ptr"));
        if(hint) {
            static const tjs_uint32 hash_ptr =
                tTJSHashFunc<tjs_char *>::Make(str_ptr.c_str());
            if(!*hint)
                *hint = tTJSHashFunc<tjs_char *>::Make(membername);
            if(*hint != hash_ptr)
                return TJS_E_NOTIMPL;
        } else if(str_ptr != membername) {
            return TJS_E_NOTIMPL;
        }
    }
    tjs_uint32 op = flag & TJS_OP_MASK;
    switch(op) {
        case TJS_OP_ADD:
            m_curPos += param->AsInteger();
            break;
        case TJS_OP_SUB:
            m_curPos -= param->AsInteger();
            break;
        case TJS_OP_INC:
            ++m_curPos;
            break;
        case TJS_OP_DEC:
            --m_curPos;
            break;
        default:
            return TJS_E_NOTIMPL;
    }
    return TJS_S_OK;
}

tjs_error CBinaryAccessor::IsValid(
    /* get validation, returns TJS_S_TRUE (valid) or TJS_S_FALSE
       (invalid) */
    tjs_uint32 flag,
    /* calling flag */ const tjs_char *membername,
    /* member name ( nullptr for a default member ) */
    tjs_uint32 *hint,
    /* hint for the member name (in/out) */
    iTJSDispatch2 *objthis /* object as "this" */) {
    return m_buff ? TJS_S_TRUE : TJS_S_FALSE;
}

tjs_error CBinaryAccessor::Invalidate(
    /* invalidation */ tjs_uint32 flag,
    /* calling flag */ const tjs_char *membername,
    /* member name ( nullptr for a default member ) */
    tjs_uint32 *hint,
    /* hint for the member name (in/out) */
    iTJSDispatch2 *objthis /* object as "this" */) {
    m_buff = nullptr;
    return TJS_S_OK;
}

tjs_error CBinaryAccessor::GetCount(
    /* get member count */ tjs_int *result,
    /* variable that receives the result */
    const tjs_char *membername,
    /* member name ( nullptr for a default member ) */
    tjs_uint32 *hint,
    /* hint for the member name (in/out) */
    iTJSDispatch2 *objthis /* object as "this" */) {
    if(membername)
        return TJS_E_MEMBERNOTFOUND;
    *result = m_length;
    return TJS_S_OK;
}

tjs_error CBinaryAccessor::PropSetByNum(
    /* property set by index number */ tjs_uint32 flag,
    /* calling flag */ tjs_int num,
    /* index number */ const tTJSVariant *param,
    /* parameters */ iTJSDispatch2 *objthis /* object as "this" */) {
    num += m_curPos;
    if(num < 0 || num >= m_length)
        return TJS_E_MEMBERNOTFOUND;
    m_buff[num] = param->AsInteger();
    return TJS_S_OK;
}

tjs_error CBinaryAccessor::PropSet(
    /* property set */ tjs_uint32 flag,
    /* calling flag */ const tjs_char *membername,
    /* member name ( nullptr for a default member ) */
    tjs_uint32 *hint,
    /* hint for the member name (in/out) */ const tTJSVariant *param,
    /* parameters */ iTJSDispatch2 *objthis /* object as "this" */) {
    if(!membername)
        return TJS_E_NOTIMPL;
    if(!TJS_strcmp(membername, TJS_W("ptr"))) {
        m_curPos = param->AsInteger();
        return TJS_S_OK;
    }
    return TJS_E_NOTIMPL;
}

tjs_error CBinaryAccessor::PropGetByNum(
    /* property get by index number */ tjs_uint32 flag,
    /* calling flag */ tjs_int num,
    /* index number */ tTJSVariant *result,
    /* result */ iTJSDispatch2 *objthis /* object as "this" */) {
    num += m_curPos;
    if(num < 0 || num >= m_length)
        return TJS_E_MEMBERNOTFOUND;
    result->operator=((tjs_int32)m_buff[num]);
    return TJS_S_OK;
}

tjs_error CBinaryAccessor::PropGet(
    /* property get */ tjs_uint32 flag,
    /* calling flag */ const tjs_char *membername,
    /* member name ( nullptr for a default member ) */
    tjs_uint32 *hint,
    /* hint for the member name (in/out) */ tTJSVariant *result,
    /* result */ iTJSDispatch2 *objthis /* object as "this" */) {
    if(!membername)
        return TJS_E_NOTIMPL;
    if(!TJS_strcmp(membername, TJS_W("count"))) {
        result->operator=((tjs_int64)m_length);
    } else if(!TJS_strcmp(membername, TJS_W("ptr"))) {
        result->operator=((tjs_int64)m_curPos);
    } else {
        result->Clear();
    }
    return TJS_S_OK;
}

tjs_error CBinaryAccessor::FuncCall(
    /* function invocation */ tjs_uint32 flag,
    /* calling flag */ const tjs_char *membername,
    /* member name ( nullptr for a default member ) */
    tjs_uint32 *hint,
    /* hint for the member name (in/out) */ tTJSVariant *result,
    /* result */ tjs_int numparams,
    /* number of parameters */ tTJSVariant **param,
    /* parameters */ iTJSDispatch2 *objthis /* object as "this" */) {
    if(hint) {
        if(!*hint) {
            *hint = !TJS_strcmp(membername, TJS_W("xor"));
            if(*hint) {
                return FuncXor(numparams, param);
            }
        } else {
            return FuncXor(numparams, param);
        }
    } else if(!TJS_strcmp(membername, TJS_W("xor"))) {
        return FuncXor(numparams, param);
    }
    return TJS_E_NOTIMPL;
}

CBinaryAccessor::CBinaryAccessor(unsigned char *buff, unsigned int len) {
    m_buff = buff;
    m_length = len;
    m_curPos = 0;
}

tjs_error CBinaryAccessor::FuncAdd(tjs_int numparams, tTJSVariant **param) {
    if(numparams < 3) {
        return TJS_E_BADPARAMCOUNT;
    }

    int bufoff = param[0]->AsInteger();
    int len = param[1]->AsInteger();
    unsigned char xorval = param[2]->AsInteger();

    unsigned char *buf = m_buff + m_curPos + bufoff;
    unsigned int i;
    for(i = 0; i < len; ++i)
        buf[i] += xorval;
    return TJS_S_OK;
}

tjs_error CBinaryAccessor::FuncXor(tjs_int numparams, tTJSVariant **param) {
    if(numparams < 3) {
        return TJS_E_BADPARAMCOUNT;
    }

    int bufoff = param[0]->AsInteger();
    int len = param[1]->AsInteger();
    unsigned char xorval = param[2]->AsInteger();

    unsigned char *buf = m_buff + m_curPos + bufoff;
    unsigned char *pend = buf + len;
    if(len > 32) {
        int PreFragLen = (unsigned char *)((((intptr_t)buf) + 7) & ~7) - buf;
        for(int i = 0; i < PreFragLen; i++)
            *(buf++) ^= xorval;

        uint64_t k = /*0x101010101010101 * */ xorval;
        k |= k << 8;
        k |= k << 16;
        k |= k << 32;
        unsigned char *pVecEnd = (unsigned char *)(((intptr_t)pend) & ~7) - 7;
        while(buf < pVecEnd) {
            *(uint64_t *)(buf) ^= k;
            buf += 8;
        }
    }
    while(buf < pend)
        *(buf++) ^= xorval;

    return TJS_S_OK;
}
