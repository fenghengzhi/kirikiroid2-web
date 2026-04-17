#!/usr/bin/env python3
"""
compare_trace.py — diff motionsim expected TSV against a Web port runtime trace log.

USAGE:
    python3 tools/motionsim/compare_trace.py \\
        /tmp/m2logo_expected.tsv /tmp/m2logo_trace.log \\
        [--motion m2logo.mtn] [--max-rows 30]

The expected TSV is produced by tools/bin/mac/rel/motionsim. The trace log is the
browser console output captured via /tmp/capture_trace.mjs, containing
`phase2.accum_final` lines emitted by PlayerUpdateLayers.cpp.

The script joins on (time, nodeIdx) and prints rows where any numeric field
exceeds the per-field tolerance. It highlights the FIRST divergence per
(nodeIdx × field) pair, which is the most useful signal when hunting bugs.
"""

import argparse
import math
import re
import sys
from collections import defaultdict

# Per-field tolerance for considering an expected/actual pair "equal".
# Tighter than the simulator's output precision (fmt::format %g). Adjust
# if Python's float parsing introduces rounding.
TOL = {
    # Web trace formats floats with %.3f, so ±0.0005 is quantisation noise.
    "accum.posX":   1e-2,   # pixels
    "accum.posY":   1e-2,
    "accum.posZ":   1e-2,
    "accum.scaleX": 5e-4,
    "accum.scaleY": 5e-4,
    "accum.angle":  1e-3,   # degrees
    "accum.opacity": 1,     # integer 0-255
    "accum.m11": 1e-3,
    "accum.m21": 1e-3,
    "accum.m12": 1e-3,
    "accum.m22": 1e-3,
    "visible": 0.5,
    "active":  0.5,
}


# Field order in the expected TSV (matches motionsim.cpp header).
EXPECTED_COLS = [
    "time", "nodeIdx", "label", "type", "parentIdx", "inheritFlags",
    "visible", "active",
    "state.posX", "state.posY", "state.scaleX", "state.scaleY",
    "state.angle", "state.opacity",
    "accum.posX", "accum.posY", "accum.posZ",
    "accum.scaleX", "accum.scaleY", "accum.angle", "accum.opacity",
    "accum.m11", "accum.m21", "accum.m12", "accum.m22",
    "clipW", "clipH", "originX", "originY",
    "coverageWarn",
]


def parse_expected(path):
    """Returns dict: (time_str, nodeIdx) -> {col: str value}."""
    table = {}
    with open(path, "r", encoding="utf-8") as f:
        header = f.readline().rstrip("\n").split("\t")
        if header != EXPECTED_COLS:
            sys.exit(f"unexpected expected-TSV header: {header}")
        for line in f:
            cells = line.rstrip("\n").split("\t")
            if len(cells) != len(header):
                continue
            row = dict(zip(header, cells))
            t = float(row["time"])
            key = (round(t, 3), int(row["nodeIdx"]))
            table[key] = row
    return table


# Trace regex for `phase2.accum_final` lines emitted by PlayerUpdateLayers.cpp.
# Captures the fields we care about in the order the log writes them.
TRACE_LINE_RE = re.compile(
    r"stage=updateLayers\.phase2\.accum_final"
    r"\s+func=\S+"
    r"\s+motion=(?P<motion>\S+)"
    r"\s+frame=(?P<time>[0-9.]+)"
    r"\s+nodeIndex=(?P<nodeIdx>-?\d+)"
    r"\s+label=(?P<label>\S+)"
    r"\s+type=(?P<type>-?\d+)"
    r"\s+parentIdx=(?P<parentIdx>-?\d+)"
    r"\s+parentLabel=\S+\s+"
    r"state\[visible=(?P<visible>-?\d+),"
    r"evaluated=-?\d+,"
    r"opacity=(?P<state_opacity>-?[0-9.eE+-]+),"
    r"scale=\((?P<state_scaleX>-?[0-9.eE+-]+),(?P<state_scaleY>-?[0-9.eE+-]+)\),"
    r"localPos=\((?P<state_posX>-?[0-9.eE+-]+),(?P<state_posY>-?[0-9.eE+-]+),(?P<state_posZ>-?[0-9.eE+-]+)\)\]"
    r"\s+parentAccum\[\S+.*?\]"   # not compared directly; child.accum is derived
    r"\s+accum\["
    r"pos=\((?P<accum_posX>-?[0-9.eE+-]+),(?P<accum_posY>-?[0-9.eE+-]+),(?P<accum_posZ>-?[0-9.eE+-]+)\),"
    r"m=\((?P<accum_m11>-?[0-9.eE+-]+),(?P<accum_m21>-?[0-9.eE+-]+),(?P<accum_m12>-?[0-9.eE+-]+),(?P<accum_m22>-?[0-9.eE+-]+)\),"
    r"scale=\((?P<accum_scaleX>-?[0-9.eE+-]+),(?P<accum_scaleY>-?[0-9.eE+-]+)\),"
    r"opacity=(?P<accum_opacity>-?\d+),"
    r"visible=(?P<accum_visible>-?\d+),"
    r"active=(?P<accum_active>-?\d+)\]"
)


def parse_trace(path, motion_filter):
    """Returns dict: (time, nodeIdx) -> {col: str value}."""
    table = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = TRACE_LINE_RE.search(line)
            if not m:
                continue
            if motion_filter and m.group("motion") != motion_filter:
                continue
            key = (round(float(m.group("time")), 3), int(m.group("nodeIdx")))
            table[key] = {
                "nodeIdx": int(m.group("nodeIdx")),
                "label":   m.group("label"),
                "type":    int(m.group("type")),
                "parentIdx": int(m.group("parentIdx")),
                "visible": int(m.group("accum_visible")),
                "active":  int(m.group("accum_active")),
                "accum.posX":   float(m.group("accum_posX")),
                "accum.posY":   float(m.group("accum_posY")),
                "accum.posZ":   float(m.group("accum_posZ")),
                "accum.scaleX": float(m.group("accum_scaleX")),
                "accum.scaleY": float(m.group("accum_scaleY")),
                # Web trace doesn't currently log accum.angle — inferred from matrix if needed
                "accum.opacity": int(m.group("accum_opacity")),
                "accum.m11": float(m.group("accum_m11")),
                "accum.m21": float(m.group("accum_m21")),
                "accum.m12": float(m.group("accum_m12")),
                "accum.m22": float(m.group("accum_m22")),
            }
    return table


def pick_number(row, col):
    raw = row.get(col)
    if raw is None:
        return None
    try:
        if col in ("visible", "active", "accum.opacity"):
            return int(float(raw))
        return float(raw)
    except ValueError:
        return None


def compare(expected, actual):
    """
    Returns list of (time, nodeIdx, label, field, exp, act, delta) for every
    diverging row.
    """
    divs = []
    for key, exp_row in expected.items():
        act_row = actual.get(key)
        if act_row is None:
            continue
        label = exp_row["label"]
        for field, tol in TOL.items():
            exp_v = pick_number(exp_row, field)
            act_v = pick_number(act_row, field)
            if exp_v is None or act_v is None:
                continue
            if math.isnan(exp_v) or math.isnan(act_v):
                continue
            delta = abs(exp_v - act_v)
            if delta > tol:
                divs.append((key[0], key[1], label, field, exp_v, act_v, delta))
    return divs


def main():
    p = argparse.ArgumentParser()
    p.add_argument("expected_tsv", help="output of tools/bin/mac/rel/motionsim")
    p.add_argument("trace_log", help="Web port console trace log with phase2.accum_final lines")
    p.add_argument("--motion", default="m2logo.mtn",
                   help="filter trace lines to this motion filename (default: m2logo.mtn)")
    p.add_argument("--max-rows", type=int, default=40,
                   help="max diverging rows to print")
    args = p.parse_args()

    exp = parse_expected(args.expected_tsv)
    act = parse_trace(args.trace_log, args.motion)

    exp_keys = set(exp.keys())
    act_keys = set(act.keys())
    only_exp = exp_keys - act_keys
    only_act = act_keys - exp_keys
    both = exp_keys & act_keys

    print(f"# expected rows: {len(exp_keys)}")
    print(f"# trace    rows: {len(act_keys)}")
    print(f"# joined rows:   {len(both)}")
    if only_exp:
        print(f"# only in expected: {len(only_exp)} "
              f"(e.g. {sorted(only_exp)[:3]})")
    if only_act:
        print(f"# only in actual:   {len(only_act)} "
              f"(e.g. {sorted(only_act)[:3]})")

    divs = compare(exp, act)
    if not divs:
        print("\nNO divergences above tolerance. Phase-2 accumulator matches.")
        return

    # 1. First divergence per (nodeIdx, field) — earliest time
    by_nf = {}
    for (t, idx, label, f, e, a, d) in divs:
        key = (idx, f)
        if key not in by_nf or t < by_nf[key][0]:
            by_nf[key] = (t, label, e, a, d)
    first_rows = sorted(
        [(t, idx, label, f, e, a, d)
         for (idx, f), (t, label, e, a, d) in by_nf.items()],
        key=lambda r: (r[0], r[1], r[3])
    )
    print(f"\n=== FIRST divergence per (nodeIdx × field), {len(first_rows)} pairs ===")
    print(f"{'time':>8}  {'idx':>3}  {'label':<24}  {'field':<14}  "
          f"{'expected':>14}  {'actual':>14}  {'|delta|':>10}")
    for (t, idx, label, f, e, a, d) in first_rows[: args.max_rows]:
        print(f"{t:>8.3f}  {idx:>3}  {label[:24]:<24}  {f:<14}  "
              f"{e:>14.4f}  {a:>14.4f}  {d:>10.4f}")

    # 2. Field-wise top-delta summary
    by_field = defaultdict(list)
    for (t, idx, label, f, e, a, d) in divs:
        by_field[f].append((d, t, idx, label, e, a))
    print(f"\n=== per-field divergence counts & worst case ===")
    for f in sorted(by_field):
        xs = sorted(by_field[f], reverse=True)
        worst = xs[0]
        print(f"{f:<14}  count={len(xs):>4}  worst=|Δ|={worst[0]:.4f} "
              f"at t={worst[1]:.3f} idx={worst[2]} label={worst[3]} "
              f"(exp={worst[4]:.4f} act={worst[5]:.4f})")

    # 3. Per-node first-divergence time (which node goes wrong first)
    earliest_per_node = {}
    for (t, idx, label, f, e, a, d) in divs:
        if idx not in earliest_per_node or t < earliest_per_node[idx][0]:
            earliest_per_node[idx] = (t, label, f, e, a, d)
    print(f"\n=== earliest divergence per node ===")
    for idx in sorted(earliest_per_node):
        t, label, f, e, a, d = earliest_per_node[idx]
        print(f"  idx={idx:>3}  {label[:24]:<24}  first at t={t:.3f} "
              f"field={f} (exp={e:.4f} act={a:.4f} Δ={d:.4f})")


if __name__ == "__main__":
    main()
