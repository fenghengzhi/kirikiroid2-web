# MotionPlayer variable-track incremental seek four-binary reconstruction (2026-08-15)

## Scope and authority

This vertical re-audits the variable-track regions in the two complete
incremental timeline members.  It does not inherit the old single
`libkrkr2.so` comments as evidence.  The joint authority is:

- Android arm64 `reference/binaries/android/arm64-v8a/libmotionplayer.so`;
- Android armv7 `reference/binaries/android/armeabi-v7a/libmotionplayer.so`;
- the iOS arm64 reference slice;
- the iOS armv7 reference slice.

The two native members also process layer/tag, root/priority, and non-root node
timelines.  This note deliberately follows only their inlined variable-track
regions plus the member ABI needed to call those regions correctly.  Exact
addresses remain here rather than in compiled-source comments.  Names ending
in `_guess` are semantic recovery names, not claims about stripped symbols.

## Function map and corrected ABI

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| forward four-stream member | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| reverse four-stream member | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |
| variable slot step | `0x6B4C4C` | `0x583518` | `0x10010B604` | `0x108EDC` |
| variable slot merge | `0x6B4E50` | `0x583648` | `0x10010B76C` | `0x109090` |

Fresh xrefs find exactly three calls to each directional member, all from the
one frame-progress member:

| target | forward calls | reverse calls |
|---|---|---|
| Android arm64 | `0x6BE590`, `0x6BE7B4`, `0x6BE848` | `0x6BE55C`, `0x6BE5B0`, `0x6BE7E8` |
| Android armv7 | `0x58A770`, `0x58A886`, `0x58A8F0` | `0x58A74A`, `0x58A788`, `0x58A892` |
| iOS arm64 | `0x100113C80`, `0x100113D8C`, `0x100113DFC` | `0x100113C58`, `0x100113C9C`, `0x100113D9C` |
| iOS armv7 | `0x11168A`, `0x1117AC`, `0x111810` | `0x111664`, `0x1116A2`, `0x1117B8` |

Every call site loads only the Player pointer.  None materializes a floating
argument in the platform argument location.  The corrected prototypes are:

```cpp
void Player_advanceTimelineStreams_guess(Player *player);
void Player_rewindTimelineStreams_guess(Player *player);
```

The former `(Player *, double targetTime)` recovery type was invented.  Both
members repeatedly load the current evaluation cursor from Player:

| field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| live evaluation cursor | `Player+0x1C8` | `Player+0x120` | `Player+0x158` | `Player+0xE4` |
| variable deque object | `Player+0x510` | `Player+0x380` | `Player+0x480` | `Player+0x32C` |

This matters under re-entrant TJS dispatch: a count or frame-time getter may
change Player's cursor and the next comparison in the same native loop sees the
new value.

## Forward region: instruction anchors

| boundary | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| per-item source owner copy | `0x6B45DC` | `0x582FB8` | `0x10010AF00` | `0x10880C` |
| count through local owner | `0x6B4634` | `0x582FCE` | `0x10010AF20` | `0x10882C` |
| raw-cursor active pointer | `0x6B4644` | `0x582FDA` | `0x10010AF34` | `0x108840` |
| wrapping `count-2` | `0x6B464C` | `0x582FE2` | `0x10010AF38` | `0x108832` |
| signed index/limit compare | `0x6B4658` | `0x582FF0` | `0x10010AF44` | `0x108856` |
| live Player time load | `0x6B4660` | `0x582FF8` | `0x10010AF4C` | `0x108860` |
| cursor store before step | `0x6B4530` | `0x583012` | `0x10010AF70` | `0x10887E` |
| step with persistent source | `0x6B453C` | `0x58301C` | `0x10010AF7C` | `0x10888C` |
| slot-0 second merge | `0x6B4580` | `0x583036` | `0x10010AFA8` | `0x1088AE` |
| local owner tail release | `0x6B4594` | `0x583042` | `0x10010AFC8` | `0x1088C0` |

### Owner split

Each forward item first copies `item.frameSource` into an owning local Variant.
Only the dynamic `count` lookup consumes that local:

```cpp
Variant countOwner = item.frameSource;
int32 count = PropGetInt(countOwner, L"count");
```

The active-slot step and both merge branches receive the address of the
persistent `item.frameSource` field instead.  The local owner protects the
underlying dispatch's lifetime; it does not freeze the Variant value used by
later parsing.  Therefore:

- a count callback can clear the persistent field and the caller's last owner
  without destroying its dispatch during the callback;
- if no step/merge is needed, the dispatch dies at the normal per-item tail;
- if a later helper is needed, that helper observes the newly cleared or
  replaced persistent field, not `countOwner`;
- the local owner is released after both merge tests, and ordinary C++ unwind
  also destroys it on a callback exception.

Rewind has no analogous per-item Variant copy.

### Raw cursor and wrapping limit

The active pointer uses the complete signed cursor as an array index.  Only the
other-slot expression reduces the cursor to parity:

```cpp
int32 cursor = item.activeSlotCursor;
Slot *active = &item.slot[cursor];
Slot *other = &item.slot[(cursor & 1) == 0];
```

The ordinary invariant is `cursor == 0 || cursor == 1`.  The references do not
repair a malformed value with `cursor & 1`; a malformed raw cursor can form an
out-of-bounds active pointer and enters source-level undefined behavior.

`count - 2` is a 32-bit register subtraction and wraps modulo `2^32`.  The
result and `active.frameIndex` are then compared as signed 32-bit integers.
Portable source therefore reconstructs both steps explicitly instead of using
signed-overflow-prone C++ arithmetic or an implementation-defined unsigned to
signed cast.  In particular, `INT_MIN - 2` becomes `INT_MAX - 1`.

### Live-time and NaN branch shape

Common source-shaped forward loop:

```cpp
int32 limit = signed32(uint32(count) - 2u);
while (signed32(active->frameIndex) < limit) {
    if (player->evaluationTime < other->time)
        break;

    uint32 next = other->frameIndex + 1u;
    item.activeSlotCursor = ((item.activeSlotCursor & 1) == 0);
    step(*active, item.frameSource, next);
    swap(active, other);
}
```

The Player field is loaded again on every loop test.  The branch is an ordered
less-than break, not a `evaluationTime >= other->time` while condition.  Those
forms differ for NaN: ordered `<` is false, so NaN does not break and forward
continues until the signed frame-index limit or an exception.

The add producing `next` is also a 32-bit wrapping add.  Cursor parity is stored
before `step`; `step` then stores `slot.frameIndex` before numeric dispatch,
stores `slot.time` after the time conversion, and clears `merged` last.  A
throwing numeric lookup thus leaves both cursor and frame index committed while
the previous time and merged byte survive.

### Intentional forward merge defect

The two branches test distinct physical merged bytes but both call merge on
physical slot zero:

```cpp
if (!item.slot[0].merged) merge(item.slot[0], item.frameSource);
if (!item.slot[1].merged) merge(item.slot[0], item.frameSource);
```

This is present in all four references.  Android arm32 and both iOS compilers
express it as a loop whose merged-byte pointer advances but whose slot argument
does not.  It must not be normalized into slot-zero/slot-one behavior.

## Rewind region

| boundary | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| raw-cursor active pointer | `0x6B74C8` | `0x584B86` | `0x10010D658` | `0x10AF2E` |
| live Player time load | `0x6B74E8` | `0x584B8E` | `0x10010D670` | `0x10AF4E` |
| cursor store before step | `0x6B73CC` | `0x584BAA` | `0x10010D688` | `0x10AF66` |
| wrapping index decrement | `0x6B73D4` | `0x584BB2` | `0x10010D690` | `0x10AF70` |
| reverse step | `0x6B73E0` | `0x584BB8` | `0x10010D69C` | `0x10AF78` |
| physical slot merges | `0x6B7400`/`0x6B7414` | `0x584BDA` | `0x10010D6C8` | `0x10AF9C` |

Rewind neither queries count nor creates the forward path's local source owner:

```cpp
int32 cursor = item.activeSlotCursor;
Slot *active = &item.slot[cursor];
Slot *other = &item.slot[(cursor & 1) == 0];

while (active->time > player->evaluationTime) {
    uint32 previous = active->frameIndex - 1u;
    item.activeSlotCursor = ((item.activeSlotCursor & 1) == 0);
    step(*other, item.frameSource, previous);
    swap(active, other);
}

if (!item.slot[0].merged) merge(item.slot[0], item.frameSource);
if (!item.slot[1].merged) merge(item.slot[1], item.frameSource);
```

Again, the active pointer uses raw cursor and the Player time is loaded for
every comparison.  Here the loop condition is ordered `active.time > liveTime`,
so NaN on either side stops the rewind.

The decrement is an unsigned 32-bit operation with no zero guard.  Index zero
becomes `0xFFFFFFFF`; the numeric TJS ABI receives the same bits as signed
`-1`.  Merge must preserve the same bridge when it re-fetches that slot's
frame.  After the loop, rewind genuinely advances its slot argument from
physical slot zero to physical slot one; it does not share the forward defect.

## Portable corrections

`cpp/plugins/motionplayer/PlayerFrameProgress.cpp` now:

- gives the two aggregate incremental members their actual this-only ABI;
- evaluates `_clampedEvalTime` at each extracted phase boundary;
- makes the forward per-item `frameSource` owner used by count while retaining
  persistent-field reads for step/merge;
- reconstructs wrapping `count-2` and signed frame-index comparison explicitly;
- uses raw cursor for the active pointer and parity only for the other pointer;
- reloads `_clampedEvalTime` inside every variable forward/rewind test;
- preserves the forward ordered-LT break so NaN continues;
- preserves cursor-before-step, wrapping `+1`/`-1`, the forward slot-zero
  double merge, and reverse physical slot-zero/slot-one merge;
- bridges slot frame-index bits to signed `tjs_int` in merge as well as step.

`Player.h` exposes only test-harness setup/run/observation helpers; none is
registered as a script member.  The new Catch2 case covers:

- a count getter changing live evaluation time from 15 to 25;
- `count == INT_MIN` wrapping and forward slot-zero/slot-zero merge reads;
- forward NaN continuing to the signed frame-index limit;
- count clearing both persistent owners while the local owner survives to the
  item tail;
- cursor and frame-index partial commits on a throwing numeric getter;
- rewind observing a time-getter change from 20 to 5;
- rewind index-zero underflow reaching numeric dispatch and merge as `-1`;
- rewind's absence of a count lookup and its physical slot merge order.

## Recovery database updates and validation

All four recovery databases now carry the corrected this-only function types,
line comments at the owner/wrap/live-time/commit/merge boundaries, and
bookmarks for the forward owner split, wrapping signed limit, slot-zero double
merge, rewind underflow, and rewind live-time reload.  All four databases were
saved successfully after a post-type decompile confirmed one-parameter
signatures.

Validation completed on 2026-08-15:

- the complete motionplayer Catch2 translation unit passed the real
  Emscripten `-fsyntax-only` response file, with only the pre-existing `_tss`
  warning;
- `cmake --build --preset "Web Debug Build" -- -j 8` completed all 33 compile,
  archive, and final `index.html`/Wasm link steps;
- the focused source/document diff passed `git diff --check`, apart from the
  repository's existing LF/CRLF notices.

## Remaining adjacent audit boundary

The corrected aggregate ABI proves that every inlined phase obtains time from
Player rather than a formal floating argument.  This vertical closes live
reloads inside the variable-track loops and refreshes the value at each local
phase boundary.  The adjacent layer/tag and root/priority regions have since
received their own fresh four-reference audit, including aggregate owner
lifetimes, wrapping count/cursor arithmetic, ordered NaN gates, event order,
and re-entrant live-time reloads; see
`analysis/motionplayer_incremental_tag_root_streams_four_binary_2026-08-15.md`.
The non-root incremental node phase has since received the same fresh
four-reference treatment: live deque size, parameterized routing, ordinary
forward count-only owner, no-count rewind, raw selector/parity slots, wrapping
indices, action/exception prefixes, exact dirty publication, physical merges,
and the unguarded source-mask shift are now closed in
`analysis/motionplayer_node_incremental_seek_four_binary_2026-08-15.md`.
