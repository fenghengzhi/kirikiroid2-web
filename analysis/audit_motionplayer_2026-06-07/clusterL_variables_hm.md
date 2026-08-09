# Cluster L — Player variable system + 4 inline-HM cascade — Alignment Audit

Date: 2026-06-07. Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64). Read-only on cpp/; IDB updated (2 comments at 0x6B8118/0x6CD16C, idb_save'd). Every binary claim below is backed by a decompile call made THIS session.

Scope: PlayerVariable.cpp (819 lines) + internal/value_structs.h, player_containers.h, ttstr_hash.h, Player.h variable fields.

## Verdict: ✅ ALIGNED (cascade topology + containers faithful) — 1 inert architectural gap (L-1, +1064 redirect layer), 2 legitimate DEFERRED/platform boundaries.

The 2026-06-02 brick chain (1→6.5) and 2026-06-03 setVariable rework already brought this cluster into 1:1 alignment. This audit re-verified every load-bearing binary fact from fresh decompiles and found NO new local deviation. Critically, it OVERTURNS the stale clusterJ J-1 ("HM4 value = owning tTJSVariant*") — the binary releases the KEY, not the value; the port's `unordered_map<ttstr,double>` is correct.

## Decompiled this session (all addresses verified)
| addr | func | role |
|---|---|---|
| 0x533E1C | getVariable_wrapper | entry router: scope-gate (sub_6CD16C on *(a1+1064)) → HM1-join vs HM4-first |
| 0x6CD16C | isLabelInBindScopeList | scans var-track deque _M_start @+1312 (obj @+1296); item+0 cascadeKey ptr-eq OR wcscmp |
| 0x6CD23C | evalKey_cascade | HM4@+1240 find by RAW key → *(node+16) double; miss → HM1 cascade |
| 0x6CD39C | HM1_cascadeJoinAndLookup | sub_6D0BF4 split OK → join "scope::label" → HM1@+264 *(node+48); else → HM2@+320 *(node+16) |
| 0x6D0BF4 | split helper | find first "::" (else '/') → a1=scope, a2=suffix; ret 1 on success |
| 0x6CD750 | initVariables | var-track deque @+1296 CTOR: TJS-dispatch PropGet(*(a1+528),"variable") → per-item PropGet("label"/"scope") → push 160B item |
| 0x6B80E4 | clearHM3_HM4 | HM4 list@+1256 releases node[1]=KEY ttstr; memset value buckets @+1240; HM3 @+1184 |
| 0x6B2D3C | resetMotionState | loop2 = HM4 WRITE: item+16 double → upsert(+1240) node+16; loop3 = HM3 write |
| 0x6C4668 | bindParameterValue | HM1-cascade (gated by split) + HM2[rawKey] unconditional + 3 ramp loops |
| 0x6B1ECC | finalizeParameterTable | +384 entry vector (56B stride) → +408 multimap keyed by entry+32=id, value=&entry |
| 0x6B9650 | rebuildEvalCascadeHeapResult | gate weight!=0; clear; ancestor label-chain == chainSegments → push node |

## Cascade pseudocode (verified)
```
getVariable_wrapper(player, key):                                  // 0x533E1C
  P = *(player+1064)                  // embedded/childRoot Player* — REDIRECT LAYER
  if isLabelInBindScopeList(P, key):  // 0x6CD16C scan deque @P+1312
      return HM1_cascadeJoinAndLookup(P, key)     // 0x6CD39C
  else:
      return evalKey_cascade(P, key)              // 0x6CD23C (HM4-first)

evalKey_cascade(P,key):                                            // 0x6CD23C
  node = HM2_find(P+1240, hash(key)%*(P+1248), key)  // HM4 reuses HM2 node shape
  if node && *node: return *(double*)(*node+16)      // RAW double
  else: return HM1_cascadeJoinAndLookup(P, key)

HM1_cascadeJoinAndLookup(P,key):                                   // 0x6CD39C
  if sub_6D0BF4(&scope,&suffix,key):  // split "::"/"/"
      joined = (scope?scope:"::") # suffix          // sub_A1359C concat
      node = HM1_find(P+264, hash(joined)%*(P+272), joined)
      return node ? *(double*)(node+48) : 0          // HM1 value @node+48
  else:
      node = HM2_find(P+320, hash(key)%*(P+328), key)
      return node&&*node ? *(double*)(*node+16) : 0   // HM2 fallback @node+16
```

## Item-by-item

| # | Dim | Binary | Local (PlayerVariable.cpp) | Status |
|---|---|---|---|---|
| 1 | container HM1 | std::unordered_map<ttstr,V96B>@+264, ttstr_hash | `_evalCascadeMap` unordered_map<ttstr,EvalCascadeState,ttstr_hash> | ✅ |
| 2 | container HM2 | unordered_map<ttstr,double>@+320 | `_evalResultValues` LabelValueMap (ttstr key — std::string corrected) | ✅ |
| 3 | container HM4 | unordered_map<ttstr,**double**>@+1240 (node16=RAW double) | `_variableSnapshotMap` VariableSnapshotMap unordered_map<ttstr,double> | ✅ (J-1 overturned) |
| 4 | container +1296 | std::deque<160B item> | `_variableLabelScopes` deque<VariableLabelScope> | ✅ |
| 5 | container +408 | std::multimap<ttstr id, Entry*> | `_parameterRampMap` multimap<ttstr,MotionParameterEntry*> | ✅ |
| 6 | key type | ALL 4 HM keys = ttstr UTF-16LE；null `Ptr`→0，非零 Hint 复用，否则计算/写 Hint，非 null 计算结果 0→`0xFFFFFFFF` | 算术 mix 原本正确；旧 functor 的 null/Hint 缺口已于 2026-07-26 修复 | ✅ 当前对齐（旧 byte-identical 结论已 superseded） |
| 7 | HM4 ownership | clearHM3_HM4 releases node[1]=KEY ttstr; value@+16 = bare double (memset only) | unordered_map<ttstr,double> — key owned by map, double trivial | ✅ |
| 8 | getVariable router | 2-branch: inScope→HM1-join ; else HM4-first→HM1-join | getVariable() lines 645-690: isLabelInBindScope gate → HM4 lookup → HM1-join/HM2 | ✅ |
| 9 | scope-gate scan | deque _M_start @+1312 (obj @+1296); item+0 cascadeKey ptr-eq OR wcscmp | isLabelInBindScopeListLike_0x6CD16C scans _variableLabelScopes, item.cascadeKey==key | ✅ |
| 10 | split helper | first "::" else '/'; normalises '/'→"::" in join | getVariable / bindParameterValue both find "::" then '/'; join uses "::" | ✅ |
| 11 | initVariables source | TJS dispatch: PropGet(*(a1+528),"variable") list → per-item PropGet("label"/"scope") | (port build path in PlayerMotionLoad, brick 1) — cascadeKey = scope?scope+"::"+label:label | ✅ (data source = dispatch, NOT PSB struct) |
| 12 | var-track item | 160B: +0 cascadeKey ttstr, +8 cursor, +16 value double, +24 labelName, +48/+104 slot[2] 56B, gate @slot+20 | VariableLabelScope {cascadeKey, cursor, value, labelName, VarTrackSlot slot[2]} | ✅ |
| 13 | HM4 write (loop2) | reset 0x6B2D40: !slot[cursor].gate → node+16 = item+16 double | resetMotionStateLike loop2 (brick 3): _variableSnapshotMap[cascadeKey]=item.value | ✅ |
| 14 | bind HM1/HM2 | 0x6C4668: split→HM1[joined].writeVal=a4(weight 1.0 seed); HM2[raw]=a4 always | bindParameterValueLike_0x6C4668 (both overloads): HM1 gated + HM2 unconditional | ✅ |
| 15 | bind ramps | HM1-block heapResult→child+408 by SUFFIX; HM2-tail own+408 by RAW label | applyParameterRampsLike_0x6C4C0C: heapResult loop uses suffixKey, tail uses widen(label) | ✅ |
| 16 | ramp math | entry+40=mode=a3; entry+48=rangeScale*(clamp(rawV)-rangeBegin)/(rangeEnd-rangeBegin) | normalizeParameterValueLike_0x6B1718 (value-rangeBegin)*rangeScale, rangeScale=division/range | ✅ equivalent (intermediate pre-divide, same result) |
| 17 | heapResult rebuild | 0x6B9650: weight!=0 gate; clear; (nodeType-3)>1 skip; ancestor label-chain==chainSegments | rebuildEvalCascadeHeapResultLike_0x6B9650 line-for-line | ✅ |
| 18 | param table | 0x6B1ECC: 56B-stride entry vec → +408 multimap, key entry+32=id, dup kept | finalizeParameterTableLike_0x6B1ECC emplace(widen(id),&entry) | ✅ |

## Deviations

### L-1 (inert architectural gap) — +1064 redirect layer MISSING
Binary getVariable_wrapper @0x533E1C runs the entire cascade on `*(a1+1064)` (an embedded/childRoot `Player*`), not on the dispatched-to Player itself. The port's `getVariable()` runs the cascade directly on `this`. For a standalone Player, +1064 typically points back to self or the childRoot, so values flow through the same HM set and the read result is identical. The port has NO +1064 field (no childRoot model). This is a genuine indirection-layer omission, but currently **inert**: there is no port construction that makes +1064 differ from `this`, and all available content is variable-free (cascade map empty → fall-through). Resolving it requires modeling the +1064 childRoot Player pointer — out of cluster-L container scope, blocked on the same childRoot architecture that gates other Player-redirect sites. NOT patchable in PlayerVariable.cpp alone; flagged for the Player-layout / childRoot re-arch.

### Stale-memory CORRECTION (this session)
- clusterJ J-1 "HM4 value type = OWNING tTJSVariant*" is **FALSIFIED**. clearHM3_HM4 @0x6B8118 releases `node[1]` = node+8 = the KEY ttstr (AddRef'd at insert), NOT node+16. The value @node+16 is a bare double written as double-bits by resetMotionState loop2 (0x6B2D40) and read as double by evalKey_cascade (0x6CD304); clear only memsets the bucket array. Port `unordered_map<ttstr,double>` is correct. IDB comment added at 0x6B8118.
- clusterJ "deque @+1312" vs initVariables "@+1296": SAME container. +1296 is the std::deque object; +1312 is its `_M_start` iterator (cur@+1312/first@+1320/last@+1328/node@+1336, finish@+1344). isLabelInBindScopeList walks _M_start; initVariables ctors @+1296 and pushes via finish@+1344. No deviation. IDB comment added at 0x6CD16C.
- **2026-07-26 hash correction (supersedes this report's “byte-identical” claim):** fresh decompiles at `0x6F52AC`, `0x686944`, `0x6F2674`, `0x689760`, `0x6885CC`, and `0x6E2060/0x6E2150/0x6E2484/0x6E2574` prove that the arithmetic mix was correct but the old local functor was incomplete: it collapsed null `Ptr` into the empty payload and never reused/wrote `Hint@+68`. The implementation now returns 0 for null, reuses a nonzero Hint, writes a newly computed Hint, and maps a zero result for non-null input to `0xFFFFFFFF`.

## DEFERRED (legitimate, evidence-backed, not deviations)
- **sub_697D34 chainDispatches build** (bind 0x6C48BC): pure TJS-dispatch scope resolution into HM1 node+8..24 (vector<tTJSVariant*>). Port has NO getVariable consumer of chainDispatches (the cascade reads writeVal@node+48 / HM2, never chainDispatches; sub_6B9650 reads chainSegments STRING values only, which the port models as vector<ttstr>). DEFERRED with no observable effect.
- **HM3 populate** (resetMotionState loop3): HM3 (`_perNodeLayerStateMap`) has NO reader anywhere (declare+clear only) — dead-data. brick 6.5 ported 20/24 snapshot fields; 4 remain DEFERRED (no port source). Not a cluster-L variable-read concern.
- **stream③ interpolation → item+16**: all 36 .mtn + e-mote .psb fixtures are variable-free (motionsim --dump-variable confirmed), so the entire var-track→HM4→getVariable path is inert-by-data on existing content. No fixture exists to exercise it; per CLAUDE.md no fixture is fabricated. Faithful copy is in place; verification gap noted.

## Platform boundaries (skipped, listed)
- value_structs.h:7-9 / player_containers.h:8 / VariableLabelScope sizeof note: ttstr is 8B on Web (libc++) vs 16B on Android NDK, so sizeof(EvalCascadeState)=72B / sizeof(VariableLabelScope)=160B byte layouts cannot match. Legitimate ABI boundary (reason stated). Container SELECTION (unordered_map/deque/multimap + ttstr key + ttstr_hash) IS aligned — this is the dimension that matters.

## Subfunction alignment status (tree)
- ✅ sub_6D0BF4 (split) — getVariable + both bind overloads replicate first-"::"-else-'/' faithfully
- ✅ sub_6CD16C (scope-gate) — isLabelInBindScopeListLike_0x6CD16C
- ✅ sub_6CD23C (evalKey_cascade HM4-first) — getVariable HM4 lookup
- ✅ sub_6CD39C (HM1-join/HM2-fallback) — getVariable join block
- ✅ sub_6B9650 (heapResult rebuild) — rebuildEvalCascadeHeapResultLike_0x6B9650 (incl. STL deque-size +1 cancel)
- ✅ sub_6B1ECC (param multimap build) — finalizeParameterTableLike_0x6B1ECC
- ✅ sub_6B2D3C loop2 (HM4 write) — resetMotionStateLike (brick 3)
- ✅ sub_6B80E4 (clearHM3_HM4) — VariableSnapshotMap/PerNodeLayerStateMap .clear()
- ❓ sub_6CD750 initVariables — ctor lives in PlayerMotionLoad (brick 1), not re-audited line-by-line here; cascadeKey form (scope+"::"+label) confirmed via decompile, matches
- ⏸ sub_697D34 (chainDispatches) — DEFERRED, no consumer
- ⏸ Player_HM3_initValueFromNode 0x699510 — DEFERRED, HM3 dead-data

## IDB changes (idb_save'd)
- Comment @0x6B8118: HM4 clear releases KEY not value; corrects J-1.
- Comment @0x6CD16C: deque @+1296 obj / @+1312 _M_start, same container as bind.
