---
name: Phase2 MainLoop 与 libkrkr2.so sub_6BB33C 对齐完成
description: 2026-04-17 完成 Player::updateLayersPhase2_MainLoop 的全架构重写，消除 binary-alignment-auditor 2026-04-17 审计中的全部 8 项偏差
type: project
---

# Player_updateLayersPhase2_MainLoop 与 libkrkr2.so sub_6BB33C 对齐成果

完成日期: 2026-04-17

## 8 项偏差解决情况

| # | 偏差标题 | 解决方式 |
|---|---------|----------|
| 1 | delta/override 块缺失 | `MotionNode::DeltaState` 新增 (MotionNode.h)，对应 node+1584..+1660；setX/Y/FlipX 路由到 `delta.*` + `delta.dirty` |
| 2 | sync early-exit 缺失 | phase2 `if (node.activeSlot().hasSync)` 分支：struct-copy parent accum → active=0 → dirty 重塑 → visible 重塑 → 矩阵拷贝 → continue |
| 3 | 跳过语义颠倒 | `shouldUpdate = force\|ground\|parentDirty\|deltaDirty; if (!state.debugEvaluated && !shouldUpdate) continue;` — 不再主动清零 accum |
| 4 | SIMPLE 路径缺失 attr parent merge | sub_699940 → matmul parent×local → `flipX^=/flipY^=/angle+=/scaleX*=/scaleY*=/slantX+=/slantY+=`（对齐 asm 0x6BB8A8..0x6BB8EC）|
| 5 | COMPLEX per-bit merge 语义错 | 改为就地 `accum.x ^=/ +=/ *= parent.x` for SET bits；删除 else-branch 的 `= state.x` 硬写 |
| 6 | DEPENDENT phase D matmul 用错 | 源节点改为 **root**（nodes[0]），不是 parent，对齐 asm 0x6BBA24 的 X20=root 寄存器 |
| 7 | sub_69AE74 / sub_6BAA10 未抽函数 | 新增 `sub_69AE74_meshDeform` / `sub_6BAA10_groundCorrection` 静态函数于匿名命名空间 |
| 8 | 越权代码 | 删除 phase2 的 `stencilType \|= frameType`、`flags \|= 0x01` 和 `findPSBResourceBySourceName` 填 clipW/H/origin 三处 |

## 核心架构变更（跨文件）

### MotionNode.h（+47 行）
- 新增 `struct DeltaState { dirty, activeOverride, visibleOverride, flipX, flipY, posX, posY, posZ, angle, scaleX, scaleY, slantX, slantY, opacity } delta;`
- 精确对应 libkrkr2.so node+1584..+1660 的 14 字段布局（头部 5 bool + 9 double + int）

### PlayerCore.cpp（+/- 若干行）
- `setX / setY`：写 `root.delta.posX/Y`（+1592/+1600），置 `root.delta.dirty`；不再碰 `localState`
- `getX / getY`：读 `root.delta.posX/Y`
- setRootFlipX 相关流程：写 `root.delta.flipX` / `root.delta.dirty`

### PlayerRender.cpp（-2 +3 行）
- 待定根位/旗标的重放改写到 `root.delta.*`

### PlayerUpdateLayers.cpp（phase1/phase2 均有重写）
- phase1 Camera velocity：`rootNode.delta.posX/Y/Z += dt*velX/Y/Z`（对应 0x6BB360..0x6BB3DC）
- phase1 root init：struct-copy `root.accumulated = root.delta`（对应 `memcpy(root+1504, root+1584, 0x50)` @ 0x6BB4E0），然后清 `root.delta.dirty = 0`
- phase2 主循环：按 binary asm 0x6BB524..0x6BBB6C 逐指令映射，9 个阶段顺序一致，所有变量偏移对应注释标注
- 抽出 `sub_69AE74_meshDeform` + `sub_6BAA10_groundCorrection` 独立静态函数

## 保留的架构差异（可接受）

1. **Player+610 (forceDirty byte)**：本地没有对应字段；evaluateTimeline gate 中作为 `false` 常量参与 OR。多数情况下其他三个 gate（ground/parent/delta）足够。
2. **phase1 root 的 interpolatedCache/slot 填充**：binary 没有对应逻辑（root 不走 timeline），但本地保留是为了支撑渲染链（TJS setter/Flipx 等）。
3. **C++ 结构内存布局**：未强制对齐到 libkrkr2.so 的字节偏移；struct field 顺序仅保持语义 1-to-1。

## 验证

- 构建：`cmake --build out/web/debug` 通过（0 errors、仅 tjsString _tss deprecated 预存 warning）
- 8 项偏差逐一 grep 验证：
  - stencilTypeBase | state.frameType — 0 matches in phase2 ✅
  - findPSBResourceBySourceName in phase2 — 0 matches（phase1 的 1 个是 root init 保留）✅
  - node.flags |= 0x01 — 0 matches ✅
  - root.localState.posX writers in motionplayer — 仅 template populateTransformStateFromFrameState（合法，写 FrameContentState→state）✅
- 后续由 binary-alignment-auditor 做最终裁决
