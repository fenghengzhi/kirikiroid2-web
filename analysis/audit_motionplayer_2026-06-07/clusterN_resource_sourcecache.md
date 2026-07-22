# CLUSTER N — ResourceManager / SourceCache / ObjSource / SLA / GLL / RuntimeSupport

> **2026-07-19 correction:** Sections 3/4 and the original deviation table were
> based on a false `tTJSVariant` dict-facade interpretation. Fresh construction,
> destruction and consumer decompilation proves ObjSource is
> `{retained PSBRawOwner*, node*, lazy texture*}`. The local raw navigation,
> clip/ensureTexture/drawLayer, adaptor-failure leak and destruction order are now
> restored. The corrected text below supersedes the earlier verdict.

> **2026-07-22 correction:** the later `0x6A8D8C/0x6AAB3C/0x6AB56C`
> reconstruction also disproved this note's remaining `findLoaded`, random
> STUB, blank-integer and RuntimeSupport snapshot claims. Current code has no
> `findLoaded`; `random` calls the ctor-created generator Variant; blank's four
> dimensions remain String Variants while only `blank` is Integer 1; the eager
> MotionSnapshot subsystem was deleted. The corrected passages below are the
> current verdict.

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
  created via global Layer class CreateNew(owner, primaryLayer)), +60 current
  cache bytes=0, +64 cache byte limit (a3), +72/+80 intrusive layer-list
  head/tail sentinel (both = a1+72). The older `layerType` reading was disproved
  by `trim@0x6A6B08` and corrected 2026-07-23.
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

  *** CORRECTION (function boundary finalized 2026-07-18): IDA merged two adjacent functions. The body at
  0x6A8B94 is the ResourceManager destructor; that is where the +168 layerId
  RB-tree, +144 RandomGen and SourceCache base are destroyed. The actual
  unloadAll starts at the independent prologue `0x6A8CF8` and only walks +104,
  memsets buckets@+88, and zeros +104/+112. Moreover +104 is not a separate
  motion-list: it is HashMap A's libstdc++ `_M_before_begin._M_nxt`, the global
  node chain belonging to the same unordered_map rooted at +88.

Local: load/unload/findSource/isExistMotion/findMotion/
requireLayerId/releaseLayerId/unloadAll are backed by the aligned containers.
`random` mirrors `0x6AB56C`: it copies/uses the ctor-created generator Variant,
calls `random()` without arguments, converts a non-void result to Real, and
otherwise returns 0. There is no local `findLoaded` member.

## 3. RM findSource @0x6AAB3C — CONFIRMED raw-node path (item ②/③)

```
split name by "/" (sub_697D34); !pieces[0] -> void
if pieces[0] != "src":
  if pieces[0] != "blank" -> void
  blank: split pieces[1] by ":" -> width/height/originX/originY strings;
         build dict{width,height,originX,originY as String, blank=1(Integer)};
         return it
src: FNV-hash(name) -> sub_6EB8F4(this+88 HashMap A, hash%this+96, name) -> record
  miss -> void
  root=record.file.GetRoot(); strict "source"; dynamic group has+strict;
  strict "icon"; dynamic icon has+strict
  hit: operator new(0x18) {qword[0..1]=raw owner/node, [2]=0 texture}; owner AddRef
       CreateAdaptor(sticky=false,err=false); null -> void and leak new object
```
Local RM::findSource mirrors this structure using `LoadedResourceRecord::file`,
`PSBRawNode` fixed strict / dynamic has+strict reads, `new ObjSource(rawNode)` and
the same adaptor-null leak. No intermediate TJS dictionary graph remains.

## 4. ObjSource @0x69CCB8 — CONFIRMED raw-node facade (item ③)

Constructor plus 6 exposed NCB members: originX(sub_69D014), originY(sub_69D0D8), width(sub_69D19C),
height(sub_69D27C), clip(sub_69D35C prop-ro), drawLayer(sub_69D6D8 method).
- qword[0..1] are a retained raw owner/node pair; qword[2] is lazy texture.
- originX/Y are strict raw reads. Width/height return 32 only when the raw node is
  not a dictionary; a missing dictionary member throws.
- clip is try-gated and strictly reads left/top/right/bottom into a new property
  object. ensureTexture handles raw/RL8/RL32/palette/aligned-buffer/pitch copy;
  drawLayer assigns the texture and its own dimensions.
- Destructor releases texture before decrementing/deleting the raw owner.

Local ObjSource now mirrors all of these steps. The older dict-facade and clip-STUB
conclusions are disproven.

## 5. Player_findSource @0x6948E8 — the real render-source resolver (item ②/③, CRITICAL)

This is NOT RM.findSource. It is the Player-internal source->texture resolver
(renamed Motion_Player_findSource). It is the function the prompt meant by
"findSource 0x6948E8 (RM mapped record + two nested maps)":

```
RM = Player+636 dispatch -> native @ *(prop+8)=v10; spec = v10+224 (1=krkr 2=win)
arg !"blank" prefix:
 spec==2 (win): HashMap A lookup sub_6EB8F4(v10+88, FNV(moduleKey)%v10+96) -> record
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

ARCHITECTURE CORRECTION (2026-07-18): outer HashMap A is keyed by the loaded
module key. Its mapped record is constructed by sub_6EBCFC and contains, in
declaration order, PSBFile root + Win `group->texture` map + KRKR
`src/group/icon->descriptor` map. The record destructor sub_6DB3E8 destroys
KRKR map, Win map, then PSBFile; unload/unloadAll therefore release textures
with the outer module node. This is three maps total, not a SourceCache-list
substitute and not a platform boundary.

Local now uses `LoadedResourceRecord` as `_loadedModules` mapped value with the
same two nested ttstr unordered_maps, `rehash(10)` construction, owning
AddRef/Release entries, flat KRKR full-path key and matching member destruction
order. `MotionSnapshot` no longer owns either texture table. Win/spec=2 now reads
the record's raw `PSBRawNode` graph directly and mirrors discarded
`truncated_*`, exact width/height, raw RGBA8/A8L8 conversion and icon geometry.
KRKR/spec=1 atlas decode now also reads the record's raw `PSBRawNode` graph and
mirrors all-group enumeration, raw/RL/palette branches and transparent 2x2
handling. The Web texture API still requires a full-page KRKR upload instead of
per-subrect non-zero-offset updates; that upload primitive is the concrete
platform adaptation. These named raw-owner/map and decode sites are aligned;
the former claim that Player_findSource's remaining source-pixel chain was
globally closed was disproven by the later shared-caller, direct-alias and
field-order audit and must not be reused as a 100% coverage conclusion.

## 6. SourceCache loadSource/clearCache — CONFIRMED base behavior

- loadSource sub_6A7BA8: reads descriptor "key"/"src"/"blendMode"/"color"[4];
  matches the +72 libstdc++ `std::list<Entry>` node by (full Variant key via
  sub_A10428 strict compare, src ttstr wcscmp,
  blendMode @node+64). A same-color hit returns in place without reordering;
  color mismatch rewrites/rebakes via sub_6A6BE0 and clone-to-front. A miss calls
  trim@0x6A6B08 before creating/baking the Layer, stores `4*width*height`, adds
  that weight to +60 and inserts at the front. Output = node+36 Layer variant.
- clearCache sub_6A8438: walks +72 list, calls dispatch `Invalidate` on every
  Layer with itself as objthis, clears the list and resets +60=0. Does NOT touch hashmaps.
Local SourceCache.cpp uses `std::list<Entry>`. The production prepared-item route
now matches exact `(full Variant key,src,blendMode)`, treats color as mutable
state, leaves same-color hits in place, and implements color-change as
`push_front(copy)+erase(old)`. Its trim is the same greedy subsequence scan (not a
prefix cut), and incoming source is borrowed only for miss/color-change bake.
The inherited public NCB method now has the exact `(source,descriptor) -> Layer`
boundary; the separate `Player.loadSource(name)` compatibility helper neither
aliases nor populates this cache. These named functions are aligned; this does
not turn the rest of the unaudited subsystem into a global 100% proof.

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
- RuntimeSupport.cpp now contains shared TJS/Layer helpers only. The former
  `loadMotionSnapshot`, tag/priority cache and eager `MotionSnapshot` graph were
  deleted; live resource navigation stays on raw PSB/TJS owners. One confirmed
  remaining caller-family gap is Array construction: several Player accessors
  still pre-materialize a `vector` and call TJS `add`, whereas Android calls
  `sub_704CB8` and writes `tTJSArrayNI::Items` directly.
- MotionTraceWeb.cpp: web console trace shim. Pure platform.

## Deviation summary

| # | Item | Binary | Local | Status |
|---|------|--------|-------|--------|
| 1 | RM:SourceCache | public inherit, base ctor seeds owner/bufLayer | inherit OK, base fields empty | P3 ARCH OK / field gap |
| 2 | unloadAll addr | body @0x6A8CF8 | comment says 0x6A8BBC | DOC ERR (fix comment) |
| 3 | RM findSource | mapped record raw root + ObjSource | mapped record raw root + ObjSource | CLOSED |
| 4 | ObjSource | raw owner/node/texture; strict/try getters | same, including texture→owner dtor | CLOSED |
| 5 | Player_findSource | outer record + Win/KRKR nested maps + shared `0x695DE8` render-time caller | 2026-07-23 corrected direct SourceState alias, getter-after-write rect flow, branch-local decode calls and atlas geometry; full-page upload remains Web API boundary | AUDITED SITES + BOUNDARY |
| 6 | SourceCache loadSource | std::list keyed by full Variant key/src/blend; Layer dispatch | inherited NCB `(source,descriptor)` and production route exact; separate Web-only `Player.loadSource(name)` helper remains extra compatibility surface | NAMED CHAIN ALIGNED + EXTRA SURFACE |
| 7 | SLA surface | 5 members @0x6ABFAC | 4+factory | OK |
| 8 | RuntimeSupport/GLL | direct TJS/Array NI / internal Layer | eager snapshot removed; GLL adapter remains; some Array callers still use vector+`add` | PARTIAL |

## IDB changes (saved)
- rename: Motion_Player_findSource @0x6948E8 (was already set; idempotent).
- set_comments: 0x694c60 (dual-hashmap texture cache), 0x6a88f8 (RM:SourceCache
  inheritance proof). idb_save OK.

## Follow-ups for reviewer
- FIX the unloadAll address in RM.h:147 + RM.cpp:372 comments: 0x6A8BBC -> 0x6A8CF8.
- ObjSource raw navigation and lifecycle are closed; do not reintroduce the old
  dict-facade/PropGet side graph.
- Player_findSource Win/KRKR raw chains are restored. KRKR full-page upload is a
  concrete Web rendering API boundary, not a raw-navigation gap.
