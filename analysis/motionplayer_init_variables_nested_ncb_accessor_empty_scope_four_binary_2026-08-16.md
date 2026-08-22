# `Player::initVariables` nested NCB ownership and empty-scope gate: four-reference recovery (2026-08-16)

## Scope

This vertical freshly re-decompiled and instruction-checked the current Android
arm64, Android armv7, iOS arm64, and iOS armv7 reference implementations of
`Player::initVariables`. It replaces the remaining source-shaped assumptions
in the older variable-deque note with four-reference evidence for:

- the three nested `ncbPropAccessor` source identities;
- full-expression and function/iteration accessor lifetimes;
- dispatch flags, hints, receivers, result conversion, and ignored ordinary
  `HRESULT` values;
- append-before-property-read exception behavior;
- the independent double read of `label`;
- the signed `Count` snapshot and ascending numeric traversal; and
- the real `scope` boundary: unconditional Variant-to-`ttstr` conversion,
  followed by an empty-string test rather than a Variant-type test.

The four stripped implementations retain the recovery name
`Player_initVariables_guess`; `_guess` remains appropriate because none of the
shipped binaries supplies an authoritative C++ symbol.

## Function map

| target | function | size |
|---|---:|---:|
| Android arm64 | `0x6CAB30` | `0x6C8` |
| Android armv7 | `0x592944` | `0x23E` |
| iOS arm64 | `0x10011D540` | `0x344` |
| iOS armv7 | `0x11BF04` | `0x2D4` |

The much larger Android-arm64 body is primarily libstdc++ deque block/map
growth inlining. The two 64-bit native deque elements are 160 bytes and the two
32-bit elements are 128 bytes, as established by the earlier container-layout
vertical. Those ABI differences do not change the shared source algorithm.

## Recovered source shape

The common source-level behavior is:

```cpp
tracks.clear();

Variant variables =
    ncbPropAccessor(Variant(selectedMotionContent))
        .GetValue(L"variable", Tag<Variant>(), 0);
if(variables.Type() == tvtVoid)
    return;

ncbPropAccessor listObject{Variant(variables)};
const signed_int count = listObject.GetArrayCount();
for(signed_int index = 0; index < count; ++index) {
    ncbPropAccessor itemObject{
        listObject.GetValue(index, Tag<Variant>(), 0)};

    Entry &entry = tracks.emplace_back();
    entry.cascadeKey =
        itemObject.GetValue(L"label", Tag<String>(), 0);
    entry.value = 0.0;
    entry.slot[0].typeZeroFlag = true;
    entry.slot[1].typeZeroFlag = true;
    entry.activeSlotCursor = 0;
    entry.frameSource =
        itemObject.GetValue(L"label", Tag<Variant>(), 0);

    String scope(itemObject.GetValue(L"scope", Tag<Variant>(), 0));
    if(!scope.IsEmpty())
        entry.cascadeKey = scope + L"::" + entry.cascadeKey;
}
```

This is structural pseudocode, not a claim that the original spelling or local
names are known.

## Exact owner tree and teardown order

Three accessor roles are observable; combining them into a generic raw
property helper is not lifetime-equivalent.

1. The selected motion Variant is copied and forced into a temporary accessor
   used only for the typed `variable` read. The accessor releases its dispatch,
   then the conversion Variant is destroyed, before the result's Void gate.
   Re-entrant replacement or clearing of the Player's stored motion owner
   cannot invalidate that in-flight read.
2. A non-Void `variable` result is copied and forced into one function-wide
   list accessor. Its conversion Variant is destroyed before `Count`; the
   accessor remains alive through the entire numeric loop and is released after
   traversal. The original result Variant is destroyed last.
3. Each numeric result is copied/forced into one per-iteration item accessor.
   The numeric result Variant is destroyed before deque append. The item
   accessor remains alive across append, both `label` reads, `scope`, and scope
   conversion/prefixing, then releases at the iteration tail.

The normal local teardown nesting is therefore:

```text
scope ttstr
item accessor
... next iteration ...
list accessor
variable result Variant
```

The motion accessor and its conversion Variant have already disappeared before
the Void test and before list traversal begins.

## Dispatch contract and ignored ordinary failures

Every named read uses flags `0`, a null member hint, and the same retained
dispatch as receiver and `objthis`. Numeric reads also use flags `0` and the
retained list dispatch as `objthis`. `Count` is issued once with flags `0`, null
hint, and the list receiver.

The generated typed NCB accessors preserve a usable result even when a custom
dispatch writes the result/count and returns an ordinary failure code. All four
implementations continue from those written values. Conversion/force failures
still follow the NCB exception path; the statement here is specifically about
ordinary post-write `HRESULT` values.

`Count` is held in a signed integer. The loop is entered only for a positive
snapshot and advances `0, 1, ... count-1`; a zero or negative count performs no
numeric read. There is no second `Count` query and no mutation-sensitive length
refresh.

## Append and partial-commit boundary

The indexed item accessor exists before `emplace_back`, but the deque element
is appended before the first named property read. Consequently:

- numeric lookup or item-accessor construction failure leaves no new element;
- a failure during the first `label` read leaves a newly appended zero/default
  element;
- later failures retain exactly the prefix of field writes already completed;
- the second `label` read is independent and may observe re-entrant script
  changes made by the first; and
- neither named-property failure nor conversion failure rolls the appended
  element back.

On a normal iteration the native write order is cascade key, zero value, both
type-zero sentinels, zero cursor, raw frame source, then optional scoped cascade
key replacement.

## Corrected `scope` boundary

The older migration note described the last branch as `scope is non-Void`.
Fresh instructions on all four targets contradict that description. The
implementation always converts the returned Variant to `ttstr`, destroys the
scope-result Variant, and tests the resulting string's internal empty state.

This distinction is observable:

| returned scope | converted string | prefix? |
|---|---|---|
| Void | empty | no |
| empty String | empty | no |
| non-empty String | same string | yes |
| another convertible value | conversion result | iff non-empty |
| conversion error | no completed string | exception; partial deque entry remains |

In particular, an empty non-Void String must yield `label`, not `"::" + label`.
The portable source now uses `if(!scope.IsEmpty())` after an unconditional
Variant-to-`ttstr` construction.

## Instruction map

| event | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| clear deque | `0x6CAB68` | `0x592960` | `0x10011D56C` | `0x11BF28` |
| copy motion source | `0x6CAB74` | `0x59296A` | `0x10011D578` | `0x11BF36` |
| build/force motion accessor | `0x6CAB98` | `0x592974` | `0x10011D588` | `0x11BF5A` |
| typed `variable` read | `0x6CABD8` | `0x59298C` | `0x10011D5B4` | `0x11BF9E` |
| release motion accessor | `0x6CAC00` | `0x5929A4` | `0x10011D5D8` | `0x11BFB2` |
| destroy motion conversion | `0x6CAC08` | `0x5929A8` | `0x10011D5E0` | `0x11BFB6` |
| Void-only result gate | `0x6CAC10` | `0x5929B0` | `0x10011D5E8` | `0x11BFBE` |
| copy list source | `0x6CAC1C` | `0x5929B8` | `0x10011D5F4` | `0x11BFCA` |
| build/force list accessor | `0x6CAC34` | `0x5929C2` | `0x10011D604` | `0x11BFD0` |
| destroy list conversion | `0x6CAC70` | `0x5929CE` | `0x10011D618` | `0x11BFE0` |
| one `Count` snapshot | `0x6CAC7C` | `0x5929D8` | `0x10011D624` | `0x11BFEE` |
| indexed typed Variant read | `0x6CACBC` | `0x592A0E` | `0x10011D664` | `0x11C00A` |
| build/force item accessor | `0x6CACD8` | `0x592A14` | `0x10011D668` | `0x11C010` |
| destroy indexed result | `0x6CAD18` | `0x592A20` | `0x10011D67C` | `0x11C020` |
| append element | `0x6CAD20` | `0x592A26` | `0x10011D684` | `0x11C02A` |
| first `label` as string | `0x6CADD0` | `0x592A4C` | `0x10011D6D8` | `0x11C064` |
| seed value/flags/cursor | `0x6CAE24` | `0x592A86` | `0x10011D720` | `0x11C0B0` |
| second `label` as Variant | `0x6CAE58` | `0x592AA4` | `0x10011D748` | `0x11C0D6` |
| assign frame source | `0x6CAE78` | `0x592AAE` | `0x10011D758` | `0x11C0E4` |
| typed `scope` Variant read | `0x6CAEAC` | `0x592AC2` | `0x10011D780` | `0x11C108` |
| convert scope to `ttstr` | `0x6CAECC` | `0x592ACA` | `0x10011D78C` | `0x11C114` |
| converted-string empty gate | `0x6CAEDC` | `0x592AD8` | `0x10011D79C` | `0x11C120` |
| scoped key construction | `0x6CAEE0` | `0x592AE4` | `0x10011D7B0` | `0x11C134` |
| destroy scope string | `0x6CAFF0` | `0x592B2A` | `0x10011D804` | `0x11C182` |
| release item accessor | `0x6CB00C` | `0x592B3C` | `0x10011D828` | `0x11C196` |
| release list accessor | `0x6CB034` | `0x592B60` | `0x10011D858` | `0x11C1B4` |
| destroy variable result last | `0x6CB03C` | `0x592B64` | `0x10011D860` | `0x11C1B8` |

## Portable implementation and differential probe

`Player::initVariables` now spells the three accessor roles directly and has
no `motionPropGet*` wrapper in this function. A source audit records exactly
three `ncbPropAccessor` occurrences, five typed `GetValue` calls, one
`GetArrayCount`, no Variant-type scope gate, and one `scope.IsEmpty()` gate.

The differential probe uses independent motion, list, and two item dispatches.
It verifies that:

- re-entrant owner clearing cannot kill the source retained by the active
  accessor;
- the motion accessor is gone before `Count`, while the list accessor survives
  the entire loop;
- each indexed result can drop both its list-held and external owners after the
  item accessor has acquired it;
- all ordinary getters may return failure after writing a usable value;
- every flag/hint/receiver tuple matches the native calls;
- deque sizes are already one and two during the respective first named reads;
- item zero is destroyed before index one is requested, both items die while
  the list remains alive, and the list dies after traversal;
- the two independent label reads publish the expected frame sources; and
- empty scope yields `leaf0`, while non-empty `root` yields `root::leaf1`.

The added `Player` accessors are test-only `_guess` helpers and do not add a
script-visible NCB member.

## IDB write-back and verification

Each recovery database received one function comment, 27 address comments, and
the bookmark `V148 initVariables nested ncb owners + empty scope gate`.
`Player_initVariables_guess` was force-recompiled and decompiled again in every
database; scoped comment searches returned exactly 28 `V148:` hits in each.
All four databases were saved in place.

Verification completed after the source change:

- normal and `KRKR2_WASMTIME_HEADLESS` test-translation-unit syntax checks
  passed; only the pre-existing `_tss` warning remained;
- the full Web Debug and Wasmtime Headless Debug builds completed;
- Web `index.wasm` is 85,638,834 bytes and the headless `index.wasm` is
  84,985,975 bytes;
- both artifacts were accepted by `WebAssembly.Module` (Web: 539 imports and
  69 exports; headless: 538 imports and 69 exports);
- `llvm-objdump -h` parsed both section tables; and
- CTest currently reports `No tests were found` for both build trees, so the
  probe is compile-checked by the project build but no runtime CTest execution
  is claimed.

