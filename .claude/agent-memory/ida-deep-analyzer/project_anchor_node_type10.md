---
name: anchor-node-type10
description: Player_evaluateAnchorNodes_type10 @0x6C0528 — dampPow formula byte-verified, w/h from internal render Layer (player+696) PropGet, +592=_deltaTime gate, +612=post-draw snapshot. RESOLVED in port (commits 7caf558/5018087/eb347f5).
type: project
---

Player_evaluateAnchorNodes_type10 @0x6C0528 (local: Player::updateLayersPhase3_AnchorNode, PlayerUpdateAnchor.cpp). Called once from Player_updateLayers@0x6BB33C (call site 0x6BBCA8). nodeType==10 anchor nodes only (NOT camera=type? — this is the anchor/constraint path). Iterates nodes from index 1.

**dampPow (v28/D8) byte-verified disasm @0x6C0884-0x6C08B8:**
- D3 = [Player+0x250] (=a1[74], Player+592) — the dt numerator. Same field used as frameLastTime==0 gate at top (0x6C06E8).
- D4 = [node+0x980] (node+2432) = type-10 timeline output sourced from
  `feedback.timespan` (merge @0x6941E4, evaluator @0x699AE4)
- X8 = [a1] (the object *a1, i.e. EmoteEngine via Player; a1 here is the Player, X19=a1)
- D1 = [X8+0x250] = *a1+592 ; D2 = [X8+0x490] = *a1+1168 ; v27 = D1/D2
- v28 = dt * (v27*dt / v27) / v27 / 60.0 / feedbackTimespan
- Note `(v27*dt/v27)` algebraically == dt, so v28 == dt*dt / v27 / 60 / feedbackTimespan. Compiler kept the redundant mul/div (not simplified). The /60.0 const is qword_14D67D8=0x404E000000000000=60.0 (verified).
- dampPow is then used as **pow exponent** for scale(node+1544/1552), opacity(node+1576), color channels — AND as **lerp factor** for angle(node+1536), slant(node+1560/1568), position(node+1512/1520/1528 toward root node[0] v14[189..191]).

**w/h source = internal render Layer PropGet, NOT cache (byte-verified @0x6C0770-0x6C0848):**
- CORRECTION (2026): player+696 is NOT a PSB dict — it is the per-player internal
  render LAYER dispatch (a window.Layer instance materialized by sub_6CE19C from
  canvas->window->CreateNew("Layer")->setSize(window.w/h)). w/h are that Layer's
  width/height (window-sized), shared by all anchor nodes. Port mirror =
  _internalRenderLayer (Player.h); the +612 gate guarantees it was materialized
  last frame. (NOT node.psbNode, NOT node.interpolatedCache.)
- v83 = dispatch obtained from sub_A0F5E0(player+696). v82=off_19FD968 vtable.
- width: v83->vtbl[32](PropGet, flags=0x400, key=L"width"(aRwidth+2, real UTF-16LE), out=v85) -> if >=0 sub_6635DC(&v82,L"width",...) -> (double) -> node+232.
- height: same with L"height" -> node+240.
- node+248 = w*0.5 (originX), node+256 = h*0.5 (originY). node+264/272 zeroed. node+280 = identity oword (1.0,1.0). NO 32.0 default clamp in binary (local PlayerUpdateAnchor.cpp cw<=0?32 is WRONG/extra). 32.0 appears only as scale normalizer: pow(scaleX*32.0/w, dampPow).

**Field map (offset -> local name):**
- Player+592 = _deltaTime (dt; Player.h:954) — numerator + frameLastTime gate
- Player+1168 = _speedMul (Player.h:953) — v27 denominator pairs with +592 (but read from *a1)
- node+2432 = feedbackTimespan (type-10 eval channel; MotionNode.h)
- node+2440 = anchorOpaScale (MotionNode.h:341)
- node+2448.. = anchorColorScale[16] (node+2472=v55 base, MotionNode.h:343)
- node+1505 = active byte gate ; node+200 = anchorEnabled/renderTreeFlag (set 1, cleared 0 if frameLastTime==0)
- node+232=clipW node+240=clipH node+248=originX node+256=originY
- node+1536=angle 1544=scaleX 1552=scaleY 1560=slantX 1568=slantY 1576=opacity(int)
- node+1512/1520/1528 = posX/Y/Z (lerp toward root v14[189/190/191] = root+1512/1520/1528)
- node+100..115 = color bytes (4 RGBA sets) ; player+613 = needsAssignImages (set 1) = _needsInternalAssignImages
- player+612 = second gate. Gate = `Player+592(=_deltaTime, NOT frameLastTime) == 0 || !*(player+612) -> clear`. +612 = POST-DRAW snapshot of +613, written `+612 = +613` first thing in updateLayerAfterDraw @0x6CE7F4 (port: updateLayerAfterDrawLike_0x6CE7D8). Means "internal render Layer materialized last frame". Port mirror = _internalRenderLayerReady.
- qword_14D7C50[(blendMode&0xF0)==0x10] = color base: [0]=255.0 [1]=128.0

**Resolution (port, all aligned):**
- dampPow: fixed to dt*(v27*dt/v27)/v27/60/feedbackTimespan, dt=_deltaTime, v27=_deltaTime/_speedMul, removed max(.,0.001). The older `anchorDamping` name was corrected after the merge/evaluator data flow was traced end-to-end.
- color base 255:128 (was 255:255). commit 5018087.
- w/h from _internalRenderLayer PropGet("width"/"height") + removed <=0?32 clamp; +612 gate via _internalRenderLayerReady (snapshot in updateLayerAfterDrawLike). commit eb347f5.
- NOTE: a transient commit (5018087) wrongly marked w/h + 612 "architecture-blocked / prerequisite missing" — that was a faulty-grep error; the port already had updateLayerAfterDrawLike_0x6CE7D8 + _internalRenderLayer. Corrected in eb347f5.
- Remaining: gate field naming aside, anchor type-10 is byte-aligned. Non-anchor paths (SLA piledCopy @0x6CE938) separate.
