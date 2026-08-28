# Player accurate SeparateLayer post-draw（四参考二进制，2026-08-27）

## 1. 入口与完整取证

| 端 | `updateAccurateSLAAfterDraw` | body instructions | iOS armv7 SjLj cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6CBD18` | 253 | — |
| Android armv7 | `0x593344` | 169 | — |
| iOS arm64 | `0x10011E808` | 147 | — |
| iOS armv7 | `0x11D078` | 238 | `0x11D2FE`, 101 instructions |

四端 807 条主函数指令已经完整分页读取；四份 fresh decompile 也逐端取得。iOS armv7
额外的 101 条 SjLj cleanup 已完整读取并与主函数的 landing-pad state 联合核对。主函数、
materialize callee 和 armv7 cleanup 已命名/注释，入口及 cleanup 已 bookmark，四个 IDB
均已保存。

四端的差异只在 ABI 展开：AArch64 以 DWARF unwind landing pad清理，Android armv7的
Variant参数析构大部分留在主函数尾部，iOS armv7用一个 14-state SjLj cleanup集中表达。
控制流、成员偏移关系、调用顺序和 owner拓扑一致。

## 2. 共同控制流

四端共同伪代码可归纳为：

```cpp
ready = needs;
if(!needs) return;

TargetObjectOwner targetOwner(copy(target));
materializeInternalRenderLayers(target);
InternalObjectOwner internalOwner(copy(internalRenderLayer));

height = target.HasValue("height") ? target.GetValue<int>("height") : 0;
width  = target.HasValue("width")  ? target.GetValue<int>("width")  : 0;

internal.piledCopy(0, 0, target, 0, 0, width, height);
```

`ready = needs` 是无条件字节复制，发生在早退判断之前；四端分别表现为 Player内部相邻
byte `+0x264 <- +0x265`、`+0x19C <- +0x19D`、`+0x1F4 <- +0x1F5`、
`+0x15C <- +0x15D`。函数从不清除 producer `needs` byte。

因此 `needs == false` 的唯一行为是发布 `ready=false` 后立即返回；不会转换 target、不会
materialize Layer，也不会读尺寸或发 `piledCopy`。

## 3. target 与 internal owner 生命周期

需要回写时，四端先按值复制完整 target Variant，再从临时值取得一份 Object-only
AddRef，随即析构临时 Variant；留下的 raw Object owner跨越 materialize、尺寸读取和
最终调用。本地 `ncbPropAccessor targetAccessor{tTJSVariant(target)}` 恢复了同一个
“完整 Variant临时 owner → Object-only owner → 临时析构”形状，并且明确位于
`materializeInternalRenderLayers_guess(target)` 之前。

materialize返回后，函数对 Player persistent `_internalRenderLayer`重复同一套临时
Variant copy、Object AddRef、临时析构，得到 internal raw Object owner。正常尾部先析构
七个调用参数 Variant，再 Release internal owner，最后 Release target owner。

这两个 accessor都不是 borrowed raw pointer，也不是保持 object/objthis双指针的完整
Variant owner。相反，作为 `piledCopy`第三个参数传入的原始 target保持完整 Variant
语义，包括可能与 object不同的 objthis；本地直接传 `target` 与四端一致。

## 4. materialize边界与尺寸读取顺序

四端在取得 target Object-only owner后调用同一个 Player materialize helper：

- Android arm64 `0x6CB57C`；
- Android armv7 `0x592F7C`；
- iOS arm64 `0x10011E2BC`；
- iOS armv7 `0x11CAC8`。

本项只证明 post-draw对该 callee的调用位置、参数和 owner窗口；callee内部的 Layer创建、
部分失败残留和精确 EH ledger仍作为独立覆盖项，不用本项替代。

materialize之后，post-draw固定先处理 `height`，再处理 `width`。每一维都先用相同
process-global hint做 `HasValue`，存在时才用同一 accessor/hint做 Integer `GetValue`；
不存在时取 0。不存在不抛错，存在但类型转换失败则异常传播。没有 positive、zero或
overflow修正，取得的 `tjs_int`原样进入调用参数。

本地 `getInternalWorkspaceDimension_guess` 已保持这套双读/缺失为零语义，现有 probe测试
还验证了 flags、hint复用、objthis以及负 Integer不被钳制。

## 5. `piledCopy` 参数与返回值

最终调用 receiver和 objthis都是 internal raw Object，成员名固定为 `piledCopy`，argc
严格为 7：

1. Integer 0；
2. Integer 0；
3. 完整 target Variant；
4. Integer 0；
5. Integer 0；
6. width Integer；
7. height Integer。

注意读取顺序是 `height → width`，参数发布顺序仍是 `width → height`。没有 result
Variant，`FuncCall` HRESULT被忽略；普通脚本错误状态不会被本函数改写成成功/失败标志。
参数临时值在返回后逆序析构。iOS armv7 cleanup的 case 9显式销毁前五个临时参数，
随后 case 8销毁 width/height两个临时参数，再 Release internal与target两个 Object owner，
与其他三端正常尾和异常 landing pad一致。

## 6. 边界行为与异常残留

- `needs`发布给 `ready`之后发生的任何异常都不会回滚这次发布；
- 非 Object target的严格转换异常发生在 materialize之前；
- Object-null仍按 ncbind accessor的原生转换/调用边界处理，没有 Web侧 null bypass；
- materialize已经产生的 persistent Layer状态不会由 post-draw回滚；
- height成功、width失败时，height读取副作用保留，但不会调用 `piledCopy`；
- `piledCopy`抛出的 C++/TJS转换异常按 owner stack清理后继续传播；普通失败 HRESULT被忽略；
- producer `needs`在成功、普通失败或异常路径中都不由本函数清除。

## 7. 本地差异与修改

本地核心控制流、owner构造点、materialize位置、尺寸次序和七参数调用原本已经与四端
一致。唯一实质偏差是函数前后附加了 motion-path/logo trace：它会额外解析 motion path、
执行条件分支并在早退/成功路径调用日志设施，参考二进制均无这些副作用。

本轮删除这些 trace owner和调用，没有增加补偿逻辑。当前 source因此只保留参考共同体：
无条件 ready快照、needs早退、两个 Object-only owner、materialize、height/width双读和
`piledCopy`。

## 8. 验证状态

本项标记 `IMPLEMENTED`。完成的验证包括：四份 fresh decompile、四端全部 807 条主函数
指令、完整 101 条 iOS armv7 cleanup、本地逐语句对照、现有 workspace-dimension probe
测试审阅、IDB命名/注释/bookmark/save，以及 `git diff --check`。

正式 CMake/unit/Web build仍因本机缺少 CMake、Ninja、Emscripten且没有既有 build/out
目录而未运行；不能把 source审阅或 probe测试存在误报成已执行测试。
