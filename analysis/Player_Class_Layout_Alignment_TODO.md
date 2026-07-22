# motion::Player 类布局对齐 TODO 清单

> 审计目标: 本地 `cpp/plugins/motionplayer/Player.h` (类声明 L88) + `std::shared_ptr<detail::PlayerRuntime> _runtime` (Player.h:562, 定义在 RuntimeSupport.h:223) 对比 libkrkr2.so `motion::Player` (0x568=1384 字节)。
> 权威来源: libkrkr2.so 反编译。ctor `Player_ctor` @ 0x6CED30,dtor `Player_dtor` @ 0x6CFADC,NCB `Player_ncb_registerMembers` @ 0x6D69C8。
> 本次审计已重新反编译 ctor/dtor/`sub_149EDF8` 校验容器选型(见下方 §0 修正)。
> **本清单为纯审计产物,不修改任何 cpp/ 代码。**

---

## 审计结论: 🔧 需要重新设计

本地 Player 是一个**功能等价但架构完全偏离**的实现:
- 1384 字节扁平 POD-with-containers 对象 → 本地拆成 `Player` 直接成员(40+ 个)+ `shared_ptr<PlayerRuntime>` pimpl。
- 二进制 6 个内部容器(4 哈希表 + 2 deque)+ 4 个动态数组/list → 本地 `std::unordered_map×6 + std::list + std::deque + std::unordered_set×2 + std::vector + std::map`。
- 二进制无 vtable(Player 不是多态类,ctor 无 vptr 写入,无虚函数表)→ **本地也无 vtable,这一项 ✅ 对齐**(见 §3)。

按 CLAUDE.md "完全对齐架构,不接受功能等价",**全部容器选型与 pimpl 拆分均判为偏差**。但偏差严重度分层:语义已对齐(⚠️)的可延后,字段缺失/类型错(❌)的需先修。

---

## §0 本次反编译修正(纠正旧文档歧义)

| 旧文档说法 | 反编译实证(ctor 0x6CED30 / dtor 0x6CFADC) | 结论 |
|---|---|---|
| +264/+320/+1184/+1240 = "hashMap" (Player_Class_Layout) vs "controller deque" (Misalignment Report) | dtor 对这 4 处都做 **链表节点 `[next, value]` 逐个 operator delete + bucket 数组 memset + 内联哨兵判堆** → 是 **KiriKiri 哈希表**(prime 桶数 via sub_149EDF8,负载因子 float@+296/+352/+1216/+1272=1.0f) | **4 个哈希表**。注:旧文档里把这些当成 controller deque / hairScale,实为 EmoteObject 引擎的偏移被误抄(见下行) |
| **EmoteObject vs Player conflation**(EmotePlayer_Internal_Implementation.md 把 0x5D8=1496B 对象整体称为 "Player") | sub_67E38C(1496B 对象的构造)内部 `v13=operator new(0x568); Player_ctor(v13,a2); a1[133]=v13` | **只有一个 Player 类,永远 1384B(0x568),ctor sub_6CED30**;0x5D8=1496B 的是 **EmoteObject 引擎**,它在 +1064 持有真正的 Player 指针。hairScale(+1184)/partsScale(+1192)/bodyScale(+1200)/variableHashMap(+1384) 等都是**引擎**字段,不是 Player。EmotePlayer NCB 访问器 sub_681F20/28/30 写的是引擎 +1184/+1192/+1200。已修正 EmotePlayer_Internal_Implementation.md §2.4 |
| sub_149EDF8 用途未知 | 二分查找 prime 桶数表 `qword_16496D0`,写桶数到 `a1+8`,读负载因子 `*(float*)a1` | 哈希表桶数选择器,确证哈希表 |
| +384 render list "56-byte stride" | dtor `v4 += 7` (7 qword=56B),entry+0 = tTJSVariant* | 确认 stride=56 |
| +936 旧称 variable list "44-byte stride" | fresh producer/consumer 审计确认元素为 `{int32 kind; tTJSVariant a; tTJSVariant b;}`；两个 20B Variant 的 copy/dtor 分别调用 `sub_A0FB64/sub_A0F778` | 这是 child→parent 聚合后清空的 DEAD residual render buffer，不是 live draw list，也不是 ttstr variable list |

---

## §1 逐字段对齐表(按二进制偏移)

状态图例: ✅对齐 / ⚠️语义对齐但容器/布局不同 / ❌缺失或类型错 / 🔬需进一步反编译

### 1.1 头部 / 自引用 / 节点 deque

| 偏移 | 二进制类型 | 二进制语义 | 本地对应 | 状态 | TODO |
|---|---|---|---|---|---|
| +0 | ptr | self_ptr (= this) | (无) | ❌ | 二进制对象首字段是自引用指针。本地无。复刻需在对象起始放 `self`。低优先,仅影响某些自指逻辑 |
| +8 | ptr | next/prev (链表 0) | (无) | ❌ | 同上,节点链表字段 |
| +16 | ptr | objthis (progress 时设) | (无,本地用参数传 objthis) | ⚠️ | 本地 progressCompat 通过参数拿 objthis,语义等价。复刻需内联字段 |
| +24..40 | struct | 节点 label map/tree (sub_6DD228 哨兵) | `PlayerRuntime::nodeLabelMap` (std::map<string,int>) | ⚠️ | 二进制是带哨兵的 KiriKiri map;本地 std::map。容器不同 |
| +32/+48/+56 | list 哨兵 | `a1[6]=a1[7]=a1+4` list head | (无显式) | 🔬 | +48/+56 list 哨兵用途未明,需反编译插入点确认承载什么 |
| +120 | double | rootOffsetX | `Player::_rootOffsetX` (Player.h:622) | ✅ | 已标注 player+120 |
| +128 | double | rootOffsetY | `Player::_rootOffsetY` (Player.h:623) | ✅ | 已标注 player+128 |
| +144 | float | cameraOffsetX | `Player::_cameraOffsetX` (Player.h:631) | ✅ | float,已标注 |
| +148 | float | cameraOffsetY | `Player::_cameraOffsetY` (Player.h:632) | ✅ | float,已标注 |
| +152 | double | boundsMinX (DBL_MAX) | `Player::_boundsMinX=1e308` (Player.h:636) | ⚠️ | 值近似;二进制是 0x7FEFFFFFFFFFFFFF(真 DBL_MAX),本地 1e308≠DBL_MAX。**TODO: 改用 `std::numeric_limits<double>::max()`** |
| +160 | double | boundsMinY | `Player::_boundsMinY=1e308` (Player.h:637) | ⚠️ | 同上,ctor 用 `*((_OWORD*)a1+10)=xmmword_14D68E0` 初始化 +160/+168,需核对该常量 🔬 |
| +168 | double | boundsMaxX (-DBL_MAX) | `Player::_boundsMaxX=-1e308` (Player.h:638) | ⚠️ | 改 `-max()` |
| +176 | double | boundsMaxY | `Player::_boundsMaxY=-1e308` (Player.h:639) | ⚠️ | 同上 |
| +184..232 | deque | 节点 deque (sub_6F4E90 init),node=2632B | `PlayerRuntime::nodes` (std::deque<MotionNode>) | ⚠️ | **核心容器**。二进制 KiriKiri deque,2632B/节点;本地 std::deque<MotionNode>,MotionNode 布局不保证 2632B 对齐。见 §2.1 |
| +200 | ptr | **rootNodePtr** (deque 块指针) | `_runtime->nodes[0]` | ⚠️ | 关键: 二进制几乎所有 root 属性 getter 读 `*(player+200)`。本地用 nodes[0]。MotionNode 字段偏移须与 2632B Node 对齐(见 §2.2) |

### 1.2 哈希表区 (4 个 KiriKiri 哈希表)

> ✅ **已解决（2026-06-02）**: 4↔N 映射经 insert/lookup 调用点逐个反编译 + byte-verify 确定。
> 详见 [Player_4_HashMaps_Container_Mapping.md](Player_4_HashMaps_Container_Mapping.md)。本地 4 个 mirror
> 命名/类型注释**全部正确**。下表更新为 RESOLVED；上方旧"候选/value=tTJSVariant*"判断均被推翻（4 HM value
> 实为 double/double/696B-payload/double，无一是 tTJSVariant*）。

| 偏移 | node size | key | value (byte-verified) | 本地镜像 | 状态 |
|---|---|---|---|---|---|
| +264 HM1 | 96B | joined "scope::label" ttstr | **node+48 double** (富 node) | `_evalCascadeMap` (L1158) | ✅ RESOLVED |
| +320 HM2 | 32B | raw label ttstr | **node+16 double** | `_evalResultValues` (L1197) | ✅ RESOLVED |
| +1184 HM3 | 720B | node-path ttstr | **node+16 696B PerNodeLayerState** | `_perNodeLayerStateMap` (L1180) | ✅ RESOLVED |
| +1240 HM4 | 32B (共享 HM2 node) | label ttstr | **node+16 double** | `_variableSnapshotMap` (L1190) | ✅ RESOLVED |

> **裁决（替代旧"最大未知缺口"）**: 二进制 1384B Player 仅 4 HM + +24 std::map = 5 关联容器。本地多出 ~7 个：
> - **port 凭空发明**（binary 无）: `_motionsByKey` / `_disabledSelectorTargets` / `_parameterEntryById`；`_timelines/_playingTimelineLabels` 已于 2026-07-19 删除
> - **EmoteEngine(1496B) 字段误植**: `_layerIdsByName`(1012) / `_layerNamesById`(1013) / `_renderLayerStates`(1014)（语义 = engine+1440/+1384，Player* 存 engine+1064）
> - 移除/迁移属 **P3 终极重构**（pimpl 内联回 1384B），差分依赖、本地无 oracle → 禁止盲改。
> - **真正 open 的 P0**: 4 个 mirror 已就位但**多为空**，缺 WRITE 侧填充（bindParameterValue 0x6C4668 / resetMotionState 0x6B2D3C / initVariables 0x6CD750）。这是 M3 getVariable 级联的前置实施项。

### 1.3 渲染列表 / 时间线 / bounds 工作区

| 偏移 | 二进制类型 | 二进制语义 | 本地对应 | 状态 | TODO |
|---|---|---|---|---|---|
| +376 | ptr | activeTimeline | 🔬 (`_runtime->activeClip`?) | 🔬 | 需确认 |
| +384/+392 | 动态数组 stride=56 | **旧 renderList 映射已证伪**；容器身份须由其构造/消费者独立确认 | 与 caller-stack `PreparedRenderItemList` 无关 | 🔬 | 56B 是 Android STL/元素 ABI 证据，不是 wasm32 本地对象尺寸要求。见 §2.3 |
| +400..408 | 容量字段 | renderList capacity | (vector 内部) | ⚠️ | std::vector 内含 |
| +408..432 | struct/list | someList (sub_6DD144 清理) | 🔬 | 🔬 | 需反编译 sub_6DD144 确认 |
| +424 | ptr | someList arg | 🔬 | 🔬 | dtor 传给 sub_6DD144 |
| +456 | double | clampedEvalTime | `Player::_clampedEvalTime` (Player.h:573) | ✅ | 已标注 player+456 |
| +464 | double | emoteAngle | 🔬 | 🔬 | 本地 `_rotateAngle`? 需确认偏移 |
| +472 | double | cameraAngle | `Player::_cameraAngle` (Player.h:588) | ✅ | 已标注 a1+472 |
| +480 | uint16 | progressFlags (init 257) | 🔬 (拆成多个 bool?) | ❌🔬 | ctor `*((_WORD*)a1+240)=1`,实际 §4 写的 257 需复核。本地无单一字段。**TODO: 反编译用到 +480 的 progress_inner,确认是位标志还是单值** |
| +481 | bool | first-frame flag | 🔬 | 🔬 | progress_inner 用 |
| +482 | bool | emoteMode | `PlayerRuntime::isEmoteMode` (RuntimeSupport.h:387) | ⚠️ | 语义对齐,位置在 runtime |
| +483 | bool | motionCompleted | 🔬 | 🔬 | ctor 置 0,progress 检查。本地未显式 |

### 1.4 ttstr 字段块 (+484..+736) + 变体

| 偏移 | 二进制类型 | 二进制语义 | 本地对应 | 状态 | TODO |
|---|---|---|---|---|---|
| +484..508 | ttstr | dtor sub_A0F778 释放 | 🔬 | 🔬 | 4 个连续 ttstr,语义未定。需反编译写入点 |
| +508..528 | ttstr | 同上 | 🔬 | 🔬 | |
| +528..548 | ttstr | rootMatrix? (ctor sub_699940 用 player+528) | 🔬 | 🔬 | 与 skipRootMatrix(+908) 关联 |
| +548..568 | ttstr | 同上 | 🔬 | 🔬 | |
| +592 | double | deltaTime (speed*dt) | 🔬 | 🔬 | progress 写 |
| +600 | double | cameraDamping (1.0) | `Player::_cameraDamping=1.0` (Player.h:621) | ✅ | 已标注 player+600 |
| +608 | bool | updateMarker / noUpdateYet | `Player::_noUpdateYet=true` (Player.h:700) | ✅ | 已标注 player+608 |
| +609 | bool | syncPlayFlag | 🔬 | 🔬 | progress_inner sync |
| +610 | bool | forceUpdate (0) | 🔬 | 🔬 | ctor 置 0 |
| +611 | bool | flag_611 (0) | 🔬 | 🔬 | |
| +613 | bool | needsInternalAssignImages | `Player::_needsInternalAssignImages` (Player.h:640) | ✅ | 已标注 flag +613 |
| +616..636 | ttstr | dtor 释放 | 🔬 | 🔬 | |
| +636..656 | ttstr/variant | ctor sub_A0F5E0(player+636, rmArg) | `Player::_resourceManager` (tTJSVariant)? | ⚠️🔬 | ctor 从构造参数 a2 拷贝。a2=resourceManager。需确认 |
| +656..676 | ttstr/variant | ctor sub_A0F5E0(player+656, rmArg) (a1+82) | `PlayerRuntime::sourceCacheObject`? | ⚠️🔬 | Misalignment 称 +656 是 SourceCache。需确认 |
| +676..696 | variant | ctor 将 `sub_9C8440` 创建的对象写入 +676 | render descriptor（非 RandomGenerator） | ✅语义纠正 | **2026-07-23 纠正**：+676 是 render descriptor；0x6CF080 把 +716 color 对象以 key `color` PropSet 到它。旧“RandomGen 在 +676”与“只需修注释”结论已证伪。 |
| +696..716 | ttstr | dtor 释放 | 🔬 | 🔬 | |
| +716..736 | variant | ctor 将第二个 `sub_9C8440` 对象写入 +716 | render descriptor 的 color 对象 | ✅语义纠正 | 0x6CF080 以 key `color` 把 +716 PropSet 到 +676；不是 RNG 配置。 |
| +736..756 | ttstr | dtor 释放 | 🔬 | 🔬 | |

### 1.5 D3D / motion 变体指针 / 相机速度

| 偏移 | 二进制类型 | 二进制语义 | 本地对应 | 状态 | TODO |
|---|---|---|---|---|---|
| +760 | ptr | d3dAdaptorPtr (0) | 🔬 (本地 D3DAdaptor 经 runtime?) | 🔬 | dtor delete + sub_6CFFB8 |
| +768 | refcounted string value* | pending stealthMotion | `Player::_pendingStealthMotion` (ttstr) | ✅ | `Player_play@0x6B21E8` flush 后 Release/null |
| +776 | refcounted string value* | pending stealthChara | `Player::_pendingStealthChara` (ttstr) | ✅ | chara setter/child path flush 后 Release/null |
| +784 | double | cameraVelocityX (0) | `Player::_cameraVelocityX` (Player.h:618) | ✅ | 已标注 player+784 |
| +792 | double | cameraVelocityY (0) | `Player::_cameraVelocityY` (Player.h:619) | ✅ | 已标注 player+792 |
| +800 | double | cameraVelocityZ (0) | `Player::_cameraVelocityZ` (Player.h:620) | ✅ | 已标注 player+800 |
| +808 | double | boundsCalc_field1 (1.0) | 🔬 | 🔬 | ctor a1[101]=1.0 |
| +832 | double | boundsCalc_field4 (1.0) | 🔬 | 🔬 | ctor a1[104]=1.0 |
| +840 | ptr | field_105 (0) | 🔬 | 🔬 | |
| +864..908 | tTVPComplexRect | previous-frame draw region (sub_7E2344/sub_7E24AC, Clear@0x7E2544, Or@0x7E2B38, GetBound@0x7E3ECC) | `Player::_drawRegion` | ✅ | `Player_renderToCanvas@0x6C7440` 每帧 Clear+Or paintBox；`Player_drawToLayerCompat@0x6D2D80` 下一帧 clear 时读 bound |
| +908 | bool | skipRootMatrix (0) | 🔬 | 🔬 | ctor 关联 player+528 |
| +909 | bool | useD3DFlag | `Player::_d3dDrawMode` (Player.h:603) | ✅ | 已标注 player+909 |
| +912 | int | pixelateDivision (默认100) | (无) | ❌ | **本地缺失字段**。ctor `*((_DWORD*)a1+228)=100`。**TODO: 加 `int _pixelateDivision=100;` (player+912),NCB useD3D/pixelateDivision getter/setter** |

### 1.6 变量列表 / 4 个 TJS variant 指针 / 5 个 ttstr

| 偏移 | 二进制类型 | 二进制语义 | 本地对应 | 状态 | TODO |
|---|---|---|---|---|---|
| +936/+944 | `std::vector`，Android 元素 stride=44 | DEAD child-motion render aggregate；元素 `{int32 kind; tTJSVariant a; tTJSVariant b;}` | `Player::_childMotionRenderAggregate` | ✅ | 两个 writer 都 insert-at-begin 后 clear child；无 producer/consumer，和 live caller-stack render lists 完全分离。见 §2.4 |
| +960 | refcounted string value* | primary chara | `Player::_chara` (ttstr) | ✅ | NCB chara getter 0x6D9470 |
| +968 | refcounted string value* | stealthChara | `Player::_stealthChara` (ttstr) | ✅ | NCB stealthChara getter 0x6D9490 |
| +976 | refcounted string value* | primary motion | `Player::_motionKey` (ttstr) | ✅ | NCB motion getter 0x6D9544 |
| +984 | refcounted string value* | stealthMotion | `Player::_stealthMotion` (ttstr) | ✅ | NCB stealthMotion getter 0x6D9564 |
| +992..1012 | tTJSVariant | ResourceManager dispatch（ctor 第三次 AddRef 拷贝） | 本地统一由 `Player::_resourceManager` 表示 +636/+656/+992 三个同指针槽 | ✅语义 | `Player_ctor@0x6CED30` 的 0x6CEF28 直接 `tTJSVariant_copy_ctor(this+992, resourceManager_dispatch)`。`Player::random@0x6BA7B8` 对该 dispatch 调 `random`；真正 `Math.RandomGenerator` 在 `ResourceManager_ctor@0x6A88CC` 创建的 RM+144，不在 Player+676。 |
| +1012..1032 | tTJSVariant | `findMotion` 命中 module key 上下文 | `Player::_findMotionContextVariant` | ✅ | `Player_playImpl@0x6B2284` 取 `findMotion` 返回数组元素1写 +1012；`ResourceManager_findMotion@0x6A9ED4` 的元素1来自命中 HashMap 节点 key@+8；随后 `Player_findSource@0x6948E8` 作为参数0传回 RM。 |
| +1032..1052 | ttstr | outline | `Player::_outline` (ttstr, Player.h:569) | ✅ | 已对齐(类型已从 bool 改 ttstr) |
| +1052..1072 | ttstr | meshline | `Player::_meshline` (ttstr, Player.h:613) | ✅ | 已对齐 |
| +1072..1092 | tTJSVariant | motion `tag` frame-array dispatch | `Player::_tagFrameSourceVariant` | ✅ | `Player_initNonEmoteMotion@0x6B365C` CopyRef；`Player_skipToSync@0x6D3504` 与 progress 直接枚举 raw dispatch |

### 1.7 9 个独立 bool 字节 (+1092..+1100) — 已验证

| 偏移 | 二进制 | 本地 | 状态 |
|---|---|---|---|
| +1092 | preview | `Player::_preview` | ✅ | sub_6D9634/sub_6D963C，setter `&1`；completionType 真身是 +1144 int |
| +1093 | syncActive | `Player::_syncActive` | ✅ | sub_6D968C/sub_6D9694；ctor 从 `defaultSyncActive` 初始化 |
| +1094 | cameraActive | `Player::_cameraActive` | ✅ | sub_6D9708/sub_6D9710 |
| +1095 | stereovisionActive | `Player::_stereovisionActive` | ✅ | sub_6D971C/sub_6D9724 |
| +1096 | priorDraw | `Player::_priorDraw` | ✅ | sub_6D9648/sub_6D9650；与 node+48 priorDraw 分离 |
| +1097 | independentLayerInherit | `Player::_independentLayerInherit` | ✅ | sub_6D9768/sub_6CC9D4；`colorWeight` 是独立的 +1156 packed color |
| +1098 | syncWaiting | `Player::_syncWaiting` | ✅ | sub_6D979C；releaseSyncWait@0x6D9A48 清零 |
| +1099 | playing | `Player::_allplaying` | ✅ | Player_getPlaying@0x6D9794 直读；play/init/stop/progress 生命周期维护该 byte |
| +1100 | cameraAlive | `Player::_cameraAlive` | ✅ | sub_6D978C RO getter |

### 1.8 数值字段 (+1104..+1296)

| 偏移 | 二进制类型 | 二进制语义 | 本地对应 | 状态 | TODO |
|---|---|---|---|---|---|
| +1104 | double | cameraFOV | `Player::_cameraFov` 与另一个 `_cameraFOV` owner 并存 | ⚠️🔬 | sub_6D9784 直读 +1104；本地两个 FOV owner 的属性/相机更新数据流仍须收束 |
| +1112 | double | zFactor/coordinate | `Player::_zFactor=1.0` (Player.h:596) | ⚠️🔬 | NCB coordinate getter 0x6D977C 读 +1112 |
| +1120 | double | frameTickCount (0) | `Player::_frameTickCount` (Player.h:592) | ✅ | |
| +1128 | double | frameLastTime/cachedTotalFrames (0) | `Player::_cachedTotalFrames` (Player.h:575,标 +1128) | ✅ | 注意: 旧文档把 frameLastTime 标 +1128,本地 _frameLastTime 默认 +? 需核对 _frameLastTime vs _cachedTotalFrames 偏移 🔬 |
| +1136 | double | frameLoopTime/loopTime (0) | `Player::_loopTime` (Player.h:574,标 player+1136) | ✅ | _frameLoopTime(Player.h:572) 也声明,偏移可能重复 🔬 |
| +1144 | int | completionType (0) | `Player::_completionType` (tjs_int) | ✅ | sub_6D9624/sub_6D962C；NCB `project` 实际别名到 +1012 motion-key 上下文，不是此槽 |
| +1148 | int | maskMode (0) | `Player::_maskMode` (tjs_int, Player.h:593) | ✅ | 已对齐 |
| +1152 | int | progressCounter (0) | 🔬 | 🔬 | progress_inner 清零 |
| +1156 | uint32 | colorWeightPacked / inherited parent color (0xFF808080) | `Player::_colorWeightPacked` | ✅ | sub_6CD710/sub_6CD724 做 R/B swap；父→子传播写同一字段 |
| +1160 | double | outsideFactor (1.5) | `Player::_outsideFactor=1.5` | ✅ | sub_6D965C/sub_6D9664；ctor a1[145]=0x3FF8000000000000 |
| +1168 | double | speed multiplier (1.0) | `Player::_speedMul=1.0` | ✅ | sub_6D967C/sub_6D9684；progress 入口写 +592 = speed*dt |
| +1176 | double | meshDivisionRatio (1.0) | `Player::_meshDivisionRatio=1.0` | ✅ | sub_6D966C/sub_6D9674；与 EmoteEngine 的 ratio pair 分离 |
| +1184..1232 | hashMap HM3 | (见 §1.2) | ✅ 已清理 — `_hairScale` 等曾误标 player+1184 | ✅ | **已解决**: +1184 是哈希表 HM3;_hairScale/_partsScale/_bustScale 是 **EmoteObject 引擎(1496B)** +1184/+1192/+1200 的字段(sub_681F20/28/30,仅 EmotePlayer NCB 引用),不属于 1384B Player。已从 Player 删除字段+setter+NCB 注册,保留在 EmotePlayer。详见 §0 conflation 行 |
| +1296..1368 | deque item=160B | variableDeque (sub_6F4FD8/sub_6CF678) | 🔬 | 🔬 | 需反编译确认存什么(160B/项) |

---

## §2 容器与对象生命周期偏差(架构级)

### 2.1 节点 deque (+184, std::deque<Node 2632B>)
- 二进制: KiriKiri 风格 deque,固定块,node=2632 字节,`*(player+200)` 为当前块指针。
- 本地: `std::deque<MotionNode> nodes` (RuntimeSupport.h:285)。
- **对齐目标**：确认源码容器选型、元素语义、构造/移动/析构和边界行为；不得用自定义块、padding 或 `sizeof==2632` 去复刻 Android libstdc++/ARM64 ABI。

### 2.2 root Node 字段偏移 (经 *(player+200))
旧文档已列 root node 偏移表(+1586 visible / +1587 flipX / +1592 x / +1600 y / +1616 angle(deg) / +1656 opacity 等)。
- **TODO 🔬**：把这些偏移只当作定位原生读写点的坐标，逐项确认命名字段、类型、默认值和数据流；wasm32 由编译器自行布局，不比较 `offsetof/sizeof`，更不补 padding。

### 2.3 `Player+384` 旧 renderList 映射（已证伪）
- caller 反编译已经证明真正的 render main/aux 是 draw 栈上的两只 `std::vector<PreparedRenderItem *>`，不是 `Player+384` 成员。
- 因此不得把 `Player+384` 的 56B 元素或 Android `PreparedRenderItem` 的 0x1B0 对象尺寸变成本地布局目标。
- **TODO 🔬**：从 `Player+384` 自身的构造、push/erase、读取和析构链独立确认它的源码语义；在确认前不再把它命名为 renderList。

### 2.4 DEAD child-motion render aggregate (+936/+944)
- 二进制源码容器为 `std::vector<RenderItem44>`；Android 元素为 `{int32 kind; tTJSVariant a; tTJSVariant b;}`，两个 Variant 分别 copy/destroy。
- 两个 writer（child-motion 与 particle child pass）都把 child range insert 到 parent begin，再 clear child；没有 producer 或 consumer，因此运行时恒为空，但源码 token 仍须保留。
- 本地 `std::vector<detail::DeadChildMotionRenderItem> _childMotionRenderAggregate` 复刻该独立成员。live 渲染由 node-owned persistent item 加 caller-stack main/aux pointer lists 承载，不映射到 +936。

### 2.5 4 个哈希表 vs 本地 ~10 个 unordered_map (§1.2) — ✅ 已解决 (2026-06-02)
- ✅ **映射已建立**。二进制 4 HM ↔ 本地 `_evalCascadeMap`/`_evalResultValues`/`_perNodeLayerStateMap`/`_variableSnapshotMap`（1:1，命名正确）+ `_nodeLabelMap`=+24 std::map。
- ✅ **多余/误植已裁决**: 4 发明 map（motionsByKey/timelines/disabledSelectorTargets/parameterEntryById）+ 3 误植 EmoteEngine map（layerIdsByName/layerNamesById/renderLayerStates）。详见 [Player_4_HashMaps_Container_Mapping.md](Player_4_HashMaps_Container_Mapping.md)。
- **后续 P0**: WRITE 侧填充（mirror 当前多为空）→ 解锁 M3 getVariable 级联。P3 才做 STL→内联 HM + 删/迁多余 map。

### 2.6 pimpl 拆分 (PlayerRuntime)
- 二进制无 pimpl,全部内联在 1384B。本地拆 `Player` 直接成员 + `shared_ptr<PlayerRuntime>`。
- `_runtime` 持有: 节点 deque(+184)、4 哈希表的部分(+264/+320/+1184/+1240)、尚待确认的 +384 容器、variableDeque(+1296)、nodeLabelMap(+24)、以及大量二进制中**不在 1384B Player 上**的辅助容器(_variableAnimators、_type4..8ControllerAnimators、_evalResultList 等——这些很可能对应 **EmoteObject 引擎(1496B,sub_67E38C)**的 7 个 sub_667030 控制器 / 变量哈希表,或是本地新增的功能等价物;需逐个核对归属)。+936 dead aggregate 已是 `Player` 直接成员，不在 `_runtime`。
- **判定**: ⚠️ 整体 pimpl 是源码所有权拓扑偏差。**TODO**: 长期按已证实的直接成员、构造/析构顺序与容器语义收束；不得以 1384B 总尺寸或 ARM64 偏移排布为实现目标。

---

## §3 vtable 审计

- 反编译 `Player_ctor` (0x6CED30): **无 vptr 写入**(无 `*a1 = &vtable` 形式,首字段 `*a1=a1` 是 self-ptr 非 vtable)。
- Player 在 NCB 中以 native instance 注册,所有 TJS 方法是 ncb 自由函数包装(`Player_progressCompat` 等),非 C++ 虚函数。
- **结论**: ✅ Player **不是多态类,无 vtable**。本地 `class Player` 同样无 virtual 方法。vtable 项完全对齐(均无)。

---

## §4 ctor/dtor 对齐

### ctor (0x6CED30) 关键默认值核对(本次反编译实证)
| 字段 | 二进制 ctor 值 | 本地默认 | 状态 |
|---|---|---|---|
| +912 pixelateDivision | 100 | 缺失 | ❌ 加字段 |
| +1156 parentColor | 0xFF808080 (`-8355712`) | 0xFF808080u | ✅ |
| +1160 (a1[145]) outsideFactor | 1.5 (0x3FF8000000000000) | 1.5 | ✅ |
| +1168 (a1[146]) speed | 1.0 | 1.0 | ✅ |
| +1176 (a1[147]) meshDivisionRatio | 1.0 (0x3FF0000000000000) | 1.0 | ✅ |
| +600 (a1[75]) damping | 1.0 | 1.0 | ✅ |
| +152/+168 bounds | DBL_MAX/-DBL_MAX | 1e308/-1e308 | ⚠️ 用 numeric_limits |
| +1092 preview | 0 | false | ✅ |
| +1096 priorDraw | 0 | false | ✅ |
| +1144 completionType | 0 | 0 | ✅ |
| +482 emoteMode | 0 | false | ✅ |
| render descriptor | sub_A0FCC0(+676) | 旧误标为 `_tjsRandomGenerator` | ❌ 需按 descriptor 语义复刻 |
| descriptor color 对象 | sub_A0FCC0(+716)，0x6CF080 PropSet 到 +676 | 🔬 | 需复刻对象关系 |

### dtor (0x6CFADC) 资源释放顺序(权威)
1. sub_6CDE18 预清理
2. +384 未命名 56B-element 容器释放（旧 renderList 命名已证伪）
3. sub_6C0DE8(+1296) 变量 deque
4. Player_resetAndReleaseNodes(节点系统)
5. delete d3dAdaptor(+760)
6. sub_6F436C(+184) 节点 deque 销毁; sub_6CF678(+1296)
7. **HM4(+1240)** 链表清理 + bucket memset + 条件 delete
8. **HM3(+1184)** 链表清理(sub_6DD018)+ memset + 条件 delete
9. 20B value 释放: variant +1072/+1012/+992；ttstr +1052/+1032
10. variant 释放: +984,+976,+968,+960
11. DEAD render aggregate(+936) 释放每项 2 个 tTJSVariant(stride 44)+ delete
12. sub_7E24AC(+864)
13. variant 释放: +776,+768
14. ttstr 释放: +736,+716,+696,+676,+656,+636,+616,+548,+528,+508,+484
15. sub_6DD144(+408)
16. +384 未命名容器二次释放 + delete
17. **HM2(+320)** 链表 + memset + delete
18. **HM1(+264)** 链表(sub_6DD1A0)+ memset + delete
19. sub_6CF9B4(+184); sub_6DD228(+24)

- **判定**: 本地用 RAII(shared_ptr/容器析构),释放顺序由成员声明逆序决定,**不保证**与二进制一致。⚠️
- **TODO**: 复刻需手写析构按上述顺序;但 AddRef/Release(tTJSVariant)生命周期已被本地 tTJSVariant 值语义覆盖,功能等价。优先级低。

---

## §5 修复 TODO 分组(按优先级与依赖)

### P0 — 实施进度(2026-05-29,反编译复核后)
> 本轮在主对话反编译了 Player_ctor(0x6CED30)、sub_6D9624/sub_6D962C(completionType)、
> sub_6D9634/sub_6D963C(preview)、sub_6D9648/sub_6D9650(priorDraw)、
> sub_6D965C/sub_6D9664(outsideFactor)、sub_6D966C/sub_6D9674(meshDivisionRatio)、
> sub_6D967C/sub_6D9684(speed)、sub_6D9768/sub_6CD710/sub_6CC9D4(independentLayerInherit/packed color)、
> sub_681F20(hairScale)。复核推翻了审计对若干 P0 项的"安全机械修改"判断。

**✅ 已实施(铁证,已改 Player.h):**
- **+1092 preview 与 +1144 completionType 解耦** — sub_6D963C 证实 `*(player+1092)=v&1`，getter sub_6D9634 读 preview byte；completionType 由 sub_6D9624/sub_6D962C 直读写 +1144 int。本地分别使用 `_preview` 与 `_completionType`。
- **+1096 priorDraw 恢复为 bool** — sub_6D9648/sub_6D9650 直读写该 byte；本地 `_priorDraw=false`，不再误映射到 +1160 double。
- **+1160/+1168/+1176 三 scalar 恢复注册顺序** — outsideFactor=1.5、speed=1.0、meshDivisionRatio=1.0；分别由 0x6D965C/0x6D9664、0x6D967C/0x6D9684、0x6D966C/0x6D9674 访问。
- **+152/+176 bounds: 1e308 → numeric_limits<double>::max()** — ctor a1[19]=0x7FEFFFFFFFFFFFFF(真 DBL_MAX),a1[22]=0xFFEFFFFFFFFFFFFF(-DBL_MAX)。加 `#include <limits>`。
- **_hairScale/_partsScale/_bustScale 从 Player 彻底删除(连同 NCB 注册)** — 追查 sub_681F20/28/30 的 xref 证实它们**只被 `EmotePlayer_ncb_registerMembers`(0x67FAC8)引用**,Player 注册(0x6D69C8)从不引用;且 ctor 证实 1384B Player 的 +1184 是哈希表 HM3。这些字段是 **EmoteObject 引擎(1496B,sub_67E38C)**的 +1184/+1192/+1200,不是 Player 的(详见下方"EmoteObject vs Player"修正)。已删除:Player.h 字段+3 个 setter 声明、PlayerCore.cpp 三个 setter 定义、main.cpp:197-199 的 Motion.Player `NCB_METHOD`。保留 EmotePlayer 侧(EmotePlayer::_hairScale + main.cpp:317-319/599-601 NCB_PROPERTY)。构建通过(BUILD_EXIT=0)。

**❌ 审计判断被推翻,移出 P0(需重新分析):**
- **+912 pixelateDivision 并非"缺失"** — 它是 `static int _pixelateDivision=1` 在 `D3DEmoteModule`(D3DEmoteModule.h:48),NCB 在 main.cpp:573。但二进制 +912 是 **Player 实例字段,默认 100**(ctor `*((_DWORD*)a1+228)=100`)。问题是"在错误的类上、static 而非实例、默认 1 vs 100"。**TODO(P1):** 决定是否把它迁成 Player 实例字段并补实例 NCB getter/setter(0x6D992C/0x6D9934),评估对 D3D 渲染路径的影响。
- **2026-07-23 关闭：+1144 不是 project 类型冲突** — NCB 字面绑定已证实 +1144 int32 是 `completionType`，本地已由 `_completionType` 承载。TJS `project` 与 `motionKey` 共用 +1012 的 variant/module-key 路径，本地 `_project`/module snapshot 不应再被拿来解释 +1144。旧 TODO 来自 Player NCB 表 off-by-one 命名，已证伪。
- **+992 / +676/+716 已重新闭合（2026-07-23 纠正）** — `Player_ctor@0x6CED30`：0x6CEF28 把 `resourceManager_dispatch` 拷到 +992；0x6CF014..0x6CF024 创建的 +676 对象是 render descriptor，第二个对象存在 +716，0x6CF080 把 +716 以 key `color` PropSet 到 +676。旧“+676=Math.RandomGenerator”结论已证伪。`Player::random@0x6BA7B8` 经 +992 RM dispatch 调 `random`，真 RNG 在 `ResourceManager_ctor@0x6A88CC` 的 RM+144。

**⚠️ 结构确证为同一字段,但合并有跨文件行为变更,移出 P0:**
- **+1156 双字段** — ctor 证实 +1156 是**单个 uint32**(=0xFF808080)。本地 `_colorWeightPacked`(PlayerCore.cpp:146/150 经 sub_6CD710 R-B swap)与 `_parentColorPacked`(父→子传播)都映射 +1156,**应合并为一**。但二进制单字段意味着 colorWeight 设值与父色传播**互相覆盖**,合并会改变本地现行为(目前两字段独立不互扰)。注:NCB "colorWeight" 属性实为 +1097 byte bool(getter sub_6D9768/setter sub_6CC9D4),与 +1156 packed color 是两回事——命名待厘清。**TODO(P1):** 选定权威字段名,统一 3 文件引用,确认互覆盖行为对齐二进制。

### P1 — 偏移/语义复核(需少量反编译,中等影响)
8. ~~**🔬 +960/+968/+976/+984 类型**~~ — **已闭合**：getter、
   `ttstr_c_str` 比较和手工 AddRef/Release 共同证明为 refcounted string-value
   owner，源码层以四个独立 `ttstr` 复刻。
9. ~~**🔬 +1012 类型**~~ — **已闭合**：tTJSVariant module-key 上下文；证据链见 §1.6 表。
10. **🔬 +1104/+1112 相机/坐标** — 单 double vs 本地多字段。
11. **🔬 +1128/+1136 frame 时间字段** — 本地 _frameLastTime/_cachedTotalFrames/_loopTime/_frameLoopTime 四个,二进制只 +1128/+1136 两个,核对去重。
12. ~~**🔬 +1099 playing**~~ — **已闭合**：`Player_getPlaying@0x6D9794`
    直接读该字节；本地 `_allplaying` 对应。
13. **🔬 +480 progressFlags** — 位标志 vs 单值,反编译 progress_inner。

### P2 — 容器映射(大型,有前置依赖)
14. **🔬🔒 4 哈希表 ↔ 6 unordered_map 映射** (前置: 反编译 +264/+320/+1184/+1240 的 insert/lookup) — 这是后续所有容器对齐的**地基**。先做映射表,再判多余/缺失。
15. **🔬 +864 / +408 / +1296 容器** — 反编译 sub_7E2344 / sub_6DD144 / sub_6F4FD8 确认类型与 stride。
16. **MotionNode 语义字段完整性** — 用原生偏移定位读写点，但只核对字段类型、数据流和生命周期，不比较 wasm32 record layout。
17. **+936 DEAD aggregate 已闭合**：保留独立 `vector<DeadChildMotionRenderItem>` 与两个聚合/clear 调用点，不映射到 live render lists。

### P3 — 终极架构重构(长期)
18. **独立确认 `Player+384` 容器语义**；已禁止按 56B Android ABI 元素去替换 `PreparedRenderItem`。
19. **pimpl 所有权拓扑收束**(依赖 P2 全部完成)，目标是直接成员与生命周期，不是 1384B 字节布局。
20. **dtor 手写释放顺序**对齐 §4。
21. **1384B Player 上不存在的本地容器**(_variableAnimators / _type4..8ControllerAnimators / _evalResultList / _mirror*Cache)审计——重点核对它们是否属于 **EmoteObject 引擎(1496B,sub_67E38C)**:引擎在 +256/+336/+416/+576/+656/+736 有 6~7 个控制器 deque、+1384 变量哈希表、+1440 evalResultMap、+1456 evalResultList。这些 emote 专属容器若被误植到 Player,应像 hairScale 一样移到 EmotePlayer 或独立的引擎对象。**先建立"引擎字段 ↔ 本地容器"映射,再决定该删该移。**（hairScale/partsScale/bustScale 已按此方式清理,见 §0 + P0 进度）

---

## §6 需进一步反编译验证的清单(🔬 汇总)

| 项 | 反编译目标 | 目的 |
|---|---|---|
| 4 哈希表内容 | +264/+320/+1184/+1240 访问点 | 确定 key/value 类型,建 4↔6 映射(P2 地基) |
| +864 容器 | sub_7E2344 / sub_7E24AC | 容器类型与 stride |
| +408 someList | sub_6DD144 | 类型 |
| +1296 deque | sub_6F4FD8 (160B/项) | 存什么 |
| +484..548 ttstr 块 | 写入点 + sub_699940(+528) | rootMatrix 字符串语义 |
| +636/+656 | ctor a2 来源 + 读取点 | resourceManager / sourceCache 确认 |
| +676/+716 TJS 对象 | `Player_ctor@0x6CED30` + 0x6CF080 | render descriptor + color 对象拓扑（已纠正，非 RandomGen） |
| +1104 cameraFOV owner | sub_6D9784 + camera-node writer | 收束本地 `_cameraFov` / `_cameraFOV` 双 owner |
| MotionNode 布局 | clang -fdump-record-layouts | root 属性偏移正确性 |
| Node +160/+168 init | xmmword_14D68E0 常量 | bounds 初始化常量 |

---

## §7 已确认对齐项(✅,无需动作)

- vtable: 无(非多态类),完全对齐。
- +120/+128 rootOffset, +144/+148 cameraOffset(float), +456 clampedEvalTime, +472 cameraAngle, +600 damping, +608 noUpdateYet, +613 needsInternalAssignImages, +784/792/800 cameraVelocity, +909 d3dDrawMode, +1092..+1100 bool 字节组, +1120 frameTickCount, +1144 completionType, +1148 maskMode, +1156 packed color(0xFF808080), +1160 outsideFactor(1.5), +1168 speed(1.0), +1176 meshDivisionRatio(1.0), +1032 outline(ttstr), +1052 meshline(ttstr)。
- ctor: +1156/+1160/+1168/+1176/+600/+1092/+1096/+1144/+482 默认值。
