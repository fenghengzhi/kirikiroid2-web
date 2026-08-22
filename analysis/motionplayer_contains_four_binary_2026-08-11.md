# MotionPlayer `contains` / raw-label resolution four-binary recovery (2026-08-11)

## Scope

This note records the current four-reference-binary evidence for the complete
hit-test call chain:

1. `Motion.EmotePlayer.contains(label, x, y)`;
2. `D3DEmotePlayer.contains(label, x, y)`;
3. `Motion.Player.contains(x, y)`;
4. the shared raw-label resolver; and
5. the shared child/particle-Player visitor used by both recursive operations.

The addresses below belong here rather than in compiled-source comments.  The
older local comments that cite `libkrkr2.so` instruction addresses as function
entries are not authoritative for the current four reference binaries.

## Four-target address map

### Facade callbacks

| Role | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `Motion.EmotePlayer` registrar | `0x67CEA8` | `0x5612E8` | `0x1001B5130` | `0x1B4DE0` |
| `EmotePlayer.contains` callback | `0x67EEEC` | `0x497BFE` | `0x1001B5E84` | `0x1B5B74` |
| `D3DEmotePlayer` registrar | `0x52E8E4` | `0x494078` | `0x100232278` | `0x230F46` |
| `D3DEmotePlayer.contains` callback | `0x530F3C` | `0x4950F0` | `0x100233558` | `0x2322EC` |
| `Motion.Player` registrar | `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |
| `Player.contains` callback | `0x6D071C` | `0x595AF8` | `0x1001218E8` | `0x12065C` |

Android ARM64 IDA had incorrectly merged the independent function beginning at
`0x6D071C` into the preceding draw function.  The previous function ends at
`0x6D071C`; the `SUB SP, SP, #0xA0` prologue at that address starts
`Player.contains`.  The IDB was split into `[0x6D0160, 0x6D071C)` and
`[0x6D071C, 0x6D0CD4)` before taking the fresh decompilation used here.

### Recursive lookup and traversal

| Role | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| raw-label resolver | `0x6B2EB8` | `0x58220C` | `0x100109EEC` | `0x10777C` |
| child/particle visitor | `0x6B33FC` | `0x5824E4` | `0x10010A13C` | `0x107A20` |
| resolver recursion callback | `0x6EF6EC` | `0x5AD280` | `0x100141B54` | `0x142B6A` |
| recursive-contains callback | `0x6F2A5C` | `0x5AFAB2` | `0x100145288` | `0x145B4E` |

The Android ARM64 recursive-contains callback does not currently decompile, but
its complete 18-instruction body was checked directly.  It loads the captured
`x` and `y`, calls `Player.contains`, writes the captured result byte and
returns false on a hit, and otherwise returns true.

## Recovered common source structure

Compiler-specific deque arithmetic, TJS dispatch extraction and function-object
ABI details removed, the four binaries implement the following common flow:

```cpp
MotionNode *Player::findNodeByRawLabel_guess(const ttstr &label,
                                             bool recursive) {
    if (auto it = rawLabelToNodeIndex.find(label);
        it != rawLabelToNodeIndex.end()) {
        return &nodes[it->second];
    }

    if (!recursive)
        return nullptr;

    MotionNode *found = nullptr;
    visitChildPlayerDispatches_guess([&](Player *child) {
        found = child->findNodeByRawLabel_guess(label, recursive);
        return found == nullptr; // true = keep visiting
    });
    return found;
}

void Player::visitChildPlayerDispatches_guess(
    const std::function<bool(Player *)> &visitor) {
    for (MotionNode &node : nodes) {
        if (node.nodeType == 4) {
            for (each Player dispatch in node.particlePlayerArray) {
                if (!visitor(nativePlayerFromDispatch))
                    return;
            }
        } else if (node.nodeType == 3) {
            if (!visitor(nativePlayerFromDispatch(node.childPlayerVariant)))
                return;
        }
    }
}

bool Player::contains(double x, double y) {
    for (size_t i = 1; i < nodes.size(); ++i) {
        MotionNode &node = nodes[i];
        if (node.nodeType == 1 && node.shape.contains(x, y))
            return true;
    }

    bool found = false;
    visitChildPlayerDispatches_guess([&](Player *child) {
        if (child->contains(x, y)) {
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

bool EmotePlayer::contains(const ttstr &label, double x, double y) {
    MotionNode *node = player->findNodeByRawLabel_guess(label, true);
    return node != nullptr && node->shape.contains(x, y);
}
```

`D3DEmotePlayer.contains` is a facade over the same operation.  ARMv7 and both
iOS builds call the EmotePlayer callback after copying/AddRef'ing the label
variant into a temporary and release it afterward.  Android ARM64 optimizes the
same resolver-plus-shape-test body into the D3D callback itself.  No target adds
a visibility check.

## Object and container behavior

The resolver first searches the current Player's `std::map<ttstr, node-index>`.
The index is translated back into the platform's `std::deque<MotionNode>`:

| Target | `MotionNode` stride | shape record offset | deque block evidence |
|---|---:|---:|---|
| Android ARM64 | `2632` (`0xA48`) | `1664` (`0x680`) | libstdc++ deque iterator arithmetic |
| Android ARMv7 | `2272` (`0x8E0`) | `1424` (`0x590`) | libstdc++ deque iterator arithmetic |
| iOS ARM64 | `2648` (`0xA58`) | `1680` (`0x690`) | libc++ 16-elements-per-block indexing |
| iOS ARMv7 | `2228` (`0x8B4`) | `1392` (`0x570`) | libc++ 16-elements-per-block indexing |

If the map misses and recursion is enabled, the shared visitor walks the node
deque in order.  A type-4 node enumerates its particle Player array from index
zero to `count - 1`; a type-3 node contributes its single child Player.  A false
callback result terminates the entire walk.  Consequently:

- a current-Player label always wins over every descendant;
- descendants use depth-first traversal in node order;
- particle children use array order;
- the first recursive match/hit wins and later siblings are not examined; and
- the same visitor/order is used by both recursive label lookup and recursive
  coordinate-only `Player.contains`.

The native visitor still invokes the supplied callback when native-instance
extraction produces null.  Its two callbacks immediately call a Player method
through that value, so malformed type-3/type-4 dispatch state reaches the
native null-dereference boundary rather than being silently skipped.  Normal
motion construction maintains the non-null invariant.

## Exact gates and boundary results

- `EmotePlayer.contains(label, x, y)` and `D3DEmotePlayer.contains` pass
  `recursive = true` unconditionally.
- The label is used as the raw UTF-16 `ttstr` map key.  There is no narrow-string
  conversion and no empty-label precheck.  An empty label can therefore resolve
  if the raw-label map contains it.
- A missing label returns false.
- The facade callbacks do not call motion loading, `updateLayers`, bounds
  calculation, or coordinate transforms.  They consume the node geometry that
  is already present at the time of the call.
- They do not test Player visibility, layer visibility/activity, or node type
  before calling the geometry record's `contains` method.
- Coordinate-only `Player.contains(x, y)` scans local nodes starting at index
  one, excluding the constructor-created root.  Only local node type 1 is tested
  as geometry.  Recursive child/particle traversal happens after every local
  shape misses.
- Geometry-specific edge and NaN behavior is recorded separately in
  `analysis/motionplayer_geometry_four_binary_2026-08-11.md`.

## Per-target implementation differences

- Android uses a manager/invoker pair for the captured function object.  The
  resolver and contains captures are heap allocations of 24 bytes on ARM64 and
  12 bytes on ARMv7.
- iOS uses a polymorphic function-object wrapper.  The corresponding captures
  occupy 32 bytes on ARM64 and 16 bytes on ARMv7; their invoke entries are the
  last function pointers in the recovered vtables.
- Android ARM64's D3D facade inlines the Emote contains body.  The other three
  D3D facades call the corresponding Emote callback.
- These ABI/container differences do not change lookup priority, traversal
  order, short-circuiting, or returned booleans.

## Local pre-edit line-by-line delta

Before the semantic edit, `Player::hitTestLayer` differs from all four binaries
in five observable ways:

1. it calls `ensureMotionLoaded()`;
2. it calls `updateLayers()` and `calcBounds()` when the deque is non-empty;
3. it narrows the label to an 8-bit string;
4. it rejects labels whose narrowed form is empty; and
5. it creates an unnecessary second `ttstr` copy before the raw map lookup.

The existing recursive lookup and coordinate-only contains loops have the
correct high-level node/particle order, but duplicate the native shared visitor
and silently skip null native Player extractions.  Their comments and symbol
names also cite obsolete single-target/interior addresses (`0x6B5AD8`,
`0x6B601C`, `0x6D333C`, `0x681B0C`, and `0x530B5C`) as though they were current
function entries.

The intended edit is therefore narrowly scoped:

- make the label-based hit test a direct raw-map resolve plus shape test;
- preserve recursive=true and the existing current-then-descendant priority;
- restore one shared child/particle visitor with false-to-stop semantics;
- use `_guess` for recovered names whose original spelling is unknown; and
- remove binary addresses from compiled-source comments while retaining this
  four-target evidence record.

## Implemented local alignment

The following alignment was applied after the evidence and pre-edit delta above
were recorded:

- `Player::hitTestLayerByRawLabel_guess` is now exactly a recursive raw-label
  resolve followed by one geometry-record hit test.  The invented load, layer
  update, bounds calculation, narrowing and empty-label gates were removed.
- `Player::visitChildPlayerDispatches_guess` now owns the shared type-4/type-3
  traversal and false-to-stop contract.
- `findNodeByRawLabel_guess` and coordinate-only `Player::contains` now express
  their recursive work through that shared visitor, including the native null
  extraction boundary.
- Emote and D3D facade methods delegate only to the direct current-state hit
  operation.
- Targeted stale single-binary/interior-address comments were replaced with
  semantic comments; this document remains the address-bearing evidence source.

## IDB improvements and verification

All four IDBs were improved and saved:

- Android ARM64's wrongly merged function was split at the verified independent
  `Player.contains` prologue.
- Seven functions per target (28 total) were renamed with `_guess`: both facade
  callbacks, coordinate-only contains, raw-label resolution, shared traversal,
  and the two recursive visitor invoke functions.
- Matching function prototypes were applied on all four targets and the
  decompiler caches were invalidated.
- Fresh post-type decompilation again showed index-one local shape scanning and
  shared visitor recursion on every target.

Verification after the local edit:

- `git diff --check`: passed (only the worktree's existing LF-to-CRLF notices).
- `cmake --build out/web/debug`: passed the full incremental compile and final
  Emscripten link.
- `cmake --build out/wasmtime/debug --target geometry_hit_test_wasm`: passed
  (`ninja: no work to do`; the unchanged geometry guest remained current).
