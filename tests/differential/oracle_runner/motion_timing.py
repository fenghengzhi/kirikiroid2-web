"""Shared timing and segment contracts for motion_playback differential tests."""

from __future__ import annotations

from typing import Any, Iterable, Sequence


SIMULATION_FPS = 15.0
SIMULATION_DELTA_MS = 1000.0 / SIMULATION_FPS
STARTUP_WARMUP_TICKS = 10
DEFAULT_STARTUP_XP3_NAME = "logo_test_oracle_15hz.xp3"


def validate_simulation_contract(specs: Iterable[dict[str, Any]]) -> None:
    for spec in specs:
        spec_id = str(spec.get("id", "<unknown>"))
        try:
            fps = float(spec["simulation_fps"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError(
                f"motion_playback spec {spec_id} has no valid simulation_fps"
            ) from exc
        if fps != SIMULATION_FPS:
            raise ValueError(
                f"motion_playback spec {spec_id} requests {fps:g} Hz; "
                f"runner/fixture contract is {SIMULATION_FPS:g} Hz"
            )


def _expected_segment_lengths(specs: Iterable[dict[str, Any]]) -> list[int]:
    specs = list(specs)
    validate_simulation_contract(specs)
    lengths = [int(spec["frames"]) for spec in specs]
    if not lengths or any(length <= 0 for length in lengths):
        raise ValueError(
            f"motion_playback specs require positive frame counts: {lengths}"
        )
    return lengths


def select_expected_segments(
    segments: Sequence[dict[str, Any]],
    specs: Sequence[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Select the unique ordered segment sequence whose lengths match specs.

    Short engine-owned warmup players may appear before the fixture players.
    Segment identity therefore comes from the fixture's exact ordered frame
    contract, never from an arbitrary minimum-length threshold.
    """

    expected = _expected_segment_lengths(specs)
    actual = [
        len(segment.get("frames", []))
        if isinstance(segment, dict) else -1
        for segment in segments
    ]
    matches: list[list[int]] = []

    def visit(spec_index: int, segment_start: int, chosen: list[int]) -> None:
        if len(matches) > 1:
            return
        if spec_index == len(expected):
            matches.append(list(chosen))
            return
        wanted = expected[spec_index]
        for segment_index in range(segment_start, len(segments)):
            if actual[segment_index] != wanted:
                continue
            chosen.append(segment_index)
            visit(spec_index + 1, segment_index + 1, chosen)
            chosen.pop()

    visit(0, 0, [])
    if not matches:
        raise ValueError(
            "no ordered motion segment sequence matches spec frame counts "
            f"{expected}; raw segment lengths: {actual}"
        )
    if len(matches) != 1:
        raise ValueError(
            "motion segment selection is ambiguous for spec frame counts "
            f"{expected}; raw segment lengths: {actual}; "
            f"candidate indexes: {matches}"
        )
    return [segments[index] for index in matches[0]]
