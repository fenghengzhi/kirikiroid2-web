//---------------------------------------------------------------------------
// extrans.dll — 'ripple' （波紋）置換マップ転場
//
// 実現基底 = 上游 krkrz/SamplePlugin/extrans/ripple.cpp（W.Dee）。
// 差分裁判 = libkrkr2.so（kirikiroid2/ARM64）反編譯。一致処は上游源码原様
// （変数名 / 制御流 / 容器 / 波形・置換マップ計算）を逐字保留、分岐処は
// 二進を権威とし注釈に函数地址を引用。analysis/extrans_port.md §11→ripple 節。
//
// 二進地址索引：
//   RegisterRippleTransHandlerProvider   sub_7C7490 @0x7C7490
//   Provider::GetName                    sub_7C7920 @0x7C7920  → L"ripple"
//   Provider::StartTransition            sub_7C7938 @0x7C7938
//   Handler::ctor                        sub_7C4FB0 @0x7C4FB0  （TVPGetRippleTable 内联）
//   Handler vtable                       off_1A25898
//   Handler::StartProcess                sub_7C5288 @0x7C5288
//   Handler::EndProcess                  sub_7C53B8 @0x7C53B8
//   Handler::Process                     sub_7C53CC @0x7C53CC  （軟件 / GPU 双路径）
//   Handler::MakeFinalImage              sub_7C76C4 @0x7C76C4  → *dest=src2
//   tTVPRippleTable::MakeTable           sub_7C47B0 @0x7C47B0  （運行期建表）
//   C 標量 transform _c_f                sub_7C7824 @0x7C7824  （forward, *map++）
//   C 標量 transform _c_b                sub_7C77A4 @0x7C77A4  （backward, *map--）
//   能力門 CPU 累加器                    sub_9162C4 @0x9162C4  = TVPGetCPUType()
//   能力門 空例程                        nullsub_216..219 @0x7C78AC..7C78B8
//
// kirikiroid2 delta（上游 Win32 相対）：
//   D1. 能力門（重点）：Register 内 cputype=TVPGetCPUType() で位門選択——上游
//       TVPInitRippleTransformFuncs と同構造だが、ARM build では MMX(0x20000)/
//       EMMX(0x400000) の最適化扫描函数が **nullsub（空函数）** に編まれている
//       （x86 MMX/EMMX/SSE2 は ARM に存在しないため）。SSE2(0x800000) 分岐は
//       二進に無し（上游にはあるが ARM build で消去）。函数指针の選択構造を
//       忠実復刻し、選ばれる最適化例程は nullsub の場合 no-op + 注釈で明記。
//       既定値は C 標量版 _c_f / _c_b（二進 off_1AA59E0 / off_1AA59D8[0] の静的初値）。
//   D2. Process 双路径分発（IsSoftware()）：軟件逐行 scanline blend vs
//       GPU OperateRect + 内嵌 GLSL fragment shader（render-method 名は二進で
//       字面 "MosaicTrans"——kirikiroid2 作者のコピペ痕、shader 本体は ripple の
//       GetOffset 涙波位移。二進権威に従い名前は "MosaicTrans" を保持）。
//   D3. scanline 経 texture（GetTextureForRender()->GetScanLineForWrite /
//       GetTexture()->GetScanLineForRead）、上游の直接 iTVPScanLineProvider 取得を置換。
//   注：ripple は色字段を持たない（wave/turn の R/B swizzle delta は無し）。
//   注：上游の MMX/EMMX/SSE2 inline-asm transform（_mmx_f/_emmx_f/_sse2_f 等）は
//       ARM build に存在しない（x86 専用）ため移植せず、C 標量版 + 能力門 nullsub
//       構造のみ復刻する。デバッグ log TVPAddLog(L"ripple update count : ") は
//       上游 #ifdef 内で、二進にも文字列引用無し（全二進 0 匹配確認）→ 復刻せず。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <math.h>
#include <string.h>  // memmove

#include "ripple.h"
#include "common.h"
#include "transhandler.h"
#include "TransIntf.h"
#include "RenderManager.h"        // iTVPRenderManager / iTVPTexture2D / OperateRect
#include "DetectCPU.h"            // TVPGetCPUType
#include "cpu_types.h"            // TVP_CPU_HAS_MMX / _EMMX / _SSE2
#include "MsgIntf.h"              // TVPThrowExceptionMessage

//---------------------------------------------------------------------------
/*
    '波紋' トランジション
    置換マップによる、波紋が広がっていくような感じのトランジション
    このトランジションは転送先がαを持っていると(要するにトランジションを行う
    レイヤの type が ltOpaque 以外の場合)、正常に透過情報を処理できないので
    注意
*/
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#define TVP_RIPPLE_DIR_PREC 32
    // テーブル内で１象限中(90°)の方向をいくつに分割するか
    // (2 の累乗で 256 まで。大きくするとメモリを食う)
#define TVP_RIPPLE_DRIFT_PREC 4
    // drift 1 ピクセルをいくつに分割するか
//---------------------------------------------------------------------------
#ifndef M_PI
    #define M_PI (3.14159263589793238462)
#endif
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
/*
    いくつか テーブルを管理するクラス/関数群
    テーブルは、中心座標、トランジション画像のサイズ、
    波の幅、波紋の縦/横比、揺れの幅が前回と変わらない限り再生成はされない。
    再生成にはすこし時間がかかるため、4つまでキャッシュを行うことができる。
*/
//---------------------------------------------------------------------------
class tTVPRippleTable {
    tjs_int RefCount; // 参照カウンタ

    tjs_int Width; // トランジション画像の幅
    tjs_int Height; // トランジション画像の高さ

    tjs_int CenterX; // 波紋の中心 X 座標
    tjs_int CenterY; // 波紋の中心 Y 座標

    tjs_int RippleWidth; // 波紋の幅
    float Roundness; // 波紋の縦/横比
    tjs_int MaxDrift; // 揺れの最大幅

    tjs_int MapWidth; // 置換マップの幅
    tjs_int MapHeight; // 置換マップの高さ

    tjs_uint16 *DisplaceMap; // [位置]->[方向,距離] 置換マップ
    tjs_uint16 *DriftMap; // [揺れの大きさ,方向,距離]->[ずれ] 置換マップ

public:
    tjs_int GetWidth() const { return Width; }
    tjs_int GetHeight() const { return Height; }

    tjs_int GetCenterX() const { return CenterX; }
    tjs_int GetCenterY() const { return CenterY; }

    tjs_int GetRippleWidth() const { return RippleWidth; }
    float GetRoundness() const { return Roundness; }
    tjs_int GetMaxDrift() const { return MaxDrift; }

    tjs_int GetMapWidth() const { return MapWidth; }
    tjs_int GetMapHeight() const { return MapHeight; }

public:
    // Aligned with libkrkr2.so tTVPRippleTable ctor (inline @0x7C4FB0 + sub_7C47B0)
    tTVPRippleTable(tjs_int width, tjs_int height, tjs_int centerx,
                    tjs_int centery, tjs_int ripplewidth, float roundness,
                    tjs_int maxdrift) {
        RefCount = 1;

        DisplaceMap = NULL;
        DriftMap = NULL;

        Width = width;
        Height = height;
        CenterX = centerx;
        CenterY = centery;
        RippleWidth = ripplewidth;
        Roundness = roundness;
        MaxDrift = maxdrift;

        MakeTable();
    }

protected:
    ~tTVPRippleTable() { Clear(); }

public:
    void AddRef() { RefCount++; }

    void Release() {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
    }

public:
    const tjs_uint16 *GetDisplaceMap(tjs_int x, tjs_int y) const {
        return DisplaceMap + x + y * MapWidth;
    }

    const tjs_uint16 *GetDriftMap(tjs_int drift, tjs_int phase) {
        return DriftMap + drift * RippleWidth * (2 * TVP_RIPPLE_DIR_PREC) +
            phase * TVP_RIPPLE_DIR_PREC;
    }

private:
    void MakeTable();
    void Clear();
};
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so 波形生成（MakeTable @0x7C47B0 内、GLSL の
// "sin(rad) + sin(rad*2-2)*0.2/1.19" と同形）
float inline TVPRippleWaveForm(float rad) {
    // 波を生成する関数
    // 適当に。s は正にしかならないが見た目が良いのでこれでいく
    float s = (sin(rad) + sin(rad * 2 - 2) * 0.2) / 1.19;
    s *= s;
    return s;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPRippleTable::MakeTable @0x7C47B0
// 二進確認：運行期計算（new uint16[] + atan/sqrt 二重ループ + cos/sin 定点
// テーブル）。常量 DIR_PREC=32 / DRIFT_PREC=4 / 2048.0 定点 / -2π 二進一致。
void tTVPRippleTable::MakeTable() {
    tjs_int32 *rippleform = NULL;
    tjs_int32 *cos_table = NULL;
    tjs_int32 *sin_table = NULL;

    try {
        // MapWidth, MapHeight の計算
        // Width, Height を CenterX, CenterY で分割する４つの象限のうち
        // もっとも大きい物のサイズを MapWidth, MapHeight とする
        MapWidth = CenterX < (Width >> 1) ? Width - CenterX : CenterX;
        MapHeight = CenterY < (Height >> 1) ? Height - CenterY : CenterY;

        // DisplaceMap メモリ確保
        DisplaceMap = new tjs_uint16[MapWidth * MapHeight];

        // DisplaceMap 計算
        // 置換マップは１象限についてのみ計算する(他の象限は対称だから)
        tjs_uint16 *rmp = DisplaceMap;
        tjs_int ripplemask = RippleWidth - 1;
        tjs_int x, y;
        for(y = 0; y < MapHeight; y++) {
            float yy = ((float)y + 0.5) * Roundness;
            float fac = 1.0 / yy;
            for(x = 0; x < MapWidth; x++) {
                float xx = (float)x + 0.5;

                tjs_int dir =
                    atan(xx * fac) * ((1.0 / (M_PI / 2.0)) * TVP_RIPPLE_DIR_PREC);
                // dir = 方向コード

                tjs_int dist = (int)sqrt(xx * xx + yy * yy) & ripplemask;
                // dist = 中心からの距離

                *(rmp++) = (tjs_uint16)((dist * TVP_RIPPLE_DIR_PREC) + dir);
            }
        }

        // DriftMap メモリ確保
        // DriftMap に使用するメモリ量は
        // MaxDrift*TVP_RIPPLE_DRIFT_PREC * RippleWidth * 2 * TVP_RIPPLE_DIR_PREC *sizeof(tjs_uint16)
        // *2 が入っているのは 画像演算中に & でマスクをかける必要がないように
        DriftMap = new tjs_uint16[MaxDrift * TVP_RIPPLE_DRIFT_PREC * RippleWidth *
                                  2 * TVP_RIPPLE_DIR_PREC];

        // 波形の計算
        float rcp_rw = 1.0 / (float)RippleWidth;
        rippleform = new tjs_int32[RippleWidth];
        tjs_int w;
        for(w = 0; w < RippleWidth; w++) {
            // 適当に波っぽく見える波形(単純なsin波でもよい)
            float rad = (float)w * rcp_rw * (M_PI * -2.0);

            float s = TVPRippleWaveForm(rad);

            if(s < -1.0)
                s = -1.0;
            if(s > 1.0)
                s = 1.0;
            s *= 2048.0;
            rippleform[w] = (tjs_int32)(s < 0 ? s - 0.5 : s + 0.5); // 1.11
        }

        // sin/cos テーブルの生成
        cos_table = new tjs_int32[TVP_RIPPLE_DIR_PREC];
        sin_table = new tjs_int32[TVP_RIPPLE_DIR_PREC];
        for(w = 0; w < TVP_RIPPLE_DIR_PREC; w++) {
            float fdir = M_PI * 0.5 -
                (((float)w + 0.5) *
                 ((1.0 / (float)TVP_RIPPLE_DIR_PREC) * (M_PI / 2.0)));
            float v;
            v = cos(fdir) * 2048.0;
            cos_table[w] = (tjs_int32)(v < 0 ? v - 0.5 : v + 0.5); // 1.11
            v = sin(fdir) * 2048.0;
            sin_table[w] = (tjs_int32)(v < 0 ? v - 0.5 : v + 0.5); // 1.11
        }

        // DriftMap 計算
        // float で計算するとエラく遅いので固定小数点で計算する
        tjs_int drift, dir;
        tjs_int ripplewidth_step = RippleWidth * TVP_RIPPLE_DIR_PREC;
        for(drift = 0; drift < MaxDrift * TVP_RIPPLE_DRIFT_PREC; drift++) {
            tjs_int32 fdrift = (drift << 10) / TVP_RIPPLE_DRIFT_PREC; // 8.10
            tjs_uint16 *dmp =
                DriftMap + drift * RippleWidth * (2 * TVP_RIPPLE_DIR_PREC);
            for(w = 0; w < RippleWidth; w++) {
                tjs_int32 fd = rippleform[w] * fdrift >> 10; // 8.11
                for(dir = 0; dir < TVP_RIPPLE_DIR_PREC; dir++) {
                    tjs_int32 xd = cos_table[dir] * fd >> 11; // 8.11
                    tjs_int32 yd = sin_table[dir] * fd >> 11; // 8.11

                    tjs_uint16 val =
                        (tjs_uint16)(((int)(char)(int)(xd >> 11) << 8) +
                                     (int)(char)(int)(yd >> 11));

                    dmp[w * TVP_RIPPLE_DIR_PREC + dir] =
                        dmp[w * TVP_RIPPLE_DIR_PREC + ripplewidth_step + dir] =
                            val;
                }
            }
        }
    } catch(...) {
        Clear();
        if(rippleform)
            delete[] rippleform;
        if(sin_table)
            delete[] sin_table;
        if(cos_table)
            delete[] cos_table;
        throw;
    }
    if(rippleform)
        delete[] rippleform;
    if(sin_table)
        delete[] sin_table;
    if(cos_table)
        delete[] cos_table;
}
//---------------------------------------------------------------------------
void tTVPRippleTable::Clear() {
    if(DisplaceMap)
        delete[] DisplaceMap, DisplaceMap = NULL;
    if(DriftMap)
        delete[] DriftMap, DriftMap = NULL;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// キャッシュ管理
// Aligned with libkrkr2.so：二進 ctor @0x7C4FB0 内に TVPGetRippleTable が
// 内联され、2 本の OWORD ペア（xmmword_1AD90C8 / xmmword_1AD90D8 = 各 2 ポインタ
// = 計 4 槽）+ memmove LRU として展開されている。これは上游 4 槽キャッシュ配列 +
// memmove の編譯器内联展開。本地は上游源码構造（4 槽配列 + TVPGetRippleTable）を
// 忠実復刻する。
//---------------------------------------------------------------------------
#define TVP_RIPPLE_TABLE_MAX_CACHE 4
//---------------------------------------------------------------------------
static tTVPRippleTable *TVPRippleTableCache[TVP_RIPPLE_TABLE_MAX_CACHE] = { NULL };
//---------------------------------------------------------------------------
static tTVPRippleTable *
TVPGetRippleTable(tjs_int width, tjs_int height, tjs_int centerx,
                  tjs_int centery, tjs_int ripplewidth, float roundness,
                  tjs_int maxdrift) {
    // キャッシュの中から指定された条件のデータを取ってくる
    // あればキャッシュ中での優先順位を最上位にして返し、
    // そうでなければデータを作成してキャッシュの最後のデータを削除し、
    // 優先順位の先頭に挿入して返す

    // キャッシュ中にあるか
    tjs_int i;
    for(i = 0; i < TVP_RIPPLE_TABLE_MAX_CACHE; i++) {
        tTVPRippleTable *table = TVPRippleTableCache[i];
        if(!table)
            continue;

        if(table->GetWidth() == width && table->GetHeight() == height &&
           table->GetCenterX() == centerx && table->GetCenterY() == centery &&
           table->GetRippleWidth() == ripplewidth &&
           table->GetRoundness() == roundness &&
           table->GetMaxDrift() == maxdrift) {
            // キャッシュ中に見つかった

            // リストの先頭にもってくる
            if(i != 0) {
                memmove(TVPRippleTableCache + 1, TVPRippleTableCache,
                        i * sizeof(tTVPRippleTable *));
                TVPRippleTableCache[0] = table;
            }

            // 参照カウンタをインクリメントして返す
            table->AddRef();
            return table;
        }
    }

    // キャッシュ中には見つからなかった

    // 最後の要素を削除
    if(TVPRippleTableCache[TVP_RIPPLE_TABLE_MAX_CACHE - 1] != NULL) {
        tTVPRippleTable *table =
            TVPRippleTableCache[TVP_RIPPLE_TABLE_MAX_CACHE - 1];
        TVPRippleTableCache[TVP_RIPPLE_TABLE_MAX_CACHE - 1] = NULL;
        table->Release();
    }

    // データを作成
    tTVPRippleTable *table = new tTVPRippleTable(
        width, height, centerx, centery, ripplewidth, roundness, maxdrift);

    // リストの先頭に挿入
    memmove(TVPRippleTableCache + 1, TVPRippleTableCache,
            (TVP_RIPPLE_TABLE_MAX_CACHE - 1) * sizeof(tTVPRippleTable *));
    TVPRippleTableCache[0] = table;
    table->AddRef();

    // 返す
    return table;
}
//---------------------------------------------------------------------------
static void TVPInitRippleTableCache() {
    // キャッシュの初期化（二進 Register @0x7C7490 開頭 xmmword_1AD90C8/D8 = 0）
    tjs_int i;
    for(i = 0; i < TVP_RIPPLE_TABLE_MAX_CACHE; i++) {
        TVPRippleTableCache[i] = NULL;
    }
}
//---------------------------------------------------------------------------
static void TVPClearRippleTableCache() {
    // キャッシュをクリア
    tjs_int i;
    for(i = 0; i < TVP_RIPPLE_TABLE_MAX_CACHE; i++) {
        tTVPRippleTable *table = TVPRippleTableCache[i];
        TVPRippleTableCache[i] = NULL;
        if(table)
            table->Release();
    }
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// 演算関数群 (TVPRippleTransform_????) は、
// ・置換マップテーブルを正方向に見ていくか逆方向に見ていくか (_f _b サフィックス)
// ・置換マップの y を正にとるか負にとるか (_a _d サフィックス)
// からなる。kirikiroid2/ARM build では C 標量版 (_c_f / _c_b) のみが実在し、
// 上游の MMX/EMMX/SSE2 inline-asm 版は x86 専用のため存在しない。
//---------------------------------------------------------------------------
#define TVP_RIPPLE_BLEND                                                        \
    {                                                                          \
        tjs_uint32 s1, s2, s1_;                                                \
        s1 = *(const tjs_uint32 *)(src1 + ofs);                               \
        s2 = *(const tjs_uint32 *)(src2 + ofs);                               \
        s1_ = s1 & 0xff00ff;                                                   \
        s1_ = (s1_ + (((s2 & 0xff00ff) - s1_) * ratio >> 8)) & 0xff00ff;       \
        s2 &= 0xff00;                                                          \
        s1 &= 0xff00;                                                          \
        dest[i] = s1_ | ((s1 + ((s2 - s1) * ratio >> 8)) & 0xff00);            \
    }
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so sub_7C7824 @0x7C7824（forward, *displacemap++ = _c_f）
static void TVPRippleTransform_c_f(const tjs_uint16 *displacemap,
                                   const tjs_uint16 *driftmap, tjs_uint32 *dest,
                                   tjs_int num, tjs_int pitch,
                                   const tjs_uint8 *src1, const tjs_uint8 *src2,
                                   tjs_int ratio) {
    for(int i = 0; i < num; i++) {
        tjs_int n = driftmap[displacemap[i]];
        tjs_intptr_t ofs =
            (int)((i - (int)(char)(n >> 8)) * sizeof(tjs_uint32)) +
            (int)(char)(n)*pitch;
        TVP_RIPPLE_BLEND
    }
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so sub_7C77A4 @0x7C77A4（backward, *displacemap-- = _c_b）
static void TVPRippleTransform_c_b(const tjs_uint16 *displacemap,
                                   const tjs_uint16 *driftmap, tjs_uint32 *dest,
                                   tjs_int num, tjs_int pitch,
                                   const tjs_uint8 *src1, const tjs_uint8 *src2,
                                   tjs_int ratio) {
    for(int i = 0; i < num; i++) {
        tjs_int n = driftmap[*(displacemap--)];
        tjs_intptr_t ofs =
            (int)((i + (int)(char)(n >> 8)) * sizeof(tjs_uint32)) +
            (int)(char)(n)*pitch;
        TVP_RIPPLE_BLEND
    }
}
//---------------------------------------------------------------------------
typedef void (*tTVPRippleTransformFunc)(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int ratio);
// Aligned with libkrkr2.so off_1AA59E0 / off_1AA59D8[0]（既定 = C 標量版）。
// 二進 Process @0x7C53CC では中段大塊の forward 区段が off_1AA59E0、
// backward 区段が off_1AA59D8[0] を呼ぶ。
static tTVPRippleTransformFunc TVPRippleTransform_f = TVPRippleTransform_c_f;
static tTVPRippleTransformFunc TVPRippleTransform_b = TVPRippleTransform_c_b;
//---------------------------------------------------------------------------
// 能力門で選ばれる最適化扫描例程（二進 nullsub_216..219 @0x7C78AC..7C78B8）。
// ARM build では MMX/EMMX 最適化版が空函数に編まれている（x86 MMX/EMMX/SSE2 は
// ARM に存在しないため最適化路径が消去され no-op になっている）。能力門の構造を
// 忠実復刻するため空例程を保持する（D1）。
// Aligned with libkrkr2.so nullsub_216 @0x7C78AC（MMX forward, off_1AA59E0）
static void TVPRippleTransform_nullsub_216(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int ratio) {
    // no-op（MMX 最適化 forward 扫描——ARM build で空に編まれている）
}
// Aligned with libkrkr2.so nullsub_217 @0x7C78B0（MMX backward, off_1AA59D8[0]）
static void TVPRippleTransform_nullsub_217(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int ratio) {
    // no-op（MMX 最適化 backward 扫描——ARM build で空に編まれている）
}
// Aligned with libkrkr2.so nullsub_218 @0x7C78B4（EMMX forward, off_1AA59E0）
static void TVPRippleTransform_nullsub_218(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int ratio) {
    // no-op（EMMX 最適化 forward 扫描——ARM build で空に編まれている）
}
// Aligned with libkrkr2.so nullsub_219 @0x7C78B8（EMMX backward, off_1AA59D8[0]）
static void TVPRippleTransform_nullsub_219(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int ratio) {
    // no-op（EMMX 最適化 backward 扫描——ARM build で空に編まれている）
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so RegisterRippleTransHandlerProvider 内の能力門
// @0x7C74C8〜0x7C74E8。cputype = TVPGetCPUType()（二進 sub_9162C4）で位門選択。
// 二進反汇编字面（auditor 三重交叉確認）：
//   if (cputype & MMX)  { off_1AA59E0(=f)=nullsub_216; off_1AA59D8[0](=b)=nullsub_217;
//      if (cputype & EMMX) { off_1AA59E0(=f)=nullsub_218; off_1AA59D8[0](=b)=nullsub_219; } }
// off_1AA59E0=forward(_c_f)、off_1AA59D8[0]=backward(_c_b)（既定 sub_7C7824 / sub_7C77A4）。
// 上游 TVPInitRippleTransformFuncs と同構造（ただし上游は SSE2 分岐も持つが
// ARM build には無いため復刻せず）。選ばれる最適化版は二進では空例程（D1）。
static void TVPInitRippleTransformFuncs() {
    tjs_uint32 cputype = TVPGetCPUType();
    if(cputype & TVP_CPU_HAS_MMX) {
        // MMX が使用可能な場合（二進では nullsub に置換 = no-op）
        TVPRippleTransform_f = TVPRippleTransform_nullsub_216; // 二進 forward←216
        TVPRippleTransform_b = TVPRippleTransform_nullsub_217; // 二進 backward←217

        if(cputype & TVP_CPU_HAS_EMMX) {
            // MMX/EMMX が使用可能な場合（二進では nullsub に置換 = no-op）
            TVPRippleTransform_f = TVPRippleTransform_nullsub_218; // 二進 forward←218
            TVPRippleTransform_b = TVPRippleTransform_nullsub_219; // 二進 backward←219
        }
    }
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// 上下左右を折り返しながら画面外を参照しないように慎重に転送する C 関数
// (_e サフィックス) 4 個。二進 Process @0x7C53CC では循環体内に内联展開
// されているが、上游源码では独立函数として存在するため源码 token として復刻する。
// 折返公式 2*srcwidth-1-x / 2*srcheight-1-y は二進の 2*v57-1-x /
// 2*v56+0x3FFFFFFF-y（= 2*h-1-y、0x3FFFFFFF は +1 折込みの編譯器表現）と一致。
//---------------------------------------------------------------------------
#define TVP_RIPPLE_TURN_BORDER                                                  \
    {                                                                          \
        if(x < 0)                                                              \
            x = -x;                                                            \
        if(y < 0)                                                              \
            y = -y;                                                            \
        if(x >= srcwidth)                                                      \
            x = srcwidth - 1 - (x - srcwidth);                                 \
        if(y >= srcheight)                                                     \
            y = srcheight - 1 - (y - srcheight);                               \
    }
#define TVP_RIPPLE_CALC_OFS                                                     \
    tjs_intptr_t ofs = x * sizeof(tjs_uint32) + y * pitch;
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so Process inline forward/y正 @0x7C5C10〜（_f_a_e）
static void TVPRippleTransform_f_a_e(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int srcx, tjs_int srcy, tjs_int srcwidth, tjs_int srcheight,
    tjs_int ratio) {
    for(int i = 0; i < num; i++) {
        tjs_int n = driftmap[displacemap[i]];
        tjs_int x = srcx + i - (int)(char)(n >> 8);
        tjs_int y = srcy + (int)(char)n;
        TVP_RIPPLE_TURN_BORDER
        TVP_RIPPLE_CALC_OFS
        TVP_RIPPLE_BLEND
    }
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so Process inline forward/y負 @0x7C5E90〜（_f_d_e）
static void TVPRippleTransform_f_d_e(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int srcx, tjs_int srcy, tjs_int srcwidth, tjs_int srcheight,
    tjs_int ratio) {
    for(int i = 0; i < num; i++) {
        tjs_int n = driftmap[displacemap[i]];
        tjs_int x = srcx + i - (int)(char)(n >> 8);
        tjs_int y = srcy - (int)(char)n;
        TVP_RIPPLE_TURN_BORDER
        TVP_RIPPLE_CALC_OFS
        TVP_RIPPLE_BLEND
    }
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so Process inline backward/y正 @0x7C5708〜（_b_a_e）
static void TVPRippleTransform_b_a_e(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int srcx, tjs_int srcy, tjs_int srcwidth, tjs_int srcheight,
    tjs_int ratio) {
    for(int i = 0; i < num; i++) {
        tjs_int n = driftmap[*(displacemap--)];
        tjs_int x = srcx + i + (int)(char)(n >> 8);
        tjs_int y = srcy + (int)(char)n;
        TVP_RIPPLE_TURN_BORDER
        TVP_RIPPLE_CALC_OFS
        TVP_RIPPLE_BLEND
    }
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so Process inline backward/y負 @0x7C57DC〜（_b_d_e）
static void TVPRippleTransform_b_d_e(
    const tjs_uint16 *displacemap, const tjs_uint16 *driftmap, tjs_uint32 *dest,
    tjs_int num, tjs_int pitch, const tjs_uint8 *src1, const tjs_uint8 *src2,
    tjs_int srcx, tjs_int srcy, tjs_int srcwidth, tjs_int srcheight,
    tjs_int ratio) {
    for(int i = 0; i < num; i++) {
        tjs_int n = driftmap[*(displacemap--)];
        tjs_int x = srcx + i + (int)(char)(n >> 8);
        tjs_int y = srcy - (int)(char)n;
        TVP_RIPPLE_TURN_BORDER
        TVP_RIPPLE_CALC_OFS
        TVP_RIPPLE_BLEND
    }
}
//---------------------------------------------------------------------------
#undef TVP_RIPPLE_CALC_OFS
#undef TVP_RIPPLE_TURN_BORDER
#undef TVP_RIPPLE_BLEND
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// GPU 路径 GLSL fragment shader（D2、Aligned with libkrkr2.so Process @0x7C5FCC
// の sub_84B074("MosaicTrans", <下記 GLSL>, 2, 0)）。render-method 名は二進で
// 字面 "MosaicTrans"（作者コピペ痕）だが shader 本体は ripple の涙波位移。
//---------------------------------------------------------------------------
static const char *const TVPRippleTransGLSL =
    "uniform float opa;\n"
    "uniform float roundness;\n"
    "uniform float rwidth;\n"
    "uniform float phase;\n"
    "uniform float drift;\n"
    "uniform vec2 center;\n"
    "vec2 GetOffset(vec2 d) {\n"
    "    d.y *= roundness;\n"
    "    float dist = sqrt(d.x * d.x + d.y * d.y) + phase;\n"
    "    float rad = dist / rwidth * (-2.0 * 3.14159265);\n"
    "    float t = sin(rad) + sin(rad * 2.0 - 2.0) * 0.2 / 1.19;\n"
    "    dist = clamp(t * t, 0.0, 1.0) * drift;\n"
    "    float dir = atan(d.y, d.x);\n"
    "    return vec2(dist * cos(dir), dist * sin(dir));\n"
    "}void main() {\n"
    "    vec2 offset = GetOffset(v_texCoord0 - center);    vec4 s = "
    "texture2D(tex0, v_texCoord0 - offset);\n"
    "    vec4 d = texture2D(tex1, v_texCoord1 - offset);\n"
    "    gl_FragColor = mix(s.rgba, d.rgba, opa);\n"
    "}";
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTVPRippleTransHandler
//---------------------------------------------------------------------------
class tTVPRippleTransHandler : public iTVPDivisibleTransHandler {
    //	'波紋' トランジションハンドラクラスの実装

    tjs_int RefCount; // 参照カウンタ

protected:
    tjs_uint64 StartTick; // トランジションを開始した tick count
    tjs_uint64 Time; // トランジションに要する時間
    tTVPLayerType LayerType; // レイヤのタイプ
    tjs_int Width; // 処理する画像の幅
    tjs_int Height; // 処理する画像の高さ
    tjs_int64 CurTime; // 現在の tick count
    tjs_int BlendRatio; // ブレンド比
    tjs_int Phase; // 位相
    tjs_int Drift; // 揺れ
    bool First; // 一番最初の呼び出しかどうか

    tjs_int DriftCarePixels; // 周囲の折り返しに注意しなければならないピクセル数

    tjs_int CenterX; // 中心 X 座標
    tjs_int CenterY; // 中心 Y 座標
    tjs_int RippleWidth; // 波紋の幅 (16, 32, 64, 128 のいずれか)
    float Roundness; // 波紋の縦/横比
    float Speed; // 波紋の動く角速度
    tjs_int MaxDrift; // 揺れの最大幅(ピクセル単位) (127まで)

    const tjs_uint16 *CurDriftMap; // 現在描画中の DirftMap

    tTVPRippleTable *Table; // 置換マップなどのテーブル

public:
    // Aligned with libkrkr2.so Handler ctor @0x7C4FB0（StartTransition 内 new）
    tTVPRippleTransHandler(tjs_uint64 time, tTVPLayerType layertype,
                           tjs_int width, tjs_int height, tjs_int centerx,
                           tjs_int centery, tjs_int ripplewidth, float roundness,
                           float speed, tjs_int maxdrift) {
        RefCount = 1;

        LayerType = layertype;
        Width = width;
        Height = height;
        Time = time;

        CenterX = centerx;
        CenterY = centery;

        RippleWidth = ripplewidth;

        Roundness = roundness;
        Speed = speed;

        First = true;

        MaxDrift = maxdrift;

        // 二進 ctor は StartTick / CurTime / BlendRatio / Phase / Drift /
        // DriftCarePixels / CurDriftMap を初期化しない（忠実復刻、0 補填せず）。

        Table = TVPGetRippleTable(Width, Height, CenterX, CenterY, RippleWidth,
                                  Roundness, MaxDrift);
    }

    // Aligned with libkrkr2.so Handler dtor @0x7C7738
    virtual ~tTVPRippleTransHandler() {
        // 二進では TVPAddLog(L"ripple update count") は #ifdef 内で消去済み。
        Table->Release();
    }

    // Aligned with libkrkr2.so Handler::AddRef @0x7C7670
    tjs_error AddRef() override {
        RefCount++;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Handler::Release @0x7C7684
    tjs_error Release() override {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Handler::SetOption @0x7C76BC (no-op)
    tjs_error SetOption(
        /*in*/ iTVPSimpleOptionProvider *options) override {
        return TJS_S_OK;
    }

    tjs_error StartProcess(tjs_uint64 tick) override;

    tjs_error EndProcess() override;

    tjs_error Process(tTVPDivisibleData *data) override;

    // Aligned with libkrkr2.so Handler::MakeFinalImage @0x7C76C4
    tjs_error MakeFinalImage(
        iTVPScanLineProvider **dest, iTVPScanLineProvider *src1,
        iTVPScanLineProvider *src2) override {
        *dest = src2; // 常に最終画像は src2
        return TJS_S_OK;
    }
};
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPRippleTransHandler::StartProcess @0x7C5288
tjs_error tTVPRippleTransHandler::StartProcess(tjs_uint64 tick) {
    // トランジションの画面更新一回ごとに呼ばれる

    if(First) {
        // 最初の実行
        First = false;
        StartTick = tick;
    }

    // 画像演算に必要な各パラメータを計算
    CurTime = (tick - StartTick);

    // BlendRatio
    BlendRatio = CurTime * 255 / Time;
    if(BlendRatio > 255)
        BlendRatio = 255;

    // Phase
    // 角速度が Speed (rad/sec) で与えられている
    // 二進 0.000159154943 = 1/(2π) 確認
    Phase = (int)(Speed * ((1.0 / (M_PI * 2)) * (1.0 / 1000.0)) * CurTime *
                  RippleWidth) %
        RippleWidth;
    if(Phase < 0)
        Phase = 0;
    Phase = RippleWidth - Phase - 1;

    // Drift
    // 二進 vcvts_n_s32_f32(sin(...)*MaxDrift, 2) = (int)(...*MaxDrift*4)
    float s = sin(M_PI * CurTime / Time);
    Drift = (int)(s * MaxDrift * TVP_RIPPLE_DRIFT_PREC);
    if(Drift < 0)
        Drift = 0;
    if(Drift >= MaxDrift * TVP_RIPPLE_DRIFT_PREC)
        Drift = MaxDrift * TVP_RIPPLE_DRIFT_PREC - 1;

    DriftCarePixels = (int)(Drift / TVP_RIPPLE_DRIFT_PREC) + 1;
    if(DriftCarePixels & 1)
        DriftCarePixels++; // 一応偶数にアライン

    // CurDriftMap
    CurDriftMap = Table->GetDriftMap(Drift, Phase);

    return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPRippleTransHandler::EndProcess @0x7C53B8
tjs_error tTVPRippleTransHandler::EndProcess() {
    if(BlendRatio == 255)
        return TJS_S_FALSE; // トランジション終了

    return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPRippleTransHandler::Process @0x7C53CC
tjs_error tTVPRippleTransHandler::Process(tTVPDivisibleData *data) {
    // トランジションの各領域ごとに呼ばれる
    //
    // 二進: if ( (TVPIsSoftwareRenderer_guess() & 1) == 0 ) → GPU 路径
    //                                                  else → 软件 scanline 路径
    if(!TVPGetRenderManager()->IsSoftware()) {
        // ---- GPU OperateRect 路径（二進 0x7C5408〜、render-method "MosaicTrans"）----
        // 涙波位移は fragment shader 内 GetOffset() で per-pixel に行う。
        iTVPTexture2D *src1tex = data->Src1->GetTexture();
        float texw = (float)src1tex->GetWidth();  // 二進 v213 = Src1->[vtbl+24]()
        float texh = (float)src1tex->GetHeight(); // 二進 v214 = Src1->[vtbl+32]()

        iTVPRenderMethod *method =
            TVPGetRenderManager()->GetOrCompileRenderMethod(
                "MosaicTrans", nullptr, TVPRippleTransGLSL, 2, 0);

        // uniform 灌入（二進 0x7C6058〜0x7C6318）
        int opa_id = method->EnumParameterID("opa");
        int center_id = method->EnumParameterID("center");
        int roundness_id = method->EnumParameterID("roundness");
        int rwidth_id = method->EnumParameterID("rwidth");
        int drift_id = method->EnumParameterID("drift");
        int phase_id = method->EnumParameterID("phase");

        method->SetParameterFloat(opa_id, (float)BlendRatio / 255.0f);
        float center[2] = { (float)CenterX / texw, (float)CenterY / texh };
        method->SetParameterFloatArray(center_id, center, 2);
        method->SetParameterFloat(roundness_id, (float)(Roundness * texh) / texw);
        method->SetParameterFloat(rwidth_id, (float)RippleWidth / texw);
        method->SetParameterFloat(drift_id, (float)Drift / texw);
        // 二進 GPU phase は -1/(2π)（軟件 StartProcess の +1/(2π) と符号逆）。
        float phase = (float)(((double)RippleWidth +
                               (double)Speed * -0.000159154943 *
                                   (double)CurTime * (double)RippleWidth) /
                              texw);
        method->SetParameterFloat(phase_id, phase);

        // OperateRect（二進 0x7C63CC、rm->[vtbl+160]、CrossFade::Blend と同形）
        tRenderTexRectArray::Element src_tex[] = {
            tRenderTexRectArray::Element(
                data->Src1->GetTexture(),
                tTVPRect(data->Src1Left, data->Src1Top,
                         data->Src1Left + data->Width,
                         data->Src1Top + data->Height)),
            tRenderTexRectArray::Element(
                data->Src2->GetTexture(),
                tTVPRect(data->Src2Left, data->Src2Top,
                         data->Src2Left + data->Width,
                         data->Src2Top + data->Height))
        };
        TVPGetRenderManager()->OperateRect(
            method, data->Dest->GetTextureForRender(), nullptr,
            tTVPRect(data->DestLeft, data->DestTop,
                     data->DestLeft + data->Width,
                     data->DestTop + data->Height),
            tRenderTexRectArray(src_tex));

        return TJS_S_OK;
    }

    // ---- 软件 scanline 路径（二進 0x7C5410〜、上游逐行算法を texture-based に）----

    // 変数の準備
    tjs_int destxofs = data->DestLeft - data->Left;
    //	tjs_int destyofs = data->DestTop - data->Top;

    tjs_uint8 *dest;
    tjs_int destpitch;
    const tjs_uint8 *src1;
    tjs_int src1pitch;
    const tjs_uint8 *src2;
    tjs_int src2pitch;
    // 二進 D3: Dest は GetTextureForRender、Src1/Src2 は GetTexture を経由。
    iTVPTexture2D *desttex = data->Dest->GetTextureForRender();
    iTVPTexture2D *src1tex = data->Src1->GetTexture();
    iTVPTexture2D *src2tex = data->Src2->GetTexture();
    dest = (tjs_uint8 *)desttex->GetScanLineForWrite(data->DestTop);
    src1 = (const tjs_uint8 *)src1tex->GetScanLineForRead(0);
    src2 = (const tjs_uint8 *)src2tex->GetScanLineForRead(0);
    destpitch = desttex->GetPitch();
    src1pitch = src1tex->GetPitch();
    src2pitch = src2tex->GetPitch();

    if(src1pitch != src2pitch)
        return TJS_E_FAIL; // 両方のpitchが一致していないと駄目

    // ラインごとに処理
    tjs_int h = data->Height;
    tjs_int y = data->Top;
    while(h--) {
        tjs_int l, r;

        if(y < DriftCarePixels || y >= Height - DriftCarePixels) {
            // 上下のすみではみ出す可能性があるので
            // 折り返し転送を行う

            // 左端 ～ CenterX
            l = 0;
            r = CenterX;
            if(Clip(l, r, data->Left, data->Left + data->Width)) {
                if(y < CenterY) {
                    TVPRippleTransform_b_a_e(
                        Table->GetDisplaceMap(CenterX - l - 1, CenterY - y - 1),
                        CurDriftMap, (tjs_uint32 *)dest + l + destxofs, r - l,
                        src1pitch, src1, src2, l, y, Width, Height, BlendRatio);
                } else {
                    TVPRippleTransform_b_d_e(
                        Table->GetDisplaceMap(CenterX - l - 1, y - CenterY),
                        CurDriftMap, (tjs_uint32 *)dest + l + destxofs, r - l,
                        src1pitch, src1, src2, l, y, Width, Height, BlendRatio);
                }
            }

            // CenterX ～ 右端
            l = CenterX;
            r = Width;
            if(Clip(l, r, data->Left, data->Left + data->Width)) {
                if(y < CenterY) {
                    TVPRippleTransform_f_a_e(
                        Table->GetDisplaceMap(l - CenterX, CenterY - y - 1),
                        CurDriftMap, (tjs_uint32 *)dest + l + destxofs, r - l,
                        src1pitch, src1, src2, l, y, Width, Height, BlendRatio);
                } else {
                    TVPRippleTransform_f_d_e(
                        Table->GetDisplaceMap(l - CenterX, y - CenterY),
                        CurDriftMap, (tjs_uint32 *)dest + l + destxofs, r - l,
                        src1pitch, src1, src2, l, y, Width, Height, BlendRatio);
                }
            }

        } else {
            // 左端 ～ CenterX
            l = 0;
            r = CenterX;
            if(Clip(l, r, data->Left, data->Left + data->Width)) {
                int ll, rr;
                ll = 0, rr = DriftCarePixels;
                if(Clip(ll, rr, l, r)) {
                    // この ll ～ rr で表される左端は 左端にはみ出す可能性がある
                    // ので折り返し転送をさせる
                    if(y < CenterY) {
                        TVPRippleTransform_b_a_e(
                            Table->GetDisplaceMap(CenterX - ll - 1,
                                                  CenterY - y - 1),
                            CurDriftMap, (tjs_uint32 *)dest + ll + destxofs,
                            rr - ll, src1pitch, src1, src2, ll, y, Width, Height,
                            BlendRatio);
                    } else {
                        TVPRippleTransform_b_d_e(
                            Table->GetDisplaceMap(CenterX - ll - 1, y - CenterY),
                            CurDriftMap, (tjs_uint32 *)dest + ll + destxofs,
                            rr - ll, src1pitch, src1, src2, ll, y, Width, Height,
                            BlendRatio);
                    }
                }

                ll = DriftCarePixels;
                rr = r;
                if(Clip(ll, rr, l, r)) {
                    // ここははみ出さない
                    if(y < CenterY) {
                        TVPRippleTransform_b(
                            Table->GetDisplaceMap(CenterX - ll - 1,
                                                  CenterY - y - 1),
                            CurDriftMap, (tjs_uint32 *)dest + ll + destxofs,
                            rr - ll, src1pitch,
                            (const tjs_uint8 *)((const tjs_uint32 *)(src1 +
                                                                     y * src1pitch) +
                                                ll),
                            (const tjs_uint8 *)((const tjs_uint32 *)(src2 +
                                                                     y * src2pitch) +
                                                ll),
                            BlendRatio);
                    } else {
                        TVPRippleTransform_b(
                            Table->GetDisplaceMap(CenterX - ll - 1, y - CenterY),
                            CurDriftMap, (tjs_uint32 *)dest + ll + destxofs,
                            rr - ll, -src1pitch,
                            (const tjs_uint8 *)((const tjs_uint32 *)(src1 +
                                                                     y * src1pitch) +
                                                ll),
                            (const tjs_uint8 *)((const tjs_uint32 *)(src2 +
                                                                     y * src2pitch) +
                                                ll),
                            BlendRatio);
                    }
                }
            }

            // CenterX ～ 右端
            l = CenterX;
            r = Width;
            if(Clip(l, r, data->Left, data->Left + data->Width)) {
                int ll, rr;
                ll = l, rr = Width - DriftCarePixels;
                if(Clip(ll, rr, l, r)) {
                    // ここははみ出さない
                    if(y < CenterY) {
                        TVPRippleTransform_f(
                            Table->GetDisplaceMap(ll - CenterX, CenterY - y - 1),
                            CurDriftMap, (tjs_uint32 *)dest + ll + destxofs,
                            rr - ll, src1pitch,
                            (const tjs_uint8 *)((const tjs_uint32 *)(src1 +
                                                                     y * src1pitch) +
                                                ll),
                            (const tjs_uint8 *)((const tjs_uint32 *)(src2 +
                                                                     y * src2pitch) +
                                                ll),
                            BlendRatio);
                    } else {
                        TVPRippleTransform_f(
                            Table->GetDisplaceMap(ll - CenterX, y - CenterY),
                            CurDriftMap, (tjs_uint32 *)dest + ll + destxofs,
                            rr - ll, -src1pitch,
                            (const tjs_uint8 *)((const tjs_uint32 *)(src1 +
                                                                     y * src1pitch) +
                                                ll),
                            (const tjs_uint8 *)((const tjs_uint32 *)(src2 +
                                                                     y * src2pitch) +
                                                ll),
                            BlendRatio);
                    }
                }

                ll = Width - DriftCarePixels, rr = r;
                if(Clip(ll, rr, l, r)) {
                    // この ll ～ rr で表される右端は 右端にはみ出す可能性がある
                    // ので折り返し転送をさせる
                    if(y < CenterY) {
                        TVPRippleTransform_f_a_e(
                            Table->GetDisplaceMap(ll - CenterX, CenterY - y - 1),
                            CurDriftMap, (tjs_uint32 *)dest + ll + destxofs,
                            rr - ll, src1pitch, src1, src2, ll, y, Width, Height,
                            BlendRatio);
                    } else {
                        TVPRippleTransform_f_d_e(
                            Table->GetDisplaceMap(ll - CenterX, y - CenterY),
                            CurDriftMap, (tjs_uint32 *)dest + ll + destxofs,
                            rr - ll, src1pitch, src1, src2, ll, y, Width, Height,
                            BlendRatio);
                    }
                }
            }
        }

        dest += destpitch;
        y++;
    }

    return TJS_S_OK;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
class tTVPRippleTransHandlerProvider : public iTVPTransHandlerProvider {
    tjs_uint RefCount; // 参照カウンタ
public:
    tTVPRippleTransHandlerProvider() { RefCount = 1; }
    ~tTVPRippleTransHandlerProvider() override {}

    // Aligned with libkrkr2.so Provider::AddRef @0x7C78D4
    tjs_error AddRef() override {
        RefCount++;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::Release @0x7C78E8
    tjs_error Release() override {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::GetName @0x7C7920
    tjs_error GetName(const tjs_char **name) override {
        if(name)
            *name = TJS_W("ripple");
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::StartTransition @0x7C7938
    tjs_error StartTransition(
        /*in*/ iTVPSimpleOptionProvider *options, // option provider
        /*in*/ iTVPSimpleImageProvider *imagepro, // image provider
        /*in*/ tTVPLayerType layertype, // destination layer type
        /*in*/ tjs_uint src1w, tjs_uint src1h, // source 1 size
        /*in*/ tjs_uint src2w, tjs_uint src2h, // source 2 size
        /*out*/ tTVPTransType *type, // transition type
        /*out*/ tTVPTransUpdateType *updatetype, // update type
        /*out*/ iTVPBaseTransHandler **handler // transition handler
        ) override {
        if(type)
            *type = ttExchange; // transition type : exchange
        if(updatetype)
            *updatetype = tutDivisible; // update type : divisible
        if(!handler)
            return TJS_E_FAIL;
        if(!options)
            return TJS_E_FAIL;

        if(src1w != src2w || src1h != src2h)
            return TJS_E_FAIL; // src1 と src2 のサイズが一致していないと駄目

        // オプションを得る
        tTJSVariant tmp;
        tjs_uint64 time;

        tjs_int centerx = src1w >> 1, centery = src1h >> 1;
        tjs_int ripplewidth = 128;
        float roundness = 1.0;
        float speed = 6;
        tjs_int maxdrift = 24;

        if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp)))
            return TJS_E_FAIL; // time 属性が指定されていない
        if(tmp.Type() == tvtVoid)
            return TJS_E_FAIL;
        time = (tjs_int64)tmp;
        if(time < 2)
            time = 2; // あまり小さな数値を指定すると問題が起きるので

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("centerx"), &tmp)))
            if(tmp.Type() != tvtVoid)
                centerx = (tjs_int)tmp;

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("centery"), &tmp)))
            if(tmp.Type() != tvtVoid)
                centery = (tjs_int)tmp;

        if(centerx < 0 || centery < 0 || (tjs_uint)centerx >= src1w ||
           (tjs_uint)centery >= src1h)
            TVPThrowExceptionMessage(
                TJS_W("centerx and centery cannot be out of the image"));

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("rwidth"), &tmp)))
            if(tmp.Type() != tvtVoid)
                ripplewidth = (tjs_int)tmp;

        if(ripplewidth != 16 && ripplewidth != 32 && ripplewidth != 64 &&
           ripplewidth != 128)
            TVPThrowExceptionMessage(
                TJS_W("rwidth must be 16, 32, 64 or 128"));

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("roundness"), &tmp)))
            if(tmp.Type() != tvtVoid)
                roundness = (float)(double)tmp;

        if(roundness <= 0.0)
            TVPThrowExceptionMessage(
                TJS_W("roundness cannot be nagative or equal to 0"));

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("speed"), &tmp)))
            if(tmp.Type() != tvtVoid)
                speed = (float)(double)tmp;

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("maxdrift"), &tmp)))
            if(tmp.Type() != tvtVoid)
                maxdrift = (tjs_int)tmp;
        if(maxdrift < 0 || maxdrift >= 128)
            TVPThrowExceptionMessage(
                TJS_W("maxdrift cannot be nagative or larger than 127"));

        if((tjs_uint)maxdrift >= src1w || (tjs_uint)maxdrift >= src1h)
            TVPThrowExceptionMessage(TJS_W(
                "maxdrift must be lesser than both image width and height"));

        // オブジェクトを作成
        *handler = new tTVPRippleTransHandler(time, layertype, src1w, src1h,
                                              centerx, centery, ripplewidth,
                                              roundness, speed, maxdrift);

        return TJS_S_OK;
    }

} static *RippleTransHandlerProvider;
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so RegisterRippleTransHandlerProvider @0x7C7490
void RegisterRippleTransHandlerProvider() {
    TVPInitRippleTableCache(); // テーブルのキャッシュの初期化

    TVPInitRippleTransformFuncs(); // 演算関数の初期化（能力門 D1）

    // TVPAddTransHandlerProvider を使ってトランジションハンドラプロバイダを登録
    RippleTransHandlerProvider = new tTVPRippleTransHandlerProvider();
    TVPAddTransHandlerProvider(RippleTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterRippleTransHandlerProvider() {
    TVPRemoveTransHandlerProvider(RippleTransHandlerProvider);
    RippleTransHandlerProvider->Release();

    TVPClearRippleTableCache(); // 置換マップなどのテーブルのキャッシュのクリア
}
//---------------------------------------------------------------------------
