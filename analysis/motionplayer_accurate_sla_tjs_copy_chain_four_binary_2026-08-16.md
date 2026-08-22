# Accurate-SLA TJS setSize/copy-family 调用链四端复核（2026-08-16）

## 1. 结论

accurate `SeparateLayerAdaptor` renderer 对 resolver 返回的 source Layer 保持 TJS object
closure，不把它转换成 `tTJSNI_BaseLayer`、不取得 main image，也不直接调用 native
`AffineCopy/MeshCopy/BezierPatchCopy`。四端共同调用链是：

```text
resolve source Variant
read source.width / source.height by TJS PropGet
itemLayer.setSize(Real clipWidth, Real clipHeight) by TJS FuncCall
itemLayer.affineCopy / meshCopy / bezierPatchCopy(source Variant, ...) by TJS FuncCall
```

本地 accurate 路径已恢复该 source-side 调用链。本轮当时未覆盖的 target item Layer
publication 后续已由
`motionplayer_accurate_sla_target_publication_four_binary_2026-08-16.md` 独立四端闭合。

## 2. UTF-16 member 搜索与四端 xref

普通 IDA string search 对 `affineCopy`、`meshCopy`、`bezierPatchCopy` 在四份 IDB 中都返回
零；按 `ida-search-string` 的 UTF-16LE byte pattern 搜索后全部命中，再以 data xref 归属
完整 accurate renderer：

| 目标 | `affineCopy` xref | `meshCopy` xref | `bezierPatchCopy` xref |
|---|---:|---:|---:|
| Android arm64 | `0x6C7C34` | `0x6C7A2C` | `0x6C7DD4` |
| Android armv7 | `0x590E7A` | `0x590CA2` | `0x590FC8` |
| iOS arm64 | `0x10011B4F0` | `0x10011B158` | `0x10011B2E8` |
| iOS armv7 | `0x1199F0` | `0x11969C` | `0x1197EE` |

每个 xref 的 enclosing function 都是对应端的
`Player_renderAccurateSeparateLayerAdaptor_guess`，不是同名 MotionLayer method registrar、
normal canvas renderer 或 command builder。

四端 `setSize` dispatch call：

| 目标 | member literal setup | indirect FuncCall |
|---|---:|---:|
| Android arm64 | `0x6C78F4..0x6C7910` | `0x6C791C` |
| Android armv7 | `0x590B56..0x590B62` | `0x590B6A` |
| iOS arm64 | `0x10011B018..0x10011B020` | `0x10011B030` |
| iOS armv7 | `0x119570..0x119582` | `0x119586` |

四端都是 argc=2、两个 Real Variant，并在 indirect call 后按逆构造顺序销毁参数 Variant；
caller 不以 HRESULT 决定是否进入 geometry branch。

## 3. 没有 source native-layer 下钻

四端 `TJSNI_Layer_FromVariant_guess` 的 code-xref 总数都恰为 15。完整 xref 列表覆盖
ObjSource、MotionLayer mesh/bezier renderer、tint、D3D capture、alpha mask、recursive draw、
private-target ensure、PrivateMotionGLL source resolve 与 D3D source getter等，但四份列表中都
没有 accurate renderer。

完整 accurate renderer 的 fresh disassembly 同样没有对该 helper的 direct call。与此相对，
三种 copy-family 的 member literal 与 indirect FuncCall 都直接位于 accurate function 内。
因此本地旧流程：

```text
sourceObject -> resolveNativeLayer -> GetMainImage
itemLayer native AffineCopy/MeshCopy/BezierPatchCopy
```

改变了 receiver、objthis、argument Variant 生命周期、script override 可见性、HRESULT/异常
边界和 source owner 链，不能作为 Web 等价实现保留。

## 4. geometry 参数与 container handoff

- affine：继续使用 prepared item 的三个 corner point，加 clip-local `-0.5-left/top`
  offset 后构造 argc=14 `affineCopy` 参数；source 仍是 owning local Variant copy；
- Bezier：把 `item.meshPoints` 逐个发布进 fresh TJS Array，cell division 使用上一纵切已
  恢复的 descriptor width/height；
- composite mesh：把 `item.commandCompositeMeshPoints` 发布进 fresh TJS Array，并传
  prepared `meshDivX/meshDivY`；
- 两个 mesh branch 都在 indirect call 返回后 Release Array dispatch；未知 geometry type
  不调用 copy-family。

四端 geometry switch 只按 type `0/1/2` 分支；没有本地旧实现的 vector-nonempty 或
`meshDivX/Y >= 1` admission guard。即使 Array 为空或 division 非正，也由被调 TJS method
处理，caller 仍保留原参数构造与销毁边界。

## 5. 源码落地

`cpp/plugins/motionplayer/PlayerRenderTargets.cpp`：

- 删除 source `resolveNativeLayer`/`GetMainImage`；
- direct native `AffineCopy/MeshCopy/BezierPatchCopy` 改为共享 TJS dispatch helpers；
- direct/checked `trySetAccurateSlaLayerSize` 改为忽略 HRESULT 的
  `callLayerSetSizeReal_guess`；
- 删除 mesh vector/positive-division admission guards；
- target post-copy publication 当时保持不动；后续独立纵切已恢复其 TJS dispatch 与
  owning-Variant 生命周期，详见上述 target-publication 文档。

`cpp/plugins/motionplayer/PlayerRenderInternal.{h,cpp}`：删除现在零 caller 的
`buildMeshPoints` native-point-vector adapter。准确路径使用原版 TJS Array handoff，shared
D3D 路径有自己不同的 double-point backend，不再需要这层通用转换。

绝对地址只记录在本文与 recovery IDB，不进入编译源码注释。

## 6. IDB 与验证

四个 recovery IDB 的完整 accurate renderer 均写入同一条 function comment，说明 source
保持 TJS Variant/dispatch、`setSize` 与三种 copy-family 均为 indirect TJS call、function
中不存在 `TJSNI_Layer_FromVariant_guess`/main-image downcast，并记录 geometry 1/2 的 fresh
TJS Array 与 caller 不增加 admission guard。随后逐端执行 force recompile，fresh
decompile 均回读到该注释，并原位保存：

- Android arm64：`0x6C7088`；
- Android armv7：`0x590468`；
- iOS arm64：`0x10011A9E8`；
- iOS armv7：`0x118D70`。

2026-08-16 本地验证：

- ordinary Emscripten 单元翻译单元语法检查：通过；
- `KRKR2_WASMTIME_HEADLESS=1` 语法检查：通过；
- `Web Debug Build` 的 `motionplayer` target：10/10，通过；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：10/10，通过；
- 完整 `Web Debug Build` 最终链接：1/1，通过。

输出仅包含项目既有的 `_tss` literal-operator、pthread + memory-growth、JSPI experimental
及 JS library dependency warning；本轮没有新增编译或链接错误。scoped residual scan 还确认：
`trySetAccurateSlaLayerSize`、`buildMeshPoints`、accurate source-side native downcast/main-image
copy 均为零命中；新 TJS helper 调用只落在预期 accurate branch。
