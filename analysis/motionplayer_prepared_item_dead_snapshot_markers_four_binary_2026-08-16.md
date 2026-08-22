# PreparedRenderItem dead snapshot/build-marker removal (four references, 2026-08-16)

## Scope

The Web-derived tail of `detail::PreparedRenderItem` contained five persistent members with no data consumer:

```cpp
tTJSVariant sourceObject;
bool leafBuilt;
bool composedBuilt;
std::array<int, 4> builtRect;
bool executedDirect;
```

`sourceObject` was assigned from `node.source.object` during ordinary item population but never read. `leafBuilt` was reset and assigned the return value of the leaf-copy helper but never tested. `composedBuilt` was reset and set after group composition but never tested. `builtRect` was repeatedly converted and assigned after leaf, composition, direct, and buffered operations but never read. `executedDirect` was reset and assigned from the live local route decision but never read. The render calls, local route decision, and their mutations are live; only the stored results/snapshots are dead.

This audit combines that complete local use graph with fresh four-reference allocation/destruction evidence. It does not remove the Web fields that have actual consumers (`sourceKey`, `sourceTexture`, `sourceRect`, local geometry, build rectangle, or direct-execution state).

## Fresh four-reference allocation evidence

| Target | lazy item allocation/ensure | allocated bytes |
|---|---:|---:|
| arm64-v8a | inline in `Player_appendPreparedRenderItems_guess` `0x6BF714` | `0x1B0` (432) |
| armeabi-v7a | `ensureNodePreparedRenderItem_guess` `0x58BDF0` | `0x148` (328) |
| iOS arm64 | `ensureNodePreparedRenderItem_guess` `0x1001157BC` | `0x1B0` (432) |
| iOS armv7 | `ensureNodePreparedRenderItem_guess` `0x113108` | `0x148` (328) |

The arm64 builder repeats the same `operator new(0x1B0)` initialization at each inlined ensure site. The other three helpers allocate only when the MotionNode owner slot is null, initialize the exact ABI-sized owner/string/vector/Variant core, and publish that pointer back to the node. There is no second allocation or separately owned extension record.

## Fresh four-reference destruction evidence

| Target | item destruction location |
|---|---:|
| arm64-v8a | `PreparedRenderItem_destroy_guess` `0x6F21DC` |
| armeabi-v7a | `PreparedRenderItem_destroy_guess` `0x5AF2D0` |
| iOS arm64 | inline in `MotionNode_destroy_guess` `0x10012A48C` |
| iOS armv7 | inline in `MotionNode_destroy_guess` `0x1290A6` |

All four destructors have the same source-level owning suffix:

```text
meshPoints vector
commandBezierPatchPoints vector
commandCompositeMeshPoints vector
composedLayer Variant
leafLayer Variant
commandVariant Variant
commandKey string
childItems pointer-vector buffer
commandSrc string
ownerLabel string
operator delete(item)
clear MotionNode owner slot
```

The exact ABI coordinates are already recorded in `analysis/motionplayer_prepared_render_item_lifecycle_four_binary_2026-08-13.md`. For this audit, the decisive fresh observation is the closed owner count: exactly three item Variants are destroyed. A persistent `sourceObject` Variant after the native core would require a fourth owner destruction edge and a larger allocation; neither exists on any target.

The exact allocation also ends at the native core's final vector header. There is no tail space/lifecycle for persistent `leafBuilt`, `composedBuilt`, `builtRect`, or `executedDirect` fields. More importantly, the portable source itself has no read of any of them, so their stores cannot represent a missing control-flow decision.

## Source correction

Removed:

- `PreparedRenderItem::sourceObject` and its write-only assignment;
- `PreparedRenderItem::leafBuilt` and its reset/result stores;
- `PreparedRenderItem::composedBuilt` and its reset/success stores;
- `PreparedRenderItem::builtRect` and its write-only conversions/stores;
- `PreparedRenderItem::executedDirect` and its write-only reset/route-result stores.

Retained:

- the call to `emitPreparedLeafLayerCopy_guess`, now explicitly discarded with `(void)` so all copy/publication side effects remain;
- `composePreparedGroupLayers_guess` and the group clip publication;
- the local `useDirectRenderPath` decision and the direct/buffered branches it controls;
- the consumed native-core `dirtyRect` state;
- `sourceState` as the reference-backed borrowed descriptor authority;
- consumed Web backend snapshots and diagnostics.

Two nearby comments that still described the already removed Player by-name cache helper as existing compatibility code were also corrected to the current topology: the helper is gone and there is no second cache.

## Validation

- scoped member scan: no `entry.sourceObject`/`item.sourceObject` access remains, and `leafBuilt`/`composedBuilt`/`builtRect`/`executedDirect` have zero source matches;
- ordinary motionplayer unit translation-unit syntax: passed;
- the same syntax check with `KRKR2_WASMTIME_HEADLESS=1`: passed;
- `Web Debug Build`: completed 35/35 incremental steps and linked;
- `Wasmtime Headless Debug Build --target motionplayer`: completed 32/32 incremental steps and linked `libmotionplayer.a`;
- scoped patch whitespace check: passed.

The remaining `sourceObject` identifiers are live local parameters/temporary Variants used while issuing actual Layer source operations; they are not persistent `PreparedRenderItem` members and were intentionally retained.
