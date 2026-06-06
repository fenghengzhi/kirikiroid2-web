---
name: hm3-hm4-chain-prunehm3-done
description: HM3/HM4 populate+consume already ported (premise falsified); reseek STEP5 tail pruneHM3 loop1 now ported, sub_6B9650 still gapped on node+408
metadata:
  type: project
---

HM3/HM4 populate + consume + maintenance chain — 2026-06-06.

**PREMISE CORRECTION (the task brief was partly falsified by fresh decompile):**
The brief said HM3(_perNodeLayerStateMap +1184)/HM4(_variableSnapshotMap +1240)
are 恒空 because populate+consume+maintain were all unported. FALSE for populate
+consume:
- HM4 populate = resetMotionState loop2 @0x6B2D40 → ALREADY ported
  (PlayerFrameProgress.cpp resetMotionStateLike_0x6B2D3C, the
  `_variableSnapshotMap[item.cascadeKey]=item.value` loop). Landed earlier.
- HM3 populate = resetMotionState loop3 @0x6B2DF8 + HM3_initValueFromNode
  @0x699510 → ALREADY ported (loop3 + hm3InitValueFromNodeLike_0x699510,
  PARTIAL snapshot — V+28 contentMask/V+44 srcDispatch/type3-4 child dispatch
  DEFERRED). Landed in commit a5de9fd.
- HM4 consume = getVariable HM4-first cascade @0x6CD23C → ALREADY ported
  (PlayerVariable.cpp getVariable, `_variableSnapshotMap.find(label)`).
- resetMotionStateLike wired at PlayerTimeline.cpp when (flags&PlayFlagJoin).
Stale Player.h comments saying loop1/loop3 DEFERRED + "HM3 unread/inert" were
CORRECTED in-place this pass.

**Function addresses (fresh-decompiled, authoritative):**
- resetMotionState_clearAndRebuild @0x6B2B7C (loops at 2BDC/2C64/2D68), caller
  Player_playImpl @0x6B2284.
- HM3_initValueFromNode @0x699510 (688B node→V snapshot, leaf). loop3 gate =
  node+46 (joinTarget bool, FIRST @0x6b2dcc `if(!node+46)continue`) AND nodeType
  mask 0x19D = bits{0,2,3,4,7,8} (@0x6b2df8). [CORRECTED 2026-06-06: node+46 is
  joinTarget (PSB bool, writer Player_initNodeFields @0x6b3ef0), NOT a "visible
  byte" — that label was falsified by fresh decompile. node+46 NOW MODELED on
  MotionNode (joinTarget) + the loop3 gate is NOW PORTED.]
- reseekTimelineCursors @0x6B86C8; STEP5 tail = pruneHM3 @0x6B9234 +
  Player+280 aux walk @0x6B9248.
- pruneHM3_byNodeIdentity @0x6B826C: loop1 gate a1[158]=Player+1264 (HM4 bucket
  cnt), loop2 gate a1[151]=Player+1208 (HM3 elem cnt), tail clearHM3_HM4 @0x6B80E4.
- clearHM3_HM4 @0x6B80E4: HM4 node[1]=KEY ttstr release (NOT value) → confirms
  R-M4 (HM4 value @+16 is RAW double, not owning tTJSVariant*). HM2/HM4 node 0x20B
  {next, key@+8, value@+16, hash@+24}; upsert @0x686944 returns node+16;
  find @0x686B6C returns before-ptr or null.

**WHAT GOT PORTED THIS PASS:** pruneHM3ByNodeIdentityLike_0x6B826C
(PlayerFrameProgress.cpp), wired into reseekTimelineCursors STEP5 (replacing the
fully-DEFERRED block). All 5 reseek call sites (firstFrame 0x6C1160/0x6C131C +
loop-wrap 0x6C1488/0x6C1428/0x6C13B8) now route through it — faithful since binary
reseek ALWAYS ends with pruneHM3.
- loop1 FULLY ported: HM4 snapshot → active var-track slot.value. item+56*cursor+72
  = slot[cursor].value (VarTrackSlot +24); +68 = typeZeroFlag (+20). key = item+0
  cascadeKey. `find()!=end()` == binary `*v27!=0` node-exists.
- loop2: per-node restore NOW PORTED 2026-06-06 (was DEFERRED). Iterate _nodes
  k=1.., build path-key, find HM3; gate `node.joinTarget && V.nodeType==node.nodeType`
  (@0x6b855c+0x6b8574); restore common scalar block (hm3RestoreValueToNodeLike_0x6997F0
  @0x6997F0) + erase matched entry (@0x6b8644); terminal clearHM3_HM4. sub_6997F0
  restore writes ACTIVE-SLOT MERGED fields (NOT a "finalized region"): the binary
  node+536*idx+{340..480} resolve to slot-relative +{20,44,56,72,88,96,112,120,128,
  136,144,160} = the SAME merged parse fields evaluateTimeline copy-branch reads.
  Gate `!slot.done && !V.doneFlag` (@0x6998a4). Mapped to port ClipSlot:
  blendMode/ox/oy/packedColors/opacity(/255)/x/y/z/flipX/flipY/angle/scaleX/scaleY/
  slantY. SKIPS slot+152 slantX (binary writes slot+160 slantY but not slot+152 —
  faithfully skipped). 3 sub-paths NOW PORTED 2026-06-06 (Stage 4, was DEFERRED):
  (1) contentMask: ClipSlot.contentMask(int, slot+340=parseSlot+20=content["mask"]
      via parseFrame 0x6926E8 Motion_propGetInt, bit0x40000 gates "act"; default 0
      from resetFrameSlot) + V.contentMask(V+28). init @0x699654 V+28←slot+340 (in
      common block, doneFlag==0 only); restore @0x6998b8 slot+340←V+28.
  (2) type-3 child: V+544 CORRECTED ttstr→tTJSVariant childPlayerSnapshot. sub_A0FB64
      (0xA0FB64)=tTJSVariant copy-assign (switch type@+16, AddRef object), NOT ttstr.
      init @0x699598 V+544←node+1912(node.childPlayerVar) then sub_A0F790 clear
      node+1912; restore @0x699844 node+1912←V+544 then sub_A0F790 clear V+544.
      sub_A0F790=tTJSVariant Clear/destruct. Maps to tTJSVariant op= + .Clear().
  (3) mesh restore: ClipSlot.meshControlPoints(vector<float>, slot+640). copyVector
      0x6996E8=std::vector 8B-elem copy (>>3); node+2024 elem={float x,float y}
      (sub_6BC4F0 vst2q). ASYMMETRY: eval @0x699c08 node+2024←slot+640; init @0x699588
      V+568←node+2024; restore @0x699828 slot+640←V+568 (node-base read, slot-base
      write). Port keeps flat float view (matches existing node.meshControlPoints).
  ALSO added doneFlag early-return to init (@0x6995cc) — was missing; common block now
  gated doneFlag==0 matching binary (snapshots run first, regardless). web+wasmtime
  clean, m2logo93+yuzulogo243 PASS bit-identical (ORACLE-INERT: logos no Join→HM3
  empty→init/restore never run with data; non-regression guard, honest gap).
  STILL DEFERRED: findSource(0x6b85a0 — src=std::string not iTJSDispatch2/icon pair,
  PLATFORM_BOUNDARY), srcDispatch V+44(same boundary), type-4 particle (node+2296←V+672
  + slot+744←V+150 0x48B — needs mergeFrameContent prt block decompile for slot+424..488
  source + slot+744 consumer xref, both unconfirmed; evaluateTimeline type-4 branch
  unported = no V+600..664 source).

**STILL GENUINELY GAPPED (prerequisite, NOT oracle-inert excuse):**
- STEP5 (B) Player+280 aux walk → sub_6B9650 @0x6B9650 (builds HM1 entry+48 =
  EvalCascadeState aux vector<MotionNode*>, gate entry+40 weight==0→skip, dedup via
  sub_6BA5B4/sub_9B1ED0, push nodeType∈{3,4}). NOT ported because its ONLY reader,
  the bindParameter consumer loop @0x6C4978, ramp-writes into node+408 (a
  std::map<ttstr,ControllerRamp>, ramp fields +8 intcast/+16 rangeStart/+24
  rangeEnd/+32 factor/+40 out/+48 mode) which is NOT modeled on port MotionNode
  (populated by unported per-node controller-frame parse, not buildNodeTree
  @0x6B4A6C). Porting builder alone = "vector no one reads" = inverse port-invent.
  Faithful order: model node+408 + port @0x6C4978 ramp consumer FIRST.

**VERIFICATION:** web/debug + krkr2_wasmtime_guest clean. motion_playback
--only-structural: m2logo 93f + yuzulogo 243f PASS bit-identical. ORACLE-INERT for
logos (no PlayFlagJoin → HM3/HM4 stay empty; no loop-wrap → reseek STEP5 with
populated data never runs) = non-regression guard, honest gap (no Join/loop fixture).
