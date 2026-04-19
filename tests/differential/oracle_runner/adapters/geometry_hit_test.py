"""Adapter for Player_hitTest at 0x690DF0.

Prototype (decompiled):
    __int64 Player_hitTest(double *hd, double x, double y);

hd points to HitData = {int32 type; int32 pad; double values[16];}.
Circle: values[0..2] = cx, cy, r
Rect:   values[3..6] = left, top, right, bottom
Quad:   values[7..14] = x0, y0, x1, y1, x2, y2, x3, y3
No libm deps.
"""

from __future__ import annotations

from ..stl_layout import build_hit_data

PLAYER_HIT_TEST_OFFSET = 0x690DF0


def _flatten_shape(shape: dict) -> tuple[int, list[float]]:
    kind = shape["kind"]
    values = [0.0] * 16
    if kind == "circle":
        type_id = int(shape.get("type_override", 1))
        values[0] = float(shape["cx"])
        values[1] = float(shape["cy"])
        values[2] = float(shape["r"])
    elif kind == "rect":
        type_id = int(shape.get("type_override", 2))
        values[3] = float(shape["left"])
        values[4] = float(shape["top"])
        values[5] = float(shape["right"])
        values[6] = float(shape["bottom"])
    elif kind == "quad":
        type_id = int(shape.get("type_override", 3))
        values[7] = float(shape["x0"])
        values[8] = float(shape["y0"])
        values[9] = float(shape["x1"])
        values[10] = float(shape["y1"])
        values[11] = float(shape["x2"])
        values[12] = float(shape["y2"])
        values[13] = float(shape["x3"])
        values[14] = float(shape["y3"])
    else:
        raise ValueError(f"unknown shape kind: {kind}")
    return type_id, values


def run_case(engine, spec: dict, tracer=None) -> dict:
    engine.reset_heap()
    type_id, values = _flatten_shape(spec["shape"])
    hd_ptr = build_hit_data(engine.heap, type_id, values)
    px = float(spec["point"]["x"])
    py = float(spec["point"]["y"])
    addr = engine.offset(PLAYER_HIT_TEST_OFFSET)
    trace = None
    if tracer is not None:
        tracer.start_case()
    try:
        result = engine.call(addr, ints=(hd_ptr,), doubles=(px, py), ret="int")
    finally:
        if tracer is not None:
            trace = tracer.stop_case()
    out = {"case_id": spec["id"], "hit": bool(result), "status": "ok"}
    if trace is not None:
        out["trace"] = trace
    return out
