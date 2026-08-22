# motionplayer ordinary Canvas：by-value Variant、void submit 与 post-draw 顺序

日期：2026-08-16

> **V246 更正（2026-08-18）**：本文关于 Canvas by-value/void ABI、caller 临时 Variant
> 先析构再无条件 post-draw 的结论继续成立；但第“源码变更”节曾把本地
> `_lastCanvas = target` 误称为 native publication。四端完整 Canvas 尾和 Player ctor/dtor
> 现已共同证明：Canvas 在 final `setClip` 后直接按 target raw→Layer-class raw 清理并返回，
> draw-affine 前方是无析构的 POD 区而非 Variant member。`_lastCanvas` 字段及写点均已删除，
> 以 `motionplayer_canvas_final_reset_outer_owner_no_lastcanvas_four_binary_2026-08-18.md` 为准。

## 结论

四个参考二进制的普通 Canvas 路径都由 `Player::draw` 唯一调用一个完整 Canvas submitter。该 submitter 接收按值传入的目标 `tTJSVariant`、main prepared list 和 aux prepared list，返回 `void`。caller 在 prepare/projection 后 copy-construct 目标临时 Variant，调用 submitter，立即析构临时 Variant，然后无条件对原始 target Variant 调 post-draw Layer updater；没有读取或测试 submitter 的返回寄存器。

旧移植将 Canvas submitter 和内部拆出的 execute helper 表达为 `bool`，并以 target pointer、Layer class dispatch、motion content 等友好检查生成失败返回；`Player::draw` 再用该 fabricated bool 决定是否执行 post-draw。这改变了原版的 Variant 生命周期、调用链和异常/无效对象边界。本轮已恢复 by-value/void 形态和无条件 post-draw 顺序。

`renderToCanvas_guess` 是 stripped 符号的语义恢复名；`executeLayerRenderCommands` 是为了 Web 移植可维护性从参考大函数中拆出的本地 helper，不宣称参考二进制存在同名独立函数。

## 函数、唯一 caller 与调用点

| 目标 | Canvas submitter | `Player::draw` | submit 调用点 | code xref 数 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6C4820` | `0x6D3398` | `0x6D3600` | 1 |
| Android armv7 | `0x58E2CC` | `0x597864` | `0x597A1A` | 1 |
| iOS arm64 | `0x1001186E0` | `0x100123C84` | `0x100123EF0` | 1 |
| iOS armv7 | `0x11653C` | `0x122F28` | `0x1231B0` | 1 |

四份完整 xref 枚举都只有对应的 `Player::draw` 一条 code edge。Canvas 函数的 IDA instruction count 分别为 2363、1891、1531、2155；尽管优化器和 ABI 让函数体差异较大，四端入口 prototype 都稳定为：

```text
void Canvas(Player *, targetVariantByValue, mainList *, auxList *)
```

## caller 的精确生命周期

四端 ordinary 分支共同序列：

```text
prepare current main/aux lists
if prepare failed: clean lists and return
apply prepared-item projection(main)
copy-construct target temporary from Player::draw by-value argument
CanvasSubmit(player, target temporary, main, aux)
destroy target temporary
postDrawUpdate(player, original target argument)
destroy aux/main vectors
return void
```

关键点：

- `Player::draw` 本身已有一个按值 target owner；调用 Canvas 时再产生一个独立 CopyRef owner。
- Canvas 临时 owner 在 post-draw 之前析构。
- post-draw 接收外层 `Player::draw` 的原始 target owner，不是 Canvas 内部保存的 raw dispatch。
- Canvas submit 没有成功布尔值；post-draw 不是条件调用。

本地已把 `renderToCanvas_guess(tTJSVariant *target, ...)` 改成 `renderToCanvas_guess(tTJSVariant target, ...)`。调用方直接传 `target`，由 C++ 的按值调用自动产生与参考一致的临时 Variant 构造/析构，而不是手工维护 pointer + bool facade。

## Canvas callee 入口顺序

四份函数入口 disassembly 归一化为：

1. 保存 `Player *`、by-value target、main/aux list 参数并建立大栈帧。
2. 用全局类名构造一个 `ncbPropAccessor`/Layer class accessor。
3. copy-construct target Variant 临时值。
4. 将临时 Variant 转成 raw target object；随后析构该临时值。
5. 如果 `priorDraw == false`，清 Player 保留的 complex draw region。
6. 经 Layer class dispatch、以 raw target 为 `objthis` 读取 `width`。
7. 随后读取 `height`。
8. 如果 `priorDraw == false`，调用 build-render-commands 阶段。
9. 进入 main-list submit 逻辑，最终重置 target clip；Layer Update 属于 caller 的 post-draw helper，不在 submitter 内。

入口不存在：

- `targetVariantPointer == nullptr` 返回；
- `Player::hasMotionContent() == false` 返回；
- Layer class dispatch 为空返回；
- target object 解析为空时转换为 `false` 返回；
- execute 阶段失败时传播布尔值。

外层 `Player::draw` 已在 ordinary path 的 prepare 之前处理无 target 和无 motion 情况。Canvas callee 本身依赖 caller 建立的不变量；Layer class/target 解析失败保留原生 TJS/dispatch 自然边界，而不是静默变成“不更新”。

## 本地 execute helper 的返回值校正

参考 Canvas submitter 是一个整体，没有独立 `executeLayerRenderCommands` code xref。Web 移植为了管理大函数，把 submit loop 拆成了本地 private member。该 helper 原先只有一个 outer-level `false` 路径：`layerClassObject`、`renderLayerObject` 或 motion content 不满足；正常结尾固定 `true`。caller 再据此提前退出。

这三项检查在四端 Canvas 入口/主干中不存在，而且 fabricated bool 直接改变 post-draw 是否执行。因此本地 helper 现改为 `void`，删除 outer friendly gate；内部逐项 TJS 调用原本就忽略 HRESULT 或按参考异常边界执行，submit 完成后仍调用无参 `setClip` reset。Headless trace 的 result 字段只保留诊断记录，不参与 production 控制流。

## `Player::draw` 的恢复

ordinary branch 现在：

- `renderToCanvas_guess(target, mainList, auxList)` 按值调用；
- Headless sidecar 将 submit 记为已进入，不再从 production helper 获取 fabricated success；
- `_needsInternalAssignImages` 快照不再被 `rendered` bool 短路；
- `updateLayerAfterDrawRecovered_guess(target)` 无条件调用；
- updater 自身的 bool 仅用于 trace/diagnostic 结果，不控制任何后续 production 语义。

这既恢复了参考调用顺序，也避免诊断 sidecar 改写 native control flow。

## 源码变更

- `Player.h`：Canvas submitter 改为 by-value `tTJSVariant` + `void`；本地 execute helper 改为 `void`。
- `PlayerRenderTargets.cpp`：删除 target/motion/Layer-class friendly returns；直接构造 target accessor并执行 submit，无成功返回值。V246 又删除了本文当时误保留的 `_lastCanvas` publication。
- `PlayerRenderExecute.cpp`：删除 outer null/motion gate与固定 `true` 返回。
- `PlayerDrawDispatch.cpp`：删除手工 target-copy pointer facade和 `rendered` 条件链；恢复按值调用、临时析构后无条件 post-draw。

## IDB 更新

四份 recovery IDB 已：

- 将 Canvas submitter prototype 统一为 void 四参数，并标记 target 为 by-value Variant ABI owner；
- 在 Canvas function、`Player::draw` function 和四个 submit call site 追加本轮入口/生命周期/无条件 post-draw 注释；
- force-recompile Canvas 与 caller，重新读取确认 void prototype 和 caller 注释。

四份 recovery IDB 均已保存；保存后重新读取的 Canvas prototype 为 `void`，`Player::draw` 注释标记也均存在。

## 验证

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 均通过；只有仓库既有 `_tss` warning。
- Web `motionplayer` archive 目标：30/30，通过。
- Wasmtime Headless `motionplayer` archive 目标：30/30，通过。
- 完整 Web Debug build/link：1/1，通过。
- 本轮四个 C++ 文件和本文档的 `git diff --check` 通过；旧 bool/pointer Canvas signature、`const bool rendered` 等模式扫描均无命中。
