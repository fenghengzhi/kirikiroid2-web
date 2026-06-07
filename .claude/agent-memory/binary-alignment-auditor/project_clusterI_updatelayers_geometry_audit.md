---
name: clusterI-updatelayers-geometry-audit
description: 簇I审计(2026-06-07) updateLayers/geometry/anchor — H系列P0/P1多数已修;残留I-1 _deltaTime(player+592)从未赋值架构前置缺失
metadata:
  type: project
---

簇 I 审计 2026-06-07，5 文件:PlayerUpdateLayers/LayersInternal.h/LayerEval/Geometry/Anchor。
报告:analysis/audit_motionplayer_2026-06-07/clusterI_updatelayers_geometry.md。结论 ⚠️ PARTIAL + 1处🔧。

**结论:2026-05-30 cluster H 的 P0/P1 多数已修复**(代码已演进,务必先反编译再下结论):
- H-1/H-2/H-3/H-4 anchor(0x6C0528): dampPow公式/w-h PSB dispatch/612 gate/COLOR base 128-255 全已对齐。
  byte-verified qword_14D7C50={255.0,128.0},index1(blend&0xF0==0x10)=128.0。allEqual sentinel 0xFF808080。
- H-10/H-11 calcBounds(0x6C3D04): 已有 type3/type4 递归(PlayerRenderItems.cpp:185-203)。
- H-14..H-18 root setters: 已写 root-delta(_nodes[0].delta.*+dirty)。0x6C0F1C=root+1587/1588,
  0x6C0F54=root+1624/1632,0x6C1028=root+1656,0x6C1048=root+1586。
- H-20 applyTranslateOffset(0x6D5264): 已实装(PlayerRenderItems.cpp:1040+)。

**I-1 (🔧 跨簇前置缺失,最重要):** player+592(_deltaTime=speedMul*dt)在 port **从未赋值**。
- 唯一写入源:progress_inner 0x6C1094 `*(a1+592)=speedMul*a2`(入口第一件事)。
- port grep `_deltaTime *=` → 0 hits;只在 PlayerUpdateAnchor.cpp:23/66/67 + PlayerUpdateChildMotion.cpp:186 被读。
- frameProgress(PlayerFrameProgress.cpp:2144)只设 `_frameLastTime=dt`(未缩放),漏 `_deltaTime=_speedMul*dt`。
- 后果:anchor gate `_deltaTime==0`恒真→anchor物理全 dead(虽 byte-faithful);phase1 root velocity 误用字段。
- 修复在 cluster G(frameProgress),不在簇I。anchor type-10 logo fixture 无 → oracle-inert 但真实前置缺失,非平台边界。

**I-2 (⚠ 依赖I-1):** phase1(PlayerUpdateLayerEval.cpp:938/949)用 `_frameLastTime`(=raw dt),
二进制 0x6BB38C/0x6BB400 用 player+592=_deltaTime(=speedMul*dt)。speed!=1 时不同。
另 port 多 `&& _frameLastTime>0` subgate(二进制只 `damp!=1.0`)。speed==1 logo inert。

**残留 ⚠(非阻塞):**
- calcBounds type3/type4 递归(line 187/193)漏 `!_preview`(player+1092)gate,二进制两递归臂都有。
- calcBounds 拆两循环 vs 二进制单循环交织(min/max 可交换,result-equiv,源结构⚠)。
- camera-target(H-5)仍 root/prev-frame placeholder,非 nodePathMap resolve(stereovision-only inert)。
- root setter X/Y split + legacy viewport-scalar setFlip/setSlant(_flip/_slant)双 API(port-extra)→cluster E 核 NCB arity。

phase2 主循环(0x6BB33C 全反编译核对)+ geometry 各 phase(CameraConstraint/Vertex/Visibility/CameraNode/
ShapeAABB/ShapeGeometry)+ 子函数(699940/69AE74/6BAA10/699AE4)全 ✅。phase3 call order ✅。

IDB:重命名5函数(加偏移后缀),3处注释(0x6C1094/0x6BB38C/0x6BB400 标 _deltaTime 缺失),idb_save OK。
