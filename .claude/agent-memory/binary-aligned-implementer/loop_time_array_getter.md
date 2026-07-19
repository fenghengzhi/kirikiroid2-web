---
name: loop-time-array-getter
description: CORRECTED — binary Player "loopTime" member returns a SCALAR (Player_getLastTime); the TJS-Array getter 0x6D139C is the "variableKeys" member. a343ce9's "loopTime returns Array" was a registration-table misread that crashed Senren Banka.
metadata:
  type: project
---

> **2026-07-19 纠正：** 本文旧实现中从 `_activeMotion->variableLabels` 读取 variableKeys
> 的描述已失效；当前 getter 直接遍历 Player+1296 var-track deque，Player 已无 `_activeMotion`。

**R0-3 (a343ce9, 2026-06-03) WAS WRONG — corrected 2026-06-04.**

The claim "binary `loopTime` getter == Player_getLoopTime_array @0x6D139C (TJS Array)"
is FALSE. Fresh decompile of `Player_ncb_registerMembers` (0x6d6c80) — the actual
ncb_addMember pairings, in address order:

- `0x6d6c68`: v9.getter = `Player_getLastTime` (0x6d9448)
- `0x6d6c88`: `ncb_addMember(L"loopTime", v9)`  → **"loopTime" → Player_getLastTime (SCALAR)**
- `0x6d6ccc`: v10.getter = `0x6D139C`
- `0x6d6cec`: `ncb_addMember(L"variableKeys", v10)` → **"variableKeys" → 0x6D139C (the cascadeKey Array)**

`Player_getLastTime` (0x6d9448) = `r = *(double*)(this+1136); return r>0 ? r*1000/60 : r;`
— a scalar (frames→ms). So `_player.loopTime` is a NUMBER in the binary.

**Why it mattered:** `AffineSourceMotion.canSync` does `_player.loopTime < 0` on a
(non-emote) `Motion.Player`. a343ce9 bound the local Player "loopTime" to the
Array getter `getLoopTimeArrayLike_0x6D139C`, so the comparison coerced an Array
→ real and threw "Cannot convert (object Array) to real" at custom.ks:89 (Senren
Banka 千恋万花 logo). 1053775 propagated the same array-return into
`EmotePlayer::getLoopTime()`. logo_test never tripped it because its hand-written
loop never calls `canSync`.

**Fix (2026-06-04):**
- main.cpp Player block: `NCB_PROPERTY_RO(loopTime, getLastTime)` (was getLoopTimeArrayLike).
- EmotePlayer.h #32: `getLoopTime()` reverted to scalar `return player().getLoopTime();`.
- Deleted the orphaned `Player::getLoopTimeArrayLike_0x6D139C()` (Player.h + PlayerTimeline.cpp).
- IDB: `Player_getLoopTime_array` → `Player_getVariableKeys_0x6D139C`; corrective
  set_comments at 0x6d139c / 0x6d6c88 / 0x6d6cec; idb_save.
- Verified live: full-game ZIP now passes yuzulogo.mtn → m2logo.mtn, 0 object→real.

**Lesson (same class as the bustScale/queing mislabel):** the ncb_addMember KEY
string is the naming authority. Pair each getter to the L"..." key at its OWN
addMember site — do NOT assume the function whose IDB label looks related is the
one bound. Here `Player_getLoopTime_array` (an IDB label) was bound to
"variableKeys", not "loopTime".

**Separate OPEN item (NOT this regression):** binary "variableKeys" → 0x6D139C
walks the var-track `std::deque<VariableLabelScope>` @Player+1296 cascadeKeys,
but local "variableKeys" → `getVariableKeys` reads `_activeMotion->variableLabels`
(cached `_variableKeys`) — different data source. Reconcile separately (also note
the IDB getter cluster here is heavily swap-labeled: sub_6D9470 "getVariableKeys"
is bound to member "chara" and returns a cached +960 object). See [[motionplayer-setvar-progress-resolved]].
