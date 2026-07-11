---
name: extrans-wave-done
description: extrans.dll wave 转场垂直切片已落地（脚手架+注册+wave 双路径），含 analysis 误判与 IDB 误命名纠正
metadata:
  type: project
---

extrans.dll wave 转场第一个垂直切片 DONE（2026-06-15，dev/texrender）。

**落地文件**：`cpp/plugins/extrans/{common.h,wave.h,wave.cpp,Main.cpp}`。CMake 接入 `cpp/plugins/CMakeLists.txt` 的 `krkr2plugins_ncbind` 库（whole-archive，避免 NCB dead-strip）+ set_source_files_properties COMPILE_DEFINITIONS NCB_MODULE_NAME=extrans.dll + include dir extrans/。NOT in 普通 krkr2plugin 库。

**方法论**：上游 krkrz/SamplePlugin/extrans 源码（/tmp/extrans_src/）为实现基底，libkrkr2.so 反编译为差分裁判。analysis/extrans_port.md 已确证同源。

**wave 二进制地址**：Register sub_7CD05C / StartTransition sub_7CD1A8 / GetName sub_7CD190(L"wave") / StartProcess sub_7CBEDC / EndProcess sub_7CC080 / Process sub_7CC094(双路径) / MakeFinalImage sub_7CD118 / GPU shader sub_7CC428。Handler 0x78=120B。EndProcess BlendRatio==255→TJS_S_FALSE(2)否则 TJS_S_TRUE(1)。AddRef/Release RefCount==1→delete this 否则 --ref。time<2→2（二进制 v17=(time<=2)?2:time，整数下等价）。默认 maxh=50/maxomega=0.2/bg=0/wavetype=0。LayerType ltAlpha=2→_d / ltAddAlpha=12→_a / else 普通（drawable.h 确认）。

**3 大 kirikiroid2 delta（已全复刻）**：
- D1: bgcolor1/2 存入 handler 时 R/B 交换（c&0xFF00FF00 | BYTE2(c) | ((u8)c<<16)，0x7cd5f8），因 GL/Cocos 纹理 ABGR。
- D2/D3: Process texture-based 双路径分发。软件路径经 Dest->GetTextureForRender()->GetScanLineForWrite + Src1/Src2->GetTexture()->GetScanLineForRead（上游直接 GetScanLine），逐行 TVPFillARGB+TVPConstAlphaBlend_SD[_a/_d]（tvpgl.h 全可用）。GPU 路径用 WaveTrans/_a/_d 三套 GLSL（GetOrCompileRenderMethod nTex=2）+ OperateRect，CrossFade::Blend(TransIntf.cpp:614) 同形样板。

**纠正 1（analysis 误判）**：analysis/extrans_port.md §4.2/§70 旧称"GPU 路径单 OperateRect 无 per-line 位移→退化平直 blend"是 WRONG。sub_7CC428 证据：GPU 路径用 fragment shader `dx=sin(v_texCoord0.y*omega+phase)*h` 在 shader 内做 wave 位移，效果与软件路径一致，NOT 退化平直。已照实复刻（注释里写明）。

**纠正 2（IDB 误命名，已 rename+set_comments+idb_save）**：sub_84B7FC 旧名 hasGPUAccel_guess 方向相反——它=TVPGetRenderManager()->[vtbl+64]()&1 缓存于 byte_1ADEB88，真→软件渲染器→CPU scanline 路径，假→GPU。已 rename 为 TVPIsSoftwareRenderer_guess。本地对应 TVPGetRenderManager()->IsSoftware()（标准分支，LayerBitmapIntf/motionplayer 惯用）。

**陷阱**：项目环境无 TJS_INTF_METHOD 宏（cpp/ 全仓既无定义也无使用）——上游 wave.cpp 用了它会编译失败。必须移除，与项目 CrossFade 风格一致（裸 `tjs_error ... override`）。tjs 基础类型用 `#include "tjsCommHead.h"` 替代 Win32 tp_stub.h。

**验证**：binary-alignment-auditor 结论"完全对齐"，8 组方法+双路径逐行对齐，软件路径逐行 clip/偏移代数验证等价，无逻辑偏差。web/debug 构建通过（extrans/{Main,wave}.cpp.o + index.html 链接 clean）。wasmtime guest 不消费 extrans（手工源列表只含 motionplayer+psbfile+xp3filter），无需改 platforms/wasmtime/CMakeLists.txt；仍跑了 krkr2_wasmtime_guest 构建确认零回归。无转场 differential fixture=运行期验证留后续轮次（honest gap）。

**后续轮次（拓扑序，V2Link/sub_7C2ACC 顺序 Wave→Mosaic→Turn→Rotate→Ripple）**：mosaic(sub_7C4438)/turn(sub_7CBB5C,417KB 查表)/rotate(sub_7C9344,3 名字)/ripple(sub_7C7490,52KB+NEON 空桩)。Main.cpp 的 InitPlugin_Extrans 已留注释占位按序补 Register*。
