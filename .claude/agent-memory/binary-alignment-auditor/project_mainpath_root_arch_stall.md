---
name: mainpath-root-arch-stall
description: 2026-05-31 one-line verdict for motionplayer main path — progress=root-arch-wrong (recurring unsolved stall), draw=right-arch-few-branches. The recurring unresolved finding.
metadata:
  type: project
---

User asked (2026-05-31) for a single-sentence verdict on the motionplayer MAIN PATH
(progress frame-advance + draw render-submit): "wrong-arch-needs-rewrite" vs
"right-arch-missing-branches"? And: which finding keeps recurring across rounds unsolved?

## Verdict (split — the two halves differ)
- **progress (帧推进) = ROOT ARCHITECTURE IS WRONG. Needs whole rewrite.**
- **draw (渲染提交) = right architecture, missing/mis-phased branches only.**

## THE recurring, never-resolved finding (the stall)
**progress 的根架构是 node-deque frame-stepping cursor machine，本地是 STL timeline machine。**
- Binary authority: Player_progress_inner @0x6C106C drives a scalar frame cursor
  (+1120 frameTickCount, gated by +480 progressFlags init=257) -> per-step
  advance/rewind over the node deque (+184, stride 2632; per-node frame slot +1392
  stride 536) -> Player_parseFrame@0x6926B4 -> Player_mergeFrameContent@0x692AB0
  (mask-gated PropGet field merge). +456 clampedEvalTime = min(+1120,+1128).
- Port: PlayerFrameProgress.cpp = _timelines std::map + per-track control-animator
  queues + blend animators. advance/rewind/parseFrame/mergeFrameContent have NO
  live port path (MISSING, not just diverging).
- This was filed 2026-04-17 (phase2 delta/override block missing) AND 2026-05-30
  (cluster G ❌ SEVERE) AND is still true 2026-05-31. The 4 setTickCount/getLoopTime/
  initVariables/delta-block sub-findings are all DOWNSTREAM SYMPTOMS of this one root:
  every "scalar accessor that's actually non-scalar in binary" reads/writes the
  cursor-machine fields (+480/+456/+1120/+1136/+1312 deque) the port doesn't have.

## Why patching keeps failing (stall mechanism)
The M15 branch (player-class-alignment-p0) added property/method SCAFFOLDING
(onAction/clear/angleDeg/bounds/meshDivisionRatio storage) on top of the wrong
substrate. None of it consumes the cursor machine; clear() resets _timelines (STL),
setTickCount writes _frameTickCount but not +480/+456. So the surface grows while
the root divergence is untouched -> same ❌ reappears every round.

## draw side (NOT the stall)
Render pipeline call-graph is correctly shaped (cluster I): drawCompat 3-way,
skipFlag1/rawFlag20/node+48=priorDraw ALIGNED, build_flow yuzulogo 242->0 after
d51cce9. Remaining = item-layout STL-split (standing ⚠ container), clipRect
float-vs-int, m2logo items[1] frame12+ (a per-frame priorDraw timing symptom that
itself traces back to progress producing wrong per-frame node state). Draw is
fixable on the current data flow; it is gated by progress correctness, not by its
own architecture.

## Recommendation given to user
Stop per-function patching on progress. Escalate to module-alignment-driver to do
Phase-B/C: rebuild Player_progress_inner cursor machine + node frame slots +
advance/rewind/parseFrame/mergeFrameContent FIRST, then re-derive accessors/clear/
loopTime/initVariables off it. Draw needs no rewrite, only the float clipRect + the
already-tracked container ⚠.
