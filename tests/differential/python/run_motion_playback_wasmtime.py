#!/usr/bin/env python3
"""Wasmtime port-side verifier for the motion_playback differential family."""

from __future__ import annotations

import argparse
import ctypes
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="motion_playback Wasmtime differential runner")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of spec JSON files")
    p.add_argument("--trace-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "traces" / "motion_playback"),
                   help="Directory of cached oracle JSONs")
    p.add_argument("--wasm",
                   default=str(REPO_ROOT / "out" / "web" / "debug" /
                               "tests" / "differential" / "wasm" /
                               "motion_playback_wasmtime.wasm"),
                   help="Path to the Wasmtime guest wasm")
    p.add_argument("--host",
                   default=None,
                   help="Path to krkr2_wasmtime_host. When present, run the "
                        "generic headless host instead of Python wasmtime-py.")
    p.add_argument("--startup-xp3",
                   default=str(REPO_ROOT / "reference" / "xp3" /
                               "logo_test_oracle.xp3"),
                   help="Host path to logo_test_oracle.xp3")
    p.add_argument("--strict-missing-trace", action="store_true",
                   help="Fail when a disk golden is missing instead of "
                        "auto-skipping the case")
    p.add_argument("--only-structural", action="store_true",
                   help="Diff only structural Motion state fields")
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def load_wasmtime():
    try:
        import wasmtime  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "wasmtime is not installed; run "
            "'python3 -m pip install -r "
            "tests/differential/python/requirements-wasm.txt'"
        ) from exc
    return wasmtime


def instantiate_module(wasmtime, wasm_path: Path):
    config = wasmtime.Config()
    config.wasm_exceptions = True
    config.wasm_simd = True
    config.wasm_threads = True
    engine = wasmtime.Engine(config)
    module = wasmtime.Module.from_file(engine, str(wasm_path))
    store = wasmtime.Store(engine)

    wasi = wasmtime.WasiConfig()
    wasi.inherit_stdout()
    wasi.inherit_stderr()
    wasi.preopen_dir(str(REPO_ROOT), "/")
    store.set_wasi(wasi)

    linker = wasmtime.Linker(engine)
    linker.define_wasi()
    i32 = wasmtime.ValType.i32()
    linker.define_func(
        "env", "emscripten_asm_const_int",
        wasmtime.FuncType([i32, i32, i32], [i32]),
        lambda _code, _argv, _argc: 0)
    linker.define_func(
        "env", "js_decode_text",
        wasmtime.FuncType([i32, i32, i32, i32, i32], [i32]),
        lambda _src, _src_len, _dst, _dst_len, _flags: 0)
    linker.define_func(
        "env", "emscripten_notify_memory_growth",
        wasmtime.FuncType([i32], []), lambda _index: None)
    linker.define_func(
        "env", "__syscall_getdents64",
        wasmtime.FuncType([i32, i32, i32], [i32]),
        lambda _fd, _dirp, _count: -52)
    linker.define_func(
        "env", "__syscall_unlinkat",
        wasmtime.FuncType([i32, i32, i32], [i32]),
        lambda _dirfd, _path, _flags: -52)
    linker.define_func(
        "env", "__syscall_rmdir",
        wasmtime.FuncType([i32], [i32]), lambda _path: -52)
    instance = linker.instantiate(store, module)
    exports = instance.exports(store)

    initialize = None
    for init_name in ("__initialize", "_initialize"):
        try:
            initialize = exports[init_name]
            break
        except Exception:
            continue
    if initialize is not None:
        initialize(store)

    return store, exports


def mem_base(store, memory) -> int:
    return ctypes.addressof(memory.data_ptr(store).contents)


def write_bytes(store, memory, ptr: int, data: bytes) -> None:
    ctypes.memmove(mem_base(store, memory) + ptr, data, len(data))


def read_string(store, memory, ptr: int, length: int) -> str:
    if ptr == 0 or length <= 0:
        return ""
    buf = (ctypes.c_char * length).from_address(mem_base(store, memory) + ptr)
    return bytes(buf).decode("utf-8", errors="replace")


def run_wasmtime_trace(wasm_path: Path, startup_xp3: Path) -> list[dict]:
    if not wasm_path.exists():
        raise FileNotFoundError(
            f"wasm module not found: {wasm_path}. Build with "
            "`cmake --build out/web/debug --target motion_playback_wasmtime`."
        )
    if not startup_xp3.exists():
        raise FileNotFoundError(f"oracle bootstrap xp3 missing: {startup_xp3}")

    wasmtime = load_wasmtime()
    store, exports = instantiate_module(wasmtime, wasm_path)
    memory = exports["memory"]
    malloc = exports["malloc"]
    free = exports["free"]
    write_file = exports["mp_write_file"]
    startup = exports["mp_startup_from"]

    guest_path = b"reference/xp3/logo_test_oracle.xp3"
    xp3_bytes = startup_xp3.read_bytes()
    path_ptr = malloc(store, len(guest_path))
    data_ptr = malloc(store, len(xp3_bytes))
    try:
        write_bytes(store, memory, path_ptr, guest_path)
        write_bytes(store, memory, data_ptr, xp3_bytes)
        staged = write_file(store, path_ptr, len(guest_path),
                            data_ptr, len(xp3_bytes))
        if not staged:
            err = read_string(store, memory,
                              exports["mp_get_error_ptr"](store),
                              exports["mp_get_error_len"](store))
            raise RuntimeError(err or "mp_write_file returned false")
        ok = startup(store, path_ptr, len(guest_path))
    finally:
        free(store, data_ptr)
        free(store, path_ptr)

    err = read_string(store, memory,
                      exports["mp_get_error_ptr"](store),
                      exports["mp_get_error_len"](store))
    if not ok:
        raise RuntimeError(err or "mp_startup_from returned false")

    raw = read_string(store, memory,
                      exports["mp_get_trace_ptr"](store),
                      exports["mp_get_trace_len"](store))
    try:
        events = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"Wasmtime trace JSON decode failed: {exc}: {raw[:400]!r}"
        ) from exc
    if not isinstance(events, list):
        raise RuntimeError(f"Wasmtime trace root is not a list: {type(events)}")
    return events


def run_headless_host_trace(host_path: Path, wasm_path: Path,
                            startup_xp3: Path, frames: int) -> list[dict]:
    from wasmtime_headless import run_headless_host

    if not host_path.exists():
        raise FileNotFoundError(
            f"host not found: {host_path}. Build with "
            "`cmake --build out/wasmtime/debug --target krkr2_wasmtime_host`."
        )
    if not wasm_path.exists():
        raise FileNotFoundError(
            f"wasm module not found: {wasm_path}. Build with "
            "`cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`."
        )
    report = run_headless_host(
        host=host_path,
        wasm=wasm_path,
        repo_root=REPO_ROOT,
        xp3=startup_xp3,
        frames=frames,
        trace="motion,log,framebuffer",
    )
    if not report.get("ok", False):
        raise RuntimeError(
            "krkr2_wasmtime_host guest failed: "
            f"{report.get('error') or report}"
        )
    events = report.get("trace")
    if not isinstance(events, list):
        raise RuntimeError(
            f"krkr2_wasmtime_host trace is not a list: {type(events)}"
        )
    return events


def _segment_events(events: list[dict]) -> list[dict]:
    segments: list[dict] = []
    for ev in events:
        key = ev.get("objthis") or ev.get("topPlayer")
        if not segments or segments[-1]["player"] != key:
            segments.append({"player": key, "frames": []})
        segments[-1]["frames"].append(ev)
    return segments


def partition_port_frames(events: list[dict], specs: list[dict], mpb) -> dict:
    specs_by_id = {s["id"]: s for s in specs}
    unknown = [sid for sid in specs_by_id if sid not in mpb.SEGMENT_ORDER]
    if unknown:
        raise ValueError(
            f"unknown motion_playback spec id(s): {unknown}. "
            f"Expected ids are fixed by logo_test_oracle.xp3: "
            f"{mpb.SEGMENT_ORDER}."
        )

    segments = _segment_events(events)
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    if len(substantive) < len(specs_by_id):
        raise RuntimeError(
            f"only {len(substantive)} substantive Wasmtime segment(s) "
            f"captured (raw segments: {[len(s['frames']) for s in segments]})."
        )

    results: dict[str, list[dict]] = {}
    for i, spec_id in enumerate(mpb.SEGMENT_ORDER):
        if spec_id not in specs_by_id:
            continue
        spec = specs_by_id[spec_id]
        wanted = int(spec["frames"])
        frames = substantive[i]["frames"]
        if len(frames) < wanted:
            raise RuntimeError(
                f"Wasmtime segment {i} ({spec_id}) has "
                f"{len(frames)} frames; spec requires {wanted}."
            )
        results[spec_id] = [
            mpb.normalize_frame(fr, fi)
            for fi, fr in enumerate(frames[:wanted])
        ]
    return results


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)
    wasm_path = Path(args.wasm)
    startup_xp3 = Path(args.startup_xp3)

    if not spec_dir.exists():
        print(f"spec dir not found: {spec_dir}", file=sys.stderr)
        return 2

    specs = load_specs(spec_dir)
    if not specs:
        print(f"no specs in {spec_dir}", file=sys.stderr)
        return 0

    from oracle_runner.adapters import motion_playback as mpb

    try:
        if args.host:
            max_frames = max(int(spec["frames"]) for spec in specs)
            port_events = run_headless_host_trace(Path(args.host), wasm_path,
                                                  startup_xp3, max_frames)
        else:
            port_events = run_wasmtime_trace(wasm_path, startup_xp3)
        port_frames_by_id = partition_port_frames(port_events, specs, mpb)
    except Exception as exc:
        print(f"FAIL: Wasmtime port trace error: {exc}", file=sys.stderr)
        return 1

    failures = 0
    for spec in specs:
        oracle_path = trace_dir / f"{spec['id']}.oracle.json"
        if not oracle_path.exists():
            msg = f"no oracle for {spec['id']} at {oracle_path}"
            if args.strict_missing_trace:
                print(f"FAIL: {msg}", file=sys.stderr)
                failures += 1
            else:
                print(f"SKIP: {msg}")
            continue
        oracle_frames = json.loads(oracle_path.read_text(encoding="utf-8"))

        port_frames = port_frames_by_id.get(spec["id"])
        if port_frames is None:
            print(f"FAIL: {spec['id']}: no Wasmtime frames captured",
                  file=sys.stderr)
            failures += 1
            continue

        result = mpb.run_case(None, spec,
                              port_frames=port_frames,
                              oracle_frames=oracle_frames,
                              structural_only=args.only_structural)
        if result["status"] == "ok":
            print(f"PASS: {spec['id']} ({len(port_frames)} frames)")
        else:
            print(f"FAIL: {spec['id']}: {result['status']} "
                  f"({len(result['mismatches'])} mismatches)")
            for mismatch in result["mismatches"][:10]:
                print(f"  {mismatch}")
            if len(result["mismatches"]) > 10:
                print(f"  ... +{len(result['mismatches']) - 10} more")
            failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
