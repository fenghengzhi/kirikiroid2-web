# MotionPlayer timeline evaluator：四参考二进制闭环（2026-08-13）

## 1. 结论与证据范围

本记录只以 `reference/binaries/` 中当前四个参考二进制为证据源，不再把旧
`libkrkr2.so` 的地址、偏移或反编译注释当作事实。闭环对象是每个非根节点在
`Player::updateLayers` 中调用的单节点时间线求值器；四个入口已统一命名为
`Player_evaluateTimeline_guess`：

| 目标 | 求值器入口 | clip 槽步长 | 备注 |
|---|---:|---:|---|
| Android arm64-v8a | `0x696EC4` | 536 字节 | 颜色 helper 被编译器完全内联并化简 |
| Android armeabi-v7a | `0x573158` | 432 字节 | 保留独立颜色、角度 helper 调用 |
| iOS arm64 | `0x1000F6C34` | 536 字节 | 保留独立颜色、角度 helper 调用 |
| iOS armv7 | `0xF3894` | 420 字节 | 保留独立颜色、角度 helper 调用 |

四份实现的控制流、可见写入顺序、曲线调用次数、Variant 生命周期和类型分派
一致。差异来自 ABI、标准库布局和优化器是否内联/化简 helper，不是插件语义
分叉。2026-08-14 的指令级复核又补齐了三个浮点门槛在 NaN 时的
ordered-comparison 边界；详见第 9 节。

## 2. 调用契约与控制流

从四份调用点和入口寄存器/栈参数共同恢复的源级契约为：

```cpp
bool Player_evaluateTimeline_guess(
    MotionNode &node, double currentTime, bool dirtyArg);
```

高层伪代码如下。这里的 `active`、`other` 由节点当前活动槽索引选择：

```text
dirty = dirtyArg || node.flags
active = node.activeSlot()
other  = node.otherSlot()

if active.done:
    return dirty

if !active.crossfading || other.done:
    if !dirty:
        return false
    copyActivePayloadInNativeOrder()
    return true

time = node.parameterEntry ? node.parameterEntry->value : currentTime
elapsed = time - active.startTime
if active.ti != 0:
    elapsed = active.ti * uint32(elapsed / active.ti)
ratio = elapsed / (other.startTime - active.startTime)

oldRatio = node.timelineEvalRatio
if NOT ordered(abs(ratio) >= 1e-7):   # less-than OR unordered
    node.timelineEvalRatio = ratio
    if dirty OR ordered(abs(oldRatio - ratio) >= 1e-7):
        copyActivePayloadInNativeOrder()
        return true
    return false

if !dirty AND NOT ordered(abs(oldRatio - ratio) >= DBL_EPSILON):
    return false

node.timelineEvalRatio = ratio
interpolatePayloadInNativeOrder(ratio)
return true
```

关键点：

- `active.done` 不写任何输出；返回值只是已经折叠过节点 flags 的 `dirty`。
- 非 crossfade 或另一槽已经 done 时，`dirty == false` 直接返回 `false`；否则走
  完整复制分支。
- 参数化节点从已绑定参数表项取时间；普通节点使用调用者的当前求值时间。
- `ti` 量化不是浮点 `floor`：四份代码都先做无符号 32 位整数转换，再乘回
  `ti`。转换指令本身带向零舍入与 unsigned saturation，随后乘法在 32 位整数域
  回绕；完整畸形输入边界见第 8 节。
- 近零路径先把新 ratio 写回节点，再以 ordered `>= 1e-7`
  判定变化是否足够大；普通路径用 ordered `>= DBL_EPSILON`。这两个
  阈值、写回时机与 unordered 结果都不能合并。
- ratio 分母为零及后续 NaN/Infinity 仍没有显式验证；但 `ti` 商到 unsigned 的
  指令级结果已经在第 8 节由四端共同闭合，不再属于未知边界。

## 3. 输出顺序与可观察副作用

### 3.1 复制分支

完整复制分支按以下顺序写入：

```text
flipX, flipY
angle
scaleX, scaleY
slantX, slantY
position X/Y/Z
四个 packed color
opacity
mesh（仅 meshType == 1）
nodeType 4 的九个粒子字段 / nodeType 5 的 FOV / nodeType 10 的 timespan
```

若不需要复制，旧输出保持原状。复制分支在 `meshType == 1` 时总是把 active slot
vector copy-assign 到节点输出；active vector 为空也会清空旧输出。“两端都为空时不写
节点输出”只属于下面的 crossfade mesh 分支。

### 3.2 crossfade 分支

crossfade 的精确求值顺序为：

```text
1. flipX, flipY（直接取 active）
2. angle
3. scaleX
4. scaleY
5. slantX
6. slantY
7. position X/Y/Z
8. 构造一个默认 Void tTJSVariant
9. 四个 packed color
10. opacity
11. mesh
12. nodeType 4 / 5 / 10 的专有字段
13. 析构默认 Void tTJSVariant
```

顺序不仅影响数值，也影响脚本曲线 dispatch 的调用次数和异常时机：

- angle 只在两端角度不等时读 `acc` 曲线，并按最短的 ±180° 路径调整目标角，
  插值后只做一次 `[0, 360)` 归一化。
- `scaleX`、`scaleY` 分别先比较各自端点。只要该分量不等，就各自独立读取并
  调用同一个 `zcc` 曲线，不能缓存一次曲线结果给两个分量。
- `slantX`、`slantY` 对 `scc` 具有完全相同的独立调用规则。
- position 在上述四个标量之后调用位置插值 helper；把它提前会改变脚本异常
  和重入的顺序。
- 默认构造的 Void Variant 从 position 返回后一直活到 mesh 和类型分派结束。
  opacity、粒子九字段、FOV、timespan 的公共标量 helper 在端点不等时会检查
  它，但其类型恒为 Void，所以这些路径使用原始 ratio。

为锁定上述可观察行为，单元测试新增了同一 dispatch 在 X/Y 两个不等分量上
各被调用两次的断言：scale 记录 `{x, y, x, y}`，slant 也记录
`{x, y, x, y}`。

## 4. 颜色、opacity、mesh 与类型分派

### 4.1 packed color 的真实可达路径

三份保留独立函数的 helper 地址为：

| 目标 | `PackedColorInterpolation_evaluate_guess` |
|---|---:|
| Android armeabi-v7a | `0x571F90` |
| iOS arm64 | `0x1000F5964` |
| iOS armv7 | `0xF2428` |

Android arm64 在 evaluator 中已将同一逻辑内联并化简为直接复制。源代码形态对
四个角颜色均传入：

```cpp
interpolateColor(activeColor, activeColor, active.ccc, ratio)
```

helper 的第一步是比较两个 packed 端点；相等便立刻返回，早于 Variant 类型
检查和曲线 dispatch。因此 evaluator 的真实行为是复制 active 的四个颜色，
不会读取颜色曲线。不能把存在 helper 误解成 active→other 颜色 crossfade。

若单独考察 helper 的不可达非等端点分支，它会在曲线非 Void 时先重映射
ratio，然后以 `int(ratio * 256)` 为定点权重，用 `0x00FF00FF` 成对计算四个
8-bit 通道。这个分支有助于复原源代码结构，但不构成 evaluator 对畸形端点或
极端 ratio 的跨平台行为承诺。

### 4.2 opacity

opacity 两端先按无符号 32 位值提升到 double，使用默认 Void Variant，因此
直接采用原始 ratio。最终先采用 half-away-from-zero：ordered 非负或 unordered 值走
`floor(x + 0.5)`，ordered 负值走 `ceil(x - 0.5)`；随后不是 signed `int` cast，而是
`FCVTZU` / `VCVT.U32.F64` 的 unsigned 32-bit toward-zero saturation。NaN 与负结果写零，
正溢出写 `UINT32_MAX` 原始 word；没有 0..255 夹取。完整四端指令和边界见
`motionplayer_timeline_opacity_unsigned_conversion_four_binary_2026-08-16.md`。

### 4.3 mesh

- 仅 `meshType == 1` 进入 mesh 路径。
- 复制分支总是 copy-assign active vector；active 为空会清空节点输出。
- crossfade 两端实际 vector 都为空时不触碰节点输出 vector。
- crossfade 只有一端为空时，该端使用进程级 4×4 identity Bezier patch，而不是静态空
  vector；这是 2026-08-14 parent-mesh 纵向已恢复的边界。
- 两端长度不一致沿原生 helper 的异常路径抛出；实现不做补齐或最短长度截断。

### 4.4 仅有的 nodeType 分派

求值器的类型 switch 只有三种有效 case：

- `nodeType == 4`：九个粒子双精度字段。
- `nodeType == 5`：camera FOV。
- `nodeType == 10`：feedback timespan。

所有字段都先比较两端；相等时不检查默认 Variant，不等时因 Variant 为 Void 而
使用原始 ratio。其余节点类型没有 evaluator 专有写入。

## 5. 节点输出偏移交叉表

以下只记录本次四份 evaluator 直接读写并重新核验的物理输出。偏移用于证据
审计，不进入编译源码注释；便携 `MotionNode` 采用源级成员组织，不伪装成任一
目标 ABI 的字节镜像。

| 逻辑输出 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| packed colors 0..3 | `+100..+112` | `+84..+96` | `+100..+112` | `+84..+96` |
| flip X/Y | `+1507/+1508` | `+1267/+1268` | `+1523/+1524` | `+1235/+1236` |
| position X/Y/Z | `+1512/+1520/+1528` | `+1272/+1280/+1288` | `+1528/+1536/+1544` | `+1240/+1248/+1256` |
| angle | `+1536` | `+1296` | `+1552` | `+1264` |
| scale X/Y | `+1544/+1552` | `+1304/+1312` | `+1560/+1568` | `+1272/+1280` |
| slant X/Y | `+1560/+1568` | `+1320/+1328` | `+1576/+1584` | `+1288/+1296` |
| opacity | `+1576` | `+1336` | `+1592` | `+1304` |
| mesh type | `+2000` | `+1720` | `+2016` | `+1684` |
| mesh output vector | `+2024` | `+1740` | `+2040` | `+1704` |
| particle field 0 | `+2224` | `+1896` | `+2240` | `+1860` |
| particle field 8 | `+2288` | `+1960` | `+2304` | `+1924` |
| camera FOV | `+2368` | `+2032` | `+2384` | `+1996` |
| feedback timespan | `+2432` | `+2096` | `+2448` | `+2056` |

角度 helper 在三份未内联目标中的地址：Android armv7 `0x573A30`、iOS arm64
`0x1000F7580`、iOS armv7 `0xF42B0`。Android arm64 将其控制流内联。

## 6. `blendMode` 的纠错边界

四份 evaluator 都不写累计状态中的 blend mode。普通 prepared-render-item 构造
直接读取当前 active clip 槽的 blend mode；对应构造函数入口为：

| 目标 | `Player_appendPreparedRenderItems_guess` |
|---|---:|
| Android arm64-v8a | `0x6BF714` |
| Android armeabi-v7a | `0x58B178` |
| iOS arm64 | `0x1001148F8` |
| iOS armv7 | `0x1123D8` |

因此便携实现已做两项纠错：

1. evaluator 的 active-copy 分支不再写 `node.accumulated.blendMode`；
2. prepared render item 改为从 `node.activeSlot().blendMode` 取值。

2026-08-13 的 `colorWeight` 垂直复核同时纠正了 Android arm64 的旧命名：
`0x6BF714` 才是递归 prepared-render-item 构造器；`0x6C2208` 是后续的
render-command materialization，第四参数是渲染上下文/裁剪指针而非继承颜色。

后续四参考粒子链复核进一步证明，旧注释所谓的
`AccumulatedState::blendMode` 实际是紧邻块的 `DeltaState::opacity`：粒子创建
把父粒子节点的 evaluated opacity 写入子 Player 根节点 delta opacity，并在值
变化时置 delta dirty。因此便携模型已删除不存在的 accumulated blend 字段。

## 7. 便携实现改动与验证

本次实现校正包括：

- 将 evaluator 参数顺序统一为 `(node, currentTime, dirtyArg)`。
- 恢复原生写入顺序，并让 scale/slant 的 X/Y 分量分别求值曲线。
- 把一个默认 Void Variant 的生命周期扩展到颜色、opacity、mesh 和类型分派
  之后。
- 恢复 active→active 的颜色 helper 源级形态，同时保持其可达行为为直接复制。
- 把 mesh 与 type 4/5/10 写入收回同一 payload helper，避免调用者改变异常顺序。
- 从 evaluator/root-copy 中删除未经四参考支持的累计 blendMode 写入，render
  item 改读 active 槽。
- 新增测试专用 evaluator 入口和曲线 dispatch 次数测试，并更新 render topology
  测试以验证 active 槽 blend mode。

验证结果：

- Web debug 全量目标成功构建；复查 `ninja: no work to do`。
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web 目标的真实 include、define、
  ABI 和 Emscripten 参数执行整翻译单元 `-fsyntax-only`，通过；仅有项目既有的
  `_tss` 字面量运算符弃用警告。
- Wasmtime debug `krkr2_wasmtime_guest` 成功构建；复查
  `ninja: no work to do`。
- 四份 IDB 均写入 evaluator、颜色/角度 helper、render-item 构造链的恢复命名
  与语义注释，并成功原位保存。

## 8. `ti` 商的 unsigned 转换与乘法回绕补闭环（2026-08-14）

### 8.1 四端指令链

| 目标 | quotient→u32 | u32 乘法 | u32→double |
|---|---:|---:|---:|
| Android arm64-v8a | `FCVTZU W9,D0` `0x697088` | `MUL W8,W8,W9` `0x69708C` | `UCVTF D0,W8` `0x697090` |
| Android armeabi-v7a | `VCVT.U32.F64 S2,D1` `0x573334` | `MULS R2,R3` `0x57333C` | `VCVT.F64.U32 D1,S2` `0x573342` |
| iOS arm64 | `FCVTZU W9,D0` `0x1000F6E10` | `MUL W8,W9,W8` `0x1000F6E14` | `UCVTF D0,W8` `0x1000F6E18` |
| iOS armv7 | `VCVT.U32.F64 S0,D17` `0xF3AAE` | `MULS R2,R3` `0xF3AB6` | `VCVT.F64.U32 D17,S0` `0xF3ABC` |

共同源级语义为：

```text
quotient = elapsed / double(ti)
stepCount = unsignedSaturatingRoundTowardZero32(quotient)
quantizedElapsed = uint32(ti * stepCount)   // modulo 2^32
elapsed = double(quantizedElapsed)
```

Arm 的 `FPToFixed` 语义先向零舍入，再把整数结果 saturate 到 unsigned 32 位范围；
NaN 在 `FPUnpack` 中给出 0.0 数值，因而转换结果为 0。由此四端共同的值结果是：

- NaN、负数、`-Infinity`、正负零：`0`；
- `(0, 2^32)`：向零截断后的 unsigned 值；
- `>= 2^32` 与 `+Infinity`：`UINT32_MAX`。

转换可能更新原生浮点异常状态；插件没有读取该状态，Web 平台也没有对应的可观察
FPSR，因此 portable helper 只复刻数值结果。指令语义参考 Arm 官方
[ARMv7-A/R Architecture Reference Manual](https://documentation-service.arm.com/static/5f8dc043f86e16515cdbbc92)
中的 `FPToFixed`/`FPUnpack` 伪代码，以及
[Armv8 A64 ISA overview](https://developer.arm.com/-/media/Files/pdf/graphics-and-multimedia/ARMv8_InstructionSetOverview.pdf)
对 `FCVT...U` rounding suffix 的说明。

### 8.2 原端口偏差与源码恢复

旧源码写成：

```cpp
double(ti) * uint32(quotient)
```

Web Debug 对象码因此是 `i32.trunc_sat_f64_u -> f64.convert_i32_u -> f64.mul`：转换
已经碰巧与 Arm 的 saturating value 相同，但乘法发生在 double 域，不会回绕。四端
则始终先执行 `MUL/MULS` 的 32 位低半结果，再 unsigned-convert 回 double。

当前源码增加 `doubleToUnsignedIntTowardZeroSaturated_guess`，显式处理 NaN、负值与
正溢出，避免依赖越界 floating-to-integer 的 C++ 未定义行为；随后以两个
`std::uint32_t` 相乘并把结果转回 double。重建后的 Wasm 对象码已经变为：

```text
i32.trunc_sat_f64_u
i32.mul
f64.convert_i32_u
```

同时把残留的旧单库地址式源码名 `evaluateTimelineLike_0x699AE4` 迁移为四份恢复
IDB 已采用的 `evaluateTimeline_guess`。

### 8.3 回归、构建与 IDB 回写

- 新增转换端点回归：NaN、正负 Infinity、负数、正负零、分数、`UINT32_MAX`
  上沿与 `2^32`；
- 新增 evaluator 回归：`ti=0x80000001`、stepCount=2 时，32 位乘积先回绕成 2，
  ratio 因而为 `0.2`；
- `Web Debug Build` 完整重建并最终链接通过；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用真实 Web Debug 参数执行
  Emscripten syntax-only，通过，仅有既有 `_tss` warning；
- 四份 IDB 均补入正确 `(node, double currentTime, bool dirtyArg)` 原型，以及转换、
  回绕乘法、转回 double 的行级注释，并重新反编译、原位保存。

## 9. ratio 门槛的 ordered/unordered 边界补闭环（2026-08-14）

### 9.1 四端条件码

三个门槛都是“比较后以 ordered GE 跳转/物化”，而不是可在 NaN 上随意
取反的 `<`：

| 目标 | `abs(ratio)` vs `1e-7` | near-zero old/new vs `1e-7` | normal old/new vs epsilon |
|---|---:|---:|---:|
| Android arm64 | `FCMP` `0x6970B4`; `B.GE` `0x6970B8` | `FCMP` `0x6970CC`; `CSET GE` `0x6970D0` | `FCMP` `0x6970F8`; `B.GE` `0x6970FC` |
| Android armv7 | `VCMPE` `0x57335E`; `BGE` `0x573366` | `VCMPE` `0x57337C`; `MOVGE` `0x573386` | `VCMPE` `0x5733A2`; `BGE` `0x5733AA` |
| iOS arm64 | `FCMP` `0x1000F6E3C`; `B.GE` `0x1000F6E40` | `FCMP` `0x1000F6E50`; `CSET GE` `0x1000F6E54` | `FCMP` `0x1000F6E80`; `B.GE` `0x1000F6E84` |
| iOS armv7 | `VCMPE` `0xF3AD8`; `BGE` `0xF3AE0` | `VCMPE` `0xF3AF6`; `MOVGE` `0xF3B00` | `VCMPE` `0xF3B1C`; `BGE` `0xF3B24` |

Arm 浮点比较对 unordered 设置的 NZCV 不满足 GE，因而可见语义是：

1. `abs(ratio) == 1e-7` 属于普通 interpolation 分支；
2. `ratio` 为 NaN 时，首个 ordered GE 为 false，落入 near-zero/active-copy 一侧；
3. near-zero 一侧先写 `timelineEvalRatio`；若 node 干净且 old/new 差为 NaN，
   第二个 ordered GE 也为 false，因此返回 false 而不写 payload；
4. dirty 与这个 ordered-GE 结果做 OR，所以 dirty + NaN ratio 仍走 active-copy，
   不会将 NaN 用于 crossfade 插值；
5. 普通一侧若 old/new 差为 NaN 且 node 干净，epsilon 的 ordered GE 为 false，
   直接返回 false；此时新 ratio 尚未写入。

这解释了为什么 Hex-Rays 显示的 `fabs(x) < threshold` 不能直接转写为
C++ `<`：对有序数值两者是 CFG 上的反面，但 NaN 上 `<` 与 `>=` 都为
false，不是逻辑互补。指令条件语义与第 8 节使用同一批 Arm 官方参考。

### 9.2 原端口偏差与源码修复

旧便携实现把三个 native ordered-GE gate 都通过 `<` 的反条件表达。
对有限有序数值看似等价，但会产生两个 NaN 偏差：

- NaN ratio 被错送入 interpolation，而不是 near-zero/active-copy；
- old/new 差为 NaN 时，干净 node 错误继续写 ratio 和 payload，而不是按 native
  ordered GE 的 false 结果早退。

当前源码先显式计算 `fabs(...) >= threshold` 的 bool，再按原 CFG 使用该
ordered 结果：首个 gate 用 `!orderedGE` 进入 near-zero，后两个 gate 把
`orderedGE` 与 dirty 组合。没有为 NaN 增加上游“修复值”或 clamp。
同时将该 evaluator 翻译单元中仍固化旧 `libkrkr2.so` 地址的八个私有
`Like_0x...` helper 统一改为地址无关的语义 `_guess` 名；未声称恢复出未保留的
原始 C++ identifier。

### 9.3 回归、构建与 IDB 回写

- 回归 1：干净 node + NaN ratio 写入 NaN ratio，保留 payload，返回 false；
- 回归 2：dirty node + NaN ratio 复制 active payload，不做 NaN interpolation；
- 回归 3：普通 finite ratio + NaN old ratio 在干净 node 上保留 old NaN/payload，
  返回 false；
- 回归 4：`abs(ratio) == 1e-7` 进入 interpolation 一侧；
- `Web Debug Build` 完整重建并最终链接通过；完整测试 TU 使用真实 Web
  Debug 参数执行 Emscripten syntax-only 通过，仅有仓库既有 `_tss` warning；
- 重建后的 Wasm 对象码三个门槛均保留 `f64.abs -> f64.ge`，没有被降成
  NaN 语义不等价的 `f64.lt`；
- 四份 IDB 的函数入口、首个 threshold branch、near-zero diff 物化和
  epsilon branch 均写入 ordered/unordered 语义，并原位保存。

## 10. type-specific 输出 fresh 复核（2026-08-15）

### 10.1 四端分派位置

为消除 `motionplayer_emote_init_four_binary_2026-08-12.md` 仍写着“尚未独立封账”的
旧状态，本轮重新从四个当前 recovery IDB 检查复制分支、crossfade 尾部和字段消费者：

| 目标 | active-copy type switch | crossfade type dispatch |
|---|---:|---:|
| Android arm64 | `0x696FF4` | `0x6974C0` |
| Android armv7 | `0x573280` | `0x5736E2` |
| iOS arm64 | `0x1000F6D7C` | `0x1000F71B4` |
| iOS armv7 | `0xF39FC` | `0xF3E92` |

四端只有三个有效 node type：

- type 4：按 `prtFmin, prtF, prtVmin, prtV, prtAmin, prtA, prtZmin, prtZ,
  prtRange` 顺序发布九个 double；
- type 5：发布 camera FOV；
- type 10：发布 feedback timespan；
- 其他类型直接越过该尾部，三个输出块全部保留旧值。

Android armv7 重新按指令地址核验后，九字段物理范围是 `+1896..+1960`；本文件旧表写成
`+1904..+1968`，会与紧随其后的 retained particle Array Variant `+1968` 重叠，现已纠正。

### 10.2 gate、复制与插值

type-specific 写入服从 evaluator 既有早退，绝不是每次调用都刷新：

1. active slot done：立即返回合成 dirty，任何专有输出都不写；
2. 非 crossfade/other done 且 clean：返回 false，不写；
3. active-copy：公共 scalar/position/color/opacity 写完，`meshType==1` 的 vector assignment
   正常返回后，再按 type 复制专有值；
4. crossfade：公共 interpolation、identity-patch mesh 处理完成后，再按 type 插值专有值；
5. ratio near-zero 的 copy 路径与普通 active-copy 使用同一专有复制尾部。

crossfade 尾部仍持有 position 之后构造的默认 Void Variant。每个专有 double 都先比较
active/other：相等直接复制 active；不等才进入公共 scalar helper。Variant 恒为 Void，故可达
路径都使用原始 ratio 作线性插值；没有 clamp、整型量化或 node-type-specific easing。

### 10.3 公共复制顺序迁移

四端 active-copy 的共同 store/call 顺序是：

```text
flipX/Y
angle
scaleX/Y
slantX/Y
position X/Y/Z
packed colors 0..3
opacity
mesh vector assignment
type 4/5/10 output
```

本地 helper 曾在后续重构中把 position 放到 angle 前、把 opacity 放到 packed colors 前，
与同文件既有四端分析矛盾。本轮恢复上述顺序；所有普通 store 都继续位于可能抛异常的 mesh
vector assignment 之前，type-specific 输出继续位于其后。因此 mesh allocation/copy 抛异常时，
公共 scalar 已发布，但 type-specific 旧值仍保留。

### 10.4 回归与数据库

新增 evaluator 回归覆盖：

- active-copy 的 type 4 九字段、type 5 FOV、type 10 timespan；
- ratio `0.25` 的三类 crossfade 数值；
- type 6 不写任何专有输出；
- active done 即使返回 dirty=true 也保留旧 FOV；
- clean non-crossfade 返回 false 并保留旧 timespan。

四个 recovery IDB 已在两个 type dispatch 和 active-copy 顺序点补入 fresh 语义注释/bookmark
并原位保存。完整测试 TU 的 Emscripten syntax-only、`Web Debug Build` 和
`git diff --check` 均通过。
