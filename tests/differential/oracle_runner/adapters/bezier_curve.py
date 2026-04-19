"""Adapter for sub_69A754 at 0x69A754 — TJS-wrapped cubic Bézier evaluator.

Prototype (from decompile):
    double sub_69A754(tTJSVariant *curve_variant, double t);

The curve variant is a TJS object dispatch responding to PropGet(L"x") and
PropGet(L"y") with tTJSArray instances of control-point coordinates.
sub_6695BC indexes into those arrays (x control points are time breakpoints,
y are values). Bézier segments are groups of 4 consecutive points (step 3).

Construction strategy: build the input object in the harness-private tTJS
runtime via ExecScript, fetch the resulting global's tTJSVariant pointer,
pass to sub_69A754.
"""

from __future__ import annotations

import math
import struct

BEZIER_CURVE_OFFSET = 0x69A754


def _tjs_number(v: float) -> str:
    """Format a Python float as a TJS real literal that round-trips safely."""
    # repr() on Python floats gives the shortest ASCII that round-trips.
    s = repr(float(v))
    # TJS accepts `1.0`, `1.5e-3`, `.5`, negatives, etc. — all compatible.
    return s


def _tjs_array(xs) -> str:
    return "[" + ", ".join(_tjs_number(x) for x in xs) + "]"


def _build_source(var_name: str, xs, ys) -> str:
    return (
        f"var {var_name} = %[x: {_tjs_array(xs)}, y: {_tjs_array(ys)}];\n"
    )


def run_case(engine, spec: dict, tracer=None) -> dict:
    engine.reset_heap()
    engine.tjs_init()
    engine.tjs_reset()

    curve = spec["curve"]
    xs = list(curve.get("x") or [])
    ys = list(curve.get("y") or [])
    t = float(spec["t"])

    var_name = "oracle_bezier_in"
    source = _build_source(var_name, xs, ys)

    status = "ok"
    result = None
    err = None
    trace = None
    try:
        engine.tjs_exec(source)
        variant_ptr = engine.tjs_global(var_name)
        addr = engine.offset(BEZIER_CURVE_OFFSET)
        # Only the libkrkr2 call itself needs to be traced — TJS setup
        # (ExecScript / GetGlobal) is out of scope and would pollute the
        # event stream.
        if tracer is not None:
            tracer.start_case()
        try:
            result = engine.call(
                addr, ints=(variant_ptr,), doubles=(t,), ret="double"
            )
        finally:
            if tracer is not None:
                trace = tracer.stop_case()
    except Exception as exc:
        status = "error"
        err = repr(exc)

    expected = spec.get("expected")
    mismatches = []
    if status == "ok" and expected is not None:
        tol = 1e-12
        if not math.isclose(result, float(expected), rel_tol=tol, abs_tol=tol):
            status = "mismatch"
            mismatches = [(0, result, float(expected))]

    out = {
        "case_id": spec["id"],
        "status": status,
        "result": result,
        "expected": expected,
        "mismatches": mismatches,
    }
    if err:
        out["error"] = err
    if trace is not None:
        out["trace"] = trace
    return out
