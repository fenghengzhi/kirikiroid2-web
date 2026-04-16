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


def write_bytes_at(base: int, ptr: int, values: list[int]):
    data = bytes(values)
    ctypes.memmove(base + ptr, data, len(data))


def read_bytes_at(base: int, ptr: int, count: int) -> list[int]:
    arr = (ctypes.c_uint8 * count).from_address(base + ptr)
    return list(arr)


def read_int32_at(base: int, ptr: int) -> int:
    return ctypes.c_int32.from_address(base + ptr).value


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
    compressed_ptr = exports["get_compressed_ptr"](store)
    decompressed_ptr = exports["get_decompressed_ptr"](store)
    decompressed_size_ptr = exports["get_decompressed_size_ptr"](store)
    run_fn = exports["run_psb_rl_decompress"]

    failed = False
    for spec in specs:
        case_id = spec["id"]
        compressed = spec["compressed"]
        element_count = spec["element_count"]
        align = spec["align"]
        expected = spec["expected"]

        base = mem_base(store, memory)

        if compressed:
            write_bytes_at(base, compressed_ptr, compressed)

        run_fn(store, len(compressed), element_count, align)

        base = mem_base(store, memory)
        output_size = read_int32_at(base, decompressed_size_ptr)
        actual = read_bytes_at(base, decompressed_ptr, output_size) if output_size > 0 else []

        result = {
            "case_id": case_id,
            "status": "ok",
            "output_size": output_size,
            "runner": "port-wasm",
        }
        print(json.dumps(result, ensure_ascii=True))

        if actual != expected:
            failed = True
            print(
                f"mismatch case_id={case_id} "
                f"align={align} element_count={element_count} "
                f"wasm={actual} expected={expected}",
                file=sys.stderr,
            )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
