# motion::Player 4 内联哈希表 ↔ 本地容器 权威映射（P2 地基）

> 日期：2026-06-02
> 方法：ida-deep-analyzer 递归反编译 4 个 HM 的 insert/lookup 调用点 + helper，byte-verify key/value 类型。
> 权威：libkrkr2.so。ctor 0x6CED30 / dtor 0x6CFADC / getVariable 0x533E1C。
> 性质：只读分析。IDB 已重命名 9 站点 + idb_save。**未改 cpp/**。
> 解决：[Player_Class_Layout_Alignment_TODO.md](Player_Class_Layout_Alignment_TODO.md) §1.2 + §2.5「4↔6 HM 映射」最大未决项。

## 结论：4 个 HM ↔ 本地 4 个 map 是 1:1 真实镜像，且本地命名/类型注释**已全部正确**。

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
| `_timelines`<string,TimelineState> | 1005 | **port 凭空发明** | 二进制无 timeline map；时间线状态在 node deque 2632B 节点内 + +456/+1120 游标 |
| `_disabledSelectorTargets`<string,bool> | 1036 | **port 凭空发明** | 二进制无对应 |
| `_parameterEntryById`<string,size_t> | 1039 | **port 凭空发明** | 二进制无；参数走 HM1/var-track deque |
| `_layerIdsByName`<string,int> | 1012 | **EmoteEngine(1496B) 字段误植** | 语义 = EmoteEngine+1440 ttstr→double HM；Player* 存于 engine+1064（0x67D3E8）|
| `_layerNamesById`<int,string> | 1013 | **EmoteEngine 字段误植** | 同上 |
| `_renderLayerStates`<int,LayerRenderState> | 1014 | **EmoteEngine 字段误植** | EmoteEngine_setVariable 0x671228 操作 engine+1384/+1440 + per-type animator deque engine+256/+336/+416/+576/+656 |

> 注：上表「误植/发明」是**容器归属**裁决（架构级）。本地这些 map 当前在 render/timeline live path 被使用、
> 差分依赖它们、本地无 Android oracle → **移除/迁移属 P3 终极重构（pimpl 内联回 1384B），禁止盲改**。
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
| +24 | labelName ttstr | = PropGet("label")（sub_A0FB64）| label 内容快照 |
| +48 | slot0 (56B) | memset 0；slot0+20(=item+68)=1 | per-track 帧 slot；gate flag@+20 |
| +104 | slot1 (56B) | memset 0；slot1+20(=item+124)=1 | 同上 |

### READ 侧 key 形态（本轮反编译，brick 1 锚点）
- `Player_evalKey_cascade` @0x6CD23C：HM4 按**原始 lookup key**（`ttstr_c_str(key)` 的 hash，**不 join**）查找，命中取 `*(node+16)` 为 double。
- `Player_getVariable_wrapper` @0x533E1C：cascade 作用于 `*(a1+1064)`（= 内嵌 Player），scope-gate 在 HM1-join 路径与 HM4-first 路径之间分派。
- ⇒ HM4 命中要求 lookup key == 存储的 item+0 cascadeKey。结合 initVariables 写 item+0 = `scope+"::"+label`（concat），**权威 cascadeKey 形态 = scope 存在 ? scope+"::"+label : label**，labelName(item+24)=label。active `initVariables` 旧写的 scope-**后缀**确证为分歧。

### Player+1296 基地原问题（已在 brick 1 修复）
1. ~~两个竞争模型~~：active `std::vector<VariableLabelEntry>`（错形态 vector）vs `std::deque<VariableLabelScope>`（**形态正确**=deque，但从未实例化的死 alias）。**勘误**：deque alias 形态本就对，问题是活的是 vector、死的是 deque。
2. **缺 value 字段**：binary item+16（HM4 读的 double）在两 struct 都缺。
3. **key 分歧（确证）**：binary item+0 = `scope+"::"+label`（concat）vs active `initVariables` 写 scope-后缀。
4. **slot 误映**：`VariableLabelScope` 的 scope@+64 / flagValidated@+108 实落在 +48/+104 两个 56B slot **内部**（gate flag = slot+20 = +68/+124），非顶层字段。
5. **value-writer 延迟**：item+16 仅由 var-track advance **stream③**（sub_6B786C/sub_6B7A70）写；DEFERRED。⇒ loop2 即使移植也 **inert-by-data**（gate `!flag` 永不通过）——与二进制"无轨道前进"行为一致，非 bug。

### 忠实 brick 路线
1. ✅ **基地重构（brick 1）DONE（2026-06-02）**：`VariableLabelScope` 补全为 {cascadeKey, activeSlotCursor, value, labelName, VarTrackSlot slot[2]}（slot 含 +20 gateFlag）；删死 struct `VariableLabelEntry`；Player 字段 `vector<VariableLabelEntry> _variableLabelEntries` → `deque<VariableLabelScope> _variableLabelScopes`（容器形态对齐 deque）；`initVariables` 改产出 binary 一致 cascadeKey=`scope+"::"+label`。**provably inert**（_variableLabelScopes 零 reader）→ web debug build 通过、logo diff 不受影响（构造上）。改动文件：value_structs.h / player_containers.h / RuntimeSupport.h / Player.h / PlayerMotionLoad.cpp。
2. **stream③（brick 2）**：var-track advance（sub_6B786C step / sub_6B7A70 merge）写 item+16 value + toggle activeSlotCursor + 清 slot gateFlag。
3. **loop2（brick 3）**：HM4 populate（gate + upsert），可先做 inert free 函数 + 单测。
4. **getVariable READ（brick 4）**：cascade 接 HM4（HM4-first by raw key → HM1 join → HM2）。

## 五、IDB 改善（已 idb_save）
重命名/注释 9 站点：0x6F51BC/0x686B6C/0x686944/0x686A4C/0x6B2D3C/0x6CD2F4/0x6CD5A8/0x6CD5E4/0x6F2674；
标注 4 HM node 布局 + HM2/HM4 共享 + HM4 唯一写点。
agent-memory: `cpp/plugins/motionplayer/.claude/agent-memory/ida-deep-analyzer/player_4_hashmaps.md`。
