# SeparateLayerAdaptor shared `absolute` member-hint 边界的四参考复原（2026-08-17）

## 结论

四个参考二进制一致表明，`SeparateLayerAdaptor` 向目标 Layer 发布 `absolute` 时复用同一个
4-byte、进程级可变 member-hint 槽。该槽在四端都直接位于 shared `Layer` class-factory hint
之后，但二者是地址不同、consumer 不同的独立 data object。`absolute` 槽恰有三类语义
consumer：

1. payload-free `resolveLayerOrdinal_guess` 的 sequence-based publication；
2. payload-bearing `resolveLayerNodeInternal_guess` 的 sequence-based publication；
3. `assignFromAdaptor_guess` 中覆盖临时 absolute 的 source-relative rebased publication。

同一 assign 流程从 source Layer 读取 `absolute` 时仍传 `hint=null`；只有向 target Layer 的
两次写入复用 shared slot。因此不能把本结论误推广为所有同名读写都共享 hint，也不能把
source getter 改用 target publication 的缓存。参考符号已 strip，恢复名称继续保留
`absoluteMemberHint_guess` 的 `_guess` 后缀。

## 精确数据布局

| 目标 | shared `Layer` hint | shared `absolute` hint | 相对关系 |
|---|---:|---:|---|
| Android arm64 | `0x1AB553C` | `0x1AB5540` | `absolute = Layer + 4` |
| Android armv7 | `0x11119BC` | `0x11119C0` | `absolute = Layer + 4` |
| iOS arm64 | `0x101B699FC` | `0x101B69A00` | `absolute = Layer + 4` |
| iOS armv7 | `0x187D68C` | `0x187D690` | `absolute = Layer + 4` |

四项 `absolute` 均已建立为独立 size-4 `unsigned int` data item。raw data xref 数分别为
Android arm64 6、Android armv7 9、iOS arm64 3、iOS armv7 7；AArch64 的 ADRP/ADD pair、
ARM literal-pool materialization、函数合并与控制流复制会改变 raw 数量。按 caller 和实际
dispatch 数据流折叠后，四端都严格只有上述三个语义 consumer。

紧邻关系是稳定的链接后 family 证据，不表示 `Layer` 与 `absolute` alias。前一 word 是
`createLayerVariant_guess` 等 Layer class lookup 使用的 class-factory hint；后一 word只传给
target Layer 的 `PropSet("absolute")`。任何一个 consumer 都没有以数组索引或 aggregate
base 同时访问两项。

## consumer 映射

| 语义 consumer | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `SeparateLayerAdaptor_assignFromAdaptor_guess` | independent entry `0x6A97F0`，IDA merged owner `0x6A965C` | `0x57C814` | `0x10010347C` | `0x100874` |
| payload-bearing resolve | `0x6C3F28` | `0x58DCD4` | `0x100117E88` | `0x115B34` |
| payload-free ordinal resolve | `0x6C90C4` | `0x591DEC` | `0x10011C628` | `0x11AE24` |

Android arm64 将 assign 的独立入口并入更早的 decompiler owner，故 Hex-Rays 输出不能稳定
单独显示整个尾部。对该端在 `0x6A9C18` / `0x6A9C2C` 做 fresh disassembly readback，仍直接
显示 `absoluteMemberHint_guess@PAGE` / `@PAGEOFF`；其余两 consumer 及其它三端均在 fresh
decompile 中直接显示新符号。这里保留 merged-function 差异，没有为了得到整洁伪代码而
虚构函数边界。

## 宽字符串搜索与 literal 归属

本轮按 `ida-search-string` 的三编码流程复核了 `absolute`。IDA 普通 string-list 搜索在两个
Android 库为空，在两个 iOS 库各返回 27 个 ASCII/string-list 结果；所以普通搜索是否命中
不能作为 SLA literal 缺失与否的证据。随后对 ASCII/UTF-16LE/UTF-32LE raw bytes 分页搜索
至 cursor 结束：UTF-32LE 无命中，UTF-16LE 命中如下。

| 目标 | UTF-16LE raw matches | SLA 实际使用的 literal |
|---|---|---:|
| Android arm64 | `0x14D5A90`, `0x1506E74` | `0x14D5A90` |
| Android armv7 | `0xD855B0`, `0xDB00FA` | `0xD855B0` |
| iOS arm64 | `0x101957A9C`, `0x101957AAE`, `0x10195BF3E` | `0x10195BF3E` |
| iOS armv7 | `0x1749E00`, `0x1749E12`, `0x174E2A2` | `0x174E2A2` |

每一处均以 `get_bytes` 检查前后 code-unit 边界，再用 xref/caller 归属筛选。其它同文本命中
属于 `absoluteOrder` 等无关命名或其它 registrar；不能只凭字符串相同将它们并入 SLA
member-hint family。SLA 实际 literal 也被 NCB registrar 与 PrivateMotionGLL registrar 引用，
但 registrar 不取 `absolute` hint data 的地址，故不会把 semantic hint consumer 数从 3
扩大。

## 三条数据流

### payload-free ordinal resolve

四端可归一为：

```text
reuse retired Layer by ordinal, or create Layer through shared Layer factory
target = strict assignable Layer accessor
ignore target.PropSet(MEMBERENSURE, "absolute",
                      adaptorAbsolute + assignSequence,
                      &sharedAbsoluteHint)
ignore target.PropSet(MEMBERENSURE, "hitThreshold", 256,
                      &sharedHitThresholdHint)  // V181 follow-up
return retained Layer Variant
```

该路径使用当前 `_assignSequence`，但写完后不递增。这个差异在四端一致，不能为了与 payload
overload 对称而增加 increment。

### payload-bearing resolve

```text
reuse retired payload/Layer or create a new Layer
publish absolute = adaptorAbsolute + assignSequence with shared hint
++assignSequence
publish hitThreshold = 256 with shared hitThreshold hint  // V181 follow-up
return retained Layer Variant
```

它与 payload-free overload 复用同一个 absolute word，而不是各有一个函数静态缓存。新增或
复用 Layer、`createdOrChanged` 计算、active/retired tree 搬运顺序均保持原实现。

### assign from adaptor

每个 source active entry 先调用 payload-bearing resolve；因此 target Layer 先收到一次
sequence-based `absolute` 写入并递增 sequence。随后按固定次序执行：

```text
ignore target.assignImages(sourceLayerVariant)
height = source.GetInt(default=0, "height")  // probe 1024/null, then read 0/null
width  = source.GetInt(default=0, "width")   // same two-stage protocol
ignore target.setSize(width, height, sharedSetSizeHint)

absolute = source.GetInt(default=0, "absolute") // each: probe 1024/null,
visible  = source.GetInt(default=0, "visible")  // then flags=0/null read
opacity  = source.GetInt(default=0, "opacity")  // with ignored second HRESULT
type     = source.GetInt(default=0, "type")
left     = source.GetInt(default=0, "left")
top      = source.GetInt(default=0, "top")

ignore target.PropSet(MEMBERENSURE, "absolute",
                      targetBase + absolute - sourceBase,
                      &sharedAbsoluteHint)
publish visible, opacity, type, left, top in that order
```

所以同一 target 在一次 assign entry 内可观察到两次 `absolute` publication：先是临时
sequence-based value，后是 source-relative rebased value；两次传入完全相同的 hint 指针。
它不是一个应该被优化掉的冗余 store，因为 target dispatch 可以观察调用次数、值、顺序和
可写 hint 地址。

上面的 `GetInt` 双阶段读取以及 target `setSize` 的 shared method hint 后由 V183 以四端 helper
和 call ABI 单独闭合；详见
`analysis/motionplayer_separate_layer_assign_double_read_set_size_shared_hint_boundary_four_binary_2026-08-17.md`。

## status、对象生命周期与异常边界

三个 target `absolute` 写入均使用 `TJS_MEMBERENSURE`（512），value 是 non-null Integer
`tTJSVariant *`，receiver 与 `objthis` 都是同一 target Layer dispatch。portable helper 与
四端 reference 一样忽略 `PropSet` HRESULT：失败不阻断后续 `hitThreshold` 或 assign 中的
`visible/opacity/type/left/top` publication，也不使 `assignFromAdaptor_guess` 改为返回错误。

member-hint word 只保存 dispatch cache state，不拥有 Layer、accessor 或 Variant。Layer 的
active/retired tree ownership、创建时的 Variant handoff、strict accessor 获取、retired erase
以及正常尾部 `clear(true)` 均未改变。若 dispatch/C++ conversion 抛异常，现有 native 渐进
提交边界继续保留；本轮没有加入事务回滚、catch 或额外 RAII。payload-free 路径不递增
sequence，payload-bearing 路径在 absolute publication 后、hitThreshold publication 前递增，
因此异常发生点附近的 sequence 可见状态也没有被重排。

## portable 源码与探针

修改覆盖：

1. `RuntimeSupport.cpp`
   - 在 `layerClassMemberHint_guess` 后定义 process-lifetime
     `absoluteMemberHint_guess`；
2. `MotionDispatch.h`
   - 按同一相对 family 顺序发布 extern，并注释三条 Layer-publication path 的共享边界；
3. `SeparateLayerAdaptor.cpp`
   - payload-free resolve、payload-bearing resolve、assign rebase 三处 target write 都传
     `&detail::absoluteMemberHint_guess`；
   - source getter 保持 `nullptr`；`hitThreshold` 的独立 shared hint 后由 V181 闭合；
4. `tests/unit-tests/plugins/motionplayer-dll.cpp`
   - 扩展既有 SeparateLayer dispatch recorder；
   - 锁定 source-created Layer 的 absolute/hitThreshold hint 分界；
   - 锁定一次 assign 的两个 target absolute writes 都使用同一 shared hint、flags 为 512、
     `objthis` 为 target self；
   - 额外驱动 payload-free ordinal path，证明第三类 `absolute` consumer 复用同一地址；
     同一 probe 后由 V181 扩展为锁定独立 shared `hitThreshold` hint。

compiled source comments 只记录语义和相对 family，不嵌入参考库绝对地址。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery` 均已完成：

- 四个 `absolute` 槽建立为 size-4 `unsigned int` data item 并命名
  `absoluteMemberHint_guess`；
- 每库对 data、实际 UTF-16 literal、assign、payload resolve、ordinal resolve 各写一条
  comment，共 20 条；
- 每库在 data item 建一个 bookmark，共 4 个；
- 每库 force-recompile 三个语义 consumer，共 12 次；
- Android armv7、iOS arm64、iOS armv7 三函数 readback 及 Android arm64 两个 resolve
  readback 直接显示新符号；Android arm64 merged assign 用上述 operand disassembly readback
  闭合；
- 四份 recovery IDB 均原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- 扩展后的 test TU 在两个 macro profile 下均编译通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,648,700 bytes，539 imports / 69 exports；
- Headless wasm：84,995,841 bytes，538 imports / 69 exports；
- 相较 V179 两份 wasm 均增加 72 bytes，import/export ABI 表面不变；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- 两个 build tree 的 CTest 均报告 `No tests were found`，因此没有虚报 runtime unit-test；
  recorder 由 ordinary/headless syntax-only 覆盖；
- `git diff --check` 通过，只报告 working tree 既有 LF/CRLF 转换提示。

首次最终构建触发 CMake 自动 regeneration 时，两个旧 cache 中的 Emscripten toolchain 路径
被环境解析成不存在的 `/upstream/...`，导致 compiler `NOTFOUND`。显式恢复
`EMSDK=C:\Users\fengxuexin\Developer\emsdk` 后重新运行 Web/Wasmtime presets，configure、
generate 和上述双构建全部成功；这是本机构建缓存/环境问题，不是 motionplayer 源码失败。
构建只出现项目既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI 与 JS-library 警告。

## 结论边界

本轮只闭合 SeparateLayerAdaptor target `absolute` publication 的共享地址 identity、三个
consumer、调用顺序与 sequence 边界。`hitThreshold` 使用另一个、与 absolute 不相邻且
四端间距不同的 hint word，已由 2026-08-17 V181 单独闭合；详见
`analysis/motionplayer_separate_layer_hit_threshold_shared_hint_boundary_four_binary_2026-08-17.md`。
`type/left/top` 不能因为出现在同一函数中就被并入本 slot；它们的 source-null/target-shared
分流已由 2026-08-17 V182 单独闭合，分别接入既有 type 与 geometry family，详见
`analysis/motionplayer_separate_layer_assign_type_left_top_shared_hint_boundary_four_binary_2026-08-17.md`。
strip binary 不能绝对证明原 `.cpp` 文件名或 namespace 拼写，故 portable placement 是以四端
稳定链接布局和消费边界作出的 source-structure 推断。
