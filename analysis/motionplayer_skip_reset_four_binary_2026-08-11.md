# MotionPlayer `skip` / controller-reset four-binary recovery (2026-08-11)

## Scope

This note records a fresh recovery of the `Motion.EmotePlayer.skip` and
`D3DEmotePlayer.skip` call chains in all four current reference binaries.  Both
facades reach the same Engine controller-reset operation.  The audit also
follows the active-timeline lookup far enough to distinguish lookup-only access
from `unordered_map::operator[]`, because that distinction is observable when
the active-label vector and timeline map are inconsistent.

## Four-target map

| Target | EmotePlayer registrar / site | Engine reset | active-timeline phase | HM3 subscript | `skip` UTF-16 | D3D registrar / site | D3D wrapper |
|---|---:|---:|---:|---:|---:|---:|---:|
| Android ARM64 | `0x67CEA8` / `0x67E2CC` | `0x66BF6C` | `0x6670F0` | `0x685060` | `0x14BEE58` | `0x52E8E4` / `0x52FADC` | `0x530E24` |
| Android ARMv7 | `0x5612E8` / `0x5617F2` | `0x558888` | `0x555B4C` | `0x5669AC` | `0xD76C34` | `0x494078` / `0x49449A` | `0x49500E` |
| iOS ARM64 | `0x1001B5130` / `0x1001B58C8` | `0x1001AB03C` | `0x1001A6844` | `0x1001A6938` | `0x1019606FC` | `0x100232278` / `0x100232930` | `0x100233458` |
| iOS ARMv7 | `0x1B4DE0` / `0x1B54DC` | `0x1AA714` | `0x1A5FC0` | `0x1A6074` | `0x1752A60` | `0x230F46` / `0x23158A` | `0x23219E` |

The EmotePlayer registrars bind the Engine reset body directly; there is no
EmotePlayer-specific `skip` wrapper.  The D3D registrars bind a one-hop wrapper
which follows the D3DEmotePlayer native instance to its primary EmoteObject and
then to the same Engine:

```text
64-bit: reset(*( *(D3D + 24) + 8))
32-bit: reset(*( *(D3D + 16) + 4))
```

The wrapper therefore requires a loaded primary object.  It contains no null
check, no secondary-object fallback, and no `modified` write.

## Common reset data flow

The four reset bodies have the same source-level sequence.  iOS outlines every
large phase, Android ARMv7 outlines most phases, and Android ARM64 inlines the
deque walks into the reset body; those are compiler differences rather than
semantic differences.

1. Walk the active timeline-label vector.
2. Subscript HM3 with the current label.
3. For a state whose `loopBegin >= 0`, test the blend-controller owner in the
   active-timeline phase itself; reset it only when non-null, and keep the label
   in the vector.
4. For a state whose `loopBegin < 0`, apply the terminal timeline window with
   `(force=true, time=lastTime)` and erase the current label without advancing
   the index.
5. Reset the three direct outer-force owners in declaration order: bust, hair,
   then parts.
6. Set both re-arm bytes on every hair/parts spring node.
7. Set both re-arm bytes on every bust-chain-1 spring node.
8. Set both re-arm bytes on every bust-chain-2 spring node.
9. Reset every blink controller.
10. Reset every eyebrow controller.
11. Reset every mouth controller.
12. Reset every selector controller.
13. Reset every transition controller.
14. Reset position, scale, angle, and color controllers, in that order.

The selector-before-transition order is observable: applying the selector's
final selection can enqueue work into borrowed transition controllers, and the
following transition phase immediately commits and clears that work.

## Outlined phase maps

Android ARM64 keeps the three spring walks and the five per-entry controller
walks in the main reset body.  Its active phase is `0x6670F0`; common controller
helpers used by the inline walks include Var `0x66451C`, blink `0x660E80`,
eyebrow `0x6628A4`, selector `0x665774`, and the final base-controller group
`0x66780C`.

Android ARMv7 uses:

| Phase | Address |
|---|---:|
| active timelines | `0x555B4C` |
| three target controllers | `0x555DE0` |
| three spring walks | inline in `0x558888` |
| blink | `0x555E92` |
| eyebrow | `0x555EC6` |
| mouth | `0x555EFA` |
| selector | `0x555F2E` |
| transition | `0x555F62` |
| base controllers | `0x555F96` |

iOS ARM64 uses:

| Phase | Address |
|---|---:|
| active timelines | `0x1001A6844` |
| three target controllers | `0x1001A6E38` |
| hair/parts spring walk | `0x1001A6E68` |
| bust-chain-1 spring walk | `0x1001A6F08` |
| bust-chain-2 spring walk | `0x1001A6FB0` |
| blink | `0x1001A7058` |
| eyebrow | `0x1001A70E8` |
| mouth | `0x1001A7178` |
| selector | `0x1001A7228` |
| transition | `0x1001A72D8` |
| base controllers | `0x1001A7388` |

iOS ARMv7 uses the corresponding sequence at `0x1A5FC0`, `0x1A656C`,
`0x1A658E`, `0x1A6610`, `0x1A6670`, `0x1A66D0`, `0x1A6724`, `0x1A6778`,
`0x1A6808`, `0x1A6880`, and `0x1A6912`.

## Controller queue boundary behavior

- A Var controller with queued keyframes commits every channel from the last
  queued destination, sets state to idle, and clears the full queue.  With an
  empty queue but active state, it commits the controller's destination-side
  array and sets state to idle.  Idle plus empty queue is a no-op.
- Blink and eyebrow controllers first prefer the last value-track keyframe.  If
  that track is empty but the controller is active, they use the auxiliary
  track's last value when present, otherwise the stored target.  Both owned
  tracks are cleared on the relevant path.
- Mouth commits the last queued value, or the stored end value when active with
  no queued value.
- Selector converts the last queued float to an integer selection and applies
  it with zero duration/easing, or reapplies `selectedIndex` when active with an
  empty queue.
- Angle commits the last queued angle without normalization.  Only the
  active-with-empty-queue path normalizes the stored target into `[0, 6.2832)`
  using the truncated `6.2832f` literal.

## HM3 subscript edge behavior

All four active-timeline helpers call an inserting HM3 subscript helper.  On a
miss it allocates a new hash node, retains the `ttstr` key, zero-initializes the
mapped state, and seeds `blendWeight` to `1.0f`.  The returned mapped value is
then processed normally.  A missing active label consequently remains active:
its freshly materialized state has `loopBegin == 0`, a null blend controller,
and takes the keep-label branch.

The ABI-specific node sizes are `0x88` on both 64-bit targets, `0x70` on Android
ARMv7, and `0x60` on iOS ARMv7.  Android uses libstdc++ hash/deque layouts while
iOS uses libc++; the insertion and lifetime semantics are common.

## Local comparison and applied correction

The local `resetControllers_guess` already matches the phase order and the
queue commit/clear rules above.  One line did not match: it used
`_compoundHM3_936.at(activeLabel)`.  That lookup throws on an inconsistent
active-label vector, whereas every reference calls the inserting subscript
operation.  The implementation now uses `operator[]` and a Catch2 regression
case verifies that reset materializes a default HM3 entry, retains the active
label, and does not synthesize a blend controller.

The local EmotePlayer and D3DEmotePlayer `skip` methods both call the same Engine
reset and neither marks the D3D object modified, matching the recovered chains.

The 2026-08-15 fresh pass also moved the blend-owner null gate back into the
active-timeline Engine phase.  Calling a locally null-tolerant reset helper with
`nullptr` had the same result but introduced a call that none of the four
references makes.  Existing missing-active-state coverage fixes the null-owner
keep-label boundary.

## IDB improvements and verification

All four IDBs now name and type the active-timeline phase as
`EmoteEngine_resetActiveTimelines_guess`, the inserting map helper as
`EmoteTimelineMap_subscript_guess`, and the D3D wrapper as
`D3DEmotePlayer_skip_guess`.  The EmotePlayer UTF-16 string is named
`aSkip_utf16_guess`.  Fresh post-type decompilation was taken for all three
functions in every target before the databases were saved.

After this correction, both `cmake --build --preset "Web Debug Build"` and
`cmake --build --preset "Wasmtime Headless Debug Build"` rebuild and link
successfully; a second incremental invocation reports `ninja: no work to do`
for both presets. `git diff --check` reports no whitespace errors. The focused
native Catch2 result is recorded after the still-running first-time Windows
vcpkg test configuration completes.
