# `outsideFactor` and `speed` four-reference audit — 2026-08-13

## Result

`Motion.Player.outsideFactor` and `Motion.Player.speed` are independent raw
`double` fields. All four current reference binaries give them direct load/store
accessors: neither setter clamps, rejects non-finite values, dirties render
state, nor invokes another function. Their constructor defaults are exactly
`1.5` and `1.0`, respectively.

The important correction is the role of `outsideFactor`. It does **not** alter
the public `calcBounds()` result and it does **not** expand the target rectangle
passed to the render-command builder. The accurate SeparateLayerAdaptor path
maintains two distinct rectangles:

```text
target [0, 0, width, height]
    |
    +--> inverse draw-affine + camera offset
    |         --> center scaling by outsideFactor
    |         --> Player particle-outside scratch rectangle
    |                  |
    |                  +--> particle child deleteOutside test
    |
    +---------------------------------------------------------->
               unchanged target rectangle --> command builder
```

`speed` remains the raw per-Player time multiplier. Frame progress computes
`_deltaTime = speed * delta`; the anchor-feedback path recovers/scales the
incoming time from `_deltaTime / speed`. There is no separate Boolean “speed
flag” and no setter-side normalization.

## Direct property evidence

| Reference | `outsideFactor` getter / setter | field | `speed` getter / setter | field |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D6A3C` / `0x6D6A44` | `Player+1160` (`0x488`) | `0x6D6A5C` / `0x6D6A64` | `Player+1168` (`0x490`) |
| Android armv7 | `0x598E82` / `0x598E8C` | `Player+816` (`0x330`) | `0x598EAA` / `0x598EB4` | `Player+824` (`0x338`) |
| iOS arm64 | `0x100125580` / `0x100125588` | `Player+1048` (`0x418`) | `0x1001255A0` / `0x1001255A8` | `Player+1056` (`0x420`) |
| iOS armv7 | `0x12477A` / `0x124784` | `Player+748` (`0x2EC`) | `0x1247A2` / `0x1247AC` | `Player+756` (`0x2F4`) |

Constructor stores:

| Reference | `outsideFactor = 1.5` | `speed = 1.0` |
|---|---:|---:|
| Android arm64 | `0x6CC54C` | `0x6CC49C` |
| Android armv7 | `0x593896` | `0x5937D2` |
| iOS arm64 | `0x10011EEE0` | `0x10011EE20` |
| iOS armv7 | `0x11D950` | `0x11D7F8` |

The direct loads/stores preserve negative and positive infinity, negative and
positive zero, arbitrary finite values, and NaN. Any unusual downstream result
comes from the consumer's floating-point operations rather than the property
boundary.

## NCB member registration

The `Motion.Player` member registrars and the literal-reference sites that bind
these accessors are:

| Reference | registrar | `outsideFactor` reference | `speed` reference |
|---|---:|---:|---:|
| Android arm64 | `0x6D3DA8` | `0x6D45F0` (string `0x14D63D6`) | `0x6D46E0` (string `0x14D385C`) |
| Android armv7 | `0x597EC8` | `0x5980B0` (string `0xD85CE4`) | `0x5980EC` (string `0xD8433C`) |
| iOS arm64 | `0x1001244F8` | `0x1001247BC` (string `0x10195CAA8`) | `0x100124814` (string `0x10195CAE8`) |
| iOS armv7 | `0x123848` | `0x123ACC` (string `0x174EE0C`) | `0x123B20` (string `0x174EE4C`) |

The Android-arm64 `speed` literal has unrelated references elsewhere, so the
member-table use rather than a raw string hit is the registration proof.

## Accurate SLA data flow

The only cross-reference to the outside-rectangle helper in each binary is the
accurate SeparateLayerAdaptor renderer:

| Reference | accurate renderer | outside helper call | builder call |
|---|---:|---:|---:|
| Android arm64 | `0x6C7088` | `0x6C71A4` -> `0x6C6E8C` | `0x6C7254` -> `0x6C2208` |
| Android armv7 | `0x590468` | `0x590540` -> `0x590148` | `0x59055E` -> `0x58C7C4` |
| iOS arm64 | `0x10011A9E8` | `0x10011AAE0` -> `0x10011A7A4` | `0x10011AB08` -> `0x1001167BC` |
| iOS armv7 | `0x118D70` | `0x118ED4` -> `0x118AA0` | `0x118EFA` -> `0x114118` |

The two calls do not share a mutable rectangle. The helper receives one local
`[0,0,width,height]` rectangle and writes its result into Player fields. The
builder receives a separate stack-local `[0,0,width,height]` rectangle that is
still unchanged. Concrete locals include Android arm64 `SP+var_2B0`, Android
armv7 `var_14C`, iOS arm64 `var_210`, and iOS armv7 `var_1C0` for the builder
argument. Android arm64 reads the builder's four input floats at
`0x6C31B0/0x6C31B4` and `0x6C32CC/0x6C32D0`; iOS arm64 does the corresponding
reads at `0x1001173FC`, `0x100117414`, `0x100117534`, and `0x100117548`.

The helper's persistent destination layout is:

| Reference | helper | left / top / right / bottom |
|---|---:|---:|
| Android arm64 | `0x6C6E8C` | `Player+848/+852/+856/+860` |
| Android armv7 | `0x590148` | `Player+576/+580/+584/+588` |
| iOS arm64 | `0x10011A7A4` | `Player+736/+740/+744/+748` |
| iOS armv7 | `0x118AA0` | `Player+512/+516/+520/+524` |

Those four floats are initialized to zero by construction and remain stale or
zero until the accurate SLA renderer refreshes them. Ordinary render paths do
not synthesize a replacement rectangle.

## Outside-rectangle numeric pipeline

The four helpers agree on the same unguarded calculation. Let the stored draw
affine be:

```text
[ m11  m12  m14 ]
[ m21  m22  m24 ]
```

Field locations are:

| Reference | camera X/Y | matrix `m11/m12/m21/m22` | affine translation X/Y |
|---|---:|---:|---:|
| Android arm64 | `+144/+148` | `+808/+816/+824/+832` | `+840/+844` |
| Android armv7 | `+112/+116` | `+536/+544/+552/+560` | `+568/+572` |
| iOS arm64 | `+120/+124` | `+696/+704/+712/+720` | `+728/+732` |
| iOS armv7 | `+96/+100` | `+472/+480/+488/+496` | `+504/+508` |

The native order is:

```text
det  = m11*m22 - m21*m12
im11 =  m22/det
im12 = -m12/det
im21 = -m21/det
im22 =  m11/det

offsetX = float(im11*m14 + im12*m24 + cameraOffsetX)
offsetY = float(im21*m14 + im22*m24 + cameraOffsetY)
```

The float affine translations and camera offsets are promoted to double for
the complete sums; each sum is narrowed once, at the end. Every input rectangle
corner is then transformed by the inverse linear matrix plus this offset. Each
corner coordinate is narrowed to float before the ordered min/max reduction.

The reduced edges are `left/top/right/bottom`. Midpoints deliberately perform
the edge addition in float before promotion:

```text
centerX = double(float(left + right)) * 0.5
centerY = double(float(top + bottom)) * 0.5

outLeft   = float(centerX + outsideFactor * (left   - centerX))
outTop    = float(centerY + outsideFactor * (top    - centerY))
outRight  = float(centerX + outsideFactor * (right  - centerX))
outBottom = float(centerY + outsideFactor * (bottom - centerY))
```

There is no singular-matrix, finite-value, rectangle-order, or factor-range
guard. For an ordinary ordered rectangle, `1.5` expands it, `1` preserves it,
`0` collapses it to its float midpoint, and a negative factor reverses the
edge order. Singular and unordered floating inputs retain their natural IEEE
NaN/infinity behavior.

## Particle `deleteOutside` consumer

The first pass of the particle-child worker consumes the persistent four-float
rectangle from the root Player (`*a1`, corresponding to the port's
`_rootPlayer`):

| Reference | worker | outside-rectangle reads |
|---|---:|---:|
| Android arm64 | `0x6BEB84` | `0x6BEC94..0x6BECC4` |
| Android armv7 | `0x58AB50` | `0x58ABE0..0x58AC16` |
| iOS arm64 | `0x1001140C8` | `0x1001141A8..0x1001141D8` |
| iOS armv7 | `0x111AF8` | `0x111BCE..0x111C04` |

Its lifecycle and boundary behavior is:

1. A child whose `_allplaying` flag is false is erased.
2. A playing child is retained without geometric tests when the particle node's
   `deleteOutside` flag is false.
3. With `deleteOutside` true, an ordered inverted child AABB
   (`maxX < minX || maxY < minY`) is retained.
4. Otherwise all four strict overlap comparisons must pass:

   ```text
   child.maxX > outside.left
   child.minX < outside.right
   child.maxY > outside.top
   child.minY < outside.bottom
   ```

   Touching any edge is therefore outside and causes erasure.

The inverted-bounds test uses ordered comparisons. A NaN does not take that
retain branch; it makes the later strict overlap chain fail and is erased. This
distinguishes ordered inverted bounds from unordered floating-point values.

## `speed` consumers

The same raw multiplier is observed in both time-flow sites:

| Consumer | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| frame-progress multiplication | `0x6BE460` | `0x58A648` | `0x100113B68` | `0x11156E` |
| anchor-feedback time recovery | `0x6BDC74` | `0x589D62` | `0x100113674` | `0x110AF0` |

Frame progress writes `_deltaTime = speed * delta`. Anchor feedback follows
the native algebra containing `_deltaTime / speed`; it does not special-case
zero, NaN, or infinity. Consequently the direct property's unusual values can
produce IEEE division/multiplication results in the consumer, exactly as in the
references.

## Port corrections

- Added the root-owned four-float particle-outside scratch rectangle with the
  native zero initial state.
- Added the inverse-affine/camera/outsideFactor numeric helper and invoke it only
  in the accurate SLA route, immediately before command construction.
- Kept the builder's target rectangle separate and unchanged.
- Replaced the port-invented `0.._width` / `0.._height` particle deletion test
  with the root scratch rectangle and the native strict overlap chain.
- Preserved the existing raw `speed` multiplier flow, while replacing stale
  comments that treated an adjacent Boolean as a possible speed flag.
- Added regression coverage for defaults, typed-property raw-double boundaries,
  factor geometry, affine/camera transformation, singular matrices, strict edge
  contact, ordered inverted child bounds, and NaN child bounds.
- Renamed and documented the accessors, helper, accurate renderer, command
  builder, and particle worker in all four IDBs.

## Stale-analysis corrections

The current four references invalidate three older assumptions:

- `outsideFactor` is not part of public bounds computation.
- Its expanded rectangle is not the render-command builder's target clip.
- Particle `deleteOutside` is not tested against the Player's logical
  `_width/_height` rectangle.

The correct flow is accurate SLA target rectangle -> root Player particle
scratch rectangle -> particle child pruning, alongside an unchanged target
rectangle that independently enters command construction.

## Validation

- Full Web debug build passed through final `index.html`/Wasm link and
  `sync_prealloc_memory`.
- Full Wasmtime-headless debug build passed through the same final link stage;
  both the normal plugin target and the Wasmtime guest-object target compiled
  the changed particle implementation.
- The complete `motionplayer-dll.cpp` Catch2 translation unit passed an
  Emscripten `-fsyntax-only` compile using the active Web target's flags and the
  repository's prepared Catch2 headers. The only diagnostic was the existing
  deprecated whitespace before the `_tss` literal-operator suffix.
- Immediate rebuilds of both trees reported `ninja: no work to do.`
- `git diff --check` exited successfully; its output contained only the
  repository's existing LF-to-CRLF working-copy warnings.
- All four improved IDBs were saved after correcting the particle-worker
  comments to distinguish ordered inverted bounds from unordered/NaN values.
