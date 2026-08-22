# MotionPlayer remove-variable-label owner / FuncCall pipeline (2026-08-15)

## Scope and authority

This vertical re-audits the small Engine helper which removes one label from the
current script Array.  It uses only the four shipped plugin references as the
joint authority:

- Android arm64: `reference/binaries/android/arm64-v8a/libmotionplayer.so`
- Android armv7: `reference/binaries/android/armeabi-v7a/libmotionplayer.so`
- iOS arm64: the arm64 slice under `reference/binaries/ios/`
- iOS armv7: the armv7 slice under `reference/binaries/ios/`

The semantic function name is not present in the stripped references, so the
portable source and recovery databases use
`EmoteEngine_removeVariableLabel_guess`.  Absolute addresses remain in this
analysis page and are not copied into compiled-source comments.

## Four-target function and member map

| target | helper entry | size | current-label Variant offset |
|---|---:|---:|---:|
| Android arm64 | `0x66B628` | `0x150` | `+1228` |
| Android armv7 | `0x5582C8` | `0x90` | `+652` |
| iOS arm64 | `0x1001AA8B4` | `0xE8` | `+860` |
| iOS armv7 | `0x1A9F84` | `0xEC` | `+464` |

The offsets identify the already recovered `_variableLabels` member in each
physical ABI layout.  They are evidence about the native objects, not portable
C++ layout constants.

The UTF-16 `remove` literal and its xrefs independently locate the same helper:

| target | `remove` literal | function xref site |
|---|---:|---:|
| Android arm64 | `0x14BE3B0` | `0x66B6CC`, `0x66B6D4` |
| Android armv7 | `0xD762C8` | `0x558316`, `0x55831C` |
| iOS arm64 | `0x101960064` | `0x1001AA92C` |
| iOS armv7 | `0x17523C8` | `0x1AA014`, `0x1AA01C` |

Multiple xrefs on ARM are address-materialization instructions for one literal
use, not multiple method calls.

## Exact call graph

All four helpers have exactly three code callers.  Two are branches in the
selector metadata builder and one is the selector synchronization pass:

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| selector builder: disabled entry | `0x66AF70` | `0x557FE8` | `0x1001AA2B8` | `0x1A9962` |
| selector builder: matched option | `0x66B3C8` | `0x5580F0` | `0x1001AA424` | `0x1A9AD2` |
| selector synchronization | `0x66E274` | `0x559B50` | `0x1001ACA48` | `0x1AC29A` |

This closes the helper's consumer set: there is no separate generic script
utility or external NCB entry calling it.

## Common owner and call sequence

The four decompilations reduce to the following source-shaped sequence:

```cpp
void removeVariableLabel_guess(const ttstr &label) {
    Variant labelsValue = this->_variableLabels; // CopyRef Object closure
    labelsValue.ToObject();
    Accessor labels(labelsValue);                // AddRef dispatch
    labelsValue.Clear();                         // release copied closure early

    Variant argument(label);                     // AddRef shared string body
    Variant *arguments[] = { &argument };
    Dispatch *dispatch = labels.GetDispatch();
    dispatch->FuncCall(
        0, L"remove", &removeHint, nullptr,
        1, arguments, dispatch);
} // argument dies first, retained dispatch dies second
```

The important lifetime events are identical on every target:

1. Copy-construct a Variant from the Engine's current-label member.
2. Force Object conversion when the copied Variant is not already Object.
3. AddRef the Object dispatch for a standalone accessor owner.
4. Destroy/Clear the copied Variant before constructing the method argument.
5. Construct a string Variant from the caller's `ttstr`; this increments the
   shared string body's reference count.
6. Call the dispatch vtable with flags `0`, name `remove`, a dedicated mutable
   hint slot, null result, one argument, and `ObjThis == dispatch`.
7. Destroy the string argument after the call.
8. Release the accessor-owned dispatch last.

The 64-bit references use a 24-byte Variant-shaped stack slot and the 32-bit
references a 12-byte slot, but the source-level ownership protocol is the same.

## Boundary and reentrancy behavior

- The helper does not borrow `_variableLabels.AsObjectNoAddRef()` across the
  script call.  If `Array.remove` re-enters native/script code and the Engine
  replaces or clears `_variableLabels`, the called Array remains alive through
  the accessor reference.
- The copied member Variant is deliberately not kept alive through `FuncCall`;
  only the retained dispatch is.  This matters for exact Object/ObjThis
  reference counts and release order.
- There is no object/null guard before the vtable call.  Object conversion or a
  bad/null closure follows the ordinary TJS conversion/failure path rather than
  becoming a silent no-op.
- The `FuncCall` HRESULT is ignored.  Native code performs no rollback or retry
  if the script method reports failure.
- The method lookup receives a real mutable hint pointer.  Passing null changes
  the dispatch cache path even when the final Array contents happen to match.
- Normal unwinding is represented by the source RAII owners: the argument is
  inside the accessor scope, so any escaping exception releases the argument
  before the retained dispatch.

## Portable-source and recovery-IDB alignment

The local helper was changed from a raw borrowed dispatch call to the exact
copy/force/retain/early-clear pipeline.  It now supplies a dedicated
`engineRemoveHint_guess`, preserves the native null result and receiver, and
keeps the one string Variant argument alive only across `FuncCall`.

Because the references retain no original C++ symbol, the method and all four
source call sites were renamed from the unjustified non-suffixed name to
`removeVariableLabel_guess`.  All four recovery IDBs received the corresponding
semantic `_guess` function name, an owner/call-boundary function comment, and a
bookmark.

Validation for this vertical:

- the real Emscripten response-file syntax-only check passed with only the
  repository's existing `_tss` warning;
- `cmake --build --preset "Web Debug Build"` completed ten incremental steps,
  rebuilt the affected motionplayer dependents and successfully linked the
  final `index.html`; emitted warnings are the existing `_tss`, imagepacker,
  pthread memory-growth, JSPI, and JS-library warnings;
- a targeted whitespace/diff check covers the source, plan, and this page;
- all four recovery IDBs are saved after the semantic write-back.

This closes only the remove-label helper and its complete three-caller set.  It
does not imply that the remaining motionplayer code has reached full one-to-one
reconstruction.
