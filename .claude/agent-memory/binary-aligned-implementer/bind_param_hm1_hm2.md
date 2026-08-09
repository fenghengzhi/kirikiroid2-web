---
name: bind-param-hm1-hm2
description: Player_bindParameterValue @0x6C4668 — HM1 cascade upsert vs HM2 green-critical write, what is safe to implement vs must defer
metadata:
  type: project
---

Player_bindParameterValue_writesHM1_HM2 @0x6C4668 → bindParameterValueLike_0x6C4668 in PlayerVariable.cpp.

Two-region control flow:
- TOP HALF (HM1 cascade @0x6c46c4..0x6c4968): gated by `sub_6D0BF4(...)&1` @0x6c46bc — only runs when label splits (has `::` or `/`). scopeJoin=scope?scope:"::"; joinedKey=label?scopeJoin+label:scopeJoin; HM1.upsert(joinedKey) into _evalCascadeMap (Player+264). node+40 weight=1.0 (seeded once @0x6c4964), node+32 writeVal=a4 (every bind @0x6c4968).
- LABEL_132 (HM2 @0x6c4c0c): `Player_HM2_upsert_labelToValue(a1+320, a2=rawLabel)=a4`. a2 is the RAW input label, NOT the joined key. Port mirror = `_evalResultValues[label]=value` (value-identical).

**Green-critical equivalence:** HM2 stores raw-label→a4. HM1 writes a SEPARATE structure (joined-scope key) and never feeds HM2, so adding HM1 cannot drift HM2. Confirmed by motion_playback wasmtime differential: m2logo+yuzulogo PASS after HM1 added.

**DEFERRED (no port consumer + unported input, do NOT guess):**
- chainDispatches `sub_697D34` @0x6c48bc — pure TJS-dispatch scope resolution (splits scope by sep, calls sub_A0CBEC/sub_A0CA58 ttstr find/substr, builds vector). No validator: getVariable reads HM2 not HM1; cascade-read getVariable @0x533E1C out of scope.
- aux node list `sub_6B9650` @0x6c4974 (node+48 vector of type-3/4 child nodes).
- node+408 controller ramps (sub_6F2F98 RB-tree lower-bound find, lerp `out=dur>0?dur*(clamp(val,lo,hi)-lo)/(hi-lo):0`). RB-trees unpopulated in port (controller system unported) → ramp loops are no-ops.

**HM1/HM3/HM4 maps are declared-but-never-cleared dead storage.** _evalCascadeMap (HM1) now populated by bind but not cleared on reset — same state as HM3 _perNodeLayerStateMap / HM4 _dispatchAliasMap (all await resetMotionState port). Harmless: no reader. When reset is ported, clear all three together.

Subfunction map: HM2 upsert=0x686944 (was ttstr_doubleMap_upsert), HM1 upsert=0x6F52AC, HM1 find=0x6F51BC, split=0x6D0BF4. ttstr_hash inline (1025*^>>6,*9,*32769^>>11) == internal/ttstr_hash.h ttstr_hash_utf16, reuse it.

> **2026-07-26 superseding correction:** 上述旧记录只覆盖非 null key 的算术 mix，错误地把它外推为 `ttstr_hash` 完整对齐。fresh 证据证明：`ttstr::Ptr == nullptr` 时 hash 为 `0`；Ptr 非 null 时先复用 `Hint@+68`，Hint 为 `0` 才计算并写回；仅非 null 计算结果为 `0` 时改为 `0xFFFFFFFF`。当前 `internal/ttstr_hash.h` 已按此修复，旧“inline 等同完整 functor”结论不得继续使用。
