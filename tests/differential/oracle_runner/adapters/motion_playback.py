"""Adapter for the `motion_playback` differential family.

Drives libkrkr2's TJS-side Motion.Player from inside the APK harness and
captures a per-frame snapshot of every layer's accumulated state. The
snapshot is the oracle that the port-side `motion_playback_port` CLI
diff'd against.

Two modes:
  * Live oracle: feed `engine` (an `AdbEngine`); we exec a TJS script that
    plays the motion and returns a JSON string via the harness's
    TJS_EXEC_STR command. Use `--record-oracle` in the runner for this.
  * Disk oracle: pass `engine=None` and provide `spec["expected_trace"]`;
    the adapter just loads the cached golden JSON.

The TJS script body is built inline because TJS standard library coverage
varies between APK builds; we cannot rely on a built-in JSON encoder, so
we ship a tiny one (~30 lines of TJS) tuned for the snapshot schema.
"""

from __future__ import annotations

import json
import shlex
import subprocess
from pathlib import Path
from typing import Any


# Schema fields, kept in sync with tests/differential/port_runners/motion_playback_port.cpp.
LAYER_FIELDS_NUM = (
    "posX", "posY", "posZ", "angleDeg",
    "scaleX", "scaleY", "slantX", "slantY",
)
LAYER_FIELDS_INT = ("opacity", "blendMode", "nodeType", "index")
LAYER_FIELDS_BOOL = ("visible", "active", "flipX", "flipY")
LAYER_FIELDS_STR = ("label", "currentImage")


def _build_snapshot_script(mtn_path: str, label: str, frames: int) -> str:
    """TJS source. The function returns a JSON string; ExecScript writes
    that string into the result variant which TJS_EXEC_STR ferries back."""
    # JSON encoder kept TJS-1.x compatible (no try/catch around toString).
    return f"""
(function() {{
  var enc = function(v) {{
    if (v === void) return "null";
    var t = typeof v;
    if (t == "Object") {{
      if (v == null) return "null";
      // Treat any Array-like as array if it has .count and integer keys.
      if (v instanceof "Array") {{
        var s = "[";
        for (var i = 0; i < v.count; i++) {{
          if (i > 0) s += ",";
          s += enc(v[i]);
        }}
        return s + "]";
      }}
      var keys = v.getKeys();  // Dictionary
      var s2 = "{{";
      var first = true;
      for (var i = 0; i < keys.count; i++) {{
        var k = keys[i];
        if (!first) s2 += ",";
        first = false;
        s2 += "\\"" + k + "\\":" + enc(v[k]);
      }}
      return s2 + "}}";
    }}
    if (t == "Integer") return "" + v;
    if (t == "Real") {{
      var s = "" + v;
      // TJS floats can stringify as "1" — make them JSON-safe doubles.
      if (s.indexOf(".") < 0 && s.indexOf("e") < 0 && s.indexOf("E") < 0)
        s += ".0";
      return s;
    }}
    if (t == "String") {{
      var s = "\\"";
      for (var i = 0; i < v.length; i++) {{
        var ch = v[i];
        if (ch == "\\\\") s += "\\\\\\\\";
        else if (ch == "\\"") s += "\\\\\\"";
        else if (ch == "\\n") s += "\\\\n";
        else if (ch == "\\r") s += "\\\\r";
        else if (ch == "\\t") s += "\\\\t";
        else s += ch;
      }}
      return s + "\\"";
    }}
    if (v === true) return "true";
    if (v === false) return "false";
    return "null";
  }};

  var rm = new Motion.ResourceManager(null, 0);
  var p = new Motion.Player(rm);
  p.motion = "{mtn_path}";
  p.play("{label}", 0);
  var frames = [];
  for (var f = 0; f < {frames}; f++) {{
    p.progress(1000.0 / 60.0);
    var names = p.getLayerNames();
    var layers = [];
    for (var i = 0; i < names.count; i++) {{
      var g = p.getLayerGetter(names[i]);
      layers.add(%[
        "index": i,
        "label": names[i],
        "nodeType": g.type,
        "visible": g.visible,
        "active": g.branchVisible,
        "flipX": g.flipX,
        "flipY": g.flipY,
        "posX": g.x,
        "posY": g.y,
        "posZ": 0.0,
        "angleDeg": g.angleDeg,
        "scaleX": g.zoomX,
        "scaleY": g.zoomY,
        "slantX": g.slantX,
        "slantY": g.slantY,
        "opacity": g.opacity,
        "blendMode": 16,
        "currentImage": ""
      ]);
    }}
    frames.add(%[ "frame": f, "layers": layers ]);
  }}
  return enc(frames);
}})()
"""


def push_fixture(serial: str | None, local: Path, remote: str) -> None:
    cmd = ["adb"]
    if serial:
        cmd += ["-s", serial]
    cmd += ["push", str(local), remote]
    subprocess.run(cmd, check=True, capture_output=True)


def record_oracle(engine, spec: dict, *, serial: str | None = None) -> list:
    """Run the TJS snapshot inside the APK and return a Python list-of-dicts
    matching the schema written by motion_playback_port.cpp."""
    mtn_local = Path(spec["mtn_path"])
    if not mtn_local.is_absolute():
        mtn_local = Path(__file__).resolve().parents[3] / spec["mtn_path"]
    if not mtn_local.exists():
        raise FileNotFoundError(f"motion fixture missing: {mtn_local}")

    remote = f"/data/local/tmp/{mtn_local.name}"
    push_fixture(serial, mtn_local, remote)

    engine.tjs_init()
    engine.tjs_reset()

    script = _build_snapshot_script(remote, spec["label"], int(spec["frames"]))
    payload = engine.tjs_exec_str(script)
    return json.loads(payload)


def _floats_close(a: float, b: float, *, rel: float, abs_: float) -> bool:
    if a == b:
        return True
    diff = abs(a - b)
    return diff <= max(abs_, rel * max(abs(a), abs(b)))


def diff_frames(port_frames: list, oracle_frames: list, *,
                rel: float = 1e-6, abs_: float = 1e-6) -> list:
    mismatches: list[dict[str, Any]] = []
    n = min(len(port_frames), len(oracle_frames))
    if len(port_frames) != len(oracle_frames):
        mismatches.append({
            "kind": "frame_count",
            "port": len(port_frames),
            "oracle": len(oracle_frames),
        })
    for f in range(n):
        pf = port_frames[f]
        of = oracle_frames[f]
        pl = pf.get("layers", [])
        ol = of.get("layers", [])
        if len(pl) != len(ol):
            mismatches.append({
                "kind": "layer_count",
                "frame": f,
                "port": len(pl),
                "oracle": len(ol),
            })
        for i in range(min(len(pl), len(ol))):
            pli = pl[i]
            oli = ol[i]
            for k in LAYER_FIELDS_INT + LAYER_FIELDS_BOOL + LAYER_FIELDS_STR:
                if pli.get(k) != oli.get(k):
                    mismatches.append({
                        "kind": "field",
                        "frame": f,
                        "layer_index": i,
                        "field": k,
                        "port": pli.get(k),
                        "oracle": oli.get(k),
                    })
            for k in LAYER_FIELDS_NUM:
                pv = pli.get(k)
                ov = oli.get(k)
                if pv is None or ov is None:
                    if pv != ov:
                        mismatches.append({
                            "kind": "field",
                            "frame": f,
                            "layer_index": i,
                            "field": k,
                            "port": pv,
                            "oracle": ov,
                        })
                    continue
                if not _floats_close(float(pv), float(ov),
                                     rel=rel, abs_=abs_):
                    mismatches.append({
                        "kind": "float",
                        "frame": f,
                        "layer_index": i,
                        "field": k,
                        "port": pv,
                        "oracle": ov,
                    })
    return mismatches


def run_case(engine, spec: dict, *, port_frames: list,
             oracle_frames: list | None = None,
             tracer=None) -> dict:
    """Compare port_frames against oracle_frames (live or cached)."""
    out: dict[str, Any] = {
        "case_id": spec["id"],
        "status": "ok",
        "mismatches": [],
    }
    if oracle_frames is None:
        out["status"] = "error"
        out["error"] = "no oracle frames provided"
        return out
    mismatches = diff_frames(port_frames, oracle_frames)
    out["mismatches"] = mismatches
    if mismatches:
        out["status"] = "mismatch"
    return out
