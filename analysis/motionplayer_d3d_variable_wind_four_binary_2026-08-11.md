# D3DEmotePlayer variable / wind four-binary audit (2026-08-11)

This note records a fresh comparison of the D3D `setVariable`, `getVariable`,
`startWind`, and `stopWind` paths in all four current reference binaries. It
supersedes the old source comments that cited only `libkrkr2.so`.

## Native D3D member mapping

| Method | Android A64 | Android ARMv7 | iOS A64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `setVariable` | `0x5309A8` | `0x494D18` | `0x100233150` | `0x231D68` |
| `getVariable` | `0x5309B4` | `0x494D20` | `0x10023315C` | `0x231D70` |
| `startWind` | `0x530A60` | `0x494D94` | `0x1002331D4` | `0x231E38` |
| `stopWind` | `0x530A6C` | `0x494D9C` | `0x1002331E0` | `0x231E40` |
| shared Engine wind core | `0x66DD8C` | `0x559900` | `0x1001AC718` | `0x1ABF24` |

The registration-table references are `0x52F410/0x52F480/0x52F4F0/0x52F548`,
`0x494326/0x494338/0x49434A/0x49435C`,
`0x100232690/0x1002326B0/0x1002326D0/0x1002326F0`, and
`0x23131A/0x231338/0x231356/0x231374`, respectively.

Every native member follows the same shell chain:

```text
D3DEmotePlayer
  -> primary EmoteObject slot
  -> EmoteObject::Engine
  -> variable helper or Engine wind core
```

`setVariable` forwards `ttstr, double value, double transition, double ease`
without a Player-side duplicate write. `getVariable` passes the Engine to the
Engine facade query: a scope-deque hit reads the inner Player HM1/HM2 bound
value, while a miss first tries the Player HM4 join snapshot and then falls
back to HM1/HM2. This preserves the live HM7-versus-Player-map split: absent a
matching HM4 snapshot, a fresh HM7 write is not visible until the Engine
progress bind loop propagates it. The router's four-target details are in
`motionplayer_get_variable_routing_four_binary_2026-08-14.md`.

2026-08-15 的fresh router/caller复核进一步确认：D3D body不执行Primary
`Motion.EmotePlayer` raw callback的ease预变换，原ease只在Engine router中变换一次；
Primary API则先在raw callback中变换、再由Engine变换。完整指令与极值边界见
`motionplayer_set_variable_router_double_ease_integer_conversion_four_binary_2026-08-15.md`。

`startWind` is a five-`float` native member on all targets. The NCB invocation
layer first converts all five TJS values through `AsReal` and then narrows them
to `float`. `stopWind` calls the same Engine wind core with five positive-zero
floats; it is not a separate direct-delete implementation.

## NCB `FuncCall` mapping and boundary order

| Method | Android A64 | Android ARMv7 | iOS A64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `setVariable` | `0x545908` | `0x4A6A10` | `0x1002492F0` | `0x24A4FC` |
| `getVariable` | `0x545DFC` | `0x4A6DC8` | `0x1002497B4` | `0x24AA78` |
| `startWind` | `0x546134` | `0x4A7100` | `0x100249BC8` | `0x24AF6C` |
| `stopWind` | `0x543224` | `0x4A46BC` | `0x1002465BC` | `0x246E94` |

All four implementations use this outer order:

1. non-null `membername` returns `TJS_E_MEMBERNOTFOUND` (`-1001`);
2. null `objthis` returns `TJS_E_NATIVECLASSCRASH` (`-1008`);
3. release the member-name hint when one is supplied;
4. enforce the generated minimum arity;
5. resolve the D3D native instance without raising a class-mismatch exception;
6. a missing/wrong native receiver returns `TJS_E_NATIVECLASSCRASH`;
7. convert arguments, invoke the member pointer, and return `TJS_S_OK`.

The minimum arities are exactly 4, 1, 5, and 0. In particular,
`setVariable(label, value)` is not accepted by the D3D class and there are no
generated defaults for its final two values. `stopWind` accepts extra arguments;
its signed `numparams < 0` check is the otherwise unreachable bad-count edge.

The per-method conversion/invocation helpers are:

| Method | Android A64 | Android ARMv7 | iOS A64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `setVariable` | `0x545A24` | `0x4A6AD4` | `0x1002493D0` | `0x24A594` |
| `getVariable` | `0x545F18` | `0x4A6E88` | `0x100249894` | `0x24AB0C` |
| `startWind` | `0x546250` | `0x4A71C4` | `0x100249CA8` | `0x24B004` |

## Shared wind data flow

Common pseudocode, before the platform-width stop predicate:

```cpp
amplitude = abs(amplitude);
if (originalAmplitude < 0)
    swap(minAngle, maxAngle);

if (shouldStop(...)) {
    delete engine->windEmitter;
    engine->windEmitter = nullptr;
    return; // cached wind values remain unchanged
}

if (!emitter || cachedMin != minAngle || cachedMax != maxAngle) {
    delete emitter; // owner field deliberately remains unchanged here
    emitter = new EmoteWindEmitter; // exactly 0x61C bytes in every reference
    emitter->init(minAngle / meshDivisionRatio,
                  maxAngle / meshDivisionRatio);
    engine->windEmitter = emitter;
}

cache minAngle, maxAngle, abs(amplitude), freqX, freqY;
emitter->yHi = freqX;
emitter->yLo = freqY;
emitter->gate = true;
emitter->velocity = (endPos < startPos ? -1.0f : 1.0f)
                  * abs(amplitude) / meshDivisionRatio;
emitter->emitAccumulator = 0.0f;
```

The stop predicate is a real two-versus-two target difference:

```cpp
// Android A64 and iOS A64
amplitude == 0.0f || minAngle == maxAngle ||
    (freqX == 0.0f && freqY == 0.0f)

// Android ARMv7 and iOS ARMv7
amplitude == 0.0f || freqX == 0.0f
```

The ARMv7 bodies contain no compare against `minAngle == maxAngle` and no read
of `freqY` in the stop decision. Conversely, both A64 bodies explicitly contain
both conditions. The local reconstruction therefore uses pointer width as the
compile-time discriminator, matching both references in each width family.

2026-08-13 的 owner 专题进一步确认，上述 replacement 中的 `delete emitter` 到
`engine->windEmitter = emitter` 之间没有 member-slot clear。若 `operator new` 抛异常，
字段保留已经释放的旧地址。这一共同边界排除了 `unique_ptr`；完整构造/析构/失败分析见
`motionplayer_wind_raw_owner_replacement_four_binary_2026-08-13.md`。

## IDB improvements

All four databases now name the native members, their NCB `FuncCall` methods,
their conversion/invocation helpers, and the shared Engine core with `_guess`
suffixes. The native member and Engine core prototypes were also applied so
the A64 tail-forwarding of five live float registers and the all-zero stop call
decompile correctly. All four databases were saved after the changes.
