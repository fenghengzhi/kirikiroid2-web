---
name: m3-container-keytype-verdict
description: M3 cluster (4 Player HM + var-track deque) container-selection fresh-decompile 2026-06-03 — 4 HM 确证是 libstdc++ std::unordered_map(非 inline open-addressing),无需重写; 2 个真实 key-type retype gap 仍 open
metadata:
  type: project
---

Fresh-decompile 2026-06-03 (read-only) 核实 M3 cluster 容器选型两大命题。

**命题1: 4 个 Player HM 是 libstdc++ std::unordered_map,不是 inline open-addressing → 无需 STL→inline-HM 重写。CONFIRMED。**
- 证据: insert 0x686A4C 字面调用 `std::__detail::_Prime_rehash_policy::_M_need_rehash`; rehash 0x686C5C 是教科书 libstdc++ `_M_rehash`(size==1 走 single-bucket a1+48 优化, 否则 operator new(8*a2)+memset; 按 node+24 cached hash 重链; before-begin sentinel = 控制结构+16)。node=operator new(0x20)={+0 next,+8 key(ttstr owning AddRef),+16 value(raw double),+24 cached hash}。控制结构48B:{+0 bucket,+8 nbkt,+16 before-begin,+24 count,+32 loadFactor 1.0f,+40 next_resize}。
- 本地 _evalCascadeMap/_perNodeLayerStateMap/_variableSnapshotMap 用 unordered_map<ttstr,V,ttstr_hash,ttstr_equal> = 正确选型,已对齐。**player_4_hashmaps.md 与 Player.h:341 用"内联/inline KiriKiri HM"措辞误导**——它们就是标准 std::unordered_map(自定义 ttstr hash),勿据此做 open-addressing 重写。

**命题2: getVariable(0x533E1C) 与 setVariable(0x671228) 操作 disjoint maps。CONFIRMED。**
- setVariable 0x671228 this=EmotePlayer(~1576B),写 HM7@+1440 + 5 controller deque(+256/336/416/576/656)。getVariable 读 Player+264(HM1)/+320(HM2)/+1240(HM4)。不同对象不同 offset,桥接仅 progress bind-loop,非同一 HM2。

**2 个真实 OPEN GAP (非平台边界, 有反编译证据):**
- GAP-A(高): `_evalResultValues`(HM2 镜像 @Player+320) 本地是 `unordered_map<std::string,double>`(Player.h:1310)。二进制 HM2 是 ttstr-keyed + KiriKiri UTF-16 hash(0x686944/0x6CD5A8 读 node+16 raw double)。是 4 HM 里唯一未用 ttstr_hash 的掉队者。别名 `detail::LabelValueMap`(player_containers.h:39) 已就绪未启用。retype 会改桶分布/迭代序。
- GAP-B(中): `_nodeLabelMap`(Player+24, Player.h:1181) 本地是 `std::map<std::string,int>`(UTF-8 byte 序)。二进制 0x6F2228 是 std::map<ttstr,int> 红黑树,比较器=sub_9B1ED0=UTF-16 code-unit 词典序。纯 ASCII label 一致,非 ASCII(日文)label 的 lower_bound/迭代序偏离。M5 memory 确认 key 是 RAW label(非 path)但未记 key-TYPE 缺口。

**evidence gap**: EvalCascadeState 内 writeVal/weight 字节偏移两份文档冲突(value_structs.h:144 标 V+32/V+40; player_4_hashmaps.md 标 V+48/V+56),本次未复编 0x6C4964/0x6C4968 核实。不影响上述结论(HM1 key 已 ttstr 对齐)。

var-track deque(Player+1296): isLabelInBindScopeList 0x6CD16C 扫 std::deque(stride 160B=20 qword, cascadeKey@+0, 比较 sub_9B1ED0)。本地 VariableLabelScope(160B 字段对齐)+VariableLabelScopeDeque=std::deque ✅。
