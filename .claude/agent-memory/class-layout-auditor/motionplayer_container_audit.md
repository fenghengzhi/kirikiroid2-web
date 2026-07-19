---
name: motionplayer-container-audit
description: Fresh-decompile verdict on all motionplayer内部容器选型 (Player 1384B + EmoteEngine 1496B). 二进制全是 libstdc++ unordered_map/deque/_Rb_tree (非 KiriKiri 自研内联). 容器选型对齐表 + 唯一真实偏差.
metadata:
  type: project
---

Fresh-decompile (2026-06-03) of Player_ctor 0x6CED30 / Player_dtor 0x6CFADC /
HM2_upsert 0x686944 / HM1_upsert 0x6F52AC / EmoteEngine_ctor 0x67E38C /
EmoteEngine_dtor 0x67F4B8 / initVariables 0x6CD750 / buildNodeTree_recursive 0x6B4CB0.

**核心定性纠正:** Player/EmoteEngine 的 4+7 个哈希表**全是 libstdc++ `std::unordered_map`**
(每个 ctor 调 `std_Prime_rehash_policy_M_next_bkt(base, 10)` + load factor 1.0f @base+32 dword
= 1065353216 + 56B 控制块 {_M_buckets@0 _M_bucket_count@8 _M_before_begin@16 _M_element_count@24
_M_max_load_factor@32 _M_next_resize@40 _M_single_bucket@48})。**不是** KiriKiri 自研内联 hashmap。
"KiriKiri 哈希"指的只是 **hash 函数** (1025/6/9/32769/11 mix on ttstr UTF-16 = `ttstr_hash`),
容器本体是标准 libstdc++ unordered_map。⇒ 本地用 `std::unordered_map<ttstr, V, ttstr_hash, ttstr_equal>`
是**正确的 1:1 选型** (容器实现一致 + hash 一致),不是"STL 简化替代"。**`player_container_layout.md`
里"4 个 KiriKiri 哈希表(非vector)"措辞会误导成自研内联表,实为 std::unordered_map。**

**Player 1384B 容器选型对齐表 (二进制 → 本地):**
| 容器 | 二进制 | node/elem | 本地字段 | 选型对齐 |
|---|---|---|---|---|
| HM1 @+264 | unordered_map<ttstr,EvalCascade> | new(0x60)=96B, val@+48 | _evalCascadeMap (EvalCascadeMap) | ✅ |
| HM2 @+320 | unordered_map<ttstr,double> | new(0x20)=32B, val@+16 | _evalResultValues **unordered_map<std::string,double>** | 🟡 key=std::string≠ttstr |
| HM3 @+1184 | unordered_map<ttstr,PerNodeLayerState> | new(0x2D0)=720B | _perNodeLayerStateMap | ✅ (空,DEAD: 无 reader) |
| HM4 @+1240 | unordered_map<ttstr,double> 共享 HM2 helper | 32B | _variableSnapshotMap (ttstr) | ✅ |
| +24 node map | std::map<ttstr,int> (_Rb_tree lowerBoundInsert; val=DWORD deque idx) | — | _nodeLabelMap std::map<std::string,int> | 🟡 key=std::string≠ttstr |
| +1296 var-track | std::deque<VariableLabelScope> 160B/elem (new 0x1E0=3×160) | 160B | _variableLabelScopes (deque 160B) | ✅ |
| +184 nodes | std::deque<MotionNode> 2632B/elem | 2632B | _nodes (deque MotionNode) | ✅ |
| +384 render list | **连续数组** 56B/elem, dtor `v4+=7` 释放*v4 dispatch | 56B | (port: PreparedRenderItem vector) | ✅ vector OK (非 map) |
| +408 list | _Rb_tree dispatch-value (stdmap_recursive_destroy_dispatchValue) | — | port 无直接镜像 | n/a |

**EmoteEngine 1496B 容器: 全对齐 ✅** — 10 deque (ctor 10 distinct helper, 80B header memset 0x50;
末两个 @+80/+160 共享 sub_6848C8 dtor) + 7 unordered containers
(@824/880/936/1272/1328/1384/1440；其中 HM1/HM2/HM4 是 set，其余是 map)
+ 4 vector<ttstr> (@800/992/1016/1040, [begin,end) release string elements + delete buffer).
Player* @+1064 raw new(0x568)+manual delete ✅; 7 controllers @1072-1120 raw new+sub_683AA8+delete ✅.
HM7 @+1440 = unordered_map<ttstr,double> 已 VERIFIED (旧结论正确).

**容器偏差: 全部 CLOSED (2026-06-05 fresh 复核)。** 此前 2 处 std::string→ttstr key retype 均已完成:
1. `_evalResultValues` (Player.h:1446) = `detail::LabelValueMap`
   = `unordered_map<ttstr,double,ttstr_hash,ttstr_equal>` ✅ (注释明确 "Retyped 2026-06-03 from
   std::unordered_map<std::string,double>")。bucket 分布/迭代序现与二进制一致。
2. `_nodeLabelMap` (Player.h:1292) = `detail::NodeLabelMap`
   = `std::map<ttstr,int,ttstr_utf16_less>` ✅ (comparator 复刻 sub_9B1ED0 @0x9B1ED0 UTF-16 码元序)。
⇒ Player 4 HM + var-track deque + node deque + +24 map + render array 容器维度 **无 open 偏差**。
(旧 doc Player_4_HashMaps_Container_Mapping.md §澄清块仍写 "2 处 retype 待办" 已 stale。)

容器**选型** (unordered_map/deque/map/vector 的实现类) **全部对齐**;偏差仅在 2 个 std::string-keyed map
应改 ttstr-keyed。无任何 std::vector 误代 TJS-Array/dispatch 的情况；4 只 Engine vector
已由宽字符生产/消费链确认为 vector<ttstr>，旧 vector<tTJSVariant*> 结论已纠正。

被证伪/需澄清的既有结论:
- ❌ "4 内联 HM" / "KiriKiri 自研内联 hashmap" 措辞 — 实为标准 libstdc++ unordered_map (仅 hash 自研)。
- ✅ "STL→内联 HM 替换属 P3 终极重构" — **此前提本身有误**: 二进制就是 STL unordered_map,
  无需替换成"内联 HM"。真正待办仅是 2 个 std::string→ttstr key retype (轻量, 非 P3 重构)。
