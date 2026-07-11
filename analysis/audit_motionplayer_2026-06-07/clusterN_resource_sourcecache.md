# CLUSTER N — ResourceManager / SourceCache / ObjSource / SLA / GLL / RuntimeSupport

> Date: 2026-06-07. Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64).
> Scope: ResourceManager.cpp/.h, SourceCache.cpp/.h, PrivateMotionGLL.cpp/.h,
> SeparateLayerAdaptor.cpp/.h, RuntimeSupport.cpp/.h, MotionTraceWeb.cpp/.h.
> Protocol: decompile -> pseudocode -> local compare -> 6-dim verdict. IDB
> renamed (Motion_Player_findSource) + 2 comments + idb_save OK.

## Verdict: PARTIAL DEVIATION (container/lifetime divergences; architecture topology now CONFIRMED correct in headers)

The four priority items the prompt flagged are ALL re-verified against fresh
decompiles. The header notes (esp. the C-1 2026-06-07 RM:public SourceCache
correction) are ACCURATE. The remaining gaps are the known, documented
container-choice + GPU-upload platform boundaries, plus a few real STUBs.

---

## 1. RM : public SourceCache inheritance — CONFIRMED (item ①)

- RM ctor `sub_6A88CC` @0x6A88CC: FIRST insn (0x6a88f8) calls SourceCache base
  ctor `sub_6A78F4` on `a1`@offset0, THEN inits RM-own fields from +88.
- SourceCache ctor `sub_6A78F4`: seeds base subobject — +20 primaryLayer
  (sub_A0FB64 from owner.PropGet "primaryLayer"), +40 bufLayer (Layer variant,
  created via global Layer class CreateNew(owner, primaryLayer)), +64 layerType
  (a3), +72/+80 intrusive layer-list head/tail sentinel (both = a1+72).
- `sub_6A78F4` has EXACTLY ONE caller (0x6a88f8, xref-confirmed earlier). No
  standalone SourceCache instance — it is only RM's [0,88) base subobject.
- NCB proof: RM registrar @0x6AB8BC and SourceCache registrar @0x6A85A8 bind the
  SAME callback addresses for the 3 inherited members:
  loadSource→`sub_6A7BA8`, clearCache→`sub_6A8438`, bufLayer(prop-ro)→`sub_6A84FC`.
- bufLayer getter sub_6A84FC = `sub_A0F5E0(out, a1+40)` (base Layer variant). NOT
  a ttstr name.

Local: `class ResourceManager : public SourceCache` (RM.h:117). Default base ctor
runs first (mirrors order). The base _owner/_bufLayer left empty — GAP documented
(RM.cpp:57): binary base ctor seeds owner/bufLayer Layer from the RM dispatch;
wiring it is the larger P3-B ownership refactor, and the RM NCB instance's
inherited members are not exercised by any fixture (Player's standalone
`_sourceCacheNative` is the live path). Topology aligned; field-seed deferred.
Status: ARCH OK, field-seed gap (P3, oracle-inert).

## 2. RM NCB member set — CORRECTED vs header (minor)

RM registrar @0x6AB8BC binds 14 members (ctor dispatch + 13):
loadSource, clearCache, bufLayer (all 3 inherited), load(=ResourceManager_loadResource),
unload(=sub_6A959C), unloadAll(=**loc_6A8CF8**), isExistMotion(=sub_6A96F8),
findMotion(=sub_6A9ED4), findSource(=sub_6AAB3C), random(=sub_6AB56C),
requireLayerId(=sub_6AB694), releaseLayerId(=sub_6AB750).

  *** CORRECTION (2026-07-12): IDA merged two adjacent functions. The body at
  0x6A8BBC is the ResourceManager destructor; that is where the +168 layerId
  RB-tree, +144 RandomGen and SourceCache base are destroyed. The actual
  unloadAll starts at the independent prologue `0x6A8CF8` and only walks +104,
  memsets buckets@+88, and zeros +104/+112. Moreover +104 is not a separate
  motion-list: it is HashMap A's libstdc++ `_M_before_begin._M_nxt`, the global
  node chain belonging to the same unordered_map rooted at +88.

Local: load/unload/findLoaded/findSource/isExistMotion/findMotion/
requireLayerId/releaseLayerId/unloadAll are backed by the aligned containers;
random remains a STUB.

## 3. RM findSource @0x6AAB3C — CONFIRMED (TJS facade path, item ②/③)

```
split name by "/" (sub_697D34); !pieces[0] -> void
if pieces[0] != "src":
  if pieces[0] != "blank" -> void
  blank: split pieces[1] by ":" -> width/height/originX/originY ints;
         build dict{width,height,originX,originY, blank=1(int)}; return it
src: FNV-hash(name) -> sub_6EB8F4(this+88 HashMap A, hash%this+96, name) -> module
  miss -> void
  module["source"].hasKey(group)? else void; module["source"][group]["icon"][icon]
  hasKey(icon)? else void
  hit: operator new(0x18) {qword[0]=iconEntry dict variant (AddRef), [1]=?, [2]=0}
       wrap as TJS obj via NCB class sub_6EC124; return it
```
Local RM::findSource (RM.cpp:238) mirrors this 1:1 in C++ structure: splitTtstr,
src/blank gate, findLoaded(path) (container-divergent HashMap-A substitute),
module["source"][group]["icon"][icon] via psbGet hasKey gates, `new ObjSource(iconEntry)`
+ ncbInstanceAdaptor::CreateAdaptor. Container deviation = HashMap A vs
unordered_map<ttstr,V>; documented. Status: ARCH OK (container choice tolerated).

## 4. ObjSource @0x69CCB8 — CONFIRMED dict facade (item ③)

8 NCB members: ctor, originX(sub_69D014), originY(sub_69D0D8), width(sub_69D19C),
height(sub_69D27C), clip(sub_69D35C prop-ro), drawLayer(sub_69D6D8 method).
- width getter: `if AsType(a1)==7(tvtObject) return dict["width"]; else return 32`.
  Confirms default 32 for width/height. originX/Y default 0.
- ObjSource operates DIRECTLY on the source variant (a1=qword[0]) — it IS a thin
  dict facade, NO struct fields. MASTER's "ObjSource missing 6 members" is
  INVERTED (header SourceCache.h:116 already records this correctly).
Local ObjSource (SourceCache.h:131): readInt(key,dflt) with tvtObject gate +
default 32/0. MATCHES. clip getter: binary builds a Motion.Rect dispatch from
left/top/right/bottom when type==7 & hasKey("clip"); local `getClip()` returns {}
unconditionally — REAL STUB DEVIATION (oracle-inert: findSource ObjSource path
has no internal C++ caller; unit tests exercise Player::findSource). P2.

## 5. Player_findSource @0x6948E8 — the real render-source resolver (item ②/③, CRITICAL)

This is NOT RM.findSource. It is the Player-internal source->texture resolver
(renamed Motion_Player_findSource). It is the function the prompt meant by
"findSource 0x6948E8 (RM 双 hashmap + raw upload vs list+shared_ptr)":

```
RM = Player+636 dispatch -> native @ *(prop+8)=v10; spec = v10+224 (1=krkr 2=win)
arg !"blank" prefix:
 spec==2 (win): HashMap A lookup sub_6EB8F4(v10+88, FNV(group)%v10+96) -> group dict
   nested map: v24+1 base, v24[2] bucketcount, ttstrHashMap_findNode(name)
     hit: a1+24 = cachedTexture
     miss: read pixel/w/h/type from PSB dict; raw aligned alloc sub_A0DE48(4*w*h,4);
           RGBA8 -> TVPReverseRGB; A8L8 -> 2->4 byte expand;
           Motion_createTextureFromPixels -> device vtbl+24 CreateTexture(buf,
             pitch=4w, w, h, fmt=4, mip=1)  [DIRECT GPU UPLOAD]
           store via ttstrHashMap_findOrInsert + AddRef; a1+24=tex
   then read icon[icon] originX/Y/w/h/clip into a1+32..108
 spec==1 (krkr): a1+112 = arg; if Player+909: sub_695DE8 (decode-all PSB path)
```

ARCHITECTURE: **DUAL hashmap** (RM HashMap A keyed by FNV(group-name) -> PSB
group dict; + a NESTED intrusive ttstr->texture map living INSIDE each dict
value, keyed by source name) + **raw new/aligned-alloc + direct device
CreateTexture GPU upload at resolve time** + AddRef/Release lifetime.

Local equivalent = SourceCache + `resolveMotionSourcePathLike_0x6948E8`
(SourceCache.cpp:177) + loadRenderSourceTextureByName: uses a SINGLE
`std::list<Entry>` keyed (key,blendMode) + `std::shared_ptr<tTVPBaseBitmap>`
backing + LAZY `TVPGetRenderManager()->CreateTexture2D`. This is the named
divergence:
 - container: dual-hashmap (group-map + per-dict nested ttstr-map) -> one std::list.
 - lifetime: raw AddRef/Release + intrusive nodes -> shared_ptr RAII.
 - upload: direct device CreateTexture at parse -> deferred RenderManager texture.
Status: ARCH DEVIATION (🔧). PLATFORM_BOUNDARY-adjacent (web render stack has no
per-dict intrusive map and uses RenderManager texture abstraction, not a raw
GL device vtbl+24 upload), but the dual-hashmap topology itself is NOT a platform
necessity — a faithful port would keep two maps. Documented; deferred under
phase-D texture-topology boundary (SourceCache.cpp / RM.h:50-97 notes).

## 6. SourceCache loadSource/clearCache — CONFIRMED base behavior

- loadSource sub_6A7BA8: reads arg dict "key"/"src"/"blendMode"/"color"[4];
  matches +72 intrusive layer-list node by (key sub_A10428, src ttstr wcscmp,
  blendMode @node+64); HIT -> if color (node+68..80) changed, rewrite + re-bake
  via sub_6A6BE0 + clone-to-front (LRU); MISS -> evictLRU + create Layer
  (global Layer CreateNew(owner, primaryLayer)) + sub_6A6BE0 bake + insert,
  +60 += widthLike. Output = node+36 Layer variant.
- clearCache sub_6A8438: walks +72 list, releases each Layer image (vtbl+112),
  frees nodes, resets +72/+80 sentinel + +60=0. Does NOT touch hashmaps.
Local SourceCache.cpp: std::list<Entry>, findEntry by (key,blendMode) + splice-
to-front LRU + per-color re-bake (applyPackedCornerTintLike_0x6A7518 reproduces
the 128/255-divisor bilinear bake). clearCache clears _entries. Container
divergence (intrusive list -> std::list) + bitmap-bake-vs-Layer-dispatch; the
(key,blendMode) match + LRU + color re-bake DATA FLOW matches. ARCH OK modulo
container.

## 7. SeparateLayerAdaptor @0x6ABFAC — surface CONFIRMED

5 members: ctor, absolute(prop sub_6AC260/sub_6AC258), targetLayer(prop
sub_6AC274/sub_6AC268), clear(=Player_resetRenderState_guess), assign(=sub_6AC410).
Local SLA.h exposes absolute/targetLayer/clear/assign + factory. Surface matches.
Layout note (+0 owner, +20 targetLayer, +40 privateTarget, +56 = vt of +40)
documented. Deeper SLA render chain (0x6D5658/0x6D5948/0x6DE738) is cluster I/SLA
chain (analysis/SLA_Rendering_Chain_libkrkr2so.md) — not re-decompiled this pass.

## 8. PrivateMotionGLL / RuntimeSupport — port host layers (no 1:1 binary fn)

- PrivateMotionGLL.cpp: the port's render-target child layer + render-item queue
  (clear/append/size Like_0x6DE738). It models the binary PrivateMotionGLL
  internal Layer subclass behavior; render-item input struct mirrors the
  0x6DE738 vertex/color/sourceRect packing. Not a single binary function — a
  composed port adapter. Out of priority scope; not re-decompiled.
- RuntimeSupport.cpp (motion::detail, 1723 lines): Web-port PSB-snapshot model
  (loadMotionSnapshot @1320 builds a `MotionSnapshot` shared_ptr aux model;
  tag/priority frame caches; timeline stepping; logo-chain trace diagnostics).
  This is the LARGEST architectural divergence in cluster N: the binary loads
  PSB directly into TJS dicts (load -> sub_695DE8 decode), while the port builds
  an auxiliary snapshot struct for the web render path. It is the port's
  resource-model platform layer (no 1:1 binary fn). PSB RL decompression itself
  (sub_695DE8, analysis/PSB_RL_Decompression_libkrkr2so.md) lives in PSB::
  parsing / PlayerResource.cpp — OUTSIDE these 6 files (cross-cluster). Item ④
  decode algorithm reference verified against the doc; consumer is here only.
- MotionTraceWeb.cpp: web console trace shim. Pure platform.

## Deviation summary

| # | Item | Binary | Local | Status |
|---|------|--------|-------|--------|
| 1 | RM:SourceCache | public inherit, base ctor seeds owner/bufLayer | inherit OK, base fields empty | P3 ARCH OK / field gap |
| 2 | unloadAll addr | body @0x6A8CF8 | comment says 0x6A8BBC | DOC ERR (fix comment) |
| 3 | RM findSource | HashMap A + ObjSource facade | unordered_map + facade | container dev (OK) |
| 4 | ObjSource | dict facade, w/h dflt 32 | readInt dflt 32 | OK; clip STUB (P2) |
| 5 | Player_findSource | DUAL hashmap + raw GPU upload | list+shared_ptr+lazy tex | 🔧 ARCH (phase-D) |
| 6 | SourceCache loadSource | +72 intrusive list, Layer dispatch | std::list, bitmap bake | container dev (OK) |
| 7 | SLA surface | 5 members @0x6ABFAC | 4+factory | OK |
| 8 | RuntimeSupport/GLL | direct TJS dict / internal Layer | aux MotionSnapshot / adapter | port host layer |

## IDB changes (saved)
- rename: Motion_Player_findSource @0x6948E8 (was already set; idempotent).
- set_comments: 0x694c60 (dual-hashmap texture cache), 0x6a88f8 (RM:SourceCache
  inheritance proof). idb_save OK.

## Follow-ups for reviewer
- FIX the unloadAll address in RM.h:147 + RM.cpp:372 comments: 0x6A8BBC -> 0x6A8CF8.
- ObjSource::getClip is a STUB returning {} vs binary Motion.Rect dispatch (P2,
  oracle-inert).
- Player_findSource dual-hashmap + raw-GPU-upload topology (item 5) is the one
  genuine 🔧 architectural divergence; it is parked under the documented phase-D
  texture-topology platform boundary, not a local-patchable defect.
