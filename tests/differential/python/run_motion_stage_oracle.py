#!/usr/bin/env python3
"""Record staged Android libkrkr2 motion_playback oracle diagnostics."""

from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
import tempfile
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))
from oracle_runner.png_artifacts import (
    png_manifest_entry,
    write_bgra_png,
    write_rgba_png,
)
from oracle_runner.motion_capture_window import (
    FrameCaptureWindow,
    add_frame_capture_args,
    captured_case_ranges,
    frame_capture_window_from_args,
)


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
    "layer_raw_probe",
    "layer_visual_readback",
)
RENDER_STEP_CHECKPOINT_PHASES: tuple[str, ...] = (
    "execute_pre",
    "execute_post",
    "updateLayerAfterDraw_pre",
    "updateLayerAfterDraw_post",
    "post_draw",
)
RENDER_CAPTURE_SURFACES: tuple[str, ...] = ("initial", "post_draw")
POST_DRAW_CANVAS_SIZE = (1920, 1080)


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
    p.add_argument("--startup-xp3",
                   default=str(REPO_ROOT / "reference" / "xp3" /
                               "logo_test_oracle.xp3"),
                   help="Host path to the oracle startup XP3")
    p.add_argument("--case", action="append", default=[],
                   help="Motion case id to record; repeat for multiple cases. "
                        "Defaults to all specs in --spec-dir")
    p.add_argument("--serial", required=True,
                   help="ADB serial for the Android oracle harness")
    p.add_argument("--stage", default="all",
                   choices=("all", RENDER_PATH_STAGE) + STAGES,
                   help="Stage to write, or all stages")
    p.add_argument("--render-artifact-dir", type=Path, default=None,
                   help="Output directory for --stage render_path artifacts "
                        "(default: tests/differential/artifacts/"
                        "motion_playback_render_stages/<run-id>)")
    p.add_argument("--record-render-step-checkpoints", action="store_true",
                   help="With --stage render_path, capture execute_pre/"
                        "execute_post Layer images around sub_6C7440 and "
                        "updateLayerAfterDraw_pre/post images around "
                        "updateLayerAfterDraw, plus true post_draw after "
                        "startup.tjs onPaint")
    p.add_argument("--checkpoint-render-only", action="store_true",
                   help="With --record-render-step-checkpoints, build render "
                        "PNG artifacts only from Frida Layer MainImage "
                        "checkpoints instead of fixture saveLayerImage PNGs")
    p.add_argument("--record-layer-raw-probes", action="store_true",
                   help="With --stage render_path, capture raw Layer "
                        "MainImage probes at fillRect/saveLayerImage/"
                        "drawCompat/render execute/update boundaries")
    p.add_argument("--record-save-layer-visual-readback-probes",
                   action="store_true",
                   help="With --stage render_path, capture saveLayerImage "
                        "visual readback row hashes")
    p.add_argument("--save-layer-visual-readback-frame-start", type=int,
                   default=0,
                   help="First global frame id for saveLayerImage visual "
                        "readback row probes")
    p.add_argument("--save-layer-visual-readback-frame-count", type=int,
                   default=1,
                   help="Number of global frames to capture visual readback "
                        "rows for; use -1 for all frames")
    p.add_argument("--playback-timeout", type=float, default=90.0,
                   help="Seconds to wait for deterministic playback")
    p.add_argument("--raw-out", default=None,
                   help="Optional path for the unsplit raw staged event stream")
    add_frame_capture_args(p)
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict[str, Any]]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def filter_specs(specs: list[dict[str, Any]],
                 case_ids: list[str]) -> list[dict[str, Any]]:
    if not case_ids:
        return specs
    wanted = {str(case_id) for case_id in case_ids}
    selected = [spec for spec in specs if str(spec.get("id")) in wanted]
    found = {str(spec.get("id")) for spec in selected}
    missing = sorted(wanted - found)
    if missing:
        raise ValueError(f"unknown motion_playback case id(s): {missing}")
    return selected


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
    require_substantive_segments: bool = True,
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
            if not require_substantive_segments:
                return events
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
    capture_window: FrameCaptureWindow | None = None,
) -> list[dict[str, Any]]:
    specs_by_id = {s["id"]: s for s in specs}
    segment_order = mpb.segment_order_for_specs(specs_by_id)

    if capture_window is not None and capture_window.enabled:
        frames_by_id = {
            int(frame["frameId"]): frame
            for frame in trace_flatten_frames(events)
            if isinstance(frame.get("frameId"), int)
        }
        out: list[dict[str, Any]] = []
        for case in captured_case_ranges(
            specs_by_id, segment_order, capture_window):
            selected = [
                frames_by_id[frame_id]
                for frame_id in case["capturedFrameIds"]
                if frame_id in frames_by_id
            ]
            if len(selected) != len(case["capturedFrameIds"]):
                missing = [
                    frame_id for frame_id in case["capturedFrameIds"]
                    if frame_id not in frames_by_id
                ]
                raise RuntimeError(
                    f"missing trace_flatten frame(s) for {case['caseId']}: "
                    f"{missing[:8]}"
                )
            seqs = [int(frame.get("seq", -1)) for frame in selected]
            out.append({
                "caseId": str(case["caseId"]),
                "spec": case["spec"],
                "player": (
                    selected[0].get("diagnostics", {}).get("topPlayer")
                    if selected else None
                ),
                "frames": selected,
                "firstSeq": min(seqs) if seqs else -1,
                "lastSeq": max(seqs) if seqs else -1,
                "firstFrameId": int(case["capturedFrameIdRange"][0]),
                "lastFrameId": int(case["capturedFrameIdRange"][1]) - 1,
                "caseFrameIdBase": int(case["fullFrameIdRange"][0]),
                "fullFrameIdRange": case["fullFrameIdRange"],
                "capturedFrameIdRange": case["capturedFrameIdRange"],
                "capturedLocalFrames": case["capturedLocalFrames"],
            })
        return out

    frames = trace_flatten_frames(events)
    segments = segment_trace_frames(frames)
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    if len(substantive) < len(specs_by_id):
        raise RuntimeError(
            f"only {len(substantive)} substantive trace_flatten segment(s) "
            f"captured (raw segments: {[len(s['frames']) for s in segments]})."
        )

    out: list[dict[str, Any]] = []
    total_frames = sum(int(s["frames"]) for s in specs_by_id.values())
    full_ranges = {
        case["caseId"]: case
        for case in captured_case_ranges(
            specs_by_id, segment_order,
            FrameCaptureWindow(
                mode="all",
                total_frames=total_frames,
                start=0,
                end=total_frames,
            ),
        )
    }
    for i, spec_id in enumerate(segment_order):
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
            "caseFrameIdBase": int(
                full_ranges[spec_id]["fullFrameIdRange"][0]),
            "fullFrameIdRange": full_ranges[spec_id]["fullFrameIdRange"],
            "capturedFrameIdRange": full_ranges[spec_id][
                "capturedFrameIdRange"],
            "capturedLocalFrames": full_ranges[spec_id][
                "capturedLocalFrames"],
        })
    return out


def render_case_frame_bases(
    specs: list[dict[str, Any]],
    mpb,
    capture_window: FrameCaptureWindow,
) -> dict[str, int]:
    specs_by_id = {str(spec["id"]): spec for spec in specs}
    segment_order = mpb.segment_order_for_specs(specs_by_id)
    return {
        str(case["caseId"]): int(case["fullFrameIdRange"][0])
        for case in captured_case_ranges(
            specs_by_id, segment_order, capture_window)
    }


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
        frame_id = ev.get("frameId")
        if isinstance(frame_id, int):
            matched = False
            for i, seg in enumerate(case_segments):
                if int(seg["firstFrameId"]) <= frame_id <= \
                        int(seg["lastFrameId"]):
                    case_id = str(case_segments[i]["caseId"])
                    out.setdefault(stage, {}).setdefault(case_id, []).append(ev)
                    matched = True
                    break
            if matched:
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


def merge_case_lists(
    existing: list[dict[str, Any]],
    incoming: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    order: list[str] = []
    by_id: dict[str, dict[str, Any]] = {}
    for item in [*existing, *incoming]:
        case_id = str(item.get("caseId"))
        if case_id not in by_id:
            order.append(case_id)
        by_id[case_id] = item
    return [by_id[case_id] for case_id in order]


def count_manifest_images(cases: list[dict[str, Any]]) -> int:
    total = 0
    for case in cases:
        phases = case.get("phases", {})
        if not isinstance(phases, dict):
            continue
        for images in phases.values():
            if isinstance(images, list):
                total += len(images)
    return total


def count_manifest_events(artifact_dir: Path,
                          cases: list[dict[str, Any]]) -> int:
    total = 0
    for case in cases:
        event_files = case.get("eventFiles", {})
        if not isinstance(event_files, dict):
            continue
        for rel in event_files.values():
            if not isinstance(rel, str):
                continue
            path = artifact_dir / rel
            if not path.exists():
                continue
            data = json.loads(path.read_text(encoding="utf-8"))
            events = data.get("events", [])
            if isinstance(events, list):
                total += len(events)
    return total


def merge_unique(existing: list[Any], incoming: list[Any]) -> list[Any]:
    out: list[Any] = []
    for value in [*existing, *incoming]:
        if value is not None and value not in out:
            out.append(value)
    return out


def merge_render_stage_manifest(
    artifact_dir: Path,
    manifest: dict[str, Any],
) -> dict[str, Any]:
    target = artifact_dir / "manifest.json"
    if not target.exists():
        return manifest

    existing = json.loads(target.read_text(encoding="utf-8"))
    merged = dict(existing)
    merged["generatedAt"] = manifest.get("generatedAt")
    merged["stages"] = merge_unique(
        list(existing.get("stages", [])),
        list(manifest.get("stages", [])),
    )
    merged["startupXp3s"] = merge_unique(
        list(existing.get("startupXp3s", [
            existing.get("fixture", {}).get("xp3")
            if isinstance(existing.get("fixture"), dict) else None
        ])),
        [manifest.get("fixture", {}).get("xp3")
         if isinstance(manifest.get("fixture"), dict) else None],
    )

    cases = merge_case_lists(
        list(existing.get("cases", [])),
        list(manifest.get("cases", [])),
    )
    merged["cases"] = cases

    fixture = dict(existing.get("fixture", {}))
    incoming_fixture = manifest.get("fixture", {})
    if isinstance(incoming_fixture, dict):
        fixture["segmentOrder"] = merge_unique(
            list(fixture.get("segmentOrder", [])),
            list(incoming_fixture.get("segmentOrder", [])),
        )
    merged["fixture"] = fixture

    existing_images = existing.get("images", {})
    incoming_images = manifest.get("images", {})
    if isinstance(existing_images, dict) and isinstance(incoming_images, dict):
        image_cases = merge_case_lists(
            list(existing_images.get("cases", [])),
            list(incoming_images.get("cases", [])),
        )
        image_manifest = dict(existing_images)
        image_manifest["captureSurfaces"] = merge_unique(
            list(existing_images.get("captureSurfaces", [])),
            list(incoming_images.get("captureSurfaces", [])),
        )
        image_manifest["cases"] = image_cases
        image_manifest["summary"] = {
            **dict(existing_images.get("summary", {})),
            "caseCount": len(image_cases),
            "imageCount": count_manifest_images(image_cases),
        }
        merged["images"] = image_manifest

    merged["summary"] = {
        **dict(existing.get("summary", {})),
        "caseCount": len(cases),
        "traceFlattenFrameCount": sum(int(case.get("frames", 0))
                                      for case in cases),
        "eventCount": count_manifest_events(artifact_dir, cases),
        "imageCount": (
            merged.get("images", {}).get("summary", {}).get("imageCount", 0)
            if isinstance(merged.get("images"), dict) else 0
        ),
    }
    return merged


def layer_save_events_for_case(
    case_images: dict[str, Any],
    case_segment: dict[str, Any],
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    case_id = str(case_images["caseId"])
    first_frame_id = int(case_segment.get(
        "caseFrameIdBase", case_segment["firstFrameId"]))
    seq = 0
    for phase in RENDER_CAPTURE_SURFACES:
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
                **({"rgbaSha256": image["rgbaSha256"]}
                   if "rgbaSha256" in image else {}),
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


def enrich_draw_dispatch_events_for_case(
    events: list[dict[str, Any]],
    case_segment: dict[str, Any],
    case_images: dict[str, Any],
) -> list[dict[str, Any]]:
    first_frame_id = int(case_segment.get(
        "caseFrameIdBase", case_segment["firstFrameId"]))
    last_frame_id = int(case_segment["lastFrameId"])
    initial_image = _phase_images_by_frame(case_images, "initial").get(0)
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
        post_draw = post_by_frame.get(local_frame)

        kind = str(event.get("kind") or "")
        if kind == "draw_leave":
            event["initialImage"] = initial_image
            event["postDrawImage"] = post_draw
            if initial_image is None:
                _add_image_manifest_error(
                    event, "missing initial image")
            if post_draw is None:
                _add_image_manifest_error(
                    event, f"missing post_draw image for frame {local_frame}")

        enriched.append(event)

    return enriched


def _semantic_render_item(item: dict[str, Any]) -> dict[str, Any]:
    flags = item.get("flags")
    clip_valid = (
        bool(flags.get("clipValid21"))
        if isinstance(flags, dict) else False
    )
    clip_rect = item.get("clipRect") if clip_valid else [0, 0, 0, 0]
    build_clip_rect = (
        item.get("buildClipRect", item.get("clipRect"))
        if clip_valid else [0, 0, 0, 0]
    )
    return {
        "index": item.get("index"),
        "nodeIndex": item.get("nodeIndex"),
        "sourceKey": item.get("sourceKey"),
        "flags": flags,
        "layerIds": item.get("layerIds"),
        "sortKey64": item.get("sortKey64"),
        "paintBox": item.get("paintBox"),
        "clipRect": clip_rect,
        "buildClipRect": build_clip_rect,
        "dirtyRect": item.get("dirtyRect"),
        "viewportRect": item.get("viewportRect"),
        "sourceGate232": item.get("sourceGate232"),
        "stencilType244": item.get("stencilType244"),
        "parentItemIndex": item.get("parentItemIndex"),
        "parentItem264": item.get("parentItem264"),
        "childItemCount": item.get("childItemCount"),
        "meshType280": item.get("meshType280"),
        "leafLayerVariantTag": item.get("leafLayerVariantTag"),
        "composedLayerVariantTag": item.get("composedLayerVariantTag"),
        "leafLayerVariantTag320": item.get("leafLayerVariantTag320"),
        "composedLayerVariantTag340": item.get("composedLayerVariantTag340"),
        "leafBuilt": item.get("leafBuilt"),
        "composedBuilt": item.get("composedBuilt"),
        "executedDirect": item.get("executedDirect"),
    }


def _has_valid_clip_rect(item: dict[str, Any]) -> bool:
    flags = item.get("flags")
    flag16 = bool(flags.get("flag16")) if isinstance(flags, dict) else False
    flag17 = bool(flags.get("flag17")) if isinstance(flags, dict) else False
    if flag16 or flag17:
        return False
    source_gate = item.get("sourceGate232")
    try:
        if int(source_gate) == 0:
            return False
    except (TypeError, ValueError):
        pass
    clip_valid = (
        bool(flags.get("clipValid21"))
        if isinstance(flags, dict) else False
    )
    rect = (
        item.get("buildClipRect", item.get("clipRect"))
        if clip_valid else item.get("paintBox")
    )
    if not clip_valid:
        viewport = item.get("viewportRect")
        if isinstance(rect, list) and len(rect) == 4 and \
                isinstance(viewport, list) and len(viewport) == 4:
            try:
                v_left, v_top, v_right, v_bottom = (
                    float(value) for value in viewport)
                left, top, right, bottom = (float(value) for value in rect)
            except (TypeError, ValueError):
                pass
            else:
                if v_right >= v_left and v_bottom >= v_top:
                    rect = [
                        max(left, math.floor(v_left)),
                        max(top, math.floor(v_top)),
                        min(right, math.ceil(v_right)),
                        min(bottom, math.ceil(v_bottom)),
                    ]
    if not isinstance(rect, list) or len(rect) != 4:
        return False
    try:
        left, top, right, bottom = (float(value) for value in rect)
    except (TypeError, ValueError):
        return False
    return left < right and top < bottom


def _build_flow_summary(event: dict[str, Any]) -> dict[str, Any]:
    if isinstance(event.get("buildFlow"), dict):
        return dict(event["buildFlow"])
    main_items = event.get("mainListSemanticItems")
    if isinstance(main_items, list):
        main_semantic_items = [
            _semantic_render_item(item)
            for item in main_items if isinstance(item, dict)
        ]
        aux_items = event.get("auxListSemanticItems")
        aux_semantic_items = [
            _semantic_render_item(item)
            for item in aux_items if isinstance(item, dict)
        ] if isinstance(aux_items, list) else []
        return {
            "inputItemCount": event.get("inputItemCount"),
            "builtItemCount": event.get("builtItemCount",
                                        len(main_semantic_items)),
            "validDrawableItemCount": event.get("validDrawableItemCount"),
            "leafBuiltCount": event.get("leafBuiltCount"),
            "composedBuiltCount": event.get("composedBuiltCount"),
            "mainListSemanticItems": main_semantic_items,
            "auxListSemanticItems": aux_semantic_items,
            "items": main_semantic_items,
        }
    render_lists = event.get("renderLists")
    main_list = (
        render_lists.get("mainList")
        if isinstance(render_lists, dict) else None
    )
    items = main_list.get("items") if isinstance(main_list, dict) else []
    semantic_items = [
        _semantic_render_item(item)
        for item in items if isinstance(item, dict)
    ]
    return {
        "inputItemCount": main_list.get("count")
        if isinstance(main_list, dict) else None,
        "builtItemCount": len(semantic_items),
        "validDrawableItemCount": sum(
            1 for item in semantic_items if _has_valid_clip_rect(item)),
        "leafBuiltCount": None,
        "composedBuiltCount": None,
        "mainListSemanticItems": semantic_items,
        "auxListSemanticItems": [],
        "items": semantic_items,
    }


def enrich_render_commands_events_for_case(
    events: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    enriched: list[dict[str, Any]] = []
    for source_event in events:
        event = dict(source_event)
        if str(event.get("kind") or "").startswith("build_commands"):
            event["buildFlow"] = _build_flow_summary(event)
        enriched.append(event)
    return enriched


def enrich_render_execute_events_for_case(
    events: list[dict[str, Any]],
    case_segment: dict[str, Any],
    case_images: dict[str, Any],
) -> list[dict[str, Any]]:
    first_frame_id = int(case_segment.get(
        "caseFrameIdBase", case_segment["firstFrameId"]))
    last_frame_id = int(case_segment["lastFrameId"])
    pre_by_frame = _phase_images_by_frame(case_images, "execute_pre")
    post_by_frame = _phase_images_by_frame(case_images, "execute_post")
    update_pre_by_frame = _phase_images_by_frame(
        case_images, "updateLayerAfterDraw_pre")
    update_post_by_frame = _phase_images_by_frame(
        case_images, "updateLayerAfterDraw_post")
    post_draw_by_frame = _phase_images_by_frame(case_images, "post_draw")
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
        execute_pre = pre_by_frame.get(local_frame)
        execute_post = post_by_frame.get(local_frame)
        update_pre = update_pre_by_frame.get(local_frame)
        update_post = update_post_by_frame.get(local_frame)
        post_draw = post_draw_by_frame.get(local_frame)
        kind = str(event.get("kind") or "")
        if kind == "execute_enter":
            event["executePreImage"] = execute_pre
            if execute_pre is None:
                _add_image_manifest_error(
                    event, f"missing execute_pre image for frame {local_frame}")
        elif kind == "execute_leave":
            event["executePreImage"] = execute_pre
            event["executePostImage"] = execute_post
            event["updateLayerAfterDrawPreImage"] = update_pre
            event["updateLayerAfterDrawPostImage"] = update_post
            event["postDrawImage"] = post_draw
            if execute_pre is None:
                _add_image_manifest_error(
                    event, f"missing execute_pre image for frame {local_frame}")
            if execute_post is None:
                _add_image_manifest_error(
                    event, f"missing execute_post image for frame {local_frame}")
            if update_pre is None:
                _add_image_manifest_error(
                    event,
                    "missing updateLayerAfterDraw_pre image for frame "
                    f"{local_frame}")
            if update_post is None:
                _add_image_manifest_error(
                    event,
                    "missing updateLayerAfterDraw_post image for frame "
                    f"{local_frame}")
            if post_draw is None:
                _add_image_manifest_error(
                    event, f"missing post_draw image for frame {local_frame}")
        enriched.append(event)
    return enriched


def add_oracle_execute_checkpoint_images(
    *,
    artifact_dir: Path,
    image_manifest: dict[str, Any],
    checkpoints: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
    strict: bool = True,
) -> dict[str, Any]:
    by_frame_phase: dict[tuple[int, str], dict[str, Any]] = {}
    for checkpoint in checkpoints:
        frame_id = checkpoint.get("frameId")
        phase = checkpoint.get("phase")
        if isinstance(frame_id, int) and isinstance(phase, str):
            by_frame_phase[(frame_id, phase)] = checkpoint

    case_by_id = {
        str(case["caseId"]): case for case in image_manifest.get("cases", [])
    }
    total_added = 0
    for segment in case_segments:
        case_id = str(segment["caseId"])
        case = case_by_id.get(case_id)
        if case is None:
            continue
        phases = case.setdefault("phases", {})
        first_frame_id = int(segment.get(
            "caseFrameIdBase", segment["firstFrameId"]))
        local_frames = list(segment.get(
            "capturedLocalFrames",
            range(len(segment["frames"])),
        ))
        for phase in RENDER_STEP_CHECKPOINT_PHASES:
            images: list[dict[str, Any]] = []
            for local_frame in local_frames:
                frame_id = first_frame_id + local_frame
                checkpoint = by_frame_phase.get((frame_id, phase))
                if checkpoint is None:
                    if phase == "post_draw":
                        continue
                    if not strict and phase != "post_draw":
                        continue
                    raise RuntimeError(
                        f"missing oracle {phase} checkpoint for "
                        f"{case_id} frame {local_frame} (frameId {frame_id})")
                if not checkpoint.get("ok"):
                    diagnostics = checkpoint.get("diagnostics")
                    diagnostic_suffix = (
                        f" diagnostics={json.dumps(diagnostics, sort_keys=True)}"
                        if isinstance(diagnostics, dict) and diagnostics else ""
                    )
                    raise RuntimeError(
                        f"oracle {phase} checkpoint failed for "
                        f"{case_id} frame {local_frame}: "
                        f"{checkpoint.get('error')}{diagnostic_suffix}")
                raw_path_value = checkpoint.get("rawPath")
                if not isinstance(raw_path_value, str):
                    raise RuntimeError(
                        f"oracle {phase} checkpoint has no rawPath for "
                        f"{case_id} frame {local_frame}")
                if phase == "post_draw":
                    size = (
                        int(checkpoint["width"]),
                        int(checkpoint["height"]),
                    )
                    if size != POST_DRAW_CANVAS_SIZE:
                        raise RuntimeError(
                            f"oracle post_draw checkpoint for {case_id} "
                            f"frame {local_frame} captured {size[0]}x{size[1]}, "
                            f"expected {POST_DRAW_CANVAS_SIZE[0]}x"
                            f"{POST_DRAW_CANVAS_SIZE[1]} canvas")
                rel = Path("images") / case_id / phase / \
                    f"frame_{local_frame:04d}.png"
                path = artifact_dir / rel
                pixel_format = checkpoint.get("pixelFormat")
                if pixel_format == "bgra32":
                    write_bgra_png(
                        raw_path=Path(raw_path_value),
                        path=path,
                        width=int(checkpoint["width"]),
                        height=int(checkpoint["height"]),
                    )
                elif pixel_format == "rgba32":
                    write_rgba_png(
                        raw_path=Path(raw_path_value),
                        path=path,
                        width=int(checkpoint["width"]),
                        height=int(checkpoint["height"]),
                    )
                elif pixel_format == "rgba32-bottom-left":
                    write_rgba_png(
                        raw_path=Path(raw_path_value),
                        path=path,
                        width=int(checkpoint["width"]),
                        height=int(checkpoint["height"]),
                        bottom_left_origin=True,
                    )
                else:
                    raise RuntimeError(
                        f"oracle {phase} checkpoint has unsupported "
                        f"pixelFormat for {case_id} frame {local_frame}: "
                        f"{pixel_format}")
                entry = png_manifest_entry(
                    frame=local_frame,
                    phase=phase,
                    path=path,
                    rel=rel,
                )
                diagnostics = checkpoint.get("diagnostics")
                if isinstance(diagnostics, dict) and diagnostics:
                    entry["diagnostics"] = {
                        "rawCheckpoint": diagnostics,
                    }
                images.append(entry)
                try:
                    Path(raw_path_value).unlink()
                except FileNotFoundError:
                    pass
            phases[phase] = images
            total_added += len(images)
            if phase == "post_draw" and not images:
                raise RuntimeError(
                    f"no oracle post_draw checkpoints captured for {case_id}")

    surfaces = list(image_manifest.get("captureSurfaces", []))
    for phase in RENDER_STEP_CHECKPOINT_PHASES:
        if phase not in surfaces:
            surfaces.append(phase)
    image_manifest["captureSurfaces"] = surfaces
    summary = dict(image_manifest.get("summary") or {})
    summary["imageCount"] = int(summary.get("imageCount", 0)) + total_added
    image_manifest["summary"] = summary
    return image_manifest


def checkpoint_only_image_manifest(
    *,
    artifact_dir: Path,
    specs: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
    remote_capture_root: str | None,
    capture_window: FrameCaptureWindow,
) -> dict[str, Any]:
    images_root = artifact_dir / "images"
    images_root.mkdir(parents=True, exist_ok=True)

    specs_by_id = {str(spec["id"]): spec for spec in specs}
    cases: list[dict[str, Any]] = []
    for segment in case_segments:
        case_id = str(segment["caseId"])
        case_dir = images_root / case_id
        if case_dir.exists():
            shutil.rmtree(case_dir)
        spec = segment.get("spec") or specs_by_id.get(case_id, {})
        captured_local_frames = list(segment.get(
            "capturedLocalFrames", range(len(segment.get("frames", [])))))
        cases.append({
            "caseId": case_id,
            "mtnPath": spec.get("mtn_path"),
            "chara": spec.get("chara"),
            "label": spec.get("label"),
            "frames": int(spec.get("frames", len(segment.get("frames", [])))),
            "fullFrameIdRange": segment.get("fullFrameIdRange"),
            "capturedFrameIdRange": segment.get("capturedFrameIdRange"),
            "capturedLocalFrames": captured_local_frames,
            "requestedLocalFrames": captured_local_frames,
            "phases": {},
        })

    return {
        "remoteCaptureRoot": remote_capture_root,
        "captureSurfaces": [],
        "cases": cases,
        "summary": {
            "caseCount": len(cases),
            "imageCount": 0,
        },
        **capture_window.manifest_fields(),
    }


def write_render_stage_artifacts(
    *,
    artifact_dir: Path,
    stages: list[str],
    specs: list[dict[str, Any]],
    events: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
    image_manifest: dict[str, Any],
    renderer_metadata: dict[str, str],
    capture_window: FrameCaptureWindow,
    startup_xp3: Path,
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
                elif stage == "render_commands":
                    stage_events = enrich_render_commands_events_for_case(
                        stage_events)
                elif stage == "render_execute":
                    stage_events = enrich_render_execute_events_for_case(
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
        **renderer_metadata,
        "generatedAt": datetime.now(timezone.utc)
        .replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "localRoot": str(artifact_dir),
        "remoteCaptureRoot": image_manifest.get("remoteCaptureRoot"),
        "fixture": {
            "xp3": str(startup_xp3),
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
        **capture_window.manifest_fields(),
        "cases": [
            {
                "caseId": seg["caseId"],
                "frames": len(seg["frames"]),
                "frameIdRange": [seg["firstFrameId"], seg["lastFrameId"]],
                "fullFrameIdRange": seg.get("fullFrameIdRange"),
                "capturedFrameIdRange": seg.get("capturedFrameIdRange"),
                "capturedLocalFrames": seg.get("capturedLocalFrames", []),
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
    manifest = merge_render_stage_manifest(artifact_dir, manifest)
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
    startup_xp3 = Path(args.startup_xp3)
    stages = selected_stages(args.stage)
    render_path = args.stage == RENDER_PATH_STAGE
    if args.record_render_step_checkpoints and not render_path:
        print("--record-render-step-checkpoints requires --stage render_path",
              file=sys.stderr)
        return 2
    if args.checkpoint_render_only and not args.record_render_step_checkpoints:
        print("--checkpoint-render-only requires "
              "--record-render-step-checkpoints", file=sys.stderr)
        return 2
    if args.record_layer_raw_probes and not render_path:
        print("--record-layer-raw-probes requires --stage render_path",
              file=sys.stderr)
        return 2
    if args.record_save_layer_visual_readback_probes and not render_path:
        print("--record-save-layer-visual-readback-probes requires "
              "--stage render_path", file=sys.stderr)
        return 2
    render_artifact_dir = (
        Path(args.render_artifact_dir)
        if args.render_artifact_dir is not None
        else default_render_artifact_dir()
    ) if render_path else None

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

    from oracle_runner.adb_engine import AdbHarnessEngine
    from oracle_runner.adapters import motion_playback as mpb
    from oracle_runner.frida_motion_stage_tracer import FridaMotionStageTracer

    renderer_metadata = mpb.oracle_renderer_metadata()
    total_frames = sum(int(spec["frames"]) for spec in specs)
    try:
        capture_window = frame_capture_window_from_args(args, total_frames)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    expected_frames = capture_window.driven_frames
    specs_by_id = {spec["id"]: spec for spec in specs}
    render_step_checkpoints: list[dict[str, Any]] = []

    try:
        mpb.ensure_oracle_renderer_software(args.serial)
        with AdbHarnessEngine(serial=args.serial) as engine:
            print(
                f"[record-stage] capturing stages={stages} "
                f"driven_frames={expected_frames} "
                f"capture={capture_window.filter_manifest()}"
            )
            if render_path:
                assert render_artifact_dir is not None
                remote_game, remote_render_root = \
                    mpb._prepare_render_stage_capture(
                        args.serial, specs_by_id,
                        render_artifact_dir, capture_window,
                        startup_xp3=startup_xp3)
            else:
                remote_game = mpb._ensure_logo_test_xp3_pushed(
                    args.serial, startup_xp3)
                remote_render_root = None
            mpb.ensure_oracle_renderer_software(
                args.serial, remote_game=remote_game, write_global=False)

            with FridaMotionStageTracer(
                engine, device_id=args.serial) as tracer:
                checkpoint_raw_dir = (
                    render_artifact_dir / ".oracle_execute_raw"
                    if (
                        args.record_render_step_checkpoints or
                        args.record_layer_raw_probes
                    ) and
                    render_artifact_dir is not None else None
                )
                tracer.configure_image_checkpoints(checkpoint_raw_dir)
                tracer.start_record(
                    stages,
                    record_render_step_checkpoints=(
                        args.record_render_step_checkpoints),
                    record_layer_raw_probes=(
                        args.record_layer_raw_probes),
                    record_save_layer_visual_readback_probes=(
                        args.record_save_layer_visual_readback_probes),
                    save_layer_visual_readback_frame_start=(
                        args.save_layer_visual_readback_frame_start),
                    save_layer_visual_readback_frame_count=(
                        args.save_layer_visual_readback_frame_count),
                    capture_frame_start=capture_window.start,
                    capture_frame_count=(
                        -1 if not capture_window.enabled
                        else capture_window.count),
                    render_case_frame_bases=render_case_frame_bases(
                        specs, mpb, capture_window),
                )
                engine.tjs_init()
                mpb.trigger_startup(engine, remote_game)
                events = wait_for_stage_trace(
                    tracer,
                    expected_frames=expected_frames,
                    timeout=args.playback_timeout,
                    stabilise_seconds=5.0 if render_path else 2.0,
                    require_substantive_segments=(
                        not capture_window.enabled and len(specs) > 1),
                )
                render_step_checkpoints = tracer.image_checkpoints()

        case_segments = build_case_segments(
            events, specs, mpb, capture_window)
        segment_lengths = [len(seg["frames"]) for seg in case_segments]
        print(f"[record-stage] trace_flatten segments={segment_lengths}")

        if args.raw_out:
            raw_path = Path(args.raw_out)
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_text(
                json.dumps({
                    "schema": RENDER_SCHEMA if render_path else SCHEMA,
                    "source": RENDER_SOURCE if render_path else SOURCE,
                    **renderer_metadata,
                    "events": events,
                    "summary": {
                        "eventCount": len(events),
                        "traceFlattenFrameCount":
                            len(trace_flatten_frames(events)),
                        "segmentLengths": segment_lengths,
                        **capture_window.manifest_fields(),
                    },
                }, indent=2, ensure_ascii=True, allow_nan=False) + "\n",
                encoding="utf-8",
            )
            print(f"[record-stage] wrote raw stream to {raw_path}")

        if render_path:
            assert render_artifact_dir is not None
            if args.checkpoint_render_only:
                image_manifest = checkpoint_only_image_manifest(
                    artifact_dir=render_artifact_dir,
                    specs=specs,
                    case_segments=case_segments,
                    remote_capture_root=remote_render_root,
                    capture_window=capture_window,
                )
            else:
                assert remote_render_root is not None
                image_manifest = mpb._collect_render_stage_capture(
                    args.serial, specs_by_id, render_artifact_dir,
                    remote_render_root, timeout=args.playback_timeout,
                    capture_window=capture_window)
            if args.record_render_step_checkpoints:
                image_manifest = add_oracle_execute_checkpoint_images(
                    artifact_dir=render_artifact_dir,
                    image_manifest=image_manifest,
                    checkpoints=render_step_checkpoints,
                    case_segments=case_segments,
                    strict=not args.checkpoint_render_only,
                )
            written = write_render_stage_artifacts(
                artifact_dir=render_artifact_dir,
                stages=stages,
                specs=specs,
                events=events,
                case_segments=case_segments,
                image_manifest=image_manifest,
                renderer_metadata=renderer_metadata,
                capture_window=capture_window,
                startup_xp3=startup_xp3,
            )
            if args.checkpoint_render_only and remote_render_root is not None:
                mpb._adb_shell_root(args.serial, ["rm", "-rf",
                                                  remote_render_root])
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
