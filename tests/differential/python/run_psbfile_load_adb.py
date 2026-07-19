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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input", action="append", required=True, type=Path,
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
    args = parser.parse_args()

    failed = False
    tracer = None
    with AdbHarnessEngine(
        serial=args.serial, remote_dir=args.remote_dir) as engine:
        if args.trace:
            from oracle_runner.frida_tracer import FridaTracerEngine
            from oracle_runner.trace_targets import PSBFILE_LOAD_TARGETS

            tracer = FridaTracerEngine(engine, PSBFILE_LOAD_TARGETS)
            tracer.attach()
        try:
            for index, input_path in enumerate(args.input):
                cases = [("octet", None)]
                if args.storage:
                    remote_path = (
                        f"{engine.remote_dir.rstrip('/')}"
                        f"/psbfile-load-oracle-{index}.bin")
                    adb = [engine.adb]
                    if engine.serial:
                        adb += ["-s", engine.serial]
                    subprocess.run(
                        adb + ["push", str(input_path), remote_path],
                        check=True, capture_output=True)
                    subprocess.run(
                        adb + ["shell", "chmod", "644", remote_path],
                        check=True, capture_output=True)
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
        finally:
            if tracer is not None:
                tracer.detach()

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
