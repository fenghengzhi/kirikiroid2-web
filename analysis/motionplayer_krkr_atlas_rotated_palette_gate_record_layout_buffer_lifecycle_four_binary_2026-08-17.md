# motionplayer KRKR atlas rotated palette gate, record layout, and pixel-buffer lifecycle four-binary audit

## 1. Scope and corrected conclusions

This slice freshly re-audits the complete per-record decode boundary inside the
four current `reference/binaries/` atlas loaders.  It does not inherit the old
single-`libkrkr2.so` address comments.

The most important result is an unusual cross-record state machine shared by all
four references:

```text
enumerate every icon:
    persistentNode = current icon
    append record(copy of current icon, padded dimensions, source key)

for every record in encounter order:
    hasPalette = persistentNode.Contains("pal")
    allocate current record BGRA
    persistentNode = record.iconNode
    if hasPalette:
        decode current record's pixel resource as byte indexes
        optionally fetch current record's pal and expand indexes
    else:
        decode current record's pixel resource as 32-bit pixels
    scan alpha and possibly discard/shrink
```

The enumeration loop does not reset `persistentNode`.  For nonempty records
`[r0, r1, ..., rn-1]`, the decode-mode sequence is therefore:

```text
[pal(rn-1), pal(r0), pal(r1), ..., pal(rn-2)]
```

Only the Boolean branch mode is rotated.  The owner assignment occurs before
`compress`, `pixel`, and the second `pal` lookup, so all payloads are read from
the current record.  This is not a decompiler variable-reuse artifact: the
pre-assignment `Contains`, old-owner release, current-owner copy/AddRef, and
branch are distinct instruction blocks in every image.

The audit also corrects an older source-structure claim.  Native record member
order is STL-family/platform split rather than common across all four:

- Android old libstdc++: `iconNode`, `rect`, `sourceKey`;
- iOS libc++: `sourceKey`, `rect`, `iconNode`.

The compilers do not reorder ordinary C++ data members, so this is evidence for
a platform/STL-conditioned source definition (or equivalent platform source
variants).  It affects value-vector copy construction and reverse destruction,
not just cosmetic offsets.  The Web libc++ reconstruction now selects the iOS
order through `_LIBCPP_VERSION`.

## 2. Four-image control-flow mapping

| Reference | atlas loader | enumeration overwrite | decode record setup | pre-assignment `Contains("pal")` | current icon owner assignment | alpha scan gate |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x6931C8` | `0x693840` | `0x694284` | `0x694384` | `0x6943CC` | `0x6945EC` |
| Android armeabi-v7a | `0x570F54` | `0x5711A4` | `0x571556` | `0x5715E6` | `0x57160E` | `0x571746` |
| iOS arm64 | `0x1000F4098` | `0x1000F4384` | `0x1000F4888` | `0x1000F499C` | `0x1000F49CC` | `0x1000F4BB8` |
| iOS armv7 | `0xF0BE4` | `0xF0EC0` | `0xF120E` | `0xF12E0` | `0xF130A` | `0xF14B2` |

Enumeration backedges establish that the overwrite sites execute for every
encountered icon rather than only for a requested icon:

| Reference | inner icon backedge | outer group backedge |
|---|---:|---:|
| Android arm64-v8a | `0x693B14 -> 0x693804` | `0x693C3C -> 0x693B90` |
| Android armeabi-v7a | `0x5713F0 -> 0x57118C` | surrounding group loop in the same function |
| iOS arm64 | `0x1000F46FC -> 0x1000F4354` | `0x1000F4728 -> 0x1000F4300` |
| iOS armv7 | `0xF11C4 -> 0xF0E9E` | `0xF11DE -> 0xF0E54` |

The AArch64 post-pack metadata assignment at `0x693D28` uses the same holder
later in the function but is not the enumeration seed; its earlier accidental
annotation was corrected in-place while the downstream publication block was
audited.  The Android armv7 compiler combines more of the outer iterator state in the
surrounding block, but its final persistent-owner write and later pal call use
the same stack holder, with no intervening clear.

## 3. Exact record layouts and the stale-comment correction

### 3.1 Android / old libstdc++

| Field | arm64 offset | armv7 offset |
|---|---:|---:|
| `iconNode.owner` | `+0x00` | `+0x00` |
| `iconNode.raw` | `+0x08` | `+0x04` |
| `rect_xywhf` | `+0x10` | `+0x08` |
| `rect.record` | `+0x20` | `+0x18` |
| `rect.contentWidth` | `+0x28` | `+0x1C` |
| `rect.contentHeight` | `+0x2C` | `+0x20` |
| `rect.bgra` | `+0x30` | `+0x24` |
| `sourceKey` | `+0x38` | `+0x28` |
| total stride | `0x40` | `0x2C` |

Value-vector construction/copy order is raw node, rect, then string.  Normal
reverse destruction destroys the string before releasing the raw-node owner.

### 3.2 iOS / libc++

| Field | arm64 offset | armv7 offset |
|---|---:|---:|
| `sourceKey` | `+0x00` | `+0x00` |
| `rect_xywhf` | `+0x18` | `+0x0C` |
| `rect.record` | `+0x28` | `+0x1C` |
| `rect.contentWidth` | `+0x30` | `+0x20` |
| `rect.contentHeight` | `+0x34` | `+0x24` |
| `rect.bgra` | `+0x38` | `+0x28` |
| `iconNode.owner` | `+0x40` | `+0x2C` |
| `iconNode.raw` | `+0x48` | `+0x30` |
| total stride | `0x50` | `0x34` |

Value-vector construction/copy order is string, rect, then raw node.  Reverse
destruction releases the raw-node owner before destroying a heap-backed string.
The iOS arm64 unwind cleanup visibly reads the owner at the record tail and
releases it before testing/freeing the string at the record head.

The previous claim that all four native records were
`PSBRawNode + rect + std::string` was therefore too broad.  The equal record
strides had hidden different endpoint-member placement.

### 3.3 Web layout after correction

Emscripten uses libc++.  The rebuilt Web object's DWARF now reports:

```text
KrkrAtlasRecord_guess byte_size = 0x34
sourceKey                         = +0x00
rect                              = +0x0C
iconNode                          = +0x2C
```

This exactly matches the iOS armv7 libc++ layout, including construction and
reverse-destruction order, while the non-libc++ branch retains the Android
source order.

## 4. Persistent-node ownership and call ordering

Before enumeration, the holder is used by the requested group/icon probe.  Each
enumerated icon assignment then:

1. releases the previous owner held by the shared node;
2. copies the enumerated icon's owner/raw pair;
3. AddRefs the new owner;
4. copies the same node into the appended record, adding another owner reference.

After a nonempty enumeration, the final record and the shared holder both keep
the last icon's PSB backing alive.  No clear occurs before pass 2.

Each decode call performs this order:

1. default-construct the per-call scratch raw node;
2. call `persistentNode.Contains("pal")` and save the result;
3. compute dimensions and allocate the record's BGRA buffer;
4. release the old persistent owner;
5. copy/AddRef the current record's owner and raw payload;
6. branch on the result saved in step 2.

The record's own raw-node member is an independent owner, so release-first
assignment remains backed even for the single-record case where the old and new
holders refer to the same PSB owner.  The decode call's scratch releases its
last successful lookup result on exit.

Special cardinalities:

- zero records: the decode loop does not execute, so the seed is unobserved;
- one record: final-icon seed and current record are the same icon, so the
  rotation is behaviorally invisible;
- two or more records: the final record controls the first branch, then each
  previous record controls the next branch.

The ordering is the same in both Android's inlined code and iOS's helper-heavy
code.

## 5. Palette and non-palette data flow

### 5.1 Palette-mode branch

When the old node's pal test is true, the current record is interpreted as byte
indexes:

1. allocate a temporary `pixelCountW32`-byte index buffer, aligned to 4;
2. query current `compress`;
3. if it equals `"RL"`, run unchecked byte-RL expansion;
4. otherwise copy the current pixel resource's reported byte size, not the
   destination pixel count;
5. separately try-get current `pal`;
6. on hit, size a 32-bit palette vector from signed resource size divided by 4,
   reverse RGB, and expand current indexes into current BGRA;
7. free the index buffer unconditionally.

`Contains("pal")` and try-get `pal` are deliberately distinct and operate on
different logical nodes because assignment lies between them.  If the previous
icon has a palette but the current icon does not, the code still decodes current
pixels into indexes; the second lookup simply fails, leaving BGRA uninitialized
before the alpha scan.  There is no fallback to 32-bit decode.

### 5.2 Non-palette branch

When the old node's pal test is false, current pixels are treated as 32-bit
pixels:

- `compress == "RL"`: unchecked RL32 expansion followed by in-place RGB
  reversal;
- otherwise: RGB reversal copies `pixelCountS32` current 32-bit pixels directly
  from the resource, without using its reported byte size as a bound.

If the current icon actually has a palette but the previous icon did not, its
byte-index payload is therefore consumed through this 32-bit path.  The native
code does not repair the mismatch or re-test the current node to select mode.

### 5.3 RL helper placement

| Reference | byte RL | 32-bit RL |
|---|---|---|
| Android arm64-v8a | inlined in atlas loader | inlined in atlas loader |
| Android armeabi-v7a | inlined in atlas loader | retained helper `0x571DA4` |
| iOS arm64 | retained helper `0x1000F5510` | retained helper `0x1000F5474` |
| iOS armv7 | retained helper `0xF1F6A` | retained helper `0xF1F10` |

All forms reinterpret the 32-bit source size as signed.  A high-bit-set size
skips RL work.  Positive streams have no packet-boundary or output-capacity
checks; literal/run packets may over-read input and overrun the caller's buffer.

## 6. Dimension arithmetic and allocation boundary

`contentWidth` and `contentHeight` are signed 32-bit values derived from padded
rect dimensions minus one.  The decoder sign-extends both and forms a signed
64-bit product:

```text
pixelCount64  = int64(contentWidth) * int64(contentHeight)
pixelCountW32 = low32(pixelCount64)
pixelCountS32 = int32(pixelCountW32)
bgraBytes     = low32(pixelCountW32 << 2)
```

BGRA is allocated even for zero/negative/truncated sizes.  The palette index
allocation uses `pixelCountW32` bytes.  There is no checked multiplication or
null-allocation guard before either buffer is used.

This creates native wrap boundaries that the reconstruction deliberately keeps:

- high product bits are discarded for allocation;
- the color helpers receive signed low-32 pixel count;
- the alpha scan is entered only when signed low-32 count is positive;
- once entered, its loop limit is the full signed 64-bit product, not the
  truncated allocation count.

Large/corrupt dimensions can therefore make scan length and allocation length
disagree.  This is a recovered boundary, not a recommendation for safe input
handling.

## 7. Alpha scan and all-transparent shrink

After either decode branch, all four loaders scan byte `3` of every BGRA pixel.
The first nonzero alpha preserves the buffer and padded rect.

If no nonzero alpha is observed, including when `pixelCountS32 <= 0`, the code:

1. frees `rect.bgra`;
2. writes `rect.bgra = nullptr`;
3. writes packed `rect.w = 2` and `rect.h = 2`;
4. leaves `contentWidth` and `contentHeight` unchanged.

The later atlas upload checks only `rect.bgra != nullptr`.  Transparent records
still publish metadata and occupy a 2x2 packed rectangle, but skip texture
`Update`.

For visible records, upload uses `contentWidth * 4` as pitch, passes inclusive
packed right/bottom coordinates through the existing descriptor path, and then
frees BGRA without clearing the pointer.  Record destruction never owns/frees
BGRA, so normal execution avoids double-free; exceptions before upload can leak
it, and post-upload records retain a stale nonnull pointer that cleanup ignores.

## 8. Container and phase topology

The atlas builder uses two separate value phases:

1. enumerate all groups/icons and finish every potentially reallocating
   `vector<KrkrAtlasRecord_guess>` append;
2. iterate the stable record range, publish rect-subobject pointers into a
   second vector, and decode in encounter order.

This prevents record-vector growth from invalidating pointers already handed to
`ImagePacker`.  Each rect holds an explicit back-pointer to its complete record.

The final enumeration order comes from the PSB dictionary key vectors: the last
icon of the last encountered nonempty group supplies the first mode seed.  It is
not necessarily the requested icon and is unrelated to atlas packing order.

After decode, `ImagePacker::pack` receives rect pointers.  Its Boolean return is
ignored.  Per-page texture construction, cache publication, metadata reads,
optional clip descent, upload, buffer free, and normal-path texture release keep
the exception/lifetime behavior recovered in the earlier atlas audit.

## 9. Portable-source changes

`cpp/plugins/motionplayer/PlayerResource.cpp` now:

- documents the exact rotated gate sequence and explicitly distinguishes mode
  source from payload source;
- preserves the final enumerated icon as the first decode seed;
- replaces the stale common-member-order comment with the two native layouts;
- selects `sourceKey/rect/iconNode` under `_LIBCPP_VERSION` and
  `iconNode/rect/sourceKey` otherwise;
- orders constructor initializers to match the selected declaration/copy order;
- retains all unchecked arithmetic, buffer, RL, pal-miss, alpha-shrink, and
  stale-post-upload-pointer behavior.

The operational change is confined to aggregate member/copy/destruction order;
the decoded results on ordinary valid inputs remain governed by the already
present rotated gate logic.

## 10. Recovery-IDB writeback

All four databases were handled sequentially, saved, and closed.  V224 plus the
immediate AArch64 site correction made during the downstream re-open added:

- 41 comments: nine control/data/buffer comments per image, one exact
  record-layout/destruction-order correction per image, and the corrected
  AArch64 enumeration site (the wrong post-pack comment was replaced in place);
- 21 bookmarks: four state-machine landmarks plus one record-layout landmark
  per image, with the wrong AArch64 bookmark relabeled and the real seed added;
- no speculative function renames;
- no unnecessary type applications.

The final session audit reports zero open IDA sessions.

## 11. Validation and products

- the dedicated `motionplayer-dll.cpp` translation unit passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation;
- Web rebuilt `PlayerResource.cpp`, the motionplayer archive, and final link in
  3 steps;
- Wasmtime rebuilt the guest and plugin `PlayerResource.cpp` objects, archive,
  and final link in 4 steps;
- DWARF inspection proves the Web libc++ record is size `0x34` with
  `sourceKey +0x00`, `rect +0x0C`, and `iconNode +0x2C`;
- both CTest trees remain configured without registered tests and report
  `No tests were found`;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- `git diff --check` passes apart from the existing LF-to-CRLF worktree warning.

The layout correction removes 37 bytes from each CODE section/final module;
all non-CODE section sizes below remain unchanged from V223.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,756 B | `1054673E8D3149F7EBEF69C6F7DCCA6497A81B265EF6873F63E6F5E8E9F5C041` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,897 B | `541D3EF4FD82CFEA9C66BF344A25EA2C57A69AF8D309344C0E22EAEBB3C300F1` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41A90` | `0x19E9A3E` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E4E` | `0x3141CE4` |

## 12. Limits

- the current CMake trees do not execute the Catch2 production-atlas test, so
  validation here is compile/link/module/DWARF based rather than a newly run
  fixture assertion;
- the four references prove the rotated mode gate, but do not make malformed
  mixed-mode resources memory-safe; several mismatches intentionally reach
  uninitialized or out-of-bounds behavior;
- `_LIBCPP_VERSION` reproduces the observed STL-family split for the current
  Web toolchain and references; an unobserved third STL/platform would require
  its own binary evidence rather than inference;
- this closes one KRKR atlas decode/layout vertical slice and does not complete
  the full motionplayer recovery goal.
