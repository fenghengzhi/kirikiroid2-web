# Cluster C Audit (re-core) — EmotePlayer 类 + NCB 暴露面

Original audit: 2026-06-07. Superseding update: 2026-08-04.
Authority: Android ARM64 `libkrkr2.so`.

## 0. Current verdict

The old 69+2 count and “no activateSelectorTarget” result were caused by a missed UTF-16 literal.
Fresh byte inspection and registration decompilation corrected the authoritative surface to
**70 members + 2 constants**, including `activateSelectorTarget@0x14D7796`. The local
`NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer)` now carries that complete table in binary order.

The later module-entry audit also found and fixed a separate owner/timing defect. EmotePlayer is no
longer an extra row in `motionplayer_ncb_register`; the sole `emoteplayer.dll` callback creates and
attaches it, then injects both decrypt setters into ResourceManager.

Current registration verdict: **ALIGNED**. Detailed implementation changes made after this historical
cluster are tracked in `analysis/psbfile_android_reconstruction_2026-07-18.md`; the obsolete field/
delegate TODO list from the 2026-06-07 snapshot is intentionally not retained as current status.

## 1. Authoritative class surfaces

### Motion.EmotePlayer

`EmotePlayer_loadClass@0x685BC0` calls both:

- `EmotePlayer_NCB_classInit@0x686148`;
- `EmotePlayer_ncb_registerMembers@0x67FAC8`.

Both calls operate on the same class object. The registrar exposes two timeline constants followed by
70 members. The tail is:

```text
... getPlayingTimelineInfoList,
isSelectorTarget,
activateSelectorTarget,
deactivateSelectorTarget,
getCommandList
```

Therefore the class is neither finalize-only nor a constructor-only shell. The local table at
`main.cpp:453-554` mirrors the complete class surface.

### D3DEmotePlayer

`D3DEmotePlayer_ncb_registerMembers@0x52E504` remains an independent class/table with four constants
and its D3D-shell API. Motion.EmotePlayer and D3DEmotePlayer are not interchangeable and are not
collapsed locally.

## 2. Registration owner and order

`emoteplayer_static_init@0x42EB00` constructs exactly one auto-register node:

```text
module = "emoteplayer.dll"
init   = emoteplayer_entry@0x682528
term   = null
```

The entry performs this exact sequence:

```text
LoadModule("motionplayer.dll")
value = global.Motion; motion = value.AsObject()
load full EmotePlayer class; attach to Motion with flags 0x10000
value = motion.ResourceManager
methodValue = Object(seedMethod, seedMethod); PropSet seed with 0x10200
methodValue.SetObject(funcMethod, funcMethod); PropSet func with 0x10200
destroy methodValue and value; no explicit global Release
```

`motionplayer_ncb_register@0x6D9B08` has eleven subclass edges—Point, Circle, Rect, Quad,
LayerGetter, Player, SourceCache, ObjSource, ResourceManager, SeparateLayerAdaptor, D3DAdaptor—and
no EmotePlayer reference. Current local module ownership matches this split.

## 3. Native instance lifecycle

```text
EmotePlayerNativeInstance_create@0x68629C:
  new 24-byte ARM64 shell; payload = null; sticky = 0

EmotePlayerNativeInstance_destroy@0x6862D0:
  if (payload && !sticky) { destroy EmoteEngine; delete payload; }
  payload = null; sticky = 0
```

This is a source-semantic object model, not a requirement to force ARM64 byte offsets into wasm32.
The local class retains the payload/sticky ownership gate while the target compiler chooses ABI layout.

## 4. Decrypt setter boundary

Both setters are independent FDEs:

- seed: `0x685D30..0x685E60`;
- function: `0x685E60..0x686148`.

They are injected only by `emoteplayer_entry`; they do not belong to ResourceManager's twelve-member
registrar. Their process-wide `OwnerFilter` capture/copy/manager/invoker lifecycle is covered by
`FOLLOWUP_OWNER_FILTER_BRIDGE_LIFECYCLE_SURFACE_2026-08-04.md`.

## 5. Verification

The independent registration gate fixes eight FDEs, seven positive UTF-16 literals, three unique entry/setter
materializations, eleven entry/class-loader edges, 23 Motion constant edges, eleven subclass edges,
two namespace callback materializations, two function member-add edges, zero forbidden
Motion→Emote references, zero `ShortCutInitial*KeyMap` UTF-16 matches, 52 semantic words and five
full-FDE hashes. The runtime regression passes
21/21 test cases and 1555/1555 assertions.

See `FOLLOWUP_EMOTE_REGISTRATION_INJECTION_SURFACE_2026-08-04.md` for the complete binary-to-source
mapping and canonical digest.
