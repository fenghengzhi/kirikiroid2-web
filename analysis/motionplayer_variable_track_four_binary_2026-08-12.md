# MotionPlayer variable-track four-binary reconstruction (2026-08-12)

## Scope and authority

This note replaces the old single-`libkrkr2.so` interpretation of the
`VariableLabelScope` path.  The following four shipped plugin binaries are the
joint authority:

- Android arm64: `reference/binaries/android/arm64-v8a/libmotionplayer.so`
- Android armv7: `reference/binaries/android/armeabi-v7a/libmotionplayer.so`
- iOS arm64: the arm64 slice under `reference/binaries/ios/`
- iOS armv7: the armv7 slice under `reference/binaries/ios/`

The portable source names below end in `_guess` where the original C++ symbol
name was not retained. Exact addresses and ABI offsets intentionally stay in
this analysis file instead of compiled-source comments.

## Four-target function map

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_initVariables_guess` | `0x6CAB30` | `0x592944` | `0x10011D540` | `0x11BF04` |
| `VariableTrackSlot_step_guess` | `0x6B4C4C` | `0x583518` | `0x10010B604` | `0x108EDC` |
| `VariableTrackSlot_merge_guess` | `0x6B4E50` | `0x583648` | `0x10010B76C` | `0x109090` |
| `Player_advanceTimelineStreams_guess` | `0x6B3EBC` | `0x582BE0` | `0x10010AA08` | `0x1083D8` |
| `Player_rewindTimelineStreams_guess` | `0x6B6E1C` | `0x584838` | `0x10010D230` | `0x10AB68` |
| `Player_reseekTimelineCursors_guess` | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |
| `Player_initializeNodeTimelineSlots_guess` | `0x6B388C` | `0x5827D8` | `0x10010A57C` | `0x107EE8` |
| `MotionNode_seekParameterizedFrames_guess` | `0x6B5224` | `0x58387C` | `0x10010BA1C` | `0x1093A0` |
| `Player_refreshParameterizedNodeTimelines_guess` | `0x6B7D30` | `0x5851BC` | `0x10010DF70` | `0x10B8A8` |
| `MotionNodeFrameSlot_parse_guess` | `0x68FA94` | `0x56EDE0` | `0x1000F1464` | `0xED638` |
| `MotionNodeFrameSlot_reset_guess` | `0x68F9EC` | `0x56ED5A` | `0x1000F13A0` | `0xED558` |
| `MotionNodeFrameSlot_mergeContent_guess` | `0x68FE90` | `0x56F06C` | `0x1000F1970` | `0xEDD80` |
| `Player_interpolateVarTrackValues_guess` | `0x6B9200` | `0x5860BC` | `0x10010F094` | `0x10C8D2` |
| `VariableTrackEasing_evaluate_guess` | `0x697B34` | `0x573D40` | `0x1000F78C0` | `0xF4648` |
| `Player_resetMotionState_guess` | `0x6AFF5C` | `0x580668` | `0x100107B90` | `0x1051AC` |
| `Player_restoreAndPruneJoinSnapshots_guess` | `0x6B564C` | `0x583B0C` | `0x10010C1E8` | `0x109BDC` |
| variable deque `clear` | `0x6BE1C8` | `0x58A43C` | `0x100129DE8` | `0x128CC0` |
| zeroed append helper | inlined | `0x592C94` | `0x10011D9C4` | `0x11C328` |
| Android range destroy helper | `0x6F0670` | `0x5ADF0C` | libc++ inline/helper split | libc++ inline/helper split |
| `Player` destructor | `0x6CCEBC` | `0x593C24` | `0x10011F2A0` | `0x11DCC4` |
| variable deque storage destructor | `0x6CCA58` | `0x593AE0` | `0x100129DA0` | `0x128C98` |

The iOS arm64 interpolation entry is exactly `0x10010F094`.  An earlier
decompile started at the interior address `0x10010F0D4`; an entity-boundary
query and a fresh exact-entry decompile corrected that bookkeeping error.

The two incremental entries were also previously named too narrowly as
variable-track helpers. Fresh whole-function decompilation shows that each is
the complete four-stream function: layer/tag, root/priority, variable tracks,
then node timelines. The portable implementation extracts those inline regions
into phase helpers but retains one `advanceTimelineStreams_guess` /
`rewindTimelineStreams_guess` source boundary and the native phase order.
The 2026-08-15 call-site audit additionally corrects both prototypes to
this-only members. No caller passes a double; each inlined loop reads the live
Player evaluation field.

The extracted node region has now also been scrubbed of old Android-address
identifiers. Portable helpers use `seekNodeFrameSelection_guess`,
`seekParameterizedNodeFrames_guess`, `seekNodeFramesForwardPhase_guess`,
`seekNodeFramesBackwardPhase_guess`, `nodeFrameSelectionTime_guess`, and
`initializeNodeTimelineSlots_guess`. These are source-shaped names only: the
directional phase helpers describe inline regions of the two complete native
four-stream functions and are not asserted to be separately callable binary
functions. The raw slot parser and content merger are likewise named
`parseNodeFrame_guess` and `mergeNodeFrameContent_guess`; exact native entries
remain recorded only in analysis rather than encoded into compiled C++ names.

Fresh four-target prototype recovery further shows that the merger receives
only `(slot, nodeType, rawFrameList)`, never a `MotionNode *`. The absolute
initializer owns parse/merge, source refresh and exact-frame action; the
parameterized stepper owns source refresh after an actual crossing and emits no
action. The equal-time/non-playing loop visits only nodes whose parameter-entry
pointer is non-null. Detailed xrefs, layouts and edge behavior are recorded in
`analysis/motionplayer_node_timeline_slot_helpers_four_binary_2026-08-14.md`.

## Source-shaped data flow

The complete path is:

```text
selected motion content
  └─ property "variable"
       └─ one zeroed deque element per numeric item
            ├─ first item["label"] -> ttstr cascadeKey
            ├─ explicit slot type-zero seeds
            ├─ second item["label"] -> retained Variant frameSource
            └─ optional item["scope"] -> scope + "::" + cascadeKey

frame progress / reseek
  └─ step two bracketing slots -> lazily merge frame payloads
       └─ interpolate or hold -> item.value
            └─ shared parameter binder, mode 0 -> HM2 and optional HM1 cascade

join reset
  └─ interpolate live tracks -> HM4[cascadeKey] = item.value

full reseek
  └─ reseed slots -> HM4 hit overwrites active slot.value -> clear HM4/HM3
```

`frameSource` is not derived from a separate `frames` property. The native
builder performs a second independent `item["label"]` read and retains that raw
Variant. The first read is converted to a string; the second is subsequently
indexed through `PropGetByNum`. This odd schema is common to all four targets.

## Builder and partial-construction boundary

Common source-shaped pseudocode:

```cpp
tracks.clear();
Variant variables = motionContent["variable"];
if (variables is Void)
    return;

for (i = 0; i < variables.count; ++i) {
    Variant item = variables[i];
    VariableLabelScope &out = tracks.emplace_back(); // fully zeroed first

    out.cascadeKey = ttstr(item["label"]);            // first read
    out.value = 0.0;
    out.slot[0].typeZero = true;
    out.slot[1].typeZero = true;
    out.cursor = 0;
    out.frameSource = item["label"];                   // second read/copy

    Variant scope = item["scope"];
    if (scope is not Void)
        out.cascadeKey = ttstr(scope) + "::" + out.cascadeKey;
}
```

Important boundaries:

- The deque is cleared before the `"variable"` property is read.
- The native function does not silently return merely because the selected
  motion Variant is not already object-typed. It immediately enters the normal
  object-conversion/dispatch path.
- Numeric item lookup happens before append. Named-property reads happen after
  append.
- Append zeroes the entire element and separately makes the embedded Variant
  tag/type words zero. Therefore both `typeZero` bytes are initially `false`.
- The explicit `true` seeds happen only after the first `label` lookup and string
  conversion succeed.
- A getter/conversion exception does not roll the deque element back. The exact
  prefix of writes completed before the exception remains observable.
- The `label` and `scope` reads use null member-hint pointers. The arm64
  decompiler's `"v"` rendering is a bad `char *` view of the wide literal;
  Android armv7 makes `L"variable"` explicit and the surrounding four-target
  call sequence agrees.

The old port built a stack temporary and pushed it only after all properties
had succeeded. That erased the native partial element on exceptions and has
been replaced with append-first construction.

## Element and slot layouts

### `VarTrackSlot`

The internal offsets are common even though the trailing Variant size changes:

| offset | field | producer / meaning |
|---:|---|---|
| `+0` | `uint32 frameIndex` | `step`, written before any dispatch call |
| `+8` | `double time` | `frame["time"]` |
| `+16` | `uint32 interval` | `content["interval"]` on nonzero type |
| `+20` | `bool typeZero` | true only for merged type 0 |
| `+21` | `uint8 interp` | type 2 -> 0, type 3 -> 1, other nonzero -> stale |
| `+22` | `bool merged` | `step` clears; `merge` sets before any lookup |
| `+24` | `double value` | `content["value"]` on nonzero type |
| `+32` | `Variant easing` | `frame["easing"]`, not `content["easing"]` |

Native slot stride is 56 bytes on both 64-bit targets and 48 bytes on both
32-bit targets.

### `VariableLabelScope`

| field | 64-bit offset | 32-bit offset |
|---|---:|---:|
| `ttstr cascadeKey` | `+0` | `+0` |
| `int cursor` | `+8` | `+4` |
| `double value` | `+16` | `+8` |
| `Variant frameSource` | `+24` | `+16` |
| `slot[0]` | `+48` | `+32` |
| `slot[1]` | `+104` | `+80` |
| total element size | `160` | `128` |

Declaration order is important. Ordinary reverse member destruction releases
`slot[1].easing`, then `slot[0].easing`, then `frameSource`, then `cascadeKey`,
matching all four native range-destruction paths.

## Step and merge ordering

```cpp
step(slot, frames, index):
    slot.frameIndex = index
    frame = frames[index]
    slot.time = double(frame["time"])
    slot.merged = false

merge(slot, frames):
    slot.merged = true
    frame = frames[slot.frameIndex]
    type = int(frame["type"])
    if type == 0:
        slot.typeZero = true
        return
    slot.typeZero = false
    if type == 2: slot.interp = 0
    else if type == 3: slot.interp = 1
    content = frame["content"]
    slot.interval = uint32(content["interval"])
    slot.value = double(content["value"])
    slot.easing = frame["easing"]
```

Every named property in these two helpers uses a null hint. Write placement is
observable on exceptions:

- `step` always commits `frameIndex` before `frames[index]` and `time` before
  clearing `merged`.
- `merge` commits `merged=true` before re-fetching the frame.
- Type 0 commits `typeZero=true` and returns, deliberately retaining stale
  `interp`, `interval`, `value` and `easing`.
- Any nonzero type other than 2 or 3 deliberately retains the old `interp`
  byte while refreshing the remaining payload.

The 2026-08-16 V150 fresh four-product pass additionally recovers the NCB
source tree that this early algorithm summary omitted. `step` retains a
frame-source accessor, then directly builds a retained frame accessor from the
indexed Variant; normal teardown is frame then root. Nonzero `merge` retains
root -> indexed frame -> content accessors. The content accessor deliberately
survives the later frame-level `easing` getter and assignment, then teardown is
content -> frame -> root. Type zero has no content accessor and tears down frame
-> root. Every typed read uses flags 0, null hint and receiver==objthis;
ordinary post-write failure is ignored. The uint32 frame index is passed to
`PropGetByNum` as the same signed 32-bit bit pattern, and a signed interval is
stored as its uint32 low word. Detailed unwind prefixes, addresses and
reentrant-owner probes are in
`motionplayer_variable_slot_step_merge_nested_ncb_lifecycle_four_binary_2026-08-16.md`.

## Incremental forward and backward cursors

Forward:

```cpp
Variant countOwner = item.frameSource
count = countOwner["count"]
active = &slot[cursor]                  // raw cursor, invariant 0 or 1
other  = &slot[(cursor & 1) == 0]
limit  = signed32(uint32(count) - 2u)
while (signed32(active.frameIndex) < limit) {
    if (player.evaluationTime < other.time) break
    cursor = ((cursor & 1) == 0)
    step(active, item.frameSource, other.frameIndex + 1u)
    swap(active, other)
}
if (!slot[0].merged) merge(slot[0], item.frameSource)
if (!slot[1].merged) merge(slot[0], item.frameSource) // intentional
destroy(countOwner)
```

The second forward merge is not a transcription error. Android arm64 passes
element `+48` in both calls; Android armv7 and iOS armv7 pass element `+32` in
both; iOS arm64 passes element `+48` in both. The flags tested before those
calls are the distinct slot-0 and slot-1 `merged` bytes. The portable source
must not “repair” the second argument to slot 1.

The local owner is used only for count. Step and merge re-read the persistent
field, and the owner releases after both merge tests. `count-2` wraps in a
32-bit register and the index comparison is signed. The time load is live on
every iteration. Native uses an ordered `<` break, so a NaN Player cursor does
not break the forward loop; spelling the loop as `target >= other.time` would
incorrectly stop it. Cursor parity commits before step, and step commits the
frame index before numeric dispatch.

Backward:

```cpp
active = &slot[cursor]                  // raw cursor
other  = &slot[(cursor & 1) == 0]
while (active.time > player.evaluationTime) {
    uint32 prev = active.frameIndex - 1u
    cursor = ((cursor & 1) == 0)
    step(other, item.frameSource, prev)
    swap(active, other)
}
if (!slot[0].merged) merge(slot[0], item.frameSource)
if (!slot[1].merged) merge(slot[1], item.frameSource)
```

Rewind performs no count lookup and creates no per-item source owner. The
decrement is unsigned. No zero guard exists; an invalid state can forward
`UINT32_MAX` through the signed numeric-property ABI as `-1`, including the
merge re-fetch. Its live ordered `>` comparison stops on NaN. Exact call-site
ABI, four-target instruction anchors, owner split, raw-cursor boundary,
exception commits and regression coverage are recorded in
`motionplayer_variable_track_incremental_seek_four_binary_2026-08-15.md`.

## Absolute reseed and short-list edge

For every track, full reseek scans frame times from zero:

```cpp
k = 0
for (; k < count; ++k) {
    time = double(frames[k]["time"])
    if (time == target) break
    if (time <= target) continue
    --k
    break
}
seed = (k >= count - 2) ? count - 2 : k
step(slot[0], frames, uint32(seed));     merge(slot[0], frames)
step(slot[1], frames, uint32(seed + 1)); merge(slot[1], frames)
cursor = 0
```

There is no empty/single-frame guard. For counts zero and one, `count - 2`
produces a negative seed which is converted to `uint32_t` and passed to numeric
property access. Exceptions retain whatever prefix `step`/`merge` wrote; there
is no transaction or rollback.

The 2026-08-15 fresh full-reseek audit adds two boundaries that the earlier
summary did not express precisely. First, each iteration makes an independent
owning copy of `item.frameSource` before dynamic `count` and releases it only
after both merges and cursor reset. Second, native `count-2` is a wrapping
32-bit subtraction, not C++ signed arithmetic: `INT_MIN` maps the skipped
scan's index zero to seed zero. The time comparison also reloads the live
Player evaluation field after every dynamic time getter. Exact four-target
addresses, owner releases, root's shared wrap edge, tests and corrections are
in `motionplayer_variable_track_absolute_reseed_four_binary_2026-08-15.md`.

## Interpolation and easing

```cpp
active = slot[cursor & 1]
other  = slot[(cursor & 1) ^ 1]
if (active.typeZero) continue

if (!active.interp || other.typeZero) {
    out = active.value
} else {
    delta = currentTime - active.time
    if (active.interval != 0) {
        q = uint64(delta / uint32(active.interval))
        delta = double(q * uint64(active.interval))
    }
    if (other.value == active.value) {
        out = active.value
    } else {
        t = delta / (other.time - active.time)
        if (active.easing is not Void)
            t = evaluateEasing(active.easing, t)
        out = other.value * t + active.value * (1.0 - t)
    }
}
item.value = out
bindParameterValue(player, item.cascadeKey, mode = 0, out)
```

The interval operation is not `floor(delta / interval) * interval`. Fresh
disassembly shows unsigned conversion, unsigned multiply, then conversion back
to double:

- arm64 targets: `UCVTF`, `FDIV`, `FCVTZU X`, `MUL X`, `UCVTF` sequence;
- armv7 targets: the corresponding unsigned-double conversion and
  `__fixunsdfdi`/runtime-helper sequence.

For ordinary nonnegative in-range deltas this is truncation toward zero. The
likely source expression is a direct `uint64_t` cast; invalid negative, NaN or
out-of-range states additionally expose target/compiler conversion behavior.
The port therefore reconstructs the likely cast/multiply expression and does
not introduce a clamp or `floor`.

### Easing evaluator

The evaluator reads `easing.x` and `easing.y` with two process-persistent,
plugin-wide member hints. These are not VariableTrack-private slots: fresh xref
audits show the same identities in quad point dictionaries, position control
curves, LayerGetter vertex dictionaries, and Player camera-offset dictionaries:

| target | x hint | y hint |
|---|---:|---:|
| Android arm64 | `0x1AB5234` | `0x1AB5238` |
| Android armv7 | `0x1111768` | `0x111176C` |
| iOS arm64 | `0x101B696FC` | `0x101B69700` |
| iOS armv7 | `0x187D42C` | `0x187D430` |

Common algorithm:

```cpp
xs = easing["x", xHint]
ys = easing["y", yHint]
count = xs.count
firstX = xs[0]
if (unordered(firstX, t) || firstX >= t) return ys[0]
last = count - 1
lastX = xs[last]
if (unordered(lastX, t) || lastX <= t) return ys[last]
s = 0
do s += 3; while (ordered(xs[s] < t))
return cubicBezierY(ys[s-3], ys[s-2], ys[s-1], ys[s], t)
```

There is no explicit `s < count` test in the stride loop. The prior last-x gate
is assumed to make the segment search valid. X chooses a stride-three segment,
but the original `t` is passed directly to the cubic Y polynomial; there is no
inverse-X reparameterization. Empty, malformed or non-`3n+1` control arrays are
not sanitized.

The 2026-08-16 V149 fresh raw-instruction pass also fixes two observable
boundaries that the earlier text did not express. The first endpoint uses
`PL`, the second uses `LE`, and both accept unordered; the stride repeat uses
`MI` and accepts only ordered-less. Consequently `t=NaN` performs Count,
`x[0]`, `y[0]` and returns `y[0]`. If only the last x endpoint is NaN, it
returns `y[last]`. The stride cursor is the segment end, and the four dynamic
control reads begin at `s-3`; all Count-derived and stride indices use 32-bit
wrap. The retained source tree is root accessor -> persistent x/y Variants ->
x/y accessors, with reverse teardown. Complete four-product evidence is in
`motionplayer_shared_easing_nested_ncb_segment_base_unordered_four_binary_2026-08-16.md`.

## HM4 join-snapshot lifecycle

`Player_resetMotionState_guess`:

1. Return immediately when `queuing` is true.
2. Clear HM3 and HM4.
3. Interpolate variable tracks and evaluate every non-root node timeline.
4. For each variable track whose active slot is not type zero, store
   `HM4[cascadeKey] = item.value`.
5. Rebuild eligible per-node HM3 snapshots.

`Player_restoreAndPruneJoinSnapshots_guess` during full reseek:

1. If HM4 is nonempty, visit live tracks.
2. Skip tracks whose active slot is type zero.
3. On a cascade-key hit, overwrite only `activeSlot.value` with the snapshot.
   `item.value`, cursor, timing, interpolation flags and easing are untouched.
4. Restore/prune HM3.
5. Clear both snapshot maps unconditionally.

HM4 is consequently a short-lived join bridge, not the primary live value
table. Live interpolation writes the shared binder, whose mode-zero route feeds
HM2 and the applicable HM1 cascade.

The Engine/Player read surfaces are intentionally different. Motion.Player's
getter reads bound HM1/HM2 directly; EmotePlayer and D3D use an EmoteEngine
router which bypasses HM4 for a label present in this track deque and otherwise
tries HM4 before the bound-value fallback. The exact call and ownership split is
recorded in `motionplayer_get_variable_routing_four_binary_2026-08-14.md`.

## Native container ABI

### Player field anchors

| field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| current evaluation time | `+456` | `+288` | `+344` | `+228` |
| queuing byte | `+480` | `+312` | `+368` | `+252` |
| HM4 variable snapshot map | `+1240` | `+868` | `+1112` | `+792` |
| variable-track deque object | `+1296` | `+896` | `+1152` | `+812` |

These offsets differ because the surrounding standard-library ABIs and Player
layouts differ. They must not be copied into the portable C++ object layout.

### Deque policies

| target family | deque object | block bytes | elements per block |
|---|---:|---:|---:|
| Android arm64, libstdc++ | `80` | `480` used of 512-byte policy | `3 x 160` |
| Android armv7, libstdc++ | `40` | `512` | `4 x 128` |
| iOS arm64, libc++ | `48` | `4000` used of 4096-byte policy | `25 x 160` |
| iOS armv7, libc++ | `24` | `4096` | `32 x 128` |

The Android counts follow libstdc++'s approximately-512-byte buffer policy.
The iOS counts follow libc++'s `max(4096 / sizeof(T), 16)` block-element rule.
This strongly identifies the container as `std::deque<VariableLabelScope>` on
both library families, rather than a custom ring buffer.

`clear()` destroys every live element but retains the deque's standard-library
map/block capacity according to the implementation. The Player destructor runs
the variable-element clear while the node tree and dependent Player state still
exist, later resets/destroys the node tree, and only then destroys the deque's
remaining storage object.

## Portable-source alignment made in this vertical

- Removed the object-type early return in `Player::initVariables`.
- Changed variable construction from stack temporary plus final `push_back` to
  append-first `emplace_back`.
- Reproduced the two independent `label` reads and native write ordering.
- Changed the slot's default `typeZeroFlag` to false, while retaining the
  explicit post-label true seeds.
- Split/renamed step, merge, forward, backward, reseed, interpolation and reset
  functions to source-shaped `_guess` names.
- Removed stale single-Android addresses from the node-stage helper names and
  comments, preserving the distinction between real native function boundaries
  and source-level extractions of inlined four-stream phases.
- Preserved the forward slot-0/slot-0 double merge.
- Replaced `floor` interval quantization with the recovered unsigned cast and
  multiply expression.
- Added persistent x/y easing member hints and removed the non-native stride
  loop count guard.
- Updated container/lifecycle comments so obsolete single-binary offsets do not
  masquerade as portable structure facts.
- Added a unit case that locks the zeroed pre-label partial state.
- Re-audited the absolute reseed phase, added its per-track source owner,
  replaced root/variable signed-overflow expressions with explicit 32-bit
  wrap semantics, and added live-time/`INT_MIN` differential regressions.

Validation for this vertical consists of fresh all-four decompilation, local
source comparison, IDB symbol/type/comment improvement, Emscripten unit-test TU
syntax checking, Web linking, Wasmtime linking and `git diff --check`.
