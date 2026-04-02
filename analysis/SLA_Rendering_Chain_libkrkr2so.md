# SLA Rendering Chain — libkrkr2.so Complete Analysis

## Function Chain

```
Player_drawCompat (0x6D5FB8)
  └─ Player_DrawSLA (0x6D5658)
       ├─ sub_6D5164: build/sort render tree
       ├─ Player_applyTranslateOffset_guess (0x6D5264): add camera+root offset
       ├─ Player_ResolveSLATarget_guess (0x6D5948): create PrivateMotionGLL child layer
       └─ Player_RenderMotionFrame_guess (0x6DE738): render to target layer
```

## Step-by-step Coordinate Flow

### Step 1: Player_updateLayers (0x6BB33C) — PSB Tree Evaluation

For each node in the PSB layer tree (each 2632 bytes):

**a) Camera velocity → root offset**
```c
node[1592] += player[592] * player[784];  // rootOffset.x += dt * velocityX
node[1600] += player[592] * player[792];  // rootOffset.y += dt * velocityY
```

**b) Per-node state accumulation**
- Position (double): node+1512, +1520, +1528 (x, y, z)
- 2×2 matrix (double): node+120, +128, +136, +144
- Flip: node+1507, +1508 (flipX, flipY)
- Scale: node+1544, +1552 (scaleX, scaleY)
- Angle: node+1536
- Opacity: node+1576 (int, 0-255)

**c) Matrix inheritance (0x6BB850):**
```c
child[120] = parent[120]*child[120] + parent[128]*child[136];  // m11
child[128] = parent[120]*child[128] + parent[128]*child[144];  // m12
child[136] = parent[136]*child[120] + parent[144]*child[136];  // m21
child[144] = parent[136]*child[128] + parent[144]*child[144];  // m22
```

**d) Position inheritance (0x6BB7CC):**
```c
child[1512] = parent[128]*child[1528] + parent[120]*child[1512] + parent[1512];
child[1520] = ...  (similarly with matrix)
child[1528] = child[1528] + parent[1528];
```

### Step 2: sub_6BC4F0 — Vertex Computation (0x6BCE44..0x6BCEC0)

Computes 4 corner vertices (float) at node+1856..1887 from accumulated state:

```c
// Origin = accumulatedPosition - matrix × originOffset
originOffsetX = node[248] + clip[376];   // from PSB "ox" + parent offset
originOffsetY = node[256] + clip[384];   // from PSB "oy" + parent offset
origin.x = (float)(node[1512] - (node[128]*originOffsetY + originOffsetX*node[120]));
origin.y = (float)(node[1520] - (originOffsetY*node[144] + originOffsetX*node[136]));

// Width/Height from PSB source dimensions
w = node[232];  // source width
h = node[240];  // source height

// 4 corners: origin, origin+right, origin+right+bottom, origin+bottom
vertex0 = (origin.x,                    origin.y)
vertex1 = (origin.x + m11*w,            origin.y + m21*w)
vertex2 = (origin.x + m11*w + m12*h,    origin.y + m21*w + m22*h)
vertex3 = (origin.x + m12*h,            origin.y + m22*h)
```

Where m11=node[120], m12=node[128], m21=node[136], m22=node[144] (accumulated 2×2 matrix).

**These vertices are in PSB native coordinate space** (the motion's own coordinate system).

### Step 3: sub_6C2334 — Render Tree Building (0x6C2B90..0x6C2CD0)

Copies vertices from node+1856 to renderNode+136, then IF drawAffineMatrix flag (+611) is set:

```c
// internal = Player->internalStruct (at Player+0x428/1064)
m11 = *(double*)(internal + 808);
m12 = *(double*)(internal + 816);
m21 = *(double*)(internal + 824);
m22 = *(double*)(internal + 832);
tx  = *(float*)(internal + 840);
ty  = *(float*)(internal + 844);

// For each of the 4 vertices:
new_x = m12 * old_y + m11 * old_x + tx;
new_y = m22 * old_y + m21 * old_x + ty;
```

**After this transform, vertices are in the ownerLayer's coordinate space.**

### Step 4: Player_applyTranslateOffset_guess (0x6D5264)

Adds camera + root offset to ALL vertex positions:

```c
offsetX = *(float*)(player + 144);  // cameraOffsetX (set by setCameraOffset)
offsetY = *(float*)(player + 148);  // cameraOffsetY

// For each render node, for each vertex/bound:
vertex.x += offsetX;
vertex.y += offsetY;
```

Note: rootOffset (player+120/128) is also involved but only when a zoom flag (player+1095) is set, for perspective correction.

### Step 5: Player_ResolveSLATarget_guess (0x6D5948)

Creates `PrivateMotionGLL` (internal Layer subclass):
- **Parent**: ownerLayer (the AffineLayer)
- **Type**: ltAlpha (via g_PrivateMotionGLL_ClassID)
- **Size**: ownerLayer's (right-left, bottom-top)

### Step 6: Player_RenderMotionFrame_guess (0x6DE738)

For each visible render node:
1. Clips bounding box to (0, targetWidth) × (0, targetHeight)
2. Writes vertex data directly to render commands (no operateAffine)
3. Render commands are processed by the PrivateMotionGLL's internal renderer

## Key Findings

1. **drawAffineMatrix is applied ONCE** in Step 3, transforming PSB native coords to ownerLayer coords
2. **cameraOffset is applied ONCE** in Step 4, shifting all vertices
3. **Vertices go through 3 coordinate spaces**:
   - PSB native → (Step 2 computes vertices)
   - ownerLayer coords → (Step 3 applies drawAffineMatrix)
   - Final screen → (Step 4 adds camera offset, then Step 6 clips/renders)
4. **The child layer (PrivateMotionGLL) is under ownerLayer**, not primaryLayer
5. **No operateAffine** — the SLA path uses pre-computed vertices directly

## Mapping to Our Implementation

| libkrkr2.so | Our Code | Notes |
|-------------|----------|-------|
| Step 2: vertex computation | `flattenLayerNodes` | Computes positions from PSB tree |
| Step 3: drawAffineMatrix | `globalAffine` includes matrix | Applied during flattening |
| Step 4: cameraOffset | `_cameraOffsetX/Y` + `_rootOffsetX/Y` | Should be added to globalAffine tx/ty |
| Step 5: child layer creation | Need to create under ownerLayer | Currently creates under primaryLayer |
| Step 6: rendering | `OperateAffine` call | Equivalent to direct vertex rendering |

## Diagnostic Values (yuzulogo.mtn)

```
drawAffineMatrix = [1, 0, 0, 1, 960, 540]  // identity scale, tx/ty = center of 1920×1080
ownerLayer = 1920×1440 at (0,0), type=ltAlpha
motion native = 720×417
screen = 1280×720
```

## Game Resolution Configuration

From `Config.tjs`:
```
scWidth = 1920, scHeight = 1080     // game window size
exHeight = 1440                      // extended layer height
```

- `scWidth × scHeight` = game window internal resolution
- `exWidth × exHeight` = extended layer size (used for scroll/pan overflow)
- TJS scripts (yuzu_system.tjs) set effect layers to `exWidth × exHeight`
- The AffineLayer (ownerLayer) is `1920×1440` = exWidth × exHeight

## DrawDevice Coordinate Mapping

DrawDevice maps `primaryLayerSize → screen`:
```
screen_x = game_x × screenWidth / primaryLayer.width
screen_y = game_y × screenHeight / primaryLayer.height
```

**If primaryLayer = scWidth × scHeight = 1920×1080** (NOT exHeight):
- Content at (960, 540) → screen (640, 360) = **centered** ✓
- The compositor clips child layers to primaryLayer bounds
- ownerLayer's (1920×1440) content below y=1080 is invisible

## Implementation Plan

Based on the full decompilation analysis:

1. **Create child layer (PrivateMotionGLL equivalent) under ownerLayer** (not primaryLayer)
   - Type: ltAlpha
   - Size: ownerLayer dimensions (1920×1440)
   
2. **Include drawAffineMatrix in globalAffine** during `flattenLayerNodes`
   - The matrix is applied during tree building in libkrkr2.so (sub_6C2334)
   - tx/ty positions content correctly in ownerLayer coordinate space
   
3. **Add cameraOffset + rootOffset** to globalAffine tx/ty
   - Per `Player_applyTranslateOffset_guess` (0x6D5264)
   - cameraOffset: player+144/148, set by `setCameraOffset`
   - rootOffset: player+120/128, accumulated from camera velocity

4. **Render to child layer via OperateAffine**
   - Equivalent to libkrkr2.so's direct vertex rendering
   - The compositor handles clipping to primaryLayer bounds and DrawDevice scaling

## Window Scaling (TVPWindowLayer::RecalcPaintBox, 0xaa7c58)

libkrkr2.so's `TVPWindowLayer` extends `cocos2d::extension::ScrollView`. Scaling logic:

```
paintBox = primaryLayer size (e.g. 1920×1440)
container = screen size (e.g. 1280×720)

if (containerAspect > paintAspect):
    scale = containerW / paintBoxW          // scale by width
    offsetY = (containerH - paintBoxH * scale) / 2   // can be negative = crop
else:
    scale = containerH / paintBoxH          // scale by height
    offsetX = (containerW - paintBoxW * scale) / 2

setZoomScale(scale)
setContentOffset(offsetX, offsetY)
```

For 1920×1440 → 1280×720:
- scale = 1280/1920 = 0.6667
- offsetY = (720 - 1440×0.6667)/2 = (720-960)/2 = **-120**
- Top and bottom each cropped by 120 scaled pixels (= 180 game pixels)

Content at game (960, 540) → screen: x=640, y=540×0.667-120=240 (NOT centered)
Content at game (960, 720) → screen: x=640, y=720×0.667-120=360 (centered!)

**Implication**: The drawAffineMatrix tx/ty should be (960, 720) for vertical centering on a 1920×1440 canvas, not (960, 540). The TJS script computes this based on the actual primaryLayer size. The value (960, 540) seen in our diagnostic suggests the TJS script may be using the wrong height value.

## Summary

The rendering position depends on the FULL chain:
1. PSB tree evaluation → native coords
2. drawAffineMatrix transform → ownerLayer coords (set by TJS, includes centering offset)
3. cameraOffset + rootOffset → final game coords
4. Compositor clips to primaryLayer bounds
5. ScrollView scales uniformly and applies content offset (crop/letterbox)
6. Cocos2D renders to screen

Our implementation covers steps 1-4. Steps 5-6 are handled by the existing Window/DrawDevice code. If steps 5-6 are correctly implemented in our web port, the positioning should be correct once steps 1-4 are aligned to libkrkr2.so.
