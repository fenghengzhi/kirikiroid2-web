"""Android ARM64 STL container layouts + project-specific structs.

GNU libstdc++/gnustl and libc++ both use a 24-byte `std::vector<T>` header
on Android ARM64 for the layouts this file models:
`{T* begin; T* end; T* end_cap;}`. If bezier_curve / other smoke tests fail,
cross-check against sub_69A754 prologue's offset accesses
([x0], [x0+8], [x0+16]).
"""

from __future__ import annotations

import struct
from typing import Sequence

VECTOR_SIZE = 24  # sizeof(std::vector<T>) on Android ARM64


def build_vector(heap, payload: bytes, element_size: int) -> int:
    """Build a `std::vector<T>` whose storage is `payload`.

    Returns the guest address of the 24-byte vector header. Payload length
    must be a multiple of element_size.
    """
    assert len(payload) % element_size == 0, "payload length not aligned to element_size"
    data_addr = heap.write(payload, align=max(element_size, 8)) if payload else heap.alloc(0)
    end = data_addr + len(payload)
    hdr = struct.pack("<QQQ", data_addr, end, end)
    return heap.write(hdr, align=8)


def build_vector_double(heap, values: Sequence[float]) -> int:
    payload = struct.pack(f"<{len(values)}d", *values) if values else b""
    return build_vector(heap, payload, element_size=8)


def build_vector_uint8(heap, values: bytes | Sequence[int]) -> int:
    payload = bytes(values)
    return build_vector(heap, payload, element_size=1)


def build_vector_of_inline_structs(
    heap, inline_structs: Sequence[bytes], element_size: int
) -> int:
    """Store each struct inline in a contiguous buffer, then wrap in a vector."""
    for s in inline_structs:
        assert len(s) == element_size, f"struct len {len(s)} != {element_size}"
    payload = b"".join(inline_structs)
    return build_vector(heap, payload, element_size=element_size)


# ---- project-specific structs ----

HIT_DATA_SIZE = 4 + 4 + 16 * 8  # 136


def build_hit_data(heap, type_id: int, values: Sequence[float]) -> int:
    """{int32 type; int32 pad; double values[16];} — 136 B, 8-aligned."""
    vals = list(values) + [0.0] * (16 - len(values))
    assert len(vals) == 16
    payload = struct.pack("<ii16d", int(type_id), 0, *vals)
    return heap.write(payload, align=8)


def build_affine2x3(heap, affine: Sequence[float]) -> int:
    """6 doubles in/out, 48 B."""
    assert len(affine) == 6
    return heap.write(struct.pack("<6d", *map(float, affine)), align=8)


def read_affine2x3(ql, addr: int) -> list[float]:
    data = ql.mem.read(addr, 48)
    return list(struct.unpack("<6d", bytes(data)))


def build_int_array4(heap, values: Sequence[int]) -> int:
    """int32_t[4] — 16 B."""
    assert len(values) == 4
    return heap.write(struct.pack("<4i", *map(int, values)), align=4)


def build_vec3(heap, xyz: Sequence[float]) -> int:
    assert len(xyz) == 3
    return heap.write(struct.pack("<3d", *map(float, xyz)), align=8)


def read_vec3(ql, addr: int) -> list[float]:
    data = ql.mem.read(addr, 24)
    return list(struct.unpack("<3d", bytes(data)))


# ---- BezierCurve / ControlPointCurve (from PlayerInternal.h) ----
# BezierCurve = {vector<double> x; vector<double> y;} = 48 bytes
# SplineSegment = {vector<double> x; vector<double> y; vector<double> p;} = 72 bytes
# ControlPointCurve = {vector<double> x; vector<double> y; vector<double> t;
#                      vector<SplineSegment> s;} = 96 bytes

BEZIER_CURVE_SIZE = 2 * VECTOR_SIZE
SPLINE_SEGMENT_SIZE = 3 * VECTOR_SIZE
CONTROL_POINT_CURVE_SIZE = 4 * VECTOR_SIZE


def build_bezier_curve(heap, xs: Sequence[float], ys: Sequence[float]) -> int:
    """Allocate a BezierCurve = two consecutive vector<double>.

    Layout must be contiguous so the struct address == &curve.x (the first
    vector). We write a placeholder, then overwrite with the two vectors.
    """
    # Build element buffers first
    x_payload = struct.pack(f"<{len(xs)}d", *xs) if xs else b""
    y_payload = struct.pack(f"<{len(ys)}d", *ys) if ys else b""
    x_data = heap.write(x_payload, align=8) if x_payload else heap.alloc(0)
    y_data = heap.write(y_payload, align=8) if y_payload else heap.alloc(0)
    # Emit struct = {vec_x{begin,end,cap}, vec_y{begin,end,cap}}
    hdr = struct.pack(
        "<QQQQQQ",
        x_data, x_data + len(x_payload), x_data + len(x_payload),
        y_data, y_data + len(y_payload), y_data + len(y_payload),
    )
    return heap.write(hdr, align=8)


def build_spline_segment_inline(xs, ys, ps, heap) -> bytes:
    """Build 72B of {vec_x, vec_y, vec_p} whose payloads live in `heap`."""
    x_payload = struct.pack(f"<{len(xs)}d", *xs) if xs else b""
    y_payload = struct.pack(f"<{len(ys)}d", *ys) if ys else b""
    p_payload = struct.pack(f"<{len(ps)}d", *ps) if ps else b""
    x_data = heap.write(x_payload, align=8) if x_payload else heap.alloc(0)
    y_data = heap.write(y_payload, align=8) if y_payload else heap.alloc(0)
    p_data = heap.write(p_payload, align=8) if p_payload else heap.alloc(0)
    return struct.pack(
        "<QQQQQQQQQ",
        x_data, x_data + len(x_payload), x_data + len(x_payload),
        y_data, y_data + len(y_payload), y_data + len(y_payload),
        p_data, p_data + len(p_payload), p_data + len(p_payload),
    )


def build_control_point_curve(heap, xs, ys, ts, segments) -> int:
    """ControlPointCurve = {vec<double> x; vec<double> y; vec<double> t;
                            vec<SplineSegment> s;}"""
    x_payload = struct.pack(f"<{len(xs)}d", *xs) if xs else b""
    y_payload = struct.pack(f"<{len(ys)}d", *ys) if ys else b""
    t_payload = struct.pack(f"<{len(ts)}d", *ts) if ts else b""
    x_data = heap.write(x_payload, align=8) if x_payload else heap.alloc(0)
    y_data = heap.write(y_payload, align=8) if y_payload else heap.alloc(0)
    t_data = heap.write(t_payload, align=8) if t_payload else heap.alloc(0)

    # segments = list of dicts {x:[], y:[], p:[]}
    seg_blobs = [build_spline_segment_inline(s["x"], s["y"], s["p"], heap) for s in segments]
    if seg_blobs:
        s_payload = b"".join(seg_blobs)
        s_data = heap.write(s_payload, align=8)
    else:
        s_data = heap.alloc(0)
        s_payload = b""

    hdr = struct.pack(
        "<QQQQQQQQQQQQ",
        x_data, x_data + len(x_payload), x_data + len(x_payload),
        y_data, y_data + len(y_payload), y_data + len(y_payload),
        t_data, t_data + len(t_payload), t_data + len(t_payload),
        s_data, s_data + len(s_payload), s_data + len(s_payload),
    )
    return heap.write(hdr, align=8)
