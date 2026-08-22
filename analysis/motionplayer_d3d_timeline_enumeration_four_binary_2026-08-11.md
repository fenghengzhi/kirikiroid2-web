# D3DEmotePlayer timeline enumeration — four-reference reconstruction

Date: 2026-08-11

This note replaces the old single-`libkrkr2.so` address annotations for the
`count/get Main`, `count/get Diff`, `count/get Playing`, and
`getPlayingTimelineFlagsAt` cluster.  All four current reference binaries were
decompiled again before the corresponding C++ names and tests were changed.

## Public D3DEmotePlayer bodies

| Method | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `countMainTimelines` | `0x530A8C` | `0x494DBA` | `0x100233200` | `0x231E5E` |
| `getMainTimelineLabelAt` | `0x530AA8` | `0x494DC8` | `0x10023321C` | `0x231E70` |
| `countDiffTimelines` | `0x530AB4` | `0x494DD6` | `0x100233228` | `0x231E7E` |
| `getDiffTimelineLabelAt` | `0x530AD0` | `0x494DE4` | `0x100233244` | `0x231E90` |
| `countPlayingTimelines` | `0x530ADC` | `0x494DF2` | `0x100233250` | `0x231E9E` |
| `getPlayingTimelineLabelAt` | `0x530AF8` | `0x494E00` | `0x10023326C` | `0x231EB0` |
| `getPlayingTimelineFlagsAt` | `0x530B04` | `0x494E0E` | `0x100233278` | `0x231EBE` |

Each public body resolves the primary `EmoteObject`'s `EmoteEngine`.  On the
64-bit targets the observable chain is the pointer at D3D object `+24`, followed
by the Engine pointer at inner object `+8`; on the 32-bit targets the analogous
offsets are `+16` and `+4`.  The label/flags methods then tail-forward to the
Engine helpers below.  The count methods are compiled inline and read the
corresponding vector directly.

## Engine helper bodies

| Helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `getMainTimelineLabelAt` | `0x672064` | `0x55B45C` | `0x1001AED80` | `0x1AE554` |
| `getDiffTimelineLabelAt` | `0x6720CC` | `0x55B4A4` | `0x1001AEDE4` | `0x1AE5A0` |
| `getPlayingTimelineLabelAt` | `0x672134` | `0x55B4EC` | `0x1001AEE48` | `0x1AE5EC` |
| `getPlayingTimelineFlagsAt` | `0x67219C` | `0x55B534` | `0x1001AEEAC` | `0x1AE638` |

The Android-arm64 flags helper begins at `0x67219C`, not `0x6721AC`.
`0x6721AC` is inside its prologue.  This distinction mattered when repairing
the IDA function boundary.

## Exact container layouts observed by this cluster

Every label collection is a three-pointer `std::vector<ttstr>` in the source
model.  The helpers use only `begin` and `end`; the capacity pointer immediately
following them is not read by this cluster.

| Role | Android arm64 begin/end | Android armv7 begin/end | iOS arm64 begin/end | iOS armv7 begin/end |
| --- | ---: | ---: | ---: | ---: |
| main labels | `+992/+1000` | `+496/+500` | `+624/+632` | `+312/+316` |
| diff labels | `+1016/+1024` | `+508/+512` | `+648/+656` | `+324/+328` |
| playing labels | `+1040/+1048` | `+520/+524` | `+672/+680` | `+336/+340` |

The timeline-state hash table used by the flags query is at Engine `+936`,
`+468`, `+584`, and `+292` respectively.  After a successful label lookup, the
32-bit play flags are read from the value/node at `+32`, `+24`, `+40`, and
`+20` respectively.  Those differing offsets reflect ABI-dependent pointer and
value layouts; they do not represent different high-level behavior.

The element size is one pointer: eight bytes on arm64 and four bytes on armv7.
Accordingly the count bodies compute `(end - begin) / 8` or
`(end - begin) / 4`.  Returning a label copies the `ttstr` backing pointer and
increments its atomic reference count.  On an out-of-range return all four
targets call their ordinary string-from-literal helper with an empty literal.
Fresh decompilation of those four helpers shows that an empty first code unit
returns a zero backing pointer, so this result is precisely a default,
null-backed `ttstr`; it is not a distinct allocated empty-string object.

## Common pseudocode

```cpp
int count(const vector<ttstr> &labels) {
    return static_cast<int>(labels.size());
}

ttstr getLabel(const vector<ttstr> &labels, uint32_t index) {
    if (index >= labels.size())
        return ttstr();
    return labels[index];
}

int getPlayingFlags(uint32_t index) {
    ttstr label = getLabel(playingLabels, index);
    auto found = timelineStateByLabel.find(label);
    return found == timelineStateByLabel.end() ? 0 : found->second.flags;
}
```

The public C++ method receives a signed `tjs_int`, while the Engine helper uses
a 32-bit unsigned index.  Therefore a negative public index is converted to a
large unsigned value and follows the ordinary out-of-range path.  It never
indexes before `begin`.  `getPlayingTimelineFlagsAt` also returns zero when the
playing vector contains a valid label but the state-table node is absent; it
does not insert a missing node.  It does **not**, however, short-circuit an
out-of-range index: the null-backed empty label is still passed to the HM3
lookup.  If HM3 already contains a null-backed empty-label key, an invalid index
returns that node's flags.  A valid playing-vector entry containing the same
empty label follows the identical lookup path.

## Per-target differences

- arm64 returns `ttstr` through the hidden result pointer in `X8`, while armv7
  passes the hidden result pointer in `R0` and shifts `this`/`index` to
  `R1`/`R2`.
- arm64 performs the backing-string reference increment with exclusive
  acquire/release operations; armv7 surrounds its `ldrex`/`strex` loop with
  data-memory barriers.
- Object offsets, element width, hash-table value offsets, and helper addresses
  differ as tabulated above.
- No semantic difference was found in count arithmetic, unsigned bounds checks,
  empty-string fallback, lookup order, or zero fallback.

## Android-arm64 IDA boundary caveat

The current recovery database still has an IDA tail-chunk ownership defect:
the real Engine flags body beginning at `0x67219C` is attached to the tiny D3D
forwarder at `0x530B04`.  Therefore `lookup_funcs(0x67219C)` reports the wrapper,
and whole-function pseudocode can be contaminated by the wrapper prefix even
though the local label, control flow, calls and raw instructions at the Engine
address are intact.  The earlier statement that this split had been durably
repaired was stale and has been removed.

For the 2026-08-15 re-audit the exact Engine range was disassembled first, its
code items were restored after a bounded redefine attempt, and the data flow was
read directly from that range.  The current MCP mutation surface cannot detach
an existing IDA function tail, so the structural ownership defect remains
explicitly documented in the IDB rather than being hidden.  The other three
targets decompile as independent functions and agree with the Android-arm64
instruction stream.

## Local source comparison

The pre-audit implementation already had the correct high-level operations,
but its internal method names embedded addresses from the old single-binary
analysis and several comments presented Android-arm64 offsets as universal.
The implementation now uses semantic `_guess` helper names.  The relevant
compiled-source address comments were removed, the container comment explicitly
identifies the numeric field suffixes as Android-arm64 layout anchors, and unit
coverage now fixes the following observable boundaries:

- independent main/diff/playing counts and label order;
- negative-index conversion to the unsigned out-of-range path;
- empty-string fallback for all three label getters;
- zero flags for an invalid index when no empty-label state exists;
- existing empty-label flags for both invalid indices and valid empty labels;
- zero flags for a playing label missing from the state table; and
- exact stored flags when the state-table entry exists.
