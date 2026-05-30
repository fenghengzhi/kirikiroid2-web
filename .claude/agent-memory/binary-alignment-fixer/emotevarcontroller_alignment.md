---
name: emotevarcontroller-alignment
description: motion::EmoteVarController ctor/step alignment facts vs libkrkr2.so (0x667030, 0x666BF8) — allocation size, loop bounds, inverted lerp array roles, element offsets
metadata:
  type: project
---

EmoteVarController (cpp/plugins/motionplayer/EmoteVarController.{h,cpp}) aligned to libkrkr2.so:
- ctor `EmoteVarController_ctor_20Bdeque` @ 0x667030
- step `EmoteVarController_step` @ 0x666BF8
- deque header init `sub_6878D8` @ 0x6878D8 (element stride 20, 25/block -> a1[4]=base+500)

**Allocation (ctor):** `v5 = is_mul_ok(count,4) ? 4*count : -1` BYTES; `operator new[](v5)` x3 =
**count floats each** (4*count BYTES), then `memset(.,0,4*count)`. NOT count*4 floats. Earlier
local code over-allocated 4x (channelCount=count*4). `*(self+80)=count`, `*(self+84)=0`.

**Element layout (20B deque element):** per-channel destination floats at `*(float*)(elem+4*i)`
for i in [0,count); `duration` at element **+12**; `powCount` at element **+16**. (Earlier .h
comment wrongly said duration@+4 / powCount@+8.) For count==4, channel index 3 aliases
duration@+12 — binary reads raw bytes, so index from element base, not a fixed channel[3].

**step loop bounds:** every loop bounded by `count = *(int*)(self+80)`, NOT count*4. out gets count floats.

**INVERTED lerp roles (the subtle bug):** the binary's named arrays do NOT mean what they say.
- +88 currentValue = output value
- +96 "targetValue" = lerp SOURCE (snapshot of currentValue captured at keyframe start)
- +104 "startValue" = lerp DESTINATION (the element channel values)
state==0 keyframe setup: `targetValue[i]=currentValue[i]`; `startValue[i]=element[i]`.
state==1 update: `current[i] = target[i] + f*(start[i]-target[i])` where f=powf(phase,powCount).
phase>=1 commit: `current[i]=start[i]` (the destination), set phase=1.0, state=0.
Earlier local code had it backwards (broadcast elem.endValue to targetValue, current=start+f*(target-start)).

**Common偏差 pattern observed here:** review (analysis/MotionPlayer_Restoration_Review_2026-05-30.md P0-1)
flagged the 4x over-alloc + count*4 loops; reverse-engineering additionally revealed inverted
lerp roles and wrong element field offsets — i.e. a review-flagged symptom often sits on top of a
deeper data-flow inversion. Always re-derive the full lerp direction from the decompiled
state==1 scalar tail, do not trust local array names.

**No unit test coverage:** tests/unit-tests/plugins/motionplayer-dll.cpp does not reference
EmoteVarController; only build verification is available for this function.
