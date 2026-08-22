# Motion.Player methods and ground-correction callback — four-binary record (2026-08-11)

## Scope

This record audits the `Motion.Player` registrations for `onAction`, `onSync`,
and `onGroundCorrection`, plus the layer-evaluation call site that invokes
`onGroundCorrection`.  It replaces comments based only on an older
`libkrkr2.so` address map.

Authoritative targets:

- Android ARM64 `Kirikiroid2_1.3.9_Android_arm64-v8a.so`
- Android ARMv7 `Kirikiroid2_1.3.9_Android_armabi-v7a.so`
- iOS ARM64 `Kirikiroid2_1.3.9_iOS_arm64`
- iOS ARMv7 `Kirikiroid2_1.3.9_iOS_armv7`

All registrar windows, method bodies, ground-correction workers, and their
`Player_updateLayers` call windows were freshly disassembled/decompiled in the
current investigation.

## Registered method mapping

| Target | `onAction` registration / body | `onSync` registration / body | `onGroundCorrection` registration / body |
| --- | --- | --- | --- |
| Android ARM64 | `0x6D62B0` / `0x6D6E30` | `0x6D62BC` / `0x6D6E34` | `0x6D634C` / `0x6D6E38` |
| Android ARMv7 | `0x5987C8` / `0x599142` | `0x5987DE` / `0x599144` | `0x5987F4` / `0x599146` |
| iOS ARM64 | `0x100125250` / `0x10012595C` | `0x100125270` / `0x100125960` | `0x100125290` / `0x100125964` |
| iOS ARMv7 | `0x12448C` / `0x124B5C` | `0x1244AA` / `0x124B5E` | `0x1244C8` / `0x124B60` |

All three entries use Function-kind NCB registration, not property getters and
setters.  `onAction` and `onSync` are true empty bodies on all targets: one
`RET` on AArch64 or one `BX LR` in Thumb.

`onGroundCorrection` is different.  Each implementation copies its first
`tTJSVariant` argument into the return slot and ignores the Player object and
any later script argument.  Its common source behavior is:

```cpp
tTJSVariant Player::onGroundCorrection(tTJSVariant currentPosition) {
    return currentPosition;
}
```

This is the default identity callback used when a Player dispatch has not
overridden the method.  Treating it as `void` loses observable return behavior.

## Runtime worker and caller mapping

| Target | Ground worker | `Player_updateLayers` | gated call site | Current/parent xyz offsets |
| --- | ---: | ---: | ---: | --- |
| Android ARM64 | `0x6B7DF0` | `0x6B871C` | `0x6B8BD8` | `+1512/+1520/+1528` |
| Android ARMv7 | `0x585230` | `0x5856E0` | `0x585B72` | `+1272/+1280/+1288` |
| iOS ARM64 | `0x10010DFF4` | `0x10010E544` | `0x10010EAB0` | `+1528/+1536/+1544` |
| iOS ARMv7 | `0x10B8FC` | `0x10BE5C` | `0x10C542` | `+1240/+1248/+1256` |

In every target, `Player_updateLayers` reads the node's one-byte
`groundCorrection` flag immediately before the call and skips the worker when
it is zero. The caller passes the current `Player`, current node and actual
parent node. The worker follows `Player.rootPlayer` and returns without work
when that root Player's raw `currentDispatch` bridge slot is null.

## Player/root callback field identity correction (2026-08-14)

Fresh four-IDB caller and worker disassembly corrects the older shorthand
"layer/native wrapper" used in the first version of this record:

| Target | caller arg0 | root link | callback slot |
| --- | --- | ---: | ---: |
| Android ARM64 | current Player (`X19`) | `Player+0` | `rootPlayer+16` |
| Android ARMv7 | current Player (`R8`) | `Player+0` | `rootPlayer+8` |
| iOS ARM64 | current Player (`X19`) | `Player+0` | `rootPlayer+16` |
| iOS ARMv7 | current Player (`R12`) | `Player+0` | `rootPlayer+8` |

The first worker instructions are respectively equivalent to
`player->rootPlayer->currentDispatch`. This is the same non-owning raw bridge
slot written by script `play`/`progress`; a type-3 or particle child therefore
still invokes the outer root script wrapper. Engine-only progress supplies
null and intentionally suppresses the callback. There is no node-resident raw
layer dispatch involved.

## Common callback data flow

The four workers reduce to:

```cpp
if (node.groundCorrection &&
    player.rootPlayer.currentDispatch != nullptr) {
    Array current = [node.posX, node.posY, node.posZ];
    Array parent  = [parent.posX, parent.posY, parent.posZ];

    retained callback = AddRef(player.rootPlayer.currentDispatch);
    Variant result = callback.onGroundCorrection(current, parent);
    ncbPropAccessor corrected{tTJSVariant(result)};

    node.posX = corrected.getRealValue(0, 0.0);
    node.posY = corrected.getRealValue(1, 0.0);
    node.posZ = corrected.getRealValue(2, 0.0);
}
```

The argument order is load-bearing: **current node first, parent second**.
This also explains the default Player method: returning its first argument
preserves the already-computed current position.

The 64-bit implementations create two native TJS arrays and append the three
real variants directly to their Items deques.  The 32-bit implementations use
equivalent helper calls.  STL/ABI layout changes the offsets but not order or
ownership.

The callback name is UTF-16 `onGroundCorrection` on all four targets. Its
string addresses are `0x14D60E2`, `0xD85AD4`, `0x10195C64A`, and `0x174E9AE`;
the call uses one process-wide mutable member-hint slot at `0x1AB5420`,
`0x11118BC`, `0x101B698E8`, and `0x187D58C` respectively. Flags are zero and
the retained callback dispatch is also passed as `objthis`.

The exact owner sequence is:

1. create current Array, then parent Array, and append three `tvtReal` values
   to each;
2. AddRef the borrowed root `currentDispatch`;
3. CopyRef current and parent Array Variants into two call-argument Variants;
4. call `FuncCall`, then destroy argument Variants in parent/current order;
5. copy the callback result and construct an `ncbPropAccessor`, retaining its Object dispatch before the
   temporary copy dies;
6. parse and incrementally store x/y/z; each present index first uses a `MEMBERMUSTEXIST` probe and then
   performs a second flags-0 read and conversion;
7. release result dispatch, callback dispatch, result Variant, parent Array,
   then current Array.

Thus callback re-entry may replace or release script members without
invalidating either the active wrapper or returned Array during parsing.

## Result and failure boundaries

- The callback result is converted to a TJS object unconditionally.  A void,
  integer, string, or other non-object result raises the ordinary TJS
  variant-conversion error; it is not silently ignored.
- A null dispatch inside an object variant reaches the native null-dereference
  boundary.  There is no defensive null-result gate.
- Each returned coordinate is handled independently through
  `ncbPropAccessor::getRealValue(index, 0.0)`. `HasValue` first performs a
  `TJS_MEMBERMUSTEXIST` read and accepts any nonnegative status; a successful
  probe value is discarded and a second flags-0 read supplies the converted
  value. Missing index `0`, `1`, or `2` therefore contributes default `0.0`
  after one probe, while a present index invokes the getter twice.
- A present but non-numeric element enters ordinary TJS real conversion and may
  throw.
- There is no catch-and-ignore wrapper around the callback or result parsing.
  Native exception-cleanup blocks release temporary Variants/dispatches, then
  resume unwinding; script/callback/conversion errors propagate.
- Values are written in x/y/z order.  A later conversion exception can leave an
  earlier coordinate already updated.

## Pre-edit local comparison

Before the corresponding source edit:

- `Player` retained three unexposed `_onAction`, `_onSync`, and
  `_onGroundCorrection` Variants plus six property-like accessors.  No live
  code read them.  All four binaries register methods and have no analogous
  per-Player callback storage for these entries.
- Local `onGroundCorrection()` returned `void`; all four references return a
  copy of the first Variant argument.
- The local worker passed `[parent, current]`; all four references pass
  `[current, parent]`.
- The local worker accepted only object results, silently ignored other result
  types and null object dispatches, and swallowed every exception.  The four
  references perform throwing object conversion and propagate failures.
- Local indexed reads used one reused Variant and ignored each result code.
  The references independently produce `0.0` for every missing index.
- The local helper repeated the ground flag gate internally rather than placing
  it at the `Player_updateLayers` call boundary.  This was equivalent for the
  normal path but obscured the native call chain.
- The first corrected port still introduced a `MotionNode::tjsLayerObject`
  non-owning pointer and gated on it, but no construction path ever assigned
  that field. The native worker instead follows `Player.rootPlayer` to the
  bridge `_currentDispatch`, so that intermediate port made every callback
  unreachable.
- That intermediate helper also omitted the callback AddRef, the two
  independent argument Variant copies, the result Object AddRef, and the
  non-null global member-hint pointer. These are observable lifetime/callback
  boundaries even though the ordinary default method is an identity.

The correction is limited to these verified differences and removes the stale
single-target registration comments from compiled source.

## Applied implementation and verification

- `Player::onGroundCorrection` now accepts and returns the first
  `tTJSVariant`; `onAction` and `onSync` remain empty methods.
- The six unexposed property-style accessors and three invented callback
  Variants were removed from `Player`, eliminating their unnecessary
  construction/copy/destruction lifetime.
- The live evaluation call is gated at `Player_updateLayers`; the worker takes
  `rootPlayer._currentDispatch` and checks only that borrowed bridge dispatch.
- Both argument arrays are built through the native-Items helper in current,
  parent order.  The result undergoes unconditional object conversion; no
  exception is swallowed.  Indexed x/y/z reads use independent Variants and
  default each failed read to `0.0`, with incremental writes.
- Every IDB now names the four functions
  `Player_onAction_default_guess`, `Player_onSync_default_guess`,
  `Player_onGroundCorrection_default_guess`, and
  `Player_applyGroundCorrection_guess` (16 renames total).  Safe prototypes
  were applied to the two empty methods and runtime worker, decompiler caches
  were invalidated, and fresh four-target decompilation reconfirmed that the
  default ground method copies its first Variant into the return slot.
- All four IDBs saved successfully.
- `git diff --check` passed apart from existing line-ending notices.
- `cmake --build out/web/debug` completed all 32 incremental compile/link steps
  successfully; only pre-existing compiler/toolchain warnings were emitted.

2026-08-14 follow-up implementation/verification:

- removed the unassigned `MotionNode::tjsLayerObject` field;
- the call site now follows `_rootPlayer->_currentDispatch`, preserving child
  callback routing to the root wrapper;
- the worker retains callback and result dispatches independently, CopyRefs the
  two Array arguments, supplies the shared member-hint slot, and preserves the
  native reverse cleanup order;
- unit coverage records current/parent argument order, member/hint/objthis,
  missing-index default zero, non-Object result exception, and null-dispatch
  no-op;
- all four IDBs now name/comment the correct Player parameter, UTF-16 string,
  hint global, root-field chain and owner sequence and were saved;
- the full motionplayer test TU syntax check and a 37-step Web Debug
  compile/static-link/final-Wasm-link completed successfully.

2026-08-16 source-identity follow-up:

- the returned Object is now represented by its actual `ncbPropAccessor`, constructed from a copied result
  Variant;
- each coordinate uses `getRealValue`, preserving the `MEMBERMUSTEXIST` probe plus second flags-0 read;
- a recording result dispatch locks the exact `(index,flags)` sequence, positive-status success,
  second-read value, missing-index single probe and final owner destruction;
- ordinary/headless syntax-only, Web Debug, Wasmtime Headless Debug and all four in-place IDB saves pass.

The precise helper map, status boundary and reentrant read behavior are recorded in
`analysis/motionplayer_ground_correction_accessor_double_read_four_binary_2026-08-16.md`.
