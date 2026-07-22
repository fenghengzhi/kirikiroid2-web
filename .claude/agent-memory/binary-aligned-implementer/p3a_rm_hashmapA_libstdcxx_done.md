---
name: p3a-rm-hashmapA-libstdcxx-done
description: P3-A RM HashMap A container-selection aligned to libstdc++ unordered_map<ttstr,LoadedResourceRecord,ttstr_hash,ttstr_equal>; FALSIFIED prior inline-bucket, Variant-value and lastLoaded claims
metadata:
  type: project
---

P3-A container choice was completed 2026-06-05. **Current correction
2026-07-22:** ResourceManager no longer has a shared `State`; the inline map is
`unordered_map<ttstr, LoadedResourceRecord, detail::ttstr_hash,
detail::ttstr_equal>`. `LoadedResourceRecord` contains, in declaration order,
`PSBFile + Win source-texture map + KRKR source-entry map`; it is not a
`tTJSVariant` value. The earlier `findLoaded/getLastLoadedModule/lastLoadedPath`
model was disproved by `0x6A8D8C/0x6A959C/0x6A8CF8` and
`EmoteObject_init@0x67DBAC` and has been deleted. `clearCache@0x6A8438` clears
only the inherited SourceCache layer list; HashMap A is changed only by
load/unload/unloadAll and read by findSource/isExistMotion/findMotion/Player
source lookup.

**Why:** P3-A architectural plan (analysis/MotionPlayer_P3_Architectural_Plan_2026-06-05.md). Container-selection alignment with binary RM HashMap A (+88/+96).

**FALSIFIED + corrected in-place** (ResourceManager.h comment): prior note claimed "binary is NOT libstdc++ std::unordered_map but a KiriKiri inline bucket hashmap". Fresh decompile proves it IS libstdc++ std::unordered_map with a custom FNV hash functor:
- RM ctor sub_6A88CC @0x6A88CC: a1+96 = _M_next_bkt(10) (sub_149EDF8); a1+88 = operator new(8*bucketCount) bucket-array of node-chain head ptrs (==&single-bucket when count==1).
- lookup sub_6EB8F4 @0x6EB8F4 = _M_find_before_node walk: bucket=hash%a1[1]; node[0]=next, node[1]=key ttstr, node+136=cached hash, node+16=value.
- findOrInsert sub_6EB9E4 @0x6EB9E4 returns node+16; hash functor = KiriKiri FNV (0x6eba2c-0x6eba78, cached at key+68 == ttstr+68), equal functor = case-SENSITIVE ordinal wcscmp sub_9B1ED0 @0x9B1ED0.

**Case-normalization cross-check (strong assertion, verified):** binary HashMap A is CASE-SENSITIVE — FNV at 0x6eba2c hashes raw UTF-16 code units (no case-fold), sub_9B1ED0 is plain wcscmp (no tolower). Local `load()` keys the map with `TVPGetPlacedPath(path)` and `unload()` performs the same normalization before lookup; no extra case fold is introduced.

Removed `_psbDictCache` empty scaffolding field (was based on the falsified "not std container" premise; zero consumers; HashMap A IS loadedModules now).

web/debug + krkr2_wasmtime_guest build clean. logo differential PASS bit-identical (m2logo 93f, yuzulogo 243f) — load path IS exercised by logo, genuine oracle-guarded non-regression (P3-A is NOT oracle-inert).

The old P3-B wording is historical: subsequent reconstruction removed the
shared-state/by-value duplicate RM model and established the single native RM
owned through its adaptor. Remaining current gaps are tracked in
`analysis/psbfile_android_reconstruction_2026-07-18.md`, not by this 2026-06-05
snapshot.
