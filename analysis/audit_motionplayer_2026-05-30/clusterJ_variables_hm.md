# Cluster J — Player variable system + 4 inline HM containers — Alignment Audit

Date: 2026-05-30. Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64). Read-only on cpp/; IDB updated (renames + comments, idb_save'd).

## Verdict: 🔧 NEEDS RE-ARCH (container/value-type) + ⚠️ DATAFLOW gaps

The 4-HM byte layout is now fully reverse-engineered (all node sizes + hash offsets + value reads confirmed by decompile). Two of the local container aliases have the WRONG value type, and the cascade get/set dataflow is largely unimplemented locally (P0/P1).

## HM 4-map mapping table (decompile-confirmed)

| Binary offset | node size | hash@ | key | value (confirmed) | local field | status |
|---|---|---|---|---|---|---|
| HM1 +264 (hdr@+264, bucketcount@+272) | 0x60=96B | node+88 | ttstr@+8 | mainDispatch@+16(owning) + chainDispatches vector@+24..40(owning tTJSVariant*) + weight@+40=1.0 + writeVal double@+32 + result@+48(read in cascade) | `_evalCascadeMap` EvalCascadeMap | ⚠️ shape OK, STL boundary |
| HM2 +320 (hdr@+320, bucketcount@+328) | 0x20=32B | node+24 | ttstr@+8 | **double@+16** | `_evalResultValues` std::unordered_map<std::string,double> | ❌ P1-2 wrong key (std::string not ttstr) |
| HM3 +1184 (hdr@+1184, bucketcount@+1192) | 0x2D0=720B | node+712 | ttstr@+8 (node-path key) | PerNodeLayerState@+16.. (8 ttstr/5 dispatch/2 heap) | `_perNodeLayerStateMap` PerNodeLayerStateMap | ⚠️ shape OK, STL boundary |
| HM4 +1240 (hdr@+1240, bucketcount@+1248) | 0x20=32B | node+24 | ttstr@+8 | **tTJSVariant*@+16 (OWNING)** | `_dispatchAliasMap` ttstr→iTJSDispatch2* (non-owning) | ❌ P0 WRONG value type + WRONG ownership + UNUSED |

HM1/HM2/HM3/HM4 all share the KiriKiri UTF-16 ttstr hash (1025*acc XOR>>6 … 9* … 32769* … -1 sentinel), confirmed byte-identical inline in setVariable/upsert/cascade. Local ttstr_hash.h reproduces it exactly. ✅

## Decompiled functions / pseudocode

### Player_getVariable_wrapper @0x533e1c (the real Player::getVariable)
```
v5=*(player+1064)  // <-- NOTE: dispatches to *(a1+1064), a SEPARATE Player* (childRoot/self ptr)
if (isLabelInBindScopeList(v5,key))            // sub_6CD16C scans deque @*(v5+1312), 160B/40q stride
    return HM1_cascadeJoinAndLookup(v5,key)     // 0x6cd39c
else
    return evalKey_cascade(v5,key)              // 0x6cd23c  (HM4 first)
```
NOTE: a1+1064 is dereferenced as the Player whose +1240/+264/+320 HMs are queried. (offset +1064 = childRoot/owner Player ptr — same +1064 used as Player* in Player_setVariable EmotePlayer path.)

### Player_evalKey_cascade_HM4_HM2_guess @0x6cd23c  (HM4 lookup, HM1 fallback)
```
h = ttstr_hash(key)
node = HM2_find_node(player+1240, h % *(player+1248), key)   // SAME find shape as HM2 (node+16=value)
if (node && *node) return *(double*)(*node+16)               // HM4 hit -> double
else return HM1_cascadeJoinAndLookup(player, key)            // 0x6cd39c
```
=> HM4 value is read as a double-bits at node+16. clearHM3_HM4 releases node[1] as tTJSVariant* => OWNING tTJSVariant whose .Real is the double.

### Player_HM1_cascadeJoinAndLookup_guess @0x6cd39c  (HM1 scope-join, HM2 fallback)
```
if (sub_6D0BF4(&full,&suffix,key)) {            // split "scope::label"
    pfx = full ? c_str(full)#"::" : "::"        // sub_A1359C = ttstr concat
    joined = pfx # suffix  (or pfx / suffix)
    node = HM1_find_node(player+264, hash(joined)%*(player+272), joined)
    if (node) result = *(double*)(*node+48)     // HM1 value@node+48
    else result = 0
} else {
    node = HM2_find_node(player+320, hash(key)%*(player+328), key)
    result = node&&*node ? *(double*)(*node+16) : 0   // HM2 fallback
}
```
=> HM1 result slot is node+48. HM1 also stores mainDispatch@+16 + chainDispatches@+24..40 + writeVal@+32 + weight@+40.

### Player_bindParameterValue_writesHM1_HM2 @0x6c4668 (the bind-to-HM writer; "0x6C4668")
```
split(key)->full,suffix; joined = "::"#suffix-or-full        // same scope-join as cascade
node1 = HM1_find_node(player+264, hash(joined), joined)
if (!node1) {                                                // first bind for this label
    node1 = HM1_upsert_evalCascade(player+264, joined)        // new 0x60 node
    node1.mainDispatch@+16 = joined (AddRef'd)
    sub_697D34(&chainVec, joined, "<wide const @0x15218E8+2>")// builds chainDispatches
    move chainVec -> node1+8..24 ; release old ; weight@+40 = 1.0
}
node1.writeVal@+32 = a4 (value)
sub_6B9650(player, node1)                                     // propagate into node tree
for each child node (type 3 / type 4 particles): walk node+408 controller list,
    set ctrl+48 = mode(a3); ctrl+40 = normalizedRamp(value vs ctrl range@+16/+24, scale@+32, discretize@+8)
HM2_upsert_labelToValue(player+320, key).double = a4          // <-- ALWAYS writes HM2[key]=value
walk player+408 controller list (same ramp write)
```
=> bind writes BOTH HM1 (cascade node, keyed by JOINED scope path) AND HM2 (keyed by RAW key). Confirms HM2=raw double map, HM1=cascade-join map.

### Player_setVariable @0x671228 — **NOT motion::Player**
IDB NOTE confirms: this is EmotePlayer::setVariable (a1 ~1576B). HMs accessed are EmotePlayer +1384 (find sub_6887F4) / +1440 (HM2_upsert). cases 4-8 read EmotePlayer deques @+256/+336/+416/+576/+656. Out of Player scope; local Player::setVariable maps to a different path (the bind/eval write path, ~0x6c4668 family), not 0x671228.

### Player_getVariableKeys @0x6d9470
Returns AddRef'd object cached at *(player+960). Not an HM; separate keys cache. MISSING locally (no +960 field modeled).

### HM helper shapes (all confirmed)
- find_node (HM1 0x6f51bc / HM2 0x686b6c / HM3 0x6f28a4 / HM4 0x6887f4): identical single-chain prime-bucket walk; only the hash field offset differs (88/24/712/24). ttstr compare = ptr-eq OR (len@+60 eq AND sub_9B1ED0 strcmp).
- insert_node (HM1 0x6f53c8 stores hash@a4[11]=+88; HM2 0x686a4c hash@a4[3]=+24; HM3 0x6f2790 hash@a4[89]=+712): libstdc++ _Prime_rehash_policy::_M_need_rehash + before-begin singly-linked insert. ✅
- upsert (HM1 0x6f52ac new(0x60); HM2 0x686944=ttstr_doubleMap_upsert new(0x20); HM3 0x6f2674 new(0x2D0)): hash→find→on-miss new node, key AddRef'd, value zero-init (memset), insert. Returns value ptr (node+16 for HM2/0x20, node+2*8=+16 then caller uses, HM1 returns node+2 i.e. +16 area but value read at +48/+32). ✅
- HM1_value_destroy 0x6dd1a0: delete writeVal-area@+56; release chainVec@+16..24 each tTJSVariant; delete vec; release dispatch@+8; release@+0. (8 owning slots)
- HM3_value_destroy 0x6dd06c / entry_destroy 0x6dd018: many ttstr (sub_A0F778) + tTJSVariant_Release + heap delete, descending offset order.
- clear: HM1 0x6cf930, HM3 0x6cf7c4, clearHM3_HM4 0x6b80e4.

## Findings

| # | Sev | Item | Binary | Local | Fix |
|---|---|---|---|---|---|
| J-1 | P0 | HM4 value type | +1240 OWNING `tTJSVariant*` (clearHM3_HM4 releases node[1]); cascade reads node+16 as double | `_dispatchAliasMap` ttstr→iTJSDispatch2* NON-owning, never written | Retype HM4 to ttstr→tTJSVariant* (owning) OR ttstr→double-backed-variant; wire into getVariable cascade. DispatchAliasMap is INVENTED (no binary backing). |
| J-2 | P0 | getVariable cascade unimplemented | scope-list gate → HM1 join-cascade vs HM4→HM1 fallback (0x533e1c/0x6cd23c/0x6cd39c) | `Player::getVariable` reads `_evalResultValues` then activeMotion frames/ranges — NO HM1/HM4 cascade, NO scope-join, NO bind-scope-list gate | Re-arch getVariable to the 2-branch cascade; needs scope-list deque @+1312 + HM1 join-key build (sub_A1359C concat). |
| J-3 | P1-2 | HM2 key type | ttstr key @node+8, ttstr_hash | `_evalResultValues` std::unordered_map<**std::string**,double> | Retype to detail::LabelValueMap (ttstr key). TODO(A8) already noted. |
| J-4 | P1-3 RESOLVED | 6→4 map mapping | 4 inline HM = +264/+320/+1184/+1240 | _evalCascadeMap=HM1, _evalResultValues=HM2, _perNodeLayerStateMap=HM3, _dispatchAliasMap=HM4(wrong) | The other local maps (_motionsByKey/_timelines/_layerIdsByName/_layerNamesById/_renderLayerStates/_disabledSelectorTargets) are **Web-port INVENTIONS** — none correspond to a binary HM. See J-5. |
| J-5 | P2 | Invented maps | binary has NO motionsByKey/timelines/layerId/selector HMs at these offsets | 6 STL maps in Player.h:708-749 | Confirm they are platform extensions; annotate. NOT part of the 4-HM contract. |
| J-6 | P1 | HM1 bind writer dataflow | 0x6c4668 writes HM1(join key) AND HM2(raw key), builds chainDispatches via sub_697D34, ramps node+408 controller lists | local `writeEvalResultValueLike_0x6C4668` writes ensureEvalResultSlot(list) + _evalResultValues[label] + bindParameterValue; NO HM1 cascade node, NO chainDispatches build, NO node+408 controller ramp from this path | Re-arch bind path to populate HM1 cascade node + dual HM1/HM2 write. |
| J-7 | P2 | getVariableKeys +960 cache | 0x6d9470 returns *(player+960) AddRef'd | no +960 field | Model +960 keys cache (low priority). |
| J-8 | P1 | _evalResultList / _evalResultListIndex | binary has NO such list+index; HM2 itself IS the label→value store; insertion order = HM2 node chain | local maintains std::list<EvalResultEntry> + index map alongside _evalResultValues | The list duplicates HM2; binary keeps a single chained HM2. Collapse into one ttstr-keyed inline-order map. |

## MISSING locally (binary has, port lacks)
- getVariable cascade evaluator (HM1 scope-join + HM4→HM1 fallback) — J-2.
- HM1 cascade node population (mainDispatch + chainDispatches + weight) on bind — J-6.
- bind-scope-list deque @player+1312 (160B stride) used to gate which branch — J-2.
- player+960 keys cache for getVariableKeys — J-7.
- HM4 as owning tTJSVariant* variable map — J-1.

## P1-2 / P1-3 resolution
- **P1-2 CONFIRMED**: HM2 @+320 is ttstr-keyed (node+8), node+24 hash, node+16 double value. Local _evalResultValues uses std::string key — wrong bucket distribution/order. Retype to LabelValueMap.
- **P1-3 RESOLVED**: The 4 binary inline HMs are exactly _evalCascadeMap(HM1+264), _evalResultValues(HM2+320), _perNodeLayerStateMap(HM3+1184), _dispatchAliasMap(HM4+1240). HM4's value type is mismodeled (owning tTJSVariant*, not non-owning dispatch). The 6 other local unordered_maps (motionsByKey/timelines/layerIdsByName/layerNamesById/renderLayerStates/disabledSelectorTargets) are **Web-port inventions with no binary HM backing** — they are NOT among the inline 4 and do not occupy +264/+320/+1184/+1240.

## IDB changes (idb_save'd)
- Renamed: 0x6f2674→Player_HM3_upsert_perNodeLayerState, 0x6dd06c→Player_HM3_value_destroy, 0x6dd018→Player_HM3_entry_destroy, 0x6cf7c4→Player_HM3_clear, 0x6cf930→Player_HM1_clear, 0x6dd1a0→Player_HM1_value_destroy, 0x6cd16c→Player_isLabelInBindScopeList_guess.
- Comments added at 0x6cd23c, 0x6b80e4, 0x6d9470, 0x533e1c documenting HM4 owning-tTJSVariant* + cascade routing.
