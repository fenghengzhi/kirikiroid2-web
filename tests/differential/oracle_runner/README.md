# Qiling Oracle Runner

Runs libkrkr2.so under Qiling (Unicorn-based ARM64 emulator) to independently
validate the hardcoded `EXPECTED_*` tables / inline `"expected"` fields in
`tests/differential/python/run_*_wasmtime.py`.

A mismatch means either the EXPECTED value (derived by hand from IDA reading)
is wrong, or the WASM port diverged from libkrkr2.so.

## Status

| Family | Qiling oracle | Notes |
|---|---|---|
| geometry_hit_test | **✓ 10/10** | Pure C leaf `Player_hitTest` (0x690DF0), no libm/TJS deps |
| local_transform | **✓ 8/8** | `sub_699940` (0x699940) builds local 2×2; Python does A×L; libm sin/cos hooked |
| psb_rl_decompress | — skipped | RL loop is inlined inside sub_695DE8 (a 53 KB PSB loader); no standalone entry |
| bezier_curve | — skipped | `sub_69A754` wraps math in TJS dispatch (`PropGet L"x"`/`L"y"`); calling standalone needs a fake TJS dispatch + vtable |
| position_interp | — skipped | `sub_69A4D4` depends on TJS-wrapped `sub_69A754` + `sub_698454`; same blocker as above |

Skipped families still run in the WASM port harness against the inline
`expected` values. Lifting them to the Qiling oracle would require a TJS
dispatch fake object that responds to `iTJSDispatch2::PropGet` with guest
arrays — a substantial undertaking left for future work.

## Prerequisites

**libkrkr2.so** — default path `reference/libkrkr2/libkrkr2.so` (via the
`reference` git submodule → `kirikiroid2-web-private`). Ensure it's checked
out: `git submodule update --init reference`. Override with
`KRKR2_SO_PATH=/path/to/libkrkr2.so` or `--so`.

**Android ARM64 rootfs** — clone Qiling's sample rootfs:

```bash
git clone --depth 1 https://github.com/qilingframework/rootfs.git ~/.qiling-rootfs
export KRKR2_ROOTFS=~/.qiling-rootfs/arm64_android6.0
```

The engine points at `arm64_android6.0/` (contains `libc.so`, `libm.so`,
`libc++.so`, `libstdc++.so`, `linker64`). The smaller `arm64_android/`
subdir is **insufficient** — missing libm and libc++. Override via
`KRKR2_ROOTFS` env var or `--rootfs`.

**Python deps**:

```bash
pip install -r tests/differential/oracle_runner/requirements-oracle.txt
```

## Running

```bash
export KRKR2_ROOTFS=~/.qiling-rootfs/arm64_android6.0

python3 tests/differential/python/run_geometry_hit_test_qiling.py \
  --spec-dir tests/differential/specs/geometry_hit_test

python3 tests/differential/python/run_local_transform_qiling.py \
  --spec-dir tests/differential/specs/local_transform
```

Output is one JSON line per case (same format as `run_*_wasmtime.py` but with
`"runner": "qiling-oracle"`); mismatches are reported on stderr; exit 0 if all
match, 1 otherwise.

## Architecture

```
oracle_runner/
├── qiling_engine.py    OracleEngine: loads libkrkr2.so, resolves PIE base,
│                       hooks libm PLT stubs, exposes call(addr, ints=..., doubles=..., ret=...)
├── arm64_abi.py        AAPCS64 register/stack packing (x0-x7 ints, d0-d7 doubles)
├── guest_heap.py       16 MB bump allocator at fixed guest VA 0x50000000
├── stl_layout.py       HitData / Affine2x3 / Vec3 / vector<double> builders
│                       (kept for future adapters; hit_test only uses HitData)
└── adapters/
    ├── geometry_hit_test.py
    └── local_transform.py
```

`run_*_qiling.py` (siblings of `run_*_wasmtime.py`) are thin wrappers that
instantiate `OracleEngine` once and iterate spec JSON. They import
`EXPECTED_*` tables and `load_specs` from their wasmtime counterparts to
avoid duplication.

## Implementation notes

**libm PLT hooking** — Qiling loads libkrkr2.so but doesn't fully initialise
libm.so's GOT/PLT chain. Calling `sin`/`cos` via PLT crashes with
`UC_ERR_FETCH_UNMAPPED`. We hook libkrkr2.so's `.sin` and `.cos` PLT stubs
(offsets 0x411390 and 0x4091E0, found via IDA `lookup_funcs`) and execute
Python `math.sin`/`math.cos` in-process, then advance PC to LR. This bypasses
libm entirely. All 8 local_transform cases pass bit-exact against the port.

**A × L multiplication** — libkrkr2.so's `sub_699940` only builds the local
2×2 matrix and writes it back to layer offsets +120..+144. The port's
`applyLocalTransform` additionally performs `A_new = A × L`. The adapter
reads L back from guest memory and performs the A×L step in Python so the
oracle result matches the spec's `"expected"` Affine2x3.

**Guard page** — before each call, LR is set to 0x90000000 (a mapped guard
page). `ql.emu_start(begin=addr, end=0x90000000)` runs until PC reaches the
guard, giving a clean function-return boundary.
