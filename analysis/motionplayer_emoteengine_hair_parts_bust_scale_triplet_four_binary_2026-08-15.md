# EmoteEngine hair/parts/bust scale triplet in four references

Date: 2026-08-15

## Correction and source-level result

The old `PlayerCore.cpp` note identified Android arm64 `0x681F20/28/30` as
EmotePlayer scale accessors. That identity is invalid in the current reference:
`0x681F20` is inside `EmoteHairPartsDeque_initializeMap_guess`, and
`0x681F30` begins its element-block allocator helper.

Fresh registrar-to-target tracing in all four binaries instead recovers six
tiny accessors that directly read or write three consecutive `double` members
of `EmoteEngine`:

```cpp
double hairScale;
double partsScale;
double bustScale;
```

`Motion.EmotePlayer` owns an Engine-sized native payload directly, so its NCB
accessors receive that payload as `this`. `Motion.Player` has no corresponding
triplet or member names. `D3DEmotePlayer` exposes the same values through the
longer shell -> primary EmoteObject -> EmoteEngine chain.

## Field matrix and construction

| Field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `hairScale` | `+1184` | `+616` | `+816` | `+428` |
| `partsScale` | `+1192` | `+624` | `+824` | `+436` |
| `bustScale` | `+1200` | `+632` | `+832` | `+444` |

All four Engine constructors initialize the complete triplet to exact `1.0`.
The representative commits are:

- Android arm64: bust at `0x67BB30`, then a paired hair/parts store at
  `0x67BB40`;
- Android armv7: bust/hair/parts at `0x560B84/0x560B90/0x560B94`;
- iOS arm64: hair/parts/bust at
  `0x1001B8144/0x1001B8148/0x1001B814C`;
- iOS armv7: the low/high word pairs ending at
  `0x1B799C/0x1B79A6/0x1B79B0`.

The preceding Engine fields are `metadataScale` and
`inverseCombinedScale`. Physical adjacency does not merge their semantics:
none of the six public triplet accessors reads or writes the metadata pair.

## Motion.EmotePlayer accessor map

The setter order is hair, parts, bust because members #39-41 are methods
`setHairScale`, `setPartsScale`, `setBustScale`. Property publication is hair,
bust, parts at members #42-44, but each property setter reuses the earlier
method target.

| Accessor | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| set hair | `0x67F300` | `0x5620AC` | `0x1001B619C` | `0x1B5F84` |
| set parts | `0x67F308` | `0x5620B6` | `0x1001B61A4` | `0x1B5F8E` |
| set bust | `0x67F310` | `0x5620C0` | `0x1001B61AC` | `0x1B5F98` |
| get hair | `0x67F318` | `0x5620CA` | `0x1001B61B4` | `0x1B5FA2` |
| get bust | `0x67F320` | `0x5620D4` | `0x1001B61BC` | `0x1B5FAC` |
| get parts | `0x67F328` | `0x5620DE` | `0x1001B61C4` | `0x1B5FB6` |

Registrar coordinates:

| Surface | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| methods #39-41 | `0x67DDE8/0x67DE08/0x67DE28` | `0x5616D4/0x5616EC/0x561704` | `0x1001B56E4/0x1001B5708/0x1001B572C` | `0x1B5320/0x1B5340/0x1B5360` |
| properties #42-44 | `0x67DEB0/0x67DF20/0x67DF90` | `0x56171E/0x561736/0x56174C` | `0x1001B5754/0x1001B577C/0x1001B57A4` | `0x1B5382/0x1B53A4/0x1B53C6` |

Every setter is a single raw `double` store and every getter is a single raw
load, modulo 32-bit ABI register moves. There is no equality check, clamp,
absolute value, finite check, Engine dirty write, controller enqueue, Player
lookup, or metadata-pair update. Signed zero, NaN and infinities round-trip as
their stored IEEE bit patterns.

## Physics readers and gating

The Engine progress tail runs these readers only when the original frame delta
is not equal to zero and `directEdit` is false. The incoming double is narrowed
once to float for the physics pass. Setter calls themselves do not trigger
physics and do not mark any state dirty.

After stepping the bust/hair/parts outer-force controllers, the tail consumes
the triplet asymmetrically:

1. `EmoteEngine_stepHairPartsSpring_guess` loads `bustScale` for each spring
   step, including every `<= 1.1f` substep.
2. The first `EmoteEngine_stepBust` call pairs the hair outer-force controller,
   first chain deque and `hairScale`.
3. The second call pairs the parts outer-force controller, second chain deque
   and `partsScale`.

Reader coordinates:

| Reader | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| hair/parts loads in progress | `0x67A840/0x67A858` | `0x55FFDC/0x55FFF6` | `0x1001B4414/0x1001B442C` | `0x1B3EFE/0x1B3F1E` |
| bust loads in simple spring | `0x678C00/0x678C94` | `0x55EF32/0x55EFD4` | `0x1001B2B00/0x1001B2B94` | `0x1B25D8/0x1B268E` |

Each double is passed to the spring/chain helper at its documented conversion
boundary. No scale-specific validity guard is introduced before the physics
helper; subsequent float narrowing and arithmetic own the observable handling
of unusual IEEE values.

## D3D receiver chain

The D3D getter bodies make the extra ownership hop explicit. For example,
`getHairScale` is at `0x5304D0 / 0x494A6E / 0x100232EA8 / 0x231AFA` and performs:

```text
D3D native shell
  -> primary EmoteObject slot
  -> EmoteObject Engine owner
  -> Engine hairScale
```

The other D3D triplet getters/setters are consecutive functions and use the
same chain with the next two Engine offsets. They do not use shell-local or
EmoteObject-local shadow doubles. A missing primary object or Engine pointer is
not guarded in these tiny callbacks; the normal script lifecycle is expected
to establish the chain first.

## Port consequence

The portable layout keeps the triplet solely on `EmoteEngine`.
`Motion.EmotePlayer` forwards both its methods and properties to those fields;
`D3DEmotePlayer` forwards through `EmoteObject`; and `Motion.Player` defines no
parallel state. Regression coverage now checks constructor defaults, raw
signed-zero/NaN/infinity storage, method/property aliasing, metadata-pair
isolation and the absence of an implicit dirty write.
