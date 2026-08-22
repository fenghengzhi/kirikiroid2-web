# Motion.Player root `coordinate` / `transformOrder` four-binary record (2026-08-11)

## Scope and authority

This record replaces the old `libkrkr2.so`-only comments around the two
`Motion.Player` properties with fresh evidence from all four reference
binaries:

- Android ARM64: `Kirikiroid2_1.3.9_Android_arm64-v8a.so`
- Android ARMv7: `Kirikiroid2_1.3.9_Android_armabi-v7a.so`
- iOS ARM64: `Kirikiroid2_1.3.9_iOS_arm64`
- iOS ARMv7: `Kirikiroid2_1.3.9_iOS_armv7`

The accessors below were located from each target's `Motion.Player` NCB
registration and freshly decompiled in the current investigation.  Function
addresses stay in this analysis file; compiled source uses semantic names only.

## Four-target mapping

### `coordinate`

| Target | Registration site | Setter | Getter | Player root storage | Node field |
| --- | ---: | ---: | ---: | --- | ---: |
| Android ARM64 | `0x6D4C88` | `0x6B1D60` | `0x6D6B50` | direct root pointer at `Player+200` | `root+24` |
| Android ARMv7 | `0x598254` / `0x598262` | `0x5817AC` | `0x598FC8` | direct root pointer at `Player+160` | `root+16` |
| iOS ARM64 | `0x100124A24` | `0x100109170` | `0x100125688` | deque index 0 via `Player+168/+192` | `root+24` |
| iOS ARMv7 | `0x123D18` / `0x123D1E` / `0x123D32` | `0x10699C` | `0x1248AC` | deque index 0 via `Player+140/+152` | `root+16` |

The 64-bit and 32-bit node offsets differ only because of target layout.  In
both layouts this is the field already represented locally by
`MotionNode::coordinateMode`.

### `transformOrder`

| Target | Registration site | Getter | Setter | Root order field | Root dirty byte |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x6D4C10` | `0x6C9568` | `0x6C96A4` | `root+84..96` | `root+1584` |
| Android ARMv7 | `0x598236` | `0x591F98` | `0x59202C` | `root+68..80` | `root+1344` |
| iOS ARM64 | `0x1001249F8` | `0x10011C83C` | `0x10011C8E8` | `root+84..96` | `root+1600` |
| iOS ARMv7 | `0x123CEE` | `0x11B0A0` | `0x11B194` | `root+68..80` | `root+1312` |

The order field is the outer `MotionNode::transformOrder[4]`, not either clip
slot's optional frame-content `transformOrder`.  The dirty byte is the first
member of the root node's setter/camera-override block, represented locally by
`MotionNode::delta.dirty`.

## Common source-level behavior

The four `coordinate` implementations reduce to:

```cpp
void Player::setCoordinate(int value) {
    nodes[0].coordinateMode = value;
}

int Player::getCoordinate() const {
    return nodes[0].coordinateMode;
}
```

There is no validation, normalization, dirty flag, recursive propagation, or
empty-container guard.  All four binaries rely on the constructor/tree
invariant that the root node exists.

The four `transformOrder` getters reduce to:

```cpp
Variant Player::getTransformOrder() const {
    Array result;
    for (int i = 0; i != 4; ++i)
        result.nativeItems.push_back(nodes[0].transformOrder[i]);
    return result;
}
```

They create a TJS Array and append four integer variants directly to the
array native-instance Items deque, in root-field order.

The four setters reduce to:

```cpp
void Player::setTransformOrder(Variant value) {
    Dispatch *array = value.coerceToObjectOrThrow();
    MotionNode &root = nodes[0];
    bool used[4] = {};

    for (int i = 0; i != 4; ++i) {
        Variant element;
        int v = 0;
        if (succeeded(array->PropGetByNum(TJS_MEMBERMUSTEXIST,
                                          i, &element, array)))
            v = element.AsInteger();

        if (static_cast<unsigned>(v) > 3 || used[v])
            throw "illegul variable for transform order";

        if (root.transformOrder[i] != v) {
            root.transformOrder[i] = v;
            root.delta.dirty = true;
        }
        used[v] = true;
    }
}
```

## Target-specific representation differences

- Android ARM64 stores a direct pointer to the root node at `Player+200`.
  Its compiler unrolls the four setter iterations.
- Android ARMv7 stores a direct pointer to the root node at `Player+160`.
- Both iOS builds address index zero through the target STL deque map and
  start-offset fields rather than keeping a direct root pointer.
- The iOS implementations retain a loop in the setter.  These ABI/container
  differences do not change the source-level ordering or edge behavior.
- 64-bit nodes place `coordinateMode` at `+24` and `transformOrder` at `+84`;
  32-bit nodes place them at `+16` and `+68` respectively.

## Boundary and failure behavior

- Assigning a non-object `transformOrder` value enters TJS variant-to-object
  conversion and throws; it is not silently ignored.  Locally,
  `tTJSVariant::AsObjectNoAddRef()` provides the same type check/throw behavior.
- Each read uses `TJS_MEMBERMUSTEXIST` (`1024`).  Unlike the class-level
  `defaultTransformOrder` setter, a failed indexed read does **not** throw the
  `"illegul size of transform order"` message here.  It substitutes `0`, then
  performs the ordinary range/duplicate check.
- Range checking is unsigned.  Negative integers and integers above `3` both
  fail the same validation.
- The native exception text is exactly
  `"illegul variable for transform order"` (typo preserved).
- Writes are incremental, not transactional.  Each successfully validated
  element is compared and written immediately.  If a later element is invalid,
  earlier changes remain observable and the dirty byte remains set.
- The dirty byte is set only when a stored element actually changes.  Writing
  the same valid permutation leaves its prior dirty state untouched.
- The coordinate setter is a raw integer store and intentionally does not set
  this dirty byte.

## Pre-edit local line-by-line comparison

Before the corresponding source edit:

- `Player.h` implements `coordinate` through a standalone `_coordinate`
  scalar, while all four binaries read/write the root node field.
- `PlayerCore.cpp::getTransformOrder()` returns standalone
  `_transformOrder[4]`, while all four binaries return the root node field.
- `PlayerCore.cpp::setTransformOrder()` returns silently for a non-object,
  while all four binaries perform throwing object conversion.
- The local setter uses flag `0` and returns on the first failed `PropGetByNum`;
  all four binaries use flag `1024`, substitute integer `0` on failure, and
  continue into validation.
- The local setter stages all four values in a temporary array and commits only
  after full validation; all four binaries commit element-by-element.
- The local setter never marks the root dirty; all four binaries set the root
  dirty byte after each changed element.
- `_coordinate` and `_transformOrder` are therefore dead/inaccurate mirrors
  that can diverge from the fields consumed by runtime layer evaluation.

The planned correction is limited to wiring both properties to `_nodes[0]`,
matching the exact setter failure ordering, deleting the two standalone Player
mirrors, and replacing the stale single-binary/pending comments.

## Applied implementation and verification

- `Player::getCoordinate` / `setCoordinate` now read and write
  `_nodes[0].coordinateMode` directly.
- `Player::getTransformOrder` now serializes
  `_nodes[0].transformOrder[0..3]`.
- `Player::setTransformOrder` now performs throwing object conversion, uses
  `TJS_MEMBERMUSTEXIST`, substitutes `0` after an indexed read failure, validates
  with an unsigned range test, writes each element immediately, and sets
  `_nodes[0].delta.dirty` only after a changed write.
- The standalone `_coordinate` and `_transformOrder` Player fields were removed.
- All four IDBs now name the four accessors
  `Player_getCoordinate_guess`, `Player_setCoordinate_guess`,
  `Player_getTransformOrder_guess`, and `Player_setTransformOrder_guess`.
  Confirmable coordinate/setter prototypes were applied, all four decompiler
  caches were invalidated, and fresh setter pseudocode retained the control flow
  recorded above.
- All four IDBs were saved successfully.
- `git diff --check` passed (apart from the repository's existing line-ending
  conversion warnings).
- `cmake --build out/web/debug` completed all 31 incremental compile/link steps
  successfully.  The emitted warnings are pre-existing compiler/toolchain
  warnings; there was no new compile or link error.
