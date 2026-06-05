---
name: initNonEmote-totalframes-writers
description: +1128/+1136 (cachedTotalFrames/loopTime) 唯一成对 writer 是 initNonEmoteMotion@0x6B370C/0x6B372C; playImpl 全程无写; maxTF 覆盖删除正确
metadata:
  type: project
---

2026-06-06 反编译确认 (initNonEmoteMotion@0x6B365C + playImpl@0x6B2284):

**+1128(_cachedTotalFrames) / +1136(_loopTime) 全二进制唯一成对 writer**:
- 0x6B370C `*(double*)(a1+1136) = motion["loopTime"]`
- 0x6B372C `*(double*)(a1+1128) = motion["lastTime"]`
两者来自 SAME motion dict, 成对, 注释自承"ONLY writer ... verified by full .text STR scan"。

**playImpl@0x6B2284 全程无写 +1128/+1136**: 唯一非 emote 出口 0x6B26F0 调 initNonEmoteMotion(v4,a3); emote 出口 0x6B26C4 调 initEmoteMotion; 失败出口 0x6B27F4 设 +1099=0(loopArmed)。**无任何 max(timeline totalFrames) 覆盖 +1128 逻辑**。

**initNonEmoteMotion 关键尾段 flag 设置**:
- 0x6B3A74 `*(WORD*)(a1+1098)=256` → +1098(sync)=0, +1099(loopArmed)=1
- 0x6B3AAC `*(WORD*)(a1+480)=257`(仅 (a2&2)==0) → +480(queuing)=1, +481(firstFrame)=1
- 0x6B3AC0 `*(BYTE*)(a1+481)=1` 无条件 firstFrame=1
- 0x6B3AA4/AA8 (仅 (a2&2)==0): +1120=0, +456=min(+1128,0.0)
- loopArmed=1 是 ENABLE progress_inner loop-wrap path 的 flag; play 前为0 → all-zero child 在 0x6C10E4 入口被 gate 掉(非负 loopTime 默认)。

**本地映射(ALIGNED)**: PlayerCore.cpp:750-751 `_loopTime=clip->loopTime; _cachedTotalFrames=clip->totalFrames` 是唯一权威成对设值点(initNonEmoteMotionLike@0x6B365C 本体)。onFindMotion(PlayerMotionLoad.cpp:105-118)删除的 port-invented `_cachedTotalFrames=maxTF(max state.totalFrames)` 单独覆盖**正确**——该覆盖会破坏 loopTime<lastTime 不变量(maxTF 可能<残留loopTime)致 forward loop-wrap 空转。删除后保持同源配对值。
