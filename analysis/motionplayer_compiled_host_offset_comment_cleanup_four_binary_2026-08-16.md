# MotionPlayer runtime host-object offset 注释清理（四参考，2026-08-16）

## 结论

本轮只修正 compiled source 注释，没有改变表达式、分支、字段、类型或调用。目标是清除
`Player+480`、`node+2224`、`item+216` 这类把某一参考 ABI 的物理坐标写成 portable C++
语义的残留，并修正一处由旧 `libkrkr2.so` 语境遗留的 source lookup caller 描述。

四参考已经证明：

- Player 的 queuing byte 在四端是同一逻辑成员，但它在完整 Player 中的 physical offset
  随 ABI 改变；geometry tail 的真实语义是 `_queuing` 为真时把三个 delta 清零，否则用
  accumulated position 减 previous position；
- MotionNode 的 accumulated/delta block、particle slot block、node-level particle output、
  camera FOV 与 feedback timespan 都随完整对象布局移动；字段角色与声明顺序共同，单个
  `node+...` 数字不共同；
- PreparedRenderItem 的 flag group、clip rectangle 与 owning Variant/vector tail 同样受指针
  宽度和 STL/TJS ABI 影响；builder 的共同语义应写成 `drawFlag`、`rawFlag21`、
  `clipRect`，而不是 A64 byte coordinate；
- ResourceManager 的 script-visible `findSource(moduleKey,path)` 通过 dispatch 接收 Player
  保留的 motion-context Variant 与 path。内部 `Player::findSourceForNode_guess` 会构造
  src/icon fallback path，并复用相同 dispatch boundary；其 KRKR/Win native atlas 路径则是
  独立 data flow。旧注释把这条关系压成 `Player+1012`，既不可移植，也混淆了 public
  facade 与内部 resolver。

## 与既有四参考专题的对应

- `motionplayer_particle_inherit_emission_control_four_binary_2026-08-15.md` 已列出九项
  particle evaluator output 的四端不同 offset，并明确指出 `node+2224` 注释不可移植；
- `motionplayer_motionnode_core_comment_migration_four_binary_2026-08-15.md` 已列出 delta
  transform block 的四端基址，并明确要求移除 `node+1584` 一类 inline offset；
- `motionplayer_resource_manager_find_source_handoff_four_binary_2026-08-15.md` 闭合了
  `findSource(moduleKey,path)` 的 module-map lookup、raw-node navigation 与 ObjSource owner；
- `motionplayer_source_resolution_comment_migration_four_binary_2026-08-15.md` 闭合了内部
  node source resolver 的 `SourceState/ResourceManager/src/icon` 参数角色；
- PreparedRenderItem 的 owner/flag/clip 生命周期和精确四端物理表继续由 prepared-item、
  common-builder、Private-GLL 与 Accurate-SLA 专题保存。

因此本轮不是删除逆向证据，而是让 physical coordinates 留在 `analysis/`/recovery IDB，
让 compiled source 只承载字段角色、调用链、所有权和边界行为。

## 修改点

- `PlayerUpdateGeometry.cpp`：`player+480` 改为 `_queuing` 的 delta gate；
- `ResourceManager.cpp`：`Player+1012` 改为 public/internal 两条实际 dispatch data flow，
  ObjSource 的 native sizeof 留回 analysis；
- `PlayerUpdateLayerEval.cpp`：camera/feedback、particle copy/interpolation 与 delta cleanup
  改为字段名和数据流；
- `PlayerRenderExecute.cpp` / `PlayerRenderInternal.cpp`：prepared item 的 flag/clip 行为改为
  `drawFlag/rawFlag20/rawFlag21/clipRect` 语义。

这些位置引用的 IDB 证据已在上述专题完成四端注释和保存。本轮未引入新地址、命名或
反编译结论，所以不需要再次修改 recovery IDB。
