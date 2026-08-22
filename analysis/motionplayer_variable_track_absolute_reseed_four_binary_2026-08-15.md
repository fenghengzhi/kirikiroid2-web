# MotionPlayer variable-track absolute reseed four-binary reconstruction (2026-08-15)

## Scope and authority

This vertical re-audits the variable-track phase in the full reseek member
against the four current `reference/binaries/` artifacts. It does not inherit
the old single-`libkrkr2.so` comments as evidence. The authoritative targets
are Android arm64, Android armv7, iOS arm64 and iOS armv7 from MotionPlayer
1.3.9. Unknown source identifiers retain the `_guess` suffix.

The full member has the previously recovered `this`-only ABI and is named
`Player_reseekTimelineCursors_guess` in the recovery databases:

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| full reseek | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |
| slot step | `0x6B4C4C` | `0x583518` | `0x10010B604` | `0x108EDC` |
| slot merge | `0x6B4E50` | `0x583648` | `0x10010B76C` | `0x109090` |

Both slot helpers are out of line in all four targets. Their recovered guessed
prototypes are:

```cpp
void VariableTrackSlot_step_guess(
    void *slot, const void *frameSource, uint32_t index);
void VariableTrackSlot_merge_guess(
    void *slot, const void *frameSource);
```

## Container and element ABI

Fresh deque walks reconfirm the same source-level element layout on both STL
families:

| field | 64-bit offset | 32-bit offset |
|---|---:|---:|
| `ttstr cascadeKey` | `+0` | `+0` |
| `int activeSlotCursor` | `+8` | `+4` |
| `double value` | `+16` | `+8` |
| owning `Variant frameSource` | `+24` | `+16` |
| `VarTrackSlot slot[0]` | `+48` | `+32` |
| `VarTrackSlot slot[1]` | `+104` | `+80` |
| element size | `160` | `128` |

The Player anchors and deque policies are:

| target | Player deque object | object size | elements per block | fresh absolute-walk proof |
|---|---:|---:|---:|---|
| Android arm64 / libstdc++ | `+0x510` | `80` | `3` | `0x6B6368..0x6B6414`, stride `0xA0` |
| Android armv7 / libstdc++ | `+0x380` | `40` | `4` | iterator anchor `0x58410A`, element resolver `0x58414A` |
| iOS arm64 / libc++ | `+0x480` | `48` | `25` | `0x10010C9B8..0x10010C9F0`, stride `0xA0` |
| iOS armv7 / libc++ | `+0x32C` | `24` | `32` | `0x10A2EA..0x10A30C`, stride `0x80` |

Android arm64's block calculation divides the logical displacement by three;
iOS arm64 divides by `0x19`; iOS armv7 uses `index >> 5` and `index & 0x1f`.
These are STL deque block policies, not custom MotionPlayer ring-buffer code.

`VarTrackSlot` remains 56 bytes on 64-bit and 48 bytes on 32-bit. Its shared
offsets are `frameIndex +0`, `time +8`, `interval +16`, `typeZero +20`,
`interp +21`, `merged +22`, `value +24`, and owning `easing Variant +32`.

## Absolute per-track control flow

The exact phase landmarks are:

| landmark | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| copy `item.frameSource` to local owner | `0x6B6420` | `0x58415A` | `0x10010C9FC` | `0x10A30E` |
| dynamic signed `count` | `0x6B6470` | `0x584172` | `0x10010CA20` | `0x10A32E` |
| reload live Player time after getter | `0x6B6524` | `0x5841C2` | `0x10010CA88` | `0x10A390` |
| wrapping `count - 2` | `0x6B62F4` | `0x5841FE` | `0x10010CAE4` | `0x10A3DE` |
| step/merge slot 0 | `0x6B6310` / `0x6B631C` | `0x584216` / `0x58421E` | `0x10010CB08` / `0x10010CB14` | `0x10A3F0` / `0x10A3FC` |
| step/merge slot 1 | `0x6B6330` / `0x6B633C` | `0x58422C` / `0x584234` | `0x10010CB30` / `0x10010CB3C` | `0x10A414` / `0x10A420` |
| reset cursor to zero | `0x6B6340` | `0x58423A` | `0x10010CB48` | `0x10A428` |
| release local source owner | `0x6B635C` | `0x58424C` | `0x10010CB6C` | `0x10A43A` |

Common source-level pseudocode is:

```text
for each VariableLabelScope item:
    source = VariantCopy(item.frameSource)
    count = signed_integer(source["count"])

    index = 0
    if count >= 1:
        while index < count:
            frame = source[index]
            frameTime = real(frame["time"])
            eval = Player.liveEvaluationTime  // loaded after the getter
            if frameTime == eval: break
            if frameTime <= eval:
                index++
                continue
            index--
            break

    limitBits = uint32(count) - 2
    limit = signed32_with_same_bits(limitBits)
    seed = signed_min(index, limit)

    step(slot[0], source, uint32(seed))
    merge(slot[0], source)
    step(slot[1], source, uint32(seed) + 1)
    merge(slot[1], source)
    item.activeSlotCursor = 0
    destroy(source)
```

Unlike the tag stream, the variable scan advances by one on each less-than
frame. Equality stops on that frame. The first greater frame decrements once
and stops. An unordered comparison, including NaN, fails equality and
less-or-equal, so it follows the same decrement-and-stop route. There is no
lower clamp.

The evaluation time is not a function argument or a phase snapshot. Every
target reads the Player field only after `frame["time"]` returns. A re-entrant
time getter can therefore change which lower/upper pair this same scan selects.

## Signed count and modulo-32 boundary

`count` comes from ordinary named `PropGet("count")`, not a native array
`GetCount` operation. All four clamp sites perform one 32-bit `SUB`/`SUBS` and
then a signed compare/select. Consequently the subtraction wraps modulo
`2^32`; translating it as C++ `int countMinusTwo = count - 2` introduces signed
overflow undefined behavior for the two minimum values.

Important cases are:

| advertised signed count | wrapped signed `count-2` | scan index | selected seed | numeric reads made by step/merge |
|---:|---:|---:|---:|---|
| `INT_MIN` | `INT_MAX-1` | `0` | `0` | `0,0,1,1` |
| `INT_MIN+1` | `INT_MAX` | `0` | `0` | `0,0,1,1` |
| `-1` | `-3` | `0` | `-3` | `-3,-3,-2,-2` through the signed numeric ABI |
| `0` | `-2` | `0` | `-2` | `-2,-2,-1,-1` |
| `1` | `-1` | scan-dependent | at most `-1` | the selected pair |
| normal tail `N >= 2` | `N-2` | `N` | `N-2` | `N-2,N-2,N-1,N-1` |

The helper receives a `uint32_t` index. On the 32-bit ABIs the same register
bits flow into the signed numeric-property parameter; the table shows those
bits using their signed `tjs_int` interpretation. Slot 1's adjacent index is
also a native 32-bit `ADD`/`ADDS`.

The root-priority phase uses the same wrapping subtract before its signed min:
Android arm64 `0x6B6130`, Android armv7 `0x584050`, iOS arm64
`0x10010C894`, and iOS armv7 `0x10A1F6`. For `count == INT_MIN`, the earlier
negative-count branch has already committed scan cursor zero, so the wrapped
positive limit preserves zero and root reads indices `0`, then `1`.

## Ownership, write publication, and exceptions

Each deque element's persistent `frameSource` and the per-iteration source are
independent owning Variants. The local copy is constructed before `count`, is
passed to every scan/step/merge access, and is released after cursor reset. A
dynamic callback may clear the persistent field without invalidating the rest
of the iteration. Narrow frame/content temporaries unwind before this source
owner. The outer tag/priority/root owners described by the tag/full-reseek
vertical remain older and live still longer, through the common tail.

The out-of-line helpers reconfirm the following observable partial commits:

```text
step:
    slot.frameIndex = index
    frame = source[index]
    slot.time = real(frame["time"])
    slot.merged = false

merge:
    slot.merged = true
    frame = source[slot.frameIndex]
    type = int(frame["type"])
    if type == 0:
        slot.typeZero = true
        return
    slot.typeZero = false
    if type == 2: slot.interp = 0
    if type == 3: slot.interp = 1
    content = frame["content"]
    slot.interval = uint32(int(content["interval"]))
    slot.value = real(content["value"])
    slot.easing = frame["easing"]
```

There is no transaction. A failing slot-0 operation prevents slot 1 and cursor
reset; a failing slot-1 operation leaves the successful slot-0 writes intact;
a failure after `merged=true` retains that flag. Type zero deliberately leaves
the old interpolation byte, interval, value and easing owner untouched. The
source local still unwinds normally on every exception path.

## Portable-source corrections

`PlayerFrameProgress.cpp` now:

- copies `item.frameSource` into a per-iteration owning Variant and routes every
  dynamic access and helper call through that copy;
- retains the previously recovered live Player-time reload after each dynamic
  time getter;
- implements `count-2` by unsigned 32-bit subtraction plus bit-preserving
  signed interpretation, avoiding C++17 signed-overflow UB;
- uses the same helper for the root absolute clamp;
- forms the adjacent slot index with unsigned 32-bit addition;
- preserves slot-0 step/merge, slot-1 step/merge, cursor-zero, source-release
  order and every helper's partial-publication behavior.

The test-only Player projection does not add a script-visible member. It makes
the absolute variable phase observable without changing the production ABI.
Regression coverage includes a time getter that changes Player evaluation
`15 -> 25` (requiring the `20/30` pair), and a source advertising `INT_MIN`
that clears both persistent/external owners inside `count`. The required reads
are `{0,0,1,1}` and the source dispatch is destroyed only after both merges and
cursor reset. A `count == -1` companion locks the unsigned-slot/signed-dispatch
bit bridge with reads `{-3,-3,-2,-2}`.

## Recovery database updates and validation

All four recovery databases now carry refreshed helper prototypes, comments at
source-copy/count/live-time/clamp/slot/reset/release landmarks, four bookmarks
for the owner/live-time/wrap/release boundaries, and the matching root-wrap
comment. Every database was saved successfully.

Validation for this vertical:

- the complete `motionplayer-dll.cpp` Emscripten syntax invocation passes; the
  only diagnostic is the existing deprecated `_tss` literal warning;
- `cmake --build --preset "Web Debug Build"` completes all 33 steps and links
  `index.html`/the final WebAssembly application; diagnostics are limited to
  the project's existing `_tss`, imagepacker attribute, pthread/memory-growth,
  JSPI and JavaScript-library warnings;
- targeted `git diff --check` passes for the source, tests, plan and analysis
  files touched by this vertical. Git reports only the working tree's existing
  LF-to-CRLF conversion notices;
- a compiled-source address scan finds only older unrelated Player comments;
  this vertical adds no absolute binary address to compiled source. All exact
  addresses remain in this analysis document and the recovery databases.
