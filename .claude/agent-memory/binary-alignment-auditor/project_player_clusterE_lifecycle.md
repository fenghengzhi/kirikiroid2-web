---
name: player-clusterE-lifecycle
description: Player(MotionPlayer) lifecycle audit findings — NCB 92-member binary set, non-scalar accessors, initVariables container mismatch
metadata:
  type: project
---

CLUSTER E audit (2026-05-30), ledger at analysis/audit_motionplayer_2026-05-30/clusterE_player_lifecycle.md.

**NCB member set is the big gap.** Player_ncb_registerMembers @ 0x6D69C8 registers
**92 distinct member names** (not 78). Extracted from L"..." literals in reg order.
24 binary members MISSING locally; 70 local-only.
- Missing properties: flipX flipY opacity visible slantX slantY zoomX zoomY
  angleDeg angleRad coordinate transformOrder bounds pixelateDivision lastTime
  meshDivisionRatio defaultSyncActive defaultTransformOrder. Missing methods:
  setCoord clear contains onAction onSync onGroundCorrection.
- Local-only timeline/variable query surface (countVariables, *Timeline*, etc.)
  belongs to D3DEmotePlayer in the binary, NOT Motion.Player — hoisted onto
  Player locally (class-boundary deviation).

**Accessors are NOT scalar in the binary** (local plain-field setters drop logic):
- setChara 0x6D94B0: variant mgmt at +968/+776 + re-play dispatch sub_6B29C0(16);
  local `_chara=v` plain assign is wrong type+missing logic.
- setTickCount_ms 0x6D96C0: fmax(v*60/1000,0); +480 word=257; +456=min(+1120,+1128).
  Local drops clamp + flag + clampedEvalTime write.
- getTickCount_ms 0x6D96A0: unconditional *1000/60 (NO >0 guard; local adds one).
- setAngleDeg 0x6CD0EC: emoteMode(+482) branch -> +464 + initEmoteMotion(2), else root+1616.
- getLoopTime 0x6D139C: builds TJS Array from +1312 deque (stride 20, elem new(0x1F4=500),
  +16=2). Local returns plain double `_loopTime` — SEVERE shape mismatch.

**initVariables Player_initVariables @ 0x6CD750**: drives off Player+528 ttstr-as-
dispatch PropGet(L"variable"/L"label"/L"scope"); pushes 160B items into the +1296
KiriKiri controller deque (Player_controllerDeque_init 0x6F4FD8, stride 160).
Local reads PSB structs and pushes into std::vector _variableLabelEntries (the +936
stride-44 list) — wrong container + wrong sink. scope split "::" only in binary
(local also tries ":").

**ctor 0x6CED30（2026-07-23 纠正）**: +676 是 render descriptor（不是
RandomGen）；+716 是 color 对象，0x6CF080 把 +716 以 L"color" PropSet 到
+676。+992 是第 3 份 RM dispatch 拷贝；`Player::random@0x6BA7B8` 经它调用
`random`，真正 RNG 在 `ResourceManager_ctor@0x6A88CC` 的 RM+144。Local
ctor 当时既未复刻 +676/+716 对象拓扑，也没有三个独立 RM variant 槽。Exactly 4 inline HMs
(+264/+320/+1184/+1240, prime-bucket sub_149EDF8(10), load 1.0f).

**Carried items**: P1-2 (HM2 std::string vs ttstr key) OPEN; P1-3 (6->4 HM map)
OPEN (ctor confirms exactly 4 HMs); P2-3 的“`_tjsRandomGenerator` 只是从
+992 改标 +676”结论已证伪——这不是 comment-only fix，而是 descriptor/color
对象拓扑与 RM-owned RNG 的归属问题。

0x6FDE74 = Player_ncb_classInit (new(0xB0), vtbl off_19FD6C8, registers only finalize).
