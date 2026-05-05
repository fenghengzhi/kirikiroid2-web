#!/usr/bin/env python3
"""Compare fresh motion_playback Wasmtime and Android oracle traces."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Compare motion_playback *.port.json and *.oracle.json")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of motion_playback spec JSON files")
    p.add_argument("--port-trace-dir", required=True, type=Path,
                   help="Directory containing <id>.port.json files")
    p.add_argument("--oracle-trace-dir", required=True, type=Path,
                   help="Directory containing <id>.oracle.json files")
    p.add_argument("--only-structural", action="store_true",
                   help="Diff only structural Motion state fields")
    p.add_argument("--max-mismatches", type=int, default=10,
                   help="Maximum mismatches to print per failing case")
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict[str, Any]]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)

    if not spec_dir.exists():
        print(f"spec dir not found: {spec_dir}", file=sys.stderr)
        return 2
    if not args.port_trace_dir.is_dir():
        print(f"port trace dir not found: {args.port_trace_dir}",
              file=sys.stderr)
        return 2
    if not args.oracle_trace_dir.is_dir():
        print(f"oracle trace dir not found: {args.oracle_trace_dir}",
              file=sys.stderr)
        return 2

    specs = load_specs(spec_dir)
    if not specs:
        print(f"no specs in {spec_dir}", file=sys.stderr)
        return 0

    from oracle_runner.adapters import motion_playback as mpb

    failures = 0
    for spec in specs:
        spec_id = str(spec["id"])
        port_path = args.port_trace_dir / f"{spec_id}.port.json"
        oracle_path = args.oracle_trace_dir / f"{spec_id}.oracle.json"
        if not port_path.exists():
            print(f"FAIL: {spec_id}: missing Wasmtime trace {port_path}",
                  file=sys.stderr)
            failures += 1
            continue
        if not oracle_path.exists():
            print(f"FAIL: {spec_id}: missing oracle trace {oracle_path}",
                  file=sys.stderr)
            failures += 1
            continue

        port_frames = load_json(port_path)
        oracle_frames = load_json(oracle_path)
        result = mpb.run_case(None, spec,
                              port_frames=port_frames,
                              oracle_frames=oracle_frames,
                              structural_only=args.only_structural)
        if result["status"] == "ok":
            print(f"PASS: {spec_id} ({len(port_frames)} frames)")
            continue

        mismatches = result["mismatches"]
        print(f"FAIL: {spec_id}: {result['status']} "
              f"({len(mismatches)} mismatches)")
        for mismatch in mismatches[:args.max_mismatches]:
            print(f"  {mismatch}")
        if len(mismatches) > args.max_mismatches:
            print(f"  ... +{len(mismatches) - args.max_mismatches} more")
        failures += 1

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
