# accurate SLA target Layer 发布与 hint 槽四端复原（2026-08-16）

## 1. 结论

四个参考二进制的完整 `Player_renderAccurateSeparateLayerAdaptor_guess` 都不会把 item
Layer 下钻成 `tTJSNI_BaseLayer` 后调用 native setters。后续对完整 renderer 的回溯进一步
确认：`layerId1` 先经 payload-aware resolver 返回基础 owning Variant，并物化一个贯穿初始
copy 与 item 尾部的基础对象句柄；可选 masked Layer、debug frame 与最终 publication 才分别
物化附加对象句柄。上一版把 size、geometry 都描述成独立 phase owner，范围过宽，现已更正。

geometry 分支完成后按固定顺序发布：

```text
Layer.setPos(Real left, Real top)       // FuncCall, argc=2
Layer.type = Integer layerType         // PropSet(TJS_MEMBERENSURE)
Layer.visible = Integer 1              // PropSet(TJS_MEMBERENSURE)
Layer.opacity = Integer item.opacity   // PropSet(TJS_MEMBERENSURE)
```

四个 dispatch 结果都不参与控制流。`opacity` 使用 prepared item 中的原始整数，不做
`0..255` clamp。geometry type 不为 `0/1/2` 时只是不执行 copy-family，仍继续以上四步；
本地旧 `if(!copied) continue` 改变了该边界。

## 2. 四端 publication 指令链

`setPos`、`type`、`visible`、`opacity` 均先用 UTF-16LE byte pattern 搜索，再将 data xref
归属到完整 accurate renderer；publication block 的 raw disassembly 给出以下位置：

| 目标 | `setPos` setup / call | `type` PropSet | `visible` PropSet | `opacity` PropSet helper |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6C8768..0x6C878C` | `0x6C87D4..0x6C87F0` | `0x6C882C..0x6C8848` | `0x6C8858..0x6C8870` |
| Android armv7 | `0x5917A4..0x5917C0` | `0x5917D6..0x5917E6` | `0x5917F6..0x59180A` | `0x591816..0x591824` |
| iOS arm64 | `0x10011BD7C..0x10011BD94` | `0x10011BDB4..0x10011BDC4` | `0x10011BDDC..0x10011BDEC` | `0x10011BDF8..0x10011BE0C` |
| iOS armv7 | `0x11A312..0x11A328` | `0x11A350..0x11A35C` | `0x11A380..0x11A38C` | `0x11A3A6..0x11A3B4` |

`setPos` 两个参数在四端都是由 clip left/top 的 float 转成 Real Variant；不是 Integer
setter，也不是 direct `SetPosition`。type、visible、opacity 都以 flag `0x200`
（`TJS_MEMBERENSURE`）调用 PropSet。visible 先物化 Integer 1；opacity helper 先从传入的
`int *` 读取值、构造 Integer Variant，再调用 dispatch vtable PropSet，helper 返回值被
caller 丢弃。

Android arm64 的 opacity 输入在 `0x6C72CC..0x6C72D4` 取自 item `+0xE8` 并在
`0x6C7394` 保存该字段地址，最后于 `0x6C8854` 原样装入 helper；iOS arm64 对应
`0x10011AB88..0x10011AB90` 与 `0x10011BE00`。Android armv7 在
`0x59063E..0x59064C` 保存 item `+0xD0` 字段地址，`0x59181A` 再装入 helper。不存在
min/max、saturating narrow 或其它 clamp 指令。

## 3. 未知 geometry 仍发布 target

四端第二个 geometry switch 的 unknown-type 后继都汇入 object release 与 publication，
不是 next-item：

| 目标 | type switch / unknown edge | phase release | publication entry |
|---|---:|---:|---:|
| Android arm64 | `0x6C82A0..0x6C82B0` → `0x6C84E4` | `0x6C869C` | `0x6C86AC` |
| Android armv7 | `0x5913C6..0x5913D8` → `0x5915BC` | `0x591736` | `0x59173E` |
| iOS arm64 | `0x10011B910..0x10011B924` → `0x10011BC50` | `0x10011BCE8` | `0x10011BCF8` |
| iOS armv7 | `0x119E48..0x119E5A` → `0x11A1DA` | `0x11A272` | `0x11A286` |

因此“copy-family 是否执行”和“Layer 是否发布 position/type/visible/opacity”是两个不同
的控制流问题。原版只对 geometry `0/1/2` 选择 copy 实现，不用 bool success/copy marker
决定是否发布 Layer，也不检查 copy FuncCall 的 HRESULT。

## 4. owning Variant 与临时 object 生命周期

final publication phase 的四端引用序列：

| 目标 | Variant copy | AsObject / retain | 临时 Variant 析构 | object release | owning item Variant 析构 |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6C86AC..0x6C86B4` | `0x6C86CC..0x6C86E8`（inline） | `0x6C8718` | `0x6C8888..0x6C8894` | `0x6C8898..0x6C889C` |
| Android armv7 | `0x59173E..0x591742` | `0x591752` | `0x591758` | `0x591828..0x591836` | `0x591838..0x59183A` |
| iOS arm64 | `0x10011BCF8..0x10011BD00` | `0x10011BD1C` | `0x10011BD28` | `0x10011BE18..0x10011BE28` | `0x10011BE2C..0x10011BE30` |
| iOS armv7 | `0x11A28C..0x11A290` | `0x11A2A0` | `0x11A2A6..0x11A2A8` | `0x11A3BC..0x11A3CA` | `0x11A3CC..0x11A3CE` |

这证明本地旧 lambda 在返回 raw dispatch 前就销毁 `layerVariant`，再用
`tryResolveLayerDispatch`/native instance gate，并不能表达原版所有权。结合随后回溯出的
函数前半部，完整边界是：payload-aware resolver 返回的基础 Variant 保留到 item 尾部；
resolver 后获取的基础对象句柄跨越可选 source resolve、`setSize`、copy-family、masked-layer
选择与最终发布前的控制流。只有 masked Layer 的 `assignImages`/mask 阶段、debug-frame
阶段和本节表中的 publication 阶段会额外物化各自对象句柄。C++ Variant scope 继续提供
等价引用所有权与异常展开清理。

## 5. member-hint 槽拓扑

四端 hint global 地址：

| 目标 | `setPos` | `type` | `visible` | `opacity` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x1AB548C` | `0x1AB5124` | `0x1AB5488` | `0x1AB5490` |
| Android armv7 | `0x1111928` | `0x1111658` | `0x1111924` | `0x111192C` |
| iOS arm64 | `0x101B69954` | `0x101B695EC` | `0x101B69950` | `0x101B69958` |
| iOS armv7 | `0x187D5F8` | `0x187D31C` | `0x187D5F4` | `0x187D5FC` |

完整 xref 集在四端一致显示：

- `setPos` hint 只属于 accurate renderer；
- `type` 同一槽还被 MotionNodeFrameSlot parse、SLA assign、play、calcViewParam、
  skipToSync 与 getCommandList 使用；
- `visible` 同一槽还被 SLA assign、calcViewParam 与 draw 使用；
- `opacity` 同一槽还被 SLA assign、calcViewParam 与 getCommandList 使用。

所以当前项目“calcViewParam 拥有独立 visible/opacity/type slot”的注释和三个 duplicate
global 是过时的 libkrkr2-era 推断。本轮删除 `calcVisibleMemberHint_guess`、
`calcOpacityMemberHint_guess`、`calcTypeMemberHint_guess`，让 calcViewParam 与其它原版
consumer 共用 `visibleMemberHint_guess`、`opacityMemberHint_guess`、
`typeMemberHint_guess`；另增 accurate-exclusive `setPosMemberHint_guess`。

## 6. 源码落地

`cpp/plugins/motionplayer/PlayerRenderTargets.cpp`：

- 删除 accurate item 的 `tryResolveLayerDispatch`/`resolveNativeLayer`/native setters；
- 让 payload-aware resolver 返回的基础 owning Variant 与基础对象 owner 存活到 item 末尾；
- publication 使用独立 owner；后续回溯另补 masked Layer 与 debug-frame 独立 owner；
- final publication 恢复 TJS `setPos` 与三个 `PropSet(TJS_MEMBERENSURE)`；
- 删除 unknown geometry 的 `copied` admission guard；
- opacity 改为 raw prepared integer，不再 clamp。

`MotionDispatch.h`、`RuntimeSupport.cpp`、`PlayerLayerQuery.cpp`：恢复四端一致的 hint
共享拓扑，并修正过时注释。绝对地址只保存在本文与 recovery IDB，不进入编译源码注释。

## 7. IDB 与验证

四个 recovery IDB 均完成：

- 在完整 accurate renderer 写入 target publication/lifetime/boundary function comment；
- 将四端共 16 个 global 分别命名为 `setPosMemberHint_guess`、
  `typeMemberHint_guess`、`visibleMemberHint_guess`、`opacityMemberHint_guess`；
- force recompile 后 fresh decompile 回读审计注释；
- 原位保存全部四份 recovery IDB。

2026-08-16 验证：

- ordinary Emscripten 单元翻译单元语法检查：通过；
- `KRKR2_WASMTIME_HEADLESS=1` 语法检查：通过；
- `Web Debug Build` 的 `motionplayer` target：33/33，通过；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：33/33，通过；
- 完整 `Web Debug Build` 最终链接：1/1，通过。

输出只有项目既有的 `_tss`、imagepacker `nodiscard`、pthread + memory-growth、JSPI 与 JS
library warning；本轮没有新增编译或链接错误。
