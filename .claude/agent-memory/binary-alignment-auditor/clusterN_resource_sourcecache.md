---
name: clusterN-resource-sourcecache
description: 簇N RM/SourceCache/ObjSource/Player_findSource 二进制地址↔语义权威映射 + 2026-06-07审计结论
metadata:
  type: project
---

# Cluster N (ResourceManager / SourceCache / ObjSource / SLA) — addr map + 2026-06-07 audit

Authoritative addr↔semantics (decompiled 2026-06-07):

- **RM ctor sub_6A88CC @0x6A88CC**: RM : public SourceCache CONFIRMED. First insn
  (0x6a88f8) calls base ctor sub_6A78F4 @offset0. RM-own fields from +88: HashMap A
  (+88 bucket array / +96 count, std::unordered_map via _M_next_bkt(10)), motion-list
  +104, RandomGen +144, layerId Rb_tree +168 (pre-inserts {0}, inert), counter +216
  (=0x100000001 low32=1), spec int +224 (1=krkr 2=win).
- **SourceCache ctor sub_6A78F4 @0x6A78F4**: ONLY caller is RM ctor (no standalone
  instance). Seeds base: +20 primaryLayer, +40 bufLayer (Layer variant), +64 layerType,
  +72/+80 intrusive layer-list sentinel.
- **RM registrar @0x6AB8BC** (14 members) & **SourceCache registrar @0x6A85A8** (4)
  share SAME callbacks: loadSource=sub_6A7BA8, clearCache=sub_6A8438,
  bufLayer(prop-ro)=sub_6A84FC (reads a1+40). = C++ inheritance signature.
- **unloadAll body = loc_6A8CF8 / Motion_ResourceManager_unloadAll** — NOT 0x6A8BBC
  (that's just the canary load addr). RM.h:147 + RM.cpp:372 comments have this WRONG;
  flagged for fix.
- **RM findSource sub_6AAB3C @0x6AAB3C**: TJS facade. split "/", src/blank gate, HashMap A
  lookup, module["source"][group]["icon"][icon], operator new(0x18) ObjSource facade.
- **ObjSource registrar @0x69CCB8**: 8 members (ctor/originX/originY/width/height/clip/
  drawLayer). DICT FACADE, no struct fields (MASTER "missing 6 members" INVERTED, header
  already correct). width/height getter: type==7?dict[k]:32 (default 32). clip builds
  Motion.Rect; local getClip() STUBs {} (P2 oracle-inert).
- **Player_findSource @0x6948E8** (renamed Motion_Player_findSource): the REAL render-
  source->texture resolver (NOT RM.findSource). DUAL hashmap: RM HashMap A (FNV(group))
  -> PSB group dict; + NESTED ttstr->texture map inside dict value (v24+1 base,
  v24[2] count, Motion_ttstrHashMap_findNode/findOrInsert keyed by name). Miss -> raw
  sub_A0DE48(4wh,4) alloc + TVPReverseRGB(RGBA8)/A8L8-expand + Motion_createTextureFromPixels
  -> device vtbl+24 CreateTexture(fmt=4,mip=1) DIRECT GPU UPLOAD + AddRef. spec==1 krkr
  branch: a1+112=arg, if Player+909 -> sub_695DE8 decode-all.
- **SLA registrar @0x6ABFAC**: 5 members (ctor/absolute/targetLayer/clear=Player_resetRenderState_guess/assign=sub_6AC410).
- **PSB RL decode = sub_695DE8** (analysis/PSB_RL_Decompression_libkrkr2so.md), lives in
  PSB::/PlayerResource.cpp — OUTSIDE the 6 cluster-N files.

## Verdict: PARTIAL DEVIATION
Topology (RM:SourceCache inherit, findSource chain, ObjSource facade) all re-verified
CORRECT in headers. Genuine deviations: (5) Player_findSource dual-hashmap + raw GPU
upload -> port std::list<Entry>+shared_ptr<bitmap>+lazy CreateTexture2D (🔧, parked
phase-D texture-topology boundary); (2) unloadAll addr comment wrong; ObjSource clip STUB.
RuntimeSupport.cpp = port MotionSnapshot aux-model host layer (no 1:1 binary fn).

## Common port pattern in this cluster
Binary uses KiriKiri intrusive hashmaps/lists + raw new/AddRef/Release + direct device
GPU upload; port substitutes std::list/unordered_map + shared_ptr + RenderManager
texture abstraction. Tolerated as documented container-choice / platform boundary, but
the DUAL-hashmap topology (group-map + per-dict nested map) is NOT platform-forced.
