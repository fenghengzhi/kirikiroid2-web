#!/usr/bin/env python3
"""Offline checks for the 15 Hz motion_playback timing contract."""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))

from oracle_runner.motion_timing import (  # noqa: E402
    SIMULATION_DELTA_MS,
    SIMULATION_FPS,
    select_expected_segments,
    validate_simulation_contract,
)


class MotionTimingTest(unittest.TestCase):
    def setUp(self) -> None:
        spec_dir = (
            REPO_ROOT / "tests" / "differential" / "specs" /
            "motion_playback"
        )
        self.specs = {
            path.stem: json.loads(path.read_text(encoding="utf-8"))
            for path in spec_dir.glob("*.json")
        }

    @staticmethod
    def _segment(length: int, name: str) -> dict:
        return {"player": name, "frames": [{} for _ in range(length)]}

    def test_specs_and_fixture_sources_are_15hz(self) -> None:
        validate_simulation_contract(self.specs.values())
        self.assertEqual(SIMULATION_FPS, 15.0)
        self.assertAlmostEqual(SIMULATION_DELTA_MS, 1000.0 / 15.0)

        fixture_root = (
            REPO_ROOT / "tests" / "differential" / "oracle_runner" /
            "fixtures"
        )
        startup_files = sorted(fixture_root.glob("logo_test_oracle*_15hz/startup.tjs"))
        self.assertEqual(len(startup_files), 3)
        for path in startup_files:
            with self.subTest(path=path):
                source = path.read_text(encoding="utf-8")
                self.assertIn("FIXED_DELTA_MS = 1000.0 / 15.0", source)
                self.assertNotIn("FIXED_DELTA_MS = 1000.0 / 60.0", source)

    def test_selects_unique_ordered_fixture_segments_around_noise(self) -> None:
        noise_before = self._segment(2, "noise-before")
        yuzu = self._segment(63, "yuzulogo")
        noise_between = self._segment(4, "noise-between")
        m2 = self._segment(25, "m2logo")
        selected = select_expected_segments(
            [noise_before, yuzu, noise_between, m2],
            [self.specs["yuzulogo"], self.specs["m2logo"]],
        )
        self.assertEqual([segment["player"] for segment in selected],
                         ["yuzulogo", "m2logo"])

    def test_missing_or_ambiguous_segments_fail_closed(self) -> None:
        yuzu_spec = [self.specs["yuzulogo"]]
        with self.assertRaisesRegex(ValueError, "no ordered motion segment"):
            select_expected_segments([self._segment(62, "short")], yuzu_spec)
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            select_expected_segments([
                self._segment(63, "first"),
                self._segment(63, "second"),
            ], yuzu_spec)

    def test_wrong_spec_fps_fails_closed(self) -> None:
        spec = dict(self.specs["yuzulogo"])
        spec["simulation_fps"] = 60
        with self.assertRaisesRegex(ValueError, "runner/fixture contract"):
            validate_simulation_contract([spec])


if __name__ == "__main__":
    unittest.main()
