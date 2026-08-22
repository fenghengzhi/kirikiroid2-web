# MotionPlayer tag absolute reseek: four-binary recovery (2026-08-15)

## Scope and evidence rule

This note replaces the old Android-only reading of the first phase in the
full-reseek routine.  The function, its six callers, the tag loop, the event
gate, the relevant UTF-16 literals, and the common epilogue were freshly
checked in all four current reference binaries:

| target | full reseek | tag owner acquire | signed count | cursor commit | current-time commit | event gate/content |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x6B5AA8` | `0x6B5AE0` | `0x6B5B40` | `0x6B5C58` | `0x6B5CFC` | `0x6B5DDC` / `0x6B5E08` |
| Android armv7 | `0x583C8C` | `0x583CAA` | `0x583CCA` | `0x583D86` | `0x583DCE` | `0x583E46` / `0x583E5A` |
| iOS arm64 | `0x10010C3CC` | `0x10010C3F8` | `0x10010C428` | `0x10010C4F8` | `0x10010C554` | `0x10010C5EC` / `0x10010C60C` |
| iOS armv7 | `0x109DAC` | `0x109DD4` | `0x109E32` | `0x109EEA` | `0x109F3E` | `0x109FCA` / `0x109FE8` |

The less-than branch's body-side increment is at `0x6B5C10`, `0x583D4A`,
`0x10010C4A4`, and `0x109EB0`.  On iOS the separately visible loop increment
is at `0x10010C4E0` and `0x109ED4`; the Android loop back-edge expresses the
same second increment.

All four functions have six code xrefs, all from the corresponding
`Player_frameProgress_guess` core:

| target | frame-progress entry | six call sites |
|---|---:|---|
| Android arm64 | `0x6BE44C` | `0x6BE4C0`, `0x6BE540`, `0x6BE6FC`, `0x6BE798`, `0x6BE808`, `0x6BE868` |
| Android armv7 | `0x58A63A` | `0x58A69E`, `0x58A72C`, `0x58A7A6`, `0x58A86C`, `0x58A8AC`, `0x58A90A` |
| iOS arm64 | `0x100113B50` | `0x100113BBC`, `0x100113C3C`, `0x100113CC0`, `0x100113D70`, `0x100113DBC`, `0x100113E1C` |
| iOS armv7 | `0x111556` | `0x1115B8`, `0x111646`, `0x1116C4`, `0x111792`, `0x1117D2`, `0x11182A` |

Every call passes only the Player pointer.  The native source-level prototype
is therefore `void Player::reseekTimelineCursors()` rather than a member taking
a copied target-time argument.

## Player layout cross-check

The same fields appear in the same semantic order, with pointer width and
container ABI accounting for the different offsets:

| field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| live evaluation time | `+456` | `+288` | `+344` | `+228` |
| motion-completed byte | `+483` | `+315` | `+371` | `+255` |
| tag cursor | `+916` | `+636` | `+804` | `+572` |
| tag current time | `+920` | `+640` | `+808` | `+576` |
| tag next time | `+928` | `+648` | `+816` | `+584` |
| tag source Variant | `+1072` | `+732` | `+960` | `+668` |
| sync-active byte | `+1093` | `+745` | `+981` | `+681` |
| sync-waiting byte | `+1098` | `+750` | `+986` | `+686` |
| frame-tick time | `+1120` | `+776` | `+1008` | `+708` |

## Correct source-level algorithm

```text
tagFrames = owning copy of Player.tagSource
count = integer(tagFrames["count"])

if count >= 1:
    index = 0
    while index < count:
        frame = owning copy of tagFrames[index]
        frameTime = real(frame["time"])
        evaluationTime = Player.evaluationTime  // loaded after the getter

        if frameTime <= evaluationTime:
            if frameTime < evaluationTime:
                index += 2                      // body + loop increment
                continue
        else:
            --index
        break

    Player.tagCursor = min(index, count - 2)
    current = owning copy of tagFrames[Player.tagCursor]
    Player.tagCurrentTime = double(integer(current["time"]))
    next = owning copy of tagFrames[Player.tagCursor + 1]
    Player.tagNextTime = double(integer(next["time"]))

    if Player.tagCurrentTime == Player.evaluationTime
       and integer(current["type"]) == 1:
        content = owning copy of current["content"]

        if Player.syncActive:
            if Player.tagCurrentTime == Player.evaluationTime
               and bool(content["align"]):
                Player.motionCompleted = true
                Player.evaluationTime = Player.tagCurrentTime
                Player.frameTickTime = Player.tagCurrentTime

            if Player.syncActive and bool(content["sync"]):
                Player.syncWaiting = true
                Player.evaluationTime = Player.tagCurrentTime
                Player.frameTickTime = Player.tagCurrentTime
                enqueue onSync

        action = string(content["action"])
        if action is nonempty:
            enqueue onAction(void, action)
```

The full-reseek function then constructs the priority owner, current root
frame, and next root frame; reseeds variable tracks and node slots; restores
and prunes join snapshots; and rebuilds every HM1 entry.  The tag owner is not
destroyed at the end of the tag phase.

## Live Player time, not an argument snapshot

The four decompilations load the Player evaluation-time double after each
dynamic frame-time getter.  The outer event equality and the align equality
load the Player field again.  The root scan and the inlined variable-track scan
do the same.  Consequently, a script getter can re-enter Player and replace the
evaluation time that controls the next comparison.

The previous port passed `_clampedEvalTime` into the full reseek and into the
variable-track reseed helper.  That captured a stale value before any getter
could re-enter.  The recovered member is now parameterless; tag/root
comparisons and each variable-track iteration reload the live field at the
native observation point.  The node initializer already read Player state
directly.

## Signed-count and scan boundaries

- `count` is an ordinary named `PropGet("count")` followed by Variant integer
  conversion.  It is not `GetCount()` and its HRESULT is ignored.
- Any `count < 1`, including a negative dynamic value, skips the complete tag
  phase.  Cursor, cached times, flags, and events are not committed.  This is
  deliberately different from the root phase, where a negative nonzero count
  commits scan cursor zero before the common clamp.
- `count == 1` enters the phase, clamps the cursor to `-1`, and then performs
  numeric reads at `-1` and `0`.  `count == 2` clamps the upper end to zero.
  There is no lower-bound repair.
- A target before the first frame decrements the cursor to `-1`.  A target past
  the end ultimately clamps to `count-2`.
- A strict less-than result increments once in the loop body and again in the
  loop update.  On a sorted five-frame stream at times `0,10,20,30,40`, target
  `25` reads scan frames `0,2,4`, backs up from `4` to `3`, and selects time
  `30`, not time `20`.
- Exact odd-index frames can be skipped by the coarse walk.  They may still be
  selected when the next visited even-index frame is greater and the cursor is
  decremented.
- An unordered comparison, including NaN, takes the greater/unordered arm,
  decrements once, and stops.  No NaN or signed-zero normalization occurs.

## Real scan versus integer cache

The scan reads `time` as real, but both committed cache values use the integer
getter and are then converted back to `double`.  Event equality is tested
against the truncated cached value.  For example, an exact real scan hit at
`1.75` can select the type-1 frame, cache `1.0`, fail the `1.0 == 1.75` gate,
and never fetch `content` or `action`.

This is not a decompiler cast artifact: the four targets call the integer
named-property helper for both cached stores and the real helper inside the
scan.

## Event order and partial-state behavior

The observable order is:

1. commit cursor;
2. acquire current frame and commit integer-converted current time;
3. acquire next frame and commit integer-converted next time;
4. compare current time with the current live evaluation time;
5. fetch `type`; if it is one, fetch `content`;
6. if sync-active, test align first;
7. re-read sync-active, test sync, commit sync state, and enqueue `onSync`;
8. fetch action regardless of sync-active and enqueue nonempty `onAction`.

There is no rollback.  A failing getter preserves all cursor/time/flag/event
prefixes already committed.  The align path sets completion and snaps both time
fields before the sync getter.  The sync path enqueues before action, so a frame
with both properties produces event types `{1, 0}`.

The second sync-active read is significant: a re-entrant align getter can
disable sync before the sync property is fetched.  Conversely, action remains
outside the sync-active gate.

## Owner lifetime and reverse destruction

Fresh epilogue inspection proves four function-scope owners survive through
variable reseed, node reseed, join restoration, and HM1 rebuild:

```text
construct: tag source -> priority source -> current root frame -> next root frame
destroy:   next root frame -> current root frame -> priority source -> tag source
```

| target | next root release | current root release | priority release | tag release |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6B664C` | `0x6B6664` | `0x6B6680` | `0x6B6698` |
| Android armv7 | `0x58431C` | `0x584332` | `0x584348` | `0x58435E` |
| iOS arm64 | `0x10010CC18` | `0x10010CC30` | `0x10010CC54` | `0x10010CC6C` |
| iOS armv7 | `0x10A4C8` | `0x10A4DA` | `0x10A4EC` | `0x10A4FE` |

Tag-phase temporaries have narrower scope.  After action-string destruction,
the references release content, next tag frame, and current tag frame before
priority acquisition:

| target | action string | content | next tag frame | current tag frame |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6B5F48` | `0x6B5F64` | `0x6B5F80` | `0x6B5F98` |
| Android armv7 | `0x583F1E` | `0x583F38` | `0x583F50` | `0x583F68` |
| iOS arm64 | `0x10010C70C` | `0x10010C730` | `0x10010C754` | `0x10010C76C` |
| iOS armv7 | `0x10A0D2` | `0x10A0E6` | `0x10A0F8` | `0x10A10A` |

Thus clearing the persistent tag field inside `count` does not invalidate the
scan, and clearing the last external tag owner from the later priority getter
still does not destroy the tag object until the full-reseek epilogue.

## iOS string-label correction

IDA had rendered several iOS UTF-16 literals as one-character strings because
the data items had the wrong string presentation.  Direct bytes disambiguate
them:

- iOS arm64 `0x10195B272`: `74 00 79 00 70 00 65 00 00 00` = `type`;
- iOS arm64 `0x10195B2AE`: `74 00 69 00 6D 00 65 00 00 00` = `time`;
- iOS arm64 `0x10195B2B8`: UTF-16 `content`;
- iOS arm64 `0x10195C626` / `0x10195C632`: UTF-16 `align` / `sync`;
- iOS armv7 equivalents are `0x174D5D6`, `0x174D612`, `0x174D61C`,
  `0x174E98A`, and `0x174E996`.

The iOS IDBs now use distinct `_reseek_utf16_guess` labels for these values.
The native code never requests one-letter properties `t`, `a`, or `s` here.

## Port corrections and regression coverage

The source now:

- gives the full-reseek member its one-argument-at-ABI (`this` only)
  source-level shape;
- retains tag, priority, current-root, and next-root Variants through the common
  tail in native construction/destruction order;
- reloads live Player evaluation time after dynamic time getters in tag, root,
  and variable-track absolute scans;
- preserves the coarse double increment, signed count gate, integer cache, and
  sync-before-action event order with semantic names rather than decompiler
  temporaries.

The focused regressions cover the `25 -> cursor 3` coarse-scan case, fractional
time truncation suppressing a type-1 action, align+sync+action event order,
`count < 1` state preservation, root negative-count behavior, and re-entrant
source clearing.  The strongest lifetime case clears the Player tag owner in
tag `count`, clears the last external tag owner in priority `count`, confirms
the tag dispatch is still alive during root, and observes its destruction only
at the full-reseek epilogue.

## IDB updates

All four entries are named `Player_reseekTimelineCursors_guess` and have the
guessed prototype `void __fastcall ...(void *self)`.  The tag source, signed
count, double increment, cursor/cache commits, event gate, content phase, and
four-release epilogue carry line comments.  Each database has bookmarks for the
local-owner/live-time boundary, intentional double increment, truncated-time
event gate, and reverse owner tail.  All four recovery databases were saved
after the changes.

## Validation

- The complete `motionplayer-dll.cpp` unit-test translation unit passes the
  Emscripten syntax-only invocation after the re-entrant owner, signed-negative
  count, fractional-time, and event-order regressions were added.  The only
  diagnostic is the repository's existing `_tss` warning.
- A full `cmake --build --preset "Web Debug Build"` rebuilt 33 targets and
  linked the final WebAssembly application.  After the final semantic-name and
  regression cleanup, the incremental three-target rebuild and final link also
  passed.
- Build diagnostics remain the existing `_tss`, imagepacker `nodiscard`,
  pthread/memory-growth, JSPI, and JavaScript-library warnings; this vertical
  adds no warning or error.
