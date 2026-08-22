# Bezier bounds 复用全局 geometry member-hint family（四参考，2026-08-16）

## 结论

`BezierPatch::calcPatchBounds`、`BezierPatch::calcMeshBounds` 与
`BezierPatch::reverseCalcBezierPatch` 没有自己的 `left/top/right/bottom/width/height`
TU-local member-hint backing words。四个当前参考二进制共同证明：

- 两个 bounds producer 发布 Dictionary 时，直接使用插件级全局 geometry family；
- reverse lookup 读取 `left/top/right/bottom` 时，复用同一批 backing words；
- 这组 word 还与 `MotionNode::findSource`、`ObjSource::getClip`、
  `SeparateLayerAdaptor`、`Player::getBounds`、`Player::calcViewParam` 和
  `Player::getCommandList` 等路径共享。

旧源码在 `MotionLayerExtensions.cpp` 中定义的六个 namespace-local static 因而是错误的
源结构推断。它们不仅多占六个 word，还会切断 TJS dispatch 可观察的 hint-cache 状态：原版
同名 geometry 访问跨函数、跨翻译单元共享地址，本地旧实现却把 Bezier 路径隔离成另一组
缓存。

## 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `BezierPatch_calcPatchBounds_guess` | `0x6A264C` | `0x579258` | `0x1000FE804` | `0xFB868` |
| `BezierPatch_calcMeshBounds_guess` | `0x6A2A04` | `0x5794F8` | `0x1000FEAB8` | `0xFBBDC` |
| `BezierPatch_reverseCalcBezierPatch_guess` | `0x6A3874` | `0x579D48` | `0x1000FF508` | `0xFC7A4` |

两个 producer 都构造新 Dictionary，按 native 顺序发布边界；`calcMeshBounds` 在所有四端
都把 `left` 以同一个 hint word 重复写入一次。reverse 先调用 patch-bounds producer，再从
返回 Dictionary 取回四个 extremum，完成 positive inside-gate 后才继续 10×10 tessellation
与反向搜索。本纵切面只修正这些 dispatch 调用的 backing-word identity，不改变已闭合的
数值次序、重复 `left` 写入或 NaN gate。

## 六个全局 backing word

| member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `widthMemberHint_guess` | `0x1AB520C` | `0x1111740` | `0x101B696D4` | `0x187D404` |
| `heightMemberHint_guess` | `0x1AB5210` | `0x1111744` | `0x101B696D8` | `0x187D408` |
| `leftMemberHint_guess` | `0x1AB5224` | `0x1111758` | `0x101B696EC` | `0x187D41C` |
| `topMemberHint_guess` | `0x1AB5228` | `0x111175C` | `0x101B696F0` | `0x187D420` |
| `rightMemberHint_guess` | `0x1AB522C` | `0x1111760` | `0x101B696F4` | `0x187D424` |
| `bottomMemberHint_guess` | `0x1AB5230` | `0x1111764` | `0x101B696F8` | `0x187D428` |

`width/height` 是相邻 pair；`left/top/right/bottom` 是随后相邻的四槽。四端的相对布局与
consumer identity 一致。它们不是仅因 literal 相同而做的猜测，而是每个 dispatch call 的
真实 address argument 均回到该表中的 data item。

## Android arm64 的直接十进制实参

Android armv7 与两个 iOS 参考的 fresh decompile/data refs 会直接显示上表符号。Android
arm64 的优化/address materialization 使两个 bounds producer 的 Hex-Rays 输出把地址显示为
十进制常量，但数值逐一精确相等：

| decompile operand | 十六进制 | member |
|---:|---:|---|
| `28004876` | `0x1AB520C` | `width` |
| `28004880` | `0x1AB5210` | `height` |
| `28004900` | `0x1AB5224` | `left` |
| `28004904` | `0x1AB5228` | `top` |
| `28004908` | `0x1AB522C` | `right` |
| `28004912` | `0x1AB5230` | `bottom` |

两个 A64 producer 都含有完整六值集合；reverse 的四个 read 则恢复成命名后的
`left/top/right/bottomMemberHint_guess`。因此 A64 没有“无 xref 即使用私有槽”的反例，
只是 decompiler 的表达形式与另外三端不同。

## Fresh readback 与调用形状

四库 force-recompile 后得到一致的命中轮廓：

- `calcPatchBounds`：A32/iOS64/iOS32 中六个 global symbol 各一次；A64 中出现上述六个精确
  十进制地址；
- `calcMeshBounds`：`left` 两次，其余五个各一次；A64 同样出现完整六地址集合，第二次
  `left` 复用相同地址；
- `reverseCalcBezierPatch`：四端均各一次读取 `left/top/right/bottom`，没有 reverse-local
  duplicate。

代表性 publication/read operand 地址如下，仅用于 recovery IDB 与复核：

| 参考 | patch publication | mesh publication | reverse read |
|---|---:|---:|---:|
| Android arm64 | `0x6A2824` | `0x6A2B44` | `0x6A3944` |
| Android armv7 | `0x579344` | `0x5795D2` | `0x579DA2` |
| iOS arm64 | `0x1000FE904` | `0x1000FEBA4` | `0x1000FF59C` |
| iOS armv7 | `0xFB9B4` | `0xFBD00` | `0xFC852` |

publication 继续使用 `TJS_MEMBERENSURE`，lookup 继续使用 flags 0；Dictionary 自身仍按
`tTJSVariant` owning handoff 返回。共享的是传给 PropSet/PropGet 的可变 32-bit hint word，
不是 bounds 数值的持久存储。

## 源码修正

`MotionLayerExtensions.cpp` 已完成：

1. 删除 TU-local 六槽 `left/top/right/bottom/width/heightMemberHint_guess`；
2. 两个 producer 的全部 publication 改用 `motion::detail` 全局 geometry family；
3. reverse 的四个 lookup 改用同一全局 family；
4. 显式包含声明该 family 的 `MotionDispatch.h`，避免依赖测试大翻译单元中的偶然传递包含。

`MotionDispatch.h`/`RuntimeSupport.cpp` 中既有的全局声明与唯一实体继续作为 backing storage，
没有再引入新变量。

## 回归测试

新增 `Bezier bounds reuse the process-wide geometry hint family`：

- 保存六个全局 word 并临时清零，用 RAII 在成功或异常退出时恢复，避免污染其他测试；
- 通过公共 `BezierPatch::calcPatchBounds` 输入两点 `(-2, 3)` 与 `(4, -5)`；
- 要求六个全局 word 全部被 Dictionary PropSet 更新为非零值；
- 同时核对返回的 `left=-2`、`top=-5`、`right=4`、`bottom=3`、`width=6`、
  `height=8`，防止 identity 断言掩盖数据流回归。

测试 TU 增加 `MotionLayerExtensions.h` 以声明公共 API。现有两个构建树没有注册 CTest，故本轮
能闭合 ordinary/headless syntax-only 编译，却不能声称该测试已由 CTest 运行；这是测试驱动
现状，不是静默跳过。

## IDB 写回

四个 recovery IDB 均完成：

- 六个全局地址重建/确认成独立 size-4 `unsigned int` data item，并使用源码一致的语义名；
- data item、三个函数入口及代表性 call operand 写入 V169 注释；
- 每库为三个函数增加 V169 bookmark；
- 共 12 个函数 body force-recompile，并完成上述 symbol/decimal hit readback；
- 四库均原位保存成功。

## 验证

- ordinary Emscripten 测试 TU syntax-only：成功，仅有项目既有 `_tss` warning；
- `KRKR2_WASMTIME_HEADLESS=1` 测试 TU syntax-only：成功，同一 warning；
- `cmake --build out/web/debug`：成功，最终链接完成；
- `cmake --build out/wasmtime/debug`：成功，最终链接完成；
- Web wasm：`85,647,322` bytes，539 imports / 69 exports；
- Headless wasm：`84,994,463` bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- 两份 wasm 均由 `llvm-objdump -h` 列出完整
  TYPE/IMPORT/FUNCTION/TABLE/TAG/GLOBAL/EXPORT/START/ELEM/DATACOUNT/CODE/DATA/name/
  target_features sections；
- 相对 V167/V168 可执行基线，两份 wasm 都精确减少 143 bytes，import/export 数不变；
- Web/Headless 两配置 CTest 均未注册测试。

本纵切面证明的是这六个 geometry key 在 Bezier bounds 路径中的精确地址身份；不会把共享
规则机械外推到 literal 相同但四端地址不同的其他 member-hint family。
