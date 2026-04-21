---
name: Player.play eager vs lazy node tree build
description: libkrkr2.so Motion.Player.play eagerly builds the node tree inside play(); web port playCompat is lazy and defers to ensureNodeTreeBuilt()
type: project
---

libkrkr2.so Motion.Player NCB method "play" call chain (all eager, one play() fully builds the node tree):
- NCB name "play" lives at 0x14c0cca as UTF-16LE `70 00 6c 00 61 00 79 00 00 00` (IDA displays truncated as `"p"`).
- Player_playCompat (0x6D2C08) — NCB wrapper, unpacks args
- Player_play (0x6B21E8) — 0x9c bytes, fast-path for flag 0x10; otherwise calls Player_playImpl
- Player_playImpl (0x6B2284) — unconditionally calls Player_loadMotion, then dispatches:
  - Player_initEmoteMotion (0x6B2E90) for EmotePlayer-style motions
  - Player_initNonEmoteMotion (0x6B365C) for standard motions
- Player_initNonEmoteMotion @ 0x6B3A80: unconditional `Player_buildNodeTree(a1)`, then `Player_initVariables(a1)` @ 0x6B3A88
- Player_buildNodeTree (0x6B51F0) recursively allocates 2632-byte nodes, reads "children", links mask layers — fully materializes the tree during play().

Web port (cpp/plugins/motionplayer) is LAZY:
- Player::playCompat in PlayerQuery.cpp:1391-1500 only calls ensureMotionLoaded + primeTimelineStates, sets timeline map entries. Never calls buildNodeTree.
- Node tree is deferred to Player::ensureNodeTreeBuilt in PlayerRender.cpp:841, gated by _runtime->nodesBuilt flag. detail::buildNodeTree invoked at PlayerRender.cpp:873.
- ensureNodeTreeBuilt callers: calcBounds (PlayerRenderPrepare.cpp:41), progressCompatMethod (PlayerQuery.cpp:1539), draw/query/updateLayers paths (PlayerRender.cpp:2103/2198/2317/2500, PlayerUpdateLayers.cpp:2350/3137, PlayerQuery.cpp:169/204/224/246/649/662/1065).

**Why:** Architecture divergence violates "no functional equivalence" rule — behavior differs when any query runs between play() and the first progress/draw/calcBounds (e.g. _runtime->nodes is empty, nodeLabelMap unpopulated, child player inheritance at PlayerRender.cpp:881-894 not yet applied).
**How to apply:** When reproducing or fixing any bug where node tree state is queried right after play(), do NOT assume the web port matches the Android binary. The fix direction is to call ensureNodeTreeBuilt() at the end of playCompat (or restructure so play() eagerly builds) — matching 0x6B3A80.
