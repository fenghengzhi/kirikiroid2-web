# TVPDoTryBlock callback / exception state-machine four-binary audit

## 1. Scope and conclusion

This slice recovers the complete `TVPDoTryBlock` compatibility service around
its four callback/data arguments, two catch classes, stack description record,
callback ordering, rethrow behavior, and replacement-exception edges.

The four-reference result is:

1. both Android final images retain the full service, but neither has an
   internal caller/xref;
2. both iOS final images linker-dead-strip it;
3. `tryblock` is mandatory and is the only operation inside the source try;
4. `finallyblock` alone is nullable;
5. after normal try completion, finally runs outside the try and any exception
   it throws propagates directly;
6. after a caught exception, finally runs first, before description
   construction and before catchblock;
7. `catchblock` is mandatory on the exceptional path;
8. callback false swallows the active exception, while true uses
   `__cxa_rethrow` and preserves the original exception object/type;
9. eTJS descriptions contain type `eTJS` plus a copied virtual message;
   catch-all descriptions contain type `unknown` plus a default-empty message;
10. the description is a stack-local pair of owning `ttstr` values and is valid
    only during the catch callback.

The current portable control flow already matches the two retained Android
implementations.  The source change is documentation, a two-`ttstr` layout
assertion, and focused callback/exception regression coverage.

## 2. Four-image mapping and survivorship

| Reference | `TVPDoTryBlock` | caller/xref | result |
|---|---:|---:|---|
| Android arm64-v8a | `0x908378` | none | retained dormant service |
| Android armeabi-v7a | `0x6C7C00` | none | retained dormant service |
| iOS arm64 | absent | n/a | linker-dead-stripped |
| iOS armv7 | absent | n/a | linker-dead-stripped |

The Android arm64 primary function extent is `0x168` bytes; IDA also exposes
its exception/destructor cleanup chunks through the final unwind resume.  The
Android armv7 primary extent is `0x106` bytes, with the two terminal rethrow
stubs immediately outside the initially inferred extent.  Reading only the
main decompiler output would show almost only the normal path and miss most of
the source routine.

### 2.1 iOS absence proof

The iOS conclusion is not based only on adjacency:

- the terminated UTF-16/TJS-wide literal `eTJS`, required to construct the
  eTJS description, has zero matches in each image;
- surviving narrow `eTJSE` sequences are RTTI type-name fragments and are not
  the description literal;
- iOS arm64 has 286 direct `__cxa_rethrow` code xrefs grouped into 259 unique
  functions; all 259 were decompiled and none has the four-callback signature
  or `tryblock(data)` / nullable-finally normal body;
- iOS armv7 has 252 calls through the rethrow thunk plus 35 direct calls, 287
  sites grouped into 243 unique functions; all 243 were decompiled and likewise
  contain no service signature/body.

No fake iOS function identity was created in the recovery databases.

## 3. Callback ABI and data forwarding

The source-level signature is:

```cpp
void TVPDoTryBlock(
    void (*tryblock)(void *),
    bool (*catchblock)(void *, const tTVPExceptionDesc &),
    void (*finallyblock)(void *),
    void *data);
```

Every callback receives the original `data` pointer unchanged.  The function
does not allocate, copy, validate, or own `data`.

Nullability is deliberately uneven:

- null `tryblock`: unconditional indirect call faults on every invocation;
- null `catchblock`: harmless if no exception occurs, but faults once either
  catch handler tries to invoke it;
- null `finallyblock`: explicitly tested and skipped on both normal and
  exceptional paths.

These are hardware-fault/ABI boundaries, not C++ exceptions intentionally
converted by this service.

## 4. `tTVPExceptionDesc` stack ABI and lifetime

The record is exactly two adjacent `ttstr` owners:

| ABI | `type` | `message` | total |
|---|---:|---:|---:|
| LP64 | `+0x00` | `+0x08` | `0x10` |
| ILP32 | `+0x00` | `+0x04` | `0x08` |

There is no tag word, exception pointer, callback pointer, inline character
buffer, or extra padding field.  Both members begin default-empty/null.  Type
and optional message assignment obtain their ordinary shared-string ownership;
destruction releases message and then type in reverse construction order.

The catch callback receives a const reference/pointer to this stack record.
It must copy any data that needs to outlive the callback; the service destroys
the record before returning or completing rethrow unwind cleanup.

## 5. Normal state machine

The normal path is:

```text
tryblock(data)
if (finallyblock)
    finallyblock(data)
return
```

Only the `tryblock(data)` expression is covered by the source `try`.  The
normal finally call occurs after the catch clauses.  Therefore:

- catchblock is not called on normal completion;
- no description is constructed;
- finally runs at most once;
- a C++ exception from normal-path finally propagates directly and is not fed
  back into this function's eTJS/catch-all handlers.

Android armv7 makes the boundary especially clear by tail-calling normal
finally after restoring the frame/stack check.

## 6. `catch(const eTJS&)` state machine

The eTJS path is:

```text
begin active catch(const eTJS&)
if (finallyblock)
    finallyblock(data)
construct stack desc
desc.type = "eTJS"
desc.message = e.GetMessage()       // virtual, copied owner
decision = catchblock(data, desc)
if (decision)
    rethrow original active exception
destroy desc
end catch
return
```

Because the catch is `const eTJS&`, derived objects such as `eTJSError` retain
their dynamic type.  Message retrieval is virtual and the returned `ttstr`
content is copied into the description owner before the callback.

The finally call precedes even default description construction.  If finally
throws, no description exists and catchblock is never entered.  If type/message
construction, virtual `GetMessage`, or message copying throws, finally has
already run and the new exception propagates after active-catch cleanup.

False from catchblock destroys the record, ends the catch, and swallows the
exception.  True uses `__cxa_rethrow`; it does not copy or translate the
exception into a new eTJS object.

## 7. `catch(...)` state machine

The catch-all path has the same ordering:

```text
begin active catch(...)
if (finallyblock)
    finallyblock(data)
construct stack desc
desc.type = "unknown"
desc.message = empty
decision = catchblock(data, desc)
false -> destroy desc, end catch, return
true  -> destroy desc during unwind, rethrow original
```

The original exception is not inspected, stringified, or converted.  An
integer, standard exception, or unrelated native exception all expose the same
fixed description.  On true, the precise original object/type is preserved by
active-exception rethrow.

## 8. Replacement-exception and once-only-finally boundaries

The callback ordering implies several observable edges:

- exceptional finally throws -> its new exception replaces/escapes the active
  original; catchblock is skipped;
- catchblock throws -> its new exception propagates; exceptional finally has
  already completed and is not called a second time;
- catchblock returns true -> the original exception propagates; finally is not
  called a second time;
- description construction/copy throws -> the new exception propagates after
  the already-completed finally;
- catchblock returns false -> original is swallowed after one finally call.

This is not a generic Java-style `finally` wrapper around the entire service.
It is a manually duplicated callback, once after normal try completion and once
at the start of each catch handler.

## 9. Bool lowering detail

For valid C++ callback results, every image-relevant retained implementation
uses the same `false`/`true` meaning.  The instruction lowering differs:

- Android arm64 tests return bit zero;
- Android armv7 compares the return register exactly with `1`.

A conforming `bool` function returns canonical zero or one, so the difference
is unobservable in valid C++.  Supplying a callback through an incompatible
function-pointer type and returning a noncanonical bit pattern is an ABI/UB
violation and is not promoted into a portable source contract.

## 10. Portable-source and test alignment

`cpp/core/plugin/PluginImpl.cpp` now documents:

- Android retention versus iOS dead-strip;
- the try-only protected region;
- finally-before-description ordering on exceptional paths;
- normal finally outside the handler;
- mandatory try/catch callback boundaries;
- exact eTJS/unknown description contents;
- stack-description ownership/lifetime;
- false swallow, true original rethrow;
- propagation of replacement exceptions without a second finally.

`cpp/core/plugin/PluginImpl.h` documents the public callback contract and adds:

```cpp
static_assert(sizeof(tTVPExceptionDesc) == 2 * sizeof(ttstr));
```

`tests/unit-tests/plugins/motionplayer-dll.cpp` includes the public header and
adds a stateful callback test covering:

- normal try/finally order and null finally;
- derived `eTJSError` matching the eTJS handler;
- copied `eTJS` type/message values after stack-record destruction;
- unknown type with empty message;
- false swallow;
- true rethrow preserving eTJSError message and integer exception value;
- normal finally replacement exception bypassing catchblock;
- exceptional finally replacement exception before description/catchblock;
- catchblock replacement exception after the single finally invocation.

Null tryblock/catchblock fault paths are documented but not deliberately
executed in the in-process Catch2 case.

## 11. Recovery-IDB writeback

All databases were handled sequentially and saved/closed:

- 2 semantic function renames, one per retained Android function;
- 2 callback-signature type applications;
- 32 append-only state-machine/lifetime/dead-strip comments;
- 16 bookmarks.

The iOS comments/bookmarks record literal and complete rethrow-closure evidence
at real surviving anchors; no absent helper address was named.  Final IDA
session audit returned zero open sessions.

## 12. Validation and products

- ordinary and `KRKR2_WASMTIME_HEADLESS=1` syntax-only checks passed after the
  final two-`ttstr` assertion and callback test;
- Web rebuilt and linked all 24 affected steps;
- Wasmtime rebuilt and linked all 24 affected steps;
- both CTest invocations returned zero and reported no tests because these two
  configured trees have `ENABLE_TESTS=false`;
- both Wasm outputs validate and construct as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- `llvm-nm` shows `TVPDoTryBlock` and `TVPGetScriptDispatch` retained in both
  local products, while the V219 Register/Remove helpers remain absent;
- `git diff --check` returned zero apart from existing LF-to-CRLF warnings;
- products remain byte-identical to V219/V218.

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

## 13. Limits

- the two final iOS images prove helper absence, not the precise original
  source/library partition before dead-strip;
- Android retention without callers proves a dormant compatibility service,
  not a motionplayer runtime dependency;
- the current local link retains this service even though the iOS references
  remove it; that is a link-surface difference, not a control-flow mismatch;
- the added Catch2 case is syntax-compiled in both active configurations, but
  those build trees do not register runnable unit tests;
- invalid noncanonical bool return values remain outside the C++ ABI contract;
- this closes one service-level exception state machine and does not complete
  the full motionplayer recovery goal.
