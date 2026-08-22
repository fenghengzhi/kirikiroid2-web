# PreparedRenderItem 单一 source descriptor 与空指针边界四端复核（2026-08-16）

## 1. 结论

删除 Web-derived `detail::PreparedRenderItem::nativeNode`，并删除四个 render-time
`sourceState` nullable fallback。prepared item 现在和四个参考一样只保存一个借用的
`MotionNode::SourceState *`；Bezier cell division 直接从它读取 width/height。

删除范围：

- derived-tail `const MotionNode *nativeNode`；
- type-3 wrapper 与 ordinary item population 对该字段的两次写入；
- canvas leaf、PrivateMotionGLL、shared D3D 与 direct buffered render 中四组
  `sourceState ? ... : nativeNode/0` fallback。

保留 `nodeIndex`，因为 Web diagnostic message 实际读取它；保留 `sourceState`，因为它是
四端 native core 中的真实 descriptor alias，也是 texture/rect/extent 的 authority。

## 2. 四端 builder：唯一 descriptor pointer

fresh 复核完整 `Player_appendPreparedRenderItems_guess`：

| 目标 | 函数 | source pointer store |
|---|---:|---:|
| Android arm64 | `0x6BF714` | `0x6C09EC`，item `+0x100` |
| Android armv7 | `0x58B178` | `0x58B5B0`，item `+0xE4` |
| iOS arm64 | `0x1001148F8` | `0x100114DFC`，item `+0x100` |
| iOS armv7 | `0x1123D8` | `0x11278E`，item `+0xE4` |

四端都在 source-clip color remap 和 corner/src/blend/opacity publication 链中，把当前
node 的持久 source descriptor 地址写入 item 的同一个 core field。相邻下一个 pointer
是 visible-ancestor `parentItem`（64-bit `+0x108`、32-bit `+0xE8`）；并不存在另一个
MotionNode pointer 或 descriptor fallback field。

这与精确 allocation/destruction 纵切一致：native item 大小严格为 64-bit `0x1B0`、
32-bit `0x148`，owner suffix 结束后没有派生 node-pointer storage。

## 3. 四端 consumer：直接解引用

fresh 复核 `Player_renderPreparedItemsToD3DTexture_guess` 的 Bezier branch：

| 目标 | 函数 | direct extent read/conversion |
|---|---:|---:|
| Android arm64 | `0x6AB39C` | `0x6AB680..0x6AB6B0` |
| Android armv7 | `0x57D3DC` | `0x57D65E..0x57D676` |
| iOS arm64 | `0x100104450` | `0x100104820..0x100104850` |
| iOS armv7 | `0x101850` | `0x101F4A..0x101F60` |

共同形状为：

```text
source = item.sourceState
width  = source.width
height = source.height
widthWord/heightWord = unsigned saturated toward-zero conversion
```

四端从 item field 取 pointer 后都立即读取两个 double；没有 pointer test、node fallback、
零 extent substitute 或第二次 descriptor lookup。null `sourceState` 因而属于原版直接
解引用边界，不应由 portable backend 静默改成 node width/height 或 `{0,0}`。

## 4. 本地可达性与 owner lifetime

本地所有实际 item publication 也满足该 invariant：

- ordinary source item 在进入 main/aux list 前写 `sourceState=&node.source`；
- drawFlag/mask type-3 wrapper 在递归构造 child list 前写同一 pointer；
- visible-ancestor 与 stencil post-pass 可预先 lazy-create item，但只有已经走过 population、
  标记 `drawnThisFrame` 的 item 才进入实际 render list/Bezier consumer；
- item 由 MotionNode 独占且 node deque 在一次 render list 使用期间保持存活，所以 pointer
  是借用、无需 AddRef 或单独析构。

`nativeNode` 的全部生产用途只是为一个永远已发布的 `sourceState` 提供 redundant fallback；
它没有 diagnostic reader，也不承载额外 owner lifetime。删除后对象尾部和空值行为都更接近
四参考。

## 5. IDB 与验证

四份 recovery IDB 的 builder 与 D3D consumer 入口补充“single descriptor/direct
dereference”注释。绝对地址只保留在本文和 IDB，不写入编译源码注释。

IDB 写回结果：

- 8 个入口注释均由写入接口确认成功，随后全部执行 Hex-Rays cache invalidation；
- Android armv7、iOS arm64、iOS armv7 的 builder/consumer，以及 Android arm64
  consumer，均从重新反编译文本直接回读到新注释；Android arm64 builder 是超大函数，
  当前接口只返回截断的旧注释头，但写入结果为 `appended=true`；
- `sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery`
  四份数据库均已原位保存成功。

源码与构建验证：

- `nativeNode` 以及 `item.sourceState ? ...` nullable extent fallback 在
  `cpp/plugins/motionplayer` 与 tests 中均为零命中；
- 结构中只剩一个 `MotionNode::SourceState *sourceState` 字段，builder 保留 type-3
  wrapper 与 ordinary item 两个 publication write；六条 portable Bezier route 共 12 个
  width/height 读取全部为直接 `item.sourceState->...`；
- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 编译均通过，仅有既有 `_tss`
  literal-operator 弃用警告；
- Web Debug `motionplayer` archive `34/34`、Wasmtime Headless Debug
  `motionplayer` archive `34/34`、完整 Web Debug 最终构建/链接 `3/3` 全部成功；
- scoped `git diff --check` 返回 0；输出只有 Git 对工作区既有 LF/CRLF 策略的提示。
