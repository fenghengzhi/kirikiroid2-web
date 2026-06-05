---
name: rm-findsource-fnv-objsource-done
description: ResourceManager::findSource @0x6AAB3C ported (split-by-/, src/blank gate, HashMap A by full name, nested PSB dict nav, ObjSource facade construct)
metadata:
  type: project
---

motion::ResourceManager::findSource (ResourceManager.cpp) aligned to libkrkr2.so
sub_6AAB3C @0x6AAB3C — was a 1-line `return findLoaded(path)` stub.

**Prerequisite verdict: equivalent data source EXISTS (container divergence, NOT
architecture-prerequisite missing).** Binary HashMap A (this+88 buckets / this+96
count, FNV inline hash cached ttstr+68) is POPULATED by RM `load`
(ResourceManager_loadResource): on miss it opens the PSB, validates id=="motion",
reads spec/version, then sub_6EB9E4 = findOrInsert into HashMap A keyed by the
FULL load-PATH ttstr, value+16 = parsed PSB module dict. findSource looks up the
SAME map by the ORIGINAL `name` arg (a2/X1 never modified by the split). Port's
`_state->loadedModules` (std::unordered_map<string,tTJSVariant> by lowercased
path) holds the SAME module dicts (detail::loadPSBVariant -> TJS dictionary
object via file->getObjects()->toTJSVal()). So findLoaded(name) is the
container-divergent equivalent of the HashMap A lookup. No load-path registry
step is missing.

**Binary structure (5 steps):** (1) split name by "/" (sub_697D34); empty 1st
piece -> void. (2) pieces[0] gate: !="src" -> if "blank" build dict
{width,height,originX,originY from pieces[1] split by ":" as ints, blank=1} and
return it (raw dict, NOT ObjSource); else void. (3) "src": HashMap A lookup by
original name (sub_6EB8F4); miss -> void. (4) navigate
module["source"][group=pieces[1]]["icon"][icon=pieces[2]] with per-level hasKey
gates (sub_598C58 member-get / sub_5995D8 hasKey); any miss -> void. (5) hit:
operator new(0x18) ObjSource{[0]=icon sub-dict variant,[1]=?,[2]=0} + sub_6EC124
NCB facade construct.

**Port mapping:** splitTtstr() helper (anon ns) = sub_697D34; psbGet() helper =
sub_598C58+sub_5995D8 via iTJSDispatch2::PropGet on the TJS dict; blank branch =
detail::makeDictionary; ObjSource construct = `new motion::ObjSource(iconEntry)`
+ `ncbInstanceAdaptor<motion::ObjSource>::CreateAdaptor` + tTJSVariant(d,d) +
Release (precedent: PlayerLayerQuery.cpp:74 LayerGetterAdaptor::CreateAdaptor).

**Attribution (re-confirmed):** sub_6AAB3C registered ONLY by
Motion_ResourceManager_ncb_registerMembers @0x6AB8BC at site 0x6abd34
("findSource"). It is a DIFFERENT function from Player::findSource @0x6948E8
(SourceCache.findSource, PlayerRender.cpp:42). RM::findSource has NO internal C++
caller — only the TJS NCB binding. Unit tests (motionplayer-dll.cpp:572/607)
exercise Player::findSource, NOT RM::findSource. So this port is ORACLE-INERT
under current fixtures (no RM.findSource caller, no fixture) — non-regression
guard only; honest verification gap.

ObjSource local class (SourceCache.h:128) was constructed nowhere before; now
constructed here. Updated its stale "constructed NOWHERE / returns loaded module
instead" comment.

Build: web/debug clean (krkr2 + layerExDraw + psbfile + psdfile all consume
ResourceManager.cpp) + krkr2_wasmtime_guest clean. Differential:
run_motion_playback_wasmtime.py PASS m2logo(93) + yuzulogo(243) bit-identical.

OPEN/residual deviations: (a) blank-branch dim parse: binary uses sub_A0FE2C int
materialisation per ":" piece; port casts ttstr->tTJSVariant->tjs_int (empty=0) —
behaviourally equivalent, unverified for malformed specs. (b) ObjSource qword[1]
(v33=v58[1], the dict's member-table tail offset from sub_598C58) not stored in
the port facade — the port ObjSource reads fields by PropGet not by the binary's
+offset member walk, so [1] is unused. (c) Container is still STL
unordered_map<string> not the inline FNV bucket map (this+88/+96) — the systemic
phase-D container-impl divergence, unchanged by this work.
