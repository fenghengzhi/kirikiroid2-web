---
name: hm3-init-value-from-node
description: Player_HM3_initValueFromNode@0x699510 source-field map — every node/slot offset it snapshots into PerNodeLayerState, the evaluateTimeline writer chain, and slot=parseSlot identity
metadata:
  type: project
---

Player_HM3_initValueFromNode @0x699510 (a1=node 2632B, a2=V=PerNodeLayerState).
Called from Player_resetMotionState_clearAndRebuild@0x6B2B7C loop3 (nodes type∈{0,2,3,4,7,8}, mask 0x19D), AFTER loop1 ran Player_evaluateTimeline on every node. So it is a POST-INTERPOLATION snapshot of node-base mirror fields + a few raw active-slot fields.

a2 is int*: a2[N]=byte 4N; (a2+N) as _OWORD*/_DWORD* index = byte 4N; ((QWORD*)a2+N)=byte 8N; ((long double*)a2+N)=byte 16N; ((BYTE*)a2+N)=byte N.

slot identity: cursor=*(int*)(node+1392); parseSlot[c]=node+320+536*c (parseFrame/mergeFrameContent write here). init/eval index `node+536*c+X` == parseSlot[c]+(X-320). So:
  node+536*c+344 = parseSlot+24 (done/invisible, parseFrame type==0->1)
  +340=parseSlot+20 (mask), +356=parseSlot+36 (src TJS var), +364=parseSlot+44 (blendMode), +376=parseSlot+56 ({ox,oy})

SOURCE FIELD -> V MAP (byte-verified from init reads + sub_6997F0 restore):
  V+0   node+28 nodeType int
  V+8   slot+356 src-var dispatch (refcount++)
  V+28  slot+340 frame mask int
  V+32  slot+344 done/invisible byte (early-return gate)
  V+44  (restore writes slot? init: dispatch at a2+11=V+44) -- a2+11 = the src dispatch store *(QWORD*)(a2+11). NOTE init stores src dispatch at V+44 not V+8 (a2+11=byte44). Header dispatch_8/dispatch_44 both exist; SRC dispatch is V+44.
  V+52  slot+364 blendMode int
  V+64  slot+376 {ox,oy} OWORD
  V+80/84/88/92  node+100/104/108/112 packed RGBA (eval<-slot+392..404)
  V+96  node+1576 opacity int (NOT node+408; that's the slot mirror)
  V+104 node+1512 {coordX,coordY} OWORD ; V+120 node+1528 coordZ qword
  V+128 node+1507 flipX byte ; V+129 node+1508 flipY byte
  V+136 node+1536 angle qword(double)        [init a2+17]
  V+144 node+1544 {scaleX,scaleY} OWORD       [init a2+9]
  V+160 node+1560 slantX long double          [init a2+10; spans 160..175 on Android 16B LD]
  V+544 node+1912 child-Player dispatch ttstr (type==3)
  V+672 node+2296 particle-Array dispatch ttstr (type==4)
  V+600/616/632/648 node+2224/2240/2256/2272 (4x16B) + V+664 node+2288 (8B), type==4 && slot+344==0
  V+568 node+2024 mesh-control-pts vector via Player_HM3_copyVector_meshControlPts@0x6996E8 (meshType node+2000==1)

WRITER: Player_evaluateTimeline@0x699AE4 writes ALL node-base mirror fields (node+100..112, +1507/1508, +1512/1520/1528, +1536/1544/1552/1560/1568, +1576). Hold path (LABEL_8 0x699b6c) copies from active slot finalized region (slot+416/424/432/440/441/448/456/464/472/480/392..408). Crossfade path lerps active vs other slot merged region (parseSlot+120 fx ... +160 sy), angle shortest-path 180-wrap.

HEADER MISMATCH (value_structs.h PerNodeLayerState): models V+136 as oword_136, V+152 ldouble_152, V+168 qword_168 (taken from sub_6997F0 RESTORE's overlapping bulk groupings). Init's actual field grouping is V+136 angle(qword) / V+144 scale(oword) / V+160 slantX(ldouble). Both views copy the same V+136..175 byte range; restore's grouping over-counts a V+168 field that is really the high half of the slantX long double. Semantic fields: angle@1536, scaleX@1544, scaleY@1552, slantX@1560, slantY@1568.
