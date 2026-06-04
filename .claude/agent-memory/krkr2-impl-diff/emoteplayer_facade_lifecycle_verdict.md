---
name: emoteplayer-facade-lifecycle-verdict
description: Fresh-decompile audit of EmotePlayer/D3DEmotePlayer facade + EmoteObject lifecycle + NCB registration across 6 restoration dims — verified offsets, byte-confirmed recent fixes, and the queing/setOuterForce divergences
metadata:
  type: project
---

2026-06-04 fresh-decompile read-only audit (EmotePlayer.cpp/.h, D3DEmoteModule.h, main.cpp NCB).

BYTE-VERIFIED CORRECT (recent commits, do not re-question):
- completionType = Player +1144 int. `Player_getCompletionType@0x6d9624` reads *(uint*)(this+1144); setter `@0x6d962c` writes +1144. (commit d74f41e)
- preview = Player +1092 u8 bool. `Player_getPreview@0x6d9634` reads *(u8*)(this+1092). NOT completionType. (d74f41e; corrects old off-by-one IDB symbols Player_getProject/getCompletionType which were renamed).
- bustScale = engine +1200 double. `D3DEmotePlayer_get/setBustScale@0x530130/0x530140`. (commit 222b176)
- queing = engine +1161 u8 BYTE flag. getter `@0x5300cc`, setter `@0x5300dc` writes engine+1161=1 CONSTANT (ignores arg = set-always-1 trigger). (222b176)

Lifecycle (all ✅ aligned, byte-verified):
- D3DEmotePlayer_create@0x52FD84: destroy +32(secondary), destroy +24(primary), null both. == local create() EmotePlayer.cpp:122.
- D3DEmotePlayer_load@0x52FDD4: same teardown + operator new(0x28)+EmoteObject_init on +24 ONLY (secondary stays null); binary loops a2 PSBs into vector. == local load() EmotePlayer.cpp:133 (modeled single-PSB _modules.assign(1,data)).
- setScale@0x530260: finalScale = baseScale(+40)*userScale(+44), sets +1162=1 (modified). Also threads byte@+1161(queing) as 3rd arg to Animator_setKeyframes. == local setScale EmotePlayer.cpp:281 (but queing-flag pass NOT threaded — see GAP D-2).
- contains@0x530b5c: AddRef label -> sub_6B5AD8(player+1064,label,1) -> Player_hitTest(node+1664,x,y) -> Release. No AABB/visible/empty guard. == local player().hitTestLayer (D-09 removal verified correct).
- startWind@0x6709AC: this=ENGINE, gate absAmp==0||normMin==normMax||(fx==0&&fy==0); facade thin-forwards. ✅

REAL GAPS found this audit:
- **D-1 (P1) D3DEmotePlayer::setQueuing wrong field+semantics**: local EmotePlayer.h:335 `player().setQueuing(v)` writes Player+480 _queuing with ACTUAL ARG. Binary writes engine+1161=1 always. The sibling EmotePlayer (other class) EmotePlayer.h:223 is CORRECT (`engine()._emoteAnimatorFlag=true`). Fix D3DEmotePlayer copy to match: `engine()._emoteAnimatorFlag=true`. The R3 header comment claiming "route through Player+480" is the misalignment.
- **D-3 (P2) Player::setOuterForce middle label string mismatch**: binary 0x672D58 matches L"bust"(+1104)/L"hair"(+1112)/L"parts"(+1120); local PlayerCore.cpp:1576 matches "bust"/"h"/"parts" — middle key is "h" not "hair". Also binary threads byte@+1161 into Animator_setKeyframes; local OuterForceState path omits it.
- D-2 (P2) setScale queing-flag (+1161) not threaded to keyframe sink (Player::setEmoteScale internal, unaudited).
- D-4 (P3) 4 port-invented D3DEmotePlayer dup members (bodyScale/playCallback/setTimeline/addPlayCallback) — re-confirm in current main.cpp 884-956 (see ncb-surface-verdict).

OBSOLETE: the 2026-04-05 misalignment report. M2 memory line saying setVariable arch ❌ is also stale (rewritten to faithful disjoint-map).
