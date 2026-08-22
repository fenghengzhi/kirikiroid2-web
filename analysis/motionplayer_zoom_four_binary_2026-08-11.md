# Motion.Player zoom accessors and combined setter: four-binary reconstruction

Date: 2026-08-11

## Registration mapping

| Target | `setZoom` registration/body | `zoomX` registration/get/set | `zoomY` registration/get/set |
|---|---|---|---|
| Android ARM64 | `0x6D58A0` / `0x6BE334` | `0x6D5940` / `0x6D6CE8` / `0x6CE6FC` | `0x6D59B8` / `0x6D6CF4` / `0x6CE71C` |
| Android ARMv7 | `0x598574` / `0x58A4FE` | `0x598588` / `0x59907A` / `0x594838` | `0x5985A6` / `0x59908C` / `0x59485E` |
| iOS ARM64 | `0x100124EE4` / `0x10011396C` | `0x100124F04` / `0x100125828` / `0x10011FF94` | `0x100124F30` / `0x100125850` / `0x10011FFDC` |
| iOS ARMv7 | `0x124152` / `0x111372` | `0x124170` / `0x124A6C` / `0x11ECA8` | `0x12419A` / `0x124A9C` / `0x11ECEE` |

UTF-16LE searches independently confirmed `setZoom`, `zoomX`, and `zoomY` in
all four files, including the IDA operands displayed only as `"z"`.

## Layout and common pseudocode

| Target | root representation | dirty | zoom X | zoom Y |
|---|---|---:|---:|---:|
| Android ARM64 | libstdc++ deque `start.cur` `*(Player+200)` | `+1584` | `+1624` | `+1632` |
| Android ARMv7 | libstdc++ deque `start.cur` `*(Player+160)` | `+1344` | `+1384` | `+1392` |
| iOS ARM64 | deque map/start `+168/+192`, stride `2648` | `+1600` | `+1640` | `+1648` |
| iOS ARMv7 | deque map/start `+140/+152`, stride `2228` | `+1312` | `+1352` | `+1360` |

```cpp
double Player::getZoomX() const {
    return root().delta.scaleX;
}

void Player::setZoomX(double x) {
    Root &r = root();
    if (r.delta.scaleX != x) {
        r.delta.dirty = true;
        r.delta.scaleX = x;
    }
}

double Player::getZoomY() const {
    return root().delta.scaleY;
}

void Player::setZoomY(double y) {
    Root &r = root();
    if (r.delta.scaleY != y) {
        r.delta.dirty = true;
        r.delta.scaleY = y;
    }
}

void Player::setZoom(double x, double y) {
    Root &r = root();
    if (r.delta.scaleX != x || r.delta.scaleY != y) {
        r.delta.dirty = true;
        r.delta.scaleX = x;
        r.delta.scaleY = y;
    }
}
```

As with slant, all one-axis setters emit dirty-before-value.  The combined
setter emits dirty/X/Y on Android ARMv7 and both iOS targets.  Android ARM64
emits X/dirty/Y; the shared source inference follows the three-target order and
records the A64 compiler scheduling difference.

## Data flow, object lifetime, and boundary behavior

`setZoom` accepts two independent binary64 values.  It is registered directly
as the script method and also has multiple native callers.  The current xref
sets include Android ARM64 `0x673B78`, Android ARMv7 `0x55C09E`, iOS ARM64
`0x10011186C`, `0x100112BF4`, `0x1001AFE14`, and iOS ARMv7 `0x10EBE8`,
`0x110590`, `0x1AF578`.  Thus equal X/Y values are a caller choice, not part of
the setter's contract.

The five bodies use only root node zero and its dirty/scale fields.  There is no
Player-level zoom write, pending state, allocation, or empty-container fallback.
The root is constructor-owned and retained across node-tree rebuilds.

- Ordinary `!=` controls every write.  NaN always writes and dirties; equal
  infinities do not; `+0.0` and `-0.0` compare equal.
- The combined OR condition means a change in either axis rewrites both values,
  including the nominally equal axis's sign bit or NaN payload.
- No value is clamped, normalized, converted, checked for zero, or checked for
  finiteness.  Zero and negative zoom are accepted verbatim.
- The property getters return the stored binary64 values directly.

## Android ARMv7 IDB boundary repair

The registrar passed `0x594838|1` as the zoom-X setter while the IDB represented
that start as `off_594838`.  An isolated IDAPython/idat session undefined
`0x594838..0x59485E`, selected Thumb state, decoded the exact range, recreated
`Player_setRootZoomX_guess`, and saved the database.  Fresh decompilation then
matched the other three one-axis setters.

## Pre-edit local comparison

The local port uses the correct `scaleX/scaleY` fields but is still defensive
scaffolding rather than the native contract:

1. `getZoomX/getZoomY` return `1.0` for an empty deque and the setters silently
   return.  Native functions directly address root zero.
2. Property setters unconditionally assign and dirty instead of comparing with
   `!=` first.
3. Property setters write the value before dirty; every native property setter
   emits dirty before value.
4. Local `setZoom(double)` forces X=Y, writes a port-only `_zoom` scalar, guards
   root emptiness, and always dirties.  Native `setZoom(double,double)` uses a
   combined comparison and preserves independent axes.
5. `_zoom` is read by unverified local `calcViewParam`; none of these five
   native bodies writes such a Player field.  That larger scaffold remains a
   separate audit target.

The intended edit mirrors the slant correction: out-of-line conditional
property accessors, a two-argument combined method, no Player scalar side write,
and updated callers/tests.

## Applied reconstruction and verification

The port now follows the four-binary contract at the audited boundary:

- `getZoomX/getZoomY` and their setters are out-of-line implementations that
  address root zero directly, with no empty-container fallback.
- Each property setter performs the native `!=` comparison and marks the root
  dirty before assigning its axis.
- `setZoom` now accepts two independent `double` arguments, tests the combined
  `x/y` OR condition, marks dirty, and rewrites both axes in native order.
- The port-only `_zoom` write was removed from this setter.  Existing `_zoom`
  use elsewhere remains explicitly outside this audited function family.
- The native engine caller and the motionplayer DLL unit-test caller were
  updated to pass both axes explicitly.

All twenty mapped bodies were named and typed in their respective databases as
`Player_setRootZoom_guess`, `Player_getRootZoomX_guess`,
`Player_setRootZoomX_guess`, `Player_getRootZoomY_guess`, and
`Player_setRootZoomY_guess`.  The malformed iOS UTF-16 zoom symbols were also
given descriptive names.  Decompiler caches for all twenty functions were
invalidated, every body was freshly decompiled against the saved database, and
all four IDBs were saved.

Verification after the source edit:

- `git diff --check` passed (apart from existing line-ending warnings).
- `cmake --build --preset "Web Debug Build"` completed successfully in 31
  build steps and produced the final `index.html` target.
- Remaining diagnostics were pre-existing warnings involving `_tss`, ignored
  `nodiscard` results in imagepacker, pthread memory growth, JSPI, and a
  JavaScript library symbol; none was introduced by this reconstruction.
