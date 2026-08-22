# SeparateLayerAdaptor 孤立 private-target Variant getter 四端清理（2026-08-16）

## 结论

本地 `SeparateLayerAdaptor::getPrivateRenderTarget()` 是未注册且零 caller 的
Variant-returning C++ 方便方法。四端 fresh registrar 与 private-target ensure 调用链均不支持
这层 source surface：

- 四个 SLA registrar 都只发布 Factory、RW `absolute`、RW `targetLayer`、`clear`、
  `assign`，没有 private-target property/method；
- 四个 PrivateMotionGLL ensure helper 都直接读取 SLA 第三个 Variant 成员槽；Void 时在该槽
  copy-assign 新对象，非 Void 时直接把同一槽转换为 private native；
- ensure helper 不调用 Variant-returning SLA getter，也不创建第二个 private-target owner；
- 本地 getter 在 `cpp/` 与 `tests/` 中除 declaration/definition 外无任何引用；
- 本地仍在使用的 `getPrivateRenderTargetObject()` 是匿名 assign-target 解析逻辑的 raw-dispatch
  边界，不返回 owning Variant，不能与本轮孤立方法混删。

本轮只删除 `getPrivateRenderTarget()` 的声明和定义。`_privateTarget` 成员、ensure/create/
resize 数据流、clear 的 Invalidate 顺序以及 raw-dispatch 解析均保持不变。

## 四端映射

| 目标 | SLA NCB member registrar | PrivateMotionGLL ensure helper |
|---|---:|---:|
| Android arm64 | `0x6A9378` | `0x6D2D28` |
| Android armv7 | `0x57C5A8` | `0x5974D0` |
| iOS arm64 | `0x100103080` | `0x100123670` |
| iOS armv7 | `0x1004A6` | `0x122884` |

## 注册面证据

四个 fresh decompile 的 registrar 顺序完全相同：

```text
Factory
property absolute    (getter + setter)
property targetLayer (getter + setter)
method clear
raw callback assign
```

没有 `privateTarget`、`privateRenderTarget`、`getPrivateRenderTarget` 或其他隐藏 getter。
这也与 SLA 当前 public NCB 表相同；删除的不是脚本 ABI。

## private-target 真实访问链

四端 ensure helper 共同执行：

```text
targetLayerNative = strict Layer conversion of adaptor.targetLayer

if adaptor.privateTarget.Type == Void:
    created = new __Private_Motion_GLLayer(adaptor.owner,
                                           adaptor.targetLayer)
    adaptor.privateTarget = owning Object Variant(created)
    release temporary raw-object owner
    privateNative = strict native conversion(adaptor.privateTarget)
    privateNative.absolute = adaptor.absolute
    privateNative.visible = true
else:
    privateNative = strict native conversion(adaptor.privateTarget)

privateNative.setSize(targetLayer canvas width, targetLayer canvas height)
return privateNative
```

关键点是 `privateTarget` 只有一个、直接位于 adaptor 对象中。四端 64 位槽位为 `+40`，
32 位槽位为 `+24`；不同平台的 map ABI 只影响后续 active/retired map 与标量偏移。
helper 直接对该槽做 type test、copy-assign 与 native conversion，不存在先按值返回一份
Variant 再解析的 owner 峰值或析构时序。

## 为什么保留 raw-dispatch helper

本地 `getPrivateRenderTargetObject()` 有两个实际 caller：assign 的宽容与严格 layer resolver。
它只在第三个 Variant 的 type tag 为 Object 时返回 borrowed dispatch，否则返回 null；这层
逻辑用于把 SLA payload 解析成可写 target，并不制造 Variant CopyRef。

删除它会迫使匿名 helper越权访问 private 字段或复制相同判断，且会改变当前 malformed
payload 的分支结构。它与零 caller 的 Variant getter 不是同一问题。

## 源码与恢复库修正

- `cpp/plugins/motionplayer/SeparateLayerAdaptor.h`
  - 删除 `tTJSVariant getPrivateRenderTarget() const` declaration。
- `cpp/plugins/motionplayer/SeparateLayerAdaptor.cpp`
  - 删除仅返回 `_privateTarget` 副本的孤立 definition。
- 四份 recovery IDB
  - registrar 注释明确 exact public rows 与 private getter absence；
  - ensure helper 注释明确直接第三 Variant 槽访问和无 Variant getter 边；
  - 强制刷新八个相关函数、回读注释并原位保存四份 recovery IDB。

## 验证

- `getPrivateRenderTarget()` 生产源码零匹配，`getPrivateRenderTargetObject()` 两个 caller 保留；
- ordinary/headless Emscripten syntax-only 均成功；仅出现既有 `_tss` warning；
- Web Debug 与 Wasmtime Headless Debug `motionplayer` 均 25/25 成功；
- Web Debug 完整增量链接 1/1 成功。
