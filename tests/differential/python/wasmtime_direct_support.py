#!/usr/bin/env python3
"""Architecture-neutral helpers for standalone Wasmtime test modules."""

from __future__ import annotations

import ctypes
import struct
from pathlib import Path


def load_wasmtime():
    try:
        import wasmtime  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "wasmtime is not installed; run 'python3 -m pip install -r "
            "tests/differential/python/requirements-wasm.txt'"
        ) from exc
    return wasmtime


def make_direct_engine(wasmtime):
    config = wasmtime.Config()
    config.debug_info = False
    config.cranelift_opt_level = "none"
    # Avoid reserving a large host virtual-address range for tiny scalar
    # harnesses.  This also keeps the embedding fallback usable on constrained
    # hosts; the authoritative CLI lane does not depend on these Python knobs.
    for name, value in (
        ("static_memory_maximum_size", 32 * 1024 * 1024),
        ("dynamic_memory_guard_size", 0),
        ("memory_reservation", 0),
        ("memory_guard_size", 0),
        ("memory_reservation_for_growth", 0),
        ("memory_may_move", True),
    ):
        if hasattr(config, name):
            setattr(config, name, value)
    return wasmtime.Engine(config)


def instantiate_standalone_module(wasmtime, wasm_path: Path):
    engine = make_direct_engine(wasmtime)
    module = wasmtime.Module.from_file(engine, str(wasm_path))
    store = wasmtime.Store(engine)
    wasi = wasmtime.WasiConfig()
    wasi.inherit_stdout()
    wasi.inherit_stderr()
    store.set_wasi(wasi)
    linker = wasmtime.Linker(engine)
    linker.define_wasi()
    instance = linker.instantiate(store, module)
    exports = instance.exports(store)

    for init_name in ("__initialize", "_initialize"):
        try:
            exports[init_name](store)
            break
        except KeyError:
            continue
    return store, exports


def memory_base(store, memory) -> int:
    return ctypes.addressof(memory.data_ptr(store).contents)


def write_bytes(base: int, ptr: int, values: list[int]) -> None:
    if values:
        data = bytes(values)
        ctypes.memmove(base + ptr, data, len(data))


def write_doubles(base: int, ptr: int, values: list[float]) -> None:
    if values:
        data = struct.pack(f"<{len(values)}d", *values)
        ctypes.memmove(base + ptr, data, len(data))


def write_int32s(base: int, ptr: int, values: list[int]) -> None:
    if values:
        data = struct.pack(f"<{len(values)}i", *values)
        ctypes.memmove(base + ptr, data, len(data))


def read_bytes(base: int, ptr: int, size: int) -> list[int]:
    return list(ctypes.string_at(base + ptr, size))


def read_doubles(base: int, ptr: int, count: int) -> list[float]:
    return list(struct.unpack(
        f"<{count}d", ctypes.string_at(base + ptr, count * 8)
    ))
