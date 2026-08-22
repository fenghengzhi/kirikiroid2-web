# NCB `AllUnregist` Android survivor / iOS dead-strip / no unload consumer four-binary audit

## 1. Scope and result

This slice re-audits the aggregate NCB unregistration path against all four current
`reference/binaries/` images. It deliberately does not inherit the old `libkrkr2.so` assumption
that a surviving `Unregist` virtual method implies a reachable plugin-unload pipeline.

The four-image result is narrower and asymmetric:

- both Android images retain an aggregate `ncbAutoRegister::AllUnregist` traversal;
- that traversal has no caller or other code xref in either Android final image;
- both iOS images dead-strip the aggregate traversal completely;
- individual registrar `Unregist` wrappers still survive in every image because their addresses are
  present in registrar vtables;
- no final image has a consumer that connects `Plugins.unlink`, either NCB container, static
  teardown, or a named unload service to those virtual slots.

Thus the Android aggregate is dormant final-image code, not evidence of a working module-unload
state machine. The current port keeps the source template because it exactly represents the
Android survivor, but `Plugins.unlink` remains a true no-op.

## 2. Four-image mapping

### 2.1 Aggregate traversal and static top heads

| Reference | aggregate `AllUnregist` | `_top[0]` / three-head base | final-image status |
|---|---:|---:|---|
| Android arm64-v8a | `0x548DA4` | `0x1AB5920` | code survives inside IDA's preceding merged function; no xref/caller |
| Android armeabi-v7a | `0x4A9614` | `0x1111BC0` | standalone function; no xref/caller |
| iOS arm64 | absent | `0x10256B8F8` | aggregate dead-stripped; head is otherwise used by construction/`AllRegist` |
| iOS armv7 | absent | `0x218F184` | aggregate dead-stripped; head is otherwise used by construction/`AllRegist` |

The arm64 Android database had already merged `0x548DA4` into the function beginning at
`0x548D04`. The bytes and control-flow island are sufficient to recover the traversal, but this
slice does not destructively split an uncertain pre-existing function boundary. It records a line
comment and bookmark at the actual entry instead.

### 2.2 Representative surviving virtual `Unregist` wrappers

| Registrar family | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `D3DEmoteModule` | `0x540F54` | `0x4A2BC4` | `0x100244388` | `0x244788` |
| `Player` | `0x5422DC` | `0x4A3B54` | `0x10024569C` | `0x245DDC` |

For each representative wrapper, the only direct incoming reference is its registrar vtable data
slot (with the usual Thumb `+1` encoded function pointer on 32-bit ARM where applicable). There is
no direct call site. This explains why the wrappers survive on iOS even though the aggregate loop
does not: virtual table reachability retains the methods independently of aggregate call reachability.

`llvm-nm` also finds no named `AllUnregist` or `TVPUnloadPlugin` symbol in the stripped reference
images; the conclusions above come from code shape, top-head xrefs, vtable slots, and caller closure,
not from a surviving private C++ name.

## 3. Recovered Android algorithm

The two Android images implement the same source-level loop:

```cpp
void ncbAutoRegister::AllUnregist() {
    for (int line = 0; line < 3; ++line) {
        for (const ncbAutoRegister *p = _top[line]; p; p = p->_next) {
            p->Unregist();
        }
    }
}
```

The ABI-specific virtual dispatch is:

- Android arm64: load vptr from the registrar and call the second virtual slot at `vptr + 8`;
- Android armv7: load vptr and call the second virtual slot at `vptr + 4`, observing the Thumb
  function-pointer convention.

No part of the surviving body reads `_internal_plugins` or `TVPRegisteredPlugins`. It does not
inspect module names, look up a module, select only successfully loaded modules, erase a committed
marker, remove internal list nodes, or clear either container.

## 4. Ordering and duplicate-index independence

The outer loop is forward line order:

1. `PreRegist`;
2. `ClassRegist`;
3. `PostRegist`.

This is not a reverse teardown order. Within each line it follows the head-insert registrar chain,
just as the source template does. Every static registrar object is visited once per aggregate call.

This distinction matters after V215's repeated-`AllRegist` result. Repeated indexing appends
duplicate borrowed registrar pointers to per-module lists, but the aggregate unregistration loop
never consults those lists. Consequently:

- K indexed generations do not cause K aggregate `Unregist` calls;
- a registrar whose module was never loaded would still receive `Unregist` if an external caller
  invoked the dormant Android aggregate;
- the registered-set commit state does not suppress or enable any call;
- no duplicate internal-map node is removed or made less dormant afterward.

This shape is an all-static-registrars traversal, not a loaded-module teardown operation.

## 5. Failure boundary

There is no transaction or cleanup guard around a virtual call. If a registrar's `Unregist` throws,
the exception propagates immediately:

- registrars already visited in the current forward prefix stay unregistered to whatever extent
  their individual wrapper completed;
- the remaining registrar suffix is not visited;
- neither NCB container is repaired or cleared;
- a later external call would restart from `PreRegist` and the current head, not resume at the
  failure point.

This is the direct consequence of the recovered loop. In the four final images it remains a dormant
edge case because no internal caller reaches the aggregate.

## 6. No unload consumer

The surrounding lifecycle is now closed across V211, V215, V216, and this slice:

- `Plugins.unlink(name)` performs the argument conversion required by its script wrapper, returns
  success, and does not call `AllUnregist`, consume `name`, erase the registered set, or alter the
  internal map;
- the public/internal module loaders only register and commit; no paired `UnloadModule` consumer
  survives;
- NCB map and set destructors free their own key/tree/list nodes only; they do not own registrar
  pointees and do not invoke registrar virtual methods;
- registrar objects have pointer-only trivial destruction and no `__cxa_atexit` entry at all, so
  there is no registrar static-object teardown to route through the aggregate traversal;
- iOS has no aggregate entry to route through in the first place.

Therefore successful registration remains committed until process teardown, while vtable-resident
unregistration implementations are dormant capabilities rather than an exercised unload pipeline.

## 7. Portable-source alignment

The executable source algorithm was already structurally correct, so this slice changes only
provenance and boundary comments:

- `cpp/core/plugin/ncbind.hpp` now describes Android survivorship, iOS dead-strip, direct top-chain
  traversal, forward `Pre/Class/Post` order, duplicate-map independence, and lack of container clear;
- `cpp/core/plugin/PluginImpl.cpp` explicitly records that `TVPUnloadPlugin`/`Plugins.unlink` does not
  call the dormant aggregate or touch either container;
- `tests/unit-tests/plugins/motionplayer-dll.cpp` labels the existing zero-`Unregist` assertion as a
  check that unlink does not reach the dormant vtable path;
- the older module dependency/lifecycle report is corrected so it no longer overgeneralizes
  `AllUnregist` absence from iOS to Android.

No test directly invokes aggregate `AllUnregist`: doing so would destructively mutate the process-
global script registration surface and pollute the rest of the unit-test executable. The safe and
reference-relevant assertion is the negative consumer boundary at `Plugins.unlink`.

## 8. Recovery-IDB writeback

All four recovery databases were updated sequentially, saved, and closed:

- 1 semantic function rename and 1 function type application for the standalone Android armv7
  aggregate;
- 14 append-only comments covering Android aggregate entries, platform absence boundaries, and
  representative virtual wrappers;
- 12 bookmarks covering the same closure points;
- no speculative aggregate function was created in either iOS database;
- no destructive function split was performed at the merged Android arm64 entry.

The final IDA session audit reports zero open sessions.

## 9. Validation and products

- ordinary and `KRKR2_WASMTIME_HEADLESS=1` syntax-only checks both passed, with only the existing
  `_tss` deprecation warning;
- the Web build rebuilt and linked 82 affected steps;
- the Wasmtime build rebuilt and linked 119 affected steps;
- CTest returned zero for both build trees; neither tree currently has registered CTest cases;
- both products satisfy `WebAssembly.validate == true` and parse as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- `git diff --check` returned zero, with only the repository's existing LF-to-CRLF warnings.

The final products are byte-identical to V216:

| Product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,793 B | `858A3677901252A11D37637BC3BE7423D1ACD9D019080E64E18276379CE49D55` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,934 B | `FC8847E666976A424C9BD1A4780E5124F071D114CB6373B1F6985AC350A22C08` |

The relevant section sizes are likewise unchanged:

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` |
| GLOBAL | `0xD5C2` | `0xD5EA` |
| CODE | `0x1A41AB5` | `0x19E9A63` |
| DATA | `0x5A3F00` | `0x5A1150` |
| name | `0x3185E4E` | `0x3141CE4` |

## 10. Limits of the conclusion

- iOS aggregate absence proves final-image dead-strip, not that the original unoptimized source
  lacked the same inline/template definition;
- vtable-only wrapper survival proves a callable virtual capability, not a live caller;
- the Android dormant loop's source-equivalent behavior is proven, but it is intentionally not
  elevated to a supported script/public unload API;
- this slice closes one NCB lifecycle boundary and does not complete the full motionplayer recovery
  goal.
