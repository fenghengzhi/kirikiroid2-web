---
name: motionplayer node 必须有 delta/override 独立存储
description: 当一个二进制结构在偏移 +N..+N+80 有"延迟消费、TJS setter 写入、phase2 清零"的 block，本地 C++ 必须用独立 struct 承载而不是混入 localState 或 accumulated
type: feedback
---

# 规则：TJS-authored delta block 需要独立 C++ struct

**原因**: 2026-04-17 审计发现 `Player::updateLayersPhase2_MainLoop` 的第一项"无法 workaround 修复"偏差是 `node.delta` 架构缺失 — libkrkr2.so 在 node+1584..+1660 有一个独立的 override block，TJS setter（setX/setY/setLayerPosition/setLayerScale/setVariable）写入这里、phase2 主循环消费、消费后清 +1584 dirty 字节。本地之前把它跟 `localState`（每帧从 interpolation 刷新的 +1504..+1580 block）混用，导致 setter 写入在下一帧被 interpolation 覆盖。

**应用方式**:
- 审计 motionplayer / EmotePlayer 任何 setter 对 node state 的写入时，确认写入的是 delta block 还是 localState。若混用，必定是 bug。
- 新增 setter 时，若对应 libkrkr2.so 函数写到 node+1584..+1660 区段（可通过反编译检测 `*(v2+158X) = a2`），本地 **必须** 写到 `delta.*` 字段，并置 `delta.dirty = true`。
- phase2 主循环消费 delta 时，三件事：(1) 将 delta 合并到 accum（XOR/+/*/覆盖语义因字段而异），(2) 清 `delta.dirty`，(3) **不要** 清 delta 的 value 字段（下一帧如无新 setter 调用，保持上次的 override 值）。
- 类似架构可能出现在其他 C++ 移植代码中（camera pos、anchor、variable animator），需同样警惕。

**相关字段 byte offset（对 MotionNode delta block = node+1584..+1660）**:
- +1584 dirty           — byte
- +1585 activeOverride  — byte
- +1586 visibleOverride — byte
- +1587 flipX           — byte
- +1588 flipY           — byte
- +1592 posX            — double
- +1600 posY            — double
- +1608 posZ            — double
- +1616 angle           — double
- +1624 scaleX          — double
- +1632 scaleY          — double
- +1640 slantX          — double
- +1648 slantY          — double
- +1656 opacity         — int32
