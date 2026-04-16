#!/usr/bin/env python3

import argparse
import ctypes
import json
import struct
import sys
from pathlib import Path


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

    return store, exports


def mem_base(store, memory) -> int:
    return ctypes.addressof(memory.data_ptr(store).contents)


def write_doubles_at(base: int, ptr: int, values: list[float]):
    data = struct.pack(f"<{len(values)}d", *values)
    ctypes.memmove(base + ptr, data, len(data))


def read_double_at(base: int, ptr: int) -> float:
    return ctypes.c_double.from_address(base + ptr).value


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
    store, exports = instantiate_module(wasmtime, args.wasm)

    memory = exports["memory"]
    curve_x_ptr = exports["get_curve_x_ptr"](store)
    curve_y_ptr = exports["get_curve_y_ptr"](store)
    result_ptr = exports["get_result_ptr"](store)
    run_fn = exports["run_bezier_curve"]

    failed = False
    for spec in specs:
        case_id = spec["id"]
        curve_x = spec["curve"]["x"]
        curve_y = spec["curve"]["y"]
        t = spec["t"]
        expected = spec["expected"]

        base = mem_base(store, memory)
        n = len(curve_x)
        if n > 0:
            write_doubles_at(base, curve_x_ptr, curve_x)
        if len(curve_y) > 0:
            write_doubles_at(base, curve_y_ptr, curve_y)

        run_fn(store, n, t)

        base = mem_base(store, memory)
        actual = read_double_at(base, result_ptr)

        result = {
            "case_id": case_id,
            "status": "ok",
            "result": actual,
            "runner": "port-wasm",
        }
        print(json.dumps(result, ensure_ascii=True))

        if actual != expected:
            failed = True
            print(
                f"mismatch case_id={case_id} "
                f"t={t} wasm={actual} expected={expected}",
                file=sys.stderr,
            )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
