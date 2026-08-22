# Motion.Player opacity and visible: four-binary reconstruction

Date: 2026-08-11

## Registration mapping

All method/property names were located by UTF-16LE byte search.  Candidate
property literals were then disambiguated by their xrefs into the Player NCB
registrar.

| Target | `setOpacity` name/body | `opacity` name/get/set | `setVisible` name/body | `visible` name/get/set |
|---|---|---|---|---|
| Android ARM64 | `0x6D5618` / `0x6BE408` | `0x6D5690` / `0x6D6CB8` / `0x6BE408` | `0x6D5700` / `0x6BE428` | `0x6D5770` / `0x6D6CC4` / `0x6BE428` |
| Android ARMv7 | `0x5984C4` / `0x58A60A` | `0x5984D6` / `0x599042` / `0x58A60A` | `0x5984F6` / `0x58A622` | `0x598508` / `0x59904C` / `0x58A622` |
| iOS ARM64 | `0x100124DD4` / `0x100113AC0` | `0x100124DF8` / `0x100125788` / `0x100113AC0` | `0x100124E20` / `0x100113B08` | `0x100124E44` / `0x1001257B0` / `0x100113B08` |
| iOS ARMv7 | `0x12405C` / `0x1114E6` | `0x12407C` / `0x1249BC` / `0x1114E6` | `0x12409E` / `0x11151E` | `0x1240BE` / `0x1249E4` / `0x11151E` |

The method and property-setter columns deliberately repeat the same body.  In
all four references, `setOpacity` aliases the `opacity` property setter and
`setVisible` aliases the `visible` property setter; there is no adapter with
different conversion behavior between those entry points.

## Root container and field layout

| Target | root representation | dirty | visible | opacity |
|---|---|---:|---:|---:|
| Android ARM64 | libstdc++ deque `start.cur` `*(Player+200)` | `+1584` | `+1586` | `+1656` |
| Android ARMv7 | libstdc++ deque `start.cur` `*(Player+160)` | `+1344` | `+1346` | `+1416` |
| iOS ARM64 | deque map/start `+168/+192`, stride `2648` | `+1600` | `+1602` | `+1672` |
| iOS ARMv7 | deque map/start `+140/+152`, stride `2228` | `+1312` | `+1314` | `+1384` |

Opacity is a four-byte integer in every layout.  Visible is a one-byte boolean
two bytes after the delta dirty byte.  All sixteen bodies resolve root element
zero directly and contain no size check, fallback, pending state, allocation,
or Player-level scalar access.

## Common source reconstruction

```cpp
int Player::getOpacity() const {
    return root().delta.opacity;
}

void Player::setOpacity(int opacity) {
    Root &r = root();
    if (r.delta.opacity != opacity) {
        r.delta.dirty = true;
        r.delta.opacity = opacity;
    }
}

bool Player::getVisible() const {
    return root().delta.visibleOverride;
}

void Player::setVisible(bool visible) {
    Root &r = root();
    if (r.delta.visibleOverride != visible) {
        r.delta.dirty = true;
        r.delta.visibleOverride = visible;
    }
}
```

Android ARM64 and both iOS targets emit dirty-before-value for both setters.
Android ARMv7 emits value-before-dirty.  The shared source inference follows
the three-target order and treats the ARMv7 ordering as instruction scheduling;
the final single-threaded state is identical.

## Data flow and lifetime

The setters have registration references but no additional direct native call
xrefs in the four databases.  Script method calls and property assignments
therefore converge immediately on the same root-delta mutation.  The getters
expose that exact stored state without evaluating or advancing the motion.

The root is the constructor-created element zero already established in the
root-position and transform audits.  Marking its delta dirty causes the layer
evaluation pipeline to rebuild accumulated visibility/opacity.  Visibility is
the root override participating in visible-state composition; opacity remains
integer-valued through the local/accumulated state and render paths.

## Boundary behavior

- `opacity` uses an ordinary signed 32-bit source field and equality test.  The
  generated AArch64 getters display an unsigned load because a 32-bit register
  write zero-extends architecturally; the NCB member family and setter argument
  establish the source-level integer contract.
- No setter body scales by 255, divides by 255, clamps, saturates, checks sign,
  or checks an expected 0..255 range.  Negative values and values above 255 are
  stored verbatim at this boundary.  Any later render-stage interpretation is
  separate from the setter contract.
- Reassigning the identical opacity performs no writes.  All 32 bits
  participate in the comparison.
- Canonical visible values behave like the audited flip booleans: identical
  input performs no writes; a change sets dirty and stores the new byte.
- Android ARM64 explicitly masks visible with `& 1`; the other emitted bodies
  compare the stored unsigned byte with the ABI integer and store its low byte.
  Registered/native source-level `bool` calls supply canonical 0/1.  Raw
  non-canonical ABI behavior is outside the valid C++ contract but follows the
  same target-specific caveat recorded in the flip audit.

## Pre-edit local comparison

The local port still implements an older viewport-scalar model:

1. `getOpacity` returns `double`, divides the root integer by 255, and returns
   `1.0` when `_nodes` is empty.  Native returns the stored integer directly and
   addresses root zero unconditionally.
2. `setOpacity(double)` writes `_opacity`, multiplies by 255, truncates, clamps
   to 0..255, guards an empty root, and always dirties.  Every one of those
   behaviors is absent from the four native bodies.
3. `getVisible` returns `true` for an empty container; the native getter is a
   direct byte load from root zero.
4. `setVisible` writes `_visible`, guards root presence, and always assigns and
   dirties.  Native performs no Player scalar write or guard and compares first.
5. Header comments explicitly claim 0..1 opacity conversion and retain the old
   scalar behavior.  Those comments are contradicted by all four references.
6. The unit test passes `0.5`, which only made sense under the disproved double
   contract; an integer opacity must be passed instead.

The intended edit changes the public opacity type to `int`, moves both getter
pairs to audited out-of-line implementations, removes conversion/clamping and
Player scalar side writes, adds the native compare gates, and updates the direct
test call.  The unrelated `_visible/_opacity` use in local `calcViewParam`
remains a separately unverified scaffold rather than being used to distort
these native entry points.

## Applied reconstruction and verification

The audited reconstruction has now been applied to the port:

- `Player::getOpacity` and `Player::setOpacity` use `int` and read/write the
  root delta integer directly.  The old `double` API, 255 scaling, truncation,
  clamp, empty-container fallback, and `_opacity` mirror write were removed.
- `Player::getVisible` and `Player::setVisible` read/write the root visibility
  byte directly.  The old empty-container fallback and `_visible` mirror write
  were removed.
- Both setters compare before mutating, set dirty only on a real change, and
  retain the dirty-before-value source order supported by three references.
- The focused unit fixture now supplies integer opacity `128` instead of the
  obsolete normalized value `0.5`.

The four IDBs were improved before the final verification pass.  Each database
now names and types its four bodies as:

```text
Player_setRootOpacity_guess(void *self, int opacity)
Player_getRootOpacity_guess(const void *self) -> int
Player_setRootVisible_guess(void *self, bool visible)
Player_getRootVisible_guess(const void *self) -> bool
```

Fifteen independently-addressable UTF-16 registrar literals were also renamed
to descriptive `setOpacity` / `opacity` / `setVisible` / `visible` names.  The
Android ARM64 `visible` property points into a larger literal rather than
having an independent string-start symbol, so it was deliberately left as an
offset reference instead of inventing a conflicting data item.  Fresh
post-type decompilation of all sixteen functions reproduced the field offsets,
integer/boolean signatures, compare gates, and target-specific store ordering
listed above.  All four databases then saved successfully to their existing
`.i64` files.

Validation results:

1. The motionplayer call-site scan found only the updated declaration,
   implementation, D3D facade visibility forwarding, and the integer unit
   fixture; unrelated cocos2d methods with the same names remain independent.
2. `git diff --check` passed.  Git only reported the repository's existing
   LF-to-CRLF working-copy warnings.
3. `cmake --build --preset "Web Debug Build"` passed all 22 incremental steps
   and linked `index.html`.
4. `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel`
   passed and linked `krkr2_wasmtime_guest.wasm`, including the exnref
   conversion.  The first invocation was stopped by the 60-second command
   wrapper while compiling; the immediate continuation reused the completed
   objects and completed the final 1/1 link with no compiler error.

The remaining `_visible` and `_opacity` members are not evidence for these
setters.  Their only relevant local consumer is `calcViewParam`, whose native
implementation must be audited independently before those fields or that
calculation can be changed.
