# MotionPlayer selector-sync publication / deque compaction (2026-08-15)

## Scope and authority

This vertical re-audits `EmoteEngine_syncSelectorControls_guess` and the two
single-caller standard-library clones emitted around it.  Evidence comes fresh
from all four shipped plugin references:

- Android arm64: `reference/binaries/android/arm64-v8a/libmotionplayer.so`
- Android armv7: `reference/binaries/android/armeabi-v7a/libmotionplayer.so`
- iOS arm64: the arm64 slice under `reference/binaries/ios/`
- iOS armv7: the armv7 slice under `reference/binaries/ios/`

The main semantic name was already recovered, but the original C++ symbol is
stripped.  The two compiler-emitted helper names are likewise expressed as
`_guess`.  Exact addresses stay in this analysis page rather than compiled
source comments.

## Four-target function family

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_syncSelectorControls_guess` | `0x66E0FC` / `0x250` | `0x559A8C` / `0x120` | `0x1001AC8A4` / `0x21C` | `0x1AC0D0` / `0x22C` |
| `..._copyItems_guess` | `0x66E34C` / `0x2BC` | `0x559BC8` / `0x1B2` | `0x1001ACAE4` / `0xDC` | `0x1AC338` / `0xA2` |
| `..._removeLabel_guess` | `0x688C78` / `0x1B0` | `0x569544` / `0x12E` | `0x1001ACBC0` / `0x1AC` | `0x1AC3DC` / `0x1BA` |

Each helper has exactly one xref, from its target's selector-sync body.  They
are optimizer/STL artifacts, not additional Engine API functions.

The main function itself has exactly two callers:

| caller | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_applyMetadata_guess` | `0x67AE6C` | `0x5602FE` | `0x1001B4880` | `0x1B43C6` |
| `EmotePlayer_setSelectorEnabled_guess` | `0x67F37C` | `0x56211E` | `0x1001B6218` | `0x1B5FF6` |

Thus synchronization runs once after metadata reconstruction and again whenever
the public selector-enabled setting changes.

## Engine and selector-entry physical layout evidence

| field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| selector deque object | `+656` | `+328` | `+392` | `+196` |
| selector-enabled byte | `+1160` | `+592` | `+792` | `+408` |
| dirty byte | `+1162` | `+594` | `+794` | `+410` |
| published base-label Variant | `+1208` | `+640` | `+840` | `+452` |
| current-label Variant | `+1228` | `+652` | `+860` | `+464` |

Selector entries have a 48-byte stride on both 64-bit targets and a 24-byte
stride on both 32-bit targets.  The common source-shaped prefix is:

| member | 64-bit offset | 32-bit offset |
|---|---:|---:|
| owning selector-controller pointer | `+0` | `+0` |
| label `ttstr` | `+8` | `+4` |
| copied selector-enabled flag | `+16` | `+8` |
| target pointer-vector begin | `+24` | `+12` |
| target pointer-vector end | `+32` | `+16` |

The target vector contains borrowed pointers to transition entries.  This pass
never transfers or deletes their controllers.

## Publication and owner sequence

All four main bodies start in the following exact order:

```cpp
ArrayWithItems base = createFreshArray();
this->_variableLabelsBase = base.value;       // publish closure first
base.Items = nativeItems(this->_variableLabels)->Items;
this->_dirty = true;
// selector traversal follows
```

The factory-return `base.value` owner survives until the function exits.  Copy
assignment to the member adds the published member closure before the Items
copy begins.  The native Items pointer is borrowed from `_variableLabels`; the
function does not create another dispatch accessor for this non-script copy.

This ordering makes two failure boundaries observable:

- If native-instance recovery fails sharply or Items assignment throws, the
  member already names the fresh Array, but `_dirty` has not yet been set and
  no selector entry has been updated.
- If a later controller operation or `Array.remove` script call escapes, the
  fresh base remains published, `_dirty` is already true, and earlier entry
  mutations are not rolled back.  Destruction of the local owner merely drops
  its extra closure reference; the member keeps the Array alive.

The four standard libraries may leave different allocation/capacity details
after an allocation failure inside deque assignment.  The cross-target fact we
can state exactly is the publication-before-copy boundary and absence of any
Engine-level rollback.

## `tTJSArrayNI::Items` is a deque

The copy helper arithmetic identifies `Items` as
`std::deque<tTJSVariant>`, not a flat vector.  The block policies are:

| target family | Variant bytes | elements per block | used bytes | policy |
|---|---:|---:|---:|---|
| Android arm64 / libstdc++ | 20 | 25 | 500 | approximately 512 bytes |
| Android armv7 / libstdc++ | 12 | 42 | 504 | approximately 512 bytes |
| iOS arm64 / libc++ | 20 | 204 | 4080 | approximately 4096 bytes |
| iOS armv7 / libc++ | 12 | 341 | 4092 | approximately 4096 bytes |

Android's clone compares source and destination lengths, reuses destination
blocks when possible, and allocates/appends when the source is longer.  The iOS
clone computes segmented begin/end iterators and enters libc++'s deque
range-assignment helper.  These different instruction shapes implement the
same source operation.

The portable project already exposes `tTJSArrayNI::Items` and
`TJSArrayWithItems_guess::items` as `std::deque<tTJSVariant>`.  A Web build will
naturally use its toolchain's deque ABI; the Android/iOS physical block counts
belong in this mapping rather than being hard-coded into portable layout.

## Selector traversal and branch behavior

Common source-shaped pseudocode is:

```cpp
for (SelectorEntry &entry : selectorDeque) {
    entry.flag = selectorEnabled;
    if (selectorEnabled) {
        entry.controller.commandQueue.clear();
        entry.controller.selectionState = 0;
        entry.controller.applySelection(0, 0.0f, 0.0f);
    } else {
        Variant label(entry.label);
        (void)std::remove(base.Items.begin(), base.Items.end(), label);
    }

    for (TransitionEntry *target : entry.targets) {
        if (selectorEnabled)
            removeVariableLabel_guess(target->label);
        else
            target->controller.setTarget(&zero, 0.0f, 0.0f, false);
    }
}
```

Entry order is the native deque's logical order.  Targets are visited in vector
order.  The selector-enabled byte is re-read on the target branch in the native
code, so a reentrant script call from an earlier target can affect later target
decisions; portable source likewise reads `_selectorEnabled` for each branch.

On the enabled branch the selector command deque is cleared before selection
state is reset and before `applySelection`.  On the disabled target branch the
zero value is a stack scalar; duration and power are both zero and append is
false.

## Exact `std::remove` boundary

The disabled-entry helper is a segmented-deque `std::remove` specialization:

1. Construct one label Variant from `entry.label`.
2. Find the first element equal to that value.
3. Scan the remaining deque.
4. Copy-assign every nonmatching Variant forward into the next output slot.
5. Return the logical new-end iterator.

The caller discards that iterator.  It never calls `erase`, never reduces the
deque size, and never destroys the compacted tail at this point.  Consequently:

- an absent label leaves every element unchanged;
- a present label compacts later nonmatches forward by Variant copy assignment;
- the tail remains live and can retain duplicate Object/string references;
- multiple selector entries repeat this algorithm over the entire unchanged
  physical size, so earlier tail contents participate in later removals.

Replacing this with erase/remove, filtering into a new deque, or shrinking the
tail would change size, contents, reference counts, and later-pass behavior.

## Local-source and recovery-IDB alignment

The executable source already used the correct fresh Array owner, member-first
publication, deque copy, dirty timing, selector branches, target order, and
ignored `std::remove` new end.  No runtime behavior change was justified in this
vertical.  Source comments were tightened to record the full-function owner
scope, publication-before-copy boundary, and live compacted tail.

All four recovery IDBs now contain:

- a detailed main-function lifecycle/container comment and bookmark;
- `_guess` names plus comments for the one-caller Items-copy clone;
- `_guess` names plus comments for the one-caller remove/compaction clone.

Validation:

- the real Emscripten response-file syntax-only check passed with only the
  existing `_tss` warning;
- `cmake --build --preset "Web Debug Build"` completed ten incremental steps
  and linked final `index.html`; only the existing `_tss`, imagepacker, pthread
  memory-growth, JSPI, and JS-library warnings were emitted;
- targeted diff/whitespace checking covers the source comments, plan, and this
  analysis page;
- all four recovery IDBs are saved after the write-back.

This closes the selector-sync publication and compaction vertical only; it does
not imply full motionplayer reconstruction.
