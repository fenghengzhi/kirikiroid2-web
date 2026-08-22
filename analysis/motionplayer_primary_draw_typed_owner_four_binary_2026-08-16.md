# Primary #3 `draw` typed-owner and Variant lifetime (four-reference audit, 2026-08-16)

## Conclusion

All four current reference binaries register `Motion.EmotePlayer.draw` as a
typed `void(tTJSVariant)` member whose stored target is a real **Primary
`EmotePlayer` wrapper**.  The descriptor is not a direct `Player::draw` member
and is not an `EmoteEngine` member with a synthetic adjustment.

The complete call chain is:

```text
TJS argv[0] (borrowed)
  -> one-Variant/void typed-NCB owned copy
  -> EmotePlayer_draw_guess(primary, by-value Variant)
  -> load embedded Player* from the Engine-sized Primary payload
  -> second owned Variant copy (the inlined Player::draw parameter)
  -> Player_draw_guess(Player*, by-value Variant)
  -> destroy second copy
  -> destroy typed-NCB copy
  -> TJS_S_OK with an eagerly-cleared Void result
```

This closes a previously ambiguous owner boundary in the local comments.  The
script receiver owns the Engine-sized Primary object, while the state changed
by draw routing belongs to its embedded `Player`.  The two Variant copies are
temporary owners; the caller's argument remains borrowed and unchanged.

Absolute addresses below are evidence coordinates only.  Portable source uses
semantic names and keeps `_guess` on stripped identities.

## Registrar and wrapper identity

| Reference | Primary registrar | `draw` registration anchor | stored wrapper | size |
|---|---:|---:|---:|---:|
| Android arm64 | `0x67CEA8` | `0x67D01C` | `0x67ECB0` | `0x74` |
| Android armv7 | `0x5612E8` | `0x561360` | `0x561D38` | `0x40` |
| iOS arm64 | `0x1001B5130` | `0x1001B51DC` | `0x1001B5C84` | `0x40` |
| iOS armv7 | `0x1B4DE0` | `0x1B4E78` | `0x1B5898` | `0x80` |

The Android armv7, iOS arm64 and iOS armv7 registrars directly show the same
one-Variant/void specialization used by `draw`, `initPhysics` and
`unserialize`.  Android arm64 inlines the small descriptor construction, but
stores `0x67ECB0` in the same member-pointer slot with zero adjustment.  The
next descriptor stores the direct Engine `initPhysics` target instead, which
also rules out accidentally interpreting the `draw` target as an Engine
method.

## Primary wrapper body

The four wrapper bodies have the same three semantic operations:

| Reference | embedded `Player*` slot in Primary payload | Variant copy | landing target |
|---|---:|---:|---:|
| Android arm64 | `+0x428` | `tTJSVariant_copyConstruct_guess` | `0x6D3398` |
| Android armv7 | `+0x214` | `tTJSVariant_copyConstruct_guess` | `0x597864` |
| iOS arm64 | `+0x2B8` | `tTJSVariant_copyConstruct_guess` | `0x100123C84` |
| iOS armv7 | `+0x15C` | `tTJSVariant_copyConstruct_guess` | `0x122F28` |

In recovered source form:

```cpp
void EmotePlayer_draw_guess(Primary *primary,
                            const tTJSVariant &typedArgument) {
    Player *player = primary->embeddedPlayer;
    tTJSVariant local(typedArgument);
    Player_draw_guess(player, &local);
    // local destructor on normal and exceptional exits
}
```

There is no load from the script result slot, no Boolean publication, no
motion-key lookup and no implicit motion load in this wrapper.  The explicit
copy is the by-value parameter of `Player::draw` after that tiny method is
inlined.  Android arm64 and iOS armv7 also make the exceptional destructor path
especially visible; the other two retain the same C++ ownership contract and
the same normal-path destructor.

## Typed-NCB boundary and error ordering

| Reference | typed `FuncCall` | separate invoke helper |
|---|---:|---:|
| Android arm64 | `0x68A24C` | inlined into `FuncCall` |
| Android armv7 | `0x56A7F0` | `0x56A89C` |
| iOS arm64 | `0x1001C6620` | `0x1001C66A0` |
| iOS armv7 | `0x1C3A78` | `0x1C3AFC` |

The externally observable order is identical:

1. A non-null nested `membername` returns `TJS_E_MEMBERNOTFOUND` before any
   receiver lookup or result mutation.
2. A null `objthis` returns `TJS_E_NATIVECLASSCRASH`; a non-null caller result
   is still untouched.
3. For a present receiver, a non-null result Variant is cleared immediately.
4. Fewer than one argument returns `TJS_E_BADPARAMCOUNT` before native payload
   resolution.
5. With sufficient arity, failure to unwrap the Primary payload returns
   `TJS_E_NATIVECLASSCRASH`; the result remains Void from step 3.
6. On success, only `param[0]` is copy-constructed.  Surplus arguments are not
   converted or inspected.
7. The stored member pointer is resolved with its zero adjustment, invoked,
   and the adapter-owned copy is destroyed.  A void target returns `TJS_S_OK`;
   the cleared result remains `tvtVoid`.

Thus the port must keep the signature exactly
`void EmotePlayer::draw(tTJSVariant)`.  Replacing it with a raw callback,
`const tTJSVariant&`, a direct `Player` descriptor, or a result-returning method
would change observable arity, receiver, copy and result behavior.

## Embedded Player proof through D3D routing

Fresh decompilation of all four `Player_draw_guess` bodies confirms that
the first target route is the D3DAdaptor class-ID check.  A successful unwrap
sets the embedded Player's sticky D3D byte before calling the D3D renderer and
returning; the ordinary no-motion gate occurs later.

| Reference | `Player_draw_guess` | sticky D3D byte in Player |
|---|---:|---:|
| Android arm64 | `0x6D3398` | `+0x38D` |
| Android armv7 | `0x597864` | `+0x275` |
| iOS arm64 | `0x100123C84` | `+0x31D` |
| iOS armv7 | `0x122F28` | `+0x235` |

That ordering gives a sharp regression oracle without loading motion data: a
typed call on a genuine Primary adaptor with a D3DAdaptor `argv[0]` must change
the embedded Player's byte from false to true.  The caller's target Variant
must still own the same dispatch afterward, the deliberately non-convertible
surplus string must be ignored, and the successful result must be Void.

## Port changes and validation

- `EmotePlayer.cpp` and `EmotePlayer.h` now describe the real Primary wrapper,
  the second owned copy and the borrowed caller argument; the old ambiguous
  `Player_draw_NCBWrapper` label is gone.
- `motionplayer-dll.cpp` statically locks the exact member type and exercises
  null/foreign receiver ordering, eager result clear, minimum arity, surplus
  ignoring, D3D routing into the embedded Player, caller-argument preservation
  and Void result publication through the real class member object.
- The four recovery databases name the stripped wrapper
  `EmotePlayer_draw_guess`, apply its two-pointer lowered prototype, annotate
  the registrar/typed adapter/Player landing, and bookmark all three layers.
- The full motionplayer test translation unit passes Emscripten syntax-only
  compilation (only the pre-existing `_tss` warning), and the ten-step Web
  Debug build reaches the final `index.html` link successfully.
