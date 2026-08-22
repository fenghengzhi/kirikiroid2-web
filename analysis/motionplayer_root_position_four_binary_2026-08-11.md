# Motion.Player root position: four-binary reconstruction (2026-08-11)

## Scope

This note reconstructs the `Motion.Player` root-position member cluster:

- the `setCoord(x, y)` method;
- the `x` and `y` read/write properties;
- the `left` alias of `x`;
- the `top` alias of `y`.

It replaces compiled-source comments that still cited accessor addresses and
offsets from the old single-`libkrkr2.so` investigation.

## Four-target registrar and accessor mapping

| Target | `setCoord` registration / body | `x` registration / get / set | `y` registration / get / set | `left` / `top` aliases |
|---|---:|---:|---:|---|
| Android ARM64 | `0x6D5250` / `0x6CA3D8` | `0x6D52F0` / `0x6D6C88` / `0x6CA408` | `0x6D5368` / `0x6D6C94` / `0x6CA428` | `0x6D53D0` reuses x; `0x6D5438` reuses y |
| Android ARMv7 | `0x5983EA` / `0x592620` | `0x598400` / `0x59900A` / `0x592662` | `0x598420` / `0x59901C` / `0x592688` | `0x598440` reuses x; `0x598456` reuses y |
| iOS ARM64 | `0x100124C8C` / `0x10011D060` | `0x100124CAC` / `0x1001256E8` / `0x10011D0BC` | `0x100124CE0` / `0x100125710` / `0x10011D104` | `0x100124D14` reuses x; `0x100124D38` reuses y |
| iOS ARMv7 | `0x123F36` / `0x11BA0C` | `0x123F54` / `0x12490C` / `0x11BA76` | `0x123F80` / `0x12493C` / `0x11BABC` | `0x123FAE` reuses x; `0x123FD0` reuses y |

The alias result is structural, not merely equivalent behavior: each registrar
places the same x getter/setter function pair in both `x` and `left`, and the
same y pair in both `y` and `top`.

## Root-node container and layout mapping

| Target | Root lookup | dirty | x | y |
|---|---|---:|---:|---:|
| Android ARM64 | direct pointer at `Player+200` | node `+1584` | node `+1592` | node `+1600` |
| Android ARMv7 | direct pointer at `Player+160` | node `+1344` | node `+1352` | node `+1360` |
| iOS ARM64 | libc++ deque map/start at `Player+168/+192`, stride `2648` | node `+1600` | node `+1608` | node `+1616` |
| iOS ARMv7 | libc++ deque map/start at `Player+140/+152`, stride `2228` | node `+1312` | node `+1320` | node `+1328` |

Android retains a direct cached root pointer while both iOS builds perform the
target libc++ deque index-zero calculation.  None of the 20 bodies contains an
empty-container guard: Player construction has already created root node zero,
and node-tree rebuild removes only the non-root suffix.

## Common source-level pseudocode

```cpp
double Player::getX() const {
    return nodes[0].delta.posX;
}

double Player::getY() const {
    return nodes[0].delta.posY;
}

void Player::setX(double value) {
    Root &root = nodes[0];
    if (root.delta.posX != value) {
        root.delta.posX = value;
        root.delta.dirty = true;
    }
}

void Player::setY(double value) {
    Root &root = nodes[0];
    if (root.delta.posY != value) {
        root.delta.posY = value;
        root.delta.dirty = true;
    }
}

void Player::setCoord(double x, double y) {
    Root &root = nodes[0];
    if (root.delta.posX != x || root.delta.posY != y) {
        root.delta.posX = x;
        root.delta.posY = y;
        root.delta.dirty = true;
    }
}
```

`left` calls/binds the x pair and `top` calls/binds the y pair.  The NCB method
adapter owns argument-count checks and TJS-to-double conversion for `setCoord`;
the native member body begins only after receiving two doubles.  The local
`NCB_METHOD(setCoord)` uses that same adapter contract.

## Floating-point boundary behavior

The native comparisons are ordinary IEEE-754 `!=` comparisons, not bitwise
comparisons and not an epsilon test.

- `+0.0` and `-0.0` compare equal.  A one-axis setter will therefore not replace
  the stored sign bit when these are the only difference.
- Any comparison involving NaN is unequal.  Passing NaN writes it and marks the
  root dirty even when the root already contains a NaN.
- `setCoord` is a combined conditional followed by two unconditional stores.
  If y changes while stored x is `+0.0` and input x is `-0.0`, the true y branch
  also replaces x's sign bit.  Likewise, the nominally unchanged axis receives
  the passed NaN payload whenever the other comparison makes the condition
  true.  A per-axis conditional implementation is observably different.
- If both comparisons are false, neither axis is written and dirty is untouched.
  If either comparison is true, both writes occur before dirty is set.

There is no exception recovery, bounds check, pending-value store, or fallback
return value in the native bodies.

## Android ARMv7 IDB boundary repair

The ARMv7 registrar stored `0x592688 | 1` as the y setter, but the original IDB
had typed the first word as `off_592688` and had no function.  Decompilation
therefore failed even though the surrounding x setter ends at `0x592688` and
the next confirmed function begins at `0x5926AE`.

The range `0x592688..0x5926AE` was undefined, its `T` segment register was set
to `1`, and it was recreated as Thumb function `Player_setRootY_guess` using a
standalone IDAPython/idapro session.  Fresh decompilation then produced the same
read/compare/write/dirty sequence as the other three targets.  The repaired IDB
was saved before returning it to the MCP worker.

## Pre-edit local line-by-line comparison

The local port already used root `delta.posX/posY`, ordinary `!=` checks, and
dirty-on-change behavior.  `left/top` also correctly forwarded to `x/y`.
However, it differed from all four references in four connected ways:

1. `getX/getY` tested `_nodes.empty()` and returned process-local pending values
   or zero.  Native code addresses root zero directly.
2. `setX/setY/setCoord` always wrote `_pendingRootX/_pendingRootY` and set
   `_hasPendingRootPos`, then conditionally touched a root only if it existed.
   No corresponding fields or branches occur in any reference.
3. `setCoord` tested and wrote each axis separately.  Native code uses one OR
   condition and, when true, writes both axes unconditionally.
4. `buildNodeTree()` reapplied the pending values after rebuilding.  Both the
   native reset path and the local `resetNodeTreeKeepRootLike` retain node zero;
   only the non-root suffix is erased, so this reapplication is unnecessary as
   well as non-native.

The intended correction is to access `_nodes[0]` directly, implement the exact
combined `setCoord` branch, remove the three pending fields, remove their
load-time reapplication, and replace stale single-target comments with the
four-reference conclusions above.

## Applied reconstruction and verification

The local implementation now follows the common four-binary body:

- `Player::getX/getY` read `_nodes[0]` directly.
- `Player::setX/setY` compare and update only their own axis, then set the root
  dirty flag.
- `Player::setCoord` uses one `x != newX || y != newY` condition and, when it is
  true, writes both axes before setting dirty.
- `_pendingRootX`, `_pendingRootY`, and `_hasPendingRootPos` were removed, as was
  the corresponding post-load reapplication path.
- facade and node comments were rewritten to record the four-reference result
  without embedding stale `libkrkr2.so` addresses.

Each IDB now names and types the five reconstructed functions as
`Player_setRootCoord_guess`, `Player_getRootX_guess`,
`Player_setRootX_guess`, `Player_getRootY_guess`, and
`Player_setRootY_guess`.  All 20 function caches were invalidated, freshly
decompiled, and compared again after the ARMv7 boundary repair.  The four IDBs
were then saved.

`cmake --build --preset "Web Debug Build"` completed successfully in 32 build
steps and linked `index.html`.  The remaining diagnostics are the repository's
existing `_tss`, ignored-`nodiscard`, pthread-memory-growth, JSPI experimental,
and JavaScript-library symbol warnings; this phase introduced no build error.
