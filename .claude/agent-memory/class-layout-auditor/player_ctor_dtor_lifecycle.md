---
name: player-ctor-dtor-lifecycle
description: motion::Player ctor/dtor 生命周期对齐审计 — 本地 ctor 委托成员初始化器+dtor=default(RAII), 二进制 0x6CED30 是手写扁平 init + 0x6CFADC 手写有序 teardown; 二者初始化顺序/资源释放顺序不可比
metadata:
  type: project
---

# motion::Player ctor/dtor 生命周期 (审计 2026-05-31)

## 二进制权威
- ctor = **0x6CED30** (`Player::Player(iTJSDispatch2* resourceManager)`)，21 步手写扁平初始化（见 [[player-1384b-flat-spec]] "ctor 顺序关键步骤"）。关键：4 个 KiriKiri hashmap 各自 `std_Prime_rehash_policy_M_next_bkt(ptr,10)` 内联 init；2 个 deque `Player_nodesDeque_init`/`Player_controllerDeque_init`；ctor 末尾 push 第一个 MotionNode (root) 进 deque 并写 dword_1AA40D8 4 字段；末尾 Release 两个临时 dispatch (v17/v18)。
- dtor = **0x6CFADC**，手写有序 teardown：遍历 4 个 hashmap 单链表 operator delete 节点；deque 销毁 (sub_6F436C/sub_6CF678)；多个 ttstr sub_A0F778；+760 fnPtr operator delete；+768/+776/+960/+968/+976/+984 dispatch tTJSVariant_Release。
- **ctor 参数是 1 个** `iTJSDispatch2* resourceManager`，无 parentPlayer 参数（子 Player 的 parent 关系通过 initNodeFields/buildNodeTree 路径设置 +8 = _ownerEmotePlayer，不是 ctor 入参）。
- **无 vtable / 无 virtual**：ctor `*a1=a1` 只是占位写自身；NCB 方法走 callback 蹦床不走 vtable。dtor 非 virtual。

## 本地实现 (PlayerCore.cpp:14-29)
- `Player::Player(ResourceManager* resourceManager, Player* parentPlayer=nullptr)` — **签名多了 parentPlayer 第二参**，且第一参是 `ResourceManager*`(native C++ 对象) 而非二进制的 `iTJSDispatch2*`。
- ctor body = `return;` 注释 "body intentionally minimal" — **全部初始化委托给 C++ 成员初始化器**，由各 std 容器/ttstr 自己的 ctor 完成。root MotionNode 的 push 也依赖成员 `_nodes` 的 deque ctor（不是 ctor 显式 push）。
- `Player::~Player() = default;` — **编译器生成析构**，依赖成员逆序 RAII，无手写有序释放。

## 对齐结论
- ctor/dtor **机制层面 ❌ 不对齐**：二进制扁平手写 init/teardown vs 本地 RAII 成员初始化器 + =default。初始化顺序、资源释放顺序均由 C++ 成员声明顺序决定，与二进制 21 步序列无映射关系。
- 这是 CLAUDE.md "对象生命周期一比一" 的直接违反，但属于**容器选型偏差的下游后果**：只要本地继续用 std::deque/unordered_map/ttstr 成员，ctor/dtor 就必然是 RAII 而非手写序列。修 ctor/dtor 必须先修容器选型。
- 默认值已对齐 ✅：_priorDraw=1.5 / _meshDivisionRatio=1.0 / _outsideFactor=1.0 / _pixelateDivision=100 / _colorWeightPacked=0xFF808080 / _cameraFOV=60.0(但 +1100 二进制是 byte 不是 double，见 [[player-field-collisions]])。

参见 [[player-1384b-flat-spec]] [[player-container-layout]] [[player-field-collisions]]
