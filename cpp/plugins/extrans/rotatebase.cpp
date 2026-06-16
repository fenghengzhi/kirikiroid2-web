//---------------------------------------------------------------------------
// extrans.dll — rotate 系トランジション基底クラス実装
//
// 実現基底 = 上游 krkrz/SamplePlugin/extrans/rotatebase.cpp（W.Dee）。
// libkrkr2.so 反編譯為差分裁判。詳細地址索引は rotatebase.h を参照。
//
// kirikiroid2 delta（相对上游 Win32）:
//   D1. Process / AddSource 双路径（IsSoftware() 分発）。软件路径 = 上游そのまま
//       （texture-based 化：scanline は GetTexture()/GetTextureForRender() 経由、
//       pitch は纹理 GetPitch、二進 sub_7C80E0/sub_7C88B0）。
//       GPU 路径 = 上游 Win32 に無い kirikiroid2 専用：
//         - AddSource(GPU) が quad 顶点を GPUQuads に push（二進 0x7c8a14〜）。
//         - Process(GPU) が "FillARGB" で region 背景填め（OperateRect）+
//           quad ごとに "Copy" render-method で OperateTriangles(nTriangles=2)
//           メッシュ転送（二進 0x7c83f4〜）。
//   D2. bgcolor R/B swizzle 無し（rotate 系は原様、wave/turn と異なる）。
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <math.h>
#include <string.h>

#include "rotatebase.h"
#include "common.h"
#include "transhandler.h"
#include "TransIntf.h"
#include "tvpgl.h"          // TVPFillARGB / TVPStretchCopy / TVPLinTransCopy
#include "RenderManager.h"  // iTVPRenderManager / iTVPTexture2D / OperateRect / OperateTriangles

#include <stdio.h>

//---------------------------------------------------------------------------
/*
	切り替え元、切り替え先の画像をクルクル回す系のトランジション用の基底クラス
	の実装
*/
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so 基類 ctor @0x7C7F30
// 二進: GPU vector(+72/+80) を {0,0,0} に、Width(+40)/Height(+44)/Time(+24)/
// BGColor(+48) を書き込み、RefCount(+8)=1、vtable(+0)、
// DrawData(+64)=operator new[](104*Height)、First(+56)=1。
// CurTime(+32)/StartTick(+16)/Phase(+52) は未初期化（忠実複刻、0 を補わない）。
tTVPBaseRotateTransHandler::tTVPBaseRotateTransHandler(tjs_uint64 time,
	tjs_int width, tjs_int height, tjs_uint32 bgcolor)
{
	RefCount = 1;
	Width = width;
	Height = height;
	Time = time;
	BGColor = bgcolor;

	DrawData = new tRotateDrawData[height];

	First = true;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so 基類 dtor @0x7C7F9C（delete[] DrawData）
tTVPBaseRotateTransHandler::~tTVPBaseRotateTransHandler()
{
	delete [] DrawData;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so 基類 StartProcess @0x7C7FEC
tjs_error tTVPBaseRotateTransHandler::StartProcess(tjs_uint64 tick)
{
	// トランジションの画面更新一回ごとに呼ばれる

	if(First)
	{
		// 最初の実行
		First = false;
		StartTick = tick;
	}

	// 画像演算に必要なパラメータを計算
	CurTime = (tick - StartTick);
	if(CurTime > Time) CurTime = Time;


	// データをクリア
	for(tjs_int i = 0; i < Height; i++)
	{
		// 背景でクリア
		DrawData[i].count = 1;
		DrawData[i].region[0].left = 0;
		DrawData[i].region[0].right = Width;
		DrawData[i].region[0].type = 0; // 0 = 背景
	}

	// GPU 路径用顶点 list をクリア（二進 a1[10]=a1[9]＝end=begin）
	GPUQuads.clear();

	CalcPosition(); // 下位クラスの CalcPosition メソッドを呼ぶ

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so 基類 EndProcess @0x7C80CC
tjs_error tTVPBaseRotateTransHandler::EndProcess()
{
	if(CurTime == Time) return TJS_S_FALSE; // トランジション終了

	return TJS_S_TRUE;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so 基類 Process @0x7C80E0（软件 region-walk / GPU mesh 双路径）
tjs_error tTVPBaseRotateTransHandler::Process(
			tTVPDivisibleData *data)
{
	// トランジションの各領域ごとに呼ばれる
	//
	// 二進: if ( TVPIsSoftwareRenderer_guess() & 1 ) → 软件 region-walk 路径
	//                                            else → GPU mesh 路径
	if(TVPGetRenderManager()->IsSoftware())
	{
		// ---- 软件路径（= 上游基類 Process をそのまま、texture-based 化 D1）----

		// 変数の準備
		tjs_int destxofs = data->DestLeft - data->Left;
	//	tjs_int destyofs = data->DestTop - data->Top;

		tjs_uint8 *dest;
		tjs_int destpitch;
		const tjs_uint8 *src1;
		tjs_int src1pitch;
		const tjs_uint8 *src2;
		tjs_int src2pitch;
		// 二進: Dest は GetTextureForRender、Src1/Src2 は GetTexture を経由して
		// scanline(0)（dest=GetScanLineForWrite slot+72、src=GetScanLineForRead slot+64）+
		// pitch（纹理 GetPitch slot+80）。
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

		// 各ラインごとに転送
		tjs_int h = data->Height;
		tjs_int y = data->Top;
		while(h--)
		{
			tRotateDrawData *line = DrawData + y;

			for(tjs_int i = 0; i < line->count; i++)
			{
				// 各領域ごとに
				if(line->region[i].left == line->region[i].right) continue;
				tjs_int l = line->region[i].left;
				tjs_int r = line->region[i].right;
				if(Clip(l, r, data->Left, data->Left + data->Width))
				{
					// l, r は data->Left, data->Width でクリップされた結果
					// 残った
					tjs_int type = line->region[i].type;
					if(type == 0)
					{
						// 背景
						TVPFillARGB((tjs_uint32*)dest + destxofs + l, r - l, BGColor);
					}
					else
					{
						// src1 または src2
						const tRotateDrawLine & drawline
							= (type == 1) ? line->src1 : line->src2;

						// 転送
						if(drawline.stepx == 65536 && drawline.stepy == 0)
						{
							// そのまま転送
							memcpy((tjs_uint32*)dest + destxofs + l,
								(const tjs_uint32*)((type == 1 ? src1 : src2) +
								((drawline.sy + (l - drawline.start) * drawline.stepy) >> 16)*
								(type == 1 ? src1pitch : src2pitch)) +
								((drawline.sx + (l - drawline.start) * drawline.stepx) >> 16),
								(r - l) * sizeof(tjs_uint32));
						}
						else if(drawline.stepy == 0)
						{
							// 拡大縮小転送
							TVPStretchCopy((tjs_uint32*)dest + destxofs + l,
								r - l,
                            (const tjs_uint32*)((type == 1 ? src1 : src2) +
								((drawline.sy + (l - drawline.start) * drawline.stepy) >> 16)*
								(type == 1 ? src1pitch : src2pitch)),
								(drawline.sx + (l - drawline.start) * drawline.stepx),
								drawline.stepx);
						}
						else
						{
							// 線形変形転送
							TVPLinTransCopy((tjs_uint32*)dest + destxofs + l,
								r - l,
								(const tjs_uint32*)(type == 1 ? src1 : src2),
								drawline.sx + (l - drawline.start) * drawline.stepx,
								drawline.sy + (l - drawline.start) * drawline.stepy,
								drawline.stepx,
								drawline.stepy,
								type == 1 ? src1pitch : src2pitch);
						}
					}
				}
			}

			dest += destpitch;
			y++;
		}
	}
	else
	{
		// ---- GPU mesh 路径（二進 0x7c83f4〜、上游 Win32 に無い kirikiroid2 専用）----
		// 1) "FillARGB" で処理矩形を BGColor で填める（OperateRect、二進 v104/v69）。
		// 2) GPUQuads（AddSource が push した quad）ごとに "Copy" render-method で
		//    歪めた quad を OperateTriangles(nTriangles=2) メッシュ転送。

		iTVPRenderManager *rm = TVPGetRenderManager();
		iTVPTexture2D *desttex = data->Dest->GetTextureForRender();

		// 1) FillARGB 背景（二進 v104 = {DestLeft, DestTop, DestLeft+Width, DestTop+Height}、
		//    method "FillARGB"(qword_1AD90E8), param "color"(dword_1AD90F8) = BGColor、
		//    OperateRect(vtable+160) tRenderTexRectArray()）
		iTVPRenderMethod *fillmethod = rm->GetRenderMethod("FillARGB");
		int fillcolor_id = fillmethod->EnumParameterID("color");
		fillmethod->SetParameterColor4B(fillcolor_id, BGColor);
		rm->OperateRect(
			fillmethod, desttex, nullptr,
			tTVPRect(data->DestLeft, data->DestTop,
					 data->DestLeft + data->Width,
					 data->DestTop + data->Height),
			tRenderTexRectArray());

		// src1/src2 の絶対矩形（二進 v103/v102）
		// v103 = {Src1Left, Src1Top, Src1Left+Width, Src1Top+Height}
		// v102 = {Src2Left, Src2Top, Src2Left+Width, Src2Top+Height}
		tjs_int s1l = data->Src1Left;
		tjs_int s1t = data->Src1Top;
		tjs_int s1r = data->Width + s1l;
		tjs_int s1b = data->Height + s1t;
		tjs_int s2l = data->Src2Left;
		tjs_int s2t = data->Src2Top;
		tjs_int s2r = data->Width + s2l;
		tjs_int s2b = data->Height + s2t;

		// 2) "Copy" render-method（builtin、二進 qword_1AD9108）
		iTVPRenderMethod *copymethod = rm->GetRenderMethod("Copy");

		// 二進 v104 = {DestLeft, DestTop, DestLeft+Width, DestTop+Height}（OperateTriangles の rcclip）
		tTVPRect rcclip(data->DestLeft, data->DestTop,
						data->DestLeft + data->Width,
						data->DestTop + data->Height);

		// GPUQuads ごとに OperateTriangles（二進 0x7c857c〜 do-while）
		for(size_t qi = 0; qi < GPUQuads.size(); qi++)
		{
			const tRotateGPUQuad &q = GPUQuads[qi];

			// dest 6 頂点（2 三角形＝歪めた quad、二進 v107..v118）
			// 上位 3 点 {x0,y0}/{x1,y1}/{x2,y2} から 4 隅 quad を構成
			// （v78=x1+1、v112=y2+1、第4隅 = (x1+1-x0+x2, y1+1-y0+y2)）
			tjs_int x0 = q.x0, y0 = q.y0;
			tjs_int x1 = q.x1, y1 = q.y1;
			tjs_int x2 = q.x2, y2 = q.y2;
			double dx0 = (double)x0;            // v107
			double dy0 = (double)y0;            // v108
			double dx1 = (double)(x1 + 1);      // v109/v113 = v78
			double dy1 = (double)y1;            // v110/v114 = v80
			double dx2 = (double)x2;            // v111/v115 = v82
			double dy2 = (double)(y2 + 1);      // v112/v116 = v83+1
			double dx3 = (double)((x1 + 1) - x0 + x2); // v117 = v81+v82
			double dy3 = (double)(y1 + 1 - y0 + y2);   // v118
			tTVPPointD pttar[6];
			pttar[0].x = dx0; pttar[0].y = dy0; // v107/v108
			pttar[1].x = dx1; pttar[1].y = dy1; // v109/v110
			pttar[2].x = dx2; pttar[2].y = dy2; // v111/v112
			pttar[3].x = dx1; pttar[3].y = dy1; // v113/v114
			pttar[4].x = dx2; pttar[4].y = dy2; // v115/v116
			pttar[5].x = dx3; pttar[5].y = dy3; // v117/v118

			// src 6 UV（二進 v106[0..11]、type==1→src1 矩形(v103) else src2 矩形(v102)）
			iTVPScanLineProvider *srcprov;
			double sl, st, sr, sb;
			if(q.type == 1)
			{
				srcprov = data->Src1;
				sl = (double)s1l; st = (double)s1t; sr = (double)s1r; sb = (double)s1b;
			}
			else
			{
				srcprov = data->Src2;
				sl = (double)s2l; st = (double)s2t; sr = (double)s2r; sb = (double)s2b;
			}
			tTVPPointD srcquad[6];
			srcquad[0].x = sl; srcquad[0].y = st; // v106[0]/[1]
			srcquad[1].x = sr; srcquad[1].y = st; // v106[2]/[3]
			srcquad[2].x = sl; srcquad[2].y = sb; // v106[4]/[5]
			srcquad[3].x = sr; srcquad[3].y = st; // v106[6]/[7]
			srcquad[4].x = sl; srcquad[4].y = sb; // v106[8]/[9]
			srcquad[5].x = sr; srcquad[5].y = sb; // v106[10]/[11]

			// 二進 v92 = rm vtable+168 = OperateTriangles（nTriangles=2、v101={textures,1}）
			tRenderTexQuadArray::Element textures[] = {
				tRenderTexQuadArray::Element(srcprov->GetTexture(), srcquad)
			};
			rm->OperateTriangles(
				copymethod, 2, desttex, nullptr,
				rcclip, pttar, tRenderTexQuadArray(textures));
		}
	}

	return TJS_S_OK;
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so AddLine @0x7C872C
void tTVPBaseRotateTransHandler::AddLine(tjs_int line,
	tjs_int left, tjs_int right, tjs_int type)
{
	// line で示したラインに left と right で示した領域を type で上書きする
	// 領域と left, right によっていろいろ処理が変わる
	// 領域が left, right よりも左右にはみ出ている場合
	//    →その領域を２つに分割し、真ん中に left, right を挿入
	// 領域が left, right の左右のどちらかにはみ出ている場合
	//    →その領域をクリップし、右か左に left, right を挿入
	// left, right が領域を完全に内包する場合
	//    →その領域を削除

	if(Clip(left, right, 0, Width))
	{
		tRotateDrawData *data = DrawData + line;
		tjs_int i;
		for(i = 0; i < data->count; i++)
		{
			if(data->region[i].left == data->region[i].right) continue;

			if(data->region[i].left >= left && data->region[i].right <= right)
			{
				// left, right が領域を完全に内包する場合
				//    →その領域を削除
				data->region[i].right = data->region[i].left;
					// 一時的に長さを 0 にする ( あとでここに新しく挿入されるか、
					// あるいはそのまま放置される )

				continue;
			}

			if(data->region[i].left < left && data->region[i].right > right)
			{
				// 領域が left, right よりも左右にはみ出ている場合
				//    →その領域を２つに分割し、真ん中に left, right を挿入

				// 新しい領域に分割される右側を作成
				data->region[data->count].left = right;
				data->region[data->count].right = data->region[i].right;
				data->region[data->count].type = data->region[i].type;

				data->count++;

				// 分割される左側の領域の right をカット
				data->region[i].right = left;


				// これ以上処理は必要ないのでループから抜ける
				break;
			}

			if(data->region[i].left < left && data->region[i].right > left)
			{
				// 領域が left, right の左右のどちらかにはみ出ている場合
				//    →その領域をクリップし、右か左に left, right を挿入

				// 領域の右側をカット
				data->region[i].right = left;

				continue;
			}

			if(data->region[i].left < right && data->region[i].right > right)
			{
				// 領域が left, right の左右のどちらかにはみ出ている場合
				//    →その領域をクリップし、右か左に left, right を挿入

				// 領域の左側をカット
				data->region[i].left = right;

				continue;
			}

		}

		// left, right を挿入するために空きを探す
		for(i = 0; i < data->count; i++)
		{
			if(data->region[i].left == data->region[i].right) break; // 空き
		}	// 空きが見つからなかった場合は i == data->count

		// データを作成
		data->region[i].left = left;
		data->region[i].right = right;
		data->region[i].type = type;

		// 新規に追加された場合は カウントを増やす
		if(i == data->count) data->count++;
	}
}
//---------------------------------------------------------------------------
// Aligned with libkrkr2.so AddSource @0x7C88B0（软件光栅化 / GPU push quad 双路径）
void tTVPBaseRotateTransHandler::AddSource(const tPoint *points, tjs_int type)
{
	// type ( 1 = src1, 2 = src2 ) で表されたソースを、points の３点で示された
	// 位置に変形転送するように設定する。

	// 吉里吉里本体のソースの LayerBitmapIntf.cpp から引っ張ってきた

	// 二進 0x7c88e8: GPU 渲染器なら光栅化せず quad 顶点を GPUQuads に push して即返る
	//（二進 0x7c8a14〜: 28B レコード {type, points[0..2]} を vertex vector に追加）。
	if(!TVPGetRenderManager()->IsSoftware())
	{
		tRotateGPUQuad q;
		q.type = type;
		q.x0 = points[0].x; q.y0 = points[0].y;
		q.x1 = points[1].x; q.y1 = points[1].y;
		q.x2 = points[2].x; q.y2 = points[2].y;
		GPUQuads.push_back(q);
		return;
	}

	// ---- 软件路径（= 上游 AddSource 扫描线光栅化、二進 0x7c88f4〜）----

	// vertex points
	tjs_int points_x[4];
	tjs_int points_y[4];

	// check each vertex and find most-top/most-bottom/most-left/most-right points
	tjs_int scanlinestart, scanlineend; // most-top/most-bottom
	tjs_int leftlimit, rightlimit; // most-left/most-right
	tjs_int toppoint, bottompoint;

	// - upper-left
	points_x[0] = points[0].x;
	points_y[0] = points[0].y;
	leftlimit = points_x[0]; //, leftpoint = 0;
	rightlimit = points_x[0]; //, rightpoint = 0;
	scanlinestart = points_y[0], toppoint = 0;
	scanlineend = points_y[0], bottompoint = 0;

	// - upper-right
	points_x[1] = points[1].x;
	points_y[1] = points[1].y;
	if(leftlimit > points_x[1]) leftlimit = points_x[1];
	if(rightlimit < points_x[1]) rightlimit = points_x[1];
	if(scanlinestart > points_y[1]) scanlinestart = points_y[1], toppoint = 1;
	if(scanlineend < points_y[1]) scanlineend = points_y[1], bottompoint = 1;

	// - bottom-right
	points_x[2] = points[1].x - points[0].x + points[2].x;
	points_y[2] = points[1].y - points[0].y + points[2].y;
	if(leftlimit > points_x[2]) leftlimit = points_x[2];
	if(rightlimit < points_x[2]) rightlimit = points_x[2];
	if(scanlinestart > points_y[2]) scanlinestart = points_y[2], toppoint = 2;
	if(scanlineend < points_y[2]) scanlineend = points_y[2], bottompoint = 2;

	// - bottom-left
	points_x[3] = points[2].x;
	points_y[3] = points[2].y;
	if(leftlimit > points_x[3]) leftlimit = points_x[3];
	if(rightlimit < points_x[3]) rightlimit = points_x[3];
	if(scanlinestart > points_y[3]) scanlinestart = points_y[3], toppoint = 3;
	if(scanlineend < points_y[3]) scanlineend = points_y[3], bottompoint = 3;

	// check destrect intersections
	if(leftlimit >= Width) return;
	if(rightlimit < 0) return;
	if(scanlinestart >= Height) return;
	if(scanlineend < 0) return;

	// prepare to transform...
	tjs_int pd, pa, pdnext, panext;
	tjs_int pdstepx, pastepx;
	tjs_int pdx, pax;
	tjs_int sdstep, sastep;
	tjs_int sd, sa;
	tjs_int y = 0 < scanlinestart ? scanlinestart : 0;
	tjs_int ylim = (Height-1) < scanlineend ? (Height-1) : scanlineend;

	// skip to the first scanline

	// - for descent
	pd = pdnext = toppoint;
	pdnext --;
	pdnext &= 3;

	while(pdnext != bottompoint && points_y[pdnext] < y)
	{
		pdnext--;
		pdnext &= 3; // because pd, pdnext, pa and panext take ring of 0..3
		pd--;
		pd &= 3;
	}

	while(pdnext != bottompoint && points_y[pdnext] == y)
	{
		pd--;
		pdnext--;
		pd &= 3;
		pdnext &= 3;
	}

	// - for ascent
	pa = panext = toppoint;
	panext ++;
	panext &= 3;

	while(panext != bottompoint && points_y[panext] < y)
	{
		panext++;
		panext &= 3;
		pa++;
		pa &= 3;
	}

	while(panext != bottompoint && points_y[panext] == y)
	{
		pa++;
		panext++;
		pa &= 3;
		panext &= 3;
	}

	// compute initial horizontal step per a line

	// - for descent
	if(points_y[pdnext] - points_y[pd] + 1)
		pdstepx = 65536 * (points_x[pdnext] - points_x[pd]) /
			(points_y[pdnext] - points_y[pd] + 1);
	else
		pdstepx = 65536;

	// - for ascent
	if(points_y[panext] - points_y[pa] + 1)
		pastepx = 65536 * (points_x[panext] - points_x[pa]) /
			(points_y[panext] - points_y[pa] + 1);
	else
		pastepx = 65536;


	// compute initial source step

	// - for descent
	if(points_y[pdnext] - points_y[pd] + 1)
	{
		switch(pd)
		{
		case 0: sdstep = 65536 * (Height) /
				(points_y[pdnext] - points_y[pd] + 1); break;
		case 1: sdstep = 65536 * (-Width) /
				(points_y[pdnext] - points_y[pd] + 1); break;
		case 2: sdstep = 65536 * (-Height) /
				(points_y[pdnext] - points_y[pd] + 1); break;
		case 3: sdstep = 65536 * (Width) /
				(points_y[pdnext] - points_y[pd] + 1); break;
		}
	}
	else
	{
		sdstep = 65536;
	}

	// - for ascent
	if(points_y[panext] - points_y[pa] + 1)
	{
		switch(pa)
		{
		case 0: sastep = 65536 * (Width) /
				(points_y[panext] - points_y[pa] + 1); break;
		case 1: sastep = 65536 * (Height) /
				(points_y[panext] - points_y[pa] + 1); break;
		case 2: sastep = 65536 * (-Width) /
				(points_y[panext] - points_y[pa] + 1); break;
		case 3: sastep = 65536 * (-Height) /
				(points_y[panext] - points_y[pa] + 1); break;
		}
	}
	else
	{
		sastep = 65536;
	}

	// compute initial horizontal position

	// - for descent
	pdx = points_x[pd] * 65536;
	if(points_y[pd] < y) pdx += pdstepx * (y - points_y[pd]);

	// - for ascent
	pax = points_x[pa] * 65536;
	if(points_y[pa] < y) pax += pastepx * (y - points_y[pa]);

	// compute initial source position

	// - for descent
	switch(pd)
	{
	case 0: sd = 0; break;
	case 1: sd = Width * 65536 - 1; break;
	case 2: sd = Height * 65536 - 1; break;
	case 3: sd = 0; break;
	}
	if(points_y[pd] < y) sd += sdstep * (y - points_y[pd]);

	// - for ascent
	switch(pa)
	{
	case 0: sa = 0; break;
	case 1: sa = 0; break;
	case 2: sa = Width * 65536 - 1; break;
	case 3: sa = Height * 65536 - 1; break;
	}
	if(points_y[pa] < y) sa += sastep * (y - points_y[pa]);

	// process per a line
	for(; y <= ylim; y++)
	{
		// transfer a line

		// - compute descent x and ascent x
		tjs_int ddx = pdx >> 16;
		tjs_int adx = pax >> 16;

		// - compute descent source position
		tjs_int dx, dy, ax, ay;
		switch(pd)
		{
		case 0: dx = 0; dy = sd; break;
		case 1: dx = sd; dy = 0; break;
		case 2: dx = Width * 65536 - 1; dy = sd; break;
		case 3: dx = sd; dy = Height * 65536 - 1; break;
		}

		// - compute ascent source position
		switch(pa)
		{
		case 0: ax = sa; ay = 0; break;
		case 1: ax = Width * 65536 - 1; ay = sa; break;
		case 2: ax = sa; ay = Height * 65536 - 1; break;
		case 3: ax = 0; ay = sa; break;
		}

		// - swap dx/dy ax/ay dax/ddx if dax < ddx
		if(adx < ddx) Swap_tjs_int(dx, ax), Swap_tjs_int(dy, ay), Swap_tjs_int(adx, ddx);

		// - compute source step
		tjs_int sxstep, systep;
		if(adx != ddx)
		{
			sxstep = /*65536 * */ (ax - dx + 1) / (adx - ddx + 1);
			systep = /*65536 * */ (ay - dy + 1) / (adx - ddx + 1);
		}
		else
		{
			sxstep = systep = 65536;
		}

		// add line
		AddLine(y, ddx, adx + 1, type);

		// write transfer information
		tRotateDrawLine & drawline
			= (type == 1) ? DrawData[y].src1 : DrawData[y].src2;

		drawline.start = ddx;
		drawline.sx = dx;
		drawline.sy = dy;
		drawline.stepx = sxstep;
		drawline.stepy = systep;

		// check descent point
		if(points_y[pdnext] == y)
		{
			do
			{
				pd--;
				pdnext--;
				pd &= 3;
				pdnext &= 3;
			} while(pdnext != bottompoint && points_y[pdnext] == y);


			if(points_y[pdnext] - points_y[pd] + 1)
				pdstepx = 65536 * (points_x[pdnext] - points_x[pd]) /
					(points_y[pdnext] - points_y[pd] + 1);
			else
				pdstepx = 65536;

			if(points_y[pdnext] - points_y[pd] + 1)
			{
				switch(pd)
				{
				case 0: sdstep = 65536 * (Height) /
						(points_y[pdnext] - points_y[pd] + 1); break;
				case 1: sdstep = 65536 * (-Width) /
						(points_y[pdnext] - points_y[pd] + 1); break;
				case 2: sdstep = 65536 * (-Height) /
						(points_y[pdnext] - points_y[pd] + 1); break;
				case 3: sdstep = 65536 * (Width) /
						(points_y[pdnext] - points_y[pd] + 1); break;
				}
			}
			else
			{
				sdstep = 65536;
			}

			switch(pd)
			{
			case 0: sd = 0; break;
			case 1: sd = Width * 65536 - 1; break;
			case 2: sd = Height * 65536 - 1; break;
			case 3: sd = 0; break;
			}

			pdx = points_x[pd] * 65536;
		}

		// check ascent point
		if(points_y[panext] == y)
		{
			do
			{
				pa++;
				panext++;
				pa &= 3;
				panext &= 3;
			} while(panext != bottompoint && points_y[panext] == y);

			if(points_y[panext] - points_y[pa] + 1)
				pastepx = 65536 * (points_x[panext] - points_x[pa]) /
					(points_y[panext] - points_y[pa] + 1);
			else
				pastepx = 65536;

			if(points_y[panext] - points_y[pa] + 1)
			{
				switch(pa)
				{
				case 0: sastep = 65536 * (Width) /
						(points_y[panext] - points_y[pa] + 1); break;
				case 1: sastep = 65536 * (Height) /
						(points_y[panext] - points_y[pa] + 1); break;
				case 2: sastep = 65536 * (-Width) /
						(points_y[panext] - points_y[pa] + 1); break;
				case 3: sastep = 65536 * (-Height) /
						(points_y[panext] - points_y[pa] + 1); break;
				}
			}
			else
			{
				sastep = 65536;
			}

			switch(pa)
			{
			case 0: sa = 0; break;
			case 1: sa = 0; break;
			case 2: sa = Width * 65536 - 1; break;
			case 3: sa = Height * 65536 - 1; break;
			}

			pax = points_x[pa] * 65536;
		}

		// to next ...
		pdx += pdstepx;
		pax += pastepx;
		sd += sdstep;
		sa += sastep;
	}
}
//---------------------------------------------------------------------------
