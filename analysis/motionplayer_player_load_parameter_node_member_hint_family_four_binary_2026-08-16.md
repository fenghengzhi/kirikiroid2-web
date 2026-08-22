# MotionPlayer Player load/parameter/node 21-slot member-hint family 四参考复原（2026-08-16）

## 范围与结论

本纵切面从 V153 已闭合的 load-request `motionMemberHint_guess` 继续 fresh 审计
`Player_loadMotion_guess`，并沿相邻 BSS 槽逐项追踪所有 data xref。四份
`reference/binaries/` 共同证明：`chara` 起始处不是三个孤立的 file-local cache，而是一个
连续的 21×4-byte Player member-hint family。该 family 横跨：

- loadMotion 请求 Dictionary、`onFindMotion` callback 和 ResourceManager `findMotion`；
- parameter entry 的 `id/discretization/rangeBegin/rangeEnd`；
- Emote play 分支的 `division/motionList`；
- node 初始化的 `emoteEdit` 到 `transformOrder`；
- build/render 共用的 `requireLayerId`；
- modified-node prepass 的 `modified` getter/setter；
- ground-correction callback 的 `onGroundCorrection`。

四端的字段顺序、4-byte stride 和 live 消费者完全同构。Android 两端还保留一个与主
`Player_loadMotion_guess` 复用前三 hint 槽的零-xref load residual；iOS 两端只保留 live
implementation。V257 已证明 Android residual 读取未初始化 Player 尾 dispatch，且 callback
result 与 findMotion 返回槽分离，所以它不是此前所称的“同语义编译器展开”。符号已剥离，
恢复名继续保留 `_guess`。绝对地址只进入本文和 recovery IDB，不写入编译源码注释。

## family 边界

| target | first `chara` | last `onGroundCorrection` | next slot | next-slot consumer |
|---|---:|---:|---:|---|
| Android arm64 | `0x1AB53D0` | `0x1AB5420` | `0x1AB5424` | `Player_updateLayersVertexComputation_guess` |
| Android armv7 | `0x111186C` | `0x11118BC` | `0x11118C0` | `Player_updateLayersVertexComputation_guess` |
| iOS arm64 | `0x101B69898` | `0x101B698E8` | `0x101B698EC` | `Player_updateLayersVertexComputation_guess` |
| iOS armv7 | `0x187D53C` | `0x187D58C` | `0x187D590` | `Player_updateLayersVertexComputation_guess` |

本轮逻辑 family 满足 `base + index * 4`，index 为 0..20。紧邻下一槽已经进入另一段
updateLayers vertex/EmoteEdit transform 属性组，因此本轮不把它误并为 load/parameter/node
family；它留给下一纵切面继续 fresh 展开。

## 21 槽精确映射

| idx | recovered symbol / member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 0 | `requestCharaMemberHint_guess` (`chara`) | `0x1AB53D0` | `0x111186C` | `0x101B69898` | `0x187D53C` |
| 1 | `onFindMotionMemberHint_guess` (`onFindMotion`) | `0x1AB53D4` | `0x1111870` | `0x101B6989C` | `0x187D540` |
| 2 | `findMotionMemberHint_guess` (`findMotion`) | `0x1AB53D8` | `0x1111874` | `0x101B698A0` | `0x187D544` |
| 3 | `commandIdMemberHint_guess` (`id`) | `0x1AB53DC` | `0x1111878` | `0x101B698A4` | `0x187D548` |
| 4 | `playerParameterDiscretizationHint_guess` (`discretization`) | `0x1AB53E0` | `0x111187C` | `0x101B698A8` | `0x187D54C` |
| 5 | `playerParameterRangeBeginHint_guess` (`rangeBegin`) | `0x1AB53E4` | `0x1111880` | `0x101B698AC` | `0x187D550` |
| 6 | `playerParameterRangeEndHint_guess` (`rangeEnd`) | `0x1AB53E8` | `0x1111884` | `0x101B698B0` | `0x187D554` |
| 7 | `divisionMemberHint_guess` (`division`) | `0x1AB53EC` | `0x1111888` | `0x101B698B4` | `0x187D558` |
| 8 | `motionListMemberHint_guess` (`motionList`) | `0x1AB53F0` | `0x111188C` | `0x101B698B8` | `0x187D55C` |
| 9 | `nodeEmoteEditMemberHint_guess` (`emoteEdit`) | `0x1AB53F4` | `0x1111890` | `0x101B698BC` | `0x187D560` |
| 10 | `nodeLabelMemberHint_guess` (`label`) | `0x1AB53F8` | `0x1111894` | `0x101B698C0` | `0x187D564` |
| 11 | `parameterizeMemberHint_guess` (`parameterize`) | `0x1AB53FC` | `0x1111898` | `0x101B698C4` | `0x187D568` |
| 12 | `coordinateMemberHint_guess` (`coordinate`) | `0x1AB5400` | `0x111189C` | `0x101B698C8` | `0x187D56C` |
| 13 | `nodeJoinTargetMemberHint_guess` (`joinTarget`) | `0x1AB5404` | `0x11118A0` | `0x101B698CC` | `0x187D570` |
| 14 | `nodeGroundCorrectionMemberHint_guess` (`groundCorrection`) | `0x1AB5408` | `0x11118A4` | `0x101B698D0` | `0x187D574` |
| 15 | `nodeFrameListMemberHint_guess` (`frameList`) | `0x1AB540C` | `0x11118A8` | `0x101B698D4` | `0x187D578` |
| 16 | `nodeInheritMaskMemberHint_guess` (`inheritMask`) | `0x1AB5410` | `0x11118AC` | `0x101B698D8` | `0x187D57C` |
| 17 | `nodeTransformOrderMemberHint_guess` (`transformOrder`) | `0x1AB5414` | `0x11118B0` | `0x101B698DC` | `0x187D580` |
| 18 | `nodeRequireLayerIdMemberHint_guess` (`requireLayerId`) | `0x1AB5418` | `0x11118B4` | `0x101B698E0` | `0x187D584` |
| 19 | `emoteEditModifiedHint_guess` (`modified`) | `0x1AB541C` | `0x11118B8` | `0x101B698E4` | `0x187D588` |
| 20 | `onGroundCorrectionMemberHint_guess` (`onGroundCorrection`) | `0x1AB5420` | `0x11118BC` | `0x101B698E8` | `0x187D58C` |

## fresh callsite 与 data-xref 证据

主要 consumer 地址如下；表内函数均在重建 data item 后强制刷新 Hex-Rays 缓存。

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| loadMotion | `0x6AE2F0` | `0x57F654` | `0x1001067BC` | `0x103BBC` |
| Android zero-xref tail-dispatch load residual | `0x6CD42C` | `0x593F60` | — | — |
| appendParameterEntry | `0x6AEAF8` | `0x57FA14` | `0x100106D00` | `0x104168` |
| playImpl | `0x6AF664` | `0x580158` | `0x100107540` | `0x104AE8` |
| initNonEmoteMotion | `0x6B0A3C` | `0x580C28` | `0x100108258` | `0x1058F8` |
| initNodeFields | `0x6B1058` | `0x580FA4` | `0x100108720` | `0x105E70` |
| calcViewParam | `0x6CE908` | `0x594958` | `0x1001201CC` | `0x11EED4` |
| getCommandList | `0x67F900` | `0x595FF0` | `0x100121EB0` | `0x120CF8` |
| buildNodeTreeRecursive | `0x6B1E4C` | `0x5818B0` | `0x100109328` | `0x106BDC` |
| buildRenderCommands | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| buildPrivateMotionGLLCommands | `0x6DBB18` | `0x59CB20` | `0x10012B7D0` | `0x12A304` |
| refreshModifiedNodeTimelines | `0x6B3C58` | `0x582A7C` | `0x10010A88C` | `0x10820C` |
| applyGroundCorrection | `0x6B7DF0` | `0x585230` | `0x10010DFF4` | `0x10B8FC` |

逐槽 fresh `xref_query(xref_type=data)` 得到的 source-level 共享关系为：

| slot | consumers |
|---|---|
| `chara/onFindMotion/findMotion` | live loadMotion；Android tail-dispatch residual 复用相同三槽，但不复用 live result-slot/receiver 语义 |
| `id` | appendParameterEntry、getCommandList |
| `division` | playImpl、calcViewParam、getCommandList |
| `parameterize` | initNonEmoteMotion、initNodeFields |
| `coordinate` | initNodeFields、getCommandList |
| `requireLayerId` | buildNodeTreeRecursive、buildRenderCommands、buildPrivateMotionGLLCommands |
| 其余 | 表中对应单一 consumer；`modified` 的 getter 和 setter 在同一函数内共享 slot 19 |

ARM/Thumb 端一个地址常有 page/base materialization 与 add、以及 literal-pool data xref，所以 raw
xref count 大于 source-level consumer 数；Intel A64 通常只出现一条 ADRL xref。本文按 unique
containing function 归并，不能用 raw count 推断缓存数量。

## loadMotion 的 hint 数据流与边界行为

四端共同的顺序是：

1. 创建 request Dictionary；`PropSet(TJS_MEMBERENSURE, "chara")` 使用 slot 0；
2. `PropSet(TJS_MEMBERENSURE, "motion")` 不分配本 family 新槽，而复用 V153 的
   `motionMemberHint_guess`；
3. 对 current dispatch 调 `FuncCall(0, "onFindMotion", slot 1, result, 1, ...)`；
4. 把 callback 写入的 `result` 强制转 Object，然后分别读取 `chara`、`motion`；
5. 构造 `motion/<adjusted chara>/<adjusted motion>`，再调用 ResourceManager
   `FuncCall(0, "findMotion", slot 2, result, 2, ...)`；
6. `onFindMotion` 和 `findMotion` 共用同一个 result Variant，因此后者失败且不写 result 时，
   前一 callback Object 仍是返回值。

callback-result 的 `chara/motion` 与 request 写入同名字面量，但绝不复用 slot 0 或 V153
motion 槽。A64 的内联路径在 existence probe 明确把 hint register 置零，随后 typed string getter
也传零；A32/iOS 两端调用 `VariantObject_getStringOrDefault_guess`，该 helper 内部同样用 null
hint 完成 probe 和 value read。portable 的 `adjustedString` 因而继续显式传 `nullptr`。本轮测试
也精确断言两个 result-property hint 都为 null。

Android IDA 对部分宽字符串只显示首字符或前缀（如 `"c"`、`"onFind"`、`"f"`）；iOS arm64
直接显示完整 `chara/onFindMotion/findMotion`。四端槽地址、调用参数位置和同构控制流一致，
因此这只是字符串自动识别/显示差异，不是产品语义差异。

## portable 源码改动

- `MotionDispatch.h` / `RuntimeSupport.cpp` 按 native 顺序声明、定义完整 21-slot family；
- 删除 `PlayerCore.cpp` 的 request chara/onFindMotion/findMotion 三个 file-local placeholder，
  loadMotion 全部改用 process-wide `detail::*` 身份；
- 删除 `PlayerVariable.cpp` 的 discretization/rangeBegin/rangeEnd 三个 file-local cache，并与
  已证实共享的 `commandIdMemberHint_guess` 连成准确 parameter 子序列；
- 删除 `PlayerTimeline.cpp` 的重复 `motionDivisionMemberHint_guess` 和 file-local motionList；
  Emote branch 的 division 改为复用 calc/getCommand 已在使用的
  `divisionMemberHint_guess`，motionList 使用新全局槽；
- 删除 `NodeTree.cpp` 的九个 node-local hint，emoteEdit/label/parameterize/coordinate/
  joinTarget/groundCorrection/frameList/inheritMask/transformOrder 全部接到 family；
- `requireLayerId` 原先在 NodeTree 与 private MotionGLL renderer 各有一份 cache，本轮按四端
  xref 合并为 `nodeRequireLayerIdMemberHint_guess`；
- modified prepass 的 getter/setter 改用 family slot 19；既有
  `onGroundCorrectionMemberHint_guess` 移入 family 末端；
- `parameterize`、`coordinate`、`commandId`、`division` 的既有共享调用点保持同一个对象身份，
  只把声明/定义移动到准确原生顺序。

## 回归探针

现有 probe 被收紧为准确 pointer identity：

- load helper 精确断言 callback slot 1、findMotion slot 2，以及 callback-result chara/motion
  两次 null hint；
- parameter append 精确断言 id/discretization/rangeBegin/rangeEnd 对应 slot 3..6，并保留
  四指针互异检查；
- node build 的 forwarding layer recorder 对 emoteEdit 到 transformOrder 九个名字逐项断言
  slot 9..17；ResourceManager recorder 精确断言 requireLayerId slot 18；
- modified getter/setter 精确断言共同使用 slot 19；ground-correction 既有 probe 已精确断言
  slot 20；
- 新增 21 指针 pairwise-distinct 回归，防止以后因同名字面量或错误 alias 合并独立槽。

## IDB 回写

四份 recovery IDB 均完成：

- 对整段 84 bytes 先统一 `undefine`，再按升序建立 21 个独立 `unsigned int` data item；
- 所有 data item 写入 `_guess` 名、index/member 注释；bookmark 为
  `V154 complete 21-slot Player load/parameter/node member-hint family`；
- Android 两个 companion load 函数与全部 unique consumer 写入 family/shared/null-hint 注释；
- A64/A32 各强制 fresh recompile 13 个 consumer，iOS A64/A32 各 12 个；
- fresh global readback 每库恰好 21 项，全部 `size=4`；fresh xref readback 的 consumer set
  与上表一致；fresh loadMotion disasm 四端均显示恢复后的前三槽名；
- 最初直接对旧 aggregate 调逐槽 `make_data` 时，三端 readback 仍出现递减大小的重叠数组，
  A64 前八槽也被旧 item 吞并。本轮没有信任 mutation 的 success 标志，而是根据 readback
  发现并用 whole-range undefine + rebuild 修正；二进制字节未改；
- 四份数据库均已保存。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅同一既有 warning。
- `Web Debug Build` 完整链接通过；`index.wasm` 为 85,647,983 bytes。
- `Wasmtime Headless Debug Build` 完整构建通过；`index.wasm` 为 84,995,124 bytes。
- Node `WebAssembly.Module` 解析成功：Web 539 imports / 69 exports，headless 538 imports /
  69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；因此这里只报告 probe 编译通过，
  不虚报 runtime CTest 执行。

## 下一纵切面

V155 应从本 family 紧邻下一槽开始，fresh 审计
`Player_updateLayersVertexComputation_guess` 的 EmoteEdit transform/member-hint 连续组，核对
当前 `emoteEditFlipX/flipY/zoomX/zoomY/slantX` 等注释、共享 `angle` 身份和可能仍存在的
file-local placeholder，不能从旧 `libkrkr2.so` 注释推断四参考布局。
