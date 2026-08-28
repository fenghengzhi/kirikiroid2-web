#!/usr/bin/env python3
"""Architecture-neutral geometry hit-test Wasmtime verifier."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from wasmtime_direct_support import (
    instantiate_standalone_module,
    load_wasmtime,
)


EXPECTED_HITS = {
    "circle_inside": True,
    "circle_boundary": True,
    "circle_outside": False,
    "rect_left_top_inclusive": True,
    "rect_right_bottom_exclusive": False,
    "quad_inside": True,
    "quad_outside": False,
    "quad_winding_clockwise": True,
    "quad_winding_counterclockwise": True,
    "invalid_type": False,
}


def load_specs(spec_dir: Path) -> list[dict]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def flatten_case(spec: dict) -> list[float]:
    """Mirror the geometry_hit_test flattened Wasm ABI."""
    shape = spec["shape"]
    kind = shape["kind"]
    values = [0.0] * 15
    if kind == "circle":
        hit_type = int(shape.get("type_override", 1))
        values[0:3] = [shape["cx"], shape["cy"], shape["r"]]
    elif kind == "rect":
        hit_type = int(shape.get("type_override", 2))
        values[3:7] = [
            shape["left"], shape["top"], shape["right"], shape["bottom"],
        ]
    elif kind == "quad":
        hit_type = int(shape.get("type_override", 3))
        values[7:15] = [
            shape[f"{axis}{index}"]
            for index in range(4)
            for axis in ("x", "y")
        ]
    else:
        raise RuntimeError(f"unsupported shape kind: {kind}")
    return [
        hit_type,
        float(spec["point"]["x"]),
        float(spec["point"]["y"]),
        *values,
    ]


def drive_cases(wasm_path: Path, spec_dir: Path) -> dict:
    wasmtime = load_wasmtime()
    store, exports = instantiate_standalone_module(wasmtime, wasm_path)
    run_fn = exports["krkr2_hit_test_run"]
    results = []
    for spec in load_specs(spec_dir):
        results.append({
            "case_id": spec["id"],
            "hit": bool(run_fn(store, *flatten_case(spec))),
        })
    return {
        "ok": True,
        "runner": "geometry-hit-test-wasmtime-direct-export",
        "cases": [result["case_id"] for result in results],
        "results": results,
        "host_calls": len(results),
    }


def run_python_driver(wasm_path: Path, spec_dir: Path, output: Path) -> int:
    output.write_text(
        json.dumps(drive_cases(wasm_path, spec_dir), indent=2) + "\n",
        encoding="utf-8",
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--wasm", required=True, type=Path)
    args = parser.parse_args()

    if not args.wasm.exists():
        raise RuntimeError(f"wasm module not found: {args.wasm}")
    report = drive_cases(args.wasm, args.spec_dir)
    failed = False
    for result in report["results"]:
        case_id = result["case_id"]
        actual = bool(result["hit"])
        expected = EXPECTED_HITS[case_id]
        status = "ok" if actual == expected else "mismatch"
        print(json.dumps({
            "case_id": case_id,
            "status": status,
            "hit": actual,
            "runner": "port-wasm-direct-export",
        }, ensure_ascii=True))
        if status != "ok":
            print(
                f"mismatch case_id={case_id} wasm={actual} "
                f"expected={expected}",
                file=sys.stderr,
            )
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
