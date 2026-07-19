---
name: player-m15m16-accessor-audit
description: M15/M16 Player accessor audit — binary side-effects for setAngleDeg/setChara/setTickCount/getLoopTime/setCoord/lastTime confirmed; angleDeg unit bug, loopTime array shape, chara type still deviated
metadata:
  type: project
---

Audit 2026-05-31 of M15/M16 Player accessors vs libkrkr2.so. Confirmed via decompile.

> **2026-07-18 correction:** 本文的 setChara 条目受 NCB 表 off-by-one
> 误读影响，已证伪。权威映射是 chara 0x6D9470/0x6C0E9C (+960)、
> stealthChara 0x6D9490/0x6D94B0 (+968)、pending stealthChara +776；
> 这些是 refcounted string-value owner，源码层 `ttstr`，不是本地类型偏差。

**Confirmed binary behaviors (addr -> field offsets):**
- setAngleDeg 0x6CD0EC: deg=rad*57.2957795; if +482(directEdit) normalize[0,360),
  +464=deg, Player_initEmoteMotion(2); else root+1616=deg + root+1584=dirty (guarded != ).
- getAngleDeg 0x6CD0C0: returns RAD (*0.0174532925) reading +464 or root+1616. NOTE binary
  symbol "getAngleDeg" RETURNS RADIANS.
- setStealthChara 0x6D94B0: +968 live 时 writer(flags=16)+flush +776；
  否则把请求保存在 pending +776。primary chara setter 是 0x6C0E9C。
- setTickCount_ms 0x6D96C0: +1120=fmax(v*60/1000,0); *(WORD*)+480=257; +456=min(+1120,+1128).
- getTickCount_ms 0x6D96A0: +1120*1000/60 UNCONDITIONAL (no >0 guard).
- getLoopTime ARRAY 0x6D139C: builds TJS Array (sub_704CB8), iterates node deque a1[164..168]
  160B stride, new(0x1F4=500B) per entry, +16=type 2, AddRef node[0]. Returns dispatch Array.
- lastTime RO sub_6D9448: reads +1136(_loopTime); if >0 return *1000/60 else return raw.
- setCoord 0x6CCFF8: root+1592=x, root+1600=y, root+1584=dirty (combined, guarded if either changed).
- getCoord 0x6CD738: out-params from root+1592/+1600 (this is getCoord pair, NOT int `coordinate` prop).
- colorWeight setter 0x6CD724: +1156 = (a&0xFF00FF00)|byte2|(byte0<<16) R/B swap.

**Local deviations found (file:line):**
- angleDeg PROPERTY UNIT BUG: Player.h:562 getAngleDeg()=emoteGetAngleRadLike (returns RAD),
  setAngleDeg(PlayerCore.cpp:397) takes rad. But main.cpp:195 binds property `angleDeg`. So the
  `angleDeg` TJS property returns radians and `angleRad`(Player.h:564) returns raw deg from
  root.delta.angle. Names are SWAPPED relative to units. Need binary NCB reg to confirm which
  getter binds to which name — UNVERIFIED which property name the binary attaches to 0x6CD0C0.
- getLoopTime SHAPE: Player.h:154 returns plain `double _loopTime`; binary 0x6D139C returns TJS
  Array. main.cpp:165/166 bind loopTime/frameLoopTime to scalar getLoopTime. SEVERE shape mismatch
  (carried from clusterE). NOTE: 0x6D139C may be a DIFFERENT property than scalar loopTime getter
  0x6D97AC (frameLoopTime, reads +1136) — two distinct binary getters; needs NCB-reg disambiguation.
- setChara 的旧 TYPE+SIDEEFFECT 结论已证伪；2026-07-18 本地已复原四个 live
  `ttstr` 值槽、两个 pending owner、dedup/invalidation 与 flush 调用链。
- setTickCount Player.h:220-229 NOW MATCHES binary (fmax clamp, _queuing+_firstFrame=true=+480word257,
  _clampedEvalTime=min). getTickCount Player.h:232 unconditional — MATCHES. (M16 fix landed correctly).
- setAngleDeg PlayerCore.cpp:397 MATCHES binary deg conversion + normalize + storage, but OMITS
  Player_initEmoteMotion(2) call in directEdit branch (TODO comment present).
- setCoord PlayerCore.cpp:450 MATCHES binary (combined dirty). adds _pendingRootX/Y bookkeeping (port extra, benign).
- coordinate/transformOrder (Player.h:615-618): plain int fields, no binary behavior driven.
  Binary getCoord 0x6CD738 is the float-pair coord getter, UNRELATED to int `coordinate` prop.
  Int property binary semantics NOT located — scaffold only.

**NOT VERIFIED (binary getter addr not located this session):** clear() method binary counterpart;
contains(label,x,y) full hit-test entry (sub_6B5AD8 is only the node-path-map lookup helper, returns
node ptr, not the x/y test); bounds dict builder; meshDivisionRatio Player delegating getter
(EmoteEngine+1168 field confirmed, Player-side delegate not decompiled).
