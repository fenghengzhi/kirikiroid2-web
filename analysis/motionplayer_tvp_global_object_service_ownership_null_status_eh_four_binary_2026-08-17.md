# TVP global-object service ownership / null / status / EH four-binary audit

## 1. Scope and conclusion

This slice recovers the plug-in compatibility services
`TVPRegisterGlobalObject` and `TVPRemoveGlobalObject`, together with the
`TVPGetScriptDispatch` ownership contract on which both depend.  The main
platform result is intentionally asymmetric:

1. both Android final images retain the two complete service functions, even
   though neither function has an internal caller;
2. both iOS final images retain the script-global getter because many NCB and
   engine paths use it, but linker-dead-strip the two uncalled services;
3. the current portable service bodies already match the retained Android
   algorithms, including their non-obvious null and exception boundaries;
4. registration intentionally does **not** check a null global, while removal
   does;
5. a non-null object argument is temporarily AddRef'd before global
   acquisition, and the global getter returns another independently owning
   reference;
6. only the virtual `PropSet`/`DeleteMember` call is swallowed by `catch(...)`;
   object-Variant construction and global acquisition occur before the
   protected call;
7. returned TJS status is converted with `TJS_SUCCEEDED`, so every
   nonnegative status is true and every negative status is false.

No behavior change was required.  The source was hardened with exact boundary
comments and the unit-test TU gained executable ownership/status/exception
probes.

## 2. Four-image mapping and survivorship

| Reference | `TVPGetScriptDispatch` | `TVPRegisterGlobalObject` | `TVPRemoveGlobalObject` | result |
|---|---:|---:|---:|---|
| Android arm64-v8a | `0x8E4000` | `0x9081D4` | `0x9082E0` | all three retained |
| Android armeabi-v7a | `0x6B4564` | `0x6C7AE0` | `0x6C7B98` | all three retained |
| iOS arm64 | `0x100187374` | absent | absent | services dead-stripped |
| iOS armv7 | `0x184B56` | absent | absent | services dead-stripped |

The two Android service entries have no code xref/caller.  Their retention is
therefore an exported/public-service linker fact, not evidence that
motionplayer calls them at runtime.

The iOS absence was not inferred merely from adjacency or symbol stripping.
For each iOS image the complete code-xref closure of
`TVPGetScriptDispatch` was mapped to 122 unique caller functions and every
caller was decompiled.  Neither closure contains the catch-bearing generic
registration/removal shape.  Each closure instead contains 23 ordinary NCB
`UnregistEnd` wrappers which call `DeleteMember` directly:

- arm64 wrappers are the canonical `0x70`-byte shape;
- armv7 wrappers are the canonical `0x40`-byte shape;
- none is a generic `bool(const tjs_char *)` service wrapper;
- none has the service's catch/status-conversion control flow.

This distinction matters: surviving per-class NCB unregister wrappers do not
prove that the generic exported removal helper survived.

## 3. `TVPGetScriptDispatch` ownership contract

All four images implement the same semantic operation:

```cpp
iTJSDispatch2 *TVPGetScriptDispatch() {
    if (!TVPScriptEngine)
        return nullptr;
    return TVPScriptEngine->GetGlobal();
}
```

The non-null branch is not a borrowed load.  It reaches the script engine's
global slot, invokes the dispatch's virtual `AddRef`, and returns that pointer.
Thus:

- null engine -> null pointer and no ownership change;
- non-null engine -> one owning global reference;
- every successful caller owes exactly one `Release`;
- an exception from the engine accessor/AddRef is not converted by either
  service, because acquisition precedes their protected property call.

The iOS getters make this especially visible as tiny branch/tail-call
functions.  Their downstream accessor reads the engine's global field, calls
the global dispatch's first virtual slot (`AddRef`), then returns the same
pointer.

## 4. Registration dataflow and boundary behavior

The retained Android implementation is:

```cpp
bool TVPRegisterGlobalObject(const tjs_char *name, iTJSDispatch2 *dsp) {
    tTJSVariant val(dsp);
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    tjs_error er;
    try {
        er = global->PropSet(
            TJS_MEMBERENSURE, name, nullptr, &val, global);
    } catch (...) {
        global->Release();
        return false;
    }
    global->Release();
    return TJS_SUCCEEDED(er);
}
```

### 4.1 Object Variant

`val` is constructed before the global is acquired:

- non-null `dsp`: `dsp->AddRef()` is called and the local Variant owns that
  temporary reference;
- null `dsp`: the Variant is still `tvtObject`, with both closure pointers
  null; it is not rewritten to `Void`;
- the one-argument object constructor sets `Object=dsp` and `ObjThis=null`;
- normal, negative-status, and caught-exception returns all destroy `val` and
  release the temporary object reference once.

If the global actually stores the Variant, that stored copy obtains its own
independent object reference.  Destroying the local temporary after return
does not invalidate the published global member.

### 4.2 Exact call ABI

The virtual call is `iTJSDispatch2::PropSet` with:

- flags `0x200` (`TJS_MEMBERENSURE`);
- the caller's `name` pointer unchanged;
- null member hint;
- pointer to the local object Variant;
- `objthis == global`.

There is no prevalidation of `name` or `dsp`.

### 4.3 Null-global asymmetry

Registration has no `if (!global)` branch.  A missing engine therefore leaves
`global == nullptr`, and virtual dispatch attempts to dereference it.  That is
a hardware fault/trap boundary, not a C++ exception produced by `PropSet`, so
the catch-all does not make the operation safely return false.  Adding a null
guard would be reasonable defensive code but would not be one-to-one with the
two retained references.

### 4.4 Exception and status behavior

The catch region protects only the virtual property call.  A C++ exception
from `PropSet` causes:

1. one `Release` of the owning global reference;
2. `false` as the service result;
3. ordinary return cleanup of the local Variant and its temporary `dsp`
   reference.

On a non-throwing call, the global reference is released once and the signed
TJS status is tested as `>= 0`.  The function therefore accepts nonzero
success statuses; it is not an exact `TJS_S_OK` check.

The Android armv7 exception landing for registration is emitted immediately
after IDA's initial function extent.  Its sequence is nevertheless unambiguous:
begin catch, release global, end catch, select false, then join the shared
Variant cleanup.  Treating only the initial decompiler function boundary as
the whole source routine would miss this behavior.

## 5. Removal dataflow and boundary behavior

The retained Android implementation is:

```cpp
bool TVPRemoveGlobalObject(const tjs_char *name) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if (!global)
        return false;
    tjs_error er;
    try {
        er = global->DeleteMember(0, name, nullptr, global);
    } catch (...) {
        global->Release();
        return false;
    }
    global->Release();
    return TJS_SUCCEEDED(er);
}
```

The exact virtual call uses flags zero, the unchanged `name`, a null hint, and
`objthis == global`.  There is no name validation.  A null name is therefore
forwarded to `DeleteMember`, whose negative result becomes false.

Removal differs from registration at precisely one pre-call branch: a null
global returns false.  For a non-null global, normal success, negative status,
and a caught C++ exception each balance the getter's owning reference exactly
once.  As with registration, only the virtual property call is swallowed;
global acquisition itself is outside the protected operation.

## 6. Relationship to NCB and motionplayer runtime

These helpers are service-layer compatibility functions, not the active NCB
registration path recovered in V210--V218:

- NCB class `RegistEnd` obtains the global and performs its own `PropSet`;
- NCB class `UnregistEnd` obtains the global and performs its own
  `DeleteMember`;
- neither wrapper calls the generic service functions;
- `Plugins.unlink` remains a true no-op and does not call generic removal;
- both Android service entries are dormant internally;
- iOS removes them entirely;
- the current Web and Wasmtime products likewise retain
  `TVPGetScriptDispatch` but contain no `TVPRegisterGlobalObject` or
  `TVPRemoveGlobalObject` symbol.

The portable definitions should therefore remain for public/source
compatibility, but their presence must not be described as evidence of the
motionplayer module's actual registration call chain.

## 7. Portable-source and test alignment

`cpp/core/plugin/PluginImpl.cpp` now records:

- Android retention versus iOS dead-strip;
- object-Variant construction before global acquisition;
- the owning getter contract;
- exact flags/hint/objthis call shape;
- registration's intentional missing-global crash boundary;
- removal's explicit missing-global false boundary;
- the PropSet/DeleteMember-only catch scope;
- balanced global and local-object references;
- nonnegative/negative TJS status conversion.

`cpp/core/plugin/PluginImpl.h` documents the same public-service contract at
the declarations.  The implementation statements were not reordered or
refactored because the existing sequence already matches Android.

`tests/unit-tests/plugins/motionplayer-dll.cpp` adds a focused case which
checks:

- stored-object AddRef/Release counts and closure `ObjThis == null`;
- successful removal releasing the remaining published owner;
- missing-member removal returning false;
- null `dsp` publishing an object-typed null closure;
- a property object returning `TJS_S_TRUE` -> true;
- the same property object returning `TJS_E_FAIL` -> false;
- a C++ exception thrown from the exact property `PropSet` -> false without
  leaking the candidate's temporary owner;
- the existing property descriptor is not replaced on status/exception
  paths;
- null names are forwarded and become false on the real global object;
- null engine -> false for removal.

The registration null-engine crash boundary is intentionally documented but
not executed inside the ordinary in-process Catch2 test: turning a recovered
hardware fault into a safe test-only return would specify the wrong behavior.

## 8. Recovery-IDB writeback

All four databases were opened, edited, saved, and closed sequentially.
Recovery names use `_guess` because the relevant identities are stripped or
private in the inspected images.

Writeback totals:

- 8 semantic function renames;
- 8 function type applications;
- 20 append-only ownership/null/EH/dead-strip comments;
- 12 bookmarks.

The Android databases received names/types/comments for the getter and both
services.  The iOS databases received only the real surviving getter identity
plus comments/bookmarks recording the complete 122-caller dead-strip proof;
no absent service function was fabricated.  Final session audit: zero open
IDA sessions.

## 9. Validation and products

- ordinary and `KRKR2_WASMTIME_HEADLESS=1` syntax-only checks passed for the
  enlarged unit-test TU;
- Web rebuilt and linked all 24 affected steps;
- Wasmtime rebuilt and linked all 24 affected steps;
- both CTest invocations returned zero and reported no tests, because the two
  configured trees have `ENABLE_TESTS=false`;
- both output files pass `WebAssembly.validate` and construct as
  `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- `git diff --check` returned zero, apart from the repository's existing
  LF-to-CRLF warnings;
- `llvm-nm` confirms the two service helpers are absent from both current
  products while `TVPGetScriptDispatch` remains;
- the comment/test-only slice leaves both products byte-identical to V218.

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,793 B | `858A3677901252A11D37637BC3BE7423D1ACD9D019080E64E18276379CE49D55` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,934 B | `FC8847E666976A424C9BD1A4780E5124F071D114CB6373B1F6985AC350A22C08` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41AB5` | `0x19E9A63` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E4E` | `0x3141CE4` |

## 10. Limits

- iOS dead-strip proves final-image absence, not the exact source/library
  partition in which Apple builds originally defined the helpers;
- Android retention without callers proves a dormant public service, not an
  active motionplayer dependency;
- the unit-test configuration compiles the new runtime probe under both macro
  modes but the two existing Web build trees do not register Catch2 tests;
- no in-process test attempts the intentional registration null-global fault;
- this closes one plug-in-service ownership/EH boundary and does not complete
  the full motionplayer recovery goal.
