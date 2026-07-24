#!/usr/bin/env python3
"""Offline checks for the 15 Hz motion_playback timing contract."""

from __future__ import annotations

import json
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock


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
from oracle_runner.frida_motion_stage_tracer import (  # noqa: E402
    FridaMotionStageTracer,
)
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

    def test_android_oracle_pins_and_virtualizes_15hz_tick(self) -> None:
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

        class RecordingApi:
            def __init__(self) -> None:
                self.calls: list[tuple[list[str], dict]] = []

            def start_record(self, stages: list[str], options: dict) -> None:
                self.calls.append((stages, options))

        api = RecordingApi()
        tracer = object.__new__(FridaMotionStageTracer)
        tracer._api = api
        tracer.start_record(
            ["trace_flatten"], simulation_fps=SIMULATION_FPS)
        self.assertEqual(api.calls[0][1]["simulationFps"], 15.0)
        for invalid in (0.0, -1.0, float("nan")):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    tracer.start_record(
                        ["trace_flatten"], simulation_fps=invalid)

        agent_source = (
            REPO_ROOT / "tests" / "differential" / "oracle_runner" /
            "frida_motion_stage_agent.js"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "TVP_CONTINUOUS_EVENT_TICK_RETURN_OFF = 0x8DF8F4",
            agent_source,
        )
        self.assertIn(
            "TVP_GET_ROUGH_TICK_COUNT32_OFF = 0xA2BF90",
            agent_source,
        )
        self.assertIn("this.returnAddress.equals(", agent_source)
        self.assertIn(
            "virtualContinuousTickBase.add(offsetMs)", agent_source)

        grid = [
            int((index + 1) * 1000 / SIMULATION_FPS)
            for index in range(4)
        ]
        self.assertEqual(
            [grid[index] - grid[index - 1] for index in range(1, 4)],
            [67, 67, 66],
        )

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

    def test_short_stable_android_segment_fails_before_full_timeout(self) -> None:
        events = [
            {
                "stage": "trace_flatten",
                "kind": "frame",
                "frameId": frame,
                "diagnostics": {"topPlayer": "fixture-player"},
            }
            for frame in range(13)
        ]

        class StableTracer:
            def event_count(self) -> int:
                return len(events)

            def stop_record(self) -> list[dict]:
                return events

        class Clock:
            def __init__(self) -> None:
                self.now = 0.0

            def time(self) -> float:
                return self.now

            def sleep(self, seconds: float) -> None:
                self.now += seconds

        clock = Clock()
        with (
            mock.patch.object(mpb.time, "time", side_effect=clock.time),
            mock.patch.object(mpb.time, "sleep", side_effect=clock.sleep),
            self.assertRaisesRegex(
                RuntimeError, "frame count stabilised at 13"
            ),
        ):
            mpb._wait_for_trace_flatten_segments(
                StableTracer(),
                {"yuzulogo": self.specs["yuzulogo"]},
                timeout=300.0,
                stabilise_seconds=2.0,
            )
        self.assertGreaterEqual(clock.now, 10.0)
        self.assertLess(clock.now, 20.0)


if __name__ == "__main__":
    unittest.main()
