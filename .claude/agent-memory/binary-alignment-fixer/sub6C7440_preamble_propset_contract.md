---
name: sub6C7440-preamble-propset-contract
description: Verified per-item PropSet/FuncCall preamble inside 0x6C7440 (key/src/blendMode/neutralColor/setSize/setClip); local port pre-bakes via SourceCache so full migration is LARGE/RISKY, reported not patched
metadata:
  type: project
---

The "source-draw preamble" (key/src/blendMode/neutralColor + setSize/setClip) in 0x6C7440 is NOT a flat PropSet list on the Layer. Fresh-decompiled 2026-06-04. Per render item (v139 = *v11, loop 0x6c75c8..0x6c8fbc):

DISPATCH TARGETS (each property goes to a DIFFERENT resolved iTJSDispatch2, obtained from Player member-stored tTJSVariant holders via sub_A0F5E0 + off_19FD968 holder vtable):
- setClip: FuncCall vtbl+16 on v370 (the "Layer" class object from sub_5CB08C(L"Layer")). Two forms: 4-arg (L"setClip", argc=4, clipRect = floor/floor/ceil/ceil viewport-intersection) when src rect valid, else 0-arg reset. Final trailing reset setClip(0-arg) at 0x6c8fcc after loop.
- key:  PropSet vtbl+48 on v355 (resolved from Player+676), value = ttstr at v139+248 (sub_A0FE2C copies ttstr). key L"key" @0x6c770c.
- src:  PropSet vtbl+48 on v355, value = ttstr at v139+8. L"src" @0x6c7750.
- blendMode: sub_5A6020(&v354, L"blendMode", v139+48, 512,...) = PropSet vtbl+48 of int(v139+48) on *(v354+8). v354 is off_19FD968 holder. @0x6c7780.
- neutralColor: 4 separate index-PropSet vtbl+56 on v27 (resolved from Player+716), indices 0/1/2/3, values = packed u32 at v139+168/172/176/180 (sub_A0FB64 packs into tTJSVariant). @0x6c7984..0x6c7a74.
- THEN sub_6C1B70(v349, a1, *(v139+256)+4) = source resolver -> builds v349. width/height read via sub_6635DC. blendMode switch (v139+48 & 0xF) picks draw-method index 14/15/16/17/2.
- bufLayer: PropGet vtbl+32 L"bufLayer" on v49 (resolved from Player+656) -> v342 work/buffer layer. setSize: FuncCall vtbl+16 L"setSize" argc=2 on v50 (child resolved from bufLayer). @0x6c7d7c.

KEY ARCHITECTURE FACT: key/src/blendMode/neutralColor are PropSet onto INTERMEDIATE Player-member layer objects (Player+676/+716/+656 work/buffer layers), then sub_6C1B70 resolves the source FROM those. They are NOT direct setters on the source bitmap.

LOCAL PORT (cpp/plugins/motionplayer/): completely different architecture.
- SourceCache::loadRenderSourceByName(name, srcRef, blendMode, packedColors,...) PRE-BAKES blendMode + 4-corner packedColors into a cached tTVPBaseBitmap (SourceCache.cpp applyPackedCornerTintLike_0x6A7518, key cache by (name,blendMode,packedColors)). resolveSourceObjectLike_0x6C1B70 (PlayerRenderExecute.cpp:353) returns the baked bitmap layer directly.
- setClip is the ONLY preamble element that maps: native renderLayer->SetClip/ResetClip (PlayerRenderExecute.cpp:778-782) on the actual render target. NO TJS FuncCall.
- There is NO local equivalent of the key/src/blendMode/neutralColor PropSet onto Player+676/+716 holders, and NO bufLayer PropGet+setSize FuncCall. The Player TJS object does not register key/src/blendMode/neutralColor as PropSet entries nor setSize/bufLayer; the work-layer member objects (+656/+676/+716) are not modeled.

DECISION: STOP-AND-REPORT (not patched). Converting the preamble requires (a) modeling Player+656/+676/+716 intermediate work-layer member objects, (b) registering key/src/blendMode/neutralColor PropSet + setSize FuncCall + bufLayer PropGet on them, (c) replacing the SourceCache pre-bake pipeline with runtime PropSet->native-setter routing. This restructures source loading/caching = LARGE; logo exercises this path so any divergence shifts pixels = byte-identical NOT provable. Per CLAUDE.md report-for-approval over patching on inconsistent base.
