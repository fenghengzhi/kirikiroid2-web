# motionplayer Win source root owner, live-key texture cache, icon projection, and re-entry four-binary audit

## 1. Scope and conclusion

V228 recovered spec routing and V229 closed generic fallback projection. V230
audits the other successful spec-2 branch: loaded Win PSB module -> source group
-> lazy texture cache -> live icon -> persistent `SourceState` projection.

Fresh four-image decompile/disassembly yields this common structure:

```text
source.object.Clear()
moduleKey = ttstr(contextVariant)
loaded = resourceManager.loadedModules.find(moduleKey)
if loaded missing:
    source.valid = false
    goto genericFallback

root = PSBRawNode(loaded.file)                 // independent retained owner
sourceRoot = root["source"]                    // strict
utf8Group = UTF8(liveSrc)
groupFound = sourceRoot.tryGet(utf8Group, groupNode)
destroy utf8Group
destroy sourceRoot

if !groupFound:
    destroy groupNode
    destroy root
    goto genericFallback                       // no new valid write

cacheIt = loaded.winTextures.find(liveSrc)    // original ttstr object
if cacheIt found:
    source.texture = cacheIt.texture           // borrowed; no AddRef
else:
    textureNode = groupNode["texture"]         // strict raw owner
    discard int(textureNode["truncated_width"])
    discard int(textureNode["truncated_height"])
    width  = int(textureNode["width"])
    height = int(textureNode["height"])
    type   = string(textureNode["type"])
    pixels = resource(textureNode["pixel"], &sourceSize)

    bgra = alignedAlloc(wrap32(width * 4 * height), 4)
    convert RGBA8 or A8L8 into bgra
    texture = renderManager.CreateTexture2D(...)
    alignedFree(bgra)

    slot = loaded.winTextures[liveSrc]         // re-read after callback
    slot.setTexture(texture)                    // release old, AddRef new
    texture.Release()                           // construction ref, normal path
    source.texture = texture
    destroy textureNode

iconRoot = groupNode["icon"]                  // strict temporary owner
utf8Icon = UTF8(liveIcon)                      // after all texture callbacks
iconNode = iconRoot[utf8Icon]                  // strict
destroy utf8Icon
destroy iconRoot

source.valid = true
source.originX = int(iconNode["originX"])
source.originY = int(iconNode["originY"])
source.width   = int(iconNode["width"])
source.height  = int(iconNode["height"])
source.blank = false
source.clip = [0, 0, 1, 1]
left = int(iconNode["left"])
top  = int(iconNode["top"])
source.rect = [
    left,
    top,
    fpToSigned(source.width  + double(left)),
    fpToSigned(source.height + double(top))
]

destroy iconNode
destroy groupNode
destroy root
return
```

Three concrete local mismatches were corrected:

- the native root owner remains independently retained across the full branch;
  the former helper destroyed it immediately after group lookup;
- both cache `find` and post-render `operator[]` borrow the same live source
  `ttstr`, preserving its backing/cached hash and re-reading it after callback
  re-entry; the former narrow-then-widen code froze a new key snapshot;
- rectangle extents add left/top in floating point before integer conversion;
  the former code narrowed width/height first and then performed signed integer
  addition.

The icon dictionary/key temporary lifetimes are also restored: both end before
`valid=true`, while the selected icon node remains alive through projection.

## 2. Four-image mapping

| Semantic boundary | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| independent root owner | `0x691E94` | `0x5705B8` | `0x1000F3274` | `0xEFAA4` |
| strict root `source` | `0x691EC0` | `0x5705D4` | `0x1000F328C` | `0xEFAC6` |
| live src -> UTF-8 | `0x691ECC` | `0x5705DC` | `0x1000F329C` | `0xEFAD2` |
| nonthrowing group lookup | `0x691EE0` | `0x5705EA` | `0x1000F32BC` | `0xEFAF4` |
| live-key cache `find` | `0x692040` | `0x570606` | `0x1000F32EC` | `0xEFB20` |
| cache-hit texture commit | `0x692054` | `0x570612` | `0x1000F32F8` | `0xEFB2C` |
| strict `texture` node | `0x69206C` | `0x570690` | `0x1000F3394` | `0xEFBD4` |
| pixel resource borrow | `0x6921F4` | `0x570736` | `0x1000F348C` | `0xEFCC8` |
| aligned BGRA allocation | `0x692238` | `0x570754` | `0x1000F34B0` | `0xEFCE6` |
| texture factory call | `0x69233C` | `0x5707F6` | `0x1000F35B0` | `0xEFDBC` |
| BGRA free | `0x692344` | `0x5707FA` | `0x1000F35B8` | `0xEFDC4` |
| post-callback live-key `operator[]` | `0x69235C` | `0x57080A` | `0x1000F35C8` | `0xEFDD4` |
| map pointer publication | `0x692368` | `0x570810` | `0x1000F35D4` | `0xEFDDE` |
| construction-ref Release | `0x69239C` | `0x57082C` | `0x1000F3608` | `0xEFE04` |
| miss texture commit | `0x6923A0` | `0x57082E` | `0x1000F360C` | `0xEFE0A` |
| strict `icon` dictionary | `0x6923DC` | `0x570842` | `0x1000F362C` | `0xEFE26` |
| live icon -> UTF-8 | `0x6923E8` | `0x57084A` | `0x1000F363C` | `0xEFE32` |
| strict icon lookup | `0x6923F8` | `0x570854` | `0x1000F3658` | `0xEFE50` |
| `valid=true` | `0x692434` | `0x57086C` | `0x1000F367C` | `0xEFE78` |
| origin/dimension projection starts | `0x69244C` | `0x570874` | `0x1000F3690` | `0xEFE84` |
| blank/clip default commit | `0x692570` | `0x570916` | `0x1000F3740` | `0xEFF5E` |
| strict left/top | `0x69258C` | `0x57092E` | `0x1000F3760` | `0xEFF7A` |
| FP extent conversion | `0x69262C` | `0x570982` | `0x1000F37CC` | `0xEFFE6` |
| icon/group/root cleanup starts | `0x692638` | `0x570994` | `0x1000F37D8` | `0xEFFFC` |

## 3. Root owner must outlive the full route

After loaded-module hit, all four images copy the module's PSB raw root into a
dedicated local owner. That owner is distinct from:

- the `sourceRoot` strict-lookup temporary;
- the retained group node returned by nonthrowing lookup;
- the cache-miss texture node;
- the temporary icon dictionary;
- the selected icon node.

The root owner is constructed before strict `root["source"]` and before source
key conversion. It is released last on the ordinary spec-2 success path:

```text
selected iconNode
groupNode
independent root owner
```

The old portable helper constructed root internally and destroyed it on helper
return, leaving only groupNode's owner during texture/icon work. That kept the
raw backing alive but changed the reference count, re-entrant destruction
timing, and unwind owner graph. V230 lifts root to the caller and passes it by
borrowed reference into group lookup.

On strict source-root failure, root unwinds and no generic fallback runs. Since
the caller already cleared `SourceState.object`, the exception leaves that clear
while preserving the old validity and other descriptor fields.

## 4. Group key conversion and ordinary miss

Only after root and sourceRoot owners exist is the live source `ttstr` converted
to a temporary UTF-8 key. The nonthrowing PSB dictionary lookup then writes a
retained group node on hit.

Normal temporary cleanup order is:

1. UTF-8 key storage;
2. sourceRoot owner;
3. much later, groupNode;
4. finally, root owner.

An ordinary missing group does not write `valid=false` at this caller level. It
cleans group/root owners and falls through to generic fallback. The object had
already been cleared at spec-2 admission; generic fallback will clear texture
and dispatch into that object slot.

UTF-8 allocation/conversion failure or strict source-root failure throws before
the ordinary miss result and therefore does not reach fallback.

## 5. The texture cache key is the original live ttstr

The cache is the first nested `unordered_map<ttstr, texture-holder>` inside the
loaded-resource record. Both of its key operations receive the caller's
original source `ttstr` object:

```text
find(liveSrc)
...
renderManager.CreateTexture2D(...)
...
operator[](liveSrc)
```

No `std::string`/UTF-8 round trip and no new `ttstr` reconstruction occurs.
This has two independent consequences.

First, the initial lookup preserves the source backing pointer and cached Hint
field. V223 proved that the unordered map consumes the cached hash before
backing-aware equality; a stale externally modified Hint can therefore cause a
native bucket miss. Narrow-then-widen recomputed a clean hash and hid that
boundary.

Second, the same `ttstr` storage is read again after texture creation. A custom
render-manager callback can re-enter and replace the active source slot. The
texture was decoded from group A, but `operator[]` may then publish it under the
new live key B. The old snapshot code always published under A.

The loaded-resource record reference itself is not protected against re-entrant
module-map erase. The leaked ResourceManager dispatch keeps the object/native
adaptor alive, but it does not pin individual unordered-map nodes; erasing the
loaded record during the callback remains an invalid-reference boundary.

## 6. Cache hit state

On hit, the mapped raw texture pointer is copied directly into
`SourceState.texture`. No AddRef is performed; the nested map value remains the
owner.

The branch then proceeds to live icon lookup. A missing/throwing icon therefore
leaves the cache-hit texture committed while `valid` remains at its prior value
because true is written only after icon lookup.

There is no check for a null mapped pointer. A null hit is copied as null and
icon projection may still succeed and mark the descriptor valid.

## 7. Cache miss schema and pixel conversion

Miss strictly reads `group["texture"]`, then requires this schema in order:

1. `truncated_width` -> integer, discarded;
2. `truncated_height` -> integer, discarded;
3. `width` -> integer;
4. `height` -> integer;
5. `type` -> borrowed string pointer;
6. `pixel` -> borrowed resource pointer and 32-bit size output.

The truncated fields are not defaults for width/height. Any missing or invalid
field throws at its own position.

The pixel strict-node temporary is destroyed immediately after resource
extraction, but textureNode retains the raw PSB owner across allocation,
conversion, upload, map publication, and `SourceState.texture` commit. A null
resource chunk returns null before initializing the caller's size slot in all
four PSB helpers; later consumers then observe the uninitialized size boundary.

Pitch and allocation size use 32-bit arithmetic:

```text
pitch = wrap32(width << 2)
bytes = wrap32(pitch * height)
alignedAlloc(bytes, 4)
```

There is no allocation-null guard.

`RGBA8` calls the shared reverse-RGB routine for signed-low32 `size/4` pixels.
`A8L8` expands each `[alpha,luminance]` pair into
`[luminance,luminance,luminance,alpha]`; a positive odd resource length executes
the native final out-of-range luminance read previously documented in source.

Any other type frees BGRA before building and throwing:

```text
MotionPlayer.findSource: Unsupported texture format '%1'
```

## 8. Factory, publication, and reference counts

After conversion the selected render manager creates a static RGBA texture.
The BGRA allocation is freed immediately after the factory returns and before
cache publication.

The factory pointer is a raw local construction reference. There is no scope
guard and no null check. `operator[]` then uses the post-callback live source
key and either returns an existing slot or publishes a new null-valued node.

Mapped replacement is exactly:

```text
if old != texture:
    if old != null: old.Release()
    slot = texture
    if texture != null: texture.AddRef()
texture.Release()             // unconditional normal-path construction ref
source.texture = texture
```

Identity suppresses release/store/AddRef but not the final construction
Release. Re-entry may have inserted either the same or a different pointer into
the slot between initial miss and publication.

Allocation/rehash failure, an exceptional old Release, or publication failure
leaks the raw factory construction reference because unwind cleanup does not
own it. On normal success the map is sole texture owner and SourceState keeps a
borrowed pointer.

TextureNode is destroyed after the persistent texture store and before icon
dictionary lookup.

## 9. Live icon lookup and validity timing

Icon selection happens after every cache lookup, possible pixel conversion,
render callback, and publication side effect.

The resolver strictly obtains `group["icon"]`, converts the then-live icon
`ttstr` to UTF-8, and strictly selects `icon/<key>`. It does not use an icon
snapshot from resolver entry or from before texture creation.

The UTF-8 icon key and icon dictionary owner are destroyed before
`valid=true`. The selected iconNode is retained separately through all property
reads.

Consequences:

- strict icon dictionary/key failure leaves the texture commit intact;
- `valid` remains old on icon failure;
- group/root owners unwind normally;
- a callback-mutated icon chooses the new icon while the texture may still
  represent the original group.

## 10. Icon projection and partial commits

After strict icon selection, native writes `valid=true`, then reads strict PSB
integers in this order:

1. originX;
2. originY;
3. width;
4. height.

Each is converted to integer, then to double, and stored immediately. An
exception preserves true and the written prefix.

Next it writes:

```text
blank = false
clipLeft = 0
clipTop = 0
clipRight = 1
clipBottom = 1
```

Only then are strict integer `left` and `top` read and stored into rectangle
prefix fields.

## 11. Floating-point rectangle extents

All four references construct right/bottom by converting the integer left/top
to double, adding the already-double width/height, and only then converting the
sum to signed integer:

```text
right  = fpToSigned(width  + double(left))
bottom = fpToSigned(height + double(top))
```

The previous portable code did:

```text
left + int(width)
top  + int(height)
```

Those expressions differ for overflow, nonfinite values, target invalid-
conversion behavior, and C++ signed-overflow rules. V230 now preserves the
floating addition boundary. As in V229, the final portable cast matches finite
in-range truncation but target-specific nonfinite/out-of-range results remain a
documented portability limit.

Three images calculate bottom before right and one calculates right before
bottom after optimization. There is no dispatch or throwing operation between
the two conversions on ordinary finite data.

## 12. Owner and state matrix

| Boundary | extra owners | texture | valid | object |
|---|---|---|---|---|
| spec-2 module hit | root | old | old | cleared |
| strict sourceRoot | root + sourceRoot | old | old | cleared |
| group hit | root + groupNode | old | old | cleared |
| cache hit | root + groupNode | borrowed hit | old | cleared |
| cache miss during decode | root + groupNode + textureNode | old | old | cleared |
| post-publication | root + groupNode | borrowed new | old | cleared |
| icon lookup | root + groupNode + iconRoot/iconNode | current | old | cleared |
| icon selected | root + groupNode + iconNode | current | true | cleared |
| full projection | root + groupNode + iconNode | current | true | cleared |
| success cleanup | none | borrowed current | true | cleared |

Generic fallback after ordinary group miss later clears texture and dispatches a
new object. Exceptions do not run fallback and preserve the state at their
failure point.

## 13. Portable-source changes

`PlayerResource.cpp` now:

- constructs the Win PSB root in the spec-2 caller, before source-root/key work,
  and keeps it through group/texture/icon projection;
- makes `findWinSourceGroup_guess` borrow that root and the live source `ttstr`;
- narrows the group only after strict source-root lookup and destroys that
  temporary on helper return;
- makes `loadWinAtlasTexture_guess` borrow the live source `ttstr` for both
  initial `find` and post-callback `operator[]`;
- removes the narrow-then-widen texture key reconstruction;
- scopes icon dictionary and UTF-8 icon key so they die before `valid=true`;
- retains selected iconNode through projection;
- adds left/top as doubles before final rectangle conversion;
- evaluates port-only trace group/icon narrowing only when tracing is enabled,
  rather than extending native lookup temporaries across the whole branch.

No cache lock, live-key snapshot, module-record pin, texture AddRef in
SourceState, null guard, factory scope guard, schema fallback, icon recovery, or
transactional descriptor commit was introduced.

## 14. Recovery-IDB writeback

Each database now names and types the two instantiated Win texture-map helpers:

- `WinSourceTextureMap_find_guess` (Android arm64's private form also receives
  the precomputed bucket index);
- `WinSourceTextureMap_subscript_guess`.

Across four databases V230 applied:

- 8 helper renames and 8 prototypes;
- 122 line/function comments covering root/source/group owners, live-key
  find/operator[], schema, pixel/format paths, texture factory/publication,
  live icon, property/rect commits, and cleanup;
- 40 bookmarks (10 per image).

All four databases were saved sequentially and closed. The final IDA session
audit reports zero open sessions.

## 15. Validation and products

- `motionplayer-dll.cpp` passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation;
- Web rebuilt 3 affected steps and linked successfully;
- Wasmtime rebuilt 4 affected host/guest steps and linked successfully;
- a follow-up Wasmtime build reports `ninja: no work to do`;
- both CTest trees remain configured with no registered tests;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- scoped `git diff --check` succeeds with only the existing LF-to-CRLF checkout
  warning.

Relative to V229, each product grows by 60 bytes: CODE grows by `0xFA` while
the name section shrinks by `0xBE`; FUNCTION, GLOBAL, and DATA remain unchanged.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,658,063 B | `A02AE53ADA73389FDEA314C75989D76A4A8FBED4C83FE2171DCB7F6FC7EEB7E6` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,005,204 B | `BB2FCAC9E220C2939322004068E5BE598514D6A4FB99A785AE16C57D449C5ED5` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41C3C` | `0x19E9BEA` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185DD5` | `0x3141C6B` |

## 16. Limits and next boundary

- active tests do not provide a custom re-entrant render manager that mutates
  src/icon or erases the loaded module during texture creation;
- malformed/null pixel and allocation-null boundaries are intentionally not
  executed because native behavior includes invalid reads/writes;
- nonfinite/out-of-range FP conversion remains target-specific as documented;
- port-only tracing can still allocate after successful projection when
  explicitly enabled; that diagnostic path has no native counterpart;
- V231 closes the retained `SourceState.path` backing/hash/lifetime boundary;
  the KRKR split-snapshot versus live-retry behavior is documented in its own
  four-binary continuation;
- this slice does not complete the full motionplayer recovery goal.
