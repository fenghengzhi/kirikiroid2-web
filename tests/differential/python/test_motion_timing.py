#!/usr/bin/env python3
"""Offline checks for the 15 Hz motion_playback timing contract."""

from __future__ import annotations

import json
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
PYTHON_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))
sys.path.insert(0, str(PYTHON_DIR))

from oracle_runner.motion_timing import (  # noqa: E402
    SIMULATION_DELTA_MS,
    SIMULATION_FPS,
    select_expected_segments,
    validate_simulation_contract,
)
from oracle_runner.adapters import motion_playback as mpb  # noqa: E402
from run_motion_playback_wasmtime import (  # noqa: E402
    WasmtimeEnvProvider,
    _wasmtime_preference_xml,
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

    def test_specs_and_fixture_sources_use_15hz_tick_delta(self) -> None:
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
                self.assertIn("var lastTick = void;", source)
                self.assertIn(
                    "if(lastTick === void) lastTick = tick;", source)
                self.assertIn("var delta = tick - lastTick;", source)
                self.assertIn(
                    "currentSource._image._interval = delta;", source)
                self.assertNotIn("FIXED_DELTA_MS", source)

    def test_android_oracle_pins_display_and_engine_to_15hz(self) -> None:
        root = ET.fromstring(mpb._renderer_preference_xml())
        items = {
            item.attrib["key"]: item.attrib["value"]
            for item in root.findall("Item")
        }
        self.assertEqual(items["renderer"], "software")
        self.assertEqual(items["ogl_accurate_render"], "false")
        self.assertEqual(items["fps_limit"], "15")

        workflow = (
            REPO_ROOT / ".github" / "workflows" / "differential.yml"
        ).read_text(encoding="utf-8")
        self.assertIn("androidboot.redroid_fps=15", workflow)
        self.assertIn("getprop ro.boot.redroid_fps", workflow)
        self.assertIn('[ "${redroid_fps}" != "15" ]', workflow)

    def test_wasmtime_uses_the_same_preference_and_virtual_cadence(self) -> None:
        root = ET.fromstring(_wasmtime_preference_xml("software"))
        items = {
            item.attrib["key"]: item.attrib["value"]
            for item in root.findall("Item")
        }
        self.assertEqual(items["renderer"], "software")
        self.assertEqual(items["fps_limit"], "15")

        provider = WasmtimeEnvProvider(REPO_ROOT)
        start_ms = provider.simulation_time_ms
        for _ in range(3):
            provider.advance_simulation_time(SIMULATION_DELTA_MS)
        self.assertAlmostEqual(
            provider.simulation_time_ms - start_ms,
            3.0 * SIMULATION_DELTA_MS,
            places=9,
        )
        with self.assertRaises(ValueError):
            provider.advance_simulation_time(-1.0)
        with self.assertRaises(ValueError):
            provider.advance_simulation_time(float("nan"))

    def test_selects_unique_ordered_fixture_segments_around_noise(self) -> None:
        noise_before = self._segment(2, "noise-before")
        yuzu = self._segment(
            int(self.specs["yuzulogo"]["frames"]), "yuzulogo")
        noise_between = self._segment(4, "noise-between")
        m2 = self._segment(int(self.specs["m2logo"]["frames"]), "m2logo")
        selected = select_expected_segments(
            [noise_before, yuzu, noise_between, m2],
            [self.specs["yuzulogo"], self.specs["m2logo"]],
        )
        self.assertEqual([segment["player"] for segment in selected],
                         ["yuzulogo", "m2logo"])

    def test_missing_or_ambiguous_segments_fail_closed(self) -> None:
        yuzu_spec = [self.specs["yuzulogo"]]
        yuzu_frames = int(self.specs["yuzulogo"]["frames"])
        with self.assertRaisesRegex(ValueError, "no ordered motion segment"):
            select_expected_segments(
                [self._segment(yuzu_frames - 1, "short")], yuzu_spec)
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            select_expected_segments([
                self._segment(yuzu_frames, "first"),
                self._segment(yuzu_frames, "second"),
            ], yuzu_spec)

    def test_wrong_spec_fps_fails_closed(self) -> None:
        spec = dict(self.specs["yuzulogo"])
        spec["simulation_fps"] = 60
        with self.assertRaisesRegex(ValueError, "runner/fixture contract"):
            validate_simulation_contract([spec])


if __name__ == "__main__":
    unittest.main()
