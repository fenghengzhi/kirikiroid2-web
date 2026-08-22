# NCB registrar pointer-only / trivial destructor / permanent top-chain four-binary audit

## 1. Scope and corrected conclusion

V215 deliberately left the cross-translation-unit lifetime of the static `ncbAutoRegister`
objects unresolved. This slice closes that boundary against all four current
`reference/binaries/` images and corrects a tempting but wrong model: registrar objects do not own
a `ttstr`, do not run a static destructor, and do not unlink themselves from `_top`.

The common four-image result is:

1. the module name stored in every registrar is a borrowed `const tjs_char *` pointing at a static
   literal, not an owned string object;
2. class registrars add another borrowed class-name literal; callback registrars add two borrowed
   function pointers;
3. the resulting base/class/callback objects are pointer-only and trivially destructible;
4. no registrar address is registered with `__cxa_atexit` in any representative single- or multi-
   registrar bundle;
5. the three `_top` heads are BSS-zero-initialized and only receive head-insert writes; no final-
   image path unlinks an element or nulls a head at shutdown;
6. NCB set/map construction is not a registrar freeze boundary: all four images contain later
   mod-init entries which continue to insert registrars;
7. at process exit, map/set and unrelated adjacent globals may be destroyed, but registrar storage,
   vptrs, literal pointers, next links, and callback pointers have no destructor phase. Therefore the
   top chain itself never enters a dangling-registrar window.

This is stronger and more precise than saying “the map holds borrowed pointers”: the pointees are
not merely borrowed during runtime; they are process-lifetime static objects with no teardown code.

## 2. Four-image mapping

| Reference | `_top[3]` base | DrawDevice registrar bundle | emote dependency mixed init | motion mixed init | NCB set/map init | representative later registrar init |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x1AB5920` | `0x42CBD8` | `0x42EEE0` | `0x42F1F8` | `0x42F408` | `0x42F604` |
| Android armeabi-v7a | `0x1111BC0` | `0x2FF094` | `0x3013BC` | `0x3016E8` | `0x3018E0` | `0x301AE4` |
| iOS arm64 | `0x10256B8F8` | `0x10024CB00` | `0x1001CAE20` | `0x10014FC74` | `0x1002A03DC` | `0x100402100` |
| iOS armv7 | `0x218F184` | `0x24E6D8` | `0x1C8EB2` | `0x151C98` | `0x2A4DD8` | `0x3E99FC` |

Recovery names use `_guess` for stripped/private source identities:

- `DrawDeviceD3D_NCBRegistrarBundle_Init_guess`;
- `emoteplayer_DependencyRegistrar_MixedStaticInit_guess`;
- `motionplayer_NCBRegistrar_MixedStaticInit_guess`;
- the V215 `ncbPluginGlobalContainers_Init_guess` and `ncbAutoRegister_top_guess` names.

The two Android “later” examples are `extrans.dll` registrar initializers. The two iOS examples are
the already identified `TextRenderBase_NCB_StaticInit`. They are representatives selected because
their mod-init slots occur after the NCB set/map constructor, not because those unrelated plugins
are part of motionplayer.

## 3. Mod-init ordering

The static initializer slots make the relative-order claim concrete:

| Reference | DrawDevice bundle slot | motion mixed slot | emote dependency slot | NCB set/map slot | later registrar slot |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x19E6F80` | `0x19E7288` | `0x19E7210` | `0x19E72A8` | `0x19E72D0` |
| Android armeabi-v7a | `0x10A0FD8` | `0x10A115C` | `0x10A1120` | `0x10A116C` | `0x10A1180` |
| iOS arm64 | `0x1019AA598` | `0x1019AA3F8` | `0x1019AA460` | `0x1019AA628` | `0x1019AA7B8` |
| iOS armv7 | `0x1775C48` | `0x1775B78` | `0x1775BAC` | `0x1775C90` | `0x1775D58` |

Android armv7 mod-init entries encode Thumb function pointers, but the table records the slot
addresses, not the encoded function values.

The sequences differ across platform linkers, yet every row proves the same two facts:

- many registrar chains already exist before the NCB containers are constructed;
- at least one registrar is inserted after those containers are constructed.

There is therefore no single “construct all registrars, then construct map/set” or inverse phase.
Runtime startup calls `AllRegist` only after language-level static initialization has completed, so
it sees the final chains; process-exit registration order must not be inferred from one TU's source
order.

## 4. Registrar ABI

### 4.1 Base

| ABI | vptr | module literal pointer | next registrar | size |
|---|---:|---:|---:|---:|
| LP64 | `+0x00` | `+0x08` | `+0x10` | `0x18` |
| ILP32 | `+0x00` | `+0x04` | `+0x08` | `0x0C` |

The module literal itself is UTF-32/TJS-wide in these Android images and UTF-16/TJS-wide in the
iOS images, but the registrar stores only its pointer. Character width therefore does not alter the
registrar object layout.

### 4.2 Native-class registrar

| ABI | base | borrowed class-name pointer | size |
|---|---:|---:|---:|
| LP64 | `0x18` | `+0x18` | `0x20` |
| ILP32 | `0x0C` | `+0x0C` | `0x10` |

Representative Motion-root class registrar objects are:

| Reference | object |
|---|---:|
| Android arm64-v8a | `0x1AB5508` |
| Android armeabi-v7a | `0x11119A0` |
| iOS arm64 | `0x101B699C8` |
| iOS armv7 | `0x187D670` |

### 4.3 Callback registrar

| ABI | base | init callback | term callback | size |
|---|---:|---:|---:|---:|
| LP64 | `0x18` | `+0x18` | `+0x20` | `0x28` |
| ILP32 | `0x0C` | `+0x0C` | `+0x10` | `0x14` |

Representative `emoteplayer.dll` dependency callback objects are:

| Reference | object | term field |
|---|---:|---|
| Android arm64-v8a | `0x1AB5030` | null |
| Android armeabi-v7a | `0x11115C8` | null |
| iOS arm64 | `0x101B6A0E0` | null |
| iOS armv7 | `0x187DB00` | null |

These objects load motionplayer from their init callback, but contribute no term callback. That is a
separate, object-local reason the dormant aggregate `AllUnregist` would do nothing for this
particular dependency registrar even on Android.

## 5. Construction and head insertion

The source-level operation remains:

```cpp
ncbAutoRegister::ncbAutoRegister(const tjs_char *module, LineT line)
    : modulename(module), _next(_top[line]) {
    _top[line] = this;
}
```

Derived constructors then store their additional literal or callback pointers. No allocation,
reference counting, string construction, map lookup, lock, guard acquisition, or exception-producing
callee belongs to the registrar construction itself.

The optimized instruction order is intentionally not uniform. For the emote dependency callback:

- Android arm64 publishes `_top[Pre]` before final vptr/module/next/callback stores;
- Android armv7 likewise publishes before its combined field stores;
- iOS arm64 stores vptr/module first, publishes the head, then stores next/init/term;
- iOS armv7 stores module first, publishes the head, then stores next/vptr/init/term.

Other bundles schedule the same source operation differently and may fill most fields before the
head store. This is valid under the program's assumed single-threaded static initialization, but it
proves the facility is not safely publishable to a concurrent `AllRegist` reader. The existing source
warning that NCB auto-registration is thread-unsafe is therefore a binary-observable boundary, not
just a conservative comment.

After an initializer returns, all fields are complete and the chain order is the reverse of that
line's construction order. Repeated `AllRegist` never mutates `next`; V215's repeated generations
exist only in downstream map lists.

## 6. Why nearby `__cxa_atexit` calls are not registrar destructors

Several mixed initializer functions contain both registrar construction and legitimate destructor
registration for unrelated adjacent static objects. This can look like a registrar destructor if
only the function-level call graph is inspected.

The four motion mixed initializers make the distinction explicit:

- their `__cxa_atexit` object arguments point at a map/vector-like state object and a
  `std::function`-like object;
- the Motion and Bezier registrar addresses are never used as the `obj` argument;
- the DrawDevice multi-registrar bundles and emote dependency initializers contain no atexit call at
  all;
- later simple registrar initializers likewise contain no atexit call.

The base has virtual `Regist` and `Unregist` slots but no virtual destructor slot. Virtual methods do
not by themselves make its implicit non-virtual destructor nontrivial. Pointer-only derived
registrars consequently need no compiler-generated static destructor registration.

## 7. Shutdown timeline

The proven lifecycle is:

```text
zero initialization
  top[0..2] = null
  registrar storage = zero

mod-init sequence (platform-specific interleaving)
  raw-store registrar fields
  registrar.next = old top[line]
  top[line] = registrar
  construct/register destructors for unrelated globals where needed
  construct NCB registered set and internal map; register their two destructors
  continue constructing later registrars

runtime startup
  AllRegist line 0..2 snapshots the completed permanent chains into borrowed-pointer lists
  module loads register callbacks and commit lowercase keys

process exit
  later unrelated atexit entries run in reverse registration order
  internal NCB map destructor frees keys/list/tree nodes
  registered set destructor frees committed-key/tree nodes
  no registrar destructor runs
  no registrar is unlinked and no top head is nulled
```

The exact placement of the two NCB container destructors among every unrelated global destructor is
platform/link-order dependent. What is closed here is the registrar boundary: no registrar can be
destroyed “too early”, because no registrar destructor exists and static storage remains allocated
for process lifetime.

If hypothetical external code invoked Android's dormant `AllUnregist` after other dependent globals
had already been destroyed, the registrar pointer chain would still be structurally valid, but a
callback body could touch already-torn-down external state. No final-image caller creates that edge,
so this report does not invent a supported shutdown API from it.

## 8. Portable-source alignment

`cpp/core/plugin/ncbind.hpp` now records and enforces the recovered boundary:

- `modulename` is documented as a borrowed literal pointer, not a `ttstr`;
- constructor comments describe permanent head insertion, absence of unlink, and the optimized
  partial-publication/thread-unsafety boundary;
- compile-time size assertions require base `3 * sizeof(void *)` and callback registrar
  `5 * sizeof(void *)`;
- compile-time type traits require both base and callback registrar to remain trivially destructible.

`tests/unit-tests/plugins/motionplayer-dll.cpp` also requires its concrete link-probe registrar to
remain trivially destructible. This protects the real derived-object property, not only the abstract
base.

No explicit registrar destructor or unlink method is added. Such a “cleanup improvement” would
contradict all four final images, create new shutdown ordering constraints, and make V215's borrowed
map pointers potentially dangle.

V215 and V217 reports were corrected to replace their previously conservative unresolved wording
with this closed no-registrar-teardown result.

## 9. Recovery-IDB writeback

All databases were opened and saved sequentially:

- 12 local ABI type declarations: LP64/ILP32 base, callback, and class registrar layouts in each
  applicable database;
- 8 global type applications: representative callback and Motion class registrar per image;
- 12 semantic initializer renames: three per image;
- 36 append-only function/line/data comments: nine per image;
- 24 bookmarks: six per image.

All four databases were saved and closed. The final IDA session audit returned zero sessions.

## 10. Validation and products

- ordinary and `KRKR2_WASMTIME_HEADLESS=1` syntax-only checks passed, including all new size and
  trivial-destructor assertions;
- Web rebuilt and linked 82/82 affected steps;
- Wasmtime rebuilt and linked 119/119 affected steps;
- both CTest invocations returned zero; neither build tree currently registers CTest cases;
- both products satisfy `WebAssembly.validate == true` and parse as `WebAssembly.Module`;
- imports/exports remain Web `539/69` and Wasmtime `538/69`;
- `git diff --check` returned zero with only the repository's existing LF-to-CRLF warnings.

The compile-time-only change leaves both products byte-identical to V217:

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

## 11. Limits

- this report proves registrar object lifetime and top-chain validity; it does not claim every
  unrelated global used by every `Regist`/`Unregist` callback remains alive at every atexit point;
- mod-init physical ordering is a final-image linker fact, not a claim about original source file
  order;
- the exact source names of stripped initializer bundles remain `_guess`;
- the slice closes a high-value NCB lifetime boundary but does not complete the full motionplayer
  recovery goal.
