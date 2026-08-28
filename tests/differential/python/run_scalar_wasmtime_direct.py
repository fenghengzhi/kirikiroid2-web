#!/usr/bin/env python3
"""Run scalar differential families through the Python Wasmtime embedding.

This is an embedding fallback for environments without the Wasmtime CLI.  The
portable CLI lane is authoritative because it shares a Wasmtime 44+ runtime
with the Guest/Wasm-only debugger and avoids host-process JIT restrictions.
"""

from __future__ import annotations

import argparse
import importlib
import json
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
FAMILIES = {
    "geometry_hit_test": "run_geometry_hit_test_wasmtime",
    "psb_rl_decompress": "run_psb_rl_decompress_wasmtime",
    "bezier_curve": "run_bezier_curve_wasmtime",
    "local_transform": "run_local_transform_wasmtime",
    "position_interp": "run_position_interp_wasmtime",
}


def equal_value(actual, expected) -> bool:
    if isinstance(expected, list):
        return isinstance(actual, list) and actual == expected
    return actual == expected


def run_family(family: str, wasm: Path, spec_dir: Path) -> bool:
    module = importlib.import_module(FAMILIES[family])
    with tempfile.TemporaryDirectory(prefix=f"krkr2-{family}-direct-") as tmp:
        output = Path(tmp) / "report.json"
        module.run_python_driver(wasm, spec_dir, output)
        report = json.loads(output.read_text(encoding="utf-8"))

    specs = {spec["id"]: spec for spec in module.load_specs(spec_dir)}
    results = report.get("results", [])
    if len(results) != len(specs):
        raise RuntimeError(
            f"{family}: got {len(results)} result(s), expected {len(specs)}"
        )

    failed = False
    seen: set[str] = set()
    for result in results:
        case_id = str(result["case_id"])
        if case_id in seen or case_id not in specs:
            raise RuntimeError(f"{family}: unexpected/duplicate case {case_id}")
        seen.add(case_id)
        if family == "geometry_hit_test":
            actual = bool(result["hit"])
            expected = module.EXPECTED_HITS[case_id]
        else:
            actual = result["result"]
            expected = specs[case_id]["expected"]
        status = "ok" if equal_value(actual, expected) else "mismatch"
        print(json.dumps({
            "family": family,
            "case_id": case_id,
            "status": status,
            "actual": actual,
            "expected": expected,
            "runner": "port-wasm-direct-export",
        }, ensure_ascii=True))
        failed = failed or status != "ok"

    if seen != set(specs):
        raise RuntimeError(
            f"{family}: missing cases {sorted(set(specs) - seen)}"
        )
    return not failed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--family", action="append", choices=sorted(FAMILIES), required=True
    )
    parser.add_argument(
        "--wasm-dir",
        type=Path,
        default=REPO_ROOT / "tests" / "differential" / "wasmtime",
    )
    parser.add_argument(
        "--spec-root",
        type=Path,
        default=REPO_ROOT / "tests" / "differential" / "specs",
    )
    args = parser.parse_args()

    ok = True
    for family in args.family:
        wasm = args.wasm_dir / f"{family}.wasm"
        spec_dir = args.spec_root / family
        if not wasm.exists():
            raise RuntimeError(f"missing wasm module: {wasm}")
        ok = run_family(family, wasm, spec_dir) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
