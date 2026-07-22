---
name: layer-event-stream-brick5-hole3
description: layer(tag) stream historical audit；2026-07-19 current code has direction-split advance/rewind helpers and live 0x6B86C8 reseek scan
metadata:
  type: project
---

砖5/洞3 `Player::seekLayerEventStreamLike_0x6B6ADC` (PlayerFrameProgress.cpp ~803-936) audit @2026-06-01.

**CORRECTED 2026-07-19:** 该旧合并 helper 与 `_layerStreamSource` 已删除；当前按二进制
分为 `advanceLayerEventStreamLike_0x6B6ADC` 与
`rewindLayerEventStreamLike_0x6B9A3C`，cursor 仅由 init/reseek 和方向增量流管理。
下文关于 pointer-identity self-reset 的描述只记录旧实现，不是当前结论。

**CORRECTED 2026-07-23:** live port 已接入 `reseekTimelineCursors@0x6B86C8`
的 layer/root 扫描；下文“reseek-layer-gate 整体缺失”及其后果是修复前的
历史审计。+1093 的 NCB 字面绑定名是 `syncActive`，不再记为 `_speed`
或独立 `motionStopGate`。

**Decompiled & confirmed:**
- 0x6B6ADC advanceRootAndNodes: layer loop @0x6B6B80 `for(i=count-2; cursor<i;){ if(+456 < +928[nextTime]) break; ++cursor; ...; if type==1 gate }`. cursor=+916, curTime=+920, nextTime=+928. Gate writes +483(motionCompleted)/+456/+1120 on align; +1098(syncWaiting)/+456/+1120 + pushSync on sync. Action ungated.
- 0x6B9A3C rewindRootAndNodes: layer loop @0x6B9AE8 `if(count && +920>+456){ do{ --cursor; recompute +920/+928; if type==1 gate } while(+920>+456) }`. NO cursor>0 guard in binary.
- 0x6B86C8 reseekTimelineCursors: FULL non-incremental re-seek of BOTH layer+root cursors from scratch; has its OWN +1093-gated align/sync/action gate @0x6B8A8C (only fires when +920==+456, i.e. exact landing). Layer scan double-increments i. **This whole reseek-layer-gate is MISSING from the port** — port only ports advance/rewind, not reseek.
- Gate key `&MEMORY[0x14C9B9C][7]` = UTF-16LE "align" (confirmed via get_bytes). Both gates use +1093 (`syncActive`, defaultSyncActive bool, NOT speed multiplier which is +1168).
- 0x6B6294 pushSyncEvent: 44B {type=1} into +936 deque. 0x6B638C pushActionEvent: 44B {type=0; frameVar@+4=VOID copy of v87; actionVar@+24=content["action"]}. 0x6C4490 dispatchEvents: type0→onAction(+4,+24), type1→onSync(). => onAction(VOID, actionName). ALIGNED to port.

**ARCHITECTURAL DEVIATION (🔧, author-acknowledged "1-frame lag" understates it):**
Binary call graph: Player_progressCompat(0x6D2A98) -> progress_inner(0x6C106C) -> [advance|rewind|reseek]RootAndNodes (each runs layer-stream loop as its FIRST block, BEFORE root+node walks) -> updateLayers -> calcBounds -> **dispatchEvents(0x6C4490)**.
progress_inner calls these helpers at MANY points (firstFrame seed reseek; reverse-seek armed reseek+advance/rewind; LABEL_22/23 fwd loop-wrap advance; LABEL_27/28 rev; non-loop end; etc.) — potentially MULTIPLE advance/rewind per progress, EACH at a DIFFERENT +456 value (totalFrames, loopTime, wrapped). Port collapses all to ONE seekLayerEventStream call at end-of-frameProgress at the FINAL _clampedEvalTime only.

Consequences beyond "1-frame lag":
1. Loop-wrap: binary fires layer events for the pre-wrap segment (advance to totalFrames) AND post-wrap (advance from loopTime) — TWO sweeps. Port fires ONE sweep to final wrapped time -> MISSES events in skipped segment, and align/sync snaps that binary applies mid-progress (which gate further node seeks) never propagate.
2. reseekTimelineCursors gate entirely absent -> on firstFrame/reverse-seek landings exactly on a type==1 frame, binary fires align/sync; port does not.
3. align/sync snap to +456/+1120 in binary happens BEFORE same-call node walk -> same-frame node seek sees snapped cursor; port snaps after all node seeks -> 1-frame lag REAL.

**Real bugs vs harmless (author's 3 APPROXIMATIONs):**
- "1-frame lag": REAL deviation, not harmless. Snap propagation to node seek is same-call in binary.
- "layer stream source = tagFrames (motion['tag'] Player+1072)": CORRECT. advance/rewind/reseek all sub_A0F5E0(a1+1072). ✅
- "cursor>0 underflow guard": binary has NO guard but loop cond is `+920>+456`; relies on tag[0].time<=target. Guard is harmless ONLY if data always has tag[0].time<=target; defensive, not a behavior bug under valid data. ⚠️ benign.

**Forward-loop boundary ✅:** port `while(cursor<count-2){ if(target<nextTime) break; ++cursor; ...; if type==1 gate(curTime) }` matches 0x6B6B80 exactly (gate AFTER increment, on new cursor's frame, curTime=+920).
**Backward ✅ (modulo guard):** matches 0x6B9AE8 do-while.
**Cursor persistence ✅:** +916 persistent across calls (incremental), reset only via reseek/motion-load. Port self-resets via _layerStreamSource pointer identity — approximates reseek's cursor reset but reseek actually RE-SCANS from 0 every call (non-incremental), which port's advance/rewind-only model never does.

**Missing side-effects (G):** binary layer loop has NONE beyond align/sync/action + cursor/time writes (no node dirty / layer enable). Port complete on THIS axis. But root stream (+548=motion["priority"]) snapshots content into +616 every crossed frame (sub_A0FB64 a1+616) — port's seekLayerEventStream does NOT touch root stream at all (separate concern, may be elsewhere).
