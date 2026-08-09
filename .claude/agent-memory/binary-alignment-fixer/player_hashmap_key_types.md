---
name: player-hashmap-key-types
description: motion::Player 4 HMs + node-index map are ALL ttstr(UTF-16)-keyed; hash @0x686944, std::map comparator @0x9B1ED0
metadata:
  type: project
---

motion::Player's container key types are ALL ttstr (UTF-16), never std::string.

**Why:** Binary evidence — every key read goes through ttstr_c_str() and the
custom UTF-16 hash/comparator. std::string (UTF-8) keys reorder non-ASCII and
change bucket distribution.

**How to apply:** when retyping/aligning a Player map, the local std::string
key is the deviation. Convert with detail::widen/narrow at boundaries; prefer
using the original ttstr arg directly when one is already in scope.

- HM1 Player+264 EvalCascadeMap, HM2 Player+320 (_evalResultValues,
  detail::LabelValueMap), HM3 Player+1184, HM4 Player+1240 — all
  unordered_map<ttstr,V,ttstr_hash,ttstr_equal>. Shared upsert
  Player_HM2_upsert_labelToValue @0x686944 (also serves HM4 & EmoteEngine+1440).
  Hash: acc=0; per code unit c: mixed=acc+c; acc=(1025*mixed)^((1025*mixed)>>6);
  then 9*acc; then 32769*(h^(h>>11)); zero→(uint32_t)-1. = detail::ttstr_hash
  in internal/ttstr_hash.h.

  **2026-07-26 superseding correction:** 上述旧记录只覆盖非 null key 的算术 mix，错误地把它外推为 `ttstr_hash` 完整对齐。fresh 证据证明：`ttstr::Ptr == nullptr` 时 hash 为 `0`；Ptr 非 null 时先复用 `Hint@+68`，Hint 为 `0` 才计算并写回；仅非 null 计算结果为 `0` 时改为 `0xFFFFFFFF`。当前 `internal/ttstr_hash.h` 已按此修复，旧“算术 helper 等于完整 functor”结论不得继续使用。

- Player+24 node-index map (_nodeLabelMap, detail::NodeLabelMap) =
  std::map<ttstr,int,ttstr_utf16_less>. Comparator sub_9B1ED0 @0x9B1ED0:
  UTF-16 code-unit lexicographic, `while(b[i]!=0 && diff==0) advance`,
  returns sign(a[i]-b[i]); std::map less = (compare<0). Keyed by RAW PSB
  "label" (insert @0x6B4CB0, buildNodeTree_recursive @0x6B4A6C), NOT a path.
  ttstr_utf16_less added to internal/ttstr_hash.h.
- _evalResultList / _evalResultListIndex are a PORT-ONLY insertion-order mirror
  (std::string-keyed) sitting alongside HM2 — NOT a binary container, left
  std::string-keyed (out of scope of HM2 retype).
- Call-site files touched when retyping these: PlayerVariable.cpp,
  PlayerCore.cpp, PlayerFrameProgress.cpp, PlayerUpdateLayers/LayerEval.cpp,
  PlayerLayerQuery.cpp, PlayerResource.cpp, NodeTree.cpp,
  PlayerUpdateLayersInternal.h (findNodeByLabel helper).
- NodeTree.cpp is inside `namespace motion::detail` → use UNQUALIFIED widen(),
  not detail::widen() (the latter resolves to motion::detail::detail::widen).
  Other Player*.cpp are in `namespace motion` → detail::widen() is correct.
