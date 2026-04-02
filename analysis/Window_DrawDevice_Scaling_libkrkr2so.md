# Window/DrawDevice Scaling — libkrkr2.so Analysis

## TVPWindowLayer Architecture

`TVPWindowLayer` extends `cocos2d::extension::ScrollView`.

Key struct offsets:
- `+0x368` (872): ScrollView contentSize = primaryLayer logical size
- `+0x458` (1112): paintBoxW (set by `SetPaintBoxSize`)
- `+0x45C` (1116): paintBoxH
- `+0x370` (880): cached zoom scale
- `+0x374` (884): cached zoom scale × 2

## RecalcPaintBox (0xaa7c58) — Decompiled

```asm
; Check paintBox valid
LDR W8, [X19, #0x458]   ; paintBoxW  
CBZ W8, skip
LDR W8, [X19, #0x45C]   ; paintBoxH
CBZ W8, skip

; S1 = contentW (this+0x368), S2 = contentH (this+0x36C)
; S3 = containerW, S4 = containerH (from vtable[43] = getContentSize of parent)

contentAspect = contentW / contentH
containerAspect = containerW / containerH

if (containerAspect > contentAspect):
    ; Container is wider than content → scale by width
    scale = contentW / containerW
    offsetY = (contentH - containerH * scale) * 0.5
    offsetX = 0
else:
    ; Container is taller → scale by height
    scale = contentH / containerH
    offsetX = (contentW - containerW * scale) * 0.5
    offsetY = 0

setZoomScale(scale)
setContentOffset(offsetX, offsetY)
```

## Concrete Values

For this game: contentSize=1920×1440, container(screen)=1280×720

```
contentAspect = 1920/1440 = 1.333 (4:3)
containerAspect = 1280/720 = 1.778 (16:9)
containerAspect > contentAspect → scale by width

scale = 1920 / 1280 = 1.5
offsetY = (1440 - 720 * 1.5) * 0.5 = (1440 - 1080) * 0.5 = 180
offsetX = 0
```

## ScrollView Zoom Behavior

In Cocos2D ScrollView with zoomScale=1.5:
- Content (1920×1440) is rendered at 1.5× magnification = 2880×2160 pixels
- Viewport shows 1280×720 pixels of that magnified content
- contentOffset=(0, 180) means the viewport starts at y=180 in content coordinates

Visible content range (in content coordinates):
- x: [0, 1280/1.5] = [0, 853]
- y: [180/1.5, (180+720)/1.5] = [120, 600]

Wait, contentOffset in ScrollView is in **view coordinates**, not content coordinates.
The actual visible range in content coordinates:
- x: [0, 1280/1.5] = [0, 853.3]
- y: [-offsetY/scale, (-offsetY+720)/scale] = [-120, 360]

This doesn't seem right either. The ScrollView offset/scale interaction depends on Cocos2D's implementation details.

## Key Finding: PaintBox Clipping

From `ResetDrawSprite` (0xaa7d70):

```asm
; drawSprite->setTextureRect(0, 0, paintBoxW, paintBoxH)
LDR S0, [X19, #0x458]    ; paintBoxW = scWidth = 1920
LDR S1, [X19, #0x45C]    ; paintBoxH = scHeight = 1080
```

**The drawSprite only shows (0,0)-(paintBoxW, paintBoxH) of the primaryLayer texture.** Even though primaryLayer is 1920×1440, only the top 1920×1080 is rendered to screen (paintBox = scWidth×scHeight).

## Complete Mapping Chain

```
ownerLayer(1920×1440)
  → compositor → primaryLayer(1920×1440) 
    → paintBox clips to (0,0,1920,1080)  ← only scWidth×scHeight shown
      → ScrollView zoom/offset → screen(1280×720)
```

For content at game coordinate (960, 540):
- In paintBox(1920×1080): x=960/1920=50%, y=540/1080=50% = **center** ✓
- Scaled to screen: (640, 360) = **screen center** ✓

## Root Cause of Position Issue in Web Port

The motionplayer output at (960, 540) IS correct for the paintBox(1920×1080) coordinate space. The issue is likely that our web port's DrawDevice does NOT implement the paintBox clipping — it renders the full primaryLayer(1920×1440) to screen, causing the y-axis mapping to be wrong:

Without paintBox clipping: y = 540 × (720/1440) = 270 (NOT centered)
With paintBox clipping: y = 540 × (720/1080) = 360 (centered ✓)

## IDA Addresses

| Address | Function | Description |
|---------|----------|-------------|
| 0xaa7c58 | RecalcPaintBox | Uniform scaling + ScrollView offset |
| 0xaa7d70 | ResetDrawSprite | Sprite positioning + content setup |
| 0xaa5a24 | SetPaintBoxSize | Sets paintBox dimensions at +0x458/+0x45C |
| 0xaa6074 | SetSize | Sets contentSize at +0x368, triggers RecalcPaintBox |
| 0xaa0ad8 | TVPMainScene::addLayer | Sets viewSize and contentSize on add |
