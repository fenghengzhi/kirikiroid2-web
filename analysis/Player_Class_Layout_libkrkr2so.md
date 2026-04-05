# Player Class Complete Layout -- libkrkr2.so

> Analysis target: libkrkr2.so (kirikiroid2 Android, arm64-v8a)
> Analysis method: IDA Pro MCP decompilation
> Date: 2026-04-05

---

## 1. Overview

The `Player` class is the core C++ object behind the `Motion.Player` TJS2 class, registered via NCB at `sub_6D69C8` (0x6D69C8). It manages emote/motion animation playback, rendering, and TJS2 property/method bindings.

- **Object size**: 0x568 bytes (1384 bytes), allocated at 0x6F6DF4
- **Constructor**: `sub_6CED30` at 0x6CED30
- **Destructor**: `sub_6CFADC` at 0x6CFADC
- **NCB registration**: `sub_6D69C8` at 0x6D69C8

---

## 2. NCB Class Registration (sub_6D69C8, 0x6D69C8)

The registration function is massive (~160KB decompiled). It registers:
- 1 constructor (Function type, first entry)
- ~75 properties (Property type, getter/setter pairs)
- ~20 methods (Function type, raw callbacks)

### 2.1 Constructor Chain

```
sub_6D69C8 (NCB registration)
  -> registers constructor vtable off_1A1F938
     -> sub_6F6BD0 (NCB constructor dispatch, 0x6F6BD0)
        -> sub_6F6CA8 (NCB instance creation, 0x6F6CA8)
           -> sub_6F6DC0 (Player factory, 0x6F6DC0)
              -> operator new(0x568) -- allocates 1384 bytes
              -> sub_6CED30 (actual constructor, 0x6CED30)
```

### 2.2 Registered Properties

| TJS Property | Getter Function | Setter Function | Type | Player Offset | Notes |
|---|---|---|---|---|---|
| `defaultSyncActive` | sub_6D93F8 | sub_6D9404 | bool | static byte_1AB84A8 | Class-level static |
| `defaultTransformOrder` | sub_6D9414 | -- (RO) | ttstr | +992 | Copy via sub_A0F5E0 |
| `resourceManager` | sub_6D9420 | -- (RO) | double | +1128 | Returns value * 1000/60 if > 0 |
| `lastTime` | sub_6D9448 | -- (RO) | double | +1136 | Returns value * 1000/60 if > 0 |
| `loopTime` | sub_6D139C | -- | TJS Array | deque iter | Iterates deque at +1312..1368 |
| `variableKeys` | sub_6D9470 | sub_6C0E9C | tTJSVariant* | +960 | AddRef pattern |
| `chara` | sub_6D9490 | sub_6D94B0 | tTJSVariant* | +968 | Setter calls sub_6B29C0 if motion loaded |
| `stealthChara` | Player_getMotion_ncb | Player_setMotion | tTJSVariant* | +976/768 | Uses Player_playImpl |
| `motion` | Player_getStealthMotion | sub_6D9584 | tTJSVariant* | +984/768 | Setter calls Player_playImpl(0x10) |
| `stealthMotion` | -- | sub_6D9618 | ttstr | +1072 | Copy via sub_A0F5E0 |
| `tags` | sub_695BE0 | sub_6B4978 | tTJSVariant | via TJS dispatch | Complex getter |
| `motionKey` | sub_695BE0 | sub_6B4978 | tTJSVariant | via TJS dispatch | Same as tags pattern |
| `project` | sub_6D9624 | sub_6D962C | int | +1144 | Raw int32 |
| `completionType` | sub_6D9634 | sub_6D963C | bool | +1092 | byte, masked & 1 |
| `preview` | sub_6D9648 | sub_6D9650 | bool | +1096 | byte, masked & 1 |
| `priorDraw` | sub_6D965C | sub_6D9664 | double | +1160 | Raw double |
| `outsideFactor` | sub_6D966C | sub_6D9674 | double | +1176 | Raw double |
| `meshDivisionRatio` | sub_6D967C | sub_6D9684 | double | +1168 | Raw double |
| `speed` | sub_6D968C | sub_6D9694 | bool | +1093 | byte, masked & 1 |
| `syncActive` | -- | -- | -- | -- | Complex registration |
| `tickCount` | sub_6D96A0 | sub_6D96C0 | double | +1120 | Getter: val*1000/60; Setter: val*60/1000, clamps |
| `frameTickCount` | sub_6D9700 | -- (RO) | double | +1120 | Raw double (internal frames) |
| `cameraActive` | sub_6D9708 | sub_6D9710 | bool | +1094 | byte, masked & 1 |
| `stereovisionActive` | sub_6D971C | sub_6D9724 | bool | +1095 | byte, masked & 1 |
| `outline` | sub_6D9730 | sub_6D973C | ttstr | +1032 | Copy/assign via sub_A0F5E0/sub_A0FB64 |
| `meshline` | sub_6D9744 | sub_6D9750 | ttstr | +1052 | Copy/assign via sub_A0F5E0/sub_A0FB64 |
| `maskMode` | sub_6CD710 | sub_6CD724 | -- | -- | Complex |
| `colorWeight` | sub_6D9768 | sub_6CC9D4 | bool | +1097 | byte getter |
| `independentLayerInherit` | sub_6CC188 | sub_6CC2C4 | -- | -- | Complex |
| `transformOrder` | sub_6D9770 | sub_6B4980 | int | root+24 | Reads *(*(player+200)+24) |
| `coordinate` | sub_6D977C | sub_6B498C | double | +1112 | Raw double |
| `zFactor` | sub_6CC714 | -- | -- | -- | Complex |
| `cameraTarget` | sub_6CC874 | -- | -- | -- | Complex |
| `cameraPosition` | sub_6D9784 | -- (RO) | double | +1104 | Raw double |
| `cameraFOV` | sub_6D978C | -- (RO) | bool | +1100 | byte |
| `cameraAlive` | sub_6CCA84 | -- | -- | -- | Complex |
| `bounds` | -- | -- | -- | -- | Complex, iterates AABB |
| `playing` | Player_getPlaying (0x6D9794) | -- (RO) | bool | +1099 | byte |
| `allplaying` | Player_getAllplaying (0x6CCE34) | -- (RO) | bool | recursive | Walks node deque |
| `syncWaiting` | sub_6D979C | -- (RO) | bool | +1098 | byte |
| `frameLastTime` | sub_6D97A4 | -- (RO) | double | +1128 | Raw double (internal frames) |
| `frameLoopTime` | sub_6D97AC | -- (RO) | double | +1136 | Raw double (internal frames) |
| `hasCamera` | sub_6CCF98 | sub_6C1780 | bool | -- | Iterates deque for LayerType==5 |
| `angleDeg` | sub_6CD0C0 | sub_6CD0EC | double | root+1616 | Getter: val*0.0174532925 (deg->rad); Setter: val*57.2957795 (rad->deg) |
| `angleRad` | sub_6C0F84 | -- | double | root+1616 | Direct radians |
| `x` / `left` | sub_6D98A8 | sub_6CD028 | double | root+1592 | Root node X |
| `y` / `top` | sub_6D98B4 | sub_6CD048 | double | root+1600 | Root node Y |
| `flipX` | sub_6D98C0 | sub_6CD068 | bool | root+1587 | Root node flipX |
| `flipY` | sub_6D98CC | sub_6CD08C | bool | root+1588 | Root node flipY |
| `opacity` | sub_6D98D8 | sub_6C1028 | int | root+1656 | Root node opacity (int32) |
| `visible` | sub_6D98E4 | sub_6C1048 | bool | root+1586 | Root node visible |
| `slantX` | sub_6D98FC | sub_6D137C | double | root+1648 | Root node slantX |
| `slantY` | sub_6D9908 | sub_6D131C | double | root+1624 | Root node slantY |
| `zoomX` | sub_6D9914 | sub_6D133C | double | root+1632 | Root node zoomX |
| `zoomY` | sub_6D98F0 | sub_6D135C | double | root+1640 | Root node zoomY |
| `useD3D` | sub_6D992C | sub_6D9934 | int | +912 | int32 |
| `pixelateDivision` | sub_6D992C | sub_6D9934 | int | +912 | Same as useD3D? Needs verification |
| `processedMeshVerticesNum` | -- | -- | -- | -- | Complex registration |

### 2.3 Registered Methods

| TJS Method | Implementation | Address | Notes |
|---|---|---|---|
| `progress` | Player_progressCompat | 0x6D2A98 | Raw callback via ncb_createFuncWrapper |
| `draw` ("d") | Player_drawCompat | 0x6D5FB8 | Raw callback, dispatches D3D/SLA/Layer |
| `stop` | Player_stop | 0x6D9A30 | Sets byte at +1099 = 0 |
| `setCoord` | sub_6CCFF8 | 0x6CCFF8 | Sets root+1592 (x), root+1600 (y) |
| `setFlip` | sub_6C0F1C | 0x6C0F1C | Sets root+1587, root+1588 |
| `setOpacity` | sub_6C1028 | 0x6C1028 | Sets root+1656 |
| `setVisible` | sub_6C1048 | 0x6C1048 | Sets root+1586 |
| `setVariable` | sub_6CD39C | 0x6CD39C | Complex |
| `getVariable` | sub_6D1018 | 0x6D1018 | Complex |
| `modifyRoot` | sub_6CD0B0 | 0x6CD0B0 | Sets root dirty flag (root+1584 = 1) |
| `getLayerNames` | -- | -- | -- |
| `getCameraOffset` | -- | -- | -- |
| `setCameraOffset` | sub_6D9A38 | 0x6D9A38 | Sets +144 (float), +148 (float) |
| `releaseSyncWait` | sub_6D9A48 | 0x6D9A48 | Sets byte at +1098 = 0 |
| `setDrawAffineTranslateMatrix` | -- | -- | -- |
| `contains` | sub_6D1528 | 0x6D1528 | Complex |
| `calcViewParam` | -- | -- | -- |
| `getCommandList` | sub_6D3998 | 0x6D3998 | Complex |
| `getLayerMotion` | sub_6D38F4 | 0x6D38F4 | Uses sub_6B5AD8 lookup |
| `getLayerGetter` | sub_6D4F88 | 0x6D4F88 | Complex |
| `getLayerGetterList` | -- | -- | -- |
| `skipToSync` | loc_6D3504 | 0x6D3504 | Raw address |
| `onAction` | sub_6D9A58 | 0x6D9A58 | Copy variant via sub_A0F5E0 |
| `onSync` | -- | -- | -- |
| `onGroundCorrection` | sub_6D9A60 | 0x6D9A60 | Copy variant via sub_A0F5E0 |
| `onFindMotion` | sub_6D07F4 | 0x6D07F4 | Complex |
| `isExistMotion` | sub_6D0040 | 0x6D0040 | Complex |
| `setStereovisionCameraPosition` | -- | -- | -- |

---

## 3. Player Object Layout (0x568 = 1384 bytes)

Based on constructor (sub_6CED30) initialization and property getter/setter decompilation.

### 3.1 Direct Player Fields

| Offset | Size | Type | Name | Init Value | Evidence |
|---|---|---|---|---|---|
| +0 | 8 | ptr | self_ptr | = this | `*a1 = a1` in ctor |
| +8 | 8 | ptr | next/prev | 0 | `a1[1] = 0` |
| +16 | 8 | ptr | objthis | 0 (set at runtime) | `*(_QWORD *)(v8 + 16) = a4` in progressCompat |
| +24..72 | 48 | struct | nodeDequeHead | 0 | `memset(a1+9, 0, 0x48)` -- deque base |
| +72..128 | varies | struct | deque internals | -- | `a1[9]..a1[17]` |
| +120 | 8 | double | rootOffsetX | -- | Used in updateLayers camera |
| +128 | 8 | double | rootOffsetY | -- | Used in updateLayers camera |
| +144 | 4 | float | cameraOffsetX | 0.0f | sub_6D9A38: setCameraOffset |
| +148 | 4 | float | cameraOffsetY | 0.0f | sub_6D9A38: setCameraOffset |
| +152 | 8 | double | boundsMinX | DBL_MAX (0x7FEFFFFFFFFFFFFF) | `a1[19] = 0x7FEFFFFFFFFFFFFFLL` |
| +160 | 8 | double | boundsMinY | -- | |
| +168 | 8 | double | boundsMaxX | -DBL_MAX (0xFFEFFFFFFFFFFFFF) | `a1[22] = 0xFFEFFFFFFFFFFFFFLL` |
| +176 | 8 | double | boundsMaxY | -- | |
| +184..232 | 48 | struct | nodeAllocator/deque | -- | Cleaned in destructor |
| +200 | 8 | ptr | rootNodePtr | -- | **Critical**: Most property getters read `*(player+200)` to access root node |
| +264..312 | 48 | struct | hashMap1 | -- | buckets+chainList, destroyed in dtor |
| +320..368 | 48 | struct | hashMap2 | -- | buckets+chainList, destroyed in dtor |
| +376 | 8 | ptr | activeTimeline | -- | Checked in progress_inner |
| +384 | 8 | ptr | renderList.begin | -- | Array of 56-byte entries |
| +392 | 8 | ptr | renderList.end | -- | Destructor iterates 56-byte stride |
| +400..448 | varies | struct | renderList capacity | -- | |
| +408..432 | varies | struct | someList | -- | Cleaned via sub_6DD144 in dtor |
| +456 | 8 | double | clampedEvalTime | -- | Current eval time (clamped to totalFrames) |
| +464 | 8 | double | emoteAngle | -- | Used when +482 (emoteMode) is true |
| +472 | 8 | double | cameraAngle | -- | Stereovision camera |
| +480 | 2 | uint16 | progressFlags | 257 | `*(_WORD *)(a1 + 480) = 257` |
| +482 | 1 | bool | emoteMode | 0 | Controls angle set path (emote vs root node) |
| +483 | 1 | bool | motionCompleted | 0 | Set in progress, checked for early exit |
| +484..508 | 24 | struct | ttstr field | -- | sub_A0F778 release in dtor |
| +508..528 | 20 | struct | ttstr field | -- | sub_A0F778 release in dtor |
| +528..548 | 20 | struct | ttstr field | -- | sub_A0F778 release in dtor |
| +548..568 | 20 | struct | ttstr field | -- | sub_A0F778 release in dtor |
| +592 | 8 | double | deltaTime | -- | `*(double*)(a1+592) = speedScale * dt` in progress |
| +600 | 8 | double | cameraDamping | 1.0 | Used in updateLayers: velocity *= pow(damping, dt/60) |
| +608 | 1 | bool | updateMarker | -- | Sub_6BBDF8 flag, cleared after updateLayers |
| +609 | 1 | bool | syncPlayFlag | -- | Checked in progress_inner for sync play |
| +610 | 1 | bool | forceUpdate | 0 | Forces full node evaluation in updateLayers |
| +611 | 1 | bool | flag_611 | 0 | |
| +616..636 | 20 | struct | ttstr field | -- | sub_A0F778 release in dtor |
| +636..656 | 20 | struct | ttstr/variant | -- | sub_A0F5E0 init from constructor arg |
| +656..676 | 20 | struct | ttstr field | -- | sub_A0F778 release in dtor |
| +676..696 | 20 | struct | ttstr/variant | -- | sub_A0FCC0 init with random gen |
| +696..716 | 20 | struct | ttstr field | -- | sub_A0F778 release in dtor |
| +716..736 | 20 | struct | ttstr/variant | -- | sub_A0FCC0 init, "color" param set |
| +736..756 | 20 | struct | ttstr field | -- | sub_A0F778 release in dtor |
| +760 | 8 | ptr | d3dAdaptorPtr | 0 (or null) | Deleted in destructor; sub_6CFFB8 cleanup |
| +768 | 8 | ptr | stealthMotionVar | 0 | tTJSVariant*, released in dtor |
| +776 | 8 | ptr | charaMotionVar | 0 | tTJSVariant*, released in dtor |
| +784 | 8 | double | cameraVelocityX | 0 | updateLayers phase 1 |
| +792 | 8 | double | cameraVelocityY | 0 | updateLayers phase 1 |
| +800 | 8 | double | cameraVelocityZ | 0 | updateLayers phase 1 |
| +808 | 8 | double | boundsCalc_field1 | 1.0 | `a1[101] = 0x3FF0000000000000` |
| +832 | 8 | double | boundsCalc_field4 | 1.0 | `a1[104] = 0x3FF0000000000000` |
| +840 | 8 | ptr | field_105 | 0 | `a1[105] = 0` |
| +864..908 | 44 | struct | someContainer | -- | sub_7E2344 init, sub_7E24AC destroy |
| +908 | 1 | bool | skipRootMatrix | 0 | `if (!player+908): sub_699940(rootNode, player+528)` |
| +909 | 1 | bool | useD3DFlag | v19 (from defaultSyncActive) | |
| +912 | 4 | int | pixelateDivision | -- | sub_6D992C getter, sub_6D9934 setter |
| +936 | 8 | ptr | variableList.begin | -- | 44-byte stride entries, dtor iterates |
| +944 | 8 | ptr | variableList.end | -- | |
| +960 | 8 | ptr* | variableKeys | -- | tTJSVariant*, AddRef pattern |
| +968 | 8 | ptr* | charaVariant | -- | tTJSVariant*, AddRef pattern |
| +976 | 8 | ptr* | motionVariant | -- | Player_getMotion_ncb reads this |
| +984 | 8 | ptr* | stealthMotionVariant | -- | Player_getStealthMotion reads this |
| +992 | 20 | ttstr | transformOrderStr | -- | sub_6D9414 getter |
| +1012 | 20 | ttstr | emoteEditVariant | -- | |
| +1032 | 20 | ttstr | outline | -- | sub_6D9730/sub_6D973C |
| +1052 | 20 | ttstr | meshline | -- | sub_6D9744/sub_6D9750 |
| +1072 | 20 | ttstr | stealthMotionStr | -- | sub_6D9618 getter |
| +1092 | 1 | bool | completionType | 0 | sub_6D9634/sub_6D963C |
| +1093 | 1 | bool | speed (bool flag) | defaultSyncActive | sub_6D968C/sub_6D9694 |
| +1094 | 1 | bool | cameraActive | 0 | sub_6D9708/sub_6D9710 |
| +1095 | 1 | bool | stereovisionActive | 0 | sub_6D971C/sub_6D9724 |
| +1096 | 1 | bool | preview | 0 | sub_6D9648/sub_6D9650 |
| +1097 | 1 | bool | colorWeight (bool) | -- | sub_6D9768 getter |
| +1098 | 1 | bool | syncWaiting | 0 | sub_6D979C getter, sub_6D9A48 clears |
| +1099 | 1 | bool | playing | 0 | Player_getPlaying, Player_stop sets to 0 |
| +1100 | 1 | bool | cameraAlive/FOV | 0 | sub_6D978C getter |
| +1104 | 8 | double | cameraPosition | -- | sub_6D9784 getter |
| +1112 | 8 | double | zFactor/coordinate | -- | sub_6D977C getter |
| +1120 | 8 | double | frameTickCount | 0 | Current position in internal frames |
| +1128 | 8 | double | frameLastTime | 0 | Total frames of current motion |
| +1136 | 8 | double | frameLoopTime | 0 | Loop start point in internal frames |
| +1144 | 4 | int | project | 0 | sub_6D9624/sub_6D962C |
| +1148 | 4 | int | maskMode | 0 | sub_6D9758/sub_6D9760 |
| +1152 | 4 | int | progressCounter | 0 | Cleared at start of progress_inner |
| +1156 | 4 | uint32 | parentColorPacked | 0xFF808080 | Set from parent motion node color |
| +1160 | 8 | double | priorDraw | 0 | sub_6D965C/sub_6D9664 |
| +1168 | 8 | double | meshDivisionRatio | 1.0 | sub_6D967C/sub_6D9684 |
| +1176 | 8 | double | outsideFactor | 0 | sub_6D966C/sub_6D9674 |
| +1184..1232 | 48 | struct | hashMap3 | -- | Cleaned in destructor |
| +1240..1288 | 48 | struct | hashMap4 | -- | Cleaned in destructor |
| +1296..1368 | 72 | struct | variableDeque | -- | sub_6CF678 cleanup, 160-byte items |

### 3.2 Root Node Fields (accessed via `*(player+200)`)

The root node is a Node struct (2632 bytes per node). Properties that set root node
fields access via `*(_QWORD*)(player+200)` then offset into the node.

| Node Offset | Size | Type | Property | Evidence |
|---|---|---|---|---|
| +24 | 4 | int | transformOrder | sub_6D9770 getter |
| +1584 | 1 | bool | dirtyFlag | Set to 1 by any setter that changes root |
| +1586 | 1 | bool | visible | sub_6C1048 / sub_6D98E4 |
| +1587 | 1 | bool | flipX | sub_6CD068 / sub_6D98C0 |
| +1588 | 1 | bool | flipY | sub_6CD08C / sub_6D98CC |
| +1592 | 8 | double | x (position) | sub_6CD028/sub_6CCFF8 / sub_6D98A8 |
| +1600 | 8 | double | y (position) | sub_6CD048/sub_6CCFF8 / sub_6D98B4 |
| +1608 | 8 | double | z (position) | -- |
| +1616 | 8 | double | angle (degrees) | sub_6CD0EC / sub_6CD0C0 |
| +1624 | 8 | double | slantY | sub_6D131C / sub_6D9908 |
| +1632 | 8 | double | zoomX | sub_6D133C / sub_6D9914 |
| +1640 | 8 | double | zoomY | sub_6D135C / sub_6D98F0 |
| +1648 | 8 | double | slantX | sub_6D137C / sub_6D98FC |
| +1656 | 4 | int | opacity | sub_6C1028 / sub_6D98D8 |

---

## 4. Constructor Analysis (sub_6CED30, 0x6CED30)

### 4.1 Signature
```c
void Player_ctor(_QWORD *player, __int64 resourceManagerArg);
```

### 4.2 Key Initialization Sequence

```c
Player_ctor(player, rmArg):
    // Self-reference and basic init
    player[0] = player;              // +0: self pointer
    player[1] = 0;                   // +8: null
    memset(player+23, 0, 0x50);      // +184: zero 80 bytes
    memset(player+9, 0, 0x48);       // +72: zero 72 bytes (node deque)
    
    // Initialize node deque at +184
    sub_6F4E90(player+23, 0);        // NodeDeque init
    
    // Render list allocation (two lists)
    // List 1: player+33..38 (offsets +264..+304)
    //   capacity allocation via sub_149EDF8(player+37, 10)
    //   Allocates 10-slot array of 8-byte pointers
    
    // List 2: player+40..45 (offsets +320..+360)
    //   Same pattern, 10-slot initial allocation
    
    // Various flag inits
    player[50] = 0;                  // +400
    
    // String/variant field initialization
    sub_A0F5E0(player+82, rmArg);    // +636..656: copy resourceManager
    sub_A0F5E0(player+82, rmArg);    // +656..676: copy
    
    // More container inits...
    sub_7E2344(player+108);          // +864: container init
    
    // Critical value initializations:
    byte(+909) = 0;                  // d3d mode flag
    player[123] = 0;                 // +984
    dword(+912) = 100;              // pixelateDivision default = 100
    
    // String field inits (all zeroed)
    oword(+936..+984) = 0;
    
    // Random generator creation
    v17 = sub_9C8440(0);             // Create TJS object (Math.RandomGenerator?)
    sub_A0FCC0(player+84, v17);      // Store at +676
    player[2] = 0;                   // +16: objthis = null
    
    // Color variant setup
    v18 = sub_9C8440(0);             // Another TJS object
    sub_A0FCC0(player+89, v18);      // Store at +716
    // Call vtable[6](v17, 512, L"color", ...) -- set color param
    
    // Flags
    byte(+610) = 0;                  // forceUpdate
    byte(+482) = 0;                  // emoteMode
    player[140] = 0;                 // +1120: frameTickCount
    byte(+1092) = 0;                 // completionType
    word(+549*2=1098) = 0;           // syncWaiting|playing packed
    player[59] = 0;                  // +472
    
    // Double precision constants
    player[146] = 1.0;              // +1168: meshDivisionRatio = 1.0
    byte(+483) = 0;                 // motionCompleted
    
    // Sync/speed flags
    byte(+1093) = byte_1AB84A8;    // speed flag = defaultSyncActive
    word(+480) = 1;                 // progressFlags: LSB=1
    word(+608) = 1;                 // updateMarker
    
    // Color defaults
    dword(+1156) = 0xFF808080;     // parentColorPacked = gray
    word(+1096) = 0;               // preview|cameraActive packed
    byte(+1100) = 0;               // cameraAlive
    dword(+1144) = 0;              // project
    player[18] = 0;                // +144: cameraOffset = 0
    player[100] = 0;               // +800
    
    // Bounds init
    player[75] = 1.0;             // +600: cameraDamping = 1.0
    byte(+908) = 0;               // skipRootMatrix
    
    // AABB bounds defaults
    player[19] = DBL_MAX;          // +152: boundsMinX = max
    player[22] = -DBL_MAX;         // +176: boundsMaxX = -max
    player[101] = 1.0;            // +808
    player[104] = 1.0;            // +832
    player[105] = 0;              // +840
    byte(+611) = 0;
    player[147] = 1.0;            // +1176: outsideFactor? or another scale
    player[47] = 0;               // +376: activeTimeline
    word(+612) = 0;
    player[145] = 1.5;            // +1160: (0x3FF8000000000000 = 1.5)
    
    // Init SIMD constants for transforms
    oword(+160) = xmmword_14D68E0; // Bounds init data
    qword(+1148) = 0;             // maskMode = 0
    
    // Push initial root node
    // (deque push logic for first node at +184)
    // Copy default transform values from global dword_1AA40D8..E4
    
    player[95] = 0;               // +760: d3dAdaptorPtr = null
    dword(+904) = 0;
```

---

## 5. Destructor Analysis (sub_6CFADC, 0x6CFADC)

### 5.1 Cleanup Order

```c
Player_dtor(player):
    sub_6CDE18();                    // Pre-cleanup (stop timelines?)
    
    // 1. Release render list entries (stride=56 bytes)
    for each entry in [+384..+392]:
        if entry->variant: tTJSVariant_Release(entry->variant)
    
    // 2. Clean variable deque (+1296)
    sub_6C0DE8(player+1296)
    
    // 3. Clean node system
    sub_6B56F8(player)
    
    // 4. Delete D3D adaptor
    if player[+760]:
        sub_6CFFB8(player[+760])
        operator delete(player[+760])
    
    // 5. Clean node allocator
    sub_6F436C(player+184, savedState)
    sub_6CF678(player+1296)
    
    // 6. Free hashMap at +1240 (buckets + chain list)
    // 7. Free hashMap at +1184
    
    // 8. Release ttstr fields (reverse order)
    sub_A0F778(+1072)  // stealthMotionStr
    sub_A0F778(+1052)  // meshline
    sub_A0F778(+1032)  // outline
    sub_A0F778(+1012)  // emoteEditVariant
    sub_A0F778(+992)   // transformOrderStr
    
    // 9. Release TJS variants
    tTJSVariant_Release(player[+984])  // stealthMotionVariant
    tTJSVariant_Release(player[+976])  // motionVariant
    tTJSVariant_Release(player[+968])  // charaVariant
    tTJSVariant_Release(player[+960])  // variableKeys
    
    // 10. Clean variable list (44-byte stride)
    for each in [+936..+944]:
        sub_A0F778(entry+24)  // release ttstr
        sub_A0F778(entry+4)   // release ttstr
    operator delete(player[+936])
    
    // 11. Clean container at +864
    sub_7E24AC(player+864)
    
    // 12. Release motion variants
    tTJSVariant_Release(player[+776])  // charaMotionVar
    tTJSVariant_Release(player[+768])  // stealthMotionVar
    
    // 13. Release remaining ttstr fields
    sub_A0F778(+736), (+716), (+696), (+676), (+656), (+636), (+616)
    sub_A0F778(+548), (+528), (+508), (+484)
    
    // 14. Clean render list (+408)
    sub_6DD144(player+408, player[+424])
    
    // 15. Release render list again and free
    for each in [+384..+392]: release + free
    
    // 16. Free hashMap at +320
    // 17. Free hashMap at +264
    
    // 18. Clean node system
    sub_6CF9B4(player+184)
    sub_6DD228(player+24, player[+40])
```

---

## 6. Key Method Analysis

### 6.1 Player_progressCompat (0x6D2A98)

TJS entry point for `progress(ms)`. Converts ms to internal frames, runs the full pipeline.

```c
Player_progressCompat(self, numparams, params, objthis):
    if !objthis: return E_FAIL
    player = getNativeInstance(objthis)
    if !player: return E_FAIL
    if numparams < 1: return E_BADPARAMCOUNT
    
    dt = convertToDouble(params[0])    // ms from TJS
    player.objthis = objthis           // player+16 = objthis
    
    Player_progress_inner(player, dt * 60.0 / 1000.0)  // ms -> internal frames
    Player_updateLayers(player)         // 0x6BB33C
    Player_calcBounds(player)           // 0x6C3D04
    Player_dispatchEvents(player, player.objthis)  // 0x6C4490
    
    player.objthis = 0                  // clear
    return S_OK
```

### 6.2 Player_progress_inner (0x6C106C)

Core timeline advancement logic. Handles forward/backward play, looping, sync waits.

Key fields used:
- `+592`: deltaTime (speed * dt)
- `+1168`: speed multiplier
- `+1120`: current frameTickCount
- `+1128`: total frames (frameLastTime)
- `+1136`: loop time (frameLoopTime)
- `+1099`: playing flag
- `+1098`: syncWaiting flag
- `+482`: emoteMode
- `+480`: progress flags
- `+481`: first-frame flag

### 6.3 Player_stop (0x6D9A30)

```c
Player_stop(player):
    byte(player + 1099) = 0;    // Set playing = false
```

### 6.4 Player_setMotion (0x6C1B20)

```c
Player_setMotion(player, motionArg):
    Player_playImpl(player, motionArg, 0)       // flags=0 (normal play)
    if player[+768]:                             // stealthMotionVar exists?
        Player_playImpl(player, player+768, 16)  // flags=16 (stealth)
        tTJSVariant_Release(player[+768])
        player[+768] = 0
```

### 6.5 Time Unit Conversion

The binary uses internal frame units (60fps). TJS API uses milliseconds.

- **TJS -> Internal**: `value * 60.0 / 1000.0`
- **Internal -> TJS**: `value * 1000.0 / 60.0`

Properties that perform conversion:
- `resourceManager` getter (0x6D9420): `*(+1128) * 1000/60`
- `lastTime` getter (0x6D9448): `*(+1136) * 1000/60`
- `tickCount` getter (0x6D96A0): `*(+1120) * 1000/60`
- `tickCount` setter (0x6D96C0): `value * 60/1000`, clamped to [0, frameLastTime]
- `progress` method: `dt * 60/1000`

---

## 7. ResourceManager NCB Registration (0x6AB8BC)

Note: The function at 0x6AB8BC is actually `ResourceManager_ncb_registerMembers`, NOT MotionPlayer.

### Registered Methods

| TJS Method | Implementation | Address |
|---|---|---|
| (constructor) | via off_1A1DAD8 vtable | -- |
| `loadSource` | loc_6A7BA8 | 0x6A7BA8 |
| `clearCache` | loc_6A8438 | 0x6A8438 |
| `bufLayer` | sub_6A84FC | 0x6A84FC (RO property) |
| `load` | ResourceManager_loadResource | named |
| `unload` | sub_6A959C | 0x6A959C |
| `unloadAll` | loc_6A8CF8 | 0x6A8CF8 |
| `isExistMotion` | sub_6A96F8 | 0x6A96F8 |
| `findMotion` | sub_6A9ED4 | 0x6A9ED4 |
| `findSource` | sub_6AAB3C | 0x6AAB3C |
| `random` | sub_6AB56C | 0x6AB56C |
| `requireLayerId` | sub_6AB694 | 0x6AB694 |
| `releaseLayerId` | sub_6AB750 | 0x6AB750 |

---

## 8. Call Chain Summary

```
Player TJS API
├── progress (Player_progressCompat, 0x6D2A98)
│   ├── Player_progress_inner (0x6C106C) -- timeline advance
│   │   ├── Player_initEmoteMotion (when emoteMode)
│   │   ├── sub_6B6878 -- pre-progress setup
│   │   ├── sub_6B86C8 -- forward frame evaluation
│   │   ├── sub_6B6ADC -- forward motion advance
│   │   ├── sub_6B9A3C -- backward motion advance
│   │   └── sub_6B7E44 -- per-node motion eval
│   ├── Player_updateLayers (0x6BB33C) -- node tree eval
│   │   ├── Phase 1: Camera velocity, variable interpolation
│   │   ├── Phase 2: Per-node evaluation loop
│   │   └── Phase 3: Post-processing (camera, mesh, particles, etc.)
│   ├── Player_calcBounds (0x6C3D04) -- AABB computation
│   └── Player_dispatchEvents (0x6C4490) -- TJS callback dispatch
│
├── draw ("d") (Player_drawCompat, 0x6D5FB8)
│   ├── D3D path -> Player_drawD3D
│   ├── SLA path -> Player_DrawSLA
│   └── Layer path -> Player_drawToLayerCompat (0x6D2D80)
│
├── stop (Player_stop, 0x6D9A30) -- sets playing=false
│
├── setMotion (Player_setMotion, 0x6C1B20)
│   └── Player_playImpl (0x6B2284) -- load + start playback
│
├── setCoord (sub_6CCFF8, 0x6CCFF8) -- set root x/y
├── modifyRoot (sub_6CD0B0, 0x6CD0B0) -- mark root dirty
├── setCameraOffset (sub_6D9A38, 0x6D9A38) -- set float offsets
├── releaseSyncWait (sub_6D9A48, 0x6D9A48) -- clear syncWaiting
├── getLayerMotion (sub_6D38F4, 0x6D38F4)
├── getLayerGetter (sub_6D4F88, 0x6D4F88)
├── skipToSync (loc_6D3504, 0x6D3504)
├── contains (sub_6D1528, 0x6D1528) -- hit test
├── isExistMotion (sub_6D0040, 0x6D0040)
└── onFindMotion (sub_6D07F4, 0x6D07F4)
```

---

## 9. Important Constants

| Value | Meaning | Where |
|---|---|---|
| 0x568 (1384) | Player object size | sub_6F6DC0 operator new |
| 2632 (0xA48) | Node struct size | Deque stride in progress_inner, updateLayers |
| 56 (0x38) | Render list entry size | Destructor stride in render list iteration |
| 44 (0x2C) | Variable list entry size | Destructor stride in variable list |
| 160 (0xA0) | Variable deque item size | Referenced in analysis of variableDeque |
| 500 (0x1F4) | Something per-node | operator new(0x1F4) in loopTime getter |
| 0xFF808080 | Default parent color | Constructor: dword(+1156) |
| 100 | Default pixelateDivision | Constructor: dword(+912) = 100 |
| 257 (0x101) | Default progress flags | Constructor: word(+480) = 257 |
| 1.0 | Default meshDivisionRatio | Constructor: player[146] |
| 1.5 | Default priorDraw(?) | Constructor: player[145] = 0x3FF8000000000000 |
| DBL_MAX / -DBL_MAX | Bounds init | Constructor: player[19], player[22] |
