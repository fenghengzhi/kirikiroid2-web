---
name: motionplayer-three-class-fresh-audit
description: 2026-06-07 fresh-decompile 三类(Player/EmotePlayer/MotionNode)布局裁决;容器全对齐+5处open偏差表
metadata:
  type: project
---

# Player/EmotePlayer/MotionNode 类布局裁决 (2026-06-07 fresh decompile)

**Why:** 任务要求 fresh decompile 不信 stale 注释，逐字段对照三类。
**How to apply:** 后续审计这三类容器/生命周期时直接引用本表，不必重复 decompile ctor/dtor。

## 全对齐项 (✅ 已 fresh 确认，勿再质疑)
- Player 四 HM 全是 libstdc++ unordered_map: ctor@0x6CED30 各 `_M_next_bkt(...,0xA)`+1.0f load:
  HM1+264(seed 0x6cedd0)/HM2+320(0x6cee24)/HM3+1184(0x6cef58)/HM4+1240(0x6cefb0)。本地 std::unordered_map+ttstr_hash 即正确,**不要改成开放寻址表**。
- ttstr_hash 逐位匹配 HM2_upsert@0x686944: (1025x)^((1025x)>>6)→9acc→32769(h^(h>>11))→0转-1。

> **2026-07-26 superseding correction:** 上述旧记录只覆盖非 null key 的算术 mix，错误地把它外推为 `ttstr_hash` 完整对齐。fresh 证据证明：`ttstr::Ptr == nullptr` 时 hash 为 `0`；Ptr 非 null 时先复用 `Hint@+68`，Hint 为 `0` 才计算并写回；仅非 null 计算结果为 `0` 时改为 `0xFFFFFFFF`。当前 `internal/ttstr_hash.h` 已按此修复，旧“逐位/完整对齐”结论不得继续使用。

- Player+408 param-ramp = std::multimap (finalize@0x6B1ECC 用 _Rb_tree_insert_and_rebalance+sub_9B1ED0 比较器; dtor 0x6cfd4c stdmap_recursive_destroy 证实)。本地 ParameterRampMap multimap 对齐(commit be77533 正确)。
- var-track deque+1296: initVariables@0x6CD750 new(0x1E0)=480B chunk, 元素 stride160B, cascadeKey=scope+"::"+label。本地 VariableLabelScopeDeque 字段序对齐。
- node deque+184: ctor 0x6cf17c 内联 push 单 root(stride2632B)。本地 std::deque<MotionNode> 对齐。
- Player dtor@0x6CFADC 逆序: var-track→nodes→HM4→HM3→param-ramp(+408)→HM2→HM1→node-deque→+24map。本地成员声明升序→逆序销毁降序偏移=匹配。HM1 destroy@0x6DD1A0 / HM3 destroy@0x6DD06C 降序 release 由 EvalCascadeState/PerNodeLayerState 升序声明复刻。
- ctor 单参 dispatch: Player_ctor(this, rm_dispatch), 拷进+636/+656/+992(sub_A0F5E0)。2026-07-23 已纠正本地单 Variant 近似，现为三份独立 owner，恢复各自 AddRef/Release。
- ctor 默认值: +1168=1.0/+1160=1.5/+1176=1.0/+912=100/bounds±DBLMAX 全匹配。
- 无 #pragma pack/static_assert(offsetof==N)/_padN 硬凑 (grep 空; 仅 EmoteWindEmitter _pad1544 是 spring POD 数据契约例外, EmoteBlinkRng static_assert 是 bitcast 检查)。
- EmoteObject_init@0x67DBAC 4级链: new(0xE8)RM→sub_67E20C dispatch facade(2×AddRef)→new(0x5D8)Engine。dtor@0x67F420 Engine→RM→modules。

## Open 偏差 (5)
- **D1 中**: Player.h:1518-1524 `_evalResultList`+`_evalResultListIndex` 是 HM2 的 std::string 影子(注释 line1399 自承 merged 但仍在), 二进制无对应 → 建议删。
- **D2 中**: EmotePlayer.h:116 `virtual ~EmotePlayer()` 多 vptr; 二进制 vtable@0x1A18BB0 属 ncbind 框架壳非 payload; Player/EmoteObject/D3DEmotePlayer 均非多态 → 建议去 virtual。
- **D3 低**: EmotePlayer 经 EmoteObject 中间层 reach Engine; 二进制 24B shell(0x68629C) +8=EmoteEngine* 直连(无中间层)。平台边界已标注。
- **D4 低（2026-07-19 更新）**: `_timelines` 已删除；`_motionsByKey` 的 std::string key/decoded snapshot 缓存仍是二进制外旁路。
- **D5 低**: EmoteObject dtor modules 释放时机异于二进制(Engine→RM→modules); 无依赖行为等价。

## EmotePlayer 链拓扑(确认)
- EmotePlayerNativeInstance 24B(0x68629C): vtable@0x1A18BB0, +8 EmoteEngine*(懒置0), +16 ownership byte; destroy@0x6862D0 gate `if(+8 && !+16)`→sub_67F4B8(EmoteEngine_dtor)+delete。
- 与 D3DEmotePlayer 是两条独立 NCB 链(无继承)。
