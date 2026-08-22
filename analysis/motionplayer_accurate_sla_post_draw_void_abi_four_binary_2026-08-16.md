# Accurate-SLA post-draw void ABI and lifetime audit (four references, 2026-08-16)

## Scope

This pass re-audits the accurate `SeparateLayerAdaptor` caller edge that follows
the renderer and the complete `Player_updateAccurateSLAAfterDraw_guess` body in
the four current `reference/binaries/` targets.  It specifically checks the
producer/ready flags, target `tTJSVariant` ownership, internal workspace Layer
materialization, dimension/property order, `piledCopy` argument order, cleanup,
and the helper's C++ return type.

The existing post-draw data-flow implementation remains correct.  The newly
closed mismatch was the helper declaration: the reference helper is `void`, not
`bool`.  The local declaration/definition now use `void`, and the clear-producer
path uses a bare `return`.

## Four-reference entry points and sole callers

| target | helper entry | caller call instruction | first instruction after call |
| --- | ---: | ---: | --- |
| Android arm64 | `0x6CBD18` | `0x6D2B50` | `MOV X0, SP`, then destroy temporary target Variant |
| Android armv7 | `0x593344` | `0x5973EA` | load temporary target Variant address, then destroy it |
| iOS arm64 | `0x10011E808` | `0x1001234E4` | load temporary target Variant address, then destroy it |
| iOS armv7 | `0x11D078` | `0x1226EE` | load temporary target Variant address, then destroy it |

Each helper has exactly one code xref, from
`Player_renderToSeparateLayerAdaptor_guess`.  On all four targets the instruction
immediately after the call prepares the temporary target Variant destructor;
there is no compare, move, branch, or other consumption of the return register.
The caller itself is already typed `void` in all four recovery databases.

## Why the helper is `void`

The decompiler's earlier inferred return types disagreed across targets:

- Android arm64 and iOS arm64 exposed a pointer-shaped tail value left by the
  final dispatch release.
- Android armv7 and iOS armv7 exposed integer-shaped epilogue/cookie values.
- The producer-clear path merely stores `ready = producer` and returns; it does
  not synthesize a common `true`/`false` value.
- The producer-set path ends in destruction/release of the internal accessor and
  target accessor owners; it likewise does not synthesize a boolean.

Those mutually incompatible residues are normal return-register artifacts of a
`void` function.  Combined with the four callers' complete lack of return-value
use, they rule out the local `bool` declaration and its fabricated `return true`
statements.

The recovery IDBs now carry the common prototype:

```cpp
void __fastcall Player_updateAccurateSLAAfterDraw_guess(
    void *self,
    const void *targetVariant);
```

Fresh forced decompilation readback confirmed this prototype and the associated
ABI comment in all four databases; all four databases were then saved.

## Reconfirmed state and data flow

The complete body remains the following ordered state machine:

1. Read the producer flag and unconditionally snapshot it to the ready flag.
2. If the producer is clear, return immediately.  The producer flag is not
   cleared here; it is reset at the beginning of the next `updateLayers` pass.
3. Copy-construct the target Variant into a property accessor and retain its
   object dispatch.
4. Call `Player_materializeInternalRenderLayers_guess` with the original target
   Variant.
5. Copy-construct the persistent internal Layer Variant into another accessor
   and retain its dispatch.
6. Read target `height` first, then target `width`.  Each dimension uses the
   same retained target `ncbPropAccessor`: hinted `HasValue` performs the
   `TJS_MEMBERMUSTEXIST` probe, and a nonnegative result admits a hinted
   `GetValue<tjs_int>` ordinary read; a failed probe supplies zero.
7. Invoke the internal Layer's
   `piledCopy(0, 0, target, 0, 0, width, height)` with exactly seven Variants.
8. Destroy the seven argument Variants in reverse construction order, release
   the internal Layer dispatch owner, and finally release the target accessor
   owner.

The four `piledCopy` dispatches remain:

| target | call instruction |
| --- | ---: |
| Android arm64 | `0x6CBF8C` |
| Android armv7 | `0x59348A` |
| iOS arm64 | `0x10011E9E0` |
| iOS armv7 | `0x11D27A` |

No reference clears the producer flag in this helper, rolls back an already
published workspace Layer, validates positive dimensions, or consumes a helper
return value.  Optional Web/headless tracing remains outside the native/default
data path.

The four-reference accessor/template mapping and the second-read failure/status
boundary are documented separately in
`motionplayer_internal_workspace_dimension_ncb_accessor_four_binary_2026-08-16.md`.

## Source changes

- `cpp/plugins/motionplayer/Player.h`
  - changed `updateAccurateSLAAfterDraw` from `bool` to `void`.
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp`
  - changed the definition to `void`;
  - changed the producer-clear exit to bare `return`;
  - removed the fabricated terminal `return true`.

No materialization, property, `piledCopy`, flag, or object-lifetime behavior was
changed in this pass because fresh four-reference evidence reconfirmed the
existing implementation.

## Validation

- ordinary motionplayer syntax check: passed;
- `KRKR2_WASMTIME_HEADLESS=1` motionplayer syntax check: passed;
- Web Debug motionplayer archive: `30/30`, passed;
- Wasmtime Headless Debug motionplayer archive: `30/30`, passed;
- full Web Debug build/link: `3/3`, passed;
- scoped `git diff --check`: passed (the existing LF-to-CRLF notices are
  worktree-policy warnings, not whitespace errors introduced by this change).
