# Timeline blend/fade lazy initialization and commit order — four-reference reconstruction

Date: 2026-08-15

本纵切面 fresh 复核 D3D `setTimeline`、blend getter、fade-in/fade-out facade，
以及 Engine blend/fade-in 核心。旧高层伪代码大体正确；本轮闭合此前未明确的 owner、
lazy-init、异常提交与 play-miss 后继续调用边界。

## 四端地址

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| D3D setTimeline | `0x530C84` | `0x494F20` | `0x100233388` | `0x232070` |
| Engine set blend | `0x67098C` | `0x55ACDC` | `0x1001AE178` | `0x1AD918` |
| D3D get blend | `0x530C94` | `0x494F48` | `0x100233394` | `0x232098` |
| D3D fade in | `0x530DF0` | `0x494FE0` | `0x100233424` | `0x232170` |
| Engine fade in | `0x670D24` | `0x55AE44` | `0x1001AE2E8` | `0x1ADABC` |
| D3D fade out | `0x530DFC` | `0x494FE8` | `0x100233430` | `0x232178` |

D3D set/fade facade 只解析 Engine 后直传 borrowed label 和浮点/Boolean参数。
`setTimeline` 的精确顺序仍为 `(label, value, transition, easingWeight, autoStop)`；
D3D typed fade 不做 Motion raw-callback 的 script-ease 换算。

## Engine blend 核心

四端共同顺序：

```cpp
void setBlend(const ttstr &label, float value, float transition,
              float power, bool autoStop) {
    auto found = timelineStates.find(label);       // HM3, non-inserting
    if (found == timelineStates.end())
        return;                                    // silent miss

    auto &state = found->second;
    if (!state.timelineData)
        initializeTimelineState(state);            // also establishes controller

    float localValue = value;
    setTarget(state.blendController, &localValue,
              transition, power, engine.queuing);
    state.autoStop = static_cast<double>(autoStop); // only after setTarget returns
}
```

可见边界：

- label miss 不记录日志、不插入 HM3、不改变 active vector。
- state 可以存在但尚未初始化；blend setter 会在不要求 timeline active 的情况下
  lazy initialize timelineData/blendController。
- `value` 先复制到函数栈上的单 float，controller setter 接收其地址；transition、
  easingWeight/power 与 Engine `_queuing` byte 原样传入。
- autoStop 以 `0.0/1.0` double 写 mapped state，并且在 controller setTarget **之后**。
  若 lazy init 或 setTarget 抛异常，autoStop 保持旧值；已完成的 lazy init不回滚。
- fade-out 即使 label 当前不 active，也能对已有 HM3 state执行上述 lazy init和排队。

## getter owner 与发布门槛

四端 D3D blend getter把按值 label CopyRef到局部 owner，直接内联 HM3 find，再按正常/
异常路径释放该 owner。返回条件不是“active”，也不要求 blend controller非空：

```cpp
auto found = timelineStates.find(localLabel);
if (found != end && found->second.timelineData)
    return static_cast<double>(found->second.blendWeight); // float -> double
return 0.0;
```

因此 dormant 但已初始化的 state仍发布 blendWeight；存在但 timelineData为空的 state、
HM3 miss及空键 miss均返回 `0.0`，查询不插入。

## fade-in/fade-out组合与 miss提交

```cpp
void fadeIn(label, duration, power) {
    if (!isTimelinePlaying(label)) {
        playTimeline(label, 3);
        setBlend(label, 0, 0, 1, false);
    }
    setBlend(label, 1, duration, power, false);
}

void d3dFadeOut(label, duration, power) {
    setBlend(label, 0, duration, power, true);
}
```

`playTimeline` 返回 `void`，fade-in 不检查成功状态。对未知且非active label：

1. `play(label, 3)` 因 bit 0先 clear全部 active labels；
2. HM3 miss 写一条精确 label-not-found日志并正常返回；
3. fade-in仍调用零值 blend和最终一值 blend；两次 HM3 miss均静默；
4. 最终 HM3 与 active vector都保持空，且没有回滚第1步。

对已 active label，fade-in跳过 play和零值初始化，只排最终目标；对 dormant但 HM3已有
state的 label，则先 play/seek，再以 transition=0、power=1建立零值，再排最终目标。
D3D fade-out从不查 active vector，直接进入 blend核心并把 autoStop设为true。

## 本地对齐与回归

本地 blend/fade 主体已符合四端顺序，无需行为重写。本轮补充源码注释与回归，固定
未知 fade-in 的 clear→single play log→two silent blend misses 数据流；既有测试已覆盖
dormant state仍可 set/get blend、HM3 miss非插入、队列 value/duration/power、Engine
queuing flag和autoStop double。

验证结果：Emscripten syntax-only 测试翻译单元通过（仅既有 literal-operator弃用
warning），`Web Debug Build` 以3个增量步骤完成最终链接，目标 `git diff --check`
通过。
