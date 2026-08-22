# MotionPlayer render helper 身份与旧地址标签四参考审计（2026-08-14）

> 2026-08-16 更正：本文当时只迁移名字，未复核本地 helper 是否仍有 caller；列出的
> `ensureAccurateSlaStateLayer_guess` 实际为零 caller 的旧端口伪影，而且其 native Layer
> 解包/父子修补路径与后来完成的四端 accurate renderer TJS owner 链矛盾。该 helper 已按
> `motionplayer_dead_accurate_sla_native_state_layer_helper_four_binary_2026-08-16.md` 删除；
> 其余 active helper 的身份结论不受影响。
>
> 2026-08-18 V238 correction：上一句对 mesh-point Array helper 不成立。四端 fresh function/xref
> 审计确认它是各自有 16 个 caller 的独立 native function，并非重复内联 block；真实返回值是
> owning `tTJSVariant(Array)`，body直接向 `tTJSArrayNI::Items` 追加 Real。源码已更名为
> `buildMeshPointTJSArrayVariant_guess`，旧 raw dispatch/`PropSetByNum` 结构已删除。`callLayer*`
> packer仍是源码级抽取。详见
> `analysis/motionplayer_mesh_point_array_variant_leaf_local_geometry_four_binary_2026-08-18.md`。

## 结论

本轮重新反编译 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、
iOS armv7 当前 1.3.9 产物，闭合了 command builder、普通 canvas renderer 与 accurate
`SeparateLayerAdaptor` renderer 的完整函数边界和直接调用链。

编译源码中原有的 `getLayerClassDispatchVariantLike_0x5CB08C`、
`buildMeshPointTJSArrayLike_0x6C715C`、`callLayer*Like_0x6C7440`、
`callLayerFillRect5Like_0x6C4E28`、`callLayerPropGetIntLike_0x6C99B8` 与
`renderAccurateSlaLike_0x6C9CA8` 等名称，不能继续作为当前 native function 身份：

- 后缀来自已经退役的旧 `libkrkr2.so` 单目标定位；
- 当前四份产物没有与这些 portable helper 一一对应的一组独立函数边界；
- 多数 `callLayer*` helper 是从三个完整 native 大函数中重复出现或内联的 Variant/FuncCall
  block提取出的源码级复用单元；mesh-point Array builder是 V238 已恢复的独立例外；
- 相同数值地址在当前 Android arm64 产物中可能落入完全不同的函数，继续保留后缀会把
  renderer、STL helper 和 Player property accessor 错绑在一起。

因此本轮只迁移身份表达，不改参数、receiver/objthis、Variant 构造顺序、返回值忽略策略、
branch、容器或生命周期：所有 helper 改为语义 `_guess` 名，完整 accurate renderer 改为与
四份 recovery IDB 一致的 `renderAccurateSeparateLayerAdaptor_guess`。绝对地址只留在本文的
四端证据表，不再进入这些编译源码的函数名、注释和诊断标签。

## 三个完整 native 函数族

| 目标 | command builder | 完整 canvas renderer | 完整 accurate SLA renderer |
|---|---:|---:|---:|
| Android arm64-v8a | `0x6C2208`, `0x1BAC` | `0x6C4820`, `0x2578` | `0x6C7088`, `0x203C` |
| Android armv7 | `0x58C7C4`, `0x104A` | `0x58E2CC`, `0x1806` | `0x590468`, `0x1494` |
| iOS arm64 | `0x1001167BC`, `0x1198` | `0x1001186E0`, `0x18E0` | `0x10011A9E8`, `0x1590` |
| iOS armv7 | `0x114118`, `0x12C4` | `0x11653C`, `0x1AEA` | `0x118D70`, `0x1788` |

四份 recovery IDB 对三列统一使用：

```text
Player_buildRenderCommands_guess
Player_renderToCanvas_guess
Player_renderAccurateSeparateLayerAdaptor_guess
```

四端新取的 xref 给出完全一致的源码级调用图：

```text
Player_draw_guess
  -> Player_renderToCanvas_guess
       -> Player_buildRenderCommands_guess

Player_renderToSeparateLayerAdaptor_guess
  -> Player_renderAccurateSeparateLayerAdaptor_guess
       -> Player_buildRenderCommands_guess
```

每个 command builder 恰有来自后两种 renderer 的两个 direct code xref；每种完整 renderer
各只有对应外层入口的一个 direct code xref。也就是说，本地 `PlayerRenderInternal.*` 中的
细粒度 `callLayer*` 并不是 native call graph 上位于 builder 和 renderer 之间的一层函数族，
而是为避免 portable C++ 重复铺开 Variant 构造和 TJS dispatch 所做的源码级抽取。

## 宽字符串与 dispatch block 归属

普通 IDA string search 对 `affineCopy`、`meshCopy`、`bezierPatchCopy`、`fillRect`、
`operateMesh`、`operateBezierPatch` 和 `operateRect` 多数返回空，因为四份产物中的 TJS
member literals 是 UTF-16LE。按宽字节重新检索后，四端都能找到完整集合：

```text
Layer
width / height
setSize / fillRect / setClip
affineCopy / meshCopy / bezierPatchCopy
operateAffine / operateRect / operateMesh / operateBezierPatch
```

对这些 literal 的 data xref 重新分组可见：

- command builder 构造/发布 prepared item 所需的 Layer、setSize、copy-family 参数；
- canvas renderer 复用 copy-family、operate-family、setClip/reset 与 property reads；
- accurate renderer 复用 source width/height、setSize 与 copy-family，并直接完成每 item
  layer publication；
- 同一 literal 也可被 MotionLayer extension 或 SourceCache 使用，所以 literal address 本身
  不能替代 enclosing-function 身份；必须以 xref 所属完整函数为准。

四端优化器对这些 block 的外联/内联和 register allocation 不同，但 receiver 角色一致：

- `affineCopy`/`meshCopy`/`bezierPatchCopy`、`setSize`、`fillRect` 使用具体 Layer instance
  作为 receiver 和 objthis；
- target `operate*` 和 `setClip` 使用 Layer class dispatch 作为 receiver，目标 Layer 只作为
  objthis；
- `setClip` argc=4 传四个 Real Variant，argc=0 走同一 member 的 reset 入口；
- mesh-point Array 是按 `x,y,x,y,...` 顺序逐个发布的 owning TJS Array；offset 在写入前
  加到每个 float coordinate，再提升为 TJS Real；
- portable helper 继续用 `_guess`，因为完整 source symbol 没有保留，不能把源码级抽取冒充
  为四端真实命名的独立 native function。

## 旧地址在当前 Android arm64 中的真实含义

直接向当前 Android arm64 recovery IDB 查询旧后缀数值，得到：

| 旧后缀位置 | 当前产物中的 enclosing function / 含义 |
|---:|---|
| `0x5CB08C` | `std::vector<unsigned char>::_M_range_insert` 内部，不是 Layer class resolver |
| `0x6C4E28` | 完整 `Player_renderToCanvas_guess` 内部 |
| `0x6C6B48` | 同一个完整 `Player_renderToCanvas_guess` 内部 |
| `0x6C715C` | 完整 `Player_renderAccurateSeparateLayerAdaptor_guess` 内部 |
| `0x6C7440` | 同一个完整 accurate renderer 内部 |
| `0x6C99B8` | `Player_setTransformOrder_guess` 内部，不是 Layer property read helper |
| `0x6C9CA8` | `Player_getCameraPosition_guess` 内部，不是 accurate renderer |

这个碰撞表同时解释了旧注释为什么会互相矛盾：`0x6C4E28`、`0x6C715C`、`0x6C7440`
在当前产物中只是大函数内的 instruction address，而 `0x5CB08C`、`0x6C99B8`、
`0x6C9CA8` 更已经属于完全不同的功能。把它们附在 portable helper 名后，会暗示不存在的
四端独立函数映射，并妨碍后续恢复真实 source structure。

## 编译源码迁移

本轮把以下 active render 标识统一为语义 `_guess` 名：

- `getLayerClassDispatchVariant_guess`；
- `buildMeshPointTJSArrayVariant_guess`（V238 已确认独立 native function）；
- `callLayerOperateAffine_guess`、`callLayerAffineCopy_guess`、
  `callLayerOperateRect_guess`；
- `callLayerMeshCopy_guess`、`callLayerBezierPatchCopy_guess`、
  `callLayerOperateMesh_guess`、`callLayerOperateBezierPatch_guess` 与内部
  `callLayerMeshFamily_guess`；
- `callLayerSetSizeReal_guess`、`callLayerFillRect4_guess`、
  `callLayerFillRect5_guess`、`callLayerSetClip_guess`、
  `callLayerResetClip_guess`、`callLayerPropGetInt_guess`；
- `resolveBlendOperationMode_guess`、`shouldUseDirectRenderPath_guess`；
- `accurateSlaLayerType_guess`、`shouldRenderAccurateSlaItem_guess`、
  `computeAccurateSlaClip_guess`、`ensureAccurateSlaStateLayer_guess`；
- `Player::renderAccurateSeparateLayerAdaptor_guess` 与 accurate post-draw sidecar helper。

迁移同步覆盖 `Player.h`、`PlayerRenderInternal.*`、`PlayerRenderExecute.cpp`、
`PlayerRenderTargets.cpp`、`PlayerTimeline.cpp` 和 unit tests。相关编译源码注释改写为共同
数据流/argument shape，不再引用旧单目标地址；opt-in trace labels 也改用语义名，避免日志继续
宣称旧地址是当前 renderer 身份。

没有修改任何函数体表达式、参数数组、member hint、HRESULT 处理、对象 AddRef/Release、
Layer owner 或 render branch。诊断标签的文本改变只发生在原本已隔离的 opt-in sidecar 中。

## recovery IDB 落地

四份 recovery IDB 的三组完整函数入口当时追加了 helper-identity 注释；其中
`callLayer*_guess` 的源码抽取结论继续成立。V238 已对错误包含进去的 mesh-point helper逐端追加
correction，并把四个真实函数重命名为 `buildMeshPointTJSArrayVariant_guess`。随后对 12 个完整
函数强制刷新 Hex-Rays；全部返回成功。四份 recovery IDB 已原位保存。

## 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax-only 返回 0；只有既有
  `_tss` deprecation warning；
- `cmake --build --preset "Web Debug Build"` 完成 31 个步骤并成功链接最终 `index.html`/Wasm；
  warning 只有既有 `_tss`、imagepacker `nodiscard`、pthread/memory-growth、JSPI 与 JS
  library 项；
- 定向 stale scan 确认 active render 源码与测试中不再存在上述七组 `Like_0x...`、
  `renderAccurateSla_0x6C9CA8` 或旧 failure label；
- 四份 IDB 的 command builder、canvas renderer、accurate renderer 均重新反编译并保存；
- active stale scan 与 selected-render comment scan 都为零命中；定向 `git diff --check`
  返回 0，仅有工作区既有的 LF/CRLF 提示，没有 whitespace error。

本轮闭合的是 renderer helper 的真实身份层级和调用图，不声称三个大函数的每个内部 block
都已经恢复成最终 source-level function partition；因此不能去掉未知 helper 名上的 `_guess`。
