---
name: project-progress-containers-layout
description: Player 帧推进 2 个内嵌 deque 的精确 libstdc++ 80B 控制结构字段映射 + index→element 公式 + variable-track 元素 160B 字段表 + 两个 event-stream cursor。来自 0x6C106C/0x6B6ADC/0x6B86C8/0x6B786C/0x6B7A70 byte-verified
metadata:
  type: project
---

# Player 帧推进容器布局（byte-verified）

完整文档：`analysis/Player_progress_containers.md`。来源 progress_inner@0x6C106C、advanceRootAndNodes@0x6B6ADC、reseekTimelineCursors@0x6B86C8、sub_6B786C、sub_6B7A70。

## node deque @+200（element 2632B, 1 元素/block）
- 控制结构起 +200：+200 _M_map / +208 start.cur / +216 start.node / +224 finish.cur / +232 finish.last / +240 finish.first / +256 finish.node（被实际读的 end 槽）。
- **+280 不是 node-deque 字段**，是 aux composite list 头（reseek `for(n=*(a1+280);n;n=*n) sub_6B9650`）。node-deque 控制结构实际只占 +200..+264。
- size/index 魔数 0x18E6527AF1373F07 / 0xE719AD850EC8C0F9 = ÷(2632/8)=÷329 magic-division 两变体。
- index i(1-based)→addr：`v17=i - 0x18E6..*((map - start.cur)>>3); v17? *(finish.cur + 8*v17) : map + 2632*i`。1 元素/block 所以 node-map 槽==元素。

## variable-track deque @+1312（element 160B, 3 元素/block, block=480B）
- 控制结构起 +1312：+1312 start.cur / +1320 start.first / +1328 start.last / +1336 start.node / +1344 finish.cur / +1352 finish.first / +1368 finish.node。+1360 finish.last（未直接读）。+1376/+1384 padding。
- 跨块（advance 0x6B7160）：`v19+=160; if(v19==v20){v25=*(node+8); node+=8; v19=v25; v20=v25+480;}` → block=480B 权威。
- reseek index→element（0x6B8FD0, ÷3 magic 0x3333333333333333 + >>5）：`v45=i - 0x3333..*((start.cur - start.first)>>5); v45<0? blk=~(~v45/3) : v45<=2? (start.cur+160*i) : blk=v45/3; elem=*(node+8*blk)+160*(v45-3*blk)`。

## variable-track 元素 160B 字段
- +0 int frameIndex；+8 int activeSlotIndex（0/1，seek 翻转，reseek 末置 0）；+24 iTJSDispatch2 holder（frameList，~20B）；+48 / +104 两个 56B slot。
- slot 56B（0x6B786C 写 +0/+8/+22；0x6B7A70 写 +16/+20/+21/+22/+24/+32）：+0 frameIndex、+8 double time、+16 interval、+20 typeZeroFlag、+21 interpFlag、+22 mergedFlag、+24 double value、+32 tTJSVariant easing。2026-07-18 勘误：旧“+8 time/easing 可能重叠”已被 0x6B7CD4 证伪；easing 明确 CopyRef 到 +32。
- **+70/+126 = slot0/slot1 的 +22 mergedFlag**（不是独立元素字段）：advance `if(!*(v19+70)) sub_6B7A70` / `if(!*(v19+126)) sub_6B7A70`。
- Motion_VarTrackSlot_step_guess@0x6B786C 装入 index/time；Motion_VarTrackSlot_merge_guess@0x6B7A70 从 content 读 interval/value、从 frame 读 easing。

## event-stream cursor（非 deque：dispatch holder + int + 2 double）
- layer：+1072 source / +916 cursor / +920 curTime / +928 nextTime（无 content buf；type1 触发 stop/sync/align，受 +1093 门控）。
- root：+548 source / +568 cursor / +576 curTime / +584 nextTime / +616 content buf（sub_A0FB64 拷 content）。
- 都 `for(cursor; cursor<count-2;)`，nextTime<+456 break。
- reseek 路径 layer curTime/nextTime 被 int 截断（`(double)(int)propGetInt`），advance 路径 propGetDouble 不截断——精度差异须保留。

## 关键 flag（progress_inner）
+456 clampedEvalTime / +480 gate / +481 firstFrame / +482 emoteMode / +483 motionCompleted / +592 deltaTime(=+1168×dt) / +609 reverseSeek / +1092 completionType(mask 6145/6153) / +1093 motionStopGate(非speed) / +1098 syncWaiting / +1099 loopArmed / +1120 frameTickCount / +1128 totalFrames / +1136 loopTime / +1168 speedMul。

## inline 移植不可省略
两 deque 块元素数不同（node=1 因 2632>512、var-track=3 因 512/160=3）。必须各按 max(1,512/sizeof(T)) 算块容量，否则跨块判定 + 随机访问 + reseek size() magic 全错。
