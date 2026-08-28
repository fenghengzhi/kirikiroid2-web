#!/usr/bin/env python3
"""Run every scalar differential family through a Wasmtime CLI.

This lane avoids embedding Wasmtime's JIT into a host Python process.  It uses
the same standalone wasm modules and unchanged spec expectations.  Each
harness exposes a flattened scalar entry point so the CLI can pass all inputs
without a cross-invocation linear-memory mutation protocol.  Requiring
Wasmtime 44+ also guarantees that the same runtime has the portable guest
gdbstub used by ``run_wasm_guest_debug.py`` for interactive bytecode debugging.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
FAMILIES = (
    "geometry_hit_test",
    "psb_rl_decompress",
    "bezier_curve",
    "local_transform",
    "position_interp",
)
MINIMUM_GUEST_DEBUG_VERSION = (44, 0, 0)


def find_wasmtime() -> Path | None:
    value = shutil.which("wasmtime")
    return Path(value) if value else None


def wasmtime_version(wasmtime: Path) -> tuple[int, int, int]:
    proc = subprocess.run(
        [str(wasmtime), "--version"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"failed to query Wasmtime version ({proc.returncode})\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    match = re.search(r"\b(\d+)\.(\d+)\.(\d+)\b", proc.stdout)
    if match is None:
        raise RuntimeError(f"unrecognized Wasmtime version: {proc.stdout!r}")
    return tuple(int(value) for value in match.groups())


def require_guest_debug_wasmtime(wasmtime: Path) -> tuple[int, int, int]:
    version = wasmtime_version(wasmtime)
    if version < MINIMUM_GUEST_DEBUG_VERSION:
        wanted = ".".join(str(value) for value in MINIMUM_GUEST_DEBUG_VERSION)
        actual = ".".join(str(value) for value in version)
        raise RuntimeError(
            f"Wasmtime {actual} is too old; {wanted}+ is required for the "
            "Guest/Wasm-only gdbstub debugging path"
        )
    return version


def load_specs(spec_root: Path, family: str) -> list[dict]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted((spec_root / family).glob("*.json"))
    ]


def invoke(wasmtime: Path, wasm: Path, function: str, args: list) -> float:
    proc = subprocess.run(
        [
            str(wasmtime), "run", "--invoke", function, str(wasm),
            *(str(value) for value in args),
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"Wasmtime invoke failed ({proc.returncode}): {function}\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    values = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if len(values) != 1:
        raise RuntimeError(
            f"Wasmtime invoke returned {len(values)} value line(s): "
            f"{values!r}\nstderr:\n{proc.stderr}"
        )
    return float(values[0])


def geometry_args(spec: dict) -> list:
    shape = spec["shape"]
    values = [0.0] * 15
    kind = shape["kind"]
    if kind == "circle":
        hit_type = int(shape.get("type_override", 1))
        values[0:3] = [shape["cx"], shape["cy"], shape["r"]]
    elif kind == "rect":
        hit_type = int(shape.get("type_override", 2))
        values[3:7] = [
            shape["left"], shape["top"], shape["right"], shape["bottom"],
        ]
    elif kind == "quad":
        hit_type = int(shape.get("type_override", 3))
        values[7:15] = [shape[f"{axis}{index}"]
                        for index in range(4) for axis in ("x", "y")]
    else:
        raise RuntimeError(f"unsupported geometry kind: {kind}")
    return [hit_type, spec["point"]["x"], spec["point"]["y"], *values]


def run_geometry(wasmtime: Path, wasm: Path, specs: list[dict]) -> bool:
    expected = {
        "circle_inside": True,
        "circle_boundary": True,
        "circle_outside": False,
        "rect_left_top_inclusive": True,
        "rect_right_bottom_exclusive": False,
        "quad_inside": True,
        "quad_outside": False,
        "quad_winding_clockwise": True,
        "quad_winding_counterclockwise": True,
        "invalid_type": False,
    }
    ok = True
    for spec in specs:
        actual = bool(int(invoke(
            wasmtime, wasm, "krkr2_hit_test_run", geometry_args(spec)
        )))
        wanted = expected[spec["id"]]
        status = "ok" if actual == wanted else "mismatch"
        print_result("geometry_hit_test", spec["id"], status, actual, wanted)
        ok = ok and status == "ok"
    return ok


def padded(values: list, size: int) -> list:
    if len(values) > size:
        raise RuntimeError(f"flattened CLI ABI accepts at most {size} values")
    return [*values, *([0.0] * (size - len(values)))]


def run_bezier(wasmtime: Path, wasm: Path, specs: list[dict]) -> bool:
    ok = True
    for spec in specs:
        xs = spec["curve"]["x"]
        ys = spec["curve"]["y"]
        if len(xs) != len(ys):
            raise RuntimeError(f"{spec['id']}: x/y size mismatch is not an oracle case")
        actual = invoke(
            wasmtime,
            wasm,
            "run_bezier_curve_direct",
            [len(xs), spec["t"], *padded(xs, 7), *padded(ys, 7)],
        )
        wanted = spec["expected"]
        status = "ok" if actual == wanted else "mismatch"
        print_result("bezier_curve", spec["id"], status, actual, wanted)
        ok = ok and status == "ok"
    return ok


def run_local_transform(
    wasmtime: Path, wasm: Path, specs: list[dict]
) -> bool:
    ok = True
    for spec in specs:
        order = spec["transformOrder"]
        prefix = [
            *spec["affine_in"],
            1 if spec["flipX"] else 0,
            1 if spec["flipY"] else 0,
            spec["angle"],
            spec["scaleX"],
            spec["scaleY"],
            spec["slantX"],
            spec["slantY"],
            *order,
        ]
        actual = [
            invoke(
                wasmtime,
                wasm,
                "run_local_transform_direct",
                [*prefix, output_index],
            )
            for output_index in range(6)
        ]
        wanted = spec["expected"]
        status = "ok" if actual == wanted else "mismatch"
        print_result("local_transform", spec["id"], status, actual, wanted)
        ok = ok and status == "ok"
    return ok


def pack_bytes64(values: list[int], offset: int) -> int:
    value = 0
    for index, byte in enumerate(values[offset:offset + 8]):
        value |= int(byte) << (index * 8)
    # Wasmtime's CLI accepts an i64 argument.  Preserve the same 64 raw bits
    # when the top byte would otherwise produce an out-of-range positive int.
    return value - (1 << 64) if value >= (1 << 63) else value


def run_psb_rl_decompress(
    wasmtime: Path, wasm: Path, specs: list[dict]
) -> bool:
    ok = True
    for spec in specs:
        compressed = spec["compressed"]
        if len(compressed) > 16:
            raise RuntimeError(
                f"{spec['id']}: flattened PSB CLI ABI accepts at most 16 bytes"
            )
        prefix = [
            len(compressed),
            spec["element_count"],
            spec["align"],
            pack_bytes64(compressed, 0),
            pack_bytes64(compressed, 8),
        ]
        output_size = int(invoke(
            wasmtime,
            wasm,
            "run_psb_rl_decompress_direct",
            [*prefix, -1],
        ))
        actual = [
            int(invoke(
                wasmtime,
                wasm,
                "run_psb_rl_decompress_direct",
                [*prefix, output_index],
            ))
            for output_index in range(output_size)
        ]
        wanted = spec["expected"]
        status = "ok" if actual == wanted else "mismatch"
        print_result(
            "psb_rl_decompress", spec["id"], status, actual, wanted)
        ok = ok and status == "ok"
    return ok


def run_position(wasmtime: Path, wasm: Path, specs: list[dict]) -> bool:
    ok = True
    for spec in specs:
        rotation = spec["rotation_curve"]
        if any(rotation[key] for key in ("x", "y", "t", "segments")):
            raise RuntimeError(
                f"{spec['id']}: CLI scalar ABI intentionally supports only "
                "the current empty-rotation oracle cases"
            )
        easing_x = spec["easing_curve"]["x"]
        easing_y = spec["easing_curve"]["y"]
        if len(easing_x) != len(easing_y):
            raise RuntimeError(f"{spec['id']}: easing x/y size mismatch")
        prefix = [
            len(easing_x),
            *padded(easing_x, 4),
            *padded(easing_y, 4),
            *spec["src_pos"],
            *spec["dst_pos"],
            spec["coord_mode"],
            spec["t"],
        ]
        actual = [
            invoke(
                wasmtime,
                wasm,
                "run_position_interp_direct",
                [*prefix, output_index],
            )
            for output_index in range(3)
        ]
        wanted = spec["expected"]
        status = "ok" if actual == wanted else "mismatch"
        print_result("position_interp", spec["id"], status, actual, wanted)
        ok = ok and status == "ok"
    return ok


def print_result(family: str, case_id: str, status: str, actual, expected) -> None:
    print(json.dumps({
        "family": family,
        "case_id": case_id,
        "status": status,
        "actual": actual,
        "expected": expected,
        "runner": "port-wasmtime-cli-scalar-export",
    }, ensure_ascii=True))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wasmtime", default=find_wasmtime(), type=Path)
    parser.add_argument(
        "--family", action="append", choices=FAMILIES, required=True
    )
    parser.add_argument(
        "--wasm-dir",
        type=Path,
        default=REPO_ROOT / "tests" / "differential" / "wasmtime",
    )
    parser.add_argument(
        "--spec-root",
        type=Path,
        default=REPO_ROOT / "tests" / "differential" / "specs",
    )
    args = parser.parse_args()

    if args.wasmtime is None:
        raise RuntimeError(
            "Wasmtime CLI not found; install Wasmtime 44+ or pass --wasmtime"
        )
    require_guest_debug_wasmtime(args.wasmtime)

    runners = {
        "geometry_hit_test": run_geometry,
        "psb_rl_decompress": run_psb_rl_decompress,
        "bezier_curve": run_bezier,
        "local_transform": run_local_transform,
        "position_interp": run_position,
    }
    ok = True
    for family in args.family:
        specs = load_specs(args.spec_root, family)
        if not specs:
            raise RuntimeError(f"no specs for {family}")
        wasm = args.wasm_dir / f"{family}.wasm"
        ok = runners[family](args.wasmtime, wasm, specs) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
