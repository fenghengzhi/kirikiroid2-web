# accurate SLA Layer 物化、遮罩与调试帧四端复原（2026-08-16）

## 1. 结论

四个参考二进制中的 `Player_renderAccurateSeparateLayerAdaptor_guess` 不是把
`layerId1` 直接交给 payload-free ordinal resolver。每个通过 clip gate 的 item 都先从
prepared item 构造完整 `SeparateLayerPayload_guess`，再调用 payload-aware
`resolveLayerNode_guess(layerId1, payload, createdOrChanged)`。只有非特殊 blend 且存在父 item
时，才另外以 `layerId2` 调用 payload-free ordinal resolver，得到用于祖先遮罩的中间 Layer。

复原后的单 item 数据流为：

```text
PreparedRenderItem
  -> SeparateLayerPayload_guess
  -> resolveLayerNode(layerId1, payload, changed)
  -> retained base Layer Variant/object
       if changed:
         resolve source -> width/height -> base.setSize -> geometry copy
       if (blend.low4 != 6 && parent != null):
         base.visible = 0
         resolveLayerOrdinal(layerId2) -> masked Layer
         masked.assignImages(base)
         masked.setSize(clipWidth, clipHeight)
         walk ancestors -> doAlphaMask or deliberate argc=4 fillRect
       optional debug frame on final Layer
       final.setPos -> type -> visible=1 -> raw opacity
```

blend 低四位为 `6` 时 Layer type 仍为普通 alpha，但另有独立布尔边界直接绕过中间
mask buffer。把“Layer type 选择”和“是否建立 mask buffer”合并成同一个 switch 返回值会
丢失这一行为。

## 2. payload-aware 基础 Layer resolver

四端调用点与被调函数：

| 目标 | accurate 调用点 | `resolveLayerNode_guess` |
|---|---:|---:|
| Android arm64 | `0x6C7490` | `0x6C3F28` |
| Android armv7 | `0x590912` | `0x58DCD4` |
| iOS arm64 | `0x10011AD70` | `0x100117E88` |
| iOS armv7 | `0x119284` | `0x115B34` |

Android arm64 的 `0x6C73D0..0x6C7490` 将以下字段逐一写入局部 payload；其它三端的
字段次序和分支结构一致：

- Player `completionType`；
- `outline` 或 `meshline` 至少一个非 Void 的布尔值；
- item `commandSrc`、raw `blendMode`、四个 packed color；
- `paintBox[4]` 后接 `viewport[4]`；
- `meshType==2` 选择 `commandCompositeMeshPoints`；
- `meshType==1` 选择 `commandBezierPatchPoints`；
- 四角坐标 `corners[8]`。

返回的 `createdOrChanged` 在 caller 中保留独立 gate。Android arm64 在 `0x6C754C`
为 false 时跳过 source resolve、source width/height、base `setSize` 和第一次 geometry
copy，汇入后续 Layer 选择。当前四端随附 comparator 的所有 mismatch 与 all-equal 出口都
返回 true，但 caller 边界仍属于原始实现，不能据此删除。

## 3. 中间 masked Layer 与祖先链

低四位 blend 为 `6`，或 `parentItem==nullptr` 时，final Layer 直接 CopyRef 基础 Layer。
其它路径按固定顺序执行：

1. 基础 Layer `visible=Integer 0`，使用 `PropSet(TJS_MEMBERENSURE)`；
2. `resolveLayerOrdinal_guess(layerId2)`；
3. 中间 Layer `assignImages(baseLayerVariant)`，argc=1；
4. 中间 Layer `setSize(Real clipWidth, Real clipHeight)`；
5. 从 `parentItem` 沿 `parentItem` 指针向上遍历。

四端 ordinal resolver 与 `assignImages` 证据：

| 目标 | ordinal 调用点 / callee | `assignImages` setup/call |
|---|---:|---:|
| Android arm64 | `0x6C7F44` / `0x6C90C4` | `0x6C7FCC`, `0x6C7FE4` |
| Android armv7 | `0x5910C2` / `0x591DEC` | `0x591100`, `0x59110E` |
| iOS arm64 | `0x10011B634` / `0x10011C628` | `0x10011B698` |
| iOS armv7 | `0x119B16` / `0x11AE24` | `0x119B8A`, `0x119B9A` |

祖先节点满足 `rawFlag21 && !rawFlag16` 时，mask source 由
`stencilComposite & 4` 在 `composedLayer` 与 `leafLayer` 间选择。mask 参数保持原版整数
转换与位域：

```text
dstX = trunc_saturated(ancestor.clip.left - itemClip.left)
dstY = trunc_saturated(ancestor.clip.top  - itemClip.top)
srcX = 0, srcY = 0
width  = trunc_saturated(ancestor.clip.right  - ancestor.clip.left)
height = trunc_saturated(ancestor.clip.bottom - ancestor.clip.top)
threshold = 64
mode = Player.maskMode
flags = ancestor.stencilComposite & 3   // helper 接收 raw word 并在内部取位
```

四端 `Motion_doAlphaMask` 调用与 callee：

| 目标 | accurate 调用点 | callee |
|---|---:|---:|
| Android arm64 | `0x6C8144` | `0x6AC4E4` |
| Android armv7 | `0x591260` | `0x57E1E8` |
| iOS arm64 | `0x10011B7F0` | `0x100104E68` |
| iOS armv7 | `0x119D1A` | `0x10243C` |

若当前祖先不进入 alpha-mask，但 `(stencilComposite & 3)==1`，caller 故意用 argc=4 调用
`fillRect`，忽略 `TJS_E_BADPARAMCOUNT`，并立即终止祖先遍历。四端 UTF-16LE xref/call setup
分别为 Android arm64 `0x6C81C4, 0x6C81DC`、Android armv7
`0x59134E, 0x591358`、iOS arm64 `0x10011B87C`、iOS armv7
`0x119DB8, 0x119DC4`。这与 ordinary buffered renderer 的祖先链边界一致。

## 4. debug-frame 第二 geometry switch

只要 `outline`/`meshline` 至少一个非 Void，并且 `createdOrChanged` 为 true 或 item 有父级，
accurate renderer 就在 final Layer 上执行第二个 geometry switch。receiver 与 objthis 都是该
final Layer；坐标偏移为 `-0.5 - clip.left/top`，不是 ordinary canvas 路径的固定 `-0.5`。

| geometry | 调用 |
|---|---|
| `meshType==0` | 四条 `drawLine`，逐边传 outline 与端点 |
| `meshType==2` | `drawMeshFrame(outline, meshline, points, divX, divY)` |
| `meshType==1 && meshline!=Void` | `drawBezierPatchMeshFrame(...)` |
| `meshType==1 && meshline==Void` | `drawBezierPatchFrame(...)` |
| 其它 | 不调用 debug member，但仍进入 publication |

四端 UTF-16LE xref：

| 目标 | `drawLine` | `drawMeshFrame` | `drawBezierPatchFrame` | `drawBezierPatchMeshFrame` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6C839C`, `0x6C83B4` | `0x6C8490`, `0x6C84A8` | `0x6C8650`, `0x6C8668` | `0x6C85B8`, `0x6C85D0` |
| Android armv7 | `0x5914C0`, `0x5914CC` | `0x591580`, `0x59158C` | `0x591710`, `0x591716` | `0x59169C`, `0x5916A2` |
| iOS arm64 | `0x10011BA04` | `0x10011BC0C` | `0x10011BCAC` | `0x10011BB34` |
| iOS armv7 | `0x119F76`, `0x119F86` | `0x11A1A4`, `0x11A1B4` | `0x11A242`, `0x11A252` | `0x11A0BC`, `0x11A0CC` |

## 5. 对象生命周期更正

payload-aware resolver 返回基础 owning Variant 后，accurate renderer 获取一次基础对象句柄；
该句柄跨越第一次 source copy、masked-layer 选择以及 item 尾部，Android arm64 的释放位于
`0x6C88A0..0x6C88B0`。masked Layer 的 `assignImages`/mask 阶段另有 owner，debug frame
另取 final Layer owner，最终 publication 又按前一份报告列出的序列重新物化对象。

因此 `motionplayer_accurate_sla_target_publication_four_binary_2026-08-16.md` 早先所称
“size、geometry、publication 各自独立 phase owner”过宽。本轮已同步更正该文档、源码
注释与四份 recovery IDB function comment；publication 自身的地址表与边界结论不变。

## 6. 源码落地

`cpp/plugins/motionplayer/PlayerRenderTargets.cpp`：

- 构造完整 `SeparateLayerPayload_guess` 并以 `layerId1` 调用 payload-aware resolver；
- 保留 `createdOrChanged` source/copy gate；
- 独立表达 blend-low-nibble 6 的 mask-buffer skip；
- 恢复 base `visible=false`、`layerId2` ordinal Layer、`assignImages`、`setSize`；
- 复用普通 buffered 路径已经四端对齐的 alpha-mask/fillRect 祖先循环；
- 在 final Layer 上恢复带 clip-local offset 的 debug frame；
- publication 与 headless trace 改为 final Layer，而非无条件基础 Layer。

`cpp/plugins/motionplayer/PlayerRenderInternal.{h,cpp}`：frame helper 新增默认 offset 参数；
ordinary 两个 caller 保持默认 `-0.5,-0.5`，accurate caller 传
`-0.5-clip.left,-0.5-clip.top`。

`cpp/plugins/motionplayer/SeparateLayerAdaptor.h`：删除“accurate 基础 Layer 使用 payload-free
ordinal overload”的过时 libkrkr2-era 注释；准确限定 ordinal overload 的 accurate 用途为
可选中间 masked Layer。

## 7. 验证

2026-08-16：

- ordinary Emscripten 单元翻译单元语法检查：通过；
- `KRKR2_WASMTIME_HEADLESS=1` 语法检查：通过；
- `Web Debug Build` 的 `motionplayer` target：25/25，通过；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：25/25，通过；
- 完整 `Web Debug Build` 最终链接：1/1，通过。

输出只有项目既有的 `_tss`、imagepacker `nodiscard`、pthread + memory-growth、JSPI 与 JS
library warning；本轮没有新增编译或链接错误。
