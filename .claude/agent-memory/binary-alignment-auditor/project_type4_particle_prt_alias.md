---
name: type4-particle-prt-alias
description: type-4 粒子 eval COPY 分支读的 slot+744 与 mergeFrameContent 写的 prt 块 slot+424 是同一块内存(无字段拆分),正常播放发射器读非零
metadata:
  type: project
---

type-4 粒子 eval COPY 分支 @0x699c2c 读的偏移 `node+536*v5+744` 与 mergeFrameContent
写的 prt 块 slot+424 在 binary 里是**同一块字节**,不是两块独立区。

**Why:** mergeFrameContent 的 slot 基址 = node+320+536*idx(证据 advanceNodeFrames
@0x6b7ef8 `ADD X9,X20,#0x140`; @0x6b7fc8/6b7fe0 写 node+320 / node+856)。merge 写
fmin 在 slot+424(0x693d9c `*((QWORD*)slot+53)`)。eval COPY @0x699c30 读
`[X8,#0x2E8]` 其中 X8=node+536*v5(无 +320)。reconcile: node+320+536*idx+424 ==
node+536*idx+744。slot0: 均=node+744; slot1: 均=node+1280。完全同址。restore
@0x699890 `memcpy(node+536*v3+744,...,0x48)` 也写同址。

**slot+744 写者全集 = {mergeFrameContent(正常播放每帧主写), HM3 restore(仅HM3路径)}**,
不是"唯一 restore"。正常播放 COPY 分支读到的就是 merge 写的非零 prt fmin..range →
发射器(node+2224..2288 镜像 @0x6bf6b4)读非零 → 粒子正常发射,**不惰性**。

**How to apply:** 本地 MotionNode.h 曾引入幽灵字段 `ClipSlot.prtResult[9]`(slot+744)
与 `prtFmin/prtF/...`(slot+424)拆成两块独立内存,导致 COPY 分支(PlayerUpdateLayerEval.cpp
writeParticleInterpCopyLike_0x699c2c)读 prtResult(正常播放恒零)→ 粒子惰性回归。正确做法:
COPY 与 INTERP(@0x69a0f8)同源读 prt 块(prtFmin/prtF/prtVmin/prtV/prtAmin/prtA/prtZmin/
prtZ/prtRange); 删 prtResult; restore 也写回 prt 块。审计 type-4 粒子链时先确认这个 alias,
任何把 slot+744 当独立"prtResult 区"的注释/memory 都是被证伪的,需纠正。

发射器 trigger 读 slot+416(=node+536*v9+736, @0x6bf674)直接来自 merge 写入,不经镜像。
关键地址: eval 0x699AE4(COPY 0x699c2c/INTERP 0x69a0f8), emitter 0x6BF0DC,
merge 0x693c64(prt @0x693d50..0x693ecc), restore 0x6997F0, init 0x6995dc,
advanceNodeFrames 0x6B7E44.
