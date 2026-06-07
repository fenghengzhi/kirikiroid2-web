---
name: render-execute-J-cluster
description: motionplayer render pipeline 0x6C2334/0x6C4E28/0x6C7440 phase topology, local↔binary function mapping, leaf-tier divergence (J1/J7), and the differential baseline that protects it
metadata:
  type: project
---

# Render execute cluster (0x6C2334 / 0x6C4E28 / 0x6C7440) topology

**Local↔binary function map (Frida-confirmed, NOT what audit K text implies):**
- `0x6C2334` build = local `buildPreparedRenderItems` (PlayerRenderItems.cpp). Emits `build_items_*` trace. Writes item fields, NEVER +424/+20/leaf-copy.
- `0x6C4E28` Player_emitRenderItem_requireLayer = local `buildRenderCommands` (PlayerRenderExecute.cpp). Emits `build_commands_*` trace. **Frida `PLAYER_BUILD_COMMANDS_OFF=0x6C4E28`** (frida_motion_stage_agent.js) hooks build_commands_leave at 0x6C4E28's onLeave. So buildRenderCommands IS the 0x6C4E28 counterpart — the requireLayerId/item+20 latch living at PlayerRenderExecute.cpp:83+ is in the RIGHT binary function. Audit K's "moved to build loop" conflated buildRenderCommands with 0x6C2334; the P1-I3 "move back to execute" framing is partly a misread.
- `0x6C7440` Player_renderToCanvas = local `executeLayerRenderCommands` / `renderToCanvasLike_0x6C7440`.

**0x6C4E28 (re-decompiled 2026-06-07) structure:** Loop A over mainList: `if(!item+19 drawFlag) skip`; clip=paintBox∩floor/ceil(viewport); if non-empty && !item+16: item+21=1, write item+216..228=clip(FLOAT); SLA gate (player+760, lazy-create from Window.mainWindow.primaryLayer); LABEL_28 `if(!item+20){requireLayerId numparams=0 →item+424; item+20=1}`; sub_6C6B48(SLA, item+424)→item+304 leaf layer; setSize; switch(item+280): 0→affineCopy,1→bezierPatchCopy,2→meshCopy onto LEAF (pts pre-translate -0.5-clipOrigin float). Loop B over groupList: union child clips∩a4; if(!grp+340) create composed Layer(item+324) via Window.mainWindow; setSize/fillRect(0); child alpha-mask loop(child+21&&child+320); grp+21=1,grp+16=0,write grp+216..228.

**0x6C6B48 absolute (J6):** a1=SLA(player+760). `absolute = SLA+160 + SLA+164` then `++SLA+164`, hitThreshold=256. NOT "node.x+node.y" (audit J6 imprecise). It's a per-SLA monotonic counter+base → local `_nextLayerAbsolute++` is ordering-equivalent (differs by const base, unobservable). Lives in leaf path.

**0x6C7440 (re-decompiled) THREE draw mechanisms — local conflates them:**
1. 0x6C4E28 LEAF (item+304): affineCopy onto PERSISTENT per-item leaf Layer in SLA Rb_tree.
2. 0x6C7440 BUFFERED (LABEL_63, gate clearEnabled(player+1144)||item+264): affineCopy onto SHARED bufLayer=player+656 ctx (NOT item+304), then operateRect.
3. 0x6C7440 DIRECT: operateAffine onto v370, -0.5,-0.5 world offset.
Local `executeLayerRenderCommands::buildItemOutput` builds item+304 leaf AND uses it for the buffered submit, conflating #1 and #2, and uses `Player::_renderLayerStates` (flat unordered_map) instead of SLA's `_managedTargets` Rb_tree (which EXISTS: NativeSLAOrderedMapLike_0x6C6B48, SeparateLayerAdaptor.h:150). That is the J1/J7 architectural divergence.

## DONE 2026-06-07 (verified non-regressing)
- **J9** preview opacity halving (0x6c764c-0x6c7668): `v23=opa>=0?opa:opa+1; v24=preview?v23>>1:opa`. Ported exactly into executeLayerRenderCommands top-level loop. Inert for logo (_preview=false).
- **J4** removed redundant `renderLayer->Update(false)` in execute tail (0x6c8fcc has setClip(argc0)+release only; `L"Update"`=0 in whole 0x6C7440). Update belongs to wrapper (renderToLayer:1225 / updateLayerAfterDraw 0x6CE7D8). Was firing Update twice/draw. PNG byte-identical after removal.

## DONE 2026-06-07: J1/J7/J6 leaf-tier relocation (user override of prior STOP)
- **J1/J7 PORTED**: leaf-copy emit RELOCATED from execute (buildItemOutput) into the BUILD pass (buildRenderCommands), matching the binary's two-function pipeline (0x6C4E28 emits leaf/composed, 0x6C7440 submits). New `Player::emitLeafLayerCopyLike_0x6C4E28` (Loop A drawable body) materializes item+304 leaf on the SLA Rb_tree via the ALREADY-EXISTING `SeparateLayerAdaptor::resolveRenderLayerNodeLike_0x6C6B48` (`_managedTargets` map, reuse-from `_assignTargets`, create Layer, absolute/hitThreshold) — so the J7 leaf Rb_tree machinery was already aligned in the SLA; the divergence was only the WIRING (execute used port-invented `Player::_renderLayerStates`, which player_4_hashmaps.md confirms has NO HM correspondence in the 1384B Player). New `Player::composeGroupLayersLike_0x6C4E28` = Loop B (union child paintBox+184..196 gated child+21 → cam-clamp(a4) → group-viewport floor/ceil narrow → composed Layer item+324 + child alpha-mask). execute `buildItemOutput` non-direct branch now CONSUMES prebuilt item.leafLayer/composedLayer (no rebuild); dead lambdas `ensureComposedItemLayer`/`renderItemSourceToLayer`/orphan `playerStencilType` removed → execute is submit-only.
- **J6 absolute** already structurally aligned in SLA: binary `absolute=SLA+160+SLA+164;++SLA+164` == SLA `_absolute+_assignSequence;++_assignSequence`. No change needed.
- **Loop B precision TRAPS confirmed vs 0x6C4E28**: (1) union accumulates child PAINTBOX (child+184..196=v101[46..49]) NOT child clipRect; (2) TWO rect tuples — EMPTY test uses CAMERA-clamped (v102>v100||v97>v105) but composed SIZE + grp+216..228 write use VIEWPORT-NARROWED (v109/v108/v107/v106); (3) child alpha-mask gate is `child+21 && child+320` and **item+320 is ALWAYS 0** (init 0 in 0x6C2334, never written nonzero in build/0x6C4E28/0x6C7440 — grep-verified) → the alpha-mask loop body is BINARY-INERT even in the binary; reproduced structurally with always-false gate, no item+320 local field invented.
- **100% oracle-inert for BOTH logo fixtures** (mainList all drawFlag19=0 → Loop A drawable body + leaf/composed never run). VERIFIED non-regressing: web+wasmtime build clean, trace PASS m2(93)+yuzu(243), build_flow_mismatch=0 both logos, **execute_post 1008 PNGs BYTE-IDENTICAL to pre-change baseline (diff=0)** = DIRECT path provably untouched. PNG-identical is the non-regression guard (not correctness proof of the inert leaf path — honest gap: no leaf/buffered-exercising fixture exists). NOT committed.
- STILL DEFERRED (out of this scope): player+656 shared bufLayer buffered submit path (0x6C7440 BUFFERED reads player+656 ctx NOT item+304 — local buffered submit still uses chooseItemOutputLayerObject leaf; distinct inert subsystem, not invented); J5 int-vs-float clip origin; J8 accurate-SLA tree; I4 is NOT a real fix (already one object via inheritance).

## Differential baseline (the green this cluster protects)
- Locally reproducible green = (a) trace compare PASS, (b) render-steps **build_flow item-field mismatch=0** (the ITEM_FIELDS per-item semantic diff incl flags.layerResolved20/layerIds/clipRect/buildClipRect/sortKey64/leafBuilt/composedBuilt/executedDirect). NOT the full render-steps run.
- Full render-steps compare FAILS locally on execute_post/post_draw rgbaSha256 + render_prepare_shape — those are STALE committed oracle pixel hashes (2026-05-10) vs current GL provider; CI records fresh device oracle each run. They are NOT this task's signal and can't be matched locally.
- Local pixel-regression self-guard: wasmtime guest writes 1008 execute_post PNGs (`--record-render-step-checkpoints`); snapshot shasum before/after, diff for direct-path regressions.
- Run: build `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`; then per case `run_motion_playback_wasmtime.py --record-render-stages --record-render-step-checkpoints --checkpoint-render-only`. `timeout` cmd absent on macOS — don't wrap.
- web/debug cache can hold a STALE truncated `CMAKE_TOOLCHAIN_FILE=/upstream/emscripten/...` (missing $EMSDK prefix) from a prior-session env → configure fails with "could not find Emscripten.cmake". Fix: `rm out/web/debug/CMakeCache.txt CMakeFiles` + re-run `cmake --preset "Web Debug Config"`. Not a code error.
