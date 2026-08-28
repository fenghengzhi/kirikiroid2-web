# Wasmtime Guest/Wasm-only debugging migration (2026-08-29)

## Outcome

The Wasmtime differential lane no longer attaches LLDB to a host Python
process or samples JIT-native AArch64/x86-64 registers. It now has two
deliberately separate, architecture-neutral paths:

1. Automated differential execution invokes explicit Wasm exports and reads
   guest linear memory or guest-owned JSON traces.
2. Interactive debugging uses Wasmtime's built-in guest gdbstub and the LLDB
   WebAssembly process plugin, so instruction stepping happens in the virtual
   Wasm address space.

The old `wasm_lldb_runner.py`, `wasm_lldb_motion_trace.py`, and
`wasmtime_motion_playback_driver.py` native-debugger path was removed.

## Runtime baseline

Guest/Wasm-only debugging first shipped in Wasmtime 44.0.0 on 2026-04-20 as
the CLI `-g`/`--gdb` option. This migration accepts 44.0.0 or newer and pins CI
and Python embedding dependencies to Wasmtime 48.0.0 LTS.

Published Wasmtime CLI binaries include the gdbstub. Source builds must enable
the `gdbstub` Cargo feature. A Wasm-aware LLDB is still required; the wasi-sdk
distribution is the tested choice because ordinary system LLDB builds often
omit the `wasm` process plugin.

## Automated scalar lane

`tests/differential/run_all.py` builds all five standalone modules with debug
information and runs a single Wasmtime CLI comparator:

- geometry hit test
- PSB RL decompression
- Bezier curve evaluation
- local transform
- position interpolation

Local-transform and PSB harnesses gained explicit output exports plus flattened
CLI entry points. No expected values or spec files changed. A Python Wasmtime
embedding runner remains as a fallback, but it also reads explicit guest
exports and never inspects native registers.

The obsolete scalar sample hooks were removed. Guest debugging can break on
the actual implementation exports and requires no manual C/C++ probe function.

The scalar workflow now has Linux, macOS, and Windows jobs using the same
Emscripten 4.0.23 build and Wasmtime 48.0.0 execution commands.

## Interactive guest debugger

`tests/differential/python/run_wasm_guest_debug.py` validates the Wasmtime
version and launches:

```text
wasmtime run -g 127.0.0.1:1234 --invoke <export> <module.wasm> ...
```

The debugger connects with:

```text
process connect --plugin wasm connect://127.0.0.1:1234
```

It supports repeatable Wasm function breakpoints and can either print the
commands, wait for a manually launched LLDB, or launch an explicitly selected
wasi-sdk LLDB.

## Full motion playback

The full `krkr2_wasmtime_guest.wasm` module needs custom Emscripten, filesystem,
OpenGL, and application host imports, so a plain `wasmtime run` process cannot
instantiate it. Its automated lane therefore records the state inside guest
C++ and returns it through guest trace exports. This remains Wasm-only data
collection and has no host-native register dependency.

Interactive Wasm instruction stepping for this full guest would require a
Wasmtime embedder that provides the same imports while enabling the guest
debugger. That additional embedder is not part of this migration.

## Verification

Verified locally on macOS x86-64 with Emscripten 4.0.23, Wasmtime 48.0.0, the
Wasmtime Python package 48.0.0, and wasi-sdk 33 LLDB:

- all five standalone modules built from the current sources;
- Wasmtime CLI scalar comparison passed 37/37 cases;
- Python embedding fallback passed the same 37/37 cases;
- the gdbstub listened successfully on a local TCP address;
- LLDB's `wasm` plugin resolved `krkr2_hit_test_run` at virtual Wasm address
  `0x4000000000000225`;
- the breakpoint stopped at `geometry_hit_test_wasm.cpp`, exposed the real
  guest function arguments, and `thread step-inst` advanced to virtual Wasm
  address `0x4000000000000227`;
- the full Wasmtime guest built successfully (144 MiB debug module);
- `m2logo` matched 25/25 Android-oracle frames;
- `yuzulogo` matched 63/63 Android-oracle frames;
- motion timing and strict-oracle unit tests passed (10 tests total).

This demonstrates that the new debugger observes the guest Wasm machine, while
the automated comparison remains deterministic and independent of the host CPU
calling convention.
