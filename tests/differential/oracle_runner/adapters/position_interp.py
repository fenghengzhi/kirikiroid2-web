"""Adapter for sub_69A4D4 at 0x69A4D4 — position interpolation with easing + rotation.

Decompiled prototype (see docs/ida analysis):
    void sub_69A4D4(
        tTJSVariant *easing_curve,   // a1 (x0) — TJS object or empty variant
        double     *dst_pos,         // a2 (x1) — &double[3], returned at t=1
        double     *src_pos,         // a3 (x2) — &double[3], returned at t=0
        double     *out_pos,         // a4 (x3) — &double[3] (output)
        int         coord_mode,      // a5 (w4)
        tTJSVariant *rotation_curve, // a6 (x5) — TJS object or empty variant
        double      t,               // d0
        double      _unused          // d1
    );

Parameter order matches the port's interpolatePosition69A4D4
(PlayerInternal.h) and its call sites in PlayerUpdateLayers.cpp —
dst first, src second. Linear formula: srcPos*(1-et) + dstPos*et,
so t=0 → srcPos (= arg3), t=1 → dstPos (= arg2).

Important short-circuits:
  - if easing_curve's variant type (offset +16 in the variant) is 0,
    sub_69A754 is skipped and a plain linear lerp runs on `t`
  - if rotation_curve's variant type is 0, the rotation branch is skipped

Both `empty_curve` in easing and `empty segments` in rotation map to empty
variants, so we can pass a zero-filled 24-byte buffer for those.
"""

from __future__ import annotations

import math
import struct

SUB_69A4D4_OFFSET = 0x69A4D4


def _tjs_num(v: float) -> str:
    return repr(float(v))


def _tjs_arr(xs) -> str:
    return "[" + ", ".join(_tjs_num(x) for x in xs) + "]"


def _build_easing_source(var_name: str, xs, ys) -> str:
    return f"var {var_name} = %[x: {_tjs_arr(xs)}, y: {_tjs_arr(ys)}];\n"


def _build_rotation_source(var_name: str, spec: dict) -> str:
    """Builds a TJS dict like:
       %[x: [...], y: [...], t: [...], segments: [%[x:...,y:...,p:...], ...]]
    """
    xs = _tjs_arr(spec.get("x") or [])
    ys = _tjs_arr(spec.get("y") or [])
    ts = _tjs_arr(spec.get("t") or [])
    segs = spec.get("segments") or []
    seg_literals = [
        f"%[x: {_tjs_arr(s.get('x') or [])}, y: {_tjs_arr(s.get('y') or [])}, p: {_tjs_arr(s.get('p') or [])}]"
        for s in segs
    ]
    segs_lit = "[" + ", ".join(seg_literals) + "]"
    return (
        f"var {var_name} = "
        f"%[x: {xs}, y: {ys}, t: {ts}, segments: {segs_lit}];\n"
    )


def _alloc_empty_variant(heap) -> int:
    """24-byte zeroed tTJSVariant (type=0 ⇒ sub_69A4D4's short-circuit path)."""
    return heap.write(bytes(24), align=8)


def _has_content(d: dict) -> bool:
    return any(d.get(k) for k in ("x", "y", "segments"))


def run_case(engine, spec: dict, tracer=None) -> dict:
    engine.reset_heap()
    engine.tjs_init()
    engine.tjs_reset()

    src_pos = spec["src_pos"]
    dst_pos = spec["dst_pos"]
    t = float(spec["t"])
    coord_mode = int(spec["coord_mode"])
    easing = spec.get("easing_curve") or {}
    rotation = spec.get("rotation_curve") or {}

    # Both easing and rotation use the same short-circuit: sub_69A4D4
    # skips the TJS-bezier/spline branches when the variant's type (offset
    # +16) is 0. An empty curve in JSON semantically means "no easing" /
    # "no rotation" — translate that to a zero-filled variant so libkrkr2
    # takes the linear-lerp path. A non-empty curve gets a real TJS dispatch.
    easing_has_content = bool(easing.get("x")) or bool(easing.get("y"))
    rotation_has_content = _has_content(rotation)

    src_parts = []
    if easing_has_content:
        src_parts.append(_build_easing_source(
            "oracle_easing", easing.get("x") or [], easing.get("y") or []))
    if rotation_has_content:
        src_parts.append(_build_rotation_source("oracle_rot", rotation))

    if src_parts:
        try:
            engine.tjs_exec("".join(src_parts))
        except Exception as exc:
            return {
                "case_id": spec["id"], "status": "error",
                "error": f"tjs_exec: {exc!r}",
                "result": None, "expected": spec.get("expected"),
                "mismatches": [],
            }

    easing_ptr = (
        engine.tjs_global("oracle_easing") if easing_has_content
        else _alloc_empty_variant(engine.heap)
    )
    rotation_ptr = (
        engine.tjs_global("oracle_rot") if rotation_has_content
        else _alloc_empty_variant(engine.heap)
    )

    # double[3] buffers in the oracle heap
    src_addr = engine.heap.write(struct.pack("<3d", *map(float, src_pos)), align=8)
    dst_addr = engine.heap.write(struct.pack("<3d", *map(float, dst_pos)), align=8)
    out_addr = engine.heap.alloc(24, align=8)
    engine.ql.mem.write(out_addr, bytes(24))  # zero-init

    status = "ok"
    err = None
    trace = None
    try:
        if tracer is not None:
            tracer.start_case()
        try:
            engine.call(
                engine.offset(SUB_69A4D4_OFFSET),
                # a2=dst, a3=src — libkrkr2 returns a3 at t=0 and a2 at t=1.
                ints=(easing_ptr, dst_addr, src_addr, out_addr, coord_mode, rotation_ptr),
                doubles=(t,),
                ret="void",
            )
        finally:
            if tracer is not None:
                trace = tracer.stop_case()
    except Exception as exc:
        status = "error"
        err = repr(exc)

    result = None
    if status == "ok":
        raw = bytes(engine.ql.mem.read(out_addr, 24))
        result = list(struct.unpack("<3d", raw))

    expected = spec.get("expected")
    mismatches = []
    if status == "ok" and expected is not None:
        tol = 1e-12
        mismatches = [
            (i, a, float(b))
            for i, (a, b) in enumerate(zip(result, expected))
            if not math.isclose(a, float(b), rel_tol=tol, abs_tol=tol)
        ]
        if mismatches:
            status = "mismatch"

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
