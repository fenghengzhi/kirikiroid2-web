"""Adapter for sub_699940 at 0x699940 (builds local 2x2 matrix from node fields).

Prototype (decompiled):
    void sub_699940(__int64 this, __int64 ctx);

`this` is a layer-like struct; sub_699940 reads transform inputs at known
offsets and writes the local 2x2 matrix L back to offsets +120..+144.
Early-returns (no write) when *(_DWORD *)(ctx + 16) == 0.

Offsets within `this`:
    +84..96  int32 transformOrder[4]
    +120     double L[0]  (m11)  — function writes output here
    +128     double L[1]  (m12)
    +136     double L[2]  (m21)
    +144     double L[3]  (m22)
    +1507    uint8  flipX
    +1508    uint8  flipY
    +1536    double angle     (degrees)
    +1544    double scaleX
    +1552    double scaleY
    +1560    double slantX
    +1568    double slantY

Note: libkrkr2.so only builds L. The port's applyLocalTransform additionally
performs `A_new = A × L`; we replicate that step in Python so the oracle
result lines up with the spec's `"expected"` Affine2x3.

libm deps: sin, cos (case 1).
"""

from __future__ import annotations

import math
import struct

SUB_699940_OFFSET = 0x699940

_NODE_SIZE = 2048  # > 1568 + 8, with headroom
_OFF_TRANSFORM_ORDER = 84
_OFF_L11 = 120
_OFF_FLIP_X = 1507
_OFF_FLIP_Y = 1508
_OFF_ANGLE = 1536
_OFF_SCALE_X = 1544
_OFF_SCALE_Y = 1552
_OFF_SLANT_X = 1560
_OFF_SLANT_Y = 1568


def _build_node(heap, spec: dict) -> int:
    buf = bytearray(_NODE_SIZE)
    order = spec["transformOrder"]
    assert len(order) == 4
    struct.pack_into("<4i", buf, _OFF_TRANSFORM_ORDER, *map(int, order))
    buf[_OFF_FLIP_X] = 1 if spec["flipX"] else 0
    buf[_OFF_FLIP_Y] = 1 if spec["flipY"] else 0
    struct.pack_into("<d", buf, _OFF_ANGLE, float(spec["angle"]))
    struct.pack_into("<d", buf, _OFF_SCALE_X, float(spec["scaleX"]))
    struct.pack_into("<d", buf, _OFF_SCALE_Y, float(spec["scaleY"]))
    struct.pack_into("<d", buf, _OFF_SLANT_X, float(spec["slantX"]))
    struct.pack_into("<d", buf, _OFF_SLANT_Y, float(spec["slantY"]))
    return heap.write(bytes(buf), align=8)


def _build_ctx(heap) -> int:
    """Minimal ctx with nonzero int32 at offset 16 so sub_699940 does work."""
    ctx = bytearray(32)
    struct.pack_into("<i", ctx, 16, 1)
    return heap.write(bytes(ctx), align=8)


def _apply_ax_l(affine_in, l) -> list[float]:
    """Port-side A × L multiplication (PlayerUpdateLayers.cpp:200-206)."""
    a0, a1, a2, a3, a4, a5 = affine_in  # Affine2x3 = [m11,m21,m12,m22,tx,ty]
    l11, l12, l21, l22 = l
    m11 = a0 * l11 + a2 * l21
    m21 = a1 * l11 + a3 * l21
    m12 = a0 * l12 + a2 * l22
    m22 = a1 * l12 + a3 * l22
    return [m11, m21, m12, m22, a4, a5]


def run_case(engine, spec: dict, tracer=None) -> dict:
    engine.reset_heap()
    node_addr = _build_node(engine.heap, spec)
    ctx_addr = _build_ctx(engine.heap)
    addr = engine.offset(SUB_699940_OFFSET)
    trace = None
    if tracer is not None:
        tracer.start_case()
    try:
        engine.call(addr, ints=(node_addr, ctx_addr), ret="void")
    finally:
        if tracer is not None:
            trace = tracer.stop_case()

    # Read back L from layer offsets 120, 128, 136, 144.
    raw = engine.ql.mem.read(node_addr + _OFF_L11, 32)
    l11, l12, l21, l22 = struct.unpack("<4d", bytes(raw))
    affine_out = _apply_ax_l(spec["affine_in"], (l11, l12, l21, l22))

    expected = spec["expected"]
    tol = 1e-12
    mismatches = [
        (i, a, b)
        for i, (a, b) in enumerate(zip(affine_out, expected))
        if not math.isclose(a, b, rel_tol=tol, abs_tol=tol)
    ]
    status = "mismatch" if mismatches else "ok"
    out = {
        "case_id": spec["id"],
        "status": status,
        "result": affine_out,
        "expected": expected,
        "mismatches": mismatches,
        "local_l": [l11, l12, l21, l22],
    }
    if trace is not None:
        out["trace"] = trace
    return out
