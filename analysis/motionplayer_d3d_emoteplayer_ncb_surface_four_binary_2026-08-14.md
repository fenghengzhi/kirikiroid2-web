# Motion.D3DEmotePlayer NCB surface and descriptor families (four-reference audit, 2026-08-14)

## Conclusion

All four current reference binaries register the same
`Motion.D3DEmotePlayer` surface: **four constants followed by exactly 54
members in the same interleaved order**. The former port had the correct member
name set but grouped most properties before most methods, so its constructor
side effects, allocation/failure order, and duplicate-name publication order
did not match the native registrar.

Fresh decompilation also identifies the descriptor family of each member:

- `load` is the **only native-instance raw callback** in this 54-member table;
- all other functions, including `setCoord`, `setScale`, `setRot`, `setColor`,
  `setVariable`, `startWind`, `stopWind`, `setOuterForce`, and `contains`, use
  ordinary generated typed ncbind descriptors;
- `setTimelineBlendRatio` is a typed alias whose C++ target is the five-argument
  `setTimeline` method;
- script `pass()` is the typed zero-argument timeline flush; the former local
  C++ `pass(double)` helper was not registered and has been removed;
- `module`, `animating`, and `modified` are read-only properties; the remaining
  property entries in the table are read/write.

The local registration table has been restored to native order and the nine
port-invented raw compatibility shims have been removed. Absolute addresses
below are evidence coordinates only.

## Registrar identities

| Reference target | Registrar | Recovered size |
|---|---:|---:|
| Android arm64 | `0x52E8E4` | `0x1554` |
| Android armv7 | `0x494078` | `0x4A8` |
| iOS arm64 | `0x100232278` | `0x7A8` |
| iOS armv7 | `0x230F46` | `0x71E` |

Every expected name was searched as UTF-16LE bytes in every database. Full
registrar decompilation then established call order and descriptor-helper
family. This matters on Android arm64 because instruction scheduling can
materialize a later string address before an earlier descriptor call; sorting
raw xref instruction addresses alone is not a reliable source-order oracle.
On 32-bit targets one source descriptor can likewise yield several nearby
materialization xrefs, but the decompiled call order remains unambiguous.

The old Android-arm64 source comment naming `sub_52E504` as this table is stale:
`0x52E504` now lies inside `D3DEmoteModule_ncb_registerMembers_guess`, whose
function starts at `0x52E388`. The current D3DEmotePlayer registrar begins at
`0x52E8E4`.

## Constants and exact member order

The four constants precede all member descriptors:

| Prefix position | Registered name | Value |
|---:|---|---:|
| C1 | `MaskModeStencil` | 0 |
| C2 | `MaskModeAlpha` | 1 |
| C3 | `TimelinePlayFlagParallel` | 1 |
| C4 | `TimelinePlayFlagDifference` | 2 |

The following 54 descriptors are then created and published in exactly this
order on all targets:

| Member | Registered name | Descriptor kind |
|---:|---|---|
| 1 | `module` | read-only property |
| 2 | `clear` | typed `void()` method |
| 3 | `load` | native-instance raw callback |
| 4 | `clone` | typed method returning a native D3DEmotePlayer |
| 5 | `show` | typed `void()` shell-byte leaf; no EmoteObject/Player access |
| 6 | `hide` | typed `void()` shell-byte leaf; no EmoteObject/Player access |
| 7 | `visible` | shell-local Boolean property; observable but not a listener draw gate |
| 8 | `smoothing` | read/write property |
| 9 | `meshDivisionRatio` | read/write property |
| 10 | `queing` | read/write property; historical spelling |
| 11 | `hairScale` | read/write property |
| 12 | `partsScale` | read/write property |
| 13 | `bustScale` | read/write property |
| 14 | `assignState` | typed method |
| 15 | `setCoord` | typed method |
| 16 | `setScale` | typed method |
| 17 | `getScale` | typed method |
| 18 | `setRot` | typed method |
| 19 | `getRot` | typed method |
| 20 | `setColor` | typed method |
| 21 | `getColor` | typed method |
| 22 | `countVariables` | typed method |
| 23 | `getVariableLabelAt` | typed method |
| 24 | `countVariableFrameAt` | typed method |
| 25 | `getVariableFrameLabelAt` | typed method |
| 26 | `getVariableFrameValueAt` | typed method |
| 27 | `setVariable` | typed method |
| 28 | `getVariable` | typed method |
| 29 | `startWind` | typed method |
| 30 | `stopWind` | typed `void()` method |
| 31 | `countMainTimelines` | typed method |
| 32 | `getMainTimelineLabelAt` | typed method |
| 33 | `countDiffTimelines` | typed method |
| 34 | `getDiffTimelineLabelAt` | typed method |
| 35 | `countPlayingTimelines` | typed method |
| 36 | `getPlayingTimelineLabelAt` | typed method |
| 37 | `getPlayingTimelineFlagsAt` | typed method |
| 38 | `isLoopTimeline` | typed method |
| 39 | `getTimelineTotalFrameCount` | typed method |
| 40 | `playTimeline` | typed method |
| 41 | `isTimelinePlaying` | typed method |
| 42 | `stopTimeline` | typed method |
| 43 | `setTimelineBlendRatio` | typed alias to `setTimeline` |
| 44 | `getTimelineBlendRatio` | typed method |
| 45 | `fadeInTimeline` | typed method |
| 46 | `fadeOutTimeline` | typed method |
| 47 | `animating` | read-only property |
| 48 | `skip` | typed `void()` method |
| 49 | `pass` | typed zero-argument timeline flush |
| 50 | `progress` | typed one-double frame-progress method |
| 51 | `modified` | read-only property |
| 52 | `setOuterForce` | typed method |
| 53 | `getOuterForce` | typed method |
| 54 | `contains` | typed `(label,x,y)` method |

The native table does not contain the old port-only names `useD3D`,
`drawvisible`, `drawOpacity`, `opengl`, `playCallback`, `initPhysics`, `play`,
`setMirror`, `draw`, or `setDrawAffineTranslateMatrix`. None is published by
this NCB registrar. A fresh four-binary UTF-16 string/xref audit found zero
`play`, `draw`, or `setMirror` references from the D3DEmotePlayer registrar;
the three same-named local facade methods also had zero C++ callers, so they
were removed instead of being retained as an invented, unregistered surface.
The later four-reference pass/progress source-graph audit found the former
`pass(double)` helper likewise had no production caller; its three test calls
were changed to the real `progress(double)` entry and the helper was removed.
A subsequent complete-registrar plus UTF-16LE xref audit removed the copied,
unregistered Player-property facade block (`completionType`, `chara`, `motion`,
`motionKey`, `maskMode`, `outline`, `priorDraw`, and the three time names).
Those names remain owned by Motion.Player, Motion.EmotePlayer, or the separate
D3DEmoteModule; none belongs to this 54-member class.
The former public `getPlayer()` accessor also has zero binary literal/function
evidence and zero production caller. Since an unused inline helper cannot be
disproved from stripped images, it is retained only as the explicitly
provisional `playerForDifferentialTest_guess()` test hook rather than presented
as a recovered native API.

member 5–7 的 2026-08-16 fresh registrar/target/listener 复核进一步确认：show、hide
与 visible property 只读写 shell 的 `+0x30/+0x20` byte，不进入 primary EmoteObject、
Engine 或 Player。listener `IsVisible()` 只在 owner scale 改变时更新 scale controller，
最后固定返回 true；相邻 Draw 也不读该 byte。因此 visible 在当前四参考中是脚本可观察、
跨 clear 保留、但不控制渲染的兼容状态。详见
`analysis/motionplayer_d3d_visibility_shell_only_four_binary_2026-08-16.md`。

## The sole raw callback: load

| Target | Raw callback body |
|---|---:|
| Android arm64 | `0x5301B4` |
| Android armv7 | `0x494920` |
| iOS arm64 | `0x100232CB0` |
| iOS armv7 | `0x231890` |

The recovered ABI is semantically:

```text
load(result, argc, argv, native D3DEmotePlayer self) -> tjs_error
```

It accepts a variadic list of module paths. All four bodies first tear down the
secondary and primary `EmoteObject` owners and null both slots. Only then do
they convert the argument vector, allocate an ABI-sized `EmoteObject`, run its
constructor, and publish the new primary pointer. Conversion, vector growth,
allocation, or construction failure therefore leaves both shell slots null.

This control/data flow explains why `load` needs the native-instance raw
callback family instead of an ordinary fixed-arity typed method. The four IDBs
now name it `D3DEmotePlayer_loadRawCallback_guess`.

The separate generated raw-callback `FuncCall` wrapper has the following
boundary, already covered by the existing load regression:

1. nonzero dispatch flags return `TJS_E_NOTIMPL`;
2. a null `objthis` returns `TJS_E_NATIVECLASSCRASH` without clearing result;
3. with `objthis`, result is cleared;
4. sticky/native-instance resolution chooses the native self passed to the raw
   callback.

## Typed FuncCall boundary

The zero-argument `stopWind` typed wrapper is a compact representative of the
ordinary method family:

| Target | Generated `stopWind` FuncCall wrapper |
|---|---:|
| Android arm64 | `0x543224` |
| Android armv7 | `0x4A46BC` |
| iOS arm64 | `0x1002465BC` |
| iOS armv7 | `0x246E94` |

All four implement the same gate order:

1. reject nonzero dispatch flags;
2. reject null receiver with `TJS_E_NATIVECLASSCRASH`;
3. clear a non-null result Variant;
4. reject `argc < required_arity` with `TJS_E_BADPARAMCOUNT`;
5. resolve the receiver's native instance;
6. invoke the stored direct/member-function target.

Arity is a lower bound: extra script arguments are not rejected by this
generated adapter. For `stopWind`, the signed check is literally `argc < 0`.
Other signatures use the same template family with their own required count and
typed argument conversion.

`contains` is a typed three-argument target on all four references. Its
recovered semantic name contains `RawLabel` because it recursively resolves a
raw motion label; “raw” does not mean an NCB raw-callback descriptor. The former
handwritten `containsCompat` changed receiver failures to
`TJS_E_INVALIDOBJECT`, required a non-null result, and returned
`TJS_E_INVALIDPARAM` for short calls. Those were port-created boundaries and
have been removed in favor of the generated typed adapter.

## Tail-order and pass/progress separation

The last eight members are not grouped by kind:

```text
animating (RO property)
skip       (typed method)
pass       (typed void())
progress   (typed method taking one double)
modified   (RO property)
setOuterForce
getOuterForce
contains
```

This ordering is visible identically in the four registrars. Script `pass()`
stores the no-argument `passTimelines_guess` target. `progress(double)` is the
separate D3D shell wrapper that consumes frame units and gates zero before the
shared Engine core. A later source-structure audit removed the unregistered
test-only `pass(double)` forwarding convenience, so the portable class now also
keeps those two source signatures disjoint.

## Port changes and regression coverage

The portable table and source were changed as follows:

- restored the exact four-constant + 54-member interleaved order;
- numbered all 54 entries in `main.cpp` so future order drift is mechanically
  visible;
- retained `loadCompat` as the sole raw callback;
- converted the nine fixed-signature port shims back to typed `NCB_METHOD`
  descriptors;
- deleted their declarations and implementations;
- deleted the unregistered, caller-free `play`, `draw`, and `setMirror` C++
  facades after confirming their absence from every D3DEmotePlayer registrar;
- replaced direct shim tests with a regression that obtains the real registered
  typed method objects, checks generated receiver/arity behavior, and locks the
  three removed facade names to `TJS_E_MEMBERNOTFOUND` on the class object;
- removed the obsolete `sub_52E504`, `M11`, old line-number, and old
  `libkrkr2.so` progress-address commentary from this surface.

## IDB persistence

All four registrars, load raw callbacks, representative typed wrappers, and
typed `contains` targets were freshly decompiled, annotated, and saved to the
four recovery databases under `out/ida-recovery/`.

## Validation

The final local checks passed:

- exact table extraction from `main.cpp`: `members=54`, exact-order comparison
  `true`, four constants in native order, and `raw_bindings=load`;
- obsolete fixed-signature D3D raw-shim scan: zero matches;
- D3DEmotePlayer registration-block scan for `sub_...`, absolute addresses,
  `M11`, `cluster D`, and `libkrkr2.so`: zero matches;
- full `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax check:
  passed; only the repository-existing `_tss` literal-operator warning remains;
- `cmake --build --preset "Web Debug Build"`: passed and linked `index.html`;
  output contains only existing compiler/Emscripten warnings;
- targeted `git diff --check`: passed; Git printed only existing LF-to-CRLF
  working-copy notices.

The current build tree has no configured native motionplayer unit executable.
The new real-descriptor receiver/arity regression was therefore compilation-
checked in the complete test translation unit but not executed separately.
