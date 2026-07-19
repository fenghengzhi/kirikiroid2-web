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

## 2026-07-19 纠正：旧 root-architecture stall 已推进

本文件原先断言本地仍以 STL `_timelines` 替代整个 node-deque cursor machine；该断言已被
后续 raw frameList 两槽 parse/merge、advance/rewind/reseek 与主标量游标链实现证伪。
`Player_ncb_registerMembers@0x6D69C8` 也确认 Motion.Player 没有 timeline API；错误的
`_timelines/_playingTimelineLabels`、control animator 与 decoded timeline 表已经删除。
当前仍需审计的是原版 per-frame renderList owner 与本地 `_nodes` 门控的容器归属差异，
不得再把已完成的 cursor machine 标为 MISSING。

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
