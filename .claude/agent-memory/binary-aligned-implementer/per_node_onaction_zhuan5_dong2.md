---
name: per-node-onaction-zhuan5-dong2
description: 砖5/洞2 per-node onAction alignment — node+8 gating real, param1=label(NOT layer dispatch); node loop structure of advance/rewind
metadata:
  type: project
---

砖5/洞2 per-node onAction (PlayerUpdateLayerEval.cpp advanceNodeFrameSelectionLike_0x6926B4 + call site progressSeekNodeSlotsLike_0x6C106C).

**Why:** prior audit flagged two alleged bugs; only Bug-2 was real.

**Binary node-loop structure (advance 0x6B6ADC LABEL_86, rewind 0x6B9A3C LABEL_76):**
- `for(k/j=1; ...)` over node-deque (player+200, 2632B stride) — idx>=1.
- BOTH loops: `if (*(node+8)) { Player_advanceNodeFrames(node, player); continue; }`.
- node+8 = parameterEntry (set in Player_initNodeFields 0x6B3C78 @0x6B3EA0 = paramTable+56*idx or 0). MotionNode.h:71.
- Player_advanceNodeFrames (0x6B7E44): seeks the node's two slots but has NO `&4` mask check and NO pushActionEvent — parameterized nodes fire NO per-node onAction.
- Only node+8==0 (non-parameterized) runs the inline 2-slot seek; on `(*(slot+22)&4)!=0` (mask bit 0x40000) it does the per-node push:
  - advance push @0x6B74E4: `Player_pushActionEvent_guess(player, &v87, slot+0x120)` where v87=*(node+0) AddRef'd; param2=slot+288.
  - rewind push @0x6BA26C: same shape, sub_6B638C(player, v61, slot+288).
- **FIX (Bug-2, real):** gate call site so `node.parameterEntry != nullptr` passes nullptr for pendingEvents.

**param1 = *(node+0) is the "label" string variant, NOT a layer dispatch object (Bug-1 premise WRONG):**
- Player_initNodeFields 0x6B3DC8 sub_529524(PropGet "label") -> @0x6B3DF4 `*(node+0) = labelVariant.object`. So node+0 IS the label.
- node.layerName = same PSB "label" (NodeTree.cpp:108). So `widen(node.layerName)` as param1 is the faithful port. Do NOT switch to tjsLayerObject (*(node+0)+16).
- Disasm proof the alleged `v89=2`(type=Object) is NOT param1's tag: var_70(v87 data)@0x170, var_68(v87 type)@0x178, var_60(v89)@0x180 — v89 is 16B after v87, a SEPARATE slot, not v87.type.

**Player_dispatchEvents (0x6C4490):** `if(*i){ if(*i==1) onSync } else { onAction }`. type 0->onAction, 1->onSync, 2->NOTHING. Action pushes use hardcoded record.type=0 (sub_6B638C v9=0) regardless of caller scratch.
