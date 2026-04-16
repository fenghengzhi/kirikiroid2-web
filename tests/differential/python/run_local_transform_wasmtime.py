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


def read_doubles_at(base: int, ptr: int, count: int) -> list[float]:
    arr = (ctypes.c_double * count).from_address(base + ptr)
    return list(arr)


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
    affine_in_ptr = exports["get_affine_in_ptr"](store)
    affine_out_ptr = exports["get_affine_out_ptr"](store)
    run_fn = exports["run_local_transform"]

    failed = False
    for spec in specs:
        case_id = spec["id"]
        affine_in = spec["affine_in"]
        flip_x = 1 if spec["flipX"] else 0
        flip_y = 1 if spec["flipY"] else 0
        angle = spec["angle"]
        scale_x = spec["scaleX"]
        scale_y = spec["scaleY"]
        slant_x = spec["slantX"]
        slant_y = spec["slantY"]
        order = spec["transformOrder"]
        expected = spec["expected"]

        base = mem_base(store, memory)
        write_doubles_at(base, affine_in_ptr, affine_in)

        run_fn(store, flip_x, flip_y, angle, scale_x, scale_y,
               slant_x, slant_y, order[0], order[1], order[2], order[3])

        base = mem_base(store, memory)
        actual = read_doubles_at(base, affine_out_ptr, 6)

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
                f"wasm={actual} expected={expected}",
                file=sys.stderr,
            )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
