# CLUSTER H — Timeline + frame-stepping alignment audit (2026-06-07)

> Method: fresh IDA decompile of advance/rewind/advanceNode/reseek/parseFrame/
>   mergeFrameContent (6 funcs) vs cpp/plugins/motionplayer/ port. Authority =
>   libkrkr2.so. Supersedes stale cluster-G SEVERE table (those targeted the old
>   STL state machine; the P2/P3/P4 ports audited here did not exist then).
> IDB: 1 comment added @0x6B91B0; idb_save done.

## SCOPE
PlayerFrameStep.{h,cpp} (P2 parse/merge), PlayerFrameStepping.{h,cpp}
(P3/P4 node-deque cursor stepping), PlayerTimeline.cpp (timeline queries +
play/stop raw callbacks).

These P2/P3/P4 routines are INDEPENDENT free functions, exercised only by
motionplayer-dll unit tests — NOT wired into the live progress path. (The live
`advanceNodeFramesLike_0x6B7E44(MotionNode&,double)` in PlayerUpdateLayerEval.cpp
is a DIFFERENT overload, outside cluster H.) So all findings here are
oracle-inert by construction; per CLAUDE.md that is NOT a reason to deprioritize.

## VERDICT (six dimensions)
- 源码结构: ✅ — slot/cursor/node structs mirror node+320/+856/+1392, +916/+920/
  +928, +568/+576/+584/+616, the player scalars (+456/+1120/+1093/+483/+1098/
  +1092).
- 数据流: ✅ (parse/merge/advance/rewind) / ⚠ (reseek per-node tail, see H-P1).
- 调用链: ✅ — advance→advanceNode/parseFrame/merge/findSource; reseek→
  6B786C/6B7A70 var-track (deferred) + initNodeTimeline (partial).
- 对象生命周期: PLATFORM_BOUNDARY — binary AddRef/Release on TJS dispatch
  holders replaced by shared_ptr<PSBList/PSBDictionary>; documented, unavoidable
  (no live iTJSDispatch2 motion tree).
- 内部容器: ✅ topology / PLATFORM_BOUNDARY leaf — node-deque + var-track deque
  + 2 stream cursors modelled by std::vector<NodeFrameStreamsLike> + per-cursor
  shared_ptr lists; the ÷329 / ÷3 magic-division index formulas are NOT ported
  (the port uses a flat vector), which is acceptable because these are libstdc++
  deque size()/index inlinings, not source tokens (see H-N1).
- 边界行为: ✅ — mask gates (0x1849 src, 0x20600 color-grp, 0x1FC xform,
  0xF800 curves, 0x04000000 ti, 0x10000 cp, sub-objs) all byte-exact; align/sync
  gates +1093-gated; int-truncation in reseek preserved.

OVERALL: ⚠ 部分偏差 — one P1 (reseek non-child init deferred). Everything else
faithfully ported or legitimate platform boundary. NO architecture re-work
needed; NO stall (this is the first cluster-H audit of these ports).

## Decompiled pseudocode summaries (the 6 funcs)

### parseFrame @0x6926B4
```
resetSlot(slot); slot+0=frameIndex; obj=frame[frameIndex byNum]
slot+8 = obj["time"]; t = obj["type"]
if t==0: slot+24(typeZero)=1; return
slot+24=0; if t==2 slot+25(interp)=0; if t==3 slot+25=1
content=obj["content"]; slot+20(mask)=content["mask"]
if mask&0x40000: slot+288(act)=content["act"]   (AddRef'd variant)
```
PORT parseFrameLike_0x6926B4: byte-exact. ✅ (null-frame -> typeZero=1, a
faithful mirror of the binary's unconditional deref on a void frame.)

### mergeFrameContent @0x692AB0
```
slot+26(merged)=1; if slot+24(typeZero) return
reset: packed[+72]x4=0xFF808080, opacity[+88]=255, blend[+44]=16
if (1<<nodeType)&0x1849: src(+36) + icon(+28) handle [icon DEFERRED in port]
mask&1: ox/oy(+56/+64); mask&2: coord[0..2](+96/104/112)
mask&0x20600: bm(0x20000->+44); color(0x200, switch 1-5 ->4ch / -1 fill when
  no-color-bit AND blend&0xF0==0); opa(0x400->+88)
mask&0x1FC: fx/fy(0xC), angle(0x10), zx/zy(0x60), sx/sy(0x180)
if interp(+25): ti(slot+23&4 == 0x04000000); curves 0xF800(ccc/occ/acc/zcc/scc);
  cp(slot+22&1 == 0x10000)
mask&0x2000000: mesh/bezierPatch(32-pt, raw float-pair vector) [DEFERRED]
sub-objs: motion 0x80000, model 0x1000000, prt 0x100000, camera 0x200000,
  anchor 0x800000, feedback 0x8000000
```
PORT mergeFrameContentLike_0x692AB0: byte-exact on every mask gate, default,
field. ✅. The two subtle gates verified against the port:
- ti gate = mask&0x04000000 (slot+23 byte &4) — port line 276 ✅
- cp gate = interp && mask&0x10000 (slot+22 byte &1) — port line 298 ✅
- color -1 fill path = only when color-bit absent AND blend&0xF0==0 — port
  lines 217-238 ✅ (the `else if((blendMode&0xF0)==0)` exactly mirrors 0x6933D4).

### advanceNodeFrames @0x6B7E44
```
slot_idx=node+1392; t = *(node+8 child)+40  (CHILD eval time, NOT player+456)
fwd: while cur.idx<count-2 && t>=other.time: toggle; parse(cur, other.idx+1); swap
if cur.time > t: bwd: loop{ toggle; parse(other, cur.idx-1); if other.time<=t
  break; swap } else if !seeked return
merge slot0 if !node+346; merge slot1 if !node+882; node+1996|typeMask->findSource
```
PORT advanceNodeFramesLike_0x6B7E44: byte-exact incl. the SIGNED count-2 trap
(cast to int) and the `else if(!seeked) return` early-out. ✅

### advanceRootAndNodes @0x6B6ADC (4 streams)
```
1. layer(+1072): while cursor<count-2 && +456>=nextTime(+928): ++cursor;
   curTime(+920)=f[cur].time; nextTime=f[cur+1].time; if f[cur].type==1 ->
   +1093-gated align(+483)/sync(+1098 push)/action(push)
2. root(+548): while cursor<count-2 && +456>=nextTime(+584): ++cursor;
   +616=f[cur].content; +576=+584; +584=f[cur+1].time  (NO event gate)
3. var-track deque(+1312..1368): sub_6B786C/6B7A70 2-slot step  [DEFERRED]
4. node-deque(idx>=1): node+8 child->advanceNodeFrames; else inline FWD-only
   2-slot seek + per-node action(slot+22&4 -> push) + merge + findSource
```
PORT advanceRootAndNodesLike_0x6B6ADC: layer/root/node streams byte-exact;
var-track deferred (PLATFORM_BOUNDARY). ✅ for ported streams.

### rewindRootAndNodes @0x6B9A3C
```
1. layer rev: while count && curTime(+920)>+456: --cursor; refresh; type==1 gate
2. root rev: while curTime(+576)>+456: --cursor; +616=content; +584=+576;
   +576=f[cur].time
3. var-track rev: like fwd but parse(other, active.idx-1); POST-merge target
   DIFFERS: slot0 via +70 gate, slot1 via +126 gate -> sub_6B7A70(v19+104)  [DEF]
4. node-deque rev: child->advanceNodeFrames; else gate on
   slot[active]+328(clipStartTime) > +456, inline BWD seek + action + merge
```
PORT rewindRootAndNodesLike_0x6B9A3C: layer/root/node byte-exact incl. the +328
clipStartTime gate; var-track deferred. ✅ for ported streams.

### reseekTimelineCursors @0x6B86C8
```
layer scan i:0..count-1, DOUBLE-INC (++i in body when time<target) -> coarse
  overshoot; cursor=min(i,count-2); curTime/nextTime = (double)(int)time
  (INT-TRUNCATED); gate: +456==curTime && type==1 -> align/sync/action (CURSOR
  frame, not cursor+1)
root scan j:0..count-1 single-step; cursor=min(j,count-2); +616=content;
  +576=time; +584=f[cur+1].time
var-track reseed: per-track fwd-scan, v41=min(k,count-2); slot0=v41, slot1=v41+1
  UNCONDITIONAL merge; activeCursor(+8)=0  [DEFERRED]
per-node tail: Player_initNodeTimeline(player,node) for EVERY node idx>=1
pruneHM3 + sub_6B9650 aux-list(+280)  [DEFERRED]
```
PORT reseekTimelineCursorsLike_0x6B86C8: layer+root scans byte-exact incl. the
double-increment, int-truncation, and the 砖6 cursor-frame gate fix. ✅ for those.

## FINDINGS

| id | sev | func @ addr | local | one-line |
|----|-----|-------------|-------|----------|
| H-P1 | P1 | reseek per-node tail @0x6B91B0 | PlayerFrameStepping.cpp:587-596 | Binary runs Player_initNodeTimeline for EVERY node idx>=1; port only advanceNodeFrames the hasChild nodes, non-child init DEFERRED. The binary's per-node reseek init (seed slot cursors vs +456) is missing for non-child nodes -> firstFrame/loop-wrap seed for plain layer nodes is not reproduced. Real data-flow gap, but isolated (no re-arch). |
| H-N1 | note | all deque walks | nodes vector | The `dequeSize-1`/÷329/÷3 magic-division loop bounds are libstdc++ deque size()/index INLININGS, not source tokens. Port's flat std::vector is the correct source-level model; do NOT "fix" the -1 as off-by-one (cluster-D/F lesson). Verified the node-walk `-1 <= idx` == `idx < realNodeCount` against the 1-extra-block sentinel. ✅ |

## SUB-FUNCTION recursion status
- Player_parseFrame @0x6926B4 — ✅ ported & byte-verified (parseFrameLike).
- Player_mergeFrameContent @0x692AB0 — ✅ ported & byte-verified (mergeFrameContentLike).
- Player_advanceNodeFrames @0x6B7E44 — ✅ ported (advanceNodeFramesLike).
- sub_6B786C / sub_6B7A70 (var-track slot writers) — ❓ not ported; var-track
  deque DEFERRED (PLATFORM_BOUNDARY, opaque 160B records). Documented in
  Player_progress_containers.md §2.6 (already fully decompiled there).
- Player_pushSyncEvent_guess @0x6B6294 / pushActionEvent @0x6B638C — ❓ body
  DEFERRED (TJS dispatch); cursor/completion scalar effects reproduced. onAction
  param order recorded (tag-stream=void/action; per-node=label/action).
- Motion_Player_findSource (slot+348/+356) — ❓ gate reproduced, body DEFERRED.
- Player_initNodeTimeline @ reseek tail — ❌ non-child path not ported (H-P1).
- Player_pruneHM3_byNodeIdentity / sub_6B9650 aux-list — ❓ DEFERRED (cluster F/
  HM3 scope; already tracked elsewhere).

## PLATFORM_BOUNDARY segments (skipped, listed for reviewer)
1. Frame streams: TJS Array dispatch (iTJSDispatch2 + AddRef/Release) -> PSBList
   of PSBDictionary. Reason: no live iTJSDispatch2 motion tree in the port.
   (PlayerFrameStepping.h:40-46, PlayerFrameStep.h:24-32.) LEGITIMATE.
2. Var-track deque (player+1312..1368) 2-slot seed via sub_6B786C/6B7A70 over
   opaque 160B records — skipped in advance/rewind/reseek. Reason: opaque record
   format, no PSB stand-in. (PlayerFrameStepping.cpp:318/425/580.) LEGITIMATE
   (record internals ARE decompiled in Player_progress_containers.md but the deque
   itself isn't built in the P3 stand-in).
3. findSourceLike no-op body; sub_6B638C/6B6294 action/sync runner bodies — gate
   reproduced, dispatch body deferred. LEGITIMATE (deep TJS dispatch).
4. mesh/bezierPatch (mask 0x2000000) raw float-pair growing vector — DEFERRED.
   LEGITIMATE (render-buffer concern, not needed for slot-field unit coverage).
5. icon-handle (mergeFrameContent src/icon block, sub_A13878) — DEFERRED.
   LEGITIMATE (live icon table).
6. +280 aux singly-linked list (sub_6B9650) + pruneHM3 — DEFERRED. LEGITIMATE
   (opaque / cluster-F scope).

## FIX RECOMMENDATION (H-P1, optional, additive)
reseek's binary tail calls `Player_initNodeTimeline(player,node)` for EVERY node,
seeding that node's slot cursors against +456 (clampedEvalTime). The port's
`reseekTimelineCursorsLike_0x6B86C8` (PlayerFrameStepping.cpp:587-596) only runs
`advanceNodeFramesLike` for `hasChild` nodes. To close: add an inline non-child
seed for nodes where `!hasChild` (parse both slots around +456, like the
firstFrame seed reseek does for var-track slots). Since this is a unit-only
stand-in with no live wiring, the fix is additive and oracle-inert; safe to land
when a node-level reseek unit fixture exists. Until then the DEFERRED note +
the IDB comment @0x6B91B0 mark the gap. Do NOT fabricate a fixture for it.
