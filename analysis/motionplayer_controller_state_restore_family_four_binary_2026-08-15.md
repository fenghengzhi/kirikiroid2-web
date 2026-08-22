# Motionplayer controller-state restore family 四端复原（2026-08-15）

## 范围与结论

本纵切面重新审计当前 `reference/binaries/` 的四个参考目标，不沿用旧
`libkrkr2.so` 注释，闭合以下 controller state restore 及其共享 helper：

- Blink / Eye；
- Eyebrow；
- Mouth；
- Var（也供 Transition、base 与 outer-force 使用）；
- Selector；
- Eye/Eyebrow 的 `std::deque<std::pair<float,float>>` request queue；
- strict Variant property probe、native Array 提取、按名/按索引数值读取；
- state key 的进程级 member-hint 复用。

最重要的边界不是“所有 restore 都先判断 Object”：Blink、Eyebrow、Mouth、
Angle、Selector 均无外层类型 guard，会直接复制/转换入参并构造 accessor；只有 Var
明确在入参原本不是 `tvtObject` 时静默返回。另一个关键边界是 Variant 输出 probe
先写临时值、成功后才 copy-assign 调用者目标；失败绝不污染旧 Variant。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Blink restore | `0x6613A8` | `0x552820` | `0x1001A2BD8` | `0x1A1DB8` |
| Eyebrow restore | `0x662C24` | `0x55343C` | `0x1001A3AB8` | `0x1A2E38` |
| Mouth restore | `0x663588` | `0x553910` | `0x1001A40EC` | `0x1A3504` |
| Angle restore（前一纵切面） | `0x663DF4` | `0x553EE8` | `0x1001A4770` | `0x1A3C70` |
| Var restore | `0x664EBC` | `0x554618` | `0x1001A50C0` | `0x1A45EC` |
| Selector restore | `0x665950` | `0x554C68` | `0x1001A588C` | `0x1A4DD0` |
| strict named Variant probe | `0x661A1C` | `0x552BDC` | `0x1001A3020` | `0x1A2338` |
| force Object + native Array Items-or-null | `0x702000` | `0x5BAA0C` | `0x10029FEE8` | `0x2A4A44` |
| flags-0 named numeric read | `0x661AEC` | `0x552C68` | `0x1001A316C` | `0x1A2490` |
| flags-0 indexed numeric read | `0x665394` | `0x554948` | `0x1001A5494` | `0x1A4A3C` |

四端 IDB 已将后三类 stripped helper 分别恢复为
`tTJSVariant_getNativeArrayItemsOrNull_guess`、
`VariantObject_getFloatByName_guess` 与
`VariantObject_getFloatByIndex_guess`。名称保留 `_guess`，因为发布物没有源符号。

## 字符串编码与共享 hint 表

普通 IDA string search 会漏掉 `lengthDone`、`mouth` 等项，且会把 iOS/A64 的
UTF-16LE 字面量错误显示为单字符。按 `ida-search-string` 流程重新搜索 UTF-8、
UTF-16LE、UTF-32LE 并读取原始 bytes 后，八个 state key 的有效项均为 UTF-16LE；
多字符 key 没有对应 UTF-32LE 命中。短模式 `v`/`p0` 的所谓 UTF-32LE 命中只是
相邻零字节造成的假阳性。

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `v` | `0x1526034` | `0x552A28` | `0x10195FBF2` | `0x1751F56` |
| `length` | `0x151A23A` | `0xDBF60C` | `0x10195FC04` | `0x1751F68` |
| `lengthDone` | `0x14D3834` | `0xD84314` | `0x10195FC12` | `0x1751F76` |
| `rq` | `0x14D3868` | `0xD84348` | `0x10195FC46` | `0x1751FAA` |
| `p0` | `0x14D386E` | `0xD8434E` | `0x10195FC4C` | `0x1751FB0` |
| `p1` | `0x14D3874` | `0xD84354` | `0x10195FC52` | `0x1751FB6` |
| `mouth` | `0x14D387A` | `0xD8435A` | `0x10195FC58` | `0x1751FBC` |
| `value` | `0x1521C8A` | `0xDC4484` | `0x10195FC78` | `0x1751FDC` |

A32 的 `v` 特别容易误判：rodata `0xD8436C` 是完整 `"prev"` 中间的字符，真正的
独立 `"v"` 是 Blink restore 尾部 literal pool 中的 `76 00 00 00`，地址
`0x552A28`。A64 的 controller `length` 是 `0x151A23A`；另一份无 state-path xref
的相同字面量不是本纵切面的身份。

同一 key 的读写共享进程级可变 hint，不是每个 controller 私有 cache：

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `v` | `0x1AB4EC8` | `0x1111460` | `0x101B69F78` | `0x187D998` |
| `length` | `0x1AB4ED0` | `0x1111468` | `0x101B69F80` | `0x187D9A0` |
| `lengthDone` | `0x1AB4ED4` | `0x111146C` | `0x101B69F84` | `0x187D9A4` |
| `rq` | `0x1AB4EE0` | `0x1111478` | `0x101B69F90` | `0x187D9B0` |
| `p0` | `0x1AB4EE4` | `0x111147C` | `0x101B69F94` | `0x187D9B4` |
| `p1` | `0x1AB4EE8` | `0x1111480` | `0x101B69F98` | `0x187D9B8` |
| `mouth` | `0x1AB4EEC` | `0x1111484` | `0x101B69F9C` | `0x187D9BC` |
| `value` | `0x1AB4EF8` | `0x1111490` | `0x101B69FA8` | `0x187D9C8` |

xref 身份在四端一致：`v/length/lengthDone/rq` 由 Eye 与 Eyebrow state 路径共享；
`p0/p1` 由 request queue serializer/restore 共享；`mouth` 同时出现在 Mouth state 与
Engine 顶层 serialize/unserialize；`value` 由 Selector state 与 timeline 初始化复用。
此外 `length` 还被 `EmoteBustChainSpring` 构造器复用，因此源码把整组槽放到
`motion::detail`，而不是留在 `EmoteEngine.cpp` 的匿名命名空间。

## strict Variant probe 的提交语义

四端 helper 的共同流程是：

```text
temporary = Void
hr = dispatch.PropGet(MEMBERMUSTEXIST, name, hint, &temporary, dispatch)
if hr failed:
    destroy temporary
    return false                    // caller destination unchanged
intermediate = copy(temporary)
callerDestination = intermediate   // copy assignment only after success
destroy intermediate
destroy temporary
return true
```

这与 scalar helper 直接把临时 `tTJSVariant` 交给 `PropGet` 不同。源码因此拆分
`tryGetTJSProperty`（Variant 输出，probe 后提交）和 `tryGetTJSScalarProperty`
（局部 scalar 临时值）两条路径。这个差异在本纵切面可见，因为 Eye/Eyebrow 的
`rq` 以及 Var 的 `frame/prev/target` 都把 by-value 入参 Variant 本身当输出槽复用。

## Object/accessor 生命周期分型

Blink、Eyebrow、Mouth、Angle、Selector 的共同顺序是：

```text
copy incoming Variant closure
force/require Object conversion               // non-object may throw
construct ncbPropAccessor and AddRef dispatch
destroy copied Variant before first probe
probe scalar fields in source order
release accessor on normal or exceptional exit
```

这些函数没有友好的 `Type()!=Object -> return` 分支。一个类型为 Object 但 dispatch
为空的 closure 也没有专门 guard；后续虚调用自然失败。

Var 是唯一例外：它先检查原入参 `Type()==tvtObject`，不满足就返回；满足后才执行同样
的 copy/Object/accessor/early-destroy 生命周期。这个 guard 不能机械推广给其它
controller，也不能从 Var 删除。

## Eye/Eyebrow request queue

四端 Eye 与 Eyebrow 拥有相同拓扑，字段偏移不同但语义一致：

1. scalar probe 完成后，以 strict Variant helper 读取 `rq`，输出槽就是 by-value
   入参 Variant；缺失时队列保持原样。
2. `rq` 成功但不是原生 TJS Array 时队列仍保持原样；入参 Variant 已经被 `rq` 的值
   替换。
3. 只有成功取得 native Array `Items` 后才清空目标 deque。
4. 按 `Items` 顺序迭代。每个 raw item 先 CopyRef、强制 Object、由 accessor retain，
   再在第一次数值读取前销毁临时 Variant。
5. 以 flags 0、忽略 HRESULT 的 named getter，严格先读 `p0`、再读 `p1`，转换/narrow
   为 float 后 push `{p0,p1}`。
6. clear 后任一 item 的 Object 转换、getter、数值转换或 deque grow 抛异常时，不回滚；
   已 push 的前缀保留。

Android ARM64 的旧 libstdc++ 8B 元素 deque 使用 512B block，即每 block 64 个 pair；
A32 的 clear/grow helper 分别位于 `0x55167C`/`0x56664E`。iOS libc++ 使用不同的
block/index 算法。它们是 STL ABI 细节，portable 源码保持
`std::deque<std::pair<float,float>>`，不手工硬编码 block 算术。

## Var channel array restore

Var 依次 strict probe `phase/tick/speed/exponent`，然后对 `frame/prev/target` 重复：

```text
strict probe property into reusable input Variant slot
if missing: preserve the entire destination channel array
copy property Variant; force Object; accessor retain; destroy copy
for index in [0, controller.count):
    temporary = PropGetByNum(flags=0, index)   // HRESULT ignored
    destination[index] = float(temporary.AsReal())
```

映射为 `frame -> currentValue`、`prev -> startValue`、`target -> targetValue`。属性存在
但不是 Object 时会抛错；按索引读取/转换异常时已写前缀保留，不做事务式回滚。三个
属性连续复用同一个入参 Variant 槽，成功 probe 会覆盖上一次内容，失败则保留旧内容，
但因为失败分支立即跳过对应数组，不会误用旧 Variant。

## Mouth、Selector 与 serializer 对称性

Mouth restore 顺序为 `phase,mouth,frame,prev,target,tick,exponent,speed`；Selector 为
`value,phase,speed,tick`。两者都无 outer type guard，且每个缺失 scalar 仅保持对应
目标字段不变。serializer 以相同 schema 和共享 hint 发布字段。

Engine 顶层 `mouth` 子对象的 serialize 与 unserialize 也使用 controller `mouth`
hint。Selector 的 `value` 继续使用 Engine 内现有的 `engineValueHint_guess`，因为四端
已知使用者都在该翻译单元；没有为它凭空创造第二个 portable 槽。

## 本地恢复与 IDB 回写

- `EmoteEngine.cpp` 恢复 strict Variant probe 的临时提交语义，并保留独立 scalar
  helper；新增 retained-accessor overload、flags-0 named/indexed float getter。
- Eye/Eyebrow/Mouth/Selector restore 删除过时的友好 Object guard，按 closure copy、
  accessor retain、临时 Variant early release 的顺序执行。
- request queue 恢复“成功 native Array 后才 clear”、`p0` 先于 `p1`、逐项前缀提交；
  Var 恢复独有 outer guard、复用 Variant 输出槽与逐索引即时提交。
- `MotionDispatch.h` / `RuntimeSupport.cpp` 发布并定义 controller-state 共享 hint；
  `EmoteSpring.cpp` 的 BustChainSpring `length` 读取接到同一槽。
- 四份 recovery IDB 已把可可靠命名的宽字面量和三个 helper 家族恢复为语义
  `_guess` 名；五类 restore、四类 helper、八个 literal 与八个 hint 槽均补证据注释，
  restore/helper 增加 bookmark，并已原位保存。

## 验证

- 使用真实 Emscripten response file 的 `motionplayer-dll.cpp -fsyntax-only` 通过，
  仅有既有 `_tss` deprecated literal-operator warning。
- `cmake --build --preset "Web Debug Build"` 完成 34 个增量步骤，重新编译受共享 header
  影响的 motionplayer 对象，成功生成 `libmotionplayer.a` 并链接最终 `index.html`；
  仅有既有 Emscripten/JSPI/`_tss` warnings。
- 本纵切面的 C++ 与分析文件通过定向 `git diff --check`（换行转换提示不属于 whitespace
  error）。

本页闭合的是 controller-state restore/accessor/request-queue 族，不代表整个
motionplayer 已达到 100%。后续仍需继续按四端证据逐个闭合高价值容器、异常回滚与脚本
边界。controller state 与脚本可见顶层 Dictionary 之间的固定顺序、hint 复用和异常
提交边界另见 `analysis/motionplayer_engine_state_pipeline_four_binary_2026-08-15.md`。

2026-08-16 源码身份复审进一步确认 request queue 的 `p0/p1` 与 Var controller 的三个
channel read 分别是 `ncbPropAccessor::GetValue<float>` 的 named/indexed 模板实例，不是
插件自有 raw-dispatch helper。直接模板表达、内部 float 收窄点和四端 call-site 映射见
`analysis/motionplayer_engine_state_ncb_getvalue_source_identity_four_binary_2026-08-16.md`。
