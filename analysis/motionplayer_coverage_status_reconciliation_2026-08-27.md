# Motionplayer 覆盖状态对账（2026-08-27）

> **后续纠正：** 本报告原来的最终计数只对账了当时已经存在的semantic slices，不能证明
> `tasks.md` 的163个原始ticket全部进入分母。逐ticket审计发现 `D3DEmotePlayer` 与多个
> Emote controller对象族没有直接coverage映射，正式构建/差分也未完成。因此新增的
> MP-F03..F08六个“最终”聚合行已重新标为 `EVIDENCE_BLOCKED`；当前权威原始任务状态见
> `analysis/motionplayer_tasks_status.tsv`。

## 1. 目的

本轮不新增 C++ 运行语义，也不把“有四端地址”自动提升为“已实现”。它处理的是另一类
账本漂移：早期 coverage row 保持 `EVIDENCED_4_4`，但其后续 companion slice 已经完成
四端 fresh 审计、本地逐行对照、必要的语义修正和 owner/container closure，旧行的
`evidence_status` 与 `remaining_gap` 没有回填。

对账依据是：

- `plan.md` 的状态定义：`IMPLEMENTED` 表示本地实现已逐行对照联合证据，不要求把当前
  不存在的构建工具伪装成验证成功；
- `analysis/motionplayer_coverage.tsv` 中后续 `IMPLEMENTED` companion rows；
- `analysis/motionplayer_ncb_equivalence.tsv` 的 316/316 四端注册面与
  `BODY_PENDING_SEPARATE_SLICE=0`；
- MP-F01 三个根现已全部闭合；
- `git diff --check`、TSV 12 列、唯一 slice ID 和状态计数校验。

## 2. 升级的 39 个 stale rows

### 2.1 注册面与生成账本（12 项）

以下 row 的实体是“注册面/生成账本”，不是其所有下游内部 helper 的并集。它们的本地
宏顺序、descriptor kind、callback 地址、class/factory/attach topology 和四端分母均已
逐项对照；后续 callback body 也已有独立 coverage rows 承接：

- `MP-A14-REG-01`；
- `MP-A09-REG-POINT`、`MP-A09-REG-CIRCLE`、`MP-A09-REG-RECT`、
  `MP-A09-REG-QUAD`；
- `MP-A10-LAYERGETTER-REG`；
- `MP-A11-PLAYER-REG`；
- `MP-A12-SOURCECACHE-OBJSOURCE-NCB-SURFACE`；
- `MP-A13-RESOURCEMANAGER-NCB-SURFACE`；
- `MP-A14-D3DADAPTOR-NCB-SURFACE`；
- `MP-A15-BEZIER-LAYER-EXTENSIONS-NCB-SURFACE`；
- `MP-A16-EMOTEPLAYER-NCB-SURFACE`。

`MP-F03-NCB-SURFACE-LEDGER` 另作为第 13 个账本项升级：其实现目标是可重复生成、稳定
join 和保留 body disposition，而不是声称 316 个 row 的所有 native helper 都属于 NCB
分母。316/316、`UNMAPPED=0`、无孤儿 evidence、四端字段完整和 pending=0 均已满足。

### 2.2 已有 companion body/EH 分行的正常语义（8 项）

- `MP-A09-GEOMETRY-SCALARS`；
- `MP-A09-QUAD-P-NORMAL`；
- `MP-L09-GEOMETRY-CTORS`；
- `MP-L10-LAYERGETTER-CTOR`；
- `MP-A10-LAYERGETTER-SCALARS`；
- `MP-A10-LAYERGETTER-ARRAYS`；
- `MP-L10-LAYERGETTER-VTX`；
- `MP-L10-LAYERGETTER-MOTION-PARTICLE`。

其中 Quad/LayerGetter Array/Dictionary 的“正常 owner flow”row 可以实现完成；仍不能恢复
的精确 per-call-site unwind 已分别隔离在 `MP-L09-QUAD-P-EH` 和
`MP-L10-LAYERGETTER-ARRAY-EH`，没有被升级或掩盖。

### 2.3 后续 root/object/container ledger 已闭合的 producer/owner（5 项）

- `MP-D10-LAYERGETTER-ONE`；
- `MP-D10-LAYERGETTER-LIST`；
- `MP-C10-PLAYER-NODE-DEQUE`；
- `MP-L10-LAYERGETTER-BORROW`；
- `MP-L11-PLAYER-CTOR`。

关键后续证据为 `MP-C12-PLAYER-BUILD-NODE-TREE`、
`MP-L11-MOTIONNODE-PREPARED-ITEM-LIFETIME` 与
`MP-C18-PLAYER-NATIVE-CTOR-DTOR-OWNER-LEDGER`。producer 的精确 allocation/CreateNew/
append unwind 仍单独保留为 `MP-L10-LAYERGETTER-PRODUCER-EH=EVIDENCE_BLOCKED`。

### 2.4 已闭合的 Player leaf/body 与静态尾部（13 项）

- `MP-C11-PLAYER-REG-TAIL`；
- `MP-A11-PLAYER-DIRECT-14-20`；
- `MP-A11-PLAYER-TIME`；
- `MP-A11-PLAYER-ROOT-FLAGS`；
- `MP-L11-PLAYER-VARIANT-OWNERS`；
- `MP-A11-PLAYER-TRANSFORM-ORDER`；
- `MP-C11-PLAYER-HASCAMERA`；
- `MP-L11-PLAYER-CHARA-PROPS`；
- `MP-A11-PLAYER-STOP-SYNC`；
- `MP-A11-PLAYER-DEFAULT-HOOKS`；
- `MP-C11-PLAYER-GETVARIABLE`；
- `MP-D11-PLAYER-SKIP-TO-SYNC`；
- `MP-L11-PLAYER-ISEXIST`。

这些 row 的旧 gap 只剩“正式构建不可用”或“并入最终 owner ledger”；C18 已完成后者，
而前者属于验证环境限制，不是本地实现尚未逐行对照。

## 3. 最后 2 个 `EVIDENCED_4_4` 的结构闭包

`MP-L10-LAYERGETTER-STRINGS` 与 `MP-C10-MOTIONNODE-ORDER` 最终通过同一组四端
ctor/default-tail/copy/dtor 与 string getter 证据闭合：portable `MotionNode` 已恢复共同 owner
顺序，`layerName` 成为首个数据 owner，双 slot/active selector/dormant `std::string` 连续关系、
独立早期 matrix、0x50-byte accumulated block、后部 Variant/vector/ttstr owners 和逆序析构
关系均已落到声明中。

64-byte dormant record 与尾部 4-byte word 的原始语义类型/名称确实不能唯一识别，但四端共同
证明其为真实 source member、未初始化、default-copy、无析构副作用。实现用两个显式
`std::byte` object-representation record 表达这一可观察边界，既没有把它们丢掉，也没有把
`float[16]`/`double[8]` 等猜测伪装成原始源码。因此这两个 row 升级为 `IMPLEMENTED`；
“原名不可恢复”作为不可逆证据限制继续保留在报告和 coverage gap 中。

`MP-D10-RAW-LABEL-RESOLVE` 随后已经通过 5/9/9/9 全量 xref、Android arm64 五处
inline consumer 和 getLayerMotion/双 facade owner 闭包升级为 `IMPLEMENTED`，详见
`motionplayer_raw_label_resolver_caller_closure_four_binary_2026-08-27.md`。

`MP-D11-PLAYER-COLOR-INDEPENDENT-Z`、`MP-D11-PLAYER-PROCESSED-MESH` 与
`MP-D11-PLAYER-CONTAINS` 随后又通过 12 个主函数、Android capture manager、libc++
callable vtable 和 iOS armv7 SjLj cleanup 审计升级为 `IMPLEMENTED`，详见
`motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md`。

`MP-C10-TJS-ARRAY-ITEMS`、`MP-A11-PLAYER-CAMERA-VECTORS`、
`MP-A11-PLAYER-BOUNDS`、`MP-C11-PLAYER-VARIABLE-KEYS`、
`MP-A11-PLAYER-CAMERA-OFFSETS` 与 `MP-C11-PLAYER-MODROOT-LAYERNAMES` 随后通过
四端完整 append/reserve helper、libstdc++ map-before-block 提交边界、libc++ temporary
split-buffer staging、iOS arm64 LSDA-only cold cleanup、iOS armv7 deque/getter SjLj
cleanup 以及六组 caller 的 3-cleanup/1-no-local-landing 矩阵升级为 `IMPLEMENTED`，详见
`motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md`。

`MP-L09-QUAD-P-EH`、`MP-L10-LAYERGETTER-SHAPE`、
`MP-L10-LAYERGETTER-ARRAY-EH` 与 `MP-L10-LAYERGETTER-PRODUCER-EH` 又通过 24 个
Array/Dictionary getter body、四类 shape CreateAdaptor、single/list producer、Android
arm64 landing、iOS arm64 LSDA-only cold cleanup、iOS armv7 SjLj cleanup和 Android armv7
完整无本帧 cleanup disposition 升级为 `IMPLEMENTED`，详见
`motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md`。

`MP-C30-PLAYER-KRKR-ATLAS-IMAGEPACKER` 随后闭合 KRKR atlas loader、RL8/RL32、透明
record、ImagePacker split tree、page texture cache、两个 caller 与异常 partial-publication
边界。`MP-C31-PLAYER-VARIABLE-BINDER` 又闭合 HM1/HM2/ParameterRampMap binder、cache
rebuild、Engine/variable-track/raw caller 和换树生命周期，并修正本地 reset 将相邻
`writeVal` 误写为 1、却没有 rearm `weight` 的字段错误。

`MP-C32-PLAYER-FRAME-PROGRESS-EVENTS` 随后又闭合 `frameProgress` 根、modified/
parameterized refresh、absolute reseek、正反向四流、variable/node 双槽、join snapshot
恢复/裁剪和 pending-event 派发的逐构造态 EH，并将
`MP-D11-PLAYER-PROGRESS-RAW` 的旧深层 dependency gap 回填；
`MP-D11-PLAYER-PLAY-RAW` 的旧 gap 也已由既有 playback-state-machine companion row
回填。

`MP-C33-PRIVATE-GLL-CLASS-DRAW` 最后闭合了 UTF-16LE
`__Private_Motion_GLLayer` 类 registrar、ClassID、native factory、六个 callback、derived
vtable和完整 `Draw_GPU` consumer/EH；因此 `MP-L11-SLA-PRIVATE-GLL-ENSURE` 与
`MP-G11-PRIVATE-GLL-BUILDER` 的历史 gap也已回填。

最终根可达审计随后枚举了共享 Layer factory全部七个调用点、SLA empty-shell/arbitrary
target的全部 native consumer、非 NCB function-pointer/vtable/static-lifetime 根以及对象、
容器和 owner 分母。完整对账见
`motionplayer_root_reachable_denominator_final_audit_2026-08-27.md`。新增 MP-F03..F08 六个
最终账本行，并把既有 `IMPLEMENTED` 行仍写着“continue/close/audit”的历史文字统一为无
剩余语义 gap；两个平台边界不作数字美化。

此前 3 个 `EVIDENCE_BLOCKED` 均已用原生 landing/cold/SjLj 证据闭合；2 个
`PLATFORM_BOUNDARY` 原样保留，不能为了 coverage 数字将其伪装为普通共同源码行为。

## 4. 结果解释

纠正后的当前coverage状态为146 `IMPLEMENTED`、6 `EVIDENCE_BLOCKED`、
2 `PLATFORM_BOUNDARY`。146个既有语义slice的局部证据仍有效，但根可达全分母、对象/容器
总账、原始ticket映射、真实gap集合和最终验收六个聚合结论已经重新打开。正式
CMake/unit/Web构建也仍未完成。只有163项逐项台账闭合后，才能重新发布最终状态计数。
