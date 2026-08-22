# `Motion.Player` #78 `draw` direct typed entry (four-reference audit, 2026-08-16)

## Conclusion

All four current references store the complete native render dispatcher itself
as `Motion.Player` member #78 `draw`.  Its source-level identity is
`void Player::draw(tTJSVariant)`: the generated one-Variant/void adapter owns
the by-value parameter, and the method body immediately performs target class
routing, render-list ownership, drawing and post-draw cleanup.

There is **no second `Player::drawCompat(tTJSVariant*)` member** in the four
references.  That helper was a port-created source split.  It preserved most
runtime behavior but added a source function and call edge that the reference
registrars and xrefs do not have.  The portable dispatcher is now folded back
into `Player::draw(tTJSVariant)`.

Absolute addresses below are evidence coordinates only.  The stripped body is
named `Player_draw_guess` in recovery databases.

## Direct registrar target

| Reference | `Player` registrar | #78 registration anchor | stored body | size |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D3DA8` | `0x6D5F0C` | `0x6D3398` | `0x5D8` |
| Android armv7 | `0x597EC8` | `0x598716` | `0x597864` | `0x2D2` |
| iOS arm64 | `0x1001244F8` | `0x10012514C` | `0x100123C84` | `0x45C` |
| iOS armv7 | `0x123848` | `0x124398` | `0x122F28` | `0x440` |

Android arm64 constructs the descriptor inline and writes `0x6D3398` directly
into its zero-adjustment member-pointer slot.  The other three registrars call
their one-Variant/void descriptor creator with the same direct body.  None
stores a small wrapper that then calls another same-owner render member.

Fresh xrefs reinforce the structure:

- the `Player` registrar stores the body as #78;
- the Primary `EmotePlayer_draw_guess` wrapper calls the same body after loading
  the embedded `Player*` and copy-constructing the Player by-value argument;
- there is no third semantic caller and no second `Player` draw helper target.

On 32-bit registrars IDA may not materialize the stored code pointer as a normal
data xref because it flows through the generated creator.  Full registrar
decompilation still shows the direct argument unambiguously.

## One-Variant typed descriptor

| Reference | creator | `FuncCall` | invoke helper |
|---|---:|---:|---:|
| Android arm64 | inlined in registrar | `0x6F7978` | inlined in `FuncCall` |
| Android armv7 | `0x5B3F78` | `0x5B4050` | `0x5B19DC` |
| iOS arm64 | `0x10014A7B8` | `0x10014A908` | `0x1001478DC` |
| iOS armv7 | `0x14BB36` | `0x14BD20` | `0x1482AC` |

The externally visible gate and owner order is the same as the Primary
one-Variant/void specialization, but receiver unwrapping uses the `Player`
class ID:

1. non-null nested `membername` returns `TJS_E_MEMBERNOTFOUND`, result untouched;
2. null `objthis` returns `TJS_E_NATIVECLASSCRASH`, result untouched;
3. a present receiver causes eager clear of a non-null result Variant;
4. `argc < 1` returns `TJS_E_BADPARAMCOUNT` before native `Player` resolution;
5. a wrong non-null receiver with sufficient arity returns
   `TJS_E_NATIVECLASSCRASH`, with result already Void;
6. only `argv[0]` is copy-constructed into the by-value member argument;
7. surplus arguments are not converted or inspected;
8. the zero-adjustment member pointer is invoked; afterward the adapter destroys
   its owned Variant and returns `TJS_S_OK`, leaving result Void.

The adapter-produced Variant is the actual `Player::draw` parameter.  The body
uses its address internally because that is how a non-trivial by-value C++
parameter appears in the lowered ABI; this does not imply a source reference or
pointer parameter.

## Copy-count distinction from Primary #3

Direct `Motion.Player.draw` has one temporary Variant owner beyond the caller:

```text
TJS argv[0] (borrowed)
  -> Player one-Variant typed adapter / Player::draw parameter (owned)
  -> direct render body
  -> parameter destructor
```

`Motion.EmotePlayer.draw` necessarily has two:

```text
TJS argv[0] (borrowed)
  -> EmotePlayer typed parameter (owned)
  -> Primary wrapper loads embedded Player*
  -> Player::draw parameter (second owned copy)
  -> the same direct render body
  -> Player parameter destructor
  -> Primary typed-parameter destructor
```

This distinction is why the Primary registration must retain its wrapper while
the Player registration must point straight to the render body.  Replacing
either with a raw callback, a `const tTJSVariant&` target, or the other's owner
shape changes observable class-ID, ownership and exception cleanup behavior.

## Body identity and D3D oracle

All four direct bodies begin with target Variant/object normalization and the
D3DAdaptor class-ID check.  A D3D hit sets the Player's persistent D3D mode byte,
calls the D3D renderer and returns before the ordinary no-motion gate.  The
remaining common structure is D3D → SeparateLayerAdaptor → prepared ordinary
target routing with local render-list/Variant cleanup.

The regression therefore invokes the registered method on a genuine Player
adaptor with a D3DAdaptor first argument and a deliberately non-convertible
surplus string.  Success must set the same Player's sticky byte, leave the
caller's target Variant pointing at the same dispatch, ignore the surplus, and
publish Void.  Null and foreign receiver cases lock the generated gate order.

## Port and recovery changes

- `PlayerDrawDispatch.cpp` now contains the complete renderer directly in
  `Player::draw(tTJSVariant)`; the invented pointer-taking `drawCompat` member
  and its declaration were removed.
- A fresh UTF-16LE `captureCanvas` search found exactly one string per binary;
  every xref belongs to `D3DAdaptor_ncb_registerMembers_guess`, never the
  92-member Player registrar.  The unregistered and uncalled local
  `Player::captureCanvasCompat` raw callback was therefore removed. A
  2026-08-16 follow-up fresh target/xref audit also found the remaining
  no-argument `Player::captureCanvas()/draw()` pair had zero production callers
  and no native source edge; both port conveniences are now removed. See
  `analysis/motionplayer_player_dead_noarg_render_conveniences_four_binary_2026-08-16.md`.
- Adjacent source comments now identify the typed draw entry.  Existing
  `drawCompat.*` differential trace event strings remain stable for capture
  compatibility and are explicitly documented as labels, not a C++ member.
- The Player draw regression now calls the actual registered Function object,
  checks the member type, receiver/arity/result ordering, surplus behavior,
  embedded sticky-byte mutation and caller Variant preservation.
- Recovery databases rename the former `Player_drawCompat_guess` identity to
  `Player_draw_guess`; non-inlined typed creator/allocator/constructor/FuncCall/
  invoke helpers receive shared one-Variant/void semantic names.  Each database
  also records the direct body, registrar anchor and typed boundary as bookmarks.

## Validation

- The full motionplayer Catch2 translation unit passes Emscripten syntax-only
  compilation; only the repository-existing `_tss` warning remains.
- `cmake --build --preset "Web Debug Build"` rebuilt 33 steps and linked
  `index.html` successfully.
- A scoped source scan finds no `Player::drawCompat` declaration, definition or
  call, no dead Player capture helper, and no no-argument Player draw overload,
  while the #78 registration still names the sole by-value method.
- Targeted `git diff --check` passes; Git reports only existing LF-to-CRLF
  working-copy notices.
- All four renamed/type-corrected functions were force-recompiled, freshly
  decompiled, and the four recovery databases were saved in place.
