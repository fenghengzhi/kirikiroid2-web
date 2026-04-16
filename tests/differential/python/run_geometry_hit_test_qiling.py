#!/usr/bin/env python3
"""Qiling oracle runner for geometry_hit_test.

Mirrors run_geometry_hit_test_wasmtime.py but drives the real Player_hitTest
inside libkrkr2.so via Qiling. Compares against the same EXPECTED_HITS table.
A mismatch means either EXPECTED is wrong (IDA misread) or the port's
hitTestHitData diverges from libkrkr2.so.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tests/differential

from python.run_geometry_hit_test_wasmtime import EXPECTED_HITS, load_specs  # noqa: E402
from oracle_runner.qiling_engine import OracleEngine  # noqa: E402
from oracle_runner.adapters import geometry_hit_test as adapter  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--so", type=Path, default=None)
    parser.add_argument("--rootfs", type=Path, default=None)
    args = parser.parse_args()

    specs = load_specs(args.spec_dir)
    if not specs:
        raise RuntimeError(f"no specs found in {args.spec_dir}")

    engine = OracleEngine(so_path=args.so, rootfs=args.rootfs)

    failed = False
    for spec in specs:
        try:
            result = adapter.run_case(engine, spec)
        except Exception as exc:
            failed = True
            result = {"case_id": spec["id"], "status": "error", "error": repr(exc)}
            print(json.dumps(result, ensure_ascii=True))
            print(f"error in case {spec['id']}: {exc!r}", file=sys.stderr)
            continue
        result["runner"] = "qiling-oracle"
        print(json.dumps(result, ensure_ascii=True))

        expected = EXPECTED_HITS.get(result["case_id"])
        if expected is None:
            failed = True
            print(f"missing EXPECTED entry for {result['case_id']}", file=sys.stderr)
            continue
        if result["hit"] != expected:
            failed = True
            print(
                f"MISMATCH {result['case_id']}: "
                f"qiling={result['hit']} expected={expected}",
                file=sys.stderr,
            )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
