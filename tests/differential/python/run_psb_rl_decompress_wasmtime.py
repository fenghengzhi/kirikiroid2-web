#!/usr/bin/env python3
"""Architecture-neutral PSB RL-decompression Wasmtime verifier."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from wasmtime_direct_support import (
    instantiate_standalone_module,
    load_wasmtime,
    memory_base,
    read_bytes,
    write_bytes,
)


def load_specs(spec_dir: Path) -> list[dict]:
    return [json.loads(path.read_text(encoding="utf-8"))
            for path in sorted(spec_dir.glob("*.json"))]


def drive_cases(wasm_path: Path, spec_dir: Path) -> dict:
    wasmtime = load_wasmtime()
    store, exports = instantiate_standalone_module(wasmtime, wasm_path)
    memory = exports["memory"]
    input_ptr = exports["get_compressed_ptr"](store)
    output_ptr = exports["get_decompressed_ptr"](store)
    get_size = exports["get_decompressed_size"]
    run_fn = exports["run_psb_rl_decompress"]
    results = []
    for spec in load_specs(spec_dir):
        base = memory_base(store, memory)
        write_bytes(base, input_ptr, spec["compressed"])
        run_fn(store, len(spec["compressed"]),
               spec["element_count"], spec["align"])
        output_size = int(get_size(store))
        base = memory_base(store, memory)
        results.append({
            "case_id": spec["id"],
            "result": read_bytes(base, output_ptr, output_size),
        })
    return {
        "ok": True,
        "runner": "psb-rl-decompress-wasmtime-direct-export",
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
