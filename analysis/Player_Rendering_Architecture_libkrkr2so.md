# Player Rendering Architecture — libkrkr2.so Complete Analysis

> Analysis target: libkrkr2.so (kirikiroid2 Android, arm64-v8a)
> Analysis method: IDA Pro MCP decompilation of all functions in the Player render pipeline
> Date: 2026-04-04

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

### 2.1 Pipeline Stages (in order)

```
Player_updateLayers(player):
    // Stage 0: Camera velocity accumulation
    Apply camera velocity (player+784/792/800) to root node position
    Apply damping: velocity *= pow(damping, dt/60)
    
    // Stage 1: Save previous positions for delta calculation
    for each node: node.prevPos = node.pos
    
    // Stage 2: Copy root node state
    memcpy(root.accumulated, root.interpolated, 0x50)
    
    // Stage 3: Camera constraint (sub_6BC000)
    // Processes nodeType=9 nodes, adjusts all node positions
    
    // Stage 4: Evaluate non-root nodes (sub_699940 + sub_699AE4)
    // For each node from index 1:
    //   - Find parent node
    //   - sub_699AE4: interpolate between two clip slots
    //   - sub_699940: build local 2x2 matrix
    //   - Inherit: matrix multiply, flip XOR, opacity multiply
    //   - Position: parent.matrix × child.pos + parent.pos
    
    // Stage 5: sub_6BC000 — Camera constraint nodes (nodeType=9)
    //   Anchor/angle/zoom/opacity damping for smooth camera follow
    
    // Stage 6: sub_6BC4F0 — Vertex computation
    //   For each visible node with source:
    //   - Compute origin = pos - matrix × (originOffset + clipTimeOffset)
    //   - Compute 4 corner vertices
    //   - Handle mesh deformation (bezier patch)
    //   - Write PSB content properties to TJS dict for sub-motion
    
    // Stage 7: sub_6BD8DC — Visibility flag computation
    //   Sets node+1960 draw flag based on type, update state, source
    
    // Stage 8: sub_6BDA28 — Camera node processing (nodeType=5)
    //   Computes cameraOffset (player+144/148) from camera node position
    //   Uses atan2 for orientation angle
    
    // Stage 9: sub_6BDCC0 — Shape AABB (nodeType=7)
    //   Computes bounding boxes for shape nodes at node+2144
    
    // Stage 10: sub_6BDE94 — Shape geometry (nodeType=1)
    //   Computes shape vertices based on shapeType:
    //     0=point, 1=circle, 2=rect, 3=quad
    //   Stores at node+1664..1784
    
    // Stage 11: sub_6BE0C0 — Motion sub-nodes (nodeType=3) ★★★
    //   Creates/manages CHILD Player instances for nested motions
    //   - Resolves motion path from clip dtgt property
    //   - Calls Player_play() on child Player
    //   - Synchronizes timeline (currentTime, loopTime)
    //   - Calls Player_progress_inner() and Player_updateLayers() recursively
    //   - Propagates position/flip/scale/slant/opacity to child Player's root
    
    // Stage 12: sub_6BEDD0 — Particle emitter (nodeType=6)
    //   Creates particle child Players and manages their lifecycle
    //   - Evaluates particle trigger conditions
    //   - Computes emission timing (frequency fmin/f)
    //   - Position offset from parent
    
    // Stage 13: sub_6BF0DC — Particle rendering (nodeType=4) ★★★
    //   Full particle system:
    //   - Creates child Player instances for each particle
    //   - Random emission (position, angle, velocity)
    //   - Physics: velocity, acceleration, damping
    //   - Coordinate types: rectangular XY (0) or XZ (1)
    //   - Applies drawAffineMatrix transform
    //   - Recursive: calls Player_progress_inner + Player_updateLayers on each particle
    //   - Particle lifecycle: emit, live, die (based on trigger frequency)
    
    // Stage 14: sub_6C0528 — Anchor node processing (nodeType=10)
    //   Camera follow/anchor constraints:
    //   - Reads width/height from PSB content
    //   - Angle damping with pow()
    //   - Scale damping with pow()
    //   - Position lerp toward target
    //   - Color channel gamma with pow()
    //   - Opacity gamma with pow()
    
    // Stage 15: Delta position calculation
    //   node.deltaPos = node.pos - node.prevPos (or zero if mode differs)
    
    // Stage 16: Clear dirty flags
```

## 3. Render Tree Building (sub_6C4E28 / sub_6C2334)

### 3.1 sub_6C4E28 — Build Render Commands

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

### 3.2 sub_6C2334 — Render Tree Traverse

Huge function (~55K chars decompiled). Builds the final render tree from the node deque by:
1. Creating render items with source references, vertex data, color, opacity
2. Handling meshType-specific vertex arrays
3. Computing per-node clip rectangles
4. Managing render order (priority, priorDraw flag)

### 3.3 sub_6C7440 — Final Render Loop

Huge function (~61K chars decompiled). Actually composites render items onto the target:
1. For each render item in the render tree:
   - Load source texture (via findSource → PSB resource or external file)
   - Apply drawAffineMatrix transform
   - Call appropriate copy method (affineCopy / meshCopy / bezierPatchCopy)
   - Handle blendMode, opacity, color tint

## 4. Node Type System

Each PSB layer node has a `nodeType` stored at node+28 (set from PSB during initialization).

| nodeType | Name | Render Stage | Description |
|----------|------|-------------|-------------|
| 0 | Object | Stage 6 (sub_6BC4F0) | Standard image rendering node — the only type that generates visible output in our current implementation |
| 1 | Shape | Stage 10 (sub_6BDE94) | Geometric shape (point/circle/rect/quad) |
| 3 | Motion | Stage 11 (sub_6BE0C0) | Nested motion — creates child Player, recursively renders |
| 4 | Particle | Stage 13 (sub_6BF0DC) | Particle system — creates child Players for each particle |
| 5 | Camera | Stage 8 (sub_6BDA28) | Camera node — computes cameraOffset from position |
| 6 | ParticleEmitter | Stage 12 (sub_6BEDD0) | Particle emitter — triggers particle creation |
| 7 | ShapeAABB | Stage 9 (sub_6BDCC0) | Shape bounding box computation |
| 9 | CameraConstraint | Stage 3 (sub_6BC000) | Camera constraint — min/max/position tracking |
| 10 | Anchor | Stage 14 (sub_6C0528) | Anchor/follow constraint with damping |

## 5. Key Data Structures

### 5.1 Node Structure (2632 bytes)

See `analysis/` earlier docs for full layout. Key offsets:
- +28: nodeType (int)
- +36: parentIndex (int)
- +84..96: transformOrder[4] (int, default [0,1,2,3])
- +120..144: accumulated 2×2 matrix (4 doubles)
- +200: hasSource (byte)
- +1504..1576: accumulated transform state (pos/angle/scale/slant/opacity)
- +1856..1884: vertex output (8 floats, 4 corners)
- +1960: drawFlag (byte)
- +2000: meshType (int)

### 5.2 Player Structure (key offsets)

- +120/128: rootOffset X/Y
- +144/148: cameraOffset X/Y (float)
- +200: root node pointer (first node in deque)
- +456: currentTime
- +472: camera angle
- +480: playing flags
- +592: delta time
- +600: camera damping
- +608..613: various flags
- +760: renderTree pointer
- +784/792/800: camera velocity X/Y/Z
- +808..844: drawAffineMatrix (6 doubles)
- +936/944: event queue begin/end
- +1092: isEmoteMode flag
- +1097: independentLayerInherit flag
- +1112: zFactor
- +1144: blendMode
- +1148: stencilType
- +1152: processedMeshVerticesNum

### 5.3 Clip Slot (536 bytes per timeline)

Each node has 2 clip slots. See sub_692AB0 analysis for full layout.
All properties gated by mask bitmask at clip+20.

## 6. Rendering Paths

### 6.1 Non-D3D Layer Path (current project uses this)
```
draw(layerArg) → sub_6D5164 (build lists) → applyTranslateOffset → renderToCanvas → updateLayerAfterDraw
```

### 6.2 SLA Path
```
draw(SLA) → Player_DrawSLA → creates PrivateMotionGLL child layer under ownerLayer → renders to it
```

### 6.3 D3D Path
```
draw(D3DAdaptor) → Player_drawD3D → renders to D3DAdaptor pixel buffer → captures to layer
```

## 7. What Current Project Implements vs. libkrkr2.so

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

### Not Implemented ❌
- **Sub_6BE0C0 (Motion sub-nodes)**: Requires dynamic child Player creation + recursive updateLayers. This is the biggest architectural gap.
- **Sub_6BF0DC (Particle system)**: Full particle physics + child Player lifecycle. ~800 lines of decompiled code.
- **Sub_6BEDD0 (Particle emitter)**: Particle trigger/emission control.
- **Sub_6BC000 (Camera constraints)**: Anchor follow with damping.
- **Sub_6C0528 (Anchor nodes)**: Camera follow with gamma damping.
- **Sub_6BDCC0/6BDE94 (Shape nodes)**: Shape geometry + AABB.
- **Sub_6BD8DC (Visibility flags)**: Per-node draw flag computation.
- **Mesh deformation**: bezierPatchCopy and meshCopy in sub_6C4E28.
- **Render tree**: sub_6C2334 render tree building with priority/priorDraw.
- **Color tint**: Applying RGBA color to rendered output.

## 8. Architecture Gap: Child Player Instances

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

## 9. Function Address Reference

| Address | Name (IDA) | Purpose |
|---------|-----------|---------|
| 0x692AB0 | sub_692AB0 | Clip slot initialization (mask-gated property reads) |
| 0x6926B4 | sub_6926B4 | Keyframe initialization (read time/type/mask) |
| 0x699940 | sub_699940 | Local 2×2 matrix construction (transformOrder) |
| 0x699AE4 | sub_699AE4 | Dual-slot interpolation |
| 0x69A754 | sub_69A754 | Bezier curve evaluation |
| 0x69A4D4 | sub_69A4D4 | Color interpolation with ccc |
| 0x6BB33C | Player_updateLayers | Main per-frame update pipeline |
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
| 0x6C4E28 | sub_6C4E28 | Render command generation |
| 0x6C7440 | sub_6C7440 | Final render compositing |
| 0x6D5FB8 | sub_6D5FB8 | Top-level draw dispatch |
