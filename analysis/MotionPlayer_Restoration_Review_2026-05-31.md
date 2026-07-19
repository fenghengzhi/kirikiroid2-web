# MotionPlayer 源代码还原 Review 报告（M15 增量复核）

> **2026-07-18 字段纠正：** 本文关于 setChara `tTJSVariant*@+776`
> 的结论受 NCB 表 off-by-one 误读影响，已证伪。权威映射为 chara
> +960、stealthChara +968、motion +976、stealthMotion +984，均为
> refcounted string-value owner（源码层 `ttstr`）；+768/+776 分别是
> pending stealthMotion/stealthChara；+1099 是 playing。

> 日期：2026-05-31
> 方法：5 个 `binary-alignment-auditor` 子 agent，针对 2026-05-30 全量审计之后的 ~40 个 M15/M11/M20/M3 提交（全部标 `[需 CI 验证]`，即尚未验证）逐项反编译复核
> 范围：自上次审计以来**实际变化的表面**——Player NCB 暴露面、transform root-delta setter、M15/M16 accessor 副作用、getVariable HM 级联、D3DEmotePlayer NCB 表
> 性质：只读复核，未修改 cpp/ 或 IDB
> 基线：[MotionPlayer_Restoration_Review_2026-05-30.md](MotionPlayer_Restoration_Review_2026-05-30.md)、[audit_motionplayer_2026-05-30/MASTER_REPORT.md](audit_motionplayer_2026-05-30/MASTER_REPORT.md)

## 总结论

**地基依旧稳固，M15 在 NCB 暴露面/transform setter 上取得真实进展，但 100% 还原仍未达成。**两类问题：(A) M15 自身引入的新偏差（属性/方法绑定 kind 错、RO/RW 错、可能误删二进制成员）；(B) 上次审计的核心 P0（getVariable 级联、setChara、getLoopTime、容器选型）M15 并未触及，仍 open。

| 复核区 | 提交 | 结论 |
|--------|------|------|
| Player NCB 表 (0x6D69C8) | M15 多项 | ⚠️ 大量正确新增，但 3 kind 错 + 12 RO/RW 错 + 疑似误删 11 个二进制成员 |
| transform root-delta setter (0x6C0F1C..) | M20 6ebedd8 / M15 3925e05,9e064ed | ✅ 主张成立（写 root +200 delta 槽 + dirty），2 处残留偏差 |
| M15/M16 accessor 副作用 | bb0d3eb,8e0747e,da0e9a2.. | ⚠️ setTickCount/lastTime/setCoord ✅；setChara/getLoopTime ❌；4 项未定位 |
| getVariable HM 级联 (0x533E1C) | M3 d91fe86 / M4 651533e | ❌ M3 READ 结构主张**不成立**；M4（HM4 value=double）✅ 成立 |
| D3DEmotePlayer NCB 表 (0x52E504) | M11 系列 | ⚠️ 54 项 ~92% 对齐，6 别名全对；残 4 处重复注册 |

---

## P0 — 仍 open 的真数据流分歧（M15 未触及或新引入）

### R0-1: getVariable READ 级联结构未还原（M3 主张不成立）
- **位置**：`PlayerVariable.cpp:594-632`
- **二进制**（getVariable 0x533E1C → evalKey_cascade 0x6CD23C → HM1_cascadeJoinAndLookup 0x6CD39C）：
  ```
  getVariable: inScope = isLabelInBindScopeList(self,key)  // 扫 deque @self+1312
    if inScope: return HM1_join(self,key)
    else:       return evalKey_cascade(self,key)           // HM4-first
  evalKey_cascade: node=HM4_find(self+1240,key); if hit return *(double*)(node+16)
                   else return HM1_join(self,key)
  HM1_join: if split(key)->("scope::label"): node=HM1_find(self+264, "full::suffix"); return node+48
            else: node=HM2_find(self+320,key); return node+16
  ```
- **本地**：扁平 4 级 fallback `_variableSnapshotMap(HM4空)→_evalResultValues(HM2)→variableFrames→variableRanges`。**无 scope-gate、HM1 从不读、无 "scope::label" join key**；PSB frames/ranges 尾部是二进制不存在的发明 fallback。HM4(`_variableSnapshotMap`)目前**恒为空**（代码注释自承 "Empty here until that wire is added"），故 HM4-first 退化为 HM2-first。
- **结论**：commit d91fe86 "READ structure 1:1" 不成立。需重构为 2-branch router，非增量可补。
- **副产物纠错**：上次审计 J-1（"HM4 是 owning ttstr→tTJSVariant\*"）经 0x6B80E4 反汇编**证伪**——clearHM3_HM4 释放的是 node+8 的 ttstr key，不是 node+16 的 value；value 是裸 double。**M4 commit 651533e 正确，本地 `ttstr→double` 建模正确。clusterJ J-1 应从 P0 降级为 RESOLVED。**

### R0-2: setChara 平凡赋值（clusterE 旧 P0，M15 未触及）
- **位置**：`Player.h:130` `setChara(ttstr v){_chara=v;}`
- **二进制** 0x6D94B0：chara 存为 **tTJSVariant\* @this+776**（手动 ldaxr/stlxr 引用计数 + Release 旧值）；当 +968 gate 置位时触发 `sub_6B29C0(this,16,arg)` 双重 replay dispatch。
- **本地**：ttstr 字段平凡赋值，无引用计数、无 release、无 replay。数据流错。

### R0-3: getLoopTime 返回标量而非 TJS Array
- **位置**：`Player.h:154`、main.cpp:165/166（loopTime/frameLoopTime 都绑这个标量 getter）
- **二进制** 0x6D139C：构造 **TJS Array**（sub_704CB8），遍历内联 node deque `a1[164..168]`（160B stride），每项 `new(0x1F4)`=500B，type=2，AddRef node[0]。
- **本地**：返回裸 `double _loopTime`(+1136)。形状严重不符。注意二进制可能有两个 getter：0x6D139C（Array）vs frameLoopTime 标量 getter ~0x6D97AC（读 +1136）；本地把两者塌成一个标量 getter——需用 NCB reg 表消歧。

> 上次审计的 M1–M11 系列 P0（progress 帧步进机、EmoteEngine 6-deque step + 物理、NodeTree path-key、alpha-mask、findSource/ResourceManager、particle/childMotion splice、容器 STL 替代）**M15 全部未触及，依旧 open**，详见 [MASTER_REPORT.md](audit_motionplayer_2026-05-30/MASTER_REPORT.md)。

---

## P1 — M15 自身引入的 NCB 表偏差

### R1-1: Player NCB 表（0x6D69C8）— kind / RO-RW / 成员集偏差  ✅ 已修复（2026-05-31）
> **更新（2026-05-31）**：经逐成员反汇编 0x6D69C8（每个名串 xref 落在 [0x6D69C8,0x6D93F8)）确认 11 个被删成员全部真实存在 → 已回滚恢复；onAction/onSync/onGroundCorrection 改为 method（cb=nullsub_87/nullsub_88/Player_onAction_ncb 均近 no-op，Player.h 加 no-op 方法体）；colorWeight/independentLayerInherit 解纠缠（colorWeight→getColorWeight/setColorWeight @+1156；independentLayerInherit→+1097 bool，独立成员恢复）；12 个 RO/RW 逐个 verify setter 槽=XZR 后翻 RO。`cmake --build out/web/debug` 链接通过。property 侧静态 diff 与二进制 41 RW+17 RO 逐项匹配 0 mismatch。详见 main.cpp / Player.h 改动注释。**仍未决**：19 个 port-extra 方法仍绑在 Motion.Player（不在 0x6D69C8 的 32 method 表中，host adapter 内部调用，移除需单独 pass）；tags getter 二进制为 getStealthMotionStr，本地绑 getTags（语义近似）。

> 二进制注册 **87 个成员**（上次审计 "92" 把 `L"Property"`/`L"Function"` 描述符 tag 误计入）。
> 证据：member 名取自 sub_6F6970/sub_6D97B4/sub_6D993C 调用；RO/RW 取每块 `+64==0`；method-vs-property 取 `L"Function"`/new(0x40) vs `L"Property"`/new(0x50)。

**M15 正确新增（已对二进制核实）**：angleDeg/angleRad、transformOrder/coordinate、flipX/Y、slantX/Y、zoomX/Y、visible、opacity（RW）、pixelateDivision、bounds(RO)、lastTime(RO)、setCoord/contains/clear、colorWeight。

**残留偏差**：
- **3 kind 错**：`onAction/onSync/onGroundCorrection`（main.cpp:145-147）本地绑 `NCB_PROPERTY`，二进制注册为**方法**（`L"Function"`，单 closure；onSync cb=nullsub_88）。
- **12 RO/RW 错**（二进制 `+64==0` 为 RO，本地绑成 RW）：loopTime(166)、variableKeys(174)、syncWaiting(176)、frameLastTime(164)、frameLoopTime(165)、hasCamera(178)、cameraTarget(227)、cameraPosition(228)、cameraFOV(229)、cameraAlive(230)、resourceManager(236)、processedMeshVerticesNum(167)。
- **疑似误删 11 个二进制成员** ⚠️**需独立复核**：审计 agent 报告 stealthChara、stealthMotion、tags(RO)、project、meshline、useD3D、independentLayerInherit（与 colorWeight 不同名，非 alias）、setVariable、getVariable、getCommandList、onFindMotion 出现在 0x6D69C8 成员表里，而 M15 提交 9fb64d4/D-01/M-colorWeight 把它们当 port-extra 移除了。**注意：本次另一 agent 报告 0x6D69C8 函数体 ~161KB 反编译被截断，故"成员存在"的判定与提交时"port-extra"判定存在冲突，必须用 NCB reg 表（ncb_addMember 调用点逐个 get_bytes 取名）二次确认，再决定是否回滚这些删除。**
- **24 个本地 extra** 不在二进制表（其中 meshDivisionRatio(191)、tickCount(182)、frameTickCount(200)、syncActive(177)、cameraActive(179) 是错类 hoist/发明，均无 `// PLATFORM_BOUNDARY` 标注）。

### R1-2: D3DEmotePlayer NCB 表（0x52E504）— 4 处重复注册
- 二进制注册 **54 项**（4 const + 50 member）。6 个故意 name/cb 别名全部正确复刻；`TimelinePlayFlagDifference=2` 正确；`clear`→`create` 正确；别名重绑（5dfd5eb/9827e1f）全对。
- **残留**：每个别名加完后保留了 "诚实名" 重复项，导致 cb 注册两次，二进制只经别名暴露：`bodyScale`(569)、`playCallback`(580)、`setTimeline`(630)、`addPlayCallback`(644)——应删，均无 PLATFORM_BOUNDARY。
- 次要：setRot/getRot 顺序应跟在 getScale 后。
- **文档纠错**：member `progress`(#50) 绑 **EmoteEngine_progress**（非 clusterD:78 所称 "D3DEmotePlayer_progress"），由 0x52f76c target 反编译确认。

### R1-3: transform root-delta setter — 2 处残留（M20 主张成立）
- ✅ setFlip(+1587/+1588)、setZoom(+1624/+1632)、setSlant(+1640/+1648)、setVisible(+1586)、flipX/Y slantX/Y zoomX/Y 属性：写 root(+200) delta 槽 + dirty(+1584) 全部精确对齐。
- **setOpacity(+1656)**：offset/dirty 对，但二进制写裸 int **无 \*255**，本地做 `(int)(v*255)`+clamp（缺反编译证据支撑该 scale）。
- **setAngleRad**（Player.h:567）：缺二进制 emote-mode 分支（player+482 → wrap[0,360) → 写 player+464 → `initEmoteMotion(2)`），本地恒写 delta.angle(+1616)。
- 跨切偏差：本地 setter 缺二进制的 `if(old!=new)` change-gate（无条件 set dirty）；并额外写 legacy `_flip/_opacity` 标量（二进制无，注释但未 PLATFORM_BOUNDARY 标注）。
- **IDA 符号纠错（未改 IDB）**：`Player_setSlant`@0x6C0F54 实为 **setZoom**（写 +1624/+1632），真 setSlant 在 0x6C0FF8。上次审计 H-15（"setSlant 写 +1624/+1632"）系此误名所致，本地代码实际正确。

### R1-4: setAngleDeg 缺 initEmoteMotion；angleDeg/angleRad 单位命名疑点
- setAngleDeg(0x6CD0EC) 本地（PlayerCore.cpp:397）复刻 deg 转换 + [0,360) normalize + 存储，**缺 directEdit 分支的 `initEmoteMotion(this,2)`**（已有 TODO）。
- getAngleDeg(0x6CD0C0) 二进制**返回弧度**（名实不符）；本地两个 getter 返回单位相对 TJS 名疑似倒置。需反编译 NCB reg 表确认是二进制怪癖（应保留）还是 port bug——本次因 0x6D69C8 过大未能确认。

---

## P2 — 已对齐 / 已澄清

- ✅ setTickCount(0x6D96C0)：3 处副作用（+1120 clamp、+480/+481=0x0101、+456=min）全部到位（commit 8e0747e）。
- ✅ lastTime RO(0x6D9448)：`r>0 ? r*1000/60 : r` 1:1。
- ✅ setCoord(0x6CCFF8)：root+1592/+1600 + 合并 dirty 1:1。
- ✅ M4 HM4 value=double：建模正确（见 R0-1 副产物纠错）。
- ⚠️ coordinate/transformOrder int 属性：scaffold-only，无行为（int property 的二进制语义未定位）。
- ❓ 未定位二进制对应、本次无法判定：clear()、contains(label,x,y) 的 x/y hit-test 入口、bounds dict 的精确 key 集、meshDivisionRatio 的 Player 侧 delegating getter/setter。

---

## 建议下一步（按优先级）

1. **R1-1 误删成员复核**（最高）：用 ncb_addMember 调用点逐个 get_bytes，确认 stealthChara 等 11 个是否真在 0x6D69C8 表中；若是，回滚对应 R-M15.1f/D-01 删除。这是 M15 可能引入回归的最大风险点。
2. **R1-1 低风险即修**：3 个 onAction/onSync/onGroundCorrection 改 NCB_METHOD；12 个 RO/RW 改 NCB_PROPERTY_RO；删 R1-2 的 4 处 D3DEmotePlayer 重复注册。这些是纯绑定动作，回归风险低。
3. **R0-1 getVariable 重构**：2-branch router + HM1_join + scope-gate，需先反编译 isLabelInBindScopeList(sub_6CD16C)、sub_6D0BF4(split)、sub_A1359C(concat)。M3 应 reopen。
4. **R0-2/R0-3**：setChara 改 tTJSVariant\*@+776 + replay；getLoopTime 改 TJS Array——均架构级，建议交 module-alignment-driver 统筹。
5. 文档维护：更新 clusterJ J-1 为 RESOLVED；修正 clusterD:78 progress 绑定；修正 clusterH H-15。

## 反编译符号参考（本次确认）
| 符号 | 地址 | 说明 |
|------|------|------|
| Player_getVariable | 0x533E1C | 2-branch scope router |
| Player_evalKey_cascade | 0x6CD23C | HM4-first → HM1 fallback |
| Player_HM1_cascadeJoinAndLookup | 0x6CD39C | HM1 node+48 / HM2 fallback node+16 |
| Player_clearHM3_HM4 | 0x6B80E4 | 释放 node+8 ttstr key（非 value）→ 证伪 J-1 |
| Player_setChara | 0x6D94B0 | tTJSVariant\*@+776 + replay sub_6B29C0(.,16,.) |
| Player_getLoopTime | 0x6D139C | 返回 TJS Array（node deque 160B stride）|
| Player_setAngleDeg / getAngleDeg | 0x6CD0EC / 0x6CD0C0 | getAngleDeg 返回弧度 |
| Player_setCoord | 0x6CCFF8 | root+1592/+1600 |
| Player_lastTime | 0x6D9448 | r>0?r*1000/60:r |
| Player_setTickCount | 0x6D96C0 | +1120/+480/+456 三副作用 |
| Player setZoom（IDA 误名 Player_setSlant）| 0x6C0F54 | 写 +1624/+1632 |
| Player_setSlant（真）| 0x6C0FF8 | 写 +1640/+1648 |
| Player NCB classInit | 0x6D69C8 | 87 成员 |
| D3DEmotePlayer NCB classInit | 0x52E504 | 54 项 |
