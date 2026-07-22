---
name: player-local-vs-binary-audit
description: motion::Player 本地类(Player.h:116) vs 二进制 1384B 全面布局审计结论 — 字段顺序按C++源码非偏移/全std容器/无vtable/RAII ctor-dtor; ~35%对齐, Top5未对齐项
metadata:
  type: project
---

# motion::Player 本地 vs 二进制 布局审计 (2026-05-31)

本地 `class Player` @ Player.h:116, ctor PlayerCore.cpp:211, dtor PlayerCore.cpp:253。
二进制 1384B, ctor 0x6CED30, dtor 0x6CFADC。权威字段表见 [[player-1384b-flat-spec]]。

## 三大结构性偏差(决定整体只能 ~35% 对齐)
1. **字段顺序 = C++ 源码声明序,非二进制偏移序**。本地 ~63 个成员从 `_resourceManagerNative`(819) 到 `_findMotionContextVariant`(1145) 按功能聚类排，编译器布局与二进制 1384B 扁平偏移无任何映射。本地对象 sizeof ≠ 1384(含 std 容器控制块/RAII 成员)。二进制按偏移直接访问字段，本地不可能字节兼容。
2. **全 std:: 容器替代 KiriKiri 内联容器**。本地 9 个 unordered_map + 11 vector + 4 deque + 2 map + array + 多 shared_ptr，全部独立堆分配；二进制是 4 个内联 KiriKiri hashmap(+264/+320/+1184/+1240) + 2 内联 deque(+184/+1296) + 2 裸动态数组(+384 stride56 / +936 stride44),全部内联在 1384B 内,无 unique/shared_ptr。详见 [[player-container-layout]]。
3. **ctor/dtor 机制**: 本地 ctor=成员初始化器(PlayerCore.cpp:211, body 只 emplace root node + bindPlayer), dtor=手动 delete _renderSeparateLayerAdaptor 后靠 RAII; 二进制 21 步手写 init + 有序 teardown(4 hashmap 链表 operator delete / deque destroy / ttstr / dispatch Release)。详见 [[player-ctor-dtor-lifecycle]]。

## ✅ 对齐项
- 无 vtable / 无 virtual / 无基类(本地 `class Player {` 无继承; 二进制 ctor `*a1=a1` 占位,NCB 走 callback)。本地 ~Player 非 virtual ✅。
- 生命周期 raw ptr: EmoteEngine `_player = new Player` (EmoteEngine.cpp:40) + `delete _player`(:144); 子 Player `new Player`(NodeTree.cpp:238, PlayerUpdateParticles.cpp:447) — 对齐二进制 new(0x568)+手动 delete,无 unique_ptr ✅。
- 标量默认值对齐: _priorDraw=false（Player+1096 bool） / _outsideFactor=1.5（+1160） / _speedMul=1.0（+1168） / _meshDivisionRatio=1.0（+1176） / _pixelateDivision=100 / _colorWeightPacked=0xFF808080 / bounds ±DBL_MAX。
- ctor 末尾 push root MotionNode 进 deque ✅(本地 _nodes.emplace_back())。

## Top5 未对齐
1. 字段顺序漂移(源码序 vs 偏移序) — 根因,无法局部修
2. 4 KiriKiri hashmap → 9 unordered_map (容器选型 ❌)
3. 2 inline deque + 2 裸数组 → std::deque/std::vector (❌)
4. ctor/dtor 手写序列 → RAII (❌, 容器选型下游)
5. ctor 签名 `(ResourceManager rm, Player* parent)` vs 二进制 `(iTJSDispatch2* resourceManager)` 单参 — 第一参 native 对象非 dispatch, 多 parent 参

## 已知字段碰撞/phantom
见 [[player-field-collisions]]: +480/+1120/+481/+1099/+1156 五处碰撞；相机字段的当前问题是本地 `_cameraFov=0.2` 与 NCB 使用的 `_cameraFOV=60.0` 双轨，而不是把 +1100 字节解释为 cameraFOV。
本地额外 port-invented 字段(无二进制偏移): _disabledSelectorTargets/_pendingEvents/_perNodeEvalData/_layerIdsByName 等渲染宿主状态。

参见 [[player-1384b-flat-spec]] [[player-container-layout]] [[player-ctor-dtor-lifecycle]] [[player-field-collisions]]
