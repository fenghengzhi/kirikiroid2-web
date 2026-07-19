---
name: m9-source-subsystem-verdict
description: M9 source subsystem corrected verdict (2026-07-18). ResourceManager inheritance, mapped-record topology/lifetime, and both Win/spec=2 plus KRKR/spec=1 raw PSBRawNode source paths are restored; KRKR full-page upload remains a documented Web API boundary.
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
- **findSource 0x6AAB3C**: split "/" (sub_697D34) → pieces[0] "src"/"blank" gate (sub_9B1ED0); src path = FNV hash (byte-confirmed 0x6aac54) → HashMap A lookup sub_6EB8F4(+88, hash%+96) → mapped `PSBFile` raw root 上执行 fixed-key strict / dynamic-key has+strict 导航 `source/group/icon/name` → operator new(0x18) ObjSource raw-node facade。对象三只 qword 是 retained raw owner/node pair + null lazy texture；`sticky=false/err=false` adaptor 失败只返回 void，保留新对象泄漏边界。blank path = split ":" → width/height/originX/originY + blank=1。Local ResourceManager.cpp reproduces both branches without an intermediate TJS dictionary walk.
- **clearCache 0x6A8438**: resets ONLY +72 list (releases Layer via vtbl+112), not hashmaps. Local note ResourceManager.cpp:164 correct.
- **angleDeg 0x6C1780 = raw deg; angleRad 0x6CD0C0 = deg*0.0174532925**: direction CORRECT.

## RESOLVED C-1 — RM/SourceCache class relationship
**RM ctor sub_6A88CC FIRST instruction (0x6a88f8) calls SourceCache ctor sub_6A78F4() on the SAME a1 object**, initializing SourceCache fields +20 (primaryLayer)/+36/+40 (bufLayer Layer)/+56/+64 (layerType)/+72/+80 (intrusive list head). RM ctor then initializes its OWN fields from +88 (HashMap A) up. Signature of **RM being a DERIVED class of SourceCache (or embedding it as the [0,88) prefix subobject)** — binary `class ResourceManager : public SourceCache`. The 3 shared loadSource/clearCache/bufLayer members are BASE-class members re-listed on derived RM's NCB table (normal C++ NCB inheritance), NOT mere address-sharing between unrelated classes.
- **Current local reality (2026-07-18)**: `ResourceManager : public SourceCache`; the inherited `_bufLayer`/`_entries` state serves the re-listed NCB base members. The former `shared_ptr<State>`, RM-local `_bufLayer`, and unrelated-class forwarding path have been removed.
- 2026-06-04 verdict "two-class split is CORRECT, method-sharing not class-identity" is HALF RIGHT (yes 2 NCB classes) but MISSES that binary RM derives-from/embeds SourceCache.
- This is no longer an open deviation.

## RESOLVED C-2 / source record and pixel paths
Fresh decompile of `ResourceManager_loadResource @0x6A8D8C`, outer insertion `sub_6EB9E4 @0x6EB9E4`, mapped-record ctor `sub_6EBCFC @0x6EBCFC`, destruction `sub_6DB3E8 @0x6DB3E8`, `Player_findSource @0x6948E8`, and KRKR atlas `sub_695DE8 @0x695DE8` proves the outer key is the module context/path, not a source group. Its mapped value declares, in order: raw PSBFile, Win `ttstr -> texture` map, KRKR flat `src/group/icon -> descriptor` map. Local `LoadedResourceRecord` mirrors that order, `rehash(10)`, ownership and erase/clear lifetime. Both spec paths now navigate `record.file` raw nodes; KRKR also restores all-group enumeration, raw/RL/palette decoding, transparent 2x2 handling and descriptor insertion. Only the full-page Web upload primitive is a documented platform adaptation.

## NOT a deviation (cleared)
- per-vertex color: settled 2026-06-05, genuine CPU per-pixel bake boundary (sub_6A7518), local applyPackedCornerTintLike faithful. Do not re-open.
- ObjSource is a raw-node facade, not a `tTJSVariant` dictionary facade: local `PSBRawNode + iTVPTexture2D*` fields, strict/try getters, clip/ensureTexture/drawLayer and texture→owner destruction order are aligned. The older dict-facade verdict is disproven.
