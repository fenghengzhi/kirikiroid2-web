# Motion.Player dead no-argument render conveniences

## Conclusion

Fresh four-reference `Motion.Player` registrar/target/xref reads and
`captureCanvas` string/xref searches leave one native Player render entry:

```cpp
void Player::draw(tTJSVariant target);
```

The local overloads `void Player::draw()` and
`tTJSVariant Player::captureCanvas()` were unregistered port conveniences.
Neither had a production caller: no-argument `draw()` was used only by four
unit-test call sites, while Player `captureCanvas()` had no caller at all. The
pair has been removed rather than retained as a source-level API absent from
the references.

This does not affect `D3DAdaptor::captureCanvas(tTJSVariant)`. That separately
registered member is the only native owner of the `captureCanvas` script name
and remains intact.

## Player draw mapping and xrefs

| Reference | Player registrar | direct one-Variant draw target | fresh target xrefs |
|---|---:|---:|---|
| Android arm64 | `0x6D3DA8` | `0x6D3398` | Primary draw + two registrar materializations |
| Android armv7 | `0x597EC8` | `0x597864` | Primary draw; registrar target visible through Thumb PC-relative materialization |
| iOS arm64 | `0x1001244F8` | `0x100123C84` | Primary draw + registrar data reference |
| iOS armv7 | `0x123848` | `0x122F28` | Primary draw; registrar target visible through Thumb PC-relative materialization |

The registrar selects the generated one-Variant/void typed Function family on
all targets. The complete render dispatcher is the stored target itself; its
other native caller is the Primary `EmotePlayer.draw` wrapper. No target xref
or descriptor shape supports a zero-argument overload.

The local zero-argument method did not enter that dispatcher. It merely
allocated two temporary prepared-item vectors and called the internal
`prepareRenderItems` worker. That invented a public C++ call edge not present
in the native render graph.

## `captureCanvas` ownership

A fresh UTF-16LE search found exactly one `captureCanvas` string in each
reference:

| Reference | string | all xref owner |
|---|---:|---|
| Android arm64 | `0x14D5BAC` | `D3DAdaptor_ncb_registerMembers_guess` |
| Android armv7 | `0x57CE60` | `D3DAdaptor_ncb_registerMembers_guess` |
| iOS arm64 | `0x10195C09A` | `D3DAdaptor_ncb_registerMembers_guess` |
| iOS armv7 | `0x174E3FE` | `D3DAdaptor_ncb_registerMembers_guess` |

The iOS armv7 registrar yields three relocation/materialization xrefs; the
other targets yield one. Every one remains inside the D3DAdaptor registrar,
and none belongs to the 92-member Player table. The local Player convenience
also had zero callers after the earlier dead raw-callback cleanup.

## Portable source correction

- removed the no-argument `Player::draw()` declaration and definition;
- removed the caller-free `Player::captureCanvas()` declaration and definition;
- changed the no-implicit-load regression to call the real
  `draw(tTJSVariant)` entry on an empty Player;
- changed render-preparation smoke calls to use the existing explicitly
  test-only `prepareRenderItemsForDifferentialTest_guess` hook;
- simplified the draw signature assertion from an overload-selecting cast to
  `decltype(&Player::draw)`, ensuring the class now has only the one-Variant
  native-shaped member;
- left `D3DAdaptor::captureCanvas(tTJSVariant)` and its registration untouched.

## Validation

Validation includes the complete motionplayer test-TU Emscripten syntax check,
final Web Debug link, a zero-match scan for no-argument Player draw/capture
declarations/definitions/calls, a positive check for the D3DAdaptor member,
scoped `git diff --check`, fresh IDB readback, and in-place saves of all four
recovery databases.

