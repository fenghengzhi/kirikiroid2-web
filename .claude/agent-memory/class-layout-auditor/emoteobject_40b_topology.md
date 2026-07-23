---
name: emoteobject-40b-topology
description: EmoteObject 40B (init@0x67DBAC) 是 D3DEmotePlayer→Player 之间的中间层；+0 ResourceManager* +8 EmoteEngine* +16/24 vector<ttstr>。2026-07-13 已由 0x52FDD4 producer 与 0x67DBAC consumer 纠正旧 vector<tTJSVariant*> 误判并落地多路径加载。
metadata:
  type: project
---

# EmoteObject(40B) 拓扑 + Gap2 偏差权威记忆

> 更新 2026-06-05 (P3-B class-layout-auditor 审计): Gap2 偏差#1「EmoteObject 缺 RM 字段」**已修复**;
> sub_67E20C 描述已纠正(是 NCB class-object 工厂,非简单封装)。详见末尾「P3-B RM ownership 审计」节。

## 二进制对象链(全部 fresh-decompile 2026-06-03 确认)

```
D3DEmotePlayer (≥56B NCB) ── +24 → EmoteObject* (主槽)
                          └─ +32 → EmoteObject* (次槽,恒 null)
EmoteObject (40B, operator new(0x28), init@0x67DBAC)
  +0  ResourceManager*  (operator new(0xE8)=232B, ctor sub_6A88CC, dtor sub_6A8B94)
  +8  EmoteEngine*      (operator new(0x5D8)=1496B, ctor EmoteEngine_ctor@0x67E38C)
  +16 vector<ttstr> begin   ← 资源路径数组 (可多个,D3DEmotePlayer_load 逐个转换并 push)
  +24 vector<ttstr> end/cap
EmoteEngine (1496B) ── +1064 → Player* (operator new(0x568)=1384B, Player_ctor@0x6CED30)
```

EmoteObject dtor = `EmoteObject_destroy@0x67F420`: 析构序 = `+8 EmoteEngine`(sub_67F4B8 + delete) → `+0 ResourceManager`(sub_6A8B94 + delete) → `+16 vector` 逐元素 Release + delete buffer。

## init@0x67DBAC 关键流程(ctor 即建,非懒)

1. `operator new(0xE8)` RM → `sub_6A88CC(rm,...)` → 存 `EmoteObject+0`
2. `sub_67E20C(rm,1,0)` = **NCB class-object 工厂**(非简单封装): 用 class object `qword_1AB80A0` CreateNew 一个 NCB dispatch 实例 → 写 `*(dispatch+8)=native RM`(@0x67e2e0) + 按 a2=1 写 `dispatch+16=1`(ownership byte,@0x67e2ec, dispatch 拥有 native)。即 CreateAdaptor 语义。返回后 init 处 `(**v6)(v6)` ×2 AddRef 喂 EmoteEngine_ctor
3. `operator new(0x5D8)` EmoteEngine → `EmoteEngine_ctor(engine, &dispatchWrapper)` → 存 `+8`。**EmoteEngine_ctor 第2参 a2 = dispatch 包装, 转发给 Player_ctor 作 resourceManager_dispatch**
4. `ttstrVector_assign_67F0CC(EmoteObject+16, modulePaths)` 拷贝资源路径数组到 +16 vector
5. 逐个 path → `ResourceManager_loadResource` 加载 → 从最后结果取 metadata/base/chara/motion dict
6. `EmoteObject_applyChara_67F370(engine, &charaDict)` + `Player_play(*(engine+1064),1,&motionDict)`
7. `EmoteEngine_applyMetadata_buildControllers(engine, metadataDict)` ← 这一步建 controller/deque

注意:**Player 的 resourceManager 来源是 dispatch 包装(iTJSDispatch2*),不是 ResourceManager C++ 对象本身**。RM C++ 对象(232B)只 EmoteObject 持有;它经 sub_67E20C 包成 TJS dispatch 后才下传给 EmoteEngine/Player。

## 本地对照(cpp/plugins/motionplayer/EmotePlayer.h:73-88 + EmotePlayer.cpp:24-31)

```
D3DEmotePlayer { _rm; _primaryObj; _secondaryObj; 壳字段 }  ✅ 双槽对齐
  EmoteObject { _engine(裸ptr); _module(单 variant) }       ⚠️ 见偏差
    EmoteEngine { _player(裸ptr); 10 deque; 7 HM; 7 ctrl }   ✅ 已对齐
      Player(1384B by-pointer)                               ✅
```

### Gap2 EmoteObject 层偏差

1. ~~**EmoteObject 缺 ResourceManager* 字段(+0)**~~ **已修复并于 2026-07-18 纠正所有权机制**。此前的 by-value `ResourceManager` + `shared_ptr<State>` 与另建 adaptor RM 是两个 C++ 对象，不符合 `0x67DBAC/0x67F420`。本地现由 EmoteObject 构造体内 `new ResourceManager`，sticky dispatch 指向该同一对象，析构显式 delete。

2. ~~EmoteObject 单 module / 错误 variant-vector~~ **已修复(2026-07-13)**。`0x52FDD4` 对全部参数先 variant→ttstr，`0x67DBAC` 把每项交给 `ResourceManager_loadResource`，`0x67F0CC` 只操作字符串 handle refcount。本地现为 `vector<ttstr> _modulePaths`，raw callback 保留全部参数，EmoteObject 构造期顺序加载并用最后结果初始化 Player。

### 已对齐项(无需动)

- D3DEmotePlayer 双 EmoteObject 槽(_primaryObj@+24 / _secondaryObj@+32) ✅, 次槽恒 null ✅
- EmoteObject._engine 裸指针 + 手动 new/delete ✅ (CLAUDE.md 规则)
- 析构序 EmoteObject::~ = delete engine -> clear sticky facade -> delete RM；随后 vector 析构 ✅

## getVariable ↔ setVariable 桥(Gap1, 与 Gap2 解耦)

- `D3DEmotePlayer_getVariable@0x5305D4`: `*(*(this+24)+8)`=EmoteEngine → `Player_getVariable_wrapper@0x533E1C(EmoteEngine,...)` 读 `*(EmoteEngine+1064)=Player`, 在 **Player** HM1@+264/HM4@+1240 上 scope-scan 级联查找
- `D3DEmotePlayer_setVariable@0x5305C8`: 同样拿 EmoteEngine → `Player_setVariable@0x671228(EmoteEngine,...)` 写 **EmoteEngine** HM6@+1384/HM7@+1440 + deque case4-8(读 deque 内部 _M_start 在 +256/+336/+416/+576/+656, NOT 独立 deque——是 deque#4..#9 的 +16 header)
- 桥 = progress 末尾 bind-loop(EmoteEngine_progress@0x67D01C 0x67d3a4): `for(i=*(engine+1456); i; i=*i)` 遍历 HM7 insertion-order 链:
  - `sub_67C560(engine, &i.key, &i.value)` = var-track 加权级联(读 deque#10@+1040 的 sub_68C134 节点, 累加 `value += elem[48]*node[72]`)
  - `v68 = sub_67C6B0(engine, &i.key)` = negate-flag resolver(查 HM@+824, 未命中查 HM@+880, 再 indexOf 扫 vector@+800; 命中插入对应表返回 1=negate)
  - `Player_bindParameterValue_writesHM1_HM2(*(engine+1064), &i.key, 0, negate?-value:value)` ← 把 EmoteEngine HM7 的物理输出写回 **Player** HM1/HM2, 使 getVariable 能读到
- **2026-07-18 本地状态更正**: bind-loop 已实装；`sub_67C560` 使用 Engine
  HM3/+1040/56B track deque，`sub_67C6B0` 使用 Engine mirror vector/set caches，并调
  `Player_bindParameterValue@0x6C4668`。旧 D3DEmotePlayer 双写 shim 已移除，本段的
  “stub/移除阻塞”结论已被证伪。

## 1456 字段真相(纠正既有误判)

EmoteEngine+1456 = HM7(@+1440 unordered_map<ttstr,double>) 的 `_M_before_begin._M_nxt`(libstdc++ insertion-order node 链头), **不是**独立 _bindListHead 字段。dtor(0x67F4B8) `for(v3=*(a1+1456); v3; v3=*v3)` 遍历释放每节点 key ttstr。本地正确建模为直接遍历 typed map(EmoteEngine.cpp:1927), 接受 libc++ 无 insertion-order 链的 PLATFORM_BOUNDARY。详见 [[emoteengine-1496b-layout]] "_bindListHead 是伪字段" 纠正。

## P3-B RM ownership 审计裁决(2026-06-05, 全 fresh-decompile 核实, 6/6 对齐, 0 真实 bug)

RM 全程是 **dispatch 持有 + nativeRM() 经 +8 解出唯一 native** 模型(删了 Player._resourceManagerNative value 成员):

- **ctor 单参对齐（2026-07-23 纠正）**: Player_ctor@0x6CED30 头 `(this, resourceManager_dispatch)` 单参；同 dispatch 经 sub_A0F5E0 写 +636/+656/+992 三槽（0x6CEE9C/0x6CEEB0/0x6CEF28）。+992 从未被 Math.RandomGenerator 覆写；+676/+716 是 render descriptor/color 对象（0x6CF080 建立 `color` 属性关系）。`Player::random@0x6BA7B8` 对 +992 RM dispatch 调 `random`，真正 RNG 由 `ResourceManager_ctor@0x6A88CC` 持有在 RM+144。旧“本地拆 _resourceManager + _tjsRandomGenerator 可接受”结论作废。
- **nativeRM() = binary findSource +8 unpack**: findSource@0x694928 = +636 dispatch PropGet→NCB instance→`*(+8)`=native。本地 nativeRM()(PlayerCore.cpp:144) = AsObjectNoAddRef → ncbInstanceAdaptor<RM>::GetNativeInstance(ncbind.hpp:171 返回 adaptor->_instance = +8 native)。忠实等价。
- **child 继承**: Player_initNodeFields case3 @0x6b43cc `Player_ctor(child, parent+992)` + @0x6b43dc `*(child+8)=parent`(两步: ctor 单参 + 构造后设 parent)。本地 NodeTree.cpp:254/PlayerUpdateParticles.cpp:450 `new Player(getResourceManager())` + setParentPlayerLike_0x6B1ABC。3 条 child 路径全设 parent, 无漏。
- **EmoteObject ctor refcount（2026-07-18 纠正）**: `CreateAdaptor(_rm, true)` 与 `sub_67E20C(rm,1,0)` 一样令 sticky adaptor 指向 EmoteObject 唯一拥有的 RM，不再创建 state-shared 副本。
- **EmoteObject dtor 序**: binary `0x67F420` = engine → RM → vector；本地先 delete engine、清 sticky facade、delete RM，函数体结束后 vector 析构，顺序已闭合。
- **EmoteEngine dtor**: delete _player **最后**(EmoteEngine.cpp:243, _engineBack 反指针要求 engine 其他字段先死)。

**2026-07-23 当前纠正**：旧“+656 bufLayer 未实装 / findMotion 未迁 +992
FuncCall”残留项已失效。findMotion/loadMotion 已走 Player+992 RM dispatch
FuncCall；`0x6C7440` buffered 路径已从 Player+656 RM dispatch 取得持久
`bufLayer`，并接入 ancestor-mask / `operateRect` 提交。普通渲染 SLA 也已使用
active/retired 双 ordered-tree pass。仍 open：SLA payload/compare 与 accurate-SLA
J8 状态、完整 `Motion_doAlphaMaskOperation@0x6AF104` 行为；另有 EmoteEngine
4 variant-vector dtor 缺 per-element Release（当前 inert，vector 恒空，
TODO@EmoteEngine.cpp:129）。这些 open 项不反向否定本节已经闭合的 RM ownership。

## 关键引用
- EmoteEngine 字段表: [[emoteengine-1496b-layout]]
- Player 1384B 字段表: [[player-1384b-flat-spec]]
- Player 4 HM 容器: [[player-container-layout]]
- EmotePlayer(24B 退化壳, 区别于 D3DEmotePlayer): [[emoteplayer-24b-shell]]
