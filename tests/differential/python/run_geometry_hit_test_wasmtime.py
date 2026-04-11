#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path

EXPECTED_HITS = {
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


def load_specs(spec_dir: Path) -> list[dict]:
    return [
        json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(spec_dir.glob("*.json"))
    ]


def load_wasmtime():
    try:
        import wasmtime  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "wasmtime is not installed; run "
            "'python3 -m pip install -r "
            "tests/differential/python/requirements-wasm.txt'"
        ) from exc
    return wasmtime


def instantiate_module(wasmtime, wasm_path: Path):
    engine = wasmtime.Engine()
    module = wasmtime.Module.from_file(engine, str(wasm_path))
    store = wasmtime.Store(engine)
    linker = wasmtime.Linker(engine)
    instance = linker.instantiate(store, module)
    exports = instance.exports(store)

    initialize = None
    for init_name in ("__initialize", "_initialize"):
        try:
            initialize = exports[init_name]
            break
        except Exception:
            continue
    if initialize is not None:
        initialize(store)

    try:
        run_fn = exports["krkr2_hit_test_run"]
    except Exception as exc:
        raise RuntimeError(
            f"krkr2_hit_test_run export not found in {wasm_path}"
        ) from exc

    return store, run_fn


def flatten_case(spec: dict) -> list[float]:
    shape = spec["shape"]
    kind = shape["kind"]
    values = [0.0] * 15
    if kind == "circle":
        hit_type = int(shape.get("type_override", 1))
        values[0] = float(shape["cx"])
        values[1] = float(shape["cy"])
        values[2] = float(shape["r"])
    elif kind == "rect":
        hit_type = int(shape.get("type_override", 2))
        values[3] = float(shape["left"])
        values[4] = float(shape["top"])
        values[5] = float(shape["right"])
        values[6] = float(shape["bottom"])
    elif kind == "quad":
        hit_type = int(shape.get("type_override", 3))
        values[7] = float(shape["x0"])
        values[8] = float(shape["y0"])
        values[9] = float(shape["x1"])
        values[10] = float(shape["y1"])
        values[11] = float(shape["x2"])
        values[12] = float(shape["y2"])
        values[13] = float(shape["x3"])
        values[14] = float(shape["y3"])
    else:
        raise RuntimeError(f"unsupported shape kind: {kind}")

    return [
        hit_type,
        float(spec["point"]["x"]),
        float(spec["point"]["y"]),
        *values,
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--wasm", required=True, type=Path)
    args = parser.parse_args()

    if not args.wasm.exists():
        raise RuntimeError(f"wasm module not found: {args.wasm}")

    specs = load_specs(args.spec_dir)
    if not specs:
        raise RuntimeError(f"no specs found in {args.spec_dir}")

    wasmtime = load_wasmtime()
    store, run_fn = instantiate_module(wasmtime, args.wasm)

    failed = False
    for spec in specs:
        case_id = spec["id"]
        raw_result = run_fn(store, *flatten_case(spec))
        result = {
            "case_id": case_id,
            "status": "ok",
            "hit": bool(raw_result),
            "runner": "port-wasm",
        }
        print(json.dumps(result, ensure_ascii=True))

        expected = EXPECTED_HITS.get(case_id)
        if expected is None:
            failed = True
            print(f"missing expected result for case {case_id}", file=sys.stderr)
            continue

        if result["hit"] != expected:
            failed = True
            print(
                f"mismatch case_id={case_id} kind={spec['shape']['kind']} "
                f"point=({spec['point']['x']}, {spec['point']['y']}) "
                f"wasm={result['hit']} expected={expected}",
                file=sys.stderr,
            )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
