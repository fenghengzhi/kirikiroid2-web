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
  (+88 bucket array / +96 bucket count / +104 `_M_before_begin._M_nxt` global
  node-chain head / +112 element count, `std::unordered_map` via
  `_M_next_bkt(10)`), RandomGen +144, layerId Rb_tree +168 (pre-inserts {0},
  inert), counter +216 (=0x100000001 low32=1), spec int +224 (1=krkr 2=win).
  The old “+104 independent motion-list” label was an STL-layout misread.
- **SourceCache ctor sub_6A78F4 @0x6A78F4**: ONLY caller is RM ctor (no standalone
  instance). Seeds base: +20 primaryLayer, +40 bufLayer (Layer variant), +60 current
  cache bytes=0, +64 cache byte limit (the second constructor argument), +72/+80
  libstdc++ `std::list<Entry>` sentinel links. This is source-level `std::list`,
  not a hand-written intrusive list. The older `+64 layerType` label was
  disproved by trim@0x6A6B08 and corrected 2026-07-23. Local NCB construction
  uses the parameterized RM ctor and seeds this base; Player aliases the RM base,
  not a separate standalone SourceCache object.
- **RM registrar @0x6AB8BC** (constructor + 12 exposed members) & **SourceCache registrar @0x6A85A8** (4)
  share SAME callbacks: loadSource=sub_6A7BA8, clearCache=sub_6A8438,
  bufLayer(prop-ro)=sub_6A84FC (reads a1+40). = C++ inheritance signature.
- **unloadAll body = 0x6A8CF8 / Motion_ResourceManager_unloadAll** — NOT
  0x6A8B94. IDA had merged it into the destructor; the boundary, names and local
  comments were corrected 2026-07-18. unloadAll clears only HashMap A; the
  destructor clears the layer-id set.
- **RM findSource sub_6AAB3C @0x6AAB3C**: split "/", src/blank gate, HashMap A
  lookup, then navigate the mapped `PSBFile` raw root through fixed strict and dynamic
  has+strict getters. A hit allocates a 0x18-byte ObjSource containing the retained raw
  owner/node pair plus a null lazy texture and creates its adaptor with
  `sticky=false/err=false`; adaptor failure leaks the just-allocated object.
- **ObjSource registrar @0x69CCB8**: constructor plus 6 exposed members
  (originX/originY/width/height/clip/drawLayer). The instance is a RAW-NODE FACADE,
  not a dict facade: strict getters, non-dictionary width/height default 32, try-gated
  clip, lazy raw/RL/palette texture materialisation, Layer assignment and
  texture-before-owner destruction are all restored locally.
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

## Verdict: AUDITED SITES ALIGNED + PLATFORM BOUNDARY (corrected 2026-07-23)
RM mapped-record topology and lifetime are now restored: both nested maps live with
the raw PSBFile, use ttstr unordered_map keys, and die on unload. Win/spec=2 source
navigation now reads raw PSBRawNode and mirrors RGBA8/A8L8 conversion exactly.
KRKR/spec=1 likewise reads raw nodes and mirrors all-group enumeration, both RL
formats, palette expansion and transparent-image handling. Its Web full-page atlas
upload primitive is a platform adaptation. ObjSource no longer uses the former
MotionSnapshot/TJS side graph: clip, ensureTexture and drawLayer consume raw nodes and
the local object lifetime matches the binary. The former `CLOSED` verdict was too
broad: fresh xrefs found the shared `sub_695DE8` render-time caller and direct
PreparedRenderItem→SourceState alias that the snapshot-based local chain had missed.
Those sites are now restored, but this document must not be cited as a global 100%
proof beyond the addresses actually audited.

### 2026-07-23 correction
`sub_6F1060@0x6F1060` is `sub_695DE8`'s second caller; the item stores a
`SourceState*` at `sub_6C2334@0x6C360C`, and `sub_6ADFBC@0x6AE154..0x6AE188`
rereads that same object's rect after the texture getter. The old local
`sourceObject/sourceTexture/sourceRect` snapshots therefore cannot be authoritative
for this chain. The shared helper, alias and post-getter reread are now restored,
together with atlas `{x,y,right,bottom}`, difference-derived dimensions,
branch-local resource lookups, try-pal, and the single scratch/size-slot behavior.
The two common-render callers do not share one getter: `0x6D5C68` supplies
`sub_6F67CC` (direct current texture), whereas `0x6ADE24` supplies `sub_6F1060`.
The latter and Private `0x6DE738` both send the post-atlas `SourceState.object`
through `0x6C1B70`; the local route now writes Player's persistent descriptor and
color Dictionaries and invokes the inherited NCB
`loadSource(source,descriptor)`. The separate Web-only `Player.loadSource(name)`
helper remains extra compatibility code, but it is not the NCB method and does
not create an alias cache topology.

`trim@0x6A6B08` further proves the cache-size boundary: a miss trims only when
current bytes exceed the limit. It scans newest-to-oldest and independently
keeps an entry when signed `(kept+weight) <= uint32(limit*99/100)`, so the result
is a greedy subsequence rather than necessarily a prefix. The new node then
stores `4*width*height`, increments current bytes and is inserted at the front.
Same-color hits do not move; a color mismatch rebakes the same Layer and uses
`push_front(copy)+erase(old)`. `clearCache@0x6A8438` dispatch-invalidates each
Layer and resets current bytes to zero.

## Common port pattern in this cluster
Binary mapped record declaration order is PSBFile, Win texture map, KRKR descriptor
map; reverse destruction is KRKR -> Win -> PSBFile. Local `LoadedResourceRecord`
matches this and performs owning AddRef/Release. Do not describe raw-node migration as
a platform boundary.
