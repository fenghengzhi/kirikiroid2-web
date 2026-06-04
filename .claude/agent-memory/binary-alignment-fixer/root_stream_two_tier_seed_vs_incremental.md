---
name: root-stream-two-tier-seed-vs-incremental
description: Root/layer timeline cursors (+568/+576/+584/+616) follow a two-tier model — reseek(0x6B86C8) recomputes from cursor; advance/rewind(0x6B6ADC/0x6B9A3C) only evolve incrementally, NEVER recompute at entry
type: project
---

Motion.Player root timeline stream (priority array, fields +568 cursor / +576 _rootCurTime / +584 _rootNextTime / +616 _rootContent) is a TWO-TIER state machine. Confirmed by fresh decompile 2026-06-05 of 0x6B3778 / 0x6B6ADC / 0x6B9A3C / 0x6B86C8.

TIER 1 — reseek (Player_reseekTimelineCursors @0x6B86C8): the ONLY full recompute. Scans priority for cursor j where priority[j].time<=+456, clamps +568=min(j,count-2) (@0x6B8D54), then SEEDS +616=priority[cursor].content (@0x6B8E20), +576=priority[cursor].time (@0x6B8E48), +584=priority[cursor+1].time (@0x6B8E50/8F08). Wired at firstFrame seed + the two loop-wrap points (forward 0x6C1488, reverse 0x6C1428).

TIER 2 — advance(0x6B6ADC root loop @0x6B6F48) / rewind(0x6B9A3C root loop @0x6B9E84): PURE incremental, NO entry recompute. Forward: gate `+456 < +584`, body `++568; +616=priority[cursor].content; +576=+584(carry); +584=priority[cursor+1].time`. Reverse: gate `+576 > +456`, body `--568; +616=priority[cursor].content; +584=+576(carry); +576=priority[cursor].time`.

INIT (Player_initNonEmoteMotion @0x6B3778): seeds ONLY +548=motion["priority"] (@0x6B37D0) and +616=priority[0].content (@0x6B38FC). Does NOT touch +568/+576/+584 — they begin at Player object construction zero-init and stay 0 until tier-1 reseek or tier-2 first advance. Same for +916 layer cursor.

PORT NOTE: PlayerFrameProgress.cpp models advance+rewind as ONE merged bidirectional function seekRootContentStreamLike_0x6B6ADC; the binary's separate-function split is preserved at the caller boundary (advanceRootAndNodes_0x6B6ADC vs rewindRootAndNodes_0x6B9A3C, both call the merged seek). reseek = Player::reseekTimelineCursors @PlayerFrameProgress.cpp:1471 (seeds _root* from cursor, lines 1683/1686/1689). Layer stream (seekLayerEventStreamLike_0x6B6ADC) is the analogous twin (+916/+920/+928).

BUG FIXED 2026-06-05 (this file): seekRootContentStreamLike was unconditionally recomputing _rootCurTime/_rootNextTime from cursor on EVERY entry (old lines ~1090-1091) — a tier-1 operation wrongly living in the tier-2 function. Discarded persistent state each tick, contradicting the adjacent faithful +616 persist. Removed; fields now persist via Player.h members (default 0.0), seeded only at reseed. logo diff (m2logo 93 / yuzulogo 243) stayed PASS — inert because logo priority arrays have <2 frames (loop body never runs), so this was a data-flow/architecture fix not observable by current oracles.
