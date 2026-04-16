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
    if not values:
        return
    data = struct.pack(f"<{len(values)}d", *values)
    ctypes.memmove(base + ptr, data, len(data))


def write_int32s_at(base: int, ptr: int, values: list[int]):
    if not values:
        return
    data = struct.pack(f"<{len(values)}i", *values)
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
    ptrs = {
        "easing_x": exports["get_easing_x_ptr"](store),
        "easing_y": exports["get_easing_y_ptr"](store),
        "cp_x": exports["get_cp_x_ptr"](store),
        "cp_y": exports["get_cp_y_ptr"](store),
        "cp_t": exports["get_cp_t_ptr"](store),
        "cp_seg_data": exports["get_cp_seg_data_ptr"](store),
        "cp_seg_sizes": exports["get_cp_seg_sizes_ptr"](store),
        "src_pos": exports["get_src_pos_ptr"](store),
        "dst_pos": exports["get_dst_pos_ptr"](store),
        "out_pos": exports["get_out_pos_ptr"](store),
    }
    run_fn = exports["run_position_interp"]

    failed = False
    for spec in specs:
        case_id = spec["id"]
        easing = spec["easing_curve"]
        rc = spec["rotation_curve"]
        expected = spec["expected"]

        base = mem_base(store, memory)

        # Write easing curve
        easing_x = easing["x"]
        easing_y = easing["y"]
        easing_n = len(easing_x)
        write_doubles_at(base, ptrs["easing_x"], easing_x)
        write_doubles_at(base, ptrs["easing_y"], easing_y)

        # Write control point curve
        cp_x = rc["x"]
        cp_y = rc["y"]
        cp_t = rc["t"]
        cp_n = len(cp_x)
        cp_t_n = len(cp_t)
        write_doubles_at(base, ptrs["cp_x"], cp_x)
        write_doubles_at(base, ptrs["cp_y"], cp_y)
        write_doubles_at(base, ptrs["cp_t"], cp_t)

        # Pack segment data
        segments = rc["segments"]
        seg_data: list[float] = []
        seg_sizes: list[int] = []
        for seg in segments:
            sx = seg["x"]
            sy = seg["y"]
            sp = seg["p"]
            seg_sizes.extend([len(sx), len(sy), len(sp)])
            seg_data.extend(sx)
            seg_data.extend(sy)
            seg_data.extend(sp)
        write_doubles_at(base, ptrs["cp_seg_data"], seg_data)
        write_int32s_at(base, ptrs["cp_seg_sizes"], seg_sizes)

        # Write positions
        write_doubles_at(base, ptrs["src_pos"], spec["src_pos"])
        write_doubles_at(base, ptrs["dst_pos"], spec["dst_pos"])

        # Run
        run_fn(store, easing_n, cp_n, cp_t_n, len(segments),
               spec["coord_mode"], spec["t"])

        # Read output
        base = mem_base(store, memory)
        actual = read_doubles_at(base, ptrs["out_pos"], 3)

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
