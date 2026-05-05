"""Adapter for the `motion_playback` differential family.

Two modes:
  * Live oracle (`record_all_oracles`): attach Frida to the APK harness,
    drop `logo_test.xp3` on the device, trigger
    `TVPMainScene::startupFrom` via the harness-RPC engine (pure
    scheduler call; doesn't touch GL thread state), and let the embedded
    `startup.tjs` play yuzulogo then m2logo on the cocos2d GL thread.
    Frida's `Interceptor.attach` on `Player_updateLayers @ +0x6BB33C`
    captures per-frame per-layer accum state at the exact point where
    it's coherent — no cross-thread RPC into Motion.Player methods, so
    no GL-thread-affinity SIGSEGV.

    Rationale for the architecture split (harness-RPC for the boot
    call; Frida for the runtime observation): see the "分工原则"
    section in /Users/bytedance/.claude/plans/
    oracle-runner-panda-floofy-garden.md.

  * Disk oracle (`run_case`): compare Browser-WASM port trace output
    against a checked-in golden JSON. No Android device is required.

Previous revisions of this file shipped a TJS snapshot script executed
via `engine.tjs_exec_str` from the harness-rpc pthread. That approach
crashed consistently: Motion.Player's `getLayerNames`/`draw` methods
iterate the node tree with GL-thread assumptions, and calling them
from our RPC worker SIGSEGV'd in `emutls_key_destructor`. Hooking
`Player_updateLayers` from its *natural* GL-thread caller side-steps
the whole problem.
"""

from __future__ import annotations

import json
import re
import shlex
import subprocess
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any

from oracle_runner.motion_capture_window import (
    FrameCaptureWindow,
    captured_case_ranges,
    frame_capture_window_from_args,
)
from oracle_runner.png_artifacts import png_manifest_entry


# Schema fields, kept in sync with the Browser-WASM motionTrace hook.
# The default golden diff intentionally compares Motion node state only.
# Strings/images and draw diagnostics are kept in JSON snapshots to aid
# investigation, but libkrkr2's Frida trace and the Browser-WASM port trace
# do not need their diagnostic labels to match byte-for-byte for state parity.
LAYER_FIELDS_NUM = (
    "posX", "posY", "posZ", "angleDeg",
    "scaleX", "scaleY", "slantX", "slantY",
)
LAYER_FIELDS_INT = ("opacity", "blendMode", "nodeType", "index")
LAYER_FIELDS_BOOL = ("visible", "active", "flipX", "flipY")
LAYER_FIELDS_STR = ("label", "currentImage")
COMPARE_FIELDS_STR: tuple[str, ...] = ()


# Order that logo_test.xp3's startup.tjs plays motions. We partition the
# Frida trace by player pointer; the first segment is yuzulogo, second
# is m2logo. Adapter-level contract: spec ids must be one of these.
SEGMENT_ORDER: tuple[str, ...] = ("yuzulogo", "m2logo")


# Deterministic oracle-recording xp3. Its startup.tjs runs fixed-step
# `player.progress(1000/60)` loops (241 frames for yuzulogo, 91 for
# m2logo). The Browser-WASM verifier loads this same xp3 and collects the
# port-side `motionTrace=1` samples, instead of using logo_test.xp3's
# real-time variable-step doFrame. Sources live in the reference submodule
# (reference/xp3/logo_test_oracle/startup.tjs + the shared mtn files in
# reference/xp3/logo_test/). Regenerate via
# `tests/differential/oracle_runner/fixtures/build_logo_test_oracle.sh`
# whenever the spec frame counts change.
_LOGO_TEST_XP3_REL = "reference/xp3/logo_test_oracle.xp3"
_REMOTE_APP_FILES_DIR = "/sdcard/Android/data/org.github.krkr2/files"
ORACLE_RENDERER = "software"
ORACLE_RENDERER_SOURCE = "explicit-oracle-preference"
_ORACLE_GLOBAL_PREFERENCE_PATH = (
    "/data/user/0/org.github.krkr2/files/.preference/GlobalPreference.xml"
)
_ORACLE_GAME_PREFERENCE_FILE = "Kirikiroid2Preference.xml"
_ORACLE_TMP_PREFERENCE_PATH = (
    "/data/local/tmp/krkr2-motion-oracle-renderer-preference.xml"
)
_FRAMEBUFFER_SCHEMA = "motion-framebuffer-oracle-v1"
_FRAMEBUFFER_SOURCE = "android-libkrkr2-saveLayerImage"
_FRAMEBUFFER_CAPTURE_SURFACE = (
    "Layer main image immediately after Motion.Player.draw(base)"
)
_RENDER_STAGE_CAPTURE_SURFACES = ("initial", "post_draw")
_REFERENCE_RENDER_STAGE_CAPTURE_ROOT = (
    f"{_REMOTE_APP_FILES_DIR}/savedata/motion_render_stage_capture"
)


# ---------------------------------------------------------------- device ops

def push_fixture(serial: str | None, local: Path, remote: str) -> None:
    cmd = ["adb"]
    if serial:
        cmd += ["-s", serial]
    cmd += ["push", str(local), remote]
    subprocess.run(cmd, check=True, capture_output=True)


def _adb_shell(serial: str | None, cmdline: str) -> str:
    cmd = ["adb"]
    if serial:
        cmd += ["-s", serial]
    cmd += ["shell", cmdline]
    out = subprocess.run(cmd, check=True, capture_output=True)
    return out.stdout.decode(errors="replace")


def _adb_shell_root(serial: str | None, args: list[str]) -> str:
    cmd = ["adb"]
    if serial:
        cmd += ["-s", serial]
    cmd += ["shell", "su", "0"] + args
    out = subprocess.run(cmd, check=True, capture_output=True)
    return out.stdout.decode(errors="replace")


def _adb_pull(serial: str | None, remote: str, local: Path) -> None:
    cmd = ["adb"]
    if serial:
        cmd += ["-s", serial]
    cmd += ["pull", remote, str(local)]
    subprocess.run(cmd, check=True, capture_output=True)


def _subprocess_error_text(exc: subprocess.CalledProcessError) -> str:
    parts: list[str] = []
    for name in ("stdout", "stderr"):
        data = getattr(exc, name, None)
        if not data:
            continue
        if isinstance(data, bytes):
            text = data.decode(errors="replace").strip()
        else:
            text = str(data).strip()
        if text:
            parts.append(f"{name}: {text}")
    return "; ".join(parts) or str(exc)


def _renderer_preference_xml(renderer: str = ORACLE_RENDERER) -> str:
    return (
        "<?xml version=\"1.0\"?>\n"
        "<GlobalPreference>\n"
        f"    <Item key=\"renderer\" value=\"{renderer}\"/>\n"
        "</GlobalPreference>\n"
    )


def _write_local_temp_text(text: str) -> Path:
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", delete=False,
        prefix="krkr2-oracle-renderer-", suffix=".xml",
    ) as fp:
        fp.write(text)
        return Path(fp.name)


def _write_remote_text(
    serial: str | None,
    remote_path: str,
    text: str,
    *,
    root: bool,
) -> None:
    local = _write_local_temp_text(text)
    try:
        if root:
            push_fixture(serial, local, _ORACLE_TMP_PREFERENCE_PATH)
            parent = str(PurePosixPath(remote_path).parent)
            _adb_shell_root(serial, ["mkdir", "-p", parent])
            _adb_shell_root(
                serial, ["cp", _ORACLE_TMP_PREFERENCE_PATH, remote_path])
            _adb_shell_root(serial, ["chmod", "644", remote_path])
            try:
                _adb_shell(
                    serial,
                    f"rm -f {shlex.quote(_ORACLE_TMP_PREFERENCE_PATH)}",
                )
            except subprocess.CalledProcessError:
                pass
        else:
            parent = str(PurePosixPath(remote_path).parent)
            _adb_shell(serial, f"mkdir -p {shlex.quote(parent)}")
            push_fixture(serial, local, remote_path)
    finally:
        local.unlink(missing_ok=True)


def _read_remote_text(
    serial: str | None,
    remote_path: str,
    *,
    root: bool,
) -> str:
    if root:
        return _adb_shell_root(serial, ["cat", remote_path])
    return _adb_shell(serial, f"cat {shlex.quote(remote_path)}")


def _game_preference_path(remote_game: str | None = None) -> str:
    if remote_game:
        return str(
            PurePosixPath(remote_game).parent / _ORACLE_GAME_PREFERENCE_FILE
        )
    return f"{_REMOTE_APP_FILES_DIR}/{_ORACLE_GAME_PREFERENCE_FILE}"


def oracle_renderer_metadata() -> dict[str, str]:
    return {
        "renderer": ORACLE_RENDERER,
        "rendererSource": ORACLE_RENDERER_SOURCE,
    }


def ensure_oracle_renderer_software(
    serial: str | None,
    *,
    remote_game: str | None = None,
    write_global: bool = True,
) -> None:
    """Force Android oracle playback to use libkrkr2's software renderer.

    The global preference must be written before HarnessActivity starts;
    the per-game preference is written again immediately before startupFrom
    so stale device state cannot switch the parity lane to OpenGL/hardware.
    """
    xml = _renderer_preference_xml()
    try:
        if write_global:
            _write_remote_text(
                serial, _ORACLE_GLOBAL_PREFERENCE_PATH, xml, root=True)
            global_text = _read_remote_text(
                serial, _ORACLE_GLOBAL_PREFERENCE_PATH, root=True)
            if global_text.strip() != xml.strip():
                raise RuntimeError(
                    "Android Oracle renderer=software verification failed; "
                    "Oracle renderer cannot be guaranteed. "
                    "Global preference did not match: "
                    f"{_ORACLE_GLOBAL_PREFERENCE_PATH}")

        game_pref = _game_preference_path(remote_game)
        _write_remote_text(serial, game_pref, xml, root=False)
        game_text = _read_remote_text(serial, game_pref, root=False)
        if game_text.strip() != xml.strip():
            raise RuntimeError(
                "Android Oracle renderer=software verification failed; "
                "Oracle renderer cannot be guaranteed. "
                "Per-game preference did not match: "
                f"{game_pref}")
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(
            "failed to enforce Android Oracle renderer=software; "
            "Oracle renderer cannot be guaranteed. "
            f"ADB error: {_subprocess_error_text(exc)}"
        ) from exc


def _ensure_logo_test_xp3_pushed(serial: str | None) -> str:
    """Push the oracle bootstrap xp3 (fixed-step startup.tjs wrapper
    around logo_test's yuzulogo + m2logo motions) to a path that
    TVPCheckStartupPath accepts. Returns the device-side absolute path."""
    repo_root = Path(__file__).resolve().parents[4]
    local = repo_root / _LOGO_TEST_XP3_REL
    if not local.exists():
        raise FileNotFoundError(
            f"oracle bootstrap xp3 missing: {local}. "
            f"Build it via tests/differential/oracle_runner/fixtures/"
            f"build_logo_test_oracle.sh (requires the xp3pack tool).")
    # app's scoped-storage dir: write access guaranteed on API 29+.
    remote_dir = _REMOTE_APP_FILES_DIR
    _adb_shell(serial, f"mkdir -p {remote_dir}")
    remote_path = f"{remote_dir}/logo_test_oracle.xp3"
    push_fixture(serial, local, remote_path)
    return remote_path


def _prepare_framebuffer_capture(
    serial: str | None,
    specs_by_id: dict[str, dict],
    framebuffer_dir: Path,
    capture_window: FrameCaptureWindow | None = None,
) -> tuple[str, str]:
    remote_capture_root = _REFERENCE_RENDER_STAGE_CAPTURE_ROOT
    quoted_root = shlex.quote(remote_capture_root)
    _adb_shell(serial, f"rm -rf {quoted_root} && mkdir -p {quoted_root}")
    total_frames = sum(int(spec["frames"]) for spec in specs_by_id.values())
    if capture_window is None:
        class _Args:
            record_only_frame = None
            record_first_frames = None
        capture_window = frame_capture_window_from_args(_Args(), total_frames)
    for case in captured_case_ranges(specs_by_id, SEGMENT_ORDER,
                                     capture_window):
        spec_id = str(case["caseId"])
        for phase in _RENDER_STAGE_CAPTURE_SURFACES:
            _adb_shell(
                serial,
                "mkdir -p "
                f"{shlex.quote(remote_capture_root + '/' + spec_id + '/' + phase)}",
            )

    local_xp3 = Path(__file__).resolve().parents[4] / _LOGO_TEST_XP3_REL
    if not local_xp3.exists():
        raise FileNotFoundError(
            f"reference oracle XP3 missing: {local_xp3}. Regenerate with "
            "tests/differential/oracle_runner/fixtures/"
            "build_logo_test_oracle.sh")
    remote_xp3 = f"{_REMOTE_APP_FILES_DIR}/logo_test_oracle.xp3"
    push_fixture(serial, local_xp3, remote_xp3)
    return remote_xp3, remote_capture_root


def _prepare_render_stage_capture(
    serial: str | None,
    specs_by_id: dict[str, dict],
    artifact_dir: Path,
    capture_window: FrameCaptureWindow | None = None,
) -> tuple[str, str]:
    remote_capture_root = _REFERENCE_RENDER_STAGE_CAPTURE_ROOT
    quoted_root = shlex.quote(remote_capture_root)
    _adb_shell(serial, f"rm -rf {quoted_root} && mkdir -p {quoted_root}")
    total_frames = sum(int(spec["frames"]) for spec in specs_by_id.values())
    if capture_window is None:
        class _Args:
            record_only_frame = None
            record_first_frames = None
        capture_window = frame_capture_window_from_args(_Args(), total_frames)
    for case in captured_case_ranges(specs_by_id, SEGMENT_ORDER,
                                     capture_window):
        spec_id = str(case["caseId"])
        for phase in _RENDER_STAGE_CAPTURE_SURFACES:
            _adb_shell(
                serial,
                "mkdir -p "
                f"{shlex.quote(remote_capture_root + '/' + spec_id + '/' + phase)}",
            )

    local_xp3 = Path(__file__).resolve().parents[4] / _LOGO_TEST_XP3_REL
    if not local_xp3.exists():
        raise FileNotFoundError(
            f"reference oracle XP3 missing: {local_xp3}. Regenerate with "
            "tests/differential/oracle_runner/fixtures/"
            "build_logo_test_oracle.sh")
    remote_xp3 = f"{_REMOTE_APP_FILES_DIR}/logo_test_oracle.xp3"
    push_fixture(serial, local_xp3, remote_xp3)
    return remote_xp3, remote_capture_root


def _wait_for_remote_framebuffer_files(
    serial: str | None,
    remote_capture_root: str,
    expected_frames: int,
    *,
    timeout: float,
    settle_seconds: float = 2.0,
) -> None:
    deadline = time.time() + timeout
    stable_since: float | None = None
    last_count = -1
    quoted_root = shlex.quote(remote_capture_root)
    while time.time() < deadline:
        out = _adb_shell(
            serial,
            f"find {quoted_root} -type f -name 'frame_*.png' "
            "2>/dev/null | wc -l",
        )
        try:
            count = int(out.strip().splitlines()[-1])
        except (IndexError, ValueError):
            count = 0

        if count != last_count:
            stable_since = None
            last_count = count
        elif count >= expected_frames and stable_since is None:
            stable_since = time.time()

        if stable_since is not None and \
                time.time() - stable_since >= settle_seconds:
            return
        time.sleep(0.5)

    raise RuntimeError(
        f"framebuffer capture did not finish within {timeout:.1f}s "
        f"(remote files: {last_count}, expected: {expected_frames})"
    )


def _png_frame_number(path: Path) -> int | None:
    match = re.fullmatch(r"frame_(\d+)\.png", path.name)
    if not match:
        return None
    return int(match.group(1))


def _write_framebuffer_manifest(
    framebuffer_dir: Path,
    specs_by_id: dict[str, dict],
    remote_capture_root: str,
    capture_window: FrameCaptureWindow | None = None,
) -> Path:
    total_spec_frames = sum(int(spec["frames"]) for spec in specs_by_id.values())
    if capture_window is None:
        class _Args:
            record_only_frame = None
            record_first_frames = None
        capture_window = frame_capture_window_from_args(
            _Args(), total_spec_frames)
    cases: list[dict[str, Any]] = []
    total_frames = 0
    for case in captured_case_ranges(specs_by_id, SEGMENT_ORDER,
                                     capture_window):
        spec_id = str(case["caseId"])
        spec = case["spec"]
        expected = int(spec["frames"])
        case_dir = framebuffer_dir / spec_id
        images: list[dict[str, Any]] = []
        expected_frames = list(case["capturedLocalFrames"])
        expected_set = set(expected_frames)
        for frame in expected_frames:
            rel = Path(spec_id) / f"frame_{frame:04d}.png"
            path = framebuffer_dir / rel
            if not path.exists():
                raise RuntimeError(f"missing framebuffer PNG: {path}")
            images.append(png_manifest_entry(
                frame=frame,
                path=path,
                rel=rel,
            ))
        extras = [
            p for p in sorted(case_dir.glob("frame_*.png"))
            if _png_frame_number(p) not in expected_set
        ]
        if extras:
            raise RuntimeError(
                f"unexpected extra framebuffer PNG(s) for {spec_id}: "
                f"{[p.name for p in extras[:5]]}"
            )
        total_frames += len(images)
        cases.append({
            "caseId": spec_id,
            "mtnPath": spec.get("mtn_path"),
            "chara": spec.get("chara"),
            "label": spec.get("label"),
            "frames": expected,
            "fullFrameIdRange": case["fullFrameIdRange"],
            "capturedFrameIdRange": case["capturedFrameIdRange"],
            "capturedLocalFrames": expected_frames,
            "images": images,
        })

    manifest = {
        "schema": _FRAMEBUFFER_SCHEMA,
        "source": _FRAMEBUFFER_SOURCE,
        "captureSurface": _FRAMEBUFFER_CAPTURE_SURFACE,
        "generatedAt": datetime.now(timezone.utc)
        .replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "remoteCaptureRoot": remote_capture_root,
        "remoteCapturePhase": "post_draw",
        "localRoot": str(framebuffer_dir),
        "fixture": {
            "xp3": "logo_test_oracle.xp3",
            "window": {"width": 1920, "height": 1080},
            "deltaMs": 1000.0 / 60.0,
            "segmentOrder": list(SEGMENT_ORDER),
        },
        "summary": {
            "caseCount": len(cases),
            "frameCount": total_frames,
        },
        **capture_window.manifest_fields(),
        "cases": cases,
    }
    target = framebuffer_dir / "manifest.json"
    target.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True,
                   allow_nan=False) + "\n",
        encoding="utf-8",
    )
    return target


def _collect_framebuffer_capture(
    serial: str | None,
    specs_by_id: dict[str, dict],
    framebuffer_dir: Path,
    remote_capture_root: str,
    *,
    timeout: float,
    capture_window: FrameCaptureWindow | None = None,
) -> Path:
    total_frames = sum(int(s["frames"]) for s in specs_by_id.values())
    if capture_window is None:
        class _Args:
            record_only_frame = None
            record_first_frames = None
        capture_window = frame_capture_window_from_args(_Args(), total_frames)
    expected_frames = capture_window.count
    _wait_for_remote_framebuffer_files(
        serial, remote_capture_root, expected_frames,
        timeout=max(30.0, timeout))

    framebuffer_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = framebuffer_dir / "manifest.json"
    if manifest_path.exists():
        manifest_path.unlink()
    for spec_id in specs_by_id:
        old_case_dir = framebuffer_dir / str(spec_id)
        if old_case_dir.exists():
            import shutil

            shutil.rmtree(old_case_dir)
    for case in captured_case_ranges(specs_by_id, SEGMENT_ORDER,
                                     capture_window):
        spec_id = str(case["caseId"])
        local_case_dir = framebuffer_dir / spec_id
        local_case_dir.mkdir(parents=True, exist_ok=True)
        for frame in case["capturedLocalFrames"]:
            name = f"frame_{int(frame):04d}.png"
            _adb_pull(
                serial,
                f"{remote_capture_root}/{spec_id}/post_draw/{name}",
                local_case_dir / name,
            )

    manifest = _write_framebuffer_manifest(
        framebuffer_dir, specs_by_id, remote_capture_root, capture_window)
    _adb_shell(serial, f"rm -rf {shlex.quote(remote_capture_root)}")
    return manifest


def _collect_render_stage_capture(
    serial: str | None,
    specs_by_id: dict[str, dict],
    artifact_dir: Path,
    remote_capture_root: str,
    *,
    timeout: float,
    capture_window: FrameCaptureWindow | None = None,
) -> dict[str, Any]:
    total_frames = sum(int(s["frames"]) for s in specs_by_id.values())
    if capture_window is None:
        class _Args:
            record_only_frame = None
            record_first_frames = None
        capture_window = frame_capture_window_from_args(_Args(), total_frames)
    captured_cases = captured_case_ranges(specs_by_id, SEGMENT_ORDER,
                                          capture_window)
    expected_files = sum(
        len(case["capturedLocalFrames"]) + 1 for case in captured_cases)
    _wait_for_remote_framebuffer_files(
        serial, remote_capture_root, expected_files,
        timeout=max(30.0, timeout))

    artifact_dir.mkdir(parents=True, exist_ok=True)
    images_root = artifact_dir / "images"
    if images_root.exists():
        import shutil

        shutil.rmtree(images_root)
    images_root.mkdir(parents=True, exist_ok=True)
    cases: list[dict[str, Any]] = []
    total_images = 0

    for case in captured_cases:
        spec_id = str(case["caseId"])
        spec = case["spec"]

        local_case_dir = images_root / spec_id
        if local_case_dir.exists():
            import shutil

            shutil.rmtree(local_case_dir)
        _adb_pull(
            serial,
            f"{remote_capture_root}/{spec_id}",
            local_case_dir,
        )
        _normalize_pulled_render_case_dir(local_case_dir, spec_id)

        expected = int(spec["frames"])
        captured_local_frames = list(case["capturedLocalFrames"])
        phases: dict[str, list[dict[str, Any]]] = {}
        for phase in _RENDER_STAGE_CAPTURE_SURFACES:
            phase_dir = local_case_dir / phase
            if not phase_dir.is_dir():
                raise RuntimeError(
                    f"missing render stage image directory: {phase_dir}")
            expected_phase_frames = (
                [0] if phase == "initial" else captured_local_frames
            )
            expected_set = set(expected_phase_frames)
            images: list[dict[str, Any]] = []
            for frame in expected_phase_frames:
                rel = Path("images") / spec_id / phase / \
                    f"frame_{frame:04d}.png"
                path = artifact_dir / rel
                if not path.exists():
                    raise RuntimeError(
                        f"missing render stage PNG: {path}")
                images.append(png_manifest_entry(
                    frame=frame,
                    phase=phase,
                    path=path,
                    rel=rel,
                ))
            extras = [
                p for p in sorted(phase_dir.glob("frame_*.png"))
                if _png_frame_number(p) not in expected_set
            ]
            if extras:
                raise RuntimeError(
                    f"unexpected extra render stage PNG(s) for "
                    f"{spec_id}/{phase}: {[p.name for p in extras[:5]]}"
                )
            phases[phase] = images
            total_images += len(images)

        cases.append({
            "caseId": spec_id,
            "mtnPath": spec.get("mtn_path"),
            "chara": spec.get("chara"),
            "label": spec.get("label"),
            "frames": expected,
            "fullFrameIdRange": case["fullFrameIdRange"],
            "capturedFrameIdRange": case["capturedFrameIdRange"],
            "capturedLocalFrames": captured_local_frames,
            "phases": phases,
        })

    _adb_shell(serial, f"rm -rf {shlex.quote(remote_capture_root)}")
    return {
        "remoteCaptureRoot": remote_capture_root,
        "captureSurfaces": list(_RENDER_STAGE_CAPTURE_SURFACES),
        "cases": cases,
        "summary": {
            "caseCount": len(cases),
            "imageCount": total_images,
        },
        **capture_window.manifest_fields(),
    }


def _normalize_pulled_case_dir(local_case_dir: Path, case_id: str) -> None:
    if (local_case_dir / "frame_0000.png").exists():
        return
    nested = local_case_dir / case_id
    if not nested.is_dir() or not (nested / "frame_0000.png").exists():
        return

    import shutil

    for child in nested.iterdir():
        shutil.move(str(child), str(local_case_dir / child.name))
    nested.rmdir()


def _normalize_pulled_render_case_dir(
    local_case_dir: Path,
    case_id: str,
) -> None:
    if all((local_case_dir / phase).is_dir()
           for phase in _RENDER_STAGE_CAPTURE_SURFACES):
        return
    nested = local_case_dir / case_id
    if not nested.is_dir():
        return
    if not all((nested / phase).is_dir()
               for phase in _RENDER_STAGE_CAPTURE_SURFACES):
        return

    import shutil

    for child in nested.iterdir():
        target = local_case_dir / child.name
        if target.exists():
            shutil.rmtree(target)
        shutil.move(str(child), str(target))
    nested.rmdir()


# ------------------------------------------------------------ startup boot

_startup_triggered = False


def trigger_startup(engine, game_path_on_device: str) -> None:
    """Kick libkrkr2's deferred startup chain for `game_path_on_device`.

    Idempotent per AdbHarnessEngine session: cocos2d only accepts one
    `scheduleOnce` per "startup" key; a second invocation is a no-op on
    our side but would raise from startupFrom. We guard with
    `_startup_triggered`.

    After this returns, the cocos2d GL thread will (asynchronously)
    pick up the scheduled `doStartup`, run StartApplication →
    TVPInitScriptEngine → plugin loader → startup.tjs. Playback begins
    at some point 1–3s later. This function *returns immediately* once
    the scheduler accepts the path; use the Frida tracer's event count
    to determine when actual Motion.Player frames start arriving.
    """
    global _startup_triggered
    if _startup_triggered:
        return
    deadline = time.time() + 10.0
    while True:
        try:
            ok = engine.startup_from(game_path_on_device)
            break
        except RuntimeError as exc:
            if ("TVPMainScene::GetInstance returned null" not in str(exc)
                    or time.time() >= deadline):
                raise
            time.sleep(0.2)
    if not ok:
        raise RuntimeError(
            f"TVPMainScene::startupFrom({game_path_on_device!r}) returned "
            f"false — TVPCheckStartupPath rejected the path. Check the "
            f"path is under the app's scoped-storage dir and exists.")
    _startup_triggered = True


# ------------------------------------------------------------ oracle recording

def normalize_frame(frame: dict, index: int) -> dict:
    """Drop Frida-internal fields (player, frameId, layout), canonicalise
    to the oracle schema consumed by the port-side motionTrace hook."""
    layers = []
    for layer in frame.get("layers", []):
        layers.append({k: layer.get(k) for k in (
            LAYER_FIELDS_NUM + LAYER_FIELDS_INT
            + LAYER_FIELDS_BOOL + LAYER_FIELDS_STR
        )})
    return {"frame": index, "layers": layers}


def record_all_oracles(
    engine,
    specs: list[dict],
    *,
    serial: str | None = None,
    playback_timeout: float = 60.0,
    framebuffer_dir: Path | None = None,
) -> dict[str, list[dict]]:
    """Capture per-frame layer state for all specs in a single playback.

    `logo_test.xp3`'s startup.tjs plays yuzulogo then m2logo back-to-
    back; we can only trigger startupFrom once per session, so this
    adapter deliberately records every spec in one go rather than per-
    spec. Returns `{spec_id: frames_list}`.
    """
    from oracle_runner.frida_motion_tracer import (  # local import to
        FridaMotionTracer,                           # keep disk-only
        segment_by_player,                           # fast path free of
    )                                                # frida dep.

    specs_by_id = {s["id"]: s for s in specs}
    unknown = [sid for sid in specs_by_id if sid not in SEGMENT_ORDER]
    if unknown:
        raise ValueError(
            f"unknown motion_playback spec id(s): {unknown}. "
            f"Expected ids are fixed by logo_test.xp3's startup.tjs: "
            f"{SEGMENT_ORDER}.")

    framebuffer_remote_root: str | None = None
    if framebuffer_dir is not None:
        remote_game, framebuffer_remote_root = _prepare_framebuffer_capture(
            serial, specs_by_id, framebuffer_dir)
    else:
        remote_game = _ensure_logo_test_xp3_pushed(serial)

    ensure_oracle_renderer_software(
        serial, remote_game=remote_game, write_global=False)
    with FridaMotionTracer(engine, device_id=serial) as tracer:
        tracer.start_record()

        # tjs_init only to make sure the harness has a usable engine
        # pointer for subsequent calls; it does not touch cocos2d state.
        engine.tjs_init()
        trigger_startup(engine, remote_game)

        events = _wait_for_two_segments(
            tracer, specs_by_id, timeout=playback_timeout,
            stabilise_seconds=5.0 if framebuffer_dir is not None else 2.0)

        # Safety: ensure we actually stop before detaching.
        tracer.stop_record()
    segments = segment_by_player(events)
    # Filter out any "warmup" segments that fire before startup.tjs's
    # own Motion.Player instances exist (e.g. if libkrkr2 runs an
    # internal Motion.Player for an intro clip). The startup.tjs
    # playback guarantees two Motion.Player instances with ≥ 60 frames
    # each; anything shorter is noise.
    substantive = [s for s in segments if len(s["frames"]) >= 30]
    if len(substantive) < 2:
        raise RuntimeError(
            f"only {len(substantive)} substantive player segment(s) "
            f"captured (raw segments: {[len(s['frames']) for s in segments]}). "
            f"startup.tjs should produce two (yuzulogo + m2logo); check "
            f"logcat for Motion.Player creation or GL-surface failures.")

    results: dict[str, list[dict]] = {}
    for i, spec_id in enumerate(SEGMENT_ORDER):
        if spec_id not in specs_by_id:
            continue
        spec = specs_by_id[spec_id]
        wanted = int(spec["frames"])
        frames = substantive[i]["frames"]
        if len(frames) < wanted:
            raise RuntimeError(
                f"segment {i} ({spec_id}) only has {len(frames)} frames; "
                f"spec requires {wanted}. Increase playback_timeout or "
                f"check Motion.Player's per-motion frame count.")
        results[spec_id] = [
            normalize_frame(fr, fi) for fi, fr in enumerate(frames[:wanted])
        ]
    if framebuffer_dir is not None:
        assert framebuffer_remote_root is not None
        _collect_framebuffer_capture(
            serial, specs_by_id, framebuffer_dir, framebuffer_remote_root,
            timeout=playback_timeout)
    return results


def _wait_for_two_segments(
    tracer,
    specs_by_id: dict[str, dict],
    *,
    timeout: float,
    poll_interval: float = 0.4,
    stabilise_seconds: float = 2.0,
) -> list[dict]:
    """Poll until we have ≥ len(specs) substantive player segments AND
    the event count has been stable for `stabilise_seconds`. Returns the
    full event list.

    We can't peek at the buffer incrementally (rpc.exports round-trips
    freeze the whole array), so we only call stop_record() on the final
    return. Intermediate polls use `event_count` which is cheap (one
    integer over RPC).
    """
    from oracle_runner.frida_motion_tracer import segment_by_player

    needed_substantive = len(specs_by_id)
    needed_frames = sum(int(s["frames"]) for s in specs_by_id.values())

    deadline = time.time() + timeout
    stable_since: float | None = None
    last_count = -1
    while time.time() < deadline:
        count = tracer.event_count()
        if count != last_count:
            stable_since = None
            last_count = count
        elif count >= needed_frames and stable_since is None:
            stable_since = time.time()
        if stable_since is not None and \
                time.time() - stable_since >= stabilise_seconds:
            events = tracer.stop_record()
            segments = segment_by_player(events)
            substantive = [s for s in segments if len(s["frames"]) >= 30]
            if len(substantive) >= needed_substantive:
                return events
            # Not enough yet; resume recording and keep waiting.
            tracer.start_record()
            stable_since = None
        time.sleep(poll_interval)
    raise RuntimeError(
        f"motion playback did not stabilise within {timeout}s "
        f"(last event count: {last_count}, needed ≥ {needed_frames})")


# ------------------------------------------------------------ diff helpers

def _floats_close(a: float, b: float, *, rel: float, abs_: float) -> bool:
    if a == b:
        return True
    diff = abs(a - b)
    return diff <= max(abs_, rel * max(abs(a), abs(b)))


# Structural subset for focused tree/state debugging. This skips the
# accumulated transform fields while keeping the same string-diagnostic
# policy as the full diff.
STRUCTURAL_FIELDS_INT = ("index", "nodeType", "opacity", "blendMode")
STRUCTURAL_FIELDS_BOOL = ("visible", "active", "flipX", "flipY")
STRUCTURAL_FIELDS_STR: tuple[str, ...] = ()


def diff_frames(port_frames: list, oracle_frames: list, *,
                rel: float = 1e-6, abs_: float = 1e-6,
                structural_only: bool = False) -> list:
    if structural_only:
        int_fields = STRUCTURAL_FIELDS_INT
        bool_fields = STRUCTURAL_FIELDS_BOOL
        str_fields = STRUCTURAL_FIELDS_STR
        num_fields: tuple[str, ...] = ()
    else:
        int_fields = LAYER_FIELDS_INT
        bool_fields = LAYER_FIELDS_BOOL
        str_fields = COMPARE_FIELDS_STR
        num_fields = LAYER_FIELDS_NUM

    mismatches: list[dict[str, Any]] = []
    n = min(len(port_frames), len(oracle_frames))
    if len(port_frames) != len(oracle_frames):
        mismatches.append({
            "kind": "frame_count",
            "port": len(port_frames),
            "oracle": len(oracle_frames),
        })
    for f in range(n):
        pf = port_frames[f]
        of = oracle_frames[f]
        pl = pf.get("layers", [])
        ol = of.get("layers", [])
        if len(pl) != len(ol):
            mismatches.append({
                "kind": "layer_count",
                "frame": f,
                "port": len(pl),
                "oracle": len(ol),
            })
        for i in range(min(len(pl), len(ol))):
            pli = pl[i]
            oli = ol[i]
            for k in int_fields + bool_fields + str_fields:
                if pli.get(k) != oli.get(k):
                    mismatches.append({
                        "kind": "field",
                        "frame": f,
                        "layer_index": i,
                        "field": k,
                        "port": pli.get(k),
                        "oracle": oli.get(k),
                    })
            for k in num_fields:
                pv = pli.get(k)
                ov = oli.get(k)
                if pv is None or ov is None:
                    if pv != ov:
                        mismatches.append({
                            "kind": "field",
                            "frame": f,
                            "layer_index": i,
                            "field": k,
                            "port": pv,
                            "oracle": ov,
                        })
                    continue
                if not _floats_close(float(pv), float(ov),
                                     rel=rel, abs_=abs_):
                    mismatches.append({
                        "kind": "float",
                        "frame": f,
                        "layer_index": i,
                        "field": k,
                        "port": pv,
                        "oracle": ov,
                    })
    return mismatches


def run_case(engine, spec: dict, *, port_frames: list,
             oracle_frames: list | None = None,
             tracer=None,
             structural_only: bool = False) -> dict:
    """Compare port_frames against oracle_frames (live or cached)."""
    out: dict[str, Any] = {
        "case_id": spec["id"],
        "status": "ok",
        "mismatches": [],
    }
    if oracle_frames is None:
        out["status"] = "error"
        out["error"] = "no oracle frames provided"
        return out
    mismatches = diff_frames(port_frames, oracle_frames,
                             structural_only=structural_only)
    out["mismatches"] = mismatches
    if mismatches:
        out["status"] = "mismatch"
    return out
