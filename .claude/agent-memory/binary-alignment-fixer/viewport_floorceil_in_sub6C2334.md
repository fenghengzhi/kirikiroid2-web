---
name: viewport-floorceil-in-sub6C2334
description: render-item viewport (item+200..212) floor/ceil rounding lives in the item BUILDER sub_6C2334 @0x6c2950, NOT in sub_6C4E28; sub_6C4E28 only reads viewport and folds floor/ceil into clipRect
metadata:
  type: project
---

Render-item viewport field (item+200..212) is written and rounded by the ITEM BUILDER **sub_6C2334**, not by sub_6C4E28 (the build/clip pass).

**sub_6C2334 @0x6c2800-0x6c2954 (viewport materialization):**
- @0x6c2800: `*(_OWORD*)(item+200) = raw viewport` (from node+1936, or default xmmword_14D7C60 = {1,1,-1,-1}).
- @0x6c2808: gate = `*(_BYTE*)(root+611)` (root = *(_QWORD*)v6, a bool flag on the motion root).
- @0x6c281c: viewport-valid gate `item+208>=item+200 && item+212>=item+204` (right>=left && bottom>=top).
- transforms the 4 viewport corners through matrix (root+808..844) → min/max bbox v80(minX)/v81(minY)/v65(maxX)/v79(maxY), then:
  - `*(float*)(item+200)=floorf(minX); item+204=floorf(minY); item+208=ceilf(maxX); item+212=ceilf(maxY);`
- Same pattern repeats at @0x6c2fbc for a second node type. paintBox (item+184..196) rounded identically at @0x6c2a80 / @0x6c30ec (floor/floor/ceil/ceil).

**sub_6C4E28 @0x6C5DBC does NOT write item+200..212.** It only READS viewport as input and folds floor/ceil into the **clipRect** (item+216..228):
- Loop1 @0x6c5e24: `floorf(vp.left)/floorf(vp.top)/ceilf(vp.right)/ceilf(vp.bottom)` used as max/min bounds against paintBox∩a4(canvasClip), result stored to item+216..228 (clipRect) @0x6c4f8c. NO final extra rounding — v80..v85 written directly.
- Loop2 @0x6c5f54 (group/aux list, v93): same floor/ceil of viewport into clipRect, stored item+216..228 @0x6c6388.
- **clipRect item+216..228 is FLOAT in oracle** (`*(float*)`), harness reads it via readRectF. Port declares clipRect as `array<int,4>` (RuntimeSupport.h:263) — a separate latent divergence (int vs float store), not yet fixed.

**Harness:** `viewportRect = readRectF(item,200)` (frida_motion_stage_agent.js:2618) → reads item+200..212 directly. So m2logo viewport divergence traces to sub_6C2334's write, not sub_6C4E28.

**Port fix (2026-05-30):** PlayerRenderItems.cpp ~602 built entry.viewport from transformed clipNode corners with min/max only, MISSING the floor/ceil. paintBox path (same file ~581) already had floor/ceil correctly. Added floor(min)/floor(min)/ceil(max)/ceil(max) to viewport to match @0x6c2950. m2logo items[9] oracle=[612,557,1294,633], port was [612.568,557.332,1293.251,632.964]. Downstream computeRenderClipRect (PlayerRenderInternal.cpp:528) + SLA helpers re-floor/ceil the already-integral viewport — idempotent, stays oracle-faithful (oracle also re-floors stored viewport in sub_6C4E28). Build passed.

**Lesson:** when a task points at a function for a field write, verify the actual write point first. The floor/ceil "clue" in sub_6C4E28 was real but it targets clipRect, not viewport. Viewport is read-only in sub_6C4E28.
