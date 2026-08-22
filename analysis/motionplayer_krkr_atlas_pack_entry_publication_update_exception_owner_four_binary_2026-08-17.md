# motionplayer KRKR atlas pack, entry publication, Update identity, and exception-owner four-binary audit

## 1. Scope and conclusion

V224 closed record enumeration and decode.  This slice follows the same records
through `ImagePacker`, page texture construction, the persistent KRKR source
map, metadata publication, texture upload, and normal/exception cleanup.

All four references share this exact high-level order:

```text
bins = {}
ImagePacker::pack(rectPointers, count, maxSide, bins) // bool ignored

for bin in bins:
    page = RenderManager.CreateTexture2D(...)
    for rect in bin.rects:
        record = rect.record
        metadataNode = record.iconNode
        entry = krkrSourceEntries[widen(record.sourceKey)]
        entry.setTexture(page)
        entry.originX = strict(metadataNode["originX"])
        entry.originY = strict(metadataNode["originY"])
        entry.textureRect = packed inclusive rect
        if metadataNode.tryGet("clip", metadataNode):
            entry.clip[0..3] = strict left/top/right/bottom, one by one
        else:
            entry.clip = {0, 0, 1, 1}
        if record.bgra != null:
            page.Update(record.bgra, RGBA, contentWidth * 4,
                        entry.textureRect)
            alignedFree(record.bgra) // record field is not cleared
    page.Release() // construction reference, normal page tail only
```

Two local mismatches were corrected:

1. the current record's raw node must be copied before source-key conversion
   and map `operator[]`, not after the entry has already been found/published;
2. the mapped descriptor contains an actual `tTVPRect` subobject and passes
   that exact in-map object by reference to `Texture::Update`; the native call
   does not construct a temporary rectangle from duplicate coordinates.

The ignored pack result, zero-valued new descriptor, incremental/partial
metadata commits, page texture reference leak on unwind, and BGRA stale-pointer
boundary are common to all four references and remain preserved.

## 2. Four-image mapping

| Semantic boundary | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| atlas loader | `0x6931C8` | `0x570F54` | `0x1000F4098` | `0xF0BE4` |
| `ImagePacker::pack` helper | `0xA6DA58` | `0x79436C` | `0x100054E20` | `0x53EB8` |
| pack call / ignored result | `0x693C94` | `0x5717A8` | `0x1000F4C30` | `0xF15FA` |
| page `CreateTexture2D` virtual call | `0x693CD4` | `0x5717D4` | `0x1000F4C70` | `0xF1636` |
| current icon owner assignment | `0x693D28` | `0x5717F8` | `0x1000F4C9C` | `0xF165C` |
| KRKR map `operator[]` helper | `0x6DF954` | `0x5A0748` | `0x1000F5598` | `0xF1FC0` |
| `operator[]` call | `0x693D58` | `0x57181C` | `0x1000F4CDC` | `0xF168E` |
| entry texture identity/replacement gate | `0x693D7C` | `0x571838` | `0x1000F4CF4` | `0xF16A4` |
| first metadata write (`originX`) | `0x693DB4` | `0x57185C` | `0x1000F4D2C` | `0xF16D8` |
| in-place `clip` lookup | `0x693E74` | `0x5718C6` | `0x1000F4DB4` | `0xF1756` |
| `Texture::Update` call | `0x693FC8` | `0x571980` | `0x1000F4E98` | `0xF185A` |
| post-Update BGRA free | `0x693FEC` | `0x571984` | `0x1000F4EA0` | `0xF1864` |
| normal page construction-ref `Release` | `0x694000` | `0x57199E` | `0x1000F4EBC` | `0xF1880` |

The private map helpers each have exactly one code caller: their corresponding
atlas publication site.

## 3. `ImagePacker::pack` result and empty/failure behavior

Fresh decompilation of all four pack helpers confirms:

- every input rect is rejected only when width or height is greater than
  `maxSide`; equality to `maxSide` is allowed;
- the first oversized rect returns `false` before appending a bin;
- a zero record count takes the success path, appends one default 0x0 bin, and
  returns `true`;
- successful nonempty packing appends bins and returns `true`;
- the atlas caller never branches on the return value.

On AArch64, the decompiler appears to pass the returned Boolean into the next
render-manager getter because the result remains in the first argument
register.  The getter has a verified zero-argument signature and ignores that
register; this is still a discarded pack result, not a semantic argument.

Consequences:

- oversized failure leaves the freshly constructed `bins` vector empty, so no
  texture or cache entry is created;
- the builder nevertheless reaches its ordinary success return;
- the outer loader's later second map lookup remains responsible for the native
  missing-entry invalid-access boundary;
- empty record input creates a 0x0 page texture, runs zero rect iterations, and
  releases the page construction reference normally.

No port-invented `if (!pack(...)) return false` or empty-bin suppression belongs
in this path.

## 4. Page texture construction and ownership

For each bin the native virtual call receives:

- null initial pixel data;
- pitch `low32(bin.width << 2)`;
- bin width and height;
- format value `4` (`RGBA` in the reconstructed interface);
- static/create flag value `1`.

The returned page pointer is not null-checked.  It owns one construction
reference.  Each entry whose texture is changed to this page takes another
reference, but the construction reference remains a raw local until the normal
end of the page loop.

Searching every use of the page local/register in each decompilation finds only:

1. entry identity comparison/replacement;
2. the `Update` virtual call;
3. the normal page-tail `Release`.

No landing pad or vector/raw-node cleanup path references it.  Therefore:

- an exception before the first entry retains the page's sole construction
  reference and leaks the whole page;
- an exception after one or more `setTexture` operations leaves those cache
  entries owning the page and also leaks one extra construction reference;
- an exception from `Update` likewise skips page-tail release and the following
  BGRA free.

The local reconstruction deliberately does not wrap the page in a scope guard.

## 5. Raw-node assignment precedes map publication

Every reference first release-assigns the current packed record's `iconNode`
into the shared metadata scratch.  Only afterward does it:

1. convert `sourceKey` from narrow string to `ttstr`;
2. call KRKR map `operator[]`;
3. release/destroy the temporary key;
4. change the entry's texture;
5. read metadata through the scratch node.

The former local order performed `operator[]` and `setTexture` before the raw
assignment.  Ordinary valid output was usually the same, but allocation,
rehash, or key-conversion unwind observed the wrong scratch owner and the map
entry could already retain the page earlier than in native code.

`PlayerResource.cpp` now establishes `iconNode = record->iconNode` before the
key expression and `operator[]`, matching all four owner trees.

## 6. KRKR entry/map node layouts and zero initialization

The four `operator[]` helpers compute/reuse the `ttstr` Hint hash, perform
bucket/equality lookup, and return an existing mapped value on hit.  On miss
they allocate a node, retain the key backing, and zero the complete mapped
descriptor before linking/publishing the node.

### 6.1 Unordered node layout

| ABI/STL | node size | header/key | mapped entry | cached hash |
|---|---:|---|---|---|
| Android arm64 old libstdc++ | `0x58` | next `+0x00`, key `+0x08` | `+0x10`, size `0x40` | `+0x50` |
| Android armv7 old libstdc++ | `0x58` | next `+0x00`, pad `+0x04`, key `+0x08` | `+0x10`, size `0x40` | tail `+0x50` |
| iOS arm64 libc++ | `0x58` | next `+0x00`, hash `+0x08`, key `+0x10` | `+0x18`, size `0x40` | header `+0x08` |
| iOS armv7 libc++ | `0x48` | next `+0x00`, hash `+0x04`, key `+0x08` | `+0x0C`, size `0x3C` | header `+0x04` |

### 6.2 Mapped descriptor layout

| Field | LP64 | Android armv7 | iOS armv7 |
|---|---:|---:|---:|
| texture | `+0x00` | `+0x00` | `+0x00` |
| originX / originY | `+0x08/+0x0C` | `+0x04/+0x08` | `+0x04/+0x08` |
| `tTVPRect textureRect` | `+0x10` | `+0x0C` | `+0x0C` |
| alignment padding | none | `+0x1C..+0x1F` | none |
| clip doubles | `+0x20` | `+0x20` | `+0x1C` |
| total mapped size | `0x40` | `0x40` | `0x3C` |

Thus a newly inserted entry starts with null texture and all-zero origin, rect,
and clip.  The no-clip `{0,0,1,1}` value is written later by the publisher; it
is not the mapped default.  Any exception before that branch leaves the clip
prefix/all zeros observable in the persistent node.

The rebuilt wasm32 object reports `PackedSourceAtlasEntry` size `0x40`, with
texture `+0`, origins `+4/+8`, `tTVPRect +0x0C`, clip `+0x20`.  Its double
alignment matches Android armv7 at the tail while the rect identity/offset
matches both 32-bit references.

## 7. Texture replacement and incremental metadata commit

`setTexture` is identity-guarded:

```text
if entry.texture != page:
    if entry.texture != null: entry.texture.Release()
    entry.texture = page
    if page != null: page.AddRef()
```

It is executed after the persistent node has been found/inserted.  Metadata then
commits directly into that entry in this order:

1. `originX`;
2. `originY`;
3. rect left/top/right/bottom (right and bottom inclusive);
4. clip lookup and clip fields.

There is no temporary complete descriptor and no final transactional
`insert_or_assign`.  A strict getter/conversion exception retains the entry,
its page reference, and every field already written.

The `clip` lookup passes the same raw-node object as source and destination.  A
hit descends it in place; a miss preserves the current icon node.  On hit the
four double fields are requested and written one at a time, so any strict
failure preserves a partial clip prefix.  On miss the publisher writes exact
`{0.0, 0.0, 1.0, 1.0}`.

## 8. Exact `Update` rectangle identity and BGRA lifecycle

The Update call's final argument points inside the mapped descriptor:

- LP64: `entry + 0x10`;
- ILP32: `entry + 0x0C`.

This is the same `tTVPRect` whose fields were just published.  It is not a stack
temporary.  The local `PackedSourceAtlasEntry::textureRect` is now a real
`tTVPRect`, and the virtual call receives `entry.textureRect` directly.

Other arguments are:

- current record BGRA pointer;
- RGBA format value `4`;
- signed low-32 `contentWidth * 4` pitch.

If BGRA is null, both Update and free are skipped.  Otherwise the code calls
Update and then aligned-free on the record's buffer.  The record field is not
cleared, and its destructor does not own the buffer.  This produces three
distinct boundaries:

- successful upload: buffer freed, record retains a stale nonnull pointer that
  cleanup ignores;
- Update exception: buffer remains allocated and record cleanup still ignores
  it, so it leaks;
- transparent record: V224 already freed and cleared the pointer, so upload
  safely skips it.

## 9. Portable-source changes

- `ResourceManager.h` now includes the native rectangle definition and stores
  `PackedSourceAtlasEntry::textureRect` as `tTVPRect` rather than
  `std::array<int,4>`;
- the field retains explicit zero initialization, preserving `operator[]`'s
  all-zero mapped default;
- `PlayerResource.cpp` moves current-record raw-node assignment before key
  conversion/map publication;
- rect publication writes a `tTVPRect`;
- Update receives the exact in-entry rect object;
- final source descriptor copy explicitly projects the rect's four named
  coordinates into `SourceState`'s array.

No pack-result guard, texture scope owner, bulk metadata commit, BGRA cleanup
guard, or pointer clear was added.

## 10. Recovery-IDB writeback

All four databases were processed sequentially, saved, and closed:

- 8 `_guess` renames: four pack helpers and four private KRKR map subscript
  helpers;
- 8 safe function type applications;
- 40 publication comments plus the immediate AArch64 V224 site correction;
- 20 V225 bookmarks, with the corrected AArch64 metadata bookmark replacing
  the prior wrong seed label and the real seed retained separately.

The final IDA session audit reports zero open sessions.

## 11. Validation and products

- `motionplayer-dll.cpp` passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation;
- Web rebuilt 34 affected steps and linked successfully;
- Wasmtime rebuilt 64 affected plugin/guest steps and linked successfully;
- DWARF confirms the Web entry is size `0x40`, with texture `+0`, origins
  `+4/+8`, `tTVPRect +0x0C`, and clip `+0x20`;
- both CTest trees remain configured with no registered tests;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- `git diff --check` passes apart from existing LF-to-CRLF worktree warnings.

Relative to V224, the corrected call/owner ordering removes another 25 bytes
from each CODE section/module.  Other listed sections are unchanged.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,731 B | `21243C58BA0127D7E84DCE2ABEAC7BCB8F521BFEDE472B1BD9DA9D0B2206FB92` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,872 B | `06525F52E0D1D99B377CFB37308B7CE33A814B61E12B2C68C6DFF12968DBEF75` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41A77` | `0x19E9A25` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E4E` | `0x3141CE4` |

## 12. Limits

- the active build trees do not execute the existing Catch2 production-atlas
  test, so this slice is validated by compile/link/module/DWARF checks rather
  than a newly run fixture;
- failure/leak behavior is recovered from native ordering and landing-pad use,
  but intentionally is not made memory-safe for malformed data or throwing
  collaborators;
- the ignored-pack-result consequence in the outer second lookup is adjacent
  but belongs to the next loader-output boundary rather than this page
  publication slice;
- this does not complete the full motionplayer recovery goal.
