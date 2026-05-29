---
name: project-player-four-hashmaps
description: 1384B Player 类 4 张 libstdc++ std::unordered_map 哈希表（+264/+320/+1184/+1240）的 key/value/用途与本地 PlayerRuntime 6 map 对账结论
type: project
---

# Player 4 hash tables — libstdc++ std::unordered_map

All four are libstdc++ `_Hashtable` (sub_149EDF8 = `_Prime_rehash_policy::_M_next_bkt`, ttstr hash via `(1025*x)^((1025*x)>>6)`).

Common header layout (56 bytes):
- +0  void** buckets
- +8  size_t bucket_count
- +16 void* before_begin (chain head)
- +24 size_t element_count
- +32 float max_load_factor (=1.0)
- +40 size_t next_resize
- +48 char  single_bucket sentinel

## HM1 @ Player+264 — eval-result cascade map
- Insert: `Player_HM1_upsert_evalCascade` @ 0x6F52AC (caller: `Player_bindParameterValue_writesHM1_HM2` @ 0x6C4668)
- Clear:  `Player_HM1_clear` @ 0x6CF930
- Entry destroy: `Player_HM1_value_destroy` @ 0x6DD1A0
- Node: 96 bytes (`operator new(0x60)`). Layout: next(8) + ttstr*key(8) + value(72) + cached_hash(8)
- Key: ttstr formed as `"::" + right-half-of-label` (split `label` on "::")
- Value (72B starting at entry+16): tTJSVariant* (parsed key), `std::vector<tTJSVariant*>` controller list, `double currentValue` @+32, `double weight=1.0` @+40, pointer-pair animator queue @+48/+56
- **Used by**: parameter binding / eval cascade — caches "::"-suffixed eval results per controller key

## HM2 @ Player+320 — raw label → double
- Insert: `Player_HM2_upsert_labelToValue` @ 0x686944 (callers: `Player_bindParameterValue_writesHM1_HM2` @ 0x6C4668 and `Player_resetMotionState_clearAndRebuild` @ 0x6B2B7C)
- Entry destroy: anonymous inline in `Player_dtor` (just `tTJSVariant_Release(node[1])` + `operator delete(node)`)
- Node: 32 bytes (`operator new(0x20)`). Layout: next(8) + ttstr*key(8) + qword_value(8) + cached_hash(8)
- Key: ttstr (raw label, NOT "::"-split form)
- Value: `double` (set via `*(double*)result = a4` in bindParameterValue)
- **Used by**: `unordered_map<ttstr, double> evalResultValues` — the canonical "what numeric value did each variable resolve to"

## HM3 @ Player+1184 — per-layer-path → 696B layer state cache
- Insert: `Player_HM3_upsert_perNodeLayerState` @ 0x6F2674 (caller: `Player_resetMotionState_clearAndRebuild` @ 0x6B2B7C)
- Insert helper: `Player_HM3_insert_node` @ 0x6F2790
- Clear: `Player_HM3_clear` @ 0x6CF7C4
- Prune: `Player_pruneHM3_byNodeIdentity` @ 0x6B826C (per-frame: erase entries whose entry+16 != node+28)
- Entry destroy: `Player_HM3_entry_destroy` @ 0x6DD018 → recurses into `Player_HM3_value_destroy` @ 0x6DD06C
- Node: 720 bytes (`operator new(0x2D0)`). Layout: next(8) + ttstr*key(8) + value(696) + cached_hash@+712
- Key: ttstr built by `Player_buildNodePathKey` @ 0x6B5C1C — walks node parent chain via `node+36 = parent_index`, joins names with `/`. Example: `"root/body/face/eye_l"`
- Value: massive 696B per-node motion-instance cache initialized by `Player_HM3_initValueFromNode` @ 0x699510 — copies mesh data (node+1912/+2296), source PSB resource refs (node+356, kept refcounted at value+88), per-node TJS callbacks (value+544 vtable, value+672 array), mesh corners, colors, blend params. Only created for nodes with `node+46!=0` and `node.type ∈ {0=obj,2=layout,3=motion,4=particle,7,8}` (mask 0x19D).
- Erase trigger: every frame, `Player_pruneHM3_byNodeIdentity` walks the node deque; if the node at the same path no longer has matching identity (`entry+16 != node+28`), removes that HM3 entry.
- **Used by**: per-layer rendering / animation working set — the "live" per-node state surviving across frames

## HM4 @ Player+1240 — variable label name → label-ttstr pointer (initial alias map)
- Shares insert helper with HM2: `Player_HM2_upsert_labelToValue` @ 0x686944 (caller: `Player_resetMotionState_clearAndRebuild` @ 0x6B2B7C)
- Cleared by: `Player_clearHM3_HM4` @ 0x6B80E4 (clears both HM3 and HM4 together on motion change)
- Entry destroy: anonymous inline in `Player_dtor` (same 32B node as HM2)
- Node: 32 bytes. Layout: next(8) + ttstr*key(8) + qword_value(8) + cached_hash(8)
- Key: ttstr — the `name` field (offset 0) of each `VariableLabelEntry` from the std::vector at Player+1296 (built by `Player_initVariables` @ 0x6CD750 from PSB `variable[].scope` split on `::`)
- Value: `tTJSVariant*` pointer read from `VariableLabelEntry+16` (the `label` slot pointer)
- **Used by**: variable-name reverse lookup — given a scope-derived name, find the canonical label pointer. Built from +1296 vector during motion init.

## Note: Player_setVariable hash tables at +1384 and +1440 do NOT belong to 1384B Player
`Player_setVariable` @ 0x671228 reads `sub_6887F4(a1+1384, ...)` and `sub_686944(a1+1440, ...)`. The 1384B Player ctor does NOT initialize these. These belong to **EmotePlayer/EmoteEngine (1496B)** extension. Same for the 5 deques at a1+256/+336/+416/+576/+656 indexed by type 4..8 — those are EmoteEngine animator pools, not Player.

## Critical mapping result for local PlayerRuntime
Local `cpp/plugins/motionplayer/Player.h` PlayerRuntime has 6 ttstr/string-keyed unordered containers that fight with these 4 binary tables:

| Binary table | Local field (best alignment) |
| ------------ | ---------------------------- |
| HM1 (+264, "::"-key eval cascade)            | `_evalResultList` + `_evalResultListIndex` (currently `std::list+unordered_map`, structurally close but key form is raw label not "::"-prefixed) |
| HM2 (+320, raw label → double)               | `PlayerRuntime::_evalResultValues` (`unordered_map<string, double>`) — closest 1:1 match |
| HM3 (+1184, node-path → 696B layer state)    | **NO LOCAL EQUIVALENT** — local code keeps per-layer state in PreparedRenderItem + LayerRenderState by layerId, not by node path. This is the biggest divergence. |
| HM4 (+1240, name → label-ttstr alias)        | **NO LOCAL EQUIVALENT** — local `variableLabelEntries` vector lookups are O(N), no alias map |

## Local-only maps (no binary HM counterpart; either belong to EmoteEngine or are web adapter overhead)
- `Player._variableValues` (string→double) — likely should be merged into HM2 alignment (`evalResultValues`)
- `Player._variableAnimators` (string→VariableAnimatorState) — belongs on EmoteEngine (+1384 hash + deques)
- `Player._type4..8ControllerAnimators` (5x string→VariableAnimatorState) — these are EmoteEngine's deque<Animator> pools at +256/+336/+416/+576/+656; should NOT be unordered_map AND should NOT be on Player. **P0 to move out**.
- `Player._mirrorPositiveCache` / `_mirrorNegativeCache` (unordered_set) — web-side memoization, not in binary
- `PlayerRuntime::motionsByKey`, `timelines`, `layerIdsByName`, `layerNamesById`, `renderLayerStates`, `disabledSelectorTargets`, `parameterEntryById`, `nodeLabelMap`, `clipIndexByLabel`, `timelineControlByLabel`, `loopTimelines`, `timelineLoopTimes`, `timelineTotalFrames`, `variableRanges`, `variableFrames`, `controllerBindings`, `selectorControls`, `instantVariableLabels`, `renderItemNativeFieldLifetimeByNode` — these support Web port abstractions and don't map to Player's 4 HMs. Most live conceptually on the MotionSnapshot (PSB-derived), not on Player.
