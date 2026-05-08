# ADB + Frida Oracle Runner

Runs libkrkr2.so (the Android kirikiroid2 binary) inside the repacked
`krkr2-harness.apk` on a real Android arm64 device or Redroid
container, driven from the host over `adb forward tcp:5039` +
`am start HarnessActivity`. Provides two established assertion layers
against the port:

1. **Return-value diff** — the host pokes function calls into
   [libharness.so](harness/) loaded by the APK, reads return values,
   compares against the spec's `"expected"`.
2. **Call-sequence diff** — optional per-case Frida tracer attaches to
   the `HarnessActivity` process, hooks a curated set of sub-function
   offsets, and verifies the runtime event stream matches a checked-in
   golden at `tests/differential/traces/<family>/<case_id>.trace.json`.

Any divergence between the port's output and libkrkr2's output — either
at the return-value or at a sub-call — surfaces at CI time as a PR
failure.

For `motion_playback`, this directory also contains a libkrkr2-side
recording path that captures Motion.Player per-frame state from natural
playback on the cocos2d GL thread. That path is useful as a state oracle,
but it is not yet a final visual oracle: it does not capture the
framebuffer, draw commands, texture identity, shader/blend state, or
pixel output.

## Status

| Family | Oracle path | Goldens | Notes |
|---|---|---|---|
| `geometry_hit_test` | **✓ 10/10** | **✓ 10** | `Player_hitTest` (0x690DF0), pure C leaf |
| `local_transform` | **✓ 8/8** | **✓ 8** | `sub_699940` (0x699940), libm sin/cos used by `rotate_90` |
| `bezier_curve` | **✓ 6/6** | **✓ 6** | `sub_69A754` (0x69A754). `empty_curve` + `size_mismatch` specs dropped — UB inputs (empty or mismatched arrays) where libkrkr2's behaviour is an OOB-read side effect / infinite loop rather than a designed contract; oracle doesn't apply |
| `position_interp` | **✓ 5/5** | **✓ 5** | `sub_69A4D4` (0x69A4D4). Adapter had `src_addr`/`dst_addr` wired into a2/a3 — libkrkr2's convention (matching port's `interpolatePosition69A4D4` signature) is a2=dst (returned at t=1), a3=src (returned at t=0). `rotation_coord*` specs dropped — empty `segments` arrays SIGSEGV inside libkrkr2's `sub_698454` (latent libkrkr2 bug, never hit by real assets); port's defensive sanitisation is intentionally non-matching |
| `psb_rl_decompress` | — | — | RL loop is inlined in a 53 KB PSB loader; no standalone entry, no adapter |
| `motion_playback` | libkrkr2 record + Wasmtime verify | **✓ 2** | Uses `STARTUP_FROM` to schedule `reference/xp3/logo_test_oracle.xp3` inside libkrkr2, then Frida hooks `Motion.Player.progress` / `Player_updateLayers` to record per-frame Motion node state for `yuzulogo.mtn` and `m2logo.mtn`. Checked-in goldens exist under `tests/differential/traces/motion_playback/*.oracle.json`; the port-side verifier runs the Wasmtime guest, executing the same `startup.tjs` path with a headless Window stub. This is not yet a full visual oracle; see "Motion playback visual oracle status" below. |

## Motion playback visual oracle status

Target goal: use `tests/differential` to prove that the current port's
final visual output while playing `reference/xp3/logo_test/yuzulogo.mtn`
and `reference/xp3/logo_test/m2logo.mtn` matches libkrkr2.so.

Current oracle-runner side status:

- The runner can launch the real repacked Android APK and execute
  libkrkr2 on the same cocos2d/Java activity path used by the original
  app.
- `libharness.so` exposes `STARTUP_FROM <utf8_hex_path>`, constructs a
  real gnustl `std::string`, and calls
  `TVPMainScene::startupFrom(const std::string&)`. This avoids the old
  Python-side fake `std::string` ABI risk.
- The recording fixture is `reference/xp3/logo_test_oracle.xp3`, a
  deterministic wrapper around the `logo_test.xp3` playback path. It
  preserves the KAGParser `[ev]` / `[ev waitmovie]` boundary and drives
  `.mtn` playback through `AffineLayer` / `AffineSourceMotion` / `onPaint`,
  using fixed delta timing so the recorded segments match the specs.
- `FridaMotionTracer` attaches to the harness process and the in-process
  JS agent hooks `Motion.Player.progress` and `Player_updateLayers`.
  Recording happens from natural playback on the GL thread; the host
  does not call Motion.Player methods from the RPC worker thread.
- The checked-in oracle files currently contain non-empty per-frame
  node state for both fixtures:
  `tests/differential/traces/motion_playback/yuzulogo.oracle.json` and
  `tests/differential/traces/motion_playback/m2logo.oracle.json`.

What this proves today:

- It can produce a libkrkr2 baseline for Motion node evaluation:
  per-frame node count, node type, visibility/active flags, flip flags,
  accumulated position, scale, angle, opacity, and a limited blend-mode
  proxy.
- It is suitable as a state oracle for debugging the port's
  `Motion.Player` timeline and `Player_updateLayers` behaviour.

What it does not prove yet:

- It does not capture final framebuffer pixels or screenshots.
- It does not capture a complete draw-command stream, draw order
  contract, GL state, shader inputs, texture upload/sampling, clipping,
  mask/stencil behaviour, or blend results.
- `label` and `currentImage` in the current motion oracle schema are not
  populated with authoritative runtime names/textures, so texture
  identity and source image selection are not covered.
- The deterministic wrapper now uses the same AffineSourceMotion playback
  path as `logo_test.xp3`, but still fixes delta timing; it does not prove
  the original wall-clock timing behaviour.
- Normal push CI validates the Wasmtime port trace against the checked-in
  `motion_playback` goldens. Re-recording those goldens from libkrkr2
  remains opt-in via `run_motion_playback.py --record-oracle`.

Therefore, as of now, the oracle runner side is good enough to be a
libkrkr2 Motion state oracle for these two fixtures, but not enough to
claim final visual output equivalence. Reaching that goal requires
adding either framebuffer/pixel capture or a
render-command oracle that covers texture identity, draw order, clipping,
blend/stencil state, and final compositing.

## Motion tracer equivalence model

The Android Frida tracers and the macOS LLDB tracers are comparable only
as stage-specific semantic projections, not as proof that the two
runtimes share the same physical object layout.

- Android/Frida is the oracle side. It attaches to `libkrkr2.so`, sets
  breakpoints or interceptors by binary address/offset, and reads fields
  from raw process memory using the libkrkr2 layout recovered from IDA.
- macOS/LLDB is the port side. It launches the full native engine,
  breaks on local C++ functions or source lines, and reads fields through
  debug symbols, typed expressions, or local helper projections.
- A field is considered comparable only when both tracers sample at the
  same logical stage boundary and project the same runtime meaning into
  the same JSON schema field. The comparison does not require identical
  pointer values, absolute sequence numbers, addresses, padding, STL
  layout, or private native-only bookkeeping.
- When adding or changing a motion stage tracer, document the hook point,
  the sampled object, and the field projection for both sides. If either
  side uses a fallback hook or a derived field, the stage is diagnostic
  only until the timing and projection are made explicit.

For the 6-stage motion playback diagnostics, this means `static_parse`,
`init_motion`, `variable_binding`, `frame_selection`,
`sub_motion_decision`, and `trace_flatten` must each define their own
sampling boundary. A passing diff means the two tracers observed
equivalent stage outputs for the fixture; it must not be read as evidence
that the port has reproduced libkrkr2's in-memory layout byte-for-byte.

`trace_flatten` uses the `trace_flatten-semantic-v1` projection sampled at
`progressCompat.phase3-end.pre-cleanup`. Its comparable layer fields are:
`index`, `nodeType`, `visible`, `active`, `flipX`, `flipY`, `posX`,
`posY`, `posZ`, `angleDeg`, `scaleX`, `scaleY`, `slantX`, `slantY`,
`opacity`, and `stencilType`. The `stencilType` field is Android
`node+52` and native `MotionNode::stencilType`; it is intentionally not
named `blendMode` in the staged oracle schema.

Pointer values, `objthis`, `topPlayer`, player source ranges, traversal
layout, trace errors, and unsupported names/images are diagnostics only.
They may be stored under `diagnostics` for segmentation and debugging,
but they are not semantic `trace_flatten` diff fields.

## Prerequisites

**libkrkr2.so + supporting libs** — private `reference` git submodule:

```bash
git submodule update --init reference    # requires PRIVATE_SUBMODULE_PAT
# Provides:
#   reference/libkrkr2/libkrkr2.so
#   reference/lib/libSDL2.so
#   reference/lib/libffmpeg.so
```

**Android device / Redroid** — API 24+ arm64-v8a. The ADB runners need
root so `frida-server` can attach to the non-debuggable APK. Current CI
uses `ubuntu-24.04-arm` with Redroid (`redroid/redroid:12.0.0_64only`)
so Android runs as an arm64 container sharing the host kernel. Local
development can use either Redroid or a rooted arm64 emulator.

**Harness APK** — the repacked `krkr2-harness.apk` contains
`libharness.so` (arm64, NDK r17c + `gnustl_static`) and a minimal `HarnessActivity`
that extends `Cocos2dxActivity`. Build with
[harness-apk/build.sh](harness-apk/build.sh); rebuild instructions for
the native .so live in [harness/README.md](harness/README.md).

**Python deps**:

```bash
pip install -r tests/differential/oracle_runner/requirements-oracle.txt
# → frida==16.4.10 (only needed when using --trace / --record-trace)
pip install -r tests/differential/python/requirements-wasm.txt
```

**Frida server** (for `--trace` and `motion_playback --record-oracle`
mode) — pinned to match `frida-python`:

```bash
# Operator step, idempotent
curl -L -o /tmp/frida-server.xz \
  https://github.com/frida/frida/releases/download/16.4.10/frida-server-16.4.10-android-arm64.xz
xz -d /tmp/frida-server.xz
mv /tmp/frida-server tools/bin/android/frida-server
```

## Running

### One-time provisioning on device

```bash
export PATH=$ANDROID_SDK_ROOT/platform-tools:$PATH
adb root && adb wait-for-device
adb push reference/libkrkr2/libkrkr2.so   /data/local/tmp/
adb push reference/lib/libSDL2.so         /data/local/tmp/
adb push reference/lib/libffmpeg.so       /data/local/tmp/
adb push tools/bin/android/frida-server   /data/local/tmp/
adb shell "chmod 755 /data/local/tmp/frida-server"
adb shell "nohup /data/local/tmp/frida-server -D >/dev/null 2>&1 &"

# Build + install the harness APK (packages libharness.so inside).
export KRKR2_LEGACY_NDK=/path/to/android-ndk-r17c
./tests/differential/oracle_runner/harness/build_legacy.sh
./tests/differential/oracle_runner/harness-apk/build.sh
adb install -r tests/differential/oracle_runner/harness-apk/prebuilt/krkr2-harness.apk
```

### Return-value diff only (no Frida)

```bash
python3 tests/differential/python/run_geometry_hit_test_adb.py \
  --spec-dir tests/differential/specs/geometry_hit_test
```

Output: one JSON line per case on stdout (`runner: android-adb-oracle`);
mismatches on stderr; exit 0 iff all match.

### With Frida trace verification (recommended in CI)

```bash
# --trace   : verify runtime call sequence matches golden on disk
# --record-trace: overwrite goldens from the current run (golden produ-
#                 cer; use only when libkrkr2 is the new source of truth)
python3 tests/differential/python/run_bezier_curve_adb.py \
  --spec-dir tests/differential/specs/bezier_curve --trace
```

Without either flag the tracer stays off and `frida` is not even
imported — default runs have zero overhead.

On mismatch the runner prints, with the first divergent step:

```
TRACE MISMATCH single_segment_mid:
step 12: addr differs (sub_69A754 vs sub_698454)
  golden:  enter sub_69A754 depth=1 x0=<ptr> d0=0.5
  runtime: enter sub_698454 depth=1 x0=<ptr> d0=0.5
```

### Motion playback oracle recording

`motion_playback` is recorded from live libkrkr2 rather than by a scalar
`CALL`. It starts the APK harness, pushes
`reference/xp3/logo_test_oracle.xp3`, calls `STARTUP_FROM`, and records
the natural GL-thread playback with the specialised Frida motion tracer.

```bash
python3 tests/differential/python/run_motion_playback.py \
  --record-oracle --serial "$ANDROID_SERIAL"
```

The command writes:

```text
tests/differential/traces/motion_playback/yuzulogo.oracle.json
tests/differential/traces/motion_playback/m2logo.oracle.json
```

Treat those files as libkrkr2 Motion state goldens, not as final visual
goldens.

## Architecture

```
oracle_runner/
├── adb_engine.py       AdbHarnessEngine: pushes libs, launches
│                       HarnessActivity, speaks line-based RPC over a
│                       forwarded TCP socket, tracks pid for Frida attach.
├── arm64_abi.py        AAPCS64 register/stack packing (x0-x7, d0-d7)
├── guest_heap.py       Bump allocator at fixed guest VA 0x50000000
├── stl_layout.py       HitData / Affine2x3 builders
├── frida_tracer.py     FridaTracerEngine: attach to HarnessActivity pid,
│                       load agent.js, expose start_case/stop_case
├── frida_agent.js      Per-target `Interceptor.attach` recording x0-x7
│                       + d0-d7 at entry; x0/d0 at exit
├── frida_motion_agent.js
│                       Motion.Player progress/updateLayers recorder used
│                       only by motion_playback oracle recording.
├── frida_motion_tracer.py
│                       Host-side wrapper for frida_motion_agent.js.
├── trace_targets.py    Per-family target offsets + arity + return-kind
├── trace_diff.py       Golden read/write + first-divergence diff
├── adapters/           Per-family case-to-CALL translation
│   ├── geometry_hit_test.py
│   ├── local_transform.py
│   ├── bezier_curve.py
│   ├── position_interp.py
│   └── motion_playback.py
├── harness/            Native side of the harness (see harness/README.md)
│   ├── harness.cpp
│   ├── jni_bridge.cpp
│   ├── Android.mk / Application.mk
│   ├── build_legacy.sh
│   └── prebuilt/libharness.so
└── harness-apk/        APK wrapper around libharness.so (see harness-apk/README.md)
    ├── build.sh
    └── HarnessActivity.java
```

`run_*_adb.py` (siblings of `run_*_wasmtime.py`) instantiate
`AdbHarnessEngine` once, iterate specs, and optionally attach a
`FridaTracerEngine` configured with the family's target offset list.
`run_motion_playback.py --record-oracle` uses `FridaMotionTracer`
instead because it records a continuous playback rather than a single
leaf-function call.

## Implementation notes

**Pointer canonicalisation** — raw x-register values ≥ `0x1_0000_0000`
(bionic heap, libkrkr2 text, stack, TLS) are replaced with `<ptr>` at
normalisation time. Values below (our deterministic GuestHeap at
`0x50000000`, small scalars, bools) stay raw. Without this the trace
diff fires on every ASLR reshuffle.

**Arity masking** — AAPCS64 leaves unused argument registers in
whatever state the caller wrote last. Per-target `ARG_COUNTS` in
[trace_targets.py](trace_targets.py) caps the meaningful x/d register
count; beyond that we emit `-`. The return-value half uses
`RETURN_KINDS` to decide whether `x0` (int/pointer return) or `d0`
(double return) carries signal.

**Crash resilience** — `AdbHarnessEngine.is_alive()` polls the child
process; on SIGSEGV inside libkrkr2 the runner calls `restart()`,
re-spawns the harness, and re-attaches Frida. Crash cases produce no
golden trace (the script is torn down with the process); the tracer's
`stop_case()` swallows `frida.InvalidOperationError` so the adapter's
exception surface reflects the real crash, not the Frida-side
teardown.

**Script destroyed errors** — if you see `InvalidOperationError` from
`stop_case`, it means the target died mid-case *and* the canonical
swallow path didn't trigger. Verify `frida-server` on the device
matches the pinned `frida-python` version.

## Follow-ups

- Port-side tracer — instrument the wasm build to emit the same event
  schema, run a true libkrkr2-vs-port sequence diff (currently the
  golden freezes libkrkr2-side only)
- Visual motion oracle — capture either final framebuffer pixels or a
  complete draw-command stream for `yuzulogo.mtn` / `m2logo.mtn`,
  including texture identity, draw order, clipping, blend/stencil state,
  and compositing. This is required before claiming complete visual
  equivalence.
- `psb_rl_decompress` — needs static extraction of the RL loop from
  `sub_695DE8`, not in scope
- Richer target lists — hook `iTJSDispatch2::PropGet` call-sites
  inside `sub_69A754`/`sub_69A4D4` if the leaf-only coverage proves
  insufficient
