# MotionPlayer 旧二进制注释迁移台账（2026-08-13）

## 目的

当前复原真值已经从旧的单一 `libkrkr2.so` 切换为
`reference/binaries/` 中 Android ARM64、Android ARMv7、iOS ARM64、
iOS ARMv7 四个 1.3.9 参考二进制。编译源码中遗留的单端地址、`sub_xxx`
身份和旧库名不能继续充当共同源级语义的证据。

本台账只管理源码注释的证据迁移。它不把删除地址写法等同于完成函数复原，
也不允许把未经重新核对的旧单端结论简单改称“四端结论”。绝对地址继续保留在
对应的 `analysis/*four_binary*.md` 地址表中；编译源码只保留共同语义、对象关系、
边界行为及仍然准确的 `_guess` 标识。

## 初始扫描

扫描范围：

```text
cpp/plugins/motionplayer/**/*.{cpp,h,hpp,cc}
```

风险模式包括：

- `libkrkr2`；
- 注释中的五位以上绝对十六进制地址；
- `sub_` 加绝对地址形成的旧单端函数身份；
- 旧参考文件名直接嵌入编译源码。

首次扫描得到：

- 35 个源码文件；
- 913 条“旧库名或绝对地址”注释命中；
- 其中最大集中区为 `PlayerFrameProgress.cpp` 199 条、
  `PlayerUpdateLayerEval.cpp` 128 条、`PlayerUpdateParticles.cpp` 94 条、
  `PlayerRenderExecute.cpp` 66 条、`EmoteEngine.cpp` 51 条。

这些大文件不能机械替换。它们的地址通常同时承担控制流标签、字段布局和行为
论证，必须按 progress/evaluate/particle/render/engine 的四端专题逐段迁移。

## 已完成的第一批

本批只处理已有四端专题支撑、且无需改变可执行语义的位置：

- `D3DAdaptor.cpp/.h`、`SourceCache.cpp`：render-source texture、strict Layer
  转换、software texture holder/container 与 creation-ref 边界；
- `PlayerResource.cpp`：四端 atlas pack 返回值忽略、共享 atlas 路由和 fresh
  layer-id 语义，地址留在专题文档；
- `SourceCache.h`：`LayerGetter` 默认空节点及无 null guard 边界；
- `EmotePlayer.h`、`main.cpp`：四端 NCB 成员归属与命名，不再引用旧单库地址；
- `MeshPoint.h`：四端共同的两个 float/8-byte stride；
- `internal/ttstr_hash.h`：四端共同 hash/null-key 与 UTF-16 comparator 语义；
- `internal/value_structs.h`：四端 owner/destructor 顺序语义；
- `MotionDispatch.h`：进程级 TJS hint 槽身份和 conversion helper 行为；2026-08-15
  复扫发现首批实际仍漏掉八个旧 Android 单端全局地址，现已按四份当前参考补齐映射并
  删除，详见 `motionplayer_dispatch_member_hint_globals_four_binary_2026-08-15.md`；同日又以
  四份新鲜 decompile/disasm/xref 闭合 `splitTtstr_guess` 的三指针 vector、九处调用面、
  final-empty、canonical-empty-separator、两次 separator length getter 和 unwind 边界，删除
  helper 上的四个绝对地址，详见
  `motionplayer_split_ttstr_container_boundary_four_binary_2026-08-15.md`；同日又重新映射 named、
  numeric、string、count 与 strict probe 属性 helper，确认公共头最后六个 Android 地址均为
  旧库身份而非当前四参考函数。除迁出地址外，还修复 strict probe 失败污染 destination、
  Variant 返回副本和 typed getter 临时所有权顺序，详见
  `motionplayer_dispatch_property_access_helpers_four_binary_2026-08-15.md`；同日又重新闭合
  NodeLabelMap 的 build-time `operator[]`、raw-label resolver、五类非递归 caller、四套
  RB-tree ABI 与 teardown。旧 `Player_nodePathMap_find @0x6F2228` / `0x6B4CB0` /
  `sub_9B1ED0` 注释并非当前四参考身份，已删除；同时修复 comparator 丢失的 null-backing
  排序、恢复共享 Player member 调用链，并移除 particle 原版不存在的 mapped-index 上界保护，
  详见 `motionplayer_node_label_map_lookup_lifecycle_four_binary_2026-08-15.md`；同日继续重审 HM3
  `PerNodeLayerState`，四端 upsert、init/restore、invalidation 与逐节点析构共同证明 value 内嵌
  完整 `ClipSlot`，旧 `dispatch_*`/`heap_*`/`ttstr_*` 注释是把共享 slot 析构器误拆成外层字段。
  已删除这些虚构 owner、恢复 map 插入全零默认态，并从 `PlayerFrameProgress.cpp` 对应 join
  snapshot 生产/消费/清理段迁出旧 A64 地址，详见
  `motionplayer_join_snapshot_four_binary_2026-08-11.md`；随后继续闭合 full-reseek 的 root
  `priority` 绝对扫描，迁出 `0x6B8C..0x6B8F` 单端叙述，并修复 signed negative
  `count` 分支未先提交零 cursor 的边界偏差，详见
  `motionplayer_root_priority_reseek_cursor_boundary_four_binary_2026-08-15.md`；同一 full-reseek
  的 tag phase 随后完成四端重映射，确认旧端口显式 target 参数、phase-local 字段引用和
  variable-track target 快照均不属于原版。现已恢复 this-only ABI、live evaluation reload、
  tag/priority/current-root/next-root 四 owner 跨 common tail 的逆析构，以及 coarse 双增量、
  integer cache 与 sync-before-action 边界，详见
  `motionplayer_tag_absolute_reseek_four_binary_2026-08-15.md`；其后的
  variable-track absolute phase 也已从四份当前参考重新闭合：旧代码缺失
  per-track `frameSource` 局部 owner，并用会在 `INT_MIN` 溢出的 C++ signed
  `count-2` 表达原生 32 位回绕。现已恢复 owner 尾释放、live-time reload、
  wrapping root/variable clamp 与 slot 提交顺序，详见
  `motionplayer_variable_track_absolute_reseed_four_binary_2026-08-15.md`；再后的
  non-root node absolute helper 也已 fresh 重审，恢复 selection snapshot、
  frame-list local-owner/persistent-field split、wrapping count clamp 和两槽完成后的
  late active/dirty commit，并移除 native 不存在的 source-gate nodeType range guard，
  详见 `motionplayer_node_absolute_reseed_four_binary_2026-08-15.md`；随后回到
  variable-track incremental forward/rewind，纠正两函数旧 `(Player*, double)`
  prototype 为 this-only，恢复 forward count-only local owner / persistent-field
  step/merge split、raw cursor、wrapping signed limit、live ordered-LT/NaN 边界、
  cursor-before-step 与 slot0/slot0 双 merge，并恢复 rewind 无 count-owner、live
  ordered-GT、uint32 zero-underflow 到 signed `-1` 及 slot0/slot1 merge，详见
  `motionplayer_variable_track_incremental_seek_four_binary_2026-08-15.md`；同一四流成员的
  leading tag/root incremental phase 随后也已闭合。旧代码把 tag/priority owner 缩进各
  phase 且快照 target time，遗漏两 owner 跨 variable/node phase 到函数尾、priority→tag
  逆析构的生命周期。现已恢复 forward tag `count>=1`、root 无 gate、wrapping
  `count-2`/cursor 与 ordered-LT NaN-continue，以及 rewind tag `count!=0`、root 无 count、
  zero-underflow 到 signed `-1` 与 ordered-GT NaN-stop；精确四端地址与异常前缀见
  `motionplayer_incremental_tag_root_streams_four_binary_2026-08-15.md`；末尾 non-root node
  incremental phase 也已继续闭合。旧代码用共享近似 stepper、target 快照和 source
  range guard 混合两个方向；现已恢复 live deque size、parameterized shared route、
  forward count-only owner/ordered-LT、rewind no-count/ordered-GT、raw selector/parity、
  wrapping index、parse/action 异常前缀、delayed exact flags 与 direct source shift，见
  `motionplayer_node_incremental_seek_four_binary_2026-08-15.md`。

第一批结束后的同口径扫描：

2026-08-15 又清理了 `Player.h` / `PlayerVariable.cpp` 的两类旧单端注释：

- `TransformOrder` 不再引用旧 `Motion_namespace_ncb_register` 地址，改由四端当前根
  registrar 的共同 ID 与 Flip→Slant→Zoom→Angle publication order 描述；
- Player HM2 不再写死 Android arm64 `Player+320` 或虚构私有 upsert 身份。四端偏移为
  `+320/+220/+248/+180`，operator[] 是 HM2、join variable snapshot 与 Engine scalar
  map 共用的 `LabelValueMap` specialization。详见
  `motionplayer_transform_constants_player_hm2_comment_migration_four_binary_2026-08-15.md`。
- `RuntimeSupport.cpp` 的 logo-chain non-Emscripten 注释不再引用旧单端 EmoteObject 地址
  和 command-line helper。四端三编码搜索与 construct/play body fresh scan 均排除 native
  trace/snapshot query；该功能继续作为默认关闭、显式 opt-in 的 Web sidecar，详见
  `motionplayer_logo_trace_query_native_absence_four_binary_2026-08-15.md`。

- 30 个源码文件；
- 823 条绝对地址注释命中；
- 62 条仍直接写旧 `libkrkr2.so` 的命中。

数字下降只表示旧证据已从这些位置撤出，不表示剩余模块已完成四端复原。

## 迁移规则

每个后续批次必须同时满足：

1. 找到覆盖该语义的现有四端专题，或从四个当前 IDB 重新取得新鲜证据；
2. 源码注释改写为共同语义，不在编译源码保留绝对地址；
3. 平台差异不能被“共同语义”抹掉，差异仍记录在专题地址/布局表；
4. 不能从旧 Android 单端函数名推断精确原始 C++ 名；未知名继续带 `_guess`；
5. 只改注释的批次仍运行 `git diff --check`；触及声明或实现时按风险重编；
6. 若旧注释描述的语义没有四端专题覆盖，保留为待审计项，而不是换一套措辞。

## 剩余优先级

按风险和依赖顺序迁移：

1. 小型公共接口/容器/NCB 文件，依赖已经完成的专题；
2. `EmoteEngine` controller/container 生命周期；
3. Player progress 与 timeline 游标；
4. layer evaluate/update 和 particle child 生命周期；
5. render build/execute/target 三阶段；
6. RuntimeSupport 中旧 Android “不存在某功能”的负证据。这类结论尤其不能在
   未重新搜索四端前改称共同边界。

## 2026-08-15 工具状态更新

此前记录的 IDA worker 无响应已经恢复。软件纹理、dispatch hint 和 `splitTtstr` 专题都已
在四份 live recovery IDB 中重新核对、补注释/书签并分别取得 `idb_save ok=true`。旧段落
只描述 2026-08-13 当时的工具状态，不再是当前限制；后续迁移仍必须逐纵切面取得新鲜四端
证据，不能因为工具恢复而机械删除剩余地址注释。
