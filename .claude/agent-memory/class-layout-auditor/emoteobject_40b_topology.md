---
name: emoteobject-40b-topology
description: EmoteObject 40B (init@0x67DBAC) 是 D3DEmotePlayer→Player 之间的中间层；+0 ResourceManager*(232B,sub_6A88CC) +8 EmoteEngine* +16/24 vector<tTJSVariant*>。本地 EmoteObject 缺 RM 字段(rm by-value 直传 EmoteEngine)且 _module 是单 variant 非 vector — Gap2 两处偏差权威
metadata:
  type: project
---

# EmoteObject(40B) 拓扑 + Gap2 偏差权威记忆

## 二进制对象链(全部 fresh-decompile 2026-06-03 确认)

```
D3DEmotePlayer (≥56B NCB) ── +24 → EmoteObject* (主槽)
                          └─ +32 → EmoteObject* (次槽,恒 null)
EmoteObject (40B, operator new(0x28), init@0x67DBAC)
  +0  ResourceManager*  (operator new(0xE8)=232B, ctor sub_6A88CC, dtor sub_6A8B94)
  +8  EmoteEngine*      (operator new(0x5D8)=1496B, ctor EmoteEngine_ctor@0x67E38C)
  +16 vector<tTJSVariant*> begin   ← PSB 引用数组 (可多个,D3DEmotePlayer_load 逐个 push)
  +24 vector<tTJSVariant*> end/cap
EmoteEngine (1496B) ── +1064 → Player* (operator new(0x568)=1384B, Player_ctor@0x6CED30)
```

EmoteObject dtor = `EmoteObject_destroy@0x67F420`: 析构序 = `+8 EmoteEngine`(sub_67F4B8 + delete) → `+0 ResourceManager`(sub_6A8B94 + delete) → `+16 vector` 逐元素 Release + delete buffer。

## init@0x67DBAC 关键流程(ctor 即建,非懒)

1. `operator new(0xE8)` RM → `sub_6A88CC(rm,...)` → 存 `EmoteObject+0`
2. `sub_67E20C(rm,1,0)` 创建 TJS dispatch 包装(2× AddRef)
3. `operator new(0x5D8)` EmoteEngine → `EmoteEngine_ctor(engine, &dispatchWrapper)` → 存 `+8`。**EmoteEngine_ctor 第2参 a2 = dispatch 包装, 转发给 Player_ctor 作 resourceManager_dispatch**
4. `VariantPtrVector_assign_67F0CC(EmoteObject+16, psbArgs)` 拷贝 PSB 引用数组到 +16 vector
5. 逐个 PSB → `ResourceManager_loadResource` 加载 → 取 metadata/base/chara/motion dict
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

### Gap2 两处 EmoteObject 层偏差(强断言, 已交叉核实)

1. **EmoteObject 缺 ResourceManager* 字段(+0)**。本地 ctor `_engine = new EmoteEngine(std::move(rm))` 把 rm **by-value 直传 EmoteEngine→Player**, EmoteObject 自己不持有 RM。二进制 RM 是 EmoteObject 拥有的独立 232B 堆对象, EmoteObject dtor 显式 `sub_6A8B94 + delete`。本地把 RM 所有权下沉了一层。**核实**: grep `_rm`/`ResourceManager` in EmoteObject — 仅 ctor 形参, 无成员字段。

2. **EmoteObject `_module` 是单 `tTJSVariant`, 二进制 +16 是 `vector<tTJSVariant*>`**。二进制 D3DEmotePlayer_load(0x52FDD4) 把 a2 个 PSB 引用全 push 进 vector(支持多 PSB 合成); 本地只存最后一个。**核实**: EmotePlayer.h:84 `tTJSVariant _module;` 单值。

### 已对齐项(无需动)

- D3DEmotePlayer 双 EmoteObject 槽(_primaryObj@+24 / _secondaryObj@+32) ✅, 次槽恒 null ✅
- EmoteObject._engine 裸指针 + 手动 new/delete ✅ (CLAUDE.md 规则)
- 析构序 EmoteObject::~ = delete _engine (本地无 RM/vector 故只一步; 二进制三步)

## getVariable ↔ setVariable 桥(Gap1, 与 Gap2 解耦)

- `D3DEmotePlayer_getVariable@0x5305D4`: `*(*(this+24)+8)`=EmoteEngine → `Player_getVariable_wrapper@0x533E1C(EmoteEngine,...)` 读 `*(EmoteEngine+1064)=Player`, 在 **Player** HM1@+264/HM4@+1240 上 scope-scan 级联查找
- `D3DEmotePlayer_setVariable@0x5305C8`: 同样拿 EmoteEngine → `Player_setVariable@0x671228(EmoteEngine,...)` 写 **EmoteEngine** HM6@+1384/HM7@+1440 + deque case4-8(读 deque 内部 _M_start 在 +256/+336/+416/+576/+656, NOT 独立 deque——是 deque#4..#9 的 +16 header)
- 桥 = progress 末尾 bind-loop(EmoteEngine_progress@0x67D01C 0x67d3a4): `for(i=*(engine+1456); i; i=*i)` 遍历 HM7 insertion-order 链:
  - `sub_67C560(engine, &i.key, &i.value)` = var-track 加权级联(读 deque#10@+1040 的 sub_68C134 节点, 累加 `value += elem[48]*node[72]`)
  - `v68 = sub_67C6B0(engine, &i.key)` = negate-flag resolver(查 HM@+824, 未命中查 HM@+880, 再 indexOf 扫 vector@+800; 命中插入对应表返回 1=negate)
  - `Player_bindParameterValue_writesHM1_HM2(*(engine+1064), &i.key, 0, negate?-value:value)` ← 把 EmoteEngine HM7 的物理输出写回 **Player** HM1/HM2, 使 getVariable 能读到
- **本地状态**: bind-loop 是 stub(EmoteEngine.cpp:1927 空遍历 `_labelToValueHM7`)。getVariable↔setVariable 当前靠 D3DEmotePlayer::setVariable **双写**(同时调 engine().setVariable + player().setVariable, EmotePlayer.cpp:342-343)保持 round-trip。这是 PORT FORK shim, 移除前提 = 实装 sub_67C560/67C6B0/Player_bindParameterValue 三函数 + bind-loop。

## 1456 字段真相(纠正既有误判)

EmoteEngine+1456 = HM7(@+1440 unordered_map<ttstr,double>) 的 `_M_before_begin._M_nxt`(libstdc++ insertion-order node 链头), **不是**独立 _bindListHead 字段。dtor(0x67F4B8) `for(v3=*(a1+1456); v3; v3=*v3)` 遍历释放每节点 key ttstr。本地正确建模为直接遍历 typed map(EmoteEngine.cpp:1927), 接受 libc++ 无 insertion-order 链的 PLATFORM_BOUNDARY。详见 [[emoteengine-1496b-layout]] "_bindListHead 是伪字段" 纠正。

## 关键引用
- EmoteEngine 字段表: [[emoteengine-1496b-layout]]
- Player 1384B 字段表: [[player-1384b-flat-spec]]
- Player 4 HM 容器: [[player-container-layout]]
- EmotePlayer(24B 退化壳, 区别于 D3DEmotePlayer): [[emoteplayer-24b-shell]]
