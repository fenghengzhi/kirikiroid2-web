//---------------------------------------------------------------------------
// extrans.dll — 'turn' （ターン）正方形タイル翻转转场
//
// 实现基底 = 上游 krkrz/SamplePlugin/extrans/turn.cpp（W.Dee）。
// 差分裁判 = libkrkr2.so（kirikiroid2/ARM64）反编译。一致处保留上游源码原样
// （变量名 / 控制流 / TurnTransParams[phase] + gloss[] table-driven warp），
// 分歧处以二进制为准并在注释引用函数地址。
//
// 二进制地址索引（analysis/extrans_port.md §2 + 本轮 turn 反编译）：
//   RegisterTurnTransHandlerProvider   sub_7CBB5C @0x7CBB5C（全局 qword_1AD9980）
//   Provider vtable                    off_1A25B68
//   Provider::GetName                  sub_7CBC90 @0x7CBC90  → L"turn"（0x1525788）
//   Provider::StartTransition          sub_7CBCA8 @0x7CBCA8  （内联 handler ctor，0x40=64B）
//   Handler vtable                     off_1A25B10
//   Handler::AddRef                    0x7CBBC4
//   Handler::Release                   0x7CBBD8
//   Handler::SetOption                 0x7CBC10  （no-op）
//   Handler::StartProcess              0x7CA9CC
//   Handler::EndProcess                0x7CAA54
//   Handler::Process                   0x7CAA68  （软件 table-warp / GPU mesh-warp 双路径）
//   Handler::MakeFinalImage            0x7CBC18  → *dest=src2
//   TurnTransParams 表 .rodata          dword_14E4CD4 @0x14E4CD4（[64][64]×32B、字节核对吻合上游）
//   gloss[64] 整数表 .rodata            dword_14E4BD4 @0x14E4BD4（字节核对吻合上游）
//   GPU FillARGB method                "FillARGB"           @0x7cb5c8
//   GPU mesh blend method              "extrans_turn_Blend" @0x7cb90c（shader 串见 GPU 路径）
//
// kirikiroid2 关键 delta（相对上游 Win32 源码）：
//   D1. bgcolor を handler の BGColor 字段に格納する際 R/B 通道交换（swizzle）。
//       c & 0xFF00FF00 | BYTE2(c) | ((u8)c<<16)（0x7cbe84）。wave と同じ delta、
//       mosaic には無い。底层 GL/Cocos 纹理は ABGR、TJS 颜色は 0xAARRGGBB。
//   D2. Process が texture-based 双路径分发（IsSoftware()）。软件路径は上游 table-driven
//       per-tile 64×64 warp をそのまま（scanline は GetTexture()->GetScanLineFor{Read,Write}、
//       pitch は纹理 GetPitch を経由）。GPU 路径は上游 Win32 に無い kirikiroid2 専用で、
//       FillARGB で背景を BGColor で填め、その後 tile ごとに gloss 角度で歪めた quad を
//       "extrans_turn_Blend" shader + OperateTriangles でメッシュ blend する。
//   D3. scanline 経 texture（D2 に含む）。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <math.h>
#include <string.h>

#include "turn.h"
#include "turntrans_table.h"
#include "common.h"
#include "transhandler.h"
#include "TransIntf.h"
#include "tvpgl.h"          // TVPFillARGB / TVPLinTransCopy
#include "RenderManager.h"  // iTVPRenderManager / iTVPTexture2D / OperateRect / OperateTriangles

//---------------------------------------------------------------------------
/*
    'turn' トランジション
    正方形の小さなタイルをひっくり返すようにして切り替わるトランジション
    いろいろがんばってみたがいまいち回転している雰囲気が出ていない
*/
//---------------------------------------------------------------------------
// テカり（上游 static const gloss[64]。二進 dword_14E4BD4 @0x14E4BD4 と字节核对吻合）
static const tjs_int gloss[64] =
	{
	   0,   0,   0,   0,  16,  48,  80, 128,
	 192, 128,  80,  48,  16,   0,   0,   0,
	   0,   0,   0,   0,   0,   0,   0,   0,
	   0,   0,   0,   0,   0,   0,   0,   0,
	   0,   0,   0,   0,   0,   0,   0,   0,
	   0,   0,   0,   0,   0,   0,   0,   0,
	   0,   0,   0,   0,   0,   0,   0,   0,
	   0,   0,   0,   0,   0,   0,   0,   0,
	   };
#define TURN_WIDTH_FACTOR 2
	// 1 を設定すると 2 を設定したときよりも一度に回転するブロックの数が多くなる
	// 二進 StartProcess(0x7CA9CC)/Process(0x7CAA68) の乗法常数 2 で確認。
//---------------------------------------------------------------------------
// GPU 路径用 gloss（角度歪み）テーブル: 二進 qword_1AD9150[4*phase + {0..3}]
// （guard byte_1AD9988、初回 Process GPU 分支で生成、0x7cb450〜）。
// phase 0..63 ごとに 4 double {a, b, 64-a, 64-b} を格納。phase>31 と <=31 で式が異なる。
// 上游 Win32 には無い kirikiroid2 専用。runtime 生成（静的初期化しない＝忠実複刻）。
static double TVPTurnGlossQuad[64][4];
static bool TVPTurnGlossQuadInited = false; // 二進 byte_1AD9988
//---------------------------------------------------------------------------
class tTVPTurnTransHandler : public iTVPDivisibleTransHandler
{
	//	'turn' トランジションハンドラクラスの実装

	tjs_int RefCount; // 参照カウンタ

protected:
	tjs_uint64 StartTick; // トランジションを開始した tick count
	tjs_uint64 Time; // トランジションに要する時間
	tjs_uint64 CurTime; // 現在の時間
	tjs_int Width; // 処理する画像の幅
	tjs_int Height; // 処理する画像の高さ
	tjs_int BGColor; // 背景色（R/B 交換後 = ABGR、D1）
	tjs_int Phase; // アニメーションのフェーズ
	bool First; // 一番最初の呼び出しかどうか

public:
	// Aligned with libkrkr2.so Provider::StartTransition inline ctor @0x7CBCA8
	// 二進: operator new(0x40) 後、Width(+40)/Height(+44)/Time(+24)/RefCount(+8)=1/
	// First(+56)=1/BGColor(+48) を書き込む。StartTick(+16)/CurTime(+32)/Phase(+52) は
	// 未初期化（StartProcess 初回で確定）。忠実複刻のため 0 初期化を補わない。
	// bgcolor は呼び出し側で R/B 交換済みを受け取る（D1、0x7cbe84）。
	tTVPTurnTransHandler(tjs_uint64 time, tjs_int width, tjs_int height, tjs_uint32 bgcolor)
	{
		RefCount = 1;

		Width = width;
		Height = height;
		Time = time;
		BGColor = bgcolor;

		First = true;
	}

	virtual ~tTVPTurnTransHandler()
	{
	}

	// Aligned with libkrkr2.so Handler::AddRef @0x7CBBC4
	tjs_error AddRef() override
	{
		RefCount ++;
		return TJS_S_OK;
	}

	// Aligned with libkrkr2.so Handler::Release @0x7CBBD8
	tjs_error Release() override
	{
		if(RefCount == 1)
			delete this;
		else
			RefCount--;
		return TJS_S_OK;
	}

	// Aligned with libkrkr2.so Handler::SetOption @0x7CBC10 (no-op)
	tjs_error SetOption(
			/*in*/iTVPSimpleOptionProvider *options) override
	{
		return TJS_S_OK;
	}

	tjs_error StartProcess(tjs_uint64 tick) override;

	tjs_error EndProcess() override;

	tjs_error Process(tTVPDivisibleData *data) override;

	// Aligned with libkrkr2.so Handler::MakeFinalImage @0x7CBC18
	tjs_error MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2) override
	{
		*dest = src2; // 常に最終画像は src2
		return TJS_S_OK;
	}
};
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPTurnTransHandler::StartProcess @0x7CA9CC
tjs_error tTVPTurnTransHandler::StartProcess(tjs_uint64 tick)
{
	// トランジションの画面更新一回ごとに呼ばれる

	if(First)
	{
		// 最初の実行
		First = false;
		StartTick = tick;
	}

	// 画像演算に必要なパラメータを計算
	// 左下から回転し始め、最後に右上が回転を終えるまで処理をする

	CurTime = (tick - StartTick);
	if(CurTime > Time) CurTime = Time;
	int xcount = (Width-1) / 64 + 1;
	int ycount = (Height-1) / 64 + 1;
	Phase = CurTime * (64 + (xcount +  ycount) *TURN_WIDTH_FACTOR) / Time -
		ycount *TURN_WIDTH_FACTOR;

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPTurnTransHandler::EndProcess @0x7CAA54
tjs_error tTVPTurnTransHandler::EndProcess()
{
	if(CurTime == Time) return TJS_S_FALSE; // トランジション終了

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so tTVPTurnTransHandler::Process @0x7CAA68
tjs_error tTVPTurnTransHandler::Process(
			tTVPDivisibleData *data)
{
	// トランジションの各領域ごとに呼ばれる
	//
	// 二進: if ( TVPIsSoftwareRenderer_guess() & 1 ) → 软件 table-warp 路径
	//                                            else → GPU mesh-warp 路径
	// 本地は wave/mosaic と同じく TVPGetRenderManager()->IsSoftware() で分発（同語義）。
	if(TVPGetRenderManager()->IsSoftware())
	{
		// ---- 软件 table-warp 路径（上游 turn.cpp::Process をそのまま、texture-based 化 D2/D3）----

		// 変数の準備
		tjs_uint8 *dest;
		tjs_int destpitch;
		const tjs_uint8 *src1;
		tjs_int src1pitch;
		const tjs_uint8 *src2;
		tjs_int src2pitch;
		// 二進: Dest は GetTextureForRender、Src1/Src2 は GetTexture を経由して
		// scanline(0)（dest=GetScanLineForWrite slot+72、src=GetScanLineForRead slot+64）+
		// pitch（纹理 GetPitch slot+80）。block アルゴリズムは pitch で跨行アドレッシング。
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

		// 1: その転送矩形に含まれるブロックの範囲を判定する
		int startx, starty;
		int endx, endy;

		startx = data->Left / 64;
		starty = data->Top / 64;
		endx = (data->Left + data->Width - 1) / 64;
		endy = (data->Top + data->Height - 1) / 64;

		// 2: 画面一番下のブロックはアクセスオーバーランに気をつけて転送する
		// 3: その範囲の左端と右端のブロックは、上下のクリッピングに加え、
		//    左右のクリッピングを行いながら転送する
		// 4: それ以外のブロックは上下のクリッピングのみを行いながら転送する

		for(int y = starty; y <= endy; y++)
		{
			for(int x = startx; x <= endx; x++)
			{
				tjs_int phase = Phase - (x - y) * TURN_WIDTH_FACTOR;
				if(phase < 0) phase = 0;
				if(phase > 63) phase = 63;
				tjs_int gl = gloss[phase];
				if(y * 64 + 64 >= Height || x == startx || x == endx)
				{
					// 下側がアクセスオーバーランの可能性がある
					// あるいは 左端 右端のブロック
					tjs_int l = (x) * 64;
					tjs_int t = (y) * 64;
					tjs_int r = l + 64;
					tjs_int b = t + 64;
					if(Clip(l, r, data->Left, data->Left + data->Width) &&
						Clip(t, b, data->Top, data->Top + data->Height))
					{
						// l, t, r, b は既にクリップされた領域を表している

						// phase を決定

						if(phase == 0)
						{
							// 完全に src1
							tjs_uint8 * dp = dest + (t + destyofs) * destpitch +
								(l + destxofs) * sizeof(tjs_uint32);
							const tjs_uint8 * sp = src1 + t * src1pitch +
								l * sizeof(tjs_uint32);
							tjs_int count = b - t;
							tjs_int len = (r - l) * sizeof(tjs_uint32);
							while(count--)
								memcpy(dp, sp, len), dp += destpitch, sp += src1pitch;
						}
						else if(phase == 63)
						{
							// 完全に src2
							tjs_uint8 * dp = dest + (t + destyofs) * destpitch +
								(l + destxofs) * sizeof(tjs_uint32);
							const tjs_uint8 * sp = src2 + t * src2pitch +
								l * sizeof(tjs_uint32);
							tjs_int count = b - t;
							tjs_int len = (r - l) * sizeof(tjs_uint32);
							while(count--)
								memcpy(dp, sp, len), dp += destpitch, sp += src2pitch;
						}
						else
						{
							// 転送パラメータとソースを決定
							const tTurnTransParams *params = TurnTransParams[phase];
							const tjs_uint8 * src;
							tjs_int srcpitch;
							if(phase < 32)
							{
								src = src1;
								srcpitch = src1pitch;
							}
							else
							{
								src = src2;
								srcpitch = src2pitch;
							}

							tjs_int line = t - y * 64;  // 開始ライン ( 0 .. 63 )
							tjs_int start = l - x * 64; // 左端の切り取られる部分 ( 0 .. 63 )
							tjs_int end = r - x * 64; // 右端

							params += line;

							src += x * 64 * sizeof(tjs_uint32);

							tjs_int count = b - t;
							tjs_uint8 *dp =
								(tjs_uint8*)
								((tjs_uint32*)(dest + (t + destyofs) * destpitch)
									+ x * 64 + destxofs);
							while(count --)
							{
								tjs_int fl, fr;

								// 左の背景
								fl = 0;
								fr = params->start;
								if(Clip(fl, fr, start, end))
								{
									// fl-fr を背景色で塗りつぶす
									TVPFillARGB((tjs_uint32*)dp + fl, fr - fl, BGColor);
								}

								// 右の背景
								fl = params->start + params->len;
								fr = 64;
								if(Clip(fl, fr, start, end))
								{
									// fl-fr を背景色で塗りつぶす
									TVPFillARGB((tjs_uint32*)dp + fl, fr - fl, BGColor);
								}

								// 変形転送
								fl = params->start;
								fr = params->start + params->len;
								if(Clip(fl, fr, start, end))
								{
									tjs_int sx = params->sx;
									tjs_int sy = params->sy;
									sx += params->stepx * (fl - params->start);
									sy += params->stepy * (fl - params->start);
									if(gl)
									{
										for(; fl < fr; fl++)
										{
											tjs_int yy = y * 64 + (sy >> 16);
											if(yy >= Height)
												((tjs_uint32*)dp)[fl] = BGColor;
											else
												((tjs_uint32*)dp)[fl] = Blend(
													*(const tjs_uint32*)
														(src + (sx >> 16) * sizeof(tjs_uint32) +
														yy * srcpitch),
														0xffffff, gl);
											sx += params->stepx;
											sy += params->stepy;
										}
									}
									else
									{
										for(; fl < fr; fl++)
										{
											tjs_int yy = y * 64 + (sy >> 16);
											if(yy >= Height)
												((tjs_uint32*)dp)[fl] = BGColor;
											else
												((tjs_uint32*)dp)[fl] =
													*(const tjs_uint32*)
														(src + (sx >> 16) * sizeof(tjs_uint32) +
														yy * srcpitch);
											sx += params->stepx;
											sy += params->stepy;
										}
									}
								}
								dp += destpitch;
								params ++;
							}
						}
					}
				}
				else
				{
					// 右端、左端、アクセスオーバーランには注意せずに転送
					tjs_int l = (x) * 64;
					tjs_int t = (y) * 64;
					tjs_int r = l + 64;
					tjs_int b = t + 64;
					if(Clip(t, b, data->Top, data->Top + data->Height))
					{
						// l, t, r, b は既にクリップされた領域を表している

						// phase を決定

						if(phase == 0)
						{
							// 完全に src1
							tjs_uint8 * dp = dest + (t + destyofs) * destpitch +
								(l + destxofs) * sizeof(tjs_uint32);
							const tjs_uint8 * sp = src1 + t * src1pitch +
								l * sizeof(tjs_uint32);
							tjs_int count = b - t;
							tjs_int len = (r - l) * sizeof(tjs_uint32);
							while(count--)
								memcpy(dp, sp, len), dp += destpitch, sp += src1pitch;
						}
						else if(phase == 63)
						{
							// 完全に src2
							tjs_uint8 * dp = dest + (t + destyofs) * destpitch +
								(l + destxofs) * sizeof(tjs_uint32);
							const tjs_uint8 * sp = src2 + t * src2pitch +
								l * sizeof(tjs_uint32);
							tjs_int count = b - t;
							tjs_int len = (r - l) * sizeof(tjs_uint32);
							while(count--)
								memcpy(dp, sp, len), dp += destpitch, sp += src2pitch;
						}
						else
						{
							// 転送パラメータとソースを決定
							const tTurnTransParams *params = TurnTransParams[phase];
							const tjs_uint8 * src;
							tjs_int srcpitch;
							if(phase < 32)
							{
								src = src1;
								srcpitch = src1pitch;
							}
							else
							{
								src = src2;
								srcpitch = src2pitch;
							}

							tjs_int line = t - y * 64;  // 開始ライン ( 0 .. 63 )

							params += line;

							src += l * sizeof(tjs_uint32) + y * 64 * srcpitch;

							tjs_int count = b - t;
							tjs_uint8 *dp =
								(tjs_uint8*)
								((tjs_uint32*)(dest + (t + destyofs) * destpitch)
									+ l + destxofs);
							while(count --)
							{
								tjs_int fl, fr;

								// 左の背景
								// 0-params->start を背景色で塗りつぶす
								TVPFillARGB((tjs_uint32*)dp + 0, params->start, BGColor);

								// 右の背景
								fl = params->start + params->len;
								// fl-64 を背景色で塗りつぶす
								TVPFillARGB((tjs_uint32*)dp + fl, 64 - fl, BGColor);

								// 変形転送
								fl = params->start;
								fr = params->start + params->len;
								tjs_int sx = params->sx;
								tjs_int sy = params->sy;
								if(gl)
								{
									for(; fl < fr; fl++)
									{
										((tjs_uint32*)dp)[fl] = Blend(
											*(const tjs_uint32*)
												(src + (sx >> 16) * sizeof(tjs_uint32) +
												(sy >> 16) * srcpitch),
												0xffffff, gl);
										sx += params->stepx;
										sy += params->stepy;
									}
								}
								else
								{
									TVPLinTransCopy((tjs_uint32*)dp + fl, fr - fl,
										(const tjs_uint32*)src, sx, sy,
										params->stepx, params->stepy, srcpitch);
								}
								dp += destpitch;
								params ++;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		// ---- GPU mesh-warp 路径（二進 0x7cb428〜、上游 Win32 に無い kirikiroid2 専用）----
		// 1) gloss 角度テーブル（double quad）を初回のみ生成（二進 byte_1AD9988 guard）
		// 2) FillARGB で処理矩形全体を BGColor で填める
		// 3) tile ごとに gloss[phase] で歪めた quad を "extrans_turn_Blend" shader で
		//    OperateTriangles メッシュ blend する

		// 1) gloss 角度テーブル生成（二進 0x7cb450〜、qword_1AD9150[4*phase]）
		if(!TVPTurnGlossQuadInited)
		{
			TVPTurnGlossQuadInited = true;
			for(int n = 0; n < 64; n++)
			{
				int a, b;
				if(n > 31)
				{
					// 二進 0x7cb4cc: v115 = n*n/31;
					// v116 = vcvtd_n_s64_f64(sin(v115*π*0.015625), 2)
					//      = (int64)(sin(...) * 2^2) （ゼロ方向丸め＝truncate）
					// v113 = v115 + v116; v114 = 64 - v115 + v116（v116 を直接加算）
					int v = n * n / 31;
					int o = (int)(sin((double)v * 3.14159265 * 0.015625) * 4.0);
					a = v + o;
					b = 64 - v + o;
				}
				else
				{
					// 二進 0x7cb48c: v111 = n*n/31;
					// v112 = vcvtd_n_s64_f64(sin(v111*π*0.015625), 2)
					// v113 = v111 - v112; v114 = 64 - v111 - v112
					int v = n * n / 31;
					int o = (int)(sin((double)v * 3.14159265 * 0.015625) * 4.0);
					a = v - o;
					b = 64 - v - o;
				}
				TVPTurnGlossQuad[n][0] = (double)a;
				TVPTurnGlossQuad[n][1] = (double)b;
				TVPTurnGlossQuad[n][2] = 64.0 - (double)a;
				TVPTurnGlossQuad[n][3] = 64.0 - (double)b;
			}
		}

		iTVPRenderManager *rm = TVPGetRenderManager();

		// 2) FillARGB で処理矩形全体を BGColor で填める（二進 0x7cb5c8〜）
		// "FillARGB" は builtin render method（GLSL を渡さず名前のみで取得）。
		iTVPTexture2D *desttex = data->Dest->GetTextureForRender();
		iTVPRenderMethod *fillmethod = rm->GetRenderMethod("FillARGB");
		int fillcolor_id = fillmethod->EnumParameterID("color");
		fillmethod->SetParameterColor4B(fillcolor_id, BGColor);
		rm->OperateRect(
			fillmethod, desttex, nullptr,
			tTVPRect(data->DestLeft, data->DestTop,
					 data->DestLeft + data->Width,
					 data->DestTop + data->Height),
			tRenderTexRectArray());

		// 3) tile ごとに歪めた quad をメッシュ blend（二進 0x7cb674〜）
		// tile 範囲（上游と同じ block 範囲判定。二進 0x7cb674〜0x7cb6d4）
		int startx, starty;
		int endx, endy;
		startx = data->Left / 64;
		starty = data->Top / 64;
		endx = (data->Left + data->Width - 1) / 64;
		endy = (data->Top + data->Height - 1) / 64;

		// 二進 v142 = DestLeft-Left、v216 = DestTop-Top（int）、v144/v145 = (double)同
		tjs_int destxofs = data->DestLeft - data->Left; // 二進 v142
		tjs_int destyofs = data->DestTop - data->Top;   // 二進 v216
		double destxofsd = (double)destxofs;            // 二進 v144
		double destyofsd = (double)destyofs;            // 二進 v145

		for(int y = starty; y <= endy; y++)
		{
			for(int x = startx; x <= endx; x++)
			{
				// 二進 0x7cb7d4: phase = max(Phase + 2*(y-x), 0) を 63 で頭打ち
				tjs_int phase = Phase - (x - y) * TURN_WIDTH_FACTOR;
				if(phase < 0) phase = 0;
				if(phase > 63) phase = 63;

				// tile の絶対外接矩形（dest-source 共通の画像座標、l/t = x*64 / y*64）
				tjs_int l = x * 64; // 二進 v147
				tjs_int t = y * 64; // 二進 v149 = v205<<6

				// 歪み角度テーブル（二進 v155 = &qword_1AD9150[4*phase]）
				const double *g = TVPTurnGlossQuad[phase]; // {a, b, 64-a, 64-b}
				double ga = g[0];     // 二進 v158
				double gb = g[1];     // 二進 v157
				double gna = g[2];    // 二進 v156 = 64-a
				double gnb = g[3];    // 二進 v160 = 64-b

				// src tile 矩形（二進 v161 = (phase>=32)?src2:src1、v163/v162=left/top）
				iTVPScanLineProvider *srcprov;
				tjs_int srcl, srct;
				if(phase >= 32)
				{
					srcprov = data->Src2;
					srcl = data->Src2Left; // 二進 v222[0]
					srct = data->Src2Top;  // 二進 v222[1]
				}
				else
				{
					srcprov = data->Src1;
					srcl = data->Src1Left; // 二進 v223[0]
					srct = data->Src1Top;  // 二進 v223[1]
				}
				tjs_int sx = l + srcl; // 二進 v164 = v147 + v163

				// dest 6 頂点（2 三角形＝歪めた quad、二進 v238..v249）
				// pt0=lt, pt1=rt-warp, pt2=lt-warp, pt3=pt1, pt4=pt2, pt5=rb
				tTVPPointD pttar[6];
				pttar[0].x = (double)(destxofs + l);          // v238
				pttar[0].y = (double)(t + destyofs);          // v239
				pttar[1].x = gna + (double)l + destxofsd;     // v240 = (64-a)+l+ofs
				pttar[1].y = gnb + (double)t + destyofsd;     // v241 = (64-b)+t+ofs
				pttar[2].x = ga + (double)l + destxofsd;      // v242 = a+l+ofs
				pttar[2].y = gb + (double)t + destyofsd;      // v243 = b+t+ofs
				pttar[3] = pttar[1];                          // v244/v245 = v240/v241
				pttar[4] = pttar[2];                          // v246/v247 = v242/v243
				pttar[5].x = (double)(destxofs + l + 64);     // v248
				pttar[5].y = (double)(t + 64 + destyofs);     // v249 = (t+64)+ofs

				// src 6 頂点（二進 v226..v237、tile の元矩形）
				tTVPPointD srcquad[6];
				srcquad[0].x = (double)sx;          // v226
				srcquad[0].y = (double)(srct + t);  // v227 = v162 + v149
				srcquad[1].x = (double)(sx + 64);   // v228
				srcquad[1].y = srcquad[0].y;        // v229
				srcquad[2].x = (double)sx;          // v230
				srcquad[2].y = (double)(srct + t + 64); // v231 = v162 + (v149+64)
				srcquad[3].x = srcquad[1].x;        // v232 = v228
				srcquad[3].y = srcquad[0].y;        // v233 = v227
				srcquad[4].x = (double)sx;          // v234 = v164
				srcquad[4].y = srcquad[2].y;        // v235 = v231
				srcquad[5].x = srcquad[1].x;        // v236 = v228
				srcquad[5].y = srcquad[2].y;        // v237 = v231

				// rcclip = 処理矩形と tile の積矩形（dest 座標、二進 0x7cb880〜の clip）
				// v165 = max(l, Left)、v166 = min(l+64, Left+Width)
				tjs_int cl = l;
				if(cl < data->Left) cl = data->Left;          // 二進 v165
				tjs_int cr = l + 64;
				if(cr > data->Left + data->Width) cr = data->Left + data->Width; // v166
				if(cl >= cr) continue;                         // v165 < v166
				// v167 = max(t, Top)、v168 = min(t+64, Top+Height)
				tjs_int ct = t;
				if(ct < data->Top) ct = data->Top;            // 二進 v167
				tjs_int cb = t + 64;
				if(cb > data->Top + data->Height) cb = data->Top + data->Height; // v168
				if(ct >= cb) continue;                         // v167 < v168

				// "extrans_turn_Blend" shader（二進 0x7cb90c、字节級 shader 串）
				iTVPRenderMethod *method = rm->GetOrCompileRenderMethod(
					"extrans_turn_Blend", nullptr,
					"uniform vec4 color;\n"
					"uniform float opacity;\n"
					"void main() {\n"
					"    gl_FragColor = mix(texture2D(tex0, v_texCoord0), color, opacity);\n"
					"}",
					1, 0);
				int color_id = method->EnumParameterID("color");     // 二進 dword_1AD9960
				int opacity_id = method->EnumParameterID("opacity");  // 二進 dword_1AD9970
				// 二進 0x7cb9f0: color = 0xFFFFFFFF、0x7cba10: opacity = gloss[phase]
				//（整数 dword_14E4BD4[phase] = v174）
				method->SetParameterColor4B(color_id, 0xFFFFFFFF);
				method->SetParameterOpa(opacity_id, gloss[phase]);

				// 二進 v178 = rm vtable+168 = OperateTriangles（nTriangles=2）
				// rcclip = {v165+v142, v167+v216, v166+v142, v168+v216}
				tRenderTexQuadArray::Element textures[] = {
					tRenderTexQuadArray::Element(srcprov->GetTexture(), srcquad)
				};
				rm->OperateTriangles(
					method, 2, desttex, nullptr,
					tTVPRect(cl + destxofs, ct + destyofs,
							 cr + destxofs, cb + destyofs),
					pttar, tRenderTexQuadArray(textures));
			}
		}
	}

	return TJS_S_OK;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
class tTVPTurnTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount; // 参照カウンタ
public:
	tTVPTurnTransHandlerProvider() { RefCount = 1; }
	~tTVPTurnTransHandlerProvider() override {; }

	// Aligned with libkrkr2.so Provider::AddRef（off_1A25B68 slot0）
	tjs_error AddRef() override
	{
		RefCount ++;
		return TJS_S_OK;
	}

	// Aligned with libkrkr2.so Provider::Release（off_1A25B68 slot1）
	tjs_error Release() override
	{
		if(RefCount == 1)
			delete this;
		else
			RefCount--;
		return TJS_S_OK;
	}

	// Aligned with libkrkr2.so Provider::GetName @0x7CBC90 → L"turn"
	tjs_error GetName(
			/*out*/const tjs_char ** name) override
	{
		if(name) *name = TJS_W("turn");
		return TJS_S_OK;
	}

	// Aligned with libkrkr2.so Provider::StartTransition @0x7CBCA8
	tjs_error StartTransition(
			/*in*/iTVPSimpleOptionProvider *options, // option provider
			/*in*/iTVPSimpleImageProvider *imagepro, // image provider
			/*in*/tTVPLayerType layertype, // destination layer type
			/*in*/tjs_uint src1w, tjs_uint src1h, // source 1 size
			/*in*/tjs_uint src2w, tjs_uint src2h, // source 2 size
			/*out*/tTVPTransType *type, // transition type
			/*out*/tTVPTransUpdateType * updatetype, // update typwe
			/*out*/iTVPBaseTransHandler ** handler // transition handler
			) override
	{
		if(type) *type = ttExchange; // transition type : exchange
		if(updatetype) *updatetype = tutDivisible;
			// update type : divisible
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;

		if(src1w != src2w || src1h != src2h)
			return TJS_E_FAIL; // src1 と src2 のサイズが一致していないと駄目


		// オプションを得る
		tTJSVariant tmp;
		tjs_uint64 time;
		tjs_uint32 bgcolor = 0;

		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp)))
			return TJS_E_FAIL; // time 属性が指定されていない
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		time = (tjs_int64)tmp;
		if(time < 2) time = 2; // 二進: time<2→2（最小 Time=2、0x7cbdb8）

		if(TJS_SUCCEEDED(options->GetValue(TJS_W("bgcolor"), &tmp)))
		if(tmp.Type() != tvtVoid) bgcolor = (tjs_int)tmp;

		// D1: bgcolor を R/B 交換して handler の BGColor 字段へ（0x7cbe84）。
		// c & 0xFF00FF00 | BYTE2(c) | ((u8)c<<16)
		bgcolor = (bgcolor & 0xFF00FF00) | ((bgcolor >> 16) & 0xFF) |
			((bgcolor & 0xFF) << 16);

		// オブジェクトを作成
		*handler = new tTVPTurnTransHandler(time, src1w, src1h, bgcolor);

		return TJS_S_OK;
	}

} static * TurnTransHandlerProvider;
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so RegisterTurnTransHandlerProvider @0x7CBB5C
void RegisterTurnTransHandlerProvider()
{
	TurnTransHandlerProvider = new tTVPTurnTransHandlerProvider();
	TVPAddTransHandlerProvider(TurnTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterTurnTransHandlerProvider()
{
	TVPRemoveTransHandlerProvider(TurnTransHandlerProvider);
	TurnTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
