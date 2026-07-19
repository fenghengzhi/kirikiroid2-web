---
name: rewind-root-reverse-loop-0x6B9E84
description: rewindRootAndNodes reverse root decrement loop @0x6B9E84 — closed R-B1; 2026-07-19 now a separate rewindRootContentStreamLike helper
metadata:
  type: project
---

R-B1 CLOSED (2026-06-05; implementation boundary corrected 2026-07-19): reverse root
loop remains implemented, but the former bidirectional
`Player::seekRootContentStreamLike_0x6B6ADC` has been split into
`advanceRootContentStreamLike_0x6B6ADC` and
`rewindRootContentStreamLike_0x6B9A3C`, matching the two Android functions.
Previously rewindRootAndNodes_0x6B9A3C reused the forward-only seekRoot → reverse root scan was missing.

**Reverse loop @0x6B9E84 (verified via fresh decompile + disasm):**
- Gate (B.GT): `*(double*)(a1+576 curTime) > *(double*)(a1+456 target)`.
- Per iter: `--(+568 cursor)` @0x6B9EA8 — the DECREMENTED value is ALSO the byNum index (single fetch, unlike forward which fetches cursor AND cursor+1).
- `content = priority[cursor].PropGet("content")` @0x6B9F4C → `sub_A0FB64(+616, content)` @0x6B9F6C (snapshot to _rootContent).
- `+584 (nextTime) = +576 (curTime)` @0x6B9F7C.
- `+576 (curTime) = priority[cursor].propGetDouble("time")` @0x6B9F94/98 — time read from the SAME decremented item (NOT cursor+1; forward reads time from cursor+1 because that's the next clip).
- Loop-back B.GT `+576 > +456` @0x6B9FC4.

**MIRROR-vs-FORWARD trap:** forward sets `curTime = oldNextTime` + reads new nextTime from priority[cursor+1]; reverse sets `nextTime = oldCurTime` + reads new curTime from priority[cursor] (the decremented item itself). Easy to get backwards.

**Port specifics:** Added `_rootCurTime` seed on entry (`frameTimeOf(frameAt(cursor))`) — required because binary holds +576 persistently and the reverse gate reads it on first reverse call. Added `cursor > 0` underflow guard (binary's bare `curTime>target` relies on priority[0].time<=target; same guard pattern as layer-stream port @0x6B9AE8). Both are faithful recovery of persistent state / documented edge guard, not invented computation.

**FALSIFIED COMMENTS corrected in-place** (were claiming "no reverse root scan / root stays forward-only"): PlayerFrameProgress.cpp comment block above seekRootContentStreamLike, rewindRootAndNodes_0x6B9A3C dispatcher comment + line ② call comment; Player.h advanceRootAndNodes/rewindRootAndNodes/seekRootContentStreamLike decl comments. IDB: set_comments @0x6B9E84 + idb_save done.

**Verification:** web debug (240/240) + krkr2_wasmtime_guest both clean. motion_playback wasmtime differential PASS bit-identical: m2logo 93f, yuzulogo 243f. ORACLE-INERT for logo (priority single-clip count<2 → reverse gate curTime>target false → body never runs); logo 0-mismatch = non-regression guard, NOT engine exercise. No reverse-root fixture (honest gap).

**STALE-MEMORY NOTE:** advancenodeframes_0x6B7E44_convergence.md claims m2logo differential FAILs 100-vs-93 as pre-existing regression — CONTRADICTED on current HEAD (dev/motion @03af6f2): m2logo PASS 93f. That memory's regression claim is stale.
