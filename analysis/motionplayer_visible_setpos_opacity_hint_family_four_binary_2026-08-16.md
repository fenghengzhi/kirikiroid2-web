# motionplayer `visible / setPos / opacity` 共享 hint 家族与 shared-D3D 旧结论更正（四参考二进制）

日期：2026-08-16

## 1. 结论

四个 `reference/binaries/` 在上一组十二个 renderer primitive hint 之后，都紧邻放置
三个独立的 32 位、零初始化、进程级 TJS member-hint 槽，顺序严格为：

```text
visible
setPos
opacity
```

这三个槽的身份不是按源码函数或输出 Dictionary 分配，而是跨调用链共享：

- `visible`：`SeparateLayerAdaptor::assign`、accurate SLA renderer、
  `Player::calcViewParam`、sticky shared-D3D `Player::draw`；
- `setPos`：只由 accurate SLA renderer 使用；
- `opacity`：`SeparateLayerAdaptor::assign`、accurate SLA renderer、
  `Player::calcViewParam`、`Player::getCommandList`。

最重要的旧结论更正是：sticky shared-D3D 路径的 `visible=true` **没有独立
member-hint 槽**。四端 `Player::draw` 都直接取上述同一个 `visible` 地址。旧移植中的
`sharedD3DVisibleMemberHint_guess` 以及旧分析“shared-D3D call site 使用独立 hint”的说法，
是尚未完成全局 xref 身份审计时留下的过时推断，本轮已删除和更正。

紧随其后的第四个槽是 `isValid`。它只被 `Player::getBounds` 使用，控制 bounds Dictionary
的三条有效性分支，因此是 V160 的明确右边界，不属于本三槽家族。

本文绝对地址只作为四份参考二进制的分析坐标；编译源码只保存语义名和四端共同结论。

## 2. 连续数据布局

| 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `visible` | `0x1AB5488` | `0x1111924` | `0x101B69950` | `0x187D5F4` |
| `setPos` | `0x1AB548C` | `0x1111928` | `0x101B69954` | `0x187D5F8` |
| `opacity` | `0x1AB5490` | `0x111192C` | `0x101B69958` | `0x187D5FC` |
| next: `isValid` | `0x1AB5494` | `0x1111930` | `0x101B6995C` | `0x187D600` |

四端 stride 都是 4。旧 recovery IDB 把前三项错误表达为三个 1-byte item；本轮先
undefine 精确 12-byte 区间，再分别建立三个 `unsigned int` 数据项。fresh `globals`
回读确认前三项全部 `size=4`，其后的 `Player_parameterListHint_guess` 仍从 `+0x10`
开始；没有因重建前三项吞并后继数据。

## 3. 宽字符串交叉验证

普通 IDA string 查询会把一部分 TJS 宽字面量显示为首字符，因此本轮按 UTF-16LE 原始
字节重新搜索。三个家族成员的精确 literal 位置为：

| literal | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `visible` | `0x15187D2` | `0xDBDBC0` | `0x10195B658` | `0x174D9BC` |
| `setPos` | `0x14CFC8E` | `0xD81894` | `0x10195BF82` | `0x174E2E6` |
| `opacity` | `0x14BE734` | `0xD84F80` | `0x10195B70E` | `0x174DA72` |

右边界另以精确 pattern
`69 00 73 00 56 00 61 00 6C 00 69 00 64 00 00 00` 搜索。部分二进制有
同字节序列的无关命中，按 xref 过滤后，`Player::getBounds` 使用的 `isValid` 为：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x14C9986` | `0x5924C8` | `0x10195C77A` | `0x174EADE` |

这些 literal xref 与相邻 global xref 的 consumer 集相互独立地确认了成员名、顺序和
`isValid` 右边界。

## 4. Consumer 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| SLA assign | independent entry `0x6A97F0`, IDA merged under `0x6A965C` | `0x57C814` | `0x10010347C` | `0x100874` |
| accurate SLA renderer | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |
| `calcViewParam` | `0x6CE908` | `0x594958` | `0x1001201CC` | `0x11EED4` |
| `getCommandList` | body xref `0x6D1040` | `0x595FF0` | `0x100121EB0` | `0x120CF8` |
| `Player::draw` | `0x6D3398` | `0x597864` | `0x100123C84` | `0x122F28` |
| next boundary: `getBounds` | `0x6C9E64` | `0x59226C` | `0x10011CBD4` | `0x11B53C` |

Android arm64 的 `getCommandList` 大函数 tail chunk 仍被 IDA 错归到 8-byte
`EmotePlayer_getCommandList_guess @ 0x67F900` thunk；这里使用真实 `opacity` xref
`0x6D1040` 作为 body 坐标，不把这个 IDA function-chunk 误归类当成源码调用关系。

将每个 global 的完整 data xref 去重到函数语义后，四端得到同一矩阵：

| consumer | `visible` | `setPos` | `opacity` |
|---|:---:|:---:|:---:|
| SLA assign | ✓ |  | ✓ |
| accurate SLA renderer | ✓ | ✓ | ✓ |
| `calcViewParam` | ✓ |  | ✓ |
| `getCommandList` |  |  | ✓ |
| sticky shared-D3D `Player::draw` | ✓ |  |  |

`isValid` 的 data xref 则全部落在 `Player::getBounds`，没有与上表任何 consumer 共享。

## 5. `SeparateLayerAdaptor::assign` 数据流、receiver 与失败边界

三份有独立 function boundary 的反编译，以及 Android arm64 合并函数内的 data xref，
共同恢复出如下主干：

```text
active.swap(retired)
assignSequence = 0
for each source.active ordered-map entry:
    retain/copy source Layer Variant
    targetVariant = resolveLayerNodeInternal(ordinal, sourcePayload)
    sourceAccessor = strict accessor(sourceVariant)
    targetAccessor = strict accessor(targetVariant)

    ignore target.FuncCall("assignImages", sourceVariant)
    height = source.GetValue<int>("height", default=0, two-stage null hints)
    width  = source.GetValue<int>("width",  default=0, two-stage null hints)
    ignore target.FuncCall("setSize", shared setSize hint, width, height)

    absolute = source.GetValue<int>("absolute", default=0, two-stage null hints)
    visible  = source.GetValue<int>("visible",  default=0, two-stage null hints)
    opacity  = source.GetValue<int>("opacity",  default=0, two-stage null hints)
    type     = source.GetValue<int>("type",     default=0, two-stage null hints)
    left     = source.GetValue<int>("left",     default=0, two-stage null hints)
    top      = source.GetValue<int>("top",      default=0, two-stage null hints)

    ignore target.PropSet(MEMBERENSURE, "absolute", rebasedAbsolute)
    ignore target.PropSet(MEMBERENSURE, "visible", visible,
                          shared visible hint)
    ignore target.PropSet(MEMBERENSURE, "opacity", opacity,
                          shared opacity hint)
    ignore target.PropSet(MEMBERENSURE, "type", type)
    ignore target.PropSet(MEMBERENSURE, "left", left)
    ignore target.PropSet(MEMBERENSURE, "top", top)
normal tail: invalidate/clear all remaining retired entries
```

V160 关注的 visible/opacity source getter 都继续使用 null hint；共享身份只属于 target
publication。V183 后续进一步证明这里每个 `GetValue(default=0)` 都先执行
`MEMBERMUSTEXIST`/null-hint probe 并丢弃结果，成功后再执行 flags 0/null-hint read，且忽略
第二次 HRESULT；target `setSize` 也复用插件级 shared word。详见
`analysis/motionplayer_separate_layer_assign_double_read_set_size_shared_hint_boundary_four_binary_2026-08-17.md`。

两次 visible/opacity setter 的精确 ABI 都是：

- `flags = 512`（`TJS_MEMBERENSURE`）；
- value 为非 null Integer `tTJSVariant *`；
- receiver/`objthis` 都是 target Layer dispatch；
- `visible`/`opacity` 分别传入本三槽家族的对应地址；
- HRESULT 被忽略，失败不会跳过后续 property、不会回滚 map，也不会改变 assign 的
  `TJS_S_OK` 正常返回。

正常清理先释放 target accessor，再释放 source accessor，后续再销毁局部 target/source
Variant owners；retired tree 只在完整循环的正常尾部清空。dispatch 抛异常时仍保留 native
的渐进 map/property 提交边界，本轮没有用 RAII 或事务回滚改变它。

同一函数里的 `type/left/top` 属于其它已存在的 hint 地址家族，不应被误并入这三个连续槽；
其 source-null/target-shared 分流已由 2026-08-17 V182 闭合，分别接入既有 type 与 geometry
family。`absolute` target-publication 的独立 shared hint 已由 V180 闭合，source getter 仍
保持 null hint。详见
`analysis/motionplayer_separate_layer_absolute_shared_hint_boundary_four_binary_2026-08-17.md` 与
`analysis/motionplayer_separate_layer_assign_type_left_top_shared_hint_boundary_four_binary_2026-08-17.md`。

## 6. Accurate SLA 的调用 ABI

四端 fresh decompile 都显示 accurate renderer 在最终 publish Layer 上执行：

```text
publishLayer.FuncCall(
    flags=0,
    member="setPos",
    hint=&setPos,
    result=null,
    argc=2,
    argv={Real(left), Real(top)},
    objthis=publishLayer)

publishLayer.PropSet(MEMBERENSURE, "type", Integer(type), ...)
publishLayer.PropSet(MEMBERENSURE, "visible", Integer(1), &visible)
publishLayer.PropSet(MEMBERENSURE, "opacity", Integer(opacity), &opacity)
```

`setPos` 的两个参数明确是 Real，不是 Integer；结果指针为 null，返回 status 不参与分支。
`visible` 和 `opacity` 的 flags 均为 512，receiver 与 objthis 同为 publish Layer。可选
mask 路径把 base Layer 隐藏为 `visible=0` 时，也复用同一个 `visible` 槽；不存在
hidden/published 两个 visible hints。

Layer Variant、`ncbPropAccessor` 和 source/publish dispatch owner 的销毁顺序仍由既有
accurate SLA 生命周期纵切面负责，本轮只把三处 call ABI 绑定到精确全局身份，没有改变
resolve、assignImages、setSize、mask、frame overlay 或 map normal-tail cleanup。

## 7. calc/command 输出与 shared-D3D 旧结论更正

`calcViewParam` 创建/填充输出 Dictionary 时：

- `visible` 复用 V160 `visible`；
- `opacity` 复用 V160 `opacity`；
- 两者都不是 calc-local slot。

`getCommandList` 的 command Dictionary 只复用 V160 `opacity`，没有 `visible/setPos`
xref。

sticky shared-D3D `Player::draw` 的四端尾部共同为：

```text
target.FuncCall(flags=0, "setSize", ..., result=null,
                argc=2, argv={Integer(width), Integer(height)},
                objthis=target)
target.PropSet(MEMBERENSURE, "visible", Integer(1), &shared visible,
               objthis=target)
sharedAdaptor.renderFromPlayer(player, preparedMainList)
sharedAdaptor.captureCanvas(selectedTarget)
```

四端 `visible` 指令/xref 坐标分别为 `0x6D3724/0x6D372C`、
`0x597AB8/0x597AC0`、`0x100123FDC`、`0x12327C/0x123282/0x123294`，
全部解析到本三槽家族的第一个地址。IDB 的全名查询也只得到这一份 motionplayer
`visibleMemberHint` 数据项；邻近的 `g_sharedD3DAdaptor_guess` 是持久 raw renderer 指针，
不是 TJS hint，不能据此虚构第二个 visible 槽。

因此旧源码的独立 `sharedD3DVisibleMemberHint_guess` 不只是命名冗余：TJS 会在两个不同的
mutable cache word 中维护同名成员缓存，实际地址身份也会偏离原版。删除它并让 draw 复用
`visibleMemberHint_guess` 才是四端一致行为。

## 8. `isValid` 右边界

`Player::getBounds` 先创建 fresh Dictionary 并写
`left/top/right/bottom/width/height`。相邻 `isValid` 槽恰有三条语义写入：

1. min/max 几何顺序无效时写 false；
2. 任一 min/max 不是有限分类时写 false；
3. 顺序有效且全部有限时写 true。

三处都使用同一个后继槽、Dictionary receiver 和 `TJS_MEMBERENSURE`，然后返回 owning
Dictionary Variant。其唯一 consumer、三分支控制流和 literal xref 都与
`visible/setPos/opacity` 完全不同，因此不应为了“相邻四项”把它并进 V160。后续 V161 已
完整恢复其共享槽、三分支、六个既有 geometry hint 的复用、binary64 classifier 和
Dictionary accessor 生命周期，详见
`motionplayer_get_bounds_isvalid_shared_hint_lifecycle_four_binary_2026-08-16.md`。

## 9. 源码与测试落地

本轮源码更改：

- `MotionDispatch.h` / `RuntimeSupport.cpp`
  - 把 `visible -> setPos -> opacity` 按四端真实连续顺序放在十二个 renderer primitive
    globals 之后；
  - 从旧 command/frame-parser 分散位置移出三项；
  - 删除虚构的 `sharedD3DVisibleMemberHint_guess` declaration/definition；
- `SeparateLayerAdaptor.cpp`
  - integer property helper 增加可选 hint 参数；
  - assign 的 target `visible/opacity` publication 传入共享槽；
  - 本轮未覆盖的 helper 调用当时显式保留 null/default 边界；后续 V180/V181/V182/V183 分别
    闭合 `absolute`、`hitThreshold`、target `type/left/top`、source 双阶段读取与 shared
    `setSize`，source 两阶段 hint 均保持 null；
- `PlayerRenderTargets.cpp`
  - sticky shared-D3D `visible=true` 改为复用 `visibleMemberHint_guess`；
- `motionplayer-dll.cpp`
  - 锁定三槽彼此不同且与前一 `drawLine` 槽不同；
  - 实际驱动 `SeparateLayerAdaptor::assignCompat`；后续 V183 将 source 覆盖加强为 16 次
    probe/read、交替 flags、全 null hints、probe value 丢弃与第二次 failure-through，并锁定
    setSize exact shared hint；target property/value/objthis 与最终 `TJS_S_OK` 覆盖保持。

旧 `motionplayer_shared_d3d_adaptor_lifecycle_four_binary_2026-08-14.md` 的核心 call order、
raw shared adaptor 生命周期和 selected-target owner 仍有效；只把其中“visible 独立 hint”
两处表述标为本轮 superseded 并改成共享身份。`plan.md` 的相同旧句也同步更正。

## 10. IDB 回写

四份 recovery IDB 均完成：

- 精确 undefine 12 bytes，再建立三个 4-byte `unsigned int`：
  - `g_motion_visibleMemberHint_guess`
  - `g_motion_setPosMemberHint_guess`
  - `g_motion_opacityMemberHint_guess`
- 给三项和后继 `isValid` 边界写入数据注释；
- 给 assign、accurate、calcView、getCommandList、draw 与 getBounds 追加 consumer/ABI/
  stale-correction 注释；
- 添加 `V160 complete visible/setPos/opacity shared member-hint family` bookmark；
- force-recompile 六组 consumer（Android arm64 getCommandList 仍按 IDA 错归 chunk
  回读）；
- fresh global 回读确认三项独立且均为 size 4；
- 四份数据库全部原位保存成功。

## 11. 验证

2026-08-16 完成：

- ordinary `motionplayer-dll.cpp -fsyntax-only`：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- fresh Web Debug configure + build：`57/57`，最终链接成功；
- fresh Wasmtime Headless Debug configure + build：`90/90`，最终链接成功；
- Node `WebAssembly.Module` parse：两份 wasm 均通过；
- `llvm-objdump -h`：两份 wasm section table 均通过；
- Web wasm：`85,648,901` bytes，539 imports / 69 exports；
- Headless wasm：`84,996,042` bytes，538 imports / 69 exports；
- 相比 V159，两份 wasm 都精确减少 22 bytes；import/export 数不变；
- 两个 CTest build tree 均正常运行，当前都报告 `No tests were found`；新增回归已由
  ordinary/headless 两种 test-TU syntax 编译覆盖，但仓库当前没有把该 TU 注册为 CTest；
- 构建只有既有 `_tss`、CMake package、pthread memory-growth、JSPI experimental 和
  JS-library warnings，没有本轮新增 warning/error。
