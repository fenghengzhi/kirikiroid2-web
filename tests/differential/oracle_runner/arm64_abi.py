"""AAPCS64 calling convention helpers for Qiling AArch64.

Rules implemented:
  - integer/pointer args -> x0..x7 in order, overflow spilled 8B-aligned on stack
  - float/double args    -> d0..d7 (v0..v7), overflow spilled 8B-aligned on stack
  - `const T&` / `T&` / arrays passed as pointer via x-register
  - return int/pointer in x0 (bool = x0 & 1), double in d0, void ignored
  - SP must be 16B-aligned at call boundary
  - HFA/HVA composite rules ignored (none of our targets use them)
  - No red zone on AArch64 Linux
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Any, Iterable, Literal

ReturnKind = Literal["int", "uint", "bool", "ptr", "double", "void"]


@dataclass
class CallSpec:
    ints: tuple[int, ...] = ()
    doubles: tuple[float, ...] = ()
    ret: ReturnKind = "int"


# AArch64 Qiling exposes registers as lowercase names.
_X_REGS = [f"x{i}" for i in range(8)]
_D_REGS = [f"d{i}" for i in range(8)]


def pack_args(ql, ints: Iterable[int], doubles: Iterable[float]) -> None:
    """Load ints into x0..x7 and doubles into d0..d7; spill overflow on stack.

    Stack layout for overflow (AAPCS64): args pushed in reverse order so the
    first stack arg is at [sp], next at [sp+8], etc. Integer overflow spilled
    first, then double overflow, each 8-byte slot.
    """
    int_list = list(ints)
    dbl_list = list(doubles)

    int_in_regs = int_list[:8]
    int_on_stack = int_list[8:]
    dbl_in_regs = dbl_list[:8]
    dbl_on_stack = dbl_list[8:]

    for reg, val in zip(_X_REGS, int_in_regs):
        ql.arch.regs.write(reg, val & 0xFFFFFFFFFFFFFFFF)
    for reg, val in zip(_D_REGS, dbl_in_regs):
        _write_double(ql, reg, float(val))

    if int_on_stack or dbl_on_stack:
        # Build stack buffer: ints first, then doubles (our convention; target
        # funcs don't mix overflow of both). Keep 16B alignment by padding.
        parts: list[bytes] = []
        for v in int_on_stack:
            parts.append(struct.pack("<q", v & 0xFFFFFFFFFFFFFFFF))
        for v in dbl_on_stack:
            parts.append(struct.pack("<d", float(v)))
        payload = b"".join(parts)
        if len(payload) % 16:
            payload += b"\x00" * (16 - (len(payload) % 16))
        sp = ql.arch.regs.read("sp") - len(payload)
        ql.mem.write(sp, payload)
        ql.arch.regs.write("sp", sp)


def read_return(ql, kind: ReturnKind) -> Any:
    if kind == "void":
        return None
    if kind == "double":
        return _read_double(ql, "d0")
    x0 = ql.arch.regs.read("x0") & 0xFFFFFFFFFFFFFFFF
    if kind == "bool":
        return bool(x0 & 1)
    if kind == "int":
        if x0 & (1 << 63):
            return x0 - (1 << 64)
        return x0
    return x0  # uint, ptr


def _write_double(ql, reg: str, value: float) -> None:
    # Qiling's unicorn-backed register writer accepts int bit-pattern for d*
    bits = struct.unpack("<Q", struct.pack("<d", value))[0]
    ql.arch.regs.write(reg, bits)


def _read_double(ql, reg: str) -> float:
    bits = ql.arch.regs.read(reg) & 0xFFFFFFFFFFFFFFFF
    return struct.unpack("<d", struct.pack("<Q", bits))[0]
