---
name: player-containers-libstdcxx-spec
description: motion::Player 1384B 内联 6 容器全部是 GCC libstdc++ std::unordered_map<ttstr,V> + std::deque<T> 实例化, prime+load=1.0+_M_single_bucket sentinel; 4 HM 共享同一 _Hashtable 模板, 2 deque 同 deque 模板. 颠覆性发现: 不是 KiriKiri 自定义容器
metadata:
  type: project
---

# Player 6 内嵌容器 ABI (libstdc++ 实例化)

## 关键颠覆性发现
6 个容器全部是 **GCC libstdc++ STL 容器**, 不是 KiriKiri 自定义实现.
- 4 个 HM: `std::_Hashtable<...,_Hashtable_traits<true,false,true>>` (cache_hash=true, constant_iterators=false, unique_keys=true)
- 2 个 deque: `std::_Deque_base`
- 模板共享通过 `sub_149EDF8 = std::__detail::_Prime_rehash_policy::_M_next_bkt` (xref 含 libstdc++ 实例化函数符号 `_ZNSt10_Hashtable...`) 确认

旧 spec 把它们记作 "KiriKiri HashMap (prime+load=1.0)" 是误判 — prime+load=1.0 是 GCC libstdc++ `_Prime_rehash_policy::_S_growth_factor` 的默认.

本地 cpp/core/tjs2/tjsHashSearch.h 的 `tTJSHashTable` (固定 64 桶+链地址) **完全不同**, 不可复用.

## std::unordered_map 控制结构 (56B inline)
所有 4 个 HM 共享:
| 偏移 | 大小 | libstdc++ 字段 | ctor 初值 |
|---|---|---|---|
| +0  | 8 | `_M_buckets` (T**) | 单桶时 = `&_M_single_bucket` |
| +8  | 8 | `_M_bucket_count` (size_t) | `_M_next_bkt(10)` 查表 |
| +16 | 8 | `_M_before_begin._M_nxt` (_Hash_node_base*) | 0 |
| +24 | 8 | `_M_element_count` (size_t) | 0 |
| +32 | 4 | `_M_rehash_policy._M_max_load_factor` (float) | 1.0f (= 0x3F800000) |
| +36 | 4 | padding | 0 |
| +40 | 8 | `_M_rehash_policy._M_next_resize` (size_t) | 0 |
| +48 | 8 | `_M_single_bucket` (_Hash_node_base*) | 0 (容量>1时不用) |

总 56B, 标准 GCC libstdc++ unordered_map<K,V> sizeof.

## std::deque 控制结构 (80B inline)
所有 2 个 deque 共享:
| 偏移 | 大小 | libstdc++ 字段 |
|---|---|---|
| +0  | 8 | `_M_map` (T**) |
| +8  | 8 | `_M_map_size` |
| +16 | 32 | `_M_start` iterator `{cur, first, last, node}` |
| +48 | 32 | `_M_finish` iterator `{cur, first, last, node}` |

总 80B (10 qwords). 标准 GCC libstdc++ deque<T> sizeof.

deque 块大小由 `__deque_buf_size(sizeof(T))`:
- T=2632B → block=2632B (1 elem/block)
- T=160B → block=480B (3 elem/block, since 512/160=3)

## 4 HM 实例化区分

| HM | Player offset | 实例化 | Entry 大小 | Hash@ | Key | Value |
|---|---|---|---|---|---|---|
| HM1 | +264..+320 | `unordered_map<ttstr, EvalCascadeState 72B>` | 96B | +88 | ttstr | 72B struct (dispatch refs + dyn array) |
| HM2 | +320..+376 | `unordered_map<ttstr, double_or_dispatch 8B>` | 32B | +24 | ttstr | 8B (double 写, dispatch 读取场景共用) |
| HM3 | +1184..+1240 | `unordered_map<ttstr, PerNodeLayerState 688B>` | 720B | +712 | ttstr (node path "/-joined") | 688B layer state snapshot |
| HM4 | +1240..+1296 | `unordered_map<ttstr, iTJSDispatch2* 8B>` | 32B | +24 | ttstr | 8B (dispatch*) |

Entry layout 通式: `[next@0, key_ttstr_inner_ptr@8, value@16..(entry_size-8), hash_cache@(entry_size-8)]`.

> ttstr 物理上是单 ptr `tTJSVariantString*`. Key 占 8B (entry+8), 释放走 `tTJSVariant_Release`. tTJSVariantString 中: +60 字符长度, +68 ttstr hash 缓存槽 (用于 1025-base ttstr hash函数)

## HM1 Value Struct (72B at entry+16, dtor: Player_HM1_value_destroy @0x6DD1A0)
| 子偏移 (相对 entry+8 = pair 起始) | 大小 | 类型 | 释放方式 |
|---|---|---|---|
| +0 | 8 | iTJSDispatch2* | tTJSVariant_Release |
| +8 | 8 | iTJSDispatch2* | tTJSVariant_Release |
| +16 | 8 | iTJSDispatch2** (begin) | 数组逐个 Release |
| +24 | 8 | iTJSDispatch2** (end) | 配合 begin |
| +32..+56 | 24 | 未读 | (memset 0 在 insert) |
| +56 | 8 | void* (堆对象) | operator delete |
| +64..+80 | 16 | 未读 | |
> 实际是 `pair<ttstr, EvalCascadeState>`: ttstr 占 entry+8 (8B), value 80B 紧随. value 描述: 缓存 2 个 dispatch + 1 动态数组 + 1 拥有的堆对象.

## HM3 Value Struct (688B at entry+16, dtor: Player_HM3_entry_destroy @0x6DD018, init: Player_HM3_initValueFromNode @0x699510)
- entry+8 (=pair.first): ttstr key (slash-joined node path, Player_buildNodePathKey)
- entry+16..+704 (688B): PerNodeLayerState struct snapshot from MotionNode (从节点 +200~+1576 字段抓拍)
- entry+584 内有一个堆 ptr (`a2[73]` = entry+584+16) — sub_6DD018 delete 它
- entry+560 (a2+70) 和 +688 (a2+86) 是 ttstr 字段 — sub_A0F778 销毁
- entry+24 (a2+3) 有一个嵌套的小结构 (sub_6DD06C 销毁, 含 dispatch + dyn arrays)

## 2 deque 实例化区分

| deque | offset | T size | 含义 | init/dtor |
|---|---|---|---|---|
| deque-A (nodesDeque) | +184..+264 | 2632B | MotionNode (motion 节点) | init: `Player_nodesDeque_init @0x6F4E90`, destroy: `Player_nodesDeque_destroyAll @0x6CF9B4`, move-out: `Player_nodesDeque_destroy @0x6F436C` |
| deque-B (controllerDeque) | +1296..+1376 | 160B | EmoteController/VariableLabel (待最终确认) | init: `Player_controllerDeque_init @0x6F4FD8`, destroy: `Player_controllerDeque_destroy_guess @0x6CF678` |

> 之前 spec 说 deque-B 是 88B, 实测 ctor memset 0x50 + 写 10 qword = 80B. +1376..+1384 是 tail padding/对齐字节.

## 共享辅助函数 (按地址)
- `0x149EDF8` std_Prime_rehash_policy_M_next_bkt — 选 prime 桶数
- `0x149DF58` `__throw_length_error` (size>>61 时调用)
- `0x6F4F5C` std_deque_initBlocks_2632 — alloc 2632B 块
- `0x6F50D8` std_deque_initBlocks_160 — alloc 480B 块
- `0x6F1914` Player_nodesDeque_pushBlock — 扩容并 push_back 一个 MotionNode 块
- `0x6F19B4` MotionNode_initFields — 单 MotionNode 0 init (2632B 中关键字段)
- `0x6F4C8C` MotionNode_destroy_guess — 销毁单 MotionNode (含 ttstr/dispatch 子字段)

## API 表 (Hashtable, GCC libstdc++ 形态)

### `_Hashtable::_M_find_node` (4 个实例化, byte-identical, 不同 K/V 模板)
| 函数 | 地址 | HM | 签名 |
|---|---|---|---|
| Player_HM1_find_node | 0x6F51BC | HM1 | `_QWORD* find(this&hm, size_t bucket_idx, ttstr* key, size_t code)` |
| Player_HM2_find_node | 0x686B6C | HM2 | 同 |
| Player_HM3_find_node | 0x6F28A4 | HM3 | 同 |
| Player_HM4_find_node | 0x6887F4 | HM4 | 同 |
> 算法: 走桶链表, 比较缓存 hash; 相同 hash 再用 `sub_9B1ED0` (wstring strcmp) 比较 key 字节. 返回 prev-node 指针 (即指向匹配 node 的 `next` 字段的指针), 未找到返回 nullptr.

### `_Hashtable::_M_insert_unique_node` (此前已重命名)
| 函数 | 地址 | HM | 注意 |
|---|---|---|---|
| Player_HM1_insert_node | 0x6F53C8 | HM1 | hash 写 entry+88 |
| Player_HM2_insert_node | 0x686A4C | HM2 | hash 写 entry+24 |
| Player_HM3_insert_node | 0x6F2790 | HM3 | hash 写 entry+712 (估算) |
> 流程: 调用 `_Prime_rehash_policy::_M_need_rehash` 触发 rehash → 写 hash → 桶链插入头.

### Upsert (get-or-create) 包装器
| 函数 | 地址 | HM | 返回 |
|---|---|---|---|
| Player_HM1_upsert_evalCascade | 0x6F52AC | HM1 | 指向 value (entry+16) |
| Player_HM2_upsert_labelToValue | 0x686944 | HM2 | 指向 value (entry+16) |
| Player_HM3_upsert_perNodeLayerState | 0x6F2674 | HM3 | 指向 value (entry+16) |
> HM4 暂无独立 upsert; 上游通过 `Player_HM2_find_node` 直接读访问 +1240 表 (`sub_6CD23C` 即 cascadeEval).

### Clear / dtor
- HM1 clear: `Player_HM1_clear @0x6CF930` (walk list, sub_6DD1A0 each value, op-delete entry, reset buckets)
- HM3 clear: `Player_HM3_clear @0x6CF7C4`
- HM2/HM4 inline 在 Player_dtor (`0x6CFADC`)
- HM1 value destroy: `Player_HM1_value_destroy @0x6DD1A0`
- HM3 entry destroy: `Player_HM3_entry_destroy @0x6DD018`
- HM3 value sub-destroy: `Player_HM3_value_destroy @0x6DD06C`

### Hash 函数 (与 `tjs_char_hash` 等效)
所有 4 HM 共享同一 ttstr hash:
```c
v9 = 0;
v8 = first_wchar;
while (v8) {
  v11 = v9 + v8;
  v8 = *next_wchar;
  v9 = (1025 * v11) ^ ((1025 * v11) >> 6);
}
v13 = 9 * v9;
hash = 32769 * (v13 ^ (v13 >> 11));
if (!hash) hash = -1;
*(ttstr_inner + 68) = hash;  // 缓存到 tTJSVariantString 内
```

## API 表 (Deque)
| 函数 | 地址 | 用途 |
|---|---|---|
| Player_nodesDeque_init | 0x6F4E90 | (hint=0) 初始化 10 qword 控制结构 + 分配 1 块 (≥8 块容量) |
| Player_controllerDeque_init | 0x6F4FD8 | 同上但 stride=160, 块=3 elem (480B) |
| Player_nodesDeque_pushBlock | 0x6F1914 | 满时调 sub_6F1C80 扩 map, op-new 一块, init MotionNode |
| Player_nodesDeque_destroyAll | 0x6CF9B4 | 遍历析构每个 MotionNode + 释放所有块 + 释放 map |
| Player_controllerDeque_destroy_guess | 0x6CF678 | 同模式 (sub_6F3290 共享析构步骤) |
| Player_nodesDeque_destroy | 0x6F436C | 析构后从备份(_OWORD)恢复控制结构 (move-assign 用) |

## 不可对齐风险评估
- **可全部用 std::unordered_map / std::deque 直接对齐** — 因为本就是 libstdc++ STL
- 但需注意 `_Hashtable_traits<cache_hash=true,...>` 必须 enable (默认对 hash<string> 是 true)
- ttstr 作为 key 需要支持 `std::hash<ttstr>` 实现与二进制相同的 1025/9/32769 算法, 否则桶分布不一致 (功能等价但内存不等价)
- 字符串 key 比较用 wchar16 (sub_9B1ED0) — 对应 ttstr 内部 wchar_t* 比较
- 本地 `tTJSHashTable` (64 桶 chain) 与 libstdc++ `_Hashtable` 不兼容, **不要用它**

## 推荐本地实现拓扑
```
cpp/plugins/motionplayer/internal/
├── player_containers.h        // 类型别名:
│                              //   using HM1 = std::unordered_map<ttstr, EvalCascadeState>;
│                              //   using HM2 = std::unordered_map<ttstr, double>;
│                              //   using HM3 = std::unordered_map<ttstr, PerNodeLayerState>;
│                              //   using HM4 = std::unordered_map<ttstr, iTJSDispatch2*>;
│                              //   using NodesDeque = std::deque<MotionNode>;
│                              //   using ControllerDeque = std::deque<ControllerEntry160B>;
├── eval_cascade_state.h       // HM1 value struct (72B)
├── per_node_layer_state.h     // HM3 value struct (688B)
└── motion_node.h              // 2632B MotionNode
```
ttstr hash 实现见 `Player_HM2_upsert_labelToValue @0x686944` 的伪代码段.

## 单元测试覆盖建议
- HM1/HM2 sizeof = 56, HM3 sizeof = 56, HM4 sizeof = 56, NodesDeque sizeof = 80, ControllerDeque sizeof = 80
- Player 全 6 容器实例化后 + 其他字段 ≤ 1384 字节 (with tail padding 8B unused)
- HM 插入/查找路径与 libkrkr2.so 反编译伪代码一一对应 (1025/9/32769 hash 函数)
- deque-A 单元素块 (sizeof(MotionNode)=2632 > 512), deque-B 三元素块 (sizeof=160)
