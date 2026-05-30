---
name: sub6C4E28-build-execute-split
description: sub_6C4E28 @0x6C5DBC interleaves requireLayerId materialization (item+20, item+424) into the BUILD loop; port wrongly split it into the EXECUTE stage, causing render-step build_flow_mismatch on layerResolved20
metadata:
  type: project
---

motion::Player render-item pipeline is architecturally mis-split vs sub_6C4E28 @0x6C5DBC.

**Oracle sub_6C4E28 (one function, two do-while loops):**
- Loop 1 (main list, starts 0x6c4f18): per item, gate = item+19(drawFlag) set && drawable(clip valid && !item+16). When drawable, sets item+21=1, paint bounds item+216..228, then the LABEL_28 block (0x6c514c): reads `requireLayerId` PROPERTY off resolved layer object → writes item+424 (layerId) → sets item+20=1. LABEL_28 reached only when item+20==0 (once-only latch). item+20 NEVER cleared in the loop → persists across frames.
- So in oracle, requireLayerId materialization (item+424 + item+20 latch) happens DURING BUILD, interleaved with clip/paint computation.

**Port split (wrong):**
- `Player::buildRenderCommands` (PlayerRenderExecute.cpp:13) = oracle build loop, BUT only materializes rawFlag21(+21) + clipRect. Does NOT do requireLayerId/+20/+424.
- rawFlag20(+20) is instead set in EXECUTE-stage helpers: `ensureLeafItemLayer` (PlayerRenderExecute.cpp:384) + `ensureAccurateSlaItemLayer` (PlayerRenderTargets.cpp:831), meaning "leaf layer object created", NOT oracle's "requireLayerId materialized".
- layerId (item+424 equiv) sourced eagerly from `node.layerId1`, assigned at tree-build in NodeTree.cpp:103 via `requireLayerId()` (no-arg), NOT lazily off the resolved layer object's `requireLayerId` property as oracle does.
- rawFlag20 persists frame-to-frame via `_renderItemNativeFieldLifetimeByNode` (persist PlayerRenderInternal.cpp:587, restore PlayerRenderItems.cpp:322).

**Symptom:** render-step compare `build_flow_mismatch` (92/242 per frame). Trace samples `layerResolved20`=item.rawFlag20 at `build_commands_leave` (wasmtime harness motion_playback_wasmtime.cpp:1964/433). items[0]: oracle=0 (LABEL_28 not reached this build), port=1 (leftover from prior frame's execute leaking via lifetime map).

**Why it's architectural, not a patch:** Can't just gate the execute-stage write — the whole requireLayerId materialization (property read + item+424 + item+20 latch) lives in oracle's build loop and is absent from port's build loop. Fixing requires moving the LABEL_28 block (requireLayerId property resolution) INTO buildRenderCommands with the exact oracle gate, and making layerId lazy-latched there. motion_playback trace compare is currently 0-mismatch (green) — high regression risk. Needs module-alignment-driver.

**2026-05-30 Phase B attempt — DECISION: STOP, Phase B inseparable from Phase C.** Re-decompiled sub_6C4E28 + sub_6C6B48 (the SLA layer-state cache keyed by item+424, a1+120/a1+72 Rb-trees). Decisive findings:
- Oracle resolves the render-layer object (SLA-create / Window.mainWindow.primaryLayer) IN THE SAME build-loop iteration, immediately before LABEL_28. item+424 = `requireLayerId` PROPERTY read off that resolved layer dispatch (vtable+16 PropGet). The item+20=1 latch is bound to that property read.
- Port `buildRenderCommands` does ZERO layer-object resolution — all of it lives in EXECUTE (ensureLeafItemLayer→ensureReusableLayerObject). Port item.layerId = node.layerId1 = ResourceManager::requireLayerId() eager at NodeTree.cpp:103 (global allocator, different domain from oracle's per-layer-object property).
- Port invariant: rawFlag20==true ⇔ leaf layer object created (execute-stage). Setting it in build with no backing property read = hollow latch / forbidden patch.
- Conclusion: a clean "move flag, value unchanged" Phase B is impossible. Faithful relocation needs (a) moving layer-object resolution execute→build AND (b) reworking eager node.layerId1 into lazy requireLayerId property read = exactly Phase C (NodeTree.cpp:103). Recommend module-alignment-driver do Phase B+C as one refactor. NO code changed in this attempt.
