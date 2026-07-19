# 簇 G — 帧进度引擎 frameProgress 对齐审计 (2026-06-07)

> 范围: cpp/plugins/motionplayer/PlayerFrameProgress.cpp (2747 行, 全文件逐行)
> 权威: libkrkr2.so. 本次反编译: 0x6C106C(progress_inner 全函数, 入口+LABEL_48)、
>       0x6B6ADC(advanceRootAndNodes 4-stream). disasm: 0x6C1080 入口序言.
> IDB: 重命名 0x6C106C 3 局部变量(progressFlagsGate480/deltaTime592/deltaTime592_firstFrame);
>      2 注释(0x6c10a4 emoteMode gap, 0x6c1108 firstFrame deltaTime bug); idb_save 完成.
> 注: 2026-05-30 起点 ledger 判 SEVERE DIVERGENCE, 此后已彻底重构(commit 8883587→当前 HEAD),
>     入口拓扑 1:1 复刻 progress_inner. 本审计基于当前 HEAD, 起点 ledger 的 G1-G18 表已过时.

## 审计结论: ⚠️ 部分偏差

frameProgress 入口拓扑 + LABEL_48 forward/reverse loop-wrap 已 1:1 复刻 progress_inner@0x6C106C
(算术、分支、loop-wrap modulo do-while 全部逐行对齐, 含 0x6C14C4/0x6C1454 双向 wrap)。
advanceRootAndNodes/rewindRootAndNodes 的 4-stream 分解(layer/root/var-track/node)结构忠实。
HM3/HM4 restore-init 链与 var-track 步进忠实(已有 memory 链覆盖)。
残留偏差均为**局部缺口**(入口 emoteMode 门控调用缺失、firstFrame 块 deltaTime 取陈旧值、
尾部 port-invented _allplaying/_syncActive 覆写), 可在现有数据流上直接修复, 非架构重构。

---

## 反编译伪代码摘要 (progress_inner @0x6C106C, 权威)

```
ENTRY (0x6C1080):
  speedMul = +1168; emoteMode = +482; +1152=0; +483(motionCompleted)=0;
  +592(deltaTime) = speedMul*dt;                       // 入口一次性设, 全函数读这个值
  if(emoteMode) initEmoteMotion(a1, 2);                // +482 门控
  preProgressDirtyNodes(a1);                            // 0x6B6878
  if(+376 activeTimeline){ ... node-deque walk / firstFrame seed +1120=+456=AT+40 ; return }
  if(+481==0 && +1099==0){ if(renderList +384==+392) return; else node-deque walk; return }
  if(+1098 syncWaiting || +483) return;
  if(+481 firstFrame){
     v8 = +592;  +481=0;                                // ← 读入口设的 speedMul*dt
     if(v8<0 && +1120==0){ +456=+1128; +1120=+1128; }   // (b) reverse-from-end seed
     if(+609 reverseSeekFlag){ +609=0;
        if(v8>=0){ save+456; +456=0; reseek; gate sync/complete; +456=save; advanceRoot; }
        else { if(+1128>+1120){ save; +456=+1128; reseek; gate; +456=save; rewind; } }  // else goto LABEL_48
     } else { reseek; gate sync; }
     if(+483) return;
     // fall-through to LABEL_48 (NOT return)
  }
LABEL_48:
  v23=+480; v24=+592;
  if(!+480){ +1120+=+592; +456=min(+1120,+1128); }      // gated advance+clamp
  if(v24>=0){  // FORWARD
     if(+1128<=+1120){ +456=+1128;
        if(+1136>=0){ advanceRoot; gate; +456=+1136; reseek; gate; wrap LABEL_22/23; advanceRoot; }  // LOOP
        else { +1099=0; if(!gate) return advanceRoot; }                                              // STOP
     } else if(!gate) return advanceRoot;                                                            // not-at-end
  } else {     // REVERSE
     if(+1120>=0 && +1136<=+1120){ LABEL_57: if(!gate) return rewind; }
     else if(+1136<0){ +456=0; +1099=0; +1120=0; LABEL_57 }
     else { +456=+1136; rewind; gate; +456=+1128; reseek; gate; wrap LABEL_27/28; rewind; }          // rev loop-wrap
  }
  // 所有终端均 return; NO trailing +1099/_syncActive write
```

## 逐项对比 — frameProgress

| 检查项 | 二进制行为 | 本地实现 | 状态 |
|--------|-----------|---------|------|
| 入口 `+483=0` motionCompleted | 0x6C108C 无条件清 | line 2157 `_motionCompleted=false` | ✅ |
| 入口 `+1152=0` DWORD | 0x6C1088 清 | 本地 Player 无 +1152 字段(EmoteEngine._windFreqY) | 🔶 合理缺口 |
| 入口 `+592=speedMul*dt` | 0x6C1094 **在 firstFrame 块前** | line 2312 **推迟到 firstFrame 块后** | ❌ 见偏差#2 |
| 入口 `if(+482)initEmoteMotion(2)` | 0x6C10A4 emoteMode 门控调用 | **完全缺失** | ❌ 见偏差#1 |
| preProgressDirtyNodes | 0x6C10AC | line 2175 `preProgressDirtyNodesLike_0x6B6878()` | ✅ |
| +376 activeTimeline 分支 | 0x6C10B4 非零走专路 | `_defaultParameterEntryPtr` 承接 +376，按 entry.value seed/advance/rewind/equal-refresh 后 return | ✅（2026-07-13 纠正旧误判） |
| renderList 空检查 +384==+392 | 0x6C1278 | line 2196 `_nodes.empty()` 近似 | 🔶 平台边界(无 1:1 容器) |
| 短路 +1098/+483 | 0x6C10FC/0x6C1100 | line 2208/2211 | ✅ |
| firstFrame (b) reverse-end seed | 0x6C1120 `v8<0 && +1120==0` | line 2245 | ✅ 结构对齐(读值见#2) |
| firstFrame (a) reverseSeek forward | 0x6C13AC..0x6C13D4 | line 2253-2261 | ✅ 结构对齐(读值见#2) |
| firstFrame (a) reverseSeek reverse | 0x6C1144..0x6C117C (gate +1128>+1120) | line 2262-2274 | ✅ |
| firstFrame plain reseek | 0x6C131C | line 2276-2279 | ✅ |
| firstFrame 块末 fall-through→LABEL_48 | 二进制 fall-through | line 2296 `return`(注: gated no-op 等价) | 🔶 见偏差#4 |
| LABEL_48 gated clamp | 0x6C1340 `+1120+=+592; +456=min` | line 2312-2315 advance + 2426-2432 clamp(拆两段) | ✅ 重采样等价 |
| LABEL_48 FORWARD at-end LOOP wrap | 0x6C13F0..0x6C14CC do-while | line 2448-2485 | ✅ 逐行 |
| LABEL_48 FORWARD STOP `+1099=0` | 0x6C13F4 | line 2488 | ✅ |
| LABEL_48 FORWARD not-at-end | 0x6C13A4 `if(!gate)advanceRoot` | line 2493-2494 reseekNodes 标志 | ✅ |
| LABEL_48 REVERSE 3 分支 | 0x6C1360..0x6C14A8 | line 2497-2543 | ✅ 逐行(含 rev wrap 0x6C1454) |
| 尾部 `_allplaying=!list.empty()` | 二进制无此写 | line 2592 **port-invented** | ❌ 见偏差#3 |
| 尾部 `_syncActive=...` | 二进制无此字段 | line 2593 **port-invented** | ❌ 见偏差#3 |

## 偏差详情

### ❌ 偏差#1: 入口缺失 emoteMode 门控的 initEmoteMotion(2)
二进制入口 0x6C1098: `if(+482 _directEdit) Player_initEmoteMotion(a1, 2)` — 在 preProgressDirtyNodes
之前。本地 frameProgress 入口(line 2143-2175)完全无此分支。+482=`_directEdit`(已映射, PlayerCore.cpp:240)。
- 性质: directEdit/emote 直接编辑模式路径。logo/非 emote: `_directEdit=false` → 该调用 inert。
- initEmoteMotion(2) 本身在本地是 TODO/省略(PlayerCore.cpp:282 "port omits for now")。
- 修复: 在 line 2175 前加 `if(_directEdit && _engineBack){ /* initEmoteMotion(2) */ }`。
  但 initEmoteMotion(2) 无 port 目标 → 当前只能加门控调用占位。建议保持缺口但**在入口注释标注**
  (现仅有 0x6C10A4 IDB 注释, 本地代码无对应标注)。

### ❌ 偏差#2: firstFrame 块读陈旧 _deltaTime (而非本帧 speedMul*dt)
二进制 0x6C1108 `v8 = +592`, 而 +592 已在入口 0x6C1094 设为 `speedMul*a2`(本帧)。
本地 line 2241 `const double deltaTime = _deltaTime;` — 但 `_deltaTime` 本帧的赋值
(`_deltaTime = _speedMul*actualDelta`)在 **line 2312**, 即 firstFrame 块**之后**。
故本地 firstFrame 块读到的是**上一帧残留的 _deltaTime**(或首帧的 0.0)。
- 影响: (b) reverse-from-end seed 的 `deltaTime<0` 判定; (a) reverseSeekFlag 的 forward/reverse
  方向选择。两者都依赖本帧 dt 的符号。
- 性质: 对 logo 单向正向播放(dt 恒正, 上帧也正)inert; 对反向播放/变速/首帧场景偏离。
- 修复(局部, 不需重构): 把 `_deltaTime = _speedMul * actualDelta;` 上移到 frameProgress **入口**
  (line 2157 `_motionCompleted=false` 之后, 对齐二进制 0x6C1094 在 firstFrame 块前的位置),
  然后 firstFrame 块 line 2241 与 LABEL_48 line 2312/2417 都读这个已设好的本帧值。需删除 line 2312
  的重复赋值(改为只在 `!_queuing` 时 advance +1120, 不再重设 _deltaTime)。

### ✅ 偏差#3 已关闭（2026-07-19）

`Player_ncb_registerMembers@0x6D69C8` 证明 Motion.Player 无 timeline API；
`0x672F70/0x67C2A0/0x673F98` 属于 EmoteEngine。frameProgress 尾部和 firstFrame
块内基于 `_playingTimelineLabels` 的 `_allplaying/_syncActive` 伪覆写已删除，连同
Player `_timelines/_playingTimelineLabels` 及 decoded timeline 状态机一并移除。
+1099 现只由 Android 对应的 play/init/stop 路径维护。

### 🔶 偏差#4 (非偏差, 已论证): firstFrame 块末 `return` vs 二进制 fall-through
二进制 firstFrame 块结束 fall-through 到 LABEL_48(0x6C132C→0x6C1330); 本地 line 2296 `return`。
本地注释(line 2280-2296)论证: firstFrame 帧 +480(_queuing)=1(STRH 0x0101 同置 +480/+481),
故 LABEL_48 gated clamp 跳过, forward not-at-end 落 `else if(!gate)` gate=1 → return result,
净效果 = 仅 reseek 后返回。**前提依赖 +481 唯一 writer 同时置 +480=1**。
- 该前提已由 memory(frameprogress-entry-topology-refactor)+ binary-alignment-auditor 前次复核 +
  logo render-events byte-identical 验证(5287 events)交叉确认。本审计未独立再验 STRH 唯一性
  (避免在已交叉核实结论上无限深挖), 标 🔶 接受, 但**留作残留前提**: 若发现单独置 +481 不置 +480
  的 writer, 此 `return` 即为 bug。

## 架构性偏差: 无

当前 frameProgress 已是 progress_inner 的逐行复刻; 不存在"缺少二进制中间变量/计算步骤"或
"多出二进制不存在的步骤"的数据流级偏差。偏差#1-#3 均为局部分支/赋值时机/多余写, 在现有数据流上
直接修复即可, 不触发 🔧 重构判定。(起点 2026-05-30 ledger 的 SEVERE/重构判定针对的是已被替换的
旧 STL 时间线状态机, 对当前 HEAD 不再适用。)

## 子函数对齐状态 (树状递归)

- ✅ `advanceRootAndNodes_0x6B6ADC`(line 2110) — 4-stream 分解(layer①→root②→var-track③→node④)
  顺序与 0x6B6ADC 一致, 逐 stream 对应反编译确认。
- ✅ `rewindRootAndNodes_0x6B9A3C`(line 2129) — 同边界, var-track 走 reverse stepper, layer/root
  双向自选向后。
- ✅ `seekLayerEventStreamLike_0x6B6ADC`(line 807) — layer① 双向 cursor + type==1 gate(+1093 align/sync
  + ungated action), 对齐 0x6B6B80/0x6B6DD8/0x6B9AE8。
- ✅ `seekRootContentStreamLike_0x6B6ADC`(line 958) — root② 双向 +616 content snapshot, 无事件 gate,
  对齐 0x6B6F48 + reverse 0x6B9E84。
- ✅ `advanceVariableTracksLike/rewindVariableTracksLike/reseedVariableTracksLike`(line 1071/1173/1274)
  — var-track③ active/other 双槽步进 + merge, 对齐 0x6B7124/0x6B9FCC/0x6B8F30。Inert(无 fixture
  暴露 populated variable list)。
- ✅ `reseekTimelineCursors`(line 1405) — 0x6B86C8 非增量全 re-seek(layer 粗扫双增量 int 截断 +
  root 单步 + var-track reseed + node init + TAIL pruneHM3/HM1-aux), 对齐。
- ✅ `pruneHM3ByNodeIdentityLike_0x6B826C`/`hm3InitValueFromNodeLike_0x699510`/
  `hm3RestoreValueToNodeLike_0x6997F0`(line 1690/1905/1989) — HM3/HM4 restore-init 链, 已有
  memory(hm3-loop2-restore)覆盖; common 标量 + 3 条 A 类 + type-4 粒子链已通, slot+744≡slot+424
  alias 已钉死。残留: srcDispatch(V+44) 平台边界。
- ✅ `interpolateVarTrackValuesLike_0x6BBE20`(line 1772) — 0x6BBE20 HOLD/LERP + bezier easing
  (0x69A754), 对齐。
- ✅ `resetMotionStateLike_0x6B2D3C`(line 1853) — 0x6B2D3C clearHM3/HM4 + interpolate + HM4 snapshot
  + HM3 loop3(joinTarget gate + nodeType mask 0x19D), 对齐。
- ❓ `progressSeekNodeSlotsLike_0x6C106C`(node walk ④, advanceNodeFrameSelectionLike_0x6926B4 +
  parseFrame 0x6926B4 + mergeFrameContent 0x692AB0) — **不在本审计文件**(PlayerUpdateChildMotion.cpp
  或 PlayerRender.cpp)。LABEL_86 0x6B7358 的 inline node seek(parseFrame/mergeFrameContent/mask&4
  per-node action/node+346+882 merge gate/findSource)需在其所在簇单独递归审计。本簇调用点正确。
- ❓ `preProgressDirtyNodesLike_0x6B6878` — 调用点正确(line 2175 = 0x6C10AC); 函数体不在本文件。
- ❓ `progressMsLike_0x6D2A54`/`progressFramesLike_0x6D2A54`/`progressCompatMethod`(line 2597/2636/2646)
  — wrapper, 对齐 0x6D2A54/0x6D2A98 的 progress→updateLayers→calcBounds→dispatchEvents 序列。
  `updateLayers`/`calcBounds`/`dispatchEvents` 函数体不在本文件。

## 平台边界标注 (本审计跳过, 列出供 reviewer 核实)

- line 1986 `hm3InitValueFromNodeLike`: V+44 srcDispatch — 本地 src=std::string, 非 iTJSDispatch2*+icon
  对。注 PLATFORM_BOUNDARY(原因具体: src-dispatch 句柄对无 port 目标)。
- line 1743/2096 `hm3RestoreValueToNodeLike`/`pruneHM3`: findSource(0x6b85a0) srcDispatch 同边界。
- line 2196 `_nodes.empty()` 近似 renderList(+384/+392)空检查: 本地无 1:1 56B vector 容器
  (node-deque 帧步进核心整体 STL 化); 用 _nodes 空近似。注释已说明原因(memory 复核 renderList 身份)。
  **属容器拓扑近似边界**, 但严格说 "STL 化" 不是不可避免平台边界——reviewer 应核实是否可补 1:1
  renderList 容器(优先级低, oracle-inert)。
- line 2153 入口 +1152 DWORD 清零: 本地 Player 无 +1152 字段(那些 +1152 是 EmoteEngine._windFreqY)
  → 未建模字段缺口, 不臆造。合理。

## 修复建议 (优先级)

1. **偏差#2 (firstFrame deltaTime)** — 把 `_deltaTime = _speedMul*actualDelta;` 从 line 2312 上移到
   入口(line 2157 后), 删 line 2312 重复赋值(保留 line 2313-2315 的 `if(!_queuing)+1120+=`)。这样
   firstFrame 块 line 2241 读到本帧值。**最该修(数据流时机错位, 反向/变速场景真 bug)**。
2. **偏差#3 (尾部覆写)** — 删 line 2592-2593 + line 2238-2239 四行 port-invented `_allplaying`/
   `_syncActive` 写。先 grep 确认无调用方依赖 frameProgress 尾部刷新 _allplaying。
3. **偏差#1 (emoteMode gate)** — 入口加 `if(_directEdit){...}` 门控(initEmoteMotion(2) 无 port 目标,
   加占位+PLATFORM_BOUNDARY 注释)。最低优先(inert for logo)。

注: 三处偏差对 logo fixture(单向正向, _directEdit=false, _playingTimelineLabels 空)全程 inert,
故 logo 0-mismatch 是非回归守护, 不构成对修复的功能验证。无 reverse/emote/timeline fixture →
按 CLAUDE.md 不捏造物料, 在注释标注验证缺口即可。
