---
name: drawAffineMatrix coordinate transform
description: How drawAffineMatrix (internal+808..844) is set by TJS and applied in renderTree/applyTranslateOffset; tx/ty are float not double; only set by TJS script not internal C++
type: project
---

drawAffineMatrix at player->internal+808..844: m11/m12/m21/m22 are double, tx/ty (+840/+844) are **float**.
Flag at internal+611 = isNonIdentity (byte).

Only set by:
- Player_ctor (0x6CED30): identity
- Player_setDrawAffineTranslateMatrix (0x6D4F14): TJS method called from _drawAffine()
- play(): reset to identity

TJS call chain: AffineLayer.onPaint → calcMatrix(mtx) → _drawAffine → setDrawAffineTranslateMatrix(dx, ey, ex, dy, **ox, oy**) where ox/oy = centering offset from AffineMatrix.

Applied in sub_6C2334 renderTree: transforms all vertices/paintBox/mesh points.
cameraOffset (player+144/148) set by sub_6BDA28 camera node processing uses drawAffineMatrix rotation but NOT tx/ty.
applyTranslateOffset (0x6D5264) adds cameraOffset to all rendered coordinates.

**Why:** When drawAffineMatrix is identity, PSB coords (centered at origin with negatives) map directly to Layer pixels → content appears at top-left with negatives clipped.
**How to apply:** If rendering is off-center, check that TJS _drawAffine() is calling setDrawAffineTranslateMatrix with correct ox/oy values.
