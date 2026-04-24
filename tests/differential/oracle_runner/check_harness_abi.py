#!/usr/bin/env python3
"""Validate that oracle libharness.so uses the legacy GNU C++ ABI.

This is intentionally lightweight and self-contained so CI can run it on
arm64 Linux without relying on host readelf/llvm-readelf packages.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

EM_AARCH64 = 183


class Elf:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if self.data[:4] != b"\x7fELF":
            raise ValueError(f"{path}: not an ELF file")
        if self.data[4] != 2 or self.data[5] != 1:
            raise ValueError(f"{path}: expected ELF64 little-endian")
        self.machine = struct.unpack_from("<H", self.data, 18)[0]
        self._sections = self._read_sections()
        self.needed = self._read_needed()
        self.dyn_symbols = self._read_symbols((".dynsym",))
        self.symbols = self.dyn_symbols | self._read_symbols((".symtab",))

    def _read_sections(self) -> list[dict]:
        e_shoff = struct.unpack_from("<Q", self.data, 40)[0]
        e_shentsize = struct.unpack_from("<H", self.data, 58)[0]
        e_shnum = struct.unpack_from("<H", self.data, 60)[0]
        e_shstrndx = struct.unpack_from("<H", self.data, 62)[0]
        if e_shoff == 0 or e_shnum == 0:
            return []
        shstr_base = e_shoff + e_shstrndx * e_shentsize
        shstr_off = struct.unpack_from("<Q", self.data, shstr_base + 24)[0]
        sections = []
        for idx in range(e_shnum):
            off = e_shoff + idx * e_shentsize
            name_off, sh_type = struct.unpack_from("<II", self.data, off)
            fields = struct.unpack_from("<QQQQIIQQ", self.data, off + 8)
            _, _, sh_offset, sh_size, sh_link, _, _, sh_entsize = fields
            start = shstr_off + name_off
            end = self.data.find(b"\0", start)
            name = self.data[start:end].decode("utf-8", "replace")
            sections.append({
                "name": name,
                "type": sh_type,
                "offset": sh_offset,
                "size": sh_size,
                "link": sh_link,
                "entsize": sh_entsize,
            })
        return sections

    def _section(self, name: str) -> dict | None:
        return next((s for s in self._sections if s["name"] == name), None)

    def _read_symbols(self, section_names: tuple[str, ...]) -> set[str]:
        out: set[str] = set()
        for sec_name in section_names:
            sym = self._section(sec_name)
            if not sym or not sym["entsize"]:
                continue
            strtab = self._sections[sym["link"]]
            str_off = strtab["offset"]
            for i in range(sym["size"] // sym["entsize"]):
                ent = sym["offset"] + i * sym["entsize"]
                st_name = struct.unpack_from("<I", self.data, ent)[0]
                if st_name == 0:
                    continue
                start = str_off + st_name
                end = self.data.find(b"\0", start)
                out.add(self.data[start:end].decode("utf-8", "replace"))
        return out

    def _read_needed(self) -> list[str]:
        loads = []
        e_phoff = struct.unpack_from("<Q", self.data, 32)[0]
        e_phentsize = struct.unpack_from("<H", self.data, 54)[0]
        e_phnum = struct.unpack_from("<H", self.data, 56)[0]
        dyn = None
        for idx in range(e_phnum):
            off = e_phoff + idx * e_phentsize
            p_type = struct.unpack_from("<I", self.data, off)[0]
            p_offset, p_vaddr, _, p_filesz, p_memsz, _ = struct.unpack_from(
                "<QQQQQQ", self.data, off + 8)
            if p_type == 1:
                loads.append((p_offset, p_vaddr, p_filesz, p_memsz))
            elif p_type == 2:
                dyn = (p_offset, p_filesz)
        if dyn is None:
            return []

        def va_to_off(va: int) -> int | None:
            for p_offset, p_vaddr, p_filesz, _ in loads:
                if p_vaddr <= va < p_vaddr + p_filesz:
                    return p_offset + (va - p_vaddr)
            return None

        strtab = None
        needed_offsets = []
        dyn_off, dyn_size = dyn
        for i in range(dyn_size // 16):
            tag, value = struct.unpack_from("<qQ", self.data, dyn_off + i * 16)
            if tag == 0:
                break
            if tag == 1:
                needed_offsets.append(value)
            elif tag == 5:
                strtab = value
        if strtab is None:
            return []
        strtab_off = va_to_off(strtab)
        if strtab_off is None:
            return []
        out = []
        for name_off in needed_offsets:
            start = strtab_off + name_off
            end = self.data.find(b"\0", start)
            out.append(self.data[start:end].decode("utf-8", "replace"))
        return out

    def contains(self, needle: bytes) -> bool:
        return needle in self.data


def fail(msg: str) -> None:
    print(f"ABI CHECK FAILED: {msg}", file=sys.stderr)
    raise SystemExit(1)


def has_any_symbol(elf: Elf, needles: tuple[str, ...]) -> bool:
    return any(any(n in sym for n in needles) for sym in elf.symbols)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness", required=True, type=Path)
    parser.add_argument("--libkrkr2", required=True, type=Path)
    args = parser.parse_args()

    harness = Elf(args.harness)
    libkrkr2 = Elf(args.libkrkr2)

    if harness.machine != EM_AARCH64:
        fail(f"{harness.path} is not AArch64 (e_machine={harness.machine})")
    if libkrkr2.machine != EM_AARCH64:
        fail(f"{libkrkr2.path} is not AArch64 (e_machine={libkrkr2.machine})")

    if "libstdc++.so" not in libkrkr2.needed:
        fail("libkrkr2.so does not declare NEEDED libstdc++.so")
    if libkrkr2.contains(b"__ndk1") or has_any_symbol(libkrkr2, ("__ndk1",)):
        fail("libkrkr2.so contains libc++ std::__ndk1 symbols")
    if not (
        libkrkr2.contains(b"__gnu_cxx")
        or has_any_symbol(libkrkr2, ("__gnu_cxx", "ERKSs", "St6vector"))
    ):
        fail("libkrkr2.so does not show GNU libstdc++ ABI markers")

    bad_harness_markers = (
        b"__ndk1",
        b"libc++abi",
        b"libc++_shared.so",
        b"Android (12027248",
        b"based on r522817",
    )
    for marker in bad_harness_markers:
        if harness.contains(marker):
            fail(f"libharness.so contains forbidden marker {marker!r}")
    if any(name == "libc++_shared.so" for name in harness.needed):
        fail("libharness.so depends on libc++_shared.so")
    if any("__ndk1" in sym for sym in harness.symbols):
        fail("libharness.so exports or contains std::__ndk1 symbols")
    exported_runtime = {
        "__cxa_throw",
        "__cxa_begin_catch",
        "__cxa_end_catch",
        "_Znwm",
        "_ZdlPv",
        "_ZTISt9exception",
        "_ZTVSt9exception",
    }
    leaked = sorted(exported_runtime & harness.dyn_symbols)
    if leaked:
        fail("libharness.so exports C++ runtime symbols despite "
             f"--exclude-libs: {', '.join(leaked)}")
    if not (
        harness.contains(b"__gnu_cxx")
        or harness.contains(b"_ZNSs")
        or has_any_symbol(harness, ("__gnu_cxx", "_ZNSs", "St6vector"))
    ):
        fail("libharness.so does not show GNU libstdc++/gnustl ABI markers")

    print("ABI check passed:")
    print(f"  harness: {args.harness}")
    print(f"  libkrkr2: {args.libkrkr2}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
