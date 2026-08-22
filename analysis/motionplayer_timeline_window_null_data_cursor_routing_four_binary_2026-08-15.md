# MotionPlayer timeline window null-data、cursor 与路由四参考复原（2026-08-15）

## 结论

`EmoteEngine::applyTimelineWindow_guess(state, inclusive, targetTime)` 在当前四个参考中
共同拥有一条此前本地遗漏的生命周期门：`timelineData == nullptr` 时不读取track deque
或cursor vector，不dispatch任何frame，但仍无条件写
`state.currentTime = targetTime`。本地旧实现直接解引用data owner，在restore/reset等
半初始化窗口会产生原版不存在的崩溃；本轮已恢复null-data no-track语义。

其余核心边界也经fresh指令确认：window按物理track序号索引cursor，保留与seek的
compact cursor不对称；inclusive使用`<=`、strict使用`<`；尾sentinel只推进cursor但不
dispatch；parallel普通track走内部controller，其他track走通用`setVariable`；transition
表达式的NaN四端都传播，旧文档关于64位归零的结论错误。

## 四端函数与唯一caller族

| 目标 | window helper | 大小 | null-data gate | currentTime commit |
|---|---:|---:|---:|---:|
| Android arm64-v8a | `0x6671FC` | `0x2C8` | `0x667234` | `0x667498` |
| Android armeabi-v7a | `0x555BC0` | `0x20E` | `0x555BE8` | `0x555DA8` |
| iOS arm64 | `0x1001A6BDC` | `0x25C` | `0x1001A6C1C` | `0x1001A6E0C` |
| iOS armv7 | `0x1A636C` | `0x200` | `0x1A639A` | `0x1A6552` |

每端都恰有五条code xref：

1. active-timeline reset的non-loop末窗；
2. pre-progress non-loop inclusive窗；
3. pre-progress loop-end strict窗；
4. pre-progress wrap后inclusive余量窗；
5. timeline-state restore的inclusive `curTime`窗。

四端保留共同原型：

```cpp
void EmoteEngine_applyTimelineWindow_guess(
    void *engine, void *state, bool inclusive, double targetTime);
```

helper不拥有engine/state；它借用decoded data、frame/controller owner和cursor storage。
只有正常落到函数尾才提交currentTime；中途script setter或controller enqueue抛出时没有
catch/rollback。

## null-data 与空data的所有权边界

共同骨架为：

```cpp
if (state.timelineData) {
    for (physical trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        processTrack(...);
}
state.currentTime = targetTime;
```

Android只显式测试data pointer，空deque由迭代边界自然跳过；iOS编译器把pointer与
track count一起合并进入门。两者源级语义相同：

- null data：不读flags、track、frameCursors或controller，currentTime仍写target；
- nonnull empty data：同样只写currentTime；
- data存在且首个需要处理的track出现后，frameCursors没有size guard；
- targetTime可以是NaN或Inf，末尾按原bit pattern写入。

这个局部门不能被扩大为pre-progress整项skip。pre-progress在window返回后还可能：

- loop wrap调用无null-data guard的`seekTimeline_guess`；
- flags bit1开启时step blend并遍历data tracks；
- autoStop非零时解引用blend owner。

因此default-materialized state在不同flags/loop/dt组合下仍可能安全提交、无限wrap或进入
更晚的非法owner边界。

## track、cursor 与 sentinel状态机

data存在时按物理track序号运行：

1. 如果`flags & 4`且track为instant，跳过整条track，但物理`trackIndex`仍递增；
2. 否则直接读取`frameCursors[trackIndex]`为signed 32-bit cursor；
3. 以窄化到signed 32-bit的`frameCount - 1`测试是否还有下一frame；
4. 下一frame time在inclusive窗用`<= targetTime`，strict窗用`< targetTime`；
5. 每个crossed frame都会推进cursor，即使`typeZero`；
6. 只有`!typeZero`且它后面仍有一frame时才dispatch；最后一frame作为tail sentinel永不
   dispatch，但crossed后cursor仍能指向它；
7. track结束后立即把cursor写回当前物理slot；全部track正常结束后才写currentTime。

seek在flags4 instant track上**不追加cursor**，window却仍按物理index计数。当前四端都
保留这条不安全不对称：instant位于普通track之前时，后续track可能读取错位cursor或越过
compact vector。window没有补bounds check，本地也不能“修好”它。

cursor为`-1`时，下一frame地址自然对应frame0；更小负值会形成frame buffer之前的地址/
超大size_t索引。frameCount窄化、cursor加法与frameCursors索引均没有结构验证。

## dispatch路由与提交顺序

内部路由条件严格为：

```cpp
internalRoute = (state.flags & 2) != 0 && !track.instantVariable;
```

- flags2普通track：调用track独占的VarController setter，duration和power各从double窄化
  float，并读取Engine queuing byte；
- flags2 instant track（若未被flags4跳过）：仍走普通`setVariable`；
- flags2关闭：所有未跳过track都走普通`setVariable`。

通用setVariable会继续执行HM6 controller路由/HM7 fallthrough，所以window不是一个只写
timeline私有缓存的helper。每个crossed action即时enqueue/写变量；如果较晚track失败，
较早controller queue/HM7写入和已完成track的cursor都保留，当前track cursor与全局
currentTime则尚未提交。

## transition、ordered compare 与浮点边界

对crossed action frame `i`，只有`i+1 < frameCount`时计算：

```cpp
x = frame[i + 1].time - targetTime - 1.0;
transition = max(x, 0.0);
```

指令锚点：

| 目标 | clamp指令 |
|---|---:|
| Android arm64 | `0x667438` `FMAX` |
| Android armv7 | `0x555CCA..0x555CD4` ordered-negative select |
| iOS arm64 | `0x1001A6D40` `FMAX` |
| iOS armv7 | `0x1A64A4..0x1A64AE` ordered-negative select |

关键是64位使用`FMAX`而非numeric `FMAXNM`：

- finite负值四端都变`+0`；
- NaN：64位FMAX传播NaN，32位ordered negative不命中而保留NaN；
- `x=-0`：64位FMAX选择`+0`，32位保留first operand的`-0`。

Web目标为wasm32，源码保留`std::max(x,0.0)`，与两份32位参考的NaN和signed-zero语义
一致。crossing比较本身是ordered；frame time或target为NaN时`<`/`<=`均为false，不推进
cursor，但正常函数尾仍把NaN target写入currentTime。

## 本地修正与回归

源码现只在`state.timelineData`非空时进入track range-loop，把
`state.currentTime = targetTime`保留在门外。既有物理cursor、sentinel、路由和
`std::max`行为不改。

新增回归用一个null-data state预置非空frameCursors，调用inclusive window后确认：

- 不崩溃；
- cursor vector逐元素不变；
- currentTime仍精确更新到target。
