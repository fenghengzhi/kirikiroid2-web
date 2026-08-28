# Player Canvas coordinator envelope（四参考二进制，2026-08-27）

## 1. 单体函数与本地拆分

| 端 | reference `renderToCanvas` | body instructions | iOS armv7 SjLj cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6C4820` | 2363 | — |
| Android armv7 | `0x58E2CC` | 1891 | — |
| iOS arm64 | `0x1001186E0` | 1531 | — |
| iOS armv7 | `0x11653C` | 2155 | `0x118026`, 736 instructions |

四端 7940 条 body与 iOS armv7的 110-state、736 条 cleanup已经按每页240条完整
读取。四端 Hex-Rays主函数因体积只返回截断的声明/局部变量前缀，因此控制流结论以完整
分页 disassembly、字符串/调用引用和 armv7完整 cleanup decompile联合建立。

参考编译物把 coordinator与 per-item command execution编成一个单体，但仍调用独立
`buildRenderCommands`。本地为可维护性拆成：

```text
renderToCanvas_guess
  ├─ buildRenderCommands            // !priorDraw only
  └─ executeLayerRenderCommands     // reference中内联的大循环
```

本项只关闭 coordinator envelope：函数作用域owner、priorDraw gate、尺寸、builder调用、
循环入口/出口和最终setClip。约七千条 per-item primitive执行体虽已全量读取，但仍作为
独立深审项逐分支映射，不用本项标记替代。

## 2. 入口 owner顺序

四端第一项业务构造都是全局 `Layer` name accessor；它取得的 Layer-class raw Object owner
贯穿整个函数。随后才复制传入 target Variant、严格 AsObject取得 target Object-only
owner并立即析构转换临时值。

正常/异常公共尾固定先 Release target raw owner，再析构 Layer accessor并 Release class
raw owner。iOS armv7 final-setClip exception selectors直接进入这两个外层owner的逆序清理，
不会重走任何已经结束的 per-item scope。

本地 `ncbPropAccessor layerClass{TJS_W("Layer")}`位于
`ncbPropAccessor renderTargetOwner{target}`之前，保持同一 owner栈。它不先探测
NativeInstanceSupport，也不借用 target Variant内部指针。

## 3. 两次 live `priorDraw` gate

参考不是入口快照一次再复用，而是在两个时点重新读取 Player priorDraw byte：

1. target owner建立后，若当前 `!priorDraw`，清 `_drawRegion`；
2. width/height属性回调结束后再次读取，若此刻 `!priorDraw`，才调用
   `buildRenderCommands(main,aux,targetClip)`。

因此 width/height getter可重入修改 priorDraw：清 region可能已经发生，而稍后的 builder
仍可被跳过，或反之。不能把两个 gate合成一个局部 bool。

本地保留两个独立 `if(!_priorDraw)`，没有缓存；trace删除后两次读取之间只剩原版尺寸
getter。

## 4. width、height与 builder参数

尺寸通过 Layer-class dispatch调用，以 target raw Object同时作为接收上下文：严格先
`width`，再 `height`。各自使用 process-global hint、flags 0、Integer转换；没有正值或
范围gate。Integer值一方面作为 command executor的 canvas尺寸，另一方面按 signed-int
到 float转换构造 target clip：

```text
[0.0f, 0.0f, float(width), float(height)]
```

第二次 priorDraw gate为 false时，四端 builder入口分别为：

- Android arm64 `0x6C2208`；
- Android armv7 `0x58C7C4`；
- iOS arm64 `0x1001167BC`；
- iOS armv7 `0x114118`。

builder接收原 main/aux容器引用和栈上四float clip；没有在 coordinator中复制或排序容器。

## 5. execute入口与最终 `setClip`

builder gate之后无论 priorDraw真假都进入 main pointer-vector执行循环。本地抽取的
`executeLayerRenderCommands(layerClass,target,width,height,true,main)`对应 reference内联
区域；固定 `skipUpdate=true`是 ordinary caller语义。

空 main list与耗尽 list在同一个最终块汇合：通过 Layer-class dispatch对 target objthis
调用无参数 `setClip`：flags 0、共享 hint、null result、argc 0、argv null，HRESULT忽略。
随后才释放 target和Layer-class owner。四端最终块：

- Android arm64 `0x6C63AC..0x6C6454`；
- Android armv7 `0x58FA62..0x58FACC`；
- iOS arm64 `0x100119F08..0x100119FB8`；
- iOS armv7 `0x117F7C..0x118020`。

本函数没有 Player `lastCanvas`发布、post-draw assignImages或 needs/ready更新；这些属于
外层 `Player::draw`后续步骤。

## 6. 本地偏差与修改

本地 coordinator的 owner、两次 live gate、尺寸顺序、clip、builder/execute路由和最终
setClip都与四端一致。唯一 envelope偏差是原来额外执行：

- motion path解析与 trace enable/path filter；
- 尺寸后的 logo trace log；
- execute后的 trace summary。

这些回调位于两个 priorDraw gate之间或最终owner释放之前，会改变重入状态、异常点与
引用时序。四端都不存在。本轮已全部删除，未增加替代日志或安全分支。

## 7. 验证与剩余边界

本 coordinator envelope标记 `IMPLEMENTED`。已完成四端完整分页 body、完整 armv7 cleanup、
四端入口/最终块逐指令比较、本地 source映射、IDB注释/bookmark/save、coverage 12列检查
与 `git diff --check`。

尚未由本项关闭的是 reference单体中间的 per-item command executor：item admission、
setClip rectangle、source/descriptor/color owner、mesh/Bezier/affine copy、composed/mask、
debug frame、draw-region add与其 100余异常状态。它将在下一深审项单独登记。

正式 CMake/unit/Web build仍因本机缺少 CMake、Ninja、Emscripten且无既有 build/out目录
而未运行。
