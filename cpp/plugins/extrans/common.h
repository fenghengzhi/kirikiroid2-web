//---------------------------------------------------------------------------
// extrans.dll — 共通 inline helper
//
// 移植自上游官方插件 krkrz/SamplePlugin/extrans/common.h（W.Dee）。
// libkrkr2.so 中 extrans.dll 是该上游插件被 kirikiroid2 内建化的产物。
// Clip / Blend / Swap_tjs_int 三个纯 inline helper 是平台无关的源码 token，
// 逐字保留。Win32 的 #include "tp_stub.h" 在内建化环境用 tjsCommHead 提供的
// tjs 基础类型（tjs_int / tjs_uint32）替代。
//
// analysis/extrans_port.md §0/§5 已确证同源。
//---------------------------------------------------------------------------
#ifndef commonH
#define commonH

#include "tjsCommHead.h"

//---------------------------------------------------------------------------
static inline bool Clip(tjs_int &l, tjs_int &r, tjs_int cl, tjs_int cr) {
    // 線分 l <-> r (l<r) を、 cl <-> cr (cl<cr) でクリッピングし、結果を l r に返す
    // クリッピングした結果、線分 l - r が残れば真、消滅すれば偽を返す
    if(l < cl)
        l = cl;
    if(r > cr)
        r = cr;
    if(l >= r)
        return false;
    return true;
}
//---------------------------------------------------------------------------
static inline tjs_uint32 Blend(tjs_uint32 a, tjs_uint32 b, tjs_int opa) {
    // a と b を混合比 opa で混合して返す ( opa = 0 ～ 255, 0 = a, 255 = b )
    tjs_uint32 ret;
    tjs_uint32 tmp;

    tmp = a & 0x000000ff;
    ret = 0x000000ff & (tmp + (((b & 0x000000ff) - tmp) * opa >> 8));
    tmp = a & 0x0000ff00;
    ret |= 0x0000ff00 & (tmp + (((b & 0x0000ff00) - tmp) * opa >> 8));
    tmp = a & 0x00ff0000;
    ret |= 0x00ff0000 & (tmp + (((b & 0x00ff0000) - tmp) * opa >> 8));
    tmp = a >> 24;
    ret |= (0x000000ff & (tmp + (((b >> 24) - tmp) * opa >> 8))) << 24;

    return ret;
}
//---------------------------------------------------------------------------
static inline void Swap_tjs_int(tjs_int &a, tjs_int &b) {
    // a と b を入れ替える
    tjs_int tmp = a;
    a = b;
    b = tmp;
}
//---------------------------------------------------------------------------

#endif
