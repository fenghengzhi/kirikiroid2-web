# Motion.Player slant accessors and combined setter: four-binary reconstruction

Date: 2026-08-11

## Scope

This phase covers the writable `slantX` and `slantY` properties plus the
script-visible `setSlant` method.  It uses only the four current reference
images; old `libkrkr2.so` addresses in the port are treated as stale.

## Registration and function mapping

| Target | `setSlant` registration/body | `slantX` registration/get/set | `slantY` registration/get/set |
|---|---|---|---|
| Android ARM64 | `0x6D577C` / `0x6BE3D8` | `0x6D581C` / `0x6D6CD0` / `0x6CE73C` | `0x6D5894` / `0x6D6CDC` / `0x6CE75C` |
| Android ARMv7 | `0x598522` / `0x58A5C8` | `0x598538` / `0x599056` / `0x594884` | `0x598556` / `0x599068` / `0x5948AA` |
| iOS ARM64 | `0x100124E6C` / `0x100113A64` | `0x100124E8C` / `0x1001257D8` / `0x100120024` | `0x100124EB8` / `0x100125800` / `0x10012006C` |
| iOS ARMv7 | `0x1240E0` / `0x11147C` | `0x1240FE` / `0x124A0C` / `0x11ED34` | `0x124128` / `0x124A3C` / `0x11ED7A` |

Raw UTF-16LE search confirmed `slantX`, `slantY`, and `setSlant` in all four
images.  In particular, the one-character labels shown by IDA at several iOS
and Android registration operands are string-splitting artifacts.

## ABI/container layout

| Target | root container | dirty | slant X | slant Y |
|---|---|---:|---:|---:|
| Android ARM64 | libstdc++ deque `start.cur` at `*(Player+200)` | `+1584` | `+1640` | `+1648` |
| Android ARMv7 | libstdc++ deque `start.cur` at `*(Player+160)` | `+1344` | `+1400` | `+1408` |
| iOS ARM64 | libc++ deque map/start `Player+168/+192`, stride `2648` | `+1600` | `+1656` | `+1664` |
| iOS ARMv7 | libc++ deque map/start `Player+140/+152`, stride `2228` | `+1312` | `+1368` | `+1376` |

ARM32 passes the first aligned double after `this` in `R2:R3` and the second
double on the stack.  This made the combined method especially easy to mistake
for a one-argument method when its Thumb boundary was missing.  With the correct
prototype, all targets agree that `setSlant` accepts two independent doubles.

## Common source-level pseudocode

```cpp
double Player::getSlantX() const {
    return root().delta.slantX;
}

void Player::setSlantX(double x) {
    Root &r = root();
    if (r.delta.slantX != x) {
        r.delta.dirty = true;
        r.delta.slantX = x;
    }
}

double Player::getSlantY() const {
    return root().delta.slantY;
}

void Player::setSlantY(double y) {
    Root &r = root();
    if (r.delta.slantY != y) {
        r.delta.dirty = true;
        r.delta.slantY = y;
    }
}

void Player::setSlant(double x, double y) {
    Root &r = root();
    if (r.delta.slantX != x || r.delta.slantY != y) {
        r.delta.dirty = true;
        r.delta.slantX = x;
        r.delta.slantY = y;
    }
}
```

Every one-axis setter emits dirty-before-value.  For the combined setter,
Android ARMv7 and both iOS targets emit dirty, X, Y.  Android ARM64 emits X,
dirty, Y; this is the only target-specific scheduling difference.  The shared
source inference uses the three-target dirty-first order while preserving the
same single combined condition and two unconditional value stores.

## Data flow and lifetime

There is no Player-level slant cache in any of these five native bodies.  Every
read and write goes straight to the constructor-owned root node.  The combined
setter is used both as the script method and as a native propagation sink.  For
example, iOS ARM64 loads two independent doubles from a source node at
`0x1001118A0/0x1001118A4` and calls the combined setter at `0x1001118AC`; iOS
ARMv7 performs the corresponding two-double call at `0x10EC20..0x10EC3C`.
The scale controller separately calls the same logical sink with equal X/Y
values, but equality of those arguments belongs to that caller, not to the
setter contract.

The root delta survives motion-load tree rebuilds because only the non-root
suffix is removed.  No allocation, ownership transfer, pending-value object, or
exception recovery occurs in the slant bodies.

## Floating-point and boundary behavior

- One-axis setters use ordinary IEEE-754 `!=`.  NaN always writes and dirties;
  equal infinities do not write; `+0.0` and `-0.0` compare equal and therefore
  do not replace one another's sign bit.
- The combined setter tests `oldX != x || oldY != y`, then writes both axes.
  If only Y differs, X is still rewritten, including a sign-bit or NaN-payload
  change that the X comparison alone would not have triggered.
- If either new argument is NaN, its comparison is true and both values are
  written.  Repeated NaN calls therefore dirty and write on every call.
- Values are not normalized, clamped, converted, or checked for finiteness.
- Getters and setters have no empty-root guard or fallback.  Their valid object
  invariant is that Player construction already created root zero.

## Android ARMv7 IDB boundary repair

The registrar passed Thumb pointers `0x58A5C8|1` and `0x594884|1`, but the IDB
had named both starts as data (`off_58A5C8`, `off_594884`).  Using an isolated
IDAPython/idat session, the ranges `0x58A5C8..0x58A60A` and
`0x594884..0x5948AA` were undefined, assigned `T=1`, decoded instruction by
instruction, recreated as functions, and saved.  Fresh decompilation then
recovered the two-double combined setter and the ordinary X setter exactly.

## Pre-edit local line-by-line comparison

The local port stores slant in the correct root delta fields, but differs in
five connected ways:

1. Both getters and both property setters guard `_nodes.empty()` and return a
   fallback/no-op.  No reference body has that branch.
2. The property setters always write and dirty, even for equal finite values.
   Native bodies compare first and do nothing when equal.
3. Local property setters write the value before dirty; all four references
   write dirty first.
4. Local `setSlant(double)` has one argument and forces X=Y.  Native
   `setSlant(double,double)` accepts two independent values, uses one OR
   condition, and writes both when either differs.
5. The combined local method also writes a port-only `_slant` scalar and uses an
   empty-root guard.  No such Player-field write occurs in any reference body.
   `_slant` is also read by the still-unverified local `calcViewParam`, so that
   broader scaffold should be audited separately rather than silently treated
   as native state here.

The intended correction is to move the four property bodies out of their
defensive inline scaffolding, implement exact compare/dirty/write behavior,
make `setSlant` a two-argument combined setter, update its engine/test callers,
and remove stale single-target comments without yet inferring unrelated
`calcViewParam` semantics.

## Applied reconstruction and verification

`getSlantX/getSlantY` and their setters are now out-of-line direct root accessors.
The one-axis setters use ordinary `!=`, write dirty before the selected value,
and have no empty-container fallback.  `setSlant` now accepts `(x, y)`, uses one
OR condition, and writes dirty/X/Y when either value differs.  It no longer
writes the port-only `_slant` scalar.

The scale controller explicitly calls `setSlant(out[0], out[0])`; the unit-test
call was likewise updated to pass two values.  This retains the caller-specific
equal-axis behavior without narrowing the native setter contract.

All 20 IDB functions were named and typed as the combined setter plus the four
X/Y property accessors.  Malformed UTF-16 literal starts were also named.  After
forcing all 20 functions to recompile, fresh decompilation retained the common
container/compare/write behavior and the Android ARM64 store-scheduling
difference.  The repaired Android ARMv7 functions and all four IDBs were saved.

`cmake --build --preset "Web Debug Build"` completed successfully in 31 build
steps, including NCB registration and final `index.html` linkage.  The remaining
diagnostics are the existing `_tss`, imagepacker `nodiscard`, pthread memory
growth, JSPI experimental, and JavaScript-library symbol warnings.
