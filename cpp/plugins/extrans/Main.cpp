//---------------------------------------------------------------------------
// extrans.dll — モジュール登録（V2Link 体）
//
// 実現基底 = 上游 krkrz/SamplePlugin/extrans/Main.cpp::V2Link。
// libkrkr2.so 内建化後、V2Link は sub_7C2ACC（init 链）として 5 つの
// Register* を順に呼ぶ（Wave→Mosaic→Turn→Rotate→Ripple、analysis §2）。
// 内建化で TVPInitImportStub(exporter) は省略される（builtin は tp_stub 不要）。
//
// 7 つの転場名（wave/mosaic/turn/rotatezoom/rotatevanish/rotateswap/ripple）を
// 全て登録する。RegisterRotate* が 3 名分まとめて登録（analysis §11）。
//---------------------------------------------------------------------------
// NCB_MODULE_NAME は CMake の COMPILE_DEFINITIONS で与えられる。
// 単体ビルド時のフォールバックとして #ifndef で保険を掛ける（再定義警告回避）。
#ifndef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("extrans.dll")
#endif

#include "ncbind.hpp"

#include "wave.h"
#include "mosaic.h"
#include "turn.h"
#include "rotatetrans.h"
#include "ripple.h"

//---------------------------------------------------------------------------
// Aligned with libkrkr2.so sub_7C2ACC（V2Link 体の init 链）
// 登録順序は二進と一致: Wave→Mosaic→Turn→Rotate→Ripple（analysis §2）。
static void InitPlugin_Extrans() {
    RegisterWaveTransHandlerProvider();
    RegisterMosaicTransHandlerProvider();
    RegisterTurnTransHandlerProvider();
    RegisterRotateTransHandlerProvider();
    RegisterRippleTransHandlerProvider();
}
//---------------------------------------------------------------------------
NCB_PRE_REGIST_CALLBACK(InitPlugin_Extrans);
//---------------------------------------------------------------------------
