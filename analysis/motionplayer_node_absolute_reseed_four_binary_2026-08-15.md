# MotionPlayer node absolute reseed four-binary reconstruction (2026-08-15)

## Scope and evidence

This vertical re-audits the full-reseek phase that absolutely rebuilds the two
timeline slots of every non-root `MotionNode`. Only the four current
`reference/binaries/` artifacts are authoritative; historical single
`libkrkr2.so` comments are not reused as proof. Unknown original identifiers
retain `_guess`.

The independently callable Player-first helper is now named
`Player_initializeNodeTimelineSlots_guess` in all four recovery databases:

| target | helper | modified-node caller | full-reseek caller |
|---|---:|---:|---:|
| Android arm64 | `0x6B388C` | `0x6B3DFC` | `0x6B6608` |
| Android armv7 | `0x5827D8` | `0x582B26` | `0x5842B6` |
| iOS arm64 | `0x10010A57C` | `0x10010A970` | `0x10010CBBC` |
| iOS armv7 | `0x107EE8` | `0x108324` | `0x10A480` |

Fresh code xrefs show exactly these two callers in every target. The guessed
prototype is `void(Player *player, MotionNode *node)`. The full-reseek caller
walks the real half-open non-root node range; the modified-node caller invokes
the same complete helper after clearing its dynamic `modified` flag.

## Relevant object layout

The helper itself reconfirms these source-level fields and target offsets:

| field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| parameter pointer | `node+8` | `node+4` | `node+8` | `node+4` |
| node type | `node+28` | `node+20` | `node+28` | `node+20` |
| frame-list Variant | `node+64` | `node+56` | `node+64` | `node+56` |
| slot 0 | `node+320` | `node+296` | `node+320` | `node+288` |
| slot 1 | `node+856` | `node+728` | `node+856` | `node+708` |
| active-slot index | `node+1392` | `node+1160` | `node+1392` | `node+1128` |
| dirty byte | `node+44` | `node+36` | `node+44` | `node+36` |
| force-visible integer | `node+1996` | `node+1716` | `node+2012` | `node+1680` |
| Player evaluation time | `player+456` | `player+288` | `player+344` | `player+228` |
| Player preview byte | `player+1092` | `player+744` | `player+980` | `player+680` |

Parameter records store the selection value at `parameter+40` on 64-bit and
`parameter+32` on 32-bit. The differing slot-1 offsets and complete node sizes
come from target Variant/ttstr/STL ABIs, not different algorithms.

## Selection target, retained owner and field/owner split

Landmarks in the helper are:

| landmark | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| snapshot selection target | `0x6B38E4` | `0x58280A` | `0x10010A5BC` | `0x107F18` |
| retain frame-list owner | `0x6B38E8` | `0x582810` | `0x10010A5CC` | `0x107F22` |
| dynamic signed count | `0x6B3948` | `0x58282E` | `0x10010A5FC` | `0x107F7C` |
| release frame-list owner | `0x6B3B84` | `0x5829C8` | `0x10010A7D0` | `0x10812E` |

The selection target is captured before the first dynamic frame-list access:

```text
selectionTime = node.parameterEntry != null
              ? node.parameterEntry->value
              : player.evaluationTime
```

Unlike tag/root/variable full-reseek scans, this helper does not reload the
Player field after a getter. A re-entrant `count` or frame-time getter cannot
change the target used by the remaining scan, the final exact-time action gate,
or either slot selection.

The helper then retains an independent owner for the frame-list dispatch. That
owner spans count, the complete scan, both slot rebuilds, optional source
refresh and action enqueue. There is an important split:

- count and scan use the retained local owner;
- both parse/merge pairs still receive `node.frameListVariant`, the persistent
  field, rather than the local owner;
- a re-entrant getter can clear the persistent field without destroying the
  dispatch, allowing the scan to finish but making the later parser observe
  the cleared field and fail;
- the independent owner is released only by the normal/exception tail.

The former portable code passed the persistent Variant by reference through
the scan and had no owner spanning the tail, erasing this boundary.

## Scan, wrapping clamp and malformed counts

All four targets implement the same scan:

```text
count = signed_integer(localFrameList["count"])
selected = 0
if count >= 1:
    index = 0
    loop:
        time = real(localFrameList[index]["time"])
        if selectionTime == time:
            selected = index
            break
        if selectionTime < time:
            selected = index - 1
            break
        index++
        if index >= count: break

upperBits = uint32(count) - 2
upper = signed32_with_same_bits(upperBits)
selected = signed_min(selected, upper)
```

The wrap/clamp instructions are Android arm64 `0x6B3A70..0x6B3A78`, Android
armv7 `0x5828E6..0x5828F6`, iOS arm64 `0x10010A6BC..0x10010A6C4`, and iOS
armv7 `0x10803A..0x108040`. Every subtraction is a 32-bit `SUB`/`SUBS` followed
by a signed comparison. It therefore wraps modulo `2^32`; a direct C++ signed
`count - 2` is undefined for `INT_MIN` and `INT_MIN+1`.

Key boundaries are:

| count/target case | selected pair |
|---|---|
| `INT_MIN` or `INT_MIN+1` | scan skipped; wrapped positive upper keeps `0,1` |
| `-1` | `-3,-2` |
| `0` | `-2,-1` |
| `1` | `-1,0` |
| target before first frame | `-1,0`, unless the upper clamp is lower |
| exact frame `k` | `k,k+1`, capped to the final valid pair |
| target beyond a normal tail | `count-2,count-1` |
| unordered/NaN target comparison | equality and less-than both fail, so the scan advances to the tail clamp |

There is no short-list or lower-index safety guard.

## Slot publication and exception order

The post-scan sequence is:

| operation | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| parse/merge slot 0 | `0x6B3A8C` / `0x6B3AA0` | `0x582900` / `0x58290E` | `0x10010A6DC` / `0x10010A6F0` | `0x10804C` / `0x10805A` |
| parse/merge slot 1 | `0x6B3AB8` / `0x6B3AC8` | `0x582922` / `0x58292C` | `0x10010A708` / `0x10010A718` | `0x108076` / `0x108084` |
| active/dirty publication | `0x6B3AD4..0x6B3AD8` | `0x582934..0x58293C` | `0x10010A71C..0x10010A724` | `0x10808C..0x108092` |

Both complete parse/merge pairs precede the active-slot reset and dirty store.
The two independent stores may be scheduled in different order by the target
compiler because nothing throwing lies between them, but neither is published
before slot 1 merge returns. Thus:

- a slot-0 parser/merger failure leaves slot 1, the prior active cursor and the
  prior dirty flags unchanged while preserving slot-0's already-written prefix;
- a slot-1 failure preserves the completed slot 0 and slot-1's written prefix,
  but still preserves the prior active cursor and dirty flags;
- there is no rollback or copy-swap transaction.

The earlier portable helper reset `activeSlotIndex` before parsing slot 0. That
was observably wrong on an exception and has been moved after both mergers.

## Source gate and exact action tail

After both slot rebuilds and active/dirty publication:

```text
mask = player.preview ? 6153 : 6145
if node.forceVisible || (mask & (1 << node.nodeType)):
    findSource(node)

if selectionTime == slot[0].time && (slot[0].mask & ACTION):
    enqueue onAction(node.label, slot[0].action)
```

Source refresh precedes the action gate. The selection comparison uses the old
snapshot captured at entry, not a live Player/parameter reload. All four
targets perform a direct variable shift and have no explicit node-type range
check. The prior portable guard `nodeType >= 0 && nodeType < 31` was invented
and has been removed from this helper and the freshly rechecked parameterized
source-refresh helper.

For valid shipped node types the expression is ordinary and common. Invalid
negative or oversized shift counts are C++ undefined behavior in the likely
source expression: arm64's variable shift masks to five bits, while armv7's
register-shift behavior can yield zero for counts outside its valid range. This
is a genuine malformed-input compiler/ISA boundary, not a portable sanitized
rule.

## Portable corrections and regression

`PlayerUpdateLayerEval.cpp` now:

- captures selection time before dynamic dispatch, as before;
- retains a frame-list Variant owner for the complete helper while deliberately
  routing parse/merge through the persistent node field;
- implements the clamp using explicit unsigned 32-bit subtraction and
  bit-preserving signed interpretation;
- publishes active cursor and dirty only after both slot merges;
- removes the non-native node-type range guard from the absolute and freshly
  verified parameterized source gates.

The new differential regression covers two cases:

1. a source advertises `count == INT_MIN`; the required slot indices are `0,1`
   and the source reads are `{0,0,1,1}`;
2. a `count == 3` callback changes Player time `0 -> 25`, clears the persistent
   node field and clears the caller owner. The snapshotted target/local owner
   still permit scan read `{0}`; parser 0 then sees the persistent Void field
   and throws. Its reset/index prefix survives, slot 1 stays untouched, the
   old active cursor/dirty flags survive, and the local source is destroyed on
   unwind.

## Recovery database updates and validation

All four recovery databases were renamed to the common semantic helper name,
retyped with the Player-first prototype, annotated at target/owner/count/wrap/
slot/commit/source/action/release landmarks, given four focused bookmarks, and
saved successfully.

Validation for this vertical:

- the complete Emscripten `motionplayer-dll.cpp` translation-unit syntax
  invocation passes with only the project's existing `_tss` warning;
- the subsequent three-step Web Debug incremental build recompiles
  `PlayerUpdateLayerEval.cpp`, rebuilds `libmotionplayer.a`, and links the final
  WebAssembly application successfully; diagnostics are limited to the
  existing `_tss`, pthread/memory-growth, JSPI and JavaScript-library warnings;
- targeted `git diff --check` passes for the source, test, plan and analysis
  files touched by this vertical. Git reports only the working tree's existing
  LF-to-CRLF conversion notices;
- this vertical adds no binary address to compiled source. Exact locations are
  confined to analysis documents and the four saved recovery databases.
