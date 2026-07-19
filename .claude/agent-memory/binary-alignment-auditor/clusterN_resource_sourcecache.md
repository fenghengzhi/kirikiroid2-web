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
- **unloadAll body = 0x6A8CF8 / Motion_ResourceManager_unloadAll** — NOT
  0x6A8B94. IDA had merged it into the destructor; the boundary, names and local
  comments were corrected 2026-07-18. unloadAll clears only HashMap A; the
  destructor clears the layer-id set.
- **RM findSource sub_6AAB3C @0x6AAB3C**: TJS facade. split "/", src/blank gate, HashMap A
  lookup, module["source"][group]["icon"][icon], operator new(0x18) ObjSource facade.
- **ObjSource registrar @0x69CCB8**: 8 members (ctor/originX/originY/width/height/clip/
  drawLayer). DICT FACADE, no struct fields (MASTER "missing 6 members" INVERTED, header
  already correct). width/height getter: type==7?dict[k]:32 (default 32). clip builds
  Motion.Rect; local getClip() STUBs {} (P2 oracle-inert).
- **Player_findSource @0x6948E8** (renamed Motion_Player_findSource): the REAL render-
  source->texture resolver (NOT RM.findSource). Outer RM HashMap A is keyed by module;
  mapped record ctor sub_6EBCFC builds PSBFile + Win group->texture map + KRKR
  full-source-path->descriptor map. Miss -> raw
  sub_A0DE48(4wh,4) alloc + TVPReverseRGB(RGBA8)/A8L8-expand + Motion_createTextureFromPixels
  -> device vtbl+24 CreateTexture(fmt=4,mip=1) DIRECT GPU UPLOAD + AddRef. spec==1 krkr
  branch: a1+112=arg, if Player+909 -> sub_695DE8 decode-all.
- **SLA registrar @0x6ABFAC**: 5 members (ctor/absolute/targetLayer/clear=Player_resetRenderState_guess/assign=sub_6AC410).
- **PSB RL decode = sub_695DE8** (analysis/PSB_RL_Decompression_libkrkr2so.md), lives in
  PSB::/PlayerResource.cpp — OUTSIDE the 6 cluster-N files.

## Verdict: PARTIAL DEVIATION (updated 2026-07-18)
RM mapped-record topology and lifetime are now restored: both nested maps live with
the raw PSBFile, use ttstr unordered_map keys, and die on unload. Win/spec=2 source
navigation now reads raw PSBRawNode and mirrors RGBA8/A8L8 conversion exactly.
KRKR/spec=1 likewise reads raw nodes and mirrors all-group enumeration, both RL
formats, palette expansion and transparent-image handling. Its Web full-page atlas
upload primitive is a platform adaptation. The cluster remains PARTIAL because
ObjSource clip is a STUB and RuntimeSupport.cpp still hosts the broader MotionSnapshot
side graph, not because Player_findSource still consumes decoded pixels.

## Common port pattern in this cluster
Binary mapped record declaration order is PSBFile, Win texture map, KRKR descriptor
map; reverse destruction is KRKR -> Win -> PSBFile. Local `LoadedResourceRecord`
matches this and performs owning AddRef/Release. Do not describe raw-node migration as
a platform boundary.
