---
name: layerupdate-geometry-anchor-verdict
description: 2026-06-03 fresh-decompile audit of Player layer-update/geometry/anchor/child-motion/particles/query vs libkrkr2.so; anchor color base FIXED, getLayerNames empty-filter edge delta, 3 anchor minor deltas
metadata:
  type: project
---

2026-06-03 READ-ONLY fresh-decompile alignment audit (six dims) of the Player
layer-update subsystem. Files: PlayerUpdateAnchor/Geometry/LayerEval/Layers/
ChildMotion/Particles + PlayerLayerQuery.cpp.

**Why:** verify anchor color-base regression genuinely fixed + find real open gaps.
**How to apply:** treat these as the current verdict; recheck before re-flagging.

CONFIRMED FIXED (byte-verified): anchor color base @0x6C0528. qword_14D7C50 bytes
@0x14D7C50 = {255.0 (00..00 e0 6f 40), 128.0 (00..00 60 40)}. Index = (blend&0xF0)==0x10:
TRUE->idx1->128.0, FALSE->idx0->255.0. Local PlayerUpdateAnchor.cpp:144-146
`isDefaultBlend ? 128.0 : 255.0` is the CORRECT direction (NOT reverted 255:128). Alpha base always 255.0. CORRECT.

REAL OPEN DELTAS (anchor 0x6C0528, all Low-Med, type-10 absent from logo fixtures = oracle-inert):
1. blend SOURCE: binary reads `*(DWORD*)(v17 + 536*v19 + 364)` where v19=node+1392=activeSlotIndex
   (per-slot RAW blendMode at ClipSlot offset+364). Local uses single `interpolatedCache.blendMode`.
   Diverges during crossfade. Med.
2. PropGet flag: binary passes 1024 (TJS_IGNOREPROP) to width/height PropGet; local passes 0. Low.
3. opacity negative-denom constant: binary uses 4294967300.0 (=2^32+4); local PlayerUpdateAnchor.cpp:117
   uses 4294967296.0 (=2^32). Same +4 constant appears in per-channel color block too (binary 0x6C0BFC/
   0x6C0C1C/0x6C0C28 all 4294967300.0); local color block PlayerUpdateAnchor.cpp uses int-cast directly
   (no negative-denom +const path at all in color loop). Low (only triggers when (int)channel<0, i.e. >2^31).

getLayerNames @0x6D10E0 (NCB "getLayerNames" @0x6D88C8, NOT sub_6D1018; IDA merged):
- in-order RB-tree walk of Player+24 map (leftmost+48, _Rb_tree_increment=sub_1485230), emits raw KEY
  (node+32 "label"), no gating/descent. Local std::map ascending walk = faithful. CORRECT.
- filter = ttstr_indexOf(key, args[0]) >= 0 = CONTAINS, case-sensitive (sub_9B1FF8 = wcsstr-like). CORRECT.
- EDGE DELTA: outer gate `while(*a2)`. Empty-but-PRESENT string filter: binary `*a2 != 0` (tvtString
  obj ptr non-null) -> enters filter -> sub_9B1FF8 empty-needle returns haystack -> indexOf=0 -> emits ALL.
  Local PlayerLayerQuery.cpp:174 `needle.empty() -> continue` -> emits NOTHING. DIVERGENCE for present
  empty-string arg. Low (pathological input). Local comment claiming "binary emits nothing" is WRONG.

child-motion (0x6BE0C0) + particles (0x6BF0DC/0x6C17A4): heavily ported, NOT stubs. Faithful to fresh
decompile spot-checks (gates, RNG order, slot stride). Emote-only branches (_directEdit player+464 +
initEmoteMotion) intentionally N/A in web port - legit platform boundary, annotated. Not re-traced
line-by-line this pass; no new gap surfaced.

phase2 MainLoop (0x6BB33C) + LayerEval: two-pass seek/read split is faithful per existing
project_phase2_mainloop_mapping memory. Not re-audited here.
