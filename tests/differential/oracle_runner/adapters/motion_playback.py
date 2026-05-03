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
_FRAMEBUFFER_XP3_NAME = "logo_test_framebuffer_oracle.xp3"
_RENDER_STAGE_XP3_NAME = "logo_test_render_stage_oracle.xp3"
_RENDER_STAGE_CAPTURE_SURFACES = ("initial", "post_draw")


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


def _safe_remote_run_id(path: Path) -> str:
    stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", path.name).strip("._")
    if not stem:
        stem = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return stem[:80]


def _tjs_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def _xp3pack_path(repo_root: Path) -> Path:
    import os

    env_path = os.environ.get("XP3PACK")
    if env_path:
        return Path(env_path)
    return repo_root / "tools" / "bin" / "mac" / "rel" / "xp3pack"


def _build_framebuffer_startup_tjs(
    specs_by_id: dict[str, dict],
    remote_capture_root: str,
) -> str:
    lines = [
        "// Generated by tests/differential motion_playback framebuffer oracle.",
        "Plugins.link(\"motionplayer.dll\");",
        "Plugins.link(\"emoteplayer.dll\");",
        "",
        "var win = new Window();",
        "win.setInnerSize(1920, 1080);",
        "win.caption = \"logo_test framebuffer oracle\";",
        "",
        "var base = new Layer(win, null);",
        "base.setImageSize(1920, 1080);",
        "base.setSizeToImageSize();",
        "base.visible = true;",
        "base.face = dfAlpha;",
        "base.fillRect(0, 0, 1920, 1080, 0xFF000000);",
        "win.add(base);",
        "base.setAttentionPos(0, 0);",
        "base.focusable = true;",
        "base.focus();",
        "win.visible = true;",
        "",
        "var rm = new Motion.ResourceManager(win, 0);",
        f"var CAPTURE_ROOT = {_tjs_string(remote_capture_root)};",
        "var FIXED_DELTA_MS = 1000.0 / 60.0;",
        "",
        "function pad4(n)",
        "{",
        "    var s = \"\" + n;",
        "    while (s.length < 4) s = \"0\" + s;",
        "    return s;",
        "}",
        "",
        "function recordLogo(caseId, storage, chara, motion, frameCount)",
        "{",
        "    rm.load(storage);",
        "    var player = new Motion.Player(rm);",
        "    player.x = 960;",
        "    player.y = 540;",
        "    player.chara = chara;",
        "    player.motion = motion;",
        "    player.play(motion, Motion.PlayFlagForce);",
        "    for (var i = 0; i < frameCount; i++) {",
        "        player.progress(FIXED_DELTA_MS);",
        "        player.draw(base);",
        "        base.saveLayerImage(",
        "            CAPTURE_ROOT + \"/\" + caseId + \"/frame_\" + pad4(i) + \".png\",",
        "            \"png\");",
        "    }",
        "    invalidate player;",
        "}",
        "",
    ]

    for spec_id in SEGMENT_ORDER:
        spec = specs_by_id.get(spec_id)
        if spec is None:
            continue
        if spec_id == "m2logo":
            lines.append("base.fillRect(0, 0, 1920, 1080, 0xFFFFFFFF);")
        storage = Path(str(spec["mtn_path"])).name
        lines.append(
            "recordLogo("
            f"{_tjs_string(spec_id)}, "
            f"{_tjs_string(storage)}, "
            f"{_tjs_string(str(spec['chara']))}, "
            f"{_tjs_string(str(spec['label']))}, "
            f"{int(spec['frames'])});"
        )
    lines.append("base.fillRect(0, 0, 1920, 1080, 0xFF000000);")
    lines.append("")
    return "\n".join(lines)


def _build_framebuffer_capture_xp3(
    specs_by_id: dict[str, dict],
    remote_capture_root: str,
    work_dir: Path,
) -> Path:
    repo_root = Path(__file__).resolve().parents[4]
    mtn_dir = repo_root / "reference" / "xp3" / "logo_test"
    xp3pack = _xp3pack_path(repo_root)
    if not xp3pack.exists():
        raise FileNotFoundError(
            f"xp3pack not found at {xp3pack}. Set XP3PACK or build the "
            "native xp3pack tool first."
        )

    startup = work_dir / "startup.tjs"
    startup.write_text(
        _build_framebuffer_startup_tjs(specs_by_id, remote_capture_root),
        encoding="utf-8",
    )
    out = work_dir / _FRAMEBUFFER_XP3_NAME

    cmd = [
        str(xp3pack), "-o", str(out), "--map",
        f"startup.tjs={startup}",
    ]
    for spec_id in SEGMENT_ORDER:
        if spec_id not in specs_by_id:
            continue
        mtn_name = Path(str(specs_by_id[spec_id]["mtn_path"])).name
        mtn_path = mtn_dir / mtn_name
        if not mtn_path.exists():
            raise FileNotFoundError(f"motion fixture missing: {mtn_path}")
        cmd.append(f"{mtn_name}={mtn_path}")

    subprocess.run(cmd, check=True, capture_output=True, text=True)
    return out


def _build_render_stage_startup_tjs(
    specs_by_id: dict[str, dict],
    remote_capture_root: str,
) -> str:
    lines = [
        "// Generated by tests/differential motion_playback render stages.",
        "Plugins.link(\"motionplayer.dll\");",
        "Plugins.link(\"emoteplayer.dll\");",
        "",
        "var win = new Window();",
        "win.setInnerSize(1920, 1080);",
        "win.caption = \"logo_test render stage oracle\";",
        "",
        "var base = new Layer(win, null);",
        "base.setImageSize(1920, 1080);",
        "base.setSizeToImageSize();",
        "base.visible = true;",
        "base.face = dfAlpha;",
        "base.fillRect(0, 0, 1920, 1080, 0xFF000000);",
        "win.add(base);",
        "base.setAttentionPos(0, 0);",
        "base.focusable = true;",
        "base.focus();",
        "win.visible = true;",
        "",
        "var rm = new Motion.ResourceManager(win, 0);",
        f"var CAPTURE_ROOT = {_tjs_string(remote_capture_root)};",
        "var FIXED_DELTA_MS = 1000.0 / 60.0;",
        "",
        "function pad4(n)",
        "{",
        "    var s = \"\" + n;",
        "    while (s.length < 4) s = \"0\" + s;",
        "    return s;",
        "}",
        "",
        "function savePhase(caseId, phase, frame)",
        "{",
        "    base.saveLayerImage(",
        "        CAPTURE_ROOT + \"/\" + caseId + \"/\" + phase +",
        "            \"/frame_\" + pad4(frame) + \".png\",",
        "        \"png\");",
        "}",
        "",
        "function recordLogo(caseId, storage, chara, motion, frameCount)",
        "{",
        "    rm.load(storage);",
        "    var player = new Motion.Player(rm);",
        "    player.x = 960;",
        "    player.y = 540;",
        "    player.chara = chara;",
        "    player.motion = motion;",
        "    player.play(motion, Motion.PlayFlagForce);",
        "    savePhase(caseId, \"initial\", 0);",
        "    for (var i = 0; i < frameCount; i++) {",
        "        player.progress(FIXED_DELTA_MS);",
        "        player.draw(base);",
        "        savePhase(caseId, \"post_draw\", i);",
        "    }",
        "    invalidate player;",
        "}",
        "",
    ]

    for spec_id in SEGMENT_ORDER:
        spec = specs_by_id.get(spec_id)
        if spec is None:
            continue
        if spec_id == "m2logo":
            lines.append("base.fillRect(0, 0, 1920, 1080, 0xFFFFFFFF);")
        storage = Path(str(spec["mtn_path"])).name
        lines.append(
            "recordLogo("
            f"{_tjs_string(spec_id)}, "
            f"{_tjs_string(storage)}, "
            f"{_tjs_string(str(spec['chara']))}, "
            f"{_tjs_string(str(spec['label']))}, "
            f"{int(spec['frames'])});"
        )
    lines.append("base.fillRect(0, 0, 1920, 1080, 0xFF000000);")
    lines.append("")
    return "\n".join(lines)


def _build_render_stage_capture_xp3(
    specs_by_id: dict[str, dict],
    remote_capture_root: str,
    work_dir: Path,
) -> Path:
    repo_root = Path(__file__).resolve().parents[4]
    mtn_dir = repo_root / "reference" / "xp3" / "logo_test"
    xp3pack = _xp3pack_path(repo_root)
    if not xp3pack.exists():
        raise FileNotFoundError(
            f"xp3pack not found at {xp3pack}. Set XP3PACK or build the "
            "native xp3pack tool first."
        )

    startup = work_dir / "startup.tjs"
    startup.write_text(
        _build_render_stage_startup_tjs(specs_by_id, remote_capture_root),
        encoding="utf-8",
    )
    out = work_dir / _RENDER_STAGE_XP3_NAME

    cmd = [
        str(xp3pack), "-o", str(out), "--map",
        f"startup.tjs={startup}",
    ]
    for spec_id in SEGMENT_ORDER:
        if spec_id not in specs_by_id:
            continue
        mtn_name = Path(str(specs_by_id[spec_id]["mtn_path"])).name
        mtn_path = mtn_dir / mtn_name
        if not mtn_path.exists():
            raise FileNotFoundError(f"motion fixture missing: {mtn_path}")
        cmd.append(f"{mtn_name}={mtn_path}")

    subprocess.run(cmd, check=True, capture_output=True, text=True)
    return out


def _prepare_framebuffer_capture(
    serial: str | None,
    specs_by_id: dict[str, dict],
    framebuffer_dir: Path,
    work_dir: Path,
) -> tuple[str, str]:
    remote_capture_root = (
        f"{_REMOTE_APP_FILES_DIR}/motion_framebuffer_capture/"
        f"{_safe_remote_run_id(framebuffer_dir)}"
    )
    quoted_root = shlex.quote(remote_capture_root)
    _adb_shell(serial, f"rm -rf {quoted_root} && mkdir -p {quoted_root}")
    for spec_id in specs_by_id:
        _adb_shell(
            serial,
            f"mkdir -p {shlex.quote(remote_capture_root + '/' + spec_id)}",
        )

    local_xp3 = _build_framebuffer_capture_xp3(
        specs_by_id, remote_capture_root, work_dir)
    remote_xp3 = f"{_REMOTE_APP_FILES_DIR}/{_FRAMEBUFFER_XP3_NAME}"
    push_fixture(serial, local_xp3, remote_xp3)
    return remote_xp3, remote_capture_root


def _prepare_render_stage_capture(
    serial: str | None,
    specs_by_id: dict[str, dict],
    artifact_dir: Path,
    work_dir: Path,
) -> tuple[str, str]:
    remote_capture_root = (
        f"{_REMOTE_APP_FILES_DIR}/motion_render_stage_capture/"
        f"{_safe_remote_run_id(artifact_dir)}"
    )
    quoted_root = shlex.quote(remote_capture_root)
    _adb_shell(serial, f"rm -rf {quoted_root} && mkdir -p {quoted_root}")
    for spec_id in specs_by_id:
        for phase in _RENDER_STAGE_CAPTURE_SURFACES:
            _adb_shell(
                serial,
                "mkdir -p "
                f"{shlex.quote(remote_capture_root + '/' + spec_id + '/' + phase)}",
            )

    local_xp3 = _build_render_stage_capture_xp3(
        specs_by_id, remote_capture_root, work_dir)
    remote_xp3 = f"{_REMOTE_APP_FILES_DIR}/{_RENDER_STAGE_XP3_NAME}"
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


def _write_framebuffer_manifest(
    framebuffer_dir: Path,
    specs_by_id: dict[str, dict],
    remote_capture_root: str,
) -> Path:
    cases: list[dict[str, Any]] = []
    total_frames = 0
    for spec_id in SEGMENT_ORDER:
        spec = specs_by_id.get(spec_id)
        if spec is None:
            continue
        expected = int(spec["frames"])
        case_dir = framebuffer_dir / spec_id
        images: list[dict[str, Any]] = []
        for frame in range(expected):
            rel = Path(spec_id) / f"frame_{frame:04d}.png"
            path = framebuffer_dir / rel
            if not path.exists():
                raise RuntimeError(f"missing framebuffer PNG: {path}")
            images.append(png_manifest_entry(
                frame=frame,
                path=path,
                rel=rel,
            ))
        extras = sorted(case_dir.glob("frame_*.png"))[expected:]
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
            "images": images,
        })

    manifest = {
        "schema": _FRAMEBUFFER_SCHEMA,
        "source": _FRAMEBUFFER_SOURCE,
        "captureSurface": _FRAMEBUFFER_CAPTURE_SURFACE,
        "generatedAt": datetime.now(timezone.utc)
        .replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "remoteCaptureRoot": remote_capture_root,
        "localRoot": str(framebuffer_dir),
        "fixture": {
            "xp3": _FRAMEBUFFER_XP3_NAME,
            "window": {"width": 1920, "height": 1080},
            "deltaMs": 1000.0 / 60.0,
            "segmentOrder": list(SEGMENT_ORDER),
        },
        "summary": {
            "caseCount": len(cases),
            "frameCount": total_frames,
        },
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
) -> Path:
    expected_frames = sum(int(s["frames"]) for s in specs_by_id.values())
    _wait_for_remote_framebuffer_files(
        serial, remote_capture_root, expected_frames,
        timeout=max(30.0, timeout))

    framebuffer_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = framebuffer_dir / "manifest.json"
    if manifest_path.exists():
        manifest_path.unlink()
    for spec_id in specs_by_id:
        local_case_dir = framebuffer_dir / spec_id
        if local_case_dir.exists():
            import shutil

            shutil.rmtree(local_case_dir)
        _adb_pull(
            serial,
            f"{remote_capture_root}/{spec_id}",
            local_case_dir,
        )
        _normalize_pulled_case_dir(local_case_dir, spec_id)

    manifest = _write_framebuffer_manifest(
        framebuffer_dir, specs_by_id, remote_capture_root)
    _adb_shell(serial, f"rm -rf {shlex.quote(remote_capture_root)}")
    return manifest


def _collect_render_stage_capture(
    serial: str | None,
    specs_by_id: dict[str, dict],
    artifact_dir: Path,
    remote_capture_root: str,
    *,
    timeout: float,
) -> dict[str, Any]:
    expected_files = sum(int(s["frames"]) + 1 for s in specs_by_id.values())
    _wait_for_remote_framebuffer_files(
        serial, remote_capture_root, expected_files,
        timeout=max(30.0, timeout))

    artifact_dir.mkdir(parents=True, exist_ok=True)
    images_root = artifact_dir / "images"
    images_root.mkdir(parents=True, exist_ok=True)
    cases: list[dict[str, Any]] = []
    total_images = 0

    for spec_id in SEGMENT_ORDER:
        spec = specs_by_id.get(spec_id)
        if spec is None:
            continue

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
        phases: dict[str, list[dict[str, Any]]] = {}
        for phase in _RENDER_STAGE_CAPTURE_SURFACES:
            phase_dir = local_case_dir / phase
            if not phase_dir.is_dir():
                raise RuntimeError(
                    f"missing render stage image directory: {phase_dir}")
            expected_phase_frames = 1 if phase == "initial" else expected
            images: list[dict[str, Any]] = []
            for frame in range(expected_phase_frames):
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
            extras = sorted(phase_dir.glob("frame_*.png"))[expected_phase_frames:]
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
    temp_dir: tempfile.TemporaryDirectory[str] | None = None
    if framebuffer_dir is not None:
        temp_dir = tempfile.TemporaryDirectory(
            prefix="krkr2-motion-framebuffer-xp3-")
        remote_game, framebuffer_remote_root = _prepare_framebuffer_capture(
            serial, specs_by_id, framebuffer_dir, Path(temp_dir.name))
    else:
        remote_game = _ensure_logo_test_xp3_pushed(serial)

    try:
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
    finally:
        if temp_dir is not None:
            temp_dir.cleanup()

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
