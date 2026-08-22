# MotionPlayer dead affine compose conveniences: four-binary record (2026-08-16)

## Scope and conclusion

This pass rechecked the transform helper path against all four current files in
`reference/binaries/`; old `libkrkr2.so` notes were not used as evidence.

The local header contained three zero-call inline conveniences:

```text
affineTranslate(parent, x, y) -> Affine2x3
affineScale(matrix, sx, sy)   -> Affine2x3
affineRotate(matrix, degrees) -> Affine2x3
```

They are not a split representation of the native transform pipeline.  The
four references instead expose one in-place node helper.  It initializes the
node's 2x2 matrix, reads all four entries of the node's `transformOrder`, and
dispatches flip, angle, zoom, and slant directly inside that loop.  Its complete
code-xref set is five call sites on every target: four calls in the layer update
phase and one call in the feedback-anchor phase.

The three local conveniences were therefore removed.  `Affine2x3`, the
four-reference `applyLocalTransform` implementation, and all five production
call sites remain.

## Four-reference function and call map

| Target | local-matrix helper | layer update caller | anchor caller | code xrefs |
|---|---:|---:|---:|---:|
| Android arm64 | `0x696D20` | `0x6B871C` | `0x6BD908` | 5 |
| Android armv7 | `0x572F80` | `0x5856E0` | `0x589C00` | 5 |
| iOS arm64 | `0x1000F6A7C` | `0x10010E544` | `0x100113024` | 5 |
| iOS armv7 | `0xF36BC` | `0x10BE5C` | `0x110908` | 5 |

The four layer-update xrefs correspond to root rebuild plus the three ordinary /
inheritance branches recovered in `Player_updateLayers_guess`.  The fifth xref
rebuilds a type-10 feedback anchor after damping its accumulated transform.

## Native transform boundary

All four helpers have the same structural boundary:

```text
if update is enabled:
    node.matrix2x2 = identity
    for i in 0..3:
        switch node.transformOrder[i]:
            0: conditionally flip matrix rows
            1: conditionally apply node angle
            2: conditionally apply node zoom X/Y
            3: conditionally apply node slant X/Y
```

The helper does not accept or return a standalone affine value, does not update
translation, and does not expose separate translate/scale/rotate primitives.
The result lives in the node's accumulated matrix fields.

The angle branch also preserves this shared expression:

```text
(angle * 3.14159265 + angle * 3.14159265) / 360.0
```

The deleted `affineRotate` instead used a higher-precision pi and a fixed
right-composition helper.  Even if it acquired a caller later, that behavior
would not be the recovered native boundary or its floating-point expression.

## Local source effect

Removed only from `PlayerUpdateLayersInternal.h`:

- `affineTranslate`
- `affineScale`
- `affineRotate`

Kept unchanged:

- the six-double `Affine2x3` representation used by the port;
- `applyLocalTransform(Affine2x3 &, ..., transformOrder)`;
- the node overload of `applyLocalTransform`;
- four layer-evaluation calls and one feedback-anchor call;
- the unit and differential cases that lock the four-reference transform-order
  and truncated-pi behavior.

This is a source-structure correction, not a runtime behavior change: all three
removed inline functions had zero production and test callers before deletion.

## Validation

- Ordinary and `KRKR2_WASMTIME_HEADLESS` Emscripten syntax checks passed for
  the complete motionplayer unit-test translation unit.  Both reported only
  the repository's existing deprecated `_tss` literal-operator warning.
- `Web Debug Build --target motionplayer` rebuilt and linked all 6 affected
  steps successfully.
- `Wasmtime Headless Debug Build --target motionplayer` rebuilt and linked all
  6 affected steps successfully.
- The complete `Web Debug Build` linked the final `index.html`/Wasm output.
- An exact identifier search found zero remaining `affineTranslate`,
  `affineScale`, or `affineRotate` definitions/calls.  The active transform
  helper still has five production calls, its node overload and implementation,
  plus the focused unit call.
- `git diff --check` passed for the tracked source edit, and the new analysis
  record has no trailing whitespace.
- All four recovery IDBs were force-recompiled, read back with the five-caller
  closure comment, and saved in place.
