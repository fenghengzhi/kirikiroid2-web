"""Android libkrkr2 oracle adapter for PSBFile raw and MDF load paths.

The input is supplied by the operator.  This adapter deliberately does not
construct or check in a damaged PSB/MDF fixture: it only forwards an existing
``PSB\0`` or ``mdf\0`` file through PSBFile's native implementation.
"""

from __future__ import annotations

import json
import struct
import time
from pathlib import Path


PSBFILE_LOAD_VARIANT_OFFSET = 0x598268
PSBFILE_LOAD_STORAGE_OFFSET = 0x598538
PSBRAWOWNER_REFRESH_OFFSET = 0x598960
PSBRAWNODE_GET_DOUBLE_OFFSET = 0x5992E8
PSBRAWNODE_GET_INT_OFFSET = 0x599438
PSBRAWOWNER_HOLDER_RELEASE_OFFSET = 0x695CBC
PSBMEDIA_CHECK_STORAGE_OFFSET = 0x5998C4
PSBMEDIA_OPEN_OFFSET = 0x59993C
PSBMEDIA_GET_LIST_AT_OFFSET = 0x5999F4
TVP_ADD_AUTO_PATH_OFFSET = 0x8EB4B4
TVP_MEMORY_STREAM_DELETING_DTOR_OFFSET = 0x8F7D68
TJS_VARIANT_STRING_RELEASE_OFFSET = 0xA13274
TJS_VARIANT_DTOR_OFFSET = 0xA0F778
TTSTR_FROM_ASCII_OFFSET = 0xA13878
EMOTE_PSB_DECRYPT_OFFSET = 0x6863CC

PSBMEDIA_SINGLETON_SLOT_OFFSET = 0x1AB50E8
PSBFILE_CLASS_SLOT_OFFSET = 0x1AB5110
TVP_SCRIPT_ENGINE_SLOT_OFFSET = 0x1AE2FD0

MDF_MAGIC = b"mdf\0"
PSB_MAGIC = b"PSB\0"
OWNER_SIZE = 0x68
PSBMEDIA_SIZE = 0x28
MEMORY_STREAM_SIZE = 0x20
TJS_VARIANT_SIZE = 0x18
TJS_VARIANT_INTEGER_TYPE = 4


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


def run_integer_boundary_case(
    engine,
    *,
    input_path: Path,
    remote_path: str,
    node_offset: int,
    expected_node_bytes: bytes,
    tjs_expression: str,
    expected_value: int,
) -> dict:
    """Observe one natural tag-0x09 value through raw and TJS paths.

    The public TJS expression exercises PSBFile factory/load/root, Dictionary
    PropGet, Array PropGetByNum and PSBValueDispatch::assign.  A second load
    builds only the two-pointer Android PSBRawNode view needed to invoke the
    original raw GetInt/GetDouble entries at the same pinned file offset.
    """
    data, input_format, declared_size = _input_info(input_path)
    if input_format != "psb":
        raise ValueError("integer boundary mode requires a raw PSB input")
    if node_offset < 0 or node_offset + len(expected_node_bytes) > len(data):
        raise ValueError("integer boundary node lies outside the input")
    if len(expected_node_bytes) != 6 or expected_node_bytes[0] != 0x09:
        raise ValueError(
            "integer boundary node must pin one complete 6-byte tag-0x09 "
            "payload")
    if data[node_offset:node_offset + len(expected_node_bytes)] \
            != expected_node_bytes:
        raise ValueError("integer boundary input bytes do not match the pin")
    remote_path.encode("ascii")
    tjs_expression.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": "tag09-integer-boundary",
        "input_format": input_format,
        "input_size": len(data),
        "declared_size": declared_size,
        "node_offset": node_offset,
        "expected_node_bytes": expected_node_bytes.hex(),
        "expected_value": expected_value,
        "script_engine_ready": script_engine != 0,
    }
    if script_engine == 0:
        result["status"] = "setup-failed"
        return result

    tjs_initialized = False
    globals_created = False
    variant_addr = 0
    raw_holder_addr = 0
    try:
        engine.tjs_init()
        tjs_initialized = True
        singleton_addr, class_object = _ensure_psbfile_registered(engine)
        result.update({
            "singleton_ready": singleton_addr != 0,
            "class_object_ready": class_object != 0,
        })
        if singleton_addr == 0 or class_object == 0:
            result["status"] = "setup-failed"
            return result

        script = (
            "var oracle_psb_tag09_file = new PSBFile("
            f"{json.dumps(remote_path)});\n"
            "var oracle_psb_tag09_value = "
            f"{tjs_expression};"
        )
        # The first statement may publish the PSBFile global before a later
        # property lookup throws, so enable best-effort cleanup before Exec.
        globals_created = True
        engine.tjs_exec(script)
        variant_addr = engine.tjs_global("oracle_psb_tag09_value")
        variant = engine.ql.mem.read(variant_addr, TJS_VARIANT_SIZE)
        variant_value = struct.unpack_from("<q", variant, 0)[0]
        variant_type = struct.unpack_from("<I", variant, 16)[0]

        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result["raw_loaded"] = bool(loaded)
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result

        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        if node_offset + len(expected_node_bytes) > raw_size:
            raise RuntimeError("loaded owner is shorter than the pinned node")
        node_addr = raw_addr + node_offset
        node_bytes = engine.ql.mem.read(
            node_addr, len(expected_node_bytes))

        # The Android PSBRawNode data view is two pointers: owner, then raw
        # node.  This surrogate borrows both while raw_holder_addr keeps the
        # owner alive; the getters consume it synchronously, and finally
        # releases the separate owning holder.
        raw_node_addr = engine.heap.write(
            struct.pack("<QQ", owner_addr, node_addr), align=8)
        raw_get_int_x0 = engine.call(
            engine.offset(PSBRAWNODE_GET_INT_OFFSET),
            ints=(raw_node_addr,), ret="ptr",
        )
        raw_get_int_w32 = struct.unpack(
            "<i", struct.pack("<I", raw_get_int_x0 & 0xFFFFFFFF))[0]
        raw_get_double = engine.call(
            engine.offset(PSBRAWNODE_GET_DOUBLE_OFFSET),
            ints=(raw_node_addr,), ret="double",
        )
        expected_w32 = struct.unpack(
            "<i", struct.pack("<I", expected_value & 0xFFFFFFFF))[0]

        result.update({
            "variant_type": variant_type,
            "variant_value": variant_value,
            "raw_node_bytes": node_bytes.hex(),
            "raw_get_int_x0": raw_get_int_x0,
            "raw_get_int_w32": raw_get_int_w32,
            "expected_w32": expected_w32,
            "raw_get_double": raw_get_double,
        })
        ok = (
            expected_value > 0x7FFFFFFF
            and variant_type == TJS_VARIANT_INTEGER_TYPE
            and variant_value == expected_value
            and node_bytes == expected_node_bytes
            and raw_get_int_x0 == expected_value
            and raw_get_int_w32 == expected_w32
            and raw_get_double == float(expected_value)
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        if raw_holder_addr:
            try:
                # sub_695CBC is the emitted raw-node owner-release sequence:
                # decrement refcount, run 0x598B3C and operator delete at zero.
                engine.call(
                    engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                    ints=(raw_holder_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"raw owner: {exc!r}")
        if variant_addr:
            try:
                # TJS_RESET only rewinds the harness allocator.  Destroy the
                # actual output Variant first so mismatch types cannot retain
                # Object/String/Octet payloads until process exit.
                engine.call(
                    engine.offset(TJS_VARIANT_DTOR_OFFSET),
                    ints=(variant_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"TJS output Variant: {exc!r}")
        if globals_created:
            try:
                engine.tjs_exec(
                    "try { oracle_psb_tag09_value = void; } catch(e) {} "
                    "try { oracle_psb_tag09_file = void; } catch(e) {}")
            except Exception as exc:
                cleanup_errors.append(f"TJS globals: {exc!r}")
        if tjs_initialized:
            try:
                engine.tjs_reset()
            except Exception as exc:
                cleanup_errors.append(f"TJS variant heap: {exc!r}")
        if cleanup_errors:
            result["cleanup_errors"] = cleanup_errors
            if result.get("status") in {"ok", "mismatch"}:
                result["status"] = "error"


def _read_pointer(engine, addr: int) -> int:
    return struct.unpack("<Q", engine.ql.mem.read(addr, 8))[0]


def _wait_for_pointer(engine, offset: int, timeout: float = 30.0) -> int:
    deadline = time.monotonic() + timeout
    while True:
        value = _read_pointer(engine, engine.offset(offset))
        if value != 0 or time.monotonic() >= deadline:
            return value
        time.sleep(0.25)


def _ensure_psbfile_registered(
    engine, timeout: float = 30.0,
) -> tuple[int, int]:
    deadline = time.monotonic() + timeout
    while True:
        singleton_addr = _read_pointer(
            engine, engine.offset(PSBMEDIA_SINGLETON_SLOT_OFFSET))
        class_object = _read_pointer(
            engine, engine.offset(PSBFILE_CLASS_SLOT_OFFSET))
        if singleton_addr != 0 and class_object != 0:
            return singleton_addr, class_object

        # Plugins may become visible shortly after TVPScriptEngine itself.
        # The TJS catch keeps that startup window retryable without letting a
        # script exception escape across the harness RPC boundary.
        engine.tjs_exec(
            'try { Plugins.link("PSBFile.dll"); } catch(e) {}')
        singleton_addr = _read_pointer(
            engine, engine.offset(PSBMEDIA_SINGLETON_SLOT_OFFSET))
        class_object = _read_pointer(
            engine, engine.offset(PSBFILE_CLASS_SLOT_OFFSET))
        if singleton_addr != 0 and class_object != 0:
            return singleton_addr, class_object
        if time.monotonic() >= deadline:
            return singleton_addr, class_object
        time.sleep(0.25)


def _make_ttstr(engine, value: str) -> tuple[int, int]:
    encoded = value.encode("ascii")
    cstr_addr = engine.heap.write(encoded + b"\0", align=8)
    payload = engine.call(
        engine.offset(TTSTR_FROM_ASCII_OFFSET),
        ints=(cstr_addr,), ret="ptr",
    )
    if payload == 0:
        raise RuntimeError(f"failed to construct ttstr for {value!r}")
    try:
        slot = engine.heap.write(struct.pack("<Q", payload), align=8)
    except Exception:
        # Ownership has not reached the caller yet, so release it here.  A
        # lost cleanup reply is not retried because Release may have executed.
        try:
            _release_ttstr(engine, payload)
        except Exception:
            pass
        raise
    return payload, slot


def _release_ttstr(engine, payload: int) -> None:
    engine.call(
        engine.offset(TJS_VARIANT_STRING_RELEASE_OFFSET),
        ints=(payload,), ret="void",
    )


def _read_ttstr(engine, payload: int) -> str:
    if payload == 0:
        return ""
    length = struct.unpack(
        "<I", engine.ql.mem.read(payload + 60, 4))[0]
    if length > 4096:
        raise RuntimeError(f"unreasonable Android ttstr length: {length}")
    if length < 22:
        chars_addr = payload + 16
    else:
        chars_addr = _read_pointer(engine, payload + 8)
    return engine.ql.mem.read(chars_addr, length * 2).decode("utf-16-le")


def _inspect_media(engine, media_addr: int) -> dict:
    # Android aarch64 ABI observation only.  These offsets are deliberately
    # confined to the oracle adapter; production C++ follows source fields.
    raw = engine.ql.mem.read(media_addr, PSBMEDIA_SIZE)
    return {
        "file_object": struct.unpack_from("<Q", raw, 12)[0],
        "file_objthis": struct.unpack_from("<Q", raw, 20)[0],
        "file_type": struct.unpack_from("<I", raw, 28)[0],
        "container": _read_ttstr(
            engine, struct.unpack_from("<Q", raw, 32)[0]),
    }


def _inspect_memory_stream(engine, stream_addr: int) -> dict:
    # tTVPMemoryStream block ctor @0x8F7C74 writes this 0x20-byte Android
    # layout.  Reading Block's pointer value is safe; never dereference it
    # after PSBMedia replaces the owning container.
    raw = engine.ql.mem.read(stream_addr, MEMORY_STREAM_SIZE)
    return {
        "block": struct.unpack_from("<Q", raw, 8)[0],
        "reference": bool(raw[16]),
        "size": struct.unpack_from("<I", raw, 20)[0],
        "alloc_size": struct.unpack_from("<I", raw, 24)[0],
        "current_pos": struct.unpack_from("<I", raw, 28)[0],
    }


def run_media_dictionary_case(
    engine,
    *,
    input_path: Path,
    remote_dir: str,
    container: str,
    dictionary_path: str,
    expected_keys: tuple[str, ...],
) -> dict:
    """Exercise PSBMedia::GetListAt with an existing Dictionary node.

    The ABI-matched Android harness supplies only a vtable/layout-compatible
    lister surrogate; traversal, tag dispatch, packed-key decoding and callback
    order all execute inside libkrkr2's PSBMedia::GetListAt.
    """
    _input_info(input_path)
    normalized_path = dictionary_path.strip("/")
    if not normalized_path or not expected_keys:
        raise ValueError("dictionary path and expected keys must be non-empty")
    if len(set(expected_keys)) != len(expected_keys):
        raise ValueError("dictionary expected keys must be unique")
    storage_auto_path = remote_dir.rstrip("/") + "/"
    list_name = f"{container}/{normalized_path}"
    for value in (storage_auto_path, list_name, *expected_keys):
        value.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": "media-dictionary-list",
        "container": container,
        "dictionary_path": normalized_path,
        "expected_keys": list(expected_keys),
        "script_engine_ready": script_engine != 0,
    }
    if script_engine == 0:
        result["status"] = "setup-failed"
        return result

    engine.tjs_init()
    singleton_addr, class_object = _ensure_psbfile_registered(engine)
    result.update({
        "singleton_ready": singleton_addr != 0,
        "class_object_ready": class_object != 0,
    })
    if singleton_addr == 0 or class_object == 0:
        result["status"] = "setup-failed"
        return result

    payloads: list[int] = []
    try:
        storage_payload, storage_slot = _make_ttstr(
            engine, storage_auto_path)
        payloads.append(storage_payload)
        engine.call(
            engine.offset(TVP_ADD_AUTO_PATH_OFFSET),
            ints=(storage_slot,), ret="void",
        )
        list_payload, list_slot = _make_ttstr(engine, list_name)
        payloads.append(list_payload)
        listed_keys = engine.storage_list(
            engine.offset(PSBMEDIA_GET_LIST_AT_OFFSET),
            singleton_addr,
            list_slot,
        )
        media = _inspect_media(engine, singleton_addr)
        result.update({
            "listed_keys": listed_keys,
            "file_object": media["file_object"],
            "file_objthis": media["file_objthis"],
            "file_type": media["file_type"],
            "loaded_container": media["container"],
        })
        ok = (
            listed_keys == list(expected_keys)
            and media["file_type"] == 1
            and media["file_object"] != 0
            and media["file_object"] == media["file_objthis"]
            and media["container"] == container
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        while payloads:
            payload = payloads.pop()
            try:
                _release_ttstr(engine, payload)
            except Exception as exc:
                result.setdefault("cleanup_errors", []).append(repr(exc))
                if result.get("status") in {"ok", "mismatch"}:
                    result["status"] = "error"


def run_media_lifecycle_case(
    engine,
    *,
    first_input: Path,
    replacement_input: Path,
    remote_dir: str,
    first_container: str,
    replacement_container: str,
    resource_name: str,
    expected_size: int,
) -> dict:
    """Exercise PSBMedia replacement and its borrowed-stream boundary.

    Both files are existing operator/repository inputs.  The first must expose
    ``resource_name``; the replacement must load as a PSB container but miss
    that resource.  No bytes are read through the old stream after replacement.
    """
    _input_info(first_input)
    _input_info(replacement_input)
    for value in (remote_dir, first_container, replacement_container,
                  resource_name):
        value.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(
        engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)

    result = {
        "input": str(first_input),
        "replacement_input": str(replacement_input),
        "entry": "media-lifecycle",
        "script_engine_ready": script_engine != 0,
    }
    if script_engine == 0:
        result["status"] = "setup-failed"
        return result

    # TJS_INIT must run only after the APK's full TVPScriptEngine exists.
    # Otherwise the harness permanently chooses its bare-TJS fallback, which
    # has no Plugins object or NCB classes and cannot serve as a media oracle.
    engine.tjs_init()

    singleton_addr, class_object = _ensure_psbfile_registered(engine)

    result.update({
        "singleton_ready": singleton_addr != 0,
        "class_object_ready": class_object != 0,
    })
    if singleton_addr == 0 or class_object == 0:
        result["status"] = "setup-failed"
        return result

    payloads: list[int] = []
    live_streams: list[int] = []
    try:
        auto_path = remote_dir.rstrip("/") + "/"
        auto_path_payload, auto_path_slot = _make_ttstr(engine, auto_path)
        payloads.append(auto_path_payload)
        engine.call(
            engine.offset(TVP_ADD_AUTO_PATH_OFFSET),
            ints=(auto_path_slot,), ret="void",
        )

        first_name = f"{first_container}/{resource_name}"
        replacement_name = f"{replacement_container}/{resource_name}"
        first_payload, first_slot = _make_ttstr(engine, first_name)
        payloads.append(first_payload)
        replacement_payload, replacement_slot = _make_ttstr(
            engine, replacement_name)
        payloads.append(replacement_payload)

        first_stream = engine.call(
            engine.offset(PSBMEDIA_OPEN_OFFSET),
            ints=(singleton_addr, first_slot, 0), ret="ptr",
        )
        result["first_opened"] = first_stream != 0
        if first_stream == 0:
            result["status"] = "mismatch"
            return result
        live_streams.append(first_stream)

        first_media = _inspect_media(engine, singleton_addr)
        first_metadata = _inspect_memory_stream(engine, first_stream)
        result.update({
            "first_container": first_media["container"],
            "first_resource_size": first_metadata["size"],
            "borrowed_reference": first_metadata["reference"],
        })

        replacement_exists = engine.call(
            engine.offset(PSBMEDIA_CHECK_STORAGE_OFFSET),
            ints=(singleton_addr, replacement_slot), ret="bool",
        )
        replacement_media = _inspect_media(engine, singleton_addr)
        preserved_metadata = _inspect_memory_stream(engine, first_stream)
        result.update({
            "replacement_exists": bool(replacement_exists),
            "replacement_container": replacement_media["container"],
            "file_dispatch_changed": (
                first_media["file_object"]
                != replacement_media["file_object"]),
            "metadata_preserved_after_replacement": (
                preserved_metadata == first_metadata),
        })

        # 0x8F7D68 is the deleting destructor split from IDA's formerly merged
        # 0x8F7D04 body. Reference=true means it does not free or dereference
        # the now-dangling borrowed Block, then calls operator delete(this).
        # Remove ownership before RPC: a lost reply must not trigger a second
        # destructor call from finally and turn cleanup into a use-after-free.
        live_streams.remove(first_stream)
        engine.call(
            engine.offset(TVP_MEMORY_STREAM_DELETING_DTOR_OFFSET),
            ints=(first_stream,), ret="void",
        )
        result["stream_delete_returned"] = True

        roundtrip_stream = engine.call(
            engine.offset(PSBMEDIA_OPEN_OFFSET),
            ints=(singleton_addr, first_slot, 0), ret="ptr",
        )
        result["roundtrip_opened"] = roundtrip_stream != 0
        if roundtrip_stream != 0:
            live_streams.append(roundtrip_stream)
        roundtrip_media = _inspect_media(engine, singleton_addr)
        result["roundtrip_container"] = roundtrip_media["container"]
        if roundtrip_stream != 0:
            roundtrip_metadata = _inspect_memory_stream(
                engine, roundtrip_stream)
            result["roundtrip_size"] = roundtrip_metadata["size"]
            result["roundtrip_reference"] = roundtrip_metadata["reference"]
            result["roundtrip_alloc_size"] = roundtrip_metadata["alloc_size"]
            result["roundtrip_current_pos"] = roundtrip_metadata["current_pos"]
            live_streams.remove(roundtrip_stream)
            engine.call(
                engine.offset(TVP_MEMORY_STREAM_DELETING_DTOR_OFFSET),
                ints=(roundtrip_stream,), ret="void",
            )
        else:
            result["roundtrip_size"] = 0
            result["roundtrip_reference"] = False
            result["roundtrip_alloc_size"] = 0
            result["roundtrip_current_pos"] = 0

        ok = (
            first_media["file_type"] == 1
            and first_media["file_object"] != 0
            and first_media["file_object"] == first_media["file_objthis"]
            and first_media["container"] == first_container
            and first_metadata["block"] != 0
            and first_metadata["reference"]
            and first_metadata["size"] == expected_size
            and first_metadata["alloc_size"] == expected_size
            and first_metadata["current_pos"] == 0
            and not replacement_exists
            and replacement_media["file_type"] == 1
            and replacement_media["file_object"] != 0
            and replacement_media["file_object"]
            == replacement_media["file_objthis"]
            and replacement_media["container"] == replacement_container
            and first_media["file_object"] != replacement_media["file_object"]
            and preserved_metadata == first_metadata
            and roundtrip_stream != 0
            and roundtrip_media["container"] == first_container
            and result["roundtrip_size"] == expected_size
            and result["roundtrip_reference"]
            and result["roundtrip_alloc_size"] == expected_size
            and result["roundtrip_current_pos"] == 0
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        while live_streams:
            stream = live_streams.pop()
            try:
                engine.call(
                    engine.offset(TVP_MEMORY_STREAM_DELETING_DTOR_OFFSET),
                    ints=(stream,), ret="void",
                )
            except Exception as exc:  # Preserve the primary RPC failure.
                cleanup_errors.append(
                    f"stream 0x{stream:x}: {exc!r}")
        while payloads:
            payload = payloads.pop()
            try:
                _release_ttstr(engine, payload)
            except Exception as exc:  # Preserve the primary RPC failure.
                cleanup_errors.append(
                    f"ttstr 0x{payload:x}: {exc!r}")
        if cleanup_errors:
            result["cleanup_errors"] = cleanup_errors
            if result.get("status") in {"ok", "mismatch"}:
                result["status"] = "error"
