# MotionPlayer timeline pre-progress 共享余量、ordered gate 与 erase 四参考复原（2026-08-15）

## 结论

`EmoteEngine::preProgress_guess(force, dt)` 是 active timeline 在各 controller family
正常 step 之前的推进核心。四份 `reference/binaries/` 共同证明，它不是“每条 timeline
各推进一次原始 dt”的独立循环，而是一条有状态的有序流水线：函数只在 active-label
循环外建立一份 double `remaining = dt`，某条循环 timeline 的 wrap 会从中扣除完整
跨度，随后所有 label 只接收剩余量。

本轮同时纠正了本地实现的另外三处偏差：

- 原版以 ordered `loopBegin >= 0` 选择循环分支，故 NaN 与负值一样走 non-loop；
- 循环尾窗口用数值 `fmax(remaining, 0.0)`，NaN 和负数得到正零；
- `lastTime <= currentTime` 只在 non-loop 分支执行，而且先于 auto-stop blend owner
  解引用；循环 timeline 完全不按 `lastTime` 移除。

## 四端函数、调用边与抽取差异

| 目标 | pre-progress | 大小 | parallel-step helper | 大小 | 两个 caller |
|---|---:|---:|---:|---:|---|
| Android arm64-v8a | `0x66EB44` | `0x2EC` | 内联 | — | serialize `0x673260`；progress core `0x67A440` |
| Android armeabi-v7a | `0x559F78` | `0x178` | `0x55A2DC` | `0x5A` | serialize `0x55BB92`；progress core `0x55FF08` |
| iOS arm64 | `0x1001AD0DC` | `0x1E4` | `0x1001AD540` | `0x104` | serialize `0x1001AF794`；progress core `0x1001B4328` |
| iOS armv7 | `0x1AC844` | `0x1DE` | `0x1ACC42` | `0xA8` | serialize `0x1AEF5E`；progress core `0x1B3E32` |

四端 pre-progress 均只有这两个 caller：正常 Engine progress 传 `force=false` 与 frame
dt；state serialize 为取得一致 snapshot，先传 `force=true, dt=0` 做零步长预刷新。
三份非内联 helper 均只有 pre-progress 内的两条 caller，分别对应 non-loop 与 loop
窗口之后；Android A64 则把完全相同的 helper body 展开了两次。

IDB 现统一使用：

```cpp
void EmoteEngine_preProgress_guess(void *self, bool force, double dt);
void EmoteEngine_stepParallelTimelineControllers_guess(
    void *self, void *state, float dt);
```

第二个名字仍带 `_guess`；其第一个 Engine 参数在三端抽取体内未被使用，但 caller ABI
和成员调用形状保留该参数，不能仅因优化后不读取就从恢复的源码角色中删除。

## 共同控制流

四端共同的源级骨架为：

```cpp
if (dt == 0.0 && !force)
    return;

double remaining = dt; // 只初始化一次
size_t i = 0;
while (i < activeLabels.size()) {
    TimelineState &state = timelineStates[activeLabels[i]];
    const bool nonLoop = !(state.loopBegin >= 0.0);

    if (nonLoop) {
        applyWindow(state, true, state.currentTime + remaining);
    } else {
        while (state.currentTime + remaining >= state.loopEnd) {
            remaining -= state.loopEnd - state.currentTime;
            applyWindow(state, false, state.loopEnd);
            seek(state, state.loopBegin);
        }
        applyWindow(state, true,
                    state.currentTime + fmax(remaining, 0.0));
    }

    if (state.flags & 2) {
        const float step = static_cast<float>(remaining);
        step(state.blendController, state.blendWeight, step);
        for (TimelineTrack &track : state.timelineData->variableList)
            if (!track.frameList.empty() && !track.instantVariable)
                step(track.controller, track.output, step);
    }

    if (nonLoop && state.lastTime <= state.currentTime) {
        activeLabels.erase(activeLabels.begin() + i);
    } else if (state.autoStop != 0.0 &&
               state.blendController->state == 0 &&
               state.blendController->queue.empty()) {
        activeLabels.erase(activeLabels.begin() + i);
    } else {
        ++i;
    }
}
```

这里的 map 访问是 `operator[]`，不是 `find`/`at`。stale active label 会先 materialize
默认 state。2026-08-15 的后续 fresh window复核确认，window helper本身会在null data
时只提交currentTime；但loop wrap随后调用的seek、parallel bit开启时的controller/track
step，以及autoStop owner读取仍各有更窄的无guard边界。它与`animating`的整项skip仍不
等价，不能把查询函数的guard横向复制到推进核心。详见
`analysis/motionplayer_timeline_window_null_data_cursor_routing_four_binary_2026-08-15.md`。

## 共享 residual 的数据流

直接指令锚点如下：

| 目标 | 循环外初始化 | loopBegin ordered gate | wrap 扣减 |
|---|---:|---:|---:|
| Android arm64 | `0x66EB64` | `0x66EBB4` | `0x66ECA0` |
| Android armv7 | `0x559F88` | `0x559FE0` | `0x55A060` |
| iOS arm64 | `0x1001AD0F8` | `0x1001AD148` | `0x1001AD1B0` |
| iOS armv7 | `0x1AC860` | `0x1AC8B2` | `0x1AC930` |

四端都在取得 active vector begin 之前保存 `dt`，并在 wrap 中原地写回同一寄存器/
spill。erase 或递增 iterator 后没有从函数参数 reload。因此，若第一条 timeline 从
`currentTime=0` 以 `dt=5` 跨过 `[0,2)` 两次，它结束在 1；下一条 non-loop timeline
也只推进 1，而不是 5。

erase 不递增索引；被移到当前位置的下一 label 继续使用当前 residual。没有 wrap 的
non-loop timeline 不消耗 residual，因而其后的 label 仍接收相同值。

## ordered 浮点边界

### 入口门

`dt != 0.0 || force` 使用普通 ordered/not-equal 语义：正零和负零在 `force=false`
时返回；NaN 与正负无穷会进入。`force=true, dt=0` 仍执行全部 active-label pipeline。

### loop marker

原版测试 `loopBegin >= 0.0`，而不是先测试 `< 0.0` 再取反：

- `+0`、`-0`、正数：loop；
- 负数：non-loop；
- NaN：ordered `>=` 为 false，因此 non-loop。

旧源码用 `if (loopBegin < 0)`，会把 NaN 错送进 loop 分支。

### residual clamp

loop 最后一个 inclusive window 使用 `fmax(remaining, 0.0)`。Android/iOS 32 位把它
展开为“只有 `remaining > 0` 才取 remaining，否则取字面量 0”；两份 64 位反编译为
数值 max。结果为：

- 负数、负零、NaN -> `+0.0`；
- 正数、正无穷 -> 原值。

这不同于 `std::max(remaining, 0.0)`：其比较器面对 NaN 会返回第一个参数，令 NaN
进入 window target。本地已改为 `std::fmax`。

loop wrap 没有 finite/positive-span guard。`remaining=+Inf` 减有限跨度仍为 +Inf；零长
或反向 loop 也可能无法让条件变假。原版可无限循环或在更早的空 owner 解引用处失败，
本地没有增加超时、有限性或跨度校验。

## controller step 与删除提交顺序

parallel bit（flags bit 1，即值 `2`）关闭时，parallel-step helper不读取 timeline data
或 blend controller；window helper仍会读取 timeline data。bit 开启时固定：

1. 把当前共享 `remaining` 从 double 窄化为 float；
2. 先 step blend controller并写 `blendWeight`；
3. 按 track deque物理顺序扫描；
4. 只 step frame list非空且 `instantVariable == false` 的 track controller；
5. 每个输出即时写回，没有事务或回滚。

non-loop 的删除顺序在四端都有独立指令锚点：

| 目标 | `lastTime <= currentTime` | loop 分支 autoStop（无 lastTime） |
|---|---:|---:|
| Android arm64 | `0x66EC44` | `0x66ED7C` |
| Android armv7 | `0x55A00C` | `0x55A0BA` |
| iOS arm64 | `0x1001AD174` | `0x1001AD214` |
| iOS armv7 | `0x1AC8DE` | `0x1AC98A` |

若 non-loop 已到 `lastTime`，立即跳到 vector erase，不读取 `autoStop` 后的 blend状态。
这使“data存在、blend为空、但本步已经完成”的状态能够按时间安全移除。旧本地实现
先计算 `blendFinished`，会在已完成的情况下仍解引用空 blend owner。

只有未被 non-loop 时间门移除时才检查 autoStop：任意非零 double（包括 NaN）启用；
随后无 null guard地读取 blend controller state和queue。循环 timeline 不检查
`lastTime`，只可能由这个 autoStop 条件移除。

## 本地修正与回归

`EmoteEngine::preProgress_guess` 已完成四项语义修正：

- 把 `remaining = dt` 移到 active-label循环外；
- 用 `!(loopBegin >= 0.0)` 表达 non-loop ordered gate；
- 用 `std::fmax` 处理 loop尾窗；
- 只在 non-loop测试 `lastTime`，并让它在 autoStop blend解引用之前短路。

新增回归覆盖：

- 第一条 `[0,2)` loop以 dt=5消费到 residual 1，第二条 timeline也只推进1；
- loop timeline超过 `lastTime` 仍保留；
- NaN `loopBegin` 进入 non-loop完成并删除；
- non-loop已完成时，即使 `autoStop != 0` 且 blend owner为空也先删除、不崩溃；
- 有限 loop收到 NaN dt时，尾窗由 fmax保持原 `currentTime`。
