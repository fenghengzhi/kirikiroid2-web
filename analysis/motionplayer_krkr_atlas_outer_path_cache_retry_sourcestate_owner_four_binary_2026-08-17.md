# motionplayer KRKR atlas outer path, cache retry, SourceState, and root-owner four-binary audit

## 1. Scope and conclusion

V224 recovered KRKR atlas record enumeration/decoding and V225 recovered pack,
entry publication, upload, and page ownership.  V226 closes the outer loader
that surrounds those two slices: source-path admission, loaded-module lookup,
PSB-root ownership, the first atlas-cache probe, miss-build argument formation,
`SourceState::valid` timing, the unchecked second lookup, and successful
descriptor projection.

All four references implement the following native-shaped flow:

```text
pieces = split(retained_copy(source.path), '/')
if pieces[0] != "src":
    return false                         // SourceState unchanged

module = loadedModules.find(moduleKey)   // moduleKey is a separate argument
if module == end:
    source.valid = false
    return false                         // source.object is not cleared

source.object.Clear()
root = PSBRawNode(module.file)           // retained even on cache hit
key = reference_to_live(source.path)     // retained ttstr field, not a snapshot
entry = module.krkrSourceEntries.find(key)
if entry == end:
    group = narrow(pieces[1])            // no path-length check
    icon  = narrow(pieces[2])            // no path-length check
    sourceRoot = root.strict("source")
    source.valid = false                 // only after strict sourceRoot
    if ordinary group/icon build failure:
        return false
    entry = module.krkrSourceEntries.find(key) // re-reads live path after callbacks
                                             // result not guarded

source.valid = true
source.originX/Y = entry.originX/Y
source.blank = false
source.width/height = entry inclusive-rect extents
source.clip = entry.clip
source.textureRect = entry.textureRect
source.texture = entry.texture           // borrowed; no AddRef
return true
// root owner is released during return cleanup after projection
```

V226 corrected two source mismatches:

1. the PSB root owner is now constructed in the outer loader before the first
   cache probe, so it exists on cache hits as well as cache misses;
2. `source.valid=false` is now written only after strict `root["source"]`
   acquisition, and before ordinary group/icon build failures.

The malformed short-path boundary, strict-root exception timing, pack-success
contract, unchecked retry, and borrowed texture lifetime intentionally remain
visible rather than being made safer than the references.

V231 later closed the trailing-path representation that V226 left portable:
`SourceState.path` is a retained `ttstr`, the split owns an entry snapshot, and
both cache lookups use a reference to the persistent live field. Atlas-building
callbacks may therefore replace the retry key without changing `pieces`.

## 2. Four-image mapping

| Semantic boundary | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| outer atlas loader | `0x6931C8` | `0x570F54` | `0x1000F4098` | `0xF0BE4` |
| split source path | `0x69323C` | `0x570FA2` | `0x1000F4114` | `0xF0C78` |
| sole `src` prefix comparison | `0x693270` | `0x570FC0` | `0x1000F4138` | `0xF0C9E` |
| module lookup | `0x693368` | `0x570FCC` | `0x1000F4144` | `0xF0CAA` |
| module-miss `valid=false` | `0x6933F4` | `0x571084` | `0x1000F4208` | `0xF0D72` |
| `source.object.Clear()` | `0x69337C` | `0x570FD6` | `0x1000F415C` | `0xF0CC2` |
| root-owner retain begins | `0x69338C` | `0x570FDA` | `0x1000F4168` | `0xF0CCE` |
| guarded first cache find | `0x693438` | `0x570FF6` | `0x1000F4178` | `0xF0CE0` |
| unchecked `pieces[1]/[2]` conversion | `0x6934A0/0x6934B0` | `0x571090/0x57109C` | `0x1000F421C/0x1000F422C` | `0xF0D82/0xF0D92` |
| strict `root["source"]` | `0x6934C8` | `0x5710B0` | `0x1000F4244` | `0xF0DB4` |
| miss-path `valid=false` | `0x6934CC` | `0x5710B4` | `0x1000F4248` | `0xF0DBE` |
| unchecked second cache find | `0x693654` | `0x571002` | `0x1000F4188` | `0xF0CF0` |
| success `valid=true` | `0x69366C` | `0x57100A` | `0x1000F4190` | `0xF0CFC` |
| borrowed texture store | `0x6936B4` | `0x571074` | `0x1000F41F4` | `0xF0D62` |
| root-owner return cleanup | `0x6936B8` | `0x571484` | `0x1000F4798` | `0xF15A8` |

The module lookup key is the caller-supplied motion/module context argument.
It is independent of the split source path.  A transient V226 IDB annotation
that called it `pieces[3]` was caught during source/parameter reconciliation,
replaced in all four databases, and is not part of the final evidence set.

## 3. Path admission and malformed segment boundary

The loader has one path-shape test: the first slash-separated component must
equal wide string `src`.  There is no complete `src/group/icon` length check.

The failure boundaries are deliberately asymmetric:

- prefix mismatch returns `false` before module lookup and does not modify any
  `SourceState` field;
- a loaded-module cache hit needs no group or icon component because the
  already-complete raw path is the cache key;
- a cache miss converts components one and two without first checking vector
  size;
- therefore `src` or `src/group` can pass the prefix gate and remain harmless
  only if the exact full path is already cached; otherwise the loader crosses
  the split-vector boundary while forming miss-build arguments.

The local reconstruction retains the direct `pieces[1]`/`pieces[2]` accesses.
It does not introduce a three-component validation that would change this
cache-dependent malformed-input behavior.

## 4. Loaded-module lookup and early mutation order

Module lookup occurs after the prefix comparison and before clearing the
generic source object.  It uses the separate `moduleKey` parameter supplied by
the caller, not any path component.

On a module miss all four references:

1. write only `source.valid=false`;
2. leave `source.object` untouched;
3. leave the texture, dimensions, origins, clip, and rect untouched;
4. return `false`.

On a module hit they instead:

1. clear `source.object`;
2. retain/construct a `PSBRawNode` root from the loaded file;
3. retain a reference to the complete live source-path field as the cache key;
4. probe the persistent KRKR atlas-entry map.

Thus an exception from root construction or key conversion observes an already
cleared object but the previous `valid` byte and all other descriptor fields.
The two failure paths are not equivalent and should not be collapsed into a
single upfront `source.clear()` operation.

## 5. PSB root-owner lifetime

The root raw-node owner is established before the first atlas-cache lookup.
It remains live across:

- a direct cache hit and the complete successful projection;
- cache-miss group/icon conversions;
- strict source-root lookup;
- record enumeration, decode, packing, publication, and upload;
- the second cache lookup and final projection;
- ordinary false returns and exception landing-pad cleanup.

It is released only during the loader's return/unwind cleanup.  A cached entry
does not avoid this retain/release pair.  The previous source construction put
`PSBRawNode root(loadedResource.file)` inside the builder, which matched miss
behavior but skipped the native owner on a hit and changed the owner tree for
key/projection exceptions.

`buildKrkrAtlasGroup_guess` now borrows the already-retained root.  It does not
create a second independent root owner.

## 6. Initial cache lookup and STL-specific find semantics

The first lookup is guarded in all references, but its machine-level shape
depends on the bundled STL. In every ABI the key argument ultimately names the
persistent `SourceState.path` ttstr field; it is not reconstructed from a Web
narrow string and not copied into an entry snapshot:

| ABI/STL | helper | return meaning | code xrefs |
|---|---|---|---:|
| Android arm64 old libstdc++ | `KrkrSourceEntryMap_findBucketPredecessor_guess` | bucket predecessor whose successor may be the node | first probe, second probe, `operator[]` | 3 |
| Android armv7 old libstdc++ | `KrkrSourceEntryMap_find_guess` | matching node or null | first and second probe | 2 |
| iOS arm64 libc++ | `KrkrSourceEntryMap_find_guess` | matching node or null | first and second probe | 2 |
| iOS armv7 libc++ | `KrkrSourceEntryMap_find_guess` | matching node or null | first and second probe | 2 |

The Android arm64 helper accepts map, bucket index, key, and cached hash.  It
walks the old libstdc++ bucket chain and returns the predecessor pointer, which
is also why the V225 `operator[]` helper shares it.  Its initial caller checks
both the predecessor and the successor entry before treating the lookup as a
hit.

The other three helpers return a node directly.  The initial caller checks it
for null.  These differences are container ABI details, not different atlas
cache policies.

## 7. Exact `valid` mutation matrix

`valid` is not reset at function entry.  Its state after each boundary is:

| Boundary | `valid` afterward | `object` afterward | Other atlas fields |
|---|---|---|---|
| `pieces[0] != "src"` | unchanged | unchanged | unchanged |
| loaded module missing | `false` | unchanged | unchanged |
| module hit / object clear / root retain / first probe | unchanged until success | Void/cleared | unchanged |
| unchecked segment conversion throws/faults | prior value | cleared | unchanged |
| strict `root["source"]` throws | prior value | cleared | unchanged |
| strict source root succeeds | `false` | cleared | unchanged |
| group missing | `false` | cleared | unchanged |
| requested icon missing | `false` | cleared | unchanged |
| later ordinary builder failure | `false` | cleared | possibly partially mutated cache, old descriptor fields |
| cache hit or successful retry projection | `true` | cleared | overwritten from persistent entry |

The prior source reset `valid` only in the outer module-miss case and could
return `false` from group/icon build while leaving a stale true value.  The
reconstructed builder now receives `SourceState&` and writes `valid=false`
immediately after strict source-root acquisition, matching all four binaries.

Moving the write earlier than the strict lookup would also be wrong: malformed
or throwing PSB root access preserves the old flag natively.

## 8. Builder success contract and unchecked retry

After the first cache miss, group/icon lookup can return ordinary `false`.
Once the requested icon exists, however, the builder returns `true` after its
pack/publication loops even if `ImagePacker::pack` returned `false`; V225 proved
that pack's Boolean is ignored.

The outer loader treats builder `true` as a publication contract and performs a
second cache lookup.  It does not validate that the requested full key was
actually published before projecting the result:

- Android armv7 and both iOS builds directly dereference a possibly null node;
- Android arm64 checks the returned predecessor pointer, but not the successor
  entry stored through it; a missing successor is converted to zero and then
  dereferenced by projection.

Therefore an oversized pack failure, an empty/unpublished requested entry, or
any other builder-success/no-entry combination reaches an invalid-access
boundary.  The local `sourceIt = find(...); sourceIt->second` intentionally has
no `end()` guard after the retry.

The builder may call texture creation/update code before returning. Those are
re-entry boundaries. If script replaces `SourceState.path`, the retry looks up
the replacement key while group/icon build inputs still come from the entry
split snapshot. A replacement key that was not published reaches the same
unchecked invalid-access boundary; a published replacement redirects the
projected entry.

## 9. Successful `SourceState` projection

The mapped entry supplies every KRKR-specific field.  The successful result is:

- `valid = true`;
- `originX`/`originY`: integer entry fields converted to double;
- `blank = false`;
- `width = textureRect.right - textureRect.left`;
- `height = textureRect.bottom - textureRect.top`;
- clip doubles copied left, top, right, bottom;
- the four inclusive atlas coordinates copied into `SourceState::textureRect`;
- the mapped page texture pointer copied verbatim.

V225 established that the publisher stores inclusive right/bottom as
`x + paddedWidth - 1` and `y + paddedHeight - 1`.  Because padded width/height
are content size plus one, the subtraction above recovers the original content
dimensions exactly.

Independent scalar stores are scheduled slightly differently by the four
optimizers.  For example Android arm64 writes `blank=false` before the width/
height pair, while iOS arm64 schedules width/height before its blank store.
This is not a structural disagreement: all four project the same fields before
the same success return.

The recovered Android arm64 `SourceState` prefix/layout at this call boundary
is:

| Field | offset |
|---|---:|
| `valid`, `blank` | `+0x00`, `+0x01` |
| object Variant | `+0x04` |
| texture pointer | `+0x18` |
| width, height | `+0x20`, `+0x28` |
| originX, originY | `+0x30`, `+0x38` |
| clip left/top/right/bottom | `+0x40..+0x5F` |
| texture rect | `+0x60..+0x6F` |

The portable structure keeps the same logical ordering. Its trailing `path` is
now the native-shaped retained `ttstr`; target offsets are `+0x70` on Android
arm64 and iOS arm64, `+0x68` on Android armv7, and `+0x64` on iOS armv7.

## 10. Borrowed texture lifetime and stale-pointer boundary

Projection does not call `AddRef`.  `SourceState::texture` is a borrowed raw
pointer backed by the persistent mapped atlas entry.  Ownership remains:

```text
LoadedResourceRecord
  -> krkrSourceEntries unordered_map
     -> PackedSourceAtlasEntry
        -> retained page texture

MotionNode::SourceState
  -> borrowed pointer to the same page
```

Consequences:

- node/SourceState destruction must not release the page;
- repeated projection must not add another page reference;
- clearing or destroying the loaded module/cache can invalidate previously
  projected raw texture pointers unless the surrounding call chain refreshes
  or stops consuming them first;
- the temporary PSB root owner is unrelated to texture ownership and is safely
  released after projection.

The local `SourceState` declaration already marks the texture non-owning, and
V226 preserves the direct assignment.

## 11. Portable-source changes

V226 changed only `PlayerResource.cpp`:

- `buildKrkrAtlasGroup_guess` now borrows a `const PSBRawNode& root` supplied by
  the outer loader;
- it also receives `SourceState&` so it can reset `valid` at the native point;
- root construction moved to immediately after module-hit object clear and
  before source-key/cache lookup;
- `source.valid=false` moved to immediately after strict source-root lookup;
- the outer loader passes root and source into the miss builder.

No full SourceState clear, short-path guard, strict-root exception translation,
pack-result check, second-lookup guard, or texture AddRef was introduced.

V231 then changed the representation and live-key boundary:

- `SourceState.path` changed from Web `std::string` to retained `ttstr`;
- spec-1 assignment now copies the live source `ttstr` owner directly;
- split receives `source.path` by value, creating the native owning snapshot;
- both cache finds use `const ttstr& sourceKey = source.path`;
- Web-only prepared-item diagnostics narrow at their explicit sidecar boundary.

## 12. Recovery-IDB writeback

All four databases were processed sequentially, saved, and closed:

- Android arm64 private lookup renamed
  `KrkrSourceEntryMap_findBucketPredecessor_guess` and typed with its four
  map/bucket/key/hash parameters;
- the other three private lookups renamed
  `KrkrSourceEntryMap_find_guess` and typed as map/key node finds;
- 48 V226 line comments were added (12 per database);
- 24 V226 bookmarks were added (6 per database);
- the four transient wrong module-key comments were replaced in place with the
  caller-supplied-module-key mapping;
- the final session audit reports zero open IDA sessions.

## 13. Validation and products

- `motionplayer-dll.cpp` passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation;
- Web rebuilt three affected steps and linked successfully;
- Wasmtime rebuilt four affected plugin/guest steps and linked successfully;
- both CTest trees remain configured with no registered tests;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`.

Relative to V225, moving the root owner and exact valid write adds 117 bytes to
each module: CODE grows by `0x33` and the name section by `0x42`; the listed
FUNCTION, GLOBAL, and DATA sections remain unchanged.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,848 B | `5AB9E62A139A2EB9567A2F577F830DFAF91D680546E84E4122901B3340A4F08A` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,989 B | `F50CBF2BFB5F178CC2108C4EF165D7FA0687D5A45ED06806963867828176C543` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41AAA` | `0x19E9A58` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E90` | `0x3141D26` |

## 14. Limits

- this slice recovers the outer KRKR atlas loader but not yet every caller that
  reaches it or every render-time consequence of a borrowed/stale texture;
- malformed vector access and retry invalid access are native boundary
  behaviors and are not executed by a new crashing fixture in this slice;
- the optimizer can reorder independent scalar projection stores, so machine
  address order is not treated as proof of an original source statement order;
- this does not complete the full motionplayer recovery goal.
