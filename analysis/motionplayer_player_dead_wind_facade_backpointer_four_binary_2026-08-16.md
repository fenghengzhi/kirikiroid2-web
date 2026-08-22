# Player dead wind facade / Engine back-pointer four-binary audit (2026-08-16)

## Question

The portable `Player` class still exposed `startWind(double x5)` and
`stopWind()`. Both methods used a port-added non-owning `_engineBack` pointer,
and `EmoteEngine` wrote that pointer immediately after constructing its owned
`Player`. Neither method appeared in the restored 92-member Motion.Player
table. This audit asks whether the pair and the back-pointer represent a hidden
native source edge or a local test convenience.

## Fresh registration and string evidence

The complete Player registrars were freshly decompiled again:

| target | Motion.Player registrar | `startWind` / `stopWind` hits |
|---|---:|---:|
| Android arm64 | `0x6D3DA8` | 0 / 0 |
| Android armv7 | `0x597EC8` | 0 / 0 |
| iOS arm64 | `0x1001244F8` | 0 / 0 |
| iOS armv7 | `0x123848` | 0 / 0 |

The UTF-16LE literals were then searched independently instead of relying on
IDA's ordinary narrow-string search:

| target | `startWind` literal(s) | `stopWind` literal(s) |
|---|---:|---:|
| Android arm64 | `0x14BEBA0` | `0x14BEBB4` |
| Android armv7 | `0xD769D4` | `0xD769E8` |
| iOS arm64 | `0x101960300`, `0x10196FF1E` | `0x101960314`, `0x10196FF32` |
| iOS armv7 | `0x1752664`, `0x17622CA` | `0x1752678`, `0x17622DE` |

Every literal xref belongs to one of exactly two class registrars:

- Motion.EmotePlayer registers `startWind` directly to the five-float Engine
  member and registers its own dedicated zero-argument `stopWind` body;
- D3DEmotePlayer registers its shell `startWind(float x5)` and `stopWind()`
  wrappers.

No literal xref originates in the Motion.Player registrar. Android shares one
literal pair between the two real registrars; iOS carries a separate literal
pair for each registrar. The duplication difference does not create a third
Player publication surface.

## Shared Engine target and complete caller set

| target | `EmoteEngine_setWind_guess` | direct code callers | direct registrar binding |
|---|---:|---|---|
| Android arm64 | `0x66DD8C` | D3D start/stop | Primary startWind |
| Android armv7 | `0x559900` | D3D start/stop | Primary startWind, PC-relative registrar materialization |
| iOS arm64 | `0x1001AC718` | D3D start/stop | Primary startWind |
| iOS armv7 | `0x1ABF24` | D3D start/stop | Primary startWind, PC-relative registrar materialization |

Fresh `xrefs_to` returned no Player member caller. The four D3D wrappers are:

| target | D3D `startWind` | D3D `stopWind` |
|---|---:|---:|
| Android arm64 | `0x530A60` | `0x530A6C` |
| Android armv7 | `0x494D94` | `0x494D9C` |
| iOS arm64 | `0x1002331D4` | `0x1002331E0` |
| iOS armv7 | `0x231E38` | `0x231E40` |

All four `startWind` bodies traverse shell -> primary EmoteObject -> Engine and
forward five already-narrowed floats. All four `stopWind` bodies traverse the
same owner chain and call the Engine setter with five positive zeros. This is
a D3D shell behavior, not evidence for an embedded Player facade.

Primary has a different stop boundary: its registered `stopWind()` deletes and
nulls the Engine's raw emitter owner without rewriting the cached five scalar
parameters. Therefore a generic Player `stopWind()` cannot be inferred by
merging the two real script classes.

## Constructor and object-graph evidence

The Engine constructors and exact native Player allocations were freshly read
back:

| target | `EmoteEngine_ctor_guess` | allocated Player size |
|---|---:|---:|
| Android arm64 | `0x67B76C` | `0x568` (1384) |
| Android armv7 | `0x560948` | `0x3B0` (944) |
| iOS arm64 | `0x1001B7FB0` | `0x4B8` (1208) |
| iOS armv7 | `0x1B7788` | `0x348` (840) |

The common construction sequence is:

```text
pending = operator new(native Player size)
Player_ctor_guess(pending, resourceManagerDispatch)
engine.playerOwner = pending
allocate/construct the position controller
...
```

There is no store of `engine this` into the newly constructed Player between
publication of the owner and construction of the next Engine member. V257 later
proved that Player does have one final pointer-width slot, but it is an
uninitialized raw `iTJSDispatch2 *` read only by two zero-xref Android load
residuals; it has no Engine producer and is not an Engine back-pointer. The
portable `_engineBack` field and its constructor-body write were therefore not
a native object-graph edge; they existed solely to support the two local facade
methods. See
`analysis/motionplayer_player_final_tail_dispatch_residual_four_binary_2026-08-18.md`.

## Portable correction

Local call-graph scanning found zero production callers for
`Player::startWind` and `Player::stopWind`; only the wind predicate unit test
used them. The following port-only source was removed together:

- both Player method declarations and bodies;
- the trailing `Player::_engineBack` pointer;
- the `EmoteEngine` constructor-body back-pointer installation;
- comments describing the invented compatibility route.

The wind predicate regression now calls `EmoteEngine::setWind_guess(float x5)`
directly, including its final five-zero stop case. This preserves coverage of
the four-reference 64/32-bit predicate split without manufacturing a Player
owner edge or a double-parameter facade. A real Player adaptor regression also
requires `startWind` and `stopWind` with `TJS_MEMBERMUSTEXIST` and locks both
lookups to `TJS_E_MEMBERNOTFOUND` without modifying the caller's result value.
