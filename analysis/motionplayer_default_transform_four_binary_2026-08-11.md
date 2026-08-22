# Motion.Player class defaults: four-binary reconstruction (2026-08-11)

## Scope

This note records the four-reference-binary evidence for the class-level
`Motion.Player.defaultSyncActive` and `Motion.Player.defaultTransformOrder`
properties.  It supersedes the old local comments derived from a different
`libkrkr2.so`: in particular, the former Android ARM64 accessor addresses and
the claimed `defaultSyncActive == 0xff` initializer do not describe the four
current references under `reference/binaries/`.

The four current images agree on the source-level state and boundary behavior:

- one process-global boolean/byte, initially false;
- one process-global four-integer permutation, initially `{0, 3, 2, 1}`;
- `defaultTransformOrder` returns a newly allocated TJS Array;
- its setter converts the input Variant to an object first, reads four required
  numbered properties, validates a permutation, and writes incrementally.

## Four-target registration and accessor mapping

| Target | Player registrar | `defaultSyncActive` registration / get / set | `defaultTransformOrder` registration / get / set |
|---|---:|---:|---:|
| Android ARM64 | `0x6D3DA8` | `0x6D3EAC` / `0x6D67D8` / `0x6D67E4` | `0x6D3F24` / `0x6ADD5C` / `0x6ADE94` |
| Android ARMv7 | `0x597EC8` | `0x597EEC` / `0x598D24` / `0x598D30` | `0x597F08` / `0x57F4B8` / `0x57F54C` |
| iOS ARM64 | `0x1001244F8` | `0x100124520` / `0x100125430` / `0x10012543C` | `0x100124544` / `0x100106564` / `0x1001066A8` |
| iOS ARMv7 | `0x123848` | `0x123864` / `0x124626` / `0x124634` | `0x12388C` / `0x103918` / `0x103A84` |

The Android ARM64 registration walk is especially important for stale-comment
cleanup: the current binary registers `defaultTransformOrder` with accessors
`0x6ADD5C` and `0x6ADE94`.  The old local `0x6B097C` / `0x6B0AB4` pair belongs
to the prior single-binary investigation and is not the mapping of this image.

## Process-global storage and image initializers

| Target | sync byte | image bytes | transform array | decoded image initializer |
|---|---:|---:|---:|---|
| Android ARM64 | `0x1AB54A8` | `00` | `0x1AA10D8` | `{0, 3, 2, 1}` |
| Android ARMv7 | `0x111160C` | `00` | `0x1102090` | `{0, 3, 2, 1}` |
| iOS ARM64 | `0x102517794` | `00` | `0x101ADF750` | `{0, 3, 2, 1}` |
| iOS ARMv7 | `0x2143970` | `00` | `0x1831844` | `{0, 3, 2, 1}` |

`get_bytes` was run on all eight locations.  Each transform location contains
the little-endian dwords `0, 3, 2, 1`; every sync byte is zero.  Therefore the
source-equivalent initializer is `false`, not the old comment's `0xff/true`.

## Common source-level pseudocode

```cpp
static bool defaultSyncActive = false;
static int defaultTransformOrder[4] = {0, 3, 2, 1};

bool getDefaultSyncActive() {
    return defaultSyncActive;
}

void setDefaultSyncActive(bool value) {
    defaultSyncActive = value;
}

tTJSVariant getDefaultTransformOrder() {
    auto result = createTJSArrayWithNativeItems();
    for (int i = 0; i != 4; ++i)
        result.items.emplace_back(defaultTransformOrder[i]);
    return result.array;
}

void setDefaultTransformOrder(tTJSVariant value) {
    // TJS Variant-to-object conversion occurs here.  A non-object throws the
    // ordinary conversion exception before the custom size message is used.
    iTJSDispatch2 *object = value.AsObjectNoAddRef();
    bool used[4] = {};

    for (int i = 0; i != 4; ++i) {
        tTJSVariant element;
        if (TJS_FAILED(object->PropGetByNum(
                TJS_MEMBERMUSTEXIST, i, &element, object)))
            throw L"illegul size of transform order";

        int v = static_cast<int>(static_cast<tjs_int>(element));
        if (static_cast<unsigned int>(v) > 3 || used[v])
            throw L"illegul variable for transform order";

        defaultTransformOrder[i] = v;
        used[v] = true;
    }
}
```

## ABI and container differences

- All four sync getters perform a byte load and all four setters perform a byte
  store.  Android ARM64 additionally emits `AND W8, W0, #1` before `STRB`.
  Android ARMv7 and both iOS builds store the low input byte directly.  The NCB
  property is typed as `bool`, so this is a compiler/ABI code-generation
  difference rather than a different source-level property contract.
- The two 64-bit getters return the zero-extended byte in `W0`; the two 32-bit
  getters use `LDRB R0`.
- The Array/native-Items construction and Variant element sizes differ with
  target ABI and STL layout.  All four nevertheless allocate a fresh TJS Array
  and append exactly four integer Variants in global-array order.
- The global addresses differ per image, but no accessor consults a `Player`
  instance.  Changes are shared by all subsequently observed Player instances.

## Boundary and failure behavior

- A non-object `defaultTransformOrder` assignment enters the native
  `tTJSVariant` object conversion helper.  Its ordinary TJS conversion error is
  observable; it must not be rewritten as the custom size error.
- A Variant whose type is object but whose dispatch pointer is null survives
  the conversion and is then dereferenced by the numbered-property call.  The
  native code has no null guard and no recovery path.
- Every numbered read uses flag `1024` (`TJS_MEMBERMUSTEXIST`).  Any failed read
  throws exactly `"illegul size of transform order"` (native spelling kept).
- Each fetched element is converted to integer before validation; conversion
  failures propagate normally.
- Range validation is unsigned, so negative values and values above `3` take
  the same invalid-variable path.  Duplicates take that path as well, throwing
  exactly `"illegul variable for transform order"`.
- Writes are interleaved with reads and checks.  If index 2 fails, successful
  writes to indices 0 and 1 remain in the process-global array.  The setter is
  deliberately non-transactional and does not restore the old permutation.
- The class-default setter changes only the global array.  It does not mark an
  existing Player root dirty and does not rewrite existing instance transform
  orders.

## Pre-edit local line-by-line comparison

Before the semantic edit, the local port already matched the fresh-Array
getter, required indexed reads, exact typo-bearing exception strings, unsigned
range check, duplicate check, and incremental global writes.  The remaining
observable differences were:

1. `Player::s_defaultSyncActive` was initialized to `true`, based on the stale
   `byte_1AB84A8 == 0xff` comment.  All four current images initialize it to
   zero/false.
2. `setDefaultTransformOrder` manually tested `Type() == tvtObject` and mapped
   both a non-object and a null object to
   `"illegul size of transform order"`.  The references instead invoke native
   Variant-to-object conversion first and have no null guard.
3. Comments in `Player.h`, `PlayerCore.cpp`, and `main.cpp` still named the old
   Android global/accessor addresses and the old true initializer.

The intended local correction is consequently narrow: initialize the global
boolean to false, call `AsObjectNoAddRef()` directly, retain the native lack of
a null check, and replace the stale single-binary comments with address-free
four-reference descriptions.  The existing valid permutation algorithm is not
being redesigned.

## Applied reconstruction and verification

- `Player::s_defaultSyncActive` now initializes to `false`.
- `setDefaultTransformOrder` now calls `AsObjectNoAddRef()` directly and has no
  hand-written null guard; the existing required reads, validation, messages,
  and incremental writes remain intact.
- Stale addresses and the obsolete `0xff/true` claim were removed from the
  compiled-source comments in `Player.h`, `PlayerCore.cpp`, and `main.cpp`.
- In every IDB, the four accessors were renamed to
  `Player_getDefaultSyncActive_guess`,
  `Player_setDefaultSyncActive_guess`,
  `Player_getDefaultTransformOrder_guess`, and
  `Player_setDefaultTransformOrder_guess`.  The two globals were named
  `g_Player_defaultSyncActive_guess` and
  `g_Player_defaultTransformOrder_guess`, with `bool` and `int[4]` types.
- Hex-Rays caches were invalidated for all 16 accessors and fresh decompilation
  preserved the expected control flow.  All four IDBs were saved.
- `cmake --build --preset "Web Debug Build"` completed all 31 steps and linked
  `index.html` successfully.  Only pre-existing compiler/toolchain warnings
  were emitted.
