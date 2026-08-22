# Headless render-layer state isolation (four references, 2026-08-16)

## Scope

The portable `Player` type unconditionally carried two members that are read and written only inside a `KRKR2_WASMTIME_HEADLESS` block:

- `std::unordered_map<tjs_int, detail::LayerRenderState> _renderLayerStates`;
- `tjs_int _nextLayerAbsolute`.

`detail::LayerRenderState` itself was also declared unconditionally. Its comment described the counter as an independent Player semantic slot, even though the complete current source graph has no non-headless use. This audit separates the Wasmtime differential reconstruction from the ordinary plugin object layout without removing the headless behavior.

This correction does not claim that the original Player could never have any optimized-away layer bookkeeping. It makes the narrower, directly testable statement that this exact unordered-map snapshot model and sequence counter are test-side state: every local use is compile-time headless-only, while the four recovered production draw graphs use a different ownership path.

## Local compile-time boundary

The only map access and counter increment are in `PlayerRenderExecute.cpp` inside one `#if defined(KRKR2_WASMTIME_HEADLESS)` region:

```cpp
auto &state = _renderLayerStates[stateLayerId];
if(!state.initialized) {
    state.absolute = _nextLayerAbsolute++;
    // ...
}
```

No constructor, destructor, clear path, ordinary draw path, registrar, or non-headless helper reads either member. The type exists solely to support the headless layer-object reconstruction used by the differential Wasmtime guest.

## Four-reference production draw graph

Fresh decompilation of the complete draw dispatcher gives the same route on all targets:

| Target | `Player_draw_guess` | ordinary canvas helper | SLA helper |
|---|---:|---:|---:|
| arm64-v8a | `0x6D3398` | `0x6C4820` | `0x6D2A38` |
| armeabi-v7a | `0x597864` | `0x58E2CC` | `0x597328` |
| macOS x86-64 | `0x100123C84` | `0x1001186E0` | `0x1001233C8` |
| Windows x86 | `0x122F28` | `0x11653C` | `0x12257C` |

The ordinary branch owns two local prepared-item lists, applies projection, submits through the canvas helper, and runs the single post-draw update helper. D3D and `SeparateLayerAdaptor` targets branch to their typed helpers. None of the four complete dispatcher bodies looks up a Player-owned layer-id map, lazily creates a `LayerRenderState`, or increments a Player absolute counter.

Complete xrefs to the canvas helper are exactly one per target and all originate in `Player_draw_guess`; the corresponding evidence table is recorded in `analysis/motionplayer_player_dead_render_to_layer_wrapper_four_binary_2026-08-16.md`.

## `absolute` and `hitThreshold` ownership

The native motionplayer path that publishes `absolute` and `hitThreshold` is the `SeparateLayerAdaptor` resolver family. The payload-free ordinal resolver addresses are:

| Target | `SeparateLayerAdaptor_resolveLayerOrdinal_guess` |
|---|---:|
| arm64-v8a | `0x6C90C4` |
| armeabi-v7a | `0x591DEC` |
| macOS x86-64 | `0x10011C628` |
| Windows x86 | `0x11AE24` |

Fresh decompilation shows the same boundary on all four targets: resolve or create the SLA-owned Layer Variant, publish `absolute = adaptor base + current sequence`, then publish `hitThreshold = 256`. This payload-free overload does not increment the adaptor sequence.

Exact UTF-16LE searches further distinguish the owners:

- `hitThreshold` has one motionplayer literal family per target. Its motionplayer xrefs are only `SeparateLayerAdaptor_resolveLayerNode_guess` and `SeparateLayerAdaptor_resolveLayerOrdinal_guess`; there is no Player draw/constructor/destructor xref.
- `absolute` likewise has no Player draw xref. Its motionplayer references are the SLA registrar/assignment/resolver family and `PrivateMotionGLL`; unrelated engine/UI functions account for the additional literal families on some targets.

The reference-backed production state is therefore SLA-owned ordered-map/layer state. The portable `_renderLayerStates` unordered map is not a substitute for that owner and must not be presented as part of ordinary Player structure.

## Source correction

The following are now guarded by `KRKR2_WASMTIME_HEADLESS`:

- the `detail::LayerRenderState` forward declaration;
- the full `detail::LayerRenderState` definition;
- `Player::_renderLayerStates`;
- `Player::_nextLayerAbsolute`.

The headless execute path retains its reusable-Layer, `absolute`, and `hitThreshold` behavior, while its map/member lifetime remains automatic in the Wasmtime guest. Ordinary Web/plugin builds no longer add the test-only unordered-map header, mapped Variant owners, or counter to every root and child Player instance. The stale comment that promoted `_nextLayerAbsolute` to an ordinary Player semantic slot was replaced with its actual compile-time ownership boundary.

## Headless value-state pruning

A follow-up complete use scan of the now isolated `LayerRenderState` found that only three values cross statements:

- `initialized` gates the one-time absolute assignment;
- `absolute` is republished to the reusable Layer;
- `layerObject` owns that reusable Layer and is copied into the prepared item.

The remaining fields had no reader:

- `layerId`, `clipEnabled`, and `isDirty`;
- `layerGetter` (a Variant owner created and retained but never consumed);
- `clipRect`, `worldRect`, `localRect`, and `packedColors` snapshots.

`hitThreshold` was initialized to 256 and read only to publish the same constant. It is now written directly as 256 at the Layer property boundary. Removing the unused `layerGetter` also removes an otherwise unobservable LayerGetter adaptor allocation/lifetime extension from the headless reconstruction; the real registered `Player::getLayerGetter` API is unchanged.

The headless map value is therefore reduced to exactly `initialized`, `absolute`, and `layerObject`. This does not alter the ordinary plugin at all and preserves every observable headless Layer property/owner edge.

## Validation

- ordinary motionplayer unit translation-unit syntax check: passed;
- the same syntax check with `KRKR2_WASMTIME_HEADLESS=1`: passed;
- `Web Debug Build`: completed all 35 incremental steps and linked the Web executable;
- `Wasmtime Headless Debug Build --target motionplayer`: completed 32/32 incremental steps and linked the headless `libmotionplayer.a` successfully after the value-state pruning;
- scoped state scan: the headless value type contains only `initialized`, `absolute`, and `layerObject`; removed state-field accesses are zero;
- patch whitespace check: passed.

The umbrella `Wasmtime Headless Debug Build` progressed to its separate `krkr2_wasmtime_guest_objects` target and then failed because that target's existing include list cannot resolve `math/Mat4.h` from `DrawDeviceD3DIntf.h`. The installed header is under the vcpkg `include/cocos/math` subtree, while the failing compile line supplies only the parent `include` directory. This configuration failure is outside the guarded state change; the affected headless motionplayer source set itself compiled and linked through the dedicated target, and the explicit headless syntax check exercised the exact guarded declarations and uses.
