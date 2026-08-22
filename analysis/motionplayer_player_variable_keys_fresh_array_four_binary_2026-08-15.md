# `Motion.Player.variableKeys`: fresh-Array getter in four references

Date: 2026-08-15

## Scope and conclusion

This note replaces the old single-Android-address claim attached to
`Player::getVariableKeys()`. The old `0x6D139C` / `sub_704CB8` identity does not
name this getter in the current four-reference set.

All four current binaries implement the same source-level operation:

```cpp
auto result = createTJSArrayWithItems();
for (const auto &scope : variableLabelScopes)
    result.items->emplace_back(scope.cascadeKey);
return result.value;
```

The property is a generated no-argument Variant property with no setter. Every
call constructs a distinct TJS Array, including the empty-input case. The loop
uses the physical deque order and performs no filtering, sorting, uniqueness
check, intermediate-vector construction, or script-level `add` dispatch.

## Function and registration map

| Reference | Getter | Player NCB registrar | Registration site |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6CE77C` | `0x6D3DA8` | descriptor `0x6D4084`-`0x6D40CC` |
| Android armv7 | `0x5948D0` | `0x597EC8` | call ending at `0x597F88` |
| iOS arm64 | `0x1001200B4` | `0x1001244F8` | call at `0x100124604` |
| iOS armv7 | `0x11EDC0` | `0x123848` | call ending at `0x123938` |

The armv7 registrars store Thumb function pointers with bit zero set. In the
three decompiled registrar calls the getter occupies the getter slot and all
setter-related arguments are zero. Android arm64 builds the equivalent 80-byte
descriptor inline: its getter pointer is `0x6CE77C`, while the remaining
function-pointer/storage fields are zero before publication under
`variableKeys`. This matches `NCB_PROPERTY_RO(variableKeys, getVariableKeys)`.

Do not confuse this property (#6 on `Motion.Player`) with property #49 on
`Motion.EmotePlayer`. `Player.variableKeys` always creates a new Array from
per-motion variable scopes. `EmotePlayer.variableKeys` returns a CopyRef of the
Engine's long-lived `_variableLabelsBase` Variant, which can initially be Void
and later aliases the same Array across calls.

## Fresh Array construction and ownership

The getter calls the common fresh-Array helper in each binary:

| Reference | `createTJSArrayWithItems_guess` | Returned Items pointer |
| --- | ---: | --- |
| Android arm64 | `0x702098` | native Array instance `+16` |
| Android armv7 | `0x5BAA70` | native Array instance `+8` |
| iOS arm64 | `0x10029FF58` | native Array instance `+16` |
| iOS armv7 | `0x2A4A80` | native Array instance `+8` |

Each helper calls `TJSCreateArrayObject(nullptr)`, makes an owning object
Variant whose Object and ObjThis are the new dispatch, releases the factory
reference, and asks `NativeInstanceSupport` for the Array native instance. The
helper result contains that owning Variant plus a borrowed pointer to the
native `tTJSArrayNI::Items` deque. The borrowed pointer is valid because the
adjacent Variant owns the dispatch for the complete construction interval.

Only a `TJS_S_OK` status (exact zero) publishes the Items pointer. Other status
values publish null, and neither the helper nor this getter provides a graceful
fallback. A nonempty scope deque would consequently dereference the null Items
pointer. The factory dispatch is also used unconditionally after its initial
reference setup, so a null factory result is not treated as an empty Array.

On the normal return path, the owning Variant is copied/moved into the caller's
result and the local builder Variant is destroyed. Android arm64 also has an
explicit landing pad that destroys the partial Array Variant before resuming an
exception. iOS armv7 registers an SJLJ cleanup path for the same role. The
Android armv7 and iOS arm64 bodies contain no local destructor landing pad;
their emitted exception/unwind policy is therefore intentionally not inferred
beyond the observed normal cleanup.

## Input `VariableLabelScope` deque

`cascadeKey` is the first member of every scope element. The native append path
therefore receives the element address itself and reads the string handle at
offset zero. The remainder of the element is not consulted by this getter.

| Reference | Player field base | Getter-loaded iterator/state fields | Element / block layout |
| --- | ---: | --- | --- |
| Android arm64 | `+0x510` | begin.cur `+0x520`, begin.last `+0x530`, begin.node `+0x538`, finish.cur `+0x540` | 160-byte element; libstdc++ block 480 bytes / 3 elements |
| Android armv7 | `+0x380` | begin.cur `+0x388`, begin.last `+0x390`, begin.node `+0x394`, finish.cur `+0x398` | 128-byte element; libstdc++ block 512 bytes / 4 elements |
| iOS arm64 | `+0x488` | map begin `+0x488`, map end `+0x490`, start index `+0x4A0`, size `+0x4A8` | 160-byte element; libc++ block 4000 bytes / 25 elements |
| iOS armv7 | `+0x330` | map begin `+0x330`, map end `+0x334`, start index `+0x33C`, size `+0x340` | 128-byte element; libc++ block 4096 bytes / 32 elements |

The Android bodies use libstdc++'s four-pointer deque iterators. At a block
boundary they advance the node pointer and load the next block. The iOS bodies
reconstruct begin/end from libc++'s map, start index and size, then cross blocks
at 25 or 32 elements respectively. An empty iOS deque explicitly selects null
begin and end pointers; an empty Android deque naturally has equal begin.cur and
finish.cur. In both cases the append loop executes zero times.

These ABI differences explain the different native offsets and capacities; they
do not justify platform-specific source containers. `std::deque<VariableLabelScope>`
is the common source-level structure.

## Output `tTJSArrayNI::Items` deque

The Android arm64 getter inlines the String-Variant append. The other three call
the already identified `TJSArrayItems_pushBackString_guess` helper:

| Reference | Append implementation | Variant stride | Output block capacity |
| --- | ---: | ---: | ---: |
| Android arm64 | inline in `0x6CE77C` | 20 bytes | 500 bytes / 25 Variants |
| Android armv7 | `0x4EA126` | 12 bytes | 504 bytes / 42 Variants |
| iOS arm64 | `0x1001024C4` | 20 bytes | 204 Variants (libc++) |
| iOS armv7 | `0xFF7E0` | 12 bytes | 341 Variants (libc++) |

Every append writes Variant type tag 2 (String), copies the `cascadeKey` string
handle and increments its reference count when non-null. Empty `ttstr` values
are still appended as String Variants with a null string handle. The input
scope owns one reference and the returned Array owns another; clearing or
destroying either container therefore does not invalidate the other's string.

The Android output Items deques use the old libstdc++ iterator/map form and
allocate element-sized blocks. The iOS output Items deques use libc++ map,
start-index and size accounting. Their block capacities are
`floor(4096 / sizeof(tTJSVariant))`: 204 on 64-bit and 341 on 32-bit.

## Observable boundary behavior

- Empty input returns a newly allocated empty Array, not Void and not a shared
  singleton.
- Repeated calls return different Array dispatches even when the scopes have
  not changed.
- Entries remain in physical deque order. The getter does not alphabetize the
  values even though the property is named `variableKeys`.
- Duplicate cascade keys are emitted repeatedly.
- Empty cascade keys are emitted as empty String values.
- The getter snapshots values by String Variant CopyRef into the new Array.
  Mutating the returned Array cannot add, remove, or reorder Player scopes and
  cannot affect the next getter's distinct Array.
- There is no null-Items guard, allocation-failure recovery, per-entry
  conversion, or script callback in the loop.
- The generated NCB surface is read-only; a script property write is denied by
  the descriptor rather than routed into Player state.

## Port consequence

`Player::getVariableKeys()` now carries only the four-reference semantic claim.
The portable implementation deliberately uses
`createTJSArrayWithItems_guess()` plus direct `Items::emplace_back`, rather than
building a C++ vector or invoking TJS `add`. The unit regression now covers
fresh empty and populated Arrays, physical order, duplicates, and an empty key.
