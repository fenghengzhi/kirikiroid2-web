#!/usr/bin/env python3
"""Record Android libkrkr2 motion_playback oracle goldens.

This runner owns only the Android oracle-recording path:
    run_motion_playback.py --record-oracle --serial ADB_SERIAL

Wasmtime verification against the checked-in goldens lives in
`run_motion_playback_wasmtime.py`.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import traceback
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
# Match the import pattern used by the other run_*_adb.py runners:
# oracle_runner.adb_engine / oracle_runner.adapters.* rely on the
# `oracle_runner` directory being a package, so tests/differential (the
# package parent) goes on sys.path rather than oracle_runner itself.
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="motion_playback Android oracle recorder")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of spec JSON files")
    p.add_argument("--trace-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "traces" / "motion_playback"),
                   help="Directory for recorded oracle JSONs")
    p.add_argument("--startup-xp3",
                   default=str(REPO_ROOT / "reference" / "xp3" /
                               "logo_test_oracle.xp3"),
                   help="Host path to the oracle startup XP3")
    p.add_argument("--case", action="append", default=[],
                   help="Motion case id to record; repeat for multiple cases. "
                        "Defaults to all specs in --spec-dir")
    p.add_argument("--record-oracle", action="store_true",
                   help="Re-record disk goldens from a live APK harness "
                        "(required; requires --serial)")
    p.add_argument("--record-framebuffer", action="store_true",
                   help="With --record-oracle, save every libkrkr2 "
                        "motion_playback frame as PNG artifacts and write "
                        "a manifest.json")
    p.add_argument("--framebuffer-dir", type=Path, default=None,
                   help="Framebuffer artifact output directory. Default: "
                        "tests/differential/artifacts/"
                        "motion_playback_framebuffer/<run-id>")
    p.add_argument("--serial", default=None,
                   help="ADB serial")
    p.add_argument("--playback-timeout", type=float, default=90.0,
                   help="Seconds to wait for Android oracle recording")
    return p.parse_args(argv)


def default_framebuffer_dir() -> Path:
    run_id = time.strftime("%Y%m%d-%H%M%S")
    return (
        REPO_ROOT / "tests" / "differential" / "artifacts"
        / "motion_playback_framebuffer" / run_id
    )


def load_specs(spec_dir: Path) -> list[dict]:
    specs = []
    for path in sorted(spec_dir.glob("*.json")):
        with path.open() as f:
            specs.append(json.load(f))
    return specs


def filter_specs(specs: list[dict], case_ids: list[str]) -> list[dict]:
    if not case_ids:
        return specs
    wanted = {str(case_id) for case_id in case_ids}
    selected = [spec for spec in specs if str(spec.get("id")) in wanted]
    found = {str(spec.get("id")) for spec in selected}
    missing = sorted(wanted - found)
    if missing:
        raise ValueError(f"unknown motion_playback case id(s): {missing}")
    return selected


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)

    if not args.record_oracle:
        print(
            "run_motion_playback.py is oracle-record only; pass "
            "--record-oracle, or use run_motion_playback_wasmtime.py for "
            "port verification.",
            file=sys.stderr,
        )
        return 2
    if not args.serial:
        print("--record-oracle requires --serial", file=sys.stderr)
        return 2

    if not spec_dir.exists():
        print(f"spec dir not found: {spec_dir}", file=sys.stderr)
        return 2

    try:
        specs = filter_specs(load_specs(spec_dir), args.case)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    if not specs:
        print(f"no specs in {spec_dir}", file=sys.stderr)
        return 0

    from oracle_runner.adapters import motion_playback as mpb
    from oracle_runner.adb_engine import AdbHarnessEngine

    try:
        trace_dir.mkdir(parents=True, exist_ok=True)
        framebuffer_dir = (
            Path(args.framebuffer_dir) if args.framebuffer_dir is not None
            else default_framebuffer_dir()
        ) if args.record_framebuffer else None
        # Single playback covers every spec: startup.tjs inside
        # logo_test_oracle.xp3 plays all SEGMENT_ORDER motions sequentially,
        # and cocos2d only accepts one scheduleOnce("startup", ...) per
        # Activity lifetime. mpb.record_all_oracles returns
        # {spec_id: frames} in one shot.
        # record_all_oracles writes the per-game renderer=software preference
        # before startupFrom. Do not write the global preference before
        # HarnessActivity starts: Redroid CI can hang before READY there, and
        # libkrkr2's renderer default is already software.
        with AdbHarnessEngine(serial=args.serial) as engine:
            print(f"[record] capturing {len(specs)} spec(s) from "
                  f"{Path(args.startup_xp3)} "
                  "(Frida-hooked Motion.Player progress)")
            all_frames = mpb.record_all_oracles(
                engine, specs, serial=args.serial,
                playback_timeout=args.playback_timeout,
                framebuffer_dir=framebuffer_dir,
                startup_xp3=Path(args.startup_xp3))
        for spec in specs:
            frames = all_frames.get(spec["id"])
            if frames is None:
                print(f"[record] {spec['id']}: no frames captured; "
                      f"spec id not in SEGMENT_ORDER or playback ended early",
                      file=sys.stderr)
                continue
            target = trace_dir / f"{spec['id']}.oracle.json"
            with target.open("w") as f:
                json.dump(frames, f, indent=2, sort_keys=True)
            print(f"[record] {spec['id']}: wrote {len(frames)} frames "
                  f"to {target}")
        if framebuffer_dir is not None:
            manifest = framebuffer_dir / "manifest.json"
            if manifest.exists():
                print(f"[record] framebuffer manifest: {manifest}")
        return 0
    except Exception as exc:
        print(
            "FAIL: Android motion_playback oracle recording error: "
            f"{type(exc).__name__}: {exc}"
        )
        traceback.print_exc(file=sys.stdout)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
