//---------------------------------------------------------------------------
// extrans.dll — 'wave' （波）ラスタスクロール转场
//
// 实现基底 = 上游 krkrz/SamplePlugin/extrans/wave.cpp（W.Dee）。
// 差分裁判 = libkrkr2.so（kirikiroid2/ARM64）反编译。一致处保留上游源码原样
// （变量名 / 控制流 / 容器），分歧处以二进制为准并在注释引用函数地址。
//
// 二进制地址索引（analysis/extrans_port.md §5）：
//   RegisterWaveTransHandlerProvider     sub_7CD05C @0x7CD05C
//   Provider::StartTransition            sub_7CD1A8 @0x7CD1A8
//   Provider::GetName                    sub_7CD190 @0x7CD190  → L"wave"
//   Handler::StartProcess                sub_7CBEDC @0x7CBEDC
//   Handler::EndProcess                  sub_7CC080 @0x7CC080
//   Handler::Process                     sub_7CC094 @0x7CC094  （软件 / GPU 双路径）
//   Handler::MakeFinalImage              sub_7CD118 @0x7CD118  → *dest=src2
//   GPU shader 准备（WaveTrans 系）       sub_7CC428 @0x7CC428
//
// kirikiroid2 三大 delta（相对上游 Win32 源码）：
//   D1. bgcolor1/bgcolor2 存入 handler 字段时做 R/B 通道交换
//       （c & 0xFF00FF00 | BYTE2(c) | ((u8)c<<16)，0x7cd5f8），因底层 GL/Cocos
//       纹理用 ABGR 而 TJS 颜色是 0xAARRGGBB。
//   D2. Process 改为 texture-based 双路径分发：软件逐行 scanline blend
//       （IsSoftware() 真，对应二进制 sub_84B7FC=rm->[vtbl+64]()&1 缓存于
//       byte_1ADEB88；真→软件 scanline，假→GPU。原 IDA 名 hasGPUAccel_guess
//       方向相反，已纠正为 TVPIsSoftwareRenderer_guess）
//       vs GPU OperateRect + WaveTrans GLSL shader（IsSoftware() 假）。
//       上游仅软件路径，且经 iTVPScanLineProvider::GetScanLine 直接取 scanline；
//       kirikiroid2 经 GetTexture()->GetScanLineForRead/ForWrite。
//   D3. scanline 经 texture 取（见 D2）。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <math.h>

#include "wave.h"
#include "common.h"
#include "transhandler.h"
#include "TransIntf.h"
#include "tvpgl.h"          // TVPFillARGB / TVPConstAlphaBlend_SD[_a/_d]
#include "RenderManager.h"  // iTVPRenderManager / iTVPTexture2D / OperateRect

//---------------------------------------------------------------------------
/*
    '波' トランジション
    ラスタスクロールによるトランジション
*/
//---------------------------------------------------------------------------
class tTVPWaveTransHandler : public iTVPDivisibleTransHandler {
    //	'波' トランジションハンドラクラスの実装

    tjs_int RefCount; // 参照カウンタ

protected:
    tjs_uint64 StartTick; // トランジションを開始した tick count
    tjs_uint64 HalfTime; // トランジションに要する時間 / 2
    tjs_uint64 Time; // トランジションに要する時間
    tTVPLayerType LayerType; // レイヤタイプ
    tjs_int Width; // 処理する画像の幅
    tjs_int Height; // 処理する画像の高さ
    tjs_int MaxH; // 最大振幅
    double MaxOmega; // 最大角速度
    tjs_int CurH; // 現在の振幅
    double CurOmega; // 現在の角速度
    double CurRadStart; // 角開始位置
    tjs_int64 CurTime; // 現在の tick count
    tjs_int BlendRatio; // ブレンド比
    tjs_uint32 BGColor1; // 背景色その１（R/B 交換後 = ABGR、D1）
    tjs_uint32 BGColor2; // 背景色その２（R/B 交換後 = ABGR、D1）
    tjs_uint32 CurBGColor; // 現在の背景色
    tjs_int WaveType; // 0 = 最初と最後 1 = 最初 2 = 最後 が波が細かい
    bool First; // 一番最初の呼び出しかどうか

public:
    // Aligned with libkrkr2.so Provider::StartTransition inline ctor @0x7CD1A8
    // bgcolor1/bgcolor2 は呼び出し側で R/B 交換済みを受け取る（D1、0x7cd5f8）。
    tTVPWaveTransHandler(tjs_uint64 time, tTVPLayerType layertype, tjs_int width,
                         tjs_int height, tjs_int maxh, double maxomega,
                         tjs_uint32 bgcolor1, tjs_uint32 bgcolor2,
                         tjs_int wavetype) {
        RefCount = 1;

        LayerType = layertype;
        Width = width;
        Height = height;
        Time = time;
        HalfTime = time / 2; // 二進: ctor で Time>>1 を +24 に格納
        MaxH = maxh;
        MaxOmega = maxomega;
        BGColor1 = bgcolor1;
        BGColor2 = bgcolor2;
        WaveType = wavetype;

        First = true;
    }

    virtual ~tTVPWaveTransHandler() {}

    // Aligned with libkrkr2.so Handler::AddRef @0x7CD0C4
    tjs_error AddRef() override {
        RefCount++;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Handler::Release @0x7CD0D8
    // RefCount==1（最後の参照）で delete this、それ以外は --RefCount。
    tjs_error Release() override {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Handler::SetOption @0x7CD110 (no-op)
    tjs_error SetOption(
        /*in*/ iTVPSimpleOptionProvider *options) override {
        return TJS_S_OK;
    }

    tjs_error StartProcess(tjs_uint64 tick) override;

    tjs_error EndProcess() override;

    tjs_error Process(tTVPDivisibleData *data) override;

    // Aligned with libkrkr2.so Handler::MakeFinalImage @0x7CD118
    tjs_error MakeFinalImage(
        iTVPScanLineProvider **dest, iTVPScanLineProvider *src1,
        iTVPScanLineProvider *src2) override {
        *dest = src2; // 常に最終画像は src2
        return TJS_S_OK;
    }
};
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPWaveTransHandler::StartProcess @0x7CBEDC
tjs_error tTVPWaveTransHandler::StartProcess(tjs_uint64 tick) {
    // トランジションの画面更新一回ごとに呼ばれる

    if(First) {
        // 最初の実行
        First = false;
        StartTick = tick;
    }

    // 画像演算に必要な各パラメータを計算
    tjs_int64 t = CurTime = (tick - StartTick);
    if(CurTime > (tjs_int64)Time)
        CurTime = Time; // CurTime = min(Time, elapsed)
    if(t >= (tjs_int64)HalfTime)
        t = (tjs_int64)Time - t;
    if(t < 0)
        t = 0;

    double tt = sin((3.14159265358979 / 2.0) * t / (tjs_int64)HalfTime);

    // CurH, CurOmega, CurRadStart
    CurH = tt * MaxH;
    switch(WaveType) {
        case 0: // 最初と最後が波が細かい
            CurOmega = MaxOmega * tt;
            break;
        case 1: // 最初が波が細かい
            CurOmega = MaxOmega * ((tjs_int64)Time - CurTime) / (tjs_int64)Time;
            break;
        case 2: // 最後が波が細かい
            CurOmega = MaxOmega * CurTime / (tjs_int64)Time;
            break;
    }
    CurRadStart = -CurOmega * (Height / 2);

    // BlendRatio
    BlendRatio = CurTime * 255 / Time;
    if(BlendRatio > 255)
        BlendRatio = 255;

    // 背景色のブレンド
    CurBGColor = Blend(BGColor1, BGColor2, BlendRatio);

    return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPWaveTransHandler::EndProcess @0x7CC080
tjs_error tTVPWaveTransHandler::EndProcess() {
    if(BlendRatio == 255)
        return TJS_S_FALSE; // トランジション終了

    return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// GPU 路径 WaveTrans GLSL シェーダ（D2、Aligned with libkrkr2.so sub_7CC428）
// 上游 Win32 に無い、kirikiroid2 専用 GPU 経路。三種 shader は懒编译単例。
static const char *const TVPWaveTransGLSLHead =
    "uniform float opa;\n"
    "uniform float omega;\n"
    "uniform float phase;\n"
    "uniform float h;\n"
    "uniform vec4 bgclr;\n"
    "void main()\n"
    "{\n"
    "    float dx = sin(v_texCoord0.y * omega + phase) * h;\n"
    "    float dx0 = dx + v_texCoord0.x;\n"
    "    float dx1 = dx + v_texCoord1.x;\n"
    "    float sign = step(0.0, dx0);\n"
    "    vec4 s = texture2D(tex0, vec2(dx0, v_texCoord0.y));\n"
    "    vec4 d = texture2D(tex1, vec2(dx1, v_texCoord1.y));\n"
    "    s = s * sign + (1.0 - sign) * bgclr;\n"
    "    d = d * sign + (1.0 - sign) * bgclr;\n";
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPWaveTransHandler::Process @0x7CC094
tjs_error tTVPWaveTransHandler::Process(tTVPDivisibleData *data) {
    // トランジションの各領域ごとに呼ばれる
    //
    // 二進: if ( TVPIsSoftwareRenderer_guess() & 1 ) → 软件 scanline 路径
    //                                            else → GPU OperateRect 路径
    // sub_84B7FC = TVPGetRenderManager()->[vtbl+64]() & 1 をキャッシュした値
    // （byte_1ADEB88）。真→软件レンダラ→CPU 逐行 = IsSoftware()。
    if(TVPGetRenderManager()->IsSoftware()) {
        // ---- 软件 scanline 路径（上游逐行算法を texture-based に置換、D2/D3）----

        // 初期パラメータを計算
        double rad = data->Top * CurOmega + CurRadStart; // 角度

        // ラインごとに処理
        tjs_int n;
        for(n = 0; n < data->Height; n++, rad += CurOmega) {
            // ズレ位置
            tjs_int d = (tjs_int)(sin(rad) * CurH);

            // 転送
            tjs_int l, r;

            // スキャンライン（二進: Dest は GetTextureForRender、Src1/Src2 は
            // GetTexture を経由して GetScanLineFor{Write,Read}）
            tjs_uint32 *dest =
                (tjs_uint32 *)data->Dest->GetTextureForRender()
                    ->GetScanLineForWrite(data->DestTop + n);
            const tjs_uint32 *src1 =
                (const tjs_uint32 *)data->Src1->GetTexture()
                    ->GetScanLineForRead(data->Top + n);
            const tjs_uint32 *src2 =
                (const tjs_uint32 *)data->Src2->GetTexture()
                    ->GetScanLineForRead(data->Top + n);

            // 左側のずれる部分に背景色を転送
            if(d > 0) {
                l = 0;
                r = d;
                if(Clip(l, r, data->Left, data->Left + data->Width))
                    TVPFillARGB(dest + l + data->DestLeft - data->Left, r - l,
                                CurBGColor);
            }

            // 左端のずれる部分に背景色を転送
            if(d < 0) {
                l = d + Width;
                r = Width;
                if(Clip(l, r, data->Left, data->Left + data->Width))
                    TVPFillARGB(dest + l + data->DestLeft - data->Left, r - l,
                                CurBGColor);
            }

            // ブレンドしながら転送
            l = d;
            r = Width + d;
            if(Clip(l, r, data->Left, data->Left + data->Width)) {
                if(LayerType == ltAlpha)
                    TVPConstAlphaBlend_SD_d(
                        dest + l + data->DestLeft - data->Left, src1 + l - d,
                        src2 + l - d, r - l, BlendRatio);
                else if(LayerType == ltAddAlpha)
                    TVPConstAlphaBlend_SD_a(
                        dest + l + data->DestLeft - data->Left, src1 + l - d,
                        src2 + l - d, r - l, BlendRatio);
                else
                    TVPConstAlphaBlend_SD(
                        dest + l + data->DestLeft - data->Left, src1 + l - d,
                        src2 + l - d, r - l, BlendRatio);
            }
        }
    } else {
        // ---- GPU OperateRect 路径（二進 0x7cc2f4〜、Aligned with sub_7CC428）----
        // wave 位移は fragment shader 内（dx = sin(y·omega+phase)·h）で行う。
        // 单 OperateRect だが per-line 位移は shader が担うので退化平直 blend では
        // ない（kirikiroid2 設計）。

        iTVPTexture2D *src1tex = data->Src1->GetTexture();
        tjs_uint w = src1tex->GetWidth(); // 二進: v26 = Src1->[vtbl+24]()
        tjs_uint h = src1tex->GetHeight(); // 二進: v27 = Src1->[vtbl+32]()

        // layertype 別 GLSL（懒编译単例）
        const char *name;
        std::string glsl(TVPWaveTransGLSLHead);
        if(LayerType == ltAddAlpha) { // == 12
            name = "WaveTrans_a";
            glsl += "    gl_FragColor = mix(s.rgba, d.rgba, opa);\n}";
        } else if(LayerType == ltAlpha) { // == 2
            name = "WaveTrans_d";
            glsl += "    gl_FragColor = mix(s, d, opa);\n}";
        } else {
            name = "WaveTrans";
            glsl += "    d.rgb = mix(s.rgb, d.rgb, opa);\n    gl_FragColor = d;\n}";
        }
        iTVPRenderMethod *method = TVPGetRenderManager()->GetOrCompileRenderMethod(
            name, nullptr, glsl.c_str(), 2, 0);

        // uniform 灌入（二進 sub_7CC428 LABEL_83）
        int opa_id = method->EnumParameterID("opa");
        int omega_id = method->EnumParameterID("omega");
        int phase_id = method->EnumParameterID("phase");
        int h_id = method->EnumParameterID("h");
        int bgclr_id = method->EnumParameterID("bgclr");
        method->SetParameterFloat(opa_id, (float)BlendRatio / 255.0f);
        method->SetParameterFloat(omega_id, (float)(CurOmega * (double)h));
        method->SetParameterFloat(phase_id, (float)CurRadStart);
        method->SetParameterFloat(h_id, (float)CurH / (float)w);
        method->SetParameterColor4B(bgclr_id, CurBGColor);

        // OperateRect（二進: rm->[vtbl+160]、CrossFade::Blend と同形）
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
class tTVPWaveTransHandlerProvider : public iTVPTransHandlerProvider {
    tjs_uint RefCount; // 参照カウンタ
public:
    tTVPWaveTransHandlerProvider() { RefCount = 1; }
    ~tTVPWaveTransHandlerProvider() override {}

    // Aligned with libkrkr2.so Provider::AddRef @0x7CD144
    tjs_error AddRef() override {
        RefCount++;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::Release @0x7CD158
    tjs_error Release() override {
        if(RefCount == 1)
            delete this;
        else
            RefCount--;
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::GetName @0x7CD190
    tjs_error GetName(const tjs_char **name) override {
        if(name)
            *name = TJS_W("wave");
        return TJS_S_OK;
    }

    // Aligned with libkrkr2.so Provider::StartTransition @0x7CD1A8
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
        tjs_int maxh = 50;
        double maxomega = 0.2;
        tjs_uint32 bgcolor1 = 0;
        tjs_uint32 bgcolor2 = 0;
        tjs_int wavetype = 0;

        if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp)))
            return TJS_E_FAIL; // time 属性が指定されていない
        if(tmp.Type() == tvtVoid)
            return TJS_E_FAIL;
        time = (tjs_int64)tmp;
        if(time < 2)
            time = 2; // 二進: time<2→2（最小 Time=2、0x7cd1a8）

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("maxh"), &tmp)))
            if(tmp.Type() != tvtVoid)
                maxh = (tjs_int)tmp;

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("maxomega"), &tmp)))
            if(tmp.Type() != tvtVoid)
                maxomega = (double)tmp;

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("bgcolor1"), &tmp)))
            if(tmp.Type() != tvtVoid)
                bgcolor1 = (tjs_int)tmp;

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("bgcolor2"), &tmp)))
            if(tmp.Type() != tvtVoid)
                bgcolor2 = (tjs_int)tmp;

        if(TJS_SUCCEEDED(options->GetValue(TJS_W("wavetype"), &tmp)))
            if(tmp.Type() != tvtVoid)
                wavetype = (tjs_int)tmp;

        // D1: bgcolor1/bgcolor2 を R/B 交換して handler へ（0x7cd5f8）。
        // c & 0xFF00FF00 | BYTE2(c) | ((u8)c<<16)
        // A,G を保持し、原 R(bit16-23) を低 8bit、原 B(低 8bit) を bit16 へ。
        bgcolor1 = (bgcolor1 & 0xFF00FF00) | ((bgcolor1 >> 16) & 0xFF) |
            ((bgcolor1 & 0xFF) << 16);
        bgcolor2 = (bgcolor2 & 0xFF00FF00) | ((bgcolor2 >> 16) & 0xFF) |
            ((bgcolor2 & 0xFF) << 16);

        // オブジェクトを作成
        *handler = new tTVPWaveTransHandler(time, layertype, src1w, src1h, maxh,
                                            maxomega, bgcolor1, bgcolor2,
                                            wavetype);

        return TJS_S_OK;
    }

} static *WaveTransHandlerProvider;
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so RegisterWaveTransHandlerProvider @0x7CD05C
void RegisterWaveTransHandlerProvider() {
    WaveTransHandlerProvider = new tTVPWaveTransHandlerProvider();
    TVPAddTransHandlerProvider(WaveTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterWaveTransHandlerProvider() {
    TVPRemoveTransHandlerProvider(WaveTransHandlerProvider);
    WaveTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
