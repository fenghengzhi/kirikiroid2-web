---
name: node46-joinTarget-correction
description: node+46 是 joinTarget 不是 visible 字节；HM3 loop3/loop2 都用它当 gate；sub_6997F0 restore 字段图
metadata:
  type: project
---

node+0x2E(46) = **joinTarget** (PSB `"joinTarget"` bool &1)，**不是** "visible/active 字节"（本地 PlayerFrameProgress.cpp 注释长期误标，已证伪需更正）。

**Why:** 钉死 pruneHM3 loop2 per-node restore 的三处缺口时发现。
**How to apply:** 移植 loop3(HM3 populate)/loop2(HM3 restore) 时必须加 joinTarget gate。

证据：
- 唯一写点 `Player_initNodeFields@0x6b3ef0`: `STRB W8,[X19,#0x2E]; W8=propGetBool("joinTarget")&1`，tree-build 时一次。node+47=groundCorrection, node+24=coordinate(int)。
- 读点1 `resetMotionState loop3 @0x6b2dcc/0x6b2ddc`: gate = joinTarget!=0 AND nodeType∈{0,2,3,4,7,8}(0x19D)。本地 loop3 只测 mask，缺 joinTarget gate = divergence。
- 读点2 `pruneHM3 loop2 @0x6b855c`: gate1=joinTarget!=0, gate2=HM3.V+16==node.nodeType → restore+findSource+erase。

**sub_6997F0 = Player_HM3_restoreValueToNode**(已 rename, @0x6997F0): V→Node 逆操作。common block 门控 `slot+344(done)==0 && V+32(doneFlag)==0`, 写 slot eval-result 区:
slot340=V28(contentMask) /364=V52(bm) /376=V64{ox,oy} /392..404=V80..92(color) /408=V96(opa) /416=V104{cx,cy} /432=V120(cz) /440/441=V128/129(flip) /448=V144{sx,sy} /464=V160(slantX,ldouble) /480=V168(slantY)。
独立门控: meshType==1→slot640=V568; type3→node1912=V544(child dispatch); type4→node2296=V672 + (V32==0时)memcpy(slot744,V600,0x48)粒子块。

**sub_699510 = Player_HM3_initValueFromNode** snapshot Node→V(688B HM3 value):
4 DEFERRED 真缺口: V+28 contentMask(本地ClipSlot不存frame mask) / V+44 srcDispatch(本地src=std::string非dispatch) / V+544 type3 child / V+600..664+V672 type4粒子(本地evaluateTimeline type4分支未移植)。
其余20字段本地已建模(interpolatedCache+activeSlot)。

移植安全切入点: 先只做 joinTarget双gate + common标量还原(跳contentMask/src/particle), type-0节点即正确。
