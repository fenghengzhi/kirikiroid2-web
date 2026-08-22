# PositionControlCurve `t/s/p` member-hint 全局拓扑的四参考复原（2026-08-17）

## 结论

四个参考二进制一致为 `PositionControlCurve_evaluate_guess` 的三个非 `x/y` 命名读取保留
三个互不相同的 4-byte、进程级可变 member-hint 槽。它们不是 evaluator 所在 portable
translation unit 的一组私有缓存：

- root curve 的 `t`、`s` 两槽在四端都直接紧随共享 `x/y` 槽；
- selected segment 的 `p` 槽位于另一段全局 family，在四端都直接位于共享 `Layer`
  class hint 之前；
- `t`、`s`、`p` 彼此不 alias，也不与共享 `x`、`y` 或 `Layer` alias；
- evaluator 的命名读取固定为 root `x/y/t/s`，再在选中 segment 上读取 `x/y/p`；所有
  七次调用的 flags 为 0，`objthis` 是当前 root/segment accessor 自身。

旧 portable 源虽然给三者各留了独立 word，数值行为通常正确，但把 `t/s/p` 一起定义成
`PlayerFrameProgress.cpp` 的 `motion::internal` globals，丢失了参考库稳定呈现的 shared-global
family 拓扑。本轮把 `t/s` 迁到 `motion::detail` 的共享 `x/y` 后，把 `p` 迁到共享
`Layer` hint 前，并用 dispatch recorder 锁定调用点实际传入的地址。原始符号已 strip，
推测名称继续保留 `_guess`。

## 四端函数与 caller 映射

| 目标 | `PositionControlCurve_evaluate_guess` | 大小 | 唯一直接 caller `PositionInterpolation_evaluate_guess` | call site |
|---|---:|---:|---:|---:|
| Android arm64 | `0x695834` | `0xC4C` | `0x6978B4` | `0x697968` |
| Android armv7 | `0x571FF0` | `0x5A0` | `0x573AF8` | `0x573B7A` |
| iOS arm64 | `0x1000F59E0` | `0x6A8` | `0x1000F7644` | `0x1000F76EC` |
| iOS armv7 | `0xF2484` | `0x68A` | `0xF4380` | `0xF4452` |

四端 fresh decompile 均确认 control evaluator 只有这一条直接上层语义 caller。32 位与
64 位输出在 virtual dispatch 展开、literal-pool materialization 和寄存器/栈表达式上不同，
但 named-read 顺序、hint 地址 identity、Variant owner 链与后续 spline 数据流完全一致。

## 精确数据布局

| 目标 | shared `x` | shared `y` | control `t` | control `s` | control `p` | shared `Layer` |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x1AB5234` | `0x1AB5238` | `0x1AB523C` | `0x1AB5240` | `0x1AB5538` | `0x1AB553C` |
| Android armv7 | `0x1111768` | `0x111176C` | `0x1111770` | `0x1111774` | `0x11119B8` | `0x11119BC` |
| iOS arm64 | `0x101B696FC` | `0x101B69700` | `0x101B69704` | `0x101B69708` | `0x101B699F8` | `0x101B699FC` |
| iOS armv7 | `0x187D42C` | `0x187D430` | `0x187D434` | `0x187D438` | `0x187D688` | `0x187D68C` |

两组 adjacency 在四端都严格相同：

```text
shared x | shared y | PositionControl t | PositionControl s | ...
...
PositionControl p | shared Layer
```

每项都是独立的 size-4 `unsigned int` data object。`t/s/p` 的语义 data consumer 都只有
control evaluator；AArch64 的 ADRP/ADD、32 位 literal pool、异常清理和地址
materialization 会把一个源码级取址扩成多条 raw xref，不能据 raw xref 数量虚构多个
consumer。相反，紧随 `p` 的 `Layer` hint 有独立 factory consumer：Android arm64 因七处
inlined factory 显示 14 条 raw xref，Android armv7/iOS armv7 各有 standalone helper 的
多条 materialization，iOS arm64 为一条直接引用。两项相邻但从不共用地址。

`s` 后面的下一 word 是 V178 已闭合的 MotionLayer `StretchType` 函数局部静态 ID；这只
说明最终链接布局连续，不能把它误当作 PositionControl 的第四个 hint。`p` 与 `t/s` 之间
存在大量其他全局对象，也不能因为它们出现在同一 evaluator 内就把三槽重新聚成一个
TU-local array/struct。

## 共同读取与数据流

四端伪代码可归一为：

```text
root = retained accessor(curve)
mainX    = root.PropGet("x", flags=0, hint=&sharedX, objthis=root)
mainY    = root.PropGet("y", flags=0, hint=&sharedY, objthis=root)
knots    = root.PropGet("t", flags=0, hint=&controlT, objthis=root)
segments = root.PropGet("s", flags=0, hint=&controlS, objthis=root)

select segmentIndex and mainIndex from knots
segment = segments[segmentIndex]
splineX = segment.PropGet("x", flags=0, hint=&sharedX, objthis=segment)
splineY = segment.PropGet("y", flags=0, hint=&sharedY, objthis=segment)
splineP = segment.PropGet("p", flags=0, hint=&controlP, objthis=segment)

evaluate local parameter spline
release p/y/x/segment accessors in reverse order
evaluate main x/y cubic
```

本轮没有发现需要再次修改 position-control 数学算法、numeric lookup 次序、异常传播或
临时 Variant 析构点的差异；这些已由 2026-08-12 的 position/control/easing 纵切面及
2026-08-16 的 nested-NCB follow-up 闭合。本轮的新证据只改变三个 hint 的 source/global
ownership 与可见 family 布局。

## identity 与边界行为

member hint 是传给 TJS dispatch 的可写 `tjs_uint32 *`，不是只读的字符串 ID 常量。因此
这里的地址 identity 本身是可观察状态：

- 第一次或后续 dispatch 可以更新各自 word；同槽 consumer 会观察更新后的值；
- `t`、`s`、`p` 的相同初值 0 不意味着 alias；每个调用得到不同地址；
- root 与 nested segment 的 `x/y` 必须复用 plugin-wide shared x/y 地址；
- `p` 与 `Layer` 即使相邻，也必须隔离；否则 segment lookup 会污染随后 Layer factory 的
  member cache，反之亦然；
- named `PropGet` 的 HRESULT 仍由现有 motion property helper 忽略，Variant conversion 或
  accessor acquisition 的异常自然展开；本轮没有增加 fallback 或 reset；
- 三个 word 都是 process-lifetime mutable storage，不拥有 root、segment 或任何 Variant。

reference data 只证明链接后全局 identity 与稳定相邻关系。它不能从 strip binary 绝对证明
原始 `.cpp` 文件名或 C++ namespace 拼写；将它们归到 portable `motion::detail` shared-global
family 是与四端布局、现有 shared-hint 复原及消费边界最一致的 source-structure 推断，故名称
保留 `_guess`，不把推断写成已恢复原符号。

## portable 源码修复

修改覆盖：

1. `RuntimeSupport.cpp`
   - 在共享 `xMemberHint_guess/yMemberHint_guess` 后定义
     `positionControlTMemberHint_guess` 与 `positionControlSMemberHint_guess`；
   - 在 `layerClassMemberHint_guess` 前定义
     `positionControlPMemberHint_guess`；
2. `MotionDispatch.h`
   - 按同一 family 顺序发布三个 `motion::detail` extern；
   - 明确 `p` 与 `Layer` 相邻但 distinct；
3. `PlayerFrameProgress.cpp` / `PlayerInternal.h`
   - 删除旧 `motion::internal` TU-local 三槽定义与声明；
   - evaluator 的 `t/s/p` 调用改传新的 `motion::detail` 地址；
4. `tests/unit-tests/plugins/motionplayer-dll.cpp`
   - 新增 root/segment dispatch recorder；
   - 锁定 root `x/y/t/s` 与 segment `x/y/p` 的顺序、flags、`objthis` 和精确 hint 指针；
   - 锁定 `x/y/t/s/p/Layer` 六个 word 两两不同。

portable compiled-source 注释只描述语义与相对 family，不包含参考库绝对地址。没有更改
曲线数值公式、数组索引、dispatch status 处理或对象生命周期。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery` 均已完成：

- 共 12 个原 `unk_*` 槽建立为 size-4 `unsigned int` data item；
- 四端统一命名为 `positionControlTMemberHint_guess`、
  `positionControlSMemberHint_guess`、`positionControlPMemberHint_guess`；
- 每库三个 data comment 与一个 evaluator comment，共 16 处；
- 每库为 evaluator 建立一个 hint-topology bookmark，共 4 个；
- 四个 evaluator force-recompile 后 readback 均直接显示 T/S/P 三个新符号；
- 四份 recovery IDB 均原位保存成功。

本轮开始时旧 persistent workers 仍在，但 supervisor 会话注册已丢失且无法重新 adopt。由于
V178 四库已保存、本轮首次写入全部在 session lookup 阶段失败，确认没有新未保存状态后，
逐 PID 核对为目标 `ida_pro_mcp.idalib_server`，重启并重新打开四库，再执行上述写回和保存。
这段工具恢复过程没有改变参考二进制或 portable source 证据。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- 新 root/segment recorder TU 在两个 macro profile 下均编译通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,648,628 bytes，539 imports / 69 exports；
- Headless wasm：84,995,769 bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- 相较 V178，两份 wasm 均增加 12 bytes；import/export ABI 表面不变；
- 两个 build tree 的 CTest 均报告 `No tests were found`，因此没有虚报 runtime unit-test；
  新 recorder 由 ordinary/headless syntax-only 覆盖；
- 四端 IDB make-data、force-recompile、symbol readback 与 save 全部成功。

构建只出现项目既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI 与 JS-library 警告，
没有新增 motionplayer 编译或链接错误。

## 结论边界

本轮闭合的是 PositionControlCurve `t/s/p` 三个 member-hint 的地址 identity、全局 family
拓扑与实际 dispatch wiring。它不把同名 `t`、`s`、`p` 推广成整个插件共享名称缓存：其他
call site 必须重新以四端 data identity 和 caller 数据流证明是否复用。也不从 BSS adjacency
反推无法由 strip binary 证明的原 `.cpp` 文件名；portable namespace/definition 归属仍以
`_guess` 标记其 source-level 推断边界。
