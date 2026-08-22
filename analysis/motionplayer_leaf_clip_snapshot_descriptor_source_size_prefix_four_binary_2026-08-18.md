# MotionPlayer leaf clip snapshot、descriptor/source/size 提交前缀四端复原（V239，2026-08-18）

## 1. 结论

V239 闭合 common command builder 中 `createdOrChanged=true` 之后、affine/mesh/bezier primitive
之前的完整 mutable prefix。四个当前参考二进制共同证明：

- 本轮 accepted clip 的 left/top/right/bottom 是 callback 前的 call-local snapshot；persistent
  `item.clipRect` 只是同值镜像，后续 `setSize` 和 geometry offset不再读取该可变字段；
- 相反，`sourceState` pointer/object、corners、command mesh vector和相关 division字段在各自消费时
  从 live item重新读取；
- `createdOrChanged=false` 在 descriptor/color/source/size prefix 前离开；
- true 分支依次保有 sourceDescriptor accessor和 sourceColors accessor，并原位发布
  `key -> src -> blendMode -> colors[0..3]`；
- 四个 white color 是 zero-extended `uint32_t(0xFFFFFFFF)`，作为 TJS Integer
  `4294967295`，不是 signed `-1`；
- 全部 descriptor/color 写入完成后才 live读取 `item.sourceState->object` 并调用
  `resolveRenderSource_guess`；
- resolver return Variant保留到 primitive结束，另从它复制一个 source accessor owner，按
  `width -> height` 各读一次并无条件 `AsInteger()`；
- leaf先以 MEMBERENSURE写 `neutralColor=Integer(0)`，再以两个 Real参数调用
  `setSize(float(right-left), float(bottom-top))`；
- 所有普通 HRESULT 均被忽略，只有 exception改变控制流；persistent dictionary与 item publication
  不回滚；
- true-path普通/EH cleanup嵌套为
  `source accessor -> source return Variant -> color accessor -> descriptor accessor -> leaf raw Object`。

本地此前已基本复原 property/owner顺序，但 V238 移除 persistent local geometry 后暴露出一个
snapshot/live 混用：clip width/height在 resolver 前计算，affine/mesh offset却在 resolver与多个 TJS
callback 后重新读取 `item.clipRect`。native 始终用入口 clip snapshot。V239 将四边显式快照，同时
继续让 corners/vector/SourceState保持 native 的后置 live read。

## 2. 四目标 prefix 映射

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| common builder | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| accepted clip snapshot/publication | `0x6C2364..0x6C2370` | `0x58C8D4..0x58C8E0` | `0x1001168A8..0x1001168B8` | `0x1143A2..0x1143B6` |
| created gate | `0x6C2780` | `0x58CAF6` | `0x100116B80` | `0x1146B0` |
| descriptor key/src/blend start | `0x6C2810` | `0x58CB30` | `0x100116BD0` | `0x114702` |
| colors 0..3 loop/start | `0x6C2944` | `0x58CBA0` | `0x100116C5C` | `0x114790` |
| live source resolve | `0x6C2A44` | `0x58CBB6` | `0x100116C80` | `0x1147B2` |
| width then height | `0x6C2ACC`, `0x6C2AF0` | `0x58CBEE`, `0x58CC0A` | `0x100116CD4`, `0x100116CF8` | `0x114802`, `0x11482A` |
| neutralColor | `0x6C2B40` | `0x58CC26` | `0x100116D1C` | `0x114854` |
| setSize | `0x6C2BBC` | `0x58CC8E` | `0x100116D8C` | `0x1148EA` |
| cleanup start | `0x6C3118` | `0x58D0DC` | `0x1001172F4` | `0x114DCC` |

绝对地址只作为 recovery IDB 坐标；编译源码只保留共同语义。

## 3. clip snapshot 与 live item 的精确分层

outer builder在进入 leaf resolver前已经完成 intersection rounding，并把四个 float同时：

1. 写进 persistent `item.clipRect`；
2. 保存在当前 command-builder activation 的 registers/stack locals。

随后所有 callback 都只影响 persistent/reentrant state，不能改变本 activation 的四个 clip locals。
因此后半段数据源是：

| consumer | source identity |
|---|---|
| setSize width/height | pre-callback clip snapshot |
| affine x/y offset | pre-callback clip-left/top snapshot |
| mesh/Bezier float2 offset | pre-callback clip-left/top snapshot |
| affine corners | callback 后 live item corners |
| type-1 points | callback 后 live `commandBezierPatchPoints` |
| type-2 points | callback 后 live `commandCompositeMeshPoints` |
| resolve source argument | descriptor/color callback 后 live `item.sourceState->object` |
| Bezier source dimensions/division | 更晚的 live SourceState/item reads |

这不是“全部 snapshot”也不是“全部 live”。例如 descriptor key setter若通过脚本重入另一次 command
build并重写同一 item clip，外层 activation仍用旧 clip origin，却会在 primitive阶段看到重入后的
corners/vector/SourceState。native没有 generation token、item copy或 callback 后一致性重检。

## 4. descriptor 与 color 的原位提交

true branch先从 Player persistent Variant各建立一个 full-lifetime accessor：

```text
descriptor = retained accessor(player.sourceDescriptor)
descriptor.SetValue("key", item.commandKey, MEMBERENSURE, keyHint)
descriptor.SetValue("src", item.commandSrc, MEMBERENSURE, srcHint)
descriptor.SetValue("blendMode", int32(0), MEMBERENSURE, blendHint)

colors = retained accessor(player.sourceColors)
for i in 0..3:
    colors.SetValue(i, uint32(0xFFFFFFFF), MEMBERENSURE)
```

每次写入普通 status不形成 gate；setter抛异常才 unwind。因为两者是跨 item复用的 mutable dictionaries，
任意异常都保留已经完成的 prefix：

- key throw：本轮未提交任何后续字段；
- src throw：新 key保留；
- blendMode throw：新 key/src保留；
- color[k] throw：三个 descriptor字段和 colors `[0,k)` 保留；
- 后续 resolver/width/neutral/setSize/primitive throw：完整 descriptor/colors保留。

accessor retain的是建构当时对象。若重入替换 Player对应 Variant，outer accessor仍保活旧 dictionary；
但后续 `resolveRenderSource_guess` 按它自己的实现读取 Player workspace/live source，不被便利地改成沿用
outer accessor snapshot。

## 5. source return/accessor owner tree

descriptor/color全部写完后，builder才读取 live `item.sourceState` 并传其 Object Variant：

```text
sourceObject = player.resolveRenderSource_guess(item.sourceState->object)
sourceAccessor = retained accessor(CopyRef(sourceObject))
width  = sourceAccessor.GetValue<tjs_int>("width",  flags=0, widthHint)
height = sourceAccessor.GetValue<tjs_int>("height", flags=0, heightHint)
```

resolver return `sourceObject` 是整个 primitive argv的 owner，并跨 width/height、leaf property、setSize和
image primitive存活。accessor由独立 copy + AsObject owner构成；其临时 Variant在 width前已经死亡，
raw Object owner直到 primitive后才 Release。

width/height的普通失败不返回 optional：result从 Void开始，status忽略，随后总是 `AsInteger()`。
因此普通 failure且不写 result得到0；写 malformed value则按 Variant conversion语义转换/抛异常。width
exception阻止 height，height exception阻止 neutralColor；已发布 descriptor/colors不回滚。

## 6. neutralColor 与 setSize

两个 source dimensions取得后，leaf raw Object先接收：

```text
leaf.PropSet(MEMBERENSURE, "neutralColor", Integer(0), neutralColorHint)
leaf.FuncCall(flags=0, "setSize", setSizeHint, result=null,
              argv=[Real(clipRight-clipLeft),
                    Real(clipBottom-clipTop)], objthis=leaf)
```

clip差先按 f32做减法，再提升 tjs_real；不是直接 double减法。neutralColor普通 failure仍继续 setSize，
setSize普通 failure仍继续 affine/mesh/bezier。任一 callback抛异常则在对应点停止。created=true但
`leafLayer.AsObject()` 得到 null时，descriptor/color/source/width/height仍可完成，第一次自然 leaf
解引用发生在 neutralColor；没有提前 null recovery。

## 7. cleanup 与异常 partial state

normal tail和四端 EH cleanup共同实现以下逆序：

```text
release sourceAccessor raw Object
destroy sourceObject return Variant
release colors accessor raw Object
release descriptor accessor raw Object
release leafLayer copied raw Object
destroy resolver payload vectors / remaining locals
```

该 cleanup只回收局部 owner，不回滚：

- persistent `item.leafLayer` replacement；
- V237 的 renderLayerId latch；
- persistent descriptor/color字典写入；
- clip/rawFlag21 prefix；
- leaf对象内部已完成的 neutralColor/size/primitive副作用。

普通 status与 exception必须分开：所有 dispatch status均丢弃，但 TJS callback可直接抛 C++ exception，
后者进入上述 EH cleanup并跳出整个 command build，因而 normal-only adaptor end-pass/group composition也
不会执行。

## 8. 源码修正

`PlayerRenderExecute.cpp` 的 leaf helper在任何 resolver/property callback前新增显式：

```cpp
const float clipLeft   = item.clipRect[0];
const float clipTop    = item.clipRect[1];
const float clipRight  = item.clipRect[2];
const float clipBottom = item.clipRect[3];
```

width/height、affine double formula、mesh/Bezier float2 offset全部改用这些 locals。corners、command
vector和 SourceState仍在原消费位置 live读取。除这项 snapshot/live 修正外，现有 descriptor/color/
source/accessor/neutral/setSize的声明顺序已经与四端一致，不做无证据重构。

## 9. IDB 写回

四份 canonical recovery IDB各追加10条 comment、4个 bookmark，总计40 comment、16 bookmark；
没有新 rename/type。注释覆盖：function summary、clip snapshot、created gate、descriptor顺序、color
zero-extension、live source read、width/height、neutral/setSize、snapshot/live geometry split与cleanup
nesting。四库均 save、health probe、close；最终 IDA session audit为0。

## 10. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless syntax compilation：通过；
- Web Debug完整增量构建：3 steps，通过；
- Wasmtime Headless Debug完整增量构建：4 steps，通过；
- `krkr2_wasmtime_guest` 使用更新后的 shared object重新链接并完成 exnref转换；
- Node module construction、imports/exports、CTest、no-work、diff与IDB audit在 final pass闭合；
- 产品相对 V238只变化该 snapshot修正，两个 module/CODE均缩23 byte/`0x17`，DATA/name不变。

| product | size | CODE | SHA-256 |
|---|---:|---:|---|
| Web `out/web/debug/index.wasm` | 85,654,298 B | `0x1A40E2C` | `7B77840D6263BA58D397375EDA3C66C27043561A6615CAB4B9CF0D67502669ED` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,001,439 B | `0x19E8DDA` | `15245DF4EFA3596A4E2E356F32A430D222FD1A945AEA38CDD1C6C01FCAE5A565` |

## 11. 下一边界

common leaf materialization至此已闭合 latch、leaf owner、descriptor/color/source/size、call-local affine与
owning mesh Array。V240 已进一步闭合 aux group的完整四边 target clip、paint union、viewport
wrong-empty边界、`composedLayer` owner/factory、逐子 Variant CopyRef/live mask与 success-only publication；
见 `analysis/motionplayer_group_composed_layer_target_clip_live_mask_exception_publication_four_binary_2026-08-18.md`。
V241 转入 common builder的 normal-only retired-layer tail与 caller exception传播。
