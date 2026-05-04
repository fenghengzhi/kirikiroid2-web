"""Shared frame capture window helpers for motion_playback runners."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from typing import Any, Sequence


@dataclass(frozen=True)
class FrameCaptureWindow:
    mode: str
    total_frames: int
    start: int
    end: int
    requested: int | None = None

    @property
    def enabled(self) -> bool:
        return self.mode != "all"

    @property
    def count(self) -> int:
        return self.end - self.start

    @property
    def driven_frames(self) -> int:
        return self.end

    def includes(self, frame_id: int) -> bool:
        return self.start <= frame_id < self.end

    def to_options(self) -> dict[str, int]:
        return {
            "captureFrameStart": int(self.start),
            "captureFrameCount": -1 if self.mode == "all" else int(self.count),
        }

    def filter_manifest(self) -> dict[str, Any]:
        out: dict[str, Any] = {
            "mode": self.mode,
            "startFrameId": self.start,
            "endFrameIdExclusive": self.end,
            "framesDriven": self.driven_frames,
        }
        if self.mode == "only":
            out["recordOnlyFrame"] = self.requested
        elif self.mode == "first":
            out["recordFirstFrames"] = self.requested
        return out

    def manifest_fields(self) -> dict[str, Any]:
        return {
            "captureFrameFilter": self.filter_manifest(),
            "fullFrameIdRange": [0, self.total_frames],
            "capturedFrameIdRange": [self.start, self.end],
        }


def add_frame_capture_args(parser: argparse.ArgumentParser) -> None:
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--record-only-frame",
        type=int,
        default=None,
        help="Drive to global frame N, record only that frame, then exit",
    )
    group.add_argument(
        "--record-first-frames",
        type=int,
        default=None,
        help="Record only global frames [0, N), then exit",
    )


def frame_capture_window_from_args(
    args: argparse.Namespace,
    total_frames: int,
) -> FrameCaptureWindow:
    only = getattr(args, "record_only_frame", None)
    first = getattr(args, "record_first_frames", None)
    if total_frames <= 0:
        raise ValueError(f"total frame count must be positive: {total_frames}")
    if only is not None:
        if only < 0 or only >= total_frames:
            raise ValueError(
                f"--record-only-frame must be in [0, {total_frames}): {only}"
            )
        return FrameCaptureWindow(
            mode="only",
            total_frames=total_frames,
            start=int(only),
            end=int(only) + 1,
            requested=int(only),
        )
    if first is not None:
        if first <= 0 or first > total_frames:
            raise ValueError(
                f"--record-first-frames must be in [1, {total_frames}]: "
                f"{first}"
            )
        return FrameCaptureWindow(
            mode="first",
            total_frames=total_frames,
            start=0,
            end=int(first),
            requested=int(first),
        )
    return FrameCaptureWindow(
        mode="all",
        total_frames=total_frames,
        start=0,
        end=total_frames,
        requested=None,
    )


def frame_capture_window_from_bounds(
    *,
    total_frames: int,
    start: int = 0,
    count: int = -1,
) -> FrameCaptureWindow:
    if count < 0:
        if start != 0:
            raise ValueError(
                "unbounded capture windows must start at frame 0")
        return FrameCaptureWindow(
            mode="all",
            total_frames=total_frames,
            start=0,
            end=total_frames,
            requested=None,
        )
    if start < 0 or count <= 0 or start + count > total_frames:
        raise ValueError(
            f"capture frame bounds out of range: start={start} "
            f"count={count} total={total_frames}"
        )
    if count == 1:
        return FrameCaptureWindow(
            mode="only",
            total_frames=total_frames,
            start=start,
            end=start + 1,
            requested=start,
        )
    if start == 0:
        return FrameCaptureWindow(
            mode="first",
            total_frames=total_frames,
            start=0,
            end=count,
            requested=count,
        )
    return FrameCaptureWindow(
        mode="range",
        total_frames=total_frames,
        start=start,
        end=start + count,
        requested=count,
    )


def _specs_by_id(specs_or_by_id: Sequence[dict[str, Any]] | dict[str, dict[str, Any]]
                 ) -> dict[str, dict[str, Any]]:
    if isinstance(specs_or_by_id, dict):
        return {str(k): v for k, v in specs_or_by_id.items()}
    return {str(spec["id"]): spec for spec in specs_or_by_id}


def validate_specs_for_order(
    specs_or_by_id: Sequence[dict[str, Any]] | dict[str, dict[str, Any]],
    segment_order: Sequence[str],
) -> dict[str, dict[str, Any]]:
    specs_by_id = _specs_by_id(specs_or_by_id)
    unknown = [sid for sid in specs_by_id if sid not in segment_order]
    if unknown:
        raise ValueError(
            f"unknown motion_playback spec id(s): {unknown}. Expected ids "
            f"are fixed by SEGMENT_ORDER: {tuple(segment_order)}."
        )
    return specs_by_id


def ordered_case_ranges(
    specs_or_by_id: Sequence[dict[str, Any]] | dict[str, dict[str, Any]],
    segment_order: Sequence[str],
) -> list[dict[str, Any]]:
    specs_by_id = validate_specs_for_order(specs_or_by_id, segment_order)
    offset = 0
    out: list[dict[str, Any]] = []
    for spec_id in segment_order:
        spec = specs_by_id.get(str(spec_id))
        if spec is None:
            continue
        frame_count = int(spec["frames"])
        out.append({
            "caseId": str(spec_id),
            "spec": spec,
            "frames": frame_count,
            "fullFrameIdRange": [offset, offset + frame_count],
            "firstFrameId": offset,
            "lastFrameId": offset + frame_count - 1,
        })
        offset += frame_count
    return out


def captured_case_ranges(
    specs_or_by_id: Sequence[dict[str, Any]] | dict[str, dict[str, Any]],
    segment_order: Sequence[str],
    window: FrameCaptureWindow,
) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for case in ordered_case_ranges(specs_or_by_id, segment_order):
        full_start, full_end = case["fullFrameIdRange"]
        captured_start = max(full_start, window.start)
        captured_end = min(full_end, window.end)
        if captured_start >= captured_end:
            continue
        captured_frame_ids = list(range(captured_start, captured_end))
        out.append({
            **case,
            "capturedFrameIdRange": [captured_start, captured_end],
            "capturedFrameIds": captured_frame_ids,
            "capturedLocalFrames": [
                frame_id - full_start for frame_id in captured_frame_ids
            ],
        })
    return out


def case_capture_metadata(
    specs_or_by_id: Sequence[dict[str, Any]] | dict[str, dict[str, Any]],
    segment_order: Sequence[str],
    window: FrameCaptureWindow,
) -> list[dict[str, Any]]:
    return [
        {
            "caseId": case["caseId"],
            "frames": case["frames"],
            "fullFrameIdRange": case["fullFrameIdRange"],
            "capturedFrameIdRange": case["capturedFrameIdRange"],
            "capturedLocalFrames": case["capturedLocalFrames"],
        }
        for case in captured_case_ranges(specs_or_by_id, segment_order, window)
    ]


def case_base_for_frame(
    frame_id: int,
    specs_or_by_id: Sequence[dict[str, Any]] | dict[str, dict[str, Any]],
    segment_order: Sequence[str],
) -> tuple[str, int] | None:
    for case in ordered_case_ranges(specs_or_by_id, segment_order):
        start, end = case["fullFrameIdRange"]
        if start <= frame_id < end:
            return str(case["caseId"]), int(start)
    return None
