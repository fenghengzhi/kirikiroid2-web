# Accurate-SLA sourceState 与尺寸 authority 四端复核（2026-08-16）

## 1. 结论

删除 `Player::renderAccurateSeparateLayerAdaptor_guess` 中 Web-derived
`if(!item.sourceState) continue`，并拆开此前错误合并的两类尺寸来源：

- copy source rect 的 right/bottom 来自 resolver 返回的 source Layer 上按顺序执行的
  TJS `width`、`height` property read；
- Bezier cell division 来自 prepared item 借用的 persistent
  `sourceState->width/height`；
- backing `sourceImage->GetWidth()/GetHeight()` 既不是上述 property dispatch 的替代品，
  也不是 Bezier extent authority。

本轮不宣称已经重写完整 accurate renderer 的所有 Layer dispatch block；只闭合 source
pointer 的空值边界、两个尺寸数据源和它们的读取顺序。

## 2. 四端 sourceState 直接解引用

fresh 复核四个完整 `Player_renderAccurateSeparateLayerAdaptor_guess`：

| 目标 | 函数 | sourceState load | resolver call |
|---|---:|---:|---:|
| Android arm64 | `0x6C7088` | `0x6C77E0` | `0x6C77F0` |
| Android armv7 | `0x590468` | `0x590A72` | `0x590A7C` |
| iOS arm64 | `0x10011A9E8` | `0x10011AF34` | `0x10011AF44` |
| iOS armv7 | `0x118D70` | `0x11944A` | `0x11945A` |

共同指令形状是：

```text
source = item.sourceState
object = &source.object
Player_resolveRenderSource(player, object, ...)
```

64 位从 item `+0x100` 取 pointer，32 位从 `+0xE4` 取 pointer；随后直接加到
descriptor object field，并进入 resolver。四端中间都没有 pointer compare/zero branch。
因此本地在 SLA Layer 已 resolve/create 之后再对 `sourceState` 做 continue，会新增原版不存在的
半构造退出路径，并把应当直接 fault/UB 的无效 prepared item 静默吞掉。

## 3. source Layer property dimensions

resolver 返回 Variant 转成 source object closure 后，四端都严格先读 `width`、再读
`height`：

| 目标 | width property call | height property call |
|---|---:|---:|
| Android arm64 | `0x6C7870` | `0x6C7898` |
| Android armv7 | `0x590ABE` | `0x590AD8` |
| iOS arm64 | `0x10011AF8C` | `0x10011AFB0` |
| iOS armv7 | `0x1194B6` | `0x1194E2` |

共同 helper 忽略 PropGet HRESULT，把 temporary Variant 转成 integer，然后在返回前销毁
temporary。两个整数稍后组成 copy 的 source rectangle `{0,0,width,height}`。它们不是从
native Layer main image 或 backing texture 查询的尺寸；property dispatch 本身是可观察数据流。

本地现在用两个连续调用完成同样顺序，每次调用各自持有并销毁一个 temporary Variant，
receiver 与 objthis 都是 source Layer dispatch。

## 4. Bezier descriptor extents

同一 accurate renderer 的 Bezier branch 再次直接读取 item sourceState：

| 目标 | pointer reload | descriptor width/height read |
|---|---:|---:|
| Android arm64 | `0x6C851C` | `0x6C8524` |
| Android armv7 | `0x591604` | `0x59160C..0x591610` |
| iOS arm64 | `0x10011BA88` | `0x10011BA90` |
| iOS armv7 | `0x11A008` | `0x11A010..0x11A014` |

这些 double 经四端已经闭合的 uint32 toward-zero/saturation 与 wrapping arithmetic 管线生成
cell divisions。source Layer property width/height 只定义 copy rectangle；backing image
dimensions 不参与 split。由此本地 Bezier 调用改为
`item.sourceState->width/height`。

## 5. 源码落地

`cpp/plugins/motionplayer/PlayerRenderTargets.cpp`：

- 删除 accurate item loop 中的 nullable `sourceState` continue；
- resolver 返回 object 后，通过该 dispatch 顺序 PropGet `width`、`height`，每次使用独立
  temporary Variant；
- source rect 改用两个 property integer；
- 删除 backing image 宽高正值 gate，不再让 backing dimensions 决定 source rect admission；
- Bezier division 改用 persistent descriptor double extents。

绝对地址只记录在本文和 recovery IDB；编译源码继续使用语义 `_guess` 标识。

## 6. IDB 与验证

四端完整 accurate renderer 入口均已追加本轮 source-authority 注释，四个函数全部完成
Hex-Rays cache invalidation；重新反编译文本均直接回读到新注释。四份 recovery IDB 随后
原位保存成功。

源码与构建验证：

- `PlayerRenderTargets.cpp` 中 `if(!item.sourceState)` 与
  `sourceImage->GetWidth()/GetHeight()` 均为零命中；
- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 编译均通过，只有既有 `_tss`
  literal-operator 弃用 warning；
- Web Debug `motionplayer` archive `2/2`、Wasmtime Headless Debug
  `motionplayer` archive `2/2`、完整 Web Debug 最终链接 `1/1` 全部成功；
- scoped `git diff --check` 返回 0；输出只有工作区 LF/CRLF policy 提示。
