---
name: Player class layout and NCB registration
description: Complete reverse-engineering of motion::Player class - 1384-byte object layout, 75+ properties, 20+ methods, constructor/destructor chains, root node field map
type: project
---

Player class (motion::Player) in libkrkr2.so:
- Object size: 0x568 (1384 bytes), allocated in Player_factory (0x6F6DC0)
- Constructor: Player_ctor (0x6CED30)
- Destructor: Player_dtor (0x6CFADC)
- NCB registration: Player_ncb_registerMembers (0x6D69C8) -- massive ~160KB function
- Root node accessed via *(player+200), node size = 2632 bytes

Key offset groups:
- +0..8: self ptr, +16: objthis (set during progress)
- +144..148: cameraOffset (float pair)
- +152..176: AABB bounds (DBL_MAX/-DBL_MAX init)
- +200: root node pointer (CRITICAL - most property getters use this)
- +384..392: render list (56-byte stride entries)
- +456: clampedEvalTime, +592: deltaTime, +600: cameraDamping
- +768/776: stealth/chara motion TJS variants
- +784..800: camera velocity XYZ
- +908..912: flags (skipRootMatrix, useD3D, pixelateDivision)
- +960..984: variableKeys/chara/motion/stealthMotion TJS variant ptrs
- +992..1072: ttstr fields (transformOrder, emoteEdit, outline, meshline, stealthMotion)
- +1092..1100: 9 independent 1-byte bool fields, NOT a C bitfield. Each has its own TJS getter/setter. Order: completionType(+1092), speed(+1093), cameraActive(+1094), stereovisionActive(+1095), preview(+1096), colorWeight(+1097), syncWaiting(+1098), playing(+1099), cameraAlive(+1100). See project_player_completionType.md for full byte-verified layout.
- +1120: frameTickCount, +1128: frameLastTime, +1136: frameLoopTime
- +1144: project(int), +1148: maskMode(int), +1156: parentColorPacked(0xFF808080)
- +1160: priorDraw, +1168: meshDivisionRatio(1.0), +1176: outsideFactor

Time conversion: internal=frames@60fps, TJS=milliseconds. factor=60/1000.

ResourceManager NCB is at 0x6AB8BC (NOT MotionPlayer).

**Why:** Needed for aligning web port Player.h/cpp to binary layout.
**How to apply:** Reference when modifying any Player property accessor or field order. Cross-check offsets against this map before code changes.
