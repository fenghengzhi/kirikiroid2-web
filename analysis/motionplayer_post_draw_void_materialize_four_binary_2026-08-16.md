# motionplayer ordinary post-draw：void ABI、flag snapshot 与内部 Layer 物化

日期：2026-08-16

## 结论

四个参考二进制中的 ordinary post-draw updater 是 `void`。它没有“更新成功”的布尔返回契约：AArch64 反编译器把末尾 raw dispatch `Release()` 留在 `x0` 的偶然值猜成整数返回，Android armv7 又把栈保护检查的差值猜成 `int`；iOS armv7 已直接恢复为 `void`。四端唯一 caller 都是 `Player::draw`，且 caller 在调用后不读取返回寄存器。

updater 的真实控制流很小：无条件把 `_needsInternalAssignImages` producer byte 快照到 `_internalRenderLayerReady`，producer 为 0 时立即返回；producer 为 1 时调用内部 Layer materializer，再用 Player 保存的 primary internal Layer 对原始 target Variant 调一次 `assignImages`，最后清理临时 Variant 与 raw dispatch owner。producer byte 本身不在这里清零。

本地 `updateLayerAfterDrawRecovered_guess` 已改为 `void`，`Player::draw` 的 trace 不再通过固定 `true` 伪造可失败分支。没有参考 caller 的裸 `iTJSDispatch2 *`/`bool` wrapper 也已删除。

## 四端 updater 与唯一 caller

| 目标 | updater | `Player::draw` call | ready-byte 写入 | `assignImages` dispatch |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6CBBB8` | `0x6D3614` | `0x6CBBE4` | `0x6CBC88` |
| Android armv7 | `0x59327C` | `0x597A28` | `0x593294` | `0x5932E0` |
| iOS arm64 | `0x10011E6CC` | `0x100123F04` | `0x10011E6FC` | `0x10011E774` |
| iOS armv7 | `0x11CF20` | `0x1231C2` | `0x11CF4C` | `0x11CFE2` |

四份 updater xref 枚举都恰好只有上表一条 code edge。没有 NCB member registration、其他 Player helper 或 SLA 路径调用它；accurate SLA 使用另一个独立 post-draw updater。

## 为什么三个反编译返回类型是假的

修正类型之前的四端 Hex-Rays 结果分别表现为：

- Android arm64：producer 为 0 时 `return player`，非 0 时 `return internalDispatch->Release()`；
- Android armv7：函数尾部 `return stackGuardNow - savedStackGuard`；
- iOS arm64：与 Android arm64 一样，把 raw owner `Release()` 的残留 `x0` 当返回值；
- iOS armv7：直接是 `void`，两条路径都没有返回表达式。

这些值彼此不具有共同语义，也不在唯一 caller 中被消费。C++ void 函数允许返回寄存器保留尾调用/清理的任意结果；栈保护序列也不是业务返回值。结合 iOS armv7 的明确 `void` 和四端 caller 的无读取序列，真实 prototype 稳定为：

```text
void Player_updateLayerAfterDraw_guess(Player *, const tTJSVariant *target)
```

## updater 精确数据流

四端可以归一化为：

```text
ready = producer
if producer == 0:
    return void

materializeInternalRenderLayers(player, target)
internalTemp = copy(player.primaryInternalLayer)
internalRaw = internalTemp.AsObject()   // retained raw owner
destroy internalTemp
targetArg = copy(target)
internalRaw.FuncCall("assignImages", targetArg)
destroy targetArg
internalRaw.Release()
return void
```

关键边界如下：

- ready snapshot 在所有 target 解析、Layer 构造和 TJS dispatch 之前发生，即使 producer 为 0 也会覆盖旧 ready 值。
- early return 不读取 target，因此 production updater 不应为了日志预先解析 target。
- `assignImages` 接收原始外层 target 的按值副本，保留完整 object/objthis；不是从裸 dispatch 重建 Variant。
- TJS `FuncCall` 的结果被忽略；失败按 dispatch/异常的原生边界传播，不转成 false。
- updater 不调用 Layer `Update(false)`，不修改 producer。V246 已进一步证明 Player 根本不存在
  `_lastCanvas` Variant member，而不只是 updater 没有 publication。

## materializer 的两条引用边

| 目标 | materializer | ordinary updater caller | accurate-SLA updater caller | primary publish | work publish |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6CB57C` | `0x6CBBF4` | `0x6CBDC0` | `0x6CB708` | `0x6CB94C` |
| Android armv7 | `0x592F7C` | `0x59329E` | `0x59338A` | `0x592FE4` | `0x5930D2` |
| iOS arm64 | `0x10011E2BC` | `0x10011E70C` | `0x10011E880` | `0x10011E368` | `0x10011E4B0` |
| iOS armv7 | `0x11CAC8` | `0x11CF80` | `0x11D11A` | `0x11CBA6` | `0x11CCF0` |

四端 materializer 均只有 ordinary 与 accurate-SLA 两条 code xref。其 one-shot gate 只测试 primary internal Layer Variant 的 `Type()==Void`，不检查 work Layer，也不检查已发布 Layer 是否完成 sizing。

真实顺序为：

1. primary 非 Void：立即 `void` 返回，不修复、不 resize；
2. 从完整 target Variant 取 `window` owner；
3. `createLayer(window, target)`；
4. 先把返回 Variant 发布到 Player primary slot；
5. 依次读取 target `height`、`width`；
6. 对 primary 调 `setSize(width, height)`；
7. 再次 `createLayer(window, target)`；
8. 把返回 Variant 发布到 work slot；
9. 对 work 调 `setSize(width, height)`。

因此 primary publish 后的任何属性读取、setSize、第二次 createLayer 或 work setSize 失败都会留下 sticky partial state；下一次调用看见 primary 非 Void 后直接返回。当前 materializer 已保持这一边界，本轮只用 fresh 四端证据重新确认，没有添加“修复不完整 workspace”的友好逻辑。

## 源码修正

- `Player.h`：ordinary updater 返回类型改为 `void`；删除无参考 caller 的 `bool updateLayerAfterDraw(iTJSDispatch2 *)` wrapper。
- `PlayerRenderTargets.cpp`：producer=false 分支与正常尾部均改为 `void` 返回；删除固定 `true` 和裸 dispatch wrapper。
- `PlayerDrawDispatch.cpp`：无条件调用 void updater；Headless trace 只记录已进入/完成，不再产生 `render_to_canvas_failed` production 分支。诊断用 producer 快照只在 `KRKR2_WASMTIME_HEADLESS` 下读取。

## IDB 更新

四份 recovery IDB 均已：

- 把 updater 和 materializer prototype 统一为 `void (void *player, const void *targetVariant)`；
- 在 updater function、ready-byte snapshot 和 `assignImages` 调用点追加返回 ABI/数据流注释；
- force-recompile 两个函数，重新读取确认四端都显示 `void` prototype；
- 保存 recovery IDB。

## 验证

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 均已通过；只有仓库既有 `_tss` warning。
- Web `motionplayer` archive 目标：30/30，通过。
- Wasmtime Headless `motionplayer` archive 目标：30/30，通过。
- 完整 Web Debug build/link：3/3，通过。
- 本轮三个 C++ 文件的 `git diff --check` 通过；本文档无行尾空白。
- 旧 bool updater、裸 dispatch wrapper、ordinary `updated` 结果变量与 `render_to_canvas_failed` 的 scoped 模式扫描均无命中。
