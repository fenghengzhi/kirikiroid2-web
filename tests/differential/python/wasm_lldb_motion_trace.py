#!/usr/bin/env python3
"""LLDB-backed Wasmtime guest trace collector for motion_playback."""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

from wasm_lldb_runner import (
    load_lldb,
    register_double_arg,
    register_int_arg,
    run_lldb_command,
)


REPO_ROOT = Path(__file__).resolve().parents[3]
FRAME_BEGIN_SYMBOL = "krkr2_lldb_motion_frame_begin"
LAYER_SAMPLE_SYMBOL = "krkr2_lldb_motion_layer_sample"
FRAME_END_SYMBOL = "krkr2_lldb_motion_frame_end"


def ptr_to_hex(value: int | None) -> str | None:
    if not value:
        return None
    return f"0x{value:x}"


def probe_int(frame, index: int, default: int | None = None) -> int | None:
    try:
        return register_int_arg(frame, index)
    except Exception:
        return default


def probe_double(frame, index: int) -> float | None:
    try:
        parsed = register_double_arg(frame, index)
        return parsed if math.isfinite(parsed) else None
    except Exception:
        return None


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Trace motion_playback Wasmtime guest via LLDB")
    p.add_argument("--driver", required=True,
                   help="Path to the private Wasmtime LLDB driver")
    p.add_argument("--host-python", required=True,
                   help="Python interpreter LLDB launches as host")
    p.add_argument("--wasm", required=True,
                   help="Path to krkr2_wasmtime_guest.wasm")
    p.add_argument("--startup-xp3", required=True,
                   help="Path to logo_test_oracle.xp3")
    p.add_argument("--spec-dir", required=True,
                   help="Directory of motion_playback spec JSON files")
    p.add_argument("--trace-out", required=True,
                   help="Path to write LLDB-collected JSON trace events")
    p.add_argument("--driver-output", required=True,
                   help="Path for driver process bootstrap summary JSON")
    p.add_argument("--record-framebuffer", action="store_true",
                   help="Ask the driver process to write framebuffer PNGs")
    p.add_argument("--framebuffer-dir", default=None,
                   help="Host path where framebuffer PNGs should be copied")
    p.add_argument("--record-render-stages", action="store_true",
                   help="Collect Wasmtime render stage diagnostics and images")
    p.add_argument("--record-render-step-checkpoints", action="store_true",
                   help="Ask the driver process to save execute_pre/"
                        "execute_post render checkpoints")
    p.add_argument("--checkpoint-render-only", action="store_true",
                   help="Ask the driver process to build render PNG artifacts "
                        "only from direct render checkpoints")
    p.add_argument("--record-layer-raw-probes", action="store_true",
                   help="Ask the driver process to capture raw Layer "
                        "MainImage probe events")
    p.add_argument("--record-save-layer-visual-readback-probes",
                   action="store_true",
                   help="Ask the driver process to capture saveLayerImage "
                        "visual readback row hashes")
    p.add_argument("--save-layer-visual-readback-frame-start", type=int,
                   default=0,
                   help="First global frame id for saveLayerImage visual "
                        "readback row probes")
    p.add_argument("--save-layer-visual-readback-frame-count", type=int,
                   default=1,
                   help="Number of global frames to capture visual readback "
                        "rows for; use -1 for all frames")
    p.add_argument("--render-artifact-dir", default=None,
                   help="Host path where render stage artifacts should be copied")
    p.add_argument("--render-stage-out", default=None,
                   help="Path to write LLDB-collected render stage JSON events")
    p.add_argument("--manifest-startup-xp3", default=None,
                   help="Original startup XP3 path to record in manifest")
    p.add_argument("--expected-frames", type=int, default=332,
                   help="Minimum expected event count")
    p.add_argument("--capture-frame-start", type=int, default=0,
                   help="First global frame id to record in driver probes")
    p.add_argument("--capture-frame-count", type=int, default=-1,
                   help="Number of global frames to record; -1 records all")
    p.add_argument("--timeout", type=float, default=600.0,
                   help="Soft timeout checked between LLDB stops")
    p.add_argument("--repo-root", default=str(REPO_ROOT),
                   help="Repository root for driver working directory")
    return p.parse_args(argv)


class WasmMotionTracer:
    def __init__(
        self,
        *,
        lldb,
        driver: Path,
        host_python: Path,
        wasm: Path,
        startup_xp3: Path,
        spec_dir: Path,
        driver_output: Path,
        repo_root: Path,
        expected_frames: int,
        timeout: float,
        record_framebuffer: bool = False,
        framebuffer_dir: Path | None = None,
        record_render_stages: bool = False,
        record_render_step_checkpoints: bool = False,
        checkpoint_render_only: bool = False,
        record_layer_raw_probes: bool = False,
        record_save_layer_visual_readback_probes: bool = False,
        save_layer_visual_readback_frame_start: int = 0,
        save_layer_visual_readback_frame_count: int = 1,
        capture_frame_start: int = 0,
        capture_frame_count: int = -1,
        render_artifact_dir: Path | None = None,
        render_stage_out: Path | None = None,
        manifest_startup_xp3: Path | None = None,
    ) -> None:
        self.lldb = lldb
        self.driver = driver
        self.host_python = host_python
        self.wasm = wasm
        self.startup_xp3 = startup_xp3
        self.spec_dir = spec_dir
        self.driver_output = driver_output
        self.repo_root = repo_root
        self.expected_frames = expected_frames
        self.timeout = timeout
        self.record_framebuffer = record_framebuffer
        self.framebuffer_dir = framebuffer_dir
        self.record_render_stages = record_render_stages
        self.record_render_step_checkpoints = record_render_step_checkpoints
        self.checkpoint_render_only = checkpoint_render_only
        self.record_layer_raw_probes = record_layer_raw_probes
        self.record_save_layer_visual_readback_probes = (
            record_save_layer_visual_readback_probes)
        self.save_layer_visual_readback_frame_start = (
            save_layer_visual_readback_frame_start)
        self.save_layer_visual_readback_frame_count = (
            save_layer_visual_readback_frame_count)
        self.capture_frame_start = capture_frame_start
        self.capture_frame_count = capture_frame_count
        self.render_artifact_dir = render_artifact_dir
        self.render_stage_out = render_stage_out
        self.manifest_startup_xp3 = manifest_startup_xp3
        self.events: list[dict[str, Any]] = []
        self.frame_records: dict[int, dict[str, Any]] = {}
        self.callback_errors: list[str] = []
        self.begin_bp_id: int | None = None
        self.layer_bp_id: int | None = None
        self.end_bp_id: int | None = None
        self.begin_hits = 0
        self.layer_hits = 0
        self.end_hits = 0
        self.last_completed_frame_id: int | None = None
        self.last_top_player: str | None = None
        self.driver_exit_status: int | None = None
        self.driver_stdout = ""
        self.driver_stderr = ""
        self.driver_summary = ""

    def run(self) -> list[dict[str, Any]]:
        lldb = self.lldb
        debugger = lldb.SBDebugger.Create()
        debugger.SetAsync(False)
        try:
            try:
                run_lldb_command(
                    lldb,
                    debugger,
                    "settings set plugin.jit-loader.gdb.enable on",
                )
            except RuntimeError:
                if sys.platform == "darwin":
                    raise
            target = debugger.CreateTarget(str(self.host_python))
            if not target or not target.IsValid():
                raise RuntimeError(f"failed to create LLDB target: {self.host_python}")

            begin_bp = target.BreakpointCreateByName(FRAME_BEGIN_SYMBOL)
            layer_bp = target.BreakpointCreateByName(LAYER_SAMPLE_SYMBOL)
            end_bp = target.BreakpointCreateByName(FRAME_END_SYMBOL)
            self.begin_bp_id = begin_bp.GetID()
            self.layer_bp_id = layer_bp.GetID()
            self.end_bp_id = end_bp.GetID()

            with tempfile.TemporaryDirectory(
                prefix="krkr2-motion-wasmtime-driver-"
            ) as temp_dir:
                temp = Path(temp_dir)
                stdout_path = temp / "driver.stdout"
                stderr_path = temp / "driver.stderr"
                launch_args = [
                    str(self.driver),
                    "--wasm",
                    str(self.wasm),
                    "--startup-xp3",
                    str(self.startup_xp3),
                    "--spec-dir",
                    str(self.spec_dir),
                    "--frames",
                    # startupFrom schedules the first motion update a few host
                    # ticks later; the TJS fixture still stops after the
                    # selected global frame window is complete.
                    str(max(self.expected_frames + 10, 1)),
                    "--output",
                    str(self.driver_output),
                    "--capture-frame-start",
                    str(self.capture_frame_start),
                    "--capture-frame-count",
                    str(self.capture_frame_count),
                ]
                if self.record_framebuffer:
                    if self.framebuffer_dir is None:
                        raise RuntimeError(
                            "record_framebuffer requires framebuffer_dir")
                    launch_args += [
                        "--record-framebuffer",
                        "--framebuffer-dir",
                        str(self.framebuffer_dir),
                    ]
                    if self.manifest_startup_xp3 is not None:
                        launch_args += [
                            "--manifest-startup-xp3",
                            str(self.manifest_startup_xp3),
                        ]
                if self.record_render_stages:
                    if self.render_artifact_dir is None:
                        raise RuntimeError(
                            "record_render_stages requires render_artifact_dir")
                    if self.render_stage_out is None:
                        raise RuntimeError(
                            "record_render_stages requires render_stage_out")
                    launch_args += [
                        "--record-render-stages",
                        "--render-artifact-dir",
                        str(self.render_artifact_dir),
                        "--render-stage-out",
                        str(self.render_stage_out),
                    ]
                    if self.record_render_step_checkpoints:
                        launch_args.append(
                            "--record-render-step-checkpoints")
                    if self.checkpoint_render_only:
                        launch_args.append("--checkpoint-render-only")
                    if self.record_layer_raw_probes:
                        launch_args.append("--record-layer-raw-probes")
                    if self.record_save_layer_visual_readback_probes:
                        launch_args.append(
                            "--record-save-layer-visual-readback-probes")
                        launch_args += [
                            "--save-layer-visual-readback-frame-start",
                            str(self.save_layer_visual_readback_frame_start),
                            "--save-layer-visual-readback-frame-count",
                            str(self.save_layer_visual_readback_frame_count),
                        ]
                launch = lldb.SBLaunchInfo(launch_args)
                launch.SetWorkingDirectory(str(self.repo_root))
                gl_probe_env = [
                    f"{name}={value}"
                    for name, value in os.environ.items()
                    if (name.startswith("KRKR2_WASMTIME_GL_") or
                        name.startswith("MESA_") or
                        name in {
                            "DISPLAY",
                            "DYLD_FALLBACK_LIBRARY_PATH",
                            "DYLD_LIBRARY_PATH",
                            "EGL_PLATFORM",
                            "KRKR2_WASMTIME_RENDERER",
                            "KRKR2_WASMTIME_OSMESA_LIBRARY",
                            "LIBGL_ALWAYS_SOFTWARE",
                            "PYOPENGL_PLATFORM",
                            "WAYLAND_DISPLAY",
                            "XAUTHORITY",
                            "XDG_RUNTIME_DIR",
                        })
                ]
                if gl_probe_env:
                    launch.SetEnvironmentEntries(gl_probe_env, True)
                launch.AddOpenFileAction(1, str(stdout_path), False, True)
                launch.AddOpenFileAction(2, str(stderr_path), False, True)

                error = lldb.SBError()
                process = target.Launch(launch, error)
                if not error.Success():
                    raise RuntimeError(f"LLDB launch failed: {error.GetCString()}")

                self._drive_process(process, stdout_path, stderr_path)
                self.driver_exit_status = process.GetExitStatus()
                self.driver_stdout, self.driver_stderr = self._read_stdio(
                    stdout_path, stderr_path)
                if self.driver_output.exists():
                    self.driver_summary = self.driver_output.read_text(
                        encoding="utf-8", errors="replace")
                if self.driver_exit_status not in (0, -1):
                    raise RuntimeError(
                        f"driver process exited with {self.driver_exit_status}\n"
                        f"driver summary:\n{self.driver_summary}"
                        f"stdout:\n{self.driver_stdout}"
                        f"stderr:\n{self.driver_stderr}"
                    )
        finally:
            lldb.SBDebugger.Destroy(debugger)

        if self.begin_hits == 0:
            raise RuntimeError(self._missing_breakpoint_message(
                FRAME_BEGIN_SYMBOL))
        if self.layer_hits == 0:
            raise RuntimeError(self._missing_breakpoint_message(
                LAYER_SAMPLE_SYMBOL))
        if self.end_hits == 0:
            raise RuntimeError(self._missing_breakpoint_message(
                FRAME_END_SYMBOL))
        if self.expected_frames and len(self.events) < self.expected_frames:
            raise RuntimeError(
                f"Wasmtime LLDB trace captured only {len(self.events)} "
                f"event(s); expected at least {self.expected_frames}. "
                f"breakpoints: begin={self.begin_hits}, "
                f"layer={self.layer_hits}, end={self.end_hits}"
            )
        return self.events

    def _missing_breakpoint_message(self, symbol: str) -> str:
        return (
            f"LLDB breakpoint was not hit: {symbol}\n"
            f"driverExitStatus={self.driver_exit_status}\n"
            f"driver summary:\n{self.driver_summary}"
            f"stdout:\n{self.driver_stdout}"
            f"stderr:\n{self.driver_stderr}"
        )

    def _drive_process(self, process, stdout_path: Path,
                       stderr_path: Path) -> None:
        lldb = self.lldb
        deadline = time.monotonic() + self.timeout
        while True:
            if self.callback_errors:
                backtrace = self._format_backtrace(process)
                stdout, stderr = self._read_stdio(stdout_path, stderr_path)
                process.Kill()
                raise RuntimeError(
                    "; ".join(self.callback_errors)
                    + f"\n{backtrace}\nstdout:\n{stdout}\nstderr:\n{stderr}"
                )
            state = process.GetState()
            if state == lldb.eStateExited:
                break
            if time.monotonic() > deadline:
                backtrace = self._format_backtrace(process)
                stdout, stderr = self._read_stdio(stdout_path, stderr_path)
                process.Kill()
                raise RuntimeError(
                    f"Wasmtime LLDB trace timed out after {self.timeout:.1f}s "
                    f"with {len(self.events)} event(s); breakpoints: "
                    f"begin={self.begin_hits}, layer={self.layer_hits}, "
                    f"end={self.end_hits}\n"
                    f"{backtrace}\nstdout:\n{stdout}\nstderr:\n{stderr}"
                )
            if state == lldb.eStateStopped:
                self._handle_stopped_process(process)
            cont_error = process.Continue()
            if not cont_error.Success():
                raise RuntimeError(
                    f"LLDB continue failed: {cont_error.GetCString()}"
                )

    @staticmethod
    def _read_stdio(stdout_path: Path, stderr_path: Path) -> tuple[str, str]:
        stdout = stdout_path.read_text(encoding="utf-8", errors="replace") \
            if stdout_path.exists() else ""
        stderr = stderr_path.read_text(encoding="utf-8", errors="replace") \
            if stderr_path.exists() else ""
        return stdout, stderr

    def _handle_stopped_process(self, process) -> None:
        for thread_index in range(process.GetNumThreads()):
            thread = process.GetThreadAtIndex(thread_index)
            if thread.GetStopReason() != self.lldb.eStopReasonBreakpoint:
                continue
            frame = thread.GetFrameAtIndex(0)
            ids = self._breakpoint_ids_for_thread(thread)
            if self.begin_bp_id in ids:
                self._on_frame_begin(frame)
            if self.layer_bp_id in ids:
                self._on_layer_sample(frame)
            if self.end_bp_id in ids:
                self._on_frame_end(frame)

    @staticmethod
    def _breakpoint_ids_for_thread(thread) -> set[int]:
        ids: set[int] = set()
        count = thread.GetStopReasonDataCount()
        for i in range(0, count, 2):
            ids.add(int(thread.GetStopReasonDataAtIndex(i)))
        return ids

    def _on_frame_begin(self, frame) -> None:
        self.begin_hits += 1
        frame_id = probe_int(frame, 0)
        if frame_id is None:
            self.callback_errors.append(
                f"{FRAME_BEGIN_SYMBOL} had no frameId register")
            return
        record: dict[str, Any] = {
            "frameId": frame_id,
            "objthis": ptr_to_hex(probe_int(frame, 1)),
            "topPlayer": ptr_to_hex(probe_int(frame, 2)),
            "playerCount": probe_int(frame, 3, 0) or 0,
            "layer_map": {},
        }
        self.frame_records[frame_id] = record

    def _layer_for(self, frame_id: int, index: int) -> dict[str, Any]:
        record = self.frame_records.setdefault(
            frame_id,
            {
                "frameId": frame_id,
                "objthis": None,
                "topPlayer": None,
                "playerCount": 0,
                "layer_map": {},
            },
        )
        layer_map = record["layer_map"]
        layer = layer_map.get(index)
        if layer is None:
            layer = {
                "index": index,
                "label": "",
                "currentImage": "",
            }
            layer_map[index] = layer
        return layer

    def _on_layer_sample(self, frame) -> None:
        self.layer_hits += 1
        frame_id = probe_int(frame, 0)
        index = probe_int(frame, 1)
        if frame_id is None or index is None:
            self.callback_errors.append(
                f"{LAYER_SAMPLE_SYMBOL} missing frame/index registers")
            return
        layer = self._layer_for(frame_id, index)
        node_flags = probe_int(frame, 2, 0) or 0
        opacity_blend = probe_int(frame, 3, 0) or 0
        flags = int(node_flags & 0xffffffff)
        node_type = int((node_flags >> 32) & 0xffffffff)
        if node_type >= 0x80000000:
            node_type -= 0x100000000
        opacity = int((opacity_blend >> 32) & 0xffffffff)
        if opacity >= 0x80000000:
            opacity -= 0x100000000
        blend = int(opacity_blend & 0xffffffff)
        if blend >= 0x80000000:
            blend -= 0x100000000
        layer["nodeType"] = node_type
        layer["opacity"] = opacity
        layer["blendMode"] = blend
        layer["visible"] = bool(flags & (1 << 0))
        layer["active"] = bool(flags & (1 << 1))
        layer["flipX"] = bool(flags & (1 << 2))
        layer["flipY"] = bool(flags & (1 << 3))
        for key, reg in (
            ("posX", 0),
            ("posY", 1),
            ("posZ", 2),
            ("angleDeg", 3),
            ("scaleX", 4),
            ("scaleY", 5),
            ("slantX", 6),
            ("slantY", 7),
        ):
            layer[key] = probe_double(frame, reg)

    def _on_frame_end(self, frame) -> None:
        self.end_hits += 1
        frame_id = probe_int(frame, 0)
        if frame_id is None:
            self.callback_errors.append(
                f"{FRAME_END_SYMBOL} had no frameId register")
            return
        record = self.frame_records.pop(frame_id, None)
        if record is None:
            self.callback_errors.append(
                f"{FRAME_END_SYMBOL} for unknown frame {frame_id}")
            return
        layer_map = record.pop("layer_map")
        layers = [layer_map[i] for i in sorted(layer_map)]
        event = {
            "frameId": frame_id,
            "objthis": record.get("objthis"),
            "topPlayer": record.get("topPlayer"),
            "playerCount": record.get("playerCount", 0),
            "layout": "wasmtime-lldb",
            "layers": layers,
        }
        self.events.append(event)
        self.last_completed_frame_id = frame_id
        self.last_top_player = event.get("topPlayer")

    def _format_backtrace(self, process) -> str:
        lines = ["thread backtrace all:"]
        for thread_index in range(process.GetNumThreads()):
            thread = process.GetThreadAtIndex(thread_index)
            lines.append(
                f"thread #{thread_index}: stop_reason={thread.GetStopReason()}"
            )
            for frame_index in range(min(thread.GetNumFrames(), 32)):
                frame = thread.GetFrameAtIndex(frame_index)
                lines.append(f"  {self._describe_frame(frame)}")
        return "\n".join(lines)

    @staticmethod
    def _describe_frame(frame) -> str:
        function = frame.GetFunctionName() or frame.GetSymbol().GetName() or "<unknown>"
        line_entry = frame.GetLineEntry()
        file_name = ""
        line_no = 0
        if line_entry and line_entry.IsValid():
            spec = line_entry.GetFileSpec()
            directory = spec.GetDirectory() or ""
            filename = spec.GetFilename() or ""
            file_name = str(Path(directory) / filename) if directory else filename
            line_no = line_entry.GetLine()
        location = f" at {file_name}:{line_no}" if file_name and line_no else ""
        return f"{frame.GetFrameID()}: {function}{location}"


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    driver = Path(args.driver)
    host_python = Path(args.host_python)
    wasm = Path(args.wasm)
    startup_xp3 = Path(args.startup_xp3)
    spec_dir = Path(args.spec_dir)
    trace_out = Path(args.trace_out)
    driver_output = Path(args.driver_output)
    repo_root = Path(args.repo_root)
    framebuffer_dir = (
        Path(args.framebuffer_dir) if args.framebuffer_dir is not None
        else None
    )
    render_artifact_dir = (
        Path(args.render_artifact_dir)
        if args.render_artifact_dir is not None else None
    )
    render_stage_out = (
        Path(args.render_stage_out) if args.render_stage_out is not None
        else None
    )
    manifest_startup_xp3 = (
        Path(args.manifest_startup_xp3)
        if args.manifest_startup_xp3 is not None else None
    )

    for label, path in (
        ("driver", driver),
        ("host Python", host_python),
        ("wasm", wasm),
        ("startup xp3", startup_xp3),
        ("spec dir", spec_dir),
    ):
        if not path.exists():
            print(f"{label} not found: {path}", file=sys.stderr)
            return 2
    if args.record_framebuffer and framebuffer_dir is None:
        print("--framebuffer-dir is required with --record-framebuffer",
              file=sys.stderr)
        return 2
    if args.record_render_stages and render_artifact_dir is None:
        print("--render-artifact-dir is required with --record-render-stages",
              file=sys.stderr)
        return 2
    if args.record_render_stages and render_stage_out is None:
        print("--render-stage-out is required with --record-render-stages",
              file=sys.stderr)
        return 2
    if args.record_render_step_checkpoints and not args.record_render_stages:
        print("--record-render-step-checkpoints requires "
              "--record-render-stages", file=sys.stderr)
        return 2
    if args.checkpoint_render_only and not args.record_render_step_checkpoints:
        print("--checkpoint-render-only requires "
              "--record-render-step-checkpoints", file=sys.stderr)
        return 2
    if args.record_layer_raw_probes and not args.record_render_stages:
        print("--record-layer-raw-probes requires --record-render-stages",
              file=sys.stderr)
        return 2
    if (args.record_save_layer_visual_readback_probes and
            not args.record_render_stages):
        print("--record-save-layer-visual-readback-probes requires "
              "--record-render-stages", file=sys.stderr)
        return 2

    try:
        lldb = load_lldb()
        tracer = WasmMotionTracer(
            lldb=lldb,
            driver=driver,
            host_python=host_python,
            wasm=wasm,
            startup_xp3=startup_xp3,
            spec_dir=spec_dir,
            driver_output=driver_output,
            repo_root=repo_root,
            expected_frames=args.expected_frames,
            timeout=args.timeout,
            record_framebuffer=args.record_framebuffer,
            framebuffer_dir=framebuffer_dir,
            record_render_stages=args.record_render_stages,
            record_render_step_checkpoints=(
                args.record_render_step_checkpoints),
            checkpoint_render_only=args.checkpoint_render_only,
            record_layer_raw_probes=args.record_layer_raw_probes,
            record_save_layer_visual_readback_probes=(
                args.record_save_layer_visual_readback_probes),
            save_layer_visual_readback_frame_start=(
                args.save_layer_visual_readback_frame_start),
            save_layer_visual_readback_frame_count=(
                args.save_layer_visual_readback_frame_count),
            capture_frame_start=args.capture_frame_start,
            capture_frame_count=args.capture_frame_count,
            render_artifact_dir=render_artifact_dir,
            render_stage_out=render_stage_out,
            manifest_startup_xp3=manifest_startup_xp3,
        )
        events = tracer.run()
        trace_out.parent.mkdir(parents=True, exist_ok=True)
        trace_out.write_text(
            json.dumps(events, ensure_ascii=False, allow_nan=False) + "\n",
            encoding="utf-8",
        )
        print(
            "LLDB Wasmtime guest breakpoints: "
            f"begin={tracer.begin_hits}, layer={tracer.layer_hits}, "
            f"end={tracer.end_hits}",
            file=sys.stderr,
        )
    except Exception as exc:
        print(f"Wasmtime LLDB motion trace failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
