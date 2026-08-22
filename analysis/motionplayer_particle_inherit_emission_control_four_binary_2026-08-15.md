# MotionPlayer type-4 particle inheritance and emission control — four-reference audit (2026-08-15)

## Scope and result

This note covers the front half of the type-4 particle-system update only:

1. retained child-Array acquisition and count read;
2. existing-child transform/velocity inheritance;
3. transform snapshot ordering and exception boundaries;
4. activity and persistent emitter-latch control;
5. frequency/count trigger selection and timer arithmetic.

Particle child construction, randomized launch vectors, maximum-count eviction,
and the out-of-line two-pass particle worker remain separate recovery phases.

Fresh decompilation of all four current reference binaries found two observable
differences in the Web port and one stale interface assumption:

- the native pass has only a `Player *this` input and consumes the retained
  Player delta-time field; it has no second `currentTime` argument;
- on a changed inherited transform, native commits `prevM11/prevM21/prevM12/
  prevM22` and `prevParticleAngle` before testing whether the child count is
  positive and before unwrapping the first Array element;
- only accumulated inactivity clears the persistent particle-emitter active
  latch. A completed slot and the trigger-0/fmin-zero shortcut leave the prior
  latch unchanged.

The source and tests now preserve those boundaries.

## Function and caller map

The stripped name remains `Player_updateParticleSystems_guess`.

| target | function | size | sole call site in Player update |
|---|---:|---:|---:|
| Android arm64 | `0x6BC4BC` | `0x144C` | `0x6B9080` |
| Android arm32 | `0x588A48` | `0x108C` | `0x586020` |
| iOS arm64 | `0x100111D08` | `0x1184` | `0x10010EFC4` |
| iOS arm32 | `0x10F51C` | `0x1278` | `0x10C846` |

All four decompilers recover a single Player/self parameter. Each function
loads the speed-scaled per-frame delta retained by the surrounding Player
progress path. The Web declaration/call was therefore changed from
`updateLayersPhase3_ParticleSystem(double currentTime)` to
`updateLayersPhase3_ParticleSystem()`.

## Cross-target field map

Offsets are evidence for the native layouts, not offsets used by the portable
C++ structure.

| semantic field | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| node flags byte | `+44` | `+36` | `+44` | `+36` |
| active slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| particle active latch | `+2160` | `+1840` | `+2176` | `+1804` |
| particle subtype | `+2164` | `+1844` | `+2180` | `+1808` |
| maximum child count | `+2168` | `+1848` | `+2184` | `+1812` |
| inherit angle | `+2172` | `+1852` | `+2188` | `+1816` |
| inherit velocity mode | `+2176` | `+1856` | `+2192` | `+1820` |
| fly direction | `+2180` | `+1860` | `+2196` | `+1824` |
| apply zoom to velocity | `+2184` | `+1864` | `+2200` | `+1828` |
| delete outside | `+2188` | `+1868` | `+2204` | `+1832` |
| triangular/3D volume | `+2189` | `+1869` | `+2205` | `+1833` |
| acceleration ratio | `+2192` | `+1872` | `+2208` | `+1836` |
| particle motion-list Variant | `+2200` | `+1880` | `+2216` | `+1844` |
| nine evaluator outputs | `+2224..+2288` | `+1896..+1960` | `+2240..+2304` | `+1860..+1924` |
| retained child Array Variant | `+2296` | `+1968` | `+2312` | `+1932` |
| previous 2x2 matrix | `+2320..+2344` | `+1984..+2008` | `+2336..+2360` | `+1948..+1972` |
| previous particle angle | `+2352` | `+2016` | `+2368` | `+1980` |
| frequency timer | `+2360` | `+2024` | `+2376` | `+1988` |

The different I64/I32 offsets are real target-layout differences. A single
address-derived comment such as “node+2224” is consequently not portable and
was removed from `MotionNode.h`.

## Existing-child phase

The retained Array dispatch and its script-visible `count` are acquired before
the inherit-velocity gate. Existing children are processed before any node
activity or emission test, so already-created particles keep following their
parent even when the system has become inactive or its active slot is done.

The common control flow is:

```text
retain Array dispatch
childCount = Array.count

if inheritVelocity == 2:
    if !activeSlot.done && inheritAngle:
        compare current 2x2 matrix with the previous snapshot
        if any component differs:
            old = previous matrix
            previous matrix = current matrix
            signedAngleDelta = current angle - previous angle
            flip sign iff flipX != flipY
            previous angle = current angle

            if childCount >= 1:
                transform every child with inv(old) * current
                rotate the selected velocity pair with the same matrix
        else:
            add parent translation delta to every child
    else:
        add parent translation delta to every child
```

`inheritVelocity != 2` suppresses both the full transform and the translation-
only path. The full transform has no zero-determinant guard. The reciprocal and
matrix products are reached only after the positive-count test.

The four targets preserve the existing coordinate-mode split. Mode 1 operates
on X/Z and translates Y. The other mode uses the plugin's unusual 2D root
mapping/X-Z swap and operates on X/Y velocities. This phase did not rewrite
those already-recovered formulas.

## Snapshot prefix and exception boundary

The decisive ordering is visible independently on every target:

| target | inherit gate | previous-matrix stores | previous-angle store | positive-count test |
|---|---:|---:|---:|---:|
| A64 | `0x6BC6F4` | `0x6BC7D8`, `0x6BC7E0` | `0x6BC81C` | `0x6BC820` |
| A32 | `0x5895E0` | `0x5896EE..0x5896F8` | `0x589752` | `0x589756` vicinity |
| I64 | `0x100111E20` | `0x100112450`, `0x100112458` | `0x100112484` | `0x1001124A0` |
| I32 | `0x10F61A` | `0x10FCC8..0x10FCCC` | `0x10FD06` | `0x10FD26` |

Consequences:

- an empty or negative Array count still advances all five snapshots;
- determinant calculation and child access do not occur for that count;
- if `PropGetByNum` succeeds with a non-object Variant, or otherwise causes the
  strict Player unwrapping path to throw, the snapshots remain committed;
- the following frame compares against the newly committed matrix rather than
  retrying the failed transform from the old snapshot.

The old Web block put `childCount >= 1` around the snapshot work and delayed the
matrix stores until after the whole child loop. It therefore skipped empty-
Array commits and rolled back the effective state when an element conversion
threw. The updated source commits first and restricts only determinant/child
work to positive counts.

## Activity and persistent latch

After the existing-child phase:

1. `!accumulated.active` writes the persistent latch to false and jumps to the
   particle worker;
2. `activeSlot.done` jumps without modifying the latch;
3. trigger 0 with evaluator `fmin == 0.0` jumps before the latch write;
4. otherwise the old latch is captured, the latch becomes true, and trigger
   processing begins.

The `fmin == 0.0` comparison treats both signed zeros as equal. NaN does not
take that shortcut. The evaluator values come from the node's nine-double
type-4 output mirror. The active slot supplies `prtTrigger` directly. There is
no separate persistent trigger mirror. The type-6 emitter pass is unrelated
and does not consume `particleInterp`.

## Frequency trigger and IEEE-754 behavior

On first activation, trigger 0 interpolates between the periods `60/fmin` and
`60/fmax`; equal periods do not consume the shared ResourceManager RNG. The
retained timer is then reduced by Player delta time. While the timer is ordered
less than or equal to zero, another sampled period is added and `emitCount` is
incremented.

The comparison is intentionally ordered. For example, I32 executes:

- `VCMPE.F64 D16, #0.0` at `0x10F7F4`;
- `VMRS APSR_nzcv, FPSCR` at `0x10F7F8`;
- `BLS` at `0x10F800` to enter the period-add loop;
- and repeats with `VCMPE`/`BLS` at `0x10F886..0x10F892`.

The unordered flags from NaN do not satisfy `BLS`, so a NaN timer skips the
loop with `emitCount == 0`. The C++ `while (timer <= 0.0)` retains that behavior.
The loop may remain non-terminating for other invalid frequency combinations;
the references have no normalization or iteration cap, so the port must not
invent one.

After the loop, only frequency mode applies the ordered `fmin > 0.0` cap:

```text
timer = min(60 / fmin, timer)
```

The comparison/select form chooses the existing timer when the proposed cap is
unordered. Count mode has no corresponding cap.

## Count trigger and narrowing

Trigger 1 samples whenever the complete node flags byte is nonzero. Unlike the
frequency interpolation helper, it consumes RNG even when `fmin == fmax`, then
converts `fmin + (fmax - fmin) * random` from double to signed int:

| target | conversion |
|---|---:|
| A64 | `FCVTZS W20, D8` at `0x6BCB44` |
| A32 | `VCVT.S32.F64 S0, D10` at `0x589A08` |
| I64 | `FCVTZS W26, D8` at `0x100111FF4` |
| I32 | `VCVT.S32.F64 S0, D10` at `0x10F7D8` |

These instructions truncate finite in-range values toward zero and share the
same signed-int32 invalid/overflow profile already recovered at the parameter
normalizer: NaN -> 0, positive overflow -> `INT32_MAX`, negative overflow ->
`INT32_MIN`. The corresponding portable C++ `static_cast<int>` is outside the
well-defined range, so the port now uses an explicit conversion helper. Exact
boundary tests and refreshed four-IDB annotations are recorded in
`motionplayer_particle_count_trigger_conversion_four_binary_2026-08-16.md`.

## Source and regression coverage

Changed files:

- `cpp/plugins/motionplayer/Player.h`
- `cpp/plugins/motionplayer/PlayerUpdateLayers.cpp`
- `cpp/plugins/motionplayer/PlayerUpdateParticles.cpp`
- `cpp/plugins/motionplayer/MotionNode.h`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

New regression cases cover:

- all five snapshots committing for an empty Array;
- the same snapshot prefix surviving strict conversion failure on Array[0];
- inactive state clearing the persistent latch;
- a completed active slot preserving it;
- trigger 0 with zero minimum frequency preserving it.

The malformed-Array test clears the node's type-4 ownership before Player
destruction. That cleanup is necessary because the native-shaped Player
destructor/visitor also strictly unwraps type-4 children and would otherwise
encounter the deliberately invalid integer element a second time.
