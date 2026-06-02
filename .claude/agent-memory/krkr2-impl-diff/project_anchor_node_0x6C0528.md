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
- dampPow @0x6c0884-0x6c08b8: `dt*(v27*dt/v27)/v27/60.0/node+2432`, v27=`player+592/player+1168`, dt=`player+592`(_deltaTime)。冗余 (v27*dt/v27) 是 FP-真实，必须保留。
- gate @0x6c06dc/06e8: `player+592(dt)==0 || !player+612`。+612=+613 的上一帧快照(updateLayerAfterDraw 0x6CE7F4 拷 +613→+612)。+613 在 gate 前无条件 STRB=1 (0x6c06d8)。本地映射: +612=_internalRenderLayerReady, +613=_needsInternalAssignImages。**正确**。
- w/h: `sub_A0F5E0(player+696)` 内部 render Layer → PropGet(L"width"/L"height", flag=**1024**) → node+232/+240; origin=*0.5 → node+248/+256。无 <=0?32 钳制。
- color base: `qword_14D7C50[(slot+364 blend & 0xF0)==0x10]`。get_bytes(0x14D7C50,16): [0]=**255.0**, [1]=**128.0**。即条件 TRUE→128.0, FALSE→255.0。Alpha 恒 255.0。
- qword_14D78F8 = 360.0 (angle damp)。

## 提交裁决 (2026-06-03 review)
- 7caf558 dampPow+dt: ✅ 成立(逐指令一致)。
- 5018087 color base 255:128: ✘ **方向颠倒**。本地 PlayerUpdateAnchor.cpp:139-141 写 `(blend&0xF0)==0x10 ? 255.0 : 128.0`，二进制是反的(==0x10→128.0)。仍 OPEN(高优先级)。
- eb347f5 w/h from internal render Layer + 612 gate: ✅ 成立。曾被误判"缺失"的 _internalRenderLayer/updateLayerAfterDraw 数据流现已忠实复刻。

## 仍 open 的次要项
- PropGet flag: 二进制 1024 vs 本地 0 (L44/L48)。
- w/h 二段式: 二进制 PropGet→Motion_propGetInt 二次解析；本地直接 (tjs_int)。
- blend 来源容器替换: 二进制 per-slot node+536*activeSlot+364 (activeSlot=node+1392) vs 本地单 interpolatedCache.blendMode。
