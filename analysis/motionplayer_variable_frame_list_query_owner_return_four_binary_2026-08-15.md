# MotionPlayer variable-frame-list query owner / return handoff (2026-08-15)

## Scope and authority

This vertical re-audits the registered EmotePlayer `getVariableFrameList`
surface against all four shipped plugin references:

- Android arm64: `reference/binaries/android/arm64-v8a/libmotionplayer.so`
- Android armv7: `reference/binaries/android/armeabi-v7a/libmotionplayer.so`
- iOS arm64: the arm64 slice under `reference/binaries/ios/`
- iOS armv7: the armv7 slice under `reference/binaries/ios/`

The public script member spelling is proved by the NCB registration table.  The
stripped native C++ function spelling remains `_guess` in recovery IDBs.  Exact
addresses stay here rather than in compiled-source comments.

## Four-target map

| target | function | size | Engine frame-list Dictionary member |
|---|---:|---:|---:|
| Android arm64 | `0x67F67C` | `0x160` | `+1248` |
| Android armv7 | `0x5622A0` | `0x8C` | `+664` |
| iOS arm64 | `0x1001B63C8` | `0xC4` | `+880` |
| iOS armv7 | `0x1B623C` | `0xE8` | `+476` |

The member offsets agree with the producer recovered in
`motionplayer_build_variable_list_owner_pipeline_four_binary_2026-08-15.md`.
The arm64 and iOS arm64 function pointers are visible directly from the
EmotePlayer NCB registration routine; 32-bit registration stores Thumb-style
function pointers, so ordinary xref enumeration does not report a direct xref
to the even function entry.

## Common owner and return sequence

All four bodies implement the same source-shaped protocol:

```cpp
Variant dictionaryValue = engine.variableFrameLists;
dictionaryValue.ToObject();
Accessor dictionary(dictionaryValue);       // retain dispatch
dictionaryValue.Clear();                     // early release closure copy

Variant getterOutput;                        // initially Void
Dispatch *dispatch = dictionary.GetDispatch();
dispatch->PropGet(
    0,
    label.charactersOrGlobalEmpty(),
    label.body ? label.body->hintAddress() : nullptr,
    &getterOutput,
    dispatch);

Variant hiddenReturn(getterOutput);           // explicit CopyRef
getterOutput.Clear();
dictionary.Release();
return hiddenReturn;
```

The important distinction is between the copied Object closure and the
standalone retained dispatch:

1. The Engine member is CopyRef'd into a temporary Variant.
2. A non-Object value enters the ordinary TJS `ToObject` conversion path.
3. The dispatch receives one accessor-owned reference.
4. The copied Variant is destroyed before the named property getter.
5. Only the retained dispatch spans the potentially reentrant `PropGet`.
6. The getter output is CopyRef'd into the hidden return object.
7. The getter output temporary dies before the retained dispatch is released.

The previous portable implementation kept the copied closure's Object and
ObjThis references alive across the getter and never established this separate
owner.  Final values could match, but reference counts, reentrant replacement
behavior, and release order did not.

## Label characters and mutable hash hint

The by-value `ttstr` argument is used directly for both member name and hint:

| target width | string-body hint offset |
|---|---:|
| 64-bit | `+68` |
| 32-bit | `+60` |

When the `ttstr` has a body, the getter obtains the UTF-16 character pointer
from that body and passes the address of its mutable hash-hint field.  When the
body is null, it passes the process-global empty UTF-16 string and a null hint
pointer.  There is no separate static `frameList` hint because the dynamic
variable label itself is the Dictionary key.

The getter uses flags `0`, not `TJS_MEMBERMUSTEXIST`, and sets `ObjThis` equal
to the retained Dictionary dispatch.

## Failure and reentrancy boundaries

- The `PropGet` HRESULT is ignored.  `getterOutput` begins as Void, so an
  ordinary missing-property failure returns Void.  If a custom dispatch writes
  a value and nevertheless returns failure, that written value is still
  CopyRef'd and returned.
- There is no fallback to the Player variable-range table, no attempt to create
  a missing per-label Array, and no second property lookup.
- There is no defensive null check after Object conversion.  A bad/null closure
  follows the ordinary TJS conversion/dereference boundary rather than becoming
  a silent miss.
- A getter may re-enter script/native code and replace or clear the Engine's
  `_variableFrameLists` member.  The original receiver remains alive through
  the accessor reference, while the earlier copied closure is already gone.
- A returned Object/string value owns its own CopyRef before `getterOutput` and
  the Dictionary receiver are released.  It therefore remains valid
  independently of both temporaries.
- RAII reproduces exceptional unwinding: getter output is inside the accessor
  scope and is released first if a call escapes; the retained dispatch is
  released afterward.

## Portable-source and recovery-IDB alignment

`EmotePlayer::getVariableFrameList` now performs the native
copy/force/accessor-retain/early-clear handoff.  It invokes `PropGet` with the
dynamic label's own hint, explicitly CopyRefs the output into a distinct return
Variant, and preserves the ignored status and receiver identity.

All four recovery IDBs contain a detailed function comment and bookmark for
this owner/return boundary.  The already established `_guess` semantic name is
retained because the shipped symbols do not prove the original C++ spelling.

Validation:

- the real Emscripten response-file syntax-only check passed with only the
  existing `_tss` warning;
- `cmake --build --preset "Web Debug Build"` completed three incremental steps,
  rebuilt `EmotePlayer.cpp`, rebuilt the motionplayer archive, and linked final
  `index.html`; only the existing `_tss`, pthread memory-growth, JSPI, and
  JS-library warnings were emitted;
- targeted diff/whitespace checking covers the implementation, plan, and this
  page;
- all four recovery IDBs are saved after the annotation write-back.

This closes the variable-frame-list query only.  It does not imply full
motionplayer reconstruction.
