# Motion.EmotePlayer #53–56 raw timeline callbacks and #59 direct blend getter

Date: 2026-08-16

## 1. Conclusion

Fresh registrar and callback recovery in all four current reference binaries corrects two
source-structure errors inherited by the port:

1. Primary members #53 `playTimeline`, #54 `stopTimeline`, #55
   `getTimelinePlaying`, and #56 `setTimelineBlendRatio` use the same NCBind
   **native-instance raw-callback** descriptor family as #57/#58 and #14–19. They are not
   ordinary generated typed members and there are no corresponding two-argument C++ façade
   methods in the original registration path.
2. #59 `getTimelineBlendRatio` is the opposite boundary: its generated one-`ttstr`/double
   descriptor stores the Engine getter itself with member adjustment zero. The Primary
   forwarding method in the port was synthetic.

The distinction is observable. #53 makes flags optional; #54 and #55 even make label
optional; raw results are not cleared like typed `void` results; and #56 has an asymmetric
first-call state machine that is not equivalent to `setBlend(label, ratio, 0, 1, false)`.

## 2. Registrar coordinates and stored targets

Descriptor/factory call sites:

| Member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| #53 `playTimeline` | `0x67E330` | `0x561810` | `0x1001B5900` | `0x1B5512` |
| #54 `stopTimeline` | `0x67E378` | `0x561820` | `0x1001B591C` | `0x1B552E` |
| #55 `getTimelinePlaying` | `0x67E3D8` | `0x561830` | `0x1001B5938` | `0x1B554A` |
| #56 `setTimelineBlendRatio` | `0x67E438` | `0x561840` | `0x1001B5954` | `0x1B5566` |
| #59 `getTimelineBlendRatio` | `0x67E55C` | `0x56186E` | `0x1001B59AC` | `0x1B55BC` |

Stored native targets:

| Recovered semantic name | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmotePlayer_playTimeline_rawCallback_guess` | `0x670224` | `0x55A9EC` | `0x1001ADD74` | `0x1AD460` |
| `EmotePlayer_stopTimeline_rawCallback_guess` | `0x67F38C` | `0x562130` | `0x1001B6228` | `0x1B6008` |
| `EmotePlayer_getTimelinePlaying_rawCallback_guess` | `0x67F47C` | `0x5621C8` | `0x1001B62D8` | `0x1B6108` |
| `EmotePlayer_setTimelineBlendRatio_rawCallback_guess` | `0x670674` | `0x55AB8C` | `0x1001ADFB0` | `0x1AD72C` |
| `EmoteEngine_getTimelineBlendRatio_guess` | `0x67F5A8` | `0x562270` | `0x1001B6398` | `0x1B6218` |

The 32-bit registrars pass `a4=0` at each raw factory call. iOS arm64 shows the same zero
callback adjustment in the factory call. Android arm64 materializes each callback into the
descriptor and clears its adjustment word at `+0x38`; #59 clears the full member-adjustment
slot at `+0x30`. Thus the callback's fourth argument and #59's member receiver are both the
Engine-based Primary native payload without an embedded-Player hop.

## 3. Shared raw descriptor boundary

#53–56 all receive the native callback ABI:

```text
callback(result, numparams, params, nativePrimaryPayload)
```

The generated descriptor resolves the native receiver before entering the callback. The
callback bodies themselves do not perform `objthis` lookup. Their shared lifetime boundary is:

```text
argc gate/default decision
construct owned ttstr if the selected branch needs it
convert only the argv slots consumed by that branch, left to right
call Engine while the label owner is alive
destroy label
return TJS_S_OK (or early TJS_E_BADPARAMCOUNT)
```

Unlike an ordinary typed `void` member wrapper, #53/#54/#56 do not clear or otherwise write
`result`. #55 deliberately calls the Boolean Variant assignment helper unconditionally; the
native body has no null-result branch.

## 4. Exact callback behavior

### #53 `playTimeline`

```text
if argc < 1:
    return BADPARAMCOUNT
label = owned ttstr(param[0])
flags = argc >= 2 ? uint32(param[1].AsInteger()) : 0
Engine.playTimeline(label, flags)
destroy label
return OK                         // result untouched; surplus ignored
```

The signed TJS integer's low 32-bit value is passed as the Engine flag word. A label-only call
is therefore valid and does not take replace mode. When flags bit 0 is supplied, Engine clears
the active vector before its non-inserting HM3 lookup; a miss logs and does not roll that clear
back.

### #54 `stopTimeline`

```text
label = canonical empty ttstr
if argc >= 1:
    tmp = owned ttstr(param[0])
    label = tmp                   // CopyRef/assignment
    destroy tmp
Engine.stopTimeline(label)
destroy label
return OK                         // result untouched; surplus ignored
```

Zero arguments are valid and mean “stop all timelines.” This differs from a generated typed
one-`ttstr` wrapper, which would reject the call before entering Engine.

### #55 `getTimelinePlaying`

It constructs the same optional label as #54. Empty label asks whether any timeline is active;
a nonempty label performs the normal linear membership test. The Boolean is assigned through
the supplied result pointer, then the owned label is destroyed. Surplus arguments are ignored.

### #56 `setTimelineBlendRatio`

The exported script name is misleading if interpreted as a simple ratio setter. All four
bodies implement this state machine:

```text
if argc < 1:
    return BADPARAMCOUNT
label = owned ttstr(param[0])

if !Engine.isTimelinePlaying(label):
    Engine.playTimeline(label, 3)
    Engine.setBlend(label, value=0, duration=0, power=1, autoStop=false)
    destroy label
    return OK                     // param[1..] were never inspected

duration = argc >= 2 ? float(param[1].AsReal()) : 0
scriptEase = argc >= 3 ? param[2].AsReal() : 0
power = scriptEase == 0 ? 1
      : scriptEase > 0  ? scriptEase + 1
                        : 1 / (1 - scriptEase)
power = float(power)              // mapping is double-domain, then narrowed
autoStop = argc >= 4 ? bool(param[3]) : false
Engine.setBlend(label, value=1, duration, power, autoStop)
destroy label
return OK                         // result untouched; surplus ignored
```

Consequently the first call for a dormant known timeline only plays and seeds blend zero; a
later call, after the label is active, enqueues blend one. An unknown inactive label still
takes `play(flags=3)`, so it can clear the active vector before the logged miss, then performs
one silent zero-blend HM3 miss. It never evaluates optional duration/ease/autoStop on that
branch. This is also not the same as adjacent #57 `fadeInTimeline`, which always enqueues the
final blend-one target after its play/seed prefix.

## 5. #59 typed direct Engine getter

#59 reuses the already recovered one-by-value-`ttstr`/double Function family. The wrapper
enforces minimum argc one, owns and destroys the converted `ttstr`, ignores surplus, invokes
the Engine member with adjustment zero, and publishes its double as `tvtReal`. The core uses a
non-inserting HM3 find:

```text
state = timelineStates.find(label)
if state exists and state.timelineData != null:
    return double(state.blendWeight)    // native field is float
return 0.0
```

The nontrivial by-value source parameter is lowered to an invisible address by the native ABI;
the pointer-shaped decompiler argument is not evidence for a `const ttstr &` source signature.

## 6. Android arm64 IDA boundary artifact

Android arm64 contains a normal prologue at `0x670674`, the exact address taken by the #56
registrar. Code there implements the same callback as the three independent non-A64 functions:
argc gate, receiver in `X3`, owned label, inactive play/zero-seed early return, and the active
optional-argument path. The current IDA database nevertheless owns `0x670674..0x670938` as a
contiguous tail of `EmoteEngine_playTimeline_guess @ 0x670350`, so requesting decompile at the
callback entry returns the parent Engine function. This is an analysis-boundary mismerge, not
a real source-level shared function. The recovery IDB therefore names and comments the
`0x670674` code label explicitly and records the registrar proof; it does not use the polluted
parent pseudocode as callback evidence.

## 7. Source reconciliation and validation

Portable source now:

- registers #53–56 with `NCB_METHOD_RAW_CALLBACK` and explicit native-instance callbacks;
- removes the four synthetic typed façade methods;
- preserves the callback-specific argc/default/result/owner and #56 branch behavior;
- registers #59 directly to `EmoteEngine::getTimelineBlendRatio_guess`;
- removes the synthetic #59 Primary forwarder and gives the Engine member its original
  by-value `ttstr` signature.

The regression uses a real Primary adaptor and locks bad-count behavior, raw result retention,
optional empty labels, replace-mode miss commit, #56 first/second-call asymmetry, ease mapping,
autoStop, surplus ignore, and #59 `tvtReal` publication. Emscripten syntax-only validation with
the Web Debug argument set passes with only the repository's existing `_tss` warning. The full
10-step Web Debug build and final `index.html` link pass as well. A targeted search confirms no
Primary typed façade or old `NCB_METHOD` registration remains for #53–56/#59 while all four raw
registrations and the #59 direct `Method` are present; the relevant-file `git diff --check` also
passes. All four IDBs were force-recompiled/read back after the semantic names, types, comments
and bookmarks were installed, then saved in place.
