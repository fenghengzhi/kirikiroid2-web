# MotionPlayer canvas submit phase 诊断隔离（四参考二进制，2026-08-14）

> **后续更正（2026-08-16；V246 于 2026-08-18 再更正）**：本文第 5/6 节把若干当时“未改动”的本地行为误写成 native contract。后续四参考复核已经确认：canvas wrapper 是 by-value `void` 数据流；旧 target/content early returns、`builtRect`/`executedDirect` production publication 与 Boolean return 均非参考实现；submit main vector 也不允许 null slot recovery。V246 又证明本文所称 `lastCanvas publication` 同样不存在：Canvas final `setClip` 后直接清理 target/Layer owners，Player ctor/dtor 的对应前置区域是 POD 而非 Variant。以 `motionplayer_canvas_by_value_void_post_draw_four_binary_2026-08-16.md`、`motionplayer_canvas_submit_trusted_main_pointer_vector_four_binary_2026-08-16.md` 与 `motionplayer_canvas_final_reset_outer_owner_no_lastcanvas_four_binary_2026-08-18.md` 为准。本文关于诊断 sidecar 隔离的主体结论仍有效。

## 1. 函数边界结论

本地 `renderToCanvas_guess` 与 `executeLayerRenderCommands` 不是四端 native 中两个并列的独立
函数。它们共同拆分了一个 native `Player_renderToCanvas_guess`：外层负责 Layer class/target
owner、canvas width/height和priorDraw build gate；内部 helper 负责
逐 item clip/source/direct-or-buffered submit、ancestor mask 和最终 no-arg `setClip`。

因此本纵切面以四个完整 native canvas functions 为证据边界，而不是拿本地 helper 名去猜一个
不存在的四端 function start。旧源码中 `sub_6C7440` 一类单目标地址说明不再用于结论，相关
submit 注释已改成四端共同语义。

四端完整函数都含 Layer/TJS 的生产 member strings 与 dispatch，但没有 motion-path、Web
trace/snapshot、logger、fmt/fprintf、owning diagnostic branch string 或 RGBA log projection。
本轮将这些 sidecar 数据流从默认 canvas 热路径中隔离。

## 2. 四端 fresh mapping

| 目标 | `Player_renderToCanvas_guess` | size | instructions | direct-call instructions | production string refs |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x6C4820` | `0x2578` | 2363 | 350 | 21 |
| Android armv7 | `0x58E2CC` | `0x1806` | 1891 | 226 | 20 |
| iOS arm64 | `0x1001186E0` | `0x18E0` | 1531 | 197 | 10 |
| iOS armv7 | `0x11653C` | `0x1AEA` | 2155 | 227 | 20 |

完整 100-instruction pagination 覆盖全部函数。共同可识别的生产 calls 包括：

```text
Player_buildRenderCommands_guess       // only under native priorDraw gate
Player_resolveRenderSource_guess       // one shared source owner per item
ncb/TJS Variant/accessor operations
Layer property/copy/operate dispatch helpers
Motion_doAlphaMaskOperation_guess      // buffered ancestor chain
floorf/ceilf, unsigned division, unwind helpers as ABI requires
```

四端 call-set 均不含 `basic_string`/ostringstream、Variant-to-string/narrow、printf/fprintf、fmt、
trace/logger/snapshot/path 或 stack/backtrace helper。

## 3. submit phase landmarks

fresh disassembly 还对齐了三个共同 landmark：

| 目标 | conditional build call | shared source resolve | buffered mask call | final no-arg `setClip` materialization |
|---|---:|---:|---:|---:|
| Android arm64-v8a | `0x6C494C` | `0x6C4E70` | `0x6C5770` | `0x6C63B8` |
| Android armv7 | `0x58E3A4` | `0x58E5C8` | `0x58EFD2` | `0x58FA70` |
| iOS arm64 | `0x1001187E4` | `0x1001189FC` | `0x100119338` | `0x100119F14` |
| iOS armv7 | `0x1166AC` | `0x116DAE` | `0x117924` | `0x117F84` |

这些 landmark 证明本地 execute helper 是同一 native function 的 submit subregion：final
`setClip(argc=0)` 位于完整 item walk 之后，随后 owner reverse destruction/return；它不是
accurate-SLA 大函数的尾部，也不拥有单独 native ABI。

## 4. 全编码 sidecar string 排除

按 `ida-search-string` 工作流，对四个完整参考目标搜索：

- `SNAPCOPY`
- `execute.copy`
- `execute.setClip`
- `execute.source`
- `m2logo.mtn`
- `direct.operateAffine`
- `buffered.bufLayer.operateRect`

每个 term 都执行 IDA string search 与 ASCII/UTF-8、UTF-16LE、UTF-32LE byte search。四端所有
组合均为 0 matches。注意 native 确实有生产宽字符串 `operateAffine`、`operateRect`、
`setClip` 等；精确搜索含 `direct.`/`buffered.`/`execute.` 前缀，是为了排除 Web diagnostic
labels 而不误排生产 method names。

## 5. 本地恢复

### 5.1 外层 wrapper

`renderToCanvas_guess` 现在只在 `logoChainTraceEnabled()` 后物化 parent motion path，再缓存
path-specific gate。canvas-size trace 与 final summary 完整受该 Boolean 控制。Layer class owner、
target owner、width-before-height query、draw-region clear、priorDraw build gate和submit helper
顺序保持不变；V246 已删除当时误保留的 `lastCanvas publication`。

### 5.2 submit helper path control domain

`executeLayerRenderCommands` 同时缓存 trace/snapshot 总开关；二者都关闭时不调用
`matchedMotionPath()`。`traceForPath` 与 `snapshotForPath` 各自计算一次，30–50 frame 的
`m2logo.mtn` snapshot window 也只计算一次。

传给 `applyMotionAlphaMask_guess` 的是 `traceMotionPath`：trace 未命中时引用本地空串。这样
snapshot 单独开启不会让 buffered ancestor 的每个 mask operation携带真实 path 进入 trace
helper；生产 alpha modification/update order 不变。

### 5.3 per-item diagnostic projection

以下工作现只在 `traceForPath` 时执行：

- target `setClip` trace；
- resolved source pointer/size trace；
- direct/buffered `execute.copy` formatting；
- `unpackPackedRgba(item.packedColors[0])` 的四整数 diagnostic projection。

此前 RGBA unpack 对每个 admitted item无条件执行，但结果只供两个 log。native submitter直接
传递四个 packed color weights，不建立该解包数组。

### 5.4 branch 与 snapshot

旧 direct branch 使用 `std::string branch("direct.operateAffine")`，即使 trace/snapshot/headless
probe 全部关闭也会构造 owning string；mesh/bezier 分支随后再赋值。现在它是指向静态 literal
的 `const char *`，既保留日志/快照/probe 标签，也删除默认 per-item string 生命周期和潜在
allocation/throw boundary。

两处 SNAPCOPY 都使用缓存的 `snapshotWindow`，只在命中时增加 `snapshotCopyOrder` 和输出。
direct/buffered draw、outline/meshline frame、continue/break 位置均未移动。

### 5.5 local lambda/comment migration

本地 `applyTargetLayerClipLike_0x6C7440` 改名为 `applyTargetLayerClip_guess`。相邻 compiled-source
注释不再引用旧 `libkrkr2.so` 绝对地址，改为描述四端共同的 ordered/unordered compare、Real
setClip、owner construction order、direct mesh clear flag、buffer bounds、ancestor mask walk 和
final clip reset。更下层尚未在本纵切面独立映射的 Layer-call helper 名保持不动。

## 6. 未改动的 production 数据流/边界

- target/content early gates 与 native Layer class/target owner lifetime；
- priorDraw 对 draw-region clear、build pass、skipFlag1 和 opacity `/2` 的控制；
- viewport/paint-box clip 与 NaN ordered-comparison behavior；
- no-positive-canvas-size gate；
- descriptor/color/source/source-accessor construction与 reverse destruction；
- 每 item 一次 shared source resolve 及 width-before-height reads；
- completion/blend/direct-path selection；
- affine/mesh/bezier dispatch、point offsets、division conversion；
- buffered ResourceManager → bufLayer Variant → buffer accessor 三 owner nesting；
- parentItem ancestor 顺序、mask Variant copy owners、fillRect error-ignore/break；
- draw-region union、item builtRect/executedDirect publication；
- final no-argument `setClip`，以及 submit phase 不调用 Layer.Update；
- outer target/Layer owner cleanup and return behavior；不存在 lastCanvas publication。

## 7. recovery IDB 回写

四份 recovery IDB 的完整 canvas function comment 已记录各端 instruction/call/string 统计、七个
sidecar term 的全编码零命中，以及完整 call-set 无 path/format/logger 的结论。

四端 final `setClip` landmark 还记录：Web diagnostics 不得给默认 per-item路径增加 owning
string、RGBA projection 或 alpha-mask path conversion。四份 IDB 已原位保存。

## 8. 验证

- 修改后的完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax-only 检查
  通过；只有仓库既有 `_tss` warning；
- `cmake --build --preset "Web Debug Build"` 重编 `PlayerRenderExecute.cpp` 与
  `PlayerRenderTargets.cpp`，成功链接 `libmotionplayer.a` 与最终 Web/Wasm 输出；
- source scan 确认两层 parent path 都受总 gate 控制，RGBA unpack/log 受 trace gate 控制，
  SNAPCOPY 受缓存 window 控制，direct branch 已无 owning string；
- `git diff --check` 在文档/计划写入后执行，结果记录在本轮状态。

本纵切面不把 accurate-SLA renderer 与普通 canvas submitter合并；前者在四端都有另一独立
函数和不同 Layer-map/data flow，应继续作为单独恢复对象。
