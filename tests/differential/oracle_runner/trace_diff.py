"""Golden-trace storage + sequential diff.

Layout on disk:
    tests/differential/traces/<family>/<case_id>.trace.json

Each file is a JSON object:
    {
      "case_id": "...",
      "family":  "bezier_curve",
      "events":  [ {kind, addr, depth, x?, d?, x0?, d0?}, ... ]
    }

Diff algorithm: linear pairwise compare. Reports the first divergence
(index, left event, right event) — beyond that the streams are
likely unrelated, so don't overwhelm with cascade mismatches.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .trace_targets import addr_name


@dataclass
class TraceMismatch:
    index: int
    golden: dict | None
    runtime: dict | None
    reason: str

    def format(self) -> str:
        def fmt(ev: dict | None) -> str:
            if ev is None:
                return "<end>"
            name = addr_name(int(ev["addr"]))
            d = ev.get("depth", "?")
            if ev["kind"] == "enter":
                x0 = ev["x"][0] if ev.get("x") else "?"
                d0 = ev["d"][0] if ev.get("d") else "?"
                return f"enter {name} depth={d} x0={x0} d0={d0}"
            return f"exit  {name} depth={d} x0={ev.get('x0')} d0={ev.get('d0')}"

        return (
            f"step {self.index}: {self.reason}\n"
            f"  golden:  {fmt(self.golden)}\n"
            f"  runtime: {fmt(self.runtime)}"
        )


def trace_path(trace_dir: Path, family: str, case_id: str) -> Path:
    return trace_dir / family / f"{case_id}.trace.json"


def save_trace(
    trace_dir: Path, family: str, case_id: str, events: list[dict]
) -> Path:
    path = trace_path(trace_dir, family, case_id)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {"case_id": case_id, "family": family, "events": events}
    with path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=True)
        f.write("\n")
    return path


def load_trace(
    trace_dir: Path, family: str, case_id: str
) -> list[dict] | None:
    path = trace_path(trace_dir, family, case_id)
    if not path.exists():
        return None
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("events") or []


def diff_traces(
    golden: list[dict], runtime: list[dict]
) -> TraceMismatch | None:
    """First divergent step, or None if identical."""
    n = max(len(golden), len(runtime))
    for i in range(n):
        g = golden[i] if i < len(golden) else None
        r = runtime[i] if i < len(runtime) else None
        if g is None or r is None:
            return TraceMismatch(
                index=i, golden=g, runtime=r,
                reason="trace length mismatch",
            )
        cause = _event_mismatch(g, r)
        if cause:
            return TraceMismatch(
                index=i, golden=g, runtime=r, reason=cause,
            )
    return None


def _event_mismatch(g: dict, r: dict) -> str | None:
    if g.get("kind") != r.get("kind"):
        return f"kind differs ({g.get('kind')} vs {r.get('kind')})"
    if int(g.get("addr", 0)) != int(r.get("addr", 0)):
        return (
            f"addr differs ({addr_name(int(g.get('addr', 0)))} "
            f"vs {addr_name(int(r.get('addr', 0)))})"
        )
    if int(g.get("depth", 0)) != int(r.get("depth", 0)):
        return f"depth differs ({g.get('depth')} vs {r.get('depth')})"
    if g["kind"] == "enter":
        if list(g.get("x", [])) != list(r.get("x", [])):
            return "x0..x7 differ"
        if list(g.get("d", [])) != list(r.get("d", [])):
            return "d0..d7 differ"
    else:
        if g.get("x0") != r.get("x0"):
            return f"x0 differs ({g.get('x0')} vs {r.get('x0')})"
        if g.get("d0") != r.get("d0"):
            return f"d0 differs ({g.get('d0')} vs {r.get('d0')})"
    return None
