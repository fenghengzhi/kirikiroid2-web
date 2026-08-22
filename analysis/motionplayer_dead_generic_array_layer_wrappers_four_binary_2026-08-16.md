# MotionPlayer dead generic Array/Layer wrappers: four-binary record (2026-08-16)

## Conclusion

Three inline helpers in `PlayerInternal.h` had no production or test caller:

```text
getObjectCount(Variant)
getArrayItem(Variant, index, result)
tryResolveSeparateAdaptorOwner(Variant)
```

Fresh decompilation of the four current reference binaries shows that these are
not dormant copies of the native boundaries.  The real Array count path works
on an already retained dispatch, ignores the named getter HRESULT, and converts
the result.  The real particle element path performs flags-0 numeric access and
immediately forces the result through the Player native-instance boundary.  The
Layer path has one Variant-to-Layer conversion; it has no second
SeparateLayer-owner alias layer.

The three zero-call wrappers were removed.  The live named-property helper,
Layer-dispatch resolver, particle Array owner/count/index helpers, and their
callers remain unchanged.

## Four-reference map

| Target | Array-like `count` | particle element to `Player *` | strict Layer from Variant |
|---|---:|---:|---:|
| Android arm64 | `0x56CA74` | `0x6BEA58` | `0xA7959C` |
| Android armv7 | `0x4BEB84` | `0x58AAB0` | `0x79AFCE` |
| iOS arm64 | `0x1000F30F4` | `0x100113FE4` | `0x10035FF10` |
| iOS armv7 | `0xEF8B4` | `0x1119DC` | `0x36366C` |

Addresses are recorded only here; portable source comments retain semantic
names and behavior rather than a single target's layout.

## Array count boundary

The four `VariantObject_getCount_guess` implementations reduce to:

```text
result = Void
retainedDispatch.PropGet(
    flags=0, member="count", hint=null,
    out=result, objthis=retainedDispatch)
count = result.AsInteger()
destroy result
return count
```

The getter HRESULT is not tested.  The owner/receiver is prepared by the caller
before this helper; the helper does not quietly reject a non-object Variant.

The deleted local `getObjectCount` instead accepted an arbitrary Variant,
returned zero after its named-property helper reported failure, and selected
receiver/objthis from a closure.  It therefore had a different owner boundary
and different failure behavior in addition to having no caller.

## Numeric element boundary

The four `ParticleArray_getNativePlayerAt_guess` helpers perform:

```text
element = Void
arrayDispatch.PropGetByNum(
    flags=0, index, out=element, objthis=arrayDispatch)
elementObject = force Object(element)
playerAdaptor = strict Player ClassID native-instance query(elementObject)
player = playerAdaptor ? playerAdaptor.native : null
destroy element
return player
```

The call sites pass flags 0.  A non-object or wrong-class element throws; a
successful native query with a null adaptor/native pointer returns null, which
the recovered callers do not guard.

The deleted `getArrayItem` stopped before the Player conversion, returned a
success boolean, quietly rejected non-object input, and used `TJS_IGNOREPROP`
instead of flags 0.  It cannot represent this native helper or be safely used as
a substitute for it.

## Layer conversion boundary

All four `TJSNI_Layer_FromVariant_guess` implementations:

1. force the Variant to Object type;
2. read only its stored Object dispatch;
3. query the Layer ClassID once;
4. return the native Layer pointer or follow the native conversion error path.

The local `tryResolveSeparateAdaptorOwner` did nothing except call the already
live `tryResolveLayerDispatch` with the same argument.  It had no caller, no
independent owner, no different type conversion, and no registration row.  Its
name implied a SeparateLayerAdaptor-specific source boundary that does not
exist in the four recovered chains, so the alias was removed while the live
resolver was retained.

## Source-surface effect

Removed only the three inline definitions.  In particular, this pass does not
remove or alter:

- `getObjectProperty`, used by the primary render-layer resolution path;
- `tryResolveLayerDispatch`, used by render targets, render execution, and SLA
  assignment resolution;
- the scoped particle Array owner and its exact count/index helpers;
- `DictionaryEnumerator` or any public NCB row.

Because the deleted helpers were inline and had zero callers, this is a
source-structure and misleading-boundary correction with no generated runtime
behavior change.

## Validation

- Ordinary and `KRKR2_WASMTIME_HEADLESS` Emscripten syntax checks passed for
  the full motionplayer unit-test translation unit; only the existing `_tss`
  deprecation warning was emitted.
- `Web Debug Build --target motionplayer` completed all 25 affected compile /
  archive steps.
- `Wasmtime Headless Debug Build --target motionplayer` completed the same 25
  affected steps.
- The complete `Web Debug Build` linked the final WebAssembly/HTML output.
- Exact searches under motionplayer production and tests found zero remaining
  references to the three deleted identifiers.
- `getObjectProperty` remains at one definition plus one production call;
  `tryResolveLayerDispatch` remains at one definition plus twelve production
  calls.
- `git diff --check` passed for the tracked header edit; the new analysis file
  has no trailing whitespace.
- All twelve reference functions were force-recompiled and read back with the
  boundary-closure comments, then all four recovery IDBs were saved in place.
