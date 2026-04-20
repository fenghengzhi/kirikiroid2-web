# Player Rendering Architecture — libkrkr2.so Complete Analysis

> Analysis target: libkrkr2.so (kirikiroid2 Android, arm64-v8a)
> Analysis method: IDA Pro MCP decompilation of all functions in the Player render pipeline
> Date: 2026-04-04 (revised with corrections)

---

## 1. Top-Level Draw Dispatch (sub_6D5FB8)

Player_draw (0x6D5FB8) is the TJS `draw()` raw callback entry point. It dispatches to one of three rendering paths based on the argument type:

```
sub_6D5FB8(player, arg):
    if arg is D3DAdaptor:
        Player_drawD3D(player)              // D3D rendering path
        return
    
    if arg is SeparateLayerAdaptor:
        Player_DrawSLA(player)              // SLA rendering path (used by yuzulogo)
        return
    
    // Layer + bounds rendering path (non-D3D, non-SLA)
    if sub_6D5164(player, renderList, boundsList):
        if player.wasD3DMode:
            // Create D3DAdaptor, render to it, capture to layer
            D3DAdaptor_renderFromPlayer(adaptor, player, renderList)
            D3DAdaptor_captureCanvas(adaptor, layerArg)
        else:
            // Direct layer rendering
            Player_applyTranslateOffset(player, renderList)   // 0x6D5264
            Player_renderToCanvas(player, layerArg, renderList, boundsList)
            Player_updateLayerAfterDraw(player, layerArg)
```

## 2. Frame Update Pipeline (Player_updateLayers, 0x6BB33C)

Called every frame after `progress()` advances timelines. This is the core rendering pipeline.

### 2.1 Actual Pipeline Order (verified from decompilation)

The pipeline has **three phases**: pre-loop setup, main node evaluation loop, and post-loop processing. The post-loop functions are called **sequentially, not interleaved** with the main loop.

```
Player_updateLayers(player):
    //=== PHASE 1: Pre-loop Setup ===
    
    // Step 1: Camera velocity accumulation
    //   Apply camera velocity (player+784/792/800) to root node position (root+1592/1600/1608)
    //   Mark root dirty (root+1584 = 1) if any velocity is non-zero
    //   Apply damping: velocity *= pow(damping, dt/60)
    //   where damping = player+600, dt = player+592
    
    // Step 2: Save previous positions for delta calculation
    //   For each node: node+176/184/192 = node+1512/1520/1528
    
    // Step 3: Copy root node accumulated state from interpolated
    //   memcpy(root+1504, root+1584, 0x50)
    //   root+1584 = 0 (clear dirty flag)
    
    // Step 4: Variable/parameter interpolation (sub_6BBE20) ★ IMPORTANT
    //   Iterates a separate deque (player+1312..1368, 160-byte items)
    //   For each variable: interpolate current value using bezier curves
    //   Calls sub_6C4668 to bind variable values to node properties
    
    // Step 5: Build root node local matrix
    //   if !player+908: sub_699940(rootNode, player+528)
    
    //=== PHASE 2: Main Node Evaluation Loop ===
    
    // For each non-root node (index 1..N):
    //   a) Find parent node (walk parentIndex chain, skip flag 0x40 nodes)
    //   b) Determine if update needed (player+610 || node+47 || parent+1504 || node+1584)
    //   c) sub_699AE4(node, needUpdate, currentTime) — dual-slot interpolation
    //   d) If returned false: skip to next node
    //   e) Check current clip slot done flag (node + 536*slotIndex + 344)
    //      If done: copy parent accumulated state → node accumulated state, skip
    //      If not done: full evaluation (see 2.2)
    
    //=== PHASE 3: Post-loop Processing ===
    
    // Step A: Delta position / prevPos handling
    //   if playing (player+480):
    //     for each node: node+176/184/192 = 0 (zero delta)
    //   else:
    //     for each node: delta = currentPos - prevPos
    //       node+176 = node+1512 - node+176
    //       node+184 = node+1520 - node+184
    //       node+192 = node+1528 - node+192
    
    // Step B: Clear dirty flag
    //   player+610 = 0
    
    // Step C: Sequential post-processing calls (ALL called after the main loop)
    sub_6BC000(player)   // Camera constraint nodes (nodeType=9)
    sub_6BC4F0(player)   // Vertex computation
    sub_6BD8DC(player)   // Visibility flag computation
    sub_6BDA28(player)   // Camera node processing (nodeType=5)
    sub_6BDCC0(player)   // Shape AABB (nodeType=7)
    sub_6BDE94(player)   // Shape geometry (nodeType=1)
    sub_6BE0C0(player)   // Motion sub-nodes (nodeType=3)
    sub_6BEDD0(player)   // Particle emitter (nodeType=6)
    sub_6BF0DC(player)   // Particle system (nodeType=4)
    sub_6C0528(player)   // Anchor node (nodeType=10)
    
    // Step D: Clear timeline dirty flags
    //   for each timeline entry (player+384..392, 56-byte items):
    //     entry+48 = 0
    
    // Step E: Clear global flags
    //   player+608 = 0
    //   player+480 = 0
```

### 2.2 Node Evaluation Detail (Phase 2, step e — when slot NOT done)

When the current clip slot is active (not done), the main loop performs full inheritance:

```
// Initialize accumulated state
node+1504 = 1 (visible)
node+1506 = (parent+1506 != 0) if node has flag, else 0

// Flip XOR inheritance
node+1507 ^= parent+1587     // flipX XOR
node+1508 ^= parent+1588     // flipY XOR

// Transform composition (depends on inheritFlags)
node+1505 = node+1506 & (node+1585 != 0)

// Scale: multiply
node+1544/1552 *= parent+1624/1632    // scaleX/scaleY
// Slant: add
node+1560/1568 += parent+1640/1648    // slantX/slantY
// Opacity: integer multiply
node+1576 = parent+1656 * node+1576 / 255

// Position: add (from interpolated offsets)
node+1512 += parent+1592    // posX
node+1520 += parent+1600    // posY (or posZ depending on node+24)
node+1528 += parent+1608    // posZ (or posY)

// Mesh deformation (sub_69AE74)
if parent.meshType != 0:
    sub_69AE74(parent, node)    // Deform child position based on parent mesh

// Position transform: parent matrix × child position + parent position
if node+24 != 0:  // 3D coordinate mode
    newX = parent.m11 * nodeX + parent.m12 * nodeZ + parent.posX
    newY = parent.m21 * nodeX + parent.m22 * nodeZ + parent.posY
else:  // 2D coordinate mode (default)
    newX = parent.m11 * nodeX + parent.m12 * nodeY + parent.posX
    newZ = parent.m21 * nodeX + parent.m22 * nodeY + parent.posZ

// Ground correction callback (sub_6BAA10)
if node+47:
    sub_6BAA10(player, node, parent)  // Calls TJS "onGroundCorrection"

// Opacity inheritance (conditional)
if (node+40 & 0x400) OR !player+1097:
    node.opacity = sourceNode.opacity * node.opacity / 255
    where sourceNode = parent (if 0x400 set) or root (if !independentLayerInherit)

// === inheritFlags system (node+40 bits 2-8) ===
// See section 2.3 for full documentation
// Then: sub_699940(node, player+528) — build local 2x2 matrix
// Then: matrix = parent.matrix × node.matrix (standard multiply)
```

### 2.3 inheritFlags System (node+40)

The `inheritFlags` field at node+40 controls **per-property inheritance** from parent. This is a complex 3-phase process controlled by bit flags and the `independentLayerInherit` flag (player+1097).

**Bit definitions:**
| Bit | Mask | Property |
|-----|------|----------|
| 2 | 0x004 | flipX |
| 3 | 0x008 | flipY |
| 4 | 0x010 | angle |
| 5 | 0x020 | scaleX |
| 6 | 0x040 | scaleY |
| 7 | 0x080 | slantX |
| 8 | 0x100 | slantY |

**When all bits set (0x1FC):** Simple inheritance from parent — flip XOR, angle add, scale multiply, slant add. Then `sub_699940`, then matrix multiply with parent.

**When some bits NOT set AND independentLayerInherit (player+1097) is true:**
Three-phase process:
1. **Phase 1**: For each SET bit, inherit from parent (XOR/add/multiply as above)
2. **Phase 2**: `sub_699940(node)` — build local matrix
3. **Phase 3**: For each SET bit, re-apply from root (XOR/add/multiply from root node)
Then matrix = parent.matrix × node.matrix

This effectively makes unset-bit properties independent from the parent hierarchy, inheriting only from root.

**When some bits NOT set AND independentLayerInherit is false:**
Two-phase process:
1. **Phase 1**: For each SET bit only, inherit from parent
2. **Phase 2**: `sub_699940(node)` — build local matrix
Then matrix = parent.matrix × node.matrix

Properties with unset bits are not inherited at all (remain at their interpolated values).

### 2.4 Variable/Parameter Interpolation (sub_6BBE20, 0x6BBE20)

Called in Phase 1 Step 4. Iterates a separate deque from the node deque:

```
sub_6BBE20(player):
    // Deque at player+1312..1368 (160-byte items, 3 items per block)
    for each item in deque:
        slotIndex = item+8
        slot = item + 56 * slotIndex
        
        if slot+68 (done flag): continue
        
        if slot+69 (needs interpolation) AND other slot not done:
            // Interpolate between two slots
            t = (currentTime - slot.startTime) / slot.duration
            if slot+96 (has bezier): t = sub_69A754(slot+32, t)
            value = lerp(otherSlot.value, slot.value, t)
        else:
            value = slot.value (at slot+72)
        
        item+16 = value  // store interpolated result
        sub_6C4668(player, item, 0, value)  // bind to nodes
```

### 2.5 Variable Binding (sub_6C4668, 0x6C4668)

Called from sub_6BBE20 to bind interpolated variable values to nodes:

```
sub_6C4668(player, varItem, mode, value):
    // 1. Resolve source name from varItem (uses ":" separator)
    // 2. Look up/create entry in source map (player+264)
    //    New entries get path split by "/" and stored
    //    New entries get scale=1.0 (offset+40)
    // 3. Store value at entry+32
    // 4. Call sub_6B9650 to update resource
    // 5. Iterate child nodes of this source:
    //    For nodeType=3 (motion) and nodeType=4 (particle):
    //      Update timeline parameters based on variable value
    //      Clamp to [min,max] range, scale by factor
    // 6. Store value in parameter map (player+320)
```

## 3. Post-Loop Processing Functions

### 3.1 sub_6BC000 — Camera Constraint (nodeType=9)

Processes camera constraint nodes. Adjusts node positions with anchor/angle/zoom/opacity damping for smooth camera following.

### 3.2 sub_6BC4F0 — Vertex Computation

For each visible node with source:
- Compute origin = pos - matrix × (originOffset + clipTimeOffset)
- Compute 4 corner vertices from origin and source dimensions
- Handle mesh deformation (bezier patch) for meshType != 0
- Write PSB content properties to TJS dict for sub-motion

### 3.3 sub_6BD8DC — Visibility Flag Computation

Sets node+1960 (draw flag) for each non-root node. Full logic:

```
sub_6BD8DC(player):
    for each node (index 1..N):
        parent = findParent(node)
        if !parent.drawFlag:
            parent = parent+1952  // walk up to visible ancestor
        node+1952 = parent  // store visible ancestor pointer
        
        slotIndex = node+1392
        if node[536*slotIndex + 344]:  // slot done
            drawFlag = 0
        elif node+52 == 0:             // no update count
            drawFlag = 0
        elif !node+1505:               // not active
            drawFlag = 0
        elif node+1996:                // force-visible flag
            drawFlag = (node+200 != 0) // hasSource
        else:
            bitmask = isEmoteMode ? 6153 : 6145
            // 6145 = 0x1801 → nodeTypes 0, 11, 12
            // 6153 = 0x1809 → nodeTypes 0, 3, 11, 12
            if (bitmask & (1 << nodeType)):
                drawFlag = (node+200 != 0)  // hasSource
            else:
                drawFlag = 1  // non-renderable types are "visible" for hierarchy
        
        node+1960 = drawFlag
```

### 3.4 sub_6BDA28 — Camera Node Processing (nodeType=5)

Computes cameraOffset (player+144/148) from camera node position. Uses atan2 for orientation angle (player+472).

### 3.5 sub_6BDCC0 — Shape AABB (nodeType=7)

Computes bounding boxes for shape nodes at node+2144.

### 3.6 sub_6BDE94 — Shape Geometry (nodeType=1)

Computes shape vertices based on shapeType:
- 0=point, 1=circle, 2=rect, 3=quad
- Stores at node+1664..1784

### 3.7 sub_6BE0C0 — Motion Sub-nodes (nodeType=3) ★★★

Creates/manages CHILD Player instances for nested motions:
- Resolves motion path from clip dtgt property
- Calls Player_play() on child Player
- Synchronizes timeline (currentTime, loopTime)
- Calls Player_progress_inner() and Player_updateLayers() recursively
- Propagates position/flip/scale/slant/opacity to child Player's root

### 3.8 sub_6BEDD0 — Particle Emitter (nodeType=6)

Creates particle child Players and manages their lifecycle:
- Evaluates particle trigger conditions
- Computes emission timing (frequency fmin/f)
- Position offset from parent

### 3.9 sub_6BF0DC — Particle System (nodeType=4) ★★★

Full particle system:
- Creates child Player instances for each particle
- Random emission (position, angle, velocity)
- Physics: velocity, acceleration, damping
- Coordinate types: rectangular XY (0) or XZ (1)
- Applies drawAffineMatrix transform
- Recursive: calls Player_progress_inner + Player_updateLayers on each particle
- Particle lifecycle: emit, live, die (based on trigger frequency)

### 3.10 sub_6C0528 — Anchor Node Processing (nodeType=10)

Camera follow/anchor constraints:
- Reads width/height from PSB content
- Angle damping with pow()
- Scale damping with pow()
- Position lerp toward target
- Color channel gamma with pow()
- Opacity gamma with pow()

## 4. Additional Functions in Main Loop

### 4.1 sub_69AE74 — Mesh Position Deformation (0x69AE74)

Called during Phase 2 when parent node has meshType != 0 (specifically meshType=1 with flag bit 0x1 at node+2004). Deforms child position based on parent's mesh:

```
sub_69AE74(parent, child):
    if parent.meshVertices empty OR !child+1505 OR !child+200
       OR !(parent+2004 & 1) OR parent.meshType != 1:
        return
    
    // Normalize child position by parent clip dimensions
    clipSlot = parent + 536 * parent.slotIndex
    normalizedX = (child.posX + parent.originX) / parent+232
    normalizedY = (child.posY + parent.posZ_or_Y) / parent+240
    
    // Evaluate mesh at normalized coordinates
    deformedX, deformedY = sub_69B1E8(parent.meshVertices, normalizedX, normalizedY)
    child.posX = deformedX * parent+232 - parent.originX
    child.posY_or_Z = deformedY * parent+240 - parent.posZ_or_Y
    
    // Optional: angle deformation (if parent+2004 & 2 AND child+40 & 0x10)
    if angleFlag:
        // Sample mesh at 4 nearby points (±0.0001)
        // Compute angle from mesh gradient via atan2
        child.angle += average_gradient_angle * 360 / 2π
    
    // Optional: scale deformation (if parent+2004 & 4 AND child+40 & 0x60)
    if scaleFlag:
        // Compute area ratio from mesh jacobian
        scaleFactor = sqrt(area) / 0.0002
        if child+40 & 0x20: child.scaleX *= scaleFactor
        if child+40 & 0x40: child.scaleY *= scaleFactor
```

### 4.2 sub_6BAA10 — Ground Correction Callback (0x6BAA10)

Called during Phase 2 when node+47 is set. Invokes TJS callback for ground plane correction:

```
sub_6BAA10(player, childNode, parentNode):
    if !node.tjsObject+16: return
    
    // Push parent position [1512,1520,1528] and child position to TJS arrays
    // Call TJS: node.tjsObject.onGroundCorrection(parentPositions, childPositions)
    // Read back 3 corrected position values from result array
    // Apply corrected positions to childNode+1512/1520/1528
```

## 5. Render Tree Building (sub_6C4E28 / sub_6C2334)

### 5.1 sub_6C4E28 — Build Render Commands

Called from `Player_renderToCanvas`. Iterates the node deque and builds render commands:

```
sub_6C4E28(player, renderList, boundsList, clipRect):
    for each renderItem in renderList:
        node = renderItem.nodePtr
        
        // Clip against viewport
        computeClipRect(node.vertices, clipRect) → drawRect
        if drawRect is empty: skip
        
        // Acquire TJS Layer for this node
        layerId = requireLayerId(node)
        layer = getLayerById(layerId)
        
        // Set layer properties
        layer.setSize(drawRect.width, drawRect.height)
        layer.fillRect(0, 0, w, h, neutralColor=0)
        
        // Render based on meshType
        switch(node.meshType):
            case 0: // No mesh — affine copy
                layer.affineCopy(source, 0, 0, srcW, srcH,
                    vertex0-offset, vertex1-offset, vertex2-offset,
                    blendMode, stNearest)
            
            case 1: // Bezier patch mesh
                layer.bezierPatchCopy(source, 0, 0, srcW, srcH,
                    meshPoints, subdivU, subdivV,
                    blendMode, stNearest)
            
            case 2: // Mesh deformation
                layer.meshCopy(source, 0, 0, srcW, srcH,
                    meshPoints, meshDivX, meshDivY,
                    blendMode, stNearest)
```

### 5.2 sub_6C2334 — Render Tree Traverse

Huge function (~55K chars decompiled). Builds the final render tree from the node deque by:
1. Creating render items with source references, vertex data, color, opacity
2. Handling meshType-specific vertex arrays
3. Computing per-node clip rectangles
4. Managing render order (priority, priorDraw flag)

### 5.3 sub_6C7440 — Final Render Loop

Huge function (~61K chars decompiled). Actually composites render items onto the target:
1. For each render item in the render tree:
   - Load source texture (via findSource → PSB resource or external file)
   - Apply drawAffineMatrix transform
   - Call appropriate copy method (affineCopy / meshCopy / bezierPatchCopy)
   - Handle blendMode, opacity, color tint

## 6. Node Type System

Each PSB layer node has a `nodeType` stored at node+28 (set from PSB during initialization).

| nodeType | Name | Processing Function | Description |
|----------|------|-------------------|-------------|
| 0 | Object | sub_6BC4F0 (vertex) | Standard image rendering node — generates visible output |
| 1 | Shape | sub_6BDE94 | Geometric shape (point/circle/rect/quad) |
| 3 | Motion | sub_6BE0C0 | Nested motion — creates child Player, recursively renders |
| 4 | Particle | sub_6BF0DC | Particle system — creates child Players for each particle |
| 5 | Camera | sub_6BDA28 | Camera node — computes cameraOffset from position |
| 6 | ParticleEmitter | sub_6BEDD0 | Particle emitter — triggers particle creation |
| 7 | ShapeAABB | sub_6BDCC0 | Shape bounding box computation |
| 9 | CameraConstraint | sub_6BC000 | Camera constraint — min/max/position tracking |
| 10 | Anchor | sub_6C0528 | Anchor/follow constraint with damping |
| 11 | Unknown_11 | — | Renderable in both modes (in visibility bitmask 6145/6153) |
| 12 | Unknown_12 | — | Renderable in both modes (in visibility bitmask 6145/6153) |

**Visibility bitmask in sub_6BD8DC:**
- Non-emote mode: `6145 = 0x1801` → nodeTypes 0, 11, 12 are renderable
- Emote mode: `6153 = 0x1809` → nodeTypes 0, 3, 11, 12 are renderable
- Other nodeTypes (1,4,5,6,7,9,10) are marked visible=1 for hierarchy purposes but don't render directly

**Note:** nodeTypes 2 and 8 are not observed in any processing function or visibility bitmask. They may be reserved/unused or organizational group nodes.

## 7. Key Data Structures

### 7.1 Node Structure (2632 bytes)

Key offsets:
- +24: coordinateMode (int, 0=2D, nonzero=3D — affects position transform axis)
- +28: nodeType (int)
- +36: parentIndex (int)
- +40: inheritFlags (int, bits 2-8 control per-property inheritance)
- +42: flags byte (bit 0x40 = skip in parent chain walk)
- +44: flag byte (used in slot-done path)
- +47: groundCorrection flag (triggers sub_6BAA10 callback)
- +52: update count
- +84..96: transformOrder[4] (int, default [0,1,2,3])
- +120..144: accumulated 2×2 matrix (4 doubles: m11, m12, m21, m22)
- +176/184/192: prevPos / deltaPos (3 doubles, dual-use)
- +200: hasSource (byte)
- +232/240: mesh/clip dimensions (doubles)
- +248/256: origin offset X/Y (doubles)
- +344 (per slot): slot done flag (byte, at node + 536*slotIndex + 344)
- +536*2 = 1072 bytes for 2 clip slots starting at some base
- +1392: current slot index (int)
- +1504: visible flag (byte)
- +1505: active flag (byte)
- +1506: secondary visible flag (byte)
- +1507: accumulated flipX (byte)
- +1508: accumulated flipY (byte)
- +1512/1520/1528: accumulated position X/Y/Z (3 doubles)
- +1536: accumulated angle (double)
- +1544/1552: accumulated scaleX/scaleY (2 doubles)
- +1560/1568: accumulated slantX/slantY (2 doubles)
- +1576: accumulated opacity (int, 0-255)
- +1584: dirty flag (byte, in interpolated section)
- +1585: interpolated active flag
- +1586/1587/1588: interpolated visible/flipX/flipY
- +1592/1600/1608: interpolated position X/Y/Z
- +1624/1632: interpolated scaleX/scaleY (used as parent multipliers)
- +1640/1648: interpolated slantX/slantY (used as parent addends)
- +1656: interpolated opacity (int)
- +1856..1884: vertex output (8 floats, 4 corners)
- +1912: TJS object reference (for nodeType=3)
- +1952: visible ancestor pointer (set by sub_6BD8DC)
- +1960: drawFlag (byte)
- +1996: force-visible flag (int)
- +2000: meshType (int)
- +2004: mesh flags (int, bits: 0x1=enable, 0x2=angle deform, 0x4=scale deform)
- +2024: mesh vertices pointer
- +2144: shape AABB (for nodeType=7)
- +2296: TJS object reference (for nodeType=4)

### 7.2 Player Structure (key offsets)

- +120/128: rootOffset X/Y
- +144/148: cameraOffset X/Y (float)
- +200: root node pointer (first node in deque)
- +208..256: node deque internal pointers (std::deque layout)
- +264..272: source map (for sub_6C4668 variable binding)
- +320: parameter value map
- +384/392: timeline entries (56-byte items)
- +408: event/binding map
- +456: currentTime (double)
- +472: camera angle (double)
- +480: playing flag (byte)
- +528: transform context pointer (passed to sub_699940)
- +592: delta time (double)
- +600: camera damping (double)
- +608: global flag
- +610: dirty flag (cleared after main loop)
- +613: flag (cleared at start of updateLayers)
- +760: renderTree pointer
- +784/792/800: camera velocity X/Y/Z (3 doubles)
- +808..844: drawAffineMatrix (6 doubles: m11, m21, m12, m22, tx, ty)
- +908: skip root matrix flag (byte)
- +936/944: event queue begin/end
- +1092: completionType (byte, 1-byte bool, TJS "completionType"). Byte-verified via Player_getCompletionType@0x6D9634 / Player_setCompletionType@0x6D9640 / Player_ctor@0x6CF0A4. Used at sub_6B3C78@0x6B43A8 (CBZ → `stencilType &= ~4` when `completionType != 0`) and sub_6C2334@0x6C38A0. See analysis/Player_Class_Layout_libkrkr2so.md:214 authoritative table for +1092..+1100 full layout (completionType, speed, cameraActive, stereovisionActive, preview, colorWeight, syncWaiting, playing, cameraAlive — all independent 1-byte bools).
- +1096: preview flag (byte) — was previously mislabeled here as "+1097: independentLayerInherit"; +1097 is actually `colorWeight` per the authoritative layout table.
- +1112: zFactor
- +1144: blendMode (CROSS-REFERENCE: Player_Class_Layout_libkrkr2so.md lists +1144 as `project(int)` — needs re-verification, do not rely on this line)
- +1148: (WRONG label "stencilType" — stencilType is on Node+52, NOT Player. Per Player_Class_Layout_libkrkr2so.md this offset is `maskMode(int)`. Kept here only for diff visibility; use the layout table as authoritative.)
- +1152: processedMeshVerticesNum
- +1312..1368: variable deque internal pointers (160-byte items, for sub_6BBE20)

### 7.3 Clip Slot (536 bytes per timeline)

Each node has 2 clip slots. See sub_692AB0 analysis for full layout.
All properties gated by mask bitmask at clip+20.

### 7.4 Variable Item (160 bytes, in deque at player+1312)

Used by sub_6BBE20:
- +8: current slot index (int)
- +16: interpolated output value (double)
- Per slot (56 bytes each, 2 slots):
  - +56*slotIndex + 56: start time (double)
  - +56*slotIndex + 64: duration (int)
  - +56*slotIndex + 68: done flag (byte)
  - +56*slotIndex + 69: needs interpolation flag (byte)
  - +56*slotIndex + 72: value (double)
  - +56*slotIndex + 96: has bezier flag (int)

## 8. Rendering Paths

### 8.1 Non-D3D Layer Path (current project uses this)
```
draw(layerArg) → sub_6D5164 (build lists) → applyTranslateOffset → renderToCanvas → updateLayerAfterDraw
```

### 8.2 SLA Path
```
draw(SLA) → Player_DrawSLA → creates PrivateMotionGLL child layer under ownerLayer → renders to it
```

### 8.3 D3D Path
```
draw(D3DAdaptor) → Player_drawD3D → renders to D3DAdaptor pixel buffer → captures to layer
```

## 9. What Current Project Implements vs. libkrkr2.so

### Fully Implemented ✅
- Sub_692AB0: All mask-gated property reads (all 21 mask bits)
- Sub_699AE4: Dual-slot interpolation with bezier curves (acc/ccc/zcc/scc/occ)
- Sub_699940: Local 2×2 matrix construction with transformOrder loop
- Sub_6BC4F0: Vertex computation with origin offset (basic, no mesh)
- drawAffineMatrix + cameraOffset + rootOffset composition
- Flip XOR inheritance, opacity int multiplication
- Angle 360° wrap-around interpolation

### Partially Implemented ⚠️
- Sub_6C4E28/sub_6C7440: We do affineCopy but not meshCopy/bezierPatchCopy
- Sub_6BDA28: Camera offset is supported but not computed from camera node position
- Color: RGBA values read but not applied to rendering (no color tint)
- Inheritance: Simple parent inheritance works, but inheritFlags per-property control (node+40 bits 2-8) and independentLayerInherit (player+1097) 3-phase process NOT implemented

### Not Implemented ❌
- **Sub_6BBE20 (Variable interpolation)**: Separate variable deque interpolation + sub_6C4668 binding
- **Sub_6BE0C0 (Motion sub-nodes)**: Requires dynamic child Player creation + recursive updateLayers
- **Sub_6BF0DC (Particle system)**: Full particle physics + child Player lifecycle (~800 lines)
- **Sub_6BEDD0 (Particle emitter)**: Particle trigger/emission control
- **Sub_6BC000 (Camera constraints)**: Anchor follow with damping
- **Sub_6C0528 (Anchor nodes)**: Camera follow with gamma damping
- **Sub_6BDCC0/6BDE94 (Shape nodes)**: Shape geometry + AABB
- **Sub_6BD8DC (Visibility flags)**: Per-node draw flag computation with emote mode bitmask
- **Sub_69AE74 (Mesh position deformation)**: Child position deformation on parent mesh
- **Sub_6BAA10 (Ground correction)**: TJS onGroundCorrection callback
- **Mesh deformation**: bezierPatchCopy and meshCopy in sub_6C4E28
- **Render tree**: sub_6C2334 render tree building with priority/priorDraw
- **Color tint**: Applying RGBA color to rendered output
- **inheritFlags system**: Per-property inheritance control (node+40)

## 10. Architecture Gap: Child Player Instances

The single biggest difference between libkrkr2.so and the current project is **dynamic child Player management**:

1. libkrkr2.so's nodeType=3 (Motion) creates a **new Player instance** for each motion sub-node
2. The child Player has its own PSB, timelines, node deque, and render loop
3. Parent drives child: `Player_progress_inner(child, dt)` + `Player_updateLayers(child)` every frame
4. Parent propagates position/flip/scale/slant/opacity to child's root node
5. Child's render output feeds back into parent's render tree

This architecture requires:
- Player factory with per-node lifecycle management
- Recursive render loop (Player → child Player → grandchild Player...)
- State synchronization between parent and child
- Event propagation (onAction/onSync) across hierarchy

The current project evaluates PSB trees directly without intermediate node structures, so there's no place to attach child Players.

## 11. Function Address Reference

| Address | Name (IDA) | Purpose |
|---------|-----------|---------|
| 0x692AB0 | sub_692AB0 | Clip slot initialization (mask-gated property reads) |
| 0x6926B4 | sub_6926B4 | Keyframe initialization (read time/type/mask) |
| 0x699940 | sub_699940 | Local 2×2 matrix construction (transformOrder) |
| 0x699AE4 | sub_699AE4 | Dual-slot interpolation |
| 0x69A754 | sub_69A754 | Bezier curve evaluation |
| 0x69A4D4 | sub_69A4D4 | Color interpolation with ccc |
| 0x69AE74 | sub_69AE74 | Mesh position deformation (child on parent mesh) |
| 0x6B9650 | sub_6B9650 | Resource update for variable binding |
| 0x6BAA10 | sub_6BAA10 | Ground correction (TJS onGroundCorrection callback) |
| 0x6BB33C | Player_updateLayers | Main per-frame update pipeline |
| 0x6BBE20 | sub_6BBE20 | Variable/parameter interpolation |
| 0x6BC000 | sub_6BC000 | Camera constraint nodes |
| 0x6BC4F0 | sub_6BC4F0 | Vertex computation |
| 0x6BD8DC | sub_6BD8DC | Visibility flag computation |
| 0x6BDA28 | sub_6BDA28 | Camera node processing |
| 0x6BDCC0 | sub_6BDCC0 | Shape AABB computation |
| 0x6BDE94 | sub_6BDE94 | Shape geometry computation |
| 0x6BE0C0 | sub_6BE0C0 | Motion sub-node (child Player) |
| 0x6BEDD0 | sub_6BEDD0 | Particle emitter |
| 0x6BF0DC | sub_6BF0DC | Particle system |
| 0x6C0528 | sub_6C0528 | Anchor node processing |
| 0x6C2334 | sub_6C2334 | Render tree building |
| 0x6C4668 | sub_6C4668 | Variable binding to nodes |
| 0x6C4E28 | sub_6C4E28 | Render command generation |
| 0x6C7440 | sub_6C7440 | Final render compositing |
| 0x6D5FB8 | sub_6D5FB8 | Top-level draw dispatch |
