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
    p.add_argument("--markdown-report", type=Path, default=None,
                   help="Write a Markdown compare report to this path")
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict[str, Any]]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def frame_count(frames: Any) -> int | None:
    return len(frames) if isinstance(frames, list) else None


def first_layer_count(frames: Any) -> int | None:
    if not isinstance(frames, list) or not frames:
        return None
    first = frames[0]
    if not isinstance(first, dict):
        return None
    layers = first.get("layers")
    return len(layers) if isinstance(layers, list) else None


def count_by_kind(mismatches: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for mismatch in mismatches:
        kind = str(mismatch.get("kind", "unknown"))
        counts[kind] = counts.get(kind, 0) + 1
    return dict(sorted(counts.items(), key=lambda item: (-item[1], item[0])))


def write_markdown_report(
    path: Path,
    *,
    spec_dir: Path,
    port_trace_dir: Path,
    oracle_trace_dir: Path,
    records: list[dict[str, Any]],
    structural_only: bool,
    max_mismatches: int,
) -> None:
    failures = [r for r in records if r["status"] != "ok"]
    lines: list[str] = [
        "# motion_playback Fresh Compare Report",
        "",
        f"- Result: {'PASS' if not failures else 'FAIL'}",
        f"- Cases: {len(records)}",
        f"- Failed cases: {len(failures)}",
        f"- Structural only: {str(structural_only).lower()}",
        f"- Spec dir: `{spec_dir}`",
        f"- Port trace dir: `{port_trace_dir}`",
        f"- Oracle trace dir: `{oracle_trace_dir}`",
        "",
        "## Summary",
        "",
        "| Case | Status | Port frames | Oracle frames | Port layers f0 | Oracle layers f0 | Mismatches |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]

    for record in records:
        lines.append(
            "| {case_id} | {status} | {port_frames} | {oracle_frames} | "
            "{port_layers0} | {oracle_layers0} | {mismatch_count} |".format(
                case_id=record["case_id"],
                status=record["status"],
                port_frames=record.get("port_frames", "-"),
                oracle_frames=record.get("oracle_frames", "-"),
                port_layers0=record.get("port_layers0", "-"),
                oracle_layers0=record.get("oracle_layers0", "-"),
                mismatch_count=record.get("mismatch_count", "-"),
            )
        )

    for record in records:
        lines.extend(["", f"## {record['case_id']}", ""])
        lines.extend([
            f"- Status: `{record['status']}`",
            f"- Port trace: `{record['port_path']}`",
            f"- Oracle trace: `{record['oracle_path']}`",
            f"- Port frames: `{record.get('port_frames', '-')}`",
            f"- Oracle frames: `{record.get('oracle_frames', '-')}`",
            f"- Port frame-0 layers: `{record.get('port_layers0', '-')}`",
            f"- Oracle frame-0 layers: `{record.get('oracle_layers0', '-')}`",
        ])
        if record.get("error"):
            lines.extend(["", "### Error", "", "```text",
                          str(record["error"]), "```"])
            continue

        mismatches = record.get("mismatches") or []
        if not mismatches:
            lines.extend(["", "No mismatches."])
            continue

        lines.extend(["", "### Mismatch Kinds", "",
                      "| Kind | Count |", "|---|---:|"])
        for kind, count in count_by_kind(mismatches).items():
            lines.append(f"| {kind} | {count} |")

        lines.extend(["", "### First Mismatches", ""])
        for mismatch in mismatches[:max_mismatches]:
            lines.extend([
                "```json",
                json.dumps(mismatch, ensure_ascii=True, sort_keys=True),
                "```",
            ])
        if len(mismatches) > max_mismatches:
            lines.append(f"... +{len(mismatches) - max_mismatches} more")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


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
    records: list[dict[str, Any]] = []
    for spec in specs:
        spec_id = str(spec["id"])
        port_path = args.port_trace_dir / f"{spec_id}.port.json"
        oracle_path = args.oracle_trace_dir / f"{spec_id}.oracle.json"
        record: dict[str, Any] = {
            "case_id": spec_id,
            "status": "pending",
            "port_path": str(port_path),
            "oracle_path": str(oracle_path),
        }
        if not port_path.exists():
            record.update({
                "status": "missing_port",
                "error": f"missing Wasmtime trace {port_path}",
            })
            records.append(record)
            print(f"FAIL: {spec_id}: missing Wasmtime trace {port_path}",
                  file=sys.stderr)
            failures += 1
            continue
        if not oracle_path.exists():
            record.update({
                "status": "missing_oracle",
                "error": f"missing oracle trace {oracle_path}",
            })
            records.append(record)
            print(f"FAIL: {spec_id}: missing oracle trace {oracle_path}",
                  file=sys.stderr)
            failures += 1
            continue

        try:
            port_frames = load_json(port_path)
            oracle_frames = load_json(oracle_path)
            record.update({
                "port_frames": frame_count(port_frames),
                "oracle_frames": frame_count(oracle_frames),
                "port_layers0": first_layer_count(port_frames),
                "oracle_layers0": first_layer_count(oracle_frames),
            })
            result = mpb.run_case(None, spec,
                                  port_frames=port_frames,
                                  oracle_frames=oracle_frames,
                                  structural_only=args.only_structural)
        except Exception as exc:
            record.update({"status": "error", "error": repr(exc)})
            records.append(record)
            print(f"FAIL: {spec_id}: compare error: {exc!r}",
                  file=sys.stderr)
            failures += 1
            continue

        if result["status"] == "ok":
            record.update({
                "status": "ok",
                "mismatch_count": 0,
                "mismatches": [],
            })
            records.append(record)
            print(f"PASS: {spec_id} ({len(port_frames)} frames)")
            continue

        mismatches = result["mismatches"]
        record.update({
            "status": result["status"],
            "mismatch_count": len(mismatches),
            "mismatches": mismatches,
        })
        records.append(record)
        print(f"FAIL: {spec_id}: {result['status']} "
              f"({len(mismatches)} mismatches)")
        for mismatch in mismatches[:args.max_mismatches]:
            print(f"  {mismatch}")
        if len(mismatches) > args.max_mismatches:
            print(f"  ... +{len(mismatches) - args.max_mismatches} more")
        failures += 1

    if args.markdown_report is not None:
        write_markdown_report(
            args.markdown_report,
            spec_dir=spec_dir,
            port_trace_dir=args.port_trace_dir,
            oracle_trace_dir=args.oracle_trace_dir,
            records=records,
            structural_only=args.only_structural,
            max_mismatches=args.max_mismatches,
        )
        print(f"Markdown report: {args.markdown_report}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
