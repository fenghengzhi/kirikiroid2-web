# Logo Rendering Chain Analysis (yuzulogo.mtn / m2logo.mtn)

## 1. Game Assets

The game "Senren*Banka" (千恋万花) stores logo animations in `data1080.xp3`:
- `yuzulogo.mtn` - Yuzusoft company logo
- `m2logo.mtn` - M2 (publisher) logo
- `splash.mtn` - Splash screen

These are PSB-format motion files (`.mtn` = Motion). They are played between age
verification and the title screen.

## 2. TJS Script Call Chain

### 2.1 Startup Sequence

```
startup.tjs
  -> Scripts.execStorage("system/Initialize.tjs")
     -> (checks motionEnabled config, loads motion.tjs if CanLoadPlugin)
     -> motion.tjs loads emoteplayer.dll + motionplayer.dll + AffineSourceMotion.tjs
     -> Initialize.tjs loads GFX_Motion.tjs (for extension "mtn")
     -> Scripts.execStorage("sysscn/AfterInit.tjs")
     -> KAG scenario engine starts
        -> first.ks (encrypted scenario script)
           -> KAG @gfx tag with .mtn file triggers MotionController
```

### 2.2 GFX System Registration (GFX_Motion.tjs)

`GFX_Motion.tjs` registers a GFX entry with `GenericFlip.Entry()`:
```javascript
// Pseudo-TJS from bytecode disassembly
var entry = new Dictionary();
entry.type = "gfxm_file";
entry.class = MotionController;    // class that handles this GFX type
entry.trigger = "gfxm_stop";      // trigger name when animation stops
entry.ext = "mtn";                 // file extension to match
entry.options = ["gfxm_chara", "gfxm_motion", "gfxm_stealthchara",
                 "gfxm_stealthmotion", "gfxm_flags", "gfxm_tickcount",
                 "gfxm_speed", "gfxm_zoom", "gfxm_width", "gfxm_height"];
entry.autoflag = "motion";
GenericFlip.Entry(entry);
```

When a KAG script references a `.mtn` file via `@gfx`, the `GenericFlip` system
creates a `MotionController` instance and drives it.

### 2.3 MotionController Class

```javascript
class MotionController extends GenericFlip, Motion.Player {
    // Inherits GenericFlip (flip/display management) AND Motion.Player (C++ native)
    var FLIP;           // GenericFlip instance
    var PLAYER;         // Motion.Player (redundant - also inherits directly)
    var targetLayer;    // Layer to render onto (created in constructor)
    var storage;        // Current .mtn file path
    var motionwidth, motionheight, motionzoom;
    var lastTick;
    var _resourceManager;
}
```

Constructor creates a Layer as `targetLayer` and initializes:
```javascript
function MotionController(window) {
    this.FLIP = GenericFlip;
    this._resourceManager = window.motion_manager;
    this.PLAYER = Motion.Player(this._resourceManager.resourceManager);
    this.targetLayer = new Layer(window, window.poolLayer);
    this.initTarget();
}
```

### 2.4 Logo Playback Flow

```
1. KAG scenario triggers @gfx with logo.mtn
2. GenericFlip creates MotionController
3. MotionController.start(storage) called:
   a. resourceManager.load(storage)     -> loads .mtn PSB data
   b. updateParam(options)              -> calls play(motion, flags), sets chara/speed
   c. lastTick = System.getTickCount()

4. Each frame, GenericFlip calls flipFrame() -> flipUpdate(tick):
   a. delta = tick - lastTick
   b. if (this.playing):
       - targetLayer.fillRect(0, 0, w, h, targetLayer.neutralColor)  // clear
       - this.progress(delta)           // advance animation (C++ Player::progressCompatMethod)
       - this.draw(targetLayer)         // render to layer (C++ Player::drawCompat)
       - this.flipAssign(targetLayer)   // copy to display layers
   c. if (!playing): flipStop()
   d. lastTick = tick

5. flipAssign() iterates flipLayers and calls assignImagesForMovie() on each
```

### 2.5 Key TJS Method -> C++ Mapping

| TJS Method | C++ Function | libkrkr2.so Address |
|---|---|---|
| `Motion.Player.draw(arg)` | `Player::drawCompat` | `sub_6D5FB8` |
| `Motion.Player.progress(dt)` | `Player::progressCompatMethod` | (NCB wrapper) |
| `Motion.Player.play(flags)` | `Player::playCompat` | (NCB wrapper) |
| `Motion.Player.captureCanvas(arg)` | `Player::captureCanvasCompat` | (NCB wrapper) |
| `Motion.D3DAdaptor.captureCanvas(arg)` | `sub_6AD92C` | `0x6AD92C` |

## 3. C++ Function Call Chain in libkrkr2.so

### 3.1 drawCompat (sub_6D5FB8) - The Central Dispatch

This is the C++ implementation of `Motion.Player.draw(param)`. It handles three
different rendering modes based on the type of `param`:

```
sub_6D5FB8(Player* this, TJSVariant** params):
  
  // Step 1: Check if param is D3DAdaptor (NativeInstanceSupport check)
  if (param is D3DAdaptor):
      this->_d3dDrawMode = true   // byte at offset 909
      sub_6D5B90(this)            // no-op draw, just sets mode flag
      return
  
  // Step 2: Check if param is SeparateLayerAdaptor
  if (param is SLA):
      Player_DrawSLA_guess(this)  // sub_6D5658
      return
  
  // Step 3: param is a Layer (CPU rendering path)
  if (sub_6D5164(this, layerNodes, meshData)):  // prepare/sort layer nodes
      if (this->_d3dDrawMode):
          // D3D PATH: Create singleton D3DAdaptor, render to it, then copy to Layer
          D3DAdaptor* adaptor = singleton(sub_6ADB10)  // create w/h from window
          // Swap front/back buffers in the D3DAdaptor
          sub_6ADE24(adaptor, this, layerNodes)  // render via GPU texture pipeline
          sub_6AD92C(adaptor, Layer)              // captureCanvas - copy pixels to Layer
          Layer.setSize(w, h)
          Layer.visible = true
      else:
          // CPU PATH: Render directly onto Layer via TJS Layer API calls
          sub_6D5264(this, layerNodes)  // apply coordinate offsets to all nodes
          sub_6C7440(this, param, layerNodes, meshData)  // main rendering loop
          sub_6CE7D8(this, param)       // assignImages on Layer
```

### 3.2 CPU Rendering Loop (sub_6C7440)

This is the core software renderer. It iterates over all motion layer nodes and
calls TJS Layer methods to composite them onto the target Layer:

```
sub_6C7440(Player* this, Layer, layerNodes, meshData):
  for each layerNode in layerNodes:
      if (node.type == Camera) continue
      if (node.hidden || node.invisible) continue
      if (!node.hasImage) continue
      
      // Get source image via requireLayerId()
      // Set clipping rect from node bounds
      // Based on node.shapeType:
      switch (shapeType):
          case 0 (Point):
              // affineCopy or operateAffine
              Layer.affineCopy(srcLayer, ...)
          case 1 (Circle):
              // meshCopy / bezierPatchCopy
              Layer.meshCopy(srcLayer, ...)
          case 2 (Rect):
              // fillRect or operateRect
              Layer.fillRect(...)
              Layer.operateRect(...)
          case 3 (Quad):
              // affineCopy or operateAffine  
              Layer.affineCopy(srcLayer, ...)
      
      // Also handles outline drawing:
      Layer.drawMeshFrame(...)
      Layer.drawBezierPatchMeshFrame(...)
      Layer.drawBezierPatchFrame(...)
      Layer.drawLine(...)
```

### 3.3 Post-render (sub_6CE7D8)

After `sub_6C7440` completes rendering, `sub_6CE7D8` calls `Layer.assignImages()`
on the target Layer to update the display:
```
sub_6CE7D8(Player* this, Layer):
    if (this->canvasCaptureEnabled):
        sub_6CE19C(this, Layer)        // capture callback
        Layer.assignImages(Layer)       // update display
```

### 3.4 SLA Rendering Path (Player_DrawSLA_guess / sub_6D5658)

When `draw(SLA)` is called (e.g., from `AffineSourceMotion.drawAffine`):
```
Player_DrawSLA_guess(Player* this, SLA):
    sub_6D5164(this, layerNodes, meshData)   // prepare nodes
    sub_6D5264(this, layerNodes)             // apply offsets
    
    if (ogl_accurate_render):
        // OpenGL path: call sub_6C9CA8 (renders via GPU)
        sub_6C9CA8(this, SLA, layerNodes, meshData)
    else:
        // CPU path: render directly onto SLA's owner Layer
        targetLayer = Player_ResolveSLATarget_guess(SLA)
        w = targetLayer.clipWidth
        h = targetLayer.clipHeight
        Player_RenderMotionFrame_guess(renderBuf, w, h,
                                       &this, layerNodes, meshData, this)
        Layer_UpdateRect_guess(targetLayer, 0)
```

### 3.5 D3DAdaptor captureCanvas (sub_6AD92C)

This copies the rendered pixel buffer from the D3DAdaptor's internal texture
to the target Layer:
```
sub_6AD92C(D3DAdaptor* adaptor, Layer):
    if (isGPUMode):
        // Direct pixel copy from adaptor's buffer to Layer
        srcPixels = adaptor->getPixelBuffer()
        dstPixels = Layer.getMainImagePixelBuffer()
        memcpy(dstPixels, srcPixels, width * height * 4)
    else:
        // Create GPU texture from pixel buffer
        Motion_createTextureFromPixels()
```

## 4. The AffineSourceMotion Path (Stand Images)

For character stand images using `.mtn` motions, the path goes through
`AffineSourceMotion.tjs` instead of `GFX_Motion.tjs`:

```
AffineSourceMotion.drawAffine(owner, matrix):
    if (_useD3D):
        adaptor = window.motionD3DAdaptor    // D3DAdaptor instance
        adaptor.clearEnabled = true
    elif (owner == _motionSeparateAdaptor):
        adaptor = _motionSeparateAdaptor     // SLA path
    else:
        workLayer = window.motionWorkLayer   // fallback Layer path
        workLayer.setClip(0, 0, w, h)
        workLayer.fillRect(0, 0, w, h, neutralColor)
        workLayer.type = ltAlpha
        adaptor = workLayer

    _player.clear()  (if clearEnabled is defined)
    _drawAffine(adaptor, matrix):
        // Sets up coordinates, scale, rotation
        _player.setDrawAffineTranslateMatrix(matrix)
        _player.progress(interval)
        _player.completionType = ...
        _player.draw(adaptor)   // <-- THIS calls drawCompat
    
    if (adaptor is D3DAdaptor):
        workLayer = window.motionWorkLayer
        adaptor.captureCanvas(workLayer)
        adaptor.unloadUnusedTextures()
        _redrawImage()
        owner.assignImages(workLayer)
    elif (adaptor is Layer):
        _redrawImage()
        owner.assignImages(adaptor)
```

## 5. Why Logo Rendering Fails in Web Port

### 5.1 The Problem

When `GFX_Motion.flipUpdate` calls `this.draw(targetLayer)`, the C++ `drawCompat`
function (`Player::drawCompat` in `Player.cpp`) is invoked. The function correctly
identifies the parameter as a Layer (Step 3) and calls `renderToLayer()`.

However, `renderToLayer()` currently has the following issues:

### 5.2 Root Cause: renderToLayer Implementation Gaps

The current web port's `renderToLayer()` (Player.cpp line 1695) uses an OpenCV-based
renderer (`renderMotionLayer`) that:

1. **Relies on embedded PSB resources**: The code checks
   `_runtime->activeMotion->resourcesByPath.empty()` and skips rendering if no
   resources are embedded. Logo `.mtn` files reference external image files
   (not embedded in the PSB). The `#ifndef KRKR2_NO_OPENCV` path gates on
   `resourcesByPath.empty()`.

2. **Uses custom OpenCV rendering**: Instead of calling TJS Layer API methods
   like libkrkr2.so does (`affineCopy`, `operateRect`, `meshCopy`, etc.),
   the web port implements its own OpenCV-based compositing. This is a
   fundamentally different approach from the original engine.

3. **Missing the "no-OpenCV" Layer API path**: There IS a `#else` (no-OpenCV)
   path starting at line 1862, but it still tries to do manual pixel buffer
   operations rather than calling TJS Layer methods.

### 5.3 What libkrkr2.so Actually Does

In the original engine, the **CPU rendering path** (when `_d3dDrawMode` is false)
goes through `sub_6C7440`, which:

1. Iterates over all motion layer nodes
2. For each visible node with an image source:
   - Calls `requireLayerId(sourceName)` on the Player to get a source Layer ID
   - Calls `setClip` on the target Layer
   - Calls `affineCopy`, `meshCopy`, `bezierPatchCopy`, `operateRect`,
     `fillRect`, or `operateAffine` on the target Layer
3. After all nodes are rendered, calls `assignImages` on the target Layer

This is entirely done through TJS Layer API calls -- **NOT** by directly
manipulating pixel buffers. The original engine leverages the Layer system's
built-in image compositing operations.

### 5.4 The D3D Path in libkrkr2.so

When `_d3dDrawMode` is true (set when `draw(D3DAdaptor)` was previously called),
the drawCompat function for a Layer argument:

1. Creates a singleton D3DAdaptor at window half-resolution
2. Calls `sub_6ADE24` to render motion nodes into the D3DAdaptor's GPU texture
3. Calls `sub_6AD92C` (captureCanvas) to copy the texture pixels to the Layer
4. Sets `Layer.visible = true` and `Layer.setSize(w, h)`

The web port's D3DAdaptor is a stub that doesn't have GPU textures, so this path
also cannot work.

### 5.5 Why `renderToLayer` Returns but Nothing Shows

The issue is in the current `renderToLayer` implementation (Player.cpp):

1. `drawCompat` is called with `targetLayer` (from `MotionController.flipUpdate`)
2. `drawCompat` Step 3 matches: param is a Layer, `_d3dDrawMode` is false
3. `renderToLayer()` is called
4. Inside `renderToLayer()`:
   - `_runtime->activeMotion` exists (the .mtn file was loaded)
   - The OpenCV path (line 1752) checks `!resourcesByPath.empty()` --
     logo .mtn files may reference EXTERNAL images (not embedded in PSB),
     so `resourcesByPath` could be empty
   - Even when resources exist, the OpenCV-based `renderMotionLayer` function
     (a custom compositing implementation) may fail to resolve source images
     because it uses a different path resolution strategy than libkrkr2.so
   - The `#else` (no-OpenCV) fallback path at line 1862 attempts raw pixel
     buffer manipulation but also does not match libkrkr2.so's approach
5. The target Layer remains black (filled with `neutralColor` from
   `MotionController.flipUpdate` before `draw()` was called)

**The fundamental mismatch**: libkrkr2.so's `sub_6C7440` renders by calling
TJS Layer API methods (`affineCopy`, `operateRect`, etc.) which let the engine's
built-in compositing handle image loading, blending, and transformation. The
web port's `renderToLayer` tries to do all compositing manually via OpenCV or
raw pixel buffers, which requires re-implementing the entire Layer compositing
pipeline -- a much harder and error-prone approach.

## 6. What Needs to Be Implemented

### 6.1 Option A: Implement Layer API Rendering (Recommended)

Replicate `sub_6C7440`'s approach: iterate over motion layer nodes and call
TJS Layer API methods (`affineCopy`, `operateRect`, etc.) on the target Layer.

This requires:
1. **`requireLayerId(name)`**: Must resolve source image names to actual
   Layer objects. In libkrkr2.so, this loads the source image into a Layer
   and returns a layer ID. The web port's current `requireLayerId` returns
   dummy values.

2. **Node iteration**: Walk the active motion's layer node tree, computing
   transformed coordinates from the node's affine matrix.

3. **Layer method calls**: For each node, call the appropriate Layer method
   (`affineCopy` for affine transforms, `operateRect` for simple rects,
   `meshCopy` for mesh deformations).

4. **`assignImages`**: After rendering all nodes, call `assignImages` on the
   target Layer to update the display tree.

### 6.2 Option B: Fix D3DAdaptor Path

Implement a working D3DAdaptor that renders to an offscreen buffer:
1. Implement `sub_6ADE24`: render motion nodes to a pixel buffer using
   the same node iteration as `sub_6C7440` but writing to a raw buffer
2. Implement `sub_6AD92C`: copy the buffer to a TJS Layer

### 6.3 Critical Missing Pieces

Regardless of which option is chosen:

1. **Source image resolution**: `requireLayerId` must load the source images
   referenced by motion nodes. These can be:
   - External files referenced by path in the PSB motion data
   - Embedded textures within the PSB data itself
   - The current `requireLayerId` returns placeholder values and doesn't
     load actual images.

2. **Node coordinate computation**: `sub_6D5264` applies parent coordinate
   offsets to all child nodes and handles camera projection. The current
   web port's `calcViewParam` only partially implements this.

3. **Motion layer node tree traversal**: `Player_RenderMotionFrame_guess`
   shows the full iteration pattern including stencil counting, visibility
   checks, clipping bounds computation, and `requireLayerId` calls.

## 7. Key libkrkr2.so Addresses

| Function | Address | Description |
|---|---|---|
| `sub_6D5FB8` | `0x6D5FB8` | drawCompat - central draw dispatch |
| `sub_6C7440` | `0x6C7440` | CPU rendering loop (Layer API calls) |
| `sub_6D5264` | `0x6D5264` | Apply coordinate offsets to nodes |
| `sub_6D5164` | `0x6D5164` | Prepare/sort layer nodes |
| `sub_6CE7D8` | `0x6CE7D8` | Post-render: assignImages on Layer |
| `sub_6ADB10` | `0x6ADB10` | D3DAdaptor constructor |
| `sub_6AD92C` | `0x6AD92C` | D3DAdaptor captureCanvas |
| `sub_6ADE24` | `0x6ADE24` | D3DAdaptor render (GPU texture pipeline) |
| `Player_DrawSLA_guess` | `0x6D5658` | SLA rendering path |
| `Player_RenderMotionFrame_guess` | `0x6DE750` | Per-frame render (stencil + node iteration) |
| `sub_6ACE94` | `0x6ACE94` | D3DAdaptor TJS member registration |
| `sub_6D69C8` | `0x6D69C8` | Player NCB class registration |

## 8. Progress/Timing Details

In `GFX_Motion.flipUpdate`, `progress(delta)` is called where `delta` is
`tick - lastTick` in **milliseconds** (from `System.getTickCount()`).

The C++ `progressCompatMethod` (Player.cpp line 3276) converts this to frames:
```cpp
delta_frames = delta_ms * kMotionFramesPerMillisecond * speed
             = delta_ms * (60.0 / 1000.0) * speed
```

So the animation runs at 60 frames per second by default.

In `AffineSourceMotion._drawAffine`, the `_interval` property accumulates the
frame delta and is passed to `progress`. The `moveProgress` function handles
interpolated variable changes.

For logos, the `GFX_Motion` path is simpler: raw tick deltas go directly to
`progress(ms)`, which is correct.

## 9. Summary of Rendering Paths

```
                    draw(param)
                        |
          +-------------+-------------+
          |             |             |
    D3DAdaptor         SLA          Layer
          |             |             |
   set d3dDrawMode  DrawSLA    +-----+-----+
   return          /        \  | d3dDrawMode|
              GPU path  CPU path    |        |
              sub_6C9CA8  render  render via  render via
                         to layer D3DAdaptor  sub_6C7440
                                   |           |
                              captureCanvas  affineCopy
                              to Layer       operateRect
                                             meshCopy
                                             etc.
                                               |
                                          assignImages
```

For logo animations in this game:
- `_useD3D` is set to false (from patch/AffineSourceMotion.tjs)
- `_d3dDrawMode` starts as false
- Logos go through the **CPU Layer path** via `sub_6C7440`
- The web port's `renderToLayer()` must implement the `sub_6C7440` approach
  using TJS Layer API calls to make logos visible
