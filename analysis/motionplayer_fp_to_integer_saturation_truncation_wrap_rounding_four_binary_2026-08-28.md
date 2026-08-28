# MotionPlayer 浮点到整数饱和、截断、wrap 与舍入四参考横向审计

日期：2026-08-28  
原始任务：`MP-B04`

## 1. 结论

四个参考二进制共同证明，motionplayer 没有一个可全局替换为 C++ cast、`lround` 或 clamp 的
“浮点转整数”策略。必须按每个 publication call site 保留以下彼此独立的阶段：

1. `double/float -> signed int32`：NaN 为 0，正/负溢出分别饱和到 `INT_MAX/INT_MIN`，
   有限范围内向零截断；
2. `double -> unsigned int32`：NaN、负数和负无穷为 0，正溢出饱和到 `UINT_MAX`，
   有限范围内向零截断；
3. timeline opacity：先以 `value < 0 ? ceil(value-0.5) : floor(value+0.5)` 做
   half-away-from-zero，再走 unsigned 饱和；
4. command-list Bezier division：double 先走 signed-int64 饱和，但 publication 由原始
   `scaledDivision < 50` 的 ordered compare 决定，因此 NaN 最终发布 50；
5. packed word arithmetic：浮点转换完成后，乘法、加法、减法继续在 uint32/W-register
   域内自然回绕，之后才除法或按 signed word 重解释；
6. byte/channel publication：脚本 Integer 取低 32 位，再按低字节/移位拆通道；stencil 和
   opacity 等字段的 byte 写入是窄化，不是数值饱和；
7. 整数除零：两个 AArch64 参考在本体中直接证明 `UDIV` 零除数结果为 0；两个 ARMv7
   参考把该边界交给外部 EABI helper，helper 的零除数策略不在目标二进制字节内。

现有 Web 实现已经把转换、舍入、word arithmetic、byte narrowing 和除法分成独立 helper/步骤，
没有发现 task-local production 偏差。本轮没有修改 production C++；既有单元测试已经覆盖各类
有限边界、NaN/Inf、半整数、`2^31/2^32/2^63`、零除数和回绕实例。

## 2. 本轮 fresh 四端证据

本轮使用原生 `mcp__idalib__*`，对 64 个独立函数范围重新执行完整 decompile、disassembly、
constants/callee 和 `xrefs_to/from` 审计。所有范围反编译成功，所有 disassembly cursor 完成。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | `xrefs_from` | IDB 更新 |
|---|---:|---:|---:|---:|---|
| Android arm64 | 16 | 15,041 | 40 | 16 | 16 条任务注释、1 个书签 |
| Android armv7 | 16 | 11,280 | 27 | 16 | 16 条任务注释、1 个书签 |
| iOS arm64 | 16 | 9,350 | 32 | 16 | 16 条任务注释、1 个书签 |
| iOS armv7 | 16 | 13,121 | 27 | 16 | 16 条任务注释、1 个书签 |
| 合计 | 64 | 48,792 | 126 | 64 | 64 条注释、4 个书签；四库原位保存 |

本轮分母覆盖 blink、updateLayers root、timeline evaluator、particle emitter、calcView、
getCommandList、prepared item、command builder、Canvas、accurate SLA、D3D deep renderer、mesh
submit、layer-id require/release、EmotePlayer packed color setter 和 SourceCache packed tint。

## 3. 四端函数映射

| 语义范围 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| blink step / wait conversion | `0x660FBC`，250 | `0x552472`，245 | `0x1001A27A0`，223 | `0x1A19D8`，262 |
| updateLayers root / word composition | `0x6B871C`，685 | `0x5856E0`，764 | `0x10010E544`，719 | `0x10BE5C`，821 |
| timeline evaluator / ti + opacity | `0x696EC4`，634 | `0x573158`，631 | `0x1000F6C34`，585 | `0xF3894`，750 |
| particle emitter / count + source index | `0x6BC1B0`，193 | `0x588820`，172 | `0x100111A6C`，167 | `0x10F2CC`，178 |
| calcView / unsigned mesh division | `0x6CE908`，1,349 | `0x594958`，798 | `0x1001201CC`，613 | `0x11EED4`，977 |
| getCommandList / signed-int64 division | `0x6D0E2C`，1,315 | `0x595FF0`，838 | `0x100121EB0`，596 | `0x120CF8`，1,032 |
| append prepared / signed-int32 division | `0x6BF714`，1,507 | `0x58B178`，944 | `0x1001148F8`，820 | `0x1123D8`，1,034 |
| build commands / packed publication | `0x6C2208`，1,766 | `0x58C7C4`，1,348 | `0x1001167BC`，1,083 | `0x114118`，1,582 |
| Canvas renderer / cell split | `0x6C4820`，2,363 | `0x58E2CC`，1,891 | `0x1001186E0`，1,531 | `0x11653C`，2,155 |
| accurate SLA / cell split | `0x6C7088`，2,051 | `0x590468`，1,676 | `0x10011A9E8`，1,328 | `0x118D70`，1,955 |
| D3D deep / byte narrowing | `0x6AB39C`，606 | `0x57D3DC`，655 | `0x100104450`，545 | `0x101850`，888 |
| mesh submit / point integerization | `0x69AFE4`，1,829 | `0x575800`，871 | `0x1000F974C`，787 | `0xF685C`，1,035 |
| require layer id / uint32 wrap | `0x6A8A74`，47 | `0x57C258`，43 | `0x100102D40`，30 | `0x100240`，32 |
| release layer id / uint32 consumer | `0x6A8B30`，51 | `0x57C2C8`，49 | `0x100102DB8`，52 | `0x10028A`，47 |
| EmotePlayer setColor / low word + bytes | `0x66FB5C`，151 | `0x55A6E8`，49 | `0x1001ADA3C`，46 | `0x1AD0CC`，52 |
| SourceCache tint / packed byte arithmetic | `0x6A48F8`，244 | `0x57A754`，306 | `0x10010032C`，225 | `0xFD4B4`，321 |

Android arm64 的 `0x6D0E2C` 落在 forward/shared body 归属范围中，但本轮反汇编解析到完整
1,315 指令共享实现；没有把 8-byte forward stub 误计作完整 getCommandList 证据。

## 4. 转换规则总表

| call site | 浮点阶段 | 物化阶段 | 之后的整数阶段 | 关键边界 |
|---|---|---|---|---|
| blink wait | float/double interval | signed int32 饱和、向零截断 | 保存为 wait counter | NaN 0，±Inf 饱和值 |
| camera quantization | `primary*dx + secondary*dy + 0.5` | signed int32 饱和、向零截断，再转 float | 后续角度/相机计算消费 float | `+0.5` 不是 round-to-nearest；负数仍向零 |
| particle emit count/source index | double expression | signed int32 饱和、向零截断 | count 控循环，index 控 source 选择 | negative count 不发射；negative index 保留负值语义 |
| timeline `ti` | double interval product/ratio | unsigned int32 饱和、向零截断 | uint32 乘法可先回绕再返回 double | `0x80000001 * 2 -> 2` |
| timeline opacity | interpolation double | half-away，再 unsigned int32 饱和 | raw word 保存到 signed field；composition 以 uint32 消费 | `-0.5` 先到 -1，再饱和为 0；`3.5 -> 4` |
| own mesh division | ratio * raw uint32 division | unsigned int32 饱和、向零截断 | 只对 `[50,0x80000000)` 作 signed-domain cap | high-bit word 绕过 50 cap |
| inherited/calcView mesh division | 同上 | 同上 | unsigned `>=50` 全部 cap 50 | high-bit word 也变 50 |
| prepared Bezier division | ratio * raw uint32 division | signed int32 饱和、向零截断 | signed `>=50` cap | negative/`INT_MIN` 保留 |
| serialized Bezier division | scaled double | signed int64 饱和、向零截断 | 由原始 double ordered `<50` 选择 converted/50 | NaN converted=0，但发布 50 |
| Canvas/SLA cell split | width/height double | 各自 unsigned int32 饱和 | denominator、numerator、split、`+1` 全在 uint32 wrap 后按 signed word发布 | `UINT_MAX+1 -> 0`；A64 zero divisor -> 0 |
| packed-color interpolation | ratio*256 | signed int32 饱和、向零截断 | reinterpret uint32 后逐 byte 插值 | 饱和阈值为 ratio ±8,388,608 |
| script packed color | TJS signed Integer | 低 32 位 | shift/mask 取 RGBA bytes | 高 32 位丢弃，不做颜色 clamp |
| stencil/opacity byte | uint32/signed word | 低 8 位写入 | 后续按 byte 消费 | 256 窄化为 0；诊断不改变写入规则 |

## 5. signed int32 饱和不是普通 C++ cast

四端 AArch64/ARMv7 的 lowering 均对应目标 ISA 的 saturating conversion profile。共享伪代码是：

```text
to_s32(value):
    if unordered(value): return 0
    if value >= 2^31: return INT_MAX
    if value <= -2^31: return INT_MIN
    return trunc_toward_zero(value)
```

这个规则覆盖 blink wait、particle emit/source index、camera node quantization、prepared Bezier
division、render point conversion和 packed-color weight。不能写成直接 `static_cast<int32_t>` 后再检查，
因为越界浮点到整数在 C++ 语言层没有 reference 所证明的确定结果。

camera helper先加 `0.5`，再执行上述向零转换。它只对某些正值表现得像四舍五入；负半轴不是
对称的 nearest rounding。因此本地保留 `expression + 0.5 -> to_s32 -> float` 的阶段顺序。

## 6. unsigned int32 饱和与 raw word

共享伪代码是：

```text
to_u32(value):
    if unordered(value) or value <= 0: return 0
    if value >= 2^32: return UINT_MAX
    return trunc_toward_zero(value)
```

物化后的结果是 32-bit word，不应因本地字段声明为 signed int 就立刻进入 signed arithmetic。
timeline `ti`、opacity 和 mesh division 都会继续以 uint32 参与比较或乘法；只有明确的 signed compare
或最终字段消费才按 `int32_t` 重解释。`memcpy`/等宽 word conversion 用来避免 implementation-defined
或错误的数值 clamp。

## 7. 舍入只存在于明确的预处理 call site

timeline opacity 的 shared source shape 是：

```text
rounded = value < 0 ? ceil(value - 0.5) : floor(value + 0.5)
word = to_u32(rounded)
```

这不是 `std::round` 后再 cast 的可随意替代形式，因为 unordered compare、±Inf、巨大有限值以及
后续 unsigned 饱和都是可观察边界。尤其 NaN 走 nonnegative expression 分支，`floor(NaN)`仍是 NaN，
最后由 `to_u32` 变 0。

其余 conversion call site 没有这个预舍入步骤。packed color 是乘 256 后向零，prepared/serialized
Bezier division也是向零，不能因 opacity 使用 half-away 就全局换成 `round`。

## 8. wrap 发生在 conversion 之后

reference 的 W-register/uint32 算术明确证明这些不是数学整数：

- opacity composition：`uint32(lhs) * uint32(rhs)`先模 `2^32`，再除 255；
- timeline `ti`：量化 word 的乘法可模 `2^32`，回绕后才转回 double 用于 ratio；
- cell split：`width + height`、`divisionWord * width`、`split + 1`、
  `divisionWord - split + 1`均模 `2^32`；
- layer id：post-increment 自然回绕，ordered set 只负责检测当前 word 是否已占用；
- point integerization 后的坐标偏移继续在 32-bit word 域发生，不能提升到 int64 后 clamp。

因此“先在 double/int64 精确计算，再最后 clamp 到 int32”会改变中间结果，即使正常 fixture 输出相同。

## 9. byte 窄化与 packed channel

setColor 的输入已经是 TJS Integer。四端都只消费低 32 位，并按 shift/mask 取四个通道，再变成
normalized float；负 Integer 等价于其低 32-bit two's-complement word。它不是把 Integer 数值限制到
`[0, UINT_MAX]`。

D3D/stencil和packed tint路径同样把低 byte 当协议字段。部分路径在计数超过255时打印overflow诊断，
但诊断与最终 byte store 是两个步骤；不能因为有日志就把256饱和成255。

## 10. 整数除零的平台边界

Canvas/SLA cell split 和 mesh grid split 在两个 AArch64 参考中使用本体内联 `UDIV`，零除数结果
可直接审计为0。两个 ARMv7参考调用外部运行时division helper；目标文件只证明call boundary，不能
从其字节证明helper内部结果。

Web实现采用两个AArch64目标直接证明的确定profile：

```text
u32_div_a64_profile(numerator, denominator):
    return denominator != 0 ? numerator / denominator : 0
```

这是明确的 ISA/runtime boundary，不是从普通 C++ `/` 推导；直接让 wasm unsigned division 接收0
会trap，偏离已证明的AArch64 reference结果。

## 11. 本地逐项对照

| 参考数据流 | 本地实现 |
|---|---|
| signed int32 saturation + toward-zero | `PlayerUpdateLayersInternal.h` 的 `signedInt32FromDoubleTowardZeroSaturated_guess`；render路径对应 `floatToSignedIntTowardZeroSaturated_guess` |
| unsigned int32 saturation + toward-zero | `PlayerInternal.h` / timeline implementation 的 `doubleToUnsignedIntTowardZeroSaturated_guess`，mesh wrapper复用相同边界 |
| opacity half-away then unsigned | `timelineOpacityWordFromDouble_guess` |
| opacity uint32 multiply-wrap then /255 | `multiplyOpacityWordsDivide255_guess` |
| own/inherited mesh signed-vs-unsigned cap | `scaledOwnMeshDivision_guess` / `scaledInheritedMeshDivision_guess` |
| word-to-signed without numeric conversion | `meshDivisionCounterWordToInt_guess` 与 cell-count局部 `memcpy` |
| prepared signed-int32 division | `prepareBezierPatchDivision_guess` |
| command-list signed-int64 + ordered publication | `serializeBezierPatchDivision_guess` |
| Canvas/SLA u32 cell arithmetic | `renderBezierPatchCellDivisions_guess` |
| A64 zero-divisor profile | `unsignedDivideA64Profile_guess` 与 render helper的显式 denominator gate |
| packed weight s32 conversion | `packedColorInterpolationWeightS32_guess` |
| packed color/channel low-word operations | EmotePlayer setColor、SourceCache tint和D3D renderer的mask/shift/byte stores |

逐项对照没有发现reference存在而本地被普通cast/clamp/round替代的路径，也没有发现本地额外的
sanitize。故本轮无需production修改。

## 12. 现有验证映射

现有 `tests/unit-tests/plugins/motionplayer-dll.cpp` 已覆盖：

- render float→signed 的 ±0、正负小数、边界相邻值、±Inf、NaN；
- serialized/prepared Bezier division 的向零、signed 饱和、NaN unordered publication；
- render Bezier cell division 的负维度、`UINT_MAX`、denominator wrap-to-zero、numerator wrap和
  negative division word；
- timeline unsigned conversion 的负值、±0、分数、`2^32`、Inf、NaN；
- opacity half-away 的 ±0.5 邻域、巨大值和Inf/NaN；
- own/inherited mesh cap 的 `0x80000000` 分歧、word-to-signed、split `+1`回绕和零除数；
- particle emit/source、discretization、blink wait和opacity composition的signed saturation/wrap。

这些是本地逻辑的确定性回归测试；正式native unit、Web Debug以及cross-reference runtime differential
仍统一归入 `MP-V01..V08`，不作为 `MP-B04` 静态闭环的阻塞项。

## 13. 最终判定

`MP-B04` 没有剩余 task-local 静态差异。四端共同控制流、逐平台差异、本地实现和现有测试已完成
一一映射；64条IDA任务注释、4个独立书签已写入并保存到四个配套IDB。

剩余工作仅为最终验证阶段的构建、unit/runtime/differential执行与证据汇总。
