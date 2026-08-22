# SeparateLayerAdaptor assign `type/left/top` shared-hint 边界的四参考复原（2026-08-17）

## 结论

四个参考二进制一致表明，`SeparateLayerAdaptor_assignFromAdaptor_guess` 从 source Layer
读取 `type`、`left`、`top` 时故意传 null member hint，而向 target Layer 发布相同三个值时，
分别复用插件既有的 process-wide `typeMemberHint_guess`、`leftMemberHint_guess`、
`topMemberHint_guess`。这不是三个新 global，也不是 assign 私有的三个 function-static
cache：

- `type` 接入先前已经由 frame parser、`Player::playImpl`、accurate renderer、
  `calcViewParam`、`skipToSync` 与 `getCommandList` 证明的全局 type family；
- `left/top` 接入 `width/height/left/top/right/bottom` 六槽 geometry family；
- 三个 source `PropGet` 的 hint 都仍是 null，不能因为 target 共用 global 就反向接线；
- 三个 target `PropSet` 都使用 `TJS_MEMBERENSURE`（512）、target 自身作为 `objthis`，并忽略
  HRESULT 后继续执行。

因此本轮是对两个既有全局家族补齐一个遗漏 consumer，而不是依据同名 literal 新建或合并
storage。原符号已 strip，恢复名继续遵守 `_guess`。

## 精确数据与函数映射

| 目标 | assign function / entry | `type` word | `left` word | `top` word |
|---|---:|---:|---:|---:|
| Android arm64 | independent entry `0x6A97F0`，IDA merged owner `0x6A965C` | `0x1AB5124` | `0x1AB5224` | `0x1AB5228` |
| Android armv7 | `0x57C814` | `0x1111658` | `0x1111758` | `0x111175C` |
| iOS arm64 | `0x10010347C` | `0x101B695EC` | `0x101B696EC` | `0x101B696F0` |
| iOS armv7 | `0x100874` | `0x187D31C` | `0x187D41C` | `0x187D420` |

三个 word 在 V167/V169 等纵切面中已经建立为独立 size-4 data item 并完成全局 identity
恢复，本轮没有重复 make-data 或重命名。尤其不能从单个 assign 函数里的调用相邻关系推断
data adjacency：四端 `type -> left` 都相隔 `0x100`，但 `left -> top` 才是紧邻的 4-byte
geometry pair；它们在源级属于不同的语义 family。

## raw xref 与 assign-local operand

全局 data 的 raw xref 总数包含其它已知 consumer；assign 只贡献下列 materialization：

| 目标 | property | raw data xrefs | assign-local xref / target call |
|---|---|---:|---|
| Android arm64 | `type` | 16 | `0x6A9E40`, `0x6A9E54` / `0x6A9E5C` |
| Android arm64 | `left` | 14 | `0x6A9EEC`, `0x6A9F00` / `0x6A9F08` |
| Android arm64 | `top` | 14 | `0x6A9FA4`, `0x6A9FB8` / `0x6A9FC0` |
| Android armv7 | `type` | 24 | `0x57CA04`, `0x57CA0E` / `0x57CA16` |
| Android armv7 | `left` | 30 | `0x57CA28`, `0x57CA32` / `0x57CA3A` |
| Android armv7 | `top` | 27 | `0x57CA4C`, `0x57CA56` / `0x57CA5E` |
| iOS arm64 | `type` | 8 | `0x100103758` / `0x100103760` |
| iOS arm64 | `left` | 10 | `0x100103788` / `0x100103790` |
| iOS arm64 | `top` | 9 | `0x1001037B8` / `0x1001037C0` |
| iOS armv7 | `type` | 22 | `0x100B26`, `0x100B2C`, `0x100B34` / `0x100B44` |
| iOS armv7 | `left` | 24 | `0x100B64`, `0x100B6A`, `0x100B72` / `0x100B82` |
| iOS armv7 | `top` | 21 | `0x100BA2`, `0x100BA8`, `0x100BB0` / `0x100BC0` |

32 位端一处地址通常由多条指令共同 materialize；表中的 raw 数不是 consumer 数，也不能把
一组 `MOVW/MOVT/ADD` 拆成多个源级调用。反过来，全局 raw xref 大于 assign-local xref 正是
这些 word 跨函数、跨路径共享，而非 assign 私有 storage 的直接证据。

## Android arm64 merged-function 盲区

Android arm64 的独立 assign entry 被 IDA 合入更早的 owner，Hex-Rays 不能稳定呈现整个
尾部。因此该端不以残缺伪代码补猜参数，而用 fresh disassembly 对每个 getter/setter 成对
回读：

| property | source getter 参数清零 | getter call | target word materialization | target call |
|---|---:|---:|---:|---:|
| `type` | `0x6A9E00` / `0x6A9E04` | `0x6A9E08` | `0x6A9E40` / `0x6A9E54` | `0x6A9E5C` |
| `left` | `0x6A9EAC` / `0x6A9EB0` | `0x6A9EB4` | `0x6A9EEC` / `0x6A9F00` | `0x6A9F08` |
| `top` | `0x6A9F60` / `0x6A9F64` | `0x6A9F68` | `0x6A9FA4` / `0x6A9FB8` | `0x6A9FC0` |

三组 A64 指令是双阶段 getter 的第二次读取：`MOV W3, WZR` 与 `MOV X4, XZR` 对应 flags 0
和 hint null；它们之前还各有一次 `MEMBERMUSTEXIST`/null-hint probe，后由 V183 闭合。三组
target call 前则分别形成精确 `type/left/topMemberHint_guess@PAGE/@PAGEOFF` 地址。其余三端
fresh decompile 显示 getter helper 第三实参 0；V183 更正其语义为 caller default 0，而不是
hint 参数，并通过 helper body 证明内部 probe/read 两次 hint 都为 null。四端证据仍共同闭合
source/target split，没有为了得到整洁伪代码而虚构 A64 函数边界。

## 归一化数据流与调用顺序

每个 source active entry 先由 payload-bearing resolve 取得 target Layer；该 resolve 已经发布
一次 sequence-based `absolute` 与一次 `hitThreshold=256`。assign 随后的主干为：

```text
ignore target.assignImages(sourceLayerVariant)
height = source.GetInt(default=0, "height")  // probe 1024/null, then read 0/null
width  = source.GetInt(default=0, "width")   // same two-stage protocol
ignore target.setSize(width, height, sharedSetSizeHint)

absolute = source.GetInt(default=0, "absolute") // each uses the same
visible  = source.GetInt(default=0, "visible")  // two-stage null-hint protocol
opacity  = source.GetInt(default=0, "opacity")
type     = source.GetInt(default=0, "type")
left     = source.GetInt(default=0, "left")
top      = source.GetInt(default=0, "top")

ignore target.PropSet(MEMBERENSURE, "absolute", rebasedAbsolute,
                      &absoluteMemberHint_guess)
ignore target.PropSet(MEMBERENSURE, "visible", visible,
                      &visibleMemberHint_guess)
ignore target.PropSet(MEMBERENSURE, "opacity", opacity,
                      &opacityMemberHint_guess)
ignore target.PropSet(MEMBERENSURE, "type", type,
                      &typeMemberHint_guess)
ignore target.PropSet(MEMBERENSURE, "left", left,
                      &leftMemberHint_guess)
ignore target.PropSet(MEMBERENSURE, "top", top,
                      &topMemberHint_guess)
```

固定 target write order 是 `absolute -> visible -> opacity -> type -> left -> top`。共享 hint
identity 不授权把 setter 合批、按 data 地址排序、跳过重复值，或让 source getter 预热同一个
cache。dispatch 能观察每次调用的顺序、flags、hint 地址、value 与 `objthis`。

## status、对象生命周期与异常边界

三个 target write 都把 non-null Integer `tTJSVariant *` 传给 target dispatch，并把同一 target
同时作为 receiver 与 `objthis`。`PropSet` HRESULT 被忽略；即使 `type` write 返回失败，
`left/top` 仍继续，assign 的普通尾部仍返回 `TJS_S_OK`。本轮没有增加 status branch、catch、
transaction 或回滚。

三个 hint word 只保存 TJS dispatch cache state，不拥有 source/target dispatch、accessor、
Variant 或 map node。每 entry 的 target accessor 仍先于 source accessor 释放，局部 target/source
Variant owner 仍在其后析构；retired tree 仍只在完整循环正常尾部 `clear(true)`。若 conversion
或 dispatch 抛异常，已经完成的 resolve/map/property publication 不回滚，依旧保留 native
渐进提交边界。

## portable 源码与探针

本轮只修改缺失的 consumer wiring：

1. `SeparateLayerAdaptor.cpp`
   - target `type` publication 传 `&detail::typeMemberHint_guess`；
   - target `left/top` publication 分别传全局 geometry words；
   - 六个 source property getter 当时全部保持默认/null hint；后续 V183 进一步恢复其
     `MEMBERMUSTEXIST probe -> flags=0 read` 双阶段调用；
   - 没有新增 global definition 或改变 property helper ABI；
2. `tests/unit-tests/plugins/motionplayer-dll.cpp`
   - 将现有 probe 明确为 `SeparateLayer assign reuses shared publication hints`；
   - 当时锁定 source member 顺序与 null hint；后续 V183 将 flags 覆盖加强为每项
     `TJS_MEMBERMUSTEXIST, 0` 成对调用；
   - 锁定 target `type=4`、`left=-7`、`top=11` 的 exact shared pointer、
     `TJS_MEMBERENSURE` 和 `objthis=target`；
   - 保留 target 拒绝调用后仍继续到 `top`、最终返回成功的 failure-through 覆盖。

compiled source comments 只记录语义与 global family，不嵌入参考库绝对地址。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery` 均已完成：

- 三个地址在四库中已由更早纵切面命名和 data-harden，本轮没有重复建立 12 个 data item；
- 每库写入一条 assign function/entry 注释和三条 target call-site 注释，共 16 条；
- 每库增加一个 V182 assign source-null/target-shared bookmark，共 4 个；
- 每库对 assign owner/entry force-recompile 一次，共 4 次；
- Android armv7、iOS arm64、iOS armv7 fresh decompile 回读 getter default 0 与三个 exact
  target symbol；V183 随后通过 getter helper body 闭合内部两次 null hint；Android arm64
  fresh disassembly 回读第二阶段 XZR hint 参数和三个精确 global；
- 四份 recovery IDB 均原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,648,778 bytes，539 imports / 69 exports；
- Headless wasm：84,995,919 bytes，538 imports / 69 exports；
- 相较 V181 两份 wasm 均增加 3 bytes，import/export ABI 表面不变；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- Web GLOBAL section size 为 `0xD5B2`，CODE 为 `0x1A40B14`；Headless GLOBAL 为
  `0xD5DA`，CODE 为 `0x19E8AC2`；
- 两个 build tree 的 CTest 均报告 `No tests were found`，没有虚报 runtime unit-test；
  recorder 由 ordinary/headless syntax-only 覆盖；
- `git diff --check` 通过，只出现 working tree 既有 LF/CRLF 转换提示；
- 四端 xref/decompile/disassembly、comment/bookmark、force-recompile/readback 与 save 全部
  成功。

构建只出现项目既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI 与 JS-library 警告，
没有新增 motionplayer 编译或链接错误。

## 结论边界

本轮闭合的是 assign 中 `type/left/top` 的 source-null/target-shared hint identity、固定调用顺序、
status 与 owner 边界。`type` 的其它 consumer 仍由 V167 全局纵切面负责，完整 geometry family
仍由 V169 负责；本轮不重计或重命名它们。相邻的八项 source integer 双阶段读取与 target
`setSize(width,height)` method-cache identity 已由 V183 单独闭合，且明确不复用
`widthMemberHint_guess` / `heightMemberHint_guess`；详见
`analysis/motionplayer_separate_layer_assign_double_read_set_size_shared_hint_boundary_four_binary_2026-08-17.md`。
