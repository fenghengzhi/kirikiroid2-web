---
name: player-ncb-92member-alignment
description: Motion.Player NCB table aligned to EXACT 92 binary members (Player_ncb_registerMembers @0x6D69C8); 17 surplus removed, 3 added (defaultSyncActive/defaultTransformOrder/clear)
metadata:
  type: project
---

Motion.Player NCB block (main.cpp NCB_REGISTER_CLASS(Player), ~line 137) now = EXACTLY 92 members (excl ctor), matching Player_ncb_registerMembers @0x6D69C8. Verified count: 42 NCB_PROPERTY + 17 NCB_PROPERTY_RO + 1 NCB_PROPERTY_RAW_CALLBACK + 24 NCB_METHOD + 1 NCB_METHOD_DETAIL + 7 NCB_METHOD_RAW_CALLBACK = 92. IDA's "92" annotation is correct; an earlier agent's "78" was a decompile-truncation miscount.

**Why:** user wanted full faithful 1:1 binary member set on Player (EmotePlayer/ResourceManager/ObjSource/D3DEmotePlayer/Motion-ns untouched — those are separately 1:1).

**REMOVED 17 surplus NCB registrations** (C++ method bodies KEPT — only TJS surface dropped): unload, unloadAll, findMotion, requireLayerId, releaseLayerId, findSource, loadSource, clearCache, setClearColor, setResizable, unloadUnusedTextures, captureCanvas, setSize, copyRect, adjustGamma, frameProgress, isPlaying. (main.cpp Resource/Drawing section collapsed to just isExistMotion #90 + draw #77; isPlaying removed from Misc tail.)

**ADDED 3 binary members** (full decompile evidence):
- `defaultSyncActive` RW prop, CLASS-LEVEL (process-global, NOT instance). get=0x6D93F8 `return (uint8)byte_1AB84A8`, set=0x6D9404 `byte_1AB84A8 = v&1`. byte_1AB84A8 default=0xff→true. Port: `static bool Player::s_defaultSyncActive=true` + getDefaultSyncActive/setDefaultSyncActive (Player.h ~line 716, def PlayerCore.cpp).
- `defaultTransformOrder` RW prop, CLASS-LEVEL. get=sub_6B097C builds 4-elem TJS Array of int[4] dword_1AA40D8={0,3,2,1} (get_bytes 0x1AA40D8). set=sub_6B0AB4 PropGet(flag=1024,idx 0..3) per-elem; fail→throw L"illegul size of transform order", range>3||dup→throw L"illegul variable for transform order" (typos preserved), writes dword_1AA40D8/DC/E0/E4 INCREMENTALLY (partial-write-on-throw — port mirrors incremental write, not deferred). Port: `static int Player::s_defaultTransformOrder[4]={0,3,2,1}` + getDefaultTransformOrder(makeArray)/setDefaultTransformOrder (PlayerCore.cpp after getTransformOrder block). NOTE: distinct from existing INSTANCE `transformOrder` prop (sub_6CC188/sub_6CC2C4, _transformOrder[4] node+84..96) — different member.
- `clear` Function #72 (between play#70/progress#71 and stop#73). callback=Player_drawToLayerCompat @0x6D2DA0 — binary NAME/IMPL QUIRK: member "clear" but cb is gated recursive draw-to-layer (gate *(player+544); fillRect over draw rect; recurse nodeType==3 children). Port: Player::clearCompat raw-cb (gate on _clearEnabled = local player+544 analog) → drawToLayerCompat(targetLayer, fillValue) member: FuncCall(L"fillRect",left,top,w,h,fill) on target layer dispatch + recurse _nodes[1..] nodeType==3 via getChildPlayer(). Files: Player.h (decl), PlayerTimeline.cpp (impl, before namespace close).

**clear DOCUMENTED GAPS** (faithful-structure port, flagged in PlayerTimeline.cpp comments per CLAUDE.md):
1. FAST PATH skipped: binary PropGetByNum(targetLayer, flag=2, num=dword_1AB8820) → sub_6ADCAC fast route. dword_1AB8820/87F8/8848 are runtime member-index CACHES (0xffffffff placeholder in static .so) — cannot resolve UTF-16 member name statically. Port always takes general fillRect path (observably equiv for plain Layer target).
2. DRAW RECT: binary fills over cached pixel rect +884(left)/+888(top)/+892(right)/+896(bottom), lazily recomputed by sub_7E3ECC(player+864) gated !*(player+900). This +864/+884 cluster is DISTINCT from port _boundsMin/_boundsMax AABB (binary +152..+176) and has NO mapped local field. Port fills over bounds AABB as nearest available rect.

Build: `cmake --build out/web/debug --target krkr2` (target is `krkr2` NOT `index`). Clean 240/240. No .cpp added/removed → wasmtime guest source-list step N/A. No runtime fixture exercises these (logo non-emote; defaultSyncActive/defaultTransformOrder are global config, clear gated on _clearEnabled=false default) → ORACLE-INERT, non-regression only.
