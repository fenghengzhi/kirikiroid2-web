---
name: Player phase2 MainLoop ↔ libkrkr2.so sub_6BB33C 映射
description: 本地 cpp/plugins/motionplayer/PlayerUpdateLayers.cpp::Player::updateLayersPhase2_MainLoop 与 libkrkr2.so Player_updateLayers @ 0x6BB33C 的对应关系、已知偏离点
type: project
---

## 函数映射
- 本地: `cpp/plugins/motionplayer/PlayerUpdateLayers.cpp:419-982` → `Player::updateLayersPhase2_MainLoop(double currentTime)`
- libkrkr2.so: `sub_6BB33C` @ `0x6BB33C` 的 **phase2 主循环段**（0x6BB524..0x6BBB6C）
- 注意：libkrkr2.so 中 phase2 不是独立函数，它只是 `Player_updateLayers` 内部从 v16=1 开始的 while 循环；本地则把它拆成独立函数。

**原因：** 本地把 pre-loop/main-loop/post-loop 拆分方法便于测试，但要警惕"本地函数边界"不等同于 libkrkr2.so 的基本块边界。

**如何应用：** 对 phase2 做对比时必须把 libkrkr2.so 0x6BB524..0x6BBB6C 整段作为一个单元来看；其中依赖的"phase1 root 初始化"已移到本地 updateLayersPhase1_PreLoop（cpp 238..417），phase4 清理则在 phase3 末尾/单独。

## 本地实现中已识别的结构性偏离
1. **`populateTransformStateFromFrameState(node.accumulated, state)` 被调用**（行 603），它在非 sync 路径之前就把 state 直接写入 `node.accumulated`，语义上等价于 libkrkr2.so 中"accum 的 pre-merge 初值来自当前帧 evaluate 结果"；但 libkrkr2.so 是先读 `node+1504..` 作为 accum 与 `node+1584..` 作为 delta 做 vmul/vadd，二者角色完全不同。
2. **本地 `node.localState.*` ≈ libkrkr2.so `node+1584..` delta block**，但本地同时把 delta 的内容 populate 到 `node.accumulated`，导致概念混淆（accum 同时扮演 live state 和 merged accum）。
3. **LABEL_19 (sync) 分支在本地不存在** — 本地代码只有 `!state.visible → skip`，没有实现 `*(_BYTE *)(v22 + 536*activeSlot + 344)` 即 `clipSlot.hasSync` 路径（libkrkr2.so 0x6BB604..0x6BB790）。
4. **pre-matrix delta merge 的顺序/对象颠倒**：libkrkr2.so 是 `accum.scaleX = delta.scaleX * accum.scaleX`，本地是 `node.accumulated.scaleX *= node.localState.scaleX`。若 `populateTransformStateFromFrameState` 把 state 写入 accum，则 accum=state；本地做 accum*=localState 相当于 state*delta，顺序反了（delta 在左 vs state 在右）。对乘法交换律不变，但对加法同样（交换律成立）；**对于 opacity 除以 255 的顺序：libkrkr2.so `int32 *(node+1576) = *(node+1656) * *(node+1576) / 255`，本地是 `accum.opacity = accum.opacity * localState.opacity / 255`——如果 accum 已被 populate 为 state，这等价。**
5. **COMPLEX 路径中 per-property 继承的"base 值"不同**：
   - libkrkr2.so 在进入 0x6BB83C 时，`v22+1507 (flipX)` 已被 `^= v22+1587` 异或过（0x6BB668），即 accum 已包含 state^delta；之后的 `if (v46 & 4) accum.flipX ^= parent.flipX` 是在 accum 基础上叠加 parent。
   - 本地 COMPLEX 路径（行 879-892）不基于 accum，而是 `accum.flipX = state.flipX ^ parent.flipX`——把 accum 重新赋为 state，不保留 pre-merge 的贡献。**这在 delta 非零时会丢失 delta 贡献**。
6. **SIMPLE 路径也不保留 scaleX/angle 的 parent×accum 语义**：libkrkr2.so 0x6BB8C8 `accum.angle += parent.angle`、0x6BB8DC `accum.scaleX = parent.scaleX * accum.scaleX`（vmulq），本地 SIMPLE 路径（行 865-875）**只做矩阵乘，完全没有 flipX/angle/scale/slant 的 parent-merge 标量累加**！
7. **Mesh 分支 (sub_69AE74) 实现被"展开"**：libkrkr2.so 是单个 `if (parent+2000) sub_69AE74(parent, node)`，本地把 sub_69AE74 的内部逻辑内联（行 656-742），这不是一对一对齐。
8. **parent lookup 的 index 基础**：本地用 `node.parentIndex` 走 `nodes[]` 数组；libkrkr2.so 用 deque 内部索引 `*(_QWORD*)(node+36)` + 偏移 `v21`。需要确认本地 parentIndex 准确反映 PSB 中声明的 parent。
9. **"phase2 开头没有清 +1584"**：libkrkr2.so 在进入 per-node 代码时 `*(_BYTE *)(v22 + 1584) = 0`（0x6BB5F8）清 delta dirty；本地没有对应 `node.localState.dirty = false`。
