"""AAPCS64 calling convention helpers for AArch64.

Rules implemented:
  - integer/pointer args -> x0..x7 in order, overflow spilled 8B-aligned on stack
  - float/double args    -> d0..d7 (v0..v7), overflow spilled 8B-aligned on stack
  - `const T&` / `T&` / arrays passed as pointer via x-register
  - return int/pointer in x0 (bool = x0 & 1), double in d0, void ignored
  - SP must be 16B-aligned at call boundary
  - HFA/HVA composite rules ignored (none of our targets use them)
  - No red zone on AArch64 Linux

This module is engine-agnostic: callers inject a `reg_writer(name, value)`
and `mem_writer(addr, data)`, plus a `sp_getter() -> int` / `sp_setter(int)`
pair for stack-spilled overflow. Qiling callers supply Qiling-flavoured
lambdas; PANDA callers supply pypanda-flavoured lambdas. Same logic either
way.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Any, Callable, Iterable, Literal

ReturnKind = Literal["int", "uint", "bool", "ptr", "double", "void"]


@dataclass
class CallSpec:
    ints: tuple[int, ...] = ()
    doubles: tuple[float, ...] = ()
    ret: ReturnKind = "int"


# AArch64 register names exposed lowercase by both Qiling and pypanda.
_X_REGS = [f"x{i}" for i in range(8)]
_D_REGS = [f"d{i}" for i in range(8)]


def pack_args(
    reg_writer: Callable[[str, int], None],
    mem_writer: Callable[[int, bytes], None],
    sp_getter: Callable[[], int],
    sp_setter: Callable[[int], None],
    ints: Iterable[int],
    doubles: Iterable[float],
) -> None:
    """Load ints into x0..x7 and doubles into d0..d7; spill overflow on stack.

    Stack layout for overflow (AAPCS64): args pushed in reverse order so the
    first stack arg is at [sp], next at [sp+8], etc. Integer overflow spilled
    first, then double overflow, each 8-byte slot. Final SP is 16B-aligned.
    """
    int_list = list(ints)
    dbl_list = list(doubles)

    int_in_regs = int_list[:8]
    int_on_stack = int_list[8:]
    dbl_in_regs = dbl_list[:8]
    dbl_on_stack = dbl_list[8:]

    for reg, val in zip(_X_REGS, int_in_regs):
        reg_writer(reg, val & 0xFFFFFFFFFFFFFFFF)
    for reg, val in zip(_D_REGS, dbl_in_regs):
        bits = struct.unpack("<Q", struct.pack("<d", float(val)))[0]
        reg_writer(reg, bits)

    if int_on_stack or dbl_on_stack:
        parts: list[bytes] = []
        for v in int_on_stack:
            parts.append(struct.pack("<q", v & 0xFFFFFFFFFFFFFFFF))
        for v in dbl_on_stack:
            parts.append(struct.pack("<d", float(v)))
        payload = b"".join(parts)
        if len(payload) % 16:
            payload += b"\x00" * (16 - (len(payload) % 16))
        new_sp = sp_getter() - len(payload)
        mem_writer(new_sp, payload)
        sp_setter(new_sp)


def read_return(reg_reader: Callable[[str], int], kind: ReturnKind) -> Any:
    if kind == "void":
        return None
    if kind == "double":
        bits = reg_reader("d0") & 0xFFFFFFFFFFFFFFFF
        return struct.unpack("<d", struct.pack("<Q", bits))[0]
    x0 = reg_reader("x0") & 0xFFFFFFFFFFFFFFFF
    if kind == "bool":
        return bool(x0 & 1)
    if kind == "int":
        if x0 & (1 << 63):
            return x0 - (1 << 64)
        return x0
    return x0  # uint, ptr
