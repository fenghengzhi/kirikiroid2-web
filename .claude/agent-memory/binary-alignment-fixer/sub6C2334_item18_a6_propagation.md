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
- a6 = `Player::_renderItemInheritedFlag18` (latched by prepareRenderItems(arg) @line833).
- item+18 stored INVERTED as `skipFlag1 = !(...)`; harness re-inverts on emit
  (motion_playback_wasmtime.cpp:430 `flag18 = skipFlag1 ? 0 : 1`). Net emit == oracle
  polarity. So skipFlag1 inversion is correct, NOT a polarity bug.

**Bug fixed (2026-05-30):** appendChildEntriesAtCurrentNode passed
`prepareRenderItems(inheritedFlag18 || (_priorDraw != 0.0))` using the Player-level scalar
`_priorDraw` (default **1.5** → always true), forcing every child Player's nodes to
inheritedFlag18=true → item+18 wrongly 1. Binary uses the NODE's priorDraw. Fixed to
`prepareRenderItems(_renderItemInheritedFlag18 || nodePriorDraw)` (PlayerRenderItems.cpp).
Also stored node priorDraw as bool (0/1) to match node+48&1 (PlayerUpdateGeometry.cpp).
This was the m2logo items[1] frame12+ build_flow_mismatch (oracle=0/port=1).

`_priorDraw` (Player scalar, NCB property `priorDraw`) is a SEPARATE field from node-level
priorDraw — do not conflate. It remains for its TJS getter/setter; just not in a6 path.
