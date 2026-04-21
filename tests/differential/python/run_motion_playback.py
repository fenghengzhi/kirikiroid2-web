#!/usr/bin/env python3
"""motion_playback differential runner.

Default fast path:
    Runs the host-native `motion_playback_port` CLI for each spec and
    diffs its frame snapshot against a checked-in disk golden under
    `tests/differential/traces/motion_playback/<id>.oracle.json`. No ADB
    or Redroid required — pure CI lane.

Re-record path (`--record-oracle`):
    Spawns the APK harness on a Redroid / AVD device, triggers
    `TVPMainScene::startupFrom(logo_test.xp3)` via harness-RPC (which
    only schedules — actual playback runs on the cocos2d GL thread),
    and attaches Frida to hook `Player_updateLayers` for per-frame
    per-layer state. Both motion fixtures (yuzulogo, m2logo) are
    recorded in a single playback since startup.tjs plays them
    sequentially. Requires a running `frida-server` on the device.

Usage:
    run_motion_playback.py [--spec-dir DIR] [--port-runner PATH]
                           [--trace-dir DIR] [--record-oracle]
                           [--serial ADB_SERIAL]
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
# Match the import pattern used by the other run_*_adb.py runners:
# oracle_runner.adb_engine / oracle_runner.adapters.* rely on the
# `oracle_runner` directory being a package, so tests/differential (the
# package parent) goes on sys.path rather than oracle_runner itself.
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))

# Imported lazily so the disk-only fast path doesn't need adb_engine deps.


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="motion_playback differential runner")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of spec JSON files")
    p.add_argument("--trace-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "traces" / "motion_playback"),
                   help="Directory of cached oracle JSONs")
    p.add_argument("--port-runner",
                   default=str(REPO_ROOT / "out" / "macos" / "debug" /
                               "tests" / "differential" /
                               "port_runners" / "motion_playback_port"),
                   help="Path to the motion_playback_port executable")
    p.add_argument("--record-oracle", action="store_true",
                   help="Re-record disk goldens from a live APK harness "
                        "(requires --serial and a deployed harness)")
    p.add_argument("--serial", default=None,
                   help="ADB serial, only with --record-oracle")
    p.add_argument("--strict-missing-trace", action="store_true",
                   help="Fail when a disk golden is missing instead of "
                        "auto-skipping the case")
    p.add_argument("--only-structural", action="store_true",
                   help="Diff only structural fields (index/label/nodeType/"
                        "visible/active/flipX/flipY/opacity/blendMode); skip "
                        "the accumulated transform fields that rely on "
                        "Player::runUpdatePassForOracle, which currently "
                        "segfaults in the headless port CLI")
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict]:
    specs = []
    for path in sorted(spec_dir.glob("*.json")):
        with path.open() as f:
            specs.append(json.load(f))
    return specs


def run_port_snapshot(runner: Path, spec: dict) -> list:
    """Invoke motion_playback_port and parse its stdout JSON."""
    mtn = spec["mtn_path"]
    if not Path(mtn).is_absolute():
        mtn = str(REPO_ROOT / mtn)
    cmd = [
        str(runner),
        "--mtn", mtn,
        "--label", spec["label"],
        "--frames", str(int(spec["frames"])),
    ]
    if "seed" in spec and spec["seed"] is not None:
        cmd += ["--seed", str(int(spec["seed"]))]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(
            f"port runner exit {proc.returncode}\n"
            f"stderr (last 1KB):\n{proc.stderr[-1024:]}"
        )
    return json.loads(proc.stdout)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)
    runner = Path(args.port_runner)

    if not spec_dir.exists():
        print(f"spec dir not found: {spec_dir}", file=sys.stderr)
        return 2
    if not runner.exists() and not args.record_oracle:
        print(f"port runner missing (build the motion_playback_port target): "
              f"{runner}", file=sys.stderr)
        return 2

    specs = load_specs(spec_dir)
    if not specs:
        print(f"no specs in {spec_dir}", file=sys.stderr)
        return 0

    from oracle_runner.adapters import motion_playback as mpb

    if args.record_oracle:
        from oracle_runner.adb_engine import AdbHarnessEngine
        if not args.serial:
            print("--record-oracle requires --serial", file=sys.stderr)
            return 2
        trace_dir.mkdir(parents=True, exist_ok=True)
        # Single playback covers every spec: startup.tjs inside
        # logo_test.xp3 plays all SEGMENT_ORDER motions sequentially, and
        # cocos2d only accepts one scheduleOnce("startup", ...) per
        # Activity lifetime. mpb.record_all_oracles returns
        # {spec_id: frames} in one shot.
        with AdbHarnessEngine(serial=args.serial) as engine:
            print(f"[record] capturing all {len(specs)} specs in one "
                  f"playback (Frida-hooked Player_updateLayers)")
            all_frames = mpb.record_all_oracles(
                engine, specs, serial=args.serial)
        for spec in specs:
            frames = all_frames.get(spec["id"])
            if frames is None:
                print(f"[record] {spec['id']}: no frames captured — "
                      f"spec id not in SEGMENT_ORDER or playback ended "
                      f"early", file=sys.stderr)
                continue
            target = trace_dir / f"{spec['id']}.oracle.json"
            with target.open("w") as f:
                json.dump(frames, f, indent=2, sort_keys=True)
            print(f"[record] {spec['id']}: wrote {len(frames)} frames "
                  f"to {target}")
        # Record-only: port CLI isn't required on the recording host
        # (CI's Redroid runner only has libkrkr2 + harness APK, not a
        # host-native motion::Player build). The host-side fast-path
        # verification is a separate invocation after the operator
        # commits the goldens.
        return 0

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
        with oracle_path.open() as f:
            oracle_frames = json.load(f)
        try:
            port_frames = run_port_snapshot(runner, spec)
        except Exception as exc:
            print(f"FAIL: {spec['id']}: port snapshot error: {exc}",
                  file=sys.stderr)
            failures += 1
            continue
        result = mpb.run_case(None, spec,
                              port_frames=port_frames,
                              oracle_frames=oracle_frames,
                              structural_only=args.only_structural)
        if result["status"] == "ok":
            print(f"PASS: {spec['id']} ({len(port_frames)} frames)")
        else:
            print(f"FAIL: {spec['id']}: {result['status']} "
                  f"({len(result['mismatches'])} mismatches)")
            for m in result["mismatches"][:10]:
                print(f"  {m}")
            if len(result["mismatches"]) > 10:
                print(f"  ... +{len(result['mismatches']) - 10} more")
            failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
