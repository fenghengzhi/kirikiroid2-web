# MotionPlayer 源代码还原 Review（2026-06-05）

> **2026-07-19 M9 纠正：** 本文 `findSource → ObjSource dict facade` 的描述
> 已被后续 fresh decompile 证伪。当前实现与二进制均为 mapped `PSBFile` raw
> root → retained raw owner/node + lazy texture；strict/try 读取、clip、纹理物化、
> drawLayer、adaptor 失败泄漏与析构顺序均已闭合。正文相关旧表项只表示当时状态。
>
> **2026-07-23 P3-B/render CURRENT CORRECTION：** 本文把 Player+992
> `findMotion/loadMotion`、Player+656 `ResourceManager.bufLayer` 和 layer-id
> dispatch 列作 defer 的文字也已过时：三者均已恢复。普通渲染 SLA 已改为
> active/retired 双树 pass，并由 `0x6C4E28` Loop A leaf + Loop B
> group/composed 产出供 `0x6C7440` 消费。仍 open 的是 SLA payload/J8、完整
> alpha-mask 等后续对齐项；本文作为 06-05 历史 review，不能用于证明整体
> 100% 完成。

> 方法：以 [MotionPlayer_Restoration_Review_2026-06-03.md](MotionPlayer_Restoration_Review_2026-06-03.md) 为基线，
>   对自 06-03 以来 dev/motion 的 **35 个提交** delta（M1 帧步进 4 函数边界收敛 / NCB 注册表 1:1 收敛 /
>   M2 EmoteEngine 6-controller + 弹簧实装 / draw 原语→TJS dispatch 迁移 / anchor color 方向修复）做
>   **fresh decompile 独立复核**（5 个 agent 并行：M1 帧步进 / NCB 注册表 / M2 EmoteEngine / 类布局-容器 /
>   渲染-anchor-source，各自重新反编译目标函数，不信任 stale IDA 注释与 stale 文档）。
> 权威：libkrkr2.so 反编译。
> 性质：只读 review，未改 cpp/ 或 IDB（agent 仅更新各自 agent-memory）。

## 总结论：仍未达成 100% 还原，但已非常接近。本轮**未发现新真实回归**，反而纠正多条 stale「待办/缺失」误判。

剩余偏差集中在 3 类：① M1 反向 root 扫描真缺失（**高**）② findSource/blend 源容器偏差（中，FNV 算法已取齐可推进）
③ ctor/dtor 机制 + EmoteObject scriptObject 槽（架构级，方法论框架内多属可接受）。

六维评分：

| 维度 | 状态 | 本轮变化 |
|------|------|----------|
| ① 源码结构 / 函数边界 | 🟡 普遍对齐 | M1 帧步进 4 函数边界已收敛；字段按声明序非字节序（ABI 必然，方法论允许，无 `#pragma pack` 硬凑✅）|
| ② 数据流 | 🟡 持续忠实化 | anchor 数据流✅、setVariable cases4-8 路由✅、getVariable scope-router✅；**rewindRoot 反向段数据流缺失（高）**|
| ③ 调用链 | ✅ 普遍对齐 | draw 原语全经 TJS dispatch (vtbl+16 FuncCall)✅、6 controller step 顺序✅ |
| ④ 对象生命周期 | ⚠️ 机制偏差 | ctor 多 parentPlayer 参 + RM 为 native 非 dispatch；dtor 手写有序→RAII 逆序（框架内可接受）；EmoteObject +0 scriptObject 槽缺失（inert）|
| ⑤ **内部容器实现** | ✅ **完全对齐** | **2 处 string→ttstr key retype 已完成（CLOSED）**；4 HM = libstdc++ `unordered_map` 选型对齐；var-track 56B slot / node 2632B deque 对齐 |
| ⑥ 边界行为 | ✅ 普遍对齐 | **anchor color base 方向已修正** `isDefaultBlend?128.0:255.0`（byte 确证）；per-vertex-color 判定为正当平台边界（下钉 bake 消费点 0x6A7518）；float-bits raw 读取全对✅；**blend 源仍单值（🟡）**|

**差分现状**：与基线一致——m2logo+yuzulogo 0 mismatch 仅覆盖非-emote logo 路径，不含 anchor/var-track/
getVariable/event/emote 物理路径。emote 路径无 fixture/oracle，验证=反编译+构建（CLAUDE.md 证据阻塞/验证尽力）。

---

## 一、M1 帧步进 / timeline — 🟡 部分对齐，含本轮**唯一高优先级真实功能缺口**

fresh decompile：`advanceRootAndNodes@0x6B6ADC`、`rewindRootAndNodes@0x6B9A3C`、`advanceNodeFrames@0x6B7E44`、
inline seek `0x6B73DC`/`0x6BA1CC`、`reseekTimelineCursors@0x6B86C8`、`initNodeTimeline@0x6B64AC`。

| 函数 | 裁决 | 证据 |
|------|------|------|
| advanceNodeFrames @0x6B7E44 | ✅ 对齐 | LIVE 实现在 PlayerUpdateLayerEval.cpp |
| inline seek 0x6B73DC(前向)/0x6BA1CC(反向) | ✅ 对齐 | 方向边界正确 |
| advanceRootAndNodes @0x6B6ADC | 🟡 部分 | root content-snapshot stream②(+616) 已接入 |
| rewindRootAndNodes @0x6B9A3C | 🟡 部分 | **见 R-B1** |
| reseek（@0x6B86C8 入口） | 🟡 部分 | **见 R-C1** |

**定性纠正**：基线任务里写的「reseek @0x6B91B0」**不是独立函数**——`0x6B91B0` 是 `Player_reseekTimelineCursors`
（入口 `0x6B86C8`，跨至 `0x6B92E8`）内部 per-node `initNodeTimeline` 循环体的标号。

### Open 偏差（按严重度）
1. **🔴 R-B1（高，唯一真实功能缺口）** `rewindRootAndNodes` 的 root 反向段 @0x6B9E84：二进制有独立
   `while(+576 > +456){ --+568; ... }` 反向递减循环；本地复用了**前向-only** seekRoot → 反向 root 扫描缺失。
   且 [PlayerFrameProgress.cpp:1022](../cpp/plugins/motionplayer/PlayerFrameProgress.cpp#L1022) 注释「无反向 root scan」
   被本轮反编译**证伪**，须就地纠正。→ **本 review 后已派 binary-aligned-implementer 攻克。**
2. **🟡 R-C1（中）** `initNodeTimeline@0x6B64AC` tail @0x6B674C 的 per-node action push 在本地
   `initializeNodeTimelineSlotsLike_0x6B64AC` 完全缺失，影响 reseek loop-wrap + firstFrame seed 的 onAction。
3. **🟡 R-A1（中，待核实）** 三处 node-loop 上界：二进制 `1..dequeSize-2`（末节点不入循环）vs 本地
   `i<_nodes.size()`=`1..size-1`；需确认 `_nodes.size()` 与二进制 deque 元素数是否 1:1。
4. **R-C2（低，已正确 DEFERRED）** reseek tail pruneHM3 + `sub_6B9650` aux-list。

记录：`.claude/agent-memory/krkr2-impl-diff/m1_framestep_rootstream_verdict.md`（R-4 已合并）。

---

## 二、NCB 注册表 — ✅ 高度 1:1

fresh decompile registrars：Player `0x6D69C8`、EmotePlayer `0x67FAC8`、D3DEmotePlayer `0x52E504`、
ResourceManager `0x6AB8BC`、ObjSource `0x69CCB8`。

- 成员计数全对齐：**ResourceManager 12 / ObjSource 6 / Player 92 / EmotePlayer 69+2const / D3DEmotePlayer 54+4const**。
- angleDeg/angleRad getter 方向正确（历史 swap 已修复，binary v98→getAngleDeg / v101→getAngleRad 字面确认）。
- D3DEmotePlayer 4 处 name/callback 错配（clear→create / setTimelineBlendRatio→setTimeline /
  pass→addPlayCallback / modified→getPlayCallback）已忠实复刻，**无别名重复残留**（基线遗留项已 closed）。

### Open
- **O-1（低危）** D3DEmotePlayer 4 个常量（MaskModeStencil/Alpha + TimelinePlayFlagParallel/Difference）binary 注册在
  D3DEmotePlayer 类自身（0x52e5a0-0x52e5e8），本地放在 D3DEmoteModule（main.cpp:839-843）。标量 int，dict
  顺序/类归属无 shape hazard。

**本轮纠正 2 个误判**：(1) naive 正则只匹配 `sub_6F6970` 误得 Player=76 → 实际用 3 个 helper（descriptor +
2 个直接注册），全集 92 成员，**证实** memory/注释的 92 计数。(2) O-2「本地 EmotePlayer 缺 TimelinePlayFlag 常量」
是 `NCB_`-关键词 grep 落空导致的**假缺失** → 实际在 main.cpp:468-471 用 `Variant(...)` 宏注册（已撤销）。

---

## 三、M2 EmoteEngine — ✅ 基本忠实复刻

fresh decompile：builder `0x67D4D0`、setVariable keystone `0x671228`、step orchestrator `0x67D01C`、
弹簧 `0x662768`/`0x6689A4`、chain-spring `0x67C560`、clampcontrol `0x67C8A8`。

| 维度 | 裁决 | 证据 |
|---|---|---|
| setVariable cases 4-8 路由 | ✅ | 0x671228 全 5 路由齐全 + case6 dual-key + case7/8 flag gate + easeWeight 三分支 + cases0/1/2 syncWaiting gate（EmoteEngine.cpp:1645）|
| 6 controller step 推进/插值 | ✅ | 0x67D01C step 顺序对齐（selector-before-transition、mouth dual-HM）（EmoteEngine.cpp:1835-1907）|
| float-bits 读取 | ✅ | powField 全用 memcpy raw-bits（非 SCVTF）|
| 弹簧物理 bust/hair | ✅ **真实实装非 STUB** | 0x662768/0x6689A4 完整 sinf/cosf/atanf/sqrt/fmod（EmoteSpring.cpp:31/120）；无 STUB_WARN |
| deque 元素布局 + 容器选型 | ✅ | mouth 24B dual-HM、#4/5/8/9/10 stride + spring 48B/56B；typed `std::deque` + HM6 EmoteVarRefMap |

### Open
- **❌（低危，死路径）** [PlayerFrameProgress.cpp:2001-2014](../cpp/plugins/motionplayer/PlayerFrameProgress.cpp#L2001)
  的 legacy `_typeNControllerAnimators` stepping：注释自称对齐 0x67D01C，但 controller stepping 实由
  EmoteEngine::progress 的 typed-deque loop 承担；grep 全 cpp 无任何 push 进 `_typeNControllerAnimators` →
  此路径（含 PlayerCore.cpp:116-156 辅助）是 06-03 旧并行模型 dead 残骸。建议删除或改正误导性注释。
- 基线「container ❌」裁决已**过时**（容器选型本轮确认对齐）。

---

## 四、Player / EmotePlayer 类布局 — ⚠️ 容器✅、生命周期机制偏差

fresh decompile ctor `0x6CED30`、dtor `0x6CFADC`，逐字段比对 `player_containers.h`/`value_structs.h`/`ttstr_hash.h`。

### ⑤ 内部容器实现 — ✅ **完全对齐，无 open 偏差**
- 4 个 HashMap = 标准 libstdc++ `std::unordered_map`（ctor 调 `_M_next_bkt(0xA)` + 1.0f load factor +
  `_M_before_begin` 单链），**仅 hash 函数自研**（ttstr UTF-16 hash，`ttstr_hash.h:26` byte-for-byte 复刻
  `(1025*x)^((1025*x)>>6)`→`9*acc`→`32769*(h^(h>>11))`）。本地 `unordered_map<ttstr,V,ttstr_hash,ttstr_equal>`
  是正确 1:1 选型。**不存在「STL→KiriKiri 内联 HM」P3 重构目标（该前提本身有误）。**
- **基线遗留「2 处 std::string→ttstr key retype」已全部完成（CLOSED）**：`_evalResultValues`(Player.h:1446,
  ttstr-key) + `_nodeLabelMap`(Player.h:1292, `std::map<ttstr,int,ttstr_utf16_less>` 复刻 RB-tree cmp
  sub_9B1ED0)。注：基线行号 `:1159/:1294` 已漂移到 `:1292/:1446`。
- var-track deque +1296 56B slot 模型、node deque +184(2632B/elem)、+24 node map、+384 render array(vector)
  全部容器选型 + key 类型对齐。
- 无 `#pragma pack`/`_padN`/`static_assert(offsetof==N)` 硬凑（grep 全空）✅。

### ④ 对象生命周期 — ⚠️ 机制偏差（方法论框架内多可接受）
- ctor 签名多 `parentPlayer` 参 + RM 为 native 对象非 dispatch（字段顺序漂移根因之一）。
- dtor：二进制手写有序 teardown（HM4→HM3→...→HM2→HM1 逆序）vs 本地全靠 RAII 成员逆序析构。属「全 std 容器
  复刻源码」下游必然，框架内可接受；`EvalCascadeState`/`PerNodeLayerState` 成员声明序=升序偏移，逆序析构=降序，
  刻意对齐 `Player_HM1_value_destroy`(0x6DD1A0)/`Player_HM3_value_destroy`(0x6DD06C) release 顺序✅。
- EmoteObject(40B)：✅ **「+0 是 scriptObject、本地缺槽」是误判，已证伪(2026-06-05 后续，亲自反编译 sub_6A88CC)**。
  `new(0xE8)` via `sub_6A88CC` **就是 ResourceManager ctor 本身**（内含 `sub_6A78F4`=SourceCache
  `std::list`；2026-07-23 由 `_List_node_base::_M_hook/_M_unhook` 纠正旧 intrusive 误判、
  `a1+88=new(8*_M_next_bkt(0xA))`=findSource@0x6AAB3C 读的 FNV bucket map「HashMap A」、`new Math.RandomGenerator()`、
  +176 RB-tree），**不是独立 scriptObject**。2026-07-18 纠正：旧本地 +0 是 by-value/shared-state RM，且 adaptor 又创建一个 RM，并不匹配；现已改为 EmoteObject 唯一 owning `ResourceManager*` + sticky adaptor 指向同一对象。
  +16 vector by-value vs by-ptr 是 variant 内含 refcount 的等价建模。
- MotionNode 2632B：deque 元素内部数据契约（按字节读 frame slot），属方法论允许保留的元素 POD 数据格式。

### Open
| # | 偏差 | 严重度 |
|---|------|--------|
| ~~EmoteObject +0 scriptObject 槽缺失~~ **✅ 误判已证伪**：sub_6A88CC=RM ctor；2026-07-18 又纠正旧 by-value/shared-state 适配，现为唯一 owning RM 指针 | ~~低(inert)~~ closed |
| ~~ctor 签名多 parentPlayer + RM native 非 dispatch~~ **✅ P3-B 已解**：ctor 单参 dispatch-in(0x6CED30)、删 native value RM、nativeRM() 解包(0x694928)、parent 移出 ctor(0x6b43dc)、child 继承 parent dispatch(0x6b43cc)；后续又补齐 findMotion/loadMotion +992 FuncCall、+656 RM.bufLayer buffered 分支及零参 layer-id dispatch/set。SLA payload/J8/完整 alpha-mask 属独立 render open 项 | ~~中~~ closed（限本行 P3-B 范围） |
| port-invented map：`_timelines/_playingTimelineLabels` 已于 2026-07-19 删除；`_motionsByKey` 等其余 snapshot/host map 仍待迁移 | 中 |

---

## 五、渲染路径 / anchor / M9 source — ✅ 大体对齐，2 处容器偏差

fresh decompile：draw 主路径 `0x6C7440`、vertex builder `0x6C715C`、`evaluateAnchorNodes_type10@0x6C0528`、
color 消费链 `0x6C7440→0x6C1B70→0x6A7518`、findSource。

| 点 | 裁决 | 证据 |
|---|---|---|
| ① anchor color base 方向 | ✅ | `qword_14D7C50={255.0,128.0}` byte 确证，本地 `isDefaultBlend?128.0:255.0`（PlayerUpdateAnchor.cpp:144-146）正确 |
| ② blend 源 | ✅ **已修复(2026-06-05 后续)** | 二进制 anchor type10@0x6C0528 在 `0x6c0a80/0x6c0aac` 读 `*(node+536*activeSlotIndex+44)`(=active slot 的 blendMode，slot0@node+320/slot1@node+856,+44=ClipSlot::blendMode；slot index `*(node+1392)`@0x6c06d4)。本地原读 node 级缓存 `interpolatedCache.blendMode`，已改为直读 `activeSlot().blendMode`(PlayerUpdateAnchor.cpp:144-152) |
| ③ 4-corner color（phase-D）| ✅ **2026-07-23 纠正为原始分支复刻** | `0x6C7440→0x6C1B70→0x6A7518` 在软件分支做 per-pixel bake；GPU 分支只查询并丢弃 PrivateMotionGLL native。vertex builder 无 color。本地现复刻这两条分支，不再把它包装成“per-vertex 能力缺失”的平台边界。|
| ④ findSource/SourceCache 容器 | ✅ **类归属与选型均纠正** | `SourceCache_ncb_registerMembers@0x6A85A8` 无 findSource；`loadSource@0x6A7BA8` 的源码容器由 `_List_node_base::_M_hook/_M_unhook` 证实为 `std::list<Entry>`，旧“手写 intrusive”误判已删除。`ResourceManager::findSource@0x6AAB3C` 的独立 HashMap A 是 libstdc++ `unordered_map`+FNV，产出 raw-node ObjSource。两条容器链均已按各自证据选型。|
| ⑤ draw 原语 TJS dispatch | ✅ | 全部经 vtbl+16 FuncCall + UTF-16 method name 派发，vertex builder type-5/20B 忠实 |

---

## 六、仍 open 的偏差清单（按 ROI）

| # | 偏差 | 维度 | 严重度 | 状态 |
|---|------|------|--------|------|
| 1 | rewindRootAndNodes 反向 root 递减循环缺失（@0x6B9E84）+ 证伪注释 PlayerFrameProgress.cpp:1022 | ②① | **高** | **✅ 已实装**（反向循环体经 auditor 逐行确认 1:1，见下 §七）|
| 1b | seekRootContentStreamLike 入口每-tick 重算 `_rootCurTime`/`_rootNextTime`，违背二进制 +576/+584 跨调用持久持有 | ② | 中 | **✅ 已修复**（删入口重算；seed 责任归 reseek tier 0x6B86C8，见下 §七）|
| 1c | firstFrame `_queuing` 分支（~1950-1968）未调 reseekTimelineCursors，二进制 progress_inner firstFrame 分支会 → 潜在 firstFrame root seed 缺口（先前被 1b 入口重算掩盖）| ② | 中 | **✅ 已修复(2026-06-05 后续)**（_queuing 分支改调完整 `reseekTimelineCursors`，含 layer/root/var-track/绝对 node re-seed；progress_inner firstFrame @0x6C10E0/0x6C131C 证据；构建+差分 PASS）|
| 2 | initNodeTimeline tail per-node action push 缺失（loop-wrap onAction） | ② | 中 | **✅ 已实装(2026-06-05 后续)**（tail @0x6B674C action push 复刻到 PlayerUpdateLayerEval.cpp，接 reseek/dirty-rebuild 两调用点；构建+差分 PASS）|
| 3 | blend 源单值 vs active-slot `node+536*activeSlotIndex+44` | ⑥ | 中 | **✅ 已修复(2026-06-05 后续)**（PlayerUpdateAnchor.cpp:144-152 改读 `activeSlot().blendMode`）|
| 4 | ~~findSource 容器替换（SourceCache）~~ **类归属误判已纠正** → 真目标=ResourceManager::findSource@0x6AAB3C(FNV map→ObjSource)，已忠实重写函数体；SourceCache list 同构忠实不动；残留=RM 第一层容器 STL→内联 FNV bucket map(phase-D) | ⑤ | 中→低 | 函数体✅；容器选型 open(phase-D) |
| 5 | `_type4..8ControllerAnimators`（LegacyVariableAnimatorState）死路径残骸 + 误导注释 | ① | 低 | **✅ 已移除**（反编译 0x67D01C/0x671228 证伪：二进制无独立 Player animator bucket；删 5 deque+map+6 访问器+死 loop+legacy_variable_state.h；差分逐位 PASS，见下 §七）|
| 6 | EmoteObject +0 槽 / ctor 签名 / D3DEmotePlayer 4 常量类归属 | ④ | 低(inert) | **基本处理**：(A) D3DEmotePlayer 4 常量已移到 D3DEmotePlayer 类(main.cpp，0x52E504 证据)✅；(B) EmoteObject「scriptObject 槽缺失」**误判已证伪**，且 2026-07-18 已把旧 by-value/shared-state RM 纠正为唯一 owning RM 指针 + sticky adaptor✅；(C) Player ctor 单参 dispatch-in、findMotion/loadMotion +992 FuncCall、+656 RM.bufLayer 和零参 layer-id dispatch/set 均已收敛✅。D3DEmotePlayer 壳层 `_rm` 仍需按独立证据审计；SLA payload/J8/完整 alpha-mask 是 render open 项 |

**需就地纠正的被证伪注释（CLAUDE.md 硬规则）**：
- ~~PlayerFrameProgress.cpp:1022「无反向 root scan」~~ **✅ 已纠正**（#1 实装时引用 0x6B9A3C/0x6B9E84）
- ~~PlayerFrameProgress.cpp:2001-2014 死路径「对齐 0x67D01C」表述~~ **✅ 已纠正**（#5 拆除时改为「stepping 由 EmoteEngine typed-deque 模型承担，平行 bucket 为死残骸已移除」）

---

## 七、本 session 落地的修复（2026-06-05 同日，证据驱动 + 构建/差分验证）

> 性质：本 review 后按用户 `a&b` 指示推进。全部 fresh-decompile 证据驱动 + 构建通过 + logo 差分逐位非回归。

### #1 ✅ rewindRootAndNodes 反向 root 扫描（高优先级唯一真实功能缺口）
- **实装**：`seekRootContentStreamLike_0x6B6ADC` 改为双向，反向段逐行对应 `@0x6B9E84` 的 `while(curTime+576 > target+456){ --cursor; content→+616 快照; nextTime=curTime; curTime=item.time }`；rewind 路径不再复用前向-only 近似。
- **独立 auditor 复核裁决 ⚠️ 部分**：反向循环体的 **4 个镜像陷阱维度（单项 byNum 取值 / 时间赋值方向 nextTime=oldCur·cur=item.time / 计算顺序 / 严格 GT 门控）逐行精确对齐**；但发现衍生偏差 #1b（入口 persistent-hold，见上表，修复中）。
- 注释 PlayerFrameProgress.cpp:1022「无反向 root scan」就地纠正。oracle-inert（logo priority 单 clip，反向门控恒假）——非回归守护非降优先级理由。

### #1b ✅ root 时间游标 persistent-hold（auditor 在 #1 复核中发现）
- **两层状态机（fresh decompile 4 函数确证，纠正 auditor 推断）**：init `0x6B3778` 只 seed +548/+616，**不写 +568/+576/+584**（auditor 推断「init seed +576=priority[cursor].time」✘ 不成立）；advance(`0x6B6ADC`)/rewind(`0x6B9A3C`) 循环入口只读、体内 carry+refetch；唯一从 cursor 全量重算的是 reseek tier `0x6B86C8`（firstFrame + loop-wrap 0x6C1488/0x6C1428）。
- **修复**：删 seekRootContentStreamLike 入口每-tick 重算（旧 ~1090-1091），+576/+584 改由成员默认 0.0 跨调用持久持有、仅 reseek seed + 循环增量演进；本地 `reseekTimelineCursors` 已存在且已接 loop-wrap，移除不孤立 seed。构建 PASS、差分逐位 PASS。
- **带外新发现（#1c，已记 memory）**：firstFrame `_queuing` 分支未调 reseek，二进制会 → 先前被入口重算掩盖的 firstFrame root seed 缺口，inert，留待后续 session。

### #5 ✅ `_type4..8ControllerAnimators` 死路径残骸移除
- **反编译证伪**：`0x67D01C` controller stepping 只读 typed-deque（`+256/+336/+416/+576/+656/+736`），输出唯一去向 `Player_HM2_upsert`→HM7；`0x671228` cases 4-8 索引同一批 deque。**二进制无任何独立 Player 端 animator bucket**。本地 LIVE 容器（`_stateMachineDeque4/5`、`_compositeVarDeque6`、`_auxVarDeque8`、`_vectorVarDeque9`、`_lookupCurvesDeque10`）已 1:1 覆盖；`_type4..8ControllerAnimators`(+`_variableAnimators` map) 跨整个 cpp/ 零写入 → 06-03 被取代的冗余平行模型。
- **移除**：5 deque + map 成员（EmoteEngine.h）、`VariableKeyframe`/`VariableAnimatorState` 别名 + 6 访问器（Player.h/PlayerCore.cpp）、`findInDeque`/`eraseInDeque` helper、stepControllerBucket 死 loop（PlayerFrameProgress.cpp）、所有 clear/erase 调用点、**删 `internal/legacy_variable_state.h`**。
- **refresh 改写**：`refreshFixedControllerEvalOutputsLike_0x67D01C` 去死 bucket 分支，保留 binary-faithful `_evalResultValues`(HM7/HM2 镜像)→`getVariable` fallback。
- 构建 web debug 240/240 + wasmtime guest 693/693 PASS；差分 m2logo(93)/yuzulogo(243) 逐位 PASS（死容器恒空 → byte-identical）。

### 方法论旁证（CLAUDE.md「强断言需独立交叉核实」）
- #5 动手前，主线对「死容器」断言做了多搜索角度交叉核实：字面 `_typeNControllerAnimators` 首次 grep 落空（M2 报告用的是 generic 占位名，真实符号为 `_type4..8ControllerAnimators`）——印证了「grep 空 ≠ 不存在」，未据空 grep 误判；改用宽松正则 + 反编译两个 keystone 才确证。
- 同型纠正：`advancenodeframes_0x6B7E44_convergence.md` memory 称 m2logo「100-vs-93 帧 pre-existing 回归」在当前 HEAD 已证伪（m2logo PASS 93 帧），已标注过时。

## 本轮反编译符号参考
| 符号 | 地址 | 本轮确认 |
|------|------|----------|
| rewindRootAndNodes | 0x6B9A3C | 反向 root 段 @0x6B9E84 独立递减循环（本地缺失，高优先级缺口）|
| reseekTimelineCursors | 0x6B86C8(标号 0x6B91B0) | 非独立函数；内部 per-node initNodeTimeline 循环 |
| initNodeTimeline | 0x6B64AC(tail 0x6B674C) | per-node action push（本地缺失）|
| Player NCB registrar | 0x6D69C8 | 92 成员（3 helper：sub_6F6970 descriptor + sub_6D993C/sub_6D97B4）|
| EmotePlayer / D3DEmotePlayer / RM / ObjSource registrar | 0x67FAC8 / 0x52E504 / 0x6AB8BC / 0x69CCB8 | 69+2 / 54+4 / 12 / 6 |
| EmoteEngine builder / setVariable / step | 0x67D4D0 / 0x671228 / 0x67D01C | cases4-8 路由 + 6-deque step 顺序对齐 |
| 弹簧 bust/hair | 0x662768 / 0x6689A4 | 真实物理实装（sinf/cosf/atanf/sqrt/fmod）|
| Player ctor / dtor | 0x6CED30 / 0x6CFADC | 4 HM = libstdc++ unordered_map；手写有序 teardown |
| anchor type10 / color base | 0x6C0528 / qword_14D7C50 | `isDefaultBlend?128:255` 正确；{255.0,128.0} byte-verified |
| draw 路径 / vertex builder / color bake | 0x6C7440 / 0x6C715C / 0x6A7518 | TJS dispatch；type-5/20B 位置对；per-pixel bake（非 per-vertex）|
