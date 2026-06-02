---
name: loop-time-array-getter
description: R0-3 getLoopTime — binary loopTime property returns TJS Array of var-track cascadeKeys, NOT scalar; frameLoopTime is the scalar one
metadata:
  type: project
---

R0-3 RESOLVED 2026-06-03. Two distinct Player NCB properties were conflated locally:

- `frameLoopTime` getter == Player_getFrameLoopTime @0x6D97AC == `return *(double*)(this+1136)` (scalar). NCB reg disasm @0x6d7d10. Local: `getLoopTime()` returns `_loopTime` (+1136) — CORRECT for this binding.
- `loopTime` getter == Player_getLoopTime_array @0x6D139C — builds a **TJS Array** (sub_704CB8 = TJSCreateArrayObject), iterates the var-track `std::deque<VariableLabelScope>` @Player+1296 (deque iterator fields a1[164]/[166]/[167]/[168], 160B stride, 3-elem/480B chunks), pushes a type-2 (string) variant of `*v4` == element[0] == `VariableLabelScope::cascadeKey` (item+0), AddRef'd. Returns via tTJSVariant copy (sub_A0F5E0 / sub_A0F778). No label arg. NCB reg disasm @0x6d6c80 "loopTime" -> 0x6D139C, new(0x50)=property RO.

**Why:** prior main.cpp bound BOTH `frameLoopTime` and `loopTime` to scalar `getLoopTime`, with a stale "getLastTime" comment on loopTime — both wrong-shape for loopTime.

**How to apply:** local impl = `Player::getLoopTimeArrayLike_0x6D139C() const` in PlayerTimeline.cpp using `detail::makeArray(items)` (TJSCreateArrayObject + per-item add + tTJSVariant(array) return — faithful equiv of binary's manual new(0x1F4) chunk-append, which is just TJSArrayObject's internal element-append). Pushes cascadeKey in deque insertion order.

GOTCHA: `getLoopTimeline(ttstr label)->bool` (PlayerTimeline.cpp:99) is a SEPARATE D3DEmotePlayer-only method (queries _activeMotion->loopTimelines map). Do NOT conflate it with the `loopTime` property — different class table (0x52E504 vs 0x6D69C8), different signature. EmotePlayer's scalar `getLoopTime()` is also separate and untouched.

Sub-fn map: sub_704CB8=TJSCreateArray+variant-wrap; sub_53415C=deque grow(500B elem); sub_A0F5E0=tTJSVariant copy-ctor (case1=obj,2=string,3=octet refcount); sub_A0F778=variant destruct.

Verification gap: no fixture exercises this NCB property; evidence = fresh decompile of 0x6D139C + NCB reg disasm. Noted in code comment. Build out/web/debug green; no new .cpp so wasmtime guest source-list unaffected.
