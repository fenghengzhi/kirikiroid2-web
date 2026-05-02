#!/usr/bin/env python3
"""Compare motion_playback render command flow and execute image checkpoints."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))
from oracle_runner.png_artifacts import image_pixel_hash, rgba_sha256_file


COMMAND_FIELDS = (
    "index",
    "nodeIndex",
    "sourceKey",
    "flags",
    "layerIds",
    "clipRect",
    "dirtyRect",
    "viewportRect",
    "sourceGate232",
    "stencilType244",
    "parentItemIndex",
    "parentCommandIndex",
    "parentItem264",
    "childItemCount",
    "childCommandCount",
    "meshType280",
    "leafLayerVariantTag",
    "composedLayerVariantTag",
    "leafLayerVariantTag320",
    "composedLayerVariantTag340",
    "leafBuilt",
    "composedBuilt",
    "executedDirect",
)
BUILD_FLOW_FIELDS = (
    "inputItemCount",
    "renderCommandCount",
    "topLevelCommandCount",
    "groupCommandCount",
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Compare oracle/Wasmtime motion render step artifacts")
    p.add_argument("--oracle-root", type=Path, required=True,
                   help="Oracle render-stage artifact root")
    p.add_argument("--wasmtime-root", type=Path, required=True,
                   help="Wasmtime render-stage artifact root")
    p.add_argument("--case", action="append", default=[],
                   help="Case id to compare; defaults to all oracle cases")
    return p.parse_args(argv)


def load_events(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    events = data.get("events", [])
    if not isinstance(events, list):
        raise ValueError(f"{path} has no events list")
    return [event for event in events if isinstance(event, dict)]


def case_ids(oracle_root: Path) -> list[str]:
    event_dir = oracle_root / "events" / "render_commands"
    return sorted(
        path.name[:-len(".oracle.json")]
        for path in event_dir.glob("*.oracle.json"))


def _semantic_command(command: dict[str, Any]) -> dict[str, Any]:
    return {field: command.get(field) for field in COMMAND_FIELDS}


def _build_flow(event: dict[str, Any]) -> dict[str, Any]:
    flow = event.get("buildFlow")
    if not isinstance(flow, dict):
        return {}
    commands = flow.get("commands")
    semantic_commands = [
        _semantic_command(command)
        for command in commands if isinstance(command, dict)
    ] if isinstance(commands, list) else []
    return {
        **{field: flow.get(field) for field in BUILD_FLOW_FIELDS},
        "commands": semantic_commands,
    }


def _flow_digest(flow: dict[str, Any]) -> str:
    data = json.dumps(flow, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def build_flow_leaves(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [event for event in events
            if event.get("kind") == "build_commands_leave"]


def execute_leaves(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [event for event in events if event.get("kind") == "execute_leave"]


def decoded_image_hash(
    artifact_root: Path,
    image: dict[str, Any] | None,
    cache: dict[Path, str],
) -> str | None:
    if image is None:
        return None
    value = image_pixel_hash(image)
    if value is not None:
        return value
    rel = image.get("path")
    if not isinstance(rel, str) or not rel:
        return None
    path = artifact_root / rel
    cached = cache.get(path)
    if cached is not None:
        return cached
    cached = rgba_sha256_file(path)
    cache[path] = cached
    return cached


def _execute_image_changed(
    event: dict[str, Any],
    root: Path,
    cache: dict[Path, str],
) -> bool | None:
    pre_hash = decoded_image_hash(root, event.get("executePreImage"), cache)
    post_hash = decoded_image_hash(root, event.get("executePostImage"), cache)
    if pre_hash is None or post_hash is None:
        return None
    return pre_hash != post_hash


def _value_summary(value: Any) -> str:
    text = repr(value)
    if len(text) <= 220:
        return text
    return text[:217] + "..."


def _compare_command_lists(
    oracle_commands: list[dict[str, Any]],
    wasmtime_commands: list[dict[str, Any]],
) -> tuple[str, Any, Any] | None:
    if len(oracle_commands) != len(wasmtime_commands):
        return ("commands.length", len(oracle_commands), len(wasmtime_commands))
    for index, (oracle_command, wasmtime_command) in enumerate(
        zip(oracle_commands, wasmtime_commands)):
        for field in COMMAND_FIELDS:
            oracle_value = oracle_command.get(field)
            wasmtime_value = wasmtime_command.get(field)
            if oracle_value != wasmtime_value:
                return (
                    f"commands[{index}].{field}",
                    oracle_value,
                    wasmtime_value,
                )
    return None


def _compare_build_flow(
    oracle_flow: dict[str, Any],
    wasmtime_flow: dict[str, Any],
) -> tuple[str, Any, Any] | None:
    for field in BUILD_FLOW_FIELDS:
        oracle_value = oracle_flow.get(field)
        wasmtime_value = wasmtime_flow.get(field)
        if oracle_value != wasmtime_value:
            return (field, oracle_value, wasmtime_value)
    return _compare_command_lists(
        oracle_flow.get("commands") or [],
        wasmtime_flow.get("commands") or [],
    )


def _event_frame_label(event: dict[str, Any], fallback: int) -> str:
    frame = event.get("frame")
    if isinstance(frame, int):
        return str(frame)
    frame_id = event.get("frameId")
    if isinstance(frame_id, int):
        return str(fallback)
    return str(fallback)


def compare_case(
    oracle_root: Path,
    wasmtime_root: Path,
    case_id: str,
) -> bool:
    oracle_commands = build_flow_leaves(load_events(
        oracle_root / "events" / "render_commands" /
        f"{case_id}.oracle.json"))
    wasmtime_commands = build_flow_leaves(load_events(
        wasmtime_root / "events" / "render_commands" /
        f"{case_id}.wasmtime.json"))
    oracle_execute = execute_leaves(load_events(
        oracle_root / "events" / "render_execute" /
        f"{case_id}.oracle.json"))
    wasmtime_execute = execute_leaves(load_events(
        wasmtime_root / "events" / "render_execute" /
        f"{case_id}.wasmtime.json"))

    first_mismatch: tuple[int, str, str, Any, Any] | None = None
    build_flow_mismatches = 0
    execute_pre_mismatches = 0
    execute_post_mismatches = 0
    execute_changed_mismatches = 0
    oracle_cache: dict[Path, str] = {}
    wasmtime_cache: dict[Path, str] = {}

    frame_count = max(
        len(oracle_commands),
        len(wasmtime_commands),
        len(oracle_execute),
        len(wasmtime_execute),
    )
    for index in range(frame_count):
        if index >= len(oracle_commands) or index >= len(wasmtime_commands):
            build_flow_mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    index,
                    "build_flow",
                    "event_count",
                    index < len(oracle_commands),
                    index < len(wasmtime_commands),
                )
        else:
            oracle_flow = _build_flow(oracle_commands[index])
            wasmtime_flow = _build_flow(wasmtime_commands[index])
            field_diff = _compare_build_flow(oracle_flow, wasmtime_flow)
            if field_diff is not None:
                build_flow_mismatches += 1
                if first_mismatch is None:
                    field, oracle_value, wasmtime_value = field_diff
                    first_mismatch = (
                        index,
                        "build_flow",
                        field,
                        {
                            "value": oracle_value,
                            "digest": _flow_digest(oracle_flow),
                        },
                        {
                            "value": wasmtime_value,
                            "digest": _flow_digest(wasmtime_flow),
                        },
                    )

        if index >= len(oracle_execute) or index >= len(wasmtime_execute):
            execute_pre_mismatches += 1
            execute_post_mismatches += 1
            execute_changed_mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    index,
                    "render_execute",
                    "execute_leave_count",
                    index < len(oracle_execute),
                    index < len(wasmtime_execute),
                )
            continue

        oracle_event = oracle_execute[index]
        wasmtime_event = wasmtime_execute[index]
        oracle_pre = decoded_image_hash(
            oracle_root, oracle_event.get("executePreImage"), oracle_cache)
        wasmtime_pre = decoded_image_hash(
            wasmtime_root, wasmtime_event.get("executePreImage"),
            wasmtime_cache)
        if oracle_pre != wasmtime_pre:
            execute_pre_mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    index,
                    "render_execute",
                    "execute_pre.rgbaSha256",
                    oracle_pre,
                    wasmtime_pre,
                )

        oracle_post = decoded_image_hash(
            oracle_root, oracle_event.get("executePostImage"), oracle_cache)
        wasmtime_post = decoded_image_hash(
            wasmtime_root, wasmtime_event.get("executePostImage"),
            wasmtime_cache)
        if oracle_post != wasmtime_post:
            execute_post_mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    index,
                    "render_execute",
                    "execute_post.rgbaSha256",
                    oracle_post,
                    wasmtime_post,
                )

        oracle_changed = _execute_image_changed(
            oracle_event, oracle_root, oracle_cache)
        wasmtime_changed = _execute_image_changed(
            wasmtime_event, wasmtime_root, wasmtime_cache)
        if oracle_changed != wasmtime_changed:
            execute_changed_mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    index,
                    "render_execute",
                    "executeImageChanged",
                    oracle_changed,
                    wasmtime_changed,
                )

    ok = first_mismatch is None
    if ok:
        print(f"{case_id}: PASS frames={frame_count}")
    else:
        index, stage, field, oracle_value, wasmtime_value = first_mismatch
        label_event = (
            oracle_execute[index] if index < len(oracle_execute)
            else oracle_commands[index] if index < len(oracle_commands)
            else {}
        )
        print(
            f"{case_id}: FAIL first_mismatch localFrame="
            f"{_event_frame_label(label_event, index)} stage={stage} "
            f"field={field} oracle={_value_summary(oracle_value)} "
            f"wasmtime={_value_summary(wasmtime_value)}")

    print(
        f"{case_id}: summary build_flow_mismatch={build_flow_mismatches} "
        f"execute_pre_mismatch={execute_pre_mismatches} "
        f"execute_post_mismatch={execute_post_mismatches} "
        f"executeImageChanged_mismatch={execute_changed_mismatches}")
    return ok


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    cases = args.case or case_ids(args.oracle_root)
    if not cases:
        raise SystemExit("no cases found")
    all_ok = True
    for case_id in cases:
        if not compare_case(args.oracle_root, args.wasmtime_root, case_id):
            all_ok = False
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
