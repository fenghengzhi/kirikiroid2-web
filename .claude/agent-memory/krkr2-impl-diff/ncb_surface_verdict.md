---
name: ncb-surface-verdict
description: Fresh-decompile member-by-member verdict for Player/EmotePlayer/D3DEmotePlayer/ResourceManager/ObjSource NCB registration tables — counts, order, missing/extra members, name mismatches
metadata:
  type: project
---

2026-06-06 **FIFTH pass (SUPERSEDED only for the EmotePlayer count):** its 69+2 count missed UTF-16 `activateSelectorTarget@0x14D7796`; 2026-07-18 byte read + registration decompile corrects this to 70+2. All unrelated findings in this paragraph remain historical evidence.

2026-06-05 **FOURTH pass (SUPERSEDED for EmotePlayer count):** the string-extraction parser reported 69+2 because IDA had not typed the UTF-16 literal at 0x14D7796. Current authority is 70+2; other registrar observations remain historical.

2026-06-05 **THIRD pass — authoritative member-string enumeration** (extracted EVERY string literal from the full decompile of each registrar via the IDA-MCP output download, not partial decompile). **This pass CORRECTS major errors in the 2026-06-05 SECOND pass**, which severely undercounted Player (claimed 78) and mis-flagged EmotePlayer extras + an "onFind" member that does not exist. Method: `curl` the full decompile JSON, regex all `"..."` literals in registration order, drop the `"Function"`/`"Property"` kind tokens, dedup consecutive getter/setter double-refs. Cross-checked against the IDA header annotation and local main.cpp NCB macros (order-aware diff).

- **ResourceManager @0x6AB8BC** = 12 members (registrar sub_6EC458): loadSource, clearCache, bufLayer(RO), load, unload, unloadAll, isExistMotion, findMotion, findSource, random, requireLayerId, releaseLayerId. Local ResourceManager registrar matches this 12-member table. **2026-07-18 correction:** `setEmotePSBDecryptSeed/Func` are not port extras; `emoteplayer_entry @0x682528` literally creates two native class methods and injects them into `Motion.ResourceManager` with flags `0x10200` after loading `motionplayer.dll` and attaching `EmotePlayer`. Local registration has been moved to the same emoteplayer post-registration call chain. ✅ FAITHFUL.

- **ObjSource @0x69CCB8** = 6 members (registrar sub_6E3BC8): originX, originY, width, height, clip (5 RO prop), drawLayer (method). Local main.cpp:34 EXACT. new(0x18)=24B. ✅ FAITHFUL.

- **2026-07-18 correction:** EmotePlayer @0x67FAC8 has **70 named members + 2 consts**. The previous extraction missed the UTF-16 literal at `0x14D7796`, which is `activateSelectorTarget`; registration stores callback `0x67581C` at `0x6814AC` and attaches the name at `0x6814D8`. The tail is `isSelectorTarget, activateSelectorTarget, deactivateSelectorTarget, getCommandList`. Local registration and implementation are restored accordingly.

- **D3DEmotePlayer @0x52E504** (DrawDeviceD3D.dll) = 54 named members + 4 consts. Local main.cpp:847 = CLEAN 54=54 (the 2026-06-03 "4 dup" already removed). Kinds match, name/callback mismatches reproduced (clear→create, setTimelineBlendRatio→setTimeline, pass→addPlayCallback, modified(RO)→getPlayCallback). Residual: registration ORDER (local props-first vs binary interleaved) + 4 consts on D3DEmoteModule locally vs D3DEmotePlayer in binary — both low-sev (dict order-insensitive).

- **Player @0x6D69C8** (registrar sub_6F6970 + sub_6D993C) = **92 named members** — the repeated main.cpp "92-entry table" comment is **CORRECT** (SECOND pass's "78, comment is WRONG" was itself wrong — truncated decompile undercount). Local main.cpp:137 = 106 members. Authoritative diff (order-aware):
  - **MISSING in local (binary has, 3):**
    - `defaultSyncActive` (RW Prop) — get=Player_getDefaultSyncActive `return byte_1AB84A8` / set=Player_setDefaultSyncActive `byte_1AB84A8=v&1`. A **class-level GLOBAL bool default**, not a per-instance field. Distinct from `syncActive`(#19, also present in binary).
    - `defaultTransformOrder` (RW Prop) — get=sub_6B097C builds a **4-element TJS Array** from global `dword_1AA40D8[0..3]` / set=sub_6B0AB4. Class-level global default. Distinct from `transformOrder`(#29).
    - `clear` (Func) — descriptor cb=Player_drawToLayerCompat @0x6d89e0 (name/callback look mismatched; decompile Player_drawToLayerCompat before porting).
  - **EXTRA in local (binary 92 lacks, 17):**
    - RM-hoist (8): unload, unloadAll, findMotion, requireLayerId, releaseLayerId, findSource, loadSource, clearCache — already on Motion.ResourceManager (main.cpp:557-568); duplicated onto Player = pure infidelity, not platform-needed.
    - draw-device adapters (7): setClearColor, setResizable, unloadUnusedTextures, captureCanvas, setSize, copyRect, adjustGamma — web-port draw-device facade; platform-boundary candidates (verify no TJS caller before removing).
    - playback shim (2): frameProgress, isPlaying.
  - **`onFindMotion` is CORRECT** (binary index 89 string = literally `onFindMotion`; SECOND pass's "onFind" was the IDA UTF-16LE truncation it warned about but reported anyway). Likewise play/progress/stop/setCameraOffset/modifyRoot/setSlant/setZoom/syncActive/tickCount/frameTickCount/meshDivisionRatio/setVariable/getVariable/releaseSyncWait are ALL genuine binary members (SECOND pass wrongly listed many as "extra").
  - Authoritative binary Player order (0..91): defaultSyncActive, defaultTransformOrder, resourceManager, lastTime, loopTime, variableKeys, chara, stealthChara, motion, stealthMotion, tags, motionKey, project, completionType, preview, priorDraw, outsideFactor, meshDivisionRatio, speed, syncActive, tickCount, frameTickCount, cameraActive, stereovisionActive, outline, meshline, maskMode, colorWeight, independentLayerInherit, transformOrder, coordinate, zFactor, cameraTarget, cameraPosition, cameraFOV, cameraAlive, bounds, playing, allplaying, syncWaiting, frameLastTime, frameLoopTime, hasCamera, angleDeg, angleRad, setCoord, x, y, left, top, setFlip, flipX, flipY, setOpacity, opacity, setVisible, visible, setSlant, slantX, slantY, setZoom, zoomX, zoomY, useD3D, pixelateDivision, setVariable, getVariable, modifyRoot, processedMeshVerticesNum, getLayerNames, play, progress, clear, stop, setCameraOffset, getCameraOffset, releaseSyncWait, draw, setDrawAffineTranslateMatrix, contains, calcViewParam, getCommandList, getLayerMotion, getLayerGetter, getLayerGetterList, skipToSync, onAction, onSync, onGroundCorrection, onFindMotion, isExistMotion, setStereovisionCameraPosition.

- **Motion namespace free fns** @0x6D9B08: after 10 subclasses, registers doAlphaMaskOperation + getD3DAvailable on Motion dispatch (sub_6FCAAC). Local relocates to PostRegistCallback (main.cpp:729) — same end-state, M6-fix timing. ✅ aligned.

**RESOLVED 2026-06-05 (full 1:1 path, user-chosen):** Both Player and EmotePlayer NCB tables now match binary set EXACTLY (verified by member enumeration):
- EmotePlayer: 70+2, including the previously missed UTF-16 `activateSelectorTarget`.
- Player: 92, removed all 17 extras (NCB lines only; C++ methods kept — `frameProgress` confirmed internal-C++-only caller, no TJS dependency), added 3 missing: `defaultSyncActive` (static bool=true), `defaultTransformOrder` (static int[4]={0,3,2,1}, getter→TJS Array, setter validates {0,1,2,3} permutation via `PropGetByNum(TJS_MEMBERMUSTEXIST,...)`), `clear` (faithful port of Player_drawToLayerCompat@0x6D2DA0 with 2 documented field-mapping gaps: runtime member-index cache dword_1AB8820 unresolvable in static .so; +864/+884 pixel draw-rect cluster has no local field, falls back to bounds AABB).
- Web Debug build PASS (16/16). Changes are oracle-inert for logo path (removed methods unused/internal; additions are global-config + gated-off clear) → logo motion_playback differential guarded by CI wasmtime job (wasmtime not installed locally). RM/ObjSource/D3DEmotePlayer/Motion-ns unchanged (already faithful).
- Order within Player block is NOT binary-ordered (grouped by category) — low-sev, dict order-insensitive, not pursued (consistent with D3DEmotePlayer treatment).
