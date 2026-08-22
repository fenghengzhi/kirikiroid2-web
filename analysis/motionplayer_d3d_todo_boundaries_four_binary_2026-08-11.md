# D3DEmotePlayer intentional TODO boundaries, four-binary audit (2026-08-11)

## Result

`assignState` and all five variable-enumeration/frame-query methods are
intentional TODO boundaries in every current reference binary. None of the iOS
or 32-bit variants contains a hidden implementation. The existing local throw
behavior is correct; only its old Android-only provenance comments and addresses
were stale.

## Direct function mapping

| Method | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| `assignState` | `0x530530` | `0x494AC4` | `0x100232F08` | `0x231B4E` |
| `countVariables` | `0x5307FC` | `0x494CA4` | `0x100233098` | `0x231CEA` |
| `getVariableLabelAt` | `0x530910` | `0x494CB8` | `0x1002330B8` | `0x231D00` |
| `countVariableFrameAt` | `0x530948` | `0x494CDC` | `0x1002330F0` | `0x231D26` |
| `getVariableFrameLabelAt` | `0x530968` | `0x494CF0` | `0x100233110` | `0x231D3C` |
| `getVariableFrameValueAt` | `0x530988` | `0x494D04` | `0x100233130` | `0x231D52` |

The former local A64 comments (`assignState @ 0x530150`, query functions around
`0x53041C..0x5305A8`) do not identify these functions in the current four-file
reference set and have been removed from compiled source.

All 24 functions have been renamed in the four IDBs using
`D3DEmotePlayer_<method>_TODO_guess` and the databases were saved afterward.

## Exact strings

The wide-string byte search found exactly one instance of each message in every
target. The addresses appear in method order from the table above:

| Target | `assignState` | `countVariables` | `getVariableLabelAt` | `countVariableFrameAt` | `getVariableFrameLabelAt` | `getVariableFrameValueAt` |
|---|---:|---:|---:|---:|---:|---:|
| Android A64 | `0x14BEFC0` | `0x14BF01C` | `0x14BF07E` | `0x14BF0E8` | `0x14BF156` | `0x14BF1CA` |
| Android A32 | `0xD76D34` | `0xD76D90` | `0xD76DF2` | `0xD76E5C` | `0xD76ECA` | `0xD76F3E` |
| iOS A64 | `0x101970348` | `0x1019703A4` | `0x101970406` | `0x101970470` | `0x1019704DE` | `0x101970552` |
| iOS A32 | `0x17626F4` | `0x1762750` | `0x17627B2` | `0x176281C` | `0x176288A` | `0x17628FE` |

The exact messages are:

```text
TODO: implement D3DEmotePlayer::assignState()
TODO: implement D3DEmotePlayer::countVariables()
TODO: implement D3DEmotePlayer::getVariableLabelAt()
TODO: implement D3DEmotePlayer::countVariableFrameAt()
TODO: implement D3DEmotePlayer::getVariableFrameLabelAt()
TODO: implement D3DEmotePlayer::getVariableFrameValueAt()
```

IDA's ordinary string rendering truncates several of these wide literals to
`"T"`. The UTF-16 byte matches plus the direct code xrefs establish the full
messages without relying on that truncated rendering.

## `assignState` boundary

Normalized four-target pseudocode:

```cpp
[[noreturn]] void assignState(tTJSVariant state) {
    iTJSDispatch2 *object;
    if (state.Type() == tvtObject) {
        object = state.object;
    } else {
        object = state.AsObject(); // conversion errors occur before the TODO
    }

    if (object != nullptr) {
        // Query the D3DEmotePlayer native-instance slot without asking the
        // adaptor to raise on a class mismatch. Ignore both status and result.
        (void)GetNativeInstance(object, false);
    }

    throw eTJSError(
        L"TODO: implement D3DEmotePlayer::assignState()");
}
```

Observable consequences:

- A non-Object value raises the ordinary TJS Object-conversion error before the
  TODO path.
- A null Object skips the native-instance query and still raises the TODO.
- A non-null object of the wrong native class is probed, but the mismatch is
  ignored and the TODO is still raised.
- A genuine D3DEmotePlayer object is also probed and then receives the same TODO;
  no fields are copied or mutated.

The local `AsObjectNoAddRef()` plus `GetNativeInstance(..., false)` sequence has
the same boundary and therefore remains unchanged.

## Five variable-query leaves

Each query compiles to one throwing basic block (apart from ABI prologue details):

```cpp
[[noreturn]] ReturnType query(unused indices...) {
    throw eTJSError(L"TODO: implement D3DEmotePlayer::<method>()");
}
```

No query reads `this`, an index, or a frame index. There is no bounds behavior,
container walk, empty-result fallback, or ABI-specific implementation to port.
The A64 `getVariableLabelAt` leaf preserves the hidden structure-return register
in a callee-saved register before throwing; that is compiler-generated ABI noise,
not construction of a result string.

## Local and validation status

- Runtime behavior was retained exactly; no fake variable enumeration was added.
- Stale per-function Android addresses and the Android-only wording were removed.
- Existing unit coverage already asserts every exact TODO message and the
  wrong-native-object `assignState` path.
- The immediately preceding outer-force phase completed successful Web Debug and
  Wasmtime Headless Debug links plus a full motionplayer test translation-unit
  syntax check. This comments/documentation-only correction introduces no new
  executable path.
