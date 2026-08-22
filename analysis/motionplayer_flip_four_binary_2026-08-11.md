# Motion.Player flip accessors and combined setter: four-binary reconstruction

Date: 2026-08-11

## Registration mapping

The names were located independently as UTF-16LE byte sequences in every
reference binary.  This avoids relying on IDA's truncated one-character
rendering of several wide literals.

| Target | `setFlip` name reference/body | `flipX` name reference/get/set | `flipY` name reference/get/set |
|---|---|---|---|
| Android ARM64 | `0x6D54B0` / `0x6BE2FC` | `0x6D5530` / `0x6D6CA0` / `0x6CA448` | `0x6D55A8` / `0x6D6CAC` / `0x6CA46C` |
| Android ARMv7 | `0x59846C` / `0x58A4D8` | `0x598482` / `0x59902E` / `0x5926AE` | `0x5984A0` / `0x599038` / `0x5926C6` |
| iOS ARM64 | `0x100124D5C` / `0x100113910` | `0x100124D7C` / `0x100125738` / `0x10011D14C` | `0x100124DA8` / `0x100125760` / `0x10011D194` |
| iOS ARMv7 | `0x123FEA` / `0x111326` | `0x124008` / `0x12496C` / `0x11BB02` | `0x124032` / `0x124994` / `0x11BB3A` |

The method callbacks take two independent boolean arguments after `this`.
This is visible directly in all four bodies and in native callers that load two
neighboring source-node bytes before calling the combined setter.

## Root container and field layout

| Target | root representation | dirty | flip X | flip Y |
|---|---|---:|---:|---:|
| Android ARM64 | libstdc++ deque `start.cur` `*(Player+200)` | `+1584` | `+1587` | `+1588` |
| Android ARMv7 | libstdc++ deque `start.cur` `*(Player+160)` | `+1344` | `+1347` | `+1348` |
| iOS ARM64 | deque map/start `+168/+192`, stride `2648` | `+1600` | `+1603` | `+1604` |
| iOS ARMv7 | deque map/start `+140/+152`, stride `2228` | `+1312` | `+1315` | `+1316` |

Each accessor resolves root element zero directly.  There is no size test,
empty-container return, pending transform state, allocation, or Player-level
flip write in any of the twenty functions.

## Common source reconstruction

```cpp
bool Player::getFlipX() const {
    return root().delta.flipX;
}

void Player::setFlipX(bool x) {
    Root &r = root();
    if (r.delta.flipX != x) {
        r.delta.flipX = x;
        r.delta.dirty = true;
    }
}

bool Player::getFlipY() const {
    return root().delta.flipY;
}

void Player::setFlipY(bool y) {
    Root &r = root();
    if (r.delta.flipY != y) {
        r.delta.flipY = y;
        r.delta.dirty = true;
    }
}

void Player::setFlip(bool x, bool y) {
    Root &r = root();
    if (r.delta.flipX != x || r.delta.flipY != y) {
        r.delta.flipX = x;
        r.delta.flipY = y;
        r.delta.dirty = true;
    }
}
```

The value-before-dirty ordering is shared by Android ARM64 and both iOS
targets for the property setters.  Android ARMv7 emits dirty-before-value for
the property setters.  For the combined setter, Android ARM64 and both iOS
targets emit X/Y/dirty, while Android ARMv7 emits Y/X/dirty.  The common source
inference follows the three-target majority; the ARMv7 orders are compiler
scheduling differences and do not change observable single-threaded state.

## Data flow and lifetime

The combined setter is both the script-facing `setFlip` method and an internal
root-copy primitive.  For example:

- iOS ARM64 `0x100111850/0x100111854` loads source-node bytes at `+0x5F4` and
  `+0x5F3`, then calls the combined setter at `0x10011185C`.
- iOS ARM64 repeats the same two-axis flow at `0x100112AD8..0x100112AE4`.
- iOS ARMv7 loads independent bytes at `+0x4D3/+0x4D4` before calls at
  `0x10EBC8` and `0x110448`.

This independently proves that equal X/Y values are merely a caller choice,
not the method contract.  The one-axis X setter also has native callers in the
engine mirror path (for example Android ARM64 `0x66F1BC` and Android ARMv7
`0x55A356`), while Y may remain unchanged.

The target fields belong to the constructor-owned root node.  That root is
kept as element zero throughout Player lifetime and survives the node-tree
rebuild paths already audited.  A changed flag causes the root delta to be
reevaluated by the transform/update pipeline; an identical request leaves both
the values and dirty flag untouched.

## Boundary behavior

- For canonical C++ boolean inputs, identical values cause no writes.  A
  change in either axis causes the combined setter to rewrite both axes and set
  dirty, even when one axis was already equal.
- Getters load the stored byte and return it directly as the boolean result.
- Android ARM64 explicitly masks incoming arguments with `& 1`.  The other
  three generated bodies compare the stored unsigned byte with the full ABI
  integer and store its low byte.  This is only distinguishable for
  non-canonical values passed by bypassing the C++ `bool`/NCB conversion
  contract; registered script calls and native callers supply canonical 0/1.
- At that deliberately invalid raw-ABI boundary, Android ARM64 canonicalizes
  by the low bit.  On the other targets values such as `2` can be stored as byte
  `2`, while `256` compares unequal, stores byte zero, and may dirty again on a
  repeated raw call.  This is recorded as emitted-code behavior, not a valid
  source-level `bool` guarantee.
- No normalization couples X and Y, and no code redirects this method through
  the separate mirror state or `setMirror` path.

## Pre-edit local comparison

The local port diverges at every audited contract edge:

1. Inline getters return `false` when `_nodes` is empty; native getters address
   root zero directly.
2. Inline property setters silently return on an empty container and always
   assign/dirty; native setters directly address root and compare first.
3. The local property setters happen to write value then dirty, which matches
   the inferred common source order, but they lack the native no-change gate.
4. Local `setFlip(bool)` forces X=Y.  Native `setFlip(bool,bool)` preserves two
   independent axes and uses a combined OR condition.
5. Local `setFlip` writes port-only `_flip`, guards the root, and always marks
   dirty.  No one of the four native bodies contains a Player scalar write or
   a root-presence guard.
6. The stale nearby comment still describes the root writes plus legacy scalar
   as intentional compatibility scaffolding.  This is now disproved for the
   audited setter and must be replaced with the four-binary contract.

The intended edit is limited to this function family and its direct callers:
out-of-line direct accessors, compare-before-write property setters, the
two-argument combined method, removal of the `_flip` side write, and an updated
unit-test call.  The unrelated local `calcViewParam` scaffold and the separate
`setMirror` audit remain distinct future work.

## Applied reconstruction and verification

The audited source boundary now matches the common four-binary contract:

- `getFlipX/getFlipY` and their setters are out-of-line and address root zero
  directly, with no empty-container fallback.
- Each property setter uses compare-before-write and the inferred common
  value-before-dirty order.
- `setFlip` now accepts two independent `bool` values, uses the combined OR
  change test, rewrites both axes, then marks the root dirty.
- The port-only `_flip` side write and root-presence guard were removed from
  `setFlip`; the direct unit-test caller now passes both axes explicitly.
- The NCB registration remains `NCB_METHOD(setFlip)`.  The Web build confirms
  that its method adapter accepts the corrected two-argument member signature.

All twenty mapped functions were named and typed in their respective IDBs as
`Player_setRootFlip_guess`, `Player_getRootFlipX_guess`,
`Player_setRootFlipX_guess`, `Player_getRootFlipY_guess`, and
`Player_setRootFlipY_guess`.  The `setFlip`, `flipX`, and `flipY` UTF-16 start
symbols were also renamed descriptively in all four databases.  Every function
was freshly decompiled after applying its boolean prototype, and all four IDBs
were saved.

Verification after the source and IDB edits:

- A repository-wide search finds only the corrected declaration,
  implementation, and two-argument test call for `Player::setFlip`.
- `git diff --check` passed (apart from existing line-ending warnings).
- `cmake --build --preset "Web Debug Build"` completed successfully; the
  final incremental pass performed 13 steps and linked `index.html`.
- Build diagnostics were the existing `_tss`, pthread memory-growth, JSPI, and
  JavaScript library symbol warnings; no flip reconstruction error remains.
