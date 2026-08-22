# motionplayer PreparedRenderItem selective constructor, native layout, sidecar, and publication audit

## 1. Scope and conclusion

V233 closes the portion left implicit by the earlier PreparedRenderItem
lifecycle report: the exact lazy-constructor write set, the full natural member
order, the node-owner publication commit, and the distinction between native
state and Web-only observation sidecars.

All four current reference binaries agree that a newly allocated item is not
zero-initialized as a whole. Its constructor establishes only:

- empty `ownerLabel`, `commandSrc`, and `commandKey` `ttstr` owners;
- `rawFlag16=false`, `drawFlag=false`, and `rawFlag20=false`;
- an empty borrowed-pointer `childItems` vector;
- `stencilComposite=0`;
- Void `commandVariant`, `leafLayer`, and `composedLayer` Variants;
- empty `commandCompositeMeshPoints`, `commandBezierPatchPoints`, and
  `meshPoints` vectors;
- `commandPatchDivision=0`.

Every other native scalar, pointer, color, geometry, viewport, and clip field
retains allocator bytes until the builder/render path that admits it writes it.
The only throwing operation in the native lazy-construction block is the
leading `operator new`; the `MotionNode` owner slot is written only after all
selective member stores complete. Allocation failure therefore leaves the slot
unchanged. Any later builder exception observes an already-published persistent
item with the prefix reached by that population path.

V233 also restores the exact declaration order. In particular, one physical
double is both render sort key and command coordinate Z. Native storage contains
only adjacent command X/Y after the matrix; it does not contain a duplicate
three-double `commandCoord` array. The integer `dirtyRect` and portable
`visibleAncestorIndex` are Web sidecars, not native item members.

## 2. Lazy ensure mapping

| target | ensure / first inline block | item size | node owner slot |
|---|---:|---:|---:|
| Android arm64 | inlined at `0x6C06C0` | `0x1B0` / 432 | `node+0x770` |
| Android armv7 | `ensureNodePreparedRenderItem_guess` `0x58BDF0` | `0x148` / 328 | `node+0x680` |
| iOS arm64 | `ensureNodePreparedRenderItem_guess` `0x1001157BC` | `0x1B0` / 432 | `node+0x780` |
| iOS armv7 | `ensureNodePreparedRenderItem_guess` `0x113108` | `0x148` / 328 | `node+0x65C` |

Android arm64 inlines the same constructor at five ensure sites. Their owner
publication stores are `0x6C0720`, `0x6C0BA0`, `0x6C0C64`, `0x6C0CF0`, and
`0x6C0D70`. The other three targets share one helper and return the existing
pointer immediately when the node slot is non-null.

## 3. Complete native member order

The table records source-level identity and target ABI offsets. It is analysis
evidence, not a request to hard-code native offsets in portable source.

| member | 64-bit references | 32-bit references |
|---|---:|---:|
| `ownerLabel` | `+0x000` | `+0x000` |
| `commandSrc` | `+0x008` | `+0x004` |
| eight flag/reference bytes | `+0x010` | `+0x008` |
| `childItems` begin/end/cap | `+0x018/+0x020/+0x028` | `+0x010/+0x014/+0x018` |
| `blendMode` | `+0x030` | `+0x01C` |
| `layerId1`, `layerId2` | `+0x034/+0x038` | `+0x020/+0x024` |
| shared command Z / `sortKey` | `+0x040` | `+0x028` |
| `commandMatrix[4]` | `+0x048` | `+0x030` |
| command X, Y | `+0x068/+0x070` | `+0x050/+0x058` |
| `originX`, `originY` | `+0x078/+0x080` | `+0x060/+0x068` |
| `corners[8]` | `+0x088` | `+0x070` |
| `packedColors[4]` | `+0x0A8` | `+0x090` |
| `paintBox[4]` | `+0x0B8` | `+0x0A0` |
| `viewport[4]` | `+0x0C8` | `+0x0B0` |
| `clipRect[4]` | `+0x0D8` | `+0x0C0` |
| `opacity` | `+0x0E8` | `+0x0D0` |
| `coordinateMode` | `+0x0EC` | `+0x0D4` |
| `objTriPriority` | `+0x0F0` | `+0x0D8` |
| `stencilComposite` | `+0x0F4` | `+0x0DC` |
| `commandKey` | `+0x0F8` | `+0x0E0` |
| borrowed `sourceState` | `+0x100` | `+0x0E4` |
| borrowed `parentItem` | `+0x108` | `+0x0E8` |
| `meshDivX`, `meshDivY`, `meshType` | `+0x110/+0x114/+0x118` | `+0x0EC/+0x0F0/+0x0F4` |
| `commandVariant` | `+0x11C` | `+0x0F8` |
| `leafLayer` | `+0x130` | `+0x104` |
| `composedLayer` | `+0x144` | `+0x110` |
| `commandCompositeMeshPoints` | `+0x158` | `+0x11C` |
| `commandPatchDivision` | `+0x170` | `+0x128` |
| `commandBezierPatchPoints` | `+0x178` | `+0x12C` |
| `meshPoints` | `+0x190` | `+0x138` |
| `renderLayerId` | `+0x1A8` | `+0x144` |

The flag byte order is `rawFlag16`, `skipFlag0`, `skipFlag1`, `drawFlag`,
`rawFlag20`, `rawFlag21`, `stencilMaskRef`, `stencilWriteRef`. Construction
writes only bytes 0, 3, and 4 within that group.

The layout also explains the four-reference get-command reads recovered
earlier: coordinate X/Y come from the two doubles after the matrix, while Z is
read from the same double used by stable sorting and camera projection.

## 4. Per-target selective construction

### Android arm64

The first inline block allocates at `0x6C06C0..0x6C06C4`. The stores at
`0x6C06D4..0x6C071C` initialize the selected flag, owner, Variant, and vector
state. `0x6C0720` publishes the pointer. The other four inline blocks are
byte-identical in semantics.

### Android armv7

The helper allocates at `0x58BDFC..0x58BE00`. Two empty strings, the selected
flags, child vector, Variant tags, stencil/command-key pair, and the exact
`0x2C`-byte owning tail are initialized at `0x58BE08..0x58BE2A`.
`0x58BE2E` publishes the pointer.

The tail clear begins at item `+0x118` and ends before `+0x144`. It covers the
composed Variant tag, all three MeshPoint vectors, and patch division, but
excludes trailing `renderLayerId`.

### iOS arm64

The helper allocates at `0x1001157D4..0x1001157D8`, performs the selective
stores through `0x10011582C`, and publishes at `0x100115830`. Individual
zero stores from item `+0x154` through `+0x16C` cover the composed Variant tag,
composite vector, and patch-division boundary without touching earlier POD.

### iOS armv7

The helper allocates at `0x113114..0x113118`, writes only the selected members
through three overlapping 16-byte owning-tail stores, and publishes at
`0x11315A`. The tail stores cover exactly item `+0x118..+0x143`; as on Android
armv7, `renderLayerId` at `+0x144` remains unwritten.

## 5. C++ value-initialization boundary

The portable ensure sites use `new PreparedRenderItem()`. A constructor
explicitly defaulted on its first declaration is not user-provided; C++
value-initialization would first zero-initialize the complete object before
running that defaulted constructor. Merely deleting scalar in-class initializers
would therefore not recover native behavior.

The local `PreparedRenderItem()` now has an explicit empty body. This makes it
user-provided, so the base is default-initialized and executes only its selected
member initializers/default constructors. It prevents the hidden whole-object
zero pass while preserving the existing allocation call sites.

The owner/container default constructors are allocation-free in the four native
helpers and compile to tag/pointer stores. Thus no partial item requires unwind
inside the constructor: `operator new` either fails before an object exists or
all nonthrowing stores finish and the slot is published.

## 6. Portable-source changes

`RuntimeSupport.h` now follows the complete native order:

- `blendMode` precedes layer IDs;
- the shared `sortKey` precedes the four-double matrix;
- `commandCoord` contains only X/Y;
- origins follow X/Y and precede corners;
- paint, viewport, and native float clip rectangles are consecutive;
- opacity, coordinate mode, triangle priority, and stencil follow;
- source/parent pointers precede mesh dimensions/type and the owning tail.

Only the native constructor defaults listed in section 1 remain. `dirtyRect`
and `visibleAncestorIndex` moved into the derived Web sidecar alongside
`nodeIndex`, `sourceKey`, cached local geometry, and `hasViewport`.

`PlayerRenderItems.cpp` writes only command X/Y and the shared sort/Z field.
`PlayerLayerQuery.cpp` serializes coord Z from `sortKey`, eliminating the
invented duplicate storage without changing script-visible values.

Direct unit fixtures now explicitly publish `parentItem`, `meshType`, and
viewport values before calling consumers that native control flow would reach
only after the corresponding builder writes. The tests no longer depend on
invented fresh-item POD defaults.

The declaration still preserves the proven reverse destructor chain:

```text
meshPoints
  -> commandBezierPatchPoints
  -> commandCompositeMeshPoints
  -> composedLayer
  -> leafLayer
  -> commandVariant
  -> commandKey
  -> childItems
  -> commandSrc
  -> ownerLabel
```

## 7. Wasm layout self-check

The Debug Web Wasm retains named out-of-line constructors. `llvm-objdump`
shows `NativePreparedRenderItemState::NativePreparedRenderItemState()` touching
only the selected native members. More importantly, its portable 32-bit offsets
now coincide exactly with both native 32-bit layouts:

- `stencilComposite` 220;
- `commandKey` 224;
- Variants 248/260/272;
- composite vector 284;
- patch division 296;
- remaining vectors 300/312;
- `renderLayerId` 324;
- native base size 328.

`PreparedRenderItem::PreparedRenderItem()` then begins its explicit Web sidecar
at offset 328. The integer dirty rectangle and ancestor index appear only in
that suffix. This is a useful ABI-shaped validation of source order while still
keeping native absolute offsets out of compiled source comments.

## 8. IDB writeback

All four canonical databases were opened and closed sequentially. V233 added:

- 52 line/function comments (`16`, `11`, `13`, `12` by target order);
- 17 bookmarks (`5`, `4`, `4`, `4`);
- function types for the three non-inlined ensure helpers.

The existing `_guess` helper names were retained. Each database was saved and
passed a health probe; the V231-repaired iOS armv7 database remained healthy.
All IDA sessions were closed after writeback.

## 9. Validation and products

- ordinary and `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation pass;
- Web and Wasmtime full rebuilds complete successfully;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- both configured CTest trees report no registered tests;
- FUNCTION, GLOBAL, and `name` section sizes are unchanged from V232;
- CODE is `0x26E` (622) bytes smaller on both configurations;
- DATA is `0x20` (32) bytes smaller on both configurations;
- total module size is 654 bytes smaller on both configurations.

| product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,365 B | `1288D0E7997335BD0F83697EF73BC7364423D4003AB5B1F4434A2B1F7AE3C91D` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,506 B | `BAABB827D576FB6B6FFFDED474B6016B504BAB8BA0D84B7F021CDBAB8B2397A3` |

| section | Web | Wasmtime | delta from V232 |
|---|---:|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` | `0` |
| GLOBAL | `0xD5C2` | `0xD5EA` | `0` |
| CODE | `0x1A419A2` | `0x19E9950` | `-0x26E` each |
| DATA | `0x5A3EE0` | `0x5A1130` | `-0x20` each |
| name | `0x3185DD5` | `0x3141C6B` | `0` |

The remaining warnings are the existing `_tss`, `imagepacker.h` attribute,
pthread-memory-growth, JSPI, and Emscripten JS-library warnings. V233 introduces
no new warning family.

## 10. Limits and next boundary

Like V232, this recovery intentionally represents native dormant storage. It
does not make arbitrary reads of unwritten C++ scalar fields portable; supported
control flow must reach the matching writer before consumption. Test fixtures
that instantiate an item directly must initialize every native field they read.

The next PreparedRenderItem boundary is no longer constructor/layout work.
V234 has now closed the ordinary-item persistent overwrite sequence and its
command-key exception prefix; see
`motionplayer_prepared_ordinary_overwrite_exception_prefix_four_binary_2026-08-18.md`.
Type-3 wrappers, stencil parents/masks, and render-layer materialization remain
separate paths: each commits a different subset and preserves a different stale
suffix on exception.
