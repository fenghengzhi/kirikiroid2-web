/**
 * fPackinOne - バッチプラグインローダー
 *
 * このプラグインは独自の機能を持たず、ゲームスクリプトが
 * Plugins.link("PackinOne.dll") を呼び出したときに、
 * 以下の8つのサブプラグインを一括でロードする役割を果たす。
 *
 * ・fstat.dll
 * ・savestruct.dll
 * ・scriptsEx.dll
 * ・shrinkCopy.dll
 * ・layerExBTOA.dll
 * ・layerExImage.dll
 * ・layerExRaster.dll
 * ・csvParser.dll
 *
 * 元のAndroid版 libkrkr2.so のバイナリ解析により復元。
 */
#define NCB_MODULE_NAME TJS_W("fPackinOne.dll")
#include "ncbind.hpp"

static void loadSubPlugins()
{
	ncbAutoRegister::LoadModule(TJS_W("fstat.dll"));
	ncbAutoRegister::LoadModule(TJS_W("savestruct.dll"));
	ncbAutoRegister::LoadModule(TJS_W("scriptsex.dll"));
	ncbAutoRegister::LoadModule(TJS_W("shrinkcopy.dll"));
	ncbAutoRegister::LoadModule(TJS_W("layerexbtoa.dll"));
	ncbAutoRegister::LoadModule(TJS_W("layereximage.dll"));
	ncbAutoRegister::LoadModule(TJS_W("layerexraster.dll"));
	ncbAutoRegister::LoadModule(TJS_W("csvparser.dll"));
}

NCB_PRE_REGIST_CALLBACK(loadSubPlugins);
