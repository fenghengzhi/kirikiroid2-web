# PreparedRenderItem 死 source snapshot 与 texture-rect authority 四端复核（2026-08-16）

## 1. 结论

删除 Web-derived `detail::PreparedRenderItem::sourceTexture/sourceRect` 与 builder 对它们的
snapshot 写入，并删除 `sourceRectForItem` fallback helper。shared D3D 与
PrivateMotionGLL command builder 现在都从唯一借用的 `sourceState` 直接读取
`textureRect`；texture getter 的返回值只作为 texture 本身使用，不能成为 rect authority。

这不是单纯去重。原版的 observable boundary 是：

- source getter 返回后重新读取 descriptor rect，使 callback/atlas load 对同一持久
  descriptor 的原地更新立即可见；
- descriptor pointer 没有 null guard；
- texture 为 null 时也不把 rect 改成零或 texture 全尺寸；PrivateMotionGLL 仍构造并追加
  持 null texture 的 queue element，`Draw_GPU` 之后在该 element 处终止循环。

## 2. shared D3D：getter 后从同一 alias 重新读 rect

fresh 复核四端 `Player_renderPreparedItemsToD3DTexture_guess`：

| 目标 | 函数 | getter invoke | descriptor reload | rect 正尺寸测试 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6AB39C` | `0x6AB530` | `0x6AB534` | `0x6AB568` |
| Android armv7 | `0x57D3DC` | `0x57D548` | `0x57D54A` | `0x57D568` |
| iOS arm64 | `0x100104450` | `0x1001045D4` | `0x1001045D8` | `0x100104604` |
| iOS armv7 | `0x101850` | `0x101DD4` | `0x101DEA` | `0x101E12` |

四端共同顺序为：

```text
texture = sourceGetter(item)
source  = item.sourceState
if(source.textureRect.right <= source.textureRect.left ||
   source.textureRect.bottom <= source.textureRect.top)
    skip item
```

64 位 rect 位于 descriptor `+96..+108`；Android 32 位为 `+88..+100`，iOS 32 位为
`+84..+96`。这些是 Variant/STL ABI 差异；共同源结构都是同一 `SourceState::textureRect`。
四端在 descriptor reload 前后均无 pointer test，也没有读取 prepared item 的另一个 rect、
测试 texture 尺寸或根据 texture 生成 `{0,0,w,h}`。

## 3. PrivateMotionGLL：直接复制 rect，null texture 仍入队

fresh 复核四端 command builder 与 RenderItem construction：

| 目标 | command builder | rect copy | texture resolve / unconditional append |
|---|---:|---:|---:|
| Android arm64 | `0x6DBB18` | inline `0x6DBFA0` / `0x6DC0B0` | `0x6DBF3C..0x6DBF4C` / append continues |
| Android armv7 | `0x59CB20` | ctor `0x59D5AC..0x59D5B0` | `0x59CD32..0x59CD48` |
| iOS arm64 | `0x10012B7D0` | ctor `0x10012CBD0` | `0x10012BCAC..0x10012BCC0` |
| iOS armv7 | `0x12A304` | ctor `0x12B556..0x12B55E` | `0x12A8C8..0x12A8D6` |

所有 element constructor 都从 `item.sourceState` 直接复制 16-byte rect。source Layer 或
main image 解析为空时 texture local 为零，但控制流仍进入 deque append/element ctor；ctor
把 texture owner 槽先写 null，只在非 null 时 AddRef。rect copy 不受 texture 是否为空影响。

这与 `Draw_GPU` 的独立边界相配：Draw 遍历时遇到 null source texture 会 `break` 整个
command loop。builder 不能提前丢弃 element，也不能用 `{0,0,0,0}` 或 texture dimensions
替换它携带的 descriptor rect。

## 4. 本地死状态判定与源码落地

删除前本地 prepared-item snapshot 的完整引用闭包是：

1. ordinary item population 在 `hasOwnSource` 分支复制 `node.source.texture` 与
   `node.source.textureRect`；
2. `sourceRectForItem` 在 `sourceState` 为空时尝试该 snapshot；
3. snapshot 不被 diagnostics/tests 或其它 renderer 读取，sourceState 正常 publication
   又保证该 fallback 不应可达。

四端精确 native item allocation/destruction 纵切已经证明 owner core 后没有第二份 texture
或 rect tail storage。现在：

- `PreparedRenderItem` 不再保存 `sourceTexture/sourceRect`；
- ordinary population 不再写这两个 snapshot；
- shared D3D 在 getter 返回后直接读 `item.sourceState->textureRect`；
- PrivateMotionGLL queue 无条件复制 descriptor rect，并把可能为 null 的解析结果写入
  queue input 后照常 append。

绝对地址只记录在本文与 recovery IDB，不进入编译源码注释。未知原始 identifier 继续使用
语义 `_guess` 名。

## 5. IDB 与验证

四端 raw renderer 与 PrivateMotionGLL command builder 共 8 个入口都已补充
“descriptor rect is sole authority / null texture still appends”注释；8 个函数全部完成
Hex-Rays cache invalidation，并从重新反编译文本直接回读到新注释。四份 recovery IDB
随后均原位保存成功。

源码残留与结构检查：

- `sourceRectForItem`、ordinary `entry.sourceTexture`、`entry.sourceRect` 均为零命中；
- `PreparedRenderItem` 已无 `sourceTexture/sourceRect` 字段；保留的同名字段只属于
  `PrivateMotionGLLRenderItem`，它们是四端真实 queue-element owner state；
- Private queue 的两个连续 publication statement 是
  `queueItem.sourceRect = item.sourceState->textureRect` 与
  `queueItem.sourceTexture = sourceTexture`，外部没有 texture-null gate；
- shared D3D getter 后的 rect 读取是直接
  `item.sourceState->textureRect`。

编译验证：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` syntax-only 编译均通过，仅报告既有 `_tss`
  literal-operator 弃用 warning；
- Web Debug `motionplayer` archive `32/32`、Wasmtime Headless Debug
  `motionplayer` archive `32/32`、完整 Web Debug 最终构建/链接 `3/3` 全部成功；
- scoped `git diff --check` 返回 0；输出只有工作区 LF/CRLF policy 提示。
