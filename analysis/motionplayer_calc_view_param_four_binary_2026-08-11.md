# MotionPlayer `calcViewParam` four-binary recovery (2026-08-11)

## Scope and correction

This note records a fresh four-reference-binary recovery of the
`Motion.Player.calcViewParam` script method.  It also corrects the local model
that treated the method as a no-argument viewport-scalar cache.

The four references agree that `calcViewParam` is a two-argument, mutating
layer-export operation:

```cpp
void Player::calcViewParam(double frame, const tTJSVariant &viewParams);
```

It first evaluates the Player at an absolute frame position, then writes one
existing output object for every non-root node.  It does not create or retain a
Player-level `lastViewParam`, and it does not read scalar flip/opacity/visible/
slant/zoom mirrors.

## Four-target address map

| Target | Player registrar | registration site | body |
|---|---:|---:|---:|
| Android ARM64 | `0x6D3DA8` | name call at `0x6D605C`, body materialization at `0x6D6014/0x6D6024` | `0x6CE908` |
| Android ARMv7 | `0x597EC8` | `0x598756` | `0x594958` |
| iOS ARM64 | `0x1001244F8` | `0x1001251AC` | `0x1001201CC` |
| iOS ARMv7 | `0x123848` | `0x1243F2` | `0x11EED4` |

The Android ARM64 and both iOS databases initially displayed only `"c"` at
the registration call because IDA had made an overlapping interior string
item for `"alcViewParam"`.  The registration order immediately after
`contains`, the complete UTF-16 bytes, and the captured function address agree
with the unobscured Android ARMv7 row.

All four bodies were freshly decompiled before the local semantic edit.  They
were then named `Player_calcViewParam_guess`, typed as

```c
void __fastcall Player_calcViewParam_guess(
    void *self, double frame, const void *viewParams);
```

and freshly decompiled again.  The full parameter order is visible directly
on 32-bit ARM (`self`, aligned `double`, output-variant pointer).  On AArch64,
the same source order places `frame` in `D0` and the output reference in `X1`.

## Player and node layout evidence

| Target | requested frame | total frames | clamped frame | queuing/firstFrame | preview | node stride |
|---|---:|---:|---:|---:|---:|---:|
| Android ARM64 | `Player+1120` | `+1128` | `+456` | `+480/+481` | `+1092` | `2632` |
| Android ARMv7 | `Player+776` | `+784` | `+288` | `+312/+313` | `+744` | `2272` |
| iOS ARM64 | `Player+1008` | `+1016` | `+344` | `+368/+369` | `+980` | `2648` |
| iOS ARMv7 | `Player+708` | `+716` | `+228` | `+252/+253` | `+680` | `2228` |

The loop resolves node index `i+1` from the target's `std::deque` start/map
iterator and writes output element `i`.  Thus root node zero is deliberately
excluded.  The direct `*(Player+200)` / `*(Player+160)` root access seen in the
Android property getters is an optimized access through the deque start
iterator, not proof of a `std::vector` container.

## Common evaluation preamble

Ignoring the 32/64-bit ABI spellings, the common preamble is:

```cpp
double cursor = frame < 0.0 ? 0.0 : frame;
requestedFrame = cursor;
clampedFrame = cursor > totalFrames ? totalFrames : cursor;
queuing = true;
firstFrame = true;
frameProgress(0.0);
updateLayers();
```

The two flag bytes are written together as the word `0x0101`.  This is an
absolute seek/evaluate operation: the input is already in motion frames, not
milliseconds and not a delta.  `frameProgress` receives zero only after the
requested/clamped cursor and both flags have been installed.

## Output-container contract

`viewParams` is copied into a temporary Variant/closure and converted to its
object dispatch.  For each non-root node, the body performs an indexed get on
the outer object and converts that existing element to another object dispatch.
The native function does not allocate the outer array or the per-node objects.

For a visible/exportable node, its existing dictionary receives these members
with `TJS_MEMBERENSURE`:

| Key | Source |
|---|---|
| `visible` | `true` |
| `src` | active clip slot source string |
| `blendMode` | active clip slot blend mode |
| `originX`, `originY` | active clip slot origin doubles |
| `opacity` | accumulated node opacity integer |
| `mbp` | flattened current mesh-control-point array, or Void |
| `cmesh` | newly-created mesh-inheritance array |
| `clip` | newly-created clip dictionary, or Void |

It then gets three already-existing child arrays from that same dictionary and
overwrites their numeric elements:

| Key | Required existing shape | Values written |
|---|---|---|
| `coord` | Array with indices `0..2` | accumulated X, Y, Z doubles |
| `color` | Array with indices `0..3` | four zero-extended packed 32-bit colors |
| `matrix` | Array with indices `0..3` | accumulated `m11,m12,m21,m22` doubles |

There is no shape or count repair.  The outer element and its `coord`, `color`
and `matrix` members are read as objects and used directly.

### Visibility gate

The full-data branch is entered only when all of the following hold:

```cpp
(node.type == 0 || node.type == 6 ||
 (node.type == 3 && player.preview)) &&
node.accumulated.active &&
node.accumulated.visible
```

On failure, the function writes only `visible=false`.  It deliberately leaves
every other existing property and child-array element untouched.  This makes
the caller-owned dictionaries reusable across calls without reconstructing
their fixed array members.

### `mbp`

`mbp` is a fresh flattened `[x0,y0,x1,y1,...]` Array only when
`meshType == 1` and the node's current mesh-control-point vector is non-empty.
Otherwise the property is explicitly assigned Void.  It is not a retained
Player cache and is independent from the inherited mesh chain below.

### `cmesh`

`cmesh` is always replaced by a fresh Array, including the empty-chain case.
The function creates one reusable separator dictionary for that Array:

```text
{ type: "mesh.inherit.separator" }
```

It first appends that separator when the current node has its mesh-combine flag,
then walks `node.meshAncestor` toward the root.  At each ancestor it:

1. appends the same separator object when that ancestor has mesh-combine set;
2. when the ancestor has active mesh data, appends a fresh dictionary:

```text
{
  type: 1,
  division: min(
    sat_u32(meshDivisionRatio * uint32(raw ancestor.meshDivision bits)),
    50u
  ),
  invOffset: [
    m11 * -offX + m12 * -offY,
    m21 * -offX + m22 * -offY
  ],
  invMatrix: [m11, m12, m21, m22],
  patch: [x0, y0, x1, y1, ...] // transformed control-point vector
}
```

The separator is allocated once per layer export and AddRef'd for repeated
array insertion.  Mesh dictionaries and their arrays are separately owned by
their Variants.

### `clip`

When the node's inherited clip-AABB pointer is non-null, `clip` is replaced by
a fresh Dictionary:

```text
{
  left, top, right, bottom,
  width:  right - left,
  height: bottom - top
}
```

When the pointer is null, `clip` is explicitly assigned Void.

## Lifetime and data flow

- The outer output Variant is retained for the duration of the call and
  released after the loop.
- Each indexed element Variant is retained/converted to a dispatch and released
  at the end of that iteration.
- `mbp`, `cmesh`, separator, mesh dictionaries and `clip` are temporary owned
  objects transferred into the caller's dictionaries through Variant property
  assignment; the Player stores none of them.
- `coord`, `color` and `matrix` remain caller-owned objects.  Only their numeric
  elements are overwritten.
- All four xref/caller queries report no native caller beyond the NCB registrar.
  Fresh decompilation of the adjacent script-facing `draw` callbacks likewise
  shows no call to this function.

## Boundary behavior and target differences

- Negative finite frames and negative infinity become zero.  Values above the
  cached total retain the original requested-frame field but clamp the evaluated
  frame to the cached total.
- AArch64 emits `if (frame < 0) frame=0`, while both ARMv7 builds emit a
  zero-initialized temporary followed by `if (frame >= 0) temporary=frame`.
  Therefore a raw NaN is preserved on the observed AArch64 instruction path
  but becomes zero on ARMv7.  This is outside ordinary finite script input but
  is an observable four-binary edge difference.
- The loop assumes the constructor-created root invariant.  A one-node Player
  performs the evaluation preamble and no output writes.
- The native body performs no explicit null/type/count checks before converting
  `viewParams`, each outer element, and the three child arrays to dispatches.
  Invalid shapes follow the runtime Variant/dispatch failure path rather than
  being silently repaired.
- `division` interprets the ancestor slot as raw `uint32`, multiplies the raw
  Player ratio, performs a saturating unsigned-int32 toward-zero conversion,
  and only then applies an unsigned integer `>=50 ? 50` cap.  Consequently
  NaN, negative products, `-Inf`, and `Inf * 0` export `0`, while positive
  overflow and `+Inf` export `50`; this exact boundary is closed separately in
  [`motionplayer_calc_view_division_conversion_four_binary_2026-08-14.md`](motionplayer_calc_view_division_conversion_four_binary_2026-08-14.md).
- Android and iOS differ in deque/Variant ABI, node stride and cleanup code, but
  the property order, eligibility gate, nesting, retain/release ownership and
  values are common.

## Pre-edit local comparison

The local `Player::calcViewParam()` is unrelated to the four recovered bodies:

1. It takes no arguments instead of `(double frame, viewParams)`.
2. It does not install the absolute cursor, set `queuing/firstFrame`, call
   `frameProgress(0)`, or call `updateLayers`.
3. It allocates a seven-member dictionary containing `flip`, `opacity`,
   `visible`, `slant`, `zoom`, `zFactor` and `colorWeight`; none of those keys or
   that dictionary exists in the native method.
4. It stores the dictionary in `_lastViewParam`, a local field with no reader.
   No native calc body writes a Player-owned Variant.
5. Its five transform/display inputs are port-only scalar mirrors.  The freshly
   audited root setters do not maintain them, and the native calc body never
   reads them.
6. The former local no-argument `draw()` helper called this scaffold. All four
   native calc bodies have registrar-only references; the four script-facing
   draw bodies do not call it. A later source-structure audit removed that
   unregistered, test-only helper entirely.

The intended reconstruction replaces the method with the two-argument
in-place exporter, removes the disproved cache and dead mirrors, and removes
the port-only no-argument draw call. The later 2026-08-16 direct-draw xref audit
removed the now caller-free public helper; tests enter the explicit
render-preparation test hook instead.

## Applied reconstruction

The local implementation now follows the recovered two-argument contract:

- `Player::calcViewParam(double, tTJSVariant)` performs the absolute seek
  preamble and exports every non-root node into the caller's existing element;
- visible nodes receive native-order `src`, blend/origin/opacity, `mbp`,
  `cmesh`, `clip`, coordinate, color and matrix writes;
- ineligible nodes receive only `visible=false`, preserving stale caller-owned
  members exactly as the four bodies do;
- one separator Dictionary is reused within each layer's fresh `cmesh` Array,
  while every mesh record and clip Dictionary has its own Variant lifetime;
- `_lastViewParam` and the disproved Player-only flip/visible/opacity/slant/zoom
  mirrors were removed;
- the no-argument C++ draw helper first stopped gating on the removed visibility
  mirror/calling the independent export, and was later removed after fresh
  source-graph evidence showed it was test-only and absent from the references.

The implementation uses a calc-specific family of process-wide TJS member-hint
slots.  Identically spelled properties in other exporters do not alias those
slots, matching the distinct globals observed in the reference body.

The existing motionplayer unit fixture now creates the required outer Array,
per-layer Dictionaries and fixed `coord`/`color`/`matrix` child Arrays,
then checks the exported source, opacity, composite mesh, array sizes and the
absence of the obsolete viewport-scalar `flip` member.

## IDB improvements and build verification

All four bodies are saved as `Player_calcViewParam_guess` with the recovered
parameter order and function type.  The overlapping UTF-16 member-name items
are saved as `aCalcViewParam_utf16_guess`.  Fresh post-type decompilation was
performed in every database before the semantic edit, and all four IDBs were
saved afterward.

The reconstructed source passes:

- `cmake --build --preset "Web Debug Build"`, including final `index.html`
  link and shell-memory synchronization;
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel`,
  including final guest-Wasm link and exnref conversion;
- `git diff --check`; the only emitted messages are the repository's existing
  LF-to-CRLF working-copy warnings.

The dedicated native Catch2 result is recorded once the separate Windows test
configuration finishes its first-time vcpkg dependency installation.
