# MotionPlayer post-draw internal Layer 诊断隔离（四参考二进制，2026-08-14）

## 1. 结论

普通 post-draw 与 accurate-SLA post-draw 在四端都是两个独立小函数：

- `Player_updateLayerAfterDraw_guess`：snapshot producer flag，false 早退；true 时 materialize
  internal/work Layers，然后 `internal.assignImages(originalTarget)`；
- `Player_updateAccurateSLAAfterDraw_guess`：同样 snapshot producer flag；true 时持有 target/
  internal accessors、按 height-before-width 读取尺寸，再
  `internal.piledCopy(0,0,target,0,0,width,height)`。

两个函数都没有 motion-path conversion、Web trace/logger/fmt 或 snapshot side effects。尤其是
producer false path，native 只写 consumer-ready flag并立即返回，不读取 target、motion context
或 Layer owner。

本地旧代码在普通 true path 无条件转换 path；accurate helper 更在 producer flag snapshot
之前无条件转换 path，false early return 仍带来 Variant-to-string 和潜在分配/异常。现在两者都
只在 Web trace 总开关开启时物化 path，所有 log/check/fmt 均位于缓存的 path-specific gate。

## 2. 四端函数映射与统计

### 2.1 普通 assignImages post-draw

| 目标 | function | size | instructions | direct-call instructions | string refs |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x6CBBB8` | `0x160` | 88 | 13 | 0 |
| Android armv7 | `0x59327C` | `0x8A` | 57 | 9 | 0 |
| iOS arm64 | `0x10011E6CC` | `0xF4` | 59 | 7 | 0 |
| iOS armv7 | `0x11CF20` | `0xF4` | 90 | 10 | 0 |

共同可识别 calls 只有：

```text
Player_materializeInternalRenderLayers_guess
Variant/accessor construction and destruction
virtual TJS assignImages dispatch
platform stack/unwind helpers
```

### 2.2 accurate-SLA piledCopy post-draw

| 目标 | function | size | instructions | direct-call instructions | production string refs |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x6CBD18` | `0x3F8` | 253 | 36 | 2 |
| Android armv7 | `0x593344` | `0x1AA` | 169 | 23 | 0 |
| iOS arm64 | `0x10011E808` | `0x278` | 147 | 20 | 0 |
| iOS armv7 | `0x11D078` | `0x286` | 238 | 25 | 0 |

Android arm64 把部分 production wide member strings 显式计入 xrefs；其余平台通过不同
literal/accessor 形状表现。共同 call-set 只有 Variant/accessor owner、internal-layer
materialization、两次 integer property read、piledCopy virtual dispatch 和清理/unwind；没有
path/string-format/logger/trace helper。

## 3. sidecar string 排除

按 ASCII/UTF-8、UTF-16LE、UTF-32LE 与 IDA string search 搜索：

- `post.assignImages`
- `post.sla.accurate`
- `materialize internal/work Layers`
- `internal.piledCopy`
- `sub_6CE7D8`
- `sub_6CE938`

四端所有组合均为 0 matches。`piledCopy` 作为生产 method name 可存在；这里精确搜索的是本地
diagnostic sentence `internal.piledCopy`，避免混淆 native dispatch string 与 Web log text。

## 4. producer/consumer flag 数据流

两条 helper 的第一个业务效果都是：

```text
player.internalRenderLayerReady = player.needsInternalAssignImages
if !player.needsInternalAssignImages:
    return true
```

`_internalRenderLayerReady` 是 consumer-side snapshot，producer `_needsInternalAssignImages` 不在
这里清除。普通 false path在任何 target owner/materialize 前返回；accurate false path同样不
构造 target accessor。

Web diagnostic flag query是 port sidecar，不写 Player state。源码将普通 helper 的 trace gate
放在 producer false return之后，因此默认/false path完全没有 path work；accurate helper为保留
可选 false-path trace，只在全局 trace开启时物化 path，默认仍与 native early return一致。

## 5. true path owner/call order

普通 helper 保持：

```text
materializeInternalRenderLayers(target)
internal accessor owns internal Layer Variant
internal.assignImages(target as objthis/original target)
destroy accessor
return true
```

accurate helper 保持：

```text
target accessor
materializeInternalRenderLayers(target)
internal accessor
height = target.height
width  = target.width
internal.piledCopy(0,0,target,0,0,width,height)
destroy internal, target and argument Variant temporaries in native order
return true
```

本轮没有交换 width/height reads，没有缓存 native Layer dimensions，也没有改变 Variant argument
types或 ignored dispatch result边界。

## 6. 本地诊断隔离与注释迁移

普通 helper 的 `post.assignImages` check 只在 `traceForPath` 内执行。accurate helper 的 false
log 与 true `fmt::format + check` 也分别受同一缓存 Boolean 控制；默认 path不再创建 expected
string。

诊断 `func`/root-cause 文本从旧 `0x6CE7D8`、`0x6CE938`、`sub_...` 改成
`Player.updateLayerAfterDraw` / `Player.updateAccurateSLAAfterDraw` 语义名。精确四端地址只保留
在本文映射表和 recovery IDB，不继续写入 compiled source；同一函数的 headless checkpoint
sample labels 也移除了旧单目标地址后缀。

## 7. recovery IDB 回写

四份普通 post-draw function comment 已记录 zero-string、完整 call-set 和
`flag snapshot -> false return -> materialize -> assignImages` 顺序，以及 native 无 path/log。

四份 accurate function comment 已记录各端 instruction/call/string 统计、production
dimension/piledCopy call-set，以及六个 sidecar terms 的全编码零命中。四份 recovery IDB 已原位
保存。

## 8. 验证

- 完整 motionplayer Catch2 translation unit 的 Emscripten syntax-only 检查通过，仅有既有
  `_tss` warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerRenderTargets.cpp` 并成功链接最终
  Web/Wasm 输出；
- source scan 确认普通 path 位于 producer true 分支，accurate path 受 trace 总 gate控制，
  两个 fmt/check 均不会在默认路径求值；
- `git diff --check` 在文档与计划写入后执行，结果记录在本轮状态。

该纵切面只恢复 post-draw wrapper 边界；`Player_materializeInternalRenderLayers_guess` 自身的
Layer creation/owner rollback 已有独立四端函数，应继续按其专门证据维护。
