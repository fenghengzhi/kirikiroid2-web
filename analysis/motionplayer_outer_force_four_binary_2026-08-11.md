# MotionPlayer outer-force four-binary reconstruction (2026-08-11)

## Scope and authority

This note records a fresh comparison of the outer-force surface in all four
current reference binaries. It supersedes source comments based only on the old
`libkrkr2.so` decompilation, including the stale Android-only address previously
attached to `D3DEmotePlayer::getOuterForce`.

The authoritative targets are:

- Android arm64-v8a
- Android armeabi-v7a (the reference filename is spelled `armabi-v7a`)
- iOS arm64
- iOS armv7

## Function mapping

| Layer | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| `Motion.EmotePlayer.setOuterForce` wrapper | `0x66FE58` | `0x55A828` | `0x1001ADB98` | `0x1AD218` |
| shared Engine label router | `0x670138` | `0x55A928` | `0x1001ADC9C` | `0x1AD37C` |
| `D3DEmotePlayer::setOuterForce` | `0x530E6C` | `0x495048` | `0x1002334BC` | `0x2321F8` |
| `D3DEmotePlayer::getOuterForce` TODO | `0x530F08` | `0x4950D0` | `0x100233524` | `0x2322CC` |
| D3D `setOuterForce` NCB `FuncCall` adapter | `0x547FB8` | `0x4A8BA8` | `0x10024BCE0` | `0x24D724` |

The IDBs now use these `_guess` names for the five rows above and have been
saved after applying common prototypes:

- `EmotePlayer_setOuterForceCompat_guess`
- `EmoteEngine_setOuterForceTarget_guess`
- `D3DEmotePlayer_setOuterForce_guess`
- `D3DEmotePlayer_getOuterForce_TODO_guess`
- `D3DEmotePlayer_setOuterForce_NCBFuncCall_guess`

## Common source-level data flow

All four targets agree on this three-layer structure:

```text
Motion.EmotePlayer script wrapper
  required: label, x, y
  optional: duration=0, ease=0
  power = ease > 0 ? ease + 1
        : ease < 0 ? 1 / (1 - ease)
        :            1
                    \
                     +--> shared Engine label router --> fixed 20-byte queue
                    /
D3DEmotePlayer native method
  required: label, x, y, duration, power
  power passes through unchanged
```

Normalized shared router pseudocode:

```cpp
void setOuterForceTarget(engine, label, x, y, duration, power) {
    float values[2] = {float(x), float(y)};
    if (label == L"bust") {
        controller = engine.outerForce[0];
    } else if (label == L"hair") {
        controller = engine.outerForce[1];
    } else if (label == L"parts") {
        controller = engine.outerForce[2];
    } else {
        return;
    }

    EmoteVarController_setTarget(
        controller, values, float(duration), float(power), engine.append);
}
```

The comparisons are exact and case-sensitive. For example, `Bust`, `HAIR`, an
empty label, and any unknown label are silent no-ops. There is no fallback to
`bust` and no ASCII-lowercasing pass.

The shared router does **not** write the Engine dirty byte. Both public entry
paths therefore only affect the selected controller and its queue/current
values. Queue replacement versus append is controlled by the Engine append byte
already used by the base coord/scale/rotate/color setters.

2026-08-15 的 fresh 四端复核进一步闭合了窄化边界：x/y 在标签比较前就转为
float；duration/power 只在标签命中后转为 float。Primary callback 的 ease 映射在
double 域完成，不会在进入 router 前提前窄化。callback 还使用 NCBind 已解出的 native
payload，而不是自行从 `objthis` 查实例。详见
`analysis/motionplayer_primary_raw_controller_setters_four_binary_2026-08-15.md`。

## Controller layout and consumers

| Target | `bust` controller | `hair` controller | `parts` controller | append byte |
|---|---:|---:|---:|---:|
| Android A64 | `+1104` | `+1112` | `+1120` | `+1161` |
| Android A32 | `+552` | `+556` | `+560` | `+593` |
| iOS A64 | `+736` | `+744` | `+752` | `+793` |
| iOS A32 | `+368` | `+372` | `+376` | `+409` |

Every controller has two float channels and uses the same fixed 20-byte
keyframe container reconstructed for the base variable controllers. With two
channels, words 0 and 1 contain `x/y`, word 3 contains duration, and word 4
contains power. The four-channel color overwrite quirk does not occur here.

The external-label naming is important because the physics consumers are less
obvious than the old local names suggested:

- `bust` / slot 0 feeds the simple hair-parts spring pass.
- `hair` / slot 1 feeds chain pass 1 with `hairScale`.
- `parts` / slot 2 feeds chain pass 2 with `partsScale`.

The same slot order is used by controller construction/destruction, reset,
serialization (`bust`, `hair`, `parts`), restoration, per-frame controller
stepping, and spring consumption. The local fields are therefore named after
the exact externally observable labels, not a guessed physical role.

## Script wrapper boundary

All four `Motion.EmotePlayer` wrappers perform the required-count check before
reading any parameter:

```text
numparams < 3 -> TJS_E_BADPARAMCOUNT (-1004)
```

Parameters 3 and 4 are optional. Missing duration becomes `0`; missing ease
becomes power `1`. Extra parameters are ignored. The ease-to-power transform is
identical to the coord/scale/rotate/color script setters.

## D3D NCB boundary

All four D3D `FuncCall` adapters agree on this observable order:

1. non-null member name -> `TJS_E_MEMBERNOTFOUND` (`-1001`);
2. null `objthis` -> `TJS_E_NATIVECLASSCRASH` (`-1008`);
3. release the optional hint/context object when present;
4. fewer than five parameters -> `TJS_E_BADPARAMCOUNT` (`-1004`);
5. resolve the native `D3DEmotePlayer` instance;
6. missing/wrong native receiver -> `TJS_E_NATIVECLASSCRASH` (`-1008`);
7. invoke the five-parameter native method; extra parameters are ignored.

Unlike the Motion wrapper, the fifth D3D argument is already `power`; it is not
passed through the script ease transform. The old local defaults and the
two-double convenience overload therefore had no reference counterpart.

## Intentional `getOuterForce` exception

`D3DEmotePlayer::getOuterForce` is deliberately unimplemented in every current
target and unconditionally throws this exact message:

```text
TODO: implement D3DEmotePlayer::getOuterForce()
```

The UTF-32/UTF-16 byte search found one copy per target at:

| Target | string address |
|---|---:|
| Android A64 | `0x14BF23E` |
| Android A32 | `0xD76FB2` |
| iOS A64 | `0x1019705C6` |
| iOS A32 | `0x1762972` |

This is a shared intentional boundary, not an Android-only TODO.

## Local structural corrections

- Added one Engine-owned `setOuterForceTarget_guess` router.
- Routed both public facades directly to the Engine controller layer.
- Preserved Motion ease conversion and D3D raw-power behavior separately.
- Renamed the three controller fields by their exact serialized labels.
- Removed `Player::OuterForceState` and `Player::setOuterForce`; the former was
  write-only shadow state with no reference-binary consumer.
- Removed the invented two-double D3D overload and its implicit `bust` fallback.
- Corrected D3D raw-callback arity and receiver/error ordering.
- Preserved the four-target `getOuterForce` TODO exception verbatim.

## Validation

- `cmake --build --preset "Web Debug Build"`: passed and linked `index.html`.
- `cmake --build --preset "Wasmtime Headless Debug Build"`: passed and linked
  the headless guest output.
- Immediate rerun of both build presets: `ninja: no work to do.`
- Full `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten
  `-fsyntax-only`: passed. The only diagnostic was the existing deprecated
  whitespace before the `_tss` literal-operator name.
- `git diff --check`: passed; only the repository's existing CRLF conversion
  notices were emitted.
- Native Catch2 execution remains unavailable because the pre-existing Windows
  vcpkg cocos2d-x dependency fails in `unzip.cpp` with MSVC `C2491`; this phase
  did not alter that external dependency.
