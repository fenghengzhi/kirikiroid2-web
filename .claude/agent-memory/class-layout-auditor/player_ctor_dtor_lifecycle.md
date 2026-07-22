---
name: player-ctor-dtor-lifecycle
description: motion::Player ctor/dtor 生命周期对齐审计 — 本地 ctor 委托成员初始化器+dtor=default(RAII), 二进制 0x6CED30 是手写扁平 init + 0x6CFADC 手写有序 teardown; 二者初始化顺序/资源释放顺序不可比
metadata:
  type: project
---

# motion::Player ctor/dtor 生命周期 (审计 2026-05-31)

## 二进制权威
- ctor = **0x6CED30** (`Player::Player(iTJSDispatch2* resourceManager)`)，21 步手写扁平初始化（见 [[player-1384b-flat-spec]] "ctor 顺序关键步骤"）。关键：4 个 KiriKiri hashmap 各自 `std_Prime_rehash_policy_M_next_bkt(ptr,10)` 内联 init；2 个 deque `Player_nodesDeque_init`/`Player_controllerDeque_init`；ctor 末尾 push 第一个 MotionNode (root) 进 deque 并写 dword_1AA40D8 4 字段；末尾 Release 两个临时 dispatch (v17/v18)。
- dtor = **0x6CFADC**，手写有序 teardown：第一条业务调用是
  `Player_purgeParameterRampMapByParent_guess@0x6CDE18`，在 +384 参数 vector
  释放前从当前/祖先 +408 multimap 清除指向这些参数项的节点；随后遍历 4 个
  hashmap、销毁 deque/ttstr、释放 +760 fnPtr 与多个 dispatch。
- **ctor 参数是 1 个** `iTJSDispatch2* resourceManager`，无 parentPlayer 参数（子 Player 的 parent 关系通过 initNodeFields/buildNodeTree 路径设置 +8 = _ownerEmotePlayer，不是 ctor 入参）。
- **无 vtable / 无 virtual**：ctor `*a1=a1` 只是占位写自身；NCB 方法走 callback 蹦床不走 vtable。dtor 非 virtual。

## 本地实现 (PlayerCore.cpp:14-29)
- `Player::Player(ResourceManager* resourceManager, Player* parentPlayer=nullptr)` — **签名多了 parentPlayer 第二参**，且第一参是 `ResourceManager*`(native C++ 对象) 而非二进制的 `iTJSDispatch2*`。
- ctor body = `return;` 注释 "body intentionally minimal" — **全部初始化委托给 C++ 成员初始化器**，由各 std 容器/ttstr 自己的 ctor 完成。root MotionNode 的 push 也依赖成员 `_nodes` 的 deque ctor（不是 ctor 显式 push）。
- **纠正 2026-07-13**：本地析构并非 `=default`。它显式先调用
  `purgeParameterRampMapLike_0x6CDE18()`，再释放 `_renderSeparateLayerAdaptor`，
  最后由成员 RAII 完成其余释放。此前“完全依赖默认析构”的记录已被源码和
  0x6CFADC/0x6CDE18 新证据共同证伪。

## 对齐结论
- ctor/dtor **仍有机制偏差**：二进制扁平手写 init/teardown 与本地成员 RAII
  尚未整体同构；但参数跨 Player 索引的关键析构前置步骤 0x6CDE18 已按原调用
  顺序显式复刻，不能再概括为本地 `=default`。
- 这是 CLAUDE.md "对象生命周期一比一" 的直接违反，但属于**容器选型偏差的下游后果**：只要本地继续用 std::deque/unordered_map/ttstr 成员，ctor/dtor 就必然是 RAII 而非手写序列。修 ctor/dtor 必须先修容器选型。
- 已确认的默认值已对齐 ✅：_priorDraw=false（Player+1096 bool） / _outsideFactor=1.5（+1160） / _speedMul=1.0（+1168） / _meshDivisionRatio=1.0（+1176） / _pixelateDivision=100 / _colorWeightPacked=0xFF808080。旧记录把 +1096 priorDraw 与 +1160 outsideFactor 串位，并把 +1100 cameraAlive 误当 cameraFOV，均已纠正。

参见 [[player-1384b-flat-spec]] [[player-container-layout]] [[player-field-collisions]]
