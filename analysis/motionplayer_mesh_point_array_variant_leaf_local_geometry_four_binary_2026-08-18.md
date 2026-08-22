# MotionPlayer mesh-point owning Array Variant 与 leaf call-local geometry 四端复原（V238，2026-08-18）

## 1. 结论

V238 修正了两个由旧 helper-identity 注释长期遮蔽的真实偏差。

第一，四个当前参考二进制中的 mesh-point Array builder 不是 renderer 大函数内重复 block，也不是
portable 为方便抽出的 raw-dispatch helper。它在每个目标里都是一个独立 native function，恰有 16
个 code caller，共同签名/语义是：

```text
buildMeshPointTJSArrayVariant_guess(
    const vector<MeshPoint> &points,
    const float offset[2]) -> owning tTJSVariant(Array)
```

helper 创建 `{owning Array Variant, borrowed tTJSArrayNI::Items*}`，逐点以 **float
`offset + point`** 计算 x/y，提升为 TJS Real 后直接追加到 native Items deque，最后 copy 返回
owning Variant并销毁 helper-local owner。它没有逐 index `PropSetByNum`、没有 script callback、
没有 member hint，也不返回需要手工 `Release()` 的裸 Array dispatch。

第二，common command builder 的 clip-local geometry 不是 `PreparedRenderItem` 持久状态：

- `createdOrChanged == false` 在任何 local affine Real 或 translated point Array 构建前离开；
- meshType 0 只在 affineCopy 参数区构造 6 个 call-local Real；
- meshType 1 把 `commandBezierPatchPoints` 交给共享 helper；
- meshType 2 把 `commandCompositeMeshPoints` 交给共享 helper；
- native item 中不存在 `localCorners` 或 `localMeshPoints` 字段/owner。

本地旧端口却在 leaf resolver 前无条件写两个 Web sidecar；type 0 会多复制一个无关 mesh vector，
type 1 更错误地从 ordinary `meshPoints` 建 Array，并采用 `(point - 0.5) - clip` 的额外 float
quantization。随后旧 helper 通过脚本 `PropSetByNum` 写 raw Array，FuncCall 抛出时还可能泄漏 raw owner。
V238 已删除整条非 native 状态/调用链。

## 2. 独立 helper 的四目标映射

| target | helper | size | code callers | common type-2 call | common type-1 call |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6C453C` | `0x188` | 16 | `0x6C2C40` | `0x6C2FDC` |
| Android armv7 | `0x58E050` | `0x9C` | 16 | `0x58CCFA` | `0x58D004` |
| iOS arm64 | `0x100118380` | `0xB4` | 16 | `0x100116E10` | `0x100116F88` |
| iOS armv7 | `0x116160` | `0xEE` | 16 | `0x11496A` | `0x114AD2` |

四端 caller 集合都分布在 common command builder、canvas renderer、accurate SLA renderer、
private-GLL/相关 render family 的 mesh、Bezier 与 frame block。源级 `callLayer*` argument packer仍是
portable 对重复 Variant/FuncCall block 的抽取；只有 mesh-point Array builder 的旧“也只是抽取”结论
被新鲜函数边界、16 个 xref 和完整 body共同推翻。

## 3. create、Items 与返回 owner

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| create Array+Items | `0x6C457C` | `0x58E06C` | `0x1001183A8` | `0x116184` |
| offset+point | `0x6C45B0..0x6C45B4` | `0x58E094..0x58E09E` | `0x1001183D4` | `0x1161E0..0x1161E6` |
| append x/y | inline/grow `0x6C45C0..0x6C4640` | `0x58E0A8`, `0x58E0B0` | `0x1001183E0`, `0x1001183EC` | `0x1161FA`, `0x116206` |
| return-copy/local-dtor | `0x6C4668..0x6C4670` | `0x58E0C8..0x58E0CE` | `0x100118410..0x100118430` | `0x116228..0x11622E` |

helper 首先调用项目中已经由四端闭合的 `createTJSArrayWithItems_guess`。返回 pair 的 `value` 是
唯一 owning closure；`items` 只是 `tTJSArrayNI::Items` 的 borrowed pointer。之后：

```text
for p in points:
    x32 = float(offset.x + p.x)
    y32 = float(offset.y + p.y)
    items.emplace_back(Real(x32))
    items.emplace_back(Real(y32))

return CopyRef(localArrayVariant)
destroy localArrayVariant
```

Android 32 位把两次 append 抽进 deque slow/fast helper，iOS arm64 以 NEON `vadd_f32` 同时算 x/y，
其余 ISA 使用标量 add；这些是代码生成差异。四端共同点是 offset 为左操作数、运算先在 f32 完成、
再提升到 double/TJS Real。

这个操作数/量化顺序在普通有限数上常看似可交换，但对 NaN payload、signed zero 选择和与外部先行
translation 的舍入组合并不等价，不能重写成 `double(point) + double(offset)`，也不能先把整个
translated vector持久化再传零 offset。

## 4. 边界与异常生命周期

helper 的 sharp boundary 为：

- empty vector 不进入 loop，因此即使 borrowed Items 未发布也不会在 helper 内解引用；fresh Array
  Variant仍正常返回；
- nonempty vector 若 native-instance status使 `items == nullptr`，第一项 append 自然失败；没有
  friendly null return；
- x append 成功、y append 或后续 point grow 抛出时，partial Items仍由 helper-local Array Variant
  拥有，unwind 会销毁完整 Array及已追加元素；caller result尚未发布；
- 全部 append 完成后才 copy到 caller return slot，随后销毁 local owner；
- caller 把 returned Variant本身直接放进 mesh/bezier FuncCall 参数数组，不再构造第二个
  `(dispatch,dispatch)` closure；
- FuncCall/后续 argument conversion抛出时 caller-local returned Variant按 RAII 释放 Array。

旧端口的 raw dispatch + 手工 Release 在 `PropSetByNum` 或 mesh FuncCall exception 上没有对应 RAII，
同时凭空加入了每坐标一次脚本派发/status boundary；两者均已删除。

## 5. common leaf 的 created gate 与 geometry source

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| leaf resolver | `0x6C270C` | `0x58CABE` | `0x100116B30` | `0x114666` |
| persistent leaf assign | `0x6C271C` | `0x58CACA` | `0x100116B40` | `0x11467A` |
| `createdOrChanged` gate | `0x6C2780` | `0x58CAF6` | `0x100116B80` | `0x1146B0` |
| type-2 local offset | `0x6C2BE8..0x6C2BEC` | `0x58CCBA..0x58CCBE` | `0x100116DC4..0x100116DC8` | `0x11491A..0x114922` |
| type-1 local offset | `0x6C2F78..0x6C2F7C` | `0x58CF9C..0x58CFA0` | `0x100116F30..0x100116F34` | `0x114A54..0x114A5C` |
| affine local Real block | `0x6C2DBC..0x6C2E44` | `0x58CE32..0x58CEB4` | `0x100117130..0x1001171B4` | `0x114C2C..0x114CDA` |

resolver 之前仍会构造 `SeparateLayerPayload_guess`；该 payload 对 type 1/2 分别复制相应 command
vector，这是 resolver input，不是 translated output。resolver return先 copy-assign到 persistent
`leafLayer`，再从该字段取得 retained Object，两个临时 Variant死亡，然后才读
`createdOrChanged`。false 因此可能刷新 persistent leaf owner，但不会发生 descriptor/source/copy 或
任何 local geometry构造。

true 分支在 descriptor/color/source/width/height/neutralColor/setSize 之后才按 meshType 分派：

```text
meshType 0:
    Real p0 = double(corner[0/1]) + (-0.5) - double(clip.left/top)
    Real p1 = double(corner[2/3]) + (-0.5) - double(clip.left/top)
    Real p2 = double(corner[6/7]) + (-0.5) - double(clip.left/top)
    affineCopy(... p0, p1, p2 ...)

meshType 1:
    float2 offset = {-0.5f - clip.left, -0.5f - clip.top}
    points = buildMeshPointTJSArrayVariant(commandBezierPatchPoints, offset)
    bezierPatchCopy(... points ...)

meshType 2:
    float2 offset = {-0.5f - clip.left, -0.5f - clip.top}
    points = buildMeshPointTJSArrayVariant(commandCompositeMeshPoints, offset)
    meshCopy(... points ...)
```

affine 的 corner 先提升 double再做 `+(-0.5)-clip`；mesh/Bezier 则先以 f32计算整个 offset，helper
内再以 f32做 `offset+point`，然后提升 Real。两条数值管线故意不同。

## 6. 源码修正

### `PlayerRenderInternal.*`

- `buildMeshPointTJSArray_guess` 更名为 `buildMeshPointTJSArrayVariant_guess`，返回类型从 raw
  `iTJSDispatch2*` 改为 owning `tTJSVariant`；
- 复用 `createTJSArrayWithItems_guess` 并直接 `Items.emplace_back(Real)`；
- 改为 `xOffset + point.x` / `yOffset + point.y` 的 f32顺序；
- 强制 local-owner 到 return-owner 的 const-lvalue copy boundary；
- mesh/bezier call packer直接借用 caller-local points Variant作为 argv 元素，不再复制新的 closure；
- 全部 caller 删除手工 `Release()`，异常由 returned Variant RAII 覆盖。

### `PlayerRenderExecute.cpp` / `RuntimeSupport.h`

- 删除 leaf resolver 前的无条件 `localCorners/localMeshPoints` 构建；
- 从 Web `PreparedRenderItem` sidecar 删除这两个 native 不存在的持久字段；
- type 0 在 affine 参数现场按 double公式构造三点；
- type 1 改读 `commandBezierPatchPoints`，type 2 保持 `commandCompositeMeshPoints`；
- translated mesh通过原 vector + call-local offset进入共享 helper；
- 删除重复、恒真比较自身的 `renderItem.localCorners` diagnostic projection；clip diagnostic仍隔离保留。

### 其他 shared renderer callers

canvas、accurate SLA、private/direct、frame等所有 active caller统一接收 owning Variant，保持各自已恢复
的 vector选择、offset、division与 receiver/objthis，不把 common-leaf 的 vector身份泛化到其他路径。

## 7. test 与 Wasmtime differential glue

Catch2 TU 新增 helper oracle：

- 输入两个 `MeshPoint` 与 offset，确认返回值是拥有 native `tTJSArrayNI` 的 Object Variant；
- Items恰为四个 `tvtReal`：`1.75, -2.75, -3.5, 7.75`；
- empty input仍返回 fresh empty owning Array。

Wasmtime JSON 的 `localCorners` 保持为 diagnostic-time projection，不再读取不存在的 persistent field。
同时清理了 V158 后遗留的三个过时字段引用：`leafBuilt/composedBuilt` 从对应 Variant tag投影，
`executedDirect` 输出 null，因为 native 没有可在事后 item snapshot中读取的 route marker。由此
`krkr2_wasmtime_guest` 专用目标重新可编译链接。

## 8. IDB 写回与过时注释纠正

四份 canonical recovery IDB 各完成：

- helper rename为 `buildMeshPointTJSArrayVariant_guess`；
- 9 条 comment：common function correction、created gate、type1/type2 source+offset、affine local Real、
  helper identity/create/add/return；
- 4 个 bookmark：helper、两个 common mesh call与 affine block；
- save、health probe、close。

总计 36 条 comment、16 个 bookmark、4 个 rename；没有新 type。最终 IDA session audit为 0。
旧 common-builder function comment 中“`buildMeshPointTJSArray_guess` 只是重复内联 block”的句子已
逐端追加 V238 correction；`callLayer*` 抽取结论仍保留。

## 9. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless Emscripten syntax compilation：通过；
- Web Debug 最终增量完整构建：11 steps，通过；
- Wasmtime Headless Debug 完整构建：65 steps，通过；
- `krkr2_wasmtime_guest` 专用目标：2 steps，编译、链接并转换 exnref成功；
- Web/Wasmtime 两树再次构建均为 `ninja: no work to do`；
- Node module construction通过，imports/exports保持 Web `539/69`、Wasmtime `538/69`；
- CTest两树按当前配置均为 `No tests were found`；
- Web Wasm定向反汇编确认 helper 为 `f32.add -> f64.promote -> native deque append`，并有
  catch-all local Array owner cleanup；
- `git diff --check` 为零，仅有仓库既有 LF→CRLF warning。

| product | size | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---|
| Web `out/web/debug/index.wasm` | 85,654,321 B | `0x1A40E43` | `0x5A3E40` | `0x3185DF0` | `AFC93F58C7EA8D785AB2F610D044199B344F5BDC79BEEAC76958C211A1013F48` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,001,462 B | `0x19E8DF1` | `0x5A1090` | `0x3141C86` | `C11D47B6A647FB3B1BD5621C71ABA5689700E648EC645020CE7172B5B2B155E8` |

相对 V237，两端 module 均缩 3,003 byte：CODE `-0xB36`、DATA `-0xA0`，较长语义 helper 名使
name section `+0x1B`；TYPE/IMPORT/FUNCTION/TABLE/TAG/GLOBAL/EXPORT/START/ELEM均不变。

## 10. 下一边界

V239 follow-up 已闭合同一 `createdOrChanged=true` leaf prefix的 descriptor/color/source/size owner，
并修正 pre-callback clip snapshot与 post-callback live geometry混用；详见
`analysis/motionplayer_leaf_clip_snapshot_descriptor_source_size_prefix_four_binary_2026-08-18.md`。下一步进入
aux group的 composedLayer Void gate、Layer factory与 child mask composition，避免把 leaf与group两套
owner树混成一个便利 helper。
