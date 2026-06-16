//---------------------------------------------------------------------------
// extrans.dll — rotate 系トランジション基底クラス
//
// 実現基底 = 上游 krkrz/SamplePlugin/extrans/rotatebase.h（W.Dee）。
// libkrkr2.so（kirikiroid2/ARM64）反編譯為差分裁判。一致処は上游源码原様
// （tPoint/tRotateDrawLine/tRotateDrawRegionData/tRotateDrawData 構造体、
// 動的容器 DrawData = new[Height]、AddLine/AddSource 扫描线光栅化）。
//
// 二進地址索引（analysis/extrans_port.md §11 + 本轮 rotate 反編譯）：
//   基類 ctor              sub_7C7F30 @0x7C7F30（operator new[](104*Height) 確認）
//   基類 dtor              sub_7C7F9C @0x7C7F9C（delete[] DrawData 確認）
//   基類 StartProcess      sub_7C7FE8 @0x7C7FEC
//   基類 EndProcess        sub_7C80CC @0x7C80CC
//   基類 Process           sub_7C80E0 @0x7C80E0（软件 region-walk / GPU mesh 双路径）
//   AddRef                 sub_7C9108 @0x7C9108
//   Release                sub_7C911C @0x7C911C
//   SetOption(return 0)    sub_7C9154 @0x7C9154
//   MakeFinalImage         sub_7C915C @0x7C915C（*dest=src2）
//   AddLine（非虚）         sub_7C872C @0x7C872C
//   AddSource（非虚）       sub_7C88B0 @0x7C88B0（软件光栅化 / GPU push quad 双路径）
//
// kirikiroid2 delta（相对上游 Win32）:
//   D1. Process / AddSource 双路径（TVPIsSoftwareRenderer→IsSoftware() 分発）。
//       软件路径 = 上游基類 Process / AddSource そのまま（texture-based 化）。
//       GPU 路径 = 上游 Win32 に無い kirikiroid2 専用：AddSource が quad 顶点を
//       vertex vector に push、Process が "FillARGB"+"Copy" render-method で
//       OperateRect 背景填め + OperateTriangles(nTriangles=2) メッシュ転送。
//   D2. bgcolor R/B swizzle は **無い**（rotateswap も原様、wave/turn と異なる）。
//---------------------------------------------------------------------------

#ifndef rotatebaseH
#define rotatebaseH

#include "tjsCommHead.h"
#include "transhandler.h"

#include <vector>

//---------------------------------------------------------------------------
struct tPoint
{
	tjs_int x;
	tjs_int y;
};
//---------------------------------------------------------------------------
struct tRotateDrawLine
{
	tjs_int start; // destination start
	tjs_int sx; // source start x
	tjs_int sy; // source start y
	tjs_int stepx; // source step x
	tjs_int stepy; // source step y
};
//---------------------------------------------------------------------------
struct tRotateDrawRegionData
{
	tjs_int left; // left position of destination x
	tjs_int right; // right position of destination x
	tjs_int type; // 0 = bgcolor, 1 = src1, 2 = src2
};
//---------------------------------------------------------------------------
struct tRotateDrawData
{
	int count;
	tRotateDrawLine src1;
	tRotateDrawLine src2;
	tRotateDrawRegionData region[5];
};
//---------------------------------------------------------------------------
// GPU 路径用 quad 顶点レコード（二進 28B/元素、AddSource GPU 分支が push）。
// type(4B) + 上位 3 点（upper-left/upper-right/bottom-left、各 int x,y）。
// 上游 Win32 に無い kirikiroid2 専用構造。
struct tRotateGPUQuad
{
	tjs_int type;     // 二進 *(v35-28) = a3
	tjs_int x0, y0;   // 二進 *(_OWORD*)(v35-24) = *(_OWORD*)a2（a2[0..3]）
	tjs_int x1, y1;
	tjs_int x2, y2;   // 二進 *(v35-8) = *((_QWORD*)a2+2)（a2[4..5]）
};
//---------------------------------------------------------------------------
class tTVPBaseRotateTransHandler : public iTVPDivisibleTransHandler
{
	//	回転を行うトランジションハンドラ基底クラスの実装

	tjs_int RefCount; // 参照カウンタ

protected:
	tjs_uint64 StartTick; // トランジションを開始した tick count
	tjs_uint64 Time; // トランジションに要する時間
	tjs_uint64 CurTime; // 現在の時間
	tjs_int Width; // 処理する画像の幅
	tjs_int Height; // 処理する画像の高さ
	tjs_int BGColor; // 背景色（rotate は R/B swizzle 無し＝原様、D2）
	tjs_int Phase; // アニメーションのフェーズ
	bool First; // 一番最初の呼び出しかどうか

	tRotateDrawData * DrawData; // 描画用データ（動的容器 new[Height]）

	// GPU 路径用 quad 顶点 vector（二進 +72/+80。上游 Win32 に無い kirikiroid2 専用）。
	std::vector<tRotateGPUQuad> GPUQuads;

public:
	tTVPBaseRotateTransHandler(tjs_uint64 time,
		tjs_int width, tjs_int height, tjs_uint32 bgcolor);

	virtual ~tTVPBaseRotateTransHandler();

	// Aligned with libkrkr2.so AddRef @0x7C9108
	tjs_error AddRef() override
	{
		RefCount ++;
		return TJS_S_OK;
	}

	// Aligned with libkrkr2.so Release @0x7C911C
	tjs_error Release() override
	{
		if(RefCount == 1)
			delete this;
		else
			RefCount--;
		return TJS_S_OK;
	}

	// Aligned with libkrkr2.so SetOption @0x7C9154 (no-op return 0)
	tjs_error SetOption(
			/*in*/iTVPSimpleOptionProvider *options) override
	{
		return TJS_S_OK;
	}

	tjs_error StartProcess(tjs_uint64 tick) override;

	tjs_error EndProcess() override;

	tjs_error Process(
			tTVPDivisibleData *data) override;

	// Aligned with libkrkr2.so MakeFinalImage @0x7C915C
	tjs_error MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2) override
	{
		*dest = src2; // 常に最終画像は src2
		return TJS_S_OK;
	}

protected:

	void AddLine(tjs_int line, tjs_int left, tjs_int right, tjs_int type);
	void AddSource(const tPoint *points, tjs_int type);

	virtual void CalcPosition() = 0;
		// 矩形の位置を計算する
		// 下位クラスで実装すること
};
//---------------------------------------------------------------------------
#endif
