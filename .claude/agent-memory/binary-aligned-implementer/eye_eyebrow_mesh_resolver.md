---
name: eye-eyebrow-mesh-resolver
description: sub_661F7C/sub_660028 value-track mesh path-search resolver ported into EmoteMeshResolver.{h,cpp}, wired into eye+eyebrow controllers
metadata:
  type: project
---

EmoteMeshResolver.{h,cpp} = faithful port of the eye/eyebrow value-track mesh resolver. DONE 2026-06-04.

**Why:** eye/eyebrow value-track (valueTrack8B @+80) was inert — sub_661F7C was a SCOPE BOUNDARY, so trackResolvedSpan(+288) stayed 0 and trackValue froze at beginFrame. Now ported.

**Addresses:**
- sub_661F7C @0x661F7C = selector/wiring (EmoteMeshResolver_resolve). Callers: EmoteVarController4_step_guess(eye, 0x663bdc, call @0x663cd4) + EmoteEyebrowController_step(0x665600, call @0x66567c). Calls sub_661F7C(self+160, self+80, trackValue, kf.endRad) → sub_660028(a1, a3=trackValue, a4=endVal).
- sub_660028 @0x660028 = 1925-line bounded DFS path-search engine (EmoteMeshResolver_search). 4-state machine (v22&3): 0=init,1=try-close(same edge as a3?),2=explore-neighbours(from v19, match edge.hi),3=backtrack→tail(from-a2 restart). Plus a final from-a3 block (0x6613e0) after valStack drains.
- helpers: sub_686FEC=vector<88B-row>::emplace_back; sub_687234=deque<pair<float,float>> copy-ctor; sub_6827A8=deque<float-pair> ctor(count); sub_6622AC=deque::resize; sub_686D50/sub_687648=deque _M_reallocate_map. ALL = libstdc++ std::deque<pair<float,float>> internals → ported as plain std::deque.

**Data layout (resolver self = controller+160):**
- +160 a1[0..2] edgeTable = vector<{float lo, float hi}> (float intervals). edgeFind(X)=first i where lo<=X&&hi>=X.
- +184 a1[5..12] nodeRows = deque<vector<float>> (per-node neighbour-value lists).
- +264 a1[13..15] outputRows = vector<MeshPathRow{deque<pair<float,float>> path; float dist@+80}>. 88B row.
- +288 *(a1+128) trackResolvedSpan (float). NOTE +288 is INSIDE mesh state now; controllers read mesh.trackResolvedSpan into trackSpan.

**Negative-assertion RESULT:** edgeTable+nodeRows ARE populated locally — both ctors (eye 0x662968, eyebrow 0x66480C) read PSB "edge"/"node" arrays. No missing populator. Local structs only lacked the 88B output-row vector → added via embedded EmoteMeshResolverState mesh in both controllers (replaced the loose edgeTable/nodeRows/trackResolvedSpan fields).

**KEY ALIGNMENT TRAPS:**
1. Row deque is deque<pair<float,float>> (8B elem, 512B blocks), NOT flattened deque<float>. The path stores {from,to} segments.
2. dist == -1.0f is the FAILED-PATH sentinel. emit_label_268/199/447/183 all set i=-1 before emit. Selector (0x662058) skips rows with dist==-1. Successful close (block A, LABEL_317) emits with REAL accumulated dist.
3. Final from-a3 block (0x6613e0, runs when valStack empties at LABEL_199) ALWAYS ends at LABEL_447 with i=-1 → its emitted row is ALWAYS a skipped sentinel. So it is OBSERVABLY INERT to resolver output (only mutates scratch, freed at cleanup). Ported anyway for call-sequence fidelity.
4. No-row/no-valid-row fallback (LABEL_37): push single {endVal,endVal} pair into valueTrack8B, trackResolvedSpan=0.
5. Selector copy (else-branch 0x662148): a2[k]=row[k], all pairs in order (reverse-iteration but index-aligned = forward copy).
6. v18 iteration cap: <=8 at entry (9 passes), bumped ONLY on emit-and-restart paths (block-A-close, emit_label_268), NOT on backtrack continues.

**VERIFICATION:** web+wasmtime build clean; motion_playback wasmtime differential m2logo(93f)+yuzulogo(243f) PASS bit-identical. ORACLE-INERT for logo (eye/eyebrow emote-only, no populated edge/node in logo motions) — logo PASS is non-regression guard, not exercise of the engine. No fixture exercises the resolver end-to-end (verification gap noted honestly).

**CMake:** EmoteMeshResolver.cpp added to cpp/plugins/motionplayer/CMakeLists.txt AND platforms/wasmtime/CMakeLists.txt. tests/differential/native/CMakeLists.txt does NOT compile emote controllers (motion_playback_native omits them) → no change needed there.
