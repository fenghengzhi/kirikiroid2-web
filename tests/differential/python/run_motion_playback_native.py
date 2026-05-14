#!/usr/bin/env python3
"""Native verifier for the motion_playback differential family."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="motion_playback native differential runner")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of spec JSON files")
    p.add_argument("--trace-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "traces" / "motion_playback"),
                   help="Directory of cached oracle JSONs")
    p.add_argument("--runner",
                   default=None,
                   help="Path to the motion_playback_native executable")
    p.add_argument("--startup-xp3",
                   default=str(REPO_ROOT / "reference" / "xp3" /
                               "logo_test_oracle.xp3"),
                   help="Host path to logo_test_oracle.xp3")
    p.add_argument("--strict-missing-trace", action="store_true",
                   help="Fail when a disk golden is missing instead of "
                        "auto-skipping the case")
    p.add_argument("--only-structural", action="store_true",
                   help="Diff only structural Motion state fields")
    p.add_argument("--lldb-timeout", type=float, default=90.0,
                   help="Timeout for the macOS LLDB native tracer")
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def default_runner_candidates() -> list[Path]:
    exe = "motion_playback_native.exe" if sys.platform == "win32" \
        else "motion_playback_native"
    return [
        REPO_ROOT / "out" / "native" / "debug" / "tests" /
        "differential" / "native" / exe,
        REPO_ROOT / "out" / "native" / "release" / "tests" /
        "differential" / "native" / exe,
        REPO_ROOT / "out" / "macos" / "debug" / "tests" /
        "differential" / "native" / exe,
        REPO_ROOT / "out" / "macos" / "release" / "tests" /
        "differential" / "native" / exe,
        REPO_ROOT / "build" / "tests" / "differential" / "native" / exe,
    ]


def resolve_runner(runner_arg: str | None) -> Path:
    if runner_arg:
        return Path(runner_arg)
    for candidate in default_runner_candidates():
        if candidate.exists():
            return candidate
    candidates = "\n  ".join(str(p) for p in default_runner_candidates())
    raise FileNotFoundError(
        "motion_playback_native executable not found. Build it with "
        "`cmake --build <native-build-dir> --target motion_playback_native` "
        "or pass `--runner PATH`. Checked:\n  " + candidates)


def run_native_trace(runner: Path, startup_xp3: Path, *,
                     expected_frames: int,
                     timeout: float) -> list[dict]:
    if not runner.exists():
        raise FileNotFoundError(
            f"native runner not found: {runner}. Build with "
            "`cmake --build <native-build-dir> --target motion_playback_native`."
        )
    if not startup_xp3.exists():
        raise FileNotFoundError(f"oracle bootstrap xp3 missing: {startup_xp3}")
    if sys.platform != "darwin":
        raise RuntimeError("native LLDB motion trace is only supported on macOS")

    with tempfile.TemporaryDirectory(prefix="krkr2-motion-native-") as td:
        trace_path = Path(td) / "trace.json"
        tracer = REPO_ROOT / "tests" / "differential" / "python" / \
            "native_lldb_motion_trace.py"
        cmd = [
            "xcrun", "python3", str(tracer),
            "--runner", str(runner),
            "--startup-xp3", str(startup_xp3),
            "--trace-out", str(trace_path),
            "--expected-frames", str(expected_frames),
            "--timeout", str(timeout),
            "--repo-root", str(REPO_ROOT),
        ]
        try:
            proc = subprocess.run(
                cmd,
                cwd=str(REPO_ROOT),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=timeout + 15.0,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(
                f"native LLDB trace timed out after {timeout + 15.0:.1f}s\n"
                f"stdout:\n{exc.stdout or ''}\nstderr:\n{exc.stderr or ''}"
            ) from exc
        if proc.returncode != 0:
            raise RuntimeError(
                f"native LLDB tracer failed with exit code "
                f"{proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n"
                f"{proc.stderr}\n\nLLDB environment checks:\n"
                f"  xcrun lldb -P\n"
                f"  xcrun python3 -c 'import sys, subprocess; "
                f"sys.path.insert(0, subprocess.check_output([\"xcrun\", "
                f"\"lldb\", \"-P\"], text=True).strip()); "
                f"import lldb; print(lldb.SBDebugger)'"
            )
        if not trace_path.exists():
            raise RuntimeError(
                "native LLDB tracer did not write trace output\n"
                f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
            )
        try:
            events = json.loads(trace_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"native trace JSON decode failed: {exc}\n"
                f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
            ) from exc
    if not isinstance(events, list):
        raise RuntimeError(f"native trace root is not a list: {type(events)}")
    return events


def _segment_events(events: list[dict]) -> list[dict]:
    segments: list[dict] = []
    for ev in events:
        key = ev.get("objthis") or ev.get("topPlayer")
        if not segments or segments[-1]["player"] != key:
            segments.append({"player": key, "frames": []})
        segments[-1]["frames"].append(ev)
    return segments


def partition_native_frames(events: list[dict], specs: list[dict], mpb) -> dict:
    specs_by_id = {s["id"]: s for s in specs}
    segment_order = mpb.segment_order_for_specs(specs_by_id)

    segments = _segment_events(events)
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    if len(substantive) < len(specs_by_id):
        raise RuntimeError(
            f"only {len(substantive)} substantive native segment(s) "
            f"captured (raw segments: {[len(s['frames']) for s in segments]})."
        )

    results: dict[str, list[dict]] = {}
    for i, spec_id in enumerate(segment_order):
        spec = specs_by_id[spec_id]
        wanted = int(spec["frames"])
        frames = substantive[i]["frames"]
        if len(frames) < wanted:
            raise RuntimeError(
                f"native segment {i} ({spec_id}) has "
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
        runner = resolve_runner(args.runner)
        expected_frames = sum(int(spec["frames"]) for spec in specs)
        native_events = run_native_trace(
            runner, startup_xp3,
            expected_frames=expected_frames,
            timeout=args.lldb_timeout,
        )
        native_frames_by_id = partition_native_frames(native_events, specs, mpb)
    except Exception as exc:
        print(f"FAIL: native trace error: {exc}", file=sys.stderr)
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

        native_frames = native_frames_by_id.get(spec["id"])
        if native_frames is None:
            print(f"FAIL: {spec['id']}: no native frames captured",
                  file=sys.stderr)
            failures += 1
            continue

        result = mpb.run_case(None, spec,
                              port_frames=native_frames,
                              oracle_frames=oracle_frames,
                              structural_only=args.only_structural)
        if result["status"] == "ok":
            print(f"PASS: {spec['id']} ({len(native_frames)} frames)")
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
