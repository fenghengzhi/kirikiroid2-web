# Motion.Player `angleDeg` / `angleRad` four-binary reconstruction

Date: 2026-08-11

## Scope and authority

This phase rechecks the two writable root-angle properties against all four
current reference images.  Old `libkrkr2.so` addresses and comments are not used
as evidence.  The properties are audited together because they share the same
degree-valued storage, direct-edit switch, root dirty flag, and emote-motion
reselection path.

## Registration and function mapping

| Target | registrar/property anchor | `angleDeg` getter | `angleDeg` setter | `angleRad` getter | `angleRad` setter |
|---|---:|---:|---:|---:|---:|
| Android ARM64 | `0x6D3DA8`; names at `0x6D51C8` / `0x6D5240` | `0x6BEB60` | `0x6BE364` | `0x6CA4A0` | `0x6CA4CC` |
| Android ARMv7 | `0x597EC8`; blocks `0x5983AE` / `0x5983CC` | `0x58AB30` | `0x58A540` | `0x5926F0` | `0x592720` |
| iOS ARM64 | `0x1001244F8`; blocks `0x100124C34` / `0x100124C60` | `0x10011408C` | `0x1001139C8` | `0x10011D208` | `0x10011D250` |
| iOS ARMv7 | `0x123848`; blocks `0x123EE2` / `0x123F0C` | `0x111ABC` | `0x1113DC` | `0x11BB9C` | `0x11BBE8` |

The ARM32 property helper places the getter in `R2` and the setter in the stack
argument loaded from `R0`; the iOS ARM64 helper uses `X2` and `X4`.  Android
ARM64 builds the equivalent getter/setter member-pointer slots in the property
object before binding the name.  These ABI arrangements agree on the table
above.

IDA displayed several short-wchar literals as only `"a"`.  Raw UTF-16LE search
confirmed the full property names at:

| Target | `angleDeg` literal | `angleRad` literal |
|---|---:|---:|
| Android ARM64 | `0x14D53A8` | `0x14D53BA` |
| Android ARMv7 | `0xD84F40` | `0xD84F52` |
| iOS ARM64 | `0x10195B6CE` | `0x10195B6E0` |
| iOS ARMv7 | `0x174DA32` | `0x174DA44` |

This also proves that the apparently garbled A32/iOS registration operands are
not a different one-letter member.

## Player and root-node layout

All targets keep the selected angle in degrees.  Direct-edit mode selects a
Player-resident double; ordinary mode selects root node zero.

| Target | direct-edit byte | Player angle | root container | root dirty | root angle |
|---|---:|---:|---|---:|---:|
| Android ARM64 | `Player+482` | `Player+464` | libstdc++ deque `start.cur` at `*(Player+200)` | `root+1584` | `root+1616` |
| Android ARMv7 | `Player+314` | `Player+296` | libstdc++ deque `start.cur` at `*(Player+160)` | `root+1344` | `root+1376` |
| iOS ARM64 | `Player+370` | `Player+352` | libc++ deque map/start at `+168/+192`, stride `2648` | `root+1600` | `root+1632` |
| iOS ARMv7 | `Player+254` | `Player+236` | libc++ deque map/start at `+140/+152`, stride `2228` | `root+1312` | `root+1344` |

The iOS bodies spell out deque root-zero addressing; the Android bodies load
the same element through libstdc++'s cached deque `start.cur` pointer.  The
four-binary `calcViewParam` traversal exposes the Android map/start iterator
layout and disproves the earlier vector interpretation.  No getter or setter
checks container emptiness.  This is consistent with Player construction
creating root zero and node-tree rebuild preserving it for the Player lifetime.

## Exact conversion constants

All four binaries contain identical IEEE-754 binary64 constants:

| conversion | little-endian bytes | exact C++ hex-float | decimal value |
|---|---|---|---|
| degrees to radians | `39 9D 52 A2 46 DF 91 3F` | `0x1.1df46a2529d39p-6` | `0.017453292519943295` |
| radians to degrees | `F8 C1 63 1A DC A5 4C 40` | `0x1.ca5dc1a63c1f8p+5` | `57.29577951308232` |

The shorter local decimal literals `0.0174532925` and `57.2957795` round to
different binary64 values and therefore are not bit-faithful.

## Common source-level pseudocode

```cpp
double Player::getAngleDeg() const {
    return directEdit ? emoteAngle : root().delta.angle;
}

void Player::setAngleDeg(double degrees) {
    if (directEdit) {
        while (degrees < 0.0)
            degrees += 360.0;
        while (degrees >= 360.0)
            degrees -= 360.0;
        emoteAngle = degrees;
        initEmoteMotion(2);
        return;
    }

    Root &r = root();
    if (r.delta.angle != degrees) {
        r.delta.dirty = true;
        r.delta.angle = degrees;
    }
}

double Player::getAngleRad() const {
    return getAngleDeg() * 0x1.1df46a2529d39p-6;
}

void Player::setAngleRad(double radians) {
    setAngleDeg(radians * 0x1.ca5dc1a63c1f8p+5);
}
```

Three references emit `angleRad` setter as a multiply followed by a tail branch
to the degree setter: Android ARMv7 `0x592720 -> 0x58A540`, iOS ARM64
`0x10011D250 -> 0x1001139C8`, and iOS ARMv7 `0x11BBE8 -> 0x1113DC`.
Android ARM64 inlines the same degree-setter body at `0x6CA4CC`.  The joint
source-structure inference is therefore the small shared wrapper shown above,
with an Android ARM64 optimizer difference rather than two independent source
implementations.

All four radian getters inline the same selected-degree read followed by the
degree-to-radian multiply.  All four degree getters return the selected double
unchanged.

## Data flow, ordering, and lifecycle

The script-facing path is:

```text
Motion.Player property adapter
  -> TJS numeric conversion
  -> degree setter, or radian multiply -> degree setter
       -> direct-edit: normalize -> Player angle -> initEmoteMotion(2)
       -> ordinary: compare root angle -> dirty byte -> root angle
```

Direct-edit writes always invoke emote-motion initialization, even when the
normalized bit pattern equals the prior value.  That call reselects the
secondary motion using the retained emote division/motion-list state.  The
ordinary branch has no motion reinitialization; its dirty byte feeds later root
evaluation/render propagation.

The radian getter is also the internal physics angle provider.  Its four code
xrefs per target form two calls in the hair/parts spring step and two calls in
the bust-chain spring step:

| Target | first spring function/calls | second spring function/calls |
|---|---|---|
| Android ARM64 | `0x678B28`: `0x678BF8`, `0x678C90` | `0x6790C8`: `0x679298`, `0x679354` |
| Android ARMv7 | `0x55EE98`: `0x55EF2A`, `0x55EFCC` | `0x55F2F4`: `0x55F3A2`, `0x55F464` |
| iOS ARM64 | `0x1001B29D0`: `0x1001B2AEC`, `0x1001B2B88` | `0x1001B2F2C`: `0x1001B3068`, `0x1001B3130` |
| iOS ARMv7 | `0x1B24D8`: `0x1B25D0`, `0x1B2686` | `0x1B2ABC`: `0x1B2BA4`, `0x1B2C78` |

There is no angle cache, pending-root store, temporary heap object, or ownership
transfer.  Both backing doubles live for the owning Player's lifetime; the
mode byte merely selects which one participates.

## Boundary and failure behavior

- Direct-edit normalization uses repeated add/subtract loops, not `fmod`.
  Finite inputs are reduced to `[0, 360)`; the exact signed-zero input is
  retained because neither loop executes for `-0.0`.
- NaN makes both normalization comparisons false.  Direct-edit mode stores the
  NaN and calls `initEmoteMotion(2)` every time.  Ordinary mode's `!=` is true,
  so it sets dirty and stores NaN every time.
- Positive infinity remains positive infinity after subtracting `360`, and
  negative infinity remains negative infinity after adding `360`.  Therefore
  either infinity causes a non-terminating normalization loop in direct-edit
  mode.  Ordinary mode accepts infinity and performs the normal equality/dirty
  behavior.
- In ordinary mode `+0.0` and `-0.0` compare equal, so a sign-only change is not
  written.  The positive conversion factors preserve a supplied zero's sign
  before that comparison.
- Radian conversion can overflow a large finite input to infinity.  The result
  then follows the infinity behavior above.
- Getter multiplication propagates NaN and infinities and preserves signed
  zero.  It never clamps or normalizes.
- Native ordinary setters set the dirty byte before storing the angle.  There
  is no root-null/empty recovery path and no exception handling in these member
  bodies.

## Pre-edit local line-by-line comparison

The local port already selects `_emoteAngle` in direct-edit mode, uses the two
normalization loops, invokes `initEmoteMotionLike_0x6B2E90(2u)`, and exposes the
correct degree/radian property names.  It differs from the four current
references in the following observable or structural ways:

1. `getAngleDeg`, the radian getter helper, and both ordinary setter branches
   guard `_nodes.empty()` and return/skip when empty.  Every native body directly
   addresses root zero.
2. The ordinary local setter writes `angle` before `dirty`; every reference
   stores `dirty` first and `angle` second.
3. Both conversion literals are shortened and do not have the reference bit
   patterns.
4. `setAngleRad` duplicates the complete degree-setter source body.  Three of
   four targets preserve an explicit shared tail call; Android ARM64 alone
   inlines it.
5. The extra `emoteGetAngleRadLike_0x6CD0C0` helper and inline public wrapper
   encode an obsolete single-binary address and create a source-level function
   boundary that is absent from the four-reference property/caller graph.
6. `Player.h`, `PlayerCore.cpp`, `main.cpp`, and related engine comments still
   cite old `libkrkr2.so` addresses and sometimes call the radian physics getter
   `getAngleDeg` in prose.

The intended correction is to make the four public angle functions the actual
out-of-line bodies, use exact constants, share the radian setter through the
degree setter, directly access root zero, restore dirty-before-angle ordering,
and replace stale comments with these four-reference conclusions.

## Applied reconstruction and verification

The local implementation now has exactly four public out-of-line angle bodies:

- `getAngleDeg()` selects `_emoteAngle` or root zero without an empty-container
  fallback.
- `getAngleRad()` applies the exact `0x1.1df46a2529d39p-6` factor.
- `setAngleDeg()` preserves the native direct-edit loops/reinitialization and
  the ordinary dirty-before-angle store order.
- `setAngleRad()` applies the exact `0x1.ca5dc1a63c1f8p+5` factor and delegates
  to `setAngleDeg()`.

The obsolete `emoteGetAngleRadLike_0x6CD0C0` source-level helper was removed;
hair/parts and bust-chain stepping now call `getAngleRad()` directly.  The NCB,
Player, and engine comments were updated to the current four-reference result.

All 16 IDB functions were named and typed as
`Player_getAngleDeg_guess`, `Player_setAngleDeg_guess`,
`Player_getAngleRad_guess`, and `Player_setAngleRad_guess`.  Four malformed
UTF-16 start symbols were renamed as angle property literals.  The 16 Hex-Rays
caches were invalidated and freshly decompiled; ARM32 double signatures now
decompile correctly, the three radian-setter tail calls remain explicit, and
Android ARM64 still shows the optimizer-inlined body.  All four IDBs were saved.

`cmake --build --preset "Web Debug Build"` completed successfully in 18 build
steps and linked `index.html`.  `git diff --check` also passed.  The build emitted
only existing `_tss`, pthread-memory-growth, JSPI experimental, and JavaScript
library symbol warnings.
