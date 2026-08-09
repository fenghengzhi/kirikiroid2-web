# Cluster C Audit — EmotePlayer NCB binding + native instance lifecycle

Original audit: 2026-05-30. Superseding update: 2026-08-04.
Authoritative source: Android ARM64 `libkrkr2.so`. Local:
`cpp/plugins/motionplayer/`.

## 0. Current verdict

The former P0 class-identity/member-set defect and the later-discovered module-entry defect are
both fixed.

- `Motion.EmotePlayer` now owns the full `EmotePlayer_ncb_registerMembers@0x67FAC8` surface;
- `D3DEmotePlayer` retains its independent `0x52E504` surface;
- `motionplayer_ncb_register@0x6D9B08` no longer gets an extra EmotePlayer subclass row;
- the sole `emoteplayer.dll` callback now performs dependency load, class attachment and both
  ResourceManager setter injections in one data flow;
- the two setters retain their independently audited OwnerFilter target/control-block lifecycle.

Current status for the registration path covered here: **ALIGNED**. The same-day namespace follow-up
also closed the former Motion.Player post-alias item: Player is now the sixth in-flow subclass and no
top-level alias path remains.

## 1. Binary call graph

```text
emoteplayer_static_init@0x42EB00
  -> one "emoteplayer.dll" auto-register node
     init = emoteplayer_entry@0x682528; term = null

emoteplayer_entry@0x682528
  -> LoadModule("motionplayer.dll")
  -> global = TVPGetScriptDispatch(); value = global.Motion; motion = value.AsObject()
  -> EmotePlayer_loadClass@0x685BC0("EmotePlayer", 1)
       -> EmotePlayer_NCB_classInit@0x686148
       -> EmotePlayer_ncb_registerMembers@0x67FAC8 on the same class object
  -> attach class as Motion.EmotePlayer with flags 0x10000
  -> value = motion.ResourceManager
  -> create seed method; Variant(seed,seed); Release raw method
  -> manager = value.AsObject(); PropSet seed with flags 0x10200
  -> create func method; overwrite the same Variant via SetObject(func,func); Release raw method
  -> PropSet func with flags 0x10200; destroy both Variants
```

The complete `.text` scan finds exactly one materialization of the entry callback and exactly one
materialization of each setter callback. The setter UTF-16 names likewise have no other registration
owner.

`motionplayer_ncb_register@0x6D9B08` has exactly eleven subclass rows, in this order:

```text
Point, Circle, Rect, Quad, LayerGetter, Player, SourceCache, ObjSource,
ResourceManager, SeparateLayerAdaptor, D3DAdaptor
```

It has no EmotePlayer class-name materialization and no direct edge to the entry, class loader or
member registrar. EmotePlayer's final script owner is Motion, but its creation/attachment owner is
the independent emoteplayer module.

## 2. Motion.EmotePlayer class surface

`EmotePlayer_loadClass@0x685BC0` first calls class init at `0x685C24`, then the complete member
registrar at `0x685C2C`. Both operate on the same class object. The obsolete conclusion that the
binary class was finalize-only is false and has been removed from current notes.

The local `NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer)` now contains the two timeline constants and
the complete 70-member table in binary order: progress/frameProgress/draw/physics/wind/play/clear,
variable and serialization methods, transform/color/force methods, state properties, timing/bounds,
camera/root/scale accessors, selector/timeline queries and `getCommandList`. The formerly empty
constructor-only class no longer exists.

The separate `D3DEmotePlayer` class remains distinct. Its D3D-shell member table is not collapsed
into Motion.EmotePlayer.

## 3. Module-entry source alignment

The former local shape had two callbacks:

- pre callback only loaded `motionplayer.dll`;
- post callback separately looked up ResourceManager and injected the setters;
- Motion's main class table registered EmotePlayer early;
- local success/null/type guards changed the binary exception boundary;
- a lambda created two independent method Variants;
- the global dispatch was explicitly Released.

Current `main.cpp:803-858` mirrors `emoteplayer_entry` instead:

1. one module callback performs the complete operation;
2. Motion and ResourceManager PropGet reuse the same `value` Variant;
3. `Setup("EmotePlayer", true)` materializes class init + full member registration;
4. `TJSNativeClassRegisterNCM(... nitClass, TJS_STATICMEMBER)` reproduces type 0 / flags 0x10000;
5. one `methodValue` is constructed for seed and overwritten by `SetObject` for func;
6. both PropSet calls use `TJS_MEMBERENSURE | TJS_STATICMEMBER` (`0x10200`);
7. no binary-absent guards or explicit global Release remain.

The Motion main table at `main.cpp:587-599` contains no EmotePlayer row. ResourceManager's own
12-member table also contains neither setter; both appear only after emoteplayer module load.

## 4. Native instance lifecycle

```text
EmotePlayerNativeInstance_create@0x68629C:
  r = new(0x18); r.vptr = off_1A18BB0; r.payload = 0; r.sticky = 0; return r

EmotePlayerNativeInstance_destroy@0x6862D0:
  if (payload && !sticky) { EmoteEngine_dtor@0x67F4B8(payload); delete payload; }
  payload = 0; sticky = 0
```

The 24-byte ARM64 shell means `{vptr, EmoteEngine *payload, sticky/owned state}` at the target ABI;
the local source restores the named-field semantics and lets wasm32 calculate its own layout.
`payload != null && sticky == 0` remains the destruction gate.

## 5. Decrypt setter / OwnerFilter status

`setEmotePSBDecryptSeed@0x685D30..0x685E60` and
`setEmotePSBDecryptFunc@0x685E60..0x686148` were both fresh-decompiled after the original audit.
The first stores a full 8-byte TJS Integer capture and consumes only its low W32 in the xorshift
invoker. The second owns Object/ObjThis through a closure and refcount control block. Both create a
named temporary `std::function`, copy-assign through the separate `0x6A87D0` wrapper /
`0x6A87E8` assignment FDE, then destroy the source temporary independently.

That producer/consumer lifecycle is covered by
`FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md`; this update closes the distinct
question of who registers the setters and when.

## 6. Mechanical and runtime gates

`verify_elf_surface.py` now independently fixes:

- eight registration-related FDEs after including the complete motionplayer static initializer;
- seven UTF-16 module/class/member literals;
- `1 entry + 2 setter` unique full-text materializations;
- eleven entry/class-loader direct edges;
- 23 Motion constant edges, eleven subclass registration edges, two namespace callback
  materializations and two function member-add edges;
- zero forbidden EmotePlayer references inside the Motion registrar;
- zero full-ELF UTF-16 matches for the removed `ShortCutInitial*KeyMap` callback literals;
- 52 semantic words and five complete FDE hashes (3,404 raw bytes);
- a 10,062-byte canonical digest.

The unit test checks the process's first-load boundary: `motionplayer.dll` alone has no
`Motion.EmotePlayer`; after loading `emoteplayer.dll`, EmotePlayer, ResourceManager and both setter
members are Object values. NCB has no unload path, so later fixture instances correctly observe the
retained module. Final result: 21/21 test cases and 1555/1555 assertions.

## 7. Corrected stale conclusions

The following historical claims are superseded and must not be reused:

- local Motion.EmotePlayer is constructor-only;
- the full API exists only on local D3DEmotePlayer;
- registering EmotePlayer in Motion's main table is an acceptable equivalent;
- the local emoteplayer entry only loads its dependency;
- putting the two setters in ResourceManager's own member table is equivalent;
- `0x685E60` is an unanalyzed loc inside the entry.

The detailed entry gate and current source mapping are recorded in
`FOLLOWUP_EMOTE_REGISTRATION_INJECTION_SURFACE_2026-08-04.md`.
