# Motion.Player `clear` direct typed entry and recursive worker identity

## Conclusion

Fresh registrar, stored-target, xref, worker, and generated-adapter reads of all
four current reference binaries establish one source-level call edge:

```text
Motion.Player.clear typed Function
  -> Player_drawToLayerRecursive_guess(Player*, Variant, Variant)
```

The recursive draw-to-layer worker is itself the member pointer stored by the
92-member `Motion.Player` registrar. There is no intervening native
`drawToLayerCompat` member or raw-callback shim. The original C++ source name is
stripped, so the recovered semantic name deliberately retains `_guess`.

The same body has exactly two other native caller roles: the
`Motion.EmotePlayer.clear` typed wrapper and self-recursion for type-3 child
Players. Those call edges use the same by-value two-Variant signature and do
not define a second compatibility layer.

## Four-reference mapping

| Role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player registrar | `0x6D3DA8` | `0x597EC8` | `0x1001244F8` | `0x123848` |
| `clear` descriptor anchor | `0x6D5D5C` | `0x5986A4` | `0x100125090` | `0x1242FA` |
| direct stored target | `0x6D0160` | `0x595720` | `0x10012139C` | `0x120168` |
| target size | `0x5BC` | `0x318` | `0x464` | `0x40A` |
| typed creator | registrar-inline | `0x5B3A98` | `0x10014A154` | `0x14B352` |
| typed `FuncCall` | `0x6F74B4` | `0x5B3B70` | `0x10014A2A4` | `0x14B53C` |
| member invoke | `0x6F75D0` | `0x5B3C30` | `0x10014A384` | `0x14B5D0` |
| Primary `clear` caller | `0x67EE44` | `0x561DA8` | `0x1001B5D04` | `0x1B595C` |

Android arm64 constructs the 0x40-byte typed Function object inline. Its
registrar materializes the target at `0x6D5D70/0x6D5D80` and stores it in the
member-pointer payload before publishing `clear`. Android armv7 and iOS armv7
use PC-relative target materialization immediately before their typed-creator
calls; iOS arm64 passes the target directly at its creator call. Therefore the
missing ordinary data xref on the two Thumb registrars is an IDA relocation
presentation difference, not a different descriptor family.

## Direct target and xref topology

Fresh target xrefs recover the same semantic graph on all four platforms:

- `EmotePlayer_clear_guess` calls the worker directly;
- the worker calls itself only while descending into type-3 child Players;
- the Player registrar stores that exact address as member 73 `clear`;
  Android arm64 exposes two materialization xrefs and iOS arm64 one data xref,
  while the two Thumb registrars expose the same relationship in disassembly.

The target begins with the Player motion-content gate and then owns the whole
D3DAdaptor -> SeparateLayerAdaptor -> ordinary Layer/callable routing body.
It is not an adapter-shaped function: it has no `result`, `argc`, `argv`,
`objthis`, or TJS error return. Its native shape on every target is equivalent
to:

```cpp
void Player::unknown_source_name(Variant target, Variant fill);
```

Its type-3 recursion constructs fresh target and fill Variants, calls the same
entry, and destroys the fill copy before the target copy. That recursive owner
timeline is part of the member body rather than a wrapper convention.

## Generated typed boundary

The four `FuncCall`/invoke pairs agree on this observable order:

1. non-null nested `membername` returns `TJS_E_MEMBERNOTFOUND`;
2. null receiver returns `TJS_E_NATIVECLASSCRASH` without touching result;
3. a present result Variant is cleared;
4. fewer than two parameters returns `TJS_E_BADPARAMCOUNT`;
5. the Player native instance is resolved;
6. `argv[0]` and `argv[1]` are copied into owned Variant parameters;
7. the stored member pointer is invoked directly;
8. the fill parameter is destroyed before the target parameter;
9. surplus parameters are ignored and a successful call publishes Void.

This boundary proves why `clear` cannot be represented as a raw compatibility
callback. It also separates the adapter's two parameter owners from the
additional owners created by `EmotePlayer.clear` and by each child-recursion
edge.

## Portable source correction

The local body already reproduced the recovered routing and boundary behavior,
but its `drawToLayerCompat` name falsely described the direct native member as
a compatibility shim. The source was corrected as follows:

- renamed the member to `drawToLayerRecursive_guess` in declaration,
  definition, Player registration, Primary forwarding, recursion, and direct
  tests;
- retained explicit `NCB_METHOD_DETAIL(clear, ..., (Variant, Variant))`, which
  models the stripped source name differing from the script publication name;
- added a compile-time member-pointer type assertion and retained the real
  registered Function-object receiver/arity/result regression;
- updated the four recovery registrars so comments identify the direct stored
  target and no longer mention a compatibility member.

The behavioral implementation itself was not rewritten: this vertical fixes
the source structure and identity after revalidating the previously recovered
D3D/SLA/Layer routing body.

## Validation

Validation for this vertical consists of the full motionplayer test translation
unit syntax check, final Web Debug link, a zero-match scan for the removed
`drawToLayerCompat` identifier, scoped `git diff --check`, forced registrar and
worker decompile readback, and in-place saves of all four recovery databases.

