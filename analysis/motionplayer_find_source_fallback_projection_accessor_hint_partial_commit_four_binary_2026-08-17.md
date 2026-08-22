# motionplayer find-source fallback projection, accessor ownership, member-hint block, and partial-commit four-binary audit

## 1. Scope and conclusion

V228 recovered the outer find-source routing and the generic fallback join.
V229 follows the successful generic branch from the script-visible
`ResourceManager.findSource` call through the complete `SourceState` property
projection and owner cleanup.

All four references implement the same semantic sequence:

```text
status = rm.findSource(contextVariant, pathVariant, &source.object)

if status != 0:
    source.valid = false
    return
if source.object.Type == Void:
    source.valid = false
    return

source.valid = true

sourceReceiverSetup = Variant(source.object)
sourceAccessor.dispatch = sourceReceiverSetup.AsObject() // strict, AddRef
destroy sourceReceiverSetup

source.width   = propGetReal(sourceAccessor, "width",   &widthHint)
source.height  = propGetReal(sourceAccessor, "height",  &heightHint)
source.originX = propGetReal(sourceAccessor, "originX", &originXHint)
source.originY = propGetReal(sourceAccessor, "originY", &originYHint)
source.blank   = propGetBool(sourceAccessor, "blank",   &blankHint)
clip           = propGetVariant(sourceAccessor, "clip", &clipHint)

if clip.Type == Object:
    clipReceiverSetup = Variant(clip)
    clipAccessor.dispatch = clipReceiverSetup.AsObject() // strict, AddRef
    destroy clipReceiverSetup

    source.clipLeft   = propGetReal(clipAccessor, "left",   &leftHint)
    source.clipTop    = propGetReal(clipAccessor, "top",    &topHint)
    source.clipRight  = propGetReal(clipAccessor, "right",  &rightHint)
    source.clipBottom = propGetReal(clipAccessor, "bottom", &bottomHint)
    clipAccessor.Release()
else:
    source.clip = [0, 0, 1, 1]

source.textureRect.left = 0
source.textureRect.top = 0
source.textureRect.right = fpToSigned(width)
source.textureRect.bottom = fpToSigned(height)

destroy clip
sourceAccessor.Release()
```

The most important recovered boundaries are:

- the caller tests raw status against zero, not `TJS_FAILED`;
- `valid=true` is committed before strict Object conversion and every getter;
- property helpers ignore ordinary `PropGet` status and convert the resulting
  Variant anyway;
- the source accessor owns the original dispatch independently, so re-entrant
  getters may replace `source.object` without redirecting later reads;
- `clip` has its own retained Variant plus a second independent accessor;
- eleven adjacent mutable member-hint words are process-wide globals;
- getter/conversion exceptions preserve `valid=true` and the field prefix
  already stored;
- IDA's one-character `f/o/c/l/t/r/b` strings are display artifacts: UTF-16LE
  byte searches prove the full member names in all four images.

No behavior change was required in portable code. V229 replaces incomplete or
misleading comments and recovers helper/global identities in the IDBs.

## 2. Four-image mapping

| Semantic boundary | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `findSource` FuncCall | `0x692810` | `0x570A1A` | `0x1000F38A8` | `0xF00AE` |
| raw nonzero-status gate | `0x692814` | `0x570A1C` | `0x1000F38AC` | `0xF00B2` |
| post-success Void gate | `0x692860` | `0x570A4E` | `0x1000F3900` | `0xF0100` |
| early `valid=true` | `0x692868` | `0x570A52` | `0x1000F3908` | `0xF0104` |
| source-object Variant copy | `0x692874` | `0x570A58` | `0x1000F3914` | `0xF010E` |
| strict source accessor/AddRef | `0x6928A4` inline | `0x570A68` | `0x1000F392C` | `0xF0128` |
| setup Variant destruction | `0x6928C8` | `0x570A70` | `0x1000F3938` | `0xF0130` |
| first real getter (`width`) | `0x6928E8` | `0x570A88` | `0x1000F3958` | `0xF0154` |
| Boolean getter (`blank`) | `0x692978` | `0x570B06` | `0x1000F39E8` | `0xF020C` |
| Variant getter (`clip`) | `0x6929B0` inline | `0x570B1E` | `0x1000F3A10` | `0xF0238` |
| clip Object-type gate | `0x6929D0` | `0x570B26` | `0x1000F3A1C` | `0xF0240` |
| strict clip accessor/AddRef | `0x692A04` inline | `0x570B3A` | `0x1000F3A38` | `0xF0258` |
| first clip getter (`left`) | `0x692A58` | `0x570B56` | `0x1000F3A64` | `0xF0284` |
| non-Object clip defaults | `0x692A10` | `0x570BCA` | `0x1000F3AF8` | `0xF0332` |
| clip accessor Release | `0x692AE4` | `0x570BC6` | `0x1000F3AF0` | `0xF032C` |
| textureRect synthesis | `0x692AF0` | `0x570BF4` | `0x1000F3B04` | `0xF035A` |
| clip Variant destruction | `0x692B00` | `0x570C02` | `0x1000F3B1C` | `0xF0378` |
| source accessor Release | `0x692B1C` | `0x570C1C` | `0x1000F3B38` | `0xF0390` |

V228 had placed its Android arm64 and Android armv7 generic-dispatch comment
on the later Void gates because decompiler block presentation obscured the
actual indirect calls. V229 disassembly corrects those two comments while the
iOS call annotations were already on the true call instructions.

## 3. Recovered property-helper family

Three helper shapes are common to the projection:

| Helper | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Motion_propGetReal_guess` | `0x65FA48` | `0x4C779C` | `0x1000F1760` | `0xEDA64` |
| `Motion_propGetBool_guess` | `0x660AB4` | `0x552124` | `0x1000F3078` | `0xEF7F0` |
| `Motion_propGetVariant_guess` | inlined | `0x55218C` | `0x1000F1860` | `0xEDBF0` |
| strict `tTJSVariant_AsObject_AddRef_guess` | inlined | `0x495308` | `0x100030294` | `0x125338` |

Each named-property helper receives an accessor object, loads its retained
dispatch field, and calls:

```text
dispatch.PropGet(flags, member, hint, &temporary, dispatch)
```

`objthis` is the same dispatch, flags are zero in this caller, and the caller's
shared hint address is forwarded unchanged.

The `PropGet` status is ignored. The helper then:

- calls `Variant::AsReal` and returns a real;
- calls Variant truth conversion and returns a Boolean; or
- copy-constructs the temporary into caller output.

The temporary Variant is destroyed after conversion/copy. Unwind cleanup also
owns it if the getter or conversion throws.

An ordinary nonzero `PropGet` result that leaves the initialized temporary Void
therefore does not directly fail source resolution: real conversion yields the
Variant's Void conversion behavior, Boolean conversion yields false, and the
Variant getter supplies Void so clip takes its default branch. A thrown C++/TJS
exception still unwinds and preserves the caller's partial descriptor state.

## 4. Raw call status and output alias

The fallback calls `findSource` with flags zero, two arguments, and the
ResourceManager dispatch as both receiver and `objthis`. The argument array is:

1. the retained context Variant copied at resolver entry;
2. the constructed path Variant.

The result pointer is `&SourceState.object`, not a temporary. Thus the script
call can replace or partially mutate the persistent result even if it returns a
failure status.

The first gate is precisely `status != 0`. Positive non-success codes and
negative failures are treated identically. On either, `valid=false` is written
and the result object is not rolled back.

Only status zero reaches the second gate, which checks the Variant type word for
Void. Void also writes false. A non-Void wrong type is accepted by this gate and
fails later at strict Object conversion, after true has already been committed.

## 5. Early valid commit and strict source receiver

`valid=true` precedes the copy and conversion of `SourceState.object`.
Observable cases include:

| Result | State after next boundary |
|---|---|
| non-Object non-Void | strict conversion may convert/throw; valid remains true |
| typed-null Object | type gate passes, accessor dispatch is null; valid remains true before later invalid access |
| nonnull Object | accessor AddRefs dispatch and projection continues |
| getter throws | valid true plus previous field prefix |
| getter status nonzero only | helper converts its result and projection continues |

Accessor construction uses an intermediate Variant copy. Strict `AsObject()`
AddRefs a nonnull dispatch, after which the setup Variant is destroyed. The
accessor's added reference then becomes the sole dedicated projection owner.

This design is deliberately resilient to re-entrant property getters replacing
`SourceState.object`: the remaining getters continue to call the original
retained dispatch. The persistent object may nevertheless show the re-entrant
replacement when the resolver returns.

## 6. Scalar field order and partial commits

The exact store order is:

1. width;
2. height;
3. originX;
4. originY;
5. blank;
6. retained clip Variant.

Each helper completes its temporary destruction before the caller stores the
converted scalar. Consequently an exception inside a property's `PropGet` or
conversion does not store that property, but every earlier store remains.

The hint slots are shared with other plugin call sites. Re-entrant getters can
read/update the same process-global words, but they cannot redirect the retained
source receiver. Concurrent access has no local locking in the plugin.

## 7. Clip Variant and second accessor owner

The clip getter follows the Variant-returning helper shape; Android arm64
inlines it into the resolver. Its temporary is copied into a retained `clip`
Variant and destroyed before the type gate.

Only `clip.Type() == Object` enters property projection. This tests type, not
nonnull dispatch:

- a nonnull Object creates a second accessor AddRef;
- a typed-null Object creates a null accessor and later property access is an
  invalid receiver boundary;
- Void, integer, real, string, or octet clip values take defaults without
  conversion.

The second accessor is constructed from another setup copy, which is destroyed
immediately after strict conversion. It owns the clip dispatch across all four
getters. The retained `clip` Variant remains live as a separate owner until
after textureRect stores.

On the Object route, commit order is left, top, right, bottom. An exception at
right leaves newly written left/top and old right/bottom. The clip accessor is
released after all four reads and before rectangle synthesis.

On the non-Object route no script call occurs. The native code writes the
complete default quartet `[0,0,1,1]`, then continues to rectangle synthesis.

## 8. UTF-16 display trap resolved by byte search

Several IDBs had inferred narrow one-character strings at wide literal
addresses. Pseudocode therefore displayed combinations such as:

- `"f"` for `findSource`;
- `"o"` for `originX` or `originY`;
- `"c"` for `clip`;
- `"l"`, `"t"`, `"r"`, or `"b"` for clip edges.

Cross-encoding byte searches in all four databases matched the complete
UTF-16LE NUL-terminated sequences for `findSource`, `originX`, `clip`, `left`,
`top`, `right`, and `bottom` at the exact operands used by this function.

Therefore the portable full member names are correct. Changing them to single
letters based on decompiler rendering would have introduced a new bug. This is
also why V229 records both disassembly and raw byte evidence in the IDBs.

## 9. Process-wide member-hint block

The fallback uses eleven adjacent mutable 32-bit globals in this exact order:

```text
findSource
width
height
originX
originY
blank
clip
left
top
right
bottom
```

Block starts are:

| Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x1AB5208` | `0x111173C` | `0x101B696D0` | `0x187D400` |

These are plugin/process globals, not fields in Player, MotionNode, SourceState,
or an accessor. The same width/height/etc. slots have xrefs from other plugin
methods, confirming the portable shared declarations rather than per-function
static duplicates.

Each database now names and types all eleven words. The IDB names remain
`_guess` because stripped binaries do not prove original source identifiers.

## 10. textureRect conversion and owner tail

After clip projection/defaults, native writes rectangle left/top as zero and
converts source width/height toward signed integers:

- Android arm64 uses scalar `FCVTZS` into 32-bit registers;
- Android armv7 and iOS armv7 use `VCVT.S32.F64`;
- iOS arm64 uses vector `FCVTZS` to signed 64-bit lanes followed by narrowing
  to signed 32-bit lanes.

For finite in-range values these are truncations toward zero and match the
portable `static_cast<int>` payload. NaN, infinities, and out-of-range values
remain a target-instruction portability boundary: portable C++ does not promise
the architecture-specific invalid-conversion result.

The cleanup order after rectangle stores is:

1. destroy retained clip Variant;
2. reset accessor vtable state;
3. release source accessor dispatch;
4. join the outer tail that destroys path/context temporaries.

On the Object clip route, the clip accessor dispatch was already released
before the rectangle conversion. On the default route no clip accessor exists.

## 11. Exception/state matrix

| Failure point | valid | object | scalar/clip fields | rect | owners cleaned |
|---|---:|---|---|---|---|
| nonzero FuncCall status | false | direct call result | old | old | path/context normal tail |
| status zero + Void | false | Void | old | old | path/context normal tail |
| source strict conversion | true | non-Void result | old | old | setup temp unwinds |
| width getter/conversion | true | live/re-entrant result | old | old | source accessor unwinds |
| height getter/conversion | true | live/re-entrant result | width new | old | source accessor unwinds |
| origin/blank getter | true | live/re-entrant result | preceding prefix new | old | source accessor unwinds |
| clip getter throws | true | live/re-entrant result | scalar prefix new | old | source accessor unwinds |
| clip strict conversion | true | live/re-entrant result | scalar prefix new | old | clip + source owners unwind |
| clip edge getter | true | live/re-entrant result | earlier clip prefix new | old | both accessors unwind |
| full success | true | direct/re-entrant result | all projected/defaulted | synthesized | all released normally |

No branch snapshots and restores old descriptor bytes.

## 12. Portable-source changes

V229 makes documentation-only changes because the current implementation
already has the recovered behavior:

- `PlayerResource.cpp` now documents raw nonzero status, direct output alias,
  early `valid=true`, strict/typed-null receiver boundaries, independent source
  and clip owners, ignored `PropGet` status, process-global hints, full wide
  member names, default clip ordering, and FP-to-int portability;
- `MotionDispatch.h` records that helpers dispatch through the accessor-owned
  receiver, that Android arm64 inlines the Variant-returning clip helper, and
  that hint pointers are process-wide;
- `PlayerRender.cpp` records that the generic helper returns raw status and
  writes directly into the caller's persistent result.

No friendly status recovery, transactional projection, typed-null guard,
getter rollback, per-call hint cache, or saturation helper was introduced.

## 13. Recovery-IDB writeback

Across the four databases V229 applied:

- 14 helper function renames and matching prototypes;
- 44 named/typed member-hint globals (11 per image);
- 94 new or corrected comments, including correction of the two misplaced V228
  Android dispatch annotations;
- 32 bookmarks (8 per image) for call/status, valid commit, source owner,
  scalar chain, clip getter/type/edge chain, and rectangle/cleanup boundaries.

Android arm64 inlining explains why it has two helper functions instead of the
four standalone helper bodies retained by the other images.

All four recovery databases were saved sequentially and closed. The final IDA
session audit reports zero open sessions.

## 14. Validation and products

- `motionplayer-dll.cpp` passed ordinary and
  `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation;
- Web rebuilt 36 header-dependent steps and linked successfully;
- Wasmtime rebuilt 69 plugin/guest steps and linked successfully;
- a follow-up Wasmtime build reports `ninja: no work to do`;
- both CTest trees remain configured with no registered tests;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- scoped `git diff --check` succeeds with only existing LF-to-CRLF checkout
  warnings.

Because the source changes are comments only, both products are byte-for-byte
identical to V228.

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

- no registered runtime fixture injects getter status failures, re-entrant
  object replacement, typed-null clip, or per-field exceptions;
- nonfinite/out-of-range FP-to-integer behavior is documented from emitted
  target instructions but not normalized in portable source;
- member-hint use is named and traced here, but every other xref to the shared
  block is not re-audited in this slice;
- spec-2 Win PSB group lookup, texture cache publication, strict icon lookup,
  and their exception owner graph remain the next source-resolution boundary;
- this slice does not complete the full motionplayer recovery goal.
