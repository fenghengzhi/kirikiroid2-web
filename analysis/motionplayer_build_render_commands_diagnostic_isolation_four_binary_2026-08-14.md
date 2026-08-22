# MotionPlayer buildRenderCommands 诊断隔离（四参考二进制，2026-08-14）

> **后续更正（2026-08-16）**：本文第 6 节中“null main item skip 保持”与“返回值仍为 `!mainList.empty()`”只记录了当时尚未复核的端口状态，已被后续四参考证据推翻。当前 common builder 是 `void`，无 target/空 main 提前返回，且 main/aux/child pointer vectors 均为可信紧密序列。以 `motionplayer_command_builder_void_target_clip_four_binary_2026-08-16.md`、`motionplayer_common_builder_trusted_pointer_vectors_four_binary_2026-08-16.md` 与 `motionplayer_common_builder_leaf_owner_void_natural_failure_four_binary_2026-08-16.md` 为准。
>
> **后续更正（2026-08-17 / V185）**：production builder 还包含两类独立的按需脚本链：
> lazy SLA 首次构造一次，以及每个首次物化的 group composed Layer 一次。每条链重新求值
> `Window.mainWindow`，再以 strict accessor 和 shared exact hint 读取 `primaryLayer`；函数入口
> 不预取或缓存 scratch owner/parent。见
> `motionplayer_build_render_commands_primary_layer_on_demand_hint_lifecycle_four_binary_2026-08-17.md`。

## 1. 结论

四个当前参考二进制的 `Player_buildRenderCommands_guess` 确实含有生产字符串：它需要通过
TJS/Layer API 设置 source descriptor、尺寸、neutral color、mesh/bezier copy 等成员。因此，
这里不能沿用“native function 为 0 string refs”这一简化判据。

本轮同时使用三组证据闭合诊断边界：

1. 四端函数内 string refs 和完整 direct-call scan；
2. 对本地 Web sidecar 独有文本执行 ASCII/UTF-8、UTF-16LE、UTF-32LE 全二进制搜索；
3. 对源码中的 per-item projection、failure string、motion-path 和 snapshot control flow 逐项
   与 native call/data flow 对照。

结果一致：native command builder 没有 motion-path conversion、trace/logger/fmt/fprintf、
SNAPCMD、failure-reason owning string，也没有为了核对 local corners 而重复计算第二份
`expectedLocalCorners`。这些都是 Web 诊断 sidecar，必须只在 opt-in gate 内执行。

本轮恢复后的普通路径只保留 native 的 clip、local-corner/local-mesh、SeparateLayerAdaptor leaf
materialization、group compose/alpha-mask 和 retired-tree cleanup。诊断关闭时不再转换 motion
context，不再为失败原因格式化 string，也不再对每个 drawable item 建立/比较重复 corner
projection。

## 2. 四端 fresh mapping

| 目标 | function | size | instructions | direct-call instructions | production string refs |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x6C2208` | `0x1BAC` | 1766 | 244 | 12 |
| Android armv7 | `0x58C7C4` | `0x104A` | 1348 | 153 | 14 |
| iOS arm64 | `0x1001167BC` | `0x1198` | 1083 | 131 | 9 |
| iOS armv7 | `0x114118` | `0x12C4` | 1582 | 154 | 19 |

不同编译器对 TJS member string、Layer dispatch 和 leaf/group helper 的内联程度不同，因此
instruction/call/string-ref 数量不要求相等。四端共同可识别的生产调用包括：

```text
SeparateLayerAdaptor_ctor
SeparateLayerAdaptor_resolveLayerNode_guess
SeparateLayerAdaptor_createLayerVariant_guess
Player_resolveRenderSource_guess
ncbPropAccessor / tTJSVariant integer and copy operations
MeshPointVector copy/assign
Motion_doAlphaMaskOperation_guess
SeparateLayerAdaptor_clearRetiredLayers_guess
floorf / ceilf and allocation/unwind helpers as required by ABI
```

四份完整分页 call scan 均没有 `printf/fprintf`、fmt、logger、trace、snapshot、path、
Variant-to-string、narrow、stack/backtrace 或 basic/ostringstream 调用。

## 3. 宽字符串/字节搜索

按 `ida-search-string` 工作流，在四个完整参考目标中同时搜索下列本地 sidecar 文本：

- `SNAPCMD`
- `renderItem.clip`
- `m2logo.mtn`
- `invalid_intersection`

每个 term 都执行：

- IDA string search；
- ASCII/UTF-8 byte pattern；
- UTF-16LE byte pattern；
- UTF-32LE byte pattern。

四端、四个 term、三种 byte width 的结果全部为 0 matches。该结论不是说 native 没有任何
字符串，而是精确地区分了生产 TJS member strings 与 Web sidecar 独有 strings。

## 4. 本地旧偏差

### 4.1 无条件 parent path

旧入口在任何 build pass 都调用 `matchedMotionPath()`，把 persistent motion-context Variant
转换成 owning `std::string`。native function 没有这条数据流。

现在先缓存 `logoChainTraceEnabled()` 与 `logoSnapshotMarkEnabled()`；只有至少一个总开关为
true 才物化 path，再分别计算 `traceForPath` / `snapshotForPath`。传给 leaf/group 辅助函数的
是 `traceMotionPath`：trace 未命中时引用一个本地空串，snapshot 单独开启不会让每个 leaf/mask
进入 path-specific trace formatting。

### 4.2 per-item failure string

旧代码为每个 main item 无条件 default-construct `clipFailureReason`，并总是把非 null pointer
传给 `computeRenderClipRect`。clip 无效时 helper 即使 trace 完全关闭也会执行嵌套
`fmt::format`。

现在使用 `optional<string>`，只在 `traceForPath` 时 emplace；普通路径向 clip helper 传
`nullptr`，只计算 geometry/validity Boolean。失败后的两组 fmt strings 和 trace check 也整体
位于同一 Boolean 内。

### 4.3 duplicate expected-corner projection

local corners 的第一次计算是生产数据，必须保留。旧代码随后无条件再次计算
`expectedLocalCorners[8]`、逐元素 fabs compare 并生成 `cornersOk`，这些值仅供 trace。

第二份 projection 和两组 trace checks 现在完整位于 `traceForPath` 分支；普通 drawable item
只执行一次 native local-corner translation。

### 4.4 tail snapshot/count

SNAPCMD main-list scan 现在使用缓存的 `snapshotForPath`；最终 count log 只在
`traceForPath` 时调用。两者仍位于 native retired-layer cleanup 之后，避免在显式正常清理点前
增加 Web formatting/allocation 异常边界。

## 5. 源码结构命名

与本纵切面同时完成两个局部 helper 的旧单目标地址名迁移：

- `emitLeafLayerCopyLike_0x6C4E28` → `emitPreparedLeafLayerCopy_guess`
- `composeGroupLayersLike_0x6C4E28` → `composePreparedGroupLayers_guess`

声明、定义和调用点均同步。相邻 command-builder 注释中的旧 `libkrkr2.so`/单地址叙述改为
四参考共同语义：drawable-only clip materialization、fresh layer-id latch、leaf build、group
compose 和 normal-tail retired cleanup。仍未在本纵切面验证的更下层 Layer-call helper 名没有
借机猜测改名。

## 6. 未改动的 production 行为

- main/aux/child pointer vectors 作为可信紧密序列使用，无本地 null skip；
- drawFlag/rawFlag16 admission；
- paintBox/camera/viewport intersection 和 float boundary；
- clipRect/dirtyRect publication；
- persistent SeparateLayerAdaptor lazy create/begin/end pass；
- lazy SLA 门和各 group Void 门分别按需求值 `Window.mainWindow.primaryLayer`，不做入口预取；
- no-argument fresh render-layer id acquisition；
- local corner/local mesh point construction；
- leaf source resolution、Layer copy 与 item-owned Variant publication；
- aux group union、composed Layer creation/clear、child alpha-mask order；
- normal tail 对 retired tree 的 invalidation/destruction；
- common builder 返回 `void`，没有 target/空 main 提前返回。

## 7. recovery IDB 回写

四份 recovery IDB 的 command-builder function comment 已写入各端 fresh instruction/call/string
统计、四个 sidecar term 的全编码零命中、failure string/duplicate corners/motion path 的缺席。

四端 `SeparateLayerAdaptor_clearRetiredLayers_guess` call site 还记录：它是 normal-flow native
tail，Web diagnostics 必须留在其后，且默认路径不能在此前增加 allocation/throw boundary。
四份 IDB 已原位保存。

## 8. 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax-only 检查通过；只有
  仓库既有 `_tss` literal-operator deprecated warning；
- `cmake --build --preset "Web Debug Build"` 因 `Player.h` helper rename 重编 31 个步骤，
  成功链接 `libmotionplayer.a` 和最终 Web/Wasm 输出；输出仅含仓库既有 warning；
- source scan 确认 parent path 受总 gate 控制、failure string 与 expected-corner projection 受
  `traceForPath` 控制、SNAPCMD 受 `snapshotForPath` 控制；
- `git diff --check` 在文档与计划写入后执行，结果记录在本轮状态。

本纵切面只闭合 command-build phase。后续 `executeLayerRenderCommands` 对应的是更大的 native
render function 内部阶段，需另做四端边界映射，不能因为同处一个 C++ 文件而沿用本结论。
