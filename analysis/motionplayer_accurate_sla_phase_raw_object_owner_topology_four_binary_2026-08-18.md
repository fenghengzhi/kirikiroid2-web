# motionplayer accurate SLA：四阶段临时 Variant、raw Object retain 与 item-tail owner 拓扑（四参考二进制）

日期：2026-08-18  
阶段：V242

## 1. 结论

四个参考二进制的 accurate `SeparateLayerAdaptor` item loop 对 base、optional masked、debug 与 final
publication Layer 使用同一种两段式 owner acquisition：

```text
temporaryVariant = CopyRef(persistentLayerVariant)
rawObject = temporaryVariant.AsObject()   // AddRef Object only
destroy(temporaryVariant)                 // Release Object, then ObjThis

... rawObject crosses this phase's callbacks ...

rawObject->Release()
```

这不是“用一个 owning Variant copy跨越整个 phase，再以 `AsObjectNoAddRef()`借用 Object”。后者会让
temporary closure 的 ObjThis retain一直活到 phase末尾，并把 Object/ObjThis Release重入点推迟到全部
callback之后。参考实现则在任何 phase callback之前就销毁 temporary closure，只保留一个 Object-only
raw retain。

四阶段的边界为：

1. payload-aware resolver返回的 base owning Variant保留到完整 item尾；由它的 temporary CopyRef取得
   base raw Object owner，后者跨 source resolve、copy-family、mask/debug/publication全部前半流程；
2. optional masked Layer另取一个独立 raw Object owner，仅跨 `assignImages`、`setSize`与ancestor masks，
   在 debug gate前释放；
3. debug frame另取独立 raw Object owner，在 geometry frame phase尾释放；
4. publication再取独立 raw Object owner；temporary Variant在 `setPos/type/visible/opacity`之前就析构；
5. normal item tail严格执行 publication raw Release → final Layer Variant dtor → base raw Release → base
   resolver Variant dtor；异常 landing按构造进度有条件展开同一 owner stack。

当前 portable source 的四处 long-lived Variant copies因此是可观察偏差。V242改为同一显式 primitive与
raw-dispatch RAII guard，并用 distinct Object/ObjThis probe锁定 AddRef/Release次序。

## 2. 四端地址映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| accurate renderer | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |
| payload-aware base resolver | `0x6C7490` | `0x590912` | `0x10011AD70` | `0x119284` |
| base temp Variant CopyRef | `0x6C749C` | `0x590926` | `0x10011AD7C` | `0x119296` |
| base `AsObject` AddRef | `0x6C74BC` | `0x590932` | `0x10011AD8C` | `0x1192AA` |
| base temp Variant dtor | `0x6C74DC` | `0x59093C` | `0x10011AD98` | `0x1192B6` |
| masked temp CopyRef | `0x6C7F64` | `0x5910D8` | `0x10011B654` | `0x119B3A` |
| masked `AsObject` AddRef | `0x6C7F88` | `0x5910E0` | `0x10011B65C` | `0x119B44` |
| masked temp dtor | `0x6C7FAC` | `0x5910E8` | `0x10011B668` | `0x119B4C` |
| debug temp CopyRef | `0x6C8248` | `0x5913B0` | `0x10011B8F8` | `0x119E2E` |
| debug `AsObject` AddRef | `0x6C8270` | `0x5913BA` | `0x10011B900` | `0x119E38` |
| debug temp dtor | `0x6C8298` | `0x5913C2` | `0x10011B90C` | `0x119E40` |
| publication temp CopyRef | `0x6C86B4` | `0x591742` | `0x10011BD00` | `0x11A290` |
| publication `AsObject` AddRef | `0x6C86E8` | `0x591752` | `0x10011BD1C` | `0x11A2A0` |
| publication temp dtor | `0x6C8718` | `0x59175A` | `0x10011BD28` | `0x11A2A8` |
| publication raw Release | `0x6C8888..0x6C8894` | `0x591828..0x591836` | `0x10011BE18..0x10011BE28` | `0x11A3BC..0x11A3CA` |
| final Variant dtor | `0x6C8898` | `0x591838` | `0x10011BE2C` | `0x11A3CC` |
| base raw Release | `0x6C88A0..0x6C88B0` | `0x59183E..0x591850` | `0x10011BE38..0x10011BE48` | `0x11A3D8..0x11A3E8` |
| base Variant dtor | `0x6C88B4` | `0x591852` | `0x10011BE4C` | `0x11A3EA` |

地址只属于 `analysis/` 证据映射；compiled source只保留跨架构语义名。

## 3. temporary Variant 与 raw Object 的引用事件

对于 `Variant(Object=A, ObjThis=B)` 且 `A != B`，每次 phase acquisition 的共同事件是：

```text
temporary CopyRef:
    A.AddRef
    B.AddRef

temporary.AsObject:
    A.AddRef

temporary destructor:
    A.Release
    B.Release

phase callbacks:
    only the extra A retain remains from this acquisition

phase end:
    A.Release
```

Android arm64把 Object-tag fast path内联为tag compare、null test、vtable AddRef；non-Object路径调用
conversion helper。Android armv7与两份iOS使用已识别的 `tTJSVariant_AsObject*_guess` helper。四端都在
AsObject返回后立即调用 temporary Variant dtor，再进入下一业务指令。

这里的 null边界也不同于 `AsObjectNoAddRef`：Object-tag/null Object时 `AsObject()`返回 null raw owner，
后续 native phase在首次 raw dispatch使用处自然失败；non-Object Variant则走标准 conversion exception。
portable helper直接调用同一 `tTJSVariant::AsObject()`，不增加恢复分支。

## 4. base owner 的长生命周期

payload-aware resolver的返回 Variant本身是一个 item-scope owner。四端紧接着：

1. CopyRef到 call-local temporary；
2. 从 temporary执行 `AsObject()`取得 raw base Object retain；
3. 立即析构 temporary；
4. 以 raw Object执行 source-side `setSize`/copy、base visible写等；
5. 即使 final Layer later改成masked Layer，base raw owner仍保持到 item normal tail；
6. final Variant先析构，base raw retain再Release，最后base resolver Variant析构。

这意味着base phase不是一个额外long-lived Variant closure。若 ObjThis的最后一份非persistent引用来自
temporary，ObjThis Release reentry发生在 source resolve之前；旧portable实现会错误推迟到item尾。

## 5. masked、debug 与 publication 独立 owners

### 5.1 masked

`resolveLayerOrdinal_guess(layerId2)`更新 final Layer Variant后，caller用temporary CopyRef取得masked raw
Object。temporary在 `assignImages(base Variant)`之前即销毁。raw owner跨：

- `assignImages`；
- `setSize`；
- 完整 ancestor walk；
- 每个 alpha-mask/fillRect callback。

ancestor loop结束后先Release masked raw owner，再进入outline/meshline debug gate。alpha-mask helper自身的
by-value destination仍从persistent final Variant构造；删除long-lived masked Variant owner不会减少这个
真实调用参数CopyRef。

### 5.2 debug

debug gate通过时，caller再次从 final Variant构造temporary、AsObject retain、销毁temporary。raw debug
owner跨第二个geometry switch与所有frame calls，随后Release；unknown geometry也走相同release后继。

### 5.3 publication

publication不复用debug raw owner。它第三次独立取得final raw Object，temporary在任何publication callback
前析构，然后按既有顺序执行：

```text
setPos(Real left, Real top)
type = Integer layerType
visible = Integer 1
opacity = Integer raw item opacity
```

普通HRESULT继续忽略；异常按已经构造的raw/Variant owners展开，不执行后续properties或normal item-tail
cleanup。

## 6. normal item-tail 与 exception owner stack

normal publication结束后的共同顺序不是任意RAII结果，而是四端相同：

```text
Release(publicationRawObject)
destroy(finalLayerVariant)
Release(baseRawObject)
destroy(baseResolverVariant)
advance main pointer iterator
```

masked/debug raw owner已分别在更早phase结束。每个 temporary acquisition若在CopyRef或AsObject期间抛出，
只展开当时已构造的temporary；成功取得raw owner后，后续callback异常还会Release该raw owner，并按外层
构造进度继续析构persistent final/base owners。portable的move-disabled raw guard与C++ scopes复现这条
conditional unwind，而没有添加catch或rollback。

## 7. 源码修正

`PlayerRenderInternal.{h,cpp}` 新增内部 primitive：

```cpp
iTJSDispatch2 *retainObjectFromVariantCopy_guess(
    const tTJSVariant &value) {
    tTJSVariant valueCopy(value);
    return valueCopy.AsObject();
}
```

return expression完成Object AddRef后，call-local `valueCopy`在函数返回前析构，恰好形成参考两段式owner。

`PlayerRenderTargets.cpp`：

- 添加不可复制的raw dispatch Release guard；
- base、masked、debug、publication四阶段都改用上述primitive；
- phase callbacks只持raw Object owner，不再持有额外closure Variant；
- ancestor alpha-mask destination直接由persistent `finalLayerVariant`构造by-value参数，保留真实参数
  CopyRef而不再叠加non-native long-lived masked closure owner；
- declaration/scope顺序保证normal item tail为final Variant → base raw → base Variant，publication raw则在
  内层block先释放。

## 8. 测试

新增窄 probe以两个独立 `tTJSDispatch`作为Object与ObjThis，记录所有 AddRef/Release：

- primitive内部严格观察到
  `Object.AddRef, ObjThis.AddRef, Object.AddRef, Object.Release, ObjThis.Release`；
- 返回的raw pointer恒为Object而非ObjThis；
- raw phase owner手动Release只产生一次 `Object.Release`；
- 最后清persistent source Variant才产生 `Object.Release, ObjThis.Release`。

该probe直接覆盖旧实现无法满足的ObjThis提前释放边界，不依赖完整Layer工厂或diagnostic sidecar。

## 9. IDB 写回与 iOS armv7 安全保存

Android arm64、Android armv7、iOS arm64各写回17条comment与4个bookmark；iOS armv7写回13条comment、
4个bookmark，并恢复重建库尚缺的 `Player_renderAccurateSeparateLayerAdaptor_guess` semantic rename。总计
64条comment、16个bookmark、1个rename，覆盖四个temporary acquisition与normal item-tail release顺序。

iOS armv7继续使用different-path compressed-save流程：另存packed copy → 独立`idat`重开退出码0 →
MCP `save=false`关闭 → 把pre-V242 canonical及`.id0/.id1/.nam`逐文件移动到
`out/idb-recovery/v242-ios-armv7/pre-v242-canonical/` → 安装已验证copy → MCP重开读回function name与13条
V242 comments → `save=false`关闭。没有递归删除或不可恢复覆盖。

V242 canonical为375,773,392 bytes，SHA-256
`82EB884342C4131C0E9A7EC4A0998CFC324BD62827D1B86729E8D20621023449`；pre-V242 canonical SHA-256为
`D6C447F6C770B0E2A230FDFD4BCCC594644AC5FCBD0BECB0772BD7ED74D4BD93`。final IDA supervisor audit为
0 open sessions。

## 10. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless syntax compilation：通过；
- Web Debug：10个objects/archive steps加主wasm链接通过；
- Wasmtime Headless Debug：19个objects/archive steps加主wasm链接通过；
- `krkr2_wasmtime_guest`重新链接并完成exnref转换；
- Node `WebAssembly.Module` construction：通过；imports/exports仍为Web `539/69`、Wasmtime `538/69`；
- 两棵CTest当前配置明确`No tests were found`；
- Web/Wasmtime/guest final no-work：通过；
- scoped `git diff --check`：无whitespace error，仅既有LF/CRLF提示；
- final IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Web `out/web/debug/index.wasm` | 85,655,316 | `0x1BD31` | `0x1A41097` | `0x5A3E40` | `0x3185F7B` | `1D9F3BE26B27C274837B86A3850C8F268E2825DDA322495425F66EE95AFAB074` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,002,457 | `0x1BA50` | `0x19E9045` | `0x5A1090` | `0x3141E11` | `1AF7BAF64B5AD8F2C2E5870AF9317FF9844073B88B986B429ECB28C05B75F5C8` |

相对V241，两端module各增675 bytes、CODE各增`0x17E`、name各增`0x122`、FUNCTION payload各增3 bytes；
GLOBAL/DATA/imports/exports不变。增长来自一个显式owner-acquisition helper、raw guard unwind与符号名，不是
persistent item/SLA layout变化。

guest SHA-256为
`B2ACDF6188122DAD6B56257EE2363DC3E81EA7BBC6726D69CF480D0DCDDCFF69`。

## 11. 下一边界

V243 转入ordinary Canvas submitter的per-item owner chain：descriptor accessor → color accessor → source
Variant → source accessor → optional ResourceManager/bufLayer/buffer owners。需要四端锁定每个CopyRef/
AsObject时点、direct/buffered分支共享范围、normal reverse destruction与callback exception prefix，并清理
仍把closure Variant与raw Object retain混为一谈的旧注释或实现。
