---
name: requirelayerid-0x6AB694-already-aligned
description: ResourceManager::requireLayerId 已对齐 sub_6AB694; 2026-06-06 审计#5"本地复用released id"是误判,两边都单调不复用; counter+216 init=1, set+168预置{0}但inert
type: project
---

ResourceManager::requireLayerId (ResourceManager.cpp) 已与二进制 sub_6AB694 @0x6AB694 对齐，**无需修改**。2026-06-06 fresh-decompile 裁决。

**Why:** 2026-06-06 审计项 #5 称"二进制是纯单调计数器从不搜索 / 本地 while-loop 搜空位会复用 released id → 语义分叉"。fresh-decompile + disasm (0x6ab694-0x6ab74c) 证明**两个断言都错**：
- 二进制 sub_6AB694 也有 lower_bound 跳过循环：`while(*counter ∈ set@+168) ++*counter;` 然后 `_M_insert_unique(*counter); ret=*counter; *counter+=1; return ret;`。不是"纯计数器从不搜索"。
- 本地 nextLayerId 是 State 持久成员、只增不减，与二进制 counter@+216 同样单调，**也不复用** released id。审计的 mental model（本地从低位重扫）忽略了 nextLayerId 是持久成员。
- 关键：counter 永远只持有"尚未发出"的值，所以 skip-loop 体在**两边都恒不迭代**（`*counter ∈ set` 恒假），require 退化为 insert+return+increment。任何 require/release 交错下两边逐位等价。

**counter +216 初值 = 1**：RM ctor sub_6A88CC @0x6a8a3c 写 `*(a1+216)=0x100000001`（低32=1，高32=1 但 require 用 `LDR W0` 32位读，高位 dead）。本地 nextLayerId=1 对齐。
**set +168 = std::set<unsigned int>**（_Rb_tree: header+176/root+184/leftmost+192/size+208），确认非 unordered_set。本地 std::set<tjs_int> 对齐。
**ctor 预置 {0}**：sub_6A88CC @0x6a8a08-38 用 operator new(0x28) + _Rb_tree_insert_and_rebalance 插 key=0。但 counter 从1起，0 永不返回；unloadAll @0x6A8BBC 的 _M_erase 清整棵树(连{0})且不重置 counter。该 {0} 节点功能-inert + oracle-inert，且属 ctor 行为非 requireLayerId 行为；本地 State 默认构造空 set 是忠实的，强插 {0} sentinel 反而是 port-invent。

**releaseLayerId** sub_6AB750 @0x6AB750: lower_bound(id) 命中则 _M_erase_aux，不动 counter+216，返回 walk count(v8 本地未用)。本地 erase(id)+id==0守卫 对齐。

**How to apply:** 本审计项已闭环，勿重开。若未来审计再提 requireLayerId/layerId-allocator "需对齐/复用 released id"，直接引此条驳回。已就地补本地注释(requireLayerId 上方)+IDA 注释(0x6AB694/0x6AB750/0x6A8A3C)记录纠正依据。本 session 仅注释改动，无逻辑变更，无差分回归。

**模式教训（跨函数复用）**：审计声称"本地搜空位会复用"类断言时，先确认那个 next/counter 变量是**局部还是持久成员**——持久单调成员的 while-skip 与二进制 lower_bound-skip 等价，不构成分叉。本 session 5 审计项里 3 项(#2/#4/#5)含审计自身误判，审计断言必须 fresh-decompile 亲自核实。
