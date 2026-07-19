# motion::Player 4 内联哈希表 ↔ 本地容器 权威映射（P2 地基）

> 日期：2026-06-02
> 方法：ida-deep-analyzer 递归反编译 4 个 HM 的 insert/lookup 调用点 + helper，byte-verify key/value 类型。
> 权威：libkrkr2.so。ctor 0x6CED30 / dtor 0x6CFADC / getVariable 0x533E1C。
> 性质：只读分析。IDB 已重命名 9 站点 + idb_save。**未改 cpp/**。
> 解决：[Player_Class_Layout_Alignment_TODO.md](Player_Class_Layout_Alignment_TODO.md) §1.2 + §2.5「4↔6 HM 映射」最大未决项。

## 结论：4 个 HM ↔ 本地 4 个 map 是 1:1 真实镜像，且本地命名/类型注释**已全部正确**。

> **澄清（2026-06-03 fresh-decompile 复核回填，防误读）**：标题/正文的「内联哈希表」指**作为 by-value 成员内联在 1384B Player 结构里的 std::unordered_map**，**不是** KiriKiri 自研的定制 hashmap。下文 §一已证（base+8 = `std::_Prime_rehash_policy::_M_next_bkt`、base+32 = 1.0f load factor、base+16 = `_M_before_begin` 单链 head）——这 4 个 HM 的**容器本体就是 libstdc++ `std::unordered_map`**，仅 **hash 函数**自研（ttstr UTF-16LE hash）。
> 推论：本地 `std::unordered_map<ttstr, V, ttstr_hash, ttstr_equal>` 是**正确的 1:1 容器选型**，不是被 CLAUDE.md 禁止的「STL 简化替代」。**不存在**「STL → 自研内联 HM」的 P3 重构目标（曾在多份 doc/review 里如此表述，系误判）。容器维度真正待办仅 2 处 `std::string → ttstr` key retype：HM2 镜像 `_evalResultValues`（Player.h:1294）与 +24 node-index map `_nodeLabelMap`（Player.h:1159）。

二进制 1384B Player 只有 **4 个内联 HM + 1 个 +24 std::map** = 5 个关联容器。本地有 ~10 个。
本分析把 5 个真实容器逐一锚定，并裁决其余 ~6 个的归属。

---

## 一、HM 通用结构（4 个共有）

**HM 控制块 = 48B**（模板 = HM1@+264，ctor 0x6CEDxx）：

| 偏移 | 字段 | 证据 |
|---|---|---|
| base+0 | bucket 数组 ptr (`operator new(8*nbkt)`) | 0x6CEDF4 |
| base+8 | nbkt（`std::_Prime_rehash_policy::_M_next_bkt(_,0xA)`）| 0x6CEDD8 |
| base+16 | before-begin sentinel / 单链 head | dtor/insert 用 `base+2` |
| base+24 | element count | — |
| base+32 (dword) | load factor = 1065353216 = **1.0f** | 0x6CEDC4 |
| base+40 | next_resize 阈值 | — |

**4 个 HM 的 key 全是 ttstr（UTF-16LE）**，hash = KiriKiri ttstr-hash（缓存于 node 尾部）。
桶选择 `sub_149EDF8` 二分 `qword_16496D0`。

---

## 二、逐 HM 权威表

### HM1 @+264 — EvalCascade（scope::label join → double）
| 项 | 值 | 证据 |
|---|---|---|
| node size | `operator new(0x60)` = **96B** | 0x6F5368 |
| key | joined `"scope::label"` ttstr (owning) @node+8 | sub_A1359C 拼接 0x6C46D4/0x6C4734 |
| value（lookup 返回）| **node+48 = writeVal double** | `*(v39+48)` 0x6CD634 |
| node 富字段 | +0 next / +8 key / +16 key副本 / +24..40 chainDispatches vec / **+48 writeVal** / +56 weight(seed 1.0) / +64..72 RenderItem* vec / +88 cached hash | 0x6F52ac/0x6C4880/0x6C4968/0x6C4964 |
| WRITE | bindParameterValue 0x6C4668 → HM1_upsert 0x6F52AC → insert 0x6F53C8；门控 `sub_6D0BF4` 拆分成功（label 含 `::`/`/`）才走 | 0x6C46BC |
| READ | HM1_cascadeJoinAndLookup 0x6CD39C (find 0x6CD5E4)；scope-gate 经 sub_6CD16C 扫 deque | 0x533E1C |
| **本地** | `_evalCascadeMap` (EvalCascadeMap) Player.h:1158 | ✅ real mirror，注释正确 |

### HM2 @+320 — label→double 结果缓存
| 项 | 值 | 证据 |
|---|---|---|
| node size | `operator new(0x20)` = **32B** {next, key@+8, value double@+16, hash@+24} | 0x686A4C |
| key | raw label ttstr (owning，**未** join) @node+8 | 0x686A08 |
| value | **raw double @node+16** | upsert `*result=value`；read `*(node+16)` 0x6CD5BC |
| WRITE | HM2_upsert 0x686944；主写点 = bindParameterValue **LABEL_132** 0x6C4C0C (`HM2.upsert(rawLabel)=a4`) | — |
| READ | cascade fallback 0x6CD5A8（HM1 miss 后）+ evalKey_cascade 0x6CD2F4（HM4 miss 后）| — |
| **本地** | `_evalResultValues` `unordered_map<string,double>` Player.h:1197 | ✅ real mirror（_variableValues 已并入）|

### HM3 @+1184 — per-node-path 图层状态快照
| 项 | 值 | 证据 |
|---|---|---|
| node size | `operator new(0x2D0)` = **720B** {next, key@+8, **696B payload@+16**} | 0x6F2730 |
| key | node-path ttstr (owning) @node+8，由 buildNodePathKey 0x6B5C1C 生成（与 +24 path map 同 key 空间）| — |
| value | node+16 起 **0x2B8=696B PerNodeLayerState**（owns 8 ttstr + 5 dispatch + 2 heap slot）| dtor 0x6DD06C |
| WRITE | HM3_upsert 0x6F2674 ← resetMotionState loop3 0x6B2E18；fill = HM3_initValueFromNode 0x699510；仅 node type ∈ {0,2,3,7,8} | — |
| READ | pruneHM3_byNodeIdentity 0x6B826C (find 0x6F28A4)；clear 0x6B80E4 | — |
| **本地** | `_perNodeLayerStateMap` (PerNodeLayerStateMap) Player.h:1180 | ✅ real mirror（keying 正确；**populate DEFERRED**，本地空）|

### HM4 @+1240 — 变量快照缓存（getVariable cascade 首站）
| 项 | 值 | 证据 |
|---|---|---|
| node size | **与 HM2 完全相同**（0x20，val@+16 double，hash@+24），**复用 HM2 的 find/upsert/insert** | disasm 0x6B2D3C = HM2_upsert |
| key | label ttstr (owning) @node+8 | clearHM3_HM4 释放 node[1]=node+8 (Release) 0x6B8118 |
| value | **raw double @node+16**（clear 不释放 node+16 → 裸标量）| evalKey_cascade `*(node+16)`→double 0x6CD304 |
| WRITE — 唯一写点 | resetMotionState loop2 **0x6B2D3C**：遍历 var-track deque @+1296（160B/item，initVariables 0x6CD750 经 sub_6F3C14 推入），key=`*(item)` label / value=`*(item+16)` double | — |
| READ | evalKey_cascade 0x6CD2F4（HM4-first，hash%`*(Player+1248)`）+ pruneHM3 0x6B8404 | — |
| **本地** | `_variableSnapshotMap` (VariableSnapshotMap) Player.h:1190 | ✅ real mirror（R-M4 已纠正为 8B raw double）|

**+24 std::map**（node-path label map，sub_6DD228 哨兵）= 第 5 个关联容器 → 本地 `_nodeLabelMap` Player.h:1065。

---

## 三、其余 ~6 个本地 Player map 的裁决

二进制 1384B Player 无对应 → **必须 reclassify**（不是 HM）：

| 本地 map | Player.h | 裁决 | 二进制等价承载 / 证据 |
|---|---|---|---|
| `_motionsByKey`<string,shared_ptr<MotionSnapshot>> | 995 | **port 凭空发明** | 二进制无；motion 数据走 node deque(+184) + dispatch(+528) |
| ~~`_timelines`<string,TimelineState>~~ | 原 1005 | **已删除（2026-07-19）** | `Player_ncb_registerMembers@0x6D69C8` 无 timeline API；timelineControl/play/stop 属于 EmoteEngine HM3/+1040，Player 帧状态在 node deque 与 +456/+1120 游标 |
| `_disabledSelectorTargets`<string,bool> | 1036 | **port 凭空发明** | 二进制无对应 |
| `_parameterEntryById`<string,size_t> | 1039 | **port 凭空发明** | 二进制无；参数走 HM1/var-track deque |
| `_layerIdsByName`<string,int> | 1012 | **EmoteEngine(1496B) 字段误植** | 语义 = EmoteEngine+1440 ttstr→double HM；Player* 存于 engine+1064（0x67D3E8）|
| `_layerNamesById`<int,string> | 1013 | **EmoteEngine 字段误植** | 同上 |
| `_renderLayerStates`<int,LayerRenderState> | 1014 | **EmoteEngine 字段误植** | EmoteEngine_setVariable 0x671228 操作 engine+1384/+1440 + per-type animator deque engine+256/+336/+416/+576/+656 |

> 注：上表「误植/发明」是**容器归属**裁决（架构级）。其中 `_timelines` 及
> `_playingTimelineLabels` 已在逐函数反编译 Player/EmoteEngine 注册与调用链后删除；
> 其余条目仍须按各自证据迁移，不能把这段历史性的统一“live path”描述继续套用到 timeline。
> 此表的价值是：明确它们**不是** 4 个 HM 的镜像，后续重构时按此归属处理，不再误当 Player HM。

---

## 四、对后续 P0 的解锁

1. **M3 getVariable 级联**（现可实施）：4 HM 类型/键已确定 →
   READ = `evalKey_cascade`(HM4@+1240 first, hash → `*(node+16)` double) → miss → `HM1_cascadeJoinAndLookup`
   (scope-gate sub_6CD16C → HM1@+264 join "scope::label" 取 node+48 / 否则 HM2@+320 fallback 取 node+16)。
   本地 4 mirror 已就位但**多为空**（_evalCascadeMap/_perNodeLayerStateMap/_variableSnapshotMap 未 populate）→
   M3 真正缺的是 **WRITE 侧**：bindParameterValue(0x6C4668) + resetMotionState(0x6B2D3C loop2/loop3) + initVariables(0x6CD750)
   把 var-track deque(+1296) 快照进 HM4，把 bind 值写进 HM1/HM2。**先 port WRITE 侧填充，cascade READ 才有数据**。
2. **容器层 STL→KiriKiri**：4 HM 的 node 布局已知（96B/32B/720B/32B，HM2/HM4 共享 32B node + helper），
   若做内联 HM 复刻按此 stride。
3. **P3 pimpl 内联**：上表 7 个 extra map 的归属已定，重构时 4 发明 map 删除、3 误植 map 迁 EmoteEngine。

## 四之二、HM4 WRITE 侧深挖（2026-06-02，本轮新反编译）— ⚠️ 实施被基地不一致阻塞

> 反编译：`Player_resetMotionState_clearAndRebuild` @0x6B2D3C（全）+ `Player_initVariables` @0x6CD750（全）。

### resetMotionState（0x6B2D3C）3 个循环
门控 `if(!*(BYTE)(player+480))`（progressFlags LSB 清）。先 `clearHM3_HM4` + `sub_6BBE20`，然后：
- **loop1**（node-deque idx≥1）：`node+44=1; Player_evaluateTimeline(node, 1, player+456)`。
- **loop2 = HM4 写**（var-track deque @+1296，160B/item，j 从 0）：
  ```
  item = deque[j]
  gate = *(BYTE)(item + 56*(*(int*)(item+8)/*cursor*/) + 68)   // slot[cursor]+20 flag
  if(!gate){ v = *(double*)(item+16); node = HM2_upsert(player+1240, item/*key=item+0*/); node+16 = v; }
  ```
- **loop3 = HM3 写**（node-deque idx≥1，仅 `node+46` 置位且 type∈{0,2,3,7,8}）：
  `buildNodePathKey → HM3_upsert(player+1184) → HM3_initValueFromNode(node, V)`。

### var-track item 完整 160B 布局（byte-verified from initVariables 写入点）
| 偏移 | 字段 | initVariables 初值 | 用途 |
|---|---|---|---|
| +0 | cascadeKey ttstr | = `scope+"::"+label`（scope 存在时，sub_A1359C concat ×2）否则 label | **HM4 key**（loop2 读 item+0）|
| +8 | cursor (int) | 0 | active-slot 奇偶游标 |
| +16 | **value (double)** | 0 | **HM4 value**（loop2 读 item+16）；仅由 stream③ 写 |
| +24 | frameSource tTJSVariant | = 第二次 PropGet("label") 后独立 CopyRef | variable-track 帧源；后续以 dispatch 执行 PropGetByNum |
| +48 | slot0 (56B) | memset 0；slot0+20(=item+68)=1 | per-track 帧 slot；gate flag@+20 |
| +104 | slot1 (56B) | memset 0；slot1+20(=item+124)=1 | 同上 |

### READ 侧 key 形态（本轮反编译，brick 1 锚点）
- `Player_evalKey_cascade` @0x6CD23C：HM4 按**原始 lookup key**（`ttstr_c_str(key)` 的 hash，**不 join**）查找，命中取 `*(node+16)` 为 double。
- `Player_getVariable_wrapper` @0x533E1C：cascade 作用于 `*(a1+1064)`（= 内嵌 Player），scope-gate 在 HM1-join 路径与 HM4-first 路径之间分派。
- ⇒ HM4 命中要求 lookup key == 存储的 item+0 cascadeKey。结合 initVariables 写 item+0 = `scope+"::"+label`（concat），**权威 cascadeKey 形态 = scope 存在 ? scope+"::"+label : label**。item+24 是对同一 `entry["label"]` 的第二次 PropGet/Variant CopyRef，不是 ttstr 字段。active `initVariables` 旧写的 scope-**后缀**确证为分歧。

### Player+1296 基地原问题（已在 brick 1 修复）
1. ~~两个竞争模型~~：active `std::vector<VariableLabelEntry>`（错形态 vector）vs `std::deque<VariableLabelScope>`（**形态正确**=deque，但从未实例化的死 alias）。**勘误**：deque alias 形态本就对，问题是活的是 vector、死的是 deque。
2. **缺 value 字段**：binary item+16（HM4 读的 double）在两 struct 都缺。
3. **key 分歧（确证）**：binary item+0 = `scope+"::"+label`（concat）vs active `initVariables` 写 scope-后缀。
4. **slot 误映**：`VariableLabelScope` 的 scope@+64 / flagValidated@+108 实落在 +48/+104 两个 56B slot **内部**（gate flag = slot+20 = +68/+124），非顶层字段。
5. **value-writer 勘误（2026-07-18）**：item+16 不是 step/merge 直接写入，而是 Player_interpolateVarTrackValues@0x6BBE20 根据双 slot 写入；该路径现已接入，不再是 DEFERRED。

### brick 2 实现状态（2026-06-02，按 CLAUDE.md「证据是阻塞项，验证是尽力项」）
- ✅ **brick 2a DONE**：VarTrackSlot 补全为 byte+disasm-verified 完整 56B
  {frameIndex+0, time+8, interval+16, typeZeroFlag+20, interpFlag+21, merged+22, value+24, easing+32}；
  item+24 从误判的 labelName 改为 frameSource（keyframe 列表，node+64 "frameList" 类比）。commit 8bd6629。
- ✅ **brick 2b DONE**：`advanceVariableTracksLike_0x6B6ADC`（step sub_6B786C + merge sub_6B7A70 +
  step 循环 0x6B7274 + disasm 确认的双 slot0 merge 0x6B7178）实现并接入全部 5 个 advance 点
  （layer→var-track→node 单元）。
- **验证（现有 oracle，无新建物料）**：web debug build 通过；wasmtime guest 重建通过；
  **logo differential m2logo(93)+yuzulogo(243) 0 mismatch PASS**（证明 live-path 接入未破坏 Motion 状态）。
- **验证缺口（标注，不硬凑）**：所有可用 motion 均无 variable 列表（36 .mtn + e-mote .psb，
  motionsim --dump-variable --seed 742877301 确认），故 stream③ 对现有内容**必然 no-op**，
  其 frame-stepping 路径**无 fixture 可验证**；按策略不从零造 fixture，留此缺口。item+16 value 的
  **插值**（slot bracket → HM4 value）尚未实现（brick 2.5，仍 inert）。
- **遗留疑点（inert，不阻塞）**：item+0 cascadeKey 对 array-label 的 ttstr_c_str 形态、双 slot0 merge
  是否 IDA 之外另有语义——现有材料无法判，按忠实复刻 disasm 落地。
- ✅ **brick 2.5 DONE**：`interpolateVarTrackValuesLike_0x6BBE20`（item+16 = HOLD/LERP，
  active=prev/other=next，t=(eval−prevTime)/(nextTime−prevTime) 经 interval 量化 +
  cubic-bezier easing `applyBezierEasing_0x69A754`）+ easing 类型修正（VarTrackSlot.easing
  ttstr→`{x,y}` bezier dict）。本对话 decompile 0x6BBE20/0x69A754 验证。**当前 unwired**
  （port 无 resetMotionState；该函数在 resetMotionState 开头被调，brick 3 接线）。
  build 通过 + logo diff 0 mismatch（easing 类型改动触及 live merge，已复验未回归）。
  bindParameterValue(HM1/HM2) 调用 DEFERRED（HM1/HM2 写侧，brick 3b）。
  - 找到的 var-track 完整调用图（brick 3 蓝图）：`resetMotionState`(0x6B2B7C) = clearHM3_HM4
    → **interpolateVarTrackValues**(item+16 + bindHM1/HM2) → loop1(node evaluateTimeline)
    → **loop2**(item+16 → HM4@+1240) → loop3(HM3@+1184 perNodePath)。caller = playImpl(0x6B2284)。
- ✅ **brick 3 DONE（slice: clear+interp+loop2）**：`resetMotionStateLike_0x6B2D3C` = body-gate
  `!_queuing`(+480) → clearHM3_HM4(`_perNodeLayerStateMap`/`_variableSnapshotMap`.clear) →
  interpolateVarTrackValues → **loop2**(每 var-track `!active.typeZeroFlag` → `_variableSnapshotMap[cascadeKey]=item.value`，HM4 populate)。**接线**：playMotionLike_0x6B2284 在 `flags&PlayFlagJoin(8)` 时调用（playImpl 0x6B22E4 同点）。本对话 decompile playImpl 0x6B2284 验证接线点 + PlayFlagJoin=8。
  - **DEFERRED**：loop1(node evaluateTimeline)、loop3(HM3 perNodeLayerState，依赖未移植的 `HM3_initValueFromNode` 0x699510)。bindParameterValue(HM1/HM2) 仍 DEFERRED。
  - **验证**：web+guest 构建 + logo diff 0 mismatch（live 接线，已验未回归）。**HM4 数据通路打通**（stream③→interp→loop2→HM4），仍对现有内容 inert（无 variable）。
  - **剩余 brick 4**：getVariable READ cascade（evalKey_cascade 0x6CD23C：HM4-first by raw key → HM1 join → HM2）接 `_variableSnapshotMap`。
- ✅ **brick 4 CONNECTED（无新代码）**：port `getVariable`(PlayerVariable.cpp:607) **本就 HM4-first** 按 raw `label` 查 `_variableSnapshotMap`，与 evalKey_cascade(0x6CD23C) 的"HM4 按原始 key 查 → *(node+16) double"一致。brick 3 把 populate 侧接上后，**var-track → HM4 → getVariable 端到端打通**（内容有 variable 时值流通；现有内容 inert，map 空 → fall through，logo diff 绿）。仅更新注释。
  - **仍 OPEN（完整 M3 / R0-1，架构级，非本 brick 链）**：2-branch scope router（getVariable_wrapper 0x533E1C：isLabelInBindScopeList → HM1-join 路径）+ HM1(`_evalCascadeMap`) 读（port 走 HM4→HM2 跳过 HM1）+ 去除 PSB frames/ranges 发明 fallback。

## brick 链总结（1→2a→2b→2.5→3→4，本 session）
| brick | commit | 内容 |
|---|---|---|
| 1 | 07faaf6 | var-track deque 基地重构 |
| 2a | 8bd6629 | 56B slot 模型 + frameSource |
| 2b | 9ddc25d | stream③ advance + 接入 |
| 2.5 | 22a69e5 | item+16 插值 + bezier easing |
| 3 | 66510ff | resetMotionState slice (HM4 populate) |
| 4 | (本提交) | getVariable HM4-first read（已就位，注释更新） |
全程：反编译证据 → 实现 → 现有 oracle(logo diff 0 mismatch) → 不硬凑 fixture。**var-track → HM4 → getVariable 数据通路端到端打通**，对现有内容全程 inert。

## brick 5 — HM1 / scope-router（M3 / R0-1，本对话）
本对话 decompile 0x6CD16C / 0x6CD39C / 0x6C4668 / 0x6D0BF4 验证（BLOCKING）。
- ✅ **5a bindParameterValue**（`bindParameterValueLike_0x6C4668`）：HM2(`_evalResultValues`)[rawKey]=value 恒写；key 可 split("::"/"/")时 HM1(`_evalCascadeMap`)[joined].writeVal=value（weight 首插 1.0）。dispatch/animator 副作用 DEFERRED（无 getVariable consumer）。**接入 interp**（brick 2.5 的 deferred 调用）。
- ✅ **5b getVariable 2-branch scope-router**（`isLabelInBindScopeListLike_0x6CD16C` + 重构 getVariable）：
  inScope（key 匹配某 var-track cascadeKey）→ HM1-join 直走；else evalKey_cascade（HM4-first by raw key）→ miss → HM1-join。
  HM1-join = key 有"::"/"/"→ HM1.writeVal(node+48) : HM2(node+16)。**移除 port 发明的 PSB frames/ranges fallback**（R0-1 READ 路径 RESOLVED）。
- 结构事实：scope-list = var-track cascadeKeys = HM4 keys，故 inScope 键走 HM1（非 HM4），值由 bindParameterValue 供（与 loop2 HM4 同值）。忠实复刻。
- **验证**：web+guest 构建 + logo diff 0 mismatch（移除 PSB fallback + 加 router 未回归 logo）。scope-gated 变量读路径打通（HM1）；对现有内容仍 inert（无 variable）。
- **仍 DEFERRED**：bindParameterValue 内 sub_697D34 chainDispatches + RenderItem/animator 更新；resetMotionState loop1(node evaluateTimeline)。

## brick 6 — resetMotionState loop3 / HM3 结构（本对话）
本对话 decompile 0x699510 (HM3_initValueFromNode) 验证（BLOCKING）。
- ✅ **loop3 结构 DONE**：resetMotionState 内加 loop3 = 每 node(idx≥1) `nodeType ∈ {0,2,3,4,7,8}`(mask 0x19D) → `buildNodePathKeyLike_0x6B5C1C`(已有) → `_perNodeLayerStateMap[key]` upsert → `hm3InitValueFromNodeLike_0x699510`。
- ⚠️ **HM3 snapshot PARTIAL（重要）**：`hm3InitValueFromNodeLike_0x699510` 只复刻 **V+0 nodeType ← node+28**（唯一映射到现有 MotionNode 成员的字段）。其余 ~24 个字段（V+28/44/52/64/80../128.. ← slot+340/356/364/376、node+100-112/1392/1507/1508/1512/1528/1536/1544/1560/1576/1912/2000/2024/2224-2296）读 raw node 字节偏移，**port MotionNode 未暴露为命名成员**；逐个映射需 per-offset RE，DEFERRED。
- ⚠️ **node+46 门 DEFERRED**：loop3 二进制 gate 含 `node+46`(未在 port MotionNode 命名)，本端只用 type-mask 门（对 dead-data 无可观察差异）。
- 🔑 **关键定性**：`_perNodeLayerStateMap`(HM3) **无任何 reader**（只 declare + clear，从不读）。⇒ loop3 是 **dead-data**（填了没人读），与 brick 1-5 喂 getVariable（真 reader）本质不同。完整 25-字段 snapshot 是大 RE 工程且产物不可观察、不可验证。**本 brick 只落地结构 + nodeType，重 RE 留待 HM3 出现 consumer 时再投入**。
- **验证**：web+guest 构建 + logo diff 0 mismatch（loop3 live 运行于 logo PlayFlagJoin，填 unread map，未回归）。

## brick 6.5 — HM3 24-字段 RE + 全 snapshot（本对话）
ida-deep-analyzer 逆向 HM3_initValueFromNode 每个源字段语义 + 调用链。**关键发现**：HM3 快照的是 node **已插值好的状态**（由 resetMotionState **loop1** 的 `Player_evaluateTimeline` @0x699AE4 写入 node+1512.. 字节镜像），不是原始帧。port 把这些值放在 `node.interpolatedCache` + `node.activeSlot()`（语义对，非字节镜像）。
- ✅ **20/24 字段实装**：`hm3InitValueFromNodeLike_0x699510` 从 interpolatedCache + activeSlot 拷：nodeType / done / blendMode / ox,oy / packedColors / opacity(×255 round) / coordX,Y,Z / flipX,Y / angle / scaleX,Y / slantX,Y / meshControlPoints(meshType==1)。
- ✅ **PerNodeLayerState 重构**：把误标的 byte-grouped 字段（sourceRect_x/y/w/h 实为 packedColor、oword_104 实为 coord、oword_136/ldouble_152 误分组）改为语义字段（agent 勘误：V+136 angle / V+144 scaleX / V+152 scaleY / V+160 slantX / V+168 slantY；V+96 源是 node+1576 插值 opacity 非 Node+408）。
- ⚠️ **4 字段 DEFERRED（无 port 源）**：V+28 contentMask（ClipSlot 不留 frame mask）、V+44 srcDispatch（port src 建模为 std::string 非 dispatch）、V+544/V+672 child-player/particle-array dispatch（node+1912/2296）、type-4 粒子插值块 node+2224-2288（evaluateTimeline type-4 分支未移植，port 无源）。
- **验证**：web+guest 构建 + logo diff 0 mismatch。仍是 dead-data（HM3 无 reader），但快照现在忠实复刻可获取的 20 字段。

### 忠实 brick 路线
1. ✅ **基地重构（brick 1）DONE（2026-06-02）**：`VariableLabelScope` 补全为 {cascadeKey, activeSlotCursor, value, labelName, VarTrackSlot slot[2]}（slot 含 +20 gateFlag）；删死 struct `VariableLabelEntry`；Player 字段 `vector<VariableLabelEntry> _variableLabelEntries` → `deque<VariableLabelScope> _variableLabelScopes`（容器形态对齐 deque）；`initVariables` 改产出 binary 一致 cascadeKey=`scope+"::"+label`。**provably inert**（_variableLabelScopes 零 reader）→ web debug build 通过、logo diff 不受影响（构造上）。改动文件：value_structs.h / player_containers.h / RuntimeSupport.h / Player.h / PlayerMotionLoad.cpp。
2. **stream③（brick 2）✅ 已按 dispatch/Variant 架构复原**：var-track advance（0x6B786C step / 0x6B7A70 merge）。
   - **56B slot 布局（byte-verified 本轮）**：`{frameIdx@+0, time@+8, interval@+16, flag20(type==0?1:0), flag21(type2→0/type3→1), merged@+22, value@+24(frame["content"]["value"] double), easing@+32(tTJSVariant)}`。
     - sub_6B786C(slot, frameSrc, idx)：slot+0=idx; slot+8=frame["time"]; slot+22=0。
     - 0x6B7A70(slot, frameSrc)：slot+22=1; type=frame["type"]; type==0→slot+20=1(→LABEL_25 早退); 否则 slot+20=0 + slot+16=frame["content"]["interval"] + slot+24=frame["content"]["value"] + slot+32=frame["easing"] Variant CopyRef。
   - **勘误 brick 1 的 VarTrackSlot**：loop2 HM4 gate 实为 slot+20（type==0 时=1→不写 HM4；type!=0 时=0→写）；keyframe value 在 slot+24，**非 item+16**。item+16（HM4 value）来源是**插值**（slot bracket → item+16），由另一步写（待定位 brick 2.5）。
   - **item+0/item+24 同源结论（2026-07-18）**：0x6CD9F0 把 `entry["label"]` 普通转换为 ttstr 存 item+0，0x6CDA58..0x6CDA98 再次 PropGet 同一属性并将原始 tTJSVariant CopyRef 到 item+24；两者同源但类型/所有权不同。旧“字符串 PropGetCount ~0”是无独立证据的经验推断，现已删除。
   - **验证缺口而非实现阻塞**：现有资产仍没有 populated variable 轨道，所以无法做 Android runtime 差分；按仓库规则不从零构造 fixture，但 dispatch/Variant 的忠实复刻、构建与既有非回归照常保留。
3. **loop2（brick 3）**：HM4 populate（gate + upsert），可先做 inert free 函数 + 单测。
4. **getVariable READ（brick 4）**：cascade 接 HM4（HM4-first by raw key → HM1 join → HM2）。

## 五、IDB 改善（已 idb_save）
重命名/注释 9 站点：0x6F51BC/0x686B6C/0x686944/0x686A4C/0x6B2D3C/0x6CD2F4/0x6CD5A8/0x6CD5E4/0x6F2674；
标注 4 HM node 布局 + HM2/HM4 共享 + HM4 唯一写点。
agent-memory: `cpp/plugins/motionplayer/.claude/agent-memory/ida-deep-analyzer/player_4_hashmaps.md`。
