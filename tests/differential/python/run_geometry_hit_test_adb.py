#!/usr/bin/env python3
"""ADB oracle runner for geometry_hit_test.

Drives Player_hitTest inside a real Android emulator/device via adb shell.
Uses the stdin/stdout RPC harness (see oracle_runner/harness/harness.cpp).

With ``--trace``/``--record-trace``, also attaches a Frida tracer and
diffs the call sequence against checked-in golden traces. See
``oracle_runner/README.md`` for frida-server provisioning.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from python.run_geometry_hit_test_wasmtime import EXPECTED_HITS, load_specs  # noqa: E402
from oracle_runner.adb_engine import AdbHarnessEngine  # noqa: E402
from oracle_runner.adapters import geometry_hit_test as adapter  # noqa: E402


DEFAULT_TRACE_DIR = Path(__file__).resolve().parents[1] / "traces"
FAMILY = "geometry_hit_test"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--serial", default=None, help="adb device serial")
    parser.add_argument("--remote-dir", default=None,
                        help="device directory holding libkrkr2.so + harness (default /data/local/tmp)")
    parser.add_argument(
        "--trace", action="store_true",
        help="attach Frida tracer and verify against golden traces",
    )
    parser.add_argument(
        "--record-trace", action="store_true",
        help="attach Frida tracer and overwrite golden traces on disk",
    )
    parser.add_argument(
        "--trace-dir", type=Path, default=DEFAULT_TRACE_DIR,
        help=f"golden trace root (default: {DEFAULT_TRACE_DIR})",
    )
    args = parser.parse_args()

    specs = load_specs(args.spec_dir)
    if not specs:
        raise RuntimeError(f"no specs in {args.spec_dir}")

    use_tracer = args.trace or args.record_trace

    FridaTracerEngine = None
    GEOMETRY_HIT_TEST_TARGETS = None
    trace_diff = None
    if use_tracer:
        from oracle_runner.frida_tracer import FridaTracerEngine as _FT
        from oracle_runner.trace_targets import GEOMETRY_HIT_TEST_TARGETS as _TGT
        from oracle_runner import trace_diff as _TD
        FridaTracerEngine = _FT
        GEOMETRY_HIT_TEST_TARGETS = _TGT
        trace_diff = _TD

    failed = False
    tracer = None
    with AdbHarnessEngine(serial=args.serial, remote_dir=args.remote_dir) as engine:
        if use_tracer:
            tracer = FridaTracerEngine(engine, GEOMETRY_HIT_TEST_TARGETS)
            tracer.attach()
        try:
            for spec in specs:
                if not engine.is_alive():
                    if tracer is not None:
                        tracer.detach()
                        tracer = None
                    try:
                        engine.restart()
                    except Exception as exc:
                        print(f"harness restart failed: {exc!r}", file=sys.stderr)
                        break
                    if use_tracer:
                        tracer = FridaTracerEngine(engine, GEOMETRY_HIT_TEST_TARGETS)
                        tracer.attach()
                try:
                    result = adapter.run_case(engine, spec, tracer=tracer)
                except Exception as exc:
                    failed = True
                    result = {"case_id": spec["id"], "status": "error", "error": repr(exc)}
                    print(json.dumps(result, ensure_ascii=True))
                    print(f"error in case {spec['id']}: {exc!r}", file=sys.stderr)
                    continue
                result["runner"] = "android-adb-oracle"

                trace = result.pop("trace", None)
                if use_tracer and trace is not None:
                    if args.record_trace:
                        trace_diff.save_trace(
                            args.trace_dir, FAMILY, spec["id"], trace,
                        )
                    elif args.trace:
                        golden = trace_diff.load_trace(
                            args.trace_dir, FAMILY, spec["id"],
                        )
                        if golden is None:
                            failed = True
                            print(
                                f"TRACE MISSING {spec['id']}: "
                                f"no golden under {args.trace_dir}",
                                file=sys.stderr,
                            )
                        else:
                            mismatch = trace_diff.diff_traces(golden, trace)
                            if mismatch is not None:
                                failed = True
                                print(
                                    f"TRACE MISMATCH {spec['id']}:\n"
                                    f"{mismatch.format()}",
                                    file=sys.stderr,
                                )

                print(json.dumps(result, ensure_ascii=True))

                expected = EXPECTED_HITS.get(result["case_id"])
                if expected is None:
                    failed = True
                    print(f"missing EXPECTED for {result['case_id']}", file=sys.stderr)
                    continue
                if result["hit"] != expected:
                    failed = True
                    print(
                        f"MISMATCH {result['case_id']}: "
                        f"adb={result['hit']} expected={expected}",
                        file=sys.stderr,
                    )
        finally:
            if tracer is not None:
                tracer.detach()

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
