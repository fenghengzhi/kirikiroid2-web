---
name: gap1-player936-dead-buffer
description: GAP-1 childMotion render-list aggregation — player+936/944 is a DEAD/vestigial buffer in libkrkr2.so (no producer, no consumer). DONE 2026-06-07 via option (b): separate dead member, faithful inert aggregation. Do NOT map onto live _preparedRenderItems.
metadata:
  type: project
---

GAP-1 ("childMotion render-list aggregation child→parent, sub_6BE0C0 @0x6BE2C0 sub_6F363C(parent+936, child+936, child+944)") DONE 2026-06-07 via user-chosen option (b): separate DEAD member + faithful inert aggregation. (Was HELD pending user decision; superseded.)

**Binary facts (re-confirmed by fresh decompile this session):**
- `player+936/944` (qword idx 117/118) = `std::vector<RenderItem44>`, elem=44B `{int32 kind@+0; tTJSVariant a@+4; tTJSVariant b@+24}`. Copy via sub_A0FB64 (tTJSVariant copy-ctor, 20B), destroy via sub_A0F778 (tTJSVariant dtor).
- ctor Player_ctor @0x6CEF1C: `*(_OWORD*)(this+936)=0` = empty vector (begin/end zero). dtor: per-elem variant destroy + free.
- **DEAD buffer**: WRITERS only (sub_6BE0C0 @0x6BE2C0 childMotion + sub_6C17A4 @0x6C1A00 particleStepChildren, both aggregate child→parent then clear child; plus ctor/dtor). NO PRODUCER (sub_6C2334 writes caller-stack temps not +936), NO CONSUMER. sub_6C14D4 standalone merge helper has 0 xrefs. So always empty→empty, observably inert.
- sub_6F363C = libstdc++ `vector<RenderItem44>::_M_range_insert(pos,first,last)`. Both call sites insert at parent.BEGIN (a2/2nd arg = `*(parent+936)` = parent.begin), source = child+936..944. Then clear child: destroy each elem's two variants (sub_A0F778(+24) then sub_A0F778(+4)), set child+944=child+936 (end=begin, keep capacity).

**Implementation (this build):**
- `detail::DeadChildMotionRenderItem` POD `{int kind; tTJSVariant a; tTJSVariant b;}` in RuntimeSupport.h (1:1 of 44B elem; std::vector element copy/destroy = sub_A0FB64/sub_A0F778). Did NOT reuse PreparedRenderItem (rich struct, wrong layout).
- Player member `std::vector<detail::DeadChildMotionRenderItem> _childMotionRenderAggregate` (Player.h) = player+936/944. ctor/dtor auto (value member): empty-init matches @0x6CEF1C, vector dtor matches binary dtor.
- Helper `Player::aggregateChildMotionRenderItemsLike_0x6F363C(Player &child)` (PlayerUpdateChildMotion.cpp): `_childMotionRenderAggregate.insert(begin(), child.begin(), child.end())` (sub_6F363C begin-insert) + `child._childMotionRenderAggregate.clear()` (destroy variants + size→0 keep capacity = child end=begin).
- Wired at 2 sites: childMotion pass after child.frameProgress+updateLayers (PlayerUpdateChildMotion.cpp, 0x6BE2C0); particle pass after child frameProgress+updateLayers (PlayerUpdateParticles.cpp, 0x6C1A00).
- The former Player-owned live `_preparedRenderItems` was later disproved and removed. `sub_6C2334` uses caller-stack main/aux `vector<PreparedRenderItem *>` lists; every `MotionNode` owns its persistent `PreparedRenderItem*`, and those stack lists only borrow node-owned pointers. This remains distinct from the dead +936 aggregate.

**Verification:** web/debug + krkr2_wasmtime_guest build clean (248/248, 31/31). Logo differential PASS bit-identical: m2logo 93f + yuzulogo 243f. All 3 changed .cpp in wasmtime guest source list (no new files). ORACLE-INERT: buffer恒空 (logo aggregates empty→empty), so byte-identical = non-regression guard, NOT exercise of populated aggregation. No fixture seeds +936 (honest verification gap — binary itself never seeds it). Self-audit line-by-line PASS (Task/auditor agent unavailable this session, audited inline from fresh decompile).
