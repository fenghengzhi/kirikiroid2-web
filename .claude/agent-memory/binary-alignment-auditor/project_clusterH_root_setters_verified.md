---
name: clusterH-root-setters-verified
description: Verified binary name→fn→offset map for Player root-delta setters (setFlip/Zoom/Slant/Opacity/Visible/AngleRad); corrects clusterH H-15 IDA misname and confirms M20 alignment
metadata:
  type: project
---

2026-05-31 READ-ONLY audit of Player root-delta setters @0x6C0F1C–0x6C1048. M20 commit 6ebedd8 "writes root delta 1:1" VERIFIED correct.

Authoritative name→fn→offset (from `Player_ncb_registerMembers`@0x6D69C8 decompile):
- setFlip   @0x6C0F1C → root+1587/+1588 (byte&1), dirty+1584
- setZoom   @0x6C0F54 → root+1624/+1632 (double=scaleX/scaleY), dirty+1584
- setSlant  @0x6C0FF8 → root+1640/+1648 (double=slantX/slantY), dirty+1584
- setAngleRad @0x6C0F84 → emote(this+482): wrap[0,360)→this+464→initEmoteMotion(this,2); else root+1616, dirty
- setOpacity(setRootOpacity) @0x6C1028 → root+1656 (int, NO *255 scale, raw write), dirty
- setVisible(setRootVisible) @0x6C1048 → root+1586 (byte&1), dirty
ALL setters gate writes+dirty on `if(old!=new)`.

**IDA MISNAME (do not trust):** IDA symbol `Player_setSlant`@0x6C0F54 is WRONG — it is actually setZoom (+1624/+1632). Real setSlant is 0x6C0FF8 (+1640/+1648). clusterH H-15 inherited this error and wrongly claimed "setSlant writes +1624/+1632". Local code is CORRECT (zoom→scaleX/Y, slant→slantX/Y). Suggest rename 0x6C0F54→Player_setZoom, 0x6C0FF8→Player_setSlant.

DeltaState offsets in MotionNode.h:227-242 match binary exactly.

**Why:** clusterH H-15 was wrong due to IDA misname; future audits keying off the IDA symbol will repeat it.
**How to apply:** when auditing zoom/slant alignment, use offsets not IDA names. Remaining open deviations: opacity *255 coercion lacks decompile proof (D-1); local lacks change-gate (D-2); setAngleRad missing emote-mode branch — 2 rounds unresolved, audit initEmoteMotion next (D-3); port-extra _flip/_opacity scalar multi-write (D-4). See [[project_player_progress_inner_loopTime_invariant]].
