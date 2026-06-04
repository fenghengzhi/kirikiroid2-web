---
name: Player NCB property getter/setter symbols off-by-one (FIXED in IDB)
description: Player_ncb_registerMembers @0x6d6c80 had its property getter/setter IDA symbol names systematically shifted to a neighbor member's name. Fixed 44 funcs in IDB 2026-06-04. Authority = addMember L"key" + same struct's +48(getter)/+64(setter). Never trust a Player property accessor symbol — re-check the binding.
type: project
---

# Player NCB table off-by-one — getter/setter symbols mislabeled

`Player_ncb_registerMembers` @0x6d6c80 builds each TJS Property as a 0x50 struct:
`+48 = getter func ptr`, `+64 = setter func ptr`, then `sub_6F6970(*a1, L"key", struct+32)`
registers it. The L"key" + that same struct's +48/+64 are GROUND TRUTH. IDA's auto symbol
names for the getter/setter funcs were shifted by one member (each func got the *adjacent*
member's name), causing real port bugs (loopTime/speed/transformOrder).

## Verification method (reusable)
1. decompile 0x6d6c80, find every `sub_6F6970(*a1, L"key", vN+32)` → key↔struct vN.
2. struct vN's `+48`/`+64` assignment = getter/setter func.
3. decompile each func, confirm field offset matches member semantics (scalar get/set on the
   SAME offset = clean match; object getters build Array/Dict).
4. Rename func → `Player_get<Key>`/`Player_set<Key>` (two-pass via temp names to dodge
   name collisions, since target names already sit on the shifted neighbor).

## Field-offset map established (this+N unless noted; node = *(this+200))
| member | getter@ | setter@ | field |
|---|---|---|---|
| resourceManager | 0x6d9414 | (ro) | +992 obj |
| lastTime | 0x6d9420 | (ro) | +1128 double frame->ms |
| loopTime | 0x6d9448 | (ro) | +1136 double frame->ms |
| variableKeys | 0x6D139C | (ro) | var-track deque @+1296 -> Array (already correct) |
| chara | 0x6d9470 | 0x6c0e9c | +960 obj / set slot0+flush+776 |
| stealthChara | 0x6d9490 | 0x6d94b0 | +968 obj / set slot16+776 |
| tags | 0x6d9618 | (ro) | +1072 obj |
| motionKey & project (ALIAS, same addr) | 0x695be0 | 0x6b4978 | +1012 obj |
| completionType | 0x6d9624 | 0x6d962c | +1144 int |
| preview | 0x6d9634 | 0x6d963c | +1092 bool (the render/timeline gate field) |
| priorDraw | 0x6d9648 | 0x6d9650 | +1096 bool |
| outsideFactor | 0x6d965c | 0x6d9664 | +1160 double |
| meshDivisionRatio | 0x6d966c | 0x6d9674 | +1176 double |
| speed | 0x6d967c | 0x6d9684 | +1168 double _speedMul |
| syncActive | 0x6d968c | 0x6d9694 | +1093 bool |
| colorWeight | 0x6cd710 | 0x6cd724 | +1156 swizzle (already correct) |
| independentLayerInherit | 0x6d9768 | 0x6cc9d4 | +1097 bool (setter propagates dirty to nodes) |
| transformOrder | 0x6cc188 | 0x6cc2c4 | node+84..96 4-int Array perm |
| coordinate | 0x6d9770 | 0x6b4980 | node+24 int |
| zFactor | 0x6d977c | 0x6b498c | +1112 double (setter propagates to children) |
| cameraFOV | 0x6d9784 | (ro) | +1104 double |
| cameraAlive | 0x6d978c | (ro) | +1100 bool |
| angleDeg | 0x6c1780 | 0x6c0f84 | (already correct, not shifted) |
| angleRad | 0x6cd0c0 | 0x6cd0ec | (already correct) |
| useD3D | 0x695de0 | 0x6d9920 | +909 bool |
| slantX | 0x6d98f0 | 0x6d135c | node+1640 double |
| slantY | 0x6d98fc | 0x6d137c | node+1648 double |
| zoomX | 0x6d9908 | 0x6d131c | node+1624 double |
| zoomY | 0x6d9914 | 0x6d133c | node+1632 double |

## Gotchas
- **completionType vs preview swapped** — see [[project_player_completionType]]. +1092 render
  gate is `preview`, not `completionType` (+1144 int).
- **slant/zoom rotated** — symbol names were rotated within the cluster; bind by the field
  offset the func actually reads (1624=zoomX,1632=zoomY,1640=slantX,1648=slantY).
- **motionKey/project alias** — literally the same func pair (0x695be0/0x6b4978, +1012) bound
  to two members; named `Player_get/setMotionKey`, comment notes it serves both.
- angleDeg/angleRad were already corrected in a prior session (not part of this fix).

**How to apply:** Before wiring any local NCB Player property to a libkrkr2 accessor, look up
the func by the field-offset table above (or re-derive from the addMember binding), NOT by the
IDA symbol name. IDB symbols for these 44 funcs are now corrected + commented with the
addMember site + field shape.
