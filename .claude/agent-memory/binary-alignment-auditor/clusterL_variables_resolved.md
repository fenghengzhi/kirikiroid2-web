---
name: clusterL-variables-resolved
description: 簇L变量系统/4-HM级联 2026-06-07审计=✅对齐;HM4=raw double(J-1证伪);容器选型全对;残留L-1(+1064 redirect inert)
metadata:
  type: project
---

簇 L（PlayerVariable.cpp + 4 内联 HM 级联）2026-06-07 审计结论：**✅ ALIGNED**。

**事实（本session decompile 复核，权威地址↔语义）**：
- getVariable_wrapper 0x533E1C：cascade 作用于 `*(a1+1064)`(childRoot Player*)；scope-gate(0x6CD16C)→inScope 走 HM1-join(0x6CD39C)，else 走 HM4-first(0x6CD23C)。
- HM1@+264 value=node+48(writeVal)；HM2@+320 value=node+16(double)；HM4@+1240 value=node+16(**raw double**)。
- bind 0x6C4668：HM1 写 `*(v30+32)`=node+48=writeVal(v30=node+16)、`*(v30+40)`=node+56=weight=1.0；HM2[raw]=a4 恒写；ramp HM1-block 用 suffix、HM2-tail 用 raw label。
- var-track deque：对象@+1296，`_M_start`@+1312(cur/first/last/node=1312/1320/1328/1336，finish@+1344)。isLabelInBindScopeList 扫 _M_start，initVariables ctor@+1296 push@finish。**同一容器**（旧 +1312 vs +1296 之疑澄清）。
- initVariables 0x6CD750 数据源=**TJS dispatch**（PropGet(*(a1+528),"variable")→per-item PropGet "label"/"scope"），非 PSB struct。item 160B：+0 cascadeKey、+8 cursor、+16 value double、+24 labelName、+48/+104 slot[2]。

**🔧 纠正旧 memory（J-1 证伪）**：clusterJ「HM4 value=owning tTJSVariant*」**错**。clearHM3_HM4 0x6B8118 释放 `node[1]`=node+8=**KEY ttstr**（非 value）；value@node+16 是裸 double(memset 即清)。本地 `unordered_map<ttstr,double>` 正确。已在 IDB 0x6B8118 注释纠正。

**容器选型全对齐**（byte-verified 为 libstdc++ std::unordered_map+自研 ttstr_hash，非自研 HM）：HM1=EvalCascadeMap、HM2/HM4=unordered_map<ttstr,double>、+1296=deque<VariableLabelScope>、+408=multimap<ttstr,Entry*>。**无 STL→自研 HM 的 P3 目标**。

**残留 L-1（inert，非 patchable）**：+1064 childRoot redirect 层本地缺失，getVariable 直接在 this 上 cascade。独立 Player +1064≈self 故值同源、读结果一致；现有内容 variable-free 故全程 inert。需 childRoot Player 建模，属 Player-layout 重构，非 PlayerVariable.cpp 局部可修。

**合法 DEFERRED**（非偏差）：sub_697D34 chainDispatches(无 consumer)、HM3 populate(dead-data 无 reader)、stream③ 插值(无 variable fixture)。

报告：analysis/audit_motionplayer_2026-06-07/clusterL_variables_hm.md。
