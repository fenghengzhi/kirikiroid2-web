---
name: root-stream-two-tier-seed-vs-incremental
description: Root/layer timeline cursors (+568/+576/+584/+616) follow a two-tier model — reseek(0x6B86C8) recomputes from cursor; advance/rewind(0x6B6ADC/0x6B9A3C) only evolve incrementally, NEVER recompute at entry
type: project
---

Motion.Player root timeline stream (priority array, fields +568 cursor / +576 _rootCurTime / +584 _rootNextTime / +616 _rootContent) is a TWO-TIER state machine. Confirmed by fresh decompile 2026-06-05 of 0x6B3778 / 0x6B6ADC / 0x6B9A3C / 0x6B86C8.

TIER 1 — reseek (Player_reseekTimelineCursors @0x6B86C8): the ONLY full recompute. Scans priority for cursor j where priority[j].time<=+456, clamps +568=min(j,count-2) (@0x6B8D54), then SEEDS +616=priority[cursor].content (@0x6B8E20), +576=priority[cursor].time (@0x6B8E48), +584=priority[cursor+1].time (@0x6B8E50/8F08). Wired at firstFrame seed + the two loop-wrap points (forward 0x6C1488, reverse 0x6C1428).

TIER 2 — advance(0x6B6ADC root loop @0x6B6F48) / rewind(0x6B9A3C root loop @0x6B9E84): PURE incremental, NO entry recompute. Forward: gate `+456 < +584`, body `++568; +616=priority[cursor].content; +576=+584(carry); +584=priority[cursor+1].time`. Reverse: gate `+576 > +456`, body `--568; +616=priority[cursor].content; +584=+576(carry); +576=priority[cursor].time`.

INIT (Player_initNonEmoteMotion @0x6B3778): seeds ONLY +548=motion["priority"] (@0x6B37D0) and +616=priority[0].content (@0x6B38FC). Does NOT touch +568/+576/+584 — they begin at Player object construction zero-init and stay 0 until tier-1 reseek or tier-2 first advance. Same for +916 layer cursor.

PORT NOTE (corrected 2026-07-19): PlayerFrameProgress.cpp now mirrors the binary
direction split with `advanceRootContentStreamLike_0x6B6ADC` and
`rewindRootContentStreamLike_0x6B9A3C`; layer 同样拆为
`advanceLayerEventStreamLike_0x6B6ADC` / `rewindLayerEventStreamLike_0x6B9A3C`。
旧的两个合并双向 helper 与 `_layerStreamSource/_rootStreamSource` 身份重置字段已删除。
reseek 仍由 `Player::reseekTimelineCursors@0x6B86C8` 独立完成全量 seed。

BUG FIXED 2026-06-05 (this file): seekRootContentStreamLike was unconditionally recomputing _rootCurTime/_rootNextTime from cursor on EVERY entry (old lines ~1090-1091) — a tier-1 operation wrongly living in the tier-2 function. Discarded persistent state each tick, contradicting the adjacent faithful +616 persist. Removed; fields now persist via Player.h members (default 0.0), seeded only at reseed. logo diff (m2logo 93 / yuzulogo 243) stayed PASS — inert because logo priority arrays have <2 frames (loop body never runs), so this was a data-flow/architecture fix not observable by current oracles.
