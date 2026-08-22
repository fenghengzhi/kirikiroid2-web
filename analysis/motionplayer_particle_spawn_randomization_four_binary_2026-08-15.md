# MotionPlayer type-4 particle spawn and randomization — four-reference audit (2026-08-15)

## Scope and result

This note covers the spawn half of the type-4 particle-system update:

1. retaining and indexing the motion-source list;
2. constructing and linking the child `Player`;
3. sampling spawn position, speed, spread, angle, and zoom;
4. deriving position and velocity in both coordinate modes;
5. publishing the child, enforcing the maximum count, and entering the
   existing-child worker.

The existing-child inheritance and trigger/timer prefix is documented in
`motionplayer_particle_inherit_emission_control_four_binary_2026-08-15.md`.
The two-pass worker remains a separate lifecycle phase.

Fresh decompilation of all four current reference binaries found five
observable differences from the previous Web reconstruction:

- the source-list dispatch has its own retained temporary owner, distinct from
  the retained child-Array dispatch, and remains alive through the optional
  child worker;
- source selection truncates `random * signedCount` without clamping the
  resulting index;
- the selected string is split on every `/`, after which elements 1 and 2 are
  read without a size check and element 0 is ignored;
- the distance-fitting exponential-decay expression has no domain, duration,
  denominator, or finite-value guards;
- inverse zoom divides every velocity component unconditionally, including
  when zoom is zero, negative, infinite, or NaN.

All four references also preserve a zero-source-count permanent loop. The Web
port now deliberately retains that malformed-input behavior rather than
inventing recovery semantics.

## Function and caller map

The stripped function remains named `Player_updateParticleSystems_guess`.

| target | function | size | sole call site in Player update |
|---|---:|---:|---:|
| Android arm64 | `0x6BC4BC` | `0x144C` | `0x6B9080` |
| Android arm32 | `0x588A48` | `0x108C` | `0x586020` |
| iOS arm64 | `0x100111D08` | `0x1184` | `0x10010EFC4` |
| iOS arm32 | `0x10F51C` | `0x1278` | `0x10C846` |

The four functions have the same high-level topology. After the front-half
trigger code computes a signed `emitCount`, the spawn block creates at most one
child during that pass. `emitCount` later controls whether the out-of-line
worker is called; it is not an inner multi-spawn count.

## Independent source-list receiver lifetime

The child Array has already been retained near function entry. The spawn block
then constructs a second native temporary from the node's source-list Variant.
That temporary independently AddRefs the source-list dispatch and uses the
retained pointer as both callee and `objthis` for its `count` and numeric
property calls.

| target | retain/wrapper construction | count read | zero-count branch | numeric getter | split call |
|---|---:|---:|---:|---:|---:|
| A64 | `0x6BCBF0` | `0x6BCC58` | `0x6BCC5C` | `0x6BCC84` | `0x6BCCC0` |
| A32 | `0x589A9E` | `0x589ABC` | `0x589AC2` | `0x588B02` | `0x588B34` |
| I64 | `0x1001120D0` | `0x100112100` | `0x100112104` | `0x10011212C` | `0x10011216C` |
| I32 | `0x10F8E0` | `0x10F900` | `0x10F90A` | `0x10F942` | `0x10F980` |

The source receiver remains owned across all child construction, property
setup, randomization, Array publication, optional oldest-child eviction, and
the optional worker. Its cleanup follows the worker edge. This matters under
re-entrant script dispatch: replacing or clearing the node's persistent
Variant during `count` must neither switch the receiver used by the numeric
getter nor destroy it before the spawn finishes.

The portable wrapper was therefore generalized to
`ScopedVariantObjectDispatch_guess`; the old child-Array-specific name remains
an alias for recovered callers. `motionPropGetCount(iTJSDispatch2 *)` and
`motionPropGetStringByNum(iTJSDispatch2 *, ...)` operate directly on the
retained receiver.

## Source selection and malformed source boundaries

For a nonzero signed source count, native computes:

```text
sourceIndex = trunc_toward_zero(random() * double(sourceCount))
selected = sourceList[sourceIndex] converted directly to ttstr
parts = split(selected, "/")
child.chara = parts[1]
child.play(parts[2])
```

There is no upper-bound clamp. In particular, a random provider returning
exactly `1.0` produces `sourceIndex == sourceCount`. Getter failure propagates
through the native cleanup path. The final conversion is specifically signed
`FCVTZS` / `VCVT.S32.F64`: NaN becomes zero, positive overflow/Infinity becomes
`INT32_MAX`, negative overflow/-Infinity becomes `INT32_MIN`, and in-range
finite values truncate toward zero. The complete four-endpoint evidence and
portable UB closure are recorded in
`motionplayer_particle_source_index_conversion_four_binary_2026-08-16.md`.

The shared `ttstr` splitter scans the complete separator string, preserves
empty segments, and always appends the final remainder. The spawn caller does
not special-case an empty selected string and does not test the vector size
before indexing elements 1 and 2. Inputs with fewer than two slashes therefore
reach native unchecked vector access/undefined behavior. The leading element
is deliberately unused; for `ignored/chara/motion`, the child receives
`chara` and `motion`.

### Zero source count

The equality-to-zero branch does not skip emission. It drains a positive
`emitCount`, calls the child worker, and branches back to the same
decrement/worker block forever:

```text
forever:
    do:
        --emitCount
    while emitCount > 0
    stepParticleChildren()
```

| target | decrement/worker/back-edge region |
|---|---:|
| A64 | `0x6BD6AC..0x6BD6C0` |
| A32 | `0x589AC6..0x589AD2` |
| I64 | `0x100112E70..0x100112E84` |
| I32 | `0x11077E..0x110792` |

This is an original malformed-data hang, not an inferred feature. A defensive
empty-list return would be observably different and is intentionally absent.

## Child construction and link publication

The child `Player` is constructed before its adaptor Variant is created. Its
root-owner and parent-emitter links are then stored, and only afterward is the
native Player wrapped for script-visible publication:

| target | child constructor | root/parent stores | adaptor construction |
|---|---:|---:|---:|
| A64 | `0x6BCD28` | `0x6BCD30` | `0x6BCD40` |
| A32 | `0x588B8E` | `0x588B96` | `0x588B9E` |
| I64 | `0x1001121D4` | `0x1001121DC` | `0x1001121E8` |
| I32 | `0x10F9E2` | `0x10F9EE..0x10F9F2` | `0x10F9FA` |

The spawn then propagates color/context/Z factor, assigns the source-list
chara, starts the source-list motion, and propagates the evaluated opacity to
the child root before position randomization. Stack-owned Variants and strings
retain their prefix cleanup ordering if any of those operations throws.

## Shared RNG order

Each implementation contains exactly 16 call sites to the shared
`Player_random_guess` helper within this function. The complete xref sets are:

| target | 16 call sites in address order |
|---|---|
| A64 | `0x6BCAB4`, `0x6BCB34`, `0x6BCBBC`, `0x6BCC64`, `0x6BCED0`, `0x6BCEDC`, `0x6BCEE8`, `0x6BCF64`, `0x6BCF70`, `0x6BCFA0`, `0x6BCFCC`, `0x6BCFD8`, `0x6BD088`, `0x6BD1E4`, `0x6BD39C`, `0x6BD4A8` |
| A32 | `0x588ADC`, `0x588C70`, `0x588C7A`, `0x588C84`, `0x588D50`, `0x588D82`, `0x588D8C`, `0x588DBA`, `0x588E22`, `0x588E2C`, `0x588F2C`, `0x5890C2`, `0x5892D6`, `0x58939C`, `0x5899AE`, `0x5899F4` |
| I64 | `0x100111FE4`, `0x100112058`, `0x10011210C`, `0x100112304`, `0x100112310`, `0x10011231C`, `0x100112398`, `0x1001123A4`, `0x1001123D8`, `0x100112414`, `0x100112700`, `0x10011270C`, `0x1001127BC`, `0x100112944`, `0x100112AFC`, `0x100112BD0` |
| I32 | `0x10F7C2`, `0x10F862`, `0x10F916`, `0x10FB0A`, `0x10FB18`, `0x10FB26`, `0x10FBE4`, `0x10FBF2`, `0x10FC2C`, `0x10FC72`, `0x10FFB6`, `0x10FFC4`, `0x110096`, `0x11021A`, `0x11046C`, `0x11056C` |

Optimized block order makes the address order non-semantic on some targets.
Fresh decompilation nevertheless recovers the same semantic execution order:

```text
trigger sampling
source selection
position distribution
speed interval
spread interval
particle-angle interval
zoom interval
```

Within the spawn portion, every min/max interpolation except the count trigger
samples only when its endpoints compare unequal. The count trigger samples
unconditionally when its flags gate is open. Signed zero endpoints compare
equal and therefore consume no spawn-interval RNG; NaN endpoints compare
unequal and do consume it.

## Position distributions

`particleType` selects the position distribution. It is independent from
`particleFlyDirection`, which selects the later velocity direction/decay mode.

### Box (`particleType == 2`)

Native samples X then Y and samples Z only for the 3D/tri-volume mode. Each
sample is centered and scaled to a width of 16. This consumes two or three RNG
values respectively.

### Sphere or disk (`particleType == 1`)

For 3D volume, the RNG order is azimuth-like angle, elevation-like angle, then
radius. The radius is:

```text
pow(r3 + 0.0, double(float(1.0 / 3.0))) * 16.0
```

It is not `cbrt(r3)`. A64 materializes exponent bits
`0x3FD5555560000000`, the exact double promotion of the single-precision
one-third constant. Keeping `pow` also preserves its domain behavior.

For non-3D mode, native samples angle first and radius second, using
`sqrt(radiusSample) * 16`. Other subtypes leave X/Y offsets at zero.

When Z is nonzero, its later transform uses `sqrt(det)` without applying
`abs` and without guarding a negative or non-finite determinant. X/Y are
transformed around the current clip origin by the node's accumulated 2x2
matrix.

## Speed, direction, spread, and coordinate modes

Speed interpolates evaluator outputs `[2]` and `[3]`. Direction behavior is:

- fly direction 2: aim along the sampled displacement and fit its speed to the
  child duration using the exponential-decay expression below;
- fly direction 1: point opposite the sampled offset by adding 180 degrees;
- other values: use the node matrix angle.

For fly direction 2, native reads the child's cached total frames and computes:

```text
duration = childTotalFrames / 60
if decay == 1:
    speed = distance / duration
else:
    speed = distance * log(decay) / (pow(decay, duration) - 1)
speed /= 60
```

There are no guards for zero/negative/non-finite duration, nonpositive decay,
zero denominator, or non-finite intermediates.

| target | mode gate | duration/decay==1/direct division | log/pow expression |
|---|---:|---:|---:|
| A64 | `0x6BD0CC` | `0x6BD168..0x6BD170` | `0x6BD1A0..0x6BD1BC` |
| A32 | `0x588F8A` | `0x58901C..0x58902E` | `0x589072..0x58908A` |
| I64 | `0x100112820` | `0x100112888..0x1001128C0` | `0x1001128F8..0x100112914` |
| I32 | `0x1100EC` | `0x110166..0x11017C` | `0x1101C2..0x1101E6` |

Spread interpolates evaluator output `[8]` before perturbing the direction.
As elsewhere, equal endpoints—including signed zero—skip the RNG call.

Coordinate mode 1 publishes `(tx + parentX, offZ + parentY, ty + parentZ)` and
uses X/Z velocity components. Mode 0 publishes `(tx, ty, offZ)` and uses X/Y
velocity components. An invalid coordinate mode skips these root-position and
base-velocity assignments but still executes the later flip, angle, zoom,
inheritance, damping, and publication work.

## Particle angle, zoom, and velocity inheritance

Particle angle samples `[4]..[5]` before zoom samples `[6]..[7]`. Parent flip
parity changes the angle sign. When angle inheritance is enabled, the parent's
direction is added and a horizontal flip can add pi radians.

The selected zoom is written to the child root scale. Fly-direction mode 2
skips the later velocity-zoom operation. Otherwise:

- apply mode 1 multiplies X/Y/Z velocity by zoom;
- apply mode 2 divides X/Y/Z velocity by zoom with no zero or finite check.

The unconditional division is visible at A64 `0x6BD4F0..0x6BD548`, A32
`0x5893F0..0x589424`, I64 `0x100112C00..0x100112C64`, and I32
`0x11059A..0x1105F6`.
Thus zero zoom naturally produces infinities and NaNs according to the input
components and IEEE-754 arithmetic.

Translation-velocity inheritance is a separate `particleInheritVelocity == 1`
mode. It adds each parent translation delta divided by Player delta time when
the delta time merely compares unequal to zero; it need not be positive. The
node acceleration ratio is then copied to the child's camera damping field.

## Publication, eviction, and worker lifetime

The constructed adaptor Variant is appended to the retained child Array. The
Array count is read again as a signed integer. If it exceeds the configured
maximum, index 0 is erased exactly once; the code does not loop until the count
fits.

| target | Array add | post-add count | erase index 0 | optional worker |
|---|---:|---:|---:|---:|
| A64 | `0x6BD5DC` | `0x6BD5F8` | `0x6BD640` | `0x6BD65C` |
| A32 | `0x5894B6` | `0x5894C0` | `0x5894FA` | `0x58950A` |
| I64 | `0x100112D18` | `0x100112D3C` | `0x100112D84` | `0x100112DA0` |
| I32 | `0x11069E` | `0x1106BA` | `0x1106F6` | `0x11070C` |

The worker runs only when the original `emitCount <= 1` boundary permits it.
The source-list receiver, selected-source string, split vector, child adaptor
Variant, and remaining spawn temporaries are still live through that worker
call and are destroyed afterward in native unwind order. The Web control flow
now keeps the worker inside that scope instead of jumping to a shared label
after the source owner has been destroyed.

## Source and regression coverage

Changed files:

- `cpp/plugins/motionplayer/MotionNode.h`
- `cpp/plugins/motionplayer/MotionNodeBridge.cpp`
- `cpp/plugins/motionplayer/MotionDispatch.h`
- `cpp/plugins/motionplayer/PlayerUpdateParticles.cpp`
- `cpp/plugins/motionplayer/Player.h`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

New regression coverage verifies:

- the source-list receiver survives re-entrant clearing of the persistent node
  Variant;
- a random result of exactly `1.0` is not clamped and reaches numeric index
  `sourceCount`;
- exception unwinding releases the independently retained receiver once;
- path element 0 is ignored while elements 1 and 2 supply child chara/motion;
- inverse zoom by zero preserves native IEEE-754 infinity/NaN results.

The zero-source-count permanent loop and undersized split-vector access are
documented but intentionally not executed in unit tests because they are a
hang and undefined behavior, respectively.
