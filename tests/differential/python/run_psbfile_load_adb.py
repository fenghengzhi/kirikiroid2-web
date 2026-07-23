#!/usr/bin/env python3
"""Run existing PSB and MDF files through Android libkrkr2's PSBFile loader."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from oracle_runner.adb_engine import AdbHarnessEngine  # noqa: E402
from oracle_runner.adapters import psbfile_load as adapter  # noqa: E402


REPO_ROOT = Path(__file__).resolve().parents[3]
MEDIA_FIRST_INPUT = REPO_ROOT / "tests/test_files/emote/ezsave.pimg"
MEDIA_REPLACEMENT_INPUT = (
    REPO_ROOT / "tests/test_files/emote/e-mote3.0バニラパジャマa.psb"
)
MEDIA_FIRST_CONTAINER = "psb-media-ezsave.pimg"
MEDIA_REPLACEMENT_CONTAINER = "psb-media-encrypted.psb"
MEDIA_RESOURCE_NAME = "2036.tlg"
MEDIA_RESOURCE_SIZE = 48265


def _push_input(engine, input_path: Path, remote_path: str) -> None:
    adb = [engine.adb]
    if engine.serial:
        adb += ["-s", engine.serial]
    subprocess.run(
        adb + ["push", str(input_path), remote_path],
        check=True, capture_output=True,
    )
    subprocess.run(
        adb + ["shell", "chmod", "644", remote_path],
        check=True, capture_output=True,
    )


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
    args = parser.parse_args()
    if not args.input and not args.media_lifecycle:
        parser.error("provide --input and/or --media-lifecycle")
    if not args.input and (args.storage or args.decrypt_seed is not None):
        parser.error("--storage and --decrypt-seed require at least one --input")

    failed = False
    tracer = None
    with AdbHarnessEngine(
        serial=args.serial, remote_dir=args.remote_dir) as engine:
        if args.trace:
            from oracle_runner.frida_tracer import FridaTracerEngine
            from oracle_runner.trace_targets import (
                PSBFILE_LOAD_TARGETS,
                PSBFILE_MEDIA_TARGETS,
            )

            targets = (
                PSBFILE_MEDIA_TARGETS
                if args.media_lifecycle else PSBFILE_LOAD_TARGETS
            )
            tracer = FridaTracerEngine(engine, targets)
            tracer.attach()
        try:
            for index, input_path in enumerate(args.input):
                cases = [("octet", None)]
                if args.storage:
                    remote_path = (
                        f"{engine.remote_dir.rstrip('/')}"
                        f"/psbfile-load-oracle-{index}.bin")
                    _push_input(engine, input_path, remote_path)
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

            if args.media_lifecycle:
                first_remote = (
                    f"{engine.remote_dir.rstrip('/')}/"
                    f"{MEDIA_FIRST_CONTAINER}")
                replacement_remote = (
                    f"{engine.remote_dir.rstrip('/')}/"
                    f"{MEDIA_REPLACEMENT_CONTAINER}")
                trace = None
                trace_started = False
                try:
                    _push_input(engine, MEDIA_FIRST_INPUT, first_remote)
                    _push_input(
                        engine, MEDIA_REPLACEMENT_INPUT, replacement_remote)
                    if tracer is not None:
                        tracer.start_case()
                        trace_started = True
                    result = adapter.run_media_lifecycle_case(
                        engine,
                        first_input=MEDIA_FIRST_INPUT,
                        replacement_input=MEDIA_REPLACEMENT_INPUT,
                        remote_dir=engine.remote_dir,
                        first_container=MEDIA_FIRST_CONTAINER,
                        replacement_container=MEDIA_REPLACEMENT_CONTAINER,
                        resource_name=MEDIA_RESOURCE_NAME,
                        expected_size=MEDIA_RESOURCE_SIZE,
                    )
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
        finally:
            if tracer is not None:
                tracer.detach()

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
