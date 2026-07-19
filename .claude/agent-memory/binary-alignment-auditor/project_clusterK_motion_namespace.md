---
name: clusterK-motion-namespace
description: Cluster K (SourceCache/ObjSource/ResourceManager/Point/D3DAdaptor/PrivateMotionGLL/findSource/alphaMask) binary↔local member-set + arch diffs
metadata:
  type: project
---

2026-05-30 Cluster K audit, corrected 2026-07-19. Ledger: analysis/audit_motionplayer_2026-05-30/clusterK_motion_namespace.md. The source path is CLOSED except for the documented KRKR full-page Web upload boundary.

Confirmed addr↔fn (renamed in IDB):
- Motion_Player_findSource @0x6948e8 — this is **Player::findSource**, NOT SourceCache. Core = ResourceManager outer unordered_map whose mapped record owns the raw PSB plus a Win `ttstr->texture` map and KRKR flat descriptor map; then raw texture upload or TJS-dispatch fallback. name=="blank" gate; rmState+224==type (2=embedded PSB, 1=KAG layer). Hash = `(1025*h)^(>>6)` then `9*` then `32769*(x^(x>>11))`, 0→-1.
- Motion_createTextureFromPixels @0x695d04 — MISNOMER: guarded singleton accessor for the "opengl" render backend (sub_84B3A4("opengl"), guard byte_1AB8530, cache qword_1AB8528). Caller does vtbl+24 to upload.
- Motion_doAlphaMaskOperation @0x6af104 — namespace-level static. CPU pixel loops `dst.a=src.a*dst.a/255` (op1 mode1), `(~src.a)*dst.a/255` (rev) + threshold step shaders. Shader names (ASCII): AddAlphaMask/AlphaMask/AlphaMaskRev/AlphaMaskThreshold{,Fill,Crop}, uniform "threshold". Dispatch keys: clipLeft/Top/Width/Height, fillRect, update. **MISSING locally.**
- Motion_getD3DAvailable @0x6b0960 — `return !(hasGPUAccel_guess()&1)`.

NCB registrar @0x6d9b08 (motionplayer_ncb_register) registers on the **Motion namespace**:
- doAlphaMaskOperation & getD3DAvailable are NAMESPACE-level fns (local wrongly puts them as NCB_METHOD on Player, main.cpp:285-286).
- MaskModeStencil/MaskModeAlpha constants belong on Motion namespace (local only has them on D3DEmoteModule).
- Player is a subclass listed between LayerGetter and SourceCache (local aliases it via PostRegistCallback instead).

Member-set diffs (member registrars):
- ObjSource @0x69ccb8: binary=ctor+originX/originY/width/height/clip (RO) + drawLayer. Local registers the same surface and implements a retained raw owner/node pair plus lazy texture; strict/try reads, clip, ensureTexture, drawLayer and texture→owner destruction are aligned. ✅
- ResourceManager @0x6ab8bc: binary 13 = ctor+loadSource+clearCache+bufLayer(RO)+load+unload+unloadAll+isExistMotion+findMotion+findSource+random+requireLayerId+releaseLayerId. RM public-inherits SourceCache and re-lists the base callbacks (sub_6A7BA8/6A8438/6A84FC). Local now restores that inheritance and the NCB surface; decrypt-seed helpers remain additional plugin API.
- SourceCache @0x6a85a8: ctor+loadSource(sub_6A7BA8)+clearCache+bufLayer(RO). ✅ matches local main.cpp:27.
- Point @0x690fbc: ctor+type+contains(=Player_hitTest!)+x+y RO. ✅ set matches; local `contains` is false-stub.
- D3DAdaptor @0x6ace94: 19 members, many nullsub stubs. ✅ matches local main.cpp:107.

PrivateMotionGLL @0x6dd284: real NCB class (ctor delegating @0x6de24c + setSize/visible/absolute). Local PrivateMotionGLL.h = free helper fns + std::vector, no class. 🔧

2026-07-19 correction: this cross-cutting diagnosis is superseded. RM public
inheritance, outer mapped record, Win/KRKR nested maps, and texture ownership are
restored. `SourceCache` layer LRU and `Player_findSource` resource maps are distinct
chains. Win/KRKR and ObjSource now navigate raw `PSBRawNode`; the former decoded
`MotionSnapshot` side graph no longer participates. Only KRKR's one-shot full-page
upload is retained as a concrete Web rendering API boundary.
