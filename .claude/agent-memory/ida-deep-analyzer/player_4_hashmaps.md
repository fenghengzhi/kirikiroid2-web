---
name: player-4-hashmaps
description: motion::Player (1384B, ctor 0x6CED30) 的 4 个内联 KiriKiri HM 的 key/value 类型、node 布局、insert/lookup 站点、本地镜像认定
metadata:
  type: project
---

motion::Player(1384B, ctor 0x6CED30) 4 个内联 KiriKiri 哈希表权威结论。所有 4 个 HM 的 key 都是 ttstr(UTF-16LE)，hash 用 KiriKiri ttstr-hash(缓存 @key+68：`1025*x ^ (1025*x>>6)` 累加 → `*9` → `^>>11` → `*32769`，0→-1)。HM control struct 48B：base+0=bucket数组ptr, +8=nbkt, +16=before-begin sentinel/list-head, +24=count, +32=loadFactor(1.0f), +40=next_resize。

**HM1 @+264** = `unordered_map<ttstr, EvalCascadeState>`，node=operator new(0x60)=96B：{+0 next, +8 key(joined "scope::label" ttstr,owning), +16 key副本, +24/+32/+40 chainDispatches vector(begin/end/cap, tTJSVariant*), +48 writeVal(double), +56 weight(=1.0 seeded once), +64/+72 RenderItem* vector, +88 cached hash}。find=Player_HM1_find_node@0x6f51bc(读 node+88 hash)。upsert=Player_HM1_upsert_evalCascade@0x6f52ac。insert=Player_HM1_insert_node@0x6f53c8。WRITE: Player_bindParameterValue_writesHM1_HM2@0x6C4668。READ: Player_HM1_cascadeJoinAndLookup@0x6CD39C(读 node+48 writeVal)。语义=scope-join 变量 eval 缓存。本地镜像=_evalCascadeMap(Player.h L1158)。

**HM2 @+320** = `unordered_map<ttstr, double>`，node=operator new(0x20)=32B：{+0 next, +8 key(raw label ttstr,owning), +16 value(double), +24 cached hash}。find=Player_HM2_find_node@0x686b6c。upsert=Player_HM2_upsert_labelToValue@0x686944(返回 &node+16)。insert=Player_HM2_insert_node@0x686a4c。WRITE: bindParameterValue LABEL_132@0x6C4C0C(key=rawLabel,val=a4)。READ: cascade fallback@0x6CD5A8 + evalKey_cascade@0x6CD2F4。语义=eval 结果 label→value。本地镜像=_evalResultValues(L1197)。

**HM3 @+1184** = `unordered_map<ttstr, PerNodeLayerState>`，node=operator new(0x2D0)=720B：{+0 next, +8 key(node-path ttstr,owning), +16.. 0x2B8 payload}。find=Player_HM3_find_node@0x6f28a4。upsert=Player_HM3_upsert_perNodeLayerState@0x6f2674(返回 node+16)。WRITE: resetMotionState loop3@0x6B2E18(key=Player_buildNodePathKey@0x6B5C1C, fill=Player_HM3_initValueFromNode@0x699510)。clear=Player_clearHM3_HM4@0x6B80E4。语义=per-node-path 图层渲染状态快照。本地镜像=_perNodeLayerStateMap(L1180)。key 空间与 Player+24 node-path map 相同。

**HM4 @+1240** = `unordered_map<ttstr, double>`，node 与 HM2 完全相同(0x20, val@+16 raw double, hash@+24)。**复用 HM2 的 find(0x686b6c)/upsert(0x686944)/insert(0x686a4c)**。WRITE: **唯一写点** Player_resetMotionState_clearAndRebuild loop2 @0x6B2D3C：遍历 var-track deque@+1296(160B/item)，key=*(item)=label ttstr，value=*(item+16) raw double。READ: evalKey_cascade@0x6CD2F4(HM4-first) + pruneHM3@0x6b8404。clear=Player_clearHM3_HM4@0x6B80E4(list-head@+1256，释放 node+8 key ttstr，memset bucket@+1240)。语义=变量快照缓存(getVariable cascade 首站)。本地镜像=_variableSnapshotMap(L1190)。

**裁决**：4 个真实镜像=_evalCascadeMap/_evalResultValues/_perNodeLayerStateMap/_variableSnapshotMap。+24 std::map=node-path label map(sub_6DD228 哨兵)。EmoteEngine(1496B, sub_67E38C) 自己的 ttstr->double HM @engine+1440(写于 EmoteEngine_progress@0x530a5c 动画器循环、sub_67581C、sub_67C8A8，用 sub_686944/0x686b6c)，**motion::Player 指针存于 engine+1064**。EmoteEngine_setVariable@0x671228 操作的是 engine+1384/+1440，**不是 Player**。本地 `_timelines/_playingTimelineLabels` 已于 2026-07-19 删除；其余 `_motionsByKey/_layerIdsByName/_layerNamesById/_renderLayerStates/_disabledSelectorTargets/_parameterEntryById` 在 1384B Player 二进制中仍无 HM 对应。
