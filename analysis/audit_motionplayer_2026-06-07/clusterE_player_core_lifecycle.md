# CLUSTER E — Player core / lifecycle 对齐审计 (2026-06-07)

> **2026-07-18 correction:** 初版曾把 +960 写作 variableKeys、+968 写作
> primary chara。NCB 表复核后的正确映射是 +960 chara、+968
> stealthChara，+976/+984 为 motion/stealthMotion；四者是源码层 `ttstr`
> 值 owner，+1099 是 playing。当前实现已补齐两个 pending owner 与 flush；
> `variableKeys@0x6D139C` 也已迁回 +1296 var-track deque，以下表格同步采用新结论。

> 权威: libkrkr2.so IDB。本轮全部结论基于本对话 fresh decompile(非 2026-05-30 旧符号)。
> 范围: PlayerCore.cpp / Player.h / PlayerInternal.h / PlayerResource.cpp。
> 协议: decompile → 伪代码 → 本地对照 → 六维裁决。只读审计;IDB 已 idb_save。

## 审计结论: ⚠️ 部分偏差(局部 accessor 副作用 + 注释错标),无架构级重构需求

自 2026-05-30 审计以来,本地大量原始偏差已修复(pixelateDivision 改实例字段、completionType→+1144、
_preview→+1092、angleDeg/Rad 接反纠正、onAction/onSync/onGroundCorrection 改 no-op method、
defaultSyncActive/defaultTransformOrder 补齐、92-member NCB 集对齐)。2026-05-30 列出的若干"严重
DEVIATION"实为 **IDB off-by-one 误命名**导致的误判,本轮 fresh decompile 已证伪并纠正。

残留偏差均为局部:(a) chara/stealthChara setter 副作用未完全复刻,(b) 三处注释错标偏移/语义。

---

## 1. NCB 92-member 集 (main.cpp:137 NCB_REGISTER_CLASS(Player))
✅ **已对齐**。本地注释逐条标注二进制 descriptor build site,RO/RW 区分正确,注册顺序对齐
(play#70/progress#71/clear#72/stop#73 收尾)。2026-05-30 的"138 vs 92"偏差已消除:24 timeline-query
+ selector 方法判为 D3DEmotePlayer-only 不再注册;17 host 方法去 NCB 但保留 C++ body。
- `chara`→getChara/setChara, `stealthChara`→getStealthChara/setStealthChara(分别独立 setter,见 §3)。
- `loopTime`/`lastTime`/`frameLoopTime` 都绑 getLastTime/getLoopTime(scalar +1136),`variableKeys`→getVariableKeys。

## 2. ctor 0x6CED30 / 对象生命周期
✅ 默认值/init 序基本对齐。fresh decompile 确认:
- 4 HM(+264/+320/+1184/+1240)= std::unordered_map(std_Prime_rehash_policy_M_next_bkt + load 1.0f);
  本地 4 map(_evalCascadeMap/_evalResultValues/_perNodeLayerStateMap/_variableSnapshotMap)**选型对齐**。
- +912=100 ✅ _pixelateDivision、+1092=0(preview)✅ _preview、+1156=0xFF808080 ✅、+600/+1168/+1176/+1160
  默认值 ✅、bounds=±DBL_MAX ✅、ctor push 1 root node ✅。
- ❌ **+992 = 第3份 RM dispatch 拷贝**(0x6cef28),本地 Player.h:1622 `_tjsRandomGenerator // player+992`
  注释**错标** — RandomGen(sub_9C8440)实在 **+676**(0x6cf024)。注释级修复(P2)。
- ⚠️ +716 第2个 TJS 对象(sub_9C8440 + 设 "color" param)本地无对应字段(MISSING,P2,inert)。

dtor: 本地 RAII 逆序析构 vs 二进制手写释放链。功能等价(tTJSVariant 值语义),释放顺序不保证一致。
⚠️ 架构级容忍偏差(P3),非本轮 actionable。

## 3. accessor 深审(逐条 fresh decompile)

| accessor | 二进制地址 | 二进制行为 | 本地 | 状态 |
|---|---|---|---|---|
| chara setter | **0x6C0E9C** | dedup(mode0)写 +960 chara 并同步写 +968 stealthChara，清 +976/+984/+1099，flush +776 | `setCharaSlotLike_0x6B29C0(v,false)` + pending flush | ✅ |
| stealthChara setter | **0x6D94B0** | `if(+968)`: dedup(mode16，只写 +968)+flush +776；else AddRef+存 +776 | 同一 mode16 helper 与 pending owner/flush | ✅ |
| dedup helper | 0x6B29C0 | a2&0x10→仅 +968，否则 +960/+968；wcscmp dedup；清 +976/+984/+1099 | `setCharaSlotLike_0x6B29C0` | ✅ |
| tickCount set | 0x6D96C0 | +1120=fmax(v*60/1000,0);+480 WORD=257;+456=min(+1120,+1128) | setTickCount 完全复刻(_queuing+_firstFrame=true) | ✅ |
| tickCount get | 0x6D96A0 | +1120*1000/60 无 guard | getTickCount ✅ | ✅ |
| loopTime/lastTime get | 0x6D9448 | +1136>0?*1000/60:+1136 | getLastTime ✅ | ✅ |
| variableKeys get | 0x6D139C | walk var-track deque@+1296 → 每次新建 TJS Array 并追加 item+0 cascadeKey | 直接遍历 `_variableLabelScopes`，每次 `makeArray` | ✅ |
| angleDeg set/get | 0x6C0F84 / 0x6C1780 | deg-direct / raw→deg | setAngleDeg/getAngleDeg ✅ | ✅(directEdit 路径缺 initEmoteMotion(2),平台 gap) |
| angleRad set/get | 0x6CD0EC / 0x6CD0C0 | rad*57.29→deg / deg*0.0174→rad | setAngleRad/getAngleRad ✅ | ✅ |

### 偏差详情
- **2026-07-18 已闭合**：chara/stealthChara 两个 setter 已经共享 dedup helper 并恢复
  +960/+968 双槽、+776 pending 与 +976/+984/+1099 清理；`variableKeys` 已删除不存在的
  Player 缓存，直接从 +1296 deque 的 cascadeKey 每次构造新 Array。

## 4. PlayerResource.cpp
✅ 基本对齐。unload/unloadAll/isExistMotion/findMotion/releaseLayerId + dispatchRequireLayerId/
dispatchReleaseLayerId 走 RM dispatch FuncCall(L"requireLayerId"/L"releaseLayerId"),复刻二进制
0x6B4A6C/0x6C4E28/0x6DE738/0x6B56F8 的 dispatch 路径(非 native shortcut)✅。unloadAll 经 C-1 改走
RM::unloadAll(0x6A8CF8,清 HashMap A)而非 inherited clearCache,注释证据充分。
⚠️ `_timelines/_playingTimelineLabels` 已于 2026-07-19 按 Player/EmoteEngine NCB
owner 证据删除；`_motionsByKey` 仍是 port-invented decoded snapshot 容器，unload
对它的操作仍属待迁移的生命周期偏差。

## 5. 子函数对齐状态
- ✅ Player_setCharaOrKeySlot_dedup 0x6B29C0(已命名+注释)
- ✅ Player_setChara 0x6C0E9C / Player_setStealthChara 0x6D94B0(IDB 已纠正 off-by-one)
- ✅ Player_getLoopTime 0x6D9448 / Player_getVariableKeys 0x6D139C(IDB 已纠正)
- ✅ Player_setAngleDeg 0x6C0F84 / setAngleRad 0x6CD0EC / getAngleDeg 0x6C1780 / getAngleRad 0x6CD0C0
- ❓ Player_initEmoteMotion(angle directEdit 路径调用,本地未接)— 未深审(平台 gap 标注)

## 6. 平台边界标注
- directEdit angle 路径省略 initEmoteMotion(2)(Player.h:278 注释,emote 引擎路径未完整移植)。
- pimpl 拆分 / dtor 释放顺序 / renderList 56B 裸数组 → PreparedRenderItem 膨胀 = 已知 P3 容忍偏差。

## 7. 修复建议(按优先级)
1. **P2 注释**: Player.h:1622 `_tjsRandomGenerator // player+992` → `// player+676`(RandomGen 实在 +676;
   +992 是第3份 RM dispatch 拷贝)。PlayerCore.cpp setChara 注释块(L535-574)仍引"0x6D94B0=chara setter
   /sub_6B29C0(16)"应改为"chara setter=0x6C0E9C(mode0);0x6D94B0 是 stealthChara setter"。
2. ~~setStealthChara/chara 双槽与 pending owner~~：2026-07-18 已闭合。
3. ~~getVariableKeys 改走 `_variableLabelScopes` cascadeKey~~：2026-07-18 已闭合；
   每次返回独立 Array，且 NCB 保持只读。

## 8. IDB 改善(已 idb_save)
本轮无新增重命名(0x6C0E9C/0x6D94B0/0x6B29C0/0x6D9448/0x6D139C/0x6C0F84/0x6CD0EC/0x6C1780/0x6CD0C0
均已在前序 session 正确命名+注释)。复核确认符号正确,idb_save 持久化。
