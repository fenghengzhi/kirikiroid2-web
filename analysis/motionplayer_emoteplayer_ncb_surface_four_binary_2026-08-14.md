# Motion.EmotePlayer NCB surface and special-call boundaries (four-reference audit, 2026-08-14)

## Conclusion

The four current reference binaries expose the same `Motion.EmotePlayer`
surface: **two constants followed by exactly 70 members in the same order**.
The existing `main.cpp` table already contained the complete name set and order;
the only table edit required by this audit was correcting the local range label
from `#20-33` to `#20-34`.

Fresh registrar and target-function recovery also disproves several old
`libkrkr2.so`-derived source comments and three local call routes:

- `progress(milliseconds)` is a dedicated wrapper that computes
  `milliseconds * 60.0 / 1000.0` and enters the full Engine progress core.
- `frameProgress(frameDt)` is registered directly to that same Engine core. It
  must not enter the embedded `Player::frameProgress` lower-level state machine.
- `startWind` is registered directly to the Engine five-`float` setter.
- `stopWind()` has its own zero-argument delete-and-null body. It destroys only
  the current Engine wind emitter and deliberately retains the cached wind
  scalars.
- `pass()` is a zero-script-argument thunk to the Engine timeline-flush body.
  It neither accepts a time delta nor aliases either progress entry.

These results agree across Android arm64, Android armv7, iOS arm64, and iOS
armv7. Absolute addresses below are evidence coordinates only; portable source
uses semantic `_guess` names.

## Registrar identities

| Reference target | Registrar | Recovered size |
|---|---:|---:|
| Android arm64 | `0x67CEA8` | `0x1BB8` |
| Android armv7 | `0x5612E8` | `0x656` |
| iOS arm64 | `0x1001B5130` | `0x9F0` |
| iOS armv7 | `0x1B4DE0` | `0x932` |

The audit searched each name as UTF-16LE bytes and followed every reference
back into its registrar. Android code generation can produce two or three
xrefs for one registration string because an address is materialized in
several instructions; those are not duplicate registrations. The iOS arm64
registrar has exactly 72 direct read-only-data string targets, with no extra
name and no omission. The four monotonically ordered target sequences are
identical.

## Exact registration order

The constants precede the member loop:

| Prefix position | Registered name | Kind |
|---:|---|---|
| C1 | `TimelinePlayFlagParallel` | constant |
| C2 | `TimelinePlayFlagDifference` | constant |

The 70 members then appear in this exact order:

| Member | Registered name | Local binding kind |
|---:|---|---|
| 1 | `progress` | typed method; dedicated millisecond wrapper |
| 2 | `frameProgress` | typed one-double/void method; direct `EmoteEngine::progress` target, zero member adjustment |
| 3 | `draw` | typed one-Variant/void method; real Primary→embedded-Player wrapper, by-value Variant |
| 4 | `initPhysics` | typed one-Variant/void method; direct `EmoteEngine::applyMetadata_guess` target, by-value Variant |
| 5 | `startWind` | typed five-float/void method; direct `EmoteEngine::setWind_guess` target, zero member adjustment |
| 6 | `stopWind` | typed method; dedicated zero-argument body |
| 7 | `play` | typed method |
| 8 | `clear` | typed method; two-Variant draw-to-layer wrapper |
| 9 | `getVariable` | typed one-ttstr/double method; direct `EmoteEngine::getVariable` target, zero member adjustment |
| 10 | `contains` | typed method; raw-label geometry hit |
| 11 | `serialize` | typed no-argument/Variant method; direct `EmoteEngine::serializeState_guess` target |
| 12 | `unserialize` | typed one-Variant/void method; direct `EmoteEngine::unserializeState_guess` target, by-value Variant |
| 13 | `pass` | typed method; zero-argument timeline-flush thunk |
| 14 | `setVariable` | raw callback |
| 15 | `setCoord` | raw callback |
| 16 | `setScale` | raw callback |
| 17 | `setRotate` | raw callback |
| 18 | `setColor` | raw callback |
| 19 | `setOuterForce` | raw callback |
| 20 | `completionType` | signed-integer read/write property; Primary→Player wrapper |
| 21 | `chara` | ttstr read/write property; setter enters Player coordinator with flags 0 |
| 22 | `motion` | ttstr read/write property; setter enters Player play with flags 0 |
| 23 | `motionKey` | Variant getter / ttstr setter property; exact target pair shared with #24 |
| 24 | `project` | exact getter/setter alias of #23; assignment persists a String Variant |
| 25 | `maskMode` | signed-integer read/write property; Primary→Player wrapper |
| 26 | `meshDivisionRatio` | raw-double read/write property; Primary→Player wrapper |
| 27 | `outline` | Variant CopyRef/copy-assign read/write property |
| 28 | `priorDraw` | Boolean read/write property; setter consumes the converted value |
| 29 | `frameLastTime` | raw-frame double read-only property; exact getter shared with #31 |
| 30 | `frameLoopTime` | raw-frame double read-only property; exact getter shared with #32 |
| 31 | `lastTime` | exact raw-frame getter alias of #29; no milliseconds conversion |
| 32 | `loopTime` | exact raw-frame getter alias of #30; no milliseconds conversion |
| 33 | `bounds` | read-only Variant wrapper; null setter |
| 34 | `processedMeshVerticesNum` | read-only Variant wrapper; signed Integer publication, null setter |
| 35 | `setDrawAffineTranslateMatrix` | typed six-double/Boolean Player wrapper, zero adjustment |
| 36 | `getCameraOffset` | typed no-argument/Variant Player wrapper, zero adjustment |
| 37 | `setCameraOffset` | typed two-double/void Player wrapper, zero adjustment |
| 38 | `modifyRoot` | typed no-argument/void Player wrapper, zero adjustment |
| 39 | `setHairScale` | typed one-double/void direct payload store, zero adjustment |
| 40 | `setPartsScale` | typed one-double/void direct payload store, zero adjustment |
| 41 | `setBustScale` | typed one-double/void direct payload store, zero adjustment |
| 42 | `hairScale` | double property; setter is the exact #39 member pointer |
| 43 | `bustScale` | double property; setter is the exact #41 member pointer |
| 44 | `partsScale` | double property; setter is the exact #40 member pointer |
| 45 | `debugPrint` | Boolean property; setter ignores converted value and stores true |
| 46 | `queuing` | Boolean property; setter ignores converted value and stores true |
| 47 | `directEdit` | Boolean property; setter ignores converted value and stores true |
| 48 | `selectorEnabled` | Boolean property; setter stores true and always synchronizes selectors |
| 49 | `variableKeys` | read-only Variant property; zero adjustment and null setter |
| 50 | `animating` | read-only property; direct `EmoteEngine::getAnimating_guess` getter, zero adjustment |
| 51 | `setMirror` | typed method; direct `EmoteEngine::setMirror_guess` target, zero adjustment |
| 52 | `skip` | typed method; direct `EmoteEngine::resetControllers_guess` target, zero adjustment |
| 53 | `playTimeline` | native-instance raw callback; label required, flags optional/default 0 |
| 54 | `stopTimeline` | native-instance raw callback; label optional/default empty |
| 55 | `getTimelinePlaying` | native-instance raw callback; label optional/default empty, Boolean result |
| 56 | `setTimelineBlendRatio` | native-instance raw callback; asymmetric inactive/active blend state machine |
| 57 | `fadeInTimeline` | native-instance raw callback；label 必填，duration/ease 可选 |
| 58 | `fadeOutTimeline` | native-instance raw callback；label 必填，duration/ease 可选 |
| 59 | `getTimelineBlendRatio` | typed one-ttstr/double method; direct Engine target, zero adjustment |
| 60 | `getVariableRange` | typed method |
| 61 | `getVariableFrameList` | typed method |
| 62 | `getMainTimelineLabelList` | typed method; direct Engine target, zero adjustment |
| 63 | `getDiffTimelineLabelList` | typed method; direct Engine target, zero adjustment |
| 64 | `getLoopTimeline` | typed method; direct Engine target, zero adjustment |
| 65 | `getTimelineTotalFrameCount` | typed method; direct Engine target, zero adjustment |
| 66 | `getPlayingTimelineInfoList` | typed method; direct Engine target, zero adjustment |
| 67 | `isSelectorTarget` | typed method; direct Engine target, zero adjustment |
| 68 | `activateSelectorTarget` | typed method; direct Engine target, zero adjustment |
| 69 | `deactivateSelectorTarget` | typed method; direct Engine target, zero adjustment |
| 70 | `getCommandList` | typed method; real Primary-to-Player wrapper retained |

member 14 的2026-08-15 fresh body复核确认：少于2参返回`TJS_E_BADPARAMCOUNT`，label、
value、可选transition、可选ease按序转换；ease先在Primary callback中以double映射一次，
再作为Engine第五参数被映射第二次。它不是D3D direct façade的单层ease管线。见
`analysis/motionplayer_set_variable_router_double_ease_integer_conversion_four_binary_2026-08-15.md`。

member 14–19 的连续注册槽还共同选择 NCBind native-instance raw callback：wrapper
先解出 Engine-sized EmotePlayer payload，callback 第四参已经是 native pointer；body
内没有 `objthis` lookup。member 15–19 的 argc、Variant 转换、double ease、color
字节拆分及 outer-force owner/router 边界见
`analysis/motionplayer_primary_raw_controller_setters_four_binary_2026-08-15.md`。

member 53–56 的 2026-08-16 fresh registrar/body 复核确认它们也属于同一
native-instance raw-callback family，而不是 typed member：play 仅要求 label，stop/query
允许省略 label，setTimelineBlendRatio 具有“inactive 时 play+seed0 并提前返回，active
时才读取可选 duration/ease/autoStop 并 target1”的不对称边界。member59 则直接保存
Engine blend getter 与零 adjustment，参数是按值 ttstr。详见
`analysis/motionplayer_primary_timeline_raw_callbacks_direct_blend_getter_four_binary_2026-08-16.md`。

member 35–49 的 2026-08-16 fresh registrar/target 复核确认它们全部属于 typed
method/property family，member adjustment 均为零。#35–38 是实际 Primary→Player
wrapper；#39–44 直接访问 Engine-sized payload，而且 #42–44 的 setter 分别逐字复用
#39/#41/#40 的同一成员指针，不存在 property-only facade；#45–48 的 typed Boolean
setter 在 NCBind 转换后忽略值并写 true；#49 setter 槽为空。详见
`analysis/motionplayer_primary_mid_typed_binding_exact_scale_setter_reuse_four_binary_2026-08-16.md`。

member 20–34 的 2026-08-16 fresh registrar/target 复核确认它们全部属于 typed property
family，member adjustment 均为零。#23/#24 的 getter/setter 成员指针逐字相同，setter
native 参数是 ttstr，故任意脚本值先转字符串再写 persistent motion-context Variant；
#29/#31 与 #30/#32 分别精确复用两个 raw-frame getter，不走 Player 表面的毫秒换算；
#34 target 自己执行 `uint32_t -> signed tjs_int -> tvtInteger Variant`，不是 uint32 返回
留给 property adapter 转换。详见
`analysis/motionplayer_primary_property_alias_typed_variant_four_binary_2026-08-16.md`。

member 3 的 2026-08-16 fresh registrar/wrapper/typed-adapter 复核确认 descriptor 保存真实
Primary wrapper 与零 adjustment，而不是直接保存 Player 或 Engine member。typed adapter
先持有 argv[0] 的按值副本，wrapper 从 Engine-sized payload 取 embedded Player，再为内联
`Player::draw` 持有第二个 Variant 副本并进入完整 `Player_draw_guess` body；两个临时量逆序
析构，caller Variant 不变。receiver/result-clear/argc/payload-unwrapping/surplus 次序及 D3D
sticky-byte 落点详见
`analysis/motionplayer_primary_draw_typed_owner_four_binary_2026-08-16.md`。

member 8/10 的 2026-08-15 fresh registrar/member-body复核则确认它们属于 typed method
family，不是 raw callback。`clear` 必须接收两个 Variant 并无条件进入 Player 内层门，
`contains` 必须接收 label/x/y 并发布 Boolean。详见
`analysis/motionplayer_emoteplayer_clear_contains_typed_four_binary_2026-08-15.md`。

member 11/12 的 2026-08-15 fresh registrar 复核确认二者都直接存 Engine state core 与
零 adjustment，不存在 EmotePlayer forwarding body。member11 的无参 Variant wrapper
还恢复了 membername/receiver/result-clear/argc 顺序、非负 surplus 忽略及双临时 Variant
owner handoff；member12 复用一 Variant 按值 typed family。详见
`analysis/motionplayer_state_method_typed_binding_owner_four_binary_2026-08-15.md`。

The “local binding kind” column records the current port shape. It is useful
for checking table drift, but it does not imply that every typed NCB template
instantiation is emitted as a separate non-inlined native function.

## Progress and frameProgress data flow

| Target | `progress` wrapper | shared Engine core | Core size |
|---|---:|---:|---:|
| Android arm64 | `0x67EC94` | `0x67A3F8` | `0x4B8` |
| Android armv7 | `0x561D08` | `0x55FEF0` | `0x124` |
| iOS arm64 | `0x1001B5C68` | `0x1001B4304` | `0x164` |
| iOS armv7 | `0x1B586C` | `0x1B3E10` | `0x140` |

All four dedicated wrappers decompile to the same semantic operation:

```text
EmoteEngine_progressCore_guess(self, milliseconds * 60.0 / 1000.0)
```

The registrar places the shared Engine core itself in the `frameProgress`
descriptor. Therefore the native split is:

```text
progress(milliseconds)
    -> milliseconds * 60 / 1000
    -> EmoteEngine progress core(frameDt)

frameProgress(frameDt)
    -> EmoteEngine progress core(frameDt)
```

This distinction is observable at zero: the Engine core still drains its own
dirty state, whereas routing `frameProgress` to the embedded Player would skip
that Engine-owned slice. The Android D3D shell has a separate nonzero-delta gate
and does not change this Motion.EmotePlayer boundary.

### Android arm64 IDB boundary repair

The recovered Android arm64 database had incorrectly attached
`0x67A3F8..0x67A8B0` as a remote tail chunk of both the D3D progress wrapper and
the Motion.EmotePlayer progress wrapper. This is incompatible with the
registrar taking `0x67A3F8` as an independent function pointer and with all
three other references exposing a standalone Engine function.

The erroneous tail ownership was removed, the standalone `0x4B8` function and
the two small wrapper ranges were recreated, and their AArch64 code items were
restored. Fresh decompilation now succeeds for all three functions:

- D3D wrapper: reject zero, then call the Engine core through the shell's
  Engine pointer;
- Motion.EmotePlayer wrapper: convert milliseconds, then call the Engine core;
- Engine core: the complete independent progress body used directly by
  `frameProgress`.

## Wind data flow and lifetime

The `startWind` descriptor directly targets the Engine five-`float` entry:

| Target | direct Engine target |
|---|---:|
| Android arm64 | `0x66DD8C` |
| Android armv7 | `0x559900` |
| iOS arm64 | `0x1001AC718` |
| iOS armv7 | `0x1ABF24` |

There is no Motion.EmotePlayer → embedded Player → Engine-back-pointer hop at
this boundary. The portable method therefore now has five `float` parameters
and calls `engine().setWind_guess(...)` directly.

`stopWind` is not implemented by passing five zeros to that setter:

| Target | dedicated zero-argument body | Engine wind-pointer slot seen by decompiler |
|---|---:|---:|
| Android arm64 | `0x67EE18` | pointer index 141 |
| Android armv7 | `0x561D90` | pointer index 141 |
| iOS arm64 | `0x1001B5CD8` | pointer index 95 |
| iOS armv7 | `0x1B5944` | pointer index 95 |

Each body loads the raw emitter pointer, calls `operator delete` only when it is
non-null, and stores null back. No cached minimum/maximum/amplitude/frequency
field is written. Those scalar caches consequently survive `stopWind()` and
can remain observable to later internal logic.

## pass is a zero-argument timeline flush

| Target | registered thunk | Engine body |
|---|---:|---:|
| Android arm64 | `0x67F028` | `0x67A100` |
| Android armv7 | `0x561E18` | `0x55FCC4` |
| iOS arm64 | `0x1001B5ECC` | `0x1001B3FE4` |
| iOS armv7 | `0x1B5BAA` | `0x1B3BBC` |

The four thunks take only `self` and forward it to the Engine body. Registrar
template selection is also the same no-argument method family used by
`stopWind`, rather than a one-double method family. The recovered semantic
boundary is therefore:

```text
Motion.EmotePlayer.pass()
    -> EmotePlayer_passTimelines_thunk_guess(self)
    -> EmoteEngine_passTimelines_guess(self)
```

The old local `pass(double)` declaration was structurally wrong: it invented a
script delta and aliased progress. The former D3D-side
`D3DEmotePlayer::pass(double)` convenience likewise remained outside this
Motion.EmotePlayer registration table; it was removed after a separate
four-reference audit confirmed that D3D's script-facing `pass()` is bound to
its own zero-argument timeline-flush method and `progress(double)` is the only
registered one-double entry.

## Old Android-arm64 address comments shown to be stale

The following old suffixes no longer identify the claimed Motion.EmotePlayer
members in the current Android arm64 reference:

| Old source suffix | Current recovered location/meaning |
|---:|---|
| `0x6818B4` | inside `EmoteSelectorControlDeque_dtor_guess` |
| `0x6817C0` | inside `EmoteLoopControlDeque_dtor_guess` |
| `0x67D4D0` | inside `EmotePlayer_ncb_registerMembers_guess` |
| `0x681A38` | inside `EmoteClampControlDeque_dtor_guess` |
| `0x675E40` | inside `EmoteEngine_restoreEyeState_guess` |
| `0x678044` | inside `EmoteEngine_restoreSelectorState_guess` |
| `0x681C48` | inside `EmoteEyeControlDeque_dtor_guess` |
| `0x671DF0` | inside `EmoteEngine_getAnimating_guess` |
| `0x672060` | inside `EmoteEngine_getAnimating_guess` |
| `0x672568` | inside `EmoteEngine_getDiffTimelineLabelList_guess` |
| `0x67277C` | inside `EmoteEngine_getLoopTimeline_guess` |

These collisions are why the active declarations and implementations now use
semantic descriptions instead of carrying the old single-binary `sub_...` or
address annotations forward.

## Port changes and regression coverage

This audit changed the portable source as follows:

- `EmotePlayer::progress` documents and preserves millisecond conversion;
- `EmotePlayer::frameProgress` now enters `engine().progress(frameDt)`;
- `EmotePlayer::startWind` uses five `float` parameters and the direct Engine
  target;
- `EmotePlayer::stopWind` directly deletes and nulls `_windEmitter`, leaving
  scalar caches untouched;
- `EmotePlayer::pass` is now zero-argument and calls
  `engine().passTimelines_guess()`;
- stale old-address comments were removed from this surface;
- the local NCB table range comment now covers properties `#20-34`.

The added unit regression distinguishes the two important misroutes:

1. `frameProgress(0)` must clear Engine-owned dirty state, proving that it did
   not stop at the embedded Player;
2. `pass()` must remove an ordinary active timeline without changing the
   embedded Player frame tick, proving its zero-argument timeline-flush route.

## IDB persistence

The following four recovery databases were annotated and saved after fresh
decompilation of the registrars, progress wrappers/cores, stop bodies, pass
thunks, and pass bodies:

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## Validation

The final local checks passed:

- full `tests/unit-tests/plugins/motionplayer-dll.cpp` Emscripten syntax check:
  passed; the only diagnostic was the repository-existing whitespace before
  the `_tss` literal-operator suffix;
- `cmake --build --preset "Web Debug Build"`: passed and linked `index.html`;
  emitted only existing compiler/Emscripten warnings;
- registrar extraction from `main.cpp`: exactly 70 member macros, exact-order
  comparison `true`, and the two expected constants in the expected order;
- selected Motion.EmotePlayer declaration/implementation scan: no remaining
  `sub_...`, absolute-address, `Like_0x...`, stale `preProgress_guess`, or
  `pass(double)` annotation in this surface;
- targeted `git diff --check`: passed; only Git's existing LF-to-CRLF working
  copy notices were printed.

No configured native motionplayer unit executable exists in the current build
tree, so the new runtime assertions were compilation-checked as part of the
full test translation unit but were not separately executed in this audit.
