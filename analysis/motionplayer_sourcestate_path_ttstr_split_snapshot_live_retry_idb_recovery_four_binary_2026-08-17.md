# motionplayer SourceState path ttstr, split snapshot, live retry, and IDB recovery four-binary audit

## 1. Scope and conclusion

V231 closes the persistent KRKR source-path representation left open by V226,
V228, and V230. The local port stored `MotionNode::SourceState::path` as a Web
`std::string`, widened it for slash splitting and atlas lookup, and kept a local
key snapshot across atlas construction. All four references instead store a
retained `ttstr` owner inside `SourceState`.

The native loader deliberately uses that owner in two different ways:

```text
entryPath = retained_copy(source.path)
pieces = split(entryPath, '/')       // owning entry snapshot
destroy(entryPath)

liveKey = reference(source.path)     // address of persistent field
entry = atlasMap.find(liveKey)
if entry == end:
    build(group=pieces[1], icon=pieces[2])
    entry = atlasMap.find(liveKey)   // re-read after callbacks, unchecked
```

Consequently, atlas-building callbacks can replace `SourceState.path` without
changing the already-built `pieces` vector. The builder still processes the
entry group/icon, but the outer retry uses the latest path. A published
replacement redirects projection; an unpublished replacement reaches the
already-recovered unchecked retry boundary.

The source now matches this owner split: `SourceState.path` is `ttstr`, spec 1
copies the live source owner directly, split receives an owning value copy, and
both lookups retain a reference to the persistent field. Narrow conversion is
confined to Web-only logging/prepared-item diagnostics.

## 2. Four-image mapping and layout

| Boundary | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| outer loader | `0x6931C8` | `0x570F54` | `0x1000F4098` | `0xF0BE4` |
| `SourceState.path` offset | `+0x70` | `+0x68` | `+0x70` | `+0x64` |
| load path / retain field address | `0x693200` | `0x570F72` | `0x1000F40DC` | `0xF0C14` |
| split retained entry snapshot | `0x69323C` | `0x570FA2` | `0x1000F4114` | `0xF0C78` |
| first live-key find | `0x693438` | `0x570FF6` | `0x1000F4178` | `0xF0CE0` |
| live-key retrieval/retry | `0x693654` | `0x571002` | `0x1000F4188` | `0xF0CF0` |
| iOS armv7 post-build return to probe | — | — | — | `0xF191E` |

The 64-bit layouts agree on `+0x70`, but the 32-bit layouts do not: Android
old-libstdc++ places the path at `+0x68`, whereas iOS libc++ places it at
`+0x64`. Portable source therefore records only semantic field order, never one
target's byte offset.

## 3. Entry split owns a ttstr backing snapshot

At entry, each loader loads the path's `tTJSString*` backing from the persistent
field, AddRefs it when nonnull, and passes the copied `ttstr` to the shared
split helper. The delimiter is another temporary `ttstr`. After vector
construction, both temporaries are released; each string in the result vector
owns its own backing according to the previously recovered split-container
contract.

This is not equivalent to narrow UTF-8 conversion followed by widening:

- backing identity and refcount traffic are preserved;
- an allocated-empty string remains distinct from a null-backed string;
- the backing's cached hash remains available to native unordered-map code;
- exceptions during split clean the copied owner without touching the
  persistent field;
- a later assignment to `SourceState.path` cannot change `pieces`.

## 4. Cache keys borrow the live persistent field

After splitting, the native code retains the address of the persistent path
field in a callee-saved register or stack slot:

- Android arm64 advances `X20` to `SourceState + 0x70`;
- Android armv7 advances `R4` to `SourceState + 0x68`;
- iOS arm64 advances `X20` to `SourceState + 0x70`;
- iOS armv7 stores `SourceState + 0x64` in `var_2BC`.

Both outer map operations consume that address. Android arm64 additionally
reads the live backing's cached hash for its old-libstdc++ bucket-predecessor
helper. The other three libc++/old-libstdc++ helpers also receive the live
`ttstr` object rather than a locally copied entry key.

The first lookup is guarded. On a miss, the builder may call texture creation
and update code before normal cleanup returns to the lookup block. Those are
re-entry points. No code restores the entry path or copies it into a retry
local. The second lookup therefore observes any callback replacement.

## 5. Data-flow and boundary behavior

The complete path-sensitive flow is:

1. copy the entry `ttstr` owner and split it;
2. require only `pieces[0] == "src"`;
3. resolve the independently supplied module key;
4. clear only the source object, retain the PSB root, and probe with live path;
5. on miss, use snapshot `pieces[1]`/`pieces[2]` to choose the group/icon build;
6. publish packed entries under record-derived source keys;
7. after callbacks and cleanup, retry with current live path;
8. dereference the retry result without an end/null guard;
9. project the selected entry and borrow its texture pointer.

This permits intentionally asymmetric results. If an entry call starts as
`src/A/icon0`, builds A, and a callback changes the path to `src/B/icon1`, the
pieces still request A while the retry requests B. If B was already cached or
was published by the build, B is projected. Otherwise the unchecked retry
crosses the native invalid-access boundary.

Spec 1 assigns the persistent path before testing the D3D gate. Therefore a
disabled atlas attempt or ordinary atlas failure still commits the new retained
path even if control continues into generic fallback. Spec 2 does not write the
field. The local deterministic `SourceState::clear()` helper releases it via
`ttstr::Clear()`.

V232 later established that this whole-record `clear()` is only a deterministic
reconstruction-harness helper, not the native constructor or a native loader
failure path. Native destruction still releases the retained path owner.

## 6. Render-item consumption

The native prepared render item does not own an independent source-key string;
it stores a direct pointer to the node's `SourceState`. The Web port retains a
separate `std::string sourceKey` only for diagnostics and headless traces. V231
keeps that sidecar explicitly non-native and narrows `node.source.path` only
when refreshing the diagnostic item.

This prevents the Web sidecar from accidentally becoming the atlas lookup
owner or masking the native live-field behavior.

## 7. Portable-source changes

V231 changes are confined to the path owner and explicit diagnostic boundary:

- `MotionNode.h`: `SourceState.path` is `ttstr`; the local deterministic reset
  helper uses `path.Clear()`;
- `PlayerResource.cpp`: split accepts `source.path` directly; lookup uses
  `const ttstr& sourceKey = source.path`; spec 1 assigns `sourceValue` directly;
  trace formatting narrows only when enabled/reached;
- `PlayerRenderItems.cpp`: the Web diagnostic `sourceKey` narrows at assignment;
- `motionplayer-dll.cpp`: its UTF-8 prefix assertion narrows explicitly.

No short-path validation, post-build entry guard, texture AddRef, full
descriptor reset, or independent retry snapshot was introduced.

## 8. Recovery-IDB writeback and armv7 repair

The first three databases received V231 comments/bookmarks sequentially and
were saved normally. The iOS armv7 canonical `.i64` could no longer reopen
after V230; both headless and GUI backends failed. A byte-identical diagnostic
backup was made before recovery:

| Artifact | size | SHA-256 |
|---|---:|---|
| corrupt V230 canonical backup | 394,671,494 B | `3704BC00AE6C79910A0B8ABECF2EC26C87F4AE36C56F6B57ADE306AFC321517A` |

Native `idat` against the backup passed licensing/memory initialization but
reported `Database is empty`, `Database initialization failed with error 4`.
An earlier healthy recovery database under `out/ida-recovery/ios-armv7` opened
with the correct module, input path, `imagebase=0x4000`, and Hex-Rays. It was
used as the non-destructive recovery base.

That base received concentrated V228-V230 function notes plus complete V231
line comments/bookmarks, was saved to a new file, independently reopened, and
only then copied over the canonical armv7 `.i64`. The repaired canonical again
opens from the original binary path with correct health metadata.

| Artifact | size | SHA-256 |
|---|---:|---|
| V231 recovered / repaired canonical | 403,117,759 B | `43F71A960D671B567E3A2ECF38E2A08B8CE1FE66EB2C77942FB43DF5E69F7BDA` |

The corrupt backup, earlier healthy base, and verified V231 recovered copy all
remain available. V231 itself added 24 path-owner comments and 21 path-owner
bookmarks across the four databases; armv7 also received four concentrated
V228-V230 recovery comments and one recovery bookmark.

## 9. Validation and products

- ordinary and `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation pass;
- Web and Wasmtime configure/build complete successfully after repairing an
  accidentally cached empty-EMSDK toolchain path with the original presets;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- both configured CTest trees report no registered tests;
- FUNCTION, GLOBAL, DATA, and `name` are byte-size identical to V230;
- the trace-disabled narrow guard adds `0x44` bytes of CODE, so each module is
  68 bytes larger than V230; native behavior is unaffected because the extra
  branch belongs only to the explicitly port-only trace sidecar.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,658,131 B | `F341AA68645C9A411E58B42B1687EA6C682F5E34D4172F5859AF5B867C2C9FB6` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,005,272 B | `B299F6C4A043E37508D583FB111E718233F1ADEFFC2089BCFDA211CA7C7ECED8` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41C80` | `0x19E9C2E` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185DD5` | `0x3141C6B` |

## 10. Limits and next boundary

- the repaired armv7 database preserves the earlier healthy recovery plus
  concentrated V228-V230 notes, but not every individual line comment/bookmark
  that existed only in the corrupt V230 root; later slices should continue
  normal four-end writeback and can progressively restore equivalent detail;
- no crashing fixture executes the unchecked replacement-key miss because the
  native result is an invalid access;
- the next high-value boundary is the exact `SourceState` copy/reset/destructor
  and render-consumer chain, including all writers that can replace `path`
  during atlas re-entry;
- this slice does not complete the full motionplayer recovery goal.
