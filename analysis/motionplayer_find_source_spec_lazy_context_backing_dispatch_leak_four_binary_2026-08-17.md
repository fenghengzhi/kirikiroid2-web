# motionplayer find-source spec routing, lazy context conversion, backing-aware fallback, and ResourceManager leak four-binary audit

## 1. Scope and conclusion

V226 recovered the shared KRKR atlas loader, and V227 recovered its render-time
retry caller. V228 moves one level outward to the persistent node resolver
itself: `MotionNode_findSource_guess` / portable
`Player::findSourceForNode_guess`.

Fresh decompile/disassembly of all four references establishes this common
entry and routing shape:

```text
rmDispatch = player.findSourceResourceManager.AsObject()  // strict, AddRef
contextArg = Variant(player.motionContextVariant)         // retained copy
rmNative = GetNativeInstance<ResourceManager>(rmDispatch)

src  = borrow(node.activeSlot.src)
icon = borrow(node.activeSlot.icon)

if src.backing != null && src != "blank":
    spec = rmNative->_spec                                // no null guard

    if spec == 2:
        source.object.Clear()
        moduleKey = ttstr(contextArg)                     // lazy conversion
        module = rmNative.loadedModules.find(moduleKey)
        if module missing:
            source.valid = false
        else if Win-PSB source/icon projection succeeds:
            return

    else if spec == 1:
        source.path = src
        if player.useD3D:
            moduleKey = ttstr(contextArg)                 // lazy conversion
            if loadKrkrAtlas(source, rmNative, moduleKey):
                source.valid = true
                return

source.texture = null
fallbackPath = ttstr(src)
if icon.backing != null:
    fallbackPath += "/"
    fallbackPath += icon

status = rmDispatch.findSource(contextArg, fallbackPath, &source.object)
if status != S_OK || source.object is Void:
    source.valid = false
    return

source.valid = true
strictly project width/height/origin/blank/clip into SourceState
```

The ResourceManager reference added by `AsObject()` is never released on any
normal or unwind path in any image. Thus every call with a nonnull dispatch
permanently leaks one reference, including calls that immediately take generic
fallback. A non-Object Variant throws before the copy; a typed-null Object does
not AddRef but remains an invalid receiver/native boundary later.

Three prior local assumptions were wrong and are corrected in this slice:

- context is retained as a Variant at entry, not converted to `ttstr` eagerly;
- null-backed and allocated-empty `ttstr` values are different route inputs;
- loaded-module lookup belongs only to spec 2, while the generic dispatch
  receives the original retained context Variant and a `ttstr` path.

## 2. Four-image mapping

| Semantic boundary | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| resolver entry | `0x691CC8` | `0x570500` | `0x1000F316C` | `0xEF97C` |
| strict `AsObject` / leaking AddRef | `0x691D24` | `0x570522` | `0x1000F31B0` | `0xEF9B6` |
| context Variant retained copy | `0x691D48` | `0x570532` | `0x1000F31C0` | `0xEF9C4` |
| native ResourceManager lookup | `0x691D80` | `0x570554` | `0x1000F31EC` | `0xEFA1E` |
| src backing-pointer admission | `0x691D8C` | `0x570566` | `0x1000F3208` | `0xEFA36` |
| backed `src == "blank"` gate | `0x691DB0` | `0x570578` | `0x1000F321C` | `0xEFA4E` |
| native spec read | `0x691DB4` | `0x570582` | `0x1000F3224` | `0xEFA5E` |
| spec 2 object clear / lazy conversion | `0x691DC4/0x691DD0` | `0x570588/0x57058E` | `0x1000F3230` | `0xEFA68` |
| spec 2 module miss `valid=false` | `0x691F74` | `0x57061C` | `0x1000F3300` | `0xEFB36` |
| spec 1 persistent path copy | `0x691F84..0x691FAC` | `0x570626..0x57064E` | `0x1000F3310` | `0xEFB46` |
| spec 1 `useD3D` gate | `0x691FB4` | `0x570650` | `0x1000F333C` | `0xEFB7C` |
| atlas Boolean/result gate | `0x691FE4` | `0x570674` | `0x1000F336C` | `0xEFBAC` |
| fallback texture clear/path build | `0x6926C0` | `0x5709B4` | `0x1000F3808` | `0xF001E` |
| fallback `findSource` dispatch | `0x692860` | `0x570A4E` | `0x1000F38A8` | `0xF00AE` |

The recovered function name remains suffixed `_guess`: the binaries are
stripped and do not prove an original C++ member spelling.

## 3. Entry ownership and the second ResourceManager leak site

The resolver begins from the Player's retained `_findSourceResourceManager`
Variant. `AsObject()` is strict:

```text
if Type != Object: throw VariantConvertError(Object)
if dispatch != null: dispatch.AddRef()
return dispatch
```

Android arm64 inlines the type test/AddRef; the other images retain or share a
helper form. The returned dispatch is used both for native-adaptor extraction
and the later generic method call. It is never placed in a local owner and no
tail calls `Release`.

The exact owner order is significant:

1. strict conversion and leaking AddRef;
2. retained copy of the motion-context Variant;
3. nonthrowing NCB native-instance query;
4. only then, inspection of the live node `src` backing.

Consequently a null-backed source still leaks the dispatch reference. A later
context conversion, map exception, PSB exception, fallback dispatch exception,
or property getter exception also leaves it leaked. The leak keeps the dispatch
and its native adaptor alive across re-entrant context conversion, but this is
an incidental consequence rather than a scoped owner.

This is a distinct call site from the V227 render-time retry leak. A successful
find-source invocation leaks once here; later render retries may leak again
whenever their initial texture slot remains null.

## 4. Context is a Variant first and a string only on two routes

All four images copy the persistent context into a local `tTJSVariant` before
examining `src`. They do not call its string conversion at entry.

| Route | Resolver converts context to `ttstr`? | Consumer |
|---|---:|---|
| null-backed src | no | generic `findSource` receives Variant |
| backed `src == "blank"` | no | generic `findSource` receives Variant |
| nonblank src, spec other than 1/2 | no | generic `findSource` receives Variant |
| spec 1, `useD3D == false` | no | generic `findSource` receives Variant |
| spec 1, `useD3D == true` | yes | KRKR loaded-module/atlas helper |
| spec 2 | yes | Win loaded-module lookup |

Object-valued contexts make this ordering observable because Variant-to-string
conversion can dispatch into script and re-enter Player. On spec 2 the object
slot has already been cleared before conversion. On spec 1 the persistent path
has already been copied before conversion. An exception therefore preserves
those route-specific prefix writes rather than an entry snapshot.

The generic fallback does not consume the converted module key. It passes the
retained Variant itself as argument zero. Portable tracing may derive a narrow
string only on the two native conversion routes; that diagnostic value is not
part of source resolution.

## 5. Backing-pointer admission: null is not allocated empty

`src` and `icon` are `ttstr` owners whose internal backing pointer is tested
directly. Length is not consulted for presence.

For `src`:

| Input representation | backing | length | route |
|---|---:|---:|---|
| default/null `ttstr` | null | 0 | skip spec; generic fallback |
| allocated-empty string | nonnull | 0 | nonblank; read native spec |
| allocated `"blank"` | nonnull | 5 | skip spec; generic fallback |
| any other backed value | nonnull | any | read native spec |

The native spec pointer is dereferenced only after the backed/nonblank gate and
has no null-native guard. The old local `.empty()` check merged the first two
rows and hid both the spec dereference and any spec-specific state mutations
for allocated-empty input.

Fallback checks `icon.backing`, again not length. Representative path results
are:

| live src | live icon | fallback path payload |
|---|---|---|
| null-backed | null-backed | null/empty source owner |
| null-backed | allocated-empty | `/` |
| null-backed | `face` | `/face` |
| allocated-empty | null-backed | allocated-empty source owner |
| allocated-empty | allocated-empty | `/` |
| allocated-empty | `face` | `/face` |
| `blank` | allocated-empty | `blank/` |
| `src/body/a` | `face` | `src/body/a/face` |

The slash is appended solely because icon backing exists. There is no test for
nonzero icon length.

## 6. Live source/icon references across re-entry

The resolver borrows the active-slot `src` and `icon` storage; it does not make
entry copies of both strings. It snapshots only the initial source-backing
Boolean used for admission.

This matters after context-to-string conversion dispatches/re-enters:

- spec 1 has already copied the pre-conversion source into persistent path;
- spec 2 later converts the live source for group lookup;
- any fallthrough constructs fallback path from the then-live source;
- icon backing and payload are re-read only at fallback construction time.

Thus the fallback icon decision is not an entry snapshot. The portable source
now keeps references to the slot values and performs the backing checks at the
same semantic points instead of caching narrow strings eagerly.

## 7. Spec 2 (Win PSB) caller boundary

Spec 2 first clears only `SourceState.object`. Texture, validity, dimensions,
clip, rectangle, and path retain their previous bytes at this point.

It then converts the retained context Variant to `ttstr` and probes the native
loaded-module map:

- module miss writes `valid=false` and falls through to generic fallback;
- module hit continues through Win PSB source-group lookup;
- group miss falls through without a caller-level `valid=false` write;
- successful texture/icon projection writes the descriptor and returns.

The generic fallback subsequently clears texture and dispatches directly into
the already-cleared object slot. Therefore module miss and group miss normally
converge at fallback, but their pre-dispatch validity histories differ. An
exception during lazy conversion preserves the cleared object and the old
valid flag; an exception after the explicit module-miss store preserves false.

The internal Win texture cache, raw-node owners, strict `icon/<key>` navigation,
and property commit order are a separate deeper boundary; V228 records only
the outer routing and owner timing needed to reach them.

## 8. Spec 1 (KRKR atlas) caller boundary

Spec 1 copies the live source string into the persistent path before testing
the Player's `useD3D` byte. It does not clear object or texture at this point.

- with `useD3D == false`, context is not string-converted and execution falls
  directly to generic fallback;
- with `useD3D == true`, context is lazily converted and the V226 atlas helper
  runs against the persistent SourceState;
- atlas true causes the caller to repeat `valid=true` and return;
- atlas false falls through without restoring path, object, validity,
  dimensions, or rectangle from an entry snapshot.

Fallback then clears texture unconditionally. Any object clear or `valid=false`
performed inside the atlas helper remains observable by the generic dispatch;
prefix-mismatch and module-miss behaviors therefore differ exactly as recorded
in V226.

## 9. Generic fallback dispatch and path owner

Every non-returning route joins one fallback block. Its first descriptor write
is `source.texture = null`.

The path is a `ttstr` initialized from the live source owner. If icon has a
nonnull backing, the block appends `/` and then the live icon. The dispatch call
receives two arguments in order:

1. the retained context Variant copied at entry;
2. the constructed path `ttstr` Variant.

The result target aliases persistent `SourceState.object`; there is no
temporary result followed by atomic commit. A nonzero call status or resulting
Void object writes `valid=false` and returns. The object may therefore contain
whatever prefix/partial value the dispatch wrote even on status failure.

On success, `valid=true` is written before any property projection. A separate
property-accessor owner is then built from the object so re-entrant getters may
replace `SourceState.object` without destroying the receiver used by the
remaining sequence.

## 10. Descriptor commit matrix

| Boundary | object | texture | valid | path | geometry/clip/rect |
|---|---|---|---|---|---|
| resolver entry | retain old | retain old | retain old | retain old | retain old |
| spec 2 admission | clear | retain old | retain old | retain old | retain old |
| spec 2 module miss | clear | retain old | false | retain old | retain old |
| spec 1 admission | retain old | retain old | retain old | copy src | retain old |
| atlas false | helper result | helper result | helper result | copied src | helper result |
| fallback entry | retain current | null | retain current | retain current | retain current |
| fallback status fail/Void | dispatch result | null | false | retain current | retain current |
| fallback non-Void success before getters | dispatch result | null | true | retain current | retain current |
| fallback complete success | accessor result | null | true | retain current | projected |

The successful generic projection order is width, height, originX, originY,
blank, then clip. Object-valued clip is projected as left/top/right/bottom;
non-Object clip commits defaults `[0,0,1,1]`. Finally texture rectangle becomes
`[0,0,int(width),int(height)]`.

Because `valid=true` precedes getters, an exception during width or any later
property leaves true plus the successfully committed prefix and old suffix.
This slice deliberately does not add transactional rollback.

## 11. Exception and lifetime topology

```text
Player._findSourceResourceManager Variant
  -> strict AsObject AddRef (nonnull only)
     -> no owner / permanently leaked
     -> native ResourceManager pointer
     -> generic findSource receiver

Player._findMotionContextVariant
  -> local retained Variant copy
     -> optional temporary ttstr on spec 2 or spec 1+useD3D
     -> otherwise passed as Variant to generic dispatch

MotionNode active slot
  -> borrowed live src/icon references
     -> persistent spec-1 path copy
     -> fallback ttstr owner

SourceState.object
  -> direct dispatch result target
     -> independent accessor receiver owner on successful projection
```

Normal cleanup releases the local context Variant, optional module-key string,
fallback path, and property accessor owners. None of those owners covers the
ResourceManager AddRef.

There is also no friendly recovery for a failed native-instance query once a
backed nonblank source reaches the spec read. Typed-null dispatch/native values
remain null-dereference or invalid-call boundaries rather than returning an
empty source.

## 12. Portable-source changes

`PlayerResource.cpp` now mirrors the recovered route order:

- calls strict `_findSourceResourceManager.AsObject()` and intentionally omits
  `Release` for the returned nonnull dispatch;
- copies `_findMotionContextVariant` as a Variant before native lookup;
- removes eager context-to-string conversion and entry loaded-module lookup;
- tests `ttstr::AsVariantStringNoAddRef()` for src/icon backing presence;
- reads `_spec` only after backed/nonblank source admission and retains the
  native no-null-guard boundary;
- performs context conversion only in spec 2 and spec 1+`_d3dDrawMode`;
- makes loaded-module lookup local to spec 2;
- commits spec-1 path before the D3D gate;
- constructs fallback as `ttstr`, preserving allocated-empty icon slash
  behavior, and passes the retained context Variant to dispatch;
- rechecks live icon backing after any possible context conversion/re-entry.

The previous RAII wrapper around the strict ResourceManager dispatch was
removed only from this recovered call site. Other helper/caller ownership is
not broadened to leak.

V231 subsequently closed the path-owner portability difference documented by
this slice. `SourceState.path` now stores a retained `ttstr`; spec 1 copies the
live source owner directly, the shared atlas helper takes an owning split
snapshot, and cache probe/retry retain a reference to the live persistent
field. Narrow conversion remains only at Web diagnostic boundaries.

## 13. Recovery-IDB writeback

All four resolver bodies retain the semantic name
`MotionNode_findSource_guess` and now have the same four-pointer `void` callable
prototype. Each database received:

- 13 V228 line comments covering the leaking `AsObject`, context owner order,
  native lookup, backing-aware src/blank gates, spec read, spec-2/module miss,
  spec-1 path/D3D/atlas gates, and generic fallback dispatch;
- 6 V228 bookmarks for the principal ownership/control boundaries.

That is 4 finalized function types, 52 comments, and 24 bookmarks across the
four references. Databases were saved sequentially and closed. The final IDA
session audit reports zero open sessions.

## 14. Validation and products

- `motionplayer-dll.cpp` passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation after the V228
  source edit;
- Web rebuilt 34 affected steps and linked successfully;
- Wasmtime rebuilt 64 affected plugin/guest steps and linked successfully;
- a follow-up Wasmtime build reports `ninja: no work to do`;
- both CTest trees remain configured with no registered tests;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- scoped `git diff --check` succeeds; only the repository's existing
  LF-to-CRLF checkout warnings are emitted.

Relative to V227, restoring lazy conversion/backing-aware branches adds 107
bytes to each product. CODE grows by `0x7E`; the name section shrinks by
`0x13`; FUNCTION, GLOBAL, and DATA remain unchanged.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,658,003 B | `2F88E9ACC3F930788908C46BBF13D0D7FC00908EEA7F00A83572FF84390DEF97` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,005,144 B | `D73A723B7124FC8AD9B082BB5011985CCD9AC7066ACE9F0AB18222593F2B744F` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41B42` | `0x19E9AF0` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E93` | `0x3141D29` |

## 15. Limits and next boundary

- active CTest trees contain no runtime fixture that can independently create
  null-backed versus allocated-empty TJS strings and observe dispatch paths;
  this distinction is established from four-binary control flow and TJS owner
  representation;
- the deeper spec-2 Win group/texture/icon cache implementation is not fully
  re-audited here;
- generic fallback property getter hint caches, strict receiver conversion,
  re-entrant result replacement, and per-field exception cleanup deserve a
  dedicated four-image continuation;
- the persistent-path backing/hash/lifetime boundary was closed by V231; its
  split-snapshot versus live-retry re-entry behavior is documented separately;
- this slice does not complete the full motionplayer recovery goal.
