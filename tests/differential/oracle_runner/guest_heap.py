"""Simple bump allocator over a mapped Qiling region.

One 16 MB region is reserved at a fixed VA. The allocator hands out
8-byte-aligned chunks. `reset()` rewinds the bump pointer without unmapping,
so the engine can be reused across many calls cheaply.
"""

from __future__ import annotations


DEFAULT_BASE = 0x5000_0000
DEFAULT_SIZE = 16 * 1024 * 1024


class GuestHeap:
    def __init__(self, ql, base: int = DEFAULT_BASE, size: int = DEFAULT_SIZE):
        self.ql = ql
        self.base = base
        self.size = size
        self.cur = base
        ql.mem.map(base, size, info="oracle.heap")

    def reset(self) -> None:
        self.cur = self.base

    def alloc(self, nbytes: int, align: int = 8) -> int:
        self.cur = (self.cur + align - 1) & ~(align - 1)
        addr = self.cur
        self.cur += nbytes
        if self.cur - self.base > self.size:
            raise MemoryError(
                f"GuestHeap exhausted: want {nbytes} bytes at {addr:#x}, "
                f"region ends at {self.base + self.size:#x}"
            )
        return addr

    def write(self, data: bytes, align: int = 8) -> int:
        addr = self.alloc(len(data), align)
        self.ql.mem.write(addr, data)
        return addr
