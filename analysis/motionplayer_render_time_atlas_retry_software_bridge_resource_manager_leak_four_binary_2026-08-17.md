# motionplayer render-time atlas retry, fallback-only software bridge, and ResourceManager leak four-binary audit

## 1. Scope and conclusion

V226 closed the shared KRKR atlas loader.  V227 follows its second caller: the
D3D render-time source-texture callback.  This callback combines three source
routes with the D3DAdaptor software-renderer copy cache.

Fresh decompile/disassembly of all four references yields this exact control
shape:

```text
source = preparedItem.sourceState

if source.texture != null:
    return source.texture                    // no software conversion

rmDispatch = player.findSourceRM.AsObject() // strict; AddRef
rmNative = GetNativeInstance(rmDispatch)    // added ref is never Released

moduleKey = ttstr(copy(player.motionContextVariant))
atlasLoaded = loadKrkrAtlas(source, rmNative, moduleKey)
destroy moduleKey and copied context

if atlasLoaded && source.texture != null:
    return source.texture                    // no software conversion

layerValue = player.resolveRenderSource(source.object)
layer = strict Layer conversion(layerValue)
fallbackTexture = layer.MainImage.Texture

if process renderer is not software:
    return fallbackTexture
if adaptor.softwareCopies contains fallbackTexture:
    return cached static copy

copy = private OpenGL manager.CreateTexture2D(
    fallbackTexture.scanline0,
    fallbackTexture.pitch,
    fallbackTexture.width,
    fallbackTexture.height,
    fallbackTexture.format,
    STATIC)
adaptor.softwareCopies.emplace(fallbackTexture, copy)
return copy
```

The local implementation previously split this native callback into a
SourceCache texture resolver followed by an unconditional
`D3DAdaptor::getRenderTexture_guess`.  That made both atlas-success branches
enter the software-copy map, contrary to all four references.  V227 moves the
adaptor bridge inside the combined resolver so only the generic Layer fallback
is converted.

The same audit also recovers a deliberate/accidental native lifetime defect:
every entry that starts with `source.texture == null` calls strict
`tTJSVariant::AsObject()` on the retained ResourceManager Variant.  That helper
AddRefs a nonnull dispatch, but the callback never Releases it.  The local
render path now preserves this one-reference leak per retry entry rather than
using the friendly borrowed `Player::nativeRM()` fast pointer.

## 2. Four-image mapping

| Semantic boundary | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| callback entry | `0x6EE440` | `0x5AC518` | `0x10014019C` | `0x1414C0` |
| initial `source.texture` load | `0x6EE474` | `0x5AC532` | `0x1001401C4` | `0x1414F8` |
| strict ResourceManager `AsObject` | `0x6EE488` inline | `0x495308` via `0x5AC546` | `0x100030294` via `0x1001401D4` | `0x125338` via `0x141528` |
| native-adaptor query | `0x6EE4D0` | `0x5AC560` | `0x1001401FC` | `0x141548` |
| motion-context copy | `0x6EE50C` | `0x5AC586` | `0x100140228` | `0x14157A` |
| temporary `ttstr` conversion | `0x6EE518` | `0x5AC58E` | `0x100140234` | `0x141586` |
| shared atlas call | `0x6EE528` | `0x5AC598` | `0x100140244` | `0x141594` |
| module-key/context cleanup | `0x6EE538/0x6EE540` | `0x5AC5A0/0x5AC5A6` | `0x100140250/0x100140258` | `0x14159C/0x1415A2` |
| success + reloaded texture gate | `0x6EE548..0x6EE550` | `0x5AC5AE..0x5AC5B6` | `0x100140260..0x100140268` | `0x1415A8..0x1415B6` |
| generic source fallback | `0x6EE560` | `0x5AC5C2` | `0x100140278` | `0x1415C6` |
| strict Layer conversion | `0x6EE56C` | `0x5AC5CC` | `0x100140284` | `0x1415D8` |
| fallback main-image texture | `0x6EE57C` | `0x5AC5D8` | `0x100140294` | `0x1415E8` |
| software-renderer gate | `0x6EE584` | `0x5AC5E2` | `0x1001402A0` | `0x1415F6` |
| ordered software-map probe | `0x6EE58C` | `0x5AC5E4` | `0x1001402B0` | `0x141604` |
| private OpenGL manager | `0x6930E4` via `0x6EE600` | `0x570EA0` via `0x5AC628` | `0x1000F3D90` via `0x1001402CC` | `0xF0834` via `0x14161C` |
| static copy creation | `0x6EE668` | `0x5AC668` | `0x100140334` | `0x141670` |
| map publication | `0x6EE67C` | `0x5AC670` | `0x100140348` | `0x141682` |
| fallback Variant cleanup | `0x6EE688` | `0x5AC678` | `0x100140354` | `0x14168A` |

The callback is installed/stored as callable data rather than reached by an
ordinary direct code call in three images; Android arm64 has a data reference
from its D3DAdaptor render closure.  The recovered semantic name is therefore
`D3DSourceTextureGetter_call_guess`, not a claimed original class-member name.

## 3. Closure, item, and persistent-state topology

The callback borrows a closure containing the D3DAdaptor and Player context,
and a prepared-item reference.  The item does not own a copied source
descriptor: it points to the persistent node `SourceState` recovered in earlier
slices.

Observed prepared-item/source offsets at this boundary are:

| ABI | `PreparedRenderItem::sourceState` | `SourceState::texture` |
|---|---:|---:|
| LP64 | item `+0x100` | source `+0x18` |
| ILP32 | item `+0xE4` | source `+0x10` |

The callback reloads `item.sourceState` after the shared atlas call rather than
assuming that its prior texture value remains current.  It reads the same
persistent texture slot used by find-source and V226 projection.

No returned texture path gives the render callback a new caller-owned
reference.  Atlas results are borrowed from the module atlas map; fallback GPU
textures are borrowed from the Layer main image; software copies are borrowed
from the adaptor map.

## 4. Initial texture fast path

The very first semantic operation is:

```text
texture = item.sourceState->texture
if texture != null: return texture
```

This branch precedes all of the following:

- ResourceManager Variant conversion/native lookup;
- the leaking ResourceManager AddRef;
- motion-context Variant copy and string conversion;
- KRKR atlas retry;
- generic source fallback;
- strict Layer conversion;
- `TVPIsSoftwareRenderManager`;
- D3DAdaptor software map lookup or copy creation.

Consequently an already-projected atlas texture returns verbatim even when the
process renderer reports software.  There is no validation that its owning
module entry is still alive, no map identity recheck, and no AddRef.  A stale
nonnull borrow after module/cache destruction is still returned as-is.

The previous local split resolved the same pointer and then unconditionally
called `adaptor->getRenderTexture_guess`, which dereferenced/copied it on a
software renderer and populated a map node absent from the references.

## 5. Strict ResourceManager extraction and permanent reference leak

Only the null-texture path touches the Player's retained find-source
ResourceManager Variant.  The shared `AsObject` helpers in all four images have
the same behavior:

```text
if variant.type != Object:
    throw VariantConvertError(Object)
if variant.Object != null:
    variant.Object.AddRef()
return variant.Object
```

The 32-bit and iOS arm64 implementations preserve standalone helper bodies;
Android arm64 inlines the type test/AddRef and calls its no-return conversion
error helper on mismatch.

The returned dispatch is passed to `NativeInstanceSupport(GETINSTANCE,
ResourceManagerClassID, ...)`, and a successful adaptor yields its native
pointer.  Failure or a typed-null Object produces null because this lookup does
not request the throwing `err=true` form.

Critically, no normal tail and no exception cleanup calls `Release` on the
dispatch reference added by `AsObject`.  The leak occurs before module-key
conversion and atlas work, so it remains on:

- atlas success;
- atlas ordinary false;
- context-to-string exception;
- PSB/packing/publication exception;
- generic fallback success or failure;
- software map hit/miss and copy creation failure.

It does not occur when the initial texture fast path succeeds, or when the
ResourceManager Variant is a typed-null Object (there is nothing to AddRef).

Practical accumulation follows the descriptor state:

- first successful atlas retry leaks one ResourceManager reference, then future
  calls normally take the nonnull texture fast path and leak no more;
- a persistent generic fallback usually leaves `source.texture` null, so every
  rendered use re-enters this branch and leaks another reference;
- the leak incidentally keeps the ResourceManager dispatch/native alive even
  if re-entrant context string conversion clears the Player's persistent owner.

The local code now calls `_findSourceResourceManager.AsObject()` directly and
intentionally omits Release.  It does not broaden this defect by changing the
separately used `Player::nativeRM()` helper.

## 6. ResourceManager-before-module-key ordering

All four callbacks complete strict ResourceManager conversion and native
adaptor lookup before copying the Player motion-context Variant and converting
that copy to `ttstr`.

This ordering matters for Object-valued contexts because string conversion can
dispatch into script and re-enter Player.  The native pointer is already
selected, and the leaked AddRef already keeps the ResourceManager dispatch
alive, before that re-entry can occur.

The former local source constructed `moduleKey` first and evaluated
`player.nativeRM()` only as an atlas-call argument.  V227 now stores the strict
native extraction result before entering the scoped context/ttstr block.

The context copy and `ttstr` are destroyed before the atlas Boolean and
reloaded texture are tested.  A successful atlas return therefore has no live
module-key temporary at either direct texture return.

## 7. Atlas result matrix

The retry return is a conjunction, not just the helper Boolean:

| atlas return | reloaded `source.texture` | next route |
|---:|---:|---|
| false | null | generic fallback |
| false | nonnull | generic fallback (the Boolean dominates) |
| true | null | generic fallback |
| true | nonnull | direct texture return |

The helper's ordinary behavior normally makes false/null and true/nonnull the
relevant pairs, but the explicit two-part gate is common to all four binaries.

The true/nonnull branch joins the initial fast return before the software
renderer test.  Thus both an atlas cache hit recovered during retry and a newly
built atlas entry bypass D3DAdaptor software conversion.

## 8. Post-atlas generic fallback

Fallback receives the live `source.object` after the atlas call.  It does not
restore an entry snapshot, consult `source.path`, or retry find-source with a
fresh object first.

V226 makes the resulting boundary state-dependent:

- prefix mismatch leaves the entire `SourceState` unchanged, so fallback sees
  the prior generic object;
- module miss writes `valid=false` but does not clear object, so fallback still
  sees the prior object;
- module hit clears object before cache/build work; a later atlas false reaches
  fallback with that cleared/post-build object;
- exceptions do not reach fallback at all.

The fallback resolver returns a Variant that is strictly converted to a native
Layer.  The callback then follows Layer -> MainImage -> Texture without a
friendly HRESULT/null branch.  Invalid, Void, wrong-type, typed-null, null main
image, or null texture inputs remain strict/invalid-access boundaries.

## 9. Fallback-only software renderer bridge

`TVPIsSoftwareRenderManager` is executed after fallback Layer texture
extraction and nowhere on either atlas return branch.

For a non-software renderer the raw fallback main-image texture is returned.
For a software renderer the adaptor's ordered map is probed with that raw
texture pointer as a borrowed identity key.  Pointer numeric ordering is the
tree comparator; the key takes no texture reference.

On a hit the mapped `tTJSRefHolder` supplies the retained static-copy pointer
without adding a caller reference.

On a miss the callback:

1. obtains the private OpenGL render manager, independently of the process
   default renderer;
2. queries source scanline zero;
3. queries pitch;
4. reads width and height;
5. queries format;
6. calls `CreateTexture2D` with the exact values and static flag one;
7. inserts source pointer -> new copy into the adaptor map;
8. returns the new copy pointer.

There is no null guard for the source, manager, or created copy.  As recovered
in the prior software-texture publication slice, mapped-holder construction
AddRefs the new copy while this caller never releases the factory construction
reference.  Map destruction/clear releases the holder reference but not that
extra creation reference.

## 10. Exception and owner boundaries

The callback's principal owner tree is:

```text
Player._findSourceResourceManager
  + persistent Variant reference
  + one permanently leaked dispatch reference per null-texture retry entry

LoadedResourceRecord.krkrSourceEntries
  -> atlas page owner
     -> SourceState.texture borrow (direct return)

fallback Layer Variant (temporary)
  -> Layer/MainImage texture borrow
     -> direct return on GPU renderer
     -> D3DAdaptor ordered map key (borrowed) on software renderer
        -> mapped holder owns static copy
        -> factory construction reference remains leaked
```

The context Variant/ttstr temporaries have unwind cleanup around the atlas
call.  The fallback Layer Variant is cleaned after fallback texture selection,
including map hit or new-copy publication.  Neither cleanup changes the raw
texture selected for return.

The permanent ResourceManager AddRef has no unwind owner.  Software-copy
creation/publication also retains the previously recovered raw construction-
reference behavior rather than installing a scope guard.

## 11. Portable-source changes

- `SourceCache.h` forward-declares `D3DAdaptor` and makes the combined render
  resolver borrow an adaptor reference;
- `SourceCache.cpp` includes `D3DAdaptor.h`;
- initial and newly loaded atlas texture branches return directly;
- strict `_findSourceResourceManager.AsObject()` plus nonthrowing native-adaptor
  lookup replaces `player.nativeRM()` on this route, and the added dispatch
  reference is deliberately not released;
- ResourceManager extraction now precedes scoped module-key construction;
- only `loadRenderSourceTextureFromItem_guess`'s generic Layer result is passed
  to `adaptor.getRenderTexture_guess`;
- `PlayerRenderTargets.cpp` no longer applies the adaptor conversion to the
  combined resolver's already-selected atlas result.

No atlas texture AddRef, stale-borrow validation, ResourceManager release guard,
fallback type recovery, software-map key ownership, or copy-construction
release was added.

## 12. Recovery-IDB writeback

All four callback bodies were renamed `D3DSourceTextureGetter_call_guess` and
typed as two-pointer callable entries.  Each database received:

- 12 V227 line comments covering fast return, strict/leaking ResourceManager
  extraction, module-key order, atlas retry, post-atlas fallback, strict Layer,
  fallback-only software gate, ordered map lookup, static copy, publication,
  and cleanup;
- 6 V227 bookmarks for the main ownership/control boundaries.

Android arm64's initial ResourceManager comment/bookmark was replaced after
the standalone three-architecture `AsObject` helpers proved the missing AddRef
has no Release.  All four databases were saved sequentially and closed; the
final session audit reports zero open IDA sessions.

## 13. Validation and products

- `motionplayer-dll.cpp` passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation;
- Web rebuilt 34 affected steps and linked successfully;
- Wasmtime rebuilt 64 affected plugin/guest steps and linked successfully;
- a follow-up Wasmtime build reports `ninja: no work to do`;
- both CTest trees remain configured with no registered tests;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`.

Relative to V226, the corrected combined branch/owner structure adds 48 bytes
to each module: CODE grows by `0x1A` and the name section by `0x16`; FUNCTION,
GLOBAL, and DATA remain unchanged.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,896 B | `7ED168AE15D560E1F1F152D1454AB9C08C72DEFD3977E9494F4919340A2EC8B4` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,005,037 B | `81D28F48C6987BB22FB1EA52FA548EF31C97C9D7EEBCC7B0DCF32E8F5F2A8910` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41AC4` | `0x19E9A72` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185EA6` | `0x3141D3C` |

## 14. Limits

- the active test trees do not register a runtime fixture that switches the
  process renderer to software while independently observing atlas/map paths;
  this slice is validated by four-binary control flow plus compile/link/module
  checks;
- stale borrowed atlas pointer use after module destruction remains a native
  invalid-lifetime boundary and was not exercised deliberately;
- this slice does not re-audit every internal red-black-tree insertion landing
  pad already covered by the dedicated D3D software-texture publication report;
- this does not complete the full motionplayer recovery goal.
