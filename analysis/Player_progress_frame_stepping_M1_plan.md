# M1 / Cluster G — Player progress / 帧步进机 重架计划

> Date: 2026-05-30
> Authority: libkrkr2.so 反编译 (唯一权威). 本端代码可能误导.
> Scope: 只读分析 + IDB 改善. 本文件不修改 cpp/.
> 前置参考: clusterG_progress_timeline.md, Player_Class_Layout_libkrkr2so.md,
>   player_updateLayers_accum.md, agent-memory/project_framesel_analysis.md,
>   agent-memory/project_sub692AB0_key_mapping.md

本轮新反编译确认 (fresh decompile, byte-verified field types):
- Player_progress_inner @0x6C106C (主入口, 完整伪代码)
- Player_preProgressDirtyNodes @0x6B6878 (旧名 sub_6B6878, **本轮重命名**; 这是 progress_inner 真正调用的 preProgress, 不是 0x671764)
- Player_reseekTimelineCursors @0x6B86C8 (旧名 sub_6B86C8, **本轮重命名**; 全量游标 re-seek, 此前未文档化的关键函数)
- Player_preProgress @0x671764 (playing-list controller stepper, 与 progress_inner 解耦)
- Player_playImpl @0x6B2284 (+976/+984 dual-store, emote/non-emote 分派)
- Player_isAnimating @0x673F98 (3 controller bucket + 5 inline hashtable 扫描)

---

## 1. 完整状态字段图 (progress core)

字段类型经 byte-verify (LDRB/LDR/LDRD 区分). 偏移基于 Player* (a1).

| 偏移 | 类型 | 语义 | 读 | 写 | 本端对应 | 状态 |
|------|------|------|----|----|----------|------|
| +200 | ptr | root node (node-deque base, 2632B stride) | progress_inner walk, reseek | — | NodeTree root | 语义错(STL tree) |
| +208..+256 | ptr×6 | node-deque iterator pairs (libstdc++ deque 控制结构, 160B ptr-table chunks) | progress_inner, preProgressDirtyNodes, reseek | build | 无 (用 std::vector<MotionNode>) | **缺失/语义错** |
| +280 | ptr | aux singly-linked list (reseek 末尾 sub_6B9650) | reseek | — | 无 | 缺失 |
| +376 | ptr | activeTimeline (非空→直接 node-deque walk 分支) | progress_inner | playImpl/setMotion | `_activeMotion`(语义近似) | 语义错 |
| +456 | double | clampedEvalTime = min(+1120,+1128) | reseek×all-streams, advance/rewind | progress_inner (clamp), reseek (action gate) | `_clampedEvalTime` | **语义错**(本端=activeClipTime(clip) map lookup, 见 G4) |
| +480 | **1-byte** | progressFlags (LDRB; LSB gate; 默认值需 init 反编译确认, 注意非16-bit) | progress_inner (LABEL_48 门控) | init | `_queuing`(bit) | **缺失/语义错** |
| +481 | 1-byte | firstFrame one-shot | progress_inner (两处) | progress_inner 清零, setMotion 置位 | 无显式字段 | 缺失 |
| +482 | 1-byte | emoteMode (1=emote, 0=non-emote) | progress_inner (→initEmoteMotion 2) | playImpl | (emote/player 分两类) | 语义近似 |
| +483 | **1-byte** | motionCompleted (STRB WZR @entry 每帧清零) | progress_inner (每步 early-exit) | reseek (sync/align gate 置1), entry 清0 | 无 | **缺失** |
| +592 | double | deltaTime = speedMul(+1168) * dt | advance/rewind, reseek | progress_inner @entry | 无(本端直接用 dt) | **缺失** |
| +609 | 1-byte | reverseSeekFlag (一次性反向 seek 门) | progress_inner | reseek? | 无 | 缺失 |
| +1093 | 1-byte | motionStopGate (action/sync/align 处理开关) | reseek (layer-frame content) | setMotion | **`_speed`(误标"+1093")** | **语义错** (本端注释把它当 speed bool, 实为 stop gate) |
| +1098 | 1-byte | syncWaiting | progress_inner (~10× early-exit), isAnimating | reseek (sync gate 置1) | `_syncWaiting` | 已对齐(字段), 但无 per-step early-exit |
| +1099 | 1-byte | loopArmed / motion-loaded flag | progress_inner, playImpl (fail→0) | progress_inner, reseek | 无 | 缺失 |
| +1120 | double | frameTickCount (主播放游标) | progress_inner (累加), reseek | progress_inner (+=dt, wrap), reseek | `_frameTickCount` | 字段对齐, **推进逻辑错** (G3) |
| +1128 | double | totalFrames (本 motion 总帧长) | progress_inner (clamp 上界) | loadMotion/init | `_cachedTotalFrames` | 字段对齐, 来源可能错 |
| +1136 | double | loopTime (≥0 正向 wrap 基准; <0 禁 loop) | progress_inner (loop wrap 分支) | loadMotion/init | `_loopTime` | 字段对齐 |
| +1152 | dword | (progress_inner @entry 清零, 用途待定 — likely per-frame counter) | — | progress_inner @entry | 无 | 缺失 |
| +1168 | double | speedMul (播放速度倍率, **+592 的因子**) | progress_inner @entry (first read) | setSpeed/NCB | 无(本端 speed=bool) | **缺失** |
| +1312..+1368 | ptr×N | variable-track deque (160B stride, 3-per-chunk) | reseek (per-track 2-slot seed) | build | 无 | 缺失 |

node-level 字段 (在 advanceNodeFrames / parseFrame / mergeFrameContent 内, node base):
| node 偏移 | 语义 |
|-----------|------|
| node+8 | active/visible flag (progress_inner 跳过 !node+8) |
| node+64 | frameList variant (parseFrame 索引源) |
| node+320 / +856 | 两个 parsed-frame slot buffers (536B stride) |
| node+1392 | activeSlot cursor (toggles 0/1) |
| node+1980 | motion dict (preProgressDirtyNodes 'modified' query) |
| node+1996 | timelineDirty (preProgressDirtyNodes 门控) |

---

## 2. progress_inner 数据流伪代码

```
Player_progress_inner(player, dt):
  speedMul = player[+1168]
  player[+1152] = 0
  player[+483] = 0                         # motionCompleted 每帧复位
  player[+592] = speedMul * dt             # deltaTime
  if player[+482]: initEmoteMotion(player, 2)   # emote mode 每帧重 init
  preProgressDirtyNodes(player)            # 0x6B6878: 脏节点重建 timeline

  if player[+376] (activeTimeline):        # ---- 分支 A: 显式 activeTimeline ----
    t = activeTimeline[+40]                # timeline 起始/目标 time
    if player[+481] firstFrame:            # one-shot 种子
      player[+1120]=t; player[+481]=0; player[+456]=t; return reseekTimelineCursors(player)
    if t > player[+456]: wrap-forward; player[+1120]=...; player[+456]=...; return advanceRoot
    if t == player[+456]: walk node-deque, advanceNodeFrames(node, player) 每节点
    else: wrap-backward (LABEL_27)
    return

  # ---- 分支 B: 无 activeTimeline (普通 motion 播放) ----
  if !player[+481] && !player[+1099]:      # 既非首帧又未 armed → 仅 per-node advance
    walk node-deque; advanceNodeFrames(node, player); return
  if player[+1098] syncWaiting || player[+483] motionCompleted: return  # 全局门
  if player[+481] firstFrame:              # 首帧种子 + 可选反向 seek (+609)
    dt0 = player[+592]; player[+481]=0
    if dt0<0 && player[+1120]==0: player[+456]=player[+1120]=player[+1128]  # 反向从尾开始
    if player[+609] reverseSeekFlag: ... reseek/advance/rewind 一次性对齐 ...
    else: reseekTimelineCursors(player); if syncWaiting return
    if motionCompleted: return

  LABEL_48:                                # ---- 主推进 ----
  flags480 = player[+480]; d = player[+592]
  if !flags480:                            # progressFlags LSB 清时才推进游标
    player[+1120] += d
    player[+456] = min(player[+1120], player[+1128])   # G3/G4 真正的 clamp
  if d >= 0:                               # 正向
    if player[+1128] <= player[+1120]:     # 到尾
      if player[+1136] loopTime >= 0:      # loop: reseek 到尾 → wrap modulo → advanceRoot
        player[+456]=player[+1128]; advanceRoot; reseek(456=loopTime)
        wrap: while(totalFrames > tick) tick += loopTime - totalFrames; advanceRoot
      else: player[+1099]=0; if !flags480 advanceRoot      # 非 loop: 停在尾
    else: if !flags480 advanceRoot         # 未到尾: 正常 advanceRoot
  else:                                    # 反向 (d<0) 对称: rewindRoot, wrap to head
    ...
```

关键不变量:
- `+456 clampedEvalTime` 是**所有下游 eval 的时间锚** (reseek/advance/rewind/parseFrame 全部读它). 它由 `min(+1120,+1128)` 产生, **不是 timeline-map lookup** (本端 G4 错误的根因).
- `+480` LSB 为 1 时**冻结游标推进**但仍跑 advanceRoot (用于 paused/queued 状态预览).
- `+483/+1098` 在每个 advance 步之间被反复检查 → 这是 sync/completion 的**协作式打断**机制.

---

## 3. 本端 STL timeline 状态机 ↔ 二进制 node-deque 操作 (many-to-many)

| 本端构造 (PlayerFrameProgress/PlayerTimeline) | 二进制对应 | 关系 | 备注 |
|------|------|------|------|
| `frameProgress(dt)` @789 | progress_inner @0x6C106C | 1:1 入口 | 但内部 topology 完全不同 |
| `_frameTickCount += dt` (无 speedMul/clamp) | +1120 += (+592=speedMul*dt); +456=min | 1:N | 缺 speedMul, 缺 min clamp, 缺 +480 gate |
| `_clampedEvalTime = activeClipTime(selectActiveClip())` | +456 = min(+1120,+1128) | **语义冲突** | 本端做 map lookup, 二进制做标量 min |
| `preProgressPlayingTimelinesLike_0x671764` @411 | Player_preProgress @0x671764 (controller stepper) | 1:1 但**错位调用点** | 二进制此函数不在 progress_inner 链上; progress_inner 调的是 preProgressDirtyNodes@0x6B6878 |
| `_timelines` map (TimelineState{currentTime,totalFrames,...}) | node-deque (2632B node × N) + 3 frame-stream cursors | N:M | 本端 per-label state vs 二进制 per-node slot + 全局游标 |
| `_playingTimelineLabels` vector | playing-list a1[130..131] (TJS variant array) + 5 inline hashtable | 1:N | 本端 vector, 二进制 variant array + hashtable dedupe |
| control-animator queue stepping (dt<=1.1) | preProgressDirtyNodes 'modified' rebuild + per-node initNodeTimeline | 不对应 | 二进制无 "control-animator bucket" 概念 |
| blendAnimator | evaluateTimeline @0x699AE4 two-slot lerp | 部分 | 本端 child-motion crossfade 部分镜像 sub_69A4D4 |
| `selectActiveClip()` / `activeClipTime()` | (无) | 凭空多出 | 二进制无 clip 抽象, 时间直接来自 +456 |
| `_evalResultValues` clear + applyEvalResultPostProcess_0x67CC9C | (无直接对应) | 凭空多出 | |
| (无) | reseekTimelineCursors @0x6B86C8 全量游标 re-seek | **完全缺失** | layer(+916/920/928)/root(+568/576/584)/var-track 三流 + action/sync/align gate |
| (无) | advanceNodeFrames/advanceRoot/rewindRoot/parseFrame/mergeFrameContent | **完全缺失** | 整个 node-deque 帧步进核心 |

---

## 4. 分阶段计划表

排序原则: 先**可加成员、不接线、不触 live trace** 的安全叶子, 后**主体替换**. 每阶段独立 CI 绿.

| # | 阶段 | 改动范围 | 触碰 live trace? | CI 可验证? | 回归风险 |
|---|------|----------|:----------------:|:----------:|:--------:|
| **P1** | **加状态字段成员** (不接线): 在 Player 内补 `_speedMul(double)`, `_firstFrame`, `_motionCompleted`, `_progressFlags(uint8)`, `_loopArmed`, `_reverseSeekFlag`, `_deltaTime`, 并把误标的 `_speed` 注释/分离为 `_motionStopGate`(+1093). 仅声明+默认值+注释引用地址. | Player.h 字段区 | 否 (未读未写) | 是 — 编译通过即绿, logo diff 0 mismatch (未改逻辑) | 极低 (纯加成员) |
| **P2** | **实现 parseFrame/mergeFrameContent 为独立 free 函数** (PlayerFrameStep.cpp 新文件), 签名/字段读写 1:1 复刻 0x6926B4/0x692AB0, 但**不从 frameProgress 调用** (dead code, 单测覆盖). | 新增 .cpp + 单测 | 否 | 是 — 新单测 (喂 PSB frame dict, 断言 slot 字段); logo diff 不变 | 低 (新文件, 不接线) |
| **P3** | **实现 node-deque slot buffer 结构** (node+320/+856/+1392 activeSlot) 作为 MotionNode 的附加成员, build 时初始化, 但 eval 仍走旧路径. | MotionNode.h + build | 否 (字段存在但 eval 不读) | 是 — frame_selection oracle 断言 activeSlot 字段, 可新增 stage 断言; logo diff 不变 | 低-中 (build 改动, 但 eval 解耦) |
| **P4** | **实现 reseekTimelineCursors @0x6B86C8 为独立函数** + advanceNodeFrames/advanceRoot/rewindRoot, 接 P2 的 parseFrame. 仍不接 frameProgress. 用 init_motion + frame_selection 双 oracle 离线驱动单测. | 新增 .cpp + 单测 | 否 | 是 — 离线喂 oracle 输入, 断言游标 (+916/+568/+1120) 与 oracle 一致 | 中 (复杂逻辑, 但隔离) |
| **P5** | **接线 firstFrame 种子路径**: frameProgress 中, 仅当 `_firstFrame` 时走新 reseek 路径, 其余仍旧路径 (feature-flag 或 motion-type gate). | PlayerFrameProgress.cpp | **是** (改 live path, 但限 firstFrame) | 是 — logo diff frame 0 必须保持 0 mismatch; 任何首帧偏移立刻暴露 | 中-高 (首帧是渲染基线) |
| **P6** | **接线主推进 LABEL_48** (游标 +=deltaTime, min-clamp, loop wrap, advance/rewind), 替换 `_frameTickCount += dt` + `activeClipTime`. | PlayerFrameProgress.cpp 核心 | **是** (核心 live path) | 是 — logo diff **全帧** 逐层 Motion 状态; frame_selection oracle 每节点 activeSlot/visible/opacity | **高** (这是 G1/G3/G4 主体替换) |
| **P7** | **拆除旧 STL timeline 状态机** (selectActiveClip/activeClipTime/_evalResultValues/control-animator), 把 isAnimating/playTimeline 迁到 binary topology (G10-G16). | 多文件清理 | 是 | 是 — 全 motion_playback diff + isAnimating 单测 | 中 (主体已在 P6 替换, 此为收尾) |

CI 覆盖说明:
- **logo differential** (`tests/differential/specs/motion_playback/{yuzulogo,m2logo}.json`) = 非-emote logo 逐帧逐层 Motion 状态, oracle 来自 android-frida-libkrkr2. 覆盖 P5/P6/P7 的 live-path 改动. P1-P4 不触 live path → 自动保持 0 mismatch (回归即编译/单测层面).
- **staged oracles** (`motion_playback_stages/frame_selection`, `init_motion`): frame_selection 在 `evaluateTimelineLike_0x699AE4.leave` 每节点采样 {activeSlot, nodeType, flags, active, visible, opacity}. **这是 P3/P4/P6 的精准回归网** — 任何游标/slot 选择偏差直接反映在 activeSlot 与下游 visible/opacity.
- P5/P6 必须在合入前跑全 motion_playback diff 并保持 0 mismatch; 否则回退 (CLAUDE.md BLOCKING 规则).

---

## 5. 首阶段建议 (第一个最小可 CI 验证 commit)

**执行 P1: 补 progress core 状态字段成员 + 修正 +1093 误标.**

具体:
1. Player.h 字段区新增 (默认值需后续 init 反编译确认, 先用安全默认):
   - `double _speedMul = 1.0;`        // +1168 速度倍率 (frameProgress 第一读)
   - `double _deltaTime = 0.0;`       // +592 = _speedMul * dt
   - `bool _firstFrame = false;`      // +481 one-shot 种子
   - `bool _motionCompleted = false;` // +483 每帧复位的 completion 门
   - `bool _loopArmed = false;`       // +1099
   - `bool _reverseSeekFlag = false;` // +609
   - `uint8_t _progressFlags = 0;`    // +480 (1-byte LDRB, **非16-bit**; 默认值待 init 反编译)
2. 把现有 `_speed` (Player.h:665, 误注释 "+1093: bool flag") 的注释改为指向 **+1093 = motionStopGate (action/sync/align 处理开关)**, 或重命名为 `_motionStopGate` 并保留 NCB getSpeed/setSpeed 桥接 (NCB "speed" 实际映射需另行确认是否就是 +1093 — 标 `_guess` 待验证).

为什么是 P1:
- **零回归**: 纯加成员 + 注释, 不读不写, 不改任何逻辑 → logo diff 必然 0 mismatch, 编译通过即绿.
- **解地基**: re-arch 的每个后续阶段都依赖这些字段存在. 先把"地基字段"落地, 后续 P2-P6 才有承载点.
- **修正活跃误导**: `_speed` 误标 +1093 是会引人走错路的注释错误 (+1093 是 stop gate 不是 speed; speed 倍率在 +1168). 越早修正越好.
- **符合 BLOCKING 工作流**: P1 的每个字段都有本轮反编译证据 (progress_inner @0x6C106C byte-verified 偏移与类型) + IDB 注释.

明确不在 P1 做: 任何 frameProgress 逻辑改动 (那是 P5/P6, 高风险, 需 live diff 守护).

---

## 6. IDB 改善记录 (本轮)
- rename 0x6B6878 → Player_preProgressDirtyNodes
- rename 0x6B86C8 → Player_reseekTimelineCursors
- set_comments: 0x6C106C (progress_inner 完整字段图), 0x6B86C8 (reseek 三流), 0x6B6878 (dirty-node preProgress)
- byte-verify: +480/+483 = 1-byte (LDRB/STRB), +592/+1120/+1128/+1136/+1168 = double
- idb_save done

## 7. 关键勘误 (相对 clusterG_progress_timeline.md)
- G2 说 "+480 progressFlags is a 16-bit gate (init 257)" — 本轮 byte-verify 显示 progress_inner 在 0x6C1330 用 **LDRB** 读 +480 (1-byte). "init 257" 可能来自别处的 16-bit 写入 (低字节=1), 但 progress_inner 只测 LSB. 待 init 函数反编译厘清宽度.
- preProgress: progress_inner 调用的是 **Player_preProgressDirtyNodes @0x6B6878**, 不是 Player_preProgress @0x671764. 后者是 playing-list controller stepper, 与 progress core 解耦. 本端 `preProgressPlayingTimelinesLike_0x671764` 对应的是 0x671764, 接错了调用点.
- reseekTimelineCursors @0x6B86C8 此前未被任何文档/memory 记录, 是 progress core 的关键缺失环节 (firstFrame 种子 + loop wrap 都经它).

---

## 8. 砖5 (brick 5) — 洞3 advanceRootAndNodes 事件流 fresh decompile (2026-06-01)

> Authority: 本轮 re-decompile (非复用旧结论). 函数: 0x6B6ADC / 0x6B638C /
> 0x6B6294 / 0x6C106C / 0x6B6878 / 0x6B51F0 / Player_initEmoteMotion / 0x6D9618.

### 8.1 Player_advanceRootAndNodes @0x6B6ADC = 4 条流 (顺序固定)
progress_inner 在 LABEL_48 终端各分支调用它 (forward) / rewindRootAndNodes
0x6B9A3C (reverse). 4 条流按此顺序:

1. **layer 事件流** (player+1072 dispatch, 游标 +916 / curTime +920 / nextTime +928):
   `while(+916 < count-2){ if(+456 < +928) break; +916++;
     curTime(+920)=frames[+916].time; nextTime(+928)=frames[+916+1].time;
     if(frames[+916].type==1) <事件门>; }`
2. **root 流** (player+548 dispatch, 游标 +568 / +576 / +584, content 快照 +616):
   `while(+568 < count-2){ if(+456 < +584) break; +568++;
     +616 = frames[+568].content (sub_A0FB64 copy); +576=+584;
     +584=frames[+568+1].time; }`  ← 注意: root 流**无** type==1 事件门, 只快照 content.
3. **variable-track deque** (player+1312/+1328/+1336/+1344/+1368, 160B stride record,
   每 record 内 56B×2 slot @ +48/+(48+56), 游标 record+8 奇偶, sub_6B786C 步进 /
   sub_6B7A70 merge). 仍 DEFERRED.
4. **node-deque walk** (LABEL_86): 每 node (idx≥1) node+8 非空→Player_advanceNodeFrames
   0x6B7E44; 否则 inline 2-slot seek (slot+0 SIGNED vs count-2) + mergeFrameContent +
   (gated) findSource + (other.mask & 0x40000)→sub_6B638C action. 这条已被本端
   progressSeekNodeSlotsLike 近似 (洞2).

### 8.2 layer 事件门 (type==1 frame, byte-verified) — **prompt 要求确认项, 已确认**
仅当 `*(BYTE)(player+1093)` (motionStopGate) 置位时处理 align/sync:
- key "align" @0x14C9BAA byte-verified UTF-16LE `61 00 6C 00 69 00 67 00 6E 00`
  = "align". `propGetBool(content,"align")` → `+483(motionCompleted)=1;
  +456 = +1120 = +920(curTime)`.
- key "sync": `propGetBool(content,"sync")` → `+1098(syncWaiting)=1;
  +456 = +1120 = +920; sub_6B6294(player)` (push sync event).
- key "action" (sub_529524 PropGet): 非空 → `sub_6B638C(player, &frameVariant, &actionVariant)`
  (push action event).

### 8.3 事件 deque @player+936 (44B record) — push 0x6B638C/0x6B6294, consume 0x6C4490
record = `{ int type@+0; tTJSVariant a@+4 (20B); tTJSVariant b@+24 (20B) }` = 44B
(stride 11 dwords, byte-verified at consumer `i += 11`).
push 路径: `end=+944; if(end==+952) sub_6F23CC(grow); else 就地写 + (+944)+=44`.
- **Player_pushSyncEvent_guess @0x6B6294**: type=**1** (sync), a/b = 空 tTJSVariant.
- **Player_pushActionEvent_guess @0x6B638C**: type=**0** (action), a=copy(a2 帧变量), b=copy(*a3 action变量).
  ⚠ 勘误(上轮误读): 0x6B638C 的 `v9=0` 才是 record.type(=0); 之前误把 `v12=2`(那是 v11 这个
  action tTJSVariant 的 **type tag**, 非 record type) 当成 event type. **正确: action=0.**
- **Player_dispatchEvents @0x6C4490** (consumer, byte-verified): `for(i=+936; i!=+944; i+=11){
  if(*i){ if(*i==1) onSync(); /* type>=2 无 op */ } else onAction(a@+4, b@+24); }`.
  即 **type0→onAction(a,b); type1→onSync(); 其余忽略**.
- ✅ **与本端一致**: 本端 `MotionEvent` + dispatch (PlayerFrameProgress.cpp:1130-1140) 就是
  type0=onAction / type1=onSync — **type 码已对齐, 无需改**. (RuntimeSupport.h:159 注释正确.)
  onAction 参数: param1=record.a(+4), param2=record.b(+24=action 值). (record.a 即 0x6B638C 的 a2=&v87,
  其具体内容待 sub_529524 反编译确认; 本端 scanLayerActions 推 {0, actionStr, layerLabel}, 参数序待核.)

### 8.4 洞1 Player_preProgressDirtyNodes @0x6B6878 (fresh)
progress_inner @0x6C10AC 首先调用它 (本端缺). 逐 node (deque, idx≥1): 若
`+1996(timelineDirty)!=0` → 读 node+1980 motion dict, `propGetBool(...,"modified")`;
若 modified → `PropSet(512,"modified",0)` 清旗 + `Player_initNodeTimeline_guess(player,node)`
重建该 node 时间线. 与 0x671764 (controller stepper) 无关.

### 8.5 ✅ 已解: +1072 / +548 stream 来源身份 — writer = Player_initNonEmoteMotion @0x6B3694
byte-verified (Player_initNonEmoteMotion, v24 = AsObject(player+528) = motion dispatch):
- `+1136 = motion["loopTime"]` (=_loopTime), `+1128 = motion["lastTime"]` (=_cachedTotalFrames) @0x6B370C/0x6B372C.
- **`+1072` (layer stream) = `motion["tag"]`** @0x6B3778 (`sub_A0FB64(a1+1072, PropGet "tag")`).
- **`+548` (root stream) = `motion["priority"]`** @0x6B37D0 (`sub_A0FB64(a1+548, PropGet "priority")`).
- **`+616` = `priority[0].content`** @0x6B38FC (PropGetByNum 0 → AsObject → PropGet "content").
- `+1098/+1099 = 0/1` (STRH 256 @0x6B3A74); 若 `(a2&2)==0`: `+1120=0; +456=fmin(+1128,0); +480=257; +481=1`.
- ⇒ Player_Class_Layout_libkrkr2so.md 的 **"+1072 = stealthMotionStr (ttstr)" 标注错误**:
  +1072 实为 `motion["tag"]` 帧数组 dispatch; `stealthMotion` property (sub_6D9618) 只是复用该字段读出.
  (待修正该 layout note + 把 +936 "variableList" 修正为 44B 事件 deque.)

数据结构 (byte-verified from advance/rewind loops):
- **layer stream `motion["tag"]`** = array of `{ time:double, type:int, content:{align,sync,action,...} }`.
  仅 `type==1` 帧触发事件门 (见 §8.2). 这是**全局** onAction/onSync 来源.
- **root stream `motion["priority"]`** = array of `{ time:double, content:{...} }`. 无事件门, 仅 content 快照→+616.

### 8.7 advance/rewind gate form vs reseek + onAction 参数序 (commit-3 前置)
- **advance (0x6B6DD8) 与 rewind (0x6B9D0C) 的事件门完全相同** (本轮 byte-verify rewind):
  `if(+1093){ if(content["align"]){motionCompleted=1; +456=+1120=curTime;}
              if(+1093 && content["sync"]){syncWaiting=1; +456=+1120=curTime; pushSync;} }
   action=content["action"]; if(action) pushAction(player, &scratch, &action);  // action 不受 +1093 门`
  ⚠ align **不** time-gate (与 reseek 0x6B89FC 的 `curTime==clampedEvalTime` 形式不同)。
  现有 stand-in applyLayerActionGate 是 reseek 形式; live advance/rewind 端口须用此 +1093-only 形式。
- rewind 循环条件: `do{ --cursor; curTime=tag[cursor].time; nextTime=tag[cursor+1].time; gate; }
   while(curTime > clampedEvalTime)`; 入口 `count!=0 && curTime>clampedEvalTime`.
- ✅ **已 RE 定 (commit-3 决): onAction(void, actionName)**:
  - pushAction sub_6B638C(a1,a2,a3): record.a=copy(a2 处 tTJSVariant), record.b=copy({ptr=*a3, tag=2=string}).
  - Player_dispatchEvents 0x6C4490: type0 → onAction(record.a, record.b) (2 args).
  - record.b = content["action"] 值 (tag=2=tvtString, 即 action 名字符串)。 = **param2**.
  - record.a = &v87 处的 tTJSVariant。v87 在 0x6B6D68 被 sub_A0F778 **释放→type(+16)=void(0)**;
    随后只被 sub_529524 当 PropGet **hint(uint32@+0)** 写 (不动 +16 的 type)。Motion_propGetBool
    **不读 a3=&v87**。⇒ record.a = type=void 的空变量 = **param1 = void**。
  - 证据: sub_A0F5E0 读 type@(a2+16); Motion_propGetBool body 忽略 a3; sub_529524 a4=hint。
  - ⇒ 二进制 **onAction(void, actionName)**; 本端 scanLayerActions 是 onAction(actionName, layerLabel)
    (param 序 + 内容均不同)。commit-3 应改为 onAction(空, action名)。
  - ⚠ param1=void 结论意外 (回调首参为空), 静态证据充分但建议有 tag-action 帧的 motion 时 runtime
    复核 onAction 实参一次 (logo 差分可能不含 tag-action 帧, 不覆盖此路径)。

### 8.6 live 集成现状 (gap) + 实现设计 (unblocked)
- live frameProgress: node seek 用 progressSeekNodeSlotsLike_0x6C106C (简化, 仅 node 流);
  事件用 scanLayerActions (RuntimeSupport.cpp:1711) + preProgressPlayingTimelinesLike 推 `_pendingEvents`.
- ⚠ scanLayerActions 扫的是 `snapshot.layerList` (= motion["layer"], node 树) + 每 clip.layerList,
  **不是 motion["tag"]**; 且缺 align / type==1 门 / +1093 stopgate / frameTickCount 吸附. 事件源+门控均错.
- live `_activeMotion`: `+528`≈`snapshot.root`; `+548 priority`≈`snapshot.clipList` (RuntimeSupport.h:193 已对齐);
  **`+1072 tag` 当前无对应存储 — 缺**.
- 忠实端口 advance/rewind/reseekRootAndNodesLike_* 存在但仅 unit-test (PlayerFrameStreamsLike
  stand-in 模型, 非 live MotionNode). 洞2/3/4 共同根因 = live 用简化 seek 而非忠实 4 流.
- 洞4: 忠实反向端口 rewindRootAndNodesLike_0x6B9A3C **已存在** (PlayerFrameStepping.cpp:386); gap 是
  live 双向都走 progressSeekNodeSlotsLike, 未接反向端口.
- **实现序列 (最小可验证 commit, additive-first)**:
  1. (additive, 0 回归) MotionSnapshot 加 `tagFrames` (PSBList/vector<PSBDictionary>), load 期从
     `root["tag"]` 填充 (RuntimeSupport.cpp scanDictionary top-level). 无 reader → CI 必绿. 落数据地基.
  2. (additive) Player 加 live layer-stream 游标 (cursor/curTime/nextTime = +916/+920/+928) + root-stream
     游标 (+568/+576/+584) + rootContent (+616). 暂不读.
  3. (行为变更, CI 裁决) frameProgress 在 +456 定下后跑忠实 layer+root 流 advance/rewind (含 align +
     +1093 门 + frameTickCount/clampedEvalTime 吸附), 推 onAction/onSync 入 _pendingEvents, **替换
     scanLayerActions**. 先反编译 Player_dispatchEvents 0x6C4490 确认 type→callback (binary sync=1/action=2
     vs live 0/1).
