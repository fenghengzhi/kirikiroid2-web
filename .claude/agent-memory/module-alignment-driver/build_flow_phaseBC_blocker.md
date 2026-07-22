---
name: build-flow-phaseBC-blocker
description: render-step build_flow_mismatch (layerResolved20/item+20) Phase B+C 重构二度评估——确认无法安全实施，核心阻塞是 port 缺 player+760 持久 SLA 成员；记录以免反复立项
metadata:
  type: project
---

motion::Player render-pipeline `build_flow_mismatch`（render-step compare 报 `mainListSemanticItems.items[*].flags.layerResolved20` = item+20，92/242 帧，被 `--allow-render-flow-diagnostics` 容忍不 fail CI）的 Phase B+C 重构，2026-05-30 module-alignment-driver 二度评估（首度评估为 binary-alignment-fixer，见 [[sub6C4E28_build_execute_split]]）。

**判定：无法安全实施，未改任何 cpp/。** 与首度 fixer 结论一致，但补充了决定性的新证据。

**新发现的决定性阻塞（首度未点明）：**
- oracle sub_6C4E28 @0x6C5DBC 的 item+20 闩锁（LABEL_28 @0x6c514c）门控 = `drawFlag19 && drawable(v80<v84 && v83<v85 && !item+16)`，且分支于 **`player+760`（持久 SLA-adaptor 成员）**：`player+760!=0 && !item+20` 直接 goto LABEL_28；`player+760==0` 则**创建** SLA(Window.mainWindow.primaryLayer) 存入 player+760 再 LABEL_28。
- **port 根本没有 `player+760` 这个 Player 成员**（grep Player.h 无对应字段）。port 的 SLA-adaptor 是 execute 期 `Player::renderToSeparateLayerAdaptor`(PlayerRenderTargets.cpp:1245) 从 TJS caller 传入的 `slaObject` **临时**取 native instance，不是 build 可读/可懒创建的常驻字段。oracle LABEL_28 门控本质依赖 player+760 的存在性/创建，port build 阶段无等价物。
- trace 采样点：`build_commands_leave` = sub_6C4E28 **onLeave**（frida agent:3400，PLAYER_BUILD_COMMANDS_OFF 即 sub_6C4E28）→ 读的是整个 build 循环跑完后的 item+20。oracle 大量 item 读 0 = 这些 item 那些帧根本不满足 drawFlag19&&drawable（走 0x6c5e6c else 只清 item+21、不碰 item+20，保持初始 0）。
- **Historical snapshot:** port 侧 rawFlag20 当时经 `_renderItemNativeFieldLifetimeByNode` 跨帧恢复。**2026-07-23 correction:**该 side map 已删除；每个 `MotionNode` 直接拥有 persistent `PreparedRenderItem`，caller-stack main/aux lists 只借用指针。后续 UPDATE 的 Phase B+C 结果及当前 node-owned lifetime 才是现行状态。

**为何不能窄修（仅 gate flag）：** 违反 CLAUDE.md 禁止打补丁。port rawFlag20 语义=「execute 期 leaf layer 对象已建」，oracle item+20 语义=「build 期 requireLayerId 属性已物化」——是**不同事件**。仅按 oracle 门控 gate flag 值 = hollow latch（无 requireLayerId 属性读支撑），且 port 下游 execute 按「leaf 已建」消费 rawFlag20，gate/清它会改 execute 行为、威胁当前 0-mismatch 全绿的 trace compare。

**忠实 Phase B+C 的真实代价（确认过大）：** 须(1)给 Player 加 player+760 持久 SLA 成员并实现 build 期懒创建（含 Window.mainWindow.primaryLayer 解析）；(2)把 execute-only 的 layer-object 解析（scratchOwner/scratchParent/SLA）搬进 buildRenderCommands；(3)把 NodeTree.cpp:103 提前的全局 `ResourceManager::requireLayerId()`（分配域 A）改成 build 期对解析出的 render-layer 对象的 `requireLayerId` **属性**读（分配域 B，item+424）。(3) 改 layerId 值域，正是首度 fixer 指出会破坏当前正确输出的点。三者纠缠，无法产出「编译通过+不破坏绿 trace」的自洽改动。

**给后续的建议：** 此项 correctness 已完美（trace 0-mismatch），build_flow 仅是 diagnostics 类保真度缺口、不 fail CI。除非先独立完成「Player 引入 player+760 SLA 成员 + layerId 惰性属性模型」的预备重构（自身就是大块对齐工作），否则不要再单独立 Phase B+C。优先级应低于会 fail CI 的对齐项。

---

**2026-05-30 UPDATE — Phase B+C 已实施并编译通过（验证模型变更后）。** 用户显式建立 push→CI 差分→回归则 revert 循环，完成标准改为「忠实 oracle + cmake 编译通过」，运行时回归由 CI 判定。先前「blocked」判定基于「本地无法验证不破坏绿 trace」——该约束在新模型下不再是停止理由。已实施改动（编译通过 [269/269] linking）：
- **Player.h**：新增 `SeparateLayerAdaptor *_renderSeparateLayerAdaptor`（player+760，裸指针），`~Player()`（PlayerCore.cpp，原 `= default`）改为显式 `delete`。
- **buildRenderCommands**（PlayerRenderExecute.cpp drawable 分支）：复刻 LABEL_28——`player+760` 不存在则 `new SeparateLayerAdaptor(Window.mainWindow.primaryLayer)`；`if(!rawFlag20)` 则 `entry.layerId = requireLayerId(node.layerName)` + `rawFlag20=true`。门控 = drawFlag19 && drawable && !rawFlag20，与 oracle 一致。
- **execute 阶段去置位**：删 PlayerRenderExecute.cpp ensureLeafItemLayer + PlayerRenderTargets.cpp ensureAccurateSlaItemLayer 的 `rawFlag20=true`+re-persist，改为消费 build 已物化值。
- **值域决策（避免不可验证的 Phase-C 回归）**：未删 NodeTree.cpp:103 的 eager `requireLayerId()`。port `Player::requireLayerId(name)` 对 labeled 节点 return `node.layerId1`（PlayerResource.cpp:90），即 build-side 重解析 = 同一已验证值域；这给 once-only latch 真实 backing 调用（非 hollow flag）而不改 layerId 值域。eager 分配（域 A）与 build 期重读共存、不冲突，故 task#4 的「若冲突则改惰性」条件未触发。
- **回归风险点（供 push 后看 CI）**：(1) build_flow_mismatch 应转绿（rawFlag20 不再 execute 泄漏）；(2) lifetime-map 仍持久化 rawFlag20——符合 oracle 一次性 latch 跨帧持久语义；(3) layerId 值未变，leaf-layer state keying 不应回归；(4) SLA 在 build 期惰性创建——若 Window.mainWindow.primaryLayer 解析时机早于 execute 路径原有解析，可能影响 SLA 内部 absolute 计数顺序，重点看 accurate-SLA / SLA render compare case。
