# Cluster C Audit (re-core) — EmotePlayer 类 + NCB 暴露面

Date: 2026-06-07. Authoritative source: libkrkr2.so. Scope: cpp/plugins/motionplayer/
EmotePlayer.cpp (1080) + EmotePlayer.h (552) + their main.cpp NCB registration blocks.
All binary claims below have a decompile call THIS session.

## 0. 审计结论: ⚠️ 部分偏差（成员集已对齐，3 处委托/字段偏差 + 1 处注册顺序偏差）

上一轮（2026-05-30）的 P0「类身份/成员集缺失」**已被代码演进消除**：
- 本地 `Motion.EmotePlayer` 现注册完整 69 成员 + 2 常量，与二进制 EmotePlayer_ncb_registerMembers
  @0x67FAC8 **成员名 + 注册顺序 100% 1:1**（含正确删除不存在的 activateSelectorTarget）。
- 本地 `D3DEmotePlayer` 现注册 54 成员 + 4 常量，与二进制 D3DEmotePlayer_ncb_registerMembers
  @0x52E504 **成员集合 100% 一致（0 多 0 缺）**。

剩余偏差均为「成员存在但回调/字段映射错」或「注册顺序错」，可在现数据流上局部修复，无需架构重构。

---

## 1. 反编译证据账本（本 session 实际 decompile 调用）

| 地址 | 函数 | 用途 |
|------|------|------|
| 0x67FAC8 | EmotePlayer_ncb_registerMembers | 枚举 69+2 权威成员名（ncb_addMember key）|
| 0x52E504 | D3DEmotePlayer_ncb_registerMembers | 枚举 54+4 权威成员名 |
| 0x68629C | EmotePlayerNativeInstance_create | 24B shell: +0 vtable / +8=0 / +16=0 |
| 0x6862D0 | EmotePlayerNativeInstance_destroy | gate `+8 && !+16` → sub_67F4B8 + delete |
| 0x686148 | EmotePlayer_NCB_classInit | new(0xB0); native ctor=0x68629C; 注册 finalize→0x6862C8 noop |
| 0x67F4B8 | EmoteEngine_dtor | +1064 处 Player_dtor + operator delete（EmoteEngine 拥有 Player）|
| 0x681E94 | EmotePlayer frameLastTime/lastTime getter | 读 Player+1128 RAW（无 ms 转换）|
| 0x681EA0 | EmotePlayer frameLoopTime/loopTime getter | 读 Player+1136 RAW（无 ms 转换）|
| 0x6D9420 | Player_getLastTime (Motion.Player) | 读 Player+1128，**带** >0?*1000/60 转换 |
| 0x6D9448 | Player_getLoopTime (Motion.Player) | 读 Player+1136，**带**转换 |
| 0x673F98 | Player_isAnimating | EmotePlayer/D3DEmotePlayer `animating` 真实回调 |
| 0x530A38 | D3DEmotePlayer_getAnimating | → Player_isAnimating(*(*(a1+24)+8)) |
| 0x681EF8 | EmotePlayer setCameraOffset | 写 Player+144/+148 float |

---

## 2. NCB 成员对照表 — Motion.EmotePlayer（0x67FAC8）

权威序列（ncb_addMember key 字符串，源码顺序）：2 常量 +
progress, frameProgress, draw, initPhysics, startWind, stopWind, play, clear, getVariable,
contains, serialize, unserialize, pass, setVariable, setCoord, setScale, setRotate, setColor,
setOuterForce, completionType, chara, motion, motionKey, project, maskMode, meshDivisionRatio,
outline, priorDraw, frameLastTime, frameLoopTime, lastTime, loopTime, bounds,
processedMeshVerticesNum, setDrawAffineTranslateMatrix, getCameraOffset, setCameraOffset,
modifyRoot, setHairScale, setPartsScale, setBustScale, hairScale, bustScale, partsScale,
debugPrint, queuing, directEdit, selectorEnabled, variableKeys, animating, setMirror, skip,
playTimeline, stopTimeline, getTimelinePlaying, setTimelineBlendRatio, fadeInTimeline,
fadeOutTimeline, getTimelineBlendRatio, getVariableRange, getVariableFrameList,
getMainTimelineLabelList, getDiffTimelineLabelList, getLoopTimeline, getTimelineTotalFrameCount,
getPlayingTimelineInfoList, isSelectorTarget, deactivateSelectorTarget, getCommandList.

| 检查项 | 二进制 | 本地 (main.cpp #1-69) | 状态 |
|--------|--------|----------------------|------|
| 成员集合（69）| 上列 69 名 | 完全相同 | ✅ |
| 注册顺序 | 源码顺序 | #1-69 完全相同 | ✅ |
| 2 常量 TimelinePlayFlag* | 在成员前注册 | main.cpp:474-477 同位置 | ✅ |
| activateSelectorTarget | **不存在** | 已删除（注释 2026-06-05）| ✅ |
| finalize 成员 | classInit 注册→0x6862C8 noop | ncbind 框架隐式提供 | ⚠️ 框架等价（见 §6）|

### 偏差（EmotePlayer 暴露面，字段/委托错）

| # | 成员 | 二进制回调行为 | 本地实现 | 状态 |
|---|------|---------------|---------|------|
| 29 | frameLastTime | sub_681E94 = Player+1128 RAW (_cachedTotalFrames) | `getFrameLastTime()→_frameLastTime`(+1138) | ❌ 字段错 |
| 31 | lastTime | sub_681E94 = Player+1128 RAW（**同 frameLastTime**）| `getLastTime()→_loopTime(+1136), 带 *1000/60 转换` | ❌ 字段+转换错 |
| 30 | frameLoopTime | sub_681EA0 = Player+1136 RAW (_loopTime) | `getFrameLoopTime()→getLoopTime()→_loopTime` | ✅ |
| 32 | loopTime | sub_681EA0 = Player+1136 RAW（**同 frameLoopTime**）| `getLoopTime()→_loopTime` (line168 无转换) | ✅ |
| 50 | animating | Player_isAnimating @0x673F98（3 bucket + 5 哈希表扫描）| `getAnimating()→getAllplaying()`(0x6CCE34) | ❌ 委托函数错 |

注：二进制设计里 frameLastTime≡lastTime（同回调 sub_681E94 读 +1128），frameLoopTime≡loopTime
（同回调 sub_681EA0 读 +1136）。本地把 frameLastTime/lastTime 拆成读两个不同字段且其一带单位
转换 —— 与二进制「两个名共用一个无转换 getter」不符。

variableKeys(#49): 二进制 = EmotePlayer_getVariableKeys_e1208（读 EmoteEngine+1208 20B 局部容器）；
本地 `player().getVariableKeys()`（读 _activeMotion->variableLabels）。头注释已标 open，数据源不同。⚠️ 已知开口。

modifyRoot(#38): 二进制 sub_681F0C 置 Player+1064→+200→+1584=1；本地 STUB_WARN。⚠️ 已标 open。
initPhysics(#4): **2026-07-19 纠正并关闭。** 注册点 `0x67FCA4` 的字面名称直接绑定
`EmoteEngine_applyMetadata_buildControllers@0x67D4D0`；它接收 metadata variant，
不是独立物理初始化。Web 已调用同一 raw metadata builder。
activateSelectorTarget: 不在二进制（正确未暴露）；本地残留死 C++ STUB，无 NCB 绑定 — 无害。

---

## 3. NCB 成员对照表 — D3DEmotePlayer（0x52E504, DrawDeviceD3D.dll）

权威序列（源码顺序）：4 常量(MaskModeStencil/MaskModeAlpha/TimelinePlayFlagParallel/
TimelinePlayFlagDifference) +
module(RO), clear→create, load, clone, show, hide, visible, smoothing, meshDivisionRatio,
queing, hairScale, partsScale, bustScale, assignState, setCoord, setScale, getScale, setRot,
getRot, setColor, getColor, countVariables, getVariableLabelAt, countVariableFrameAt,
getVariableFrameLabelAt, getVariableFrameValueAt, setVariable, getVariable, startWind, stopWind,
countMainTimelines, getMainTimelineLabelAt, countDiffTimelines, getDiffTimelineLabelAt,
countPlayingTimelines, getPlayingTimelineLabelAt, getPlayingTimelineFlagsAt, isLoopTimeline,
getTimelineTotalFrameCount, playTimeline, isTimelinePlaying, stopTimeline,
setTimelineBlendRatio→setTimeline, getTimelineBlendRatio, fadeInTimeline, fadeOutTimeline,
animating, skip, pass→addPlayCallback, progress, modified(RO)→getPlayCallback, setOuterForce,
getOuterForce, contains.

| 检查项 | 二进制 | 本地 | 状态 |
|--------|--------|------|------|
| 成员集合（54）| 上列 54 名 | 0 多 0 缺，完全一致 | ✅ |
| 4 常量 | D3DEmotePlayer 类上 | main.cpp:883-888 在 D3DEmotePlayer 上 | ✅ |
| NAME/callback mismatch（clear→create, setTimelineBlendRatio→setTimeline, pass→addPlayCallback, modified→getPlayCallback, queing→byte flag, bustScale→+1200）| — | 全部已 1:1 复刻（NCB_METHOD_DETAIL/正确 getter）| ✅ |
| **注册顺序** | property/method 交错（module,clear,load,clone,show,hide,visible,...）| property 全提前、method 全后置 | ⚠️ 顺序偏差（见 §4-A）|

`animating`(D3DEmotePlayer) 同样应是 Player_isAnimating（sub_530A38→Player_isAnimating），本地
`getAnimating()→getAllplaying()` —— **与 EmotePlayer #50 同一 ❌**（委托函数错）。

---

## 4. 架构性 / 顺序性偏差

### 4-A（⚠️ 非架构，顺序）D3DEmotePlayer NCB 注册顺序与二进制不一致
二进制 0x52E504 按「module, clear, load, clone, show, hide, visible, smoothing, meshDivisionRatio,
queing, hairScale, partsScale, bustScale, assignState, setCoord, ...」把 property 与 method 交错
注册。本地 main.cpp:870-1022 把全部 property（module..animating）集中在前、全部 method 集中在后。
TJS 成员按名 hashmap 查找，**运行时不可观察**；但 CLAUDE.md「源码结构/调用链」维度要求 1:1，
应按二进制源码顺序重排。**EmotePlayer(0x67FAC8) 无此问题**（本地 #1-69 已严格按二进制源码顺序）。

不计为 ❌（可直接重排，无数据流改动），列为 ⚠️ 待办。

### 无真正的架构重构项（🔧 = 无）
对象链拓扑（24B shell → EmoteObject 40B → EmoteEngine 1496B → Player 1384B）本地已用裸指针 +
手动 new/delete 复刻；EmoteEngine_dtor @0x67F4B8 在 +1064 处 Player_dtor+delete 与本地
EmoteEngine 持 `Player* _player` 手动 delete 一致。生命周期六维无架构断裂。

---

## 5. 对象生命周期 / vtable 维度（✅）

| id | 二进制 @addr | 本地 | 状态 |
|----|-------------|------|------|
| create | 0x68629C: new(0x18); +0=vtable,+8=0,+16=0 | EmotePlayer.h 头注释一致；ncbind 24B native | ✅ |
| destroy | 0x6862D0: gate `+8 && !+16`→sub_67F4B8+delete | EmotePlayer::~ delete _primaryObj（经 EmoteObject 链）；sticky(+16) 由 ncbind _sticky 处理 | ✅（destroy gate 语义保留）|
| classInit | 0x686148: new(0xB0), native ctor slot=0x68629C, 注册 finalize→noop | NCB_REGISTER_SUBCLASS_DELAY 宏展开 + ctor | ⚠️ finalize 框架等价 |
| EmoteEngine_dtor | 0x67F4B8: 长销毁链, +1064 Player_dtor+delete | EmoteEngine 裸指针 Player 手动 delete | ✅ |

---

## 6. 平台边界 / 框架等价标注（审计跳过，列出供 reviewer 核实）

- **finalize 成员**：二进制 classInit 显式注册 `finalize`→sub_6862C8(`return 0` noop)。本地未显式
  注册 finalize 成员，由 ncbind 框架统一提供 native 实例的 finalize/析构语义。这是 ncbind 框架
  等价，非偏差也非合规缺失 —— 但代码中**未加** `// PLATFORM_BOUNDARY:` 注释，建议补注明
  「finalize 由 ncbind 框架提供，对应 binary 0x6862C8 noop」以免后续 session 误判为缺失成员。
- **EmotePlayer 对象链经 EmoteObject 中间层**：二进制 EmotePlayer native instance +8 直接是
  EmoteEngine（无 EmoteObject 中间层），D3DEmotePlayer 才有 EmoteObject。本地 EmotePlayer 复用
  EmoteObject 链以 reach Player。EmotePlayer.h:268-271 已注明此 ABI 偏移差异为平台必然。
  ✅ 已标注，合规。

---

## 7. 子函数对齐状态

- EmotePlayer_ncb_registerMembers (0x67FAC8): ✅ 枚举完成，本地 1:1。
- D3DEmotePlayer_ncb_registerMembers (0x52E504): ✅ 枚举完成，集合 1:1，顺序待重排。
- EmotePlayerNativeInstance create/destroy (0x68629C/0x6862D0): ✅ 对齐。
- EmotePlayer_NCB_classInit (0x686148): ✅（finalize 框架等价）。
- EmoteEngine_dtor (0x67F4B8): ✅ 销毁链与本地裸指针拓扑一致。
- **Player_isAnimating (0x673F98): ❌ 未移植**。本地无 Player::isAnimating；animating 暴露面
  错绑到 getAllplaying(0x6CCE34)。建议先递归移植 Player_isAnimating（3 controller bucket +
  5 内联哈希表扫描），再把两类的 `animating` 改委托它。属 Player 类（簇 A/B）子函数缺口。
- sub_681E94 / sub_681EA0（time getter）: ✅ 已确认读 Player+1128/+1136 RAW。
- EmotePlayer_getVariableKeys_e1208（variableKeys getter，读 engine+1208）: ❓ 未深入，本地
  已标 open（数据源不同）。
- `sub_681F0C`（modifyRoot）是本轮历史 open；`sub_67D4D0` 已于 2026-07-19
  纠正为 `initPhysics` 字面入口所绑定的 metadata/controller builder，并已接入本地。

---

## 8. 修复建议（按优先级；不在本审计内执行）

1. **(P1 ❌) EmotePlayer frameLastTime/lastTime 字段错**：两者二进制都读 Player+1128 RAW
   (_cachedTotalFrames)，无 ms 转换。本地 EmotePlayer 应让 #29 #31 都委托读 _cachedTotalFrames
   （Player+1128）原值，删除 getLastTime 的 *1000/60 转换 与 #29 的 _frameLastTime 读取。
   注意 Motion.Player 的 lastTime/loopTime（带转换 6D9420/6D9448）是另一套，勿混。
   EmotePlayer.h:189-192 / Player.h:161-162,230-232。
2. **(P1 ❌) animating 委托错（两类）**：EmotePlayer #50 + D3DEmotePlayer::getAnimating 都应
   委托 Player::isAnimating（待移植 0x673F98），非 getAllplaying。EmotePlayer.h:243 /
   EmotePlayer.cpp:100-102。
3. **(P2 ⚠️) D3DEmotePlayer NCB 注册顺序**：按 0x52E504 源码顺序重排 main.cpp:870-1022
   （property/method 交错），与 EmotePlayer 块的严格顺序看齐。
4. **(P3 ⚠️) finalize 平台边界注释**：在 EmotePlayer NCB 块补 `// PLATFORM_BOUNDARY: finalize
   由 ncbind 框架提供，对应 binary classInit 0x6862C8 noop`。
5. 历史 open（variableKeys 数据源 / modifyRoot +1584 flag）需结合后续主审计；
   `initPhysics` 已证实不是独立物理构建缺口，
   头注释已标。

## 9. IDB 改动（本 session）
- set_comments @0x681E94（frameLastTime≡lastTime 读 +1128 RAW，对比 6D9420 带转换）
- set_comments @0x681EA0（frameLoopTime≡loopTime 读 +1136 RAW，对比 6D9448 带转换）
- set_comments @0x673F98（Player_isAnimating 绑 EmotePlayer/D3DEmotePlayer animating，≠getAllplaying）
- idb_save 完成。
