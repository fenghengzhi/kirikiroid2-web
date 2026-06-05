---
name: frameprogress-entry-0x6C106C
description: Player::frameProgress 入口段 vs progress_inner@0x6C106C 入口拓扑(0x6C1080..0x6C1278)逐行审计结论 + _queuing 双角色映射等价性证明
metadata:
  type: project
---

frameProgress 入口段(PlayerFrameProgress.cpp:1925..2053) vs Player_progress_inner@0x6C106C 入口拓扑 2026-06-06 逐行复核：整体 ALIGNED。

**入口字段偏移(字节核对)**: 0x44A=1098 syncWaiting / 0x44B=1099 loopArmed(=_allplaying) / 0x1E1=481 firstFrame(=_firstFrame) / 0x1E3=483 motionCompleted / 0x180=384,0x188=392 renderList / 0x250=592 deltaTime / 0x490=1168 speedMul / 0x480=1152(DWORD 入口清零).

**入口无条件副作用顺序(0x6C1080..0x6C10AC)**: speedMul读 → +482emote读 → +1152=0(DWORD) → +483=0(STRB,任何分支前) → +592=speedMul*dt → if(emote)initEmoteMotion(2) → preProgressDirtyNodes. 本地 line1942清_motionCompleted, 1960 preProgress, _deltaTime延后到 line2050(LABEL_48)算 —— 入口/门控/firstFrame-seed 三类 return 路径**均不读+592**, 故延后 observationally 等价(ALIGNED).

**门控顺序(本地 1966→1993→1996)**: 0x6C10E4 `if(+481==0 && +1099==0) goto loc_1270` → loc_10F4: 0x6C10F8 if(syncWaiting)return → 0x6C1100 if(motionCompleted)return → 0x6C1104 if(firstFrame==0)goto LABEL_48. 本地同序 ALIGNED.

**_queuing 单字段承载二进制 +480(gate)+481(firstFrame seed)双角色**: 这是已验证的等价映射, 不是偏差。
- 二进制 firstFrame 块(0x6C1108..)清+481后 **fall-through 到 LABEL_48**(不 return); 本地 `if(_queuing){reseek;return;}` 是 return。
- 第一帧 play 后: +480 gate=1(updateLayers 未清), 二进制 LABEL_48 advance 被 gate 跳过, forward `+1128<=+1120`(=>0<=0 false 因+1120=0/+1128>0) → `else if(!v23)` v23=+480=1 → return result(无 advance)。净效果 = reseek+不推cursor+不调advance, 与本地 reseek+return 等价。
- 第二帧起: updateLayers(PlayerUpdateLayers.cpp:105, 二进制0x6BBDFC STRB WZR,[+1E0])清_queuing=0 且二进制 firstFrame 自清=0, 两者落 LABEL_48 正常推进。
- 前提: 每帧 frameProgress→updateLayers 顺序成立(2344+2346/2406+2431/child 538+539/796+798)。
- 收窄: _firstFrame 在 initNonEmoteMotionLike(PlayerCore.cpp:806-810)**未设**(只设_queuing), 故 play 后 _firstFrame 恒false, 门控 `if(!_firstFrame&&!_allplaying)` 由 _allplaying 主导。play 后 _allplaying=true 时一致。

**LOW 缺口(待 reviewer)**: 入口 +1152=0(DWORD, 0x6C1088)本地未复刻; line1944 注释自承知情但只实现+483。需 grep 确认+1152本端字段归属再定夺(dead-missing vs real-missing)。禁单次空 grep 判无字段。

**renderList → _nodes.empty() (ACCEPTABLE-PLATFORM-BOUNDARY)**: 0x6C1278 `if(+384==+392)return` 本地 `if(_nodes.empty())return`(line1981). node-deque 帧步进核心整体 STL 化, 无 1:1 renderList。never-played child(全0 +1136/+1128, _nodes 空)在此 return, 永不到 LABEL_48 forward loop-wrap do-while(否则 v7+=0-0 while(0<=v7)永真→千恋万花标题死循环)。这是二进制避免全0 child空转的真正机制(非 loopTime<lastTime 不变量/非+1136<0默认)。
