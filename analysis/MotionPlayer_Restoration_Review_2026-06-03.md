# MotionPlayer 源代码还原 Review（2026-06-03）

> **2026-07-18 再纠正：** 下文 R0-2 当时把 +960 误称 variableKeys、
> +968 误称 primary chara。NCB 注册的权威映射是 +960 chara、+968
> stealthChara、+976 motion、+984 stealthMotion，源码层均为 `ttstr`
> 值 owner；+768/+776 是两个 pending 值，+1099 是 playing。

> **同日后续修订（2026-06-03，6 子系统并行 fresh-decompile 复核后回填）**：本轮 review 的两条结论已被随后的独立反编译证伪/超越，按 CLAUDE.md「证伪即就地纠正」回填：
> - **§一 anchor color base「255:128 方向颠倒（回归 OPEN 高）」→ 已 CLOSED。** 当前 [PlayerUpdateAnchor.cpp:144-146](../cpp/plugins/motionplayer/PlayerUpdateAnchor.cpp#L144) 已是 `isDefaultBlend ? 128.0 : 255.0`（正确方向），注释 L135-143 亦已更正并自述「commit 5018087 wired to wrong side; corrected here」。本 review 的 §1.1 描述的是修复前代码，建议本项的高优先级 OPEN 状态视为 closed（仅余 §1.3 的 blend per-slot 源 / color 通道序等次要偏差 open）。
> - **§三 M9 phase-D per-vertex-color 平台边界 → 论据被证伪。** 随后反编译 draw 路径 0x6C7440 + 顶点 builder sub_6C715C：builder 只 append (x,y) 位置对（variant type 5，20B），operate*/copy 原语只传单 blend mode，anchor 的 4-corner colorBytes(node+100) 在 0x6C7440 未被引用。故「binary recombines 4-corner gradient as per-vertex vertex colors」不成立；color 消费点未定位。[ResourceManager.h](../cpp/plugins/motionplayer/ResourceManager.h) 注释已据此更正（边界须由 draw-path color 消费点论证，非 findSource、非 per-vertex-color 论据）。
> - **容器「STL→内联 HM 替换属 P3 终极重构」前提 → 有误。** 4 个 HM 二进制本就是 libstdc++ `std::unordered_map`（`_Prime_rehash_policy::_M_next_bkt` + 1.0f load factor + `_M_before_begin` 单链），仅 hash 函数自研（ttstr-hash）。本地用 `std::unordered_map<ttstr,…>` 是**对齐而非待重构偏差**，不存在需迁移到的「自研内联 HM」目标。真正待办仅 2 处 map 的 `std::string→ttstr` key retype（Player.h:1159 node-index map、Player.h:1294 HM2 镜像），属轻量级。下表「内部容器实现」与 §五容器行据此重新定性。


> **架构级 P0 推进进度（2026-06-03 同日实施，commit 15d2972..4ca8010）**：本轮 review 后按 `/goal`
> 逐个攻克架构级 P0，全部 fresh-decompile 证据驱动 + 构建验证（多数另经独立 binary-alignment-auditor 审计）：
> - **M6 alpha-mask** ✅ 解决：`doAlphaMaskOperation`/`getD3DAvailable` 从误挂 Player 改为 Motion 命名空间自由函数（0x6AF104 是 11 参 namespace fn）。
> - **R0-2 setChara** ✅ 解决：补 chara-change→motion-invalidation 副作用（0x6D94B0/sub_6B29C0；纠正"+968 gate"/"replay" 误判）。
> - **R0-3 getLoopTime** ✅ 解决：`loopTime` 改返回 TJS Array（0x6D139C，遍历 +1296 var-track deque），与 `frameLoopTime` 标量解纠缠。
> - **M1 root-stream** ✅ 推进：advance 原子单元补流② root content-snapshot(+616)（0x6B6ADC）；纠正"var-track DEFERRED"过时结论（实已接入 5 点）。Stage B reseek 仍 open。
> - **M2 EmoteEngine** 🟢 大幅推进：builder 定位（0x67D4D0，曾是硬阻塞）→ **6 个 progress-stepped controller deque 全部实装并审计**（eye/eyebrow/mouth/selector/transition/loop）→ bust/hair/parts 弹簧 deque population + chain-spring 偏移修正 → **setVariable cases 4-8 运行时分派 keystone**（0x671228，激活所有 controller）。**完整 float-bits bug 清扫**（trackPow/powCount raw-bits vs int→float）跨 6 个 controller 修复（2316276/2870209/3bcab50）。
>   - **M2 仍 open**：bust/hair target+const bind-loop pass（sub_67C560）、`sub_661F7C` mesh resolver（1925 行，un-inert 值轨道插值）、clamp/mirror/instantVariable/timeline builders、HM2-map 统一（getVariable/setVariable 双表 fork）。
> - **仍完全 open 的架构 P0**：M1 Stage B、~~容器内联（P3 终极重构）~~（**修订：容器选型已对齐，无 STL→内联 HM 重构，仅 2 处 string→ttstr key retype，见顶部修订**）、M3 残留 HM2 fork。
> - 全程 logo 非回归（emote 物理路径对 logo 差分 inert，是非回归守护非存在理由）；emote 路径无 fixture/oracle，验证=反编译+构建（CLAUDE.md 证据阻塞/验证尽力）。


> 方法：以 [MotionPlayer_Restoration_Review_2026-06-02.md](MotionPlayer_Restoration_Review_2026-06-02.md) 为基线，
>   对自 06-02 以来的全部 delta（M7 anchor 3 提交 / M9 source 子系统 5 提交 / M5 node-key 2 提交 /
>   M3 getVariable+var-track ~12 提交）做 **fresh decompile 独立复核**（4 个 krkr2-impl-diff agent 并行，
>   各自重新反编译目标函数，不信任 stale IDA 注释）。
> 权威：libkrkr2.so 反编译。本轮主对话 byte-verify：`qword_14D7C50` + `Player_evaluateAnchorNodes_type10@0x6C0528`。
> 性质：只读 review，未改 cpp/ 或 IDB。

## 总结论：仍未达成 100% 还原；本轮发现 **1 个真实回归**（anchor color base 方向颠倒，~~OPEN~~ **已于同日修复，见顶部修订**）+ **1 条错误 memory 被纠正**（M5 buildNodePathKey「缺失」误判）。delta 区域多数主张成立。

六维评分（沿用 05-30/06-02 框架，标注本轮变化）：

| 维度 | 状态 | 本轮变化 |
|------|------|----------|
| 对象布局 / 字段偏移 / vtable / 大小 | ✅ 普遍对齐 | 未触 |
| 对象生命周期 | ⚠️ 多数对齐，pimpl/STL 析构序仍偏 | 未触 |
| **内部容器实现** | ❌→🟡→✅(选型) | M3 var-track 56B-slot 模型 + HM1/HM4 WRITE 侧填充已 live。**修订：4 HM 二进制即 libstdc++ `std::unordered_map`，本地 `std::unordered_map<ttstr,…>` 选型已对齐**（非待替换）；仅余 2 处 `string→ttstr` key retype（见顶部修订）|
| **数据流 / 调用链** | 🟡 持续忠实化 | M3 getVariable scope-router 已连通、M7 anchor 数据流对齐、M9 findSource 架构对照清楚 |
| **NCB 类暴露面** | ⚠️ | M9 ResourceManager 12 成员表已对齐；ObjSource 6 成员 facade 已建 |
| 边界行为（默认值 / 分支门控）| ⚠️ | ~~anchor color base 255:128 三元方向颠倒（回归）~~ **已同日修复（见顶部修订）**；+612 gate / dampPow / getLayerNames 过滤已对齐 |

**差分现状**：与 06-02 一致——m2logo+yuzulogo 0 mismatch 仅覆盖非-emote logo 路径，不含 anchor/var-track/
getVariable/event 路径。本轮回归（anchor color）对现有 logo 差分 **inert**（anchor 仅在 emote 角色帧触发），
故「全绿 ≠ 已还原」结论不变。

---

## 一、🔴 M7 anchor — 发现真实回归：color RGB base 255:128 方向颠倒

**函数：`Player_evaluateAnchorNodes_type10 @0x6C0528`（nodeType==10, node+1505 active）。**
**本地：[PlayerUpdateAnchor.cpp:139-141](../cpp/plugins/motionplayer/PlayerUpdateAnchor.cpp#L139-L141)。**

### 1.1 byte-verified 证据（主对话独立核实，非仅 agent 报告）

```
qword_14D7C50 bytes = 00 00 00 00 00 E0 6F 40 | 00 00 00 00 00 00 60 40
                    = [0]=255.0 (0x406FE00000000000) , [1]=128.0 (0x4060000000000000)

@0x6c0aac:  v1 = qword_14D7C50[ (*(node + 536*activeSlot + 364) & 0xF0) == 16 ]
            // (blend&0xF0)==0x10 为 TRUE → index 1 → 128.0
            //                       为 FALSE → index 0 → 255.0
```

二进制语义：**default-blend (==0x10) → base = 128.0；非 default → base = 255.0**。

本地：
```cpp
const bool isDefaultBlend = (blendMode & 0xF0) == 0x10;
const double base = isDefaultBlend ? 255.0 : 128.0;   // ← 颠倒
```

**裁决：commit 5018087「anchor color base 255:128」主张 ✘ 推翻（方向颠倒）。** 该提交意在把丢失的
128.0 分支补回，但接到了错误的一侧。正确应为 `isDefaultBlend ? 128.0 : 255.0`。

> **已修复（2026-06-03 同日）**：当前 [PlayerUpdateAnchor.cpp:144-146](../cpp/plugins/motionplayer/PlayerUpdateAnchor.cpp#L144)
> 已是 `const double base = isDefaultBlend ? 128.0 : 255.0;`，注释 L135-143 亦更正并自述「commit 5018087
> wired it to the wrong side; corrected here per 0x6C0528 decompile」。本项的高优先级 OPEN 视为 closed。

### 1.2 M7 其余主张：✅ 成立（本轮主对话反编译对照确认）

| 主张 | 提交 | 裁决 | 二进制证据 |
|---|---|---|---|
| dampPow 公式 + dt 来源 | 7caf558 | ✅ | `v28 = a1[74]*(v27*a1[74]/v27)/v27/60.0/node+2432`，a1[74]=player+592=dt，v27=+592/+1168；`node+2432` 是 `feedback.timespan` 经 type-10 timeline evaluator 写入的通道（旧名 `anchorDamping` 已证伪）。本地逐项一致（含冗余乘除）|
| anchor w/h from internal render Layer | eb347f5 | ✅ | `sub_A0F5E0(player+696)→PropGet("width"/"height")`，**无 `<=0?32` 钳制**；本地从 `_internalRenderLayer`(player+696) 读，架构一致 |
| +612 gate | eb347f5 | ✅ | `if (a1[74]==0.0 \|\| !*(player+612)) { node+200=0; skip }`；player+612=上一帧 needsAssignImages 快照（updateLayerAfterDraw 写 +612<-+613）。本地 gate `_deltaTime==0 \|\| !_internalRenderLayerReady` 一致 |
| CLAUDE.md 教训（5018087 曾误判 internalRenderLayer「架构前置缺失」，eb347f5 纠正）| — | ✅ 纠正到位 | eb347f5 后本地确实经 player+696 读 w/h，数据流恢复 |

### 1.3 M7 仍 open 的次要偏差（低优先）
- **[低]** PropGet flag：二进制传 `1024` vs 本地 `0`（L44/L48）。
- **[低]** w/h 二段式解析：二进制 PSB-dispatch PropGet → `Motion_propGetInt` 两段；本地直接转换。
- **[⚠ 容器替换]** blend 来源：二进制 per-slot `node + 536*activeSlot + 364`；本地单 `interpolatedCache.blendMode`（无 per-slot）。**注意：1.1 的回归修复必须读对 blend 源，否则 base 仍可能取错。**
- **[低/inert]** opacity 负路径常量：二进制 `4294967300.0` vs 本地 `4294967296.0`（clamp 后不可达）。

---

## 二、M3 getVariable scope-router + var-track deque + HM WRITE 侧 — ✅ 主张多数成立

独立反编译 10 地址（0x533E1C/0x6CD16C/0x6CD23C/0x6CD39C/0x6D0BF4/0x6B786C/0x6B7A70/0x699510/0x6C4668/0x6B2D3C）。

| 点 | 主张/提交 | 裁决 | 备注 |
|---|---|---|---|
| getVariable scope-router | 5b0f96f/66cd17a | ✅ | 2-branch router + HM4-first(raw key) + HM1-join(scope+"::"+label→node+48) + HM2 fallback；已移除 PSB 发明 fallback。本地用 if-guard 替双分支，语义等价 |
| var-track stream③ 56B slot + step/merge | 8bd6629/9ddc25d | ✅ | step(0x6B786C)/merge(0x6B7A70) 逐字段确认 |
| HM1/HM4 WRITE 填充 | 5b0f96f/66510ff | ✅ | bindParameterValue(0x6C4668) HM1 gate + node+32 writeVal + node+40 weight 首插 1.0；resetMotionState(0x6B2D3C) loop2 HM4 + loop3 HM3（mask 0x19D 一致）|
| HM3_initValueFromNode 24 字段 | c153190 | ✅ 基本成立 | 20 字段映射对齐 |

**仍 open / 证据缺口（非反驳）**：
- var-track 插值/bezier easing（0x6BBE20/0x69A754）**本轮未独立反编译**——commit 22a69e5 主张实装，缺独立证据。
- HM3 字段 `slantY(node+1568)` 在 0x699510 中未见读取语句——本地可能多出一字段，需精读尾部确认。
- resetMotionState loop3 `node+46` 门本地缺失（dead-data，已 DEFERRED 标注）。
- 已澄清一个**伪矛盾**：HM1 writeVal WRITE 写 node+32 / READ 读 node+48，实为同一字段差 16B intrusive-node header——非 bug，避免后续 session 误报。
- 记录于 [.claude/agent-memory/krkr2-impl-diff/project_m3_getvariable_review_gaps.md](../cpp/plugins/motionplayer/.claude/agent-memory/krkr2-impl-diff/project_m3_getvariable_review_gaps.md)。

---

## 三、M9 source 子系统 — ✅ claims 1-3 成立；claim 4 平台边界判定 ⚠️ 需补证

| 点 | 主张/提交 | 裁决 |
|---|---|---|
| ResourceManager NCB = 12 成员 | a074060 | ✅ `0x6AB8BC` 恰注册 12 成员，名/序与 main.cpp:412-428 镜像一致。RM 与 SourceCache **确为同一类**，无独立 SourceCache 注册函数 |
| ObjSource = 0x18 dict facade, 6 成员 | 6259f76 | ✅ `0x69CCB8` 注册 6 个 dict 读取成员；findSource 经 `operator new(0x18)` 构造，obj[0]=dict variant。06-02「缺 6 成员」**方向反了**，已确认。width getter@0x69D19C 在 type!=7 时返回 32，本地 `readInt(...,32)` 一致 |
| findSource = outer map + mapped-record inner maps + raw upload | 3761a0b | ✅ 2026-07-18 后续纠正：本地已恢复 outer map 每项的 Win `name→texture` 表和 KRKR 平表 `src/group/icon→descriptor`、AddRef/Release 及 unload 生命期；Win/spec=2 与 KRKR/spec=1 均直接导航 raw `PSBRawNode`。KRKR 整页 CPU 合成后一次 Update 是已标注的 Web 上传 API 边界 |

**claim 4（07c4f05 phase-D per-vertex color 平台边界）：❌ 论据被证伪（2026-06-03 draw-path 已反编译）。**
> **更新**：随后反编译 draw 路径 sub_6C7440 + 顶点 builder sub_6C715C(@0x6C715C)——builder 只 append (x,y)
> 位置对（variant type 5，20B stride），operate*/copy 原语只传单 blend mode，anchor 的 4-corner colorBytes(node+100)
> 在 0x6C7440 **未被引用**。故「binary recombines 4-corner gradient as per-vertex vertex colors」**不成立**；
> color 真实消费点（若存在）未定位。ResourceManager.h 注释已据此更正：边界须由 draw-path color 消费点（地址待定）
> 论证，而非 findSource、也非 per-vertex-color 论据。下列原 ⚠️ 分析作为历史保留。

- 边界标签**不是** "oracle 看不到" 式遁词——它引用了具体渲染器能力原因（单标量 RGBA、无 per-vertex 属性），满足 CLAUDE.md。
- **但** in-code 论证把证据挂在 findSource 上是错的：两个 findSource 函数**都不施加** 4-corner/per-vertex color，findSource 是纯 name→单贴图缓存。per-vertex-color 证据（若存在）在下游消费 texHandle(`a1+24`) 的 **Layer/mesh draw 路径**，本轮未反编译（超 M9 范围）。
- **建议**（未改代码）：把 phase D 判为边界前，后续 session 应反编译消费 findSource texHandle 的 draw 路径，确证 per-vertex-color 缺失 **或** 发现 color 经本地已有 primitive 施加（则 phase D 重新可实装）。边界注释应引用 draw-path 地址而非 findSource。
- 记录于 [.claude/agent-memory/krkr2-impl-diff/m9_source_subsystem_verdict.md](../cpp/plugins/motionplayer/.claude/agent-memory/krkr2-impl-diff/m9_source_subsystem_verdict.md)。

---

## 四、M5 node 索引键 + getLayerNames — ✅ 两提交正确；🟡 纠正 06-02 一条错误 memory

独立反编译 `Player_buildNodeTree_recursive@0x6B4A6C`（逐指令 disasm 0x6b4ca8..0x6b4ce4）、
`Player_buildNodePathKey@0x6B5C1C`（+xrefs）、`Player_getLayerNames@0x6D10E0` + `ttstr_indexOf@0xA0CC00`。

- **Player+24 node-index map 的键 = RAW PSB "label"，不是 node-path。** 插入点 PropGet("label")→insert 之间**无 path-builder**。`buildNodePathKey@0x6B5C1C` **并非缺失**，它存在但 xrefs 仅 resetMotionState(0x6b2e08)+pruneHM3(0x6b84c4)——**专属 HM3(+1184) 键空间**，从不喂 Player+24。
- **🟡 纠正：06-02 review M5 行「buildNodePathKey 完全缺失；节点按扁平 PSB label 索引→重名碰撞」方向反了。** raw-label 索引是二进制的**正确行为**，非缺陷。这是又一例错误 memory（同 CLAUDE.md 记录的 M5 path-key 误判家族）。本地把 path-key 留给 HM3、node-index 用 flat label，**与二进制一一对应**。
- **commit 3ecd554（revert 回 RAW label key）：✅ 正确**——之前 commit 98ac6e0 的 path-key 改写与 disasm 直接冲突，revert 纠正之。
- **commit 73cc3ac（getLayerNames substring filter）：✅ 正确**——匹配方式 = CONTAINS（子串）、大小写敏感、present-but-empty filter → 一个不发、void/缺省 → 全发，四项与 `0x6D10E0`+`ttstr_indexOf` 逐一吻合。

---

## 五、自上次 review 未触动、仍 open 的 P0（结论沿用）

| # | 簇 | 摘要 | 来源 |
|---|----|------|------|
| M1（剩余）| G | advance 单元 root content-snapshot(+616) + var-track 步进仍未全进；reseek 三流 Stage B、emote/non-emote 分派拆分 open | 06-02 §1.2 |
| M2 | B | EmoteEngine 6-deque step 全 STUB_WARN，hair/bust 物理未实现 | MASTER P0 |
| M6 | K | doAlphaMaskOperation 整体缺失，且误挂 Player 而非 namespace | MASTER P0 |
| R0-2 | E | setChara 二进制 tTJSVariant*@+776 + 引用计数 + replay；本端 ttstr 平凡赋值 | 05-31 R0-2 |
| R0-3 | E | getLoopTime 二进制返回 TJS Array；本端返回裸 double | 05-31 R0-3 |
| 容器层 | 🟡→✅(选型) | **修订：4 HM 二进制即 libstdc++ `std::unordered_map`，本地选型已对齐**（非 STL 简化替代，无 P3「STL→内联 HM」重构）；WRITE 侧 M3 已部分填充；仅余 2 处 `string→ttstr` key retype（Player.h:1159/:1294）| MASTER 根因 #1（已重新定性）|

NCB 残留（06-02 §三未动）：D3DEmotePlayer 表 4 处别名重复注册（bodyScale/playCallback/setTimeline/addPlayCallback）仍未删（低风险即修项）。

---

## 六、建议下一步（按 ROI）

1. ~~**🔴 即修 anchor color base 方向**~~ **✅ 已完成（2026-06-03 同日）**：PlayerUpdateAnchor.cpp:144-146 已为
   `isDefaultBlend ? 128.0 : 255.0`，L135-143 注释已同步更正。**仍 open（次要）**：blend 源本地是单值
   `interpolatedCache.blendMode`，二进制是 per-slot `node+536*slot+364`——color 通道序 / PropGet flag(1024) 亦待修。
2. **就地纠正错误 memory**（CLAUDE.md 硬规则）：06-02 review M5「buildNodePathKey 缺失」行已被本轮证伪，
   应在该文档/相关 memory 标注纠正（buildNodePathKey 存在且 HM3-only，raw-label 索引是正确行为）。
3. ~~**M9 phase D 边界注释补证**~~ **🟡 部分完成（2026-06-03 同日）**：已反编译 draw 路径 sub_6C7440 + 顶点 builder
   sub_6C715C → per-vertex-color 论据被证伪（builder 仅位置对、原语仅单 blend mode）。ResourceManager.h 注释已更正。
   **仍 open**：4-corner colorBytes(node+100) 的真实消费点未定位——后续需追踪该数据流以最终确证/重开 phase D。
4. **低风险即修**：删 D3DEmotePlayer 4 处别名重复注册。
5. **架构级 open P0**（M1 剩余 / M2 / M6 / 容器 STL→HM 替换）——这些 open 的唯一原因是「反编译→实装」工作尚未做，**不是**「无 oracle 所以碰不得」。推进门槛 = 先 fresh decompile 拿伪代码证据（避免**无证据盲改** = CLAUDE.md BLOCKING：禁止从本地/键名/变量名推断、禁止先改后验），有证据即照常实装+构建验证，运行时验证尽力补即可（证据是阻塞项、验证是尽力项，CLAUDE.md:94）。**「差分不覆盖 / 无 Android oracle」按 CLAUDE.md:96-97 明确不是 defer/降优先级的合法理由。**

## 本轮反编译符号参考
| 符号 | 地址 | 本轮确认 |
|------|------|----------|
| Player_evaluateAnchorNodes_type10 | 0x6C0528 | color base `qword_14D7C50[(blend&0xF0)==0x10]`=TRUE→128/FALSE→255；dampPow/+612 gate/w-h-from-+696 对齐 |
| qword_14D7C50 | 0x14D7C50 | [255.0, 128.0]（byte-verified）|
| Player_buildNodeTree_recursive | 0x6B4A6C | Player+24 key = raw PSB "label"（disasm 0x6b4ca8..0x6b4ce4）|
| Player_buildNodePathKey | 0x6B5C1C | 存在；xrefs 仅 HM3 消费者（非缺失）|
| Player_getLayerNames | 0x6D10E0 | args[0] CONTAINS 子串过滤，大小写敏感 |
| ResourceManager ncb | 0x6AB8BC | 12 成员；RM==SourceCache 同一类 |
| ObjSource ncb | 0x69CCB8 | 6 dict-facade 成员；new(0x18) |
| bindParameterValue / resetMotionState | 0x6C4668 / 0x6B2D3C | HM1/HM4/HM3 WRITE 侧对齐 |
