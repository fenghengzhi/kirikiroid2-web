/**
 * PackinOne - バッチプラグインローダー
 *
 * このプラグインは独自の機能を持たず、ゲームスクリプトが
 * Plugins.link("PackinOne.dll") を呼び出したときに、
 * 以下の8つのサブプラグインを一括でロードする役割を果たす（libkrkr2.so 0x59B9C8）。
 *
 * ・fstat.dll          (0x14C90C6)
 * ・savestruct.dll     (0x14C90DA)
 * ・scriptsEx.dll      (0x14C90F8)
 * ・shrinkCopy.dll     (0x14C9114)
 * ・layerExBTOA.dll    (0x14C9132)
 * ・layerExImage.dll   (0x14C9152)
 * ・layerExRaster.dll  (0x14C9174)
 * ・csvParser.dll      (0x14C9198)
 *
 * 元のAndroid版 libkrkr2.so のバイナリ解析により復元。
 */
#define NCB_MODULE_NAME TJS_W("PackinOne.dll")
#include "ncbind.hpp"

static void loadSubPlugins()
{
	ncbAutoRegister::LoadModule(TJS_W("fstat.dll"));
	ncbAutoRegister::LoadModule(TJS_W("savestruct.dll"));
	ncbAutoRegister::LoadModule(TJS_W("scriptsEx.dll"));
	ncbAutoRegister::LoadModule(TJS_W("shrinkCopy.dll"));
	ncbAutoRegister::LoadModule(TJS_W("layerExBTOA.dll"));
	ncbAutoRegister::LoadModule(TJS_W("layerExImage.dll"));
	ncbAutoRegister::LoadModule(TJS_W("layerExRaster.dll"));
	ncbAutoRegister::LoadModule(TJS_W("csvParser.dll"));
}

NCB_PRE_REGIST_CALLBACK(loadSubPlugins);
