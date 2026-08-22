# MotionPlayer `tickCount` / `frameTickCount` 四参考二进制对照（2026-08-12）

## 1. 范围与结论

本纵向覆盖 `Motion.Player.tickCount` 与 `frameTickCount` 的注册、getter/setter、
内部双游标提交、构造初态、推进消费者和浮点边界。四个参考二进制共同证明：两个
属性是同一 raw-frame cursor 的毫秒视图和 frame 视图，而且两个 setter 都执行
完整状态提交；`frameTickCount` 绝不是只写 raw 字段的快捷路径。

可观测的共同源语义为：

```cpp
double getTickCount() {
    return rawCursor * 1000.0 / 60.0;
}

void setTickCount(double milliseconds) {
    setFrameCursor(milliseconds * 60.0 / 1000.0);
}

double getFrameTickCount() {
    return rawCursor;
}

void setFrameTickCount(double frames) {
    setFrameCursor(frames);
}

void setFrameCursor(double cursor) {
    if (cursor < 0.0)
        cursor = 0.0;
    rawCursor = cursor;

    double evaluationCursor = cursor;
    if (evaluationCursor > totalFrames)
        evaluationCursor = totalFrames;
    clampedEvaluationCursor = evaluationCursor;
    queuing = true;
    firstFrame = true;
}
```

这里的两个 cursor 有意分离：公开 getter 始终读取未做上界截断的 raw cursor；
内部节点/时间线求值读取 evaluation cursor，它只在有序 `cursor > totalFrames`
成立时被截到 motion 末尾。

## 2. 宽字符串、注册与访问器映射

属性名以 UTF-16LE 字节模式定位；地址与 ABI 偏移只保存在本分析文档。

| 证据 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| `tickCount` 字符串 | `0x14D6408` | `0xD85D16` | `0x10195CB0A` | `0x174EE6E` |
| `frameTickCount` 字符串 | `0x14D641C` | `0xD85D2A` | `0x10195CB1E` | `0x174EE82` |
| Player registrar | `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |
| `tickCount` 注册点 | `0x6D47D0` | `0x598128..0x598142` | `0x10012486C` | `0x123B74..0x123B9A` |
| tick getter | `0x6D6A80` | `0x598ED0` | `0x1001255C0` | `0x1247C4` |
| tick setter | `0x6D6AA0` | `0x598F00` | `0x1001255E0` | `0x1247F0` |
| `frameTickCount` 注册点 | `0x6D4848` | `0x598146..0x598160` | `0x100124898` | `0x123B9E..0x123BC4` |
| frame getter | `0x6D6AE0` | `0x598F68` | `0x100125620` | `0x12484C` |
| frame setter | `0x6BE2D4` | `0x58A490` | `0x1001138E8` | `0x1112EC` |

Android armv7 的三个短访问器 `0x598ED0..0x598F00`、
`0x598F00..0x598F68`、`0x598F68..0x598F72` 最初未被 IDA 建成函数。本轮补齐
精确边界后才进行 fresh decompile，避免把 registrar 邻接代码误判为 getter/setter。

四端 registrar 都为两个属性同时填入 typed double getter 与 setter，没有只读
descriptor，也没有额外脚本包装层。标准 typed NCB 会先把 TJS 值转换为 double；
getter 返回 `tvtReal`。本地测试覆盖 Real、Integer 与 Void 输入，其中 Void 按 typed
数值转换成为零，随后仍执行完整 setter 副作用。

## 3. 字段布局

| Player 状态 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| raw frame cursor | `+1120` | `+776` | `+1008` | `+708` |
| total frames | `+1128` | `+784` | `+1016` | `+716` |
| evaluation cursor | `+456` | `+288` | `+344` | `+228` |
| `queuing` byte | `+480` | `+312` | `+368` | `+252` |
| `firstFrame` byte | `+481` | `+313` | `+369` | `+253` |

raw cursor 和 total frames 在四端都相邻，但 setter 不覆盖 total frames。evaluation
cursor 位于推进/求值状态区，两个 Boolean 也相邻，因此编译器经常把
`queuing=true; firstFrame=true` 合并为一个半字 `0x0101` 写入。

## 4. 换算顺序与浮点边界

tick getter 无条件执行 `raw * 1000.0 / 60.0`；没有 `raw > 0` 守卫。这一点不同于
`lastTime`/`loopTime` 的正值条件换算。tick setter 同样保持操作顺序，先乘
`60.0` 再除 `1000.0`，不能把两边预折叠为近似比例常量或交换乘除顺序。

对共享 setter，普通有限输入的四端结果一致：

- 负有限值被改写为正零；
- 零和正有限值保留为 raw cursor；
- 超过 total frames 的值仍可由公开 raw getter 读回，只有 evaluation cursor 截到
  total frames；
- `+∞` 保留在 raw cursor，evaluation cursor 截到有限 total frames；
- NaN 不满足有序上界比较，因此流入 raw 与 evaluation 两个 cursor；
- 当 cursor 与 total 相等时不执行覆盖，evaluation cursor 保留 cursor 操作数。

最后一点使原本地表达式
`(cursor < totalFrames) ? cursor : totalFrames` 不等价：它会在 NaN 和相等时选择
total operand。相等的 `+0/-0` 还会使符号位选择产生可见差异。修订采用先赋
cursor、再仅在 `>` 时覆盖的原始控制流。

### 4.1 AArch64 与 ARMv7 的负值归零差异

两份 AArch64 机器码用标量 `FMAX cursor, +0.0` 完成下界限制；两份 ARMv7
机器码则显式执行有序 `cursor < 0.0`，仅在比较成立时写 `+0.0`。它们对普通有限
负数一致，但不能把 signed zero/NaN 的机器级结果笼统宣称为四端完全相同。

恢复的可移植 C++ 使用有序 `< 0.0`，与 ARMv7 的控制流和最自然的共同源形状
一致，因此保留 `-0`；AArch64 最终 `FMAX` 对 signed zero 的实际选择作为后端
代码生成差异保留在本证据中。测试对 `-0` 的断言明确限定于恢复的 C++/ARMv7
源形状，而 NaN 的 raw/evaluation 流通和普通负值归零属于共同边界。

## 5. setter 的写入顺序

四端最终稳定状态一致，但优化后的独立 store 顺序不同：

| 目标 | 观察到的主要顺序 |
| --- | --- |
| Android arm64 tick setter | raw cursor；队列半字 `0x0101`；有序上界判断；evaluation cursor |
| Android arm64 frame setter | raw cursor；有序上界判断；evaluation cursor；队列半字 `0x0101` |
| Android armv7 两个 setter | 队列半字 `0x0101` 先于 raw/evaluation cursor 提交 |
| iOS arm64 / armv7 两个 setter | raw cursor；有序上界判断；evaluation cursor；队列半字 `0x0101` |

这些字段之间没有回调、异常点或并发同步原语，因而中间 store 顺序没有当前公开
可观察者；它反映的是内联/调度差异，不足以证明四份源代码不同。恢复代码保留一次
完整 helper 调用和最终状态，不臆造平台条件分支。

## 6. 构造初态

四端构造器不是 `_queuing=false`。它们都把 raw/evaluation cursor 清零，并发布
`queuing=true, firstFrame=false`：

| 目标 | 构造器 | cursor/queue 证据 |
| --- | --- | --- |
| Android arm64 | `0x6CC110` | raw `0x6CC480`；evaluation 清零 `0x6CC494`；`STRH 1` 到 `+480` (`0x6CC4B0`) |
| Android armv7 | `0x5935C4` | raw `0x5937A6`；evaluation 区 `memset` (`0x5937BE`)；`WORD 1` 到 `+312` (`0x5937E8`) |
| iOS arm64 | `0x10011EC04` | raw `0x10011EE08`；evaluation `0x10011EE18`；queue/first 分别在 `0x10011EE38/0x10011EE40` 写 `1/0` |
| iOS armv7 | `0x11D488` | raw `0x11D7C6..0x11D7CA`；evaluation 区 `0x11D7D8..0x11D7EC`；queue/first 在 `0x11D81E/0x11D82A` 写 `1/0` |

在小端 Android 上，半字 `0x0001` 即低地址 queue byte 为 1、高地址 first-frame
byte 为 0。它与 setter 的 `0x0101` 清楚地区分：新 Player 已 queued，但尚未带有
一次显式 seek/play 所发布的 first-frame 动作。本地字段默认值原为
`_queuing=false`，现已纠正并由构造后立即断言覆盖。

## 7. 推进消费者与对象生命周期

四端 `Player_frameProgress_guess` 的 fresh decompile 共同显示：

1. `firstFrame` 为真时先清零该一次性字节；
2. 若当前 delta 为负且 raw cursor 为零，把 raw/evaluation cursor 一起播种为
   total frames，使反向播放从 motion 尾开始；
3. 普通推进只有在 `queuing` 为假时才执行 `raw += delta`；
4. 推进后再次从 raw 生成 evaluation cursor，并只用有序 `>` 做上界截断；
5. `updateLayers` 在应用非空 node tree 后释放 queue gate；空 node tree 的早退不会
   清 queue。

因此 setter 的两个 Boolean 写入不是冗余状态：`firstFrame=true` 控制下一次
reseek/反向首帧分支，`queuing=true` 阻止同一排队阶段继续普通累加。测试先消费旧
first-frame 状态，再以负 speed、零 cursor 调用两个 setter，验证下一帧都从
`lastTime` 播种；旧本地 `frameTickCount` 字段直写会在这里错误地停在零。

这些字段全部由 Player 自身持有，不引入 dispatch owner、引用计数或临时容器。
setter 也不分配、不调用脚本、不抛出新异常；其生命周期影响只通过后续
frameProgress/updateLayers 消费相邻状态字节实现。

## 8. 本地偏差与修订

本纵向修复了三项可观察偏差：

- `setFrameTickCount` 原本只写 raw cursor，漏掉负值归零、evaluation cursor、
  `queuing` 与 `firstFrame`；现与 `setTickCount` 共用完整提交 helper；
- 原 evaluation cap 的小于号三元表达式在 equality、NaN 与 signed zero operand
  选择上错误；现恢复“先赋 raw，再仅在 ordered `>` 时覆盖”的控制流；
- Player 字段默认 `_queuing=false` 与四端构造器 `true` 冲突；现恢复构造初态，
  并清理 frameProgress 中继续声称默认 false 的旧单二进制地址注释。

`main.cpp` 的两个 `NCB_PROPERTY` 仍保持 typed 可读写绑定，只新增两个视图共享完整
状态提交的说明。`evaluationFrameForDifferentialTest_guess()` 只让回归测试观察内部
evaluation cursor，没有注册为脚本 API。

单元测试覆盖：构造 queue 初态、普通负值、`-0`、NaN、`+∞`、毫秒/frame 双向
换算、raw 超界而 evaluation 截断、两个 setter 的 first-frame 反向播种，以及 NCB
Real/Integer/Void 输入、Real 输出和完整副作用。

## 9. IDB 改进

四份 IDB 的 16 个访问器统一命名为：

- `Player_getTickCountMs_guess`；
- `Player_setTickCountMs_guess`；
- `Player_getFrameTickCount_guess`；
- `Player_setFrameTickCount_guess`。

每个访问器都写入字段/换算/副作用语义注释并强制刷新反编译。四个
`Player_ctor_guess` 也补入 `0x0001` 构造 queue word 与 setter `0x0101` 的差别，
再次 fresh decompile；Android arm64 另以反汇编确认 `STRH 1`，iOS armv7 以反汇编
确认分离的 byte stores。四份 IDB 均已保存到各自数据库。

## 10. 验证

- `git diff --check`：通过；输出只有工作树既有的 LF/CRLF 转换警告；
- 使用 Web `compile_commands.json` 的真实 motionplayer Emscripten 参数，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过；仅有
  仓库既有的 `_tss` literal-operator 弃用警告；
- `cmake --build out/web/debug --target motionplayer --parallel 8`：通过；
- `cmake --build out/wasmtime/debug --target motionplayer --parallel 8`：通过；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel 8`：通过，
  包括最终 `wasm-opt`；
- `cmake --build out/web/debug --target krkr2 --parallel 8`：通过；
- `cmake --build out/web/debug --parallel 8`：完整默认 Web target 复验通过；
- 上述两个静态库、Wasmtime guest、Web `krkr2` 与完整 Web 构建均再次运行并得到
  `ninja: no work to do`。首次链接/优化超过 60 秒工具窗口后由后台 Ninja 正常
  完成，未把工具超时误记为构建失败。
