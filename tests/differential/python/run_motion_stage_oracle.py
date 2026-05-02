#!/usr/bin/env python3
"""Record staged Android libkrkr2 motion_playback oracle diagnostics."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))


SCHEMA = "motion-stage-oracle-v1"
SOURCE = "android-frida-libkrkr2"
RENDER_SCHEMA = "motion-render-stage-oracle-v1"
RENDER_SOURCE = "android-frida-libkrkr2-render"
RENDER_PATH_STAGE = "render_path"
RENDER_STAGES: tuple[str, ...] = (
    "draw_dispatch",
    "render_prepare",
    "render_commands",
    "render_execute",
    "layer_save",
)


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
                   choices=("all", RENDER_PATH_STAGE) + STAGES,
                   help="Stage to write, or all stages")
    p.add_argument("--render-artifact-dir", type=Path, default=None,
                   help="Output directory for --stage render_path artifacts "
                        "(default: tests/differential/artifacts/"
                        "motion_playback_render_stages/<run-id>)")
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
    if stage == RENDER_PATH_STAGE:
        return list(RENDER_STAGES)
    return [stage]


def default_render_artifact_dir() -> Path:
    run_id = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    return (
        REPO_ROOT / "tests" / "differential" / "artifacts"
        / "motion_playback_render_stages" / run_id
    )


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
            "firstFrameId": int(clipped[0].get("frameId", 0)),
            "lastFrameId": int(clipped[-1].get("frameId", wanted - 1)),
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


def assign_render_case_index(
    ev: dict[str, Any],
    case_segments: list[dict[str, Any]],
) -> int:
    frame_id = ev.get("frameId")
    if isinstance(frame_id, int):
        for i, seg in enumerate(case_segments):
            if int(seg["firstFrameId"]) <= frame_id <= int(seg["lastFrameId"]):
                return i
    return assign_case_index(int(ev.get("seq", -1)), case_segments)


def split_render_events_by_stage_and_case(
    events: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
) -> dict[str, dict[str, list[dict[str, Any]]]]:
    out: dict[str, dict[str, list[dict[str, Any]]]] = {}
    render_stage_set = set(RENDER_STAGES)
    for ev in events:
        stage = str(ev.get("stage") or "")
        if stage not in render_stage_set or stage == "layer_save":
            continue
        case_index = assign_render_case_index(ev, case_segments)
        case_id = str(case_segments[case_index]["caseId"])
        cloned = dict(ev)
        cloned["caseId"] = case_id
        out.setdefault(stage, {}).setdefault(case_id, []).append(cloned)
    return out


def render_stage_summary(
    events: list[dict[str, Any]],
    trace_frame_count: int,
) -> dict[str, Any]:
    kinds = Counter(str(ev.get("kind")) for ev in events)
    frame_ids = [
        int(ev["frameId"]) for ev in events
        if isinstance(ev.get("frameId"), int)
    ]
    seqs = [int(ev["seq"]) for ev in events if "seq" in ev]
    summary: dict[str, Any] = {
        "eventCount": len(events),
        "kindCounts": dict(sorted(kinds.items())),
        "traceFrameCount": trace_frame_count,
        "framesWithEvents": len(set(frame_ids)),
    }
    if frame_ids:
        summary["eventFrameIdRange"] = [min(frame_ids), max(frame_ids)]
    if seqs:
        summary["eventSeqRange"] = [min(seqs), max(seqs)]
    return summary


def layer_save_events_for_case(
    case_images: dict[str, Any],
    case_segment: dict[str, Any],
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    case_id = str(case_images["caseId"])
    first_frame_id = int(case_segment["firstFrameId"])
    seq = 0
    for phase in ("pre_draw", "post_draw"):
        for image in case_images.get("phases", {}).get(phase, []):
            local_frame = int(image["frame"])
            events.append({
                "schema": "motion-render-stage-oracle-v1-event",
                "source": RENDER_SOURCE,
                "stage": "layer_save",
                "kind": "save_layer_image",
                "samplePoint": f"startup.tjs.{phase}",
                "caseId": case_id,
                "frameId": first_frame_id + local_frame,
                "frame": local_frame,
                "seq": seq,
                "phase": phase,
                "path": image["path"],
                "width": image["width"],
                "height": image["height"],
                "bytes": image["bytes"],
                "sha256": image["sha256"],
                "diagnostics": {
                    "synthetic": True,
                    "source": "pulled-png-manifest",
                },
            })
            seq += 1
    return events


def _phase_images_by_frame(
    case_images: dict[str, Any],
    phase: str,
) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    for image in case_images.get("phases", {}).get(phase, []):
        frame = image.get("frame")
        if isinstance(frame, int):
            out[frame] = dict(image)
    return out


def _add_image_manifest_error(
    event: dict[str, Any],
    message: str,
) -> None:
    diagnostics = dict(event.get("diagnostics") or {})
    existing = diagnostics.get("imageManifestError")
    if existing:
        diagnostics["imageManifestError"] = f"{existing}; {message}"
    else:
        diagnostics["imageManifestError"] = message
    event["diagnostics"] = diagnostics


def _set_draw_path_image_changed(
    event: dict[str, Any],
    image_changed: bool | None,
) -> None:
    draw_path = event.get("drawPath")
    if isinstance(draw_path, dict):
        updated = dict(draw_path)
        updated["imageChanged"] = image_changed
        event["drawPath"] = updated


def enrich_draw_dispatch_events_for_case(
    events: list[dict[str, Any]],
    case_segment: dict[str, Any],
    case_images: dict[str, Any],
) -> list[dict[str, Any]]:
    first_frame_id = int(case_segment["firstFrameId"])
    last_frame_id = int(case_segment["lastFrameId"])
    pre_by_frame = _phase_images_by_frame(case_images, "pre_draw")
    post_by_frame = _phase_images_by_frame(case_images, "post_draw")
    enriched: list[dict[str, Any]] = []

    for source_event in events:
        event = dict(source_event)
        frame_id = event.get("frameId")
        if not isinstance(frame_id, int):
            _add_image_manifest_error(event, "event has no integer frameId")
            enriched.append(event)
            continue
        if frame_id < first_frame_id or frame_id > last_frame_id:
            _add_image_manifest_error(
                event,
                f"frameId {frame_id} outside case segment "
                f"{first_frame_id}..{last_frame_id}",
            )
            enriched.append(event)
            continue

        local_frame = frame_id - first_frame_id
        pre_draw = pre_by_frame.get(local_frame)
        post_draw = post_by_frame.get(local_frame)

        kind = str(event.get("kind") or "")
        if kind == "draw_enter":
            event["preDrawImage"] = pre_draw
            if pre_draw is None:
                _add_image_manifest_error(
                    event, f"missing pre_draw image for frame {local_frame}")
        elif kind == "draw_leave":
            event["preDrawImage"] = pre_draw
            event["postDrawImage"] = post_draw
            image_changed = (
                None if pre_draw is None or post_draw is None
                else pre_draw.get("sha256") != post_draw.get("sha256")
            )
            event["imageChanged"] = image_changed
            _set_draw_path_image_changed(event, image_changed)
            if pre_draw is None:
                _add_image_manifest_error(
                    event, f"missing pre_draw image for frame {local_frame}")
            if post_draw is None:
                _add_image_manifest_error(
                    event, f"missing post_draw image for frame {local_frame}")

        enriched.append(event)

    return enriched


def write_render_stage_artifacts(
    *,
    artifact_dir: Path,
    stages: list[str],
    specs: list[dict[str, Any]],
    events: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
    image_manifest: dict[str, Any],
) -> list[Path]:
    events_by_stage_case = split_render_events_by_stage_and_case(
        events, case_segments)
    case_by_id = {seg["caseId"]: seg for seg in case_segments}
    image_case_by_id = {
        case["caseId"]: case for case in image_manifest.get("cases", [])
    }
    written: list[Path] = []
    events_root = artifact_dir / "events"
    stage_set = set(stages)
    total_event_count = 0

    for stage in RENDER_STAGES:
        if stage not in stage_set:
            continue
        for spec in specs:
            case_id = str(spec["id"])
            case_segment = case_by_id.get(case_id)
            if case_segment is None:
                continue
            if stage == "layer_save":
                stage_events = layer_save_events_for_case(
                    image_case_by_id.get(case_id, {
                        "caseId": case_id,
                        "phases": {},
                    }),
                    case_segment,
                )
            else:
                stage_events = (
                    events_by_stage_case.get(stage, {}).get(case_id, [])
                )
                if stage == "draw_dispatch":
                    stage_events = enrich_draw_dispatch_events_for_case(
                        stage_events,
                        case_segment,
                        image_case_by_id.get(case_id, {
                            "caseId": case_id,
                            "phases": {},
                        }),
                    )
            total_event_count += len(stage_events)
            payload = {
                "schema": RENDER_SCHEMA,
                "source": RENDER_SOURCE,
                "stage": stage,
                "caseId": case_id,
                "events": stage_events,
                "summary": render_stage_summary(
                    stage_events, len(case_segment["frames"])),
            }
            target = events_root / stage / f"{case_id}.oracle.json"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(
                json.dumps(payload, indent=2, ensure_ascii=True,
                           allow_nan=False) + "\n",
                encoding="utf-8",
            )
            written.append(target)

    manifest = {
        "schema": RENDER_SCHEMA,
        "source": RENDER_SOURCE,
        "generatedAt": datetime.now(timezone.utc)
        .replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "localRoot": str(artifact_dir),
        "remoteCaptureRoot": image_manifest.get("remoteCaptureRoot"),
        "fixture": {
            "xp3": "logo_test_render_stage_oracle.xp3",
            "window": {"width": 1920, "height": 1080},
            "deltaMs": 1000.0 / 60.0,
            "segmentOrder": [s["caseId"] for s in case_segments],
        },
        "stages": list(stages),
        "eventsRoot": "events",
        "imagesRoot": "images",
        "images": image_manifest,
        "summary": {
            "caseCount": len(case_segments),
            "traceFlattenFrameCount": len(trace_flatten_frames(events)),
            "eventCount": total_event_count,
            "imageCount": image_manifest.get("summary", {}).get(
                "imageCount", 0),
        },
        "cases": [
            {
                "caseId": seg["caseId"],
                "frames": len(seg["frames"]),
                "frameIdRange": [seg["firstFrameId"], seg["lastFrameId"]],
                "traceSeqRange": [seg["firstSeq"], seg["lastSeq"]],
                "eventFiles": {
                    stage: str(
                        (Path("events") / stage /
                         f"{seg['caseId']}.oracle.json").as_posix()
                    )
                    for stage in stages
                },
            }
            for seg in case_segments
        ],
    }
    manifest_path = artifact_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True,
                   allow_nan=False) + "\n",
        encoding="utf-8",
    )
    written.append(manifest_path)
    return written


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)
    stages = selected_stages(args.stage)
    render_path = args.stage == RENDER_PATH_STAGE
    render_artifact_dir = (
        Path(args.render_artifact_dir)
        if args.render_artifact_dir is not None
        else default_render_artifact_dir()
    ) if render_path else None

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
    specs_by_id = {spec["id"]: spec for spec in specs}
    temp_dir: tempfile.TemporaryDirectory[str] | None = None

    try:
        try:
            with AdbHarnessEngine(serial=args.serial) as engine:
                print(
                    f"[record-stage] capturing stages={stages} "
                    f"expected_trace_flatten_frames={expected_frames}"
                )
                if render_path:
                    assert render_artifact_dir is not None
                    temp_dir = tempfile.TemporaryDirectory(
                        prefix="krkr2-motion-render-stage-xp3-")
                    remote_game, remote_render_root = \
                        mpb._prepare_render_stage_capture(
                            args.serial, specs_by_id,
                            render_artifact_dir, Path(temp_dir.name))
                else:
                    remote_game = mpb._ensure_logo_test_xp3_pushed(
                        args.serial)
                    remote_render_root = None

                with FridaMotionStageTracer(
                    engine, device_id=args.serial) as tracer:
                    tracer.start_record(stages)
                    engine.tjs_init()
                    mpb.trigger_startup(engine, remote_game)
                    events = wait_for_stage_trace(
                        tracer,
                        expected_frames=expected_frames,
                        timeout=args.playback_timeout,
                        stabilise_seconds=5.0 if render_path else 2.0,
                    )
        finally:
            if temp_dir is not None:
                temp_dir.cleanup()

        case_segments = build_case_segments(events, specs, mpb)
        segment_lengths = [len(seg["frames"]) for seg in case_segments]
        print(f"[record-stage] trace_flatten segments={segment_lengths}")

        if args.raw_out:
            raw_path = Path(args.raw_out)
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_text(
                json.dumps({
                    "schema": RENDER_SCHEMA if render_path else SCHEMA,
                    "source": RENDER_SOURCE if render_path else SOURCE,
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

        if render_path:
            assert render_artifact_dir is not None
            assert remote_render_root is not None
            image_manifest = mpb._collect_render_stage_capture(
                args.serial, specs_by_id, render_artifact_dir,
                remote_render_root, timeout=args.playback_timeout)
            written = write_render_stage_artifacts(
                artifact_dir=render_artifact_dir,
                stages=stages,
                specs=specs,
                events=events,
                case_segments=case_segments,
                image_manifest=image_manifest,
            )
            print(
                f"[record-stage] render artifact manifest: "
                f"{render_artifact_dir / 'manifest.json'}"
            )
        else:
            written = write_stage_oracles(
                trace_dir=trace_dir,
                stages=stages,
                specs=specs,
                events=events,
                case_segments=case_segments,
            )
        for path in written:
            payload = json.loads(path.read_text(encoding="utf-8"))
            if payload.get("stage") and payload.get("caseId"):
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
