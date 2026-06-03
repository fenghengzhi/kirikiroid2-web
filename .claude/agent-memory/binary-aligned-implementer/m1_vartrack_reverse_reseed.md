---
name: m1-vartrack-reverse-reseed
description: M1 progress var-track stream③ now has all 3 forms (forward/reverse/reseed) ported + wired into frameProgress
metadata:
  type: project
---

M1 progress var-track stream③ (Player+1296 deque, 160B VariableLabelScope, two 56B VarTrackSlot at item+48/+104) now has ALL THREE stepping forms ported in PlayerFrameProgress.cpp (2026-06-04):

- **forward** `advanceVariableTracksLike_0x6B6ADC` (binary 0x6B7124, inside advanceRootAndNodes): inner loop while active.frameIndex<count-2 && other.time<=target → step ACTIVE to other.frameIndex+1, toggle, swap. Post-merge: slot[0] then slot[0] AGAIN (both gated). [pre-existing, untouched]
- **reverse** `rewindVariableTracksLike_0x6B9A3C` (binary 0x6B9FCC, inside rewindRootAndNodes): inner loop while active.time>target → step OTHER to (active.frameIndex-1) UNSIGNED, toggle, swap. Post-merge: slot[0] then **slot[1]** (both gated). KEY DIFF vs forward = merge slot[1] not slot[0]×2, and step OTHER not ACTIVE, and idx-1 not +1, and condition active.time>target not other.time<=target.
- **reseed** `reseedVariableTracksLike_0x6B86C8` (binary 0x6B8F30, inside reseekTimelineCursors): NON-incremental. fwd scan to k (==target stop / <target ++k / >target --k stop), v41=min(k,count-2). step+merge slot[0]=v41, slot[1]=v41+1 (UNCONDITIONAL merges, NO !merged gate), activeSlotCursor=0.

step=sub_6B786C (slot.frameIndex=idx; slot.time=frames[idx]["time"]; merged=0). merge=sub_6B7A70 (merged=1; type0→typeZeroFlag=1 early-ret; else interpFlag type2→0/type3→1; interval/value/easing from content). frames = item.frameSource (item+24, == item+0 cascadeKey source).

WIRING (authoritative branch map from progress_inner 0x6C106C):
- 0x6C1468/0x6C11B0 advanceRoot → forward
- 0x6C13A4/0x6C13F8 advanceRoot (reseekNodes block, deltaTime>=0) → forward
- 0x6C138C rewind LABEL_57 (reseekNodes block, deltaTime<0) → reverse [shared block: SPLIT on deltaTime sign]
- 0x6C1408/0x6C11C0 rewind → reverse
- reseek (reseekTimelineCursors): firstFrame (0x6C10E0/0x6C131C), fwd loop-wrap (0x6C1488), rev loop-wrap (0x6C1428) → reseed
- var-track reseed is UNGATED on _nodes.empty() (added before the node-walk gate), matching binary.

ORACLE-INERT for logo (no fixture has populated "variable" list → PropGetCount~0 → loops never run). logo m2+yuzu PASS bit-identical (243+93 frames). Verification gap noted per CLAUDE.md (no oracle).

NOT done (out of scope, no local model): reverseSeekFlag +609 branches (0x6C13B8/0x6C1160) also call reseekTimelineCursors but frameProgress doesn't model +609; root/layer streams in rewind use forward-only ports (seekRootContentStreamLike still 0x6B6EE4 forward) — separate seek-direction concern flagged in code comments.
