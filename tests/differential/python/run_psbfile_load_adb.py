#!/usr/bin/env python3
"""Run existing PSB and MDF files through Android libkrkr2's PSBFile loader."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from oracle_runner.adb_engine import (  # noqa: E402
    ENV_REMOTE_DIR,
    AdbHarnessEngine,
)
from oracle_runner.adapters import psbfile_load as adapter  # noqa: E402


REPO_ROOT = Path(__file__).resolve().parents[3]
MEDIA_FIRST_INPUT = REPO_ROOT / "tests/test_files/emote/ezsave.pimg"
MEDIA_REPLACEMENT_INPUT = (
    REPO_ROOT / "tests/test_files/emote/e-mote3.0バニラパジャマa.psb"
)
MEDIA_STARTUP_XP3 = (
    REPO_ROOT / "reference/xp3/caution_minimal/caution_minimal.xp3"
)
MEDIA_FIRST_CONTAINER = "psb-media-ezsave.pimg"
MEDIA_REPLACEMENT_CONTAINER = "psb-media-encrypted.psb"
MEDIA_RESOURCE_NAME = "2036.tlg"
MEDIA_RESOURCE_SIZE = 48265
MEDIA_STARTUP_REMOTE_NAME = "psbfile-oracle-bootstrap.xp3"
MEDIA_DICTIONARY_INPUT = (
    REPO_ROOT
    / "reference/xp3/caution_test/DRACU-RIOT/data/motion/autoskip.psb"
)
MEDIA_DICTIONARY_SHA256 = (
    "131b436405c0aa8cd137a496c98fb77a77da95ca29e8af4597da1f7a42fd4a5d"
)
MEDIA_DICTIONARY_CONTAINER = "psb-media-autoskip.psb"
MEDIA_DICTIONARY_PATH = "source/main/icon"
MEDIA_DICTIONARY_KEYS = ("arrow", "auto", "skip")
INTEGER_BOUNDARY_INPUT = REPO_ROOT / "reference/xp3/logo_test/m2logo.mtn"
INTEGER_BOUNDARY_SHA256 = (
    "4382de8283cc0782fd269b16d3157bf3a9ec28916440f9192690eb178c0c18fe"
)
INTEGER_BOUNDARY_REMOTE_NAME = "psbfile-tag09-m2logo.mtn"
INTEGER_BOUNDARY_NODE_OFFSET = 0x36F8
INTEGER_BOUNDARY_NODE_BYTES = bytes.fromhex("09 00 00 00 ff 00")
INTEGER_BOUNDARY_VALUE = 0xFF000000
INTEGER_BOUNDARY_EXPRESSION = (
    'oracle_psb_tag09_file.root["object"]["m2cheeseware_logo"]'
    '["motion"]["back_white"]["layer"][1]["children"][0]'
    '["children"][3]["children"][1]["frameList"][3]'
    '["content"]["color"]'
)


def _adb_prefix(engine) -> list[str]:
    adb = [engine.adb]
    if engine.serial:
        adb += ["-s", engine.serial]
    return adb


def _app_files_dir(engine) -> str:
    return f"/data/user/0/{engine.apk_package}/files"


def _push_input(engine, input_path: Path, remote_path: str) -> None:
    adb = _adb_prefix(engine)
    subprocess.run(
        adb + ["push", str(input_path), remote_path],
        check=True, capture_output=True,
    )
    subprocess.run(
        adb + ["shell", "chmod", "644", remote_path],
        check=True, capture_output=True,
    )


def _push_app_file(engine, input_path: Path, remote_name: str) -> str:
    """Install an oracle input where the APK can read it under SELinux."""
    if not remote_name or "/" in remote_name or remote_name in {".", ".."}:
        raise ValueError(f"invalid Android app-private file name: {remote_name!r}")
    adb = _adb_prefix(engine)
    app_dir = _app_files_dir(engine)
    remote_path = f"{app_dir}/{remote_name}"
    staging_path = (
        f"/data/local/tmp/.krkr2-oracle-{os.getpid()}-{remote_name}")

    try:
        subprocess.run(
            adb + ["push", str(input_path), staging_path],
            check=True, capture_output=True,
        )
        subprocess.run(
            adb + ["shell", "mkdir", "-p", app_dir],
            check=True, capture_output=True,
        )
        subprocess.run(
            adb + ["shell", "cp", staging_path, remote_path],
            check=True, capture_output=True,
        )
        owner = subprocess.run(
            adb + ["shell", "stat", "-c", "%u:%g", app_dir],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        if not owner:
            raise RuntimeError(
                f"could not determine Android app owner for {app_dir}")
        subprocess.run(
            adb + ["shell", "chown", owner, remote_path],
            check=True, capture_output=True,
        )
        subprocess.run(
            adb + ["shell", "chmod", "644", remote_path],
            check=True, capture_output=True,
        )
    finally:
        subprocess.run(
            adb + ["shell", "rm", "-f", staging_path],
            check=False, capture_output=True,
        )
    return remote_path


def _push_readable_input(
    engine, input_path: Path, remote_dir: str, remote_name: str,
) -> str:
    normalized_dir = remote_dir.rstrip("/") or "/"
    if normalized_dir == _app_files_dir(engine):
        return _push_app_file(engine, input_path, remote_name)
    subprocess.run(
        _adb_prefix(engine) + ["shell", "mkdir", "-p", normalized_dir],
        check=True, capture_output=True,
    )
    remote_path = (
        f"/{remote_name}" if normalized_dir == "/"
        else f"{normalized_dir}/{remote_name}"
    )
    _push_input(engine, input_path, remote_path)
    return remote_path


def _trigger_startup(engine, remote_xp3: str, timeout: float = 15.0) -> None:
    """Schedule the real Android startup chain that creates Full TJS."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            accepted = engine.startup_from(remote_xp3)
            break
        except RuntimeError as exc:
            if ("TVPMainScene::GetInstance returned null" not in str(exc)
                    or time.monotonic() >= deadline):
                raise
            time.sleep(0.2)
    if not accepted:
        raise RuntimeError(
            f"TVPMainScene::startupFrom({remote_xp3!r}) returned false")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input", action="append", default=[], type=Path,
        help="existing PSB\\0 or mdf\\0 file; repeat to run more than one",
    )
    parser.add_argument("--serial", default=None)
    parser.add_argument("--remote-dir", default=None)
    parser.add_argument(
        "--trace", action="store_true",
        help="attach Frida and include the native PSB load call sequence",
    )
    parser.add_argument(
        "--storage", action="store_true",
        help="also push each input and exercise PSBFile's storage entry",
    )
    parser.add_argument(
        "--decrypt-seed", type=lambda value: int(value, 0), default=None,
        help="also apply Android's Emote PSB xorshift filter to raw PSB input",
    )
    parser.add_argument(
        "--media-lifecycle", action="store_true",
        help=(
            "exercise PSBMedia replacement and borrowed-stream destruction "
            "with the repository's two tracked Emote PSB files"
        ),
    )
    parser.add_argument(
        "--media-dictionary", action="store_true",
        help=(
            "exercise PSBMedia dictionary listing with the existing "
            "autoskip.psb reference input"
        ),
    )
    parser.add_argument(
        "--integer-boundary", action="store_true",
        help=(
            "exercise the natural PSB tag-0x09 value in m2logo.mtn through "
            "both PSBValueDispatch and raw scalar getters"
        ),
    )
    parser.add_argument(
        "--startup-xp3", type=Path, default=MEDIA_STARTUP_XP3,
        help=(
            "existing self-contained XP3 used to initialize Full TJS before "
            "the TJS-backed PSBFile/PSBMedia modes (default: "
            "reference/xp3/caution_minimal/"
            "caution_minimal.xp3)"
        ),
    )
    args = parser.parse_args()
    media_mode = args.media_lifecycle or args.media_dictionary
    tjs_mode = media_mode or args.integer_boundary
    if not args.input and not tjs_mode:
        parser.error(
            "provide --input, --integer-boundary, --media-lifecycle, and/or "
            "--media-dictionary")
    if not args.input and (args.storage or args.decrypt_seed is not None):
        parser.error("--storage and --decrypt-seed require at least one --input")
    if tjs_mode and not args.startup_xp3.is_file():
        parser.error(
            f"TJS-backed oracle startup XP3 does not exist: "
            f"{args.startup_xp3}")
    if args.integer_boundary:
        if not INTEGER_BOUNDARY_INPUT.is_file():
            parser.error(
                f"--integer-boundary input does not exist: "
                f"{INTEGER_BOUNDARY_INPUT}")
        integer_digest = hashlib.sha256(
            INTEGER_BOUNDARY_INPUT.read_bytes()).hexdigest()
        if integer_digest != INTEGER_BOUNDARY_SHA256:
            parser.error(
                "--integer-boundary input digest changed: "
                f"expected {INTEGER_BOUNDARY_SHA256}, got {integer_digest}")
    if args.media_dictionary:
        if not MEDIA_DICTIONARY_INPUT.is_file():
            parser.error(
                f"--media-dictionary input does not exist: "
                f"{MEDIA_DICTIONARY_INPUT}")
        dictionary_digest = hashlib.sha256(
            MEDIA_DICTIONARY_INPUT.read_bytes()).hexdigest()
        if dictionary_digest != MEDIA_DICTIONARY_SHA256:
            parser.error(
                "--media-dictionary input digest changed: "
                f"expected {MEDIA_DICTIONARY_SHA256}, got {dictionary_digest}")

    failed = False
    tracer = None
    with AdbHarnessEngine(
        serial=args.serial, remote_dir=args.remote_dir) as engine:
        if args.trace:
            from oracle_runner.frida_tracer import FridaTracerEngine
            from oracle_runner.trace_targets import (
                PSBFILE_INTEGER_TARGETS,
                PSBFILE_LOAD_TARGETS,
                PSBFILE_MEDIA_TARGETS,
            )

            targets = list(dict.fromkeys(
                PSBFILE_LOAD_TARGETS
                + (PSBFILE_INTEGER_TARGETS
                   if args.integer_boundary else [])
                + (PSBFILE_MEDIA_TARGETS if media_mode else [])
            ))
            tracer = FridaTracerEngine(engine, targets)
            tracer.attach()
        try:
            if args.remote_dir is not None or os.environ.get(ENV_REMOTE_DIR):
                readable_remote_dir = engine.remote_dir.rstrip("/") or "/"
            else:
                readable_remote_dir = _app_files_dir(engine)
            for index, input_path in enumerate(args.input):
                cases = [("octet", None)]
                if args.storage:
                    remote_path = _push_readable_input(
                        engine,
                        input_path,
                        readable_remote_dir,
                        f"psbfile-load-oracle-{index}.bin",
                    )
                    cases.append(("storage", remote_path))
                if args.decrypt_seed is not None:
                    cases.append(("decrypt", None))

                for entry, remote_path in cases:
                    trace = None
                    try:
                        if tracer is not None:
                            tracer.start_case()
                        if entry == "storage":
                            result = adapter.run_storage_case(
                                engine, input_path, remote_path)
                        elif entry == "decrypt":
                            result = adapter.run_decrypt_case(
                                engine, input_path, args.decrypt_seed)
                        else:
                            result = adapter.run_case(engine, input_path)
                    except Exception as exc:
                        failed = True
                        result = {
                            "input": str(input_path),
                            "entry": entry,
                            "status": "error",
                            "error": repr(exc),
                        }
                    finally:
                        if tracer is not None:
                            trace = tracer.stop_case()
                    if result["status"] != "ok":
                        failed = True
                    if trace is not None:
                        result["trace"] = trace
                    result["runner"] = "android-adb-oracle"
                    print(json.dumps(result, ensure_ascii=True))

            tjs_startup_error = None
            if tjs_mode:
                try:
                    remote_startup = _push_app_file(
                        engine, args.startup_xp3, MEDIA_STARTUP_REMOTE_NAME)
                    _trigger_startup(engine, remote_startup)
                except Exception as exc:
                    tjs_startup_error = exc

            if args.integer_boundary:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    remote_integer_input = _push_readable_input(
                        engine,
                        INTEGER_BOUNDARY_INPUT,
                        readable_remote_dir,
                        INTEGER_BOUNDARY_REMOTE_NAME,
                    )
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_integer_boundary_case(
                        engine,
                        input_path=INTEGER_BOUNDARY_INPUT,
                        remote_path=remote_integer_input,
                        node_offset=INTEGER_BOUNDARY_NODE_OFFSET,
                        expected_node_bytes=INTEGER_BOUNDARY_NODE_BYTES,
                        tjs_expression=INTEGER_BOUNDARY_EXPRESSION,
                        expected_value=INTEGER_BOUNDARY_VALUE,
                    )
                    result["input_sha256"] = INTEGER_BOUNDARY_SHA256
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(INTEGER_BOUNDARY_INPUT),
                        "entry": "tag09-integer-boundary",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

            if args.media_lifecycle:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    _push_readable_input(
                        engine,
                        MEDIA_FIRST_INPUT,
                        readable_remote_dir,
                        MEDIA_FIRST_CONTAINER,
                    )
                    _push_readable_input(
                        engine,
                        MEDIA_REPLACEMENT_INPUT,
                        readable_remote_dir,
                        MEDIA_REPLACEMENT_CONTAINER,
                    )
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_media_lifecycle_case(
                        engine,
                        first_input=MEDIA_FIRST_INPUT,
                        replacement_input=MEDIA_REPLACEMENT_INPUT,
                        remote_dir=readable_remote_dir,
                        first_container=MEDIA_FIRST_CONTAINER,
                        replacement_container=MEDIA_REPLACEMENT_CONTAINER,
                        resource_name=MEDIA_RESOURCE_NAME,
                        expected_size=MEDIA_RESOURCE_SIZE,
                    )
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(MEDIA_FIRST_INPUT),
                        "replacement_input": str(MEDIA_REPLACEMENT_INPUT),
                        "entry": "media-lifecycle",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))

            if args.media_dictionary:
                trace = None
                trace_started = False
                try:
                    if tjs_startup_error is not None:
                        raise RuntimeError(
                            f"Full TJS startup failed: {tjs_startup_error!r}"
                        ) from tjs_startup_error
                    _push_readable_input(
                        engine,
                        MEDIA_DICTIONARY_INPUT,
                        readable_remote_dir,
                        MEDIA_DICTIONARY_CONTAINER,
                    )
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_media_dictionary_case(
                        engine,
                        input_path=MEDIA_DICTIONARY_INPUT,
                        remote_dir=readable_remote_dir,
                        container=MEDIA_DICTIONARY_CONTAINER,
                        dictionary_path=MEDIA_DICTIONARY_PATH,
                        expected_keys=MEDIA_DICTIONARY_KEYS,
                    )
                    result["input_sha256"] = MEDIA_DICTIONARY_SHA256
                    result["startup_xp3"] = str(args.startup_xp3)
                except Exception as exc:
                    failed = True
                    result = {
                        "input": str(MEDIA_DICTIONARY_INPUT),
                        "entry": "media-dictionary-list",
                        "status": "error",
                        "error": repr(exc),
                    }
                finally:
                    if tracer is not None and trace_started:
                        trace = tracer.stop_case()
                if result["status"] != "ok":
                    failed = True
                if trace is not None:
                    result["trace"] = trace
                result["runner"] = "android-adb-oracle"
                print(json.dumps(result, ensure_ascii=True))
        finally:
            if tracer is not None:
                tracer.detach()

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
