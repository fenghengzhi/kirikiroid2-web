# PlayerInternal 孤立 `tryGetLayerObject` 二次 fallback 四端清理（2026-08-16）

## 结论

`PlayerInternal.h::tryGetLayerObject(value, layer)` 是零 caller 的旧宽容 Layer resolver。
它不仅冗余，还保留了一条与四个当前参考二进制相反的 fallback：第一次 GETINSTANCE 失败后，
再读取 closure 并尝试另一个 `closure.Object` dispatch。

四端 fresh `tTJSNI_Layer::FromVariant` / FromObject 证明真实边界为：

1. 强制 Variant 转换为 Object；
2. 只取 Variant 首槽保存的 Object dispatch；
3. 初始化一个 null native output；
4. 对该 dispatch 只调用一次 `NativeInstanceSupport(GETINSTANCE, LayerClassID, &out)`；
5. query status 失败时抛 `TVPSpecifyLayer`；
6. 不读取 ObjThis/closure 第二槽，也不重试另一个 dispatch。

本地生产路径已经通过 `tTJSNI_Layer::FromVariant`、`resolveNativeLayer` 或与四端一致的
`tryResolveLayerDispatch` 表达各自 strict/nullable 边界。孤立 helper 没有保留价值，本轮将其
完整删除。

## 四端映射

| 目标 | `TJSNI_Layer_FromVariant_guess` | FromObject 实现 |
|---|---:|---:|
| Android arm64 | `0xA7959C` | 内联于同一函数 |
| Android armv7 | `0x79AFCE` | `0x79AFF0` |
| iOS arm64 | `0x10035FF10` | `0x10035FF40` |
| iOS armv7 | `0x36366C` | `0x36368C` |

Android arm64 的编译器把两层折叠到一个函数；其余三端保留相邻的小型 FromObject helper。
这只是内联差异，四端数据流和异常边界一致。

## fresh 四端数据流

共同源级形态为：

```text
FromVariant(variant):
    if variant.type != Object:
        variant.convertTo(Object)          // conversion failure propagates
    return FromObject(variant.objectSlot0)

FromObject(dispatch):
    native = null
    if dispatch != null:
        hr = dispatch.NativeInstanceSupport(
            GETINSTANCE, LayerClassID, &native)
        if hr < 0:
            throw TVPSpecifyLayer
    return native
```

`Object` type + null 首槽会返回 null；成功 status + null output 也返回 null。是否随后自然空指针
解引用、显式判空或把 null 作为 miss，取决于 caller 自己。核心 helper 不通过另一个 closure
slot 修复这些状态。

## 被删除 helper 的错误拓扑

旧本地实现先做一次 nullable query，然后执行：

```text
closure = value.AsObjectClosureNoAddRef()
if closure.Object != firstDispatch:
    retry GETINSTANCE on closure.Object
```

这一段存在三重问题：

- `tryGetLayerObject` 在 `cpp/` 与 `tests/` 中零 caller；
- 它把 query 失败降级为 false，而 strict native helper 会抛标准 Layer 错误；
- 它暗示 Object/ObjThis 或首槽/closure Object 之间存在第二条候选链，四端机器码没有读取该
  slot，也没有第二次虚调用。

因此只修注释或把它留作“将来可能用到”的工具函数都会继续传播过时的 libkrkr2-era
推断；删除更接近当前四参考背后的共同源结构。

## 源码与恢复库修正

- `cpp/plugins/motionplayer/PlayerInternal.h`
  - 删除完整 `tryGetLayerObject` inline definition 及二次 closure fallback。
- Android armv7 / iOS arm64 / iOS armv7 recovery IDB
  - 将三个 stripped FromObject helper 命名为 `TJSNI_Layer_FromObject_guess`。
- 四份 recovery IDB
  - 在 FromVariant 注明只用首槽 dispatch、无 ObjThis/第二槽 retry；
  - 在三份 FromObject helper 注明一次 GETINSTANCE、失败抛错、无替代 dispatch；
  - 强制刷新七个函数并回读，随后原位保存四份 recovery IDB。

## 验证

- `tryGetLayerObject` 在生产源码与测试中零匹配；
- ordinary/headless Emscripten syntax-only 均成功；仅出现既有 `_tss` warning；
- Web Debug 与 Wasmtime Headless Debug `motionplayer` 均 25/25 成功；
- Web Debug 完整增量链接 1/1 成功。
