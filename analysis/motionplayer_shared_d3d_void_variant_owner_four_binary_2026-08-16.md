# motionplayer shared-D3D：void 内联分支与 selected-target Variant 所有权

日期：2026-08-16

## 结论

四个参考二进制的 sticky shared-D3D 路径都直接内联在返回 `void` 的 `Player::draw(tTJSVariant)` 中；参考实现不存在一个返回成功状态的 shared-D3D member。进入该分支后，`renderFromPlayer`、`captureCanvas` 及其后的清理之间没有返回寄存器测试，也没有失败布尔分支。异常/NCB/TJS dispatch 的原生行为才是该路径的失败边界。

本地为拆分大函数而保留的 `renderViaSharedD3DAdaptor` 因此改为 `void`。Headless 与 logo trace 仍可记录“进入 shared D3D”，但不再用一个固定 `true` 的 production 返回值伪造可失败的控制流。

本轮同时修正了更重要的 Variant 生命周期差异：参考分支先默认构造一个 Void `selectedTarget`，SLA 命中时只把 ordinal 0 的 Layer Variant 赋给它，未命中时才从完整的原始 draw 参数赋值。旧移植先用 `iTJSDispatch2 *` 构造 `selectedTarget`，会在 SLA 分支额外 retain/release 原始 adaptor，并在普通对象分支把可能不同的 `objthis` 错改为 object 本身。

`renderViaSharedD3DAdaptor` 是 Web 移植从参考内联代码中抽出的维护性 helper，不宣称四个参考二进制中存在同名独立函数。

## 四端位置

| 目标 | `Player::draw` | SLA ordinal 0 resolve | 非 SLA 完整 Variant 赋值 | `renderFromPlayer` | `captureCanvas` |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6D3398` | `0x6D3844` | `0x6D3638` | `0x6D3758` | `0x6D3770` |
| Android armv7 | `0x597864` | `0x5979E2` | `0x597A42` | `0x597AD6` | `0x597AE6` |
| iOS arm64 | `0x100123C84` | `0x100123E9C` | `0x100123F28` | `0x100124000` | `0x100124018` |
| iOS armv7 | `0x122F28` | `0x123158` | `0x1231EA` | `0x1232B4` | `0x1232CC` |

iOS armv7 的反编译器把原始输入缓存命名为 `v17`，但它在 `0x1230FA`、`0x123126`、`0x1231D6` 都直接取自 `targetVariant`；因此 `0x1231EA` 的 `copyAssign(destination=v37, source=v17)` 与另外三端一样，是完整输入 Variant 的赋值，而不是从裸 dispatch 重建对象。

## 真实调用和所有权顺序

四端共同顺序可以归一化为：

```text
prepare(main, aux)
if prepare failed: destroy vectors and return
if sticky useD3D is false: take ordinary Canvas branch

shared = process-global raw D3DAdaptor pointer
if shared is null:
    width  = mainWindow.width
    height = mainWindow.height
    raw = operator new
    window owner = mainWindow.GetOwnerNoAddRef()
    construct D3DAdaptor(raw, window owner, width, height,
                         signed(width / 2), signed(height / 2))
    publish raw to process-global slot only after ctor returns

selectedTarget = Void
if original target is SeparateLayerAdaptor:
    rotate active/retired trees and reset ordinal sequence
    temp = resolve ordinal 0
    selectedTarget = temp
    destroy temp
    clear retired tree
    clear SLA private target and newly active tree
else:
    selectedTarget = original target Variant

targetTemp = selectedTarget
targetDispatch = targetTemp.AsObjectNoAddRef() with raw owner retained
destroy targetTemp
targetDispatch.setSize(shared.width, shared.height)
targetDispatch.visible = 1
shared.renderFromPlayer(player, main)
captureTemp = selectedTarget
shared.captureCanvas(captureTemp)
destroy captureTemp
release targetDispatch raw owner
destroy selectedTarget
destroy aux/main vectors
return void
```

这里有三个关键边界：

- shared adaptor 的构造在目标选择之前；目标解析或后续 dispatch 抛出时，已成功发布的 adaptor 仍保留在进程全局槽中。
- `selectedTarget` 在 SLA 分支从未先持有原始 SLA Variant；其第一个非 Void owner 就是 ordinal 0 返回的 Layer。
- 普通分支使用 `tTJSVariant::operator=` 从外层按值参数复制完整 object/objthis 对，不是 `tTJSVariant(targetObject, targetObject)`。

## 返回 ABI 与分支边界

四个 `Player::draw` prototype 均为 `void (Player *, const void *targetVariant)` 的恢复类型。shared-D3D 尾部共同表现为：

1. 调用已恢复为 `void` 的 `D3DAdaptor_renderFromPlayer_guess`；
2. copy-construct `selectedTarget` 作为 `captureCanvas` 的按值参数；
3. 调用 `captureCanvas`；
4. 析构 capture 参数；
5. 释放 setSize/visible 使用的 raw target dispatch owner；
6. 析构 `selectedTarget`；
7. 直接跳转到 prepared-list 容器清理。

没有任何指令把 render/capture 返回寄存器规范化为 bool，也没有条件跳转到“shared_d3d_failed”。shared 分支也不调用 ordinary Canvas 的 projection 或 post-draw Layer Update；V246 又以四端 Player ctor/dtor 证明 `_lastCanvas` 字段本身不存在。

## 源码修正

- `Player.h`：本地 helper 改为 `void renderViaSharedD3DAdaptor(const tTJSVariant &, PreparedRenderItemList &)`。
- `PlayerDrawDispatch.cpp`：传入外层完整 target Variant；删除 `const bool ok` 以及基于它的 trace/logo 分支。
- `PlayerRenderTargets.cpp`：`selectedTarget` 先默认构造；SLA 命中只赋 ordinal 0 Layer，普通分支才赋原始 Variant；删除固定 `return true`。

用 `const tTJSVariant &` 只表示拆分 helper 借用外层 `Player::draw` 已拥有的按值参数，不额外制造参考内联函数中不存在的 helper-argument CopyRef owner。真正与参考一致的持久副本仍是函数体内的 `selectedTarget`。

## IDB 更新

四份 recovery IDB 均已在 `Player::draw`、非 SLA copy-assign 指令和 `captureCanvas` 调用点追加：

- shared-D3D 是 void 内联分支、无成功状态；
- selected-target 从 Void 开始，普通分支复制完整 Variant/objthis；
- capture 后只做 Variant/raw owner 与 vector 清理。

四端均已 force-recompile、读取 copy-assign 行确认注释存在，并保存 recovery IDB。

## 验证

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 均已通过；只有仓库既有 `_tss` warning。
- Web `motionplayer` archive 目标：30/30，通过。
- Wasmtime Headless `motionplayer` archive 目标：30/30，通过。
- 完整 Web Debug build/link：3/3，通过。
- 本轮三个 C++ 文件的 `git diff --check` 通过；本文档无行尾空白。
- 旧 bool helper、`const bool ok = renderViaSharedD3DAdaptor(...)`、`shared_d3d_failed` 和从裸 `targetObject` 预构造 `selectedTarget` 的模式扫描均无命中。
