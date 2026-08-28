# Player node parameter pointer消费者四参考二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的边界

本报告只闭合两个update phase中的 `MotionParameterEntry*` 选择与重读语义：

1. type-3 motion sub-node pass的 replay `mode`与两处parent-time读取；
2. type-6 particle emitter初始化timer时的parent-time读取。

两个phase的其余角度、child播放、矩阵、粒子offset等大状态机不因本报告而宣称全部闭合；它们
保留在后续各自完整phase slice。这里之所以fresh读取完整函数，是为了在删除
`parameterizeIndex`/resolver语义前满足四端完整map/decompile/disassembly规则，并核对每一个
parameter pointer use-site，而不是只截取一条指令猜测。

## 2. 四端函数映射

### 2.1 type-3 motion sub-node

| 平台 | 函数 | 完整指令 | updateLayers caller |
|---|---:|---:|---:|
| Android arm64 | `0x6BB4A0` | 833 | `0x6B871C` call `0x6B9070` |
| Android armv7 | `0x587E00` | 760 | `0x5856E0` call `0x586014` |
| iOS arm64 | `0x100110EEC` | 709 | `0x10010E544` call `0x10010EFB4` |
| iOS armv7 | `0x10E68C` | 921 | `0x10BE5C` call `0x10C83A` |

### 2.2 type-6 particle emitter

| 平台 | 函数 | 完整指令 | updateLayers caller |
|---|---:|---:|---:|
| Android arm64 | `0x6BC1B0` | 193 | `0x6B871C` call `0x6B9078` |
| Android armv7 | `0x588820` | 172 | `0x5856E0` call `0x58601A` |
| iOS arm64 | `0x100111A6C` | 167 | `0x10010E544` call `0x10010EFBC` |
| iOS armv7 | `0x10F2CC` | 178 | `0x10BE5C` call `0x10C840` |

八个phase均fresh decompile；完整disassembly覆盖motion-sub 833/760/709/921与emitter
193/172/167/178条指令，所有cursor `done=true`。每个函数都只有对应updateLayers root一个
code xref；四端phase order均为motion-sub -> particle-emitter -> particle-system。

## 3. native pointer与字段坐标

| 平台 | node parameter pointer | Player selected pointer | entry value | entry mode |
|---|---:|---:|---:|---:|
| Android arm64 | node `+0x8` | Player `+0x178` | entry `+0x28` | entry `+0x30` |
| Android armv7 | node `+0x4` | Player `+0xF8` | entry `+0x20` | entry `+0x28` |
| iOS arm64 | node `+0x8` | Player `+0x120` | entry `+0x28` | entry `+0x30` |
| iOS armv7 | node `+0x4` | Player `+0xC8` | entry `+0x20` | entry `+0x28` |

这些坐标与parameter/build-node reports恢复的自然record stride和pointer-only node producer一致。
八个函数都没有加载一个node parameter index、没有乘record stride、没有访问parameter vector
begin/end，也没有range error string/xref。

## 4. motion-sub的三种选择不是一个resolver

### 4.1 replay mode

每个type-3 node一进入phase，四端共同执行：

```text
modeEntry = node.parameterEntry
if modeEntry == null:
    modeEntry = player.selectedParameterEntry
parameterMode = modeEntry != null ? modeEntry.mode : 0
```

对应fresh load：

- Android arm64：`0x6BB5E4` node `+8`，`0x6BB5EC` Player `+0x178`，
  `0x6BB5F4` entry `+0x30`；
- Android armv7：`0x587E96` node `+4`，`0x587E9E` Player `+0xF8`，
  `0x587EA6` entry `+0x28`；
- iOS arm64：`0x100111350` node `+8`，`0x100111358` Player `+0x120`，
  `0x100111360` entry `+0x30`；
- iOS armv7：`0x10F0EC` node `+4`，`0x10F0F4` Player `+0xC8`，
  `0x10F0FC` entry `+0x28`。

因此unparameterized type-3 node并不总用mode 0。ordinary motion的Player-level
`parameterize`选择会作为replay/chain flag mode fallback；node自身parameter pointer优先。
selected pointer可能因native异常前沿悬空，这里没有防御性validity check。

### 4.2 angle crossfade parent time

进入angle offset crossfade计算时，四端重新加载 `node.parameterEntry`：

```text
nodeEntry = node.parameterEntry               // reload here
parentTime = nodeEntry != null
    ? nodeEntry.value
    : player.clampedEvaluationTime
```

这一选择不使用Player selected pointer。selected只影响开头的mode。node pointer在前面的child
play/TJS callbacks后重新读取，不复用modeEntry snapshot。

### 4.3 derivative-angle parent time

mode-3 finite-difference angle分支再次独立reload `node.parameterEntry`，并使用同一
node-value-or-clamped-time规则。它既不复用function-entry node pointer，也不复用上一处crossfade
pointer。因此一个re-entrant callback若改变/重建相关状态，native的每个use-site有自己的load
frontier；把pointer缓存到loop顶部会改变这一边界。

## 5. particle-emitter只认node pointer

type-6 emitter在进入“新target/需要重设timer”分支后执行：

```text
nodeEntry = emitter.parameterEntry
parentTime = nodeEntry != null
    ? nodeEntry.value
    : player.frameTickCount
emitter.timer = parentTime
              - activeSlot.clipStartTime
              + activeSlot.modelTimeOffset
```

四端都直接测试node pointer；没有Player selected fallback，也没有clamped-time fallback。该load
发生在active/done/src/flags/target-string stores之后，按当前位置读取live node field。

## 6. 修改前本地差异

修改前 `PlayerInternal.h::resolveNodeParameterEntry` 实现：

```text
if node.parameterEntry: return it
if node.parameterizeIndex in player.vector: return &vector[index]
if nonnegative out-of-range index: throw
return null
```

两个phase又都在loop顶部调用一次并缓存结果。这产生四项非native行为：

1. motion-sub mode在node pointer为null时没有回退Player selected pointer；
2. 两个motion-sub time use-site错误复用顶部cached pointer，而非各自reload node field；
3. emitter允许按diagnostic index重新查vector并可能抛range error；
4. compiled MotionNode多出native producer/consumer均不存在的 `parameterizeIndex`字段。

build-node slice已经让producer只写pointer，使index fallback在正常构建路径休眠；本slice的八个
fresh consumer bodies进一步证明可以并且应该完整删除该字段/helper，而不是把它保留成所谓
“健壮性fallback”。

## 7. 证据后修改计划

本报告完成后实施：

- motion-sub开头使用 `node.parameterEntry ?: _selectedParameterEntry` 只计算mode；
- 两个parent-time位置分别直接重读 `mn.parameterEntry`，null用`_clampedEvalTime`；
- emitter直接读取 `en.parameterEntry`，null用`_frameTickCount`；
- 删除 `resolveNodeParameterEntry` declaration/definition/friend；
- 删除 `MotionNode::parameterizeIndex`与剩余SNAPCHILD format argument；
- 把只验证旧helper的test改为直接验证unparameterized node pointer为null；
- 更新build-node报告，标明diagnostic parameter-index结构已由consumer audit完全清除。

`node.index`是另一组diagnostic sidecar字段，不参与parameter选择；它的motion-sub消费者只能在
本报告已完成四端完整函数证据之后删除。

## 8. 验证限制

修改后执行`git diff --check`、coverage strict-column/duplicate-id检查和可用脚本语法检查。当前
环境缺CMake/Ninja/Emscripten正式工具链，不能声称unit/Web build通过。八个phase的大状态机
除上述parameter use-sites外仍需后续完整审计；本报告不会把它们整体状态提前标成完成。

## 9. 证据后实施结果

完成上述八个fresh全函数证据、共同pointer伪代码和修改前对照后，已经实施：

- motion-sub mode恢复node-pointer优先、Player selected-pointer fallback；
- 两个motion-sub parent-time use-site各自重新加载node pointer；
- particle emitter改为直接node-pointer或frameTick；
- 删除`resolveNodeParameterEntry` declaration/definition/friend；
- 删除compiled `MotionNode::parameterizeIndex`与最后一个SNAPCHILD format argument；
- build-node producer维持pointer-only store；
- 增加selected-mode fallback回归用例，并把unparameterized-node用例改为直接验证null pointer。

同一组motion-sub完整证据还证明函数入口没有snapshot开关/path materialization，child play之后和
递归update之前也没有`SNAPPLAY`/`SNAPCHILD`字符串转换、stderr写入或node ordinal读取。因此在证据
之后从`PlayerUpdateChildMotion.cpp`删除了这两个日志块及其入口状态；parameter pointer的native
load frontier保持不变。

四库已命名、注释、bookmark两个phase并保存IDB。parameter index结构现已从compiled motionplayer
源码完全消失；motion-sub也不再消费diagnostic `node.index`。随后 update root/phase2、shape-AABB
和 calcBounds 的独立完整审计关闭其余 consumers，最终删除整个 ordinal 字段；该后续结论不改变
本报告的 parameter pointer 数据流。
