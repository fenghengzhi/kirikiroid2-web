# MotionPlayer D3DAdaptor captureCanvas 提交与异常边界四参考恢复（2026-08-15）

## 结论

四份参考二进制的 `D3DAdaptor::captureCanvas` 都是同一个两分支状态机，但它比此前本地实现
更严格：

- `FromVariant` 对普通类型/错误实例仍走核心 Layer 转换错误；若 Variant 是持有 null closure
  的 Object，转换 helper 返回 null，`captureCanvas` 不补异常而在后续 Layer 调用自然解引用；
- software 分支严格按 `target size -> Layer resize -> source pointer -> destination pointer ->
  source pitch -> destination pitch -> copy` 取值，本地不能用后置 helper 改写调用/异常顺序；
- equal-pitch copy 先做 32 位乘法，再把有符号结果转成 `size_t`；unequal-pitch 只在 signed
  `height >= 1` 时逐行复制，width/pitch 同样按 signed int 进入行宽与指针步进；
- GPU 分支对 Layer 当前 texture、adaptor target 和新建 texture 都没有 null guard；
- 可复用 candidate 在 `AssignTexture` 之前先取得一份 raw `AddRef`，没有 RAII rollback；
- `AssignTexture(oldTarget)` 成功后无条件 `oldTarget->Release()`，再把 candidate/null 发布到
  adaptor；没有复用 candidate 时先发布 null，再调用 manager 创建 replacement；
- 创建抛异常或返回 null 都让 Layer 已持有旧 target，而 adaptor 保持 null。

因此正常路径是一次 texture ownership exchange，而异常路径是有明确 partial commit 的非事务
流程。为“安全”添加 candidate/target null guard、candidate RAII 或创建失败回滚都会改变原版。

## 四端函数映射

| 目标 | `captureCanvas` | software 判定 | GPU `AssignTexture` | old target `Release` | replacement/create 发布 |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6AAD0C` | `0x6AAD34` | `0x6AAE70` | `0x6AAE80` | `0x6AAE84` / `0x6AAEB0` |
| Android armv7 | `0x57CF94` | `0x57CFAE` | `0x57D064` | `0x57D06E` | `0x57D072` / `0x57D092` |
| iOS arm64 | `0x100103DBC` | `0x100103DE8` | `0x100103F2C` | `0x100103F3C` | `0x100103F40` / `0x100103F6C` |
| iOS armv7 | `0x10116E` | `0x101188` | `0x101240` | `0x10124A` | `0x10124E` / `0x101272` |

函数自身没有 owning local class object，四端正常 return 和异常 unwind 都不含 candidate/target
cleanup landing pad。Variant 参数 owner 的构造/析构属于 NCB typed wrapper，不会替 raw texture
pointer 做引用回滚。

## Variant-to-Layer 的 null Object 边界

四端 `TJSNI_Layer_FromVariant_guess` 都先要求/转换到 Object variant，再把 closure dispatch 交给
Layer native-instance query：

```text
dispatch = variant.AsObjectNoAddRef()
nativeLayer = null
if dispatch != null:
    status = dispatch.NativeInstanceSupport(GETINSTANCE, LayerClassID,
                                            &nativeLayer)
    if status < 0:
        throw TVPSpecifyLayer
return nativeLayer
```

入口分别为 Android arm64 `0xA795A0`、Android armv7 `0x79AFC4`、iOS arm64
`0x10035FF0C`、iOS armv7 `0x363664`；后三端的 FromObject 主体分别在 `0x79AFF0`、
`0x10035FF40`、`0x36368C`，Android arm64 则内联在同一函数。

所以普通非 Object conversion、非 Layer native instance 仍抛引擎错误；只有“类型已经是 Object，
但 closure dispatch 为 null”返回 null。四端 capture 紧接着就继续执行，没有本地旧实现的
`if (!layer) throw TVPSpecifyLayer`。software 路径先读取 target 尺寸再调用 null Layer 的
`SetImageSize`；GPU 路径第一项 Layer 操作是 `IndependMainImage`。

## software 分支的严格调用顺序

四端共同源级顺序为：

```text
width  = signed32(target.width)
height = signed32(target.height)
layer.SetImageSize(width, height)
src      = target.GetScanLineForRead(0)
dst      = layer.GetMainImagePixelBufferForWrite()
srcPitch = signed32(target.GetPitch())
dstPitch = signed32(layer.GetMainImagePixelBufferPitch())

if srcPitch == dstPitch:
    byteCount32 = signed32(srcPitch * height) // low 32 bits
    memcpy(dst, src, sign_extend_to_size_t(byteCount32))
else if height >= 1:                         // signed comparison
    rowBytes = size_t(signed(width) * 4)
    do:
        memcpy(dst, src, rowBytes)
        dst += signed(dstPitch)
        src += signed(srcPitch)
        height -= 1
    while height != 0
```

关键指令证据：

| 目标 | equal-pitch 乘法 | 64 位 `size_t` 扩展 | signed height gate | row/pitch 扩展 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6AAD98: MUL W8,W24,W21` | `0x6AADB0: SXTW X2,W8` | `0x6AADEC: CMP W21,#1` + `B.LT` | `0x6AADF4..0x6AAE00: SXTW` |
| Android armv7 | `0x57CFEE: MUL R2,R8,R6` | 32 位原值 | `0x57D024: CMP R6,#1` + `BLT` | 32 位 `LSL`/pointer add |
| iOS arm64 | `0x100103E48: MUL W8,W25,W21` | `0x100103E4C: SXTW X2,W8` | `0x100103EA8: CMP W21,#1` + `B.LT` | `0x100103EB0..0x100103EBC: SXTW` |
| iOS armv7 | `0x1011CA: MUL R2,R8,R6` | 32 位原值 | `0x101200: CMP R6,#1` + `BLT` | 32 位 `LSL`/pointer add |

这排除了本地旧写法的两个边界：`size_t(srcPitch) * unsignedHeight` 会在 64 位提前扩宽乘法；
`for (tjs_uint y = 0; y < height; ++y)` 会把负 height 解释为巨大正循环域。native 对 unequal
pitch 的负/零 height 什么也不复制；equal pitch 仍会把 32 位乘积按有符号值扩展为潜在巨大
`size_t`。两条路径在无效尺寸上故意不统一。

source/destination 指针均无 null 检查。相等 pitch 即使 height 为负也直接调用 `memcpy`；不同
pitch 只由 signed height gate 控制，不验证 `rowBytes <= pitch`、宽度正数或目标缓冲区容量。

## GPU candidate 选择与 ownership exchange

共同伪代码：

```text
layer.IndependMainImage()
candidate = layer.MainImage.texture

replacement = null
if !candidate.IsStatic():
    if candidate.width == adaptor.target.width
       && candidate.height == adaptor.target.height:
        candidate.AddRef()       // direct intrusive count increment
        replacement = candidate

layer.AssignTexture(adaptor.target)
adaptor.target.Release()         // unconditional virtual call
adaptor.target = replacement     // candidate or null published here

if replacement == null:
    adaptor.target = CurrentRenderManager.CreateTexture2D(
        null, 0, adaptor.width, adaptor.height, RGBA, 0)
```

| 目标 | unguarded `candidate.IsStatic` | accepted `AddRef` | no-reuse null selection |
|---|---:|---:|---:|
| Android arm64 | `0x6AADD8` | `0x6AAE50..0x6AAE5C` | `0x6AAE64..0x6AAE68` |
| Android armv7 | `0x57D016` | `0x57D054..0x57D05A` | `0x57D05E..0x57D060` |
| iOS arm64 | `0x100103E8C` | `0x100103F0C..0x100103F14` | `0x100103F20..0x100103F24` |
| iOS armv7 | `0x1011F2` | `0x101230..0x101236` | `0x10123A..0x10123C` |

`IsStatic` 为 true 时不会读取 target 尺寸；false 时只比较实际 width/height，不比较 format、
pitch、renderer identity 或 refcount。`AddRef` 是 texture base 的非虚内联计数递增。它必须发生
在 `AssignTexture` 之前，因为 Layer 的 bitmap assignment 会释放旧 candidate；额外引用把它
保留下来，供 adaptor 在交换尾部接管。

正常 accepted 路径的引用迁移为：

```text
Layer owns candidate (old layer texture)
Adaptor owns oldTarget
candidate.AddRef()                    // temporary future adaptor owner
Layer.AssignTexture(oldTarget)        // releases Layer's candidate owner;
                                      // Layer acquires oldTarget owner
Adaptor oldTarget.Release()           // drops adaptor's former owner
Adaptor.target = candidate            // publishes the extra candidate owner
```

candidate 不可复用时没有额外引用；`AssignTexture` 释放 Layer 原纹理并接管 oldTarget，adaptor
随后释放自己的 oldTarget owner、先发布 null，再取得全新的 creation reference。

## 异常与 partial commit 矩阵

该函数没有 transaction/RAII rollback：

| 失败点 | Layer 状态 | adaptor target | 额外 candidate ref |
|---|---|---|---|
| `IndependMainImage` / candidate query 前 | 由被调函数决定，通常尚未交换 | oldTarget | 无 |
| accepted `AddRef` 后、`AssignTexture` 抛出 | 可能部分独立/部分更新 | 仍为 oldTarget | 泄漏；无 cleanup |
| `AssignTexture` 成功、oldTarget `Release` 前后异常 | 已持有 oldTarget | store replacement 尚未发生，仍指 oldTarget | accepted 时仍悬挂为额外 raw ref |
| 发布 `replacement=null` 后 manager lookup/create 抛出 | 已持有 oldTarget | null | 无 |
| create 返回 null | 已持有 oldTarget | null | 无 |
| create 成功 | 已持有 oldTarget | 新 texture creation ref | 无 |

实际 engine texture `Release` 通常不抛，但虚调用边界没有 `noexcept`/catch，恢复源码不能借此
调整 store 顺序。尤其不能先创建 replacement 再交换，也不能在 create 失败时把 Layer 中的
oldTarget 偷回 adaptor；那会消除参考实现可观察的 null state。

## 本地源码纠正

`D3DAdaptor.cpp` 已按四端共同数据流调整：

- 删除 `FromVariant` 后的 friendly null-Layer exception；
- software production path 不再经后置 copy helper，恢复六项 getter/call 的原始顺序；
- 恢复 signed height gate、32 位 equal-pitch 乘法和 signed width/pitch 步进；
- 删除 candidate null guard；
- 不再调用带 null guard 的 `releaseTargetTexture()`，改为 unguarded old target `Release` 后再
  发布 replacement；
- 保留 create 结果无 guard、logical size bit-pattern 直传和交换后的 null partial commit。

`copyTargetTextureRows_guess` 是本地测试/复用抽取，不是四端独立 native function；其算术也
同步成 signed-height/32-bit-product 语义，避免它继续传播旧边界结论。生产 `captureCanvas`
仍保留原生内联顺序，不调用该 helper。

四份 recovery IDB 已在 null conversion、software 乘法/gate、candidate 直接解引用、保护性
AddRef、AssignTexture、old-target Release、replacement/null publication 和 create result store
处补注释，并为三个关键提交点加书签后保存。`Web Debug Build` 通过；完整 motionplayer test
TU 的 Emscripten `-fsyntax-only` 通过，仅有仓库既有 `_tss` 弃用警告；`git diff --check`
通过，三份相关未跟踪分析文档无行尾空白。
