# `x/y` 共享 hint 的四库 IDB 边界闭合（2026-08-17）

## 范围与结论

shared-easing 纵切面已经证明 `xMemberHint_guess` / `yMemberHint_guess` 是插件级共享 pair，
并已让 portable C++ 的五类 consumer 使用同一对全局 backing words。当时 recovery IDB 对
Android arm64 采取了过度保守处理：两个 word 位于旧大 BSS aggregate 内，只加 offset
comment，没有把它们拆成与另外三端一致的独立 data items。

V175 对四端重新取得完整 data xrefs，并对 Android arm64 五类消费者 fresh decompile。
结果显示每个调用点都把两个常量地址分别作为 `x` 与 `y` 的 hint 指针传递，不存在数组
索引、跨度 load/store 或 aggregate owner 语义。因此本轮把四库都硬化为两个独立 size-4
items；尤其修正 Android arm64 的旧 aggregate 边界。

本轮不修改 C++。已有 `motion::detail::xMemberHint_guess` /
`yMemberHint_guess` 的存储范围、调用点与参考 consumer 集一致。

## 数据地址与 fresh xrefs

| 目标 | `xMemberHint_guess` | `yMemberHint_guess` | 每槽 xrefs |
|---|---:|---:|---:|
| Android arm64 | `0x1AB5234` | `0x1AB5238` | 12 |
| Android armv7 | `0x1111768` | `0x111176C` | 18 |
| iOS arm64 | `0x101B696FC` | `0x101B69700` | 6 |
| iOS armv7 | `0x187D42C` | `0x187D430` | 15 |

x 与 y 在每端的 xref 数相等。不同端的数量差异来自 address materialization、literal pools、
function chunks 和优化，不表示 consumer 集不同。按 owning function 归并后，四端都是同五类
consumer。

## 五类 consumer 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Quad_getP_guess` | `0x68F0D4` | `0x56E7F8` | `0x1000F0C5C` | `0xECDA4` |
| `PositionControlCurve_evaluate_guess` | `0x695834` | `0x571FF0` | `0x1000F59E0` | `0xF2484` |
| `VariableTrackEasing_evaluate_guess` | `0x697B34` | `0x573D40` | `0x1000F78C0` | `0xF4648` |
| `LayerGetter_getVtx_guess` | `0x699894` | `0x574C44` | `0x1000F893C` | `0xF5744` |
| `Player_getCameraOffset_guess` | `0x67F2D0` | `0x59441C` | `0x10011F6EC` | `0x11E220` |

Android arm64 最后一项由 IDA 以 wrapper entry `0x67F2D0` 持有远端 function chunk，实际
`x/y` publication 位于 `0x6CDEEC/0x6CDF0C`。这是 function-chunk ownership，不是额外
consumer。

## 每类 dispatch 方向

fresh decompile 与四端既有映射共同得到：

1. `Quad_getP_guess`
   - 建立点 Dictionary；
   - 以 `TJS_MEMBERENSURE` 写 Real `x`、Real `y`；
   - 每个名字一次，分别传共享 pair。
2. `PositionControlCurve_evaluate_guess`
   - 对 root curve 读 Variant `x/y`；
   - 对选中的 nested curve 再读 Variant `x/y`；
   - 因而每个 hint 在语义上使用两次，全部 flags 0。
3. `VariableTrackEasing_evaluate_guess`
   - 对 retained easing root 各读一次 Variant `x/y`；
   - 两个结果再建立独立 array accessors，hint 不参与后续 index/Count 读取。
4. `LayerGetter_getVtx_guess`
   - 把当前 vertex 坐标以 `TJS_MEMBERENSURE` 写入新 Dictionary；
   - x/y 每个一次。
5. `Player_getCameraOffset_guess`
   - 把 Player camera-offset floats 以 `TJS_MEMBERENSURE` 写入新 Dictionary；
   - x/y 每个一次。

所以该 pair 同时跨越 property read/write、Variant/Real 和多个对象 schema。hint word 是
process-lifetime lookup cache，不是 property-value storage；这些方向差异不要求复制 cache。

## Android arm64 独立边界证明

拆分前 fresh pseudocode 明确显示：

```text
Quad:          SetValue("x", ..., &word_234)
               SetValue("y", ..., &word_238)
Position:      PropGet("x", &word_234)  // root + nested
               PropGet("y", &word_238)  // root + nested
Easing:        PropGet("x", &word_234)
               PropGet("y", &word_238)
LayerGetter:   SetValue("x", ..., &word_234)
               SetValue("y", ..., &word_238)
CameraOffset:  SetValue("x", ..., &word_234)
               SetValue("y", ..., &word_238)
```

没有一处使用 aggregate base 加动态 index，也没有 8-byte 成对 load/store。两个地址相差
4 bytes 只说明相邻布局；字符串角色、取址和 mutable cache 更新都以独立 word 为单位。

## recovery IDB 写回

四份 recovery IDB 均完成：

- `xMemberHint_guess` 与 `yMemberHint_guess` 两个独立 size-4 `unsigned int` items，共 8 个；
- 两个 data comments、五个 function comments、两个代表性 easing operand comments，
  每库 9 处，共 36 处；
- x data、y data、shared-easing witness 三类 bookmarks，每库 3 个，共 12 个；
- 五类 functions 每库 force-recompile，共 20 个；
- 20 份 readback 均出现正确 `x/y` 符号，旧八种 `unk_*` 名计数均为零；
- `sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery`
  全部原位保存成功。

## 源码与验证影响

本轮没有改动任何编译源码或测试代码，故不产生新的 wasm。V174 刚完成的最终产物仍是：

- Web：85,647,311 bytes，539 imports / 69 exports；
- Headless：84,994,452 bytes，538 imports / 69 exports；
- 两份均通过 Node parse 与 `llvm-objdump -h` 完整 section audit；
- 两配置 CTest 均无注册测试。

文档/计划的定向 `git diff --check` 无 whitespace error。V175 的可验证产物是四份已保存的
recovery IDB 数据模型与 20 函数 fresh symbol readback，而不是重新生成一个相同 wasm。

## 结论边界

本轮只改变 IDB 表示，不改变早已闭合的 shared-easing 数学、nested owner tree、unordered
NaN endpoint 行为或 C++ dispatch callsites。未来出现新的 `x`/`y` property 名仍不能仅凭
字符串复用本 pair；需要四端 data-address identity。
