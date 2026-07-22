---
name: sub6C2334-item18-a6-propagation
description: render item+18 (skipFlag1) = (a6&1)||(node+48!=0); a6 is per-Player build flag propagated across child-Player recursion using node-level priorDraw, NOT Player-level _priorDraw
type: project
metadata:
  type: project
---

# render-item item+18 (skipFlag1) alignment — sub_6C2334 @0x6C2334

**Fact (oracle, 0x6c3380-0x6c33c0):** `v298=1; if((a6&1)==0) v298=(node+48 != 0); item+18=v298;`
i.e. `item+18 = (a6 & 1) || (node+48 != 0)`. Neighbors: item+17 = `((preview?1097:1089)&(1<<node+28))==0`; item+16 = node+201.

**a6 propagation:** sub_6C2334(result=Player, ..., char a6) iterates the Player's own
node deque (offsets 200/208/216/224/232/240/256). The SAME `a6` is used for every
node's item+18 in that loop — it is NOT recomputed per parent node inside the loop.
Top-level caller sub_6D5164 @0x6d5198 passes a6=0 (W5=WZR). The 3 recursive calls
(@0x6c2b5c type3, @0x6c3124 type3-leaf, @0x6c36ac type4) recurse into CHILD Player
objects, each passing `a6_child = (a6&1) || (this node+48 != 0)` — same formula as item+18.

**node+48 semantics:** written only in sub_6BC4F0 @0x6bc6c4: gated by `if(*(node+1996)
/*forceVisible*/)`, then `node+48 = sub_6636D4(emoteEditDispatch@node+1980,"priorDraw")&1`;
else `node+48=0` (@0x6bc67c). sub_6636D4 @0x6636D4 is a BOOL getter (returns value!=0 as
0/1 for any TJS variant type). So **node+48 is a pure bool**, not a raw int. The old port
comment claiming "bit flags checked via (v12 & 5)" was speculation — no &5 consumer exists.

**Port mapping (verified):**
- node+48 = `MotionNode.priorDraw` (MotionNode.h); node+1996 = `MotionNode.forceVisible`.
- a6 = `appendPreparedRenderItems(..., bool inheritedFlag18)` 的递归参数；子 Player
  调用直接传 `inheritedFlag18 || ownerNode.priorDraw != 0`，不再经过
  Player 成员侧挂 latch。
- item+18 直接存为 `PreparedRenderItem::skipFlag1 = inheritedFlag18 ||
  (node.priorDraw != 0)`。`skipFlag1` 是历史遗留的误导性名称，存储极性就是
  二进制 item+18；wasmtime harness 也直接输出 `skipFlag1 ? 1 : 0`，
  没有反相层。

**2026-05-30 历史 bug（当前实现已取代该路径）：**
`appendChildEntriesAtCurrentNode` 曾把 Player 层 `_priorDraw`（当时还被错误建模为
double 1.5）混入子 Player 的 a6，使 item+18 错误恒为 1。修复的二进制
依据仍是“a6 只吸收 owner node+48”。**2026-07-23 当前状态**是上述
无侧挂 latch 的参数递归，并把 item+18 按原极性直接写入。

`_priorDraw` (Player+1096 bool, NCB property `priorDraw`) is a SEPARATE field from node-level
priorDraw — do not conflate. It remains for its TJS getter/setter; it is not in the a6 path.
