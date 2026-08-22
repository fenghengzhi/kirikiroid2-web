# Motion.Player dead `renderToLayer` wrapper audit (four references, 2026-08-16)

## Scope

This audit tests the local `Player::renderToLayer(iTJSDispatch2 *, bool)` member against all four binaries under `reference/binaries/`. The local member was not registered or called anywhere in the current source tree. It duplicated the ordinary Layer branch of `Player::draw`, added a `skipUpdate` mode, and could issue a second native `Layer::Update(false)` after the recovered post-draw helper.

The negative conclusion is deliberately limited: a completely inlined or link-time-eliminated source helper cannot be disproved from stripped binaries. What the four references do prove is that the local out-of-line member, its public-looking name, its second update edge, and its `skipUpdate` ABI have no recovered binary-observable counterpart.

## Recovered entry points

| Target | `Player_draw_guess` | `Player_renderToCanvas_guess` | `Player_updateLayerAfterDraw_guess` | Player registrar |
|---|---:|---:|---:|---:|
| arm64-v8a | `0x6D3398` | `0x6C4820` | `0x6CBBB8` | `0x6D3DA8` |
| armeabi-v7a | `0x597864` | `0x58E2CC` | `0x59327C` | `0x597EC8` |
| macOS x86-64 | `0x100123C84` | `0x1001186E0` | `0x10011E6CC` | `0x1001244F8` |
| Windows x86 | `0x122F28` | `0x11653C` | `0x11CF20` | `0x123848` |

## Fresh four-binary call-graph evidence

The complete `Player_draw_guess` body has the same routing skeleton on every architecture:

1. A `D3DAdaptor` target dispatches to `Player_renderToD3DAdaptor_guess` and returns.
2. A `SeparateLayerAdaptor` target dispatches to `Player_renderToSeparateLayerAdaptor_guess` and returns.
3. The ordinary Layer branch prepares the main/aux render lists, applies the recovered projection pass, calls `Player_renderToCanvas_guess`, then calls `Player_updateLayerAfterDraw_guess`.
4. The shared-D3D branch renders and captures through the process-global `D3DAdaptor`; it does not enter a second Layer wrapper.

Complete xrefs to `Player_renderToCanvas_guess` are exactly one code xref per target:

| Target | sole xref | containing function |
|---|---:|---|
| arm64-v8a | `0x6D3600` | `Player_draw_guess` |
| armeabi-v7a | `0x597A1A` | `Player_draw_guess` |
| macOS x86-64 | `0x100123EF0` | `Player_draw_guess` |
| Windows x86 | `0x1231B0` | `Player_draw_guess` |

Complete xrefs to `Player_updateLayerAfterDraw_guess` are also exactly one code xref per target:

| Target | sole xref | containing function |
|---|---:|---|
| arm64-v8a | `0x6D3614` | `Player_draw_guess` |
| armeabi-v7a | `0x597A28` | `Player_draw_guess` |
| macOS x86-64 | `0x100123F04` | `Player_draw_guess` |
| Windows x86 | `0x1231C2` | `Player_draw_guess` |

Therefore there is no second recovered caller with the local wrapper's `prepare -> projection -> renderToCanvas -> update` shape.

## Registration and symbol-name evidence

Searching the exact UTF-16LE byte sequence for `renderToLayer` returns zero matches in all four references. Typed IDB entity queries also return no exact `renderToLayer` function, name, or string. The only recovered Player function matching a broad `render.*to.*layer` query is `Player_renderToSeparateLayerAdaptor_guess`:

| Target | address |
|---|---:|
| arm64-v8a | `0x6D2A38` |
| armeabi-v7a | `0x597328` |
| macOS x86-64 | `0x1001233C8` |
| Windows x86 | `0x12257C` |

`Player_draw_guess` itself remains externally observable: its xrefs include the `EmotePlayer`/Primary draw wrapper on all four targets and registrar data references where the architecture exposes them. No corresponding registration row or wrapper edge exists for `renderToLayer`.

## Source correction

Removed:

- `Player::renderToLayer(iTJSDispatch2 *, bool skipUpdate = false)` declaration;
- the duplicate render-list preparation and canvas submission body;
- its binary-unsupported `skipUpdate` behavior;
- its extra conditional native `Layer::Update(false)` edge and wrapper-only diagnostics.

Retained as the recovered path:

- `Player::draw(tTJSVariant)` as the sole dispatch/ownership entry;
- `renderToCanvas_guess` as its ordinary-Layer rendering helper;
- `updateLayerAfterDrawRecovered_guess` as its sole post-render Layer update helper;
- the separate D3D, shared-D3D, and `SeparateLayerAdaptor` routes.

This is a source-surface and control-flow correction, not a claim that no optimized-away inline helper ever existed in the original source.
