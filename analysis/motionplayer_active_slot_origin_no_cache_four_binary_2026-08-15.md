# MotionPlayer active-slot snapshot and origin ownership — four-reference audit (2026-08-15)

## Scope and result

This audit follows the `ox`/`oy` values used by the vertex-computation and
type-4 particle-system passes. It was triggered by an old portable
`MotionNode::clipOriginX/Y` pair that the vertex pass filled from the selected
clip slot and later consumers treated as persistent node state.

Fresh inspection of all four current reference binaries proves that this pair
does not exist in the native source structure:

- vertex computation reads `source.originX/Y` and the selected slot's `ox/oy`
  directly at each origin calculation;
- its force-visible mirror reads the same selected slot directly;
- particle spawn reads the selected slot's `ox/oy` directly when transforming
  the randomized offset;
- no vertex instruction stores slot `ox/oy` into a second node-level double
  pair, and the MotionNode constructor/common initializer has no such
  independently consumed state.

The portable cache was therefore removed. This is not merely comment cleanup:
the old cache could remain stale when the vertex dirty gate skipped a node,
whereas native particle spawn still observes the selected slot.

The same inspection found a related re-entrancy boundary. The particle pass
loads `activeSlotIndex` once at node entry, before retaining the child Array or
calling its script-visible `count` property, and uses that saved selection for
the entire node pass. The portable implementation now holds a reference to
that exact slot across all later callbacks.

## Native node/slot layouts

These offsets describe the four ABIs only and are not portable member
declarations.

| field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| active slot index | `+1392` | `+1160` | `+1392` | `+1128` |
| slot stride | `536` | `432` | `536` | `420` |
| slot `ox` | `slot+376` | `slot+344` | `slot+376` | `slot+328` |
| slot `oy` | `slot+384` | `slot+352` | `slot+384` | `slot+336` |
| source origin X | `node+248` | `node+224` | `node+248` | `node+220` |
| source origin Y | `node+256` | `node+232` | `node+256` | `node+228` |

The differing source-origin offsets and slot layouts are further evidence that
the old `// node+248 local` annotation could not identify one common portable
field. On 64-bit targets `node+248/+256` are the source descriptor's own
origin, not an extra copy of slot `ox/oy`.

## Vertex-computation reads and absence of cache stores

The vertex pass functions are:

| target | function |
|---|---:|
| Android arm64 | `0x6B98D0` |
| Android armv7 | `0x5866F8` |
| iOS arm64 | `0x10010F6AC` |
| iOS armv7 | `0x10CE30` |

The own-affine/source-quad origin calculation reads both owners directly:

| target | source-origin read | selected-slot `ox/oy` read |
|---|---:|---:|
| A64 | `0x6B9F1C` | `0x6B9F20` (`LDP ... [slot,#0x178]`) |
| A32 | `0x586BE0..0x586BEA` | `0x586BC8..0x586BCC` |
| I64 | `0x10010FC08..0x10010FC10` | same expressions read `slot+376/+384` |
| I32 | `0x10D072..0x10D076` | same expressions read `slot+328/+336` |

The source-level expression is:

```text
totalOriginX = node.source.originX + selectedSlot.ox
totalOriginY = node.source.originY + selectedSlot.oy
```

Force-visible property publication repeats direct selected-slot reads at A64
`0x6BA994..0x6BA9CC`, A32 `0x587324..0x58734E`, I64
`0x10011043C..0x100110474`, and I32 `0x10D8AE..0x10D8EE`.

The complete A64 vertex range `0x6B98D0..0x6BACBC` was additionally searched
for stores using the relevant `#0x178/#0x180` slot offsets and
`#0xF8/#0x100` source offsets. It contains the expected loads and no matching
store. The three other fresh decompilations likewise carry the slot values in
locals and never publish a second node pair.

Consequently a node that fails the vertex dirty gate performs no origin copy,
because native has no copy to perform. Later phases still read the current
selected slot according to their own entry-time selection rules.

## Particle pass selection snapshot and origin reads

The type-4 pass loads the active index before its first re-entrant child-Array
operation:

| target | active-index snapshot | Array retain begins | first count read |
|---|---:|---:|---:|
| A64 | `0x6BC67C` | immediately afterward | before existing-child traversal |
| A32 | `0x5895B0` | `0x5895B6` | `0x5895D2` |
| I64 | `0x100111DC4` | `0x100111DD4` | `0x100111DFC` |
| I32 | `0x10F5D2` | `0x10F5E6` | `0x10F60A` |

All subsequent slot-done, trigger, and spawn-origin reads derive from that
saved index. A re-entrant Array `count` getter can change the node's live
`activeSlotIndex`, but the in-flight pass continues using its original slot.

At spawn time, origin reads are:

| target | selected-slot origin read |
|---|---:|
| A64 | `0x6BD030..0x6BD038` (`slot+376/+384`) |
| A32 | `0x588E92..0x588E9C` (`slot+344/+352`) |
| I64 | `0x100112748..0x100112758` (`slot+376/+384`) |
| I32 | `0x110026..0x110030` (`slot+328/+336`) |

The sampled offset is then transformed as:

```text
tx = m11 * (offX - slot.ox) + m12 * (offY - slot.oy)
ty = m21 * (offX - slot.ox) + m22 * (offY - slot.oy)
```

There is no intervening read from a node-level cached origin.

## Source changes and regression coverage

Changed files:

- `cpp/plugins/motionplayer/MotionNode.h`: removed the synthetic
  `clipOriginX/Y` fields;
- `cpp/plugins/motionplayer/PlayerUpdateGeometry.cpp`: removed the propagation
  stores and reads the selected slot directly for source origin and diagnostic
  projection;
- `cpp/plugins/motionplayer/PlayerUpdateParticles.cpp`: snapshots one slot at
  node entry and uses its direct `ox/oy` values;
- `tests/unit-tests/plugins/motionplayer-dll.cpp`: verifies a spawn invoked
  without a preceding vertex pass still subtracts nonzero slot origin, and a
  re-entrant Array count callback changing `activeSlotIndex` does not redirect
  the in-flight particle pass.

The re-entrancy test uses an empty retained Array whose first `count` callback
switches from live slot 0 to completed slot 1. Native-shaped behavior still
executes slot 0's count trigger once and performs the worker's second count
read. Re-reading `activeSlot()` after the callback would incorrectly take the
completed-slot early exit.
