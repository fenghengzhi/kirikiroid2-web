#!/usr/bin/env python3
"""ADB oracle runner for position_interp (sub_69A4D4).

With ``--trace``/``--record-trace``, the runner additionally attaches a
Frida tracer to the guest harness and diffs runtime call sequences
against checked-in golden traces under ``--trace-dir`` (default:
``tests/differential/traces``). See ``oracle_runner/README.md`` for
frida-server provisioning.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from python.run_position_interp_wasmtime import load_specs  # noqa: E402
from oracle_runner.adb_engine import AdbHarnessEngine  # noqa: E402
from oracle_runner.adapters import position_interp as adapter  # noqa: E402


DEFAULT_TRACE_DIR = Path(__file__).resolve().parents[1] / "traces"
FAMILY = "position_interp"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--serial", default=None)
    parser.add_argument("--remote-dir", default=None)
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
    POSITION_INTERP_TARGETS = None
    trace_diff = None
    if use_tracer:
        from oracle_runner.frida_tracer import FridaTracerEngine as _FT
        from oracle_runner.trace_targets import POSITION_INTERP_TARGETS as _TGT
        from oracle_runner import trace_diff as _TD
        FridaTracerEngine = _FT
        POSITION_INTERP_TARGETS = _TGT
        trace_diff = _TD

    failed = False
    tracer = None
    with AdbHarnessEngine(serial=args.serial, remote_dir=args.remote_dir) as engine:
        if use_tracer:
            tracer = FridaTracerEngine(engine, POSITION_INTERP_TARGETS)
            tracer.attach()
        try:
            for spec in specs:
                # sub_698454 / sub_69A754 can SIGSEGV on degenerate inputs
                # that the port sanitises but libkrkr2 doesn't. The harness
                # dies with it — restart so later cases aren't cascade-failed.
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
                        tracer = FridaTracerEngine(engine, POSITION_INTERP_TARGETS)
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
                if result["status"] == "mismatch":
                    failed = True
                    print(f"MISMATCH {result['case_id']}: {result['mismatches']}", file=sys.stderr)
        finally:
            if tracer is not None:
                tracer.detach()

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
