# MotionPlayer private-GLL 自有预处理与通用 builder 调用边纠正（四参考二进制，2026-08-16）

## 结论

非 accurate SLA 的 private-GLL 命令构建器并不复用 Canvas / accurate-SLA 的通用 render-command builder。四份参考二进制中，它各自内联持有一条更窄的预处理流水线：

1. `priorDraw == false` 时清零所有 prepared item 的两枚 stencil 字节；
2. 分配 stencil write/mask 引用并返回候选计数；
3. 仅对 `drawFlag != 0` 的 item 计算目标裁剪；
4. 有效项写入四个 `float` 裁剪边、清空 `leafLayer` Variant，并在 `rawFlag20 == 0` 时通过 ResourceManager 的无参 `requireLayerId` dispatch 分配并锁存 layer id；
5. 上述全部预处理完成后，才清空旧 private deque 并重建提交队列。

`priorDraw == true` 时整段预处理被跳过，候选计数保持为零，但旧 deque 仍被清空并按 prior-draw admission/半透明度规则重建。

因此，端口此前从 `buildPrivateMotionGLLCommands_guess` 调用 `buildRenderCommands` 的复用关系是多造出来的第三条调用边。它还会错误地引入 SeparateLayerAdaptor 叶层创建、局部几何物化、组层合成等 private-GLL 原函数中不存在的副作用。

## 四份函数与唯一调用者

| 平台 | private-GLL builder | 大小 | 唯一调用点（非 accurate SLA 外层） |
|---|---:|---:|---:|
| Android arm64 | `0x6DBB18` | `0xC04` | `0x6D2B9C` |
| Android armv7 | `0x59CB20` | `0x7C2` | `0x597416` |
| iOS arm64 | `0x10012B7D0` | `0x780` | `0x100123528` |
| iOS armv7 | `0x12A304` | `0x7C2` | `0x122734` |

四份反编译均呈现七个观察参数：private deque、宽、高、Player/ResourceManager holder、main list、aux list、Player。aux list 保留在 ABI 中，但函数体不读取它。每份 builder 均只有上述一个外层调用者。

作为调用图交叉校验，通用 builder 在每个平台都只有两个调用者：ordinary Canvas 与 accurate SLA。private-GLL builder 不在其 xref 集合中。

## 预处理顺序

### 1. priorDraw 是整段预处理的外门

- Android arm64 在 `0x6DBB64` 读取 Player prior-draw byte，并在 `0x6DBB68` 分支；
- Android armv7 在函数前段直接把 prior-draw 分支引向 `0x59CB78` 的 deque clear；
- iOS arm64 在 `0x10012B808` 读取、`0x10012B80C` 分支，true 路径把计数置零后跳到 `0x10012BB40`；
- iOS armv7 的 true 路径同样把计数置零并跳到 `0x12A762`。

这确认 prior draw 不会重置 stencil 字节、重算 clip、清 leaf Variant 或申请新 layer id；它消费 item 上一轮留下的相关状态。

### 2. stencil 阶段

非 prior draw 首先对 main list 中每个指针直接解引用并把相邻两枚 stencil byte 一次清零：

- 64 位布局：prepared item `+22/+23`；
- 32 位布局：prepared item `+14/+15`。

候选门为：

- blend mode 低四位不等于 6；
- `drawFlag != 0`；
- `rawFlag16 == 0`；
- opacity 非零；
- `parentItem != nullptr`。

候选计数无论最终是否找到可绘制 mask target 都会递增。写入 byte 的 ref 因此按 `uint8_t` 回绕；超过 255 时只记录一次 `StencilCount overflow(256)`。父链与其 child vector 获得 mask ref；若整条链不存在满足门限的 target，当前候选的 write ref 被重新清零。

端口现有 `assignD3DStencilRefs_guess` 已与这段数据流一致，所以 private builder 继续复用该窄 helper。

### 3. clip、leaf Variant 与 layer-id 阶段

第二轮仅访问 `drawFlag != 0` 的 item。每个平台都先构造：

```text
left   = fmax(paintBox.left, 0.0)
top    = fmax(paintBox.top, 0.0)
right  = min-by-ordered-compare(paintBox.right, float(width))
bottom = min-by-ordered-compare(paintBox.bottom, float(height))
```

只有 viewport 同时满足 `right >= left`、`bottom >= top` 时才参与收窄；其左/上先 `floorf`，右/下先 `ceilf`，再分别和当前四边取交集。

最终门为裁剪宽高严格为正且 `rawFlag16 == 0`。有序的 `>=` 空矩形测试会拒绝相等或反向边；unordered/NaN 比较为 false，因此到达最终门的 NaN 边不会被该门拒绝。

2026-08-16 后续逐指令复核补充：本节所说的 `min-by-ordered-compare` 还要求保持操作数
身份。`paint.right/bottom` 为 NaN 时原生选择 canvas bound，而旧
`std::min(paint, canvas)` 选择 paint；viewport 等值时原生选择 viewport 操作数，库算法则
保留 current 操作数，signed zero 也会不同。`computeD3DClip_guess` 现已把这些选择全部改为
显式三目表达式；完整四端证据见
`motionplayer_private_gll_clip_compare_select_four_binary_2026-08-16.md`。

成功路径依次执行：

1. `rawFlag21 = 1`；
2. 写四个 float `clipRect`；
3. 原位 `tTJSVariant::Clear()` prepared item 的 `leafLayer`；
4. 若 `rawFlag20 == 0`，复制/保有 ResourceManager Variant，取 dispatch，以零参数调用 `requireLayerId`，把结果转 Integer 写入 `renderLayerId`，最后令 `rawFlag20 = 1`。

关键 Clear/dispatch 区域：

| 平台 | `leafLayer.Clear` 区域 |
|---|---:|
| Android arm64 | `0x6DC4AC` |
| Android armv7 | `0x59D252` |
| iOS arm64 | `0x10012BA9C` |
| iOS armv7 | `0x12A6BC` |

失败路径只把 `rawFlag21` 清零。`drawFlag == 0` 的 item 被完全略过，连该 byte 也不会改写。

这里没有以下通用 builder 行为：

- 创建或复用 SeparateLayerAdaptor leaf Layer；
- 对 leaf Layer 执行 copy/operate；
- 计算并保存 Canvas/SLA 的 local corners 或 local mesh；
- 建立 composed/group Layer；
- 处理 aux list；
- 执行通用 layer-pass 的 retired-tree 生命周期。

### 4. 旧队列清理发生在全部预处理之后

清队列边界分别位于：

| 平台 | deque clear 边界 |
|---|---:|
| Android arm64 | `0x6DBB70` 起的内联 deque range destroy/reset |
| Android armv7 | `0x59CB78` |
| iOS arm64 | `0x10012BB40` |
| iOS armv7 | `0x12A762` |

这不是可交换的实现细节：stencil、Variant Clear 或 ResourceManager dispatch 抛出异常时，旧 private queue 仍保持原状；只有预处理完整返回后才释放旧元素。源码继续把 `clearPrivateMotionGLLRenderQueue_guess` 放在预处理块之后。

## 源码纠正

`cpp/plugins/motionplayer/PlayerRenderTargets.cpp` 的 `Player::buildPrivateMotionGLLCommands_guess` 已完成以下修正：

- 删除对 `buildRenderCommands(mainList, auxList, targetClip)` 的调用；
- 保留 `assignD3DStencilRefs_guess(mainList)`；
- 内联恢复 private-GLL 专属 draw/clip/leaf-Clear/layer-id pass；
- 为该原生调用点保留独立的 member-hint storage；
- 明确 aux list 在此 builder 中不使用；
- 保持“预处理先于旧 deque clear”的异常与对象生命周期边界。

修正后，源码中的通用 builder 只剩 ordinary Canvas 与 accurate SLA 两个调用点，与四份参考 xref 图一致。

## IDB 更新

四份 recovery IDB 均已追加：

- 函数级“private builder 自有预处理、无通用 builder 调用边”说明；
- leaf Variant Clear/layer-id latch 行注释；
- 预处理先于 deque clear、aux 参数未读取的行注释。

四份数据库均已保存到 `out/ida-recovery/` 对应平台目录。

## 验证

- ordinary Emscripten 单元测试翻译单元语法检查：通过，只有既有 `_tss` literal-operator 弃用警告；
- `KRKR2_WASMTIME_HEADLESS=1` 同一翻译单元语法检查：通过，同一既有警告；
- Web Debug `motionplayer` archive：`2/2` 通过；
- Wasmtime Headless Debug `motionplayer` archive：`2/2` 通过；
- Web Debug 完整目标：`1/1` 链接通过；只出现既有 pthread/memory-growth、JSPI 与 Emscripten JS library 警告。
