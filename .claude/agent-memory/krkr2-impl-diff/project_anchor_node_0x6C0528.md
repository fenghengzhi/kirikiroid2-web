---
name: anchor-node-0x6C0528-mapping
description: Player anchor node (nodeType==10) @0x6C0528 ↔ PlayerUpdateAnchor.cpp 映射、字节核实结论、open 偏差(color base 颠倒)
metadata:
  type: project
---

## 函数映射
- libkrkr2.so: `Player_evaluateAnchorNodes_type10` @ `0x6C0528` (phase3, NOT camera)
- 本地: `cpp/plugins/motionplayer/PlayerUpdateAnchor.cpp:7` `Player::updateLayersPhase3_AnchorNode`

## 字节核实的关键常量/字段 (fresh decompile 2026-06-03)
- dampPow @0x6c0884-0x6c08b8: `dt*(v27*dt/v27)/v27/60.0/node+2432`, v27=`player+592/player+1168`, dt=`player+592`(_deltaTime)。`node+2432` 是 `feedback.timespan` 经 type-10 timeline evaluator 写入的运行态通道，不是独立的 `anchorDamping` 配置字段。冗余 (v27*dt/v27) 是 FP-真实，必须保留。
- gate @0x6c06dc/06e8: `player+592(dt)==0 || !player+612`。+612=+613 的上一帧快照(updateLayerAfterDraw 0x6CE7F4 拷 +613→+612)。+613 在 gate 前无条件 STRB=1 (0x6c06d8)。本地映射: +612=_internalRenderLayerReady, +613=_needsInternalAssignImages。**正确**。
- w/h: `sub_A0F5E0(player+696)` 内部 render Layer → PropGet(L"width"/L"height", flag=**1024**) → node+232/+240; origin=*0.5 → node+248/+256。无 <=0?32 钳制。
- color base: `qword_14D7C50[(slot+364 blend & 0xF0)==0x10]`。get_bytes(0x14D7C50,16): [0]=**255.0**, [1]=**128.0**。即条件 TRUE→128.0, FALSE→255.0。Alpha 恒 255.0。
- qword_14D78F8 = 360.0 (angle damp)。

## 提交裁决
- 7caf558 dampPow+dt: ✅ 成立(逐指令一致)。
- color base 255:128: ✅ **已纠正，不再是 open**。CLAUDE.md 复审(2026-06-03 二次 fresh-decompile)确认 PlayerUpdateAnchor.cpp:144-146 现写 `isDefaultBlend=(blend&0xF0)==0x10; base = isDefaultBlend ? 128.0 : 255.0`，与二进制 `qword_14D7C50[(blend&0xF0)==16]`={255.0,128.0} (TRUE→idx1→128.0) **方向一致**。**先前 memory/MEMORY.md 索引称"方向颠倒 OPEN 高"已被证伪——文件在 5018087 之后(Jun 3 04:20)已修正。**
- eb347f5 w/h from internal render Layer + 612 gate: ✅ 成立。曾被误判"缺失"的 _internalRenderLayer/updateLayerAfterDraw 数据流现已忠实复刻。

## 仍 open 的次要项
- PropGet flag: 二进制 1024 vs 本地 0 (L44/L48)。中。
- w/h 二段式: 二进制 v83->PropGet(L"width")→Motion_propGetInt 二次解析(node+232 写解析结果)；本地直接 (tjs_int)wv。中。
- blend 来源容器替换: 二进制 per-slot `node+536*activeSlot+364` (activeSlot=node+1392 = slots[activeSlotIndex].blendMode) vs 本地单 `interpolatedCache.blendMode`。架构偏差(数据来源)，中。
- allEqual 判据: 二进制只比较 4 个 quad 的**首字节** (+100/+104/+108/+112 == +100)；本地比较整 32 位 packedColors[0..3] 全相等。低(配合 ==0xFF808080 实际差异窄)。
- color 通道顺序: 二进制按 +102→scale[0], +101→scale[1], +100→scale[2], +103(alpha)→scale[3] 处理并回写反序；本地按 colorBytes[ci*4+0/1/2]→scale[0/1/2] 直序。中(BGRA vs RGBA 通道映射)。
