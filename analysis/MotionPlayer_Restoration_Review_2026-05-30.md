# MotionPlayer 源代码还原 Review 报告

> 日期：2026-05-30
> 方法：4 个 `class-layout-auditor` 子 agent 递归审计，对比 libkrkr2.so（IDB: `libkrkr2.so.i64`）
> 范围：cpp/plugins/motionplayer/ 核心类的**类布局层**（字段偏移/类型、vtable、ctor/dtor、对象生命周期、容器选型）
> 性质：只读 review，未修改本地代码或 IDB
> 关联：[MotionPlayer_EmotePlayer_Misalignment_Report.md](MotionPlayer_EmotePlayer_Misalignment_Report.md)、[Player_Class_Layout_libkrkr2so.md](Player_Class_Layout_libkrkr2so.md)、[Player_Class_Layout_Alignment_TODO.md](Player_Class_Layout_Alignment_TODO.md)

## 总览

| 类 | 结论 | 地基（布局/vtable/生命周期） | 主要问题 |
|----|------|------------------------------|----------|
| EmoteAngleController | ✅ 完全对齐 | ✅ | 仅 ctor 多做无害清零 |
| EmoteVarController | ⚠️ 部分偏差 | 字段偏移 ✅ | 堆数组 4× 超额分配（真 bug）+ element 内部偏移注释错 |
| EmoteEngine | ⚠️ 部分偏差 | 1496B / 无 vtable / 裸指针 ✅ | 6 个内嵌 unordered_map 被误建模成裸字节块 + 伪字段 |
| EmotePlayer | ⚠️ 部分偏差 | 24B 壳偏移 / vtable / 引用计数 ✅ | 对象链注释错（两条独立链）+ 多注册 ctor |
| Player (MotionPlayer) | ⚠️ 部分偏差 | 1384B / 无虚函数 / new-delete ✅ | 系统性 STL 容器替代 KiriKiri 内联容器 + HM 6→4 映射未定 |

**地基普遍稳固**：4 个类的 vtable、对象生命周期（裸指针 + 手动 new/delete，无智能指针）、ctor 初值、标量字段类型均已对齐。剩余偏差集中在**内部容器实现**与**注释/语义**。

---

## 按严重度排序的问题清单

### P0 — 数据流分歧（真 bug，读/写错数据）

#### P0-1: EmoteVarController 堆数组 4× 超额分配
- **位置**：`cpp/plugins/motionplayer/EmoteVarController.cpp:26-29`、step 逻辑 :56/:67-69/:79-88
- **二进制**（ctor `EmoteVarController_ctor_20Bdeque` @ 0x667030）：`is_mul_ok(count,4)` → `v5 = 4*count` 字节 → 三个 `operator new[](4*count)` **字节** = **count 个 float**，`memset(..,4*count)` 字节。
- **本地**：`channelCount = count*4; new float[count*4]` = `16*count` 字节 = **多分配 4 倍**。
- **step**（@ 0x666BF8）：所有循环边界是 `v38 = *(int)(a1+80) = count`，out 写入只写 count 个 float。本地 step 全程按 `channelCount=count*4` 操作 4 倍数据，数据流与二进制分歧。
- **佐证**：EmoteEngine ctor reset 段 `memcpy(*(+88), &seed, 4*count)` 字节 = count 个 float，与 ctor 的 count-float 分配一致。

#### P0-2: EmoteEngine 内嵌 unordered_map 被误建模成裸字节块
- **位置**：`cpp/plugins/motionplayer/EmoteEngine.h:156-179`、:235-253
- **二进制**（ctor `EmoteEngine_ctor` @ 0x67E38C；dtor `EmoteEngine_dtor`(sub_67F4B8) @ 0x67F4B8）：偏移 `+824/+880/+936/+1272/+1328/+1384/+1440` 是 **7 个内嵌 libstdc++ `unordered_map<ttstr,V>`**（每个 56B），ctor 中 7 处 `M_next_bkt(ptr,10)` 是铁证。
- **本地**：前 6 个错误建模为 `uint8_t[]` 裸字节块（`_inlineVectorBlocks_*` / `_scalarField_824..864`），只正确建模了 HM#7（+1440，`_labelToValueHM2`）。
- **fixer 复核修正（2026-05-30）**：
  - 实际是 **7 个** map（不是 review 初判的 6 个）。
  - `_bindListHead@1456` **不是伪字段** —— 它物理上是 HM#7 的 `_M_before_begin._M_nxt`，且在 `EmoteEngine.cpp` dtor(:89-94) 与 progress(:211) 中被**真实遍历**（按节点插入链顺序）。删除它属**方法体级**改写（阶段 C），不是纯布局动作。
  - 区域内还夹着 **4 个 `std::vector<tTJSVariant*>`**（@800/992/1016/1040，dtor 逐个 `tTJSVariant_Release`+delete），被一起压平进字节块。
  - **此项当前不是 live bug**：那 6 个错块经 grep 确认在 cpp/ 中零真实读写；被真实使用的 HM#7 本地已正确实现。属保真度/清晰度缺口。
- **纠错记录**：推翻了旧 agent 记忆 `class-layout-auditor/emoteengine_1496b_layout.md`（错记为"4 个 inline vector reserve(10) 块，未详细反编译"）。验证后的完整字节图见 `.claude/agent-memory/binary-alignment-fixer/emoteengine_1496b_hashmap_layout.md`。
- **IDB 已更正（非破坏，已 idb_save）**：`sub_67F4B8`→`EmoteEngine_dtor`；`Player_HM2_upsert_labelToValue`(0x686944)→`ttstr_doubleMap_upsert`（它是 Player HM2 与 EmoteEngine HM7 共用的通用 upsert）；ctor 8 处偏移加布局注释。

##### EmoteEngine 1496B 验证后字节图（fixer 反编译产出）
HM node = `new(0x20)`=32B：`{_M_nxt@0, ttstr_key@8, value@16, hash@24}`，7 个 map 共用 upsert `ttstr_doubleMap_upsert` @ 0x686944。

| 偏移 | 大小 | 类型 |
|------|------|------|
| 0..799 | 800 | 10× KiriKiri deque（80B/个）|
| 800..823 | 24 | `std::vector<tTJSVariant*>` |
| 824..879 | 56 | **HM#1** `unordered_map<ttstr,V1>` |
| 880..935 | 56 | **HM#2** `unordered_map<ttstr,V2>` |
| 936..991 | 56 | **HM#3** `unordered_map<ttstr,V3>`（dtor `sub_683E40(node+1)`，value 非 variant）|
| 992..1063 | 72 | 3× `std::vector<tTJSVariant*>`（992/1016/1040）|
| 1064 | 8 | `Player*`（new(0x568)）|
| 1072..1120 | 56 | 7× controller 指针 |
| 1128 | 8 | heap 指针（transform/matrix）|
| 1159/1160/1162 | — | `bool _syncWaiting` / `int32=1` / `bool _dirty=1` |
| 1168/1176/1200 | 8/8/8 | `double` meshRatio + dup / `double=1.0` |
| 1184/1192 | 8/8 | `double` bust spring 常数 |
| 1208/1228/1248 | 60 | 3× 小对象（sub_A0F778，20B stride）|
| 1272..1327 | 56 | **HM#4** `unordered_map<ttstr,V4>` |
| 1328..1383 | 56 | **HM#5** `unordered_map<ttstr,V5>`（dtor 另有 `sub_68577C`）|
| 1384..1439 | 56 | **HM#6** `unordered_map<ttstr,V6>` |
| 1440..1495 | 56 | **HM#7** `unordered_map<ttstr,double>`=`LabelValueMap`；`_M_before_begin@1456`=本地 `_bindListHead` |

**value 类型现状**：仅 HM#7=`<ttstr,double>` 完全验证。HM#1/2/4/5/6 的 value 由 setVariable dispatch 写入路径决定（未反编译）；HM#3 value 走 `sub_683E40` 是独立类型。

##### P0-2 分阶段对齐计划（fixer 产出）
- **阶段 A（零方法体涟漪）**：把 6 个错块替换为 typed `unordered_map<ttstr,V>` + 4 个 `vector<tTJSVariant*>` + 1128 heap 指针字段，value 先用占位类型 + TODO。**代价**：libc++ unordered_map header ≠ libstdc++ 56B，EmoteEngine 总大小不再精确 1496B，需 PLATFORM_BOUNDARY 标注（与 Player 现有 STL 容器策略一致）。
- **阶段 B**：反编译 setVariable 写入路径，确定 HM#1/2/4/5/6 value 类型与 HM#3 `sub_683E40` value 结构。
- **阶段 C（方法体级）**：删 `_bindListHead`，把 dtor/progress 的 bind-loop 改写为遍历 HM#7 节点链（需保持二进制插入链顺序——可能需 KiriKiri 内联 hashtable 而非 std::unordered_map 才能保序）。与 P2-4、Player HM 6→4 映射（P1-3）耦合。建议由 module-alignment-driver 从主循环统筹。

### P1 — 结构/类型对齐

#### P1-1: EmoteVarController element 内部偏移注释错
- **位置**：`cpp/plugins/motionplayer/EmoteVarController.h:36-41`
- **二进制** step 读 element：`duration = *(float)(elem+12)`、`powCount = *(int)(elem+16)`（element advance +20）。
- **本地**注释：`endValue@0, duration@4, powCount@8, pad@12`——duration/powCount 偏移与二进制不符（把 +12 当成 uint64 pad）。element 总大小 20B 正确，但内部字段映射错。+0..+11 真实语义（疑似 endValue 广播源）需追写入端确认。

#### P1-2: Player `_evalResultValues` key 类型
- **位置**：`cpp/plugins/motionplayer/Player.h:863`
- 本地用 `std::string` key，二进制 HM2（+320，`Player_HM2_upsert_labelToValue` @ 0x686944）是 `ttstr` key，影响桶分布/迭代顺序。已有 `TODO(A8): retype to detail::LabelValueMap (ttstr key)` 标注。

#### P1-3: Player HM 6→4 映射未定
- 本地 6 个 unordered_map（_motionsByKey/_timelines/_layerIdsByName/_layerNamesById/_renderLayerStates/_disabledSelectorTargets，Player.h:708-749）vs 二进制 4 个 inline HM（+264/+320/+1184/+1240）。需反编译 4 个 HM 的 key 类型与用途定案哪 4 个对应、其余是否为 Web 扩展。当前阻碍完全对齐。

### P2 — 注释/语义/方法体（不影响布局）

#### P2-1: EmotePlayer 对象链注释错 + 字段语义
- **位置**：`cpp/plugins/motionplayer/EmotePlayer.h:44-53`、:31/:39-41
- 实测二进制是**两条独立链**（无继承关系）：
  - `D3DEmotePlayer` 链：native → `EmoteObject(40B)`（+0 = loader `new(0xE8)`，+8 = EmoteEngine，+16/+24 = PSB dispatch vector）→ EmoteEngine(1496B) → Player
  - `EmotePlayer`(24B) 链：native → **+8 直接是 EmoteEngine(1496B)**（同一析构器 `sub_67F4B8` = EmoteEngine_dtor），**无 EmoteObject 中间层**
- 注释误将两者画成单一拓扑。字段语义已查明：`_slot1`(+8) = EmoteEngine\* payload（懒创建），`_slot2`(+16) = ownership/sticky 标志（destroy gate `!*(a1+16)`，对应 ncbind `_sticky`）。建议重命名 `_payload` / `_owned`。

#### P2-2: EmotePlayer 多注册 constructor
- **位置**：`cpp/plugins/motionplayer/main.cpp:299-301`
- 二进制 NCB 注册（@ 0x686148）只注册 `finalize` → `sub_6862C8`（return 0，空操作）。本地多注册了 `NCB_CONSTRUCTOR(())`。取决于 ncbind 是否硬性要求 constructor 才能分配 native instance；若为框架要求则保留并加 PLATFORM_BOUNDARY 注释。

#### P2-3: Player `_tjsRandomGenerator` 注释偏移误标
- **位置**：`cpp/plugins/motionplayer/Player.h:947`
- 注释标 `player+992`，但 +992 实为 `_transformOrder` ttstr，RandomGenerator 实为 +676。字段类型对，仅注释偏移误标。

#### P2-4: EmoteEngine 方法体顺序错（属函数级）
- `applyVarControllers`(sub_6766E0) 真实顺序 pos→color→scale→angle，本地写反。
- ctor 4-controller reset 顺序应为 134/135/137/136，本地 137/136 写反。
- 建议移交 `binary-alignment-auditor` / `binary-alignment-fixer` 在方法级处理。

---

## 反编译符号参考（本次审计确认）

| 符号 | 地址 | 说明 |
|------|------|------|
| EmoteEngine_ctor (sub_67E38C) | 0x67E38C | 1496B ctor，7× `M_next_bkt(ptr,10)` |
| EmoteEngine_dtor (sub_67F4B8) | 0x67F4B8 | EmotePlayer/D3DEmotePlayer 共用析构器 |
| EmoteVarController_ctor_20Bdeque | 0x667030 | `new[](4*count)` 字节 = count float |
| EmoteVarController_step | 0x666BF8 | 循环边界 count，element 20B（duration@12/powCount@16）|
| EmoteAngleController_ctor_12Bdeque | 0x6867B0 | 仅初始化 deque header |
| EmoteAngleController_step | 0x666634 | element 12B（endRad@0/duration@4/powCount@8）|
| EmotePlayerNativeInstance_create | 0x68629C | `new(0x18)` = 24B 壳 |
| EmotePlayer_NCB_classInit | 0x686148 | 仅注册 finalize |
| EmotePlayerNativeInstance_destroy | 0x6862D0 | gate `+8 && !+16` |
| Player_ctor | 0x6CED30 | `new(0x568)` = 1384B |
| Player_dtor | 0x6CFADC | 链式逆序释放 |
| Player_ncb_registerMembers | 0x6D69C8 | 78 个暴露成员 |
| Player_HM2_upsert_labelToValue | 0x686944 | HM2 +320 ttstr→double |

---

## 已确认对齐良好（无需动作）

- 4 个类均无 unique_ptr/shared_ptr 持有，全部裸指针 + 手动 new/delete，符合 CLAUDE.md。
- EmoteAngleController 字段/vtable/大小（0x70）完全一致，仅 ctor 多做无害清零（消除 UB，非字节级偏差）。
- Player 无 virtual 方法（ctor 仅 `*a1=a1` 占位），与二进制一致；方法走 NCB callback 蹦床。
- Player 的 `shared_ptr<PlayerRuntime>` pimpl 已在 A10 阶段删除，容器内联回本体——比旧状态更接近二进制 1384B 布局。
- std::deque header 80B 是 libstdc++/libc++ ABI 差异点，已用 PLATFORM_BOUNDARY 标注，非偏差。

## 容器选型说明（CLAUDE.md ⚠️ 类别）

Player 的内部容器系统性采用 STL（std::deque/unordered_map/vector/list/map）替代二进制 KiriKiri 内联哈希表/deque。这是"语义对齐"的工程折中，不影响函数级方法体对齐推进，但按 CLAUDE.md「不接受功能等价」**永远算 ⚠️**。若追求字节级 1:1，需将 4 个 HM 替换为 prime-bucket+单链内联实现、deque 替换为 KiriKiri deque（P3 长期项）。

---

## 差分驱动追踪 (2026-05-30, CI run 26669751572)

P0-1+P0-2 推送后 differential 全绿。motion_playback trace compare **完美**（m2logo 93帧 / yuzulogo 243帧，逐层 0 mismatch）。但 render-step compare 报 `build_flow_mismatch=92/242`（每帧），被 `--allow-render-flow-diagnostics` 容忍不 fail CI，是下一个「调用链/数据流」对齐目标。

### 首个 build_flow mismatch（本地去掉 diagnostics flag 复现）
```
m2logo localFrame=1 stage=build_flow field=mainListSemanticItems.items[0].flags
  oracle:   layerResolved20 = 0
  wasmtime: layerResolved20 = 1
```
`layerResolved20` = render item `item+20` 字节（Frida `readU8(item,20)`；port 侧 `NativeRenderItemFields::rawFlag20`，RuntimeSupport.h:257）。系统性差异（92/242 帧全中）。

### oracle 语义（反编译 sub_6C4E28 @ 0x6C5DBC）
build 函数第一个循环（main list）对每个 item：
```
if (item+19 /*drawFlag19*/) {
  ...compute clip bounds vs paintBox(+184)/viewport(+200)...
  if (v80<v84 && v83<v85 && !item+16) {        // drawable
    item+21 = 1; 写 clipRect item+216..228;
    if (player+760 /*SeparateLayerAdaptor*/) { if(!item+20) goto LABEL_28; }
    else { 创建 SLA(mainWindow.primaryLayer);
           if(!item+20){ LABEL_28: requireLayerId(); item+424=layerId; item+20 = 1; } }
  } else { item+21 = 0; }                       // 不 drawable：只清 +21，不碰 +20
}
```
**结论**：oracle 只在 `LABEL_28`（requireLayerId 路径）置 `item+20=1`，门控 = `drawFlag19 置位 且 drawable 且 item+20 原为 0`。其它路径永不写 item+20（保持初始 0）。oracle items[0] 的 layerResolved20=0 → 它没满足该门控（drawFlag19=0 或不 drawable）。

### port 现状（置 rawFlag20=true 的两处，均在 execute 阶段 layer 解析后）
- PlayerRenderExecute.cpp:384 `ensureLeafItemLayer`（layerId!=0 且 layerObject 解析成功后）
- PlayerRenderTargets.cpp:831 `ensureAccurateSlaItemLayer`（accurate SLA 路径）

两处都在 execute-stage layer 解析成功后置位，对应 oracle requireLayerId 后的 item+20=1。但 trace 在 **build_commands_leave**（build 阶段，execute 之前）就读到 port=1，说明 port 的 build/execute 阶段划分与 oracle（sub_6C4E28 在 build 阶段即含 requireLayerId）不一致，或某 item 被过早 resolve。

### 下一步（待 binary-alignment-fixer 处理，含回归风险）
- 需确认 port 哪个函数对应 sub_6C4E28 的 build 阶段、build_commands_leave trace 在哪个点采样、为何 items[0] 在 build 阶段就 rawFlag20=1。
- 对齐目标：port 应只在「drawFlag19 && drawable && 原为0」时置 rawFlag20，与 oracle requireLayerId 门控一致；不满足时 build 阶段保持 0。
- **回归风险**：trace compare 当前 0 mismatch（全绿），改 render-flow 须经 CI 验证不破坏 trace/render compare。

### layerId 物化机制差异（de-risk 分析，三方确认 build_flow = Phase B+C 架构重构）
- **port**（NodeTree.cpp:103-104）：tree-build 时**提前、无参、每节点 2 次** `ResourceManager::requireLayerId()`（layerId1/layerId2）。
- **oracle**（sub_6C4E28 LABEL_28）：render-build 循环内**惰性、每 drawable item 1 次** `requireLayerId`（对 player+992 解析出的 render-layer-tree 对象的属性/方法调用）。
- 分配域 / 时机 / 次数全不同。port 当前提前方案**产出正确**（trace compare 0 mismatch 全绿）。
- **结论**：对齐 build_flow 必须把 layer 对象解析 execute→build 搬迁 + 重做 layerId 物化模型（Phase B+C 不可分割），高回归风险，本地无 Android oracle 无法验证，须分阶段 CI 验证。**不应盲目推送拿全绿状态冒险**。

---

## build_flow 重构成功 (CI 26671670507, commit d51cce9)

把 requireLayerId 物化 + rawFlag20 闩锁按 sub_6C4E28 LABEL_28 从 execute 搬回 build 循环（+ player+760 常驻 SLA 成员），**零回归**：
- **trace compare：仍 0 mismatch 全绿**（m2logo 93帧 / yuzulogo 243帧逐层一致）。
- **build_flow_mismatch：yuzulogo 242→0（完全对齐）；m2logo 92→81**。
- 值域保真策略生效：`requireLayerId`→`node.layerId1` 同值 backing，layerId 值不变，仅物化时机 execute→build。

### 下一目标：m2logo item+18 (skipFlag1) — frame12+ items[1]
新 first_mismatch（layerResolved20 修复后浮现）：`mainListSemanticItems.items[1].flags flag18` oracle=0 / port=1。
- **oracle 逻辑**（sub_6C2334 @ 0x6c3380-0x6c33c0，node stride 2632）：
  ```
  v298 = 1;                          // 默认
  if ((a6 & 1) == 0)                 // a6&1 = 继承自父的 flag（递归 build 下传）
      v298 = (node+48 != 0);
  item+18 = v298;                    // == inheritedFlag18 || (node+48 != 0)
  ```
  （邻近：item+17 = `((preview?1097:1089) & (1<<node+28)) == 0`；item+16 = node+201）
- **port**（PlayerRenderItems.cpp:471）：`skipFlag1 = !(inheritedFlag18 || (node.priorDraw != 0))`。
- **差异性质**：yuzulogo 全匹配、m2logo 仅 items[1] frame12+ 差，说明**非普遍极性反转**，而是 `node+48` 的语义映射（是否真是 priorDraw）或 inheritedFlag18 递归传播在该 case 下不一致。下一轮须：(1) 反编译确认 binary node+48 字段语义（vs port node.priorDraw）；(2) 核对 inheritedFlag18 在 sub_6C2334 递归中如何下传（a6 参数）vs port inheritedFlag18 计算；(3) 据此对齐 PlayerRenderItems.cpp:471，CI 验证保持 trace 全绿。
