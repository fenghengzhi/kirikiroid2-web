---
name: p3b-ownership-dispatchin-audit
description: P3-B(3084723/7516a64)后 Player/EmoteObject/RM/EmoteEngine 类布局+生命周期+容器选型 fresh-decompile 裁决:4HM=libstdc++/RM layer-id=set+RB-tree/ctor单参dispatch已收敛
metadata:
  type: project
---

P3-B fresh-decompile 复核裁决 (2026-06-06, Player ctor@0x6CED30 / dtor@0x6CFADC /
EmoteObject@0x67DBAC / RM@0x6A88CC / HM1destroy@0x6DD1A0 / HM3destroy@0x6DD06C).

**已对齐 (✅):**
- Player 4 HM 全是 libstdc++ unordered_map:ctor 4 次调 std_Prime_rehash_policy_M_next_bkt(...,0xAu)
  + operator new(8*bkt) bucket 数组 + _M_before_begin 单链。本地 internal/player_containers.h
  全 std::unordered_map<ttstr,V,ttstr_hash,ttstr_equal>。hash 自研(1025*x^(>>6),9*acc,32769)
  在 ttstr_hash.h:26 逐字对齐。✅
- 2 处 string→ttstr retype 已完成:_evalResultValues(HM2 @+320)=LabelValueMap(ttstr key,
  Player.h:1472);_nodeLabelMap(+24)=NodeLabelMap=std::map<ttstr,int,ttstr_utf16_less>
  (player_containers.h:84,comparator 对齐 sub_9B1ED0)。✅
- ctor 收敛单参 dispatch-in:本地 Player(const tTJSVariant& rmDispatch) 对齐二进制
  (this, rm_dispatch) 单参;parentPlayer 移出 ctor(setParentPlayerLike_0x6B1ABC)。✅
- dtor RAII:本地 ~Player=default(成员逆序);二进制 0x6CFADC 手写 teardown HM4(+1240)→HM3(+1184)
  →HM2(+320)→HM1(+264) 逆序。语义对齐(value_structs.h 各 value 析构顺序逐字对齐 destroy 函数)。✅
- RM layer-id=std::set<tjs_int>(ResourceManager.h:174)对齐二进制 +176 std::_Rb_tree
  (_Rb_tree_insert_and_rebalance)+nextId counter +216(=0x100000001)。✅ by-name map 已删(无二进制对应)。
- EmoteObject +0 RM:二进制 operator new(0xE8)=232B 独立堆对象(EmoteObject_init@0x67DBAC step1);
  本地 _rm(ResourceManager,shared_ptr<State> 值)+ _rmDispatch facade,EmoteEngine* 裸指针手动 new/delete。✅
- 无 #pragma pack/static_assert(offsetof==N) 硬凑。仅 2 处合法元素内部数据契约 pad
  (EmoteWindParticle._pad[3]/EmoteWindEmitter._pad1544[3] 都是平台无关 POD 内部对齐,合法)。✅

**残留偏差 (🟡 方法论框架内可接受 / port-extra,非架构错):**
- Player 仍有多个 std::unordered_map<std::string,...>:_motionsByKey/_disabledSelectorTargets/
  _parameterEntryById/_evalResultListIndex/RuntimeSupport snapshot maps。这些是 MotionSnapshot 解析层
  + Web render-host 扩展,**二进制无对应偏移**(注释明确标注 no-offset/port extension),非 4 权威 HM 的误用。
  属源码结构层 port-invention,不是容器选型错。`_timelines/_playingTimelineLabels` 已在
  2026-07-19 按 Player/EmoteEngine NCB owner 证据删除。
- HM3(_perNodeLayerStateMap)/HM4(_variableSnapshotMap)/HM1(_evalCascadeMap) 容器选型对,但 DEFERRED 空
  (无 resetMotionState 消费者)。键已正确,populate 待 0x699510/0x6B2D40 移植。

setVariable@0x671228 this=EmoteEngine(HM6@+1384),非 Player;本地 EmoteEngine::setVariable 归属正确。
