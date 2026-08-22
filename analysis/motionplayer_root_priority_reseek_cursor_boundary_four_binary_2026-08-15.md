# MotionPlayer root-priority absolute reseek and cursor boundary (four binaries)

Date: 2026-08-15

This note replaces the Android-arm64-only address narrative that remained in
`PlayerFrameProgress.cpp` around full reseek's root-content scan.  The four
files under `reference/binaries/` are the joint oracle.  Names ending in
`_guess` are semantic recovery names, not retained source symbols.

## Four-reference map

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_reseekTimelineCursors_guess` | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |
| acquire retained priority holder | `0x6B5FA4` | `0x583F6A` | `0x10010C778` | `0x10A10C` |
| read dynamic `count` property | `0x6B5FFC` | `0x583F8E` | `0x10010C7A4` | `0x10A138` |
| scan indexed frame / read `time` | `0x6B603C` / `0x6B60B0` | `0x583FDE` / `0x584002` | `0x10010C7E8` / `0x10010C818` | `0x10A16E` / `0x10A1A4` |
| commit scan cursor | `0x6B6124` | `0x584048` | `0x10010C888` | `0x10A1EA` |
| `min(cursor, count-2)` clamp | `0x6B6130..0x6B6134` | `0x584050..0x58405A` | `0x10010C894..0x10010C898` | `0x10A1F6..0x10A202` |
| fetch current frame | `0x6B6154` | `0x584066` | `0x10010C8AC` | `0x10A212` |
| commit current `content` | `0x6B61E0` / `0x6B6200` | `0x584094` / `0x58409E` | `0x10010C8F0` / `0x10010C8FC` | `0x10A246` / `0x10A256` |
| commit current `time` | `0x6B6224` / `0x6B6228` | `0x5840B8` / `0x5840C0` | `0x10010C920` / `0x10010C924` | `0x10A284` |
| fetch next / commit next `time` | `0x6B6250` / `0x6B62D0..0x6B62E8` | `0x5840D2` / `0x584112` | `0x10010C940` / `0x10010C980..0x10010C984` | `0x10A29A` / `0x10A2D0..0x10A2D6` |

All four full-reseek functions have six code xrefs, and every xref comes from
the corresponding `Player_frameProgress_guess` (`0x6BE44C`, `0x58A63A`,
`0x100113B50`, `0x111556`).  They are the first-frame, direction change, and
forward/reverse wrap/reposition branches already described in
`motionplayer_progress_reseek_four_binary_2026-08-11.md`; there is no separate
public root-only reseek entry in the reference binaries.

## Player field placement

| field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| retained priority source Variant | `+548` | `+356` | `+436` | `+296` |
| root cursor | `+568` | `+368` | `+456` | `+308` |
| current root time | `+576` | `+376` | `+464` | `+312` |
| next root time | `+584` | `+384` | `+472` | `+320` |
| retained current root-content Variant | `+616` | `+416` | `+504` | `+352` |

The offset changes are exactly those expected from 20-byte versus 12-byte
`tTJSVariant`, pointer width, and the different Player member packing.  The
field order and ownership are the same.

## Common source-level algorithm

```text
priorityHolder = retain(Player.prioritySource)
count = integer(priorityHolder["count"])
j = 0

if count != 0:
    if count >= 1:
        for j in [0, count):
            frameTime = real(priorityHolder[j]["time"])
            if target == frameTime:
                break
            if frameTime <= target:
                continue
            --j
            break
    Player.rootCursor = j
else:
    j = Player.rootCursor

Player.rootCursor = min(j, count - 2)
current = priorityHolder[Player.rootCursor]
Player.rootContent = current["content"]
Player.rootCurTime = real(current["time"])
next = priorityHolder[Player.rootCursor + 1]
Player.rootNextTime = real(next["time"])
```

Unlike the preceding tag scan, the root scan never performs the historical
double increment and never truncates frame time through `int`.  It uses an
exact ordered `double` equality test, followed by `frameTime <= target`.

## Boundary behavior

- `count` is obtained through the ordinary named property helper for
  `"count"`, not a container `GetCount` virtual.  Its HRESULT is ignored before
  Variant-to-integer conversion, so a dynamic script object controls the signed
  result.
- For a normal `count >= 2`, an exact hit selects that frame.  The first frame
  greater than the target decrements the scan index once.  A target after the
  last frame scans to `count`, then the common clamp selects `count-2`, ensuring
  that current and next remain a pair.
- A target before the first frame selects `-1`; there is no lower-bound guard
  before the indexed property read.
- `count == 0` reuses the prior cursor only until the unconditional
  `min(cursor, -2)` clamp.  A normal nonnegative prior cursor therefore becomes
  `-2`, after which indexed reads proceed with `-2` and `-1`.
- `count == 1` clamps to `-1`.  `count == 2` clamps to at most zero.
- A negative dynamic count takes the nonzero branch but skips the loop.  All
  four targets commit scan cursor zero before clamping.  For `count == -1`, the
  resulting cursor is `-3` and the two indexed reads are `-3`, then `-2`.
  The previous port committed the scan cursor only inside `count >= 1`; an
  already-more-negative prior cursor could therefore survive, which did not
  match any reference.
- The subtraction itself is native 32-bit wrapping arithmetic. For
  `count == INT_MIN`, `count-2` has signed interpretation `INT_MAX-1`; signed
  min therefore preserves the freshly committed zero cursor and the two reads
  are `0`, then `1`. A direct C++ signed subtraction is undefined here. The
  source now uses the same explicit modulo-32 helper as variable absolute
  reseed; the four-end arithmetic and regression are detailed in
  `motionplayer_variable_track_absolute_reseed_four_binary_2026-08-15.md`.
- NaN receives no repair.  If either ordered comparison is unordered, the
  `frameTime <= target` test fails, so the scan decrements and stops.  Signed
  zero follows ordinary exact equality.

## Observable commit and failure order

The cursor is committed before any post-scan indexed access.  After that the
four references commit in this exact order:

1. current frame lookup;
2. current frame `content` lookup and copy-assignment to Player;
3. current frame `time` lookup and store;
4. next frame lookup;
5. next frame `time` lookup and store.

Thus a throwing or malformed getter does not roll back earlier cursor/content/
time stores.  The retained priority holder, current root frame, and next root
frame all remain live across the later variable/node/join/HM1 phases.  The
common epilogue releases next frame, current frame, priority source, then the
still-older tag source.  The four-target release addresses and tag-owner
re-entrancy regression are recorded in
`motionplayer_tag_absolute_reseek_four_binary_2026-08-15.md`.

## Port correction and regression

`PlayerFrameProgress.cpp` now places the root-cursor commit outside the
`count >= 1` scan branch, uses the source-level `min(j, count-2)` expression,
retains an independent priority Variant through the full-reseek tail, and
reloads the live Player evaluation time after each frame-time getter.  The
full-reseek member now takes only `this`, matching every native call site.
`Player.h` likewise keeps portable semantic field comments instead of one
target's offsets.

The focused unit regression covers exact-hit, between-frame, after-tail, and
negative dynamic-count cases.  A custom TJS priority dispatch advertises
`count == -1` while the prior cursor is `-10`; the required result is cursor
`-3` followed by numeric reads `{-3, -2}`.  This exercises the boundary that
distinguishes the corrected source from the prior implementation.  A second
dispatch clears the persistent priority field inside `count`; the retained
local owner must still complete scan/current/next reads `{0,1,2,1,2}`.
The same custom dispatch now also advertises `INT_MIN`, requiring cursor zero
and numeric reads `{0,1}` after the wrapping clamp.

## Validation

- The complete `motionplayer-dll.cpp` unit-test translation unit passes the
  Emscripten syntax-only invocation; the only diagnostic is the existing
  `_tss` warning.
- `cmake --build --preset "Web Debug Build"` completes all 33 steps and links
  the final WebAssembly application.  Diagnostics are limited to the existing
  `_tss`, imagepacker `nodiscard`, pthread/memory-growth, JSPI, and JavaScript
  library warnings.
- A targeted scan finds no remaining `0x6B8C..0x6B8F` or Android Player-offset
  comments in the compiled source touched by this vertical.  The historical
  address range remains only in the migration ledger as a record of what was
  removed.
- `git diff --check` passes for the source, test, plan, and analysis files
  touched by this vertical; Git reports only the repository's existing
  LF-to-CRLF conversion notices.
