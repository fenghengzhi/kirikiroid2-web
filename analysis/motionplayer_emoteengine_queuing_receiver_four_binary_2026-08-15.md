# MotionPlayer `EmoteEngine::queuing` receiver/字段命名四参考复原（2026-08-15）

## 1. 结论

Motion.EmotePlayer 的 `queuing` 与 D3DEmotePlayer 的历史拼写 `queing` 共享同一个
`EmoteEngine` byte。它是 controller enqueue 的 append/replace policy：

- false：有时序工作的 setter 清除/替换已有 queue；
- true：setter 把新 keyframe 追加到现有 queue；
- 属性 setter 忽略传入 Boolean，只写常数一，因此脚本只能开启 append mode；
- Engine constructor 初始化为 false；当前公开 surface 没有恢复 false 的 setter；
- inner `Player::_queuing` 是另一项 frame-cursor/tick 状态，constructor 默认 true，
  不与 Engine byte alias。

本地 Engine 字段旧名 `_emoteAnimatorFlag` 暗示了一个不存在的 animator owner/state。
四端 reader/writer 显示它只是 `queuing` byte；本轮恢复为 `_queuing`，并更新所有 Engine、
Motion wrapper、D3D wrapper 和 controller call sites。

## 2. Motion.EmotePlayer 访问器

| 目标 | Engine byte | getter | setter | load / store |
|---|---:|---:|---:|---:|
| Android arm64 | `+1161` | `0x67F344` | `0x67F34C` | `0x67F348 / 0x67F350` |
| Android armv7 | `+593` | `0x5620F6` | `0x5620FC` | `0x5620FA / 0x5620FE` |
| iOS arm64 | `+793` | `0x1001B61E0` | `0x1001B61E8` | `0x1001B61E4 / 0x1001B61EC` |
| iOS armv7 | `+409` | `0x1B5FCE` | `0x1B5FD4` | `0x1B5FD2 / 0x1B5FD6` |

getter 是 unsigned-byte load。setter 完全不读取 typed Boolean 参数和旧值，直接 store 1。
因此 ncbind 把 false/Void/true 转成任意 bool 后，native callback 都产生相同结果；重复 true
也不会短路。

## 3. D3DEmotePlayer `queing` 对象链

| 目标 | getter | setter | shell primary | EmoteObject Engine |
|---|---:|---:|---:|---:|
| Android arm64 | `0x5304AC` | `0x5304BC` | `+24` | `+8` |
| Android armv7 | `0x494A58` | `0x494A62` | `+16` | `+4` |
| iOS arm64 | `0x100232E84` | `0x100232E94` | `+24` | `+8` |
| iOS armv7 | `0x231AE4` | `0x231AEE` | `+16` | `+4` |

共同链为：

```text
D3D shell -> primary EmoteObject -> EmoteEngine -> queuing byte
```

getter/read 和 setter/constant-one store 都没有 shell-primary 或 Engine null guard。未 load、
clear 后直接访问继续遵守 native 的已加载前置条件，不引入 shell-local cache 或自定义错误。
D3D member table 只注册错误拼写 `queing`；正确拼写 `queuing` 属于 Motion.EmotePlayer。

## 4. constructor 与相邻 byte

四端 Engine ctor 入口为
`0x67B76C / 0x560948 / 0x1001B7FB0 / 0x1B7788`。queuing 位于
selectorEnabled 与 dirty 之间：

```text
directEdit = 0
selectorEnabled = 1
queuing = 0
dirty = 0 initially, then ctor body seeds it to 1
debugPrint = 0
```

相邻 word/vector store 只是 compiler grouping；它不表示 selector/queuing/dirty/debug
共用一个 int field。queuing 的 source field 是单独 bool/byte，normal destruction 无动作。

## 5. controller append/replace 数据流

fresh `EmoteEngine_setVariable_guess` 四端入口：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x66E608` | `0x559D84` | `0x1001ACDBC` | `0x1AC5F4` |

HM6 category 4、5、6、7、8 的 enqueue/setTarget 调用把 Engine byte 原样传为 append
实参。代表性 decompile load 使用 `Engine+1161/+593/+793/+409`。同一个 policy 还进入：

- constructor 的 position/scale/angle/color duration-zero seeds；
- Motion.EmotePlayer/D3D root coord、scale、rotation、color setters；
- bust/hair/parts outer-force setters；
- Blink、Eyebrow、Mouth、Transition、Selector controller enqueue；
- timeline variable track、timeline blend 和 direct variable setters。

fresh指令进一步确认该byte只在对应category index/label/gate通过、即将调用setter时读取；
gate false、mouth first-label直写、label mismatch和default type不读取它，也不执行三个
double-to-float窄化。完整顺序见
`analysis/motionplayer_set_variable_router_double_ease_integer_conversion_four_binary_2026-08-15.md`。

duration<=0 的 direct-controller setter 会立即 clear/idling/copy current value，该分支本身
不需要 append decision；caller 仍按 native ABI 传入 queuing byte。duration>0 且 queuing
false 时先 clear queue/state，再 push；true 时保留旧工作并 push。

把字段命名为 animator flag 会掩盖这一跨 controller 的统一 policy，也容易被误解为
是否“存在 animator”或是否“正在 animate”。四端没有这样的 Boolean consumer。

## 6. 与 Player `_queuing` 的隔离

inner Player 的公开 `queuing` 由 Player 自己的 byte 支撑，constructor 默认 true。它参与
frameTick/tickCount、skip/seek 和 motion frame-cursor transition，不作为 Engine controller
append 参数。加载一个 D3D player 后可同时观察：

```text
D3D/Engine queing == false
inner Player queuing == true
```

给 D3D `queing=false` 只会把 Engine `_queuing` 置 true；inner Player 值保持 true。
给 Motion.Player 的 `queuing` setter 写值也不会修改 Engine byte。两个字段可同名，因为
receiver/class 不同，但不能通过 wrapper 自动同步。

## 7. 本地结果

- `EmoteEngine.h`：`_emoteAnimatorFlag` -> `_queuing`；
- `EmoteEngine.cpp`：constructor seed、generic target、HM6 router、timeline 与 blend 路径统一
  传 `_queuing`；
- `EmotePlayer.h/.cpp`：Motion 与 D3D one-way property、root/outer-force setters 统一读取
  Engine `_queuing`；
- unit translation unit：constructor default 与 one-way property assertion 使用语义名；
- `Player::_queuing` 和所有 Player frame-state 代码不变。

地址与物理 offset 仅保存在本文和 recovery IDB；compiled source 不再保留
`_emoteAnimatorFlag` 这一旧并行 animator 模型名称。
