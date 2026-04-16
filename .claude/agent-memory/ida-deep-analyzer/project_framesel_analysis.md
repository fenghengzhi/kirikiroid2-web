---
name: Frame selection / interpolation chain
description: sub_6926B4/sub_692AB0/sub_699AE4 frame cursor + interpolation architecture in libkrkr2.so; why frameList only has 4 frames is not a framesel issue
type: project
---

# framesel chain in libkrkr2.so

## Entry points

- `Player_parseFrame` (0x6926B4) — parse ONE frame entry from frameList at index `a3`. Reads PSB keys: `time`, `type`, `content`, `content.mask`, `content.act`. NOT a scan/select function.
- `Player_mergeFrameContent` (0x692AB0) — given an already-parsed frame slot, read mask-gated properties from its `content` dict (bm, color, ox/oy, coord, opa, fx/fy, angle, zx/zy, sx/sy, ti, ccc/occ/acc/zcc/scc, cp, mesh, motion{mask=0x80000}, model{0x1000000}, prt{0x100000}, camera, anchor, feedback). This is where sub-motion is loaded (motion.mask.1→flags, .2→dt, .4→docmpl, .8→dofst, .10→dtgt).
- `Player_evaluateTimeline` (0x699AE4, aliased) — given node with TWO parsed slots at +320 and +856 (stride 536), compute lerp factor and interpolate fields into +1504..+1656 output slab. Dispatches by node type for type-specific slots (case 4=particle, 5=?, 10=camera).

## Frame cursor — where the SELECTION actually happens

Per-node active frame cursor at node+1392 (int, toggles 0/1). Two slot buffers at node+320 (536B) and node+856 (536B). Initial choice in `Player_initNodeTimeline_guess` (0x6B64AC): binary walk `frameList` for frames with `time <= currentTime`, last such index = active, active+1 = next.

Every-frame advance is split:
- `Player_advanceNodeFrames_guess` (0x6B7E44): forward scan — while next.time <= now, toggle cursor and `parseFrame(otherSlot, index+1)`
- `Player_rewindRootAndNodes_guess` (0x6B9A3C): reverse scan — while now < active.time, toggle cursor and `parseFrame(otherSlot, index-1)`
- `Player_advanceRootAndNodes_guess` (0x6B6ADC): same for non-layer frame streams (root frames at a1+548, layer-level at a1+1072). Processes 3 independent frame streams: root frames (+548), layer frames (+1072), and variable tracks (+1312 boundary).

## Multiple parallel frame streams

Player has `std::vector` iterator pairs for MULTIPLE independent frame arrays:
- Player+200 == root node's data (motion "priority[0].content.frameList")
- Player+548 == root-level timeline (motion-scoped, holds `content` variant at +616)
- Player+1072 == layer frame list (in Player_advanceRootAndNodes / evaluation head)
- Player+1312..+1344 == variable-track deque (chunked 160B elements, 3-per-chunk) for animated parameters
- Per-node (deque of 2632B elements): frameList at node+64 scanned per-node

`sub_56C694` (aliased TJSArray_length_guess) returns count of frames — read via `dict["count"]` PropGet.

## type field semantics (confirmed by disassembly)

From `Player_parseFrame` (0x6926B4):
```c
type_raw = PropGetAsInt("type");
if (type_raw == 0) { slot.stop_flag = 1; goto SKIP_CONTENT; }  // STOP marker
slot.stop_flag = 0;
if (type_raw == 2) slot.visible_flag = 0;  // invisible keyframe
if (type_raw == 3) slot.visible_flag = 1;  // normal visible keyframe
// type==1 falls through — reads content but skips visible/stop assignment
```
So type==0 means "end of stream"; type==1 is a transient/skip; type==2/3 are visible/invisible keyframes. `Player_advanceRootAndNodes` only processes content for `type==1` frames (has "action"/"sync" semantics), and `Player_parseFrame` handles all non-zero types for data.

## content.mask semantics (at slot+20)

Written by `Player_parseFrame` directly from `content["mask"]`. Then `Player_mergeFrameContent` uses that mask to decide which keys to read:
- mask & 1 → icon (src)
- mask & 2 → coord
- mask & 0xC → fx/fy
- mask & 0x10 → angle
- mask & 0x60 → zx/zy
- mask & 0x180 → sx/sy
- mask & 0x200 → color
- mask & 0x400 → opa
- mask & 0x800 → ccc (color curve)
- mask & 0x1000 → acc (alpha curve)
- mask & 0x2000 → zcc (zoom curve)
- mask & 0x4000 → scc (skew curve)
- mask & 0x8000 → occ (opacity curve)
- mask & 0x20000 → bm (blend mode)
- mask & 0x40000 → act (additive-action list, stored at slot+288)
- mask & 0x80000 → motion (sub-motion)
- mask & 0x100000 → prt (particle)
- mask & 0x200000 → camera
- mask & 0x800000 → anchor
- mask & 0x1000000 → model
- mask & 0x2000000 → mesh/obj/cc/bp
- mask & 0x8000000 → feedback

## IDA symbol cleanups done
- 0x6926B4 → Player_parseFrame
- 0x692AB0 → Player_mergeFrameContent
- 0x69260C → Frame_resetSlot
- 0x6B64AC → Player_initNodeTimeline_guess
- 0x6B6ADC → Player_advanceRootAndNodes_guess
- 0x6B7E44 → Player_advanceNodeFrames_guess
- 0x6B9A3C → Player_rewindRootAndNodes_guess
- 0x699AE4 → Player_evaluateTimeline (was already)

## Critical finding: frameList is NOT filtered or split

`Player_initNodeFields` (0x6B3C78) reads `frameList` as a single TJS variant into node+64 with NO filtering. `sub_56C694` reads the `.count` and all indices 0..count-1 are walked. Each entry is a full frame dict with its own `time`/`type`/`content`.

So **if a .mtn logo node only yields 4 frames, the PSB itself contains 4 entries** — the binary does not split frames across multiple arrays. Per-letter animation in yuzulogo must come from something else: either (a) the 4 frames reference a layered `motion`/`model` sub-stream (mask 0x80000 / 0x1000000), or (b) the letters are separate sibling nodes each with their own frameList, or (c) the PSB root-level `priority[0].content` timeline drives src swaps via `action` strings processed by sub_6B638C (also invoked from Player_advanceRootAndNodes at "action" key).

Check `sub_6B638C` — this is the action-runner called when `content.mask & 0x40000` (slot+342 & 4) triggers. Its xrefs from 0x6B74E4, 0x6B6E68, 0x6BA26C, 0x6B6780 all pertain to "action list processing" — likely how string-based commands (including potential src swaps) are applied.
