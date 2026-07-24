#!/usr/bin/env python3
"""Offline checks for strict motion_playback oracle validation."""

from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))

from oracle_runner.adapters import motion_playback as mpb  # noqa: E402


class StrictMotionPlaybackOracleTest(unittest.TestCase):
    def _spec(self, case_id: str) -> dict:
        path = (
            REPO_ROOT / "tests" / "differential" / "specs" /
            "motion_playback" / f"{case_id}.json"
        )
        return json.loads(path.read_text(encoding="utf-8"))

    def _oracle_frames(self, case_id: str) -> list[dict]:
        path = (
            REPO_ROOT / "tests" / "differential" / "traces" /
            "motion_playback" / f"{case_id}.oracle.json"
        )
        return json.loads(path.read_text(encoding="utf-8"))

    def _strict_frames(self, case_id: str) -> list[dict]:
        spec = self._spec(case_id)
        strict_frames = []
        for index, oracle_frame in enumerate(self._oracle_frames(case_id)):
            layers = copy.deepcopy(oracle_frame["layers"])
            for layer in layers:
                layer["stencilType"] = 0
            player_count = mpb._player_count_for_frame(spec, index)
            strict_frames.append({
                "frameId": index,
                "projection": mpb.TRACE_FLATTEN_PROJECTION,
                "samplePoint": mpb.TRACE_FLATTEN_SAMPLE_POINT,
                "playerCount": player_count,
                "diagnostics": {
                    "layout": "deque",
                    "topPlayer": "0x1",
                    "players": [
                        {"ptr": f"0x{player + 1:x}", "layout": "deque"}
                        for player in range(player_count)
                    ],
                },
                "layers": layers,
            })
        return strict_frames

    def test_checked_in_oracle_matches_15hz_spec(self) -> None:
        for case_id in ("yuzulogo", "m2logo"):
            with self.subTest(case_id=case_id):
                spec = self._spec(case_id)
                frames = self._oracle_frames(case_id)
                self.assertEqual(len(frames), int(spec["frames"]))
                self.assertEqual(
                    [frame["frame"] for frame in frames],
                    list(range(int(spec["frames"]))),
                )

    def test_strict_validator_accepts_valid_trace_shape(self) -> None:
        for case_id in ("yuzulogo", "m2logo"):
            with self.subTest(case_id=case_id):
                spec = self._spec(case_id)
                frames = self._strict_frames(case_id)
                mpb._validate_trace_flatten_segment(spec, frames)

    def test_invalid_trace_flatten_frames_fail_validation(self) -> None:
        spec = self._spec("yuzulogo")
        base_frames = self._strict_frames("yuzulogo")

        def mutate_root_only(frames: list[dict]) -> None:
            diag = frames[0]["diagnostics"]
            diag["layout"] = "root-only"
            diag["players"][0]["layout"] = "root-only"

        def mutate_deque_error(frames: list[dict]) -> None:
            diag = frames[0]["diagnostics"]
            diag["error"] = "deque-error: synthetic"
            diag["players"][0]["error"] = "deque-error: synthetic"

        def mutate_bad_layer_count(frames: list[dict]) -> None:
            frames[0]["layers"].pop()

        def mutate_missing_field(frames: list[dict]) -> None:
            del frames[0]["layers"][0]["posX"]

        def mutate_huge_float(frames: list[dict]) -> None:
            frames[0]["layers"][0]["posY"] = 1.0e74

        def mutate_non_finite_float(frames: list[dict]) -> None:
            frames[0]["layers"][0]["posZ"] = float("nan")

        def mutate_bad_opacity(frames: list[dict]) -> None:
            frames[0]["layers"][0]["opacity"] = 999

        cases = {
            "root-only": mutate_root_only,
            "deque-error": mutate_deque_error,
            "bad-layer-count": mutate_bad_layer_count,
            "missing-field": mutate_missing_field,
            "huge-float": mutate_huge_float,
            "non-finite-float": mutate_non_finite_float,
            "bad-opacity": mutate_bad_opacity,
        }
        for name, mutate in cases.items():
            with self.subTest(name=name):
                frames = copy.deepcopy(base_frames)
                mutate(frames)
                with self.assertRaisesRegex(
                    RuntimeError,
                    "strict motion_playback oracle validation failed",
                ):
                    mpb._validate_trace_flatten_segment(spec, frames)


if __name__ == "__main__":
    unittest.main()
