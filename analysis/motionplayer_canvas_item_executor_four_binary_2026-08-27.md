# Player Canvas per-item command executor（四参考二进制，2026-08-27）

## 1. 证据范围与本地拆分

| 端 | reference单体 `renderToCanvas` | body instructions | iOS armv7 SjLj cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6C4820` | 2363 | — |
| Android armv7 | `0x58E2CC` | 1891 | — |
| iOS arm64 | `0x1001186E0` | 1531 | — |
| iOS armv7 | `0x11653C` | 2155 | `0x118026`, 736 instructions |

四端7940条单体 body与 iOS armv7的110-state、736条cleanup已按240条分页完整读取。
上一覆盖项关闭入口/出口 envelope；本项关闭中间的 per-item executor，并映射到本地
`Player::executeLayerRenderCommands`。四端大函数 decompile因体积截断，结论来自完整
disassembly、所有TJS成员字符串/调用引用、iOS armv7完整cleanup decompile以及本地 helper
逐项展开。

四端对应 item入口/循环汇合点：

- Android arm64：item body `0x6C49A8`，loop header/tail `0x6C6388..0x6C63A8`；
- Android armv7：item body `0x58E440`，loop tail `0x58FA42..0x58FA60`；
- iOS arm64：item body `0x100118848`，loop tail `0x100119EE4..0x100119F04`；
- iOS armv7：item body `0x116BC6`，common owner cleanup `0x117F1E..0x117F6E`。

## 2. item admission、clip与 priorDraw

main list是借用的 `PreparedRenderItem*`连续序列；没有 null slot检查。每个item首先读取
skipFlag0、rawFlag16和raw opacity。任一skip flag为真或opacity恰为0时直接进入下一item；
负opacity与大于255的值都继续。

随后只检查四个 viewport float，不读取Web sidecar `hasViewport`：

- viewport数值有效时，left/top用floor、right/bottom用ceil，再与paintBox相交；
- 只有最终 `left > right` 或 `top > bottom` 才跳过item；相等边界保留；
- viewport数值反转时调用无参数 `Layer.setClip`恢复全target clip，然后继续本item；
- 有效交集调用argc4 `setClip(left,top,width,height)`，四参数均为Real；
- `setClip` HRESULT忽略。

四端先在float精度做 `width = right-left`、`height = bottom-top`，随后才提升为Real。原本地
先提升四条边再用double相减，在大幅值或非有限边界可产生不同结果。本轮改成float相减
先行，并新增极端hex-float测试锁定该顺序。

clip调用之后才重新读取Player `_priorDraw`。若priorDraw为真且item.skipFlag1为假，本item
此时才跳过；因此clip副作用已经发生。继续执行时opacity使用C++ signed `/2`，四端等价于
`(x + signbit) ASR 1`，负奇数向零截断。

最后把paintBox四个float按目标指令的signed toward-zero边界转换为整数矩形，并OR进持久
draw region。这个更新发生在任何descriptor/source回调之前。

## 3. per-item外层 owner 栈

admitted item固定构造以下scope，顺序不可交换：

1. 从Player persistent source descriptor Variant构造descriptor accessor；
2. 依次MEMBERENSURE写 `key`、`src`、`blendMode`；
3. 从persistent source colors Variant构造color accessor；
4. 按数字索引0..3依次写四个packed color；
5. 调共享source resolver，保存完整resolved-source Variant；
6. 复制resolved Variant为临时closure，AsObject取得source Object-only owner，立即析构临时；
7. 通过同一source accessor固定先读width，再读height。

source accessor owner与resolved Variant贯穿direct/buffered primitive和debug frame。普通item
尾固定逆序：source accessor raw Object → resolved-source Variant → color accessor → descriptor
accessor。iOS armv7 `0x117F1E..0x117F6E`及110-state cleanup共同确认，任意
`continue`也汇入同一cleanup，而不是绕过C++局部析构。

本轮删除了diagnostic-only `source.layerObject/layer/image`字段和NativeInstanceSupport/
GetMainImage查询；四参考 Canvas renderer只保留TJS Variant/accessor owner。

## 4. blend映射与 direct gate

raw blendMode低4位映射：1→additive `0xE`，2/5→subtractive `0xF`，3→multiplicative
`0x10`，4→screen `0x11`，0及默认→alpha `2`。

direct路径只在三条件同时成立时使用：completionType为0、item没有parent、blend低4位为0
或大于5。它不读取visibleAncestorIndex、childItems或任何Web诊断字段。

direct primitive：

- meshType 0：corners的TL/TR/BL三点各加 `(-0.5f,-0.5f)`，调用Layer-class
  `operateAffine`；
- meshType 1：以实际 `meshPoints`构造Real point Array，Bezier cell division使用
  commandPatchDivision与sourceState尺寸，调用`operateBezierPatch`，clear为Integer 0；
- meshType 2：使用commandCompositeMeshPoints与meshDivX/Y，调用`operateMesh`，clear为0；
- 其他meshType不发primitive、不画debug frame，直接汇入per-item owner cleanup。

primitive HRESULT均忽略。合法direct primitive之后调用共享debug-frame helper，再以source
级 `continue`进入公共owner清理；不会获取bufLayer。

## 5. buffered owner与图像阶段

非direct路径在外层source owners内部再构造：

1. `_sourceCacheObject`临时CopyRef → ResourceManager Object-only accessor；
2. 通过它读取并持有完整`bufLayer` Variant，PropGet状态忽略；
3. `bufLayer`临时CopyRef → buffer Object-only accessor；
4. 临时closure在后续target尺寸回调前析构；
5. 通过Layer-class/target objthis重新读取width，再读取height。

buffer bounds：left/top使用numeric max与0，right/bottom使用ordered compare选择paint edge或
target extent。图像阶段只有 `right < left` 一个skip；bottom<top、零宽或零高都仍到
`setSize`。`setSize`参数是Real width/height，HRESULT忽略。

meshType 0/1/2分别向buffer发affineCopy、bezierPatchCopy、meshCopy；坐标offset为
`-0.5-bufferLeft/Top`，clear固定true。其他meshType不发copy，但仍继续ancestor与
operateRect阶段。

## 6. ancestor mask、operateRect与debug frame

从immediate parent沿parentItem向根遍历：

- `rawFlag21 && !rawFlag16`：按stencilComposite bit2选择composedLayer或leafLayer，
  创建独立dst/src Variant owner，调用alpha-mask；threshold 64，Player maskMode与item
  flags按原值传入；
- 否则若低2位等于1：故意以argc4调用Layer.fillRect。Layer拒绝参数数目，caller忽略错误，
  并立即终止ancestor walk；
- 其他ancestor继续向上。

随后Layer-class对target调用operateRect：dest left/top、bufLayer完整CopyRef、两个Integer
零、Real width/height、映射blend mode和最终opacity。返回状态忽略。

buffer accessor、bufLayer Variant、ResourceManager accessor在离开buffered内层scope时先
全部析构；debug frame位于该scope之外，但仍位于source/color/descriptor外层scope内。
即使right<left跳过图像阶段，仍会析构nested owners并执行debug frame。

debug helper保持outline/meshline Variant gate以及affine/Bezier/mesh frame参数；结束后才进入
外层四owner cleanup。空/耗尽main list最终继续由Canvas envelope无条件reset clip。

## 7. 删除的非参考侧车

本轮从executor删除：

- `MotionTraceRenderExecuteScope`；
- logo path/trace/snapshot初始化与每item日志；
- headless native Layer、main-window scratch owner/parent解析；
- state Layer创建、accurate-SLA candidate二次copy与post-draw candidate记录；
- software affine diagnostics；
- direct before/after probe；
- native source image查询；
-两处 `SNAPCOPY` stderr输出及branch字符串。

这些路径会额外创建Layer、重发copy primitive、查询native image、读取Player/Window状态并
改变异常点，四端均不存在；删除后没有用compatibility fallback代替。

## 8. 验证状态

本项标记 `IMPLEMENTED`。证据包括四端全部7940条body、完整736条armv7 cleanup、所有
primitive/hint/argc/owner范围、现有direct-gate/ABI/buffered-owner/debug-frame测试，以及新增
float-before-Real clip测试。IDB关键phase已补注释并保存。

`git diff --check`通过；coverage保持12列。正式CMake/unit/Web build仍因本机没有CMake、
Ninja、Emscripten且没有既有build/out目录而未运行，不能把测试源码存在误报为已执行。
