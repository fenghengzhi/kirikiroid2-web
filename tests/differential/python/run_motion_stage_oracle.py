#!/usr/bin/env python3
"""Record staged Android libkrkr2 motion_playback oracle diagnostics."""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections import Counter
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))


SCHEMA = "motion-stage-oracle-v1"
SOURCE = "android-frida-libkrkr2"


def parse_args(argv: list[str]) -> argparse.Namespace:
    from oracle_runner.frida_motion_stage_tracer import STAGES

    p = argparse.ArgumentParser(
        description="record motion_playback staged Android oracle traces")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of motion_playback spec JSON files")
    p.add_argument("--trace-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "traces" / "motion_playback_stages"),
                   help="Output root for staged oracle JSON files")
    p.add_argument("--serial", required=True,
                   help="ADB serial for the Android oracle harness")
    p.add_argument("--stage", default="all",
                   choices=("all",) + STAGES,
                   help="Stage to write, or all stages")
    p.add_argument("--playback-timeout", type=float, default=90.0,
                   help="Seconds to wait for deterministic playback")
    p.add_argument("--raw-out", default=None,
                   help="Optional path for the unsplit raw staged event stream")
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict[str, Any]]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def selected_stages(stage: str) -> list[str]:
    from oracle_runner.frida_motion_stage_tracer import STAGES

    if stage == "all":
        return list(STAGES)
    return [stage]


def wait_for_stage_trace(
    tracer,
    *,
    expected_frames: int,
    timeout: float,
    poll_interval: float = 0.4,
    stabilise_seconds: float = 2.0,
) -> list[dict[str, Any]]:
    deadline = time.time() + timeout
    last_count = -1
    stable_since: float | None = None
    while time.time() < deadline:
        count = tracer.event_count()
        if count != last_count:
            stable_since = None
            last_count = count
        elif count >= expected_frames and stable_since is None:
            stable_since = time.time()

        if stable_since is not None and \
                time.time() - stable_since >= stabilise_seconds:
            events = tracer.stop_record()
            frames = trace_flatten_frames(events)
            segments = segment_trace_frames(frames)
            substantive = [s for s in segments if len(s["frames"]) >= 30]
            if len(substantive) >= 2:
                return events
            raise RuntimeError(
                f"trace_flatten frame count stabilised at {len(frames)}, "
                f"but only {len(substantive)} substantive segment(s) were "
                f"captured (raw segments: "
                f"{[len(s['frames']) for s in segments]})"
            )
        time.sleep(poll_interval)

    raise RuntimeError(
        f"motion stage oracle did not stabilise within {timeout:.1f}s "
        f"(last trace_flatten frame count: {last_count}, "
        f"raw events: {tracer.raw_event_count()}, "
        f"expected at least {expected_frames})"
    )


def trace_flatten_frames(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        ev for ev in events
        if ev.get("stage") == "trace_flatten" and ev.get("kind") == "frame"
    ]


def segment_trace_frames(frames: list[dict[str, Any]]) -> list[dict[str, Any]]:
    segments: list[dict[str, Any]] = []
    for frame in frames:
        diagnostics = frame.get("diagnostics") or {}
        key = (
            diagnostics.get("objthis")
            or diagnostics.get("topPlayer")
            or frame.get("objthis")
            or frame.get("topPlayer")
        )
        if not segments or segments[-1]["player"] != key:
            segments.append({"player": key, "frames": []})
        segments[-1]["frames"].append(frame)
    return segments


def build_case_segments(
    events: list[dict[str, Any]],
    specs: list[dict[str, Any]],
    mpb,
) -> list[dict[str, Any]]:
    specs_by_id = {s["id"]: s for s in specs}
    unknown = [sid for sid in specs_by_id if sid not in mpb.SEGMENT_ORDER]
    if unknown:
        raise ValueError(
            f"unknown motion_playback spec id(s): {unknown}. Expected ids "
            f"are fixed by logo_test_oracle.xp3: {mpb.SEGMENT_ORDER}."
        )

    frames = trace_flatten_frames(events)
    segments = segment_trace_frames(frames)
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    if len(substantive) < len(specs_by_id):
        raise RuntimeError(
            f"only {len(substantive)} substantive trace_flatten segment(s) "
            f"captured (raw segments: {[len(s['frames']) for s in segments]})."
        )

    out: list[dict[str, Any]] = []
    for i, spec_id in enumerate(mpb.SEGMENT_ORDER):
        if spec_id not in specs_by_id:
            continue
        wanted = int(specs_by_id[spec_id]["frames"])
        frames_for_case = substantive[i]["frames"]
        if len(frames_for_case) < wanted:
            raise RuntimeError(
                f"trace_flatten segment {i} ({spec_id}) has "
                f"{len(frames_for_case)} frames; spec requires {wanted}."
            )
        clipped = frames_for_case[:wanted]
        out.append({
            "caseId": spec_id,
            "spec": specs_by_id[spec_id],
            "player": substantive[i]["player"],
            "frames": clipped,
            "firstSeq": int(clipped[0]["seq"]),
            "lastSeq": int(clipped[-1]["seq"]),
        })
    return out


def assign_case_index(seq: int, case_segments: list[dict[str, Any]]) -> int:
    if not case_segments:
        raise RuntimeError("no case segments available for event assignment")
    for i, seg in enumerate(case_segments):
        if seq <= seg["lastSeq"]:
            return i
        if i + 1 < len(case_segments) and \
                seq < case_segments[i + 1]["firstSeq"]:
            return i + 1
    return len(case_segments) - 1


def split_events_by_stage_and_case(
    events: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
) -> dict[str, dict[str, list[dict[str, Any]]]]:
    out: dict[str, dict[str, list[dict[str, Any]]]] = {}
    for ev in events:
        stage = str(ev.get("stage") or "")
        if not stage:
            continue
        seq = int(ev.get("seq", -1))
        case_index = assign_case_index(seq, case_segments)
        case_id = str(case_segments[case_index]["caseId"])
        out.setdefault(stage, {}).setdefault(case_id, []).append(ev)
    return out


def stage_case_summary(
    *,
    stage: str,
    case_segment: dict[str, Any],
    events: list[dict[str, Any]],
) -> dict[str, Any]:
    kinds = Counter(str(ev.get("kind")) for ev in events)
    seqs = [int(ev["seq"]) for ev in events if "seq" in ev]
    frame_ids = [
        int(ev["frameId"]) for ev in events
        if isinstance(ev.get("frameId"), int)
    ]
    summary: dict[str, Any] = {
        "eventCount": len(events),
        "kindCounts": dict(sorted(kinds.items())),
        "traceFrameCount": len(case_segment["frames"]),
        "traceSeqRange": [case_segment["firstSeq"], case_segment["lastSeq"]],
    }
    if seqs:
        summary["eventSeqRange"] = [min(seqs), max(seqs)]
    if frame_ids:
        summary["eventFrameIdRange"] = [min(frame_ids), max(frame_ids)]

    if stage == "trace_flatten":
        player_counts = [int(f.get("playerCount", 0))
                         for f in case_segment["frames"]]
        layer_counts = [len(f.get("layers") or [])
                        for f in case_segment["frames"]]
        summary["playerCountRange"] = [
            min(player_counts or [0]), max(player_counts or [0])]
        summary["layerCountRange"] = [
            min(layer_counts or [0]), max(layer_counts or [0])]
    return summary


def write_stage_oracles(
    *,
    trace_dir: Path,
    stages: list[str],
    specs: list[dict[str, Any]],
    events: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
) -> list[Path]:
    by_stage_case = split_events_by_stage_and_case(events, case_segments)
    case_by_id = {seg["caseId"]: seg for seg in case_segments}
    spec_by_id = {spec["id"]: spec for spec in specs}
    written: list[Path] = []

    for stage in stages:
        for case_id in spec_by_id:
            case_segment = case_by_id.get(case_id)
            if case_segment is None:
                continue
            case_events = by_stage_case.get(stage, {}).get(case_id, [])
            payload = {
                "schema": SCHEMA,
                "stage": stage,
                "caseId": case_id,
                "source": SOURCE,
                "events": case_events,
                "summary": stage_case_summary(
                    stage=stage,
                    case_segment=case_segment,
                    events=case_events,
                ),
            }
            target = trace_dir / stage / f"{case_id}.oracle.json"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(
                json.dumps(payload, indent=2, ensure_ascii=True,
                           allow_nan=False) + "\n",
                encoding="utf-8",
            )
            written.append(target)
    return written


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)
    stages = selected_stages(args.stage)

    if not spec_dir.exists():
        print(f"spec dir not found: {spec_dir}", file=sys.stderr)
        return 2

    specs = load_specs(spec_dir)
    if not specs:
        print(f"no specs in {spec_dir}", file=sys.stderr)
        return 0

    from oracle_runner.adb_engine import AdbHarnessEngine
    from oracle_runner.adapters import motion_playback as mpb
    from oracle_runner.frida_motion_stage_tracer import FridaMotionStageTracer

    expected_frames = sum(int(spec["frames"]) for spec in specs)

    try:
        with AdbHarnessEngine(serial=args.serial) as engine:
            print(
                f"[record-stage] capturing stages={stages} "
                f"expected_trace_flatten_frames={expected_frames}"
            )
            remote_game = mpb._ensure_logo_test_xp3_pushed(args.serial)
            with FridaMotionStageTracer(engine, device_id=args.serial) as tracer:
                tracer.start_record(stages)
                engine.tjs_init()
                mpb.trigger_startup(engine, remote_game)
                events = wait_for_stage_trace(
                    tracer,
                    expected_frames=expected_frames,
                    timeout=args.playback_timeout,
                )

        case_segments = build_case_segments(events, specs, mpb)
        segment_lengths = [len(seg["frames"]) for seg in case_segments]
        print(f"[record-stage] trace_flatten segments={segment_lengths}")

        if args.raw_out:
            raw_path = Path(args.raw_out)
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_text(
                json.dumps({
                    "schema": SCHEMA,
                    "source": SOURCE,
                    "events": events,
                    "summary": {
                        "eventCount": len(events),
                        "traceFlattenFrameCount":
                            len(trace_flatten_frames(events)),
                        "segmentLengths": segment_lengths,
                    },
                }, indent=2, ensure_ascii=True, allow_nan=False) + "\n",
                encoding="utf-8",
            )
            print(f"[record-stage] wrote raw stream to {raw_path}")

        written = write_stage_oracles(
            trace_dir=trace_dir,
            stages=stages,
            specs=specs,
            events=events,
            case_segments=case_segments,
        )
        for path in written:
            payload = json.loads(path.read_text(encoding="utf-8"))
            print(
                f"[record-stage] {payload['stage']}/{payload['caseId']}: "
                f"{payload['summary']['eventCount']} events -> {path}"
            )
    except Exception as exc:
        print(f"FAIL: motion stage oracle recording error: {exc}",
              file=sys.stderr)
        print(
            "Diagnostics: verify harness APK is installed, frida-server is "
            "running as root, the serial is reachable, and "
            "reference/xp3/logo_test_oracle.xp3 exists.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
