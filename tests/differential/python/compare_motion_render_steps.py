#!/usr/bin/env python3
"""Compare motion_playback render item flow and execute image checkpoints."""

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


ITEM_FIELDS = (
    "index",
    "nodeIndex",
    "sourceKey",
    "flags",
    "layerIds",
    "sortKey64",
    "paintBox",
    "clipRect",
    "buildClipRect",
    "dirtyRect",
    "viewportRect",
    "sourceGate232",
    "stencilType244",
    "parentItemIndex",
    "parentItem264",
    "childItemCount",
    "meshType280",
    "leafLayerVariantTag",
    "composedLayerVariantTag",
    "leafBuilt",
    "composedBuilt",
    "executedDirect",
)
BUILD_FLOW_FIELDS = (
    "inputItemCount",
    "builtItemCount",
    "validDrawableItemCount",
    "leafBuiltCount",
    "composedBuiltCount",
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


def _normalized_item_value(item: dict[str, Any], field: str) -> Any:
    flags = item.get("flags")
    clip_valid = (
        bool(flags.get("clipValid21"))
        if isinstance(flags, dict) else False
    )
    if field == "flags":
        value = flags
        return dict(value) if isinstance(value, dict) else value
    if field == "leafLayerVariantTag":
        value = item.get("leafLayerVariantTag")
        return item.get("leafLayerVariantTag320") if value is None else value
    if field == "composedLayerVariantTag":
        value = item.get("composedLayerVariantTag")
        return item.get("composedLayerVariantTag340") if value is None else value
    value = item.get(field)
    if field == "buildClipRect" and value is None:
        value = item.get("clipRect")
    if field in {"clipRect", "buildClipRect"} and not clip_valid:
        return [0, 0, 0, 0]
    if field == "viewportRect" and value == [1, 1, -1, -1]:
        return None
    if field in {"paintBox", "clipRect", "buildClipRect", "dirtyRect",
                 "viewportRect"} and isinstance(value, list):
        normalized = []
        for part in value:
            if isinstance(part, (int, float)):
                normalized.append(round(float(part), 3))
            else:
                normalized.append(part)
        return normalized
    return value


def _semantic_item(item: dict[str, Any]) -> dict[str, Any]:
    return {
        field: _normalized_item_value(item, field)
        for field in ITEM_FIELDS
    }


def _build_flow(event: dict[str, Any]) -> dict[str, Any]:
    flow = event.get("buildFlow")
    if not isinstance(flow, dict):
        return {}
    legacy_items_raw = flow.get("items")
    if not isinstance(legacy_items_raw, list):
        legacy_items_raw = flow.get("commands")
    legacy_items = [
        _semantic_item(item)
        for item in legacy_items_raw if isinstance(item, dict)
    ] if isinstance(legacy_items_raw, list) else []
    main_items = flow.get("mainListSemanticItems")
    semantic_main_items = [
        _semantic_item(item)
        for item in main_items if isinstance(item, dict)
    ] if isinstance(main_items, list) else legacy_items
    aux_items = flow.get("auxListSemanticItems")
    semantic_aux_items = [
        _semantic_item(item)
        for item in aux_items if isinstance(item, dict)
    ] if isinstance(aux_items, list) else []
    return {
        **{field: flow.get(field) for field in BUILD_FLOW_FIELDS},
        "mainListSemanticItems": semantic_main_items,
        "auxListSemanticItems": semantic_aux_items,
        "items": semantic_main_items,
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


def layer_save_images(events: list[dict[str, Any]]) -> dict[tuple[str, int], dict[str, Any]]:
    out: dict[tuple[str, int], dict[str, Any]] = {}
    for event in events:
        if event.get("kind") != "save_layer_image":
            continue
        phase = event.get("phase")
        frame = event.get("frame")
        if not isinstance(phase, str) or not isinstance(frame, int):
            continue
        out[(phase, frame)] = event
    return out


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


def _value_summary(value: Any) -> str:
    text = repr(value)
    if len(text) <= 220:
        return text
    return text[:217] + "..."


def _compare_item_lists(
    oracle_items: list[dict[str, Any]],
    wasmtime_items: list[dict[str, Any]],
) -> tuple[str, Any, Any] | None:
    if len(oracle_items) != len(wasmtime_items):
        return ("items.length", len(oracle_items), len(wasmtime_items))
    for index, (oracle_item, wasmtime_item) in enumerate(
        zip(oracle_items, wasmtime_items)):
        for field in ITEM_FIELDS:
            oracle_value = oracle_item.get(field)
            wasmtime_value = wasmtime_item.get(field)
            if oracle_value is None or wasmtime_value is None:
                continue
            if oracle_value != wasmtime_value:
                return (
                    f"items[{index}].{field}",
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
        if oracle_value is None or wasmtime_value is None:
            continue
        if oracle_value != wasmtime_value:
            return (field, oracle_value, wasmtime_value)
    main_diff = _compare_item_lists(
        oracle_flow.get("items") or [],
        wasmtime_flow.get("items") or [],
    )
    if main_diff is not None:
        return ("mainListSemanticItems." + main_diff[0],
                main_diff[1], main_diff[2])
    aux_diff = _compare_item_lists(
        oracle_flow.get("auxListSemanticItems") or [],
        wasmtime_flow.get("auxListSemanticItems") or [],
    )
    if aux_diff is not None:
        return ("auxListSemanticItems." + aux_diff[0],
                aux_diff[1], aux_diff[2])
    return None


def _compare_layer_save_images(
    oracle_root: Path,
    wasmtime_root: Path,
    oracle_images: dict[tuple[str, int], dict[str, Any]],
    wasmtime_images: dict[tuple[str, int], dict[str, Any]],
    oracle_cache: dict[Path, str],
    wasmtime_cache: dict[Path, str],
) -> tuple[int, tuple[str, Any, Any] | None]:
    mismatches = 0
    first_mismatch: tuple[str, Any, Any] | None = None
    phase_order = {"initial": 0, "post_draw": 1}
    keys = sorted(
        set(oracle_images) | set(wasmtime_images) | {("initial", 0)},
        key=lambda key: (phase_order.get(key[0], 99), key[1], key[0]),
    )
    for phase, frame in keys:
        if phase not in phase_order or (phase == "initial" and frame != 0):
            mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    f"{phase}[{frame}].phase",
                    phase if (phase, frame) in oracle_images else None,
                    phase if (phase, frame) in wasmtime_images else None,
                )
            continue
        oracle_image = oracle_images.get((phase, frame))
        wasmtime_image = wasmtime_images.get((phase, frame))
        if oracle_image is None or wasmtime_image is None:
            mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    f"{phase}[{frame}].present",
                    oracle_image is not None,
                    wasmtime_image is not None,
                )
            continue
        oracle_hash = decoded_image_hash(oracle_root, oracle_image, oracle_cache)
        wasmtime_hash = decoded_image_hash(
            wasmtime_root, wasmtime_image, wasmtime_cache)
        if oracle_hash != wasmtime_hash:
            mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    f"{phase}[{frame}].rgbaSha256",
                    oracle_hash,
                    wasmtime_hash,
                )
    return mismatches, first_mismatch


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
    oracle_build_events = build_flow_leaves(load_events(
        oracle_root / "events" / "render_commands" /
        f"{case_id}.oracle.json"))
    wasmtime_build_events = build_flow_leaves(load_events(
        wasmtime_root / "events" / "render_commands" /
        f"{case_id}.wasmtime.json"))
    oracle_execute = execute_leaves(load_events(
        oracle_root / "events" / "render_execute" /
        f"{case_id}.oracle.json"))
    wasmtime_execute = execute_leaves(load_events(
        wasmtime_root / "events" / "render_execute" /
        f"{case_id}.wasmtime.json"))
    oracle_layer_save = layer_save_images(load_events(
        oracle_root / "events" / "layer_save" /
        f"{case_id}.oracle.json"))
    wasmtime_layer_save = layer_save_images(load_events(
        wasmtime_root / "events" / "layer_save" /
        f"{case_id}.wasmtime.json"))

    first_mismatch: tuple[int, str, str, Any, Any] | None = None
    build_flow_mismatches = 0
    execute_pre_mismatches = 0
    execute_post_mismatches = 0
    layer_save_mismatches = 0
    oracle_cache: dict[Path, str] = {}
    wasmtime_cache: dict[Path, str] = {}
    execute_checkpoint_enabled = any(
        decoded_image_hash(oracle_root, event.get("executePreImage"), oracle_cache)
        is not None
        for event in oracle_execute
    ) or any(
        decoded_image_hash(
            wasmtime_root, event.get("executePreImage"), wasmtime_cache)
        is not None
        for event in wasmtime_execute
    ) or any(
        decoded_image_hash(oracle_root, event.get("executePostImage"), oracle_cache)
        is not None
        for event in oracle_execute
    ) or any(
        decoded_image_hash(
            wasmtime_root, event.get("executePostImage"), wasmtime_cache)
        is not None
        for event in wasmtime_execute
    )

    frame_count = max(
        len(oracle_build_events),
        len(wasmtime_build_events),
        len(oracle_execute),
        len(wasmtime_execute),
    )
    for index in range(frame_count):
        if index >= len(oracle_build_events) or index >= len(wasmtime_build_events):
            build_flow_mismatches += 1
            if first_mismatch is None:
                first_mismatch = (
                    index,
                    "build_flow",
                    "event_count",
                    index < len(oracle_build_events),
                    index < len(wasmtime_build_events),
                )
        else:
            oracle_flow = _build_flow(oracle_build_events[index])
            wasmtime_flow = _build_flow(wasmtime_build_events[index])
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
            if execute_checkpoint_enabled:
                execute_pre_mismatches += 1
                execute_post_mismatches += 1
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
        if execute_checkpoint_enabled:
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

    layer_save_mismatches, layer_save_first = _compare_layer_save_images(
        oracle_root,
        wasmtime_root,
        oracle_layer_save,
        wasmtime_layer_save,
        oracle_cache,
        wasmtime_cache,
    )
    if layer_save_first is not None and first_mismatch is None:
        field, oracle_value, wasmtime_value = layer_save_first
        first_mismatch = (
            0,
            "layer_save",
            field,
            oracle_value,
            wasmtime_value,
        )

    ok = first_mismatch is None
    if ok:
        print(f"{case_id}: PASS frames={frame_count}")
    else:
        index, stage, field, oracle_value, wasmtime_value = first_mismatch
        label_event = (
            oracle_execute[index] if index < len(oracle_execute)
            else oracle_build_events[index] if index < len(oracle_build_events)
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
        f"layer_save_mismatch={layer_save_mismatches}")
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
