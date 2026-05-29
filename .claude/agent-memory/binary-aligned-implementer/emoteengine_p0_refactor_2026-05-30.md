---
name: emoteengine-p0-refactor-2026-05-30
description: EmoteEngine 1496B class layout P0 atomic refactor — extracted EmoteEngine to own header, added EmoteVarController/EmoteAngleController POD classes, declared 10 binary-typed deques + 7 controller pointers + HM2@+1440, migrated _emoteDirty/_emoteMeshDivisionRatio from Player to EmoteEngine, implemented progress() skeleton
type: project
---

# EmoteEngine P0 Refactor — 2026-05-30

## Scope completed

5-step structural refactor + 2 new POD classes, single atomic unit:

1. EmoteEngine class moved from inline EmotePlayer.h:56-101 to dedicated
   cpp/plugins/motionplayer/EmoteEngine.{h,cpp}
2. New POD classes:
   - EmoteVarController (0x80=128B) with `EmoteVarController_ctor` /
     `_step` / `_dtor` free functions. Aligned with sub_667030 / sub_666BF8.
   - EmoteAngleController (0x70=112B) with `_ctor` / `_step` / `_dtor`.
     Aligned with sub_6867B0 / sub_666634. Shortest-path angle wrap.
3. EmoteEngine field table reorganized in ascending binary-offset order:
   - 10 std::deque @ +0..+720 with 10 distinct POD element types
     (EmoteHairPartsNode48B, EmoteBustChain1Node56B, ..., EmoteLookupCurve16B_Deque10).
     Each element is `char raw[N]` PLATFORM_BOUNDARY stub until P2 ports the
     typed step functions.
   - 7 controller pointers @ +1072..+1120 as raw `EmoteVarController*` /
     `EmoteAngleController*` (NOT unique_ptr).
   - Player* @ +1064 as raw pointer with manual new/delete (NOT unique_ptr).
   - HM2 (detail::LabelValueMap = std::unordered_map<ttstr, double>) @ +1440.
4. Migrated from Player to EmoteEngine:
   - `_emoteDirty` (was Player+1162) → `_dirty` at EmoteEngine+1162
   - `_emoteMeshDivisionRatio` / `_emoteMeshDivisionRatioDup` → EmoteEngine+1168/+1176
   - All call sites in PlayerCore.cpp / PlayerVariable.cpp / PlayerFrameProgress.cpp
     updated to `_engineBack->_dirty = true` and `_engineBack->_meshDivisionRatio`.
5. EmoteEngine::progress() skeleton implemented matching sub_67D01C:
   - dt-slice while loop with `fmin(dt, 1.1)` physics cap
   - dirty check
   - 6 deque-step STUB_WARN slots (sub_663BDC/665600/666068/666BF8/668470/lookup)
   - applyVarControllers_pos_scale_color_angle() called every slice
   - linked-list bind tail loop placeholder
   - physics-only pass when `dt != 0 && !_syncWaiting` — steps 3 physics-target
     controllers + 3 STUB_WARN physics step calls
6. applyVarControllers_pos_scale_color_angle() steps all 4 direct controllers
   (pos/scale/color/angle) and computes scale denominator at +1176.

## Files created

- cpp/plugins/motionplayer/EmoteEngine.h
- cpp/plugins/motionplayer/EmoteEngine.cpp
- cpp/plugins/motionplayer/EmoteVarController.h
- cpp/plugins/motionplayer/EmoteVarController.cpp
- cpp/plugins/motionplayer/EmoteAngleController.h
- cpp/plugins/motionplayer/EmoteAngleController.cpp
- cpp/plugins/motionplayer/internal/legacy_variable_state.h
  (extracted Player::VariableAnimatorState so EmoteEngine.h can reference
  the legacy storage type without including Player.h)

## Files modified

- cpp/plugins/motionplayer/EmotePlayer.h — removed inline EmoteEngine class;
  forward-include EmoteEngine.h; EmoteObject now holds raw EmoteEngine* with
  manual new/delete; inline EmoteEngine::player() definitions at file tail
  so D3DEmotePlayer's inline accessors resolve in headers.
- cpp/plugins/motionplayer/EmotePlayer.cpp — added EmoteObject::EmoteObject /
  ~EmoteObject manual new/delete bodies.
- cpp/plugins/motionplayer/Player.h — VariableKeyframe / VariableAnimatorState
  aliased to detail::LegacyVariable*; removed _emoteDirty,
  _emoteMeshDivisionRatio, _emoteMeshDivisionRatioDup; added migration notes.
- cpp/plugins/motionplayer/PlayerCore.cpp — Player::setEmoteMeshDivisionRatio
  writes to _engineBack->_meshDivisionRatio[Dup]; startWind reads via
  _engineBack; all `_emoteDirty = true` → `if (_engineBack) _engineBack->_dirty = true`.
- cpp/plugins/motionplayer/PlayerVariable.cpp — same _emoteDirty migration.
- cpp/plugins/motionplayer/PlayerFrameProgress.cpp — same _emoteDirty migration.
- cpp/plugins/motionplayer/CMakeLists.txt — added 3 new .cpp source files.

## Hard rules satisfied (CLAUDE.md)

- Player held by raw pointer with manual new/delete (NOT unique_ptr)
- EmoteEngine held by raw pointer with manual new/delete inside EmoteObject
- 7 controllers are raw pointers with manual new/delete (NOT unique_ptr)
- EmoteVarController + EmoteAngleController are plain POD struct (NO vtable,
  NO inheritance, NO smart pointers); step is a free function.
- 10 deques have 10 distinct element types (NOT uniform abstraction)
- HM2 at +1440 (NOT +1384) with ttstr key + double value
- _dirty migrated to EmoteEngine+1162 (NOT on Player)
- _meshDivisionRatio* migrated to EmoteEngine+1168/+1176

## Build + test results

- Web debug build (out/web/debug): PASS — index.wasm 78MB linked OK
- macOS debug build motionplayer-dll: PASS
- Unit tests: 3 / 7 passed (baseline before refactor was also 3 / 7) — no regressions
- The 4 failing tests are pre-existing failures unrelated to this refactor
  (loadFromSnapshot pipeline + isTimelinePlaying behaviour); none are caused
  by EmoteEngine changes.

## Known TODOs (P1/P2 scope)

- Physics step functions (stepHairParts, stepBust, EmotePhysics_springStep,
  sub_6689A4) — currently STUB_WARN in progress()
- 5 typed deque step functions (sub_663BDC/665600/666068/666BF8/668470) —
  STUB_WARN until binary spec is detailed and POD element types fleshed out
- 4 KiriKiri inline `vector reserve(10)` blocks at +856/+888/+952/+1272/+1328
  — represented as opaque char[] padding with PLATFORM_BOUNDARY notes
- Player_setCoord / setSlant / setColor / setAngleDeg apply path in
  applyVarControllers — controller values computed but not yet applied to player
- Legacy 5 deques + 1 map (`_type4..8ControllerAnimators`, `_variableAnimators`)
  retained alongside binary-typed deques as PLATFORM_BOUNDARY transitional
  storage; cleanup deferred until typed setVariable dispatch is ported

## PLATFORM_BOUNDARY decisions taken

- std::deque header size (libstdc++ 80B vs libc++ ~64B) — local sizeof
  cannot equal 1496B exactly; semantic contract holds (element type,
  block math, lifetime).
- std::unordered_map header size (libstdc++ 56B vs libc++ ~32B) — HM2 size
  parity not reachable on Web.
- 4 KiriKiri inline `vector reserve(10)` blocks — declared as `char[]`
  padding; precise layout reverse pending.
- Matrix/scalar field semantics @ +800..+864, +1128..+1158, +1208..+1271 —
  declared as raw byte regions with `_guess` markers.
- EmoteVarKeyValue20B packed with `#pragma pack(push, 1)` to force 20B
  size (uint64_t natural alignment would pad to 24B).
