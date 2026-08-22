# MotionPlayer updateLayers EmoteEdit 6-slot member-hint family 四参考复原（2026-08-16）

## 范围与结论

本纵切面从 V154 的 `onGroundCorrectionMemberHint_guess` 紧邻下一槽继续 fresh 审计，完整
覆盖四份 `reference/binaries/` 的 `Player_updateLayersVertexComputation_guess`。四端共同证明：

- 下一段是一个连续的 6×4-byte EmoteEdit member-hint family；
- 字段顺序严格为 `priorDraw / flipX / flipY / zoomX / zoomY / slantX`；
- `priorDraw` 是 flags=0 的 Boolean getter，发生在 mesh dirty early-return 之前；
- 后五槽是 force-visible geometry mirror 的 `TJS_MEMBERENSURE` setter；
- mirror 紧接着写入的 `angle` 不占用第七个相邻槽，而是复用 V153 node-frame family 的
  `angleMemberHint_guess`；
- 六槽后的下一相邻槽只被 `Player_updateParticleSystems_guess` 消费，因此 family 边界闭合。

当前 portable 实现原已有五个 setter 槽和正确的共享 `angle`，但 `priorDraw` getter 仍传
`nullptr`；同时五个定义放在与原生地址顺序无关的早期声明区。此次按四端证据补上
`emoteEditPriorDrawMemberHint_guess`，并把六个全局对象排列到 V154 21-slot family 之后。
符号均来自 stripped binary 的语义恢复，继续保留 `_guess`。绝对地址只写入本文和 recovery
IDB，不进入编译源码注释。

## family 与邻接边界

| target | first `priorDraw` | last `slantX` | reused `angle` slot | next slot | next-slot consumer |
|---|---:|---:|---:|---:|---|
| Android arm64 | `0x1AB5424` | `0x1AB5438` | `0x1AB5158` | `0x1AB543C` | `Player_updateParticleSystems_guess` |
| Android armv7 | `0x11118C0` | `0x11118D4` | `0x111168C` | `0x11118D8` | `Player_updateParticleSystems_guess` |
| iOS arm64 | `0x101B698EC` | `0x101B69900` | `0x101B69620` | `0x101B69904` | `Player_updateParticleSystems_guess` |
| iOS armv7 | `0x187D590` | `0x187D5A4` | `0x187D350` | `0x187D5A8` | `Player_updateParticleSystems_guess` |

六槽满足 `base + index * 4`，index 为 0..5。first slot 又正好等于 V154 的 next slot；
last+4 则跨入粒子更新 family。`angle` 地址位于更早的 V153 53-slot node-frame family，不能
因为它在 setter 序列中紧随 `slantX` 就误造一个新的相邻 cache。

## 六槽精确映射

| idx | recovered symbol / member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 0 | `emoteEditPriorDrawMemberHint_guess` (`priorDraw`) | `0x1AB5424` | `0x11118C0` | `0x101B698EC` | `0x187D590` |
| 1 | `emoteEditFlipXMemberHint_guess` (`flipX`) | `0x1AB5428` | `0x11118C4` | `0x101B698F0` | `0x187D594` |
| 2 | `emoteEditFlipYMemberHint_guess` (`flipY`) | `0x1AB542C` | `0x11118C8` | `0x101B698F4` | `0x187D598` |
| 3 | `emoteEditZoomXMemberHint_guess` (`zoomX`) | `0x1AB5430` | `0x11118CC` | `0x101B698F8` | `0x187D59C` |
| 4 | `emoteEditZoomYMemberHint_guess` (`zoomY`) | `0x1AB5434` | `0x11118D0` | `0x101B698FC` | `0x187D5A0` |
| 5 | `emoteEditSlantXMemberHint_guess` (`slantX`) | `0x1AB5438` | `0x11118D4` | `0x101B69900` | `0x187D5A4` |

## 唯一 consumer 与指令级关联

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_updateLayersVertexComputation_guess` | `0x6B98D0` | `0x5866F8` | `0x10010F6AC` | `0x10CE30` |

fresh disassembly 中，名字面量、hint 地址 materialization 与 TJS 调用在四端形成相同序列：

| member | Android arm64 hint refs | Android armv7 hint refs | iOS arm64 hint ref | iOS armv7 hint refs |
|---|---|---|---|---|
| `priorDraw` | `0x6B9A84`, `0x6B9A98` | `0x58682E`, `0x58683A` | `0x10010F7B0` | `0x10DAD0`, `0x10DAD6`, `0x10DADC` |
| `flipX` | `0x6BAA04`, `0x6BAA14` | `0x587368`, `0x587374` | `0x1001104B0` | `0x10D91A`, `0x10D922`, `0x10D926` |
| `flipY` | `0x6BAA24`, `0x6BAA34` | `0x587380`, `0x58738C` | `0x1001104D4` | `0x10D944`, `0x10D94C`, `0x10D950` |
| `zoomX` | `0x6BAA44`, `0x6BAA54` | `0x587398`, `0x5873A4` | `0x1001104F8` | `0x10D96E`, `0x10D976`, `0x10D97A` |
| `zoomY` | `0x6BAA64`, `0x6BAA74` | `0x5873B0`, `0x5873BC` | `0x10011051C` | `0x10D998`, `0x10D9A0`, `0x10D9A4` |
| `slantX` | `0x6BAA84`, `0x6BAA94` | `0x5873C8`, `0x5873D4` | `0x100110540` | `0x10D9C2`, `0x10D9CA`, `0x10D9CE` |

ARM64 的 ADRP/ADD、Thumb 的 MOVW/MOVT 或 literal-pool 会让一个 source-level address 产生
多条 raw data xref。Android armv7 在函数尾部 literal pool 还出现 `fn=null` 的 data xref；
按 containing function 与 literal-pool ownership 归并后，六槽都只有上述一个真实代码 consumer。

紧随 setter 序列的 `angle` fresh xref 为：

| target | mergeContent refs | updateLayers mirror refs |
|---|---|---|
| Android arm64 | `0x6903AC`, `0x6903B4` | `0x6BAAA4`, `0x6BAAB4` |
| Android armv7 | `0x56F41C`, `0x56F422` | `0x5873E0`, `0x5873EC` |
| iOS arm64 | `0x1000F1DF8` | `0x100110564` |
| iOS armv7 | `0xEE228`, `0xEE232` | `0x10D9EC`, `0x10D9F4`, `0x10D9F8` |

这直接证明 timeline merge 与 force-visible mirror 共用同一个 `angleMemberHint_guess` 对象身份。

## 数据流与边界行为

四端共同控制流可归纳为：

1. vertex pass 对每个非 root node 先取得 `parentIndex` 对应父节点；
2. 若 `forceVisible != 0`，复制持久 `emoteEdit` Variant，使用 flags=0 和 slot 0 调用
   `PropGet("priorDraw")`，再做 TJS Boolean conversion，结果写入 node 的一字节
   `priorDraw` 字段；否则直接把字段清零；
3. 上述 getter 回调发生在读取父 mesh-state、计算 mesh ancestor，以及
   `meshVertexPassDirty_guess` early-return 之前。因此即便本节点 vertex work 被跳过，
   `priorDraw` 的回调、异常与字段写入仍可观察；
4. 后续只有满足 vertex quad/materialization 路径的 force-visible node 才进入 geometry mirror；
5. mirror 先以 null named hint 取得 `coord` 和 `mtx` 数组，并用 numeric
   `TJS_MEMBERENSURE` 原位写入；随后写 `width/height/originX/originY` 的既有共享槽；
6. 再按 `flipX/flipY/zoomX/zoomY/slantX/angle` 顺序写入。前五项分别使用本 family
   slot 1..5，`angle` 使用 V153 共享槽。所有 named write flags 都是
   `TJS_MEMBERENSURE`；Boolean 参数由 typed accessor 发布为 TJS Integer，标量发布为 Real；
7. setter 返回值不会短路后续写入；Variant/Object conversion 或 getter 异常则沿普通 C++
   unwind 路径传播，portable 保持已有严格边界。

## portable 源码改动

- `MotionDispatch.h`：在 V154 21-slot family 后声明准确的六槽 EmoteEdit family；注释明确
  `priorDraw` 的 dirty-gate 时序和 `angle` 的共享身份；
- `RuntimeSupport.cpp`：从早期通用 scalar 区移走原五个 transform 定义，在
  `onGroundCorrectionMemberHint_guess` 后按原生顺序定义六个独立 `tjs_uint32`；
- `PlayerUpdateGeometry.cpp`：`priorDraw` 的 `motionPropGetBool` 从 null hint 改为
  `&detail::emoteEditPriorDrawMemberHint_guess`；
- `PlayerUpdateLayersInternal.h`：既有五个 setter 槽和
  `detail::angleMemberHint_guess` 保持不变，因为 fresh 四端证据证明它们已正确。

## 回归探针

- 新增 force-visible mirror dispatch recorder，精确记录 10 次 named write 的名字、flags、
  hint pointer、receiver 与顺序；断言五个 transform 写槽和共享 angle 槽身份；
- 新增六指针 pairwise-distinct 检查，并逐项断言六槽都不与
  `angleMemberHint_guess` alias；
- 构造一个 force-visible node，通过完整 `updateLayers` 流程记录 `priorDraw` getter，断言
  flags=0、唯一调用、准确 receiver、准确 slot 0 pointer，以及 Boolean 结果写入 node；
- 保留既有 mirror 数值、数组原位 mutation、Void/missing-member 异常边界测试。

## IDB 回写

四份 recovery IDB 均完成：

- 对六槽整段 24 bytes 先整体 `undefine`，避免旧的一字节 item 或聚合边界继续覆盖新定义；
- 按升序建立六个独立 `unsigned int` data item，写入 `_guess` 名和 member/共享-angle 注释；
- bookmark 为
  `V155 complete 6-slot updateLayers EmoteEdit priorDraw/transform member-hint family`；
- 强制刷新四端 `Player_updateLayersVertexComputation_guess` 的 Hex-Rays cache；
- fresh entity readback 每库恰好六项，全部 `size=4`；fresh data-xref readback 保留上述唯一
  code consumer，并再次确认共享 angle 与 next-slot particle 边界；
- 四份数据库均已保存，二进制输入字节未修改。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅同一既有 warning。
- `Web Debug Build` 完整重编译和链接通过；`index.wasm` 为 85,648,070 bytes。
- `Wasmtime Headless Debug Build` 完整重编译和链接通过；`index.wasm` 为 84,995,211 bytes。
- Node `WebAssembly.Module` 解析成功：Web 539 imports / 69 exports，headless 538 imports /
  69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；因此这里只报告 probe 编译通过，
  不虚报 runtime CTest 执行。
- `git diff --check` 通过；仅 Git 的 LF→CRLF 工作区提示。

## 下一纵切面

V156 应从六槽紧邻下一地址开始，fresh 审计 `Player_updateParticleSystems_guess` 与
`stepParticleChildren` 的 member-hint 相邻组。现有 xref 已提示 first particle slot 在部分端由
两个粒子函数共享，其后还有 render-source、`src`、`assignImages` 和 pending-event 邻接槽；
必须按四端 consumer set 分段，不能把整个 BSS 邻接区一次性误并为单一 family。
