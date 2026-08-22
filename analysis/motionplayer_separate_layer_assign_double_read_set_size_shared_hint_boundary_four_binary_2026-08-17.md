# SeparateLayerAdaptor assign 双阶段读取与 shared `setSize` hint 的四参考复原（2026-08-17）

## 结论

四个参考二进制共同暴露并闭合了 `SeparateLayerAdaptor_assignFromAdaptor_guess` 的两个旧移植
偏差：

1. 每个 source integer 并不是一次 `PropGet(MEMBERMUSTEXIST)` 后直接转换；原版使用
   `ncbPropAccessor::GetValue(default=0)` 的两阶段协议：
   - 先以 `TJS_MEMBERMUSTEXIST`（1024）、hint null 做 presence probe，丢弃 probe Variant；
   - 只有 probe status 非负才再以 flags 0、hint null 读取一次；
   - 第二次 HRESULT 被忽略，转换并销毁第二个 Variant；
   - probe status 为负则返回 caller default 0，不执行第二次读取。
2. target `setSize(width,height)` 不是 null hint；它复用一个插件级 process-wide
   `setSizeMemberHint_guess`。同一 word 在四端都恰有 10 个直接 method call，折叠为 7 个语义
   consumer function。

因此 source 读取仍不会使用 `widthMemberHint_guess`、`heightMemberHint_guess` 或 target method
cache；共享的是 target `setSize` 方法名 cache。第一阶段和第二阶段都故意传 null hint。原符号
已 strip，恢复名继续使用 `_guess`。

## 精确数据与 assign 映射

| 目标 | assign function / entry | `setSizeMemberHint_guess` | SLA `setSize` call |
|---|---:|---:|---:|
| Android arm64 | independent entry `0x6A97F0`，IDA merged owner `0x6A965C` | `0x1AB52C4` | `0x6A9B5C` |
| Android armv7 | `0x57C814` | `0x11117D0` | `0x57C96E` |
| iOS arm64 | `0x10010347C` | `0x101B6978C` | `0x100103674` |
| iOS armv7 | `0x100874` | `0x187D494` | `0x100A36` |

该 method word 到 geometry `widthMemberHint_guess` 的距离在两个 64 位端为 `+0xB8`，两个
32 位端为 `+0x90`。这明确否定“setSize 应复用 width/height property word”以及从单端相邻
布局推断一个连续 C++ struct 的做法；它们是 consumer 与 ABI 均不同的独立 data object。

## 10 个直接调用与 7 个 consumer function

四端 data xref 折叠后得到完全一致的函数和 call-site multiplicity：

| consumer | calls | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|---:|
| `SourceCache_bakeSource_guess` | 1 | `0x6A3FC0` | `0x57A168` | `0x1000FFB24` | `0xFCD68` |
| `SeparateLayerAdaptor_assignFromAdaptor_guess` | 1 | merged owner `0x6A965C` | `0x57C814` | `0x10010347C` | `0x100874` |
| `Player_buildRenderCommands_guess` | 2 | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| `Player_renderToCanvas_guess` | 1 | `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |
| `Player_renderAccurateSeparateLayerAdaptor_guess` | 2 | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |
| `Player_materializeInternalRenderLayers_guess` | 2 | `0x6CB57C` | `0x592F7C` | `0x10011E2BC` | `0x11CAC8` |
| `Player_draw_guess` | 1 | `0x6D3398` | `0x597864` | `0x100123C84` | `0x122F28` |

raw data xref 数分别为 Android arm64 20、Android armv7 30、iOS arm64 10、iOS armv7 20。
iOS arm64 每个 direct call 只形成一个 data ref；A64 与 iOS armv7 通常以两条地址
materialization 指令形成一个 call；Android armv7 还包含 function chunk/literal-pool 范围内的
额外 ref。四端最终都严格归一为上述 10 个 call，不能把 raw ref 条数当 caller 数。

其它六个 function 的 portable call site 已经使用同一 `setSizeMemberHint_guess`；本轮源码修复
的是遗漏的 SLA assign consumer。部分巨大 renderer 的 Hex-Rays 输出因 function chunk 或
truncation 没有打印全部 symbol，但精确 data xref、call operand 与其它端 readback 一致，不能
用伪代码文本缺行否定真实引用。

## `setSize` 字符串搜索与归属

本轮按三编码流程搜索到 cursor 结束。普通 IDA string search 对 Android 两端各返回 10 个
ASCII 结果，对 iOS 两端各返回 2 个 ASCII 结果；它们不能定位 TJS wide literal。raw byte
结果为：

| 目标 | ASCII matches | UTF-16LE matches | UTF-32LE | SLA 实际 literal |
|---|---:|---|---:|---:|
| Android arm64 | 10 | `0x14D57F8`, `0x1506C58` | 0 | `0x14D57F8` |
| Android armv7 | 10 | `0x616930`, `0x63A8E8`, `0x6550D8`, `0x66F440`, `0x675E8C`, `0xD85350`, `0xDAFEB2` | 0 | `0xD85350` |
| iOS arm64 | 2 | `0x10195771C`, `0x10195772C`, `0x101959F54`, `0x10195BC10`, `0x101963C26`, `0x101975580`, `0x10197AE40` | 0 | `0x10195BC10` |
| iOS armv7 | 2 | `0x1749A80`, `0x1749A90`, `0x174C2B8`, `0x174DF74`, `0x1755FD2`, `0x176792C`, `0x176D1F2` | 0 | `0x174DF74` |

四处实际 literal 均以 `get_bytes` 验证前置 code unit、完整 `setSize` UTF-16 序列、终止零及
下一字符串边界。literal xref 还包含 D3DAdaptor/PrivateMotionGLL registrar 和少量其它模块；
registrar 只消费字符串，不取 `setSizeMemberHint_guess` 地址，不能扩大上节的 10-call data
consumer 集合。反过来，只有同时命中实际 literal、exact method word 和 call ABI 才算本轮
语义证据。

## source `GetValue(default=0)` 双阶段协议

四端共享的 getter helper 映射如下：

| 目标 | `ncbPropAccessor_getIntValue_guess` |
|---|---:|
| Android arm64 | `0x533228` |
| Android armv7 | `0x496B84` |
| iOS arm64 | `0x1000F9468` |
| iOS armv7 | `0xF651C` |

四份 fresh decompile 归一为：

```text
GetInt(source, name, default=0):
    probe = Void Variant
    probeStatus = source.PropGet(
        MEMBERMUSTEXIST, name, hint=null, result=&probe, objthis=source)
    destroy probe
    if probeStatus < 0:
        return 0

    value = Void Variant
    ignore source.PropGet(
        flags=0, name, hint=null, result=&value, objthis=source)
    result = value.AsInteger()
    destroy value
    return result
```

presence 判断是 `status >= 0`，不要求 exact `TJS_S_OK`；所以 `TJS_S_TRUE` 等非负结果也进入
第二阶段。第一阶段写入的 Variant 值无条件丢弃。第二阶段即使返回负 HRESULT，只要 dispatch
写了 Variant，仍转换该值；若它没有写入，初始 Void 走 integer conversion 的零/default 分支。
这与“第二次失败就保留第一阶段值”或“第二次失败立即跳过 assign”均不同。

在所有 probe 成功时，source dispatch 可观察的完整调用序列是：

```text
height  (MEMBERMUSTEXIST, null) -> height  (0, null)
width   (MEMBERMUSTEXIST, null) -> width   (0, null)
absolute(MEMBERMUSTEXIST, null) -> absolute(0, null)
visible (MEMBERMUSTEXIST, null) -> visible (0, null)
opacity (MEMBERMUSTEXIST, null) -> opacity (0, null)
type    (MEMBERMUSTEXIST, null) -> type    (0, null)
left    (MEMBERMUSTEXIST, null) -> left    (0, null)
top     (MEMBERMUSTEXIST, null) -> top     (0, null)
```

某一 member 的 probe 失败只省略该 member 的第二次读取并返回 0；外层 assign 仍继续下一个
member 和后续 target publication。

## Android arm64 merged-tail 直接指令证据

A64 的 assign 尾部仍被并入 `0x6A965C`，所以本轮继续保留真实 merged boundary。height：

- `0x6A9A40` 把 probe flags 置为 `0x400`，`0x6A9A4C` 把 hint 置 XZR，
  `0x6A9A54` 执行 probe；
- probe Variant 随后析构，`0x6A9A64` 对负 status 分支；
- success path 在 `0x6A9A78` / `0x6A9A7C` 置 flags 0 / hint null，
  `0x6A9A80` 进入 integer read/conversion helper。

width 对应 `0x6A9AA4` / `0x6A9AB0` / `0x6A9AB8` 与
`0x6A9ADC` / `0x6A9AE0` / `0x6A9AE4`。随后：

- `0x6A9B18` 保存 sign-extended width，`0x6A9B20` 保存 sign-extended height；
- `0x6A9B24` 形成 `{&widthVariant,&heightVariant}`；
- `0x6A9B38` / `0x6A9B50` 形成精确 shared method word；
- `0x6A9B48` 为 flags 0，`0x6A9B54` 为 result null，`0x6A9B3C` 为 argc 2，
  `0x6A9B58` 令 objthis 等于 receiver；
- `0x6A9B5C` 调用后没有 status branch，先析构 height Variant，再析构 width Variant。

其它三端 fresh decompile 直接显示 `GetInt(height,0) -> GetInt(width,0)`、argv 的 width-first
顺序以及 `&setSizeMemberHint_guess`；它们与 A64 指令证据完全一致。

## target `setSize` ABI、status 与生命周期

SLA call 的四端共同 ABI 为：

```text
target.FuncCall(
    flags=0,
    member="setSize",
    hint=&setSizeMemberHint_guess,
    result=null,
    argc=2,
    argv={ Integer(width), Integer(height) },
    objthis=target)
```

HRESULT 被忽略，失败不阻止之后的 `absolute/visible/opacity/type/left/top` 双阶段 source reads
与 target publications。width/height 两个 argument Variant 在调用期间存活，调用后按
height-then-width 顺序析构；共享 hint word 不拥有 target、argument Variant 或 accessor。

probe Variant 在每次第一阶段调用后、检查 status 前已经销毁；第二阶段 Variant 在 conversion
后销毁。source/target accessor 的既有 target-first/source-second release、entry Variant owners、
retired tree normal-tail clear 与异常渐进提交边界均未改变。本轮没有添加 catch、回滚或事务。

## portable 源码与探针

修改覆盖：

1. `SeparateLayerAdaptor.cpp`
   - `getIntegerProperty_guess` 恢复 disposable `MEMBERMUSTEXIST` probe；
   - probe 成功后执行第二次 flags 0/null-hint read，并显式忽略它的 HRESULT；
   - `callSetSize_guess` 接入 `&detail::setSizeMemberHint_guess`；
   - height-before-width、width-first argv、null result 与 ignored status 保持不变；
2. `MotionDispatch.h`
   - 更正既有 setSize word 的 consumer 注释，明确它不是 per-caller static；
3. `tests/unit-tests/plugins/motionplayer-dll.cpp`
   - source probe 返回与第二次 read 不同的 sentinel，证明第一份值被丢弃；
   - 第二次 `opacity` read 在写入真实值后返回失败，证明 HRESULT 被忽略且值仍被转换；
   - 锁定 16 次 source call 的 member 顺序、交替 `1024/0` flags、全 null hints；
   - 再执行一次 width probe failure，锁定它不发生第二次 read、width 返回 default 0，同时
     setSize 与后续 publications 继续；
   - 锁定 target setSize flags 0、exact shared pointer、null result、`{320,240}` 与 target self；
   - 保留 target 所有调用失败后仍继续到 `top`、assign 返回成功的覆盖。

compiled source comments 不包含参考库绝对地址。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery` 均已完成：

- 4 个 method word 建立为独立 size-4 `unsigned int` data item 并命名
  `setSizeMemberHint_guess`；
- 每库对 data、实际 UTF-16 literal、assign、SLA call、getInt helper 各写一条注释，共 20 条；
- 每库一个 V183 data bookmark，共 4 个；
- 每库 force-recompile 七个 direct-consumer function 和 getter helper，共 32 次；
- 三端 assign fresh decompile 直接显示 exact symbol；A64 merged-tail fresh disassembly 显示
  PAGE/PAGEOFF symbol 和全部 ABI register；
- 四端 helper fresh decompile 均显示 probe/default/second-read 边界；
- 七类 consumer 的 data xref readback 仍归一为 10 个 calls；
- 四份 recovery IDB 均原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,648,879 bytes，539 imports / 69 exports；
- Headless wasm：84,996,020 bytes，538 imports / 69 exports；
- 相较 V182 两份 wasm 均增加 101 bytes，import/export ABI 表面不变；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- Web GLOBAL section 仍为 `0xD5B2`，CODE 由 `0x1A40B14` 增至 `0x1A40B79`；Headless
  GLOBAL 仍为 `0xD5DA`，CODE 由 `0x19E8AC2` 增至 `0x19E8B27`；CODE 差值也精确为
  101 bytes；
- 两个 build tree 的 CTest 均报告 `No tests were found`，没有虚报 runtime unit-test；
  recorder 由 ordinary/headless syntax-only 覆盖；
- `git diff --check` 通过，只出现 working tree 既有 LF/CRLF 转换提示；
- 四端 string/bytes、data/literal xref、make-data、comments/bookmarks、force-recompile/readback
  与 save 全部成功。

构建只出现项目既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI 与 JS-library 警告，
没有新增 motionplayer 编译或链接错误。

## 结论边界

本轮闭合的是 SLA assign 的八个 integer source reads 所共享的 `GetValue(default=0)` 双阶段
协议，以及 target `setSize` 的全局 method-hint identity、10-call consumer topology、ABI、
status 和 Variant lifetime。它不把 `setSize` method word 并入 geometry property family，也不把
同名 D3DAdaptor registrar descriptor 计为 data consumer。其它 9 个 direct setSize call 的完整
外围渲染语义仍分别由其已有纵切面负责；本轮只证明它们共享同一个 method cache。
