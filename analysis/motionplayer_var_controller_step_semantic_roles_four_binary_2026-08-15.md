# MotionPlayer `EmoteVarController_step` 数组角色与状态机四端复原（2026-08-15）

## 结论

四个当前参考二进制都把 `EmoteVarController` 的三条数组指针按以下源级角色使用：

```text
currentValue  = 当前输出
startValue    = 开始一个 keyframe 时对 currentValue 的快照
targetValue   = 当前 keyframe 的目标 channel 值
```

旧本地声明把后两条成员名互换成了 `targetValue@第二指针` 与
`startValue@第三指针`。实际执行代码虽然通过额外注释和反向公式维持了数值行为，但
`EmoteVarController.h` 同时又写了正常的 `start + f*(target-start)` 公式，导致结构注释、
字段名、完成分支说明彼此矛盾。本轮 fresh 四端反编译确认物理第二指针始终是起点快照，
第三指针始终是目标；本地字段名已恢复为与数据流一致的 `startValue/targetValue`。

数值和物理槽位没有交换：修正的是源级 owner 名称及所有引用，binary-aligned declaration
order、序列化 `prev/target` 槽和析构顺序均保持不变。

## 四端函数与字段布局

| 目标 | `EmoteVarController_step` | count/state | current | start snapshot | target | pow/phase/invDuration |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x663FD8` | `+80/+84` | `+88` | `+96` | `+104` | `+112/+116/+120` |
| Android armv7 | `0x554014` | `+40/+44` | `+48` | `+52` | `+56` | `+60/+64/+68` |
| iOS arm64 | `0x1001A48C0` | `+48/+52` | `+56` | `+64` | `+72` | `+80/+84/+88` |
| iOS armv7 | `0x1A3E48` | `+24/+28` | `+32` | `+36` | `+40` | `+44/+48/+52` |

Android 使用 80/40B libstdc++ deque header，iOS 使用 48/24B libc++ deque header；
因此绝对 member offsets 不同，但 deque 后的字段顺序完全一致。三个数组都是独立
`new float[count]` owner；constructor 的物理分配、清零和 destructor 的释放顺序都是
`current -> start -> target`。

## keyframe 启动的数据流

当 `state == 0` 且 queue 非空时，四端都按下列顺序执行：

```text
front = queue.front()
for i in [0, count):
    startValue[i]  = currentValue[i]
    targetValue[i] = front.channelAndDuration[i]

state = 1
invDuration = 1.0f / front.channelAndDuration[3]
powCount = raw_float_word(front + 16)
queue.pop_front()
phase = 0.0f
fall through into the active state in the same call
```

| 目标 | snapshot copy | keyframe-to-target copy | state/inv/pow | phase zero |
|---|---:|---:|---:|---:|
| Android arm64 | `0x664130` | `0x664138` | `0x664144..0x66416C` | `0x6641AC` |
| Android armv7 | `0x55404C` | `0x554054` | `0x554068..0x55407E` | `0x5540BC` |
| iOS arm64 | `0x1001A493C` | `0x1001A4944` | `0x1001A4958..0x1001A4978` | `0x1001A4984` |
| iOS armv7 | `0x1A3EAE` | `0x1A3EB6` | `0x1A3EC6..0x1A3EE0` | `0x1A3EEC` |

Android arm64 还会在 count 足够大且别名检查允许时使用 8-float SIMD copy；表中列出 scalar
tail 的最直接 owner 证据。其他端的编译器展开不同，不改变第二指针接收 current、第三指针
接收 keyframe 的角色。

pop 在本轮 interpolation 更新之前完成。因而 `dt == 0` 仍会消费队首 keyframe、建立
start/target、写 active state 和 phase，再计算 `powf(0,powCount)`；不能把零 dt 简化成
无副作用 getter。

## active 插值与终值提交

active state 的共同逻辑为：

```text
phase = phase + invDuration * dt
if phase >= 1.0f:
    phase = 1.0f
    for i in [0, count):
        currentValue[i] = targetValue[i]
    state = 0
else:
    factor = powf(phase, powCount)
    for i in [0, count):
        currentValue[i] =
            startValue[i] + factor * (targetValue[i] - startValue[i])

for i in [0, count):
    out[i] = currentValue[i]
```

| 目标 | active lerp store | terminal target-to-current | state idle | output copy |
|---|---:|---:|---:|---:|
| Android arm64 | `0x66428C` / `0x6642C0` | `0x6643F8` / `0x664398` | `0x6643A0` | `0x6643C8` / `0x664320` |
| Android armv7 | `0x554126` | `0x554152` | `0x55415E` | `0x554170` |
| iOS arm64 | `0x1001A49E8` | `0x1001A4A2C` | `0x1001A4A3C` | `0x1001A4A58` |
| iOS armv7 | `0x1A3F5C..0x1A3F60` | `0x1A3F8C` | `0x1A3F98` | `0x1A3FAA` |

terminal branch 不调用 `powf`，也不使用计算公式重建终点，而是把 target bits 直接复制到
current。这样可保留目标里的 `-0`、NaN payload 或其他 raw float bits；只有 phase 被强制写成
`1.0f`。state 在数组 copy 后才写回 idle。

非 terminal 分支使用 float multiply/subtract/add，Android arm64 可按 8 个 float SIMD；
ARMv7/iOS arm64 的 scalar/NEON 展开仍是相同的 `start + f*(target-start)`，没有颠倒
subtraction，也没有在每帧更新 start/target owner。

## 状态与数值边界

四端共同边界如下：

- `state == 0 && queue.empty()`：不写 phase/power/owner arrays，只把 current 复制到 out；
- `state` 既不是 0 也不是 1：同样只输出 current，不自动修复非法 state；
- `phase >= 1` 使用 ordered float compare；phase 为 NaN 时条件为假，会进入 `powf` 分支；
- overshoot 不保留：terminal branch 把 phase 精确写成 1，并直接 commit target；
- `invDuration` 不做 zero/finite guard。duration 为零可产生 infinity，随后由普通 float
  arithmetic 与 ordered compare 决定分支；
- 所有 channel loop 使用 signed `count >= 1` gate。非正 count 时 keyframe 标量状态仍可
  被消费/更新，但 array 和 output loop 跳过；正常 constructor callers只使用固定正 count；
- queue front 的 word 3 对四 channel color controller 同时是 alpha 与 duration，这是 20B
  keyframe constructor 的既有重叠边界，本轮没有改变。

## reset、state serialization 与 destructor

同一 physical role 在旁路消费者中保持一致：

- active 且 queue 为空的 reset：`targetValue -> currentValue` 后 idle；
- queue 非空的 reset：直接把最后一个 queued keyframe channels 提交到 current，再 clear；
- controller state serialization：`frame=currentValue`、`prev=startValue`、
  `target=targetValue`；restore 写回同样三条 owner；
- destructor body：`delete[] currentValue`、`delete[] startValue`、
  `delete[] targetValue`，随后自动析构 queue。

旧字段名曾把这两个属性映射写成相反的标识符。这只是本地名字反了，不是原生序列化
schema 反直觉；本轮 semantic rename 后属性名和运行时角色重新一致。

## 本地修正与测试

涉及：

- `EmoteVarController.h/.cpp`：交换后两条成员的源级名字，不交换声明槽位；统一 constructor、
  step、reset、destructor 注释和访问；
- `EmoteEngine.cpp`：serialization/restore 继续访问同一 physical slots，但用纠正后的
  `startValue/targetValue` 名；
- 两份旧分析文档同步修正 semantic names；
- unit test 新增两 channel 线性插值：从 `{-1,-2}` 到 `{3,6}`，半程必须输出 `{1,2}`，
  terminal 必须精确提交 `{3,6}` 并令 state idle。

测试同时直接检查 keyframe 启动后 start owner 保留旧 current、target owner保留 keyframe
channels，避免将来再次只靠反向公式掩盖字段名错误。

## recovery IDB 改善

四个 recovery IDB 已在 step entry、keyframe snapshot/target copy、active lerp 和 terminal
commit 处追加注释，并设置 start/target role 书签。语义名带来的结构推断仍以 `_guess`
原则处理；绝对地址仅保留在本文与 IDB，不进入新的可编译源码注释。
