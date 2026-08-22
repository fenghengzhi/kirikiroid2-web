# Player dead `_metadata` / `_busy` placeholders four-binary audit (2026-08-16)

## Local anomaly

The portable Player contained:

- a `tTJSVariant _metadata` plus public inline `setMetadata/getMetadata`;
- a `bool _busy` plus public inline `getBusy`;
- an unrelated zero-caller `parentPlayerForDiag()` accessor marked
  `CREATESITE (temp)`.

The two fields had no reader or writer beyond their own dead accessors. No test
or production path used any of the three methods.

## Script/string boundary

Fresh complete Player registrar decompilation found neither `metadata` nor
`busy` in the exact 92-member table:

| target | Motion.Player registrar | `metadata` | `busy` |
|---|---:|---:|---:|
| Android arm64 | `0x6D3DA8` | absent | absent |
| Android armv7 | `0x597EC8` | absent | absent |
| iOS arm64 | `0x1001244F8` | absent | absent |
| iOS armv7 | `0x123848` | absent | absent |

Explicit UTF-16LE byte search found one `metadata` literal per target:

| target | literal | complete xref owner |
|---|---:|---|
| Android arm64 | `0x14D3DA2` | `EmoteObject_init_guess` |
| Android armv7 | `0xD8473C` | `EmoteObject_init_guess` |
| iOS arm64 | `0x10196021C` | `EmoteObject_init_guess` |
| iOS armv7 | `0x1752580` | `EmoteObject_init_guess` |

That literal is the module metadata lookup performed while initializing an
EmoteObject. It is not a Player property or Player-owned retained Variant.
There is no UTF-16LE `busy` literal in any of the four binaries.

## Native persistent-Variant chain

The constructor and destructor were freshly compared around the five adjacent
persistent Variant owners at the end of the public property state:

| target | resourceManager | motion context / motionKey | outline | meshline | tags |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `+992` | `+1012` | `+1032` | `+1052` | `+1072` |
| Android armv7 | `+684` | `+696` | `+708` | `+720` | `+732` |
| iOS arm64 | `+880` | `+900` | `+920` | `+940` | `+960` |
| iOS armv7 | `+620` | `+632` | `+644` | `+656` | `+668` |

The stride is exactly one native Variant (`20` bytes on the two 64-bit targets,
`12` bytes on the two 32-bit targets). Constructor behavior is:

```text
CopyRef constructor argument -> resourceManager
Void-initialize motion context
Void-initialize outline
Void-initialize meshline
Void-initialize tags
```

Android arm64, for example, CopyRefs the resource owner at `+992` and writes the
four following type words at `+1028/+1048/+1068/+1088`. Android armv7 writes
the corresponding four Void type words at `+704/+716/+728/+740`; iOS arm64
writes them at `+916/+936/+956/+976`. The destructors release the same five
owners in exact reverse order. iOS armv7 independently shows the five reverse
destructors at `+668/+656/+644/+632/+620`.

There is no sixth persistent Player Variant in this source group for
`metadata`, and there is no byte gap in which the portable `_busy` member could
sit between the five owners. The recovered `resourceManager`, motion-context,
`outline`, `meshline`, and `tags` semantics account for the whole chain.

This does not assert that the original program could never have an unrelated
internal Boolean elsewhere. It establishes that the current `_busy` name,
placement, default, and accessor have no four-reference source graph. A future
recovery may add a differently located `_guess` field only if actual readers
and writers identify one.

## Correction

The port now removes:

- `setMetadata/getMetadata` and `_metadata`;
- `getBusy` and `_busy`;
- the zero-caller temporary `parentPlayerForDiag()` accessor.

The real `_parentPlayer` non-owning link remains unchanged. Removing the
temporary accessor does not alter layout; removing the two unsupported fields
eliminates one invented Variant owner/destructor and one invented byte/default
from the portable object.

