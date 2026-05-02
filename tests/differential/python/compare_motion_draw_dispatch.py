#!/usr/bin/env python3
"""Compare motion_playback draw_dispatch drawPath semantics."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))
from oracle_runner.png_artifacts import image_pixel_hash, rgba_sha256_file


SEMANTIC_FIELDS = (
    "drawPath.route",
    "drawPath.steps",
    "drawPath.prepareOk",
    "drawPath.d3dDrawModeAfterPrepare",
    "drawPath.renderToCanvasCalled",
    "drawPath.updateLayerAfterDrawCalled",
    "drawPath.internalAssignRequested",
    "imageChanged",
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Compare oracle/Wasmtime motion draw_dispatch drawPath events")
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
    return [e for e in events if isinstance(e, dict)]


def draw_leaves(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [e for e in events if e.get("kind") == "draw_leave"]


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
        return value
    path = artifact_root / rel
    cached = cache.get(path)
    if cached is not None:
        return cached
    cached = rgba_sha256_file(path)
    cache[path] = cached
    return cached


def event_image_changed(
    event: dict[str, Any],
    artifact_root: Path,
    cache: dict[Path, str],
) -> Any:
    pre_hash = decoded_image_hash(
        artifact_root, event.get("preDrawImage"), cache)
    post_hash = decoded_image_hash(
        artifact_root, event.get("postDrawImage"), cache)
    if pre_hash is not None and post_hash is not None:
        return pre_hash != post_hash
    return None


def event_value(
    event: dict[str, Any],
    field: str,
    *,
    artifact_root: Path,
    image_hash_cache: dict[Path, str],
) -> Any:
    if field == "imageChanged":
        return event_image_changed(
            event, artifact_root, image_hash_cache)
    value: Any = event
    for part in field.split("."):
        if not isinstance(value, dict):
            return None
        value = value.get(part)
    return value


def case_ids(oracle_root: Path) -> list[str]:
    event_dir = oracle_root / "events" / "draw_dispatch"
    return sorted(
        path.name[:-len(".oracle.json")]
        for path in event_dir.glob("*.oracle.json"))


def route_counts(leaves: list[dict[str, Any]]) -> Counter[str]:
    out: Counter[str] = Counter()
    for event in leaves:
        route: Any = event
        for part in "drawPath.route".split("."):
            route = route.get(part) if isinstance(route, dict) else None
        out[str(route)] += 1
    return out


def image_changed_count(
    leaves: list[dict[str, Any]],
    artifact_root: Path,
    image_hash_cache: dict[Path, str],
) -> int:
    return sum(
        1 for event in leaves
        if event_image_changed(event, artifact_root, image_hash_cache) is True)


def compare_case(
    oracle_root: Path,
    wasmtime_root: Path,
    case_id: str,
) -> bool:
    oracle_path = (
        oracle_root / "events" / "draw_dispatch" / f"{case_id}.oracle.json")
    wasmtime_path = (
        wasmtime_root / "events" / "draw_dispatch" /
        f"{case_id}.wasmtime.json")
    oracle_leaves = draw_leaves(load_events(oracle_path))
    wasmtime_leaves = draw_leaves(load_events(wasmtime_path))

    ok = True
    if len(oracle_leaves) != len(wasmtime_leaves):
        ok = False
        print(
            f"{case_id}: FAIL draw_leave count oracle={len(oracle_leaves)} "
            f"wasmtime={len(wasmtime_leaves)}")

    first_mismatch: tuple[int, str, Any, Any] | None = None
    oracle_image_cache: dict[Path, str] = {}
    wasmtime_image_cache: dict[Path, str] = {}
    for index, (oracle_event, wasmtime_event) in enumerate(
        zip(oracle_leaves, wasmtime_leaves)):
        for field in SEMANTIC_FIELDS:
            oracle_value = event_value(
                oracle_event, field,
                artifact_root=oracle_root,
                image_hash_cache=oracle_image_cache,
            )
            wasmtime_value = event_value(
                wasmtime_event, field,
                artifact_root=wasmtime_root,
                image_hash_cache=wasmtime_image_cache,
            )
            if oracle_value != wasmtime_value:
                first_mismatch = (index, field, oracle_value, wasmtime_value)
                ok = False
                break
        if first_mismatch is not None:
            break

    if first_mismatch is not None:
        index, field, oracle_value, wasmtime_value = first_mismatch
        print(
            f"{case_id}: FAIL first_mismatch localFrame={index} "
            f"field={field} oracle={oracle_value!r} "
            f"wasmtime={wasmtime_value!r}")
    elif ok:
        print(f"{case_id}: PASS frames={len(oracle_leaves)}")

    oracle_changed = image_changed_count(
        oracle_leaves, oracle_root, oracle_image_cache)
    wasmtime_changed = image_changed_count(
        wasmtime_leaves, wasmtime_root, wasmtime_image_cache)
    print(
        f"{case_id}: routes oracle={dict(route_counts(oracle_leaves))} "
        f"wasmtime={dict(route_counts(wasmtime_leaves))} "
        f"imageChanged oracle={oracle_changed} "
        f"wasmtime={wasmtime_changed}")
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
