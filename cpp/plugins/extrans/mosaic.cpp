//---------------------------------------------------------------------------
// extrans.dll — 'mosaic' （モザイク）矩形镶嵌转场
//
// 实现基底 = 上游 krkrz/SamplePlugin/extrans/mosaic.cpp（W.Dee）。
// 差分裁判 = libkrkr2.so（kirikiroid2/ARM64）反编译。一致处保留上游源码原样
// （变量名 / 控制流 / FILL_LINE/FILL_ONE 宏），分歧处以二进制为准并在注释引用地址。
//
// 二进制地址索引（analysis/extrans_port.md §2 + 本轮 mosaic 反编译）：
//   RegisterMosaicTransHandlerProvider   sub_7C4438 @0x7C4438
//   Provider vtable                      off_1A25858
//   Provider::GetName                    sub_7C456C @0x7C456C  → L"mosaic"（amosaic @0x14E432A）
//   Provider::StartTransition            sub_7C4584 @0x7C4584  （内联 handler ctor）
//   Handler vtable                       off_1A25800
//   Handler::AddRef                      0x7C44A0
//   Handler::Release                     0x7C44B4
//   Handler::SetOption                   0x7C44EC  （no-op）
//   Handler::StartProcess                0x7C2AEC
//   Handler::EndProcess                  0x7C2BD8
//   Handler::Process                     0x7C2BEC  （软件 / GPU 双路径）
//   Handler::MakeFinalImage              0x7C44F4  → *dest=src2
//
// kirikiroid2 关键 delta（相对上游 Win32 源码）：
//   D1. mosaic **无颜色字段**（区别于 wave 的 bgcolor R/B 交换）。handler ctor
//       仅写 Width/Height/Time/HalfTime=Time/2/MaxBlockSize/First=true（0x7C4584
//       内联 ctor 0x50 字节内确认无颜色偏移）。
//   D2. Process 改为 texture-based 双路径分发：软件逐 block scanline blend
//       （IsSoftware() 真）vs GPU OperateRect + "MosaicTrans" GLSL（IsSoftware() 假）。
//       上游仅软件路径，且经 iTVPScanLineProvider::GetScanLine 直接取 scanline；
//       kirikiroid2 经 GetTexture()->GetScanLineFor{Read,Write} + 纹理 GetPitch。
//       分发标志 = TVPIsSoftwareRenderer_guess（mosaic 用此而非 wave 的 sub_84B7FC，
//       本地两者同语义 = TVPGetRenderManager()->IsSoftware()）。
//   D3. GPU 路径是上游 Win32 没有、kirikiroid2 专属的 "MosaicTrans" shader：
//       floor 网格吸附（floor((uv-offset)*tile)/tile+offset）+ mix 混合。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <math.h>
#include <string>

#include "mosaic.h"
#include "common.h"
#include "transhandler.h"
#include "TransIntf.h"
#include "tvpgl.h"          // TVPFillARGB
#include "RenderManager.h"  // iTVPRenderManager / iTVPTexture2D / OperateRect

//---------------------------------------------------------------------------
/*
    'モザイク' トランジション
    矩形モザイクによるトランジション
    このトランジションは転送先がαを持っていると(要するにトランジションを行う
    レイヤの type が ltOpaque 以外の場合)、正常に透過情報を処理できないので
    注意
*/
//---------------------------------------------------------------------------
class tTVPMosaicTransHandler : public iTVPDivisibleTransHandler {
    //	'モザイク' トランジションハンドラクラスの実装

    tjs_int RefCount; // 参照カウンタ

protected:
    tjs_uint64 StartTick; // トランジションを開始した tick count
    tjs_uint64 Time; // トランジションに要する時間
    tjs_uint64 HalfTime; // トランジションに要する時間 / 2
    tjs_uint64 CurTime; // 現在の時間
    tjs_int Width; // 処理する画像の幅
    tjs_int Height; // 処理する画像の高さ
    tjs_int CurOfsX; // ブロックオフセット X
    tjs_int CurOfsY; // ブロックオフセット Y
    tjs_int MaxBlockSize; // 最大のブロック幅
    tjs_int CurBlockSize; // 現在のブロック幅
    tjs_int BlendRatio; // ブレンド比
    bool First; // 一番最初の呼び出しかどうか

public:
    // Aligned with libkrkr2.so Provider::StartTransition inline ctor @0x7C4584
    // 二進: operator new(0x50) 後、Width/Height/Time/HalfTime=Time/2/MaxBlockSize/
    // RefCount=1/First=true を書き込む。颜色字段なし（D1）。
    tTVPMosaicTransHandler(tjs_uint64 time, tjs_int width, tjs_int height,
                           tjs_int maxblocksize) {
        RefCount = 1;

        Width = width;
        Height = height;
        Time = time;
        HalfTime = time / 2; // 二進: ctor で Time>>1 を +32 に格納
        MaxBlockSize = maxblocksize;

        First = true;
        // 上游 ctor は StartTick/CurTime/CurOfsX/CurOfsY/CurBlockSize/BlendRatio を
        // 初期化しない（StartProcess 初回で確定）。二進も同様。忠実復刻のため 0 初期化
        // を補わない。
    }

    virtual ~tTVPMosaicTransHandler() {}

    // Aligned with libkrkr2.so Handler::AddRef @0x7C44A0
    tjs_error AddRef() override {
        RefCount++;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Handler::Release @0x7C44B4
    // RefCount==1（最後の参照）で delete this、それ以外は --RefCount。
    tjs_error Release() override {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Handler::SetOption @0x7C44EC (no-op)
    tjs_error SetOption(
        /*in*/ iTVPSimpleOptionProvider *options) override {
        return TJS_S_OK;
    }

    tjs_error StartProcess(tjs_uint64 tick) override;

    tjs_error EndProcess() override;

    tjs_error Process(tTVPDivisibleData *data) override;

    // Aligned with libkrkr2.so Handler::MakeFinalImage @0x7C44F4
    tjs_error MakeFinalImage(
        iTVPScanLineProvider **dest, iTVPScanLineProvider *src1,
        iTVPScanLineProvider *src2) override {
        *dest = src2; // 常に最終画像は src2
        return TJS_S_OK;
    }
};
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPMosaicTransHandler::StartProcess @0x7C2AEC
tjs_error tTVPMosaicTransHandler::StartProcess(tjs_uint64 tick) {
    // トランジションの画面更新一回ごとに呼ばれる

    if(First) {
        // 最初の実行
        First = false;
        StartTick = tick;
    }

    // 画像演算に必要なパラメータを計算
    tjs_int64 t = CurTime = (tick - StartTick);
    if(CurTime > Time)
        CurTime = Time;
    if(t >= (tjs_int64)HalfTime)
        t = (tjs_int64)Time - t;
    if(t < 0)
        t = 0;
    CurBlockSize = (MaxBlockSize - 2) * t / (tjs_int64)HalfTime + 2;

    // BlendRatio
    BlendRatio = CurTime * 255 / Time;
    if(BlendRatio > 255)
        BlendRatio = 255;

    // 中心のブロックを本当に中心に持ってこられるように CurOfsX と CurOfsY を調整
    // 二進 0x7C2AEC: hw%blk - hw + (W-blk)/2 等は上游 (W/2/blk*blk) の代数等価展開。
    int x = Width / 2;
    int y = Height / 2;
    x /= CurBlockSize;
    y /= CurBlockSize;
    x *= CurBlockSize;
    y *= CurBlockSize;
    CurOfsX = (Width - CurBlockSize) / 2 - x;
    CurOfsY = (Height - CurBlockSize) / 2 - y;
    if(CurOfsX > 0)
        CurOfsX -= CurBlockSize;
    if(CurOfsY > 0)
        CurOfsY -= CurBlockSize;

    return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPMosaicTransHandler::EndProcess @0x7C2BD8
tjs_error tTVPMosaicTransHandler::EndProcess() {
    if(BlendRatio == 255)
        return TJS_S_FALSE; // トランジション終了

    return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// GPU 路径 MosaicTrans GLSL シェーダ（D3、Aligned with libkrkr2.so Process @0x7C2BEC
// GPU 分支 + sub_84B074=GetOrCompileRenderMethod, name="MosaicTrans"）。
// 上游 Win32 に無い、kirikiroid2 専用 GPU 経路。floor 網格吸附で矩形モザイク化。
static const char *const TVPMosaicTransGLSL =
    "uniform float opa;\n"
    "uniform vec2 tile;\n"
    "uniform vec2 offset;\n"
    // 二進 @0x14E4204: "void main() {\n"（{ は main() と同行・前空白・単一 \n）。
    // get_bytes 字節確認済み。GLSL ソース文字列定数として字節一致させる。
    "void main() {\n"
    "    vec4 s = texture2D(tex0, floor((v_texCoord0 - offset) * tile) / tile + offset);\n"
    "    vec4 d = texture2D(tex1, floor((v_texCoord1 - offset) * tile) / tile + offset);\n"
    "    gl_FragColor = mix(s.rgba, d.rgba, opa);\n"
    "}";
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPMosaicTransHandler::Process @0x7C2BEC
tjs_error tTVPMosaicTransHandler::Process(tTVPDivisibleData *data) {
    // トランジションの各領域ごとに呼ばれる
    //
    // 二進: if ( TVPIsSoftwareRenderer_guess() & 1 ) → 软件 scanline 路径
    //                                            else → GPU OperateRect 路径
    // 本地は wave と同じく TVPGetRenderManager()->IsSoftware() で分発（同語義）。
    if(TVPGetRenderManager()->IsSoftware()) {
        // ---- 软件 scanline 路径（上游 block 镶嵌算法を texture-based に置換、D2/D3）----

        // 変数の準備（二進: scanline は texture 経由、pitch は texture GetPitch）
        tjs_uint8 *dest;
        tjs_int destpitch;
        const tjs_uint8 *src1;
        tjs_int src1pitch;
        const tjs_uint8 *src2;
        tjs_int src2pitch;
        // 二進: Dest は GetTextureForRender、Src1/Src2 は GetTexture を経由して
        // scanline(0) + pitch（block アルゴリズムは pitch で跨行アドレッシング）。
        {
            iTVPTexture2D *desttex = data->Dest->GetTextureForRender();
            iTVPTexture2D *src1tex = data->Src1->GetTexture();
            iTVPTexture2D *src2tex = data->Src2->GetTexture();
            dest = (tjs_uint8 *)desttex->GetScanLineForWrite(0);
            src1 = (const tjs_uint8 *)src1tex->GetScanLineForRead(0);
            src2 = (const tjs_uint8 *)src2tex->GetScanLineForRead(0);
            destpitch = desttex->GetPitch();
            src1pitch = src1tex->GetPitch();
            src2pitch = src2tex->GetPitch();
        }

        tjs_int destxofs = data->DestLeft - data->Left;
        tjs_int destyofs = data->DestTop - data->Top;

        // 1: その転送矩形に含まれるモザイクのブロックの範囲を判定する
        int startx, starty;
        int endx, endy;
        int bs = CurBlockSize;

        startx = (data->Left - CurOfsX) / bs;
        starty = (data->Top - CurOfsY) / bs;
        endx = (data->Left + data->Width - 1 - CurOfsX) / bs;
        endy = (data->Top + data->Height - 1 - CurOfsY) / bs;

        // 塗りつぶしマクロ（上游 FILL_LINE: switch(xlen) 2..7 + default TVPFillARGB）
#define FILL_LINE(dp, xlen, ylen, d) { \
            tjs_uint8 *__destp = (tjs_uint8*)(dp); \
            int __count = ylen; \
            tjs_uint32 color = (d); \
            switch(xlen)                   \
            {                              \
            case 2:                        \
                do           \
                {                          \
                    ((tjs_uint32*)__destp)[0] =       \
                    ((tjs_uint32*)__destp)[1] = color;      \
                    __destp += destpitch;  \
                } while(--__count);                          \
                break;                     \
            case 3:                        \
                do           \
                {                          \
                    ((tjs_uint32*)__destp)[0] =       \
                    ((tjs_uint32*)__destp)[1] =       \
                    ((tjs_uint32*)__destp)[2] = color;      \
                    __destp += destpitch;  \
                } while(--__count);                          \
                break;                     \
            case 4:                        \
                do           \
                {                          \
                    ((tjs_uint32*)__destp)[0] =       \
                    ((tjs_uint32*)__destp)[1] =       \
                    ((tjs_uint32*)__destp)[2] =       \
                    ((tjs_uint32*)__destp)[3] = color;      \
                    __destp += destpitch;  \
                } while(--__count);                          \
                break;                     \
            case 5:                        \
                do           \
                {                          \
                    ((tjs_uint32*)__destp)[0] =       \
                    ((tjs_uint32*)__destp)[1] =       \
                    ((tjs_uint32*)__destp)[2] =       \
                    ((tjs_uint32*)__destp)[3] =       \
                    ((tjs_uint32*)__destp)[4] = color;      \
                    __destp += destpitch;  \
                } while(--__count);                          \
                break;                     \
            case 6:                        \
                do           \
                {                          \
                    ((tjs_uint32*)__destp)[0] =       \
                    ((tjs_uint32*)__destp)[1] =       \
                    ((tjs_uint32*)__destp)[2] =       \
                    ((tjs_uint32*)__destp)[3] =       \
                    ((tjs_uint32*)__destp)[4] =       \
                    ((tjs_uint32*)__destp)[5] = color;      \
                    __destp += destpitch;  \
                } while(--__count);                          \
                break;                     \
            case 7:                        \
                do           \
                {                          \
                    ((tjs_uint32*)__destp)[0] =       \
                    ((tjs_uint32*)__destp)[1] =       \
                    ((tjs_uint32*)__destp)[2] =       \
                    ((tjs_uint32*)__destp)[3] =       \
                    ((tjs_uint32*)__destp)[4] =       \
                    ((tjs_uint32*)__destp)[5] =       \
                    ((tjs_uint32*)__destp)[6] = color;      \
                    __destp += destpitch;  \
                } while(--__count);                          \
                break;                     \
            default:                       \
                while(__count--) \
                { \
                    TVPFillARGB((tjs_uint32*)__destp, (xlen), color); \
                    __destp += destpitch; \
                } \
            } \
        }


        // 注意しながらの塗りつぶしマクロ（上游 FILL_ONE: clip + 中心像素 Blend）
        int bs2 = bs >> 1;
#define FILL_ONE(x, y) { \
        tjs_int l = (x) * bs + CurOfsX; \
        tjs_int t = (y) * bs + CurOfsY; \
        tjs_int r = l + bs; \
        tjs_int b = t + bs; \
        tjs_int mx = l + bs2, my = t + bs2; \
        if(Clip(l, r, data->Left, data->Left + data->Width) && \
            Clip(t, b, data->Top, data->Top + data->Height)) \
        { \
            if(mx < 0) mx = 0; \
            if(my < 0) my = 0; \
            if(mx >= Width) mx = Width - 1; \
            if(my >= Height) my = Height - 1; \
            tjs_uint32 d = Blend( \
                *((const tjs_uint32 *)(src1 + my*src1pitch) + mx), \
                *((const tjs_uint32 *)(src2 + my*src2pitch) + mx), \
                BlendRatio); \
            tjs_uint8 *destp = (tjs_uint8*) \
                ((tjs_uint32*)(dest + (t + destyofs)*destpitch) + l + destxofs); \
            tjs_int xlen = r - l; \
            tjs_int ylen = b - t; \
            FILL_LINE(destp, xlen, ylen, d); \
        } \
    }
        /* 本来は転送元ブロックの範囲内にあるピクセルの色の平均を取ると綺麗だけど
           重くなるのでやらない */

        // 2: まず辺境のブロックに対して転送矩形との積矩形を得てそこに色を塗りつぶす
        // 3: 残りのブロックははみ出しについて注意する必要がないので心おきなく色を塗りつぶす

        // 一番上の行
        int y = starty;
        for(int x = startx; x <= endx; x++) {
            FILL_ONE(x, y);
        }
        y++;

        // なかほどの行
        for(; y < endy; y++) {
            // 左端
            FILL_ONE(startx, y);

            // なかほど
            tjs_int x = startx + 1;
            tjs_int l = x * bs + CurOfsX;
            tjs_int t = y * bs + CurOfsY;
            const tjs_uint32 *src1p =
                (const tjs_uint32 *)(src1 + (t + bs2) * src1pitch) + (l + bs2);
            const tjs_uint32 *src2p =
                (const tjs_uint32 *)(src2 + (t + bs2) * src2pitch) + (l + bs2);
            tjs_uint32 *destp =
                ((tjs_uint32 *)(dest + (t + destyofs) * destpitch) + l + destxofs);

            for(; x < endx; x++) {
                // ここの塗りつぶしは(はみ出ているかどうかを)ノーチェックでいける
                tjs_uint32 d = Blend(*src1p, *src2p, BlendRatio);
                FILL_LINE(destp, bs, bs, d);

                src1p += bs;
                src2p += bs;
                destp += bs;
            }

            // 右端
            FILL_ONE(endx, y);
        }


        // 一番下の行
        if(y <= endy) {
            for(int x = startx; x <= endx; x++) {
                FILL_ONE(x, y);
            }
        }

#undef FILL_ONE
#undef FILL_LINE
    } else {
        // ---- GPU OperateRect 路径（二進 0x7C2BEC GPU 分支、name="MosaicTrans"）----
        // floor 網格吸附は fragment shader 内で表現（floor((uv-offset)*tile)/tile+offset）。
        // 单 OperateRect で全 block を一括モザイク化（kirikiroid2 設計）。

        iTVPTexture2D *src2tex = data->Src2->GetTexture();
        tjs_uint w = src2tex->GetWidth();  // 二進: v302 = Src2 tex GetWidth
        tjs_uint h = src2tex->GetHeight(); // 二進: v303 = Src2 tex GetHeight

        iTVPRenderMethod *method = TVPGetRenderManager()->GetOrCompileRenderMethod(
            "MosaicTrans", nullptr, TVPMosaicTransGLSL, 2, 0);

        // uniform 灌入（二進 Process @0x7C2BEC GPU 分支）
        int opa_id = method->EnumParameterID("opa");
        int tile_id = method->EnumParameterID("tile");
        int offset_id = method->EnumParameterID("offset");
        method->SetParameterFloat(opa_id, (float)BlendRatio / 255.0f);
        // tile = vec2(texW/CurBlockSize, texH/CurBlockSize)
        float tile[2] = {(float)w / (float)CurBlockSize,
                         (float)h / (float)CurBlockSize};
        method->SetParameterFloatArray(tile_id, tile, 2);
        // offset = vec2(CurOfsX/texW, CurOfsY/texH)
        float offset[2] = {(float)CurOfsX / (float)w,
                           (float)CurOfsY / (float)h};
        method->SetParameterFloatArray(offset_id, offset, 2);

        // OperateRect（二進: renderMgr vtable+160、CrossFade::Blend と同形）
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
    }

    return TJS_S_OK;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
class tTVPMosaicTransHandlerProvider : public iTVPTransHandlerProvider {
    tjs_uint RefCount; // 参照カウンタ
public:
    tTVPMosaicTransHandlerProvider() { RefCount = 1; }
    ~tTVPMosaicTransHandlerProvider() override {}

    // Aligned with libkrkr2.so Provider::AddRef（off_1A25858 slot0）
    tjs_error AddRef() override {
        RefCount++;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::Release（off_1A25858 slot1）
    tjs_error Release() override {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::GetName @0x7C456C → L"mosaic"
    tjs_error GetName(const tjs_char **name) override {
        if(name)
            *name = TJS_W("mosaic");
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::StartTransition @0x7C4584
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
        tjs_int maxblocksize = 30; // 二進: maxsize 既定値 30

        if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp)))
            return TJS_E_FAIL; // time 属性が指定されていない
        if(tmp.Type() == tvtVoid)
            return TJS_E_FAIL;
        time = (tjs_int64)tmp;
        if(time < 2)
            time = 2; // あまり小さな数値を指定すると問題が起きるので（二進: time<2→2）

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("maxsize"), &tmp)))
            if(tmp.Type() != tvtVoid)
                maxblocksize = (tjs_int)tmp;

        // オブジェクトを作成（颜色 delta なし、D1）
        *handler =
            new tTVPMosaicTransHandler(time, src1w, src1h, maxblocksize);

        return TJS_S_OK;
    }

} static *MosaicTransHandlerProvider;
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so RegisterMosaicTransHandlerProvider @0x7C4438
void RegisterMosaicTransHandlerProvider() {
    MosaicTransHandlerProvider = new tTVPMosaicTransHandlerProvider();
    TVPAddTransHandlerProvider(MosaicTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterMosaicTransHandlerProvider() {
    TVPRemoveTransHandlerProvider(MosaicTransHandlerProvider);
    MosaicTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
