#!/usr/bin/env python3
"""Architecture-neutral position-interpolation Wasmtime verifier."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from wasmtime_direct_support import (
    instantiate_standalone_module,
    load_wasmtime,
    memory_base,
    read_doubles,
    write_doubles,
    write_int32s,
)


def load_specs(spec_dir: Path) -> list[dict]:
    return [json.loads(path.read_text(encoding="utf-8"))
            for path in sorted(spec_dir.glob("*.json"))]


def drive_cases(wasm_path: Path, spec_dir: Path) -> dict:
    wasmtime = load_wasmtime()
    store, exports = instantiate_standalone_module(wasmtime, wasm_path)
    memory = exports["memory"]
    ptrs = {name: exports[f"get_{name}_ptr"](store) for name in (
        "easing_x", "easing_y", "cp_x", "cp_y", "cp_t", "cp_seg_data",
        "cp_seg_sizes", "src_pos", "dst_pos", "out_pos",
    )}
    run_fn = exports["run_position_interp"]
    results = []
    for spec in load_specs(spec_dir):
        base = memory_base(store, memory)
        easing = spec["easing_curve"]
        rotation = spec["rotation_curve"]
        write_doubles(base, ptrs["easing_x"], easing["x"])
        write_doubles(base, ptrs["easing_y"], easing["y"])
        write_doubles(base, ptrs["cp_x"], rotation["x"])
        write_doubles(base, ptrs["cp_y"], rotation["y"])
        write_doubles(base, ptrs["cp_t"], rotation["t"])
        segment_data: list[float] = []
        segment_sizes: list[int] = []
        for segment in rotation["segments"]:
            segment_sizes.extend([
                len(segment["x"]), len(segment["y"]), len(segment["p"]),
            ])
            segment_data.extend(segment["x"])
            segment_data.extend(segment["y"])
            segment_data.extend(segment["p"])
        write_doubles(base, ptrs["cp_seg_data"], segment_data)
        write_int32s(base, ptrs["cp_seg_sizes"], segment_sizes)
        write_doubles(base, ptrs["src_pos"], spec["src_pos"])
        write_doubles(base, ptrs["dst_pos"], spec["dst_pos"])
        run_fn(
            store, len(easing["x"]), len(rotation["x"]),
            len(rotation["t"]), len(rotation["segments"]),
            spec["coord_mode"], spec["t"],
        )
        base = memory_base(store, memory)
        results.append({
            "case_id": spec["id"],
            "result": read_doubles(base, ptrs["out_pos"], 3),
        })
    return {
        "ok": True,
        "runner": "position-interp-wasmtime-direct-export",
        "cases": [result["case_id"] for result in results],
        "results": results,
        "host_calls": len(results),
    }


def run_python_driver(wasm_path: Path, spec_dir: Path, output: Path) -> int:
    output.write_text(json.dumps(
        drive_cases(wasm_path, spec_dir), indent=2) + "\n", encoding="utf-8")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--wasm", required=True, type=Path)
    args = parser.parse_args()
    specs = {spec["id"]: spec for spec in load_specs(args.spec_dir)}
    failed = False
    for result in drive_cases(args.wasm, args.spec_dir)["results"]:
        wanted = specs[result["case_id"]]["expected"]
        status = "ok" if result["result"] == wanted else "mismatch"
        print(json.dumps({**result, "status": status,
                          "runner": "port-wasm-direct-export"}))
        if status != "ok":
            print(f"mismatch case_id={result['case_id']} "
                  f"wasm={result['result']} expected={wanted}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
