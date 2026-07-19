---
name: player-cluster-e-accessors
description: 簇E Player accessor 二进制地址↔语义权威映射 + 旧 IDB off-by-one 纠正记录(chara/stealthChara/tickCount/angle/loopTime/variableKeys/ctor)
metadata:
  type: project
---

> **2026-07-19 纠正：** 文内 `_activeMotion->variableLabels` 是已删除的旧偏差；
> `variableKeys@0x6D139C` 当前直接读 Player+1296 deque，Player 已无 `_activeMotion`。

# Player (motion::Player, 1384B, ctor 0x6CED30) 簇E accessor 权威映射

**Why:** 2026-05-30 cluster E 审计的多条 accessor 偏差结论建立在 IDB off-by-one 误命名上;2026-06-07 fresh decompile 全部纠正。
**How to apply:** 再审 Player accessor 时以下面地址为准,勿用 2026-05-30 doc 的旧符号名。

## chara / stealthChara 体系(关键纠正)
- **2026-07-18 correction:** 四槽均为 refcounted TJS string-value owner，源码层对应 `ttstr`；+1099 是 `playing`，不是 motion-loaded。
- `chara` getter/setter = **0x6D9470/0x6C0E9C**。writer mode 0 写 +960(primary chara) 与 +968(stealthChara)，清 +976/+984/+1099，然后 flush pending +776。
- `stealthChara` getter/setter = **0x6D9490/0x6D94B0**。mode 16 只写 +968；live slot 不存在时 queue +776。
- 共享 dedup helper = **0x6B29C0** (Player_setCharaOrKeySlot_dedup): `a2&0x10`→target +968 else +960;wcscmp(sub_9B1ED0) dedup;变更时 AddRef/Release;末尾清 +976/+984/playing(+1099)。
- 2026-05-30 doc 误把 0x6D94B0 当 `chara` setter 并称其 re-dispatch sub_6B29C0(16) — 实为 stealthChara setter。

## scalar accessor(已 fresh-verify)
- `tickCount` setter = 0x6D96C0: `+1120=fmax(v*60/1000,0); *(WORD)+480=257; +456=min(+1120,+1128)`。本地 setTickCount **对齐**。
- `tickCount` getter = 0x6D96A0: `+1120*1000/60` 无 guard。本地 getTickCount **对齐**。
- `loopTime` member getter = **0x6D9448** (Player_getLoopTime): `+1136>0 ? +1136*1000/60 : +1136`。本地 getLastTime() **对齐**(本地 NCB 把 loopTime/lastTime/frameLoopTime 都绑 getLastTime/getLoopTime scalar)。
- `variableKeys` member getter = **0x6D139C** (Player_getVariableKeys,非 loopTime!): 走 var-track deque @Player+1296,每 cascadeKey `operator new(0x1F4=500)` + `+16=2`(string tag) + AddRef → TJS Array。本地 getVariableKeys() 读 `_activeMotion->variableLabels`(不同 source = +1296 deque),**DEVIATION 但 inert**(现有 motion 无 variable)。
- `angleDeg` setter = 0x6C0F84(deg-direct), getter = 0x6C1780(raw→deg);`angleRad` setter = 0x6CD0EC(rad*57.29→deg), getter = 0x6CD0C0(deg*0.0174→rad)。本地全对齐(2026-06-03 修正了曾接反的绑定)。

## ctor 0x6CED30 fresh-verify
- +992 = **第3份 RM dispatch 拷贝**(sub_A0F5E0 @0x6cef28),**不是 RandomGenerator**。RandomGen(sub_9C8440)在 **+676**(0x6cf024)。本地 `_tjsRandomGenerator // player+992` 注释 **错**(应 +676);+716 第2个 sub_9C8440 对象(设 "color" param)本地无对应。
- +912=100(pixelateDivision,本地 _pixelateDivision 对齐)、+1092=0(preview,非 completionType,本地 _preview 对齐)、completionType 真身=+1144 int。
- 4 HM(+264/+320/+1184/+1240)全是 std::unordered_map(std_Prime_rehash_policy_M_next_bkt + 1.0f load),本地 4 map 选型对齐。ctor 只 push 1 个 root node。
