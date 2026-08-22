# motionplayer SourceState partial construction, copy, destruction, and consumer-gate four-binary audit

## 1. Scope and conclusion

V232 follows the complete persistent `MotionNode::SourceState` lifetime across
the four current reference binaries. It covers the true node constructor, its
common-field initializer, compiler-generated node copy assignment, node
destruction, and the two ordinary render-time consumer families.

The four targets agree on a boundary that the portable declaration previously
hid by over-initializing the record:

- construction establishes an empty `tTJSVariant object`, a null borrowed
  texture pointer, an empty retained `ttstr path`, and `valid=false`;
- construction does not write `blank`, the four size/origin doubles, the four
  clip doubles, or the four-integer texture rectangle;
- those scalar bytes are dormant while the source is not admitted; writer paths
  publish only the subset used by their node kind;
- geometry and ordinary prepared-item consumers apply their native validity and
  node-kind gates before reaching the relevant source payload;
- compiler-generated assignment copies owners and POD in declaration order,
  but never retains or releases the borrowed texture;
- destruction releases `path` before `object`, never touching `texture`.

This is a source-semantic recovery, not an ABI claim that the portable Wasm
object has one of the native target layouts.

## 2. Corrected function map

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| true MotionNode construction body | `0x6EED94` | `0x5ACC70` | `0x10014151C` | `0x1425BC` |
| common-field initializer | `0x696770` | `0x572A2C` | `0x1000F6580` | `0xF316C` |
| compiler-generated copy assignment | `0x6F1A6C` | `0x5AECA0` | `0x10014451C` | `0x144FCA` |
| MotionNode destruction | `0x6F206C` | `0x5AF220` | `0x10012A48C` | `0x1290A6` |

The Android arm64 name inherited from earlier recovery was stale. `0x696770`
is not the complete constructor: the true construction body is `0x6EED94`,
which establishes the member owners and reaches `0x696770` for common scalar
initialization. The canonical arm64 IDB now names these
`MotionNode_ctor_guess` and `MotionNode_initCommonFields_guess` respectively.
The older reports that carried the stale mapping were corrected in V232.

## 3. Cross-ABI SourceState layout

Offsets below are relative to the start of `SourceState`; the parenthesized
value is the physical offset in `MotionNode`.

| member | Android/iOS arm64 | Android armv7 | iOS armv7 |
|---|---:|---:|---:|
| `valid` | `+0x00` (`+0xC8`) | `+0x00` (`+0xB8`) | `+0x00` (`+0xB8`) |
| `blank` | `+0x01` (`+0xC9`) | `+0x01` (`+0xB9`) | `+0x01` (`+0xB9`) |
| `object` Variant | `+0x04` (`+0xCC`) | `+0x04` (`+0xBC`) | `+0x04` (`+0xBC`) |
| borrowed texture | `+0x18` (`+0xE0`) | `+0x10` (`+0xC8`) | `+0x10` (`+0xC8`) |
| width, height | `+0x20`, `+0x28` | `+0x18`, `+0x20` | `+0x14`, `+0x1C` |
| origin X, Y | `+0x30`, `+0x38` | `+0x28`, `+0x30` | `+0x24`, `+0x2C` |
| clip L, T, R, B | `+0x40..+0x58` | `+0x38..+0x50` | `+0x34..+0x4C` |
| four-int texture rectangle | `+0x60` | `+0x58` | `+0x54` |
| retained path `ttstr` | `+0x70` (`+0x138`) | `+0x68` (`+0x120`) | `+0x64` (`+0x11C`) |

Android armv7 aligns the first double at an eight-byte boundary; the iOS armv7
layout uses four-byte double alignment. That difference accounts for the
armv7 path offsets without changing the recovered declaration order.

## 4. Partial constructor boundary

### Android arm64

The construction body writes the empty Variant tag at `0x6EEDD4`, nulls the
texture at `0x6EEDD8`, and nulls the retained path backing at `0x6EEDDC`.
The later common initializer writes only the SourceState validity byte at
`0x696890`. There are no construction stores to blank, size/origin, clip, or
rectangle.

### Android armv7

The construction body nulls the path backing at `0x5ACC8E`, then establishes
the empty Variant tag and null texture at `0x5ACC92`. The only SourceState store
in the common initializer is `valid=false` at `0x572B2A`.

### iOS arm64

The owner stores are Variant tag `0x100141548`, texture null
`0x10014154C`, and path backing null `0x100141550`. The common initializer
writes `valid=false` at `0x1000F66A0` and no other SourceState payload.

### iOS armv7

The owner stores are Variant tag `0x1425F4`, texture null `0x142604`, and path
backing null `0x14260C`. The common initializer writes `valid=false` at
`0xF32F6` and leaves the rest of the scalar payload untouched.

Thus `blank=false`, zero dimensions/origins, unit clip-right/bottom, and a zero
rectangle were portable conveniences, not native construction behavior. A
fresh node may contain arbitrary prior allocator/placement bytes in these
fields until the writer path relevant to its node kind publishes them.

The local `SourceState::clear()` remains a deterministic reconstruction-harness
helper. It is not used by production motionplayer code, is not the native
constructor, and must not be substituted for the partial loader failure paths
recovered in V228-V231.

## 5. Compiler-generated copy assignment

All four copy-assignment bodies expose the same source declaration order:

1. copy the leading POD through the `valid/blank` prefix;
2. assign the Variant by retaining the source owner before releasing the old
   destination owner;
3. copy the texture pointer and scalar/rectangle payload as raw POD;
4. assign the path by retaining the source backing before releasing the old
   destination backing and committing the pointer;
5. continue into the two clip slots and later MotionNode members.

| target | prefix/Variant | POD payload | path retain | destination release | path commit |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6F1AF4` / `0x6F1AF8` | `0x6F1B08` | `0x6F1B0C` | `0x6F1B24` | `0x6F1B3C` |
| Android armv7 | `0x5AED0A` / `0x5AED1E` | `0x5AED2C` | `0x5AED30` | `0x5AED4C` | `0x5AED5E` |
| iOS arm64 | `0x100144598` / `0x1001445AC` | `0x1001445BC` | `0x1001445C0` | `0x1001445D8` | `0x1001445E8` |
| iOS armv7 | `0x14503E` / `0x145052` | `0x145060` | `0x145064` | `0x145080` | `0x145092` |

The borrowed texture pointer appears only inside the POD copy. There is no
texture `AddRef`, `Release`, or null-dependent branch. Retain-before-release
also makes path self-assignment and distinct `ttstr` objects sharing one backing
safe: the backing cannot reach zero between the two operations.

The SourceState segment has no allocation/container operation or explicit
exception edge. A later slot/vector assignment can fail after SourceState has
already committed, which is the native partial-assignment boundary. The local
defaulted `MotionNode` copy constructor and copy assignment now reproduce these
owner/POD semantics because `object` is a Variant, `texture` is raw borrowed
state, and `path` is the retained `ttstr` restored in V231.

## 6. Destruction order and ownership

The explicit MotionNode destructor body deletes the node's persistent prepared
render item first. Automatic member teardown occurs later in reverse declaration
order. Within SourceState this gives:

| target | release path owner | destroy/release Variant object |
|---|---:|---:|
| Android arm64 | `0x6F213C` | `0x6F214C` |
| Android armv7 | `0x5AF2B2` | `0x5AF2BA` |
| iOS arm64 | `0x10012A57C` | `0x10012A584` |
| iOS armv7 | `0x129180` | `0x129188` |

No target reads, clears, releases, or otherwise touches the texture pointer
during SourceState teardown. The atlas/module cache remains its sole owner.
The scalar payload has no destructor behavior.

## 7. Render-time admission gates

### Vertex/mesh source consumption

| target | validity gate | blank read inside admitted branch |
|---|---:|---:|
| Android arm64 | `0x6B9BD0` | `0x6BA424` |
| Android armv7 | `0x586A90` | `0x586EB8` |
| iOS arm64 | `0x10010FA94` | `0x100110024` |
| iOS armv7 | `0x10CEDC` | `0x10D2FC` |

The vertex path tests source validity together with its node-type eligibility
before materializing origins, dimensions, or blank. Mesh-child deformation has
the corresponding parent-active, parent-source-valid, mesh-kind and nonempty
control-grid gates before source origin/size reads.

### Ordinary prepared-item construction

| target | source address / validity admission | blank read after admission |
|---|---:|---:|
| Android arm64 | `0x6C06A4` / `0x6C06A8` | `0x6C0784` |
| Android armv7 | `0x58B3E8` / `0x58B3EE` | `0x58B45A` |
| iOS arm64 | `0x100114BB8` / `0x100114BC0` | `0x100114C44` |
| iOS armv7 | `0x112588` / `0x112592` | `0x112604` |

An invalid ordinary source skips before lazy item allocation and all source
payload reads. The type-3 wrapper path can borrow a source pointer for its child
list, but the wrapper itself is not emitted as an ordinary source-backed item;
that separate topology was recovered in the prepared-item audits.

Type-10 anchor feedback is also intentionally partial: it marks its internally
rendered source valid and publishes the width/height/origin/clip subset used by
the admitted anchor path. V232 therefore does not generalize the native rule to
"valid means every SourceState byte has been initialized". The real invariant
is path-specific publication followed by the matching valid/node-kind gates.

## 8. Portable-source changes

`cpp/plugins/motionplayer/MotionNode.h` now gives SourceState a constructor that
initializes only:

- `valid=false`;
- `texture=nullptr`;
- the automatically constructed empty `tTJSVariant object`;
- the automatically constructed empty `ttstr path`.

The in-class initializers for `blank`, width/height, origin, clip, and rectangle
were removed. Their declaration order, type, copy behavior, and physical Wasm
layout remain unchanged. The deterministic test helper still performs its full
explicit reset and is documented as non-native.

V232 also corrects stale Android arm64 constructor naming in:

- `motionplayer_anchor_feedback_four_binary_2026-08-12.md`;
- `motionplayer_motionnode_core_comment_migration_four_binary_2026-08-15.md`;
- `motionplayer_node_tree_child_lifecycle_four_binary_2026-08-12.md`;
- `motionplayer_visibility_four_binary_2026-08-12.md`.

The V231 path report now explicitly distinguishes the local full-reset helper
from native construction and failure behavior.

## 9. IDB writeback

All four canonical databases were updated sequentially and saved. V232 added:

- 72 line/function comments (`19`, `17`, `18`, `18` by target order);
- 32 bookmarks (eight per target);
- 16 function types (constructor, common initializer, assignment, destructor on
  each target);
- five corrected/recovered `_guess` names, including the Android arm64 ctor vs
  common-initializer split and three previously anonymous assignment bodies.

Each session passed its server-health probe before close. The repaired iOS
armv7 canonical database from V231 saved and reopened through its normal path;
all IDA sessions were closed after writeback.

## 10. Validation and products

- ordinary and `KRKR2_WASMTIME_HEADLESS=1` Emscripten syntax compilation pass;
- Web and Wasmtime builds complete successfully;
- Node constructs both outputs as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- both configured CTest trees report no registered tests;
- FUNCTION, GLOBAL, DATA, and `name` section sizes are identical to V231;
- CODE is exactly `0x70` (112) bytes smaller on both configurations, matching
  removal of the unsupported scalar constructor stores;
- total module size is exactly 112 bytes smaller on both configurations.

| product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,658,019 B | `D94CD18C0812B127AEFA4182ED2D582209A470CE770B382C87002D39AE732CA8` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,005,160 B | `55B2BBE97DA222B8E898AD5553EEBEF1307B7FD358B38880E98A8227F5926B04` |

| section | Web | Wasmtime | delta from V231 |
|---|---:|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` | `0` |
| GLOBAL | `0xD5C2` | `0xD5EA` | `0` |
| CODE | `0x1A41C10` | `0x19E9BBE` | `-0x70` each |
| DATA | `0x5A3F00` | `0x5A1150` | `0` |
| name | `0x3185DD5` | `0x3141C6B` | `0` |

The surviving warnings are pre-existing `_tss` literal-operator,
`imagepacker.h` attribute, pthread-memory-growth, JSPI, and Emscripten JS-library
warnings; V232 introduces no new warning family.

## 11. Limits and next boundary

V232 proves native write absence for the constructor and validates the recovered
consumer gates; it does not claim that reading an indeterminate C++ scalar is a
portable operation. The reconstruction avoids such reads on its supported
paths, mirroring the native control flow. Tests must explicitly write any field
they consume and must not assert invented fresh-node scalar defaults.

The next useful lifetime slice is the transition between SourceState and the
persistent `PreparedRenderItem`: exact allocation failure, source-pointer
borrowing, overwrite order, and destruction/rebuild behavior across ordinary,
type-3 wrapper, and stencil/priority publication paths.
