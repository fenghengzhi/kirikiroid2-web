"""Per-family target address catalog for the Frida call tracer.

Offsets are load-base relative (applied on device as
`Module.findBaseAddress('libkrkr2.so').add(off)`).

Keep the list minimal: only sub-functions on the critical path so the
trace doesn't drown in low-signal variant/helper noise. Callees like
`sub_A0F5E0` / `sub_A0F778` (tTJSVariant ctor/dtor) fire tens of times
per case — include them only if we need to debug variant lifetimes.
"""

# --- bezier_curve: sub_69A754 (cubic Bézier over TJS-wrapped arrays) ----
# Decompile at 0x69A754 shows critical path:
#   sub_56C694(curve)    → control-point count
#   sub_6695BC(xs, i)    → x control point
#   sub_6695BC(ys, i)    → y control point
BEZIER_CURVE_TARGETS = [
    0x69A754,   # sub_69A754 — entry
    0x56C694,   # tTJSArray length
    0x6695BC,   # tTJSArray indexed-as-double
]

# --- position_interp: sub_69A4D4 -----------------------------------------
# Decompile at 0x69A4D4 shows:
#   sub_69A754(easing, t)          — when easing variant type != 0
#   sub_698454(&rot_out, rot, ...) — when rotation variant type != 0
#   plus transitive bezier arm targets above
POSITION_INTERP_TARGETS = [
    0x69A4D4,   # sub_69A4D4 — entry
    0x69A754,   # bezier (easing branch)
    0x698454,   # rotation inner
    0x56C694,   # transitive
    0x6695BC,   # transitive
]

# --- geometry_hit_test: Player_hitTest (leaf, plain-C) -------------------
# The adapter passes a single HitData pointer + two doubles (point x/y)
# and reads the int return. No interesting callees — hooking the entry
# is enough to catch signature drift across port/libkrkr2 rebuilds.
GEOMETRY_HIT_TEST_TARGETS = [
    0x690DF0,   # Player_hitTest
]

# --- local_transform: sub_699940 (leaf, builds 2x2) ----------------------
# Takes (Layer*, Affine*), returns void, no libm calls in the hot path —
# libm sin/cos only fire on the rotate-transform branch and the adapter
# already rounds to bit-exact doubles on the Python A×L step.
LOCAL_TRANSFORM_TARGETS = [
    0x699940,   # sub_699940
]


ADDR_NAMES = {
    0x69A754: "sub_69A754",
    0x69A4D4: "sub_69A4D4",
    0x698454: "sub_698454",
    0x56C694: "sub_56C694",
    0x6695BC: "sub_6695BC",
    0x690DF0: "Player_hitTest",
    0x699940: "sub_699940",
}

# (n_int_args, n_double_args) per target — AAPCS64 arg count cap of 8
# each. Everything beyond these positions is register garbage left by
# the caller's FP/int math and must be masked when normalising; otherwise
# the trace diff fires on meaningless bits.
#
# Signatures (from IDA):
#   sub_69A754(tTJSVariant *curve, double t)
#   sub_69A4D4(tTJSVariant *easing, double *src, double *dst, double *out,
#              int coord_mode, tTJSVariant *rotation, double t, double _)
#   sub_698454(double *out_xy, tTJSVariant *rotation, double t)
#   sub_56C694(tTJSArray *arr)                        → length
#   sub_6695BC(tTJSArray *arr, int idx, tTJSVariant *out, int flag)
ARG_COUNTS: dict[int, tuple[int, int]] = {
    0x69A754: (1, 1),
    0x69A4D4: (6, 2),
    0x698454: (2, 1),
    0x56C694: (1, 0),
    0x6695BC: (4, 0),
    # Player_hitTest(HitData *hd, double px, double py) → int
    0x690DF0: (1, 2),
    # sub_699940(Layer *node, Affine *ctx) → void
    0x699940: (2, 0),
}

# Return kind per target: "int", "double", "void". Drives exit-event
# canonicalisation — for "double"-returning fns we keep d0 and drop x0
# (AAPCS64 puts doubles in d0, leaving x0 as scratch), etc.
RETURN_KINDS: dict[int, str] = {
    0x69A754: "double",
    0x69A4D4: "void",
    0x698454: "void",
    0x56C694: "int",
    0x6695BC: "double",
    0x690DF0: "int",
    0x699940: "void",
}


def return_kind(offset: int) -> str:
    return RETURN_KINDS.get(offset, "int")


def addr_name(offset: int) -> str:
    return ADDR_NAMES.get(offset, f"sub_{offset:X}")


def arg_counts(offset: int) -> tuple[int, int]:
    """(n_int, n_double) for the target, or (8, 8) if unknown.

    An unknown target has full 8/8 so we keep every register — catches
    misconfigured targets at golden-record time rather than silently
    dropping data.
    """
    return ARG_COUNTS.get(offset, (8, 8))
