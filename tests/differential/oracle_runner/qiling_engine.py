"""Qiling-backed oracle engine.

Loads libkrkr2.so under a real Android ARM64 rootfs, resolves the PIE
load base, and exposes `call(addr, ...)` that runs a single native function
by pointing LR at a guard address and telling Qiling to stop there.
"""

from __future__ import annotations

import math
import os
import struct
from pathlib import Path
from typing import Any, Callable, Iterable

from . import arm64_abi
from .guest_heap import GuestHeap


# libkrkr2.so PLT stub addresses (found via IDA `lookup_funcs`). Hooking these
# lets us short-circuit libm calls with Python math — libm.so is not fully
# initialised under Qiling's minimal loader, so its PLT resolution faults.
PLT_STUBS: dict[int, Callable[[float], float]] = {
    0x411390: math.sin,  # .sin
    0x4091E0: math.cos,  # .cos
}


DEFAULT_SO_REL = "reference/libkrkr2/libkrkr2.so"
ENV_SO = "KRKR2_SO_PATH"
ENV_ROOTFS = "KRKR2_ROOTFS"

# Address we plant in LR before each call; Qiling stops when PC hits it.
GUARD_ADDR = 0x9000_0000


def _default_so_path() -> Path:
    env = os.environ.get(ENV_SO)
    if env:
        return Path(env)
    # tests/differential/oracle_runner/qiling_engine.py -> repo root
    repo_root = Path(__file__).resolve().parents[3]
    return repo_root / DEFAULT_SO_REL


def _default_rootfs() -> Path:
    env = os.environ.get(ENV_ROOTFS)
    if env:
        return Path(env)
    raise RuntimeError(
        f"Android ARM64 rootfs required; set {ENV_ROOTFS} or pass --rootfs. "
        "See tests/differential/oracle_runner/README.md for setup."
    )


class OracleEngine:
    """Long-lived wrapper around one Qiling instance loading libkrkr2.so."""

    def __init__(self, so_path: Path | str | None = None, rootfs: Path | str | None = None):
        from qiling import Qiling  # lazy: qiling is heavy
        from qiling.const import QL_ARCH, QL_OS, QL_VERBOSE

        self.so_path = Path(so_path) if so_path else _default_so_path()
        self.rootfs = Path(rootfs) if rootfs else _default_rootfs()
        if not self.so_path.is_file():
            raise FileNotFoundError(f"libkrkr2.so not found at {self.so_path}")
        if not self.rootfs.is_dir():
            raise FileNotFoundError(f"rootfs not a directory: {self.rootfs}")

        verbose = QL_VERBOSE.DEFAULT if os.environ.get("KRKR2_ORACLE_VERBOSE") else QL_VERBOSE.OFF
        self.ql = Qiling(
            [str(self.so_path)],
            str(self.rootfs),
            archtype=QL_ARCH.ARM64,
            ostype=QL_OS.LINUX,
            verbose=verbose,
        )
        self.load_base = self._resolve_load_base()

        # Map a guard page at GUARD_ADDR so LR return is a controlled stop.
        try:
            self.ql.mem.map(GUARD_ADDR & ~0xFFF, 0x1000, info="oracle.guard")
        except Exception:
            pass  # may already be mapped; that's fine
        self.heap = GuestHeap(self.ql)
        self._install_plt_hooks()

    def _install_plt_hooks(self) -> None:
        """Trap PLT stubs for libm symbols; libm.so isn't initialised."""
        for offset, fn in PLT_STUBS.items():
            addr = self.load_base + offset
            self.ql.hook_address(
                lambda ql, f=fn: self._run_libm_unary(ql, f),
                addr,
            )

    @staticmethod
    def _run_libm_unary(ql, py_fn: Callable[[float], float]) -> None:
        bits = ql.arch.regs.read("d0") & 0xFFFFFFFFFFFFFFFF
        x = struct.unpack("<d", struct.pack("<Q", bits))[0]
        y = py_fn(x)
        out = struct.unpack("<Q", struct.pack("<d", y))[0]
        ql.arch.regs.write("d0", out)
        # Emulate BL return: jump to LR.
        ql.arch.regs.write("pc", ql.arch.regs.read("lr"))

    def _resolve_load_base(self) -> int:
        # qiling.loader.images is a list of objects with .path + .base
        for img in self.ql.loader.images:
            path = getattr(img, "path", "") or ""
            if path.endswith("libkrkr2.so") or "libkrkr2" in path:
                return int(getattr(img, "base"))
        raise RuntimeError(
            "libkrkr2.so not found among Qiling loaded images: "
            f"{[getattr(i, 'path', '?') for i in self.ql.loader.images]}"
        )

    def offset(self, off: int) -> int:
        """Return absolute guest VA of a given libkrkr2.so file offset."""
        return self.load_base + off

    def call(
        self,
        addr: int,
        *,
        ints: Iterable[int] = (),
        doubles: Iterable[float] = (),
        ret: arm64_abi.ReturnKind = "int",
        timeout_ns: int = 5_000_000_000,
    ) -> Any:
        ql = self.ql
        # Align SP to 16B boundary for the call.
        sp = ql.arch.regs.read("sp") & ~0xF
        ql.arch.regs.write("sp", sp)
        arm64_abi.pack_args(ql, ints, doubles)
        ql.arch.regs.write("lr", GUARD_ADDR)
        ql.arch.regs.write("pc", addr)
        # Emulate until PC hits our guard. Timeout guards against infinite loops.
        ql.emu_start(begin=addr, end=GUARD_ADDR, timeout=timeout_ns)
        return arm64_abi.read_return(ql, ret)

    def reset_heap(self) -> None:
        self.heap.reset()
