#!/usr/bin/env python3
"""Remove selected exports from a WebAssembly binary in place."""

from __future__ import annotations

import sys
from pathlib import Path


def read_u32leb(data: bytes, pos: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while True:
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7f) << shift
        if byte < 0x80:
            return result, pos
        shift += 7


def write_u32leb(value: int) -> bytes:
    out = bytearray()
    while True:
        byte = value & 0x7f
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def strip_exports(wasm: bytes, names: set[str]) -> bytes:
    if wasm[:8] != b"\0asm\x01\0\0\0":
        raise ValueError("not a WebAssembly 1.0 binary")

    out = bytearray(wasm[:8])
    pos = 8
    while pos < len(wasm):
        section_id = wasm[pos]
        pos += 1
        section_size, payload_pos = read_u32leb(wasm, pos)
        payload_end = payload_pos + section_size
        payload = wasm[payload_pos:payload_end]
        pos = payload_end

        if section_id != 7:
            out.append(section_id)
            out += write_u32leb(len(payload))
            out += payload
            continue

        count, entry_pos = read_u32leb(payload, 0)
        kept = []
        for _ in range(count):
            entry_start = entry_pos
            name_len, name_pos = read_u32leb(payload, entry_pos)
            name_end = name_pos + name_len
            name = payload[name_pos:name_end].decode("utf-8")
            kind_pos = name_end
            _kind = payload[kind_pos]
            _index, entry_pos = read_u32leb(payload, kind_pos + 1)
            if name not in names:
                kept.append(payload[entry_start:entry_pos])

        new_payload = bytearray(write_u32leb(len(kept)))
        for entry in kept:
            new_payload += entry
        out.append(section_id)
        out += write_u32leb(len(new_payload))
        out += new_payload

    return bytes(out)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: strip_wasm_export.py <module.wasm> <export>...",
              file=sys.stderr)
        return 2
    path = Path(argv[0])
    names = set(argv[1:])
    path.write_bytes(strip_exports(path.read_bytes(), names))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
