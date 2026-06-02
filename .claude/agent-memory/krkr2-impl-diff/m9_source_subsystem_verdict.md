---
name: m9-source-subsystem-verdict
description: M9 source subsystem fresh-decompile verdict — RM=SourceCache 12-member single class, ObjSource 0x18 dict facade, findSource double-hashmap+raw upload; phase-D per-vertex-color boundary needs draw-path verification
metadata:
  type: project
---

# M9 source subsystem — independent fresh-decompile verdict (2026-06-03)

Re-decompiled this session, confirming background note [[m9-source-subsystem]] and recent commits a074060/6259f76/8c6657e/3761a0b/07c4f05.

## Confirmed function addresses
- `Motion_ResourceManager_ncb_registerMembers` @0x6AB8BC — single register fn, 12 named TJS members: loadSource(sub_6A7BA8), clearCache(sub_6A8438), bufLayer(prop getter sub_6A84FC), load(ResourceManager_loadResource), unload(sub_6A959C), unloadAll(loc_6A8CF8), isExistMotion(sub_6A96F8), findMotion(sub_6A9ED4), findSource(sub_6AAB3C), random(sub_6AB56C), requireLayerId(sub_6AB694), releaseLayerId(sub_6AB750). RM and SourceCache are ONE class (no separate SourceCache register fn).
- `Motion_ObjSource_ncb_registerMembers` @0x69CCB8 — 6 members (originX/originY/width/height/clip/drawLayer). ObjSource = operator new(0x18) dict facade, NOT a fields struct. width getter @0x69D19C returns 32 when variant type!=7. "ObjSource missing 6 members" was INVERTED.
- `Motion_ResourceManager_findSource` @0x6AAB3C — "src"/"blank" token split → HashMap A(+88/+96) → dict["source"][grp]["icon"][ico] → ObjSource(operator new 0x18). Port RM.findSource is a placeholder (findLoaded path-map). ObjSource constructed NOWHERE in port.
- `Motion_Player_findSource` @0x6948E8 — spec(+224): ==2 win path = HashMap A + nested per-group ttstrHashMap keyed by PURE name, value=raw iTVPTexture2D* GPU handle, CPU buf freed immediately (sub_A0DE90). ==1 KAG callback. else player-self "findSource" script callback.

## Verdict on 07c4f05 phase-D per-vertex-color boundary (CAVEAT — recheck before acting)
- The boundary LABEL is defensible: it cites a concrete renderer-capability reason (OperateRect/OperateTriangles/GLVertexInfo/SetParameterColor4B carry only one scalar RGBA, no per-vertex attr), NOT an oracle-visibility excuse — so it passes CLAUDE.md's "platform boundary needs concrete technical reason" test IF those renderer signatures are as stated.
- BUT the ResourceManager.h:18-36 comment's M9-side justification overstates M9: it says the binary "recombines the 4-corner gradient as per-vertex vertex colors in its GPU draw," yet NEITHER M9 fn (findSource @0x6948E8 / @0x6AAB3C) applies any 4-corner/per-vertex color. findSource is a pure name→single-texture cache (key=pure name, confirmed). The per-vertex color, if it exists, lives in the DOWNSTREAM Layer/mesh draw path that consumes a1+24 texHandle — NOT decompiled this session.
- **How to apply:** before treating phase D as boundary-blocked OR as implementable, a follow-up MUST decompile the draw path that consumes the findSource texHandle (a1+24) to ratify per-vertex-color absence. Do NOT cite findSource as the per-vertex-color evidence; the load-bearing evidence is in the un-verified draw path. Renderer signatures (RenderManager.h OperateRect/OperateTriangles/SetParameterColor4B, GLVertexInfo) were NOT re-verified this session.
