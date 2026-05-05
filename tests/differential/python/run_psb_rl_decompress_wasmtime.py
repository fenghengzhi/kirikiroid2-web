#!/usr/bin/env python3

from __future__ import annotations

import argparse
import ctypes
import json
import sys
from pathlib import Path

from wasm_lldb_runner import (
    DEFAULT_HOST_PYTHON,
    instantiate_standalone_module,
    load_wasmtime,
    register_int_arg,
    run_lldb_probe,
    verify_wasm_debug_info,
)


BREAKPOINT_NAME = "krkr2_lldb_psb_rl_decompress_sample"


def load_specs(spec_dir: Path) -> list[dict]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def mem_base(store, memory) -> int:
    return ctypes.addressof(memory.data_ptr(store).contents)


def write_bytes_at(base: int, ptr: int, values: list[int]) -> None:
    if not values:
        return
    data = bytes(values)
    ctypes.memmove(base + ptr, data, len(data))


def run_python_driver(wasm_path: Path, spec_dir: Path, output: Path) -> int:
    wasmtime = load_wasmtime()
    store, exports = instantiate_standalone_module(wasmtime, wasm_path)
    memory = exports["memory"]
    compressed_ptr = exports["get_compressed_ptr"](store)
    run_fn = exports["run_psb_rl_decompress"]

    cases = []
    for spec in load_specs(spec_dir):
        base = mem_base(store, memory)
        write_bytes_at(base, compressed_ptr, spec["compressed"])
        run_fn(store, len(spec["compressed"]), spec["element_count"], spec["align"])
        cases.append(spec["id"])

    output.write_text(json.dumps({
        "ok": True,
        "runner": "psb-rl-decompress-wasmtime-python-driver",
        "cases": cases,
        "host_calls": len(cases),
    }, indent=2), encoding="utf-8")
    return 0


def unpack_bytes(value: int) -> list[int]:
    return [(int(value) >> (i * 8)) & 0xff for i in range(8)]


def read_sample(frame) -> dict:
    output_size = register_int_arg(frame, 1)
    if output_size > 16:
        raise RuntimeError(
            f"psb_rl_decompress LLDB POD sample supports <=16 bytes, got {output_size}"
        )
    data = unpack_bytes(register_int_arg(frame, 2))
    data.extend(unpack_bytes(register_int_arg(frame, 3)))
    return {
        "call_index": register_int_arg(frame, 0),
        "output_size": output_size,
        "result": data[:output_size],
    }


def samples_by_index(samples: list[dict], cases: list[str]) -> dict[int, dict]:
    if len(samples) != len(cases):
        raise RuntimeError(
            f"LLDB sampled {len(samples)} result(s), host drove {len(cases)} case(s)"
        )
    out: dict[int, dict] = {}
    for sample in samples:
        call_index = int(sample.get("call_index", -1))
        if call_index in out:
            raise RuntimeError(f"duplicate LLDB call_index sample: {call_index}")
        out[call_index] = sample
    expected = set(range(len(cases)))
    if set(out) != expected:
        raise RuntimeError(
            f"LLDB call indexes {sorted(out)} do not match {sorted(expected)}"
        )
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--wasm", required=True, type=Path)
    parser.add_argument("--host-python", default=DEFAULT_HOST_PYTHON, type=Path)
    parser.add_argument("--lldb-timeout", default=120.0, type=float)
    parser.add_argument("--driver-mode", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--output", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()

    if not args.wasm.exists():
        raise RuntimeError(f"wasm module not found: {args.wasm}")
    if args.driver_mode:
        if args.output is None:
            raise RuntimeError("--output is required in --driver-mode")
        return run_python_driver(args.wasm, args.spec_dir, args.output)
    if args.host_python is None or not args.host_python.exists():
        raise RuntimeError(f"host Python not found: {args.host_python}")

    verify_wasm_debug_info(args.wasm)
    specs = load_specs(args.spec_dir)
    if not specs:
        raise RuntimeError(f"no specs found in {args.spec_dir}")
    for spec in specs:
        if len(spec["expected"]) > 16:
            raise RuntimeError(
                f"{spec['id']} expected output is too large for POD probe: "
                f"{len(spec['expected'])} bytes"
            )

    debug_result = run_lldb_probe(
        breakpoint_name=BREAKPOINT_NAME,
        sample_reader=read_sample,
        driver=Path(__file__).resolve(),
        host_python=args.host_python,
        wasm=args.wasm,
        spec_dir=args.spec_dir,
        timeout=args.lldb_timeout,
    )
    if debug_result["hit_count"] == 0:
        raise RuntimeError(f"LLDB breakpoint {BREAKPOINT_NAME} was not hit")
    cases = debug_result["report"].get("cases", [])
    by_index = samples_by_index(debug_result["samples"], cases)
    specs_by_id = {spec["id"]: spec for spec in specs}

    failed = False
    for call_index, case_id in enumerate(cases):
        sample = by_index[call_index]
        actual = sample["result"]
        expected = specs_by_id[case_id]["expected"]
        print(json.dumps({
            "case_id": case_id,
            "status": "ok",
            "output_size": sample["output_size"],
            "runner": "port-wasm-lldb",
        }, ensure_ascii=True))
        if actual != expected:
            failed = True
            print(
                f"mismatch case_id={case_id} wasm={actual} expected={expected}",
                file=sys.stderr,
            )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
