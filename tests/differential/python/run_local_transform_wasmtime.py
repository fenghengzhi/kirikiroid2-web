#!/usr/bin/env python3
"""Architecture-neutral local-transform Wasmtime verifier."""

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
)


def load_specs(spec_dir: Path) -> list[dict]:
    return [json.loads(path.read_text(encoding="utf-8"))
            for path in sorted(spec_dir.glob("*.json"))]


def drive_cases(wasm_path: Path, spec_dir: Path) -> dict:
    wasmtime = load_wasmtime()
    store, exports = instantiate_standalone_module(wasmtime, wasm_path)
    memory = exports["memory"]
    input_ptr = exports["get_affine_in_ptr"](store)
    output_ptr = exports["get_affine_out_ptr"](store)
    run_fn = exports["run_local_transform"]
    results = []
    for spec in load_specs(spec_dir):
        base = memory_base(store, memory)
        write_doubles(base, input_ptr, spec["affine_in"])
        order = spec["transformOrder"]
        run_fn(
            store,
            1 if spec["flipX"] else 0,
            1 if spec["flipY"] else 0,
            spec["angle"], spec["scaleX"], spec["scaleY"],
            spec["slantX"], spec["slantY"],
            *order,
        )
        base = memory_base(store, memory)
        results.append({
            "case_id": spec["id"],
            "result": read_doubles(base, output_ptr, 6),
        })
    return {
        "ok": True,
        "runner": "local-transform-wasmtime-direct-export",
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
