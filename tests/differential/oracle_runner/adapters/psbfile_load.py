"""Android libkrkr2 oracle adapter for PSBFile raw and MDF load paths.

The input is supplied by the operator.  This adapter deliberately does not
construct or check in a damaged PSB/MDF fixture: it only forwards an existing
``PSB\0`` or ``mdf\0`` file through PSBFile's native implementation.
"""

from __future__ import annotations

import struct
from pathlib import Path


PSBFILE_LOAD_VARIANT_OFFSET = 0x598268
PSBFILE_LOAD_STORAGE_OFFSET = 0x598538
PSBRAWOWNER_REFRESH_OFFSET = 0x598960
TTSTR_FROM_ASCII_OFFSET = 0xA13878
EMOTE_PSB_DECRYPT_OFFSET = 0x6863CC

MDF_MAGIC = b"mdf\0"
PSB_MAGIC = b"PSB\0"
OWNER_SIZE = 0x68


def _input_info(input_path: Path) -> tuple[bytes, str, int]:
    data = input_path.read_bytes()
    if len(data) >= 11 and data[:4] == MDF_MAGIC:
        return data, "mdf", struct.unpack_from("<I", data, 4)[0]
    if len(data) >= 0x40 and data[:4] == PSB_MAGIC:
        return data, "psb", len(data)
    raise ValueError(f"not a PSB or MDF input: {input_path}")


def _load_octet(engine, data: bytes) -> tuple[bool, int]:
    data_addr = engine.heap.write(data, align=8)
    # Android tTJSVariantOctet layout consumed by sub_598268:
    #   +0 uint32 length, +8 const uint8_t *data.
    octet_addr = engine.heap.write(
        struct.pack("<I4xQ", len(data), data_addr), align=8)
    # tTJSVariant: +0 payload pointer, +16 type tag (3 = tvtOctet).
    variant_addr = engine.heap.write(
        struct.pack("<Q8xI4x", octet_addr, 3), align=8)
    holder_addr = engine.heap.write(b"\0" * 8, align=8)
    loaded = engine.call(
        engine.offset(PSBFILE_LOAD_VARIANT_OFFSET),
        ints=(holder_addr, variant_addr),
        ret="bool",
    )
    return loaded, holder_addr


def _inspect_owner(engine, *, input_path: Path, entry: str, input_size: int,
                   input_format: str, declared_size: int, loaded: bool,
                   holder_addr: int) -> dict:
    owner_addr = struct.unpack(
        "<Q", engine.ql.mem.read(holder_addr, 8))[0]
    if not loaded or owner_addr == 0:
        return {
            "input": str(input_path),
            "entry": entry,
            "input_format": input_format,
            "status": "load-failed",
            "loaded": bool(loaded),
            "owner": owner_addr,
        }

    owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
    refcount = struct.unpack_from("<I", owner, 0)[0]
    header_addr = struct.unpack_from("<Q", owner, 8)[0]
    raw_addr = struct.unpack_from("<Q", owner, 88)[0]
    raw_size = struct.unpack_from("<Q", owner, 96)[0]
    raw_magic = engine.ql.mem.read(raw_addr, 4)
    strict_refresh = engine.call(
        engine.offset(PSBRAWOWNER_REFRESH_OFFSET),
        ints=(owner_addr, 1),
        ret="bool",
    )

    ok = (
        refcount == 1
        and header_addr == owner_addr + 16
        and raw_magic == PSB_MAGIC
        and raw_size == declared_size
        and strict_refresh
    )
    return {
        "input": str(input_path),
        "entry": entry,
        "input_format": input_format,
        "status": "ok" if ok else "mismatch",
        "loaded": bool(loaded),
        "input_size": input_size,
        "declared_size": declared_size,
        "owner_size": raw_size,
        "owner_refcount": refcount,
        "header_is_inline": header_addr == owner_addr + 16,
        "raw_magic": raw_magic.decode("ascii", "replace"),
        "strict_refresh": bool(strict_refresh),
    }


def run_case(engine, input_path: Path) -> dict:
    data, input_format, declared_size = _input_info(input_path)
    engine.reset_heap()
    loaded, holder_addr = _load_octet(engine, data)
    return _inspect_owner(
        engine, input_path=input_path, entry="octet", input_size=len(data),
        input_format=input_format, declared_size=declared_size, loaded=loaded,
        holder_addr=holder_addr)


def _decrypt_bytes(data: bytes, seed: int) -> bytes:
    result = bytearray(data)
    x = 123456789
    y = 362436069
    z = 521288629
    w = seed & 0xFFFFFFFF
    word = 0
    for index in range(len(result)):
        if word == 0:
            temp = (x ^ ((x << 11) & 0xFFFFFFFF)) & 0xFFFFFFFF
            x, y, z = y, z, w
            w = (w ^ (w >> 19) ^ temp ^ (temp >> 8)) & 0xFFFFFFFF
            word = w
        result[index] ^= word & 0xFF
        word >>= 8
    return bytes(result)


def run_decrypt_case(engine, input_path: Path, seed: int) -> dict:
    data, input_format, _ = _input_info(input_path)
    if input_format != "psb":
        raise ValueError("decrypt seed mode requires a raw PSB input")

    engine.reset_heap()
    loaded, holder_addr = _load_octet(engine, data)
    owner_addr = struct.unpack(
        "<Q", engine.ql.mem.read(holder_addr, 8))[0]
    if not loaded or owner_addr == 0:
        return {
            "input": str(input_path),
            "entry": "octet+filter",
            "input_format": input_format,
            "status": "load-failed",
            "loaded": bool(loaded),
        }

    owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
    encrypt_start = struct.unpack_from("<Q", owner, 24)[0]
    encrypt_end = struct.unpack_from("<Q", owner, 56)[0]
    entries_addr = struct.unpack_from("<Q", owner, 80)[0]
    encrypted = engine.ql.mem.read(
        encrypt_start, encrypt_end - encrypt_start)

    seed_value = engine.heap.write(
        struct.pack("<Q", seed & 0xFFFFFFFF), align=8)
    closure_slot = engine.heap.write(
        struct.pack("<Q", seed_value), align=8)
    engine.call(
        engine.offset(EMOTE_PSB_DECRYPT_OFFSET),
        ints=(closure_slot, owner_addr),
        ret="ptr",
    )
    decrypted = engine.ql.mem.read(
        encrypt_start, encrypt_end - encrypt_start)
    expected = _decrypt_bytes(encrypted, seed)
    strict_refresh = engine.call(
        engine.offset(PSBRAWOWNER_REFRESH_OFFSET),
        ints=(owner_addr, 1),
        ret="bool",
    )
    root_type = engine.ql.mem.read(entries_addr, 1)[0]
    bytes_equal = decrypted == expected
    ok = bytes_equal and strict_refresh and root_type == 0x21
    return {
        "input": str(input_path),
        "entry": "octet+filter",
        "input_format": input_format,
        "status": "ok" if ok else "mismatch",
        "loaded": bool(loaded),
        "seed": seed & 0xFFFFFFFF,
        "encrypt_size": len(encrypted),
        "bytes_equal": bytes_equal,
        "strict_refresh": bool(strict_refresh),
        "root_type": root_type,
    }


def run_storage_case(engine, input_path: Path, remote_path: str) -> dict:
    data, input_format, declared_size = _input_info(input_path)
    engine.reset_heap()
    path_cstr = engine.heap.write(remote_path.encode("ascii") + b"\0", align=8)
    string_payload = engine.call(
        engine.offset(TTSTR_FROM_ASCII_OFFSET),
        ints=(path_cstr,), ret="ptr")
    string_slot = engine.heap.write(
        struct.pack("<Q", string_payload), align=8)
    holder_addr = engine.heap.write(b"\0" * 8, align=8)
    empty_filter = engine.heap.write(b"\0" * 32, align=8)

    loaded = engine.call(
        engine.offset(PSBFILE_LOAD_STORAGE_OFFSET),
        ints=(holder_addr, string_slot, empty_filter),
        ret="bool",
    )
    return _inspect_owner(
        engine, input_path=input_path, entry="storage", input_size=len(data),
        input_format=input_format, declared_size=declared_size, loaded=loaded,
        holder_addr=holder_addr)
