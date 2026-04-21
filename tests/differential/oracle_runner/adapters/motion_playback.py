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

  * Disk oracle (`run_case`): compare port CLI output against a
    checked-in golden JSON. No engine required.

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
import struct
import subprocess
import time
from pathlib import Path
from typing import Any


# libkrkr2 offsets (relative to load base) resolved from IDA by
# mangled-name strings present in the dynamic string table:
#   _ZN12TVPMainScene11GetInstanceEv            → 0xA9D4D4
#   _ZN12TVPMainScene11startupFromERKSs         → 0xA9F954
# The second arg's 'Ss' abbreviation confirms libkrkr2 was built with
# gnustl (libstdc++ old ABI), not libc++. That dictates the std::string
# layout used by _construct_gnustl_string below.
OFFSET_TVPMAINSCENE_GETINSTANCE = 0xA9D4D4
OFFSET_TVPMAINSCENE_STARTUPFROM = 0xA9F954


# Schema fields, kept in sync with tests/differential/port_runners/motion_playback_port.cpp.
LAYER_FIELDS_NUM = (
    "posX", "posY", "posZ", "angleDeg",
    "scaleX", "scaleY", "slantX", "slantY",
)
LAYER_FIELDS_INT = ("opacity", "blendMode", "nodeType", "index")
LAYER_FIELDS_BOOL = ("visible", "active", "flipX", "flipY")
LAYER_FIELDS_STR = ("label", "currentImage")


# Order that logo_test.xp3's startup.tjs plays motions. We partition the
# Frida trace by player pointer; the first segment is yuzulogo, second
# is m2logo. Adapter-level contract: spec ids must be one of these.
SEGMENT_ORDER: tuple[str, ...] = ("yuzulogo", "m2logo")


# Deterministic oracle-recording xp3. Its startup.tjs runs fixed-step
# `player.progress(1000/60)` loops (241 frames for yuzulogo, 91 for
# m2logo) matching the host port CLI, instead of logo_test.xp3's real-
# time variable-step doFrame. Sources live in the reference submodule
# (reference/xp3/logo_test_oracle/startup.tjs + the shared mtn files in
# reference/xp3/logo_test/). Regenerate via
# `tests/differential/oracle_runner/fixtures/build_logo_test_oracle.sh`
# whenever the spec frame counts change.
_LOGO_TEST_XP3_REL = "reference/xp3/logo_test_oracle.xp3"


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
    remote_dir = "/sdcard/Android/data/org.github.krkr2/files"
    _adb_shell(serial, f"mkdir -p {remote_dir}")
    remote_path = f"{remote_dir}/logo_test_oracle.xp3"
    push_fixture(serial, local, remote_path)
    return remote_path


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
    scene = engine.call(
        engine.offset(OFFSET_TVPMAINSCENE_GETINSTANCE), ret="ptr")
    if not scene:
        raise RuntimeError(
            "TVPMainScene::GetInstance returned null — cocos2d hasn't "
            "finished applicationDidFinishLaunching yet?")

    # gnustl std::string layout. Data block:
    #   [cap u64][len u64][refcnt i32][pad i32][chars...]['\0']
    # The std::string object itself is an 8-byte pointer to &chars[0].
    path_bytes = game_path_on_device.encode("utf-8")
    header = struct.pack("<qqii", len(path_bytes), len(path_bytes), 0, 0)
    data_blk = engine.heap.write(header + path_bytes + b"\x00")
    string_obj = engine.heap.write(struct.pack("<Q", data_blk + 24))

    ok = engine.call(
        engine.offset(OFFSET_TVPMAINSCENE_STARTUPFROM),
        ints=(scene, string_obj), ret="bool")
    if not ok:
        raise RuntimeError(
            f"TVPMainScene::startupFrom({game_path_on_device!r}) returned "
            f"false — TVPCheckStartupPath rejected the path. Check the "
            f"path is under the app's scoped-storage dir and exists.")
    _startup_triggered = True


# ------------------------------------------------------------ oracle recording

def _normalize_frame(frame: dict, index: int) -> dict:
    """Drop Frida-internal fields (player, frameId, layout), canonicalise
    to the oracle schema consumed by motion_playback_port.cpp."""
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

    remote_game = _ensure_logo_test_xp3_pushed(serial)

    with FridaMotionTracer(engine, device_id=serial) as tracer:
        tracer.start_record()

        # tjs_init only to make sure the harness has a usable engine
        # pointer for subsequent calls; it does not touch cocos2d state.
        engine.tjs_init()
        trigger_startup(engine, remote_game)

        events = _wait_for_two_segments(
            tracer, specs_by_id, timeout=playback_timeout)

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
            _normalize_frame(fr, fi) for fi, fr in enumerate(frames[:wanted])
        ]
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


# Structural subset that the port CLI fills in accurately without
# needing runUpdatePassForOracle() (which currently segfaults headless).
# These come from node identity / tree shape / clip flags set by
# buildNodeTree / playTimeline, not from per-frame updateLayers.
STRUCTURAL_FIELDS_INT = ("index", "nodeType", "opacity", "blendMode")
STRUCTURAL_FIELDS_BOOL = ("visible", "active", "flipX", "flipY")
STRUCTURAL_FIELDS_STR = ("label",)


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
        str_fields = LAYER_FIELDS_STR
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
