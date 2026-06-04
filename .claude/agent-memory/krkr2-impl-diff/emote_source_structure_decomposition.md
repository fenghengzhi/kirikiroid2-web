---
name: emote-source-structure-decomposition
description: 2026-06-04 fresh source-structure (class/fn decomposition) audit of EmotePlayer facade + EmoteEngine controllers — both reproduce binary 1:1, no flatten/merge/invent; plus the recurring "step fn" naming trap
metadata:
  type: project
---

EmotePlayer facade + EmoteEngine controller class/function DECOMPOSITION is faithful to libkrkr2.so (source-structure dimension only; value correctness not in scope).

**Why:** repeated audits keep re-checking whether the port flattened the 3-layer facade or split/merged controllers. Fresh decompile (sub_52FD84/52FDD4/67DBAC/67E38C/67D4D0/6A88CC + step fns) settled it.

**How to apply:** when asked again about EmotePlayer/EmoteEngine class layering, cite this; only re-decompile if a specific edge is challenged.

Facade 3-layer = 4 distinct local classes (NOT flattened):
- D3DEmotePlayer (EmotePlayer.h:282) = 24B native instance; two slots _primaryObj/_secondaryObj (h:535-536) = binary instance+24/+32. ctor/dtor sub_52FD84(create/clear) sub_52FDD4(load) destroy-both-then-rebuild-primary; secondary stays null per binary.
- EmoteObject (EmotePlayer.h:62, ctor sub_67DBAC) holds ResourceManager _rm@+0, EmoteEngine* _engine@+8 (h:82-83). raw ptr + manual new/delete.
- ResourceManager (ResourceManager.h:102, ctor sub_6A88CC, 232B single class — NOT folded into EmoteObject).
- EmoteEngine (EmoteEngine.h:333, ctor sub_67E38C, the 1496B obj) holds Player* _player@+1064 (h:584); Player is the new(0x568)=1384B obj.
Facade deviations are NOT layer merges: (B-1) _modules is vector<tTJSVariant> value vs binary vector<tTJSVariant*> ptr (h:73-79, deliberate); (B-2) D3DEmotePlayer hairScale/partsScale read engine+1080 shadow vs binary +1184/+1192 (h:355-357 self-flagged follow-up).

Controllers: EmoteEngine_ctor builds 7 controller objs a1[134..140] @+1072..+1120, mirrored 1:1 by EmoteEngine.h:587-599 (_ctlPosition/_ctlScale/_ctlColor/_ctlAngle/_ctlHairPartsTarget/_ctlBust1Target/_ctlBust2Target). EmoteVarController class is REUSED 7× locally exactly as binary reuses EmoteVarController_ctor_20Bdeque 5×+transition+hair/bust — reuse, NOT merge. EmoteAngleController distinct (own ctor 0x6867B0 / step 0x666634). Per-frame steps: VarController 0x666BF8, Angle 0x666634, Blink 0x663BDC, Eyebrow 0x665600, Mouth 0x666068, Selector 0x668470, Loop INLINED in progress 0x67d2a0 (local EmoteLoopController reifies it = faithful, not invented), MeshResolver 0x661F7C(+engine 0x660028), Spring 0x662768, Wind 0x6687E8, BlinkRng 0x9F1A08/9F17D0. No controller split/merged/invented/missing.

**RECURRING TRAP (naming):** sub_6638B0/6652D4/665E34/667300/6681E4 are NOT controller "step" fns — they are per-controller enqueue-transition / setKeyframes helpers (shared shape: dur<=0 snapshot else push 504B-block deque elem, differ only by element stride). Real steps are the list above, dispatched inside EmoteEngine_progress 0x67D01C. EmoteSelectorController.h:20-26 already documents this conflation. Do not treat enqueue helpers as step fns.
