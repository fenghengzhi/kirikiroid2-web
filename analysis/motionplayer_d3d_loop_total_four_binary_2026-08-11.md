# D3DEmotePlayer loop/total-frame queries — four-reference reconstruction

Date: 2026-08-11

This audit covers the D3D `isLoopTimeline` and
`getTimelineTotalFrameCount` public methods together with their shared
`EmoteEngine` state-table queries.  It supersedes the old source helper names
whose embedded addresses came from the retired single-`libkrkr2.so` analysis.

The owner/logging path was checked again on 2026-08-15. That fresh four-target
pass corrected one material error in the original version of this note: a
missing loop label is logged and returns `false`; it does **not** throw.

## Function mapping

| Function | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| D3D `isLoopTimeline` | `0x530B10` / `0xA4` | `0x494E18` / `0x58` | `0x100233284` / `0x5C` | `0x231EC8` / `0x98` |
| D3D `getTimelineTotalFrameCount` | `0x530BB4` / `0xAC` | `0x494E88` / `0x68` | `0x1002332F4` / `0x5C` | `0x231F8C` / `0xA0` |
| Engine loop query | `0x67260C` / `0x1C4` | `0x55B6B0` / `0x76` | `0x1001AF02C` / `0x84` | `0x1AE89C` / `0xCE` |
| Engine total-frame query | `0x6727D0` / `0xD4` | `0x55B750` / `0x2A` | `0x1001AF0D4` / `0x30` | `0x1AE9A4` / `0x2A` |

The old local names referenced `0x67522C` and `0x6753F0`; neither is the
Android-arm64 helper in the current reference set.  The helpers are therefore
renamed semantically with the mandatory `_guess` suffix.

## Data flow and lifetime

Both D3D public bodies resolve the primary object's Engine through the same
pointer chain documented for the timeline enumeration cluster.  Because the
Engine C++ query signature also takes `ttstr` by value, each D3D wrapper makes
a temporary backing-string reference before calling Engine and releases it on
every normal or exceptional cleanup path.  arm64 uses atomic exclusive acquire/release
increments; armv7 uses `ldrex`/`strex` plus memory barriers.  This is ownership
traffic only—the Engine query does not retain or mutate the label.

The Engine then performs a non-inserting lookup in the timeline-state hash map:

| Item | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| state map offset | `+936` | `+468` | `+584` | `+292` |
| `loopBegin` in found value/node | `+56` | `+40` | `+64` | `+36` |
| `lastTime` in found value/node | `+72` | `+56` | `+80` | `+52` |

These offsets differ only because the four ABIs lay out pointers and the
compound map value differently.

## Common pseudocode and boundary asymmetry

```cpp
bool getLoopTimeline(ttstr label) {
    auto found = timelineStateByLabel.find(label);
    if (found != timelineStateByLabel.end())
        return found->second.loopBegin >= 0.0;
    TVPAddLog("timeline label not found '" + label + "'.");
    return false;
}

double getTimelineTotalFrameCount(ttstr label) {
    auto found = timelineStateByLabel.find(label);
    if (found != timelineStateByLabel.end() &&
        found->second.loopBegin >= 0.0)
        return found->second.lastTime;
    return 0.0;
}

int D3D_getTimelineTotalFrameCount(const ttstr &label) {
    return static_cast<int>(engine.getTimelineTotalFrameCount(label));
}
```

The missing-label behavior is deliberately asymmetric and observable, but the
split is logging versus silence rather than exception versus return:

- loop query: sends exactly `timeline label not found '<label>'.` to the
  ordinary one-argument, non-important TVP log wrapper, then returns `false`;
- total-frame query: returns `0.0` without logging;
- existing non-loop state (`loopBegin < 0.0`): loop query returns false and
  total-frame query returns `0.0`, regardless of the stored `lastTime`;
- existing loop state (`loopBegin >= 0.0`): returns true / the exact stored
  `lastTime`; and
- D3D total-frame API then converts that double to its signed integer result,
  truncating finite fractional values toward zero under the target C++ ABI.

The comparison is the ordinary ordered floating-point `>= 0.0`: negative zero
passes and NaN fails.  No `timelineData` pointer check occurs in either helper.
Once that comparison passes, the total-frame helper returns `lastTime` raw,
including negative, fractional, NaN, or infinite values.

## Per-target differences

The four helpers have the same lookup order, comparison, log-false-vs-zero
split, and return fields.  Differences are limited to:

- object and compound-value offsets shown above;
- string reference-count instructions and exception-unwind format;
- hard-float double return/conversion details; and
- code addresses.

iOS builds expose small separate cleanup functions for the temporary `ttstr`;
those are compiler-generated unwind helpers, not additional plugin methods.

## Local source and tests

The pre-audit source modeled the hit decisions correctly, but the internal
names and inline address comments were stale and its miss used
`TVPThrowExceptionMessage`. The helpers are now
`getLoopTimeline_guess` and `getTimelineTotalFrameCount_guess` throughout both
the D3D and `Motion.EmotePlayer` facades. Their Engine label parameter is restored
to by-value, `Motion.EmotePlayer.getLoopTimeline` returns native bool rather than
constructing a Variant explicitly, and the miss now concatenates the diagnostic,
logs it, and returns false. Unit coverage fixes non-inserting miss behavior,
non-loop suppression, stored loop total, negative-zero/NaN `loopBegin`, and raw
negative/NaN/infinite `lastTime` propagation.

The full 2026-08-15 owner/log-callee correction and D3D conversion instruction
record is in
`analysis/motionplayer_loop_total_log_miss_value_abi_four_binary_2026-08-15.md`.
