# MotionPlayer position/control/easing 四参考二进制复原（2026-08-12）

## 范围与结论

本纵向从 MotionSub、timeline evaluator 与 particle child 的共同调用点向下，复核三层
helper：三维位置插值、nested position control curve、共享 VariableTrack easing。
Android arm64、Android armv7、iOS arm64、iOS armv7 四份当前参考二进制共同作为
事实来源。

最直接的结论是：compiled source 里原来的 `0x69A4D4` / `0x698454` / `0x69A754`
式名称来自旧单端分析，而且前两个地址并不是当前四参考二进制中 Android arm64 的
真实入口。本轮恢复了真实映射、统一源级名称、TJS 动态 dispatch 顺序、临时 Variant
析构边界、dead Void Variant，以及不可按代数等价式重排的浮点运算分组。

## 四端函数映射

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `PositionInterpolation_evaluate_guess` | `0x6978B4` (`0x280`) | `0x573AF8` (`0x232`) | `0x1000F7644` (`0x268`) | `0xF4380` (`0x29C`) |
| `PositionControlCurve_evaluate_guess` | `0x695834` (`0xC4C`) | `0x571FF0` (`0x5A0`) | `0x1000F59E0` (`0x6A8`) | `0xF2484` (`0x68A`) |
| `VariableTrackEasing_evaluate_guess` | `0x697B34` (`0x4F8`) | `0x573D40` (`0x228`) | `0x1000F78C0` (`0x2D8`) | `0xF4648` (`0x2CE`) |

position helper 的共同源级 ABI 是七参数：

```cpp
void PositionInterpolation_evaluate_guess(
    const Variant *easing,
    const double dst[3],
    const double src[3],
    double out[3],
    int coordinateMode,
    const Variant *controlCurve,
    double t);
```

两个 32 位 hard-float 目标在未加类型时会把寄存器/栈形参错误拼成 `double` 或缺参；
显式应用七参数类型后，四端 caller 和内部数据流完全对齐。不能从 armv7 初始伪代码的
形参列表推断源 ABI。

## caller 与数据流

position helper 的四端 xref 共同分成三类：

1. `Player_evaluateTimeline_guess`：当前/前一 slot 的 position 输出；
2. `Player_updateMotionSubNodes_guess`：在 ratio 与 `fmin(t+0.0001,1)` 两个采样点
   求位置，用差分方向算 angle mode 3；
3. 一个短小 type-4 particle child helper：同样连续调用两次位置插值。

四端 MotionSub 双调用点分别是：

| 目标 | 调用 1 | 调用 2 |
|---|---:|---:|
| Android arm64 | `0x6BBAE8` | `0x6BBB08` |
| Android armv7 | `0x588346` | `0x58836C` |
| iOS arm64 | `0x100111108` | `0x100111124` |
| iOS armv7 | `0x10EE34` | `0x10EE60` |

control helper 只被 position helper 直接调用，一端一个 xref。easing helper 则是更广的
共同底座：timeline evaluator 的多种字段、position helper、VariableTrack 插值、
MotionSub dofst wrapper 以及其他短 helper 都调用它。portable 源不应保留两个不同的
“Bezier evaluator”实现。

## 共享 easing evaluator

### TJS dispatch 和边界顺序

共同伪代码：

```text
x = easing.PropGet("x", sharedXHint)
y = easing.PropGet("y", sharedYHint)
count = x.PropGet("count")

firstX = real(x[0])
if unordered(firstX, t) or firstX >= t:
    return real(y[0])
last = count - 1
lastX = real(x[last])
if unordered(lastX, t) or lastX <= t:
    return real(y[last])

segmentEnd = 0
do:
    segmentEnd += 3
while ordered(real(x[segmentEnd]) < t)

index = segmentEnd - 3
for i in 0..3:
    (void)real(x[index])         // observable dynamic read, value discarded
    value[i] = real(y[index])
    index += 1

u = 1 - t
return u*(u*u)*value[0]
     + u*(u*3)*t*value[1]
     + u*3*t*t*value[2]
     + t*t*t*value[3]
```

重点：

- `x` 与 `y` 的 member-hint owner 是进程级共享状态，并被 position control curve 的
  main/nested x/y 读取复用；
- `count` 在第一次 numeric lookup 之前读取；
- endpoint 的准确机器 branch 是 `PL` / `LE`：普通值表现为 `>=` / `<=`，但
  unordered 也会命中；stride 的 `MI` 只在 ordered `<` 时继续；
- interior segment 步幅固定为 3，没有循环上限或 count guard；
- stride 取得的是 `segmentEnd`，四个控制点从 `segmentEnd - 3` 开始；选中 segment 后，
  仍会依次读取四个 x numeric element，转换成 real 后丢弃，再读
  对应 y；这是动态对象可观察行为，不能因数值未使用而删掉；
- x 只选择 segment，不反求 Bezier parameter；多项式仍直接使用原始 `t`；
- 上述乘法括号是机器级共同分组，不可改成常见 Bernstein 形式后声称等价。

`count==0` 时仍计算 `last=-1` 并走 ordinary `PropGetByNum`；没有空数组 fallback。
`t=NaN` 时第一次 endpoint compare 已 unordered，`PL` 立即命中；在一次 Count 后只读
`x[0]` 与 `y[0]` 并返回后者。若 first gate 未命中但 `x[last]` 为 NaN，`LE` 会返回
`y[last]`；stride 位置为 NaN 时 `MI` 不继续。属性调用的 HRESULT 按 motion property
helper 习惯被忽略，但 object/real 转换异常自然传播。以上更正来自 2026-08-16 V149 对
四端 raw instruction 的 fresh 复核；完整证据见
`motionplayer_shared_easing_nested_ncb_segment_base_unordered_four_binary_2026-08-16.md`。

旧 portable 实现有两份 evaluator：header 版没有 x/y hints，VariableTrack lambda 虽有
hints却使用了另一组多项式分组；两者都漏掉了 interior 四次 discarded-x 读取。本轮
合并为单一 out-of-line `evaluateVariableTrackEasing_guess`，供所有 caller 使用。

## nested position control curve

### 顶层与 segment 选择

共同属性读取顺序是 main `x`、main `y`、`t`、`s`；x/y 复用 easing 的共享 hint，
t/s 各有独立 process-wide hint。

2026-08-17 V179 已进一步闭合三个非 x/y hint 的精确四端数据拓扑：`t/s` 两个 size-4
全局槽紧随共享 x/y，selected-segment `p` 的独立 size-4 槽则紧邻共享 `Layer` class hint
之前；三者彼此及与 x/y/Layer 均不 alias。portable 定义已从旧 `motion::internal` TU-local
三槽迁到 `motion::detail` shared-global family。完整地址、xref、IDB 写回与 recorder 证据见
`motionplayer_position_control_tsp_hint_global_topology_four_binary_2026-08-17.md`。

```text
mainIndex = -3
segmentIndex = -1
do:
    next = real(knots[segmentIndex + 2])
    mainIndex += 3
    segmentIndex += 1
while next < inputT

knotStart = real(knots[segmentIndex])
knotEnd   = real(knots[segmentIndex + 1])
knotStartAgain = real(knots[segmentIndex])
segment = segments[segmentIndex]
splineX = segment["x"]
splineY = segment["y"]
splineP = segment["p"]
```

第三次 knot 读取不是反编译器重复打印：四端都实际再次 dispatch `t[segmentIndex]`，
分母使用 `knotEnd-knotStartAgain`。segment 的 p 也有独立共享 hint。

### segment 内 parameter

严格顺序是：先 `splineX[0]`，再读 `splineX.count`，最后才计算 localT。旧实现先算
localT/读 count，异常和副作用顺序不同。

```text
firstX = real(splineX[0])
count = splineX.count
localT = (inputT-knotStart)/(knotEnd-knotStartAgain)

if firstX >= localT:
    parameter = real(splineY[0])
else if real(splineX[count-1]) <= localT:
    parameter = real(splineY[count-1])
else:
    splineIndex = -1
    do:
        nextX = real(splineX[splineIndex+2])
        splineIndex += 1
    while nextX < localT

    x1 = real(splineX[splineIndex+1])
    x0ForDenominator = real(splineX[splineIndex])
    x0ForRatio       = real(splineX[splineIndex])  // repeated dispatch
    p1 = real(splineP[splineIndex+1])
    p0 = real(splineP[splineIndex])
    y1 = real(splineY[splineIndex+1])
    y0 = real(splineY[splineIndex])
    r = (localT-x0ForRatio)/(x1-x0ForDenominator)
    dx = x1-x0ForDenominator
    parameter = dx * (dx * (
          (r*(r*r)-r)*p1
        + ((1-r)*((1-r)*(1-r))-(1-r))*p0)) / 6
        + r*y1 + (1-r)*y0
```

没有 zero-denominator、count、index 或 NaN guard。inputT/localT 为 NaN 时 ordered
compare 同样落入第一个 interior interval并保留完整 dispatch 序列。

### 临时对象生命周期与 main cubic

`segment`、`splineX`、`splineY`、`splineP` 在 parameter 决定后立即按逆序析构，
然后才读取 main x/y 的四个控制点。旧 header helper 把这些 const Variant 全留到函数
末尾，改变了引用计数和动态对象析构时点。

main numeric reads 是 interleaved `x[i], y[i]`，共四对，从本轮选择出的
`mainIndex` 开始。最终权重精确为：

```text
u3 = (1-parameter)*3
w0 = (1-parameter)*((1-parameter)*(1-parameter))
w1 = parameter*((1-parameter)*u3)
w2 = parameter*(parameter*u3)
w3 = parameter*(parameter*parameter)
outX = ((w0*x0 + w1*x1) + w2*x2) + w3*x3
outY = ((w0*y0 + w1*y1) + w2*y2) + w3*y3
```

旧实现的 `u*u*u`、`p*u*u*3` 和 `dx*dx*...` 都是代数相同但 IEEE rounding 不同
的分组，现已逐项恢复。

## 三维 position interpolation

### gate 与 dead Variant

首先依次比较 src/dst 的 X、Y、Z。三轴精确相等时直接复制 src 到 out，并在读取
easing/control Variant type 之前返回。因此相等输入配非法非对象 Variant 也不抛。
任一轴 NaN 会使 equality gate 失败。

非相等路径先按 easing Variant 的非 Void tag 调共享 evaluator。随后默认构造一个
本地 `tTJSVariant`；四端都只把其 type/tag 写成 Void，payload 保持普通默认构造形状，
却仍在 control mode 的非平面轴插值前测试其 Type，并在函数尾执行析构。该分支永远
不工作，但这是四端一致的 dead/refcount-no-op source token，本轮没有“优化清理”。

### 无 control curve 的逐轴线性式

control Variant 为 Void 时，每一轴先做 `src==dst` 精确旁路。非相等表达式并非一个
统一 for-loop：

```text
X = (1-eased)*srcX + eased*dstX
Y = dstY*eased + srcY*(1-eased)
Z = dstZ*eased + srcZ*(1-eased)
```

后两轴虽然只有两个加数、通常得到相同数值，仍按共同反编译源形状直接展开，避免再
把三个轴压成会掩盖差异的循环。

### control curve 与坐标平面

control Variant 非 Void 时，先无条件求 `rotation[2]`。只有 coordinateMode 0/1 写
out；其他值在已经完成 easing/control dispatch 后完全不写三个输出槽。

mode 0（XY 平面）：

```text
dx = dstX-srcX; dy = dstY-srcY
outX = (srcX + dx*rotationX) - dy*rotationY
outY = srcY + (dx*rotationY + dy*rotationX)
outZ = equal ? srcZ : dstZ*axisEasing + srcZ*(1-axisEasing)
```

mode 1（XZ 平面）：

```text
dx = dstX-srcX; dz = dstZ-srcZ
outX = (srcX + dx*rotationX) - dz*rotationY
outY = equal ? srcY : dstY*axisEasing + srcY*(1-axisEasing)
outZ = (dz*rotationX + srcZ) + dx*rotationY
```

`axisEasing` 的二次 easing 只在前述 dead Variant 非 Void 时发生，正常永远保持第一
层 eased。括号尤其重要：测试使用大数使 `srcY + (dx*rotationY + dy*rotationX)`
得到 0，而旧左结合 `(srcY+dx*rotationY)+dy*rotationX` 得到 1。

## Portable 源码差异与修正

本轮 fresh decompile 前的本地差异包括：

1. 三个 helper 使用过时 Android 单端地址式函数名，position 的地址本身即不匹配；
2. easing 存在两份实现，未共享统一 x/y hints 和动态读序列；
3. easing interior 漏掉四次转换后丢弃的 x numeric read；
4. easing 与 control cubic 权重使用常见公式重排，未保留机器共同分组；
5. control curve 的 localT、firstX、count 顺序不对；
6. knots 的 start 与 splineX 的 x0 各漏掉一次重复 dispatch；
7. nested segment Variant 活到函数末尾，而不是 parameter 后立即逆序析构；
8. position 把三轴压成统一循环，抹掉逐轴源表达式；
9. mode-0 Y 和 mode-1 Z 使用左结合表达式，改变大数 rounding；
10. 四端共同的默认 Void dead Variant、Type check 和析构 token 缺失。

修正集中于 `PlayerInternal.h` 与 `PlayerFrameProgress.cpp`，并统一更新 timeline、
MotionSub 和 particle caller。compiled source 新增/触及注释不再写任何目标地址；全部
ABI/地址证据只保留在本文。

## 测试与构建

新增确定性测试：

- 自定义 TJS curve/array dispatch 记录 easing 的 property/numeric 调用；断言 x/y
  hint 非 null、不同且正是 process-wide owner；
- interior `t=0.5` 的 x numeric 序列必须是
  `[0,3,3,0,1,2,3]`，y 序列必须是 `[0,1,2,3]`；
- position 三轴全等时，非法 Integer easing/control Variant 不得被读取或抛异常；
- 使用真实 TJS Dictionary/Array 构造 nested control curve，验证常量 rotation 输出；
- 大数 probe 锁定 mode-0 Y 的 native grouping（native `0`，旧表达式 `1`）；
- control 存在且 coordinateMode 为 2 时，三个预填输出保持完全不变。

验证结果：

- `motionplayer` Web Debug 静态库编译成功；
- 完整 Web Debug 最终 `index.html/index.wasm` 链接成功；
- Wasmtime guest 的所有受影响对象重编、最终链接、exnref 转换成功；
- 完整 `motionplayer-dll.cpp` 使用当前 Web/Emscripten 实际参数执行
  `-fsyntax-only` 成功，仅有项目既有 `_tss` deprecation warning；
- 当前环境仍没有可运行的原生 Catch2 motionplayer executable，本文没有把
  syntax-only 误报为运行时测试通过；
- `git diff --check` 通过，仅有工作树既有 LF/CRLF 提示。

## IDB 改进与保存

四份 IDB 均完成：

- position/control helper 统一重命名为本文 `_guess` 名称；
- position 显式应用统一七参数类型，control 应用三参数类型；
- position/control/easing 的函数注释记录 gate、dead Variant、dispatch、临时对象
  生命周期和 discarded-x 行为；
- 对三层 helper 与所有四端 caller 做 fresh xref/decompile；
- 最终 12 个入口反编译均显示统一名称，position caller 明确引用 control 与 shared
  easing；
- 两个 32 位 decompiler 仍提示局部变量分配可能不精确，这是 Hex-Rays 对 hard-float
  栈/register 恢复的限制，不是四端语义分歧；
- 四份 IDB 最终原位保存均返回 `ok=true`。
