---
name: Player_updateLayersPhase2_MainLoop 架构偏差清单
description: 2026-04-17 对 cpp/plugins/motionplayer/PlayerUpdateLayers.cpp 中 updateLayersPhase2_MainLoop 与 libkrkr2.so sub_6BB33C phase2 主循环的对齐审计结果
type: project
---

# 本地 `updateLayersPhase2_MainLoop` vs libkrkr2.so `sub_6BB33C` 结论

审计日期: 2026-04-17；本地文件 `cpp/plugins/motionplayer/PlayerUpdateLayers.cpp:419-982`。

## 架构级偏差（需重构，非 workaround 可修）

1. **delta/override 块缺失** — 二进制 node 布局 `+1504..+1580` = accum，`+1584..+1660` = delta/override（同偏移 +80），TJS setter 写入 delta，phase2 消费 delta 后清 `+1584 dirty` 位。本地只有 `accumulated` + `localState`，localState 每帧从插值结果刷新，无法承载 setter 写入的持久化 delta 语义。
2. **sync early-exit 完全缺失** — `0x6BB604..0x6BB790`（约 80 行）：`if (*(clipSlot+344))` → `memcpy(accum, parent.accum, 0x50)` + `memcpy(m11..m22, parent.m11..m22, 0x20)` + LABEL_19。本地第 545 行 `hasSync` 只被赋值，没被消费。
3. **跳过语义颠倒** — 二进制 `Player_evaluateTimeline` 返回 0 → 不写 accum 任何字段 → accum 保留上帧值（惰性）。本地 606-628 行 `if(!state.visible)` → 主动清 `accumulated.visible=false; active=false; opacity=0; drawFlag=false`。
4. **SIMPLE 路径缺失属性 parent merge** — 二进制 `0x6BB8A8..0x6BB8EC` 在矩阵乘后必做 `flipX^=parent.flipX; flipY^=parent.flipY; angle+=parent.angle; scale*=parent.scale; slant+=parent.slant`。本地 865-875 行**只做矩阵乘**。
5. **COMPLEX per-bit merge 语义错误** — 二进制是 `v22+1507 ^= v23+1507` 单侧原地运算；本地 879-892 行改写成 `accum.flipX = state.flipX ^ parent.flipX`（重新覆盖，丢失 delta merge），且 else 分支硬写 `state.flipX`（二进制此时什么都不做）。
6. **DEPENDENT phase D 矩阵乘用错节点源** — 二进制 `0x6bba24..0x6bba6c` 使用 `*(v3+120)` = **root** 的矩阵，不是 parent。本地 936-939 行用 `parent.accumulated.m11` 是错的。

## 越权代码（phase2 不该做的事）

- 515 行 `node.stencilType = node.stencilTypeBase | state.frameType` — 应在 phase3 sub_6BD8DC。
- 560-565 行 `node.flags |= 0x01`（基于 previousSrc/previousFrameType 变化置位）— 二进制 phase2 无此逻辑。
- 589-599 行 `findPSBResourceBySourceName` 填 clipW/clipH/originX/originY — 应在 phase3 sub_6BC4F0。

## 函数结构级偏差（应抽独立函数）

- `sub_69AE74(parent, node)` mesh deform — 二进制调用点只测 `parent+2000!=0`，内部做 4 个门控 + Bezier。本地内联成 lambda `evalBezierPatch` + 调用点合并所有条件（656 行）。
- `sub_6BAA10(player, v22, v23)` groundCorrection — 本地 782-833 行内联 TJS dispatch，未抽独立函数。

## 正确的对齐部分（可作参考）

- parent 透传 while（434-440 行，对 0x6BB598..0x6BB5BC）— `inheritFlags & 0x00400000` 门控语义正确。
- position transform coordinateMode 分支（747-767 行，对 0x6BB718..0x6BB7E4）— 2D/3D 双分支、`posZ = localZ + parent.posZ` 都正确。
- opacity 二次乘（840-853 行，对 0x6BB808）— 逻辑等价于 v47=v23 vs v47=v3 的逗号表达式选择。
- INDEPENDENT 路径（894-903 行）— `sub_699940` only、NO parent×local matrix mul，符合 LABEL_68。

## 涉及的权威地址

- `sub_6BB33C` @0x6BB33C — Player_updateLayers 整体入口
- phase2 body: `0x6BB524..0x6BBB6C`
- parent lookup: `0x6BB598..0x6BB5BC`
- sync path: `0x6BB604..0x6BB790`（`clipSlot+344` 检测）
- delta→accum merge (vmulq/vaddq): `0x6BB6A4..0x6BB700`
- mesh deform 调用: `0x6BB704..0x6BB714`
- position transform: `0x6BB718..0x6BB7E4`
- opacity 2nd mul: `0x6BB808..0x6BB830`
- SIMPLE 路径: `0x6BB848..0x6BB8EC`
- COMPLEX 位 merge: `0x6BB8F4..0x6BB9B8`
- INDEPENDENT LABEL_68: `0x6BB918`
- DEPENDENT LABEL_76: `0x6BB9BC..0x6BBB6C`
- DEPENDENT phase D matrix mul: `0x6BBA24..0x6BBA6C`（用 root v3，不是 parent v23）
