# MotionPlayer join snapshot / HM3-HM4 four-binary reconstruction

Date: 2026-08-11

This note replaces the older `libkrkr2.so`-address narrative around the
Player join-snapshot path.  The four files under `reference/binaries/` are the
joint oracle.  Function names ending in `_guess` are semantic names recovered
from behavior rather than retained source symbols.

## Four-reference map

| Semantic function | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_resetMotionState_guess` | `0x6AFF5C` | `0x580668` | `0x100107B90` | `0x1051AC` |
| `Player_clearJoinSnapshotMaps_guess` | `0x6B54C4` | `0x583A54` | `0x10010BC60` | `0x109614` |
| `Player_restoreAndPruneJoinSnapshots_guess` | `0x6B564C` | `0x583B0C` | `0x10010C1E8` | `0x109BDC` |
| full timeline reseek caller | `0x6B5AA8` | `0x583C8C` | `0x10010C3CC` | `0x109DAC` |
| `Player_buildNodePathKey_guess` | `0x6B2FFC` | `0x5822EC` | `0x10010A038` | `0x1078D8` |
| `MotionNode_initJoinSnapshot_guess` | `0x6968F0` | `0x572BD0` | `0x1000F6710` | `0xF33A4` |
| `MotionNode_restoreJoinSnapshot_guess` | `0x696BD0` | `0x572E52` | `0x1000F6904` | `0xF3588` |
| `JoinSnapshot_invalidateRetainedChildren_guess` | `0x696584` | `0x572904` | `0x1000F6410` | `0xF2FCC` |
| `MotionNode_findSource_guess` | `0x691CC8` | `0x570500` | `0x1000F316C` | `0xEF97C` |

The names and two-argument prototypes for the init/restore functions were
written to all four IDBs.  The path builder and source resolver were also
renamed.  The retained-child invalidator and common map clear now have their
one-argument prototypes and semantic names in every IDB as well.

## Object/container placement

The ABI offsets differ, but the container order and roles agree:

| Container | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| HM3: path key -> per-node join snapshot | Player `+1184` | `+840` | `+1072` | `+772` |
| HM4: variable cascade key -> raw `double` | Player `+1240` | `+868` | `+1112` | `+792` |

Both are hash maps.  HM3 lookup uses the slash-joined node ancestry key built
for the current node index; it is not the raw-label node map.  HM4 is keyed by
the variable-track item's cascade key.  The Android arm64 HM3 implementation
exposes the libstdc++ bucket/predecessor-chain erase mechanics directly in the
decompile; the other three targets use equivalent helper calls.  This is an
erase-on-success pass followed by a full clear, not a map retained across the
reseek boundary.

### HM3 path-key construction and malformed-tree boundary

`Player_buildNodePathKey_guess` first assigns the output string to empty.  A
node index of zero therefore returns the empty key.  For every nonzero index,
all four references then perform the same unchecked parent walk:

1. Read `nodes[index]` directly from the node deque.
2. Build the segment `"/" + node.label`.
3. Prepend that segment to the accumulated output.
4. Replace `index` with `node.parentIndex` and repeat until it becomes zero.

Thus labels are ordered from the highest non-root ancestor to the selected
node, and an empty label contributes a bare slash.  For example, labels
`top`, empty, and `leaf` produce `/top//leaf`.  The root label is never part
of the key because index zero terminates the walk before a deque lookup.

There is deliberately no negative-index, out-of-range, or parent-cycle guard.
Malformed topology reaches the native deque's unchecked indexing/loop
behavior; it does not return a safe partial key.  The port previously inserted
a bounds check and `break`, which changed that boundary by silently returning
the path accumulated so far.  Fresh decompilation of all four references
falsified that compatibility guard, so it has been removed.

The function has exactly the two semantic consumers described below: HM3
production and HM3 restore/prune.  The separate raw-label node map does not use
this path key.

## HM3 hash-node and mapped-value structure (fresh 2026-08-15 audit)

Fresh decompilation of the four HM3 `operator[]`/upsert helpers and their full
node-destruction chains replaces the old `value_structs.h` interpretation.  The
mapped value is not a flat collection of unrelated opaque dispatch/string/heap
owners.  It has the same source-level nesting in every reference:

```cpp
struct PerNodeLayerState_guess {
    int nodeType;
    ClipSlot clipSlot;                 // complete slot object, initially zero
    tTJSVariant childPlayerSnapshot;
    std::vector<MeshPoint> meshControlPoints;
    double particleInterp[9];
    tTJSVariant particleArraySnapshot;
};
```

The exact ABI layout is:

| target | hash-node allocation | mapped value base / size | embedded `ClipSlot` | child Variant | outer mesh vector | particle doubles | particle Variant |
|---|---:|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x2D0` (720) | node `+16` / `696` | `V+8`, size `536` | `V+544`, 20 bytes | `V+568`, 24 bytes | `V+600..671` | `V+672`, 20 bytes; tail-align to 696 |
| Android armv7 | `0x248` (584) | node `+16` / `560` | `V+8`, size `432` | `V+440`, 12 bytes | `V+452`, 12 bytes | `V+472..543` | `V+544`, 12 bytes; tail-align to 560 |
| iOS arm64 | `0x2D0` (720) | node `+24` / `696` | `V+8`, size `536` | `V+544`, 20 bytes | `V+568`, 24 bytes | `V+600..671` | `V+672`, 20 bytes; tail-align to 696 |
| iOS armv7 | `0x228` (552) | node `+12` / `540` | `V+8`, size `420` | `V+428`, 12 bytes | `V+440`, 12 bytes | `V+456..527` | `V+528`, 12 bytes |

The upsert helpers are Android arm64 `0x6EFA54`, Android armv7 `0x5AD532`,
iOS arm64 `0x10010BF74`, and iOS armv7 `0x109928`.  A miss allocates the native
hash node and zeroes the complete mapped value (`0x2B8`, `0x230`, `0x2B8`, and
`0x21C` bytes respectively).  A hit returns the existing value unchanged.
Consequently the snapshot's embedded `ClipSlot` does **not** begin with the
ordinary live-node defaults (`frameIndex=-1`, `done=true`, blend 16, default
colors/opacity/scales, and particle defaults).  Every scalar and every owner
starts as zero/null/Void; the initializer writes only the fields described in
the producer section.

The remaining hash-node metadata explains the allocation sizes rather than
adding fields to the value.  Android libstdc++ stores the cached hash after the
value: arm64 at node `+712`, and armv7 at node `+576` followed by alignment
padding.  libc++ stores it in the prefix: iOS arm64 at node `+8` before the key
at `+16`, and iOS armv7 at node `+4` before the key at `+8`.

### Destruction proves the embedded slot identity

The per-node destroy helpers are Android arm64 `0x6DA3F8`, Android armv7
`0x59BBCE`, iOS arm64 `0x10012A038`, and iOS armv7 `0x128E08`.  Each performs
the same reverse declaration-order sequence:

1. destroy the particle Variant;
2. free the outer mesh vector backing;
3. destroy the child-Player Variant;
4. call the ordinary `MotionNodeFrameSlot_destroy_guess` helper on `V+8`;
5. release the map key and delete the hash node.

The shared slot destructors are `0x6DA44C`, `0x59BC0A`, `0x10012A0A4`, and
`0x128E4A`.  Inside the embedded slot they destroy, in reverse order,
`anchorTarget`, `cameraTarget`, `modelDtgt`, `motionDtgtValue`, the slot-local
mesh vector, `meshCurveVariant`, `actionValue`, the six curve Variants,
`srcValue`, and `iconValue`.  Those calls are the source of the earlier false
`dispatch_8`/`heap_320`/`heap_584`/`ttstr_688` model: the old audit treated
members of one reused `ClipSlot` destructor as independent outer fields and
also misidentified several Variant and vector cleanup helpers.

The portable type now embeds `MotionNode::ClipSlot` directly, declares the four
outer nontrivial members in native order, and explicitly resets the live-node
slot's nonzero portable defaults to reproduce the map's all-zero construction.
The invented `DispatchRef` and `HeapRef` types and their fabricated ownership
tests were removed.

## Producer data flow

`Player_resetMotionState_guess` is entered for Join play and returns
immediately while the Player is queuing.  Otherwise every target performs:

1. Clear HM3 and HM4.
2. Interpolate the variable tracks and evaluate every non-root node at the
   current evaluation time.  Android arm64 inlines the aggregate node loop;
   Android armv7 and both iOS targets retain a helper.
3. For each variable-track item, inspect its active slot.  If the slot is not
   the type-zero sentinel, store the item's current raw `double` in HM4 under
   the cascade key.
4. Walk nodes from index 1.  Test `joinTarget` first, then accept the exact
   type mask `0x19D`, i.e. node types `{0, 2, 3, 4, 7, 8}`.  Build the ancestry
   path key, upsert HM3, and initialize its per-node snapshot.

The root node is deliberately excluded.  A prior source comment omitted type
4 from the mask even though both the code and all four references include it.

## Per-node snapshot initialization and ownership transfer

The common semantic order is important:

1. Save `nodeType`; read the active-slot index only to select the source slot.
2. If the node uses mesh type 1, copy the node's evaluated mesh-control-point
   vector into the snapshot.
3. For node type 3, copy-assign the child-Player `tTJSVariant` into the
   snapshot and clear the node field.  Ownership is temporarily held by HM3.
4. For node type 4, copy-assign the particle-array `tTJSVariant` into the
   snapshot and clear the node field.  If the active slot is done, save the
   done byte and return.  Otherwise copy the nine evaluated particle doubles
   and continue.
5. For every non-type-4 node, save the active slot's done byte and return early
   when it is set.  The type-3 variant transfer therefore happens before this
   early return.
6. On a live slot, retain a reference-counted copy of the active `src` string,
   then snapshot content mask, blend mode, origin, four packed colors,
   opacity, XYZ, flip flags, angle, scale X/Y, and slant X/Y.

The retained `src` string is a lifetime owner only.  Restore never writes it
back to the slot; destruction of the HM3 value releases it.

## Consumer data flow and exact restore order

The full reseek calls `Player_restoreAndPruneJoinSnapshots_guess` after the
absolute node-slot reseed and before rebuilding HM1 cascade heap results.

The function first restores HM4 values.  For every variable-track item whose
active slot is not type zero, an HM4 hit overwrites that slot's `value`.

It then walks non-root nodes, rebuilds each path key and performs HM3 lookup.
An entry is consumed only when both `node.joinTarget` is true and the saved
node type equals the current node type.  A successful match performs the
following in this exact order:

1. Mesh type 1: copy the saved control-point vector into the active slot's
   mesh source vector.
2. Type 3: copy-assign the child-Player variant back into the node and clear
   the snapshot variant.
3. Type 4: copy-assign the particle-array variant back and clear the snapshot
   variant; if the saved done byte is zero, restore all nine particle doubles
   into the active slot.
4. If either the current active slot is done or the saved snapshot is done,
   skip the common scalar block.
5. Otherwise restore content mask, blend mode, origin, packed colors, opacity,
   XYZ, flip flags, angle, scale X/Y, and **both slant X and slant Y**.
6. For node type 0 with a live active slot, refresh source resolution.
7. Erase the matched HM3 entry.

After the node walk, the common clear helper is called unconditionally.
Missing keys and identity mismatches survive only until this terminal clear.

## Unmatched-snapshot invalidation lifecycle

The common map clear is not two ordinary container `clear()` calls.  All four
references first walk every remaining HM3 value and call a dedicated helper:

1. If the type-3 child-Player snapshot Variant has object tag 1, call its
   dispatch's virtual `Invalidate(0, nullptr, nullptr, dispatch)`.
2. If the type-4 particle-array snapshot is non-void, convert/copy it through
   the ordinary object holder, obtain its `count`, fetch every indexed Variant,
   and call the same `Invalidate` form on every object-tagged element.
3. Clear HM4.
4. Clear HM3, which performs the ordinary Variant/string/vector/dispatch
   destruction and releases references.

The helper ignores `Invalidate` return values and passes the Variant's Object
pointer as `objthis`; it does not substitute the closure's separate ObjThis.
A non-void, non-object particle snapshot follows the normal Variant-to-object
conversion failure path rather than being silently skipped.

This pass distinguishes consumed and unconsumed snapshots.  A successful
type-3/type-4 restore copy-assigns the saved Variant back to the MotionNode and
clears the snapshot Variant before erasing the HM3 entry.  Consequently that
child remains live and is not invalidated.  Only unmatched/mismatched entries
still retain child objects when the terminal pre-pass runs.

## Corrected stale assumption: slant X

The local port previously restored `slantY` but deliberately skipped
`slantX`, based on an obsolete comment.  Fresh decompilation shows the same
five-double tail in all four references:

- Android arm64 restore writes snapshot offsets `+136..+168` to active-slot
  offsets `+128..+160` through a 16-byte copy, another 16-byte copy, and a
  final 8-byte store.
- Android armv7 writes five consecutive 8-byte values at slot-relative
  `+128`, `+136`, `+144`, `+152`, and `+160`.
- iOS arm64 uses the same five 8-byte stores as Android arm64's semantic
  layout.
- iOS armv7 likewise writes five consecutive values in its smaller node ABI.

Therefore the fourth value is `slantX`; it is not padding and is not skipped.
The port now executes `slot.slantX = snapshot.slantX` before restoring
`slantY`.

## Boundary behavior retained by the port

- Join snapshot production is suppressed while queuing.
- The root node is never snapshotted or restored by HM3.
- `joinTarget` is checked both when producing HM3 and when consuming it.
- Type mismatch does not coerce or partially restore a snapshot.
- Mesh and type-3/type-4 ownership restoration occurs before the common done
  gate.
- The type-4 particle block is controlled by the saved done byte, while the
  common scalar block is controlled by both current and saved done bytes.
- The saved `src` owner is never copied back into the slot.
- A matching type-0 live slot performs source resolution after scalar restore.
- Before unconsumed entries are destroyed, retained type-3 children and every
  object element of retained type-4 particle arrays are explicitly invalidated.
- The HM3 path builder excludes root, preserves empty-label slash segments,
  and performs an unchecked parent-index walk with no bounds/cycle recovery.
- The clear order is HM3 invalidation pre-pass, HM4 destruction, then HM3
  destruction and ordinary release of all snapshot-owned payloads.

## Port changes and validation

- Moved the two node-only operations from private `Player` methods to
  `MotionNode::initJoinSnapshot_guess` and
  `MotionNode::restoreJoinSnapshot_guess`.  Their mutable snapshot references
  now express the native Variant transfer/clear lifecycle without
  `const_cast`.
- Added the missing `slot.slantX = snapshot.slantX` restore.
- Added a focused unit case covering both slant axes and the current-slot done
  gate.
- Added the four-reference unmatched-snapshot pre-clear invalidation pass and a
  focused unit case covering type-3 and type-4 child invalidation arguments.
- Replaced the obsolete single-target/address-stamped path helper name, removed
  its invented bounds guard, and added a regression covering the root empty key,
  ancestor-prepend order, and the bare slash emitted by an empty label.
- Fresh 2026-08-15 teardown/upsert recovery replaced the fabricated flat
  `dispatch_*`/`heap_*`/`ttstr_*` owner list with the real embedded
  `MotionNode::ClipSlot` plus four outer nontrivial owners.  The portable map
  value constructor now reproduces the native all-zero state even though a
  normal live-node `ClipSlot` has nonzero reset defaults.
- Replaced the fake raw-owner unit cases with focused coverage for zeroed slot
  construction, every real embedded owner category, move ownership of both
  mesh vectors, and unordered-map emplacement.
- Web Debug completed a full rebuild and final `index.html`/Wasm link.
- Wasmtime Debug completed the impacted rebuild; a final `ninja -n` reported
  `no work to do`.
- The complete `motionplayer-dll.cpp` test translation unit passed Emscripten
  `-fsyntax-only` with the real Web Debug compile flags.  The only diagnostic
  was the repository's existing deprecated `_tss` literal-operator warning.
- The dedicated `motionplayer-ttstr-hash-test.cpp` translation unit also passed
  `-fsyntax-only` with those same real compile flags; the diagnostic was the
  same pre-existing `_tss` warning.
- `git diff --check` passed; only the repository's existing LF/CRLF conversion
  notices were printed.
- All four IDBs were saved after applying names/prototypes for the HM3
  `operator[]` and node/chain destroy helpers, exact value-layout and clear-order
  comments, and construction/destruction/layout bookmarks.
