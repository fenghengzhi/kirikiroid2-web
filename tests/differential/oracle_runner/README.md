# ADB + Frida Oracle Runner

Runs libkrkr2.so (the Android kirikiroid2 binary) inside a real Android
arm64 emulator, driven from the host over `adb shell`. Provides two
layers of assertion against the WASM port:

1. **Return-value diff** — the host pokes function calls into a tiny
   [harness binary](harness/) running on the device, reads return
   values, compares against the spec's `"expected"`.
2. **Call-sequence diff** — optional per-case Frida tracer attaches to
   the same harness process, hooks a curated set of sub-function
   offsets, and verifies the runtime event stream matches a checked-in
   golden at `tests/differential/traces/<family>/<case_id>.trace.json`.

Any divergence between the port's output and libkrkr2's output — either
at the return-value or at a sub-call — surfaces at CI time as a PR
failure.

## Status

| Family | ADB calls | Golden traces | Notes |
|---|---|---|---|
| `geometry_hit_test` | **✓ 10/10** | **✓ 10** | `Player_hitTest` (0x690DF0), pure C leaf |
| `local_transform` | **✓ 8/8** | **✓ 8** | `sub_699940` (0x699940), libm sin/cos used by `rotate_90` |
| `bezier_curve` | **✓ 6/6** | **✓ 6** | `sub_69A754` (0x69A754). `empty_curve` + `size_mismatch` specs dropped — UB inputs (empty or mismatched arrays) where libkrkr2's behaviour is an OOB-read side effect / infinite loop rather than a designed contract; oracle doesn't apply |
| `position_interp` | **✓ 5/5** | **✓ 5** | `sub_69A4D4` (0x69A4D4). Adapter had `src_addr`/`dst_addr` wired into a2/a3 — libkrkr2's convention (matching port's `interpolatePosition69A4D4` signature) is a2=dst (returned at t=1), a3=src (returned at t=0). `rotation_coord*` specs dropped — empty `segments` arrays SIGSEGV inside libkrkr2's `sub_698454` (latent libkrkr2 bug, never hit by real assets); port's defensive sanitisation is intentionally non-matching |
| `psb_rl_decompress` | — | — | RL loop is inlined in a 53 KB PSB loader; no standalone entry, no adapter |
| `motion_playback` | scaffold | pending | Drives `Motion.Player` end-to-end via the new `TJS_EXEC_STR` harness command, snapshots per-frame layer state, diffs against a host-native `motion_playback_port` CLI. Specs land for `m2logo` / `yuzulogo` to guard the Phase 2 `PlayerRender.cpp:1000` drawFlag-gate fix. Oracle JSONs need to be recorded once in a Redroid env (see [run_motion_playback.py](../python/run_motion_playback.py) `--record-oracle`); port CLI emits structurally valid frames but only fills accumulated state once the headless `updateLayers()` crash is debugged (the public `Player::runUpdatePassForOracle()` hook is in place for that follow-up) |

## Prerequisites

**libkrkr2.so + supporting libs** — private `reference` git submodule:

```bash
git submodule update --init reference    # requires PRIVATE_SUBMODULE_PAT
# Provides:
#   reference/libkrkr2/libkrkr2.so
#   reference/lib/libSDL2.so
#   reference/lib/libffmpeg.so
```

**Android emulator** — API 24+ arm64-v8a google_apis image. The 4
ADB runners need a rooted emulator so `adb push` can drop binaries
into `/data/local/tmp/` and the harness can talk stdin/stdout over
`adb shell`. Locally we use an AVD named `oracle-arm64`; see
`.github/workflows/differential.yml` for the CI version
(`reactivecircus/android-emulator-runner@v2`, `macos-14` host for
HVF-accelerated arm64).

**Harness binary** — checked in at
[harness/prebuilt/harness-aarch64](harness/prebuilt/harness-aarch64)
(450 KB, arm64 PIE, NDK-built). Rebuild instructions are in
[harness/README.md](harness/README.md).

**Python deps**:

```bash
pip install -r tests/differential/oracle_runner/requirements-oracle.txt
# → frida==16.4.10 (only needed when using --trace / --record-trace)
```

**Frida server** (for `--trace` mode) — pinned to match `frida-python`:

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
adb push tests/differential/oracle_runner/harness/prebuilt/harness-aarch64 \
         /data/local/tmp/
adb push tools/bin/android/frida-server   /data/local/tmp/
adb shell "chmod 755 /data/local/tmp/harness-aarch64 /data/local/tmp/frida-server"
adb shell "nohup /data/local/tmp/frida-server -D >/dev/null 2>&1 &"
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

## Architecture

```
oracle_runner/
├── adb_engine.py       AdbHarnessEngine: pushes harness + libs, spawns
│                       `adb shell`, speaks line-based RPC, tracks pid
│                       for Frida attach.
├── arm64_abi.py        AAPCS64 register/stack packing (x0-x7, d0-d7)
├── guest_heap.py       Bump allocator at fixed guest VA 0x50000000
├── stl_layout.py       HitData / Affine2x3 builders
├── frida_tracer.py     FridaTracerEngine: attach to harness pid, load
│                       agent.js, expose start_case/stop_case
├── frida_agent.js      Per-target `Interceptor.attach` recording x0-x7
│                       + d0-d7 at entry; x0/d0 at exit
├── trace_targets.py    Per-family target offsets + arity + return-kind
├── trace_diff.py       Golden read/write + first-divergence diff
├── adapters/           Per-family case-to-CALL translation
│   ├── geometry_hit_test.py
│   ├── local_transform.py
│   ├── bezier_curve.py
│   └── position_interp.py
└── harness/            On-device guest (see harness/README.md)
    ├── harness.cpp
    ├── CMakeLists.txt
    └── prebuilt/harness-aarch64
```

`run_*_adb.py` (siblings of `run_*_wasmtime.py`) instantiate
`AdbHarnessEngine` once, iterate specs, and optionally attach a
`FridaTracerEngine` configured with the family's target offset list.

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
- `psb_rl_decompress` — needs static extraction of the RL loop from
  `sub_695DE8`, not in scope
- Richer target lists — hook `iTJSDispatch2::PropGet` call-sites
  inside `sub_69A754`/`sub_69A4D4` if the leaf-only coverage proves
  insufficient
