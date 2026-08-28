# Player updateLayers dispatcher 四参考二进制联合恢复

日期：2026-08-27

## 1. 本 slice 边界

本报告闭合 `Player::updateLayers` 的dispatcher级状态机：入口flag、phase1/phase2边界、十个phase3
调用的严格顺序、cleanup ranges与最终Player bytes。phase1/phase2内部每个timeline/矩阵分支和十个
phase3 helper各自的大状态机仍由独立slice负责；不能因root dispatcher已闭合而整体提前完成。

四端共同证明，参考实现没有MotionTrace、logo-path materialization、变量遍历日志或per-node日志。
当前本地把这些sidecar插在入口和phase2/phase3之间，既增加allocation/dispatch/throw frontier，
又是compiled `MotionNode::index`的一个消费者。本轮在完整证据固化后删除该sidecar。

## 2. 四端root映射

| 平台 | updateLayers | 完整指令 | direct callers |
|---|---:|---:|---|
| Android arm64 | `0x6B871C` | 685 | progress、calcView、motion child、内部bridge，共5处 |
| Android armv7 | `0x5856E0` | 764 | progress、calcView、motion child、内部bridge，共4处 |
| iOS arm64 | `0x10010E544` | 719 | progress、calcView、motion child、内部bridge，共4处 |
| iOS armv7 | `0x10BE5C` | 821 | progress、calcView、motion child、内部bridge，共4处 |

四个root均fresh decompile并读取完整disassembly，所有cursor `done=true`。Android arm64多一个
独立Engine/内部progress bridge caller，是编译/调用站点差异，不改变body。

## 3. 十个phase3调用的严格顺序

| phase | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| camera constraint | `0x6B93E0` | `0x586228` | `0x10010F22C` | `0x10CA04` |
| vertex computation | `0x6B98D0` | `0x5866F8` | `0x10010F6AC` | `0x10CE30` |
| visibility | `0x6BACBC` | `0x58762C` | `0x1001107BC` | `0x10DF88` |
| camera node | `0x6BAE08` | `0x587748` | `0x1001108C4` | `0x10E048` |
| shape AABB | `0x6BB0A0` | `0x587978` | `0x100110B20` | `0x10E274` |
| shape geometry | `0x6BB274` | `0x587BAC` | `0x100110CE0` | `0x10E46C` |
| motion sub-node | `0x6BB4A0` | `0x587E00` | `0x100110EEC` | `0x10E68C` |
| particle emitter | `0x6BC1B0` | `0x588820` | `0x100111A6C` | `0x10F2CC` |
| particle system | `0x6BC4BC` | `0x588A48` | `0x100111D08` | `0x10F51C` |
| anchor node | `0x6BD908` | `0x589C00` | `0x100113024` | `0x110908` |

四端调用次序完全相同。没有条件跳过某个helper的root gate；各helper自行处理preview/type/count等
条件。motion child会递归调用child updateLayers，形成depth-first嵌套，但parent root恢复后仍从
particle emitter继续。

## 4. 共同dispatcher伪代码

```text
updateLayers():
    needsInternalAssignImages = false

    // root and player phase-1 state
    runPhase1PreLoop(clampedEvaluationTime)

    // physical node main loop, including inheritance/evaluation
    runPhase2MainLoop(clampedEvaluationTime)

    for every node including synthetic root:
        if queuing:
            node.deltaPosition = (0, 0, 0)
        else:
            node.deltaPosition = node.accumulatedPosition
                               - node.savedPreviousPosition

    cameraConstraintDirty = false

    updateCameraConstraint()
    updateVertexComputation()
    updateVisibility()
    updateCameraNode()
    updateShapeAABB()
    updateShapeGeometry()
    updateMotionSubNodes()
    updateParticleEmitters()
    updateParticleSystems()
    updateAnchorNodes()

    for every non-root node in physical deque order:
        node.flags = 0
        node.accumulated.dirty = false

    for every parameter vector entry:
        entry.mode = 0

    noUpdateYet = false
    queuing = false
```

本地把编译器内联在root的大phase1/phase2拆到两个C++ helper，属于portable source组织；外部
observable store/call顺序必须保持上述结构。

## 5. 入口flag与root边界

四端第一项持久store都是`needsInternalAssignImages=false`。没有trace callback、logger状态读取或
motion path转换先于它。随后直接使用constructor保证存在的root node；empty deque无recoverable
guard。

phase1会处理camera velocity/damping、root accumulated状态和variable tracks；phase2按non-root
physical order合并parent/inheritance并更新node accumulated状态。紧随其后的root内联pass覆盖全部node
（包括synthetic root）：queuing时清三轴delta，否则用current accumulated减phase1保存的位置。该pass
发生在cameraConstraintDirty clear和camera-constraint helper之前，因此constraint本frame追加的位移不
属于本次delta。

## 6. phase2到phase3之间没有side effect

四端在main loop与all-node delta-position store-only pass结束后立即：

```text
cameraConstraintDirty = false
call phase3 helper #1
```

没有：

- matchedMotionPath或Variant/string转换；
- `_evalResultValues` unordered map遍历；
- full node deque日志遍历；
- LOGGER获取/格式化/写入；
- `node.index`读取；
- Web trace recorder。

本地修改前恰好在这里插入上述行为。除明显性能差异外，allocation失败、logger异常、re-entrant
object/string conversion都可阻止phase3与cleanup，形成参考实现不存在的partial state，必须移除。

## 7. cleanup ranges

十个phase3正常返回后，四端先遍历全部non-root nodes，逐项写：

```text
flags = 0
accumulated.dirty = false
```

root不清这两个字段。循环使用deque物理order；ABI只改变iterator/count公式。随后遍历完整
parameter-entry vector，把每个record的`mode`写0，保留id/range/division/value与selected/node
pointers。

最后顺序固定为：

```text
noUpdateYet = false
queuing = false
```

任一phase3 helper抛出时，cleanup完全不运行；之前phase stores保留，node flags、parameter modes、
两个Player bytes仍是进入本次update时的live值。cleanup loops自身没有callback，正常内存store
不会抛。

## 8. ABI差异

Android/libstdc++ root用deque iterator-difference公式求size；该公式包含标准的
`nodeDifference-1`项，不能误判为漏掉末节点。iOS/libc++ root读取保存的deque size。64位
parameter record stride 56，Android armv7 48，iOS armv7 44；cleanup只写各stride末部mode字段。

iOS armv7 821条较多指令来自32-bit VFP/SjLj与展开的矩阵main loop；Android/iOS64使用不同
SIMD/conditional select形状。phase顺序和cleanup语义没有平台分叉。

## 9. 修改前本地对照

匹配：

- `PlayerUpdateLayers.cpp`入口先清needsInternalAssignImages；
- phase1/phase2调用顺序；
- all-node delta-position pass位于phase2与camera dirty clear之间；
- cameraConstraintDirty clear位置；
- 十个phase3 helper顺序；
- non-root flags/dirty cleanup；
-完整parameter mode cleanup；
- noUpdateYet/queuing最终store。

不匹配：

1. 入口`motionTraceRecordUpdatePlayer(this)`；
2. `MotionTraceWeb.h`依赖；
3. phase1前motion path materialization；
4. phase2后root、variable map、全部node的logo trace；
5. trace读取非native `MotionNode::index`；
6. 把root内联的all-node delta-position pass误放进vertex helper尾部并排除root。

本报告完成后删除整组sidecar，而不是把它挪到另一个phase frontier。

## 10. 验证与剩余范围

修改后执行coverage strict列、duplicate ID、`git diff --check`与可用脚本语法检查。当前环境仍
缺CMake/Ninja/Emscripten正式工具链，不能声称unit/Web build通过。

root dispatcher闭合后，剩余重点是phase1/phase2 internals、geometry和完整
motion-sub/particle大状态机。本报告当时留下的其它 `node.index` sites 随后已由 motion-sub、
shape-AABB 和 calcBounds 各自的 fresh 四端完整审计关闭，字段与 build/root 赋值现已删除。

## 11. 证据后实施结果

完成四端root完整证据、十phase地址表、共同dispatcher伪代码和修改前逐项对照后，已经从
`PlayerUpdateLayers.cpp`删除入口MotionTrace、`MotionTraceWeb.h`依赖、motion-path materialization、
root/variable/node logo日志与`node.index`读取；同时从`PlayerUpdateLayerEval.cpp`删除仅服务于同一
phase2诊断旁路的timeline-state投影、frame-slot二次读取、parent-index/walk-step采样和累积状态日志。
这些读取全部位于四端共同的phase2 -> camera-dirty store边界内，但四端root均无对应调用、分配或
格式化。后续C29 root/helper审计又恢复了phase2后的all-node delta-position pass。当前dispatcher为
入口flag -> phase1/2 -> all-node delta -> camera-dirty clear ->十phase -> cleanup，
phase2 helper也不再为参考实现不存在的日志制造额外Variant访问与异常前沿。

四库已命名、注释、bookmark并保存updateLayers root IDB。其它 phase3 helper 仍按独立 slice
继续审计；diagnostic node-index 链已由后续证据闭合，不再是 compiled source 结构。
