#!/usr/bin/env python3
"""motion_playback differential runner.

Default path:
    Starts the Browser-WASM build, loads `logo_test_oracle.xp3`, collects
    the port-side `motionTrace=1` Motion.Player state trace, and diffs it
    against checked-in libkrkr2 goldens under
    `tests/differential/traces/motion_playback/<id>.oracle.json`.

Re-record path (`--record-oracle`):
    Spawns the APK harness on a Redroid / AVD device, triggers
    `TVPMainScene::startupFrom(logo_test_oracle.xp3)` via harness-RPC,
    and attaches Frida to hook Motion.Player progress/updateLayers.

Usage:
    run_motion_playback.py [--spec-dir DIR] [--web-build-dir DIR]
                           [--trace-dir DIR] [--record-oracle]
                           [--record-framebuffer]
                           [--framebuffer-dir DIR]
                           [--serial ADB_SERIAL]
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import mimetypes
import sys
import threading
import time
import urllib.parse
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
# Match the import pattern used by the other run_*_adb.py runners:
# oracle_runner.adb_engine / oracle_runner.adapters.* rely on the
# `oracle_runner` directory being a package, so tests/differential (the
# package parent) goes on sys.path rather than oracle_runner itself.
sys.path.insert(0, str(REPO_ROOT / "tests" / "differential"))

mimetypes.add_type("application/wasm", ".wasm")
mimetypes.add_type("application/octet-stream", ".data")


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="motion_playback differential runner")
    p.add_argument("--spec-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "specs" / "motion_playback"),
                   help="Directory of spec JSON files")
    p.add_argument("--trace-dir",
                   default=str(REPO_ROOT / "tests" / "differential" /
                               "traces" / "motion_playback"),
                   help="Directory of cached oracle JSONs")
    p.add_argument("--web-build-dir",
                   default=str(REPO_ROOT / "out" / "web" / "debug"),
                   help="Directory containing index.html/index.wasm")
    p.add_argument("--record-oracle", action="store_true",
                   help="Re-record disk goldens from a live APK harness "
                        "(requires --serial and a deployed harness)")
    p.add_argument("--record-framebuffer", action="store_true",
                   help="With --record-oracle, save every libkrkr2 "
                        "motion_playback frame as PNG artifacts and write "
                        "a manifest.json")
    p.add_argument("--framebuffer-dir", type=Path, default=None,
                   help="Framebuffer artifact output directory. Default: "
                        "tests/differential/artifacts/"
                        "motion_playback_framebuffer/<run-id>")
    p.add_argument("--serial", default=None,
                   help="ADB serial, only with --record-oracle")
    p.add_argument("--strict-missing-trace", action="store_true",
                   help="Fail when a disk golden is missing instead of "
                        "auto-skipping the case")
    p.add_argument("--only-structural", action="store_true",
                   help="Diff only structural Motion state fields "
                        "(index/nodeType/visible/active/flipX/flipY/"
                        "opacity/blendMode); skip accumulated transform "
                        "fields. Diagnostic strings/images are not compared "
                        "by default.")
    p.add_argument("--playback-timeout", type=float, default=90.0,
                   help="Seconds to wait for Browser-WASM playback trace or "
                        "Android oracle recording")
    return p.parse_args(argv)


def default_framebuffer_dir() -> Path:
    run_id = time.strftime("%Y%m%d-%H%M%S")
    return (
        REPO_ROOT / "tests" / "differential" / "artifacts"
        / "motion_playback_framebuffer" / run_id
    )


def load_specs(spec_dir: Path) -> list[dict]:
    specs = []
    for path in sorted(spec_dir.glob("*.json")):
        with path.open() as f:
            specs.append(json.load(f))
    return specs


class _MotionTraceHandler(http.server.SimpleHTTPRequestHandler):
    xp3_path: Path

    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def do_GET(self) -> None:  # noqa: N802 - http.server API
        if self._is_data_xp3():
            self._serve_xp3(head_only=False)
        else:
            super().do_GET()

    def do_HEAD(self) -> None:  # noqa: N802 - http.server API
        if self._is_data_xp3():
            self._serve_xp3(head_only=True)
        else:
            super().do_HEAD()

    def log_message(self, fmt: str, *args: Any) -> None:
        return

    def _is_data_xp3(self) -> bool:
        path = urllib.parse.urlparse(self.path).path
        return path == "/data.xp3"

    def _serve_xp3(self, *, head_only: bool) -> None:
        try:
            size = self.xp3_path.stat().st_size
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size))
            self.end_headers()
            if head_only:
                return
            with self.xp3_path.open("rb") as f:
                while True:
                    chunk = f.read(1024 * 1024)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
        except Exception as exc:
            self.send_error(500, str(exc))


class _TraceServer:
    def __init__(self, web_build_dir: Path, xp3_path: Path) -> None:
        self.web_build_dir = web_build_dir
        self.xp3_path = xp3_path
        self.httpd: http.server.ThreadingHTTPServer | None = None
        self.thread: threading.Thread | None = None
        self.url: str | None = None

    def __enter__(self) -> "_TraceServer":
        handler_cls = type(
            "MotionPlaybackTraceHandler",
            (_MotionTraceHandler,),
            {"xp3_path": self.xp3_path},
        )
        handler = functools.partial(handler_cls,
                                    directory=str(self.web_build_dir))
        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0),
                                                     handler)
        host, port = self.httpd.server_address
        self.url = (
            f"http://{host}:{port}/index.html"
            "?xp3=/data.xp3&motionTrace=1"
        )
        self.thread = threading.Thread(target=self.httpd.serve_forever,
                                       name="motion-trace-http",
                                       daemon=True)
        self.thread.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.httpd is not None:
            self.httpd.shutdown()
            self.httpd.server_close()
        if self.thread is not None:
            self.thread.join(timeout=5.0)


def _load_playwright():
    try:
        from playwright.sync_api import sync_playwright  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "playwright is not installed; run "
            "`python3 -m pip install -r "
            "tests/differential/python/requirements-wasm.txt` and "
            "`python3 -m playwright install chromium`"
        ) from exc
    return sync_playwright


def _segment_events(events: list[dict]) -> list[dict]:
    segments: list[dict] = []
    for ev in events:
        key = ev.get("objthis") or ev.get("topPlayer")
        if not segments or segments[-1]["player"] != key:
            segments.append({"player": key, "frames": []})
        segments[-1]["frames"].append(ev)
    return segments


def _wait_for_motion_trace(page, *, wanted_frames: int,
                           timeout: float) -> list[dict]:
    deadline = time.time() + timeout
    last_count = -1
    stable_since: float | None = None
    while time.time() < deadline:
        count = int(page.evaluate(
            "() => (window.__krkr2MotionTrace || []).length"
        ))
        if count != last_count:
            stable_since = None
            last_count = count
        elif count >= wanted_frames and stable_since is None:
            stable_since = time.time()
        if stable_since is not None and time.time() - stable_since >= 1.5:
            events = page.evaluate(
                "() => (window.__krkr2MotionTrace || []).slice()"
            )
            return list(events or [])
        time.sleep(0.25)
    raise RuntimeError(
        f"Browser-WASM motionTrace timed out after {timeout:.1f}s "
        f"(last frame count: {last_count}, expected at least {wanted_frames})"
    )


def run_web_port_trace(web_build_dir: Path, specs: list[dict],
                       *, timeout: float) -> list[dict]:
    index_html = web_build_dir / "index.html"
    if not index_html.exists():
        raise FileNotFoundError(
            f"Web build missing index.html: {index_html}. "
            "Build with `cmake --preset \"Web Debug Config\"` and "
            "`cmake --build out/web/debug`."
        )
    xp3_path = REPO_ROOT / "reference" / "xp3" / "logo_test_oracle.xp3"
    if not xp3_path.exists():
        raise FileNotFoundError(f"oracle bootstrap xp3 missing: {xp3_path}")

    sync_playwright = _load_playwright()
    wanted_frames = sum(int(s["frames"]) for s in specs)
    console_lines: list[str] = []
    with _TraceServer(web_build_dir, xp3_path) as server:
        assert server.url is not None
        with sync_playwright() as pw:
            browser = pw.chromium.launch(
                headless=True,
                args=["--no-sandbox", "--disable-dev-shm-usage"],
            )
            page = browser.new_page(viewport={"width": 1920, "height": 1080})
            page.on("console", lambda msg:
                    console_lines.append(f"[{msg.type}] {msg.text}"))
            try:
                page.goto(server.url, wait_until="domcontentloaded",
                          timeout=int(timeout * 1000))
                return _wait_for_motion_trace(page,
                                              wanted_frames=wanted_frames,
                                              timeout=timeout)
            except Exception as exc:
                tail = "\n".join(console_lines[-40:])
                raise RuntimeError(
                    f"Browser-WASM motion trace failed: {exc}\n"
                    f"console tail:\n{tail}"
                ) from exc
            finally:
                browser.close()


def partition_port_frames(events: list[dict], specs: list[dict], mpb) -> dict:
    specs_by_id = {s["id"]: s for s in specs}
    unknown = [sid for sid in specs_by_id if sid not in mpb.SEGMENT_ORDER]
    if unknown:
        raise ValueError(
            f"unknown motion_playback spec id(s): {unknown}. "
            f"Expected ids are fixed by logo_test_oracle.xp3: "
            f"{mpb.SEGMENT_ORDER}."
        )

    segments = _segment_events(events)
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    if len(substantive) < len(specs_by_id):
        raise RuntimeError(
            f"only {len(substantive)} substantive Browser-WASM segment(s) "
            f"captured (raw segments: {[len(s['frames']) for s in segments]})."
        )

    results: dict[str, list[dict]] = {}
    for i, spec_id in enumerate(mpb.SEGMENT_ORDER):
        if spec_id not in specs_by_id:
            continue
        spec = specs_by_id[spec_id]
        wanted = int(spec["frames"])
        frames = substantive[i]["frames"]
        if len(frames) < wanted:
            raise RuntimeError(
                f"Browser-WASM segment {i} ({spec_id}) has "
                f"{len(frames)} frames; spec requires {wanted}."
            )
        results[spec_id] = [
            mpb.normalize_frame(fr, fi)
            for fi, fr in enumerate(frames[:wanted])
        ]
    return results


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spec_dir = Path(args.spec_dir)
    trace_dir = Path(args.trace_dir)
    web_build_dir = Path(args.web_build_dir)

    if not spec_dir.exists():
        print(f"spec dir not found: {spec_dir}", file=sys.stderr)
        return 2

    specs = load_specs(spec_dir)
    if not specs:
        print(f"no specs in {spec_dir}", file=sys.stderr)
        return 0

    from oracle_runner.adapters import motion_playback as mpb

    if args.record_framebuffer and not args.record_oracle:
        print("--record-framebuffer requires --record-oracle", file=sys.stderr)
        return 2

    if args.record_oracle:
        from oracle_runner.adb_engine import AdbHarnessEngine
        if not args.serial:
            print("--record-oracle requires --serial", file=sys.stderr)
            return 2
        trace_dir.mkdir(parents=True, exist_ok=True)
        framebuffer_dir = (
            Path(args.framebuffer_dir) if args.framebuffer_dir is not None
            else default_framebuffer_dir()
        ) if args.record_framebuffer else None
        # Single playback covers every spec: startup.tjs inside
        # logo_test_oracle.xp3 plays all SEGMENT_ORDER motions sequentially,
        # and cocos2d only accepts one scheduleOnce("startup", ...) per
        # Activity lifetime. mpb.record_all_oracles returns
        # {spec_id: frames} in one shot.
        # record_all_oracles writes the per-game renderer=software preference
        # before startupFrom. Do not write the global preference before
        # HarnessActivity starts: Redroid CI can hang before READY there, and
        # libkrkr2's renderer default is already software.
        with AdbHarnessEngine(serial=args.serial) as engine:
            print(f"[record] capturing all {len(specs)} specs in one "
                  f"playback (Frida-hooked Motion.Player progress)")
            all_frames = mpb.record_all_oracles(
                engine, specs, serial=args.serial,
                playback_timeout=args.playback_timeout,
                framebuffer_dir=framebuffer_dir)
        for spec in specs:
            frames = all_frames.get(spec["id"])
            if frames is None:
                print(f"[record] {spec['id']}: no frames captured — "
                      f"spec id not in SEGMENT_ORDER or playback ended "
                      f"early", file=sys.stderr)
                continue
            target = trace_dir / f"{spec['id']}.oracle.json"
            with target.open("w") as f:
                json.dump(frames, f, indent=2, sort_keys=True)
            print(f"[record] {spec['id']}: wrote {len(frames)} frames "
                  f"to {target}")
        if framebuffer_dir is not None:
            manifest = framebuffer_dir / "manifest.json"
            if manifest.exists():
                print(f"[record] framebuffer manifest: {manifest}")
        return 0

    try:
        port_events = run_web_port_trace(web_build_dir, specs,
                                         timeout=args.playback_timeout)
        port_frames_by_id = partition_port_frames(port_events, specs, mpb)
    except Exception as exc:
        print(f"FAIL: Browser-WASM port trace error: {exc}", file=sys.stderr)
        return 1

    failures = 0
    for spec in specs:
        oracle_path = trace_dir / f"{spec['id']}.oracle.json"
        if not oracle_path.exists():
            msg = f"no oracle for {spec['id']} at {oracle_path}"
            if args.strict_missing_trace:
                print(f"FAIL: {msg}", file=sys.stderr)
                failures += 1
            else:
                print(f"SKIP: {msg}")
            continue
        with oracle_path.open() as f:
            oracle_frames = json.load(f)

        port_frames = port_frames_by_id.get(spec["id"])
        if port_frames is None:
            print(f"FAIL: {spec['id']}: no Browser-WASM frames captured",
                  file=sys.stderr)
            failures += 1
            continue

        result = mpb.run_case(None, spec,
                              port_frames=port_frames,
                              oracle_frames=oracle_frames,
                              structural_only=args.only_structural)
        if result["status"] == "ok":
            print(f"PASS: {spec['id']} ({len(port_frames)} frames)")
        else:
            print(f"FAIL: {spec['id']}: {result['status']} "
                  f"({len(result['mismatches'])} mismatches)")
            for m in result["mismatches"][:10]:
                print(f"  {m}")
            if len(result["mismatches"]) > 10:
                print(f"  ... +{len(result['mismatches']) - 10} more")
            failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
