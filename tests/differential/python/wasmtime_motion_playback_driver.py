#!/usr/bin/env python3
"""Private LLDB driver process for Wasmtime motion_playback."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from oracle_runner.motion_capture_window import frame_capture_window_from_bounds
from run_motion_playback_wasmtime import drive_full_guest, load_specs


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Private Wasmtime motion_playback LLDB driver")
    p.add_argument("--wasm", required=True, type=Path,
                   help="Path to krkr2_wasmtime_guest.wasm")
    p.add_argument("--startup-xp3", required=True, type=Path,
                   help="Path to the startup XP3")
    p.add_argument("--spec-dir", required=True, type=Path,
                   help="Directory of motion_playback spec JSON files")
    p.add_argument("--frames", required=True, type=int,
                   help="Number of guest ticks to drive")
    p.add_argument("--output", required=True, type=Path,
                   help="Path for bootstrap summary JSON")
    p.add_argument("--record-framebuffer", action="store_true",
                   help="Write framebuffer PNGs")
    p.add_argument("--framebuffer-dir", type=Path, default=None,
                   help="Host path where framebuffer PNGs should be copied")
    p.add_argument("--record-render-stages", action="store_true",
                   help="Collect render stage diagnostics and images")
    p.add_argument("--record-render-step-checkpoints", action="store_true",
                   help="Save execute_pre/execute_post render checkpoints")
    p.add_argument("--checkpoint-render-only", action="store_true",
                   help="Build render PNG artifacts only from direct render "
                        "checkpoints")
    p.add_argument("--record-layer-raw-probes", action="store_true",
                   help="Capture raw Layer MainImage probe events")
    p.add_argument("--record-save-layer-visual-readback-probes",
                   action="store_true",
                   help="Capture saveLayerImage visual readback row hashes")
    p.add_argument("--save-layer-visual-readback-frame-start", type=int,
                   default=0,
                   help="First global frame id for saveLayerImage visual "
                        "readback row probes")
    p.add_argument("--save-layer-visual-readback-frame-count", type=int,
                   default=1,
                   help="Number of global frames to capture visual readback "
                        "rows for; use -1 for all frames")
    p.add_argument("--render-artifact-dir", type=Path, default=None,
                   help="Host path where render stage artifacts should be copied")
    p.add_argument("--render-stage-out", type=Path, default=None,
                   help="Path to write render stage JSON events")
    p.add_argument("--manifest-startup-xp3", type=Path, default=None,
                   help="Original startup XP3 path to record in manifests")
    p.add_argument("--capture-frame-start", type=int, default=0,
                   help="First global frame id to record in probes")
    p.add_argument("--capture-frame-count", type=int, default=-1,
                   help="Number of global frames to record; -1 records all")
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if args.frames <= 0:
        print("--frames must be positive", file=sys.stderr)
        return 2
    if not args.spec_dir.exists():
        print(f"spec dir not found: {args.spec_dir}", file=sys.stderr)
        return 2
    if args.record_framebuffer and args.framebuffer_dir is None:
        print("--framebuffer-dir is required with --record-framebuffer",
              file=sys.stderr)
        return 2
    if args.record_render_stages and args.render_artifact_dir is None:
        print("--render-artifact-dir is required with --record-render-stages",
              file=sys.stderr)
        return 2
    if args.record_render_stages and args.render_stage_out is None:
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
        specs = load_specs(args.spec_dir)
        total_frames = sum(int(spec["frames"]) for spec in specs)
        capture_window = frame_capture_window_from_bounds(
            total_frames=total_frames,
            start=int(args.capture_frame_start),
            count=int(args.capture_frame_count),
        )
        summary = drive_full_guest(
            args.wasm,
            args.startup_xp3,
            int(args.frames),
            framebuffer_dir=args.framebuffer_dir if args.record_framebuffer else None,
            render_artifact_dir=(
                args.render_artifact_dir if args.record_render_stages else None),
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
            capture_window=capture_window,
            render_stage_out=args.render_stage_out,
            specs=specs,
            manifest_startup_xp3=args.manifest_startup_xp3,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(summary, indent=2, ensure_ascii=True) + "\n",
            encoding="utf-8")
        return 0
    except Exception as exc:
        print(f"FAIL: Wasmtime LLDB driver error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
