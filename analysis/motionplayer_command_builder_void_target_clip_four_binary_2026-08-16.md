# motionplayer command builder：void ABI、四边 float target clip 与两 caller 图

日期：2026-08-16

## 结论

四个参考二进制的 prepared render command builder 是一个 `void` Player member。它的 ABI 在四端都只有四个参数：`Player *`、main prepared-item pointer-vector、aux pointer-vector，以及按 `left/top/right/bottom` 排列的四个 `float` target-clip 边界。旧移植把最后一个 float rect 拆成两个 `tjs_int` width/height，并给函数添加 `return !mainList.empty()`；这个 bool 没有参考 caller，也把 target clip 的非零 left/top 边界丢失了。

四端 builder 都只有两个 code xref：ordinary Canvas submitter 和 accurate SeparateLayerAdaptor renderer。两个 caller 都在栈上独立构造 `[0,0,width,height]` float rect，调用 builder 后立即读取 prepared-list begin/end 或进入 item loop；没有读取或测试返回寄存器。

本地已恢复为：

```cpp
void buildRenderCommands(
    PreparedRenderItemList &mainList,
    PreparedRenderItemList &auxList,
    const std::array<float, 4> &targetClip);
```

## 四端函数与调用图

| 目标 | builder | ordinary Canvas call | accurate SLA call | 正常尾部 retired clear | 正常 return |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6C2208` | `0x6C494C` | `0x6C7254` | `0x6C3798` | `0x6C37DC` |
| Android armv7 | `0x58C7C4` | `0x58E3A4` | `0x59055E` | `0x58D7E2` | `0x58D808` |
| iOS arm64 | `0x1001167BC` | `0x1001187E4` | `0x10011AB08` | `0x100117904` | `0x10011794C` |
| iOS armv7 | `0x114118` | `0x1166AC` | `0x118EFA` | `0x11539C` | `0x1153D6` |

每份 xref 枚举都恰好为上表两条 code edge。AArch64 caller 在调用前设置 `x0=player`、`x1=main`、`x2=aux`、`x3=&targetClip`；两个 armv7 caller 同样设置 `r0..r3`。没有第五个 width/height 参数。

## target clip 是四边 float rect，不是整数尺寸

四端 builder 本体都直接读取最后一个参数的四个 float：

```text
clipLeft   = max(targetClip[0], item.paintBox.left)
clipTop    = max(targetClip[1], item.paintBox.top)
clipRight  = min(item.paintBox.right,  targetClip[2])
clipBottom = min(item.paintBox.bottom, targetClip[3])
```

例如 Android armv7 在 `0x58D17A`、`0x58D198`、`0x58D1B8`、`0x58D1DC` 分别消费四边；iOS arm64 在 `0x100117404` 至 `0x100117428` 呈现同一序列。后续 group-union/alpha-mask阶段也再次读取同一个四边 rect，而不是从 Player 或 Layer 重新查询尺寸。

ordinary Canvas 和 accurate SLA 当前都传入 `[0,0,float(width),float(height)]`，但 builder 的真实边界仍是完整 rect。保留 rect 形态很重要：

- int 到 float 的舍入发生在 caller 构造 rect 时；
- builder 的交集比较保持 float 语义，包括 NaN、有符号零和大整数精度边界；
- 非零 left/top 不应在 helper 边界被强制归零。

`computeRenderClipRect` 因此新增直接接收 `std::array<float,4>` 的路径。原有 width/height overload 只为现有测试/复用构造 `[0,0,w,h]` 后转发，不再是 builder 的 ABI。

## void 返回证据

四端 caller 在 `BL` 后的第一组业务指令分别是 prepared-list begin/end load/compare，没有 `w0/r0` 规范化或基于返回值的条件跳转。

正常 builder 尾部共同为：

1. 若持久 SeparateLayerAdaptor 存在，清 retired tree；
2. 检查 stack guard；
3. 恢复寄存器和栈；
4. `RET`/`POP {...,PC}`。

iOS arm64 本来就反编译为 `void`。Android armv7 的 `return guardNow-savedGuard` 和 iOS armv7 的 `return __stack_chk_guard-saved` 都是 Hex-Rays 把 stack-check 临时寄存器误当业务值；这个值既不稳定，也不被 caller 消费。旧移植的 `!mainList.empty()` 更没有任何指令来源。

## 源码修正

- `Player.h`：builder 改为 `void`，参数顺序恢复为 main/aux/targetClip。
- `PlayerRenderExecute.cpp`：删除 fabricated `ok` 和返回值；clip 计算直接消费四边 float rect。诊断需要的宽高只从 rect 差值派生，不参与 production ABI。
- `PlayerRenderInternal.h/.cpp`：增加四边 target-clip overload；旧整数尺寸 overload 转发到零原点 rect。
- `PlayerRenderTargets.cpp`：ordinary Canvas 与 accurate SLA 显式构造 `[0,0,width,height]` float rect；accurate SLA 的 particle-outside 与 builder 共享同一 owner。non-accurate private-GLL 本地路径也通过 rect 形态调用，避免继续扩散 width/height ABI。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：增加非零、带小数 target clip 的交集断言，确认 left/top 不再被 helper 强制归零。

## 已知源码结构差异

参考 common builder 的四端 xref 图只有 ordinary Canvas 与 accurate SLA 两条边。当前 Web non-accurate private-GLL builder 为复用已恢复的 item preprocessing，仍在本地 C++ 层调用 `buildRenderCommands`；参考 private builder 是另一个独立大函数，没有对 common builder 的第三条机器码 call edge。

因此本轮恢复了 common builder 自身的 ABI、两条真实 caller 和 target-clip数据流，但没有把 private-GLL 的本地代码复用误宣称为原始调用图。下一纵切应比较 private builder 与 common builder 的重叠块，决定哪些预处理必须复制/抽成仅用于移植的内部 phase，最终消除这条已知结构偏差而不破坏两套 builder 各自的容器副作用。

## IDB 更新

四份 recovery IDB 均已：

- 将 builder prototype 统一为 `void (player, mainList, auxList, const float *targetClip)`；
- 在 builder function 和两个 caller 的 call site 追加 void ABI、四边 rect 与无返回读取注释；
- force-recompile 并重新读取，四端 prototype 均显示为 `void` 四参数；
- 保存 recovery IDB。

## 验证

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 已通过；只有仓库既有 `_tss` warning。
- Web `motionplayer` archive 目标：30/30，通过。
- Wasmtime Headless `motionplayer` archive 目标：30/30，通过。
- 完整 Web Debug build/link：3/3，通过。
- 本轮六个源码/测试文件的 `git diff --check` 通过；本文档无行尾空白。
- 旧 bool builder、width/height 形态调用和 `!mainList.empty()` fabricated return 的 scoped 模式扫描均无命中。
