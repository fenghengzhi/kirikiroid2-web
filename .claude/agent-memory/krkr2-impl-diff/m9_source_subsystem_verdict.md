---
name: m9-source-subsystem-verdict
description: M9 source subsystem fresh-decompile verdict (2026-06-07 latest pass). RM ctor sub_6A88CC CALLS SourceCache ctor sub_6A78F4 on the SAME object → RM IS-A/HAS-A SourceCache prefix subobject (binary inheritance/embed); local two FULLY-INDEPENDENT classes is the one real architecture deviation (C-1). HashMap A=libstdc++ unordered_map ✅, layerId std::set RB-tree ✅, loadSource intrusive list (key,blendMode)-match + color in-place ✅ (local std::list faithful), findSource ObjSource facade ✅, NCB counts 12/6/3/4const all ✅, angleDeg/Rad direction ✅.
metadata:
  type: project
---

# M9 source subsystem — fresh-decompile verdict (2026-06-07, supersedes 2026-06-03/04 notes)

Re-decompiled this session: findSource 0x6AAB3C, RM ctor 0x6A88CC, SourceCache ctor 0x6A78F4, loadSource 0x6A7BA8, requireLayerId 0x6AB694, releaseLayerId 0x6AB750, random 0x6AB56C, clearCache 0x6A8438, bufLayer 0x6A84FC, RM registrar 0x6AB8BC, SourceCache registrar 0x6A85A8, Motion-ns 0x6D9B08, angleDeg 0x6C1780, angleRad 0x6CD0C0.

## CONFIRMED ✅ (no change needed)
- **NCB counts/order**: RM 12 (loadSource/clearCache/bufLayer/load/unload/unloadAll/isExistMotion/findMotion/findSource/random/requireLayerId/releaseLayerId) = main.cpp:579-590 EXACT. SourceCache 3 (loadSource/clearCache/bufLayer @0x6A85A8) = main.cpp:30-32. ObjSource 6. D3DEmotePlayer 4 consts on D3DEmotePlayer class (main.cpp:883-888) ✅. SourceCache+RM share callback addrs sub_6A7BA8/6A8438/6A84FC for the 3 overlapping members.
- **HashMap A (+88/+96)**: RM ctor 0x6a891c-0x6a8960 = std_Prime_rehash_policy_M_next_bkt(10) + operator new(8*bktcount) + memset = libstdc++ std::unordered_map bucket array. loadedModules migrated to unordered_map<ttstr,V,ttstr_hash,ttstr_equal> (ResourceManager.h:161) = correct container selection.
- **layerId std::set (+168)**: RM ctor 0x6a8a08-38 = _Rb_tree_insert_and_rebalance + operator new(0x28) (40B node) = std::set<uint>. requireLayerId 0x6AB694 = lower_bound-skip + insert + monotone counter@+216 (init 0x100000001→1). releaseLayerId 0x6AB750 = _M_erase_aux, no counter touch. Local usedLayerIds std::set + nextLayerId monotone = faithful. ctor pre-inserts {0} (inert, counter starts 1).
- **loadSource 0x6A7BA8 intrusive list (+72/+80)**: match loop 0x6a8004-0x6a8074 keys by (key sub_A10428 @v27+2, src ttstr @v27[7], blendMode @*(v27+16)) — COLOR NOT in match key. Color (v27+17..+20) compared separately @0x6a80d4, updated IN-PLACE on mismatch (0x6a80d8) then re-bake sub_6A6BE0 + clone-to-front LRU (0x6a8100-14 sub_6EAC60/sub_146359C). Local SourceCache.cpp:666 findEntry((key,blendMode)) + ensureEntry color in-place + std::list::splice to front = FAITHFUL data-flow (std::list node vs binary intrusive node = ABI-only, allowed per byte-layout methodology).
- **findSource 0x6AAB3C**: split "/" (sub_697D34) → pieces[0] "src"/"blank" gate (sub_9B1ED0); src path = FNV hash (byte-confirmed 0x6aac54) → HashMap A lookup sub_6EB8F4(+88, hash%+96) → dict["source"][grp]["icon"][ico] (sub_598C58/sub_5995D8 gates) → operator new(0x18) ObjSource facade (sub_6EC124). blank path = split ":" → width/height/originX/originY + blank=1 dict. Local ResourceManager.cpp:231 reproduces both branches.
- **clearCache 0x6A8438**: resets ONLY +72 list (releases Layer via vtbl+112), not hashmaps. Local note ResourceManager.cpp:164 correct.
- **angleDeg 0x6C1780 = raw deg; angleRad 0x6CD0C0 = deg*0.0174532925**: direction CORRECT.

## OPEN DEVIATION C-1 (MEDIUM, architecture) — RM/SourceCache class relationship
**RM ctor sub_6A88CC FIRST instruction (0x6a88f8) calls SourceCache ctor sub_6A78F4() on the SAME a1 object**, initializing SourceCache fields +20 (primaryLayer)/+36/+40 (bufLayer Layer)/+56/+64 (layerType)/+72/+80 (intrusive list head). RM ctor then initializes its OWN fields from +88 (HashMap A) up. Signature of **RM being a DERIVED class of SourceCache (or embedding it as the [0,88) prefix subobject)** — binary `class ResourceManager : public SourceCache`. The 3 shared loadSource/clearCache/bufLayer members are BASE-class members re-listed on derived RM's NCB table (normal C++ NCB inheritance), NOT mere address-sharing between unrelated classes.
- **Local reality**: motion::ResourceManager (ResourceManager.h:103) and motion::SourceCache (SourceCache.h:38) are TWO FULLY INDEPENDENT classes, ZERO is-a/has-a. RM uses shared_ptr<State>; SourceCache has its own _owner/_primaryLayer/_bufLayer/_entries. Binary RM ITSELF owns a +72 layer-list + bufLayer Layer (inherited); local RM has NONE (RM._bufLayer is an unused ttstr scaffold). So binary Motion.ResourceManager.loadSource(...) materializes a Layer into RM's own +72 list; local RM.loadSource just forwards to RM.load (no layer-list). Genuine structural gap.
- 2026-06-04 verdict "two-class split is CORRECT, method-sharing not class-identity" is HALF RIGHT (yes 2 NCB classes) but MISSES that binary RM derives-from/embeds SourceCache.
- Severity MEDIUM not HIGH: oracle-inert under current fixtures (no test calls Motion.ResourceManager.loadSource expecting a Layer; Player uses its own SourceCache). Align by `class ResourceManager : public SourceCache` (or embed as first member), wiring RM.loadSource/clearCache/bufLayer through inherited SourceCache state.

## OPEN DEVIATION C-2 (LOW) — RM container keys
HashMap A keyed by source NAME (FNV of name); local loadedModules keyed by load PATH. Same container family (unordered_map) now, but key identity differs. findMotion/isExistMotion honest STUBs (return false/void) — no name-keyed registry locally. Parked, oracle-inert.

## NOT a deviation (cleared)
- per-vertex color: settled 2026-06-05, genuine CPU per-pixel bake boundary (sub_6A7518), local applyPackedCornerTintLike faithful. Do not re-open.
- ObjSource dict facade (not fields struct): local SourceCache.h:131 correct.
