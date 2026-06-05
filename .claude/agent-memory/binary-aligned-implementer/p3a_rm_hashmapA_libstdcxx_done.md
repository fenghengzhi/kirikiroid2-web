---
name: p3a-rm-hashmapA-libstdcxx-done
description: P3-A RM HashMap A (loadedModules) container-selection aligned to libstdc++ unordered_map<ttstr,V,ttstr_hash,ttstr_equal>; FALSIFIED prior "KiriKiri inline bucket hashmap" note
metadata:
  type: project
---

P3-A DONE 2026-06-05. ResourceManager::State::loadedModules migrated `unordered_map<std::string(raw path), tTJSVariant>` → `unordered_map<ttstr, tTJSVariant, detail::ttstr_hash, detail::ttstr_equal>` (same functor selection as the 4 Player HashMaps, internal/ttstr_hash.h).

**Why:** P3-A architectural plan (analysis/MotionPlayer_P3_Architectural_Plan_2026-06-05.md). Container-selection alignment with binary RM HashMap A (+88/+96).

**FALSIFIED + corrected in-place** (ResourceManager.h comment): prior note claimed "binary is NOT libstdc++ std::unordered_map but a KiriKiri inline bucket hashmap". Fresh decompile proves it IS libstdc++ std::unordered_map with a custom FNV hash functor:
- RM ctor sub_6A88CC @0x6A88CC: a1+96 = _M_next_bkt(10) (sub_149EDF8); a1+88 = operator new(8*bucketCount) bucket-array of node-chain head ptrs (==&single-bucket when count==1).
- lookup sub_6EB8F4 @0x6EB8F4 = _M_find_before_node walk: bucket=hash%a1[1]; node[0]=next, node[1]=key ttstr, node+136=cached hash, node+16=value.
- findOrInsert sub_6EB9E4 @0x6EB9E4 returns node+16; hash functor = KiriKiri FNV (0x6eba2c-0x6eba78, cached at key+68 == ttstr+68), equal functor = case-SENSITIVE ordinal wcscmp sub_9B1ED0 @0x9B1ED0.

**Case-normalization cross-check (strong assertion, verified):** binary HashMap A is CASE-SENSITIVE — FNV at 0x6eba2c hashes raw UTF-16 code units (no case-fold), sub_9B1ED0 is plain wcscmp (no tolower). Local `load()` already keyed by `rawPath` (un-lowercased); `lowercase()` only ever fed the `.mtn` warning log, never the actual key. So NO case-normalization was lost in migration — local was already raw-path-keyed, matching binary. All key sites traced: load/unload/findLoaded/clearCache/unloadAll all internal to ResourceManager.cpp; callers (PlayerInternal.h:278 load(candidate ttstr), PlayerCore.cpp:687 getLastLoadedModule) pass ttstr. lastLoadedPath migrated std::string→ttstr (.clear()→.Clear()).

Removed `_psbDictCache` empty scaffolding field (was based on the falsified "not std container" premise; zero consumers; HashMap A IS loadedModules now).

web/debug + krkr2_wasmtime_guest build clean. logo differential PASS bit-identical (m2logo 93f, yuzulogo 243f) — load path IS exercised by logo, genuine oracle-guarded non-regression (P3-A is NOT oracle-inert).

OPEN: P3-B (RM ownership native→dispatch-in + parentPlayer chain) — high-intrusion, separate session, see plan §P3-B.
