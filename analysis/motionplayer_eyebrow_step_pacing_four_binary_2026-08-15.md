# MotionPlayer Eyebrow step 节拍与提交边界四参考纵切（2026-08-15）

> **2026-08-16 unordered overshoot 勘误：** Eyebrow 的正向 target 分支使用
> `LE`，负向使用 `!(LT)`；两者都把 unordered 视为 overshoot，NaN direction 也会
> 经正向块提交 target。本文旧版“NaN 通常令两个比较失败、留在 state 2”的表述已
> 更正。完整四端条件链见
> `analysis/motionplayer_eye_eyebrow_overshoot_unordered_four_binary_2026-08-16.md`。

## 裁决

在修复 Eye/Blink 的同帧轨道 re-entry 后，本轮重新反编译四端 Eyebrow step，确认它不能
共享 Eye 的控制流：Eyebrow 每次调用最多执行一个 state 阶段。state 2 overshoot 只提交
state 1；下一次 step 才消费或判空次级轨道；再下一次才可能在 state 0 解析主命令。

本地算法原本保留了这个节拍，不需要行为修复；但实现文件把它描述成与 Eye “语义相同”，
并保留整段 `vNN/+offset/LABEL` 风格伪代码，容易诱导后续合并。本轮将 step 改写成语义化
变量与明确的单阶段分派，并增加四次连续调用回归，锁定其与 Eye 的差异。

## 四平台函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEyebrowController_step_guess` | `0x6629E0` | `0x553280` | `0x1001A38C8` | `0x1A2C56` |
| 函数大小 | `0x244` | `0x1BA` | `0x1F0` | `0x1E2` |
| `EmoteEngine_stepEyebrowControls_guess` | `0x67391C` | `0x55BE8C` | `0x1001AFB64` | `0x1AF336` |
| Engine helper 内的 step call | `0x673980` | `0x55BECA` | `0x1001AFBE4` | `0x1AF38C` |
| state-0 resolver call | `0x662A5C` | `0x5532E0` | `0x1001A3994` | `0x1A2CF8` |
| equal-segment branch | `0x662A30` | `0x5532BE` | `0x1001A3934` | `0x1A2CB0` |
| overshoot commit | `0x662B4C` | `0x5533DC` | `0x1001A3A50` | `0x1A2DCC` |

Android ARM64 的 Engine progress core 和 dt=0 state refresh 还分别在 `0x67A51C` 与
`0x6732F4` 直接调用 step。所有新增名称都带 `_guess`，不把 stripped 产物中的恢复名冒充
原始符号。

## 调用链

```text
Engine progress / dt=0 state refresh
  -> 按 deque 顺序取 {unique_ptr<EyebrowController>, label}
  -> EyebrowController_step(controller, &floatOut, floatSlice)
     -> 只执行进入调用时 trackState 对应的一个阶段
     -> *out = trackValue
  -> floatOut 提升为 double
  -> label-value map get-or-insert(label)
  -> 覆盖 mapped double
```

Eyebrow 没有 blink phase、共享 RNG 消费或 `[beginFrame,endFrame]` remap；`beginFrame` 只在
constructor 中初始化 `trackValue`，step 不再读取它。

## 单阶段状态机

### state 0：解析一个主命令

仅在 12B 主轨道非空时：

1. 读取 front 的 `endValue/duration/power`；
2. 在主命令仍位于 deque 内时调用共享 mesh resolver；resolver 清空并重建 8B 次级轨道；
3. 写 `accum=0`、resolved span、`1/duration` 与 power 原始 float bits；
4. `pop_front` 主命令；
5. `trackState = trackState + 1`，正常从 0 变 1；
6. 立即输出，不在本调用继续消费新次级轨道。

resolver 可分配多个 vector/deque。若它抛异常，主命令尚未 pop，仍留在 primary；resolver
自己的清空/部分构建按其局部异常边界保留。这个提交顺序与 Eye/Blink 相反：Eye 在调用
resolver 前已经弹出主命令。本地继续使用裸容器操作，不增加事务回滚。

### state 1：消费一个次级 segment

- 次级轨道空：只把 state 写 0，然后输出；即使 primary 已有命令，本调用也不解析；
- 非空：复制 front `{start,end}`，按端点设置 value/target/direction/state，再
  `pop_front` 恰好一条；
- `start == end`：value 写 end，state 保持 1；后续 segment 留到下一次 step；
- `end-start` 为 NaN：相等比较和 `<0` 都为假，方向取 `+1`、state 写 2。

### state 2：推进一个 ramp

```text
eased = pow(accum/span, 1/power) + invDuration*dt
delta = pow(eased,power)*span - accum
nextValue = value + direction*delta
value = nextValue

if eyebrowTrackOvershootsByNativeConditions(direction, target, nextValue):
    delta = (target-nextValue)*direction
    value = target
    state = 1

accum = previousAccum + delta
```

overshoot 时写回的不是完整移动量，也不是 clamp 后的绝对路径位置，而是
`previousAccum + correctionTerm`。例如 `accum=0/span=1/power=1/dir=+1/value=0/target=1/dt=2`
会先得到 `nextValue=2`，再把 `delta` 改为 `-1`，最终 `accum=-1`。四端一致；当前回归明确
锁定该反直觉边界。

state 2 完成后无论是否 overshoot 都直接输出。overshoot 不在同一调用读取次级轨道；不
overshoot 也只提交 accum/value。零 span、零/负 duration、零/负/NaN power、负/NaN dt
均无保护。target/next 或 direction unordered 时，四端条件码链会进入 overshoot，提交
target/state 1，并让 correction/accum 按 IEEE-754 传播 NaN。

### 其他 state

除 0/1/2 外的所有整数都不碰轨道和 curve 字段，只把现有 `trackValue` 写到 `out`。四端
均不检查 `out == nullptr`。

## 容器与生命周期

Eyebrow 复用与 Eye 相同的两个元素类型和平台 STL helper：12B command deque 与 8B pair
deque。Android libstdc++ 的块分别为 42×12=504 B 与 64×8=512 B；iOS libc++ 的容量分别
为 341 与 512 elements。out-of-line helper 已在 Eye 纵切命名为
`ValueTrack12B_popFront_guess` / `ValueTrack8B_popFront_guess`，本函数是它们的共享 caller。

关键差异不是容器类型，而是 pop 的时点和每次调用的消费上限：state 0 resolver 成功后才
pop primary；state 1 最多 pop 一条 secondary；state 2 完全不 pop。Engine wrapper 在 step
返回后才写 label map，因此 map 插入失败也不会回滚 controller 的本次单阶段提交。

## 本地与 IDB 更新

- `EmoteEyebrowController.cpp` 删除 step 的旧反编译伪代码与 `vNN/+offset` 临时名，恢复
  `state/easedPhase/previousAccum/direction/delta/nextValue/target/segment` 等语义结构；
- 修正文件头“与 Eye 语义相同”的过度概括，并在 header 明写“每次只推进一个阶段”；
- 保持 state-0 resolver-before-pop、state-1 单 segment、state-2 correction-only accum 等
  现有正确行为；
- 单元回归以四次连续 step 分别覆盖 `2 -> 1`、`1(empty) -> 0`、
  `0(primary) -> 1`、`1({x,x}) -> 1`；
- 四份 recovery IDB 添加 entry/resolver/equal/overshoot 注释和 overshoot 书签；四个 Engine
  walker 命名为 `EmoteEngine_stepEyebrowControls_guess`。

## 验证

- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过，仅有既有 `_tss`
  deprecated warning。
- `cmake --build --preset "Web Debug Build"`：通过；重新编译
  `EmoteEyebrowController.cpp`，成功链接 motionplayer 与最终 Wasm/`index.html`。输出只有
  既有 `_tss`、imagepacker attributes、pthread memory-growth、JSPI/internal-symbol 警告。
- `git diff --check`（限定本纵切文件）与行尾空白扫描：通过；
  `EmoteEyebrowController.cpp` 的 `LABEL_*`、IDA `vNN`、`sub_*`、旧产物名、绝对地址和裸
  `*((...))` 注释扫描为空。
- 四份 recovery IDB 已强制刷新 step decompile cache、保存并回读；四个 step/walker 与共享
  deque helper 名称，以及 resolver/equal/overshoot 注释均可解析。
