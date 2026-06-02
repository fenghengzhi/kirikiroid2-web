# MotionPlayer 源代码还原 Review（2026-06-02）

> 方法：复用 2026-05-30 全量 12 簇审计 + 05-31 M15 复核 + M1 帧步进机计划为基线，
>   对自 05-31 以来的 delta（砖4-live / 砖5 / 砖6 Stage A 共 13 提交，全部 M1/cluster-G）
>   做 fresh decompile 独立复核。
> 权威：libkrkr2.so 反编译。本轮 fresh decompile：Player_advanceRootAndNodes @0x6B6ADC（4 流完整伪代码）。
> 性质：只读 review，未改 cpp/ 或 IDB。
> 基线：[audit_motionplayer_2026-05-30/MASTER_REPORT.md](audit_motionplayer_2026-05-30/MASTER_REPORT.md)、
>   [MotionPlayer_Restoration_Review_2026-05-31.md](MotionPlayer_Restoration_Review_2026-05-31.md)、
>   [Player_progress_frame_stepping_M1_plan.md](Player_progress_frame_stepping_M1_plan.md)

## 总结论：未达成 100% 还原。地基稳固、M1 帧步进机重架取得真实推进，上层仍存系统性偏差。

六维评分（沿用 05-30 框架，标注本轮变化）：

| 维度 | 状态 | 本轮变化 |
|------|------|----------|
| 对象布局 / 字段偏移 / vtable / 大小 | ✅ 普遍对齐 | 砖3/砖4 加 EmoteObject 40B、D3DEmotePlayer 56B、MotionNode 2632B/ClipSlot 536B 布局契约 |
| 对象生命周期（裸 new/delete vs RAII）| ⚠️ 多数对齐，pimpl/STL 析构序仍偏 | 砖4-live D3DEmotePlayer 双槽懒建 + clear/load destroy-rebuild「100%」 |
| **内部容器实现** | ❌ 系统性 STL 替代 KiriKiri 内联 HM/deque | 未触（4 HM ↔ 6 unordered_map 映射仍未建立） |
| **数据流 / 调用链** | ❌→🟡 progress 子系统正在忠实化 | **M1 layer 事件流 4 提交真实推进**；getVariable/findSource/alpha-mask 仍 open |
| **NCB 类暴露面** | ⚠️ Player 表 05-31 已大修，D3D/EmotePlayer 仍部分错 | 未触（05-31 后无 NCB 改动） |
| 边界行为（默认值 / 分支门控）| ⚠️ progress 门控正在补齐 | layer 事件 +1093 门、type==1 门、align/sync snap 已 live 化 |

**差分现状**：m2logo(93)+yuzulogo(243) 0 mismatch 全绿——但这只覆盖**非-emote logo 渲染路径**，
且 logo trace **不含 type==1 tag-action 帧、无 onAction/onSync 断言**。多数 open P0 在 emote 角色动画 /
变量级联 / 资源缓存 / alpha-mask / 事件触发路径，差分当前不覆盖，故「全绿 ≠ 已还原」结论不变。

---

## 一、本轮 fresh decompile 独立复核：砖6 Stage A（layer 事件流移入 advance/rewind）

**结论：✅ 主张成立，是真实的数据流忠实化。但 advance 单元仍只复刻 4 流中的 2 流。**

### 1.1 Player_advanceRootAndNodes @0x6B6ADC 四流结构（本轮 byte-verified）

二进制此函数在 progress_inner 的每个 advanceRoot 终端点被原子调用，内部按固定顺序跑 4 条流：

1. **layer 流**（+1072 = motion["tag"]，cursor +916/+920/+928）：
   `for(i=count-2; +916<i;){ if(+456 < +928[nextTime]) break; +916++; +920=frames[+916].time;
    +928=frames[+916+1].time; if(propGetInt("type")==1){ content=frames[+916].content;
      if(+1093){ if(content["align"]){+483=1; +456=+1120=+920;}
                 if(+1093 && content["sync"]){+1098=1; +456=+1120=+920; pushSync;} }
      action=content["action"]; if(action) pushAction; } }`
   —— **type==1 门 + +1093-only align/sync 门 + ungated action**，与 §8.7 记录逐字一致。
2. **root 流**（+548 = motion["priority"]，cursor +568/+576/+584）：每步 `+616 = priority[+568].content`
   快照（sub_A0FB64 copy），**无事件门**，仅 content 快照。
3. **variable-track deque**（+1312..+1368，160B stride record，record 内 56B×2 slot，奇偶游标 record+8，
   sub_6B786C 步进 / sub_6B7A70 merge）。
4. **node-deque walk**（LABEL_86，idx≥1）：node+8 子节点 → Player_advanceNodeFrames；否则 inline
   2-slot seek（slot+0 signed vs count-2）+ parseFrame + `(slot+22 & 4)`→per-node pushAction（record.a=
   *(node+0)=label 变量）+ mergeFrameContent + gated findSource。

### 1.2 本端 live 复刻度

| 二进制 advance 单元的流 | 本端 live（PlayerFrameProgress.cpp）| 状态 |
|---|---|---|
| ① layer 流（+916 cursor，type==1 + +1093 门）| `seekLayerEventStreamLike_0x6B6ADC`，在**每个** advanceRoot 等价点 node walk 之前调用（5 处）| ✅ 已 live 化，门控/snap 对齐 |
| ④ node walk | `progressSeekNodeSlotsLike_0x6C106C`（9 处 advance/reseek 点）+ 洞2 per-node action | ✅ 近似 live 化 |
| ② root 流 → +616 content 快照 | **advance 点不 seek**；`_activeMotion+548≈clipList` 仅存储对齐 | ❌ 残留缺失 |
| ③ variable-track deque（160B）| 无 | ❌ DEFERRED |

**净评**：砖6 把二进制「[layer→root→var→node] 原子单元」在 advance 点忠实复刻了 **2/4 流**（layer + node）。
比 05-31 的「末尾单次 layer 扫描」是真实进步（修了 loop-wrap 段内事件丢失 + align/sync 对同次 node walk 的
1 帧延迟），数据流拓扑更贴近二进制。**但 root content-snapshot(+616) 与 var-track 步进仍未进 advance 单元**——
这是 cluster-G 收尾的剩余项，应在 review 中明确记为 open，不能因 layer 流完成而判 M1 已闭环。

### 1.3 per-node onAction 仅非参数化节点（砖5/洞2，commit 6c3aee4）

二进制 LABEL_86 内 per-node pushAction 只在 **inline-seek 分支**（`node+8==0`，非参数化节点）触发；
`node+8!=0` 走 advanceNodeFrames 不在此 fire。本轮 0x6B6ADC 反编译确认此分支结构。commit 6c3aee4
「per-node onAction 仅非参数化节点触发」与二进制一致 ✅。

### 1.4 ⚠️ 仍未验证的静态结论（CI 盲区）

- §8.7 **onAction(void, actionName)**（layer 流首参为空变量）纯静态推导，logo 差分不含 tag-action 帧，
  **runtime 未复核**。这是 record.a = v87（被 sub_A0F778 释放为 void 后仅作 PropGet hint）的链式推断，
  证据充分但建议补含 type==1 tag-action 帧的差分 case + runtime 抓一次实参。这是当前 M1 的最大验证缺口
  （已记入 [[project_motion_event_path_ci_blindspot]]）。

---

## 二、自上次 review 未触动、仍 open 的 P0（结论沿用，未失效）

下列均自 05-30/05-31 起**无代码改动**，本轮未重新反编译（无 delta），结论按基线有效：

| # | 簇 | 摘要 | 来源 |
|---|----|------|------|
| M1（剩余） | G | progress 主推进 LABEL_48 已 live；reseek 三流 Stage B、root/var 流、emote/non-emote 分派拆分仍 open | M1_plan §4 P6/P7 |
| M2 | B | EmoteEngine 6-deque step 全 STUB_WARN，hair/bust 物理未实现，bind-loop 空体 | MASTER P0 |
| M3 | J | getVariable 实为 2-branch scope-router + HM1 join("scope::label") + HM4-first 级联；本端扁平 4 级 fallback，HM4 恒空、HM1 从不读、PSB ranges 是发明 fallback | 05-31 R0-1 |
| M5 | F | buildNodePathKey 完全缺失；节点按扁平 PSB label 索引→重名碰撞 | MASTER P0 |
| M6 | K | doAlphaMaskOperation 整体缺失，且误挂 Player 而非 namespace | MASTER P0 |
| M9 | K | ObjSource 缺 6 成员；RM↔SourceCache 共享实现被拆开；findSource 走 list+shared_ptr 而非双 hashmap+raw upload | MASTER P0 |
| R0-2 | E | setChara 二进制为 tTJSVariant*@+776 + 引用计数 + replay dispatch；本端 ttstr 平凡赋值 | 05-31 R0-2 |
| R0-3 | E | getLoopTime 二进制返回 **TJS Array**（node deque 160B 遍历）；本端返回裸 double | 05-31 R0-3 |
| 容器层 | A/B/E/F/G/J/K/L | 4 内联 HM(+264/+320/+1184/+1240) + 节点/控制器 deque → STL；4↔6 映射仍未建立（P2 地基未做）| MASTER 系统性根因 #1 |

---

## 三、NCB 暴露面：05-31 已大修，残留项未动

- **Player 表（0x6D69C8, 87 成员）**：05-31 已修（11 被删成员回滚、3 kind 错改 method、12 RO/RW 翻正、
  colorWeight/independentLayerInherit 解纠缠）。**残留**：19 个 port-extra 方法仍绑 Motion.Player（host
  adapter 内部调用）；getLoopTime 仍标量（见 R0-3）。本轮未动。
- **D3DEmotePlayer 表（0x52E504）**：4 处别名重复注册（bodyScale/playCallback/setTimeline/addPlayCallback）
  05-31 标记应删，**本轮仍未删**。
- **EmotePlayer 表（0x67FAC8）**：MASTER M14 EmoteObject unique_ptr→裸指针（CLAUDE.md 硬规则）仍未改
  （注：砖3 加了 40B 布局契约，但 unique_ptr 持有方式是否已改为裸 new/delete 需复核）。

---

## 四、建议下一步（按 ROI）

1. **补 tag-action 差分 case**（最高，解 CI 盲区）：含 type==1 tag-action 帧的 motion fixture，断言
   onAction/onSync 触发 + 实参，runtime 复核 §8.7 onAction(void,action)。这是 M1 layer 流唯一未验证环节，
   也守护 [[project_motion_event_path_ci_blindspot]]。
2. **M1 收尾**：advance 单元补 root content-snapshot(+616) + var-track deque 步进（补齐 4 流），
   再做 reseek 三流 Stage B。交 module-alignment-driver 统筹。
3. **低风险即修**（纯绑定动作）：删 D3DEmotePlayer 4 处别名重复注册。
4. **架构级 open P0**（M2/M3/M5/M6/M9 + 容器映射）维持 05-30 分层计划，禁止盲改（差分不覆盖、本地无
   Android oracle）。其中 **4↔6 HM 映射是 P2 地基**，应优先反编译 +264/+320/+1184/+1240 insert/lookup 点。

## 本轮反编译符号参考
| 符号 | 地址 | 本轮确认 |
|------|------|----------|
| Player_advanceRootAndNodes | 0x6B6ADC | 4 流原子单元；layer type==1 门 + +1093-only align/sync；per-node action 仅 node+8==0 |
