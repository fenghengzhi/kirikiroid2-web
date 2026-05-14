#!/usr/bin/env python3
"""Native macOS LLDB verifier for staged motion_playback diagnostics."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))

SCHEMA = "motion-stage-oracle-v1"
SOURCE = "native-lldb-macos"
STATIC_PARSE_PROJECTION = "static_parse-semantic-v1"
STATIC_PARSE_SAMPLE_POINTS = {
    "init_non_emote_enter": "initNonEmoteMotionLike_0x6B365C.enter",
    "init_non_emote_leave": "initNonEmoteMotionLike_0x6B365C.leave",
    "parse_parameter_enter": "appendParameterEntryLike_0x6B1718.enter",
    "parse_parameter_leave": "appendParameterEntryLike_0x6B1718.leave",
    "parse_parameter_list_enter": "parseParameterListLike_0x6B202C.enter",
    "parse_parameter_list_leave": "parseParameterListLike_0x6B202C.leave",
}
STATIC_PARSE_PARAMETER_FIELDS = (
    "index", "id", "discretization", "rangeBegin", "rangeEnd", "value", "mode",
)
INIT_MOTION_PROJECTION = "init-motion-semantic-v1"
INIT_MOTION_SAMPLE_POINTS = {
    "init_non_emote_enter": "initNonEmoteMotionLike_0x6B365C.enter",
    "init_non_emote_leave": "initNonEmoteMotionLike_0x6B365C.leave",
}
INIT_MOTION_OVERVIEW_FIELDS = (
    "nodeCount", "parameterTable", "playing",
    "currentTime",
)
TRACE_FLATTEN_PROJECTION = "trace_flatten-semantic-v1"
TRACE_FLATTEN_SAMPLE_POINT = "progressCompat.phase3-end.pre-cleanup"
TRACE_FLATTEN_NUM_FIELDS: tuple[str, ...] = (
    "posX", "posY", "posZ", "angleDeg",
    "scaleX", "scaleY", "slantX", "slantY",
)
TRACE_FLATTEN_INT_FIELDS: tuple[str, ...] = (
    "index", "nodeType", "opacity", "stencilType",
)
TRACE_FLATTEN_BOOL_FIELDS: tuple[str, ...] = (
    "visible", "active", "flipX", "flipY",
)
FRAME_SELECTION_PROJECTION_SPEC = json.loads(
    (
        REPO_ROOT / "tests" / "differential" /
        "motion_stage_projections" / "frame_selection_v1.json"
    ).read_text(encoding="utf-8")
)
FRAME_SELECTION_PROJECTION = str(FRAME_SELECTION_PROJECTION_SPEC["projection"])
FRAME_SELECTION_SAMPLE_POINT = str(FRAME_SELECTION_PROJECTION_SPEC["samplePoint"])
FRAME_SELECTION_EVENT_FIELDS: tuple[str, ...] = tuple(
    str(field) for field in FRAME_SELECTION_PROJECTION_SPEC["eventFields"]
)
FRAME_SELECTION_NODE_FIELDS: tuple[str, ...] = tuple(
    str(field) for field in FRAME_SELECTION_PROJECTION_SPEC["nodeFields"]
)
STAGES: tuple[str, ...] = (
    "static_parse",
    "init_motion",
    "variable_binding",
    "frame_selection",
    "sub_motion_decision",
    "trace_flatten",
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="motion_playback native staged differential runner")
    p.add_argument("--runner", default=None,
                   help="Path to the motion_playback_native executable")
    p.add_argument("--startup-xp3",
                   default=str(REPO_ROOT / "reference" / "xp3" /
                               "logo_test_oracle.xp3"),
                   help="Host path to logo_test_oracle.xp3")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of motion_playback spec JSON files")
    p.add_argument("--trace-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "traces" / "motion_playback_stages"),
                   help="Directory of staged Android oracle JSONs")
    p.add_argument("--stage", default="all",
                   help="Stage to compare, comma-separated stages, or all")
    p.add_argument("--lldb-timeout", type=float, default=240.0,
                   help="Timeout for the macOS LLDB native tracer")
    p.add_argument("--raw-out", default=None,
                   help="Optional path for the unsplit raw native event stream")
    p.add_argument("--only-structural", action="store_true",
                   help="For trace_flatten, diff only structural layer fields")
    p.add_argument("--strict-missing-trace", action="store_true",
                   help="Fail when a staged Android oracle is missing")
    return p.parse_args(argv)


def load_specs(spec_dir: Path) -> list[dict[str, Any]]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def selected_stages(stage: str) -> list[str]:
    if stage == "all":
        return list(STAGES)
    stages = [item.strip() for item in stage.split(",") if item.strip()]
    unknown = sorted(set(stages) - set(STAGES))
    if unknown:
        raise ValueError(f"unknown stage(s): {unknown}; expected {STAGES}")
    return stages


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


def run_native_stage_trace(
    *,
    runner: Path,
    startup_xp3: Path,
    expected_frames: int,
    timeout: float,
    stages: list[str],
) -> list[dict[str, Any]]:
    if not runner.exists():
        raise FileNotFoundError(
            f"native runner not found: {runner}. Build with "
            "`cmake --build <native-build-dir> --target motion_playback_native`."
        )
    if not startup_xp3.exists():
        raise FileNotFoundError(f"oracle bootstrap xp3 missing: {startup_xp3}")
    if sys.platform != "darwin":
        raise RuntimeError("native staged LLDB trace is only supported on macOS")

    with tempfile.TemporaryDirectory(prefix="krkr2-motion-stage-native-") as td:
        trace_path = Path(td) / "stage-trace.json"
        tracer = REPO_ROOT / "tests" / "differential" / "python" / \
            "native_lldb_motion_stage_trace.py"
        stage_arg = "all" if set(stages) == set(STAGES) else ",".join(stages)
        cmd = [
            "xcrun", "python3", str(tracer),
            "--runner", str(runner),
            "--startup-xp3", str(startup_xp3),
            "--trace-out", str(trace_path),
            "--expected-frames", str(expected_frames),
            "--timeout", str(timeout),
            "--repo-root", str(REPO_ROOT),
            "--stages", stage_arg,
        ]
        try:
            proc = subprocess.run(
                cmd,
                cwd=str(REPO_ROOT),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=timeout + 20.0,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(
                f"native staged LLDB trace timed out after "
                f"{timeout + 20.0:.1f}s\nstdout:\n{exc.stdout or ''}\n"
                f"stderr:\n{exc.stderr or ''}"
            ) from exc
        if proc.returncode != 0:
            raise RuntimeError(
                f"native staged LLDB tracer failed with exit code "
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
                "native staged LLDB tracer did not write trace output\n"
                f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
            )
        try:
            events = json.loads(trace_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"native staged trace JSON decode failed: {exc}\n"
                f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
            ) from exc
    if not isinstance(events, list):
        raise RuntimeError(f"native staged trace root is not a list: {type(events)}")
    return events


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
    segment_order = mpb.segment_order_for_specs(specs_by_id)

    frames = trace_flatten_frames(events)
    segments = segment_trace_frames(frames)
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    if len(substantive) < len(specs_by_id):
        raise RuntimeError(
            f"only {len(substantive)} substantive native trace_flatten "
            f"segment(s) captured (raw segments: "
            f"{[len(s['frames']) for s in segments]})."
        )

    out: list[dict[str, Any]] = []
    for i, spec_id in enumerate(segment_order):
        wanted = int(specs_by_id[spec_id]["frames"])
        frames_for_case = substantive[i]["frames"]
        if len(frames_for_case) < wanted:
            raise RuntimeError(
                f"native trace_flatten segment {i} ({spec_id}) has "
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


def build_native_payloads(
    *,
    stages: list[str],
    specs: list[dict[str, Any]],
    events: list[dict[str, Any]],
    case_segments: list[dict[str, Any]],
) -> dict[str, dict[str, dict[str, Any]]]:
    by_stage_case = split_events_by_stage_and_case(events, case_segments)
    case_by_id = {seg["caseId"]: seg for seg in case_segments}
    spec_by_id = {spec["id"]: spec for spec in specs}
    payloads: dict[str, dict[str, dict[str, Any]]] = {}
    for stage in stages:
        for case_id in spec_by_id:
            case_segment = case_by_id.get(case_id)
            if case_segment is None:
                continue
            case_events = by_stage_case.get(stage, {}).get(case_id, [])
            payloads.setdefault(stage, {})[case_id] = {
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
    return payloads


def load_oracle_payload(trace_dir: Path, stage: str, case_id: str) -> dict[str, Any] | None:
    path = trace_dir / stage / f"{case_id}.oracle.json"
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def trace_flatten_schema_mismatches(
    events: list[dict[str, Any]],
    *,
    side: str,
) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    forbidden_event_fields = ("objthis", "topPlayer", "layout", "error")
    forbidden_layer_fields = (
        "blendMode", "sourcePlayer", "label", "currentImage",
    )
    for frame_index, ev in enumerate(events):
        if ev.get("projection") != TRACE_FLATTEN_PROJECTION:
            mismatches.append({
                "kind": "trace_flatten_schema",
                "side": side,
                "frame": frame_index,
                "field": "projection",
                "value": ev.get("projection"),
                "expected": TRACE_FLATTEN_PROJECTION,
            })
        if ev.get("samplePoint") != TRACE_FLATTEN_SAMPLE_POINT:
            mismatches.append({
                "kind": "trace_flatten_schema",
                "side": side,
                "frame": frame_index,
                "field": "samplePoint",
                "value": ev.get("samplePoint"),
                "expected": TRACE_FLATTEN_SAMPLE_POINT,
            })
        diagnostics = ev.get("diagnostics")
        if not isinstance(diagnostics, dict):
            mismatches.append({
                "kind": "trace_flatten_schema",
                "side": side,
                "frame": frame_index,
                "field": "diagnostics",
                "value": type(diagnostics).__name__,
                "expected": "dict",
            })
        for field in forbidden_event_fields:
            if field in ev:
                mismatches.append({
                    "kind": "trace_flatten_schema",
                    "side": side,
                    "frame": frame_index,
                    "field": field,
                    "reason": "diagnostic_field_must_not_be_top_level",
                })
        for layer_index, layer in enumerate(ev.get("layers") or []):
            for field in forbidden_layer_fields:
                if field in layer:
                    mismatches.append({
                        "kind": "trace_flatten_schema",
                        "side": side,
                        "frame": frame_index,
                        "layer_index": layer_index,
                        "field": field,
                        "reason": "non_semantic_layer_field",
                    })
            if "stencilType" not in layer:
                mismatches.append({
                    "kind": "trace_flatten_schema",
                    "side": side,
                    "frame": frame_index,
                    "layer_index": layer_index,
                    "field": "stencilType",
                    "reason": "missing_semantic_layer_field",
                })
    return mismatches


def static_parse_schema_mismatches(
    events: list[dict[str, Any]],
    *,
    side: str,
) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    forbidden_top_level = (
        "addr", "player", "x0", "x1", "retval", "frameId", "objthis",
    )
    forbidden_table_fields = (
        "begin", "end", "stride", "defaultParameterEntry",
        "defaultParameterEntryIndex",
    )
    forbidden_entry_fields = ("ptr", "idPtr")
    for event_index, ev in enumerate(events):
        kind = str(ev.get("kind"))
        if ev.get("projection") != STATIC_PARSE_PROJECTION:
            mismatches.append({
                "kind": "static_parse_schema",
                "side": side,
                "event": event_index,
                "field": "projection",
                "value": ev.get("projection"),
                "expected": STATIC_PARSE_PROJECTION,
            })
        expected_sample = STATIC_PARSE_SAMPLE_POINTS.get(kind)
        if ev.get("samplePoint") != expected_sample:
            mismatches.append({
                "kind": "static_parse_schema",
                "side": side,
                "event": event_index,
                "field": "samplePoint",
                "value": ev.get("samplePoint"),
                "expected": expected_sample,
            })
        if not isinstance(ev.get("diagnostics"), dict):
            mismatches.append({
                "kind": "static_parse_schema",
                "side": side,
                "event": event_index,
                "field": "diagnostics",
                "value": type(ev.get("diagnostics")).__name__,
                "expected": "dict",
            })
        for field in forbidden_top_level:
            if field in ev:
                mismatches.append({
                    "kind": "static_parse_schema",
                    "side": side,
                    "event": event_index,
                    "field": field,
                    "reason": "diagnostic_field_must_not_be_top_level",
                })

        table = ev.get("parameterTable")
        if kind == "init_non_emote_leave":
            if not isinstance(table, dict):
                mismatches.append({
                    "kind": "static_parse_schema",
                    "side": side,
                    "event": event_index,
                    "field": "parameterTable",
                    "value": type(table).__name__,
                    "expected": "dict",
                })
                continue
            for field in forbidden_table_fields:
                if field in table:
                    mismatches.append({
                        "kind": "static_parse_schema",
                        "side": side,
                        "event": event_index,
                        "field": f"parameterTable.{field}",
                        "reason": "layout_field_must_be_diagnostic",
                    })
            entries = table.get("entries")
            if not isinstance(table.get("count"), int):
                mismatches.append({
                    "kind": "static_parse_schema",
                    "side": side,
                    "event": event_index,
                    "field": "parameterTable.count",
                    "value": type(table.get("count")).__name__,
                    "expected": "int",
                })
            if not isinstance(entries, list):
                mismatches.append({
                    "kind": "static_parse_schema",
                    "side": side,
                    "event": event_index,
                    "field": "parameterTable.entries",
                    "value": type(entries).__name__,
                    "expected": "list",
                })
                continue
            if isinstance(table.get("count"), int) and table["count"] != len(entries):
                mismatches.append({
                    "kind": "static_parse_schema",
                    "side": side,
                    "event": event_index,
                    "field": "parameterTable.count",
                    "value": table["count"],
                    "expected": len(entries),
                })
            for entry_index, entry in enumerate(entries):
                if not isinstance(entry, dict):
                    mismatches.append({
                        "kind": "static_parse_schema",
                        "side": side,
                        "event": event_index,
                        "entry": entry_index,
                        "field": "parameterTable.entries[]",
                        "value": type(entry).__name__,
                        "expected": "dict",
                    })
                    continue
                for field in STATIC_PARSE_PARAMETER_FIELDS:
                    if field not in entry:
                        mismatches.append({
                            "kind": "static_parse_schema",
                            "side": side,
                            "event": event_index,
                            "entry": entry_index,
                            "field": field,
                            "reason": "missing_semantic_parameter_field",
                        })
                for field in forbidden_entry_fields:
                    if field in entry:
                        mismatches.append({
                            "kind": "static_parse_schema",
                            "side": side,
                            "event": event_index,
                            "entry": entry_index,
                            "field": field,
                            "reason": "layout_field_must_be_diagnostic",
                        })
                if "discretization" in entry and \
                        not isinstance(entry["discretization"], bool):
                    mismatches.append({
                        "kind": "static_parse_schema",
                        "side": side,
                        "event": event_index,
                        "entry": entry_index,
                        "field": "discretization",
                        "value": type(entry["discretization"]).__name__,
                        "expected": "bool",
                    })
        elif table is not None:
            mismatches.append({
                "kind": "static_parse_schema",
                "side": side,
                "event": event_index,
                "field": "parameterTable",
                "reason": "only_init_non_emote_leave_has_parameter_table",
            })
    return mismatches


def normalize_static_parse_events(
    events: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for ev in events:
        item = {
            "kind": ev.get("kind"),
            "samplePoint": ev.get("samplePoint"),
        }
        if ev.get("kind") == "init_non_emote_leave":
            item["parameterTable"] = ev.get("parameterTable")
        out.append(item)
    return out


def diff_static_parse_events(
    native_payload: dict[str, Any],
    oracle_payload: dict[str, Any],
) -> list[dict[str, Any]]:
    n_events = native_payload.get("events") or []
    o_events = oracle_payload.get("events") or []
    n_summary = native_payload.get("summary") or {}
    o_summary = oracle_payload.get("summary") or {}
    mismatches = (
        static_parse_schema_mismatches(n_events, side="native")
        + static_parse_schema_mismatches(o_events, side="oracle")
    )
    if len(n_events) != len(o_events):
        mismatches.append({
            "kind": "event_count",
            "native": len(n_events),
            "oracle": len(o_events),
        })
    if n_summary.get("kindCounts") != o_summary.get("kindCounts"):
        mismatches.append({
            "kind": "kind_counts",
            "native": n_summary.get("kindCounts"),
            "oracle": o_summary.get("kindCounts"),
        })
    n_kinds = [ev.get("kind") for ev in n_events]
    o_kinds = [ev.get("kind") for ev in o_events]
    if n_kinds != o_kinds:
        mismatches.append({
            "kind": "event_order",
            "native": n_kinds,
            "oracle": o_kinds,
        })

    n_semantic = normalize_static_parse_events(n_events)
    o_semantic = normalize_static_parse_events(o_events)
    for i, (native_ev, oracle_ev) in enumerate(zip(n_semantic, o_semantic)):
        if native_ev != oracle_ev:
            mismatches.append({
                "kind": "semantic_event",
                "event": i,
                "native": native_ev,
                "oracle": oracle_ev,
            })
    return mismatches


def init_motion_schema_mismatches(
    events: list[dict[str, Any]],
    *,
    side: str,
) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    forbidden_top_level = (
        "addr", "player", "retval", "frameId", "objthis", "nodeLayout",
    )
    for event_index, ev in enumerate(events):
        kind = str(ev.get("kind"))
        if ev.get("projection") != INIT_MOTION_PROJECTION:
            mismatches.append({
                "kind": "init_motion_schema",
                "side": side,
                "event": event_index,
                "field": "projection",
                "value": ev.get("projection"),
                "expected": INIT_MOTION_PROJECTION,
            })
        expected_sample = INIT_MOTION_SAMPLE_POINTS.get(kind)
        if ev.get("samplePoint") != expected_sample:
            mismatches.append({
                "kind": "init_motion_schema",
                "side": side,
                "event": event_index,
                "field": "samplePoint",
                "value": ev.get("samplePoint"),
                "expected": expected_sample,
            })
        diagnostics = ev.get("diagnostics")
        if not isinstance(diagnostics, dict):
            mismatches.append({
                "kind": "init_motion_schema",
                "side": side,
                "event": event_index,
                "field": "diagnostics",
                "value": type(diagnostics).__name__,
                "expected": "dict",
            })
        for field in forbidden_top_level:
            if field in ev:
                mismatches.append({
                    "kind": "init_motion_schema",
                    "side": side,
                    "event": event_index,
                    "field": field,
                    "reason": "diagnostic_field_must_not_be_top_level",
                })
        overview = ev.get("overview")
        if kind == "init_non_emote_leave":
            if not isinstance(overview, dict):
                mismatches.append({
                    "kind": "init_motion_schema",
                    "side": side,
                    "event": event_index,
                    "field": "overview",
                    "value": type(overview).__name__,
                    "expected": "dict",
                })
                continue
            for field in INIT_MOTION_OVERVIEW_FIELDS:
                if field not in overview:
                    mismatches.append({
                        "kind": "init_motion_schema",
                        "side": side,
                        "event": event_index,
                        "field": f"overview.{field}",
                        "reason": "missing_semantic_overview_field",
                    })
            if "nodeLayout" in overview:
                mismatches.append({
                    "kind": "init_motion_schema",
                    "side": side,
                    "event": event_index,
                    "field": "overview.nodeLayout",
                    "reason": "layout_field_must_be_diagnostic",
                })
        elif overview is not None:
            mismatches.append({
                "kind": "init_motion_schema",
                "side": side,
                "event": event_index,
                "field": "overview",
                "reason": "only_init_non_emote_leave_has_overview",
            })
    return mismatches


def _overview_field_equal(a: Any, b: Any) -> bool:
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return _floats_close(float(a), float(b), rel=1e-6, abs_=1e-6)
    return a == b


def diff_init_motion_events(
    native_payload: dict[str, Any],
    oracle_payload: dict[str, Any],
) -> list[dict[str, Any]]:
    n_events = native_payload.get("events") or []
    o_events = oracle_payload.get("events") or []
    n_summary = native_payload.get("summary") or {}
    o_summary = oracle_payload.get("summary") or {}
    mismatches = (
        init_motion_schema_mismatches(n_events, side="native")
        + init_motion_schema_mismatches(o_events, side="oracle")
    )
    if len(n_events) != len(o_events):
        mismatches.append({
            "kind": "event_count",
            "native": len(n_events),
            "oracle": len(o_events),
        })
    if n_summary.get("kindCounts") != o_summary.get("kindCounts"):
        mismatches.append({
            "kind": "kind_counts",
            "native": n_summary.get("kindCounts"),
            "oracle": o_summary.get("kindCounts"),
        })
    n_kinds = [ev.get("kind") for ev in n_events]
    o_kinds = [ev.get("kind") for ev in o_events]
    if n_kinds != o_kinds:
        mismatches.append({
            "kind": "event_order",
            "native": n_kinds,
            "oracle": o_kinds,
        })

    for i, (native_ev, oracle_ev) in enumerate(zip(n_events, o_events)):
        if native_ev.get("samplePoint") != oracle_ev.get("samplePoint"):
            mismatches.append({
                "kind": "sample_point",
                "event": i,
                "native": native_ev.get("samplePoint"),
                "oracle": oracle_ev.get("samplePoint"),
            })
        if native_ev.get("kind") != "init_non_emote_leave":
            continue
        n_overview = native_ev.get("overview") or {}
        o_overview = oracle_ev.get("overview") or {}
        for field in INIT_MOTION_OVERVIEW_FIELDS:
            n_value = n_overview.get(field)
            o_value = o_overview.get(field)
            if not _overview_field_equal(n_value, o_value):
                mismatches.append({
                    "kind": "semantic_overview",
                    "event": i,
                    "field": field,
                    "native": n_value,
                    "oracle": o_value,
                })
    return mismatches


def frame_selection_schema_mismatches(
    events: list[dict[str, Any]],
    *,
    side: str,
) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    forbidden_top_level = (
        "addr", "node", "timeRaw", "objthis", "parameter",
        "parameterEntry",
    )
    forbidden_node_fields = (
        "ptr", "parameterEntry", "parameter", "idPtr",
    )
    for event_index, ev in enumerate(events):
        kind = str(ev.get("kind"))
        if ev.get("projection") != FRAME_SELECTION_PROJECTION:
            mismatches.append({
                "kind": "frame_selection_schema",
                "side": side,
                "event": event_index,
                "field": "projection",
                "value": ev.get("projection"),
                "expected": FRAME_SELECTION_PROJECTION,
            })
        if ev.get("samplePoint") != FRAME_SELECTION_SAMPLE_POINT:
            mismatches.append({
                "kind": "frame_selection_schema",
                "side": side,
                "event": event_index,
                "field": "samplePoint",
                "value": ev.get("samplePoint"),
                "expected": FRAME_SELECTION_SAMPLE_POINT,
            })
        if kind != "evaluate_timeline":
            mismatches.append({
                "kind": "frame_selection_schema",
                "side": side,
                "event": event_index,
                "field": "kind",
                "value": kind,
                "expected": "evaluate_timeline",
            })
        diagnostics = ev.get("diagnostics")
        if not isinstance(diagnostics, dict):
            mismatches.append({
                "kind": "frame_selection_schema",
                "side": side,
                "event": event_index,
                "field": "diagnostics",
                "value": type(diagnostics).__name__,
                "expected": "dict",
            })
        for field in forbidden_top_level:
            if field in ev:
                mismatches.append({
                    "kind": "frame_selection_schema",
                    "side": side,
                    "event": event_index,
                    "field": field,
                    "reason": "diagnostic_field_must_not_be_top_level",
                })
        for field in FRAME_SELECTION_EVENT_FIELDS:
            if field not in ev:
                mismatches.append({
                    "kind": "frame_selection_schema",
                    "side": side,
                    "event": event_index,
                    "field": field,
                    "reason": "missing_semantic_event_field",
                })
        for node_field_name in ("before", "after"):
            node = ev.get(node_field_name)
            if not isinstance(node, dict):
                mismatches.append({
                    "kind": "frame_selection_schema",
                    "side": side,
                    "event": event_index,
                    "field": node_field_name,
                    "value": type(node).__name__,
                    "expected": "dict",
                })
                continue
            for field in FRAME_SELECTION_NODE_FIELDS:
                if field not in node:
                    mismatches.append({
                        "kind": "frame_selection_schema",
                        "side": side,
                        "event": event_index,
                        "field": f"{node_field_name}.{field}",
                        "reason": "missing_semantic_node_field",
                    })
            for field in forbidden_node_fields:
                if field in node:
                    mismatches.append({
                        "kind": "frame_selection_schema",
                        "side": side,
                        "event": event_index,
                        "field": f"{node_field_name}.{field}",
                        "reason": "diagnostic_field_must_not_be_semantic",
                    })
    return mismatches


def normalize_frame_selection_node(node: Any) -> dict[str, Any] | None:
    if not isinstance(node, dict):
        return None
    return {
        field: node.get(field)
        for field in FRAME_SELECTION_NODE_FIELDS
    }


def normalize_frame_selection_events(
    events: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for ev in events:
        out.append({
            "kind": ev.get("kind"),
            "samplePoint": ev.get("samplePoint"),
            "frameId": ev.get("frameId"),
            "dirtyArg": ev.get("dirtyArg"),
            "time": ev.get("time"),
            "retval": ev.get("retval"),
            "before": normalize_frame_selection_node(ev.get("before")),
            "after": normalize_frame_selection_node(ev.get("after")),
        })
    return out


def _semantic_value_equal(a: Any, b: Any) -> bool:
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return _floats_close(float(a), float(b), rel=1e-6, abs_=1e-6)
    return a == b


def _diff_frame_selection_node(
    *,
    side_path: str,
    event_index: int,
    native_node: dict[str, Any] | None,
    oracle_node: dict[str, Any] | None,
) -> list[dict[str, Any]]:
    if native_node is None or oracle_node is None:
        if native_node == oracle_node:
            return []
        return [{
            "kind": "semantic_node",
            "event": event_index,
            "field": side_path,
            "native": native_node,
            "oracle": oracle_node,
        }]
    out: list[dict[str, Any]] = []
    for field in FRAME_SELECTION_NODE_FIELDS:
        native_value = native_node.get(field)
        oracle_value = oracle_node.get(field)
        if not _semantic_value_equal(native_value, oracle_value):
            out.append({
                "kind": "semantic_node",
                "event": event_index,
                "field": f"{side_path}.{field}",
                "native": native_value,
                "oracle": oracle_value,
            })
    return out


def diff_frame_selection_events(
    native_payload: dict[str, Any],
    oracle_payload: dict[str, Any],
) -> list[dict[str, Any]]:
    n_events = native_payload.get("events") or []
    o_events = oracle_payload.get("events") or []
    n_summary = native_payload.get("summary") or {}
    o_summary = oracle_payload.get("summary") or {}
    mismatches = (
        frame_selection_schema_mismatches(n_events, side="native")
        + frame_selection_schema_mismatches(o_events, side="oracle")
    )
    if len(n_events) != len(o_events):
        mismatches.append({
            "kind": "event_count",
            "native": len(n_events),
            "oracle": len(o_events),
        })
    if n_summary.get("kindCounts") != o_summary.get("kindCounts"):
        mismatches.append({
            "kind": "kind_counts",
            "native": n_summary.get("kindCounts"),
            "oracle": o_summary.get("kindCounts"),
        })
    n_kinds = [ev.get("kind") for ev in n_events]
    o_kinds = [ev.get("kind") for ev in o_events]
    if n_kinds != o_kinds:
        first_mismatch = None
        for i, (native_kind, oracle_kind) in enumerate(zip(n_kinds, o_kinds)):
            if native_kind != oracle_kind:
                first_mismatch = i
                break
        mismatches.append({
            "kind": "event_order",
            "nativeCount": len(n_kinds),
            "oracleCount": len(o_kinds),
            "firstMismatch": first_mismatch,
            "nativeSample": n_kinds[:10],
            "oracleSample": o_kinds[:10],
        })
    n_frames = frame_ids(n_events)
    o_frames = frame_ids(o_events)
    if n_frames != o_frames:
        mismatches.append({
            "kind": "frame_coverage",
            "nativeRange": [
                min(n_frames) if n_frames else None,
                max(n_frames) if n_frames else None,
            ],
            "oracleRange": [
                min(o_frames) if o_frames else None,
                max(o_frames) if o_frames else None,
            ],
            "nativeOnlySample": sorted(n_frames - o_frames)[:10],
            "oracleOnlySample": sorted(o_frames - n_frames)[:10],
        })

    n_semantic = normalize_frame_selection_events(n_events)
    o_semantic = normalize_frame_selection_events(o_events)
    for i, (native_ev, oracle_ev) in enumerate(zip(n_semantic, o_semantic)):
        for field in ("kind", "samplePoint", "frameId", "dirtyArg", "retval"):
            if native_ev.get(field) != oracle_ev.get(field):
                mismatches.append({
                    "kind": "semantic_event",
                    "event": i,
                    "field": field,
                    "native": native_ev.get(field),
                    "oracle": oracle_ev.get(field),
                })
        if not _semantic_value_equal(native_ev.get("time"), oracle_ev.get("time")):
            mismatches.append({
                "kind": "semantic_event",
                "event": i,
                "field": "time",
                "native": native_ev.get("time"),
                "oracle": oracle_ev.get("time"),
            })
        mismatches.extend(_diff_frame_selection_node(
            side_path="before",
            event_index=i,
            native_node=native_ev.get("before"),
            oracle_node=oracle_ev.get("before"),
        ))
        mismatches.extend(_diff_frame_selection_node(
            side_path="after",
            event_index=i,
            native_node=native_ev.get("after"),
            oracle_node=oracle_ev.get("after"),
        ))
    return mismatches


def normalize_trace_flatten_events(
    events: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    fields = (
        TRACE_FLATTEN_NUM_FIELDS
        + TRACE_FLATTEN_INT_FIELDS
        + TRACE_FLATTEN_BOOL_FIELDS
    )
    out = []
    for frame_index, ev in enumerate(events):
        layers = [
            {field: layer.get(field) for field in fields}
            for layer in ev.get("layers", [])
        ]
        out.append({"frame": frame_index, "layers": layers})
    return out


def _floats_close(a: float, b: float, *, rel: float, abs_: float) -> bool:
    return abs(a - b) <= max(abs_, rel * max(abs(a), abs(b)))


def diff_trace_flatten_frames(
    native_frames: list[dict[str, Any]],
    oracle_frames: list[dict[str, Any]],
    *,
    structural_only: bool,
    rel: float = 1e-6,
    abs_: float = 1e-6,
) -> list[dict[str, Any]]:
    int_fields = TRACE_FLATTEN_INT_FIELDS
    bool_fields = TRACE_FLATTEN_BOOL_FIELDS
    num_fields = () if structural_only else TRACE_FLATTEN_NUM_FIELDS
    mismatches: list[dict[str, Any]] = []
    frame_count = min(len(native_frames), len(oracle_frames))
    if len(native_frames) != len(oracle_frames):
        mismatches.append({
            "kind": "frame_count",
            "native": len(native_frames),
            "oracle": len(oracle_frames),
        })
    for frame_index in range(frame_count):
        native_layers = native_frames[frame_index].get("layers", [])
        oracle_layers = oracle_frames[frame_index].get("layers", [])
        if len(native_layers) != len(oracle_layers):
            mismatches.append({
                "kind": "layer_count",
                "frame": frame_index,
                "native": len(native_layers),
                "oracle": len(oracle_layers),
            })
        for layer_index in range(min(len(native_layers), len(oracle_layers))):
            native_layer = native_layers[layer_index]
            oracle_layer = oracle_layers[layer_index]
            for field in int_fields + bool_fields:
                if native_layer.get(field) != oracle_layer.get(field):
                    mismatches.append({
                        "kind": "field",
                        "frame": frame_index,
                        "layer_index": layer_index,
                        "field": field,
                        "native": native_layer.get(field),
                        "oracle": oracle_layer.get(field),
                    })
            for field in num_fields:
                native_value = native_layer.get(field)
                oracle_value = oracle_layer.get(field)
                if native_value is None or oracle_value is None:
                    if native_value != oracle_value:
                        mismatches.append({
                            "kind": "field",
                            "frame": frame_index,
                            "layer_index": layer_index,
                            "field": field,
                            "native": native_value,
                            "oracle": oracle_value,
                        })
                    continue
                if not _floats_close(float(native_value), float(oracle_value),
                                     rel=rel, abs_=abs_):
                    mismatches.append({
                        "kind": "float",
                        "frame": frame_index,
                        "layer_index": layer_index,
                        "field": field,
                        "native": native_value,
                        "oracle": oracle_value,
                    })
    return mismatches


def frame_ids(events: list[dict[str, Any]]) -> set[int]:
    return {
        int(ev["frameId"]) for ev in events
        if isinstance(ev.get("frameId"), int)
    }


def key_field_mismatches(stage: str, events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if not events:
        return []
    requirements = {
        "static_parse": {
            "init_non_emote_leave": ("parameterTable",),
        },
        "init_motion": {
            "init_non_emote_leave": ("overview",),
        },
        "frame_selection": {
            "evaluate_timeline": ("before", "after"),
        },
        "sub_motion_decision": {
            "sub_motion_decision": ("decisions",),
        },
        "trace_flatten": {
            "frame": ("layers", "playerCount"),
        },
    }
    out: list[dict[str, Any]] = []
    for kind, fields in requirements.get(stage, {}).items():
        sample = next((ev for ev in events if ev.get("kind") == kind), None)
        if sample is None:
            out.append({"kind": "missing_event_kind", "eventKind": kind})
            continue
        for field in fields:
            if field not in sample:
                out.append({
                    "kind": "missing_field",
                    "eventKind": kind,
                    "field": field,
                })
    return out


def diff_stage_structural(
    *,
    stage: str,
    native_payload: dict[str, Any],
    oracle_payload: dict[str, Any],
) -> list[dict[str, Any]]:
    n_events = native_payload.get("events") or []
    o_events = oracle_payload.get("events") or []
    n_summary = native_payload.get("summary") or {}
    o_summary = oracle_payload.get("summary") or {}
    mismatches: list[dict[str, Any]] = []

    if stage == "variable_binding":
        if bool(n_events) != bool(o_events):
            mismatches.append({
                "kind": "variable_binding_zero_nonzero",
                "native": len(n_events),
                "oracle": len(o_events),
            })
        elif n_events and n_summary.get("kindCounts") != o_summary.get("kindCounts"):
            mismatches.append({
                "kind": "kind_counts",
                "native": n_summary.get("kindCounts"),
                "oracle": o_summary.get("kindCounts"),
            })
        return mismatches

    if len(n_events) != len(o_events):
        mismatches.append({
            "kind": "event_count",
            "native": len(n_events),
            "oracle": len(o_events),
        })
    if n_summary.get("kindCounts") != o_summary.get("kindCounts"):
        mismatches.append({
            "kind": "kind_counts",
            "native": n_summary.get("kindCounts"),
            "oracle": o_summary.get("kindCounts"),
        })

    n_frames = frame_ids(n_events)
    o_frames = frame_ids(o_events)
    if n_frames or o_frames:
        if n_frames != o_frames:
            mismatches.append({
                "kind": "frame_coverage",
                "nativeRange": [
                    min(n_frames) if n_frames else None,
                    max(n_frames) if n_frames else None,
                ],
                "oracleRange": [
                    min(o_frames) if o_frames else None,
                    max(o_frames) if o_frames else None,
                ],
                "nativeOnlySample": sorted(n_frames - o_frames)[:10],
                "oracleOnlySample": sorted(o_frames - n_frames)[:10],
            })

    mismatches.extend(
        {"side": "native", **m} for m in key_field_mismatches(stage, n_events)
    )
    mismatches.extend(
        {"side": "oracle", **m} for m in key_field_mismatches(stage, o_events)
    )
    return mismatches


def trace_flatten_acceptance(
    payloads: dict[str, dict[str, dict[str, Any]]],
) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    tf = payloads.get("trace_flatten", {})
    m2 = tf.get("m2logo")
    if not m2:
        return [{"kind": "missing_trace_flatten_m2logo"}]
    frames = m2.get("events") or []
    checks = [
        (12, 5, 39),
        (43, 1, 31),
    ]
    for local_frame, expected_players, expected_layers in checks:
        if local_frame >= len(frames):
            out.append({
                "kind": "missing_m2logo_local_frame",
                "frame": local_frame,
                "capturedFrames": len(frames),
            })
            continue
        frame = frames[local_frame]
        players = int(frame.get("playerCount", 0))
        layers = len(frame.get("layers") or [])
        if players != expected_players or layers != expected_layers:
            out.append({
                "kind": "m2logo_key_frame_counts",
                "frame": local_frame,
                "nativePlayerCount": players,
                "expectedPlayerCount": expected_players,
                "nativeLayers": layers,
                "expectedLayers": expected_layers,
            })
    return out


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)
    startup_xp3 = Path(args.startup_xp3)
    stages = selected_stages(args.stage)

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
        print(
            f"[native-stage] capturing stages={stages} "
            f"expected_trace_flatten_frames={expected_frames}"
        )
        events = run_native_stage_trace(
            runner=runner,
            startup_xp3=startup_xp3,
            expected_frames=expected_frames,
            timeout=args.lldb_timeout,
            stages=stages,
        )
        case_segments = build_case_segments(events, specs, mpb)
        segment_lengths = [len(seg["frames"]) for seg in case_segments]
        print(f"[native-stage] trace_flatten segments={segment_lengths}")
        segment_order = mpb.segment_order_for_specs(specs)
        expected_by_id = {spec["id"]: spec for spec in specs}
        if segment_lengths != [int(expected_by_id[spec_id]["frames"])
                               for spec_id in segment_order]:
            raise RuntimeError(
                f"native trace_flatten segment lengths {segment_lengths} do "
                f"not match expected {segment_order}"
            )
        payloads = build_native_payloads(
            stages=sorted(set(stages) | {"trace_flatten"}),
            specs=specs,
            events=events,
            case_segments=case_segments,
        )
    except Exception as exc:
        print(f"FAIL: native staged trace error: {exc}", file=sys.stderr)
        return 1

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
                    "traceFlattenFrameCount": len(trace_flatten_frames(events)),
                    "segmentLengths": [
                        len(seg["frames"]) for seg in case_segments
                    ],
                },
            }, indent=2, ensure_ascii=True, allow_nan=False) + "\n",
            encoding="utf-8",
        )
        print(f"[native-stage] wrote raw stream to {raw_path}")

    failures = 0
    if "trace_flatten" in stages:
        for mismatch in trace_flatten_acceptance(payloads):
            print(f"FAIL: native trace_flatten acceptance: {mismatch}",
                  file=sys.stderr)
            failures += 1

    spec_by_id = {spec["id"]: spec for spec in specs}
    for stage in stages:
        for case_id in spec_by_id:
            native_payload = payloads.get(stage, {}).get(case_id)
            if native_payload is None:
                print(f"FAIL: {stage}/{case_id}: no native payload",
                      file=sys.stderr)
                failures += 1
                continue

            oracle_payload = load_oracle_payload(trace_dir, stage, case_id)
            if oracle_payload is None:
                msg = (f"no Android stage oracle for {stage}/{case_id} at "
                       f"{trace_dir / stage / (case_id + '.oracle.json')}")
                if args.strict_missing_trace:
                    print(f"FAIL: {msg}", file=sys.stderr)
                    failures += 1
                else:
                    print(f"SKIP: {msg}")
                continue

            if stage == "trace_flatten":
                native_events = native_payload.get("events") or []
                oracle_events = oracle_payload.get("events") or []
                schema_mismatches = (
                    trace_flatten_schema_mismatches(
                        native_events, side="native")
                    + trace_flatten_schema_mismatches(
                        oracle_events, side="oracle")
                )
                native_frames = normalize_trace_flatten_events(native_events)
                oracle_frames = normalize_trace_flatten_events(oracle_events)
                mismatches = schema_mismatches + diff_trace_flatten_frames(
                    native_frames,
                    oracle_frames,
                    structural_only=args.only_structural,
                )
                if not mismatches:
                    print(
                        f"PASS: {stage}/{case_id} "
                        f"({len(native_frames)} frames)"
                    )
                else:
                    print(
                        f"FAIL: {stage}/{case_id}: mismatch "
                        f"({len(mismatches)} mismatches)"
                    )
                    for mismatch in mismatches[:10]:
                        print(f"  {mismatch}")
                    if len(mismatches) > 10:
                        print(
                            f"  ... +{len(mismatches) - 10} more")
                    failures += 1
                continue

            if stage == "static_parse":
                mismatches = diff_static_parse_events(
                    native_payload=native_payload,
                    oracle_payload=oracle_payload,
                )
                summary = native_payload.get("summary") or {}
                if not mismatches:
                    print(
                        f"PASS: {stage}/{case_id} "
                        f"({summary.get('eventCount', 0)} events)"
                    )
                else:
                    print(
                        f"FAIL: {stage}/{case_id}: mismatch "
                        f"({len(mismatches)} mismatches)"
                    )
                    for mismatch in mismatches[:10]:
                        print(f"  {mismatch}")
                    if len(mismatches) > 10:
                        print(
                            f"  ... +{len(mismatches) - 10} more")
                    failures += 1
                continue

            if stage == "init_motion":
                mismatches = diff_init_motion_events(
                    native_payload=native_payload,
                    oracle_payload=oracle_payload,
                )
                summary = native_payload.get("summary") or {}
                if not mismatches:
                    print(
                        f"PASS: {stage}/{case_id} "
                        f"({summary.get('eventCount', 0)} events)"
                    )
                else:
                    print(
                        f"FAIL: {stage}/{case_id}: mismatch "
                        f"({len(mismatches)} mismatches)"
                    )
                    for mismatch in mismatches[:10]:
                        print(f"  {mismatch}")
                    if len(mismatches) > 10:
                        print(
                            f"  ... +{len(mismatches) - 10} more")
                    failures += 1
                continue

            if stage == "frame_selection":
                mismatches = diff_frame_selection_events(
                    native_payload=native_payload,
                    oracle_payload=oracle_payload,
                )
                summary = native_payload.get("summary") or {}
                if not mismatches:
                    print(
                        f"PASS: {stage}/{case_id} "
                        f"({summary.get('eventCount', 0)} events)"
                    )
                else:
                    print(
                        f"FAIL: {stage}/{case_id}: semantic mismatch "
                        f"({len(mismatches)} mismatches)"
                    )
                    for mismatch in mismatches[:10]:
                        print(f"  {mismatch}")
                    if len(mismatches) > 10:
                        print(f"  ... +{len(mismatches) - 10} more")
                    failures += 1
                continue

            mismatches = diff_stage_structural(
                stage=stage,
                native_payload=native_payload,
                oracle_payload=oracle_payload,
            )
            summary = native_payload.get("summary") or {}
            if not mismatches:
                print(
                    f"PASS: {stage}/{case_id} "
                    f"({summary.get('eventCount', 0)} events)"
                )
            else:
                print(
                    f"FAIL: {stage}/{case_id}: "
                    f"{len(mismatches)} structural mismatch(es)"
                )
                for mismatch in mismatches[:10]:
                    print(f"  {mismatch}")
                if len(mismatches) > 10:
                    print(f"  ... +{len(mismatches) - 10} more")
                failures += 1

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
