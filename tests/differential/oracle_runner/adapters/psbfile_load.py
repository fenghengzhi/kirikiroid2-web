"""Android libkrkr2 oracle adapter for PSBFile raw and MDF load paths.

LEGACY TOOLING NOTICE: every native offset in this module belongs to the
historical, now-removed Android ``libkrkr2.so`` oracle.  The offsets are not
valid for the four current ``reference/binaries`` files and must not be used
as source-restoration evidence.  Re-enable or rebase this adapter only after
the complete call surface has been mapped against the selected current binary.

The input is supplied by the operator.  This adapter deliberately does not
construct or check in a damaged PSB/MDF fixture: it only forwards an existing
``PSB\0`` or ``mdf\0`` file through PSBFile's native implementation.
"""

from __future__ import annotations

import hashlib
import json
import struct
import time
from pathlib import Path
import zlib


PSBFILE_LOAD_VARIANT_OFFSET = 0x598268
PSBFILE_LOAD_STORAGE_OFFSET = 0x598538
PSBFILE_GET_ROOT_OFFSET = 0x598A3C
PSBFILE_TRANSFER_OFFSET = 0x598A64
PSBRAWNODE_GET_DICTIONARY_VALUE_STRICT_OFFSET = 0x598C58
PSBRAWNODE_GET_DICTIONARY_VALUE_OFFSET = 0x598D58
PSBRAWNODE_IS_VALID_OFFSET = 0x598E44
PSBRAWNODE_GET_DICTIONARY_KEYS_OFFSET = 0x598E64
PSBRAWOWNER_REFRESH_OFFSET = 0x598960
PSBRAWNODE_GET_STRING_OFFSET = 0x598B58
PSBRAWNODE_GET_DOUBLE_OFFSET = 0x5992E8
PSBRAWNODE_GET_INT_OFFSET = 0x599438
PSBRAWNODE_GET_TYPE_CATEGORY_OFFSET = 0x599554
PSBRAWNODE_GET_RESOURCE_OFFSET = 0x5996E4
PSBRAWNODE_CONTAINS_DICTIONARY_KEY_OFFSET = 0x5995D8
PSBVALUE_DISPATCH_NATIVE_INVALIDATE_PRIMARY_OFFSET = 0x596F38
PSBVALUE_DISPATCH_NATIVE_INVALIDATE_SECONDARY_OFFSET = 0x596F3C
PSBVALUE_DISPATCH_NATIVE_INSTANCE_SUPPORT_OFFSET = 0x596D90
PSBVALUE_DISPATCH_ISINSTANCEOF_OFFSET = 0x596E24
PSBVALUE_DISPATCH_ENUMMEMBERS_OFFSET = 0x596F50
PSBVALUE_DISPATCH_GETCOUNT_OFFSET = 0x5975E0
PSBVALUE_DISPATCH_PROPGET_OFFSET = 0x597854
PSBVALUE_DISPATCH_PROPGETBYNUM_OFFSET = 0x5976C4
PSBVALUE_DISPATCH_ADDREF_OFFSET = 0x597AC0
PSBVALUE_DISPATCH_RELEASE_OFFSET = 0x597A40
PSBVALUE_DISPATCH_NATIVE_DESTRUCT_PRIMARY_OFFSET = 0x597A28
PSBVALUE_DISPATCH_NATIVE_DESTRUCT_SECONDARY_OFFSET = 0x597A2C
PSBVALUE_DISPATCH_CONSTRUCT_PRIMARY_OFFSET = 0x597A30
PSBVALUE_DISPATCH_CONSTRUCT_SECONDARY_OFFSET = 0x597A38
PSBVALUE_DISPATCH_INVALIDATE_OFFSET = 0x596F0C
PSBVALUE_DISPATCH_ISVALID_OFFSET = 0x596EF0
PSBRAWOWNER_HOLDER_RELEASE_OFFSET = 0x695CBC
STD_VECTOR_STRING_DTOR_OFFSET = 0x918690
PSBMEDIA_COMPLETE_DESTRUCTOR_OFFSET = 0x5997F0
PSBMEDIA_DELETING_DESTRUCTOR_OFFSET = 0x599830
PSBMEDIA_ADDREF_OFFSET = 0x599878
PSBMEDIA_RELEASE_OFFSET = 0x599888
PSBMEDIA_GET_NAME_OFFSET = 0x5998A8
PSBMEDIA_NORMALIZE_DOMAIN_OFFSET = 0x5998BC
PSBMEDIA_NORMALIZE_PATH_OFFSET = 0x5998C0
PSBMEDIA_CHECK_STORAGE_OFFSET = 0x5998C4
PSBMEDIA_OPEN_OFFSET = 0x59993C
PSBMEDIA_GET_LIST_AT_OFFSET = 0x5999F4
PSBMEDIA_GET_LOCAL_NAME_OFFSET = 0x599DD8
PSBMEDIA_ENSURE_CONTAINER_OFFSET = 0x599E04
TVP_ADD_AUTO_PATH_OFFSET = 0x8EB4B4
TVP_MEMORY_STREAM_DELETING_DTOR_OFFSET = 0x8F7D68
TJS_VARIANT_STRING_RELEASE_OFFSET = 0xA13274
TJS_VARIANT_DTOR_OFFSET = 0xA0F778
TTSTR_FROM_ASCII_OFFSET = 0xA13878
EMOTE_PSB_DECRYPT_OFFSET = 0x6863CC
CXX_OPERATOR_NEW_OFFSET = 0x14570AC
CXX_OPERATOR_DELETE_PLT_OFFSET = 0x415740

PSBMEDIA_SINGLETON_SLOT_OFFSET = 0x1AB50E8
PSBMEDIA_VTABLE_OFFSET = 0x1A0B510
PSBVALUE_CLASS_ID_SLOT_OFFSET = 0x1AB5098
PSBVALUE_DISPATCH_PRIMARY_VTABLE_OFFSET = 0x1A0B3D8
PSBVALUE_DISPATCH_SECONDARY_VTABLE_OFFSET = 0x1A0B4E8
PSBFILE_CLASS_SLOT_OFFSET = 0x1AB5110
TVP_SCRIPT_ENGINE_SLOT_OFFSET = 0x1AE2FD0

MDF_MAGIC = b"mdf\0"
PSB_MAGIC = b"PSB\0"
OWNER_SIZE = 0x68
PSBVALUE_DISPATCH_SIZE = 0x30
PSBMEDIA_SIZE = 0x28
MEMORY_STREAM_SIZE = 0x20
TJS_VARIANT_SIZE = 0x18
TJS_VARIANT_OBJECT_TYPE = 1
TJS_VARIANT_STRING_TYPE = 2
TJS_VARIANT_OCTET_TYPE = 3
TJS_VARIANT_INTEGER_TYPE = 4
TJS_VARIANT_REAL_TYPE = 5
TJS_MEMBERMUSTEXIST = 0x400
TJS_ENUM_NO_VALUE = 0x00100000
TJS_NIS_GETINSTANCE = 2
TJS_S_OK = 0
TJS_S_TRUE = 1
TJS_S_FALSE = 2
TJS_E_MEMBERNOTFOUND = -1001
TJS_E_NOTIMPL = -1002
TJS_E_INVALIDOBJECT = -1006
TJS_E_FAIL = -1

# Exact Android iTVPStorageMedia address-point order installed by
# PSBFile_preRegister_guess@0x59849C.  These are ABI observations for the
# oracle only; production C++ keeps ordinary virtual methods and lets each
# compiler lay out its own vtable.
PSBMEDIA_VTABLE_ENTRY_OFFSETS = (
    PSBMEDIA_COMPLETE_DESTRUCTOR_OFFSET,
    PSBMEDIA_DELETING_DESTRUCTOR_OFFSET,
    PSBMEDIA_ADDREF_OFFSET,
    PSBMEDIA_RELEASE_OFFSET,
    PSBMEDIA_GET_NAME_OFFSET,
    PSBMEDIA_NORMALIZE_DOMAIN_OFFSET,
    PSBMEDIA_NORMALIZE_PATH_OFFSET,
    PSBMEDIA_CHECK_STORAGE_OFFSET,
    PSBMEDIA_OPEN_OFFSET,
    PSBMEDIA_GET_LIST_AT_OFFSET,
    PSBMEDIA_GET_LOCAL_NAME_OFFSET,
)

# Exact Android primary iTJSDispatch2 address-point order at 0x1A0B3D8.
PSBVALUE_DISPATCH_PRIMARY_VTABLE_ENTRY_OFFSETS = (
    0x597AC0,  # AddRef
    0x597A40,  # Release
    0x597A20,  # FuncCall
    0x597A18,  # FuncCallByNum
    0x597854,  # PropGet
    0x5976C4,  # PropGetByNum
    0x5976BC,  # PropSet
    0x5976B4,  # PropSetByNum
    0x5975E0,  # GetCount
    0x5975D8,  # GetCountByNum
    0x5975D0,  # PropSetByVS
    0x596F50,  # EnumMembers
    0x596F48,  # DeleteMember
    0x596F40,  # DeleteMemberByNum
    0x596F0C,  # dispatch Invalidate
    0x596F04,  # InvalidateByNum
    0x596EF0,  # dispatch IsValid
    0x596EE8,  # IsValidByNum
    0x596EE0,  # CreateNew
    0x596ED8,  # CreateNewByNum
    0x596ED0,  # Reserved1
    0x596E24,  # IsInstanceOf
    0x596E1C,  # IsInstanceOfByNum
    0x596E14,  # Operation
    0x596E0C,  # OperationByNum
    0x596D90,  # NativeInstanceSupport
    0x596D88,  # ClassInstanceInfo
    0x596D80,  # Reserved2
    0x596D78,  # Reserved3
    0x597A30,  # primary native Construct
    0x596F38,  # primary native Invalidate
    0x597A28,  # primary native Destruct
)

# (result key, function offset, integer-register argument count including this)
PSBVALUE_DISPATCH_UNSUPPORTED_PROBES = (
    ("func_call", 0x597A20, 8),
    ("func_call_by_num", 0x597A18, 7),
    ("prop_set", 0x5976BC, 6),
    ("prop_set_by_num", 0x5976B4, 5),
    ("get_count_by_num", 0x5975D8, 4),
    ("prop_set_by_vs", 0x5975D0, 5),
    ("delete_member", 0x596F48, 5),
    ("delete_member_by_num", 0x596F40, 4),
    ("invalidate_by_num", 0x596F04, 4),
    ("is_valid_by_num", 0x596EE8, 4),
    ("create_new", 0x596EE0, 8),
    ("create_new_by_num", 0x596ED8, 7),
    ("reserved1", 0x596ED0, 1),
    ("is_instance_of_by_num", 0x596E1C, 5),
    ("operation", 0x596E14, 7),
    ("operation_by_num", 0x596E0C, 6),
    ("class_instance_info", 0x596D88, 4),
    ("reserved2", 0x596D80, 1),
    ("reserved3", 0x596D78, 1),
)

INTEGER_NODE_SIZES = {
    0x04: 1,
    0x05: 2,
    0x06: 3,
    0x07: 4,
    0x08: 5,
    0x09: 6,
    0x0A: 7,
    0x0B: 8,
    0x0C: 9,
}
RESOURCE_NODE_SIZES = {
    0x19: 2,
    0x1A: 3,
    0x1B: 4,
    0x1C: 5,
    0x2D: 1,
}
REAL_NODE_SIZES = {
    0x1D: 1,
    0x1E: 5,
    0x1F: 9,
}
STRING_NODE_SIZES = {
    0x15: 2,
    0x16: 3,
    0x17: 4,
    0x18: 5,
    0x2C: 1,
}


def _input_info(input_path: Path) -> tuple[bytes, str, int]:
    data = input_path.read_bytes()
    if len(data) >= 11 and data[:4] == MDF_MAGIC:
        return data, "mdf", struct.unpack_from("<I", data, 4)[0]
    if len(data) >= 0x40 and data[:4] == PSB_MAGIC:
        return data, "psb", len(data)
    raise ValueError(f"not a PSB or MDF input: {input_path}")


def _decoded_psb(data: bytes, input_format: str, declared_size: int) -> bytes:
    if input_format == "psb":
        decoded = data
    else:
        decoded = zlib.decompress(data[8:])
    if len(decoded) != declared_size:
        raise ValueError(
            "decoded PSB size does not match the container declaration")
    if not decoded.startswith(PSB_MAGIC):
        raise ValueError("decoded boundary input does not start with PSB\\0")
    return decoded


def _packed_array_header(data: bytes, offset: int) -> tuple[int, int]:
    """Return Android PSB packed-array count and encoded byte size."""
    if offset < 0 or offset >= len(data):
        raise ValueError("packed-array header lies outside the decoded PSB")
    tag = data[offset]
    count_width = tag - 0x0B
    if not 2 <= count_width <= 5:
        raise ValueError(f"unsupported packed-array tag 0x{tag:02x}")
    if offset + count_width >= len(data):
        raise ValueError("truncated packed-array count/width header")
    if tag == 0x0D:
        count = data[offset + 1]
    elif tag == 0x0E:
        count = struct.unpack_from("<H", data, offset + 1)[0]
    elif tag == 0x0F:
        count = struct.unpack_from("<I", data, offset + 1)[0] & 0xFFFFFF
    else:
        count = struct.unpack_from("<I", data, offset + 1)[0]
    value_width = data[offset + count_width] - 0x0C
    if not 1 <= value_width <= 5:
        raise ValueError(
            f"unsupported packed-array value width {value_width}")
    byte_size = count_width + 1 + count * value_width
    if offset + byte_size > len(data):
        raise ValueError("packed-array values lie outside the decoded PSB")
    return count, byte_size


def _collection_header(
    data: bytes, node_offset: int,
) -> tuple[int, int, int]:
    """Return (tag, entry count, table bytes including the node tag)."""
    if node_offset < 0 or node_offset >= len(data):
        raise ValueError("collection node lies outside the decoded PSB")
    tag = data[node_offset]
    if tag == 0x20:
        count, offsets_size = _packed_array_header(data, node_offset + 1)
        return tag, count, 1 + offsets_size
    if tag == 0x21:
        key_count, keys_size = _packed_array_header(data, node_offset + 1)
        value_count, offsets_size = _packed_array_header(
            data, node_offset + 1 + keys_size)
        if key_count != value_count:
            raise ValueError(
                "dictionary key/value packed-array counts do not match")
        return tag, value_count, 1 + keys_size + offsets_size
    raise ValueError(f"node tag 0x{tag:02x} is not Array/Dictionary")


def _packed_array_value(data: bytes, offset: int, index: int) -> int:
    count, _ = _packed_array_header(data, offset)
    if not 0 <= index < count:
        raise ValueError("packed-array index is outside the pinned count")
    count_width = data[offset] - 0x0B
    value_width = data[offset + count_width] - 0x0C
    value_addr = offset + count_width + 1 + index * value_width
    if value_addr + 4 > len(data):
        raise ValueError("packed-array value read lies outside decoded PSB")
    raw = struct.unpack_from("<I", data, value_addr)[0]
    shift = (8 * (4 - value_width)) & 31
    return raw & (0xFFFFFFFF >> shift)


def _integer_node_value(data: bytes, offset: int) -> tuple[int, bytes]:
    if offset < 0 or offset >= len(data):
        raise ValueError("integer node lies outside decoded PSB")
    tag = data[offset]
    node_size = INTEGER_NODE_SIZES.get(tag)
    if node_size is None or offset + node_size > len(data):
        raise ValueError("negative-index target is not a complete Integer")
    raw = data[offset:offset + node_size]
    if tag == 0x04:
        value = 0
    elif tag == 0x0B:
        value = int.from_bytes(raw[1:], "little", signed=False)
    else:
        value = int.from_bytes(raw[1:], "little", signed=True)
    return value, raw


def _decode_name(data: bytes, name_index: int) -> str:
    names_offset = struct.unpack_from("<I", data, 12)[0]
    _, charset_size = _packed_array_header(data, names_offset)
    charset_offset = names_offset
    names_data_offset = names_offset + charset_size
    _, names_data_size = _packed_array_header(data, names_data_offset)
    name_indexes_offset = names_data_offset + names_data_size
    node = _packed_array_value(
        data, names_data_offset,
        _packed_array_value(data, name_indexes_offset, name_index))
    encoded: list[int] = []
    for _ in range(100_000):
        if node == 0:
            encoded.reverse()
            return bytes(encoded).decode("utf-8")
        parent = _packed_array_value(data, names_data_offset, node)
        encoded.append((
            node - _packed_array_value(data, charset_offset, parent)) & 0xFF)
        node = parent
    raise ValueError("name parent chain did not terminate")


def _all_names(data: bytes) -> set[str]:
    """Decode the complete PSB name-index table without altering the input."""
    names_offset = struct.unpack_from("<I", data, 12)[0]
    _, charset_size = _packed_array_header(data, names_offset)
    names_data_offset = names_offset + charset_size
    _, names_data_size = _packed_array_header(data, names_data_offset)
    name_indexes_offset = names_data_offset + names_data_size
    name_count, _ = _packed_array_header(data, name_indexes_offset)
    return {_decode_name(data, index) for index in range(name_count)}


def _dictionary_keys(data: bytes, node_offset: int) -> set[str]:
    if data[node_offset] != 0x21:
        raise ValueError("dictionary-key pin requires tag 0x21")
    count, _ = _packed_array_header(data, node_offset + 1)
    return {
        _decode_name(data, _packed_array_value(
            data, node_offset + 1, index))
        for index in range(count)
    }


def _collection_members(
    data: bytes, node_offset: int,
) -> list[tuple[str, int]]:
    """Return packed enumeration order as (member name, child offset)."""
    tag, count, _ = _collection_header(data, node_offset)
    first_array_offset = node_offset + 1
    if tag == 0x20:
        _, offsets_size = _packed_array_header(data, first_array_offset)
        values_offset = first_array_offset + offsets_size
        return [
            (
                str(index),
                values_offset + _packed_array_value(
                    data, first_array_offset, index),
            )
            for index in range(count)
        ]

    _, keys_size = _packed_array_header(data, first_array_offset)
    offsets_offset = first_array_offset + keys_size
    _, offsets_size = _packed_array_header(data, offsets_offset)
    values_offset = offsets_offset + offsets_size
    return [
        (
            _decode_name(data, _packed_array_value(
                data, first_array_offset, index)),
            values_offset + _packed_array_value(data, offsets_offset, index),
        )
        for index in range(count)
    ]


def _inspect_cow_string_vector(
    engine, vector_addr: int, expected_keys: list[str],
) -> dict[str, object]:
    """Inspect Android gnustl ``vector<string>`` without taking ownership.

    Android's old-libstdc++ layout is a three-pointer vector whose elements
    are one-pointer COW strings.  The caller must subsequently invoke the
    target's own vector destructor; this helper only reads the live object.
    """
    header = bytes(engine.ql.mem.read(vector_addr, 24))
    begin, end, capacity_end = struct.unpack("<QQQ", header)
    expected_utf8 = [key.encode("utf-8") for key in expected_keys]
    result: dict[str, object] = {
        "begin": begin,
        "end": end,
        "capacity_end": capacity_end,
        "header_bits_le": header.hex(),
        "expected_keys": expected_keys,
    }
    if not expected_keys:
        empty_ok = begin == 0 and end == 0 and capacity_end == 0
        result.update({
            "size": 0,
            "capacity": 0,
            "keys": [],
            "empty_triple_zero": empty_ok,
            "ok": empty_ok,
        })
        return result

    topology_ok = (
        begin != 0
        and begin <= end <= capacity_end
        and (end - begin) % 8 == 0
        and (capacity_end - begin) % 8 == 0
    )
    size = (end - begin) // 8 if topology_ok else None
    capacity = (capacity_end - begin) // 8 if topology_ok else None
    result.update({
        "size": size,
        "capacity": capacity,
        "topology_ok": topology_ok,
    })
    if not topology_ok or size != len(expected_keys):
        result["ok"] = False
        return result

    keys: list[str] = []
    data_addresses: list[int] = []
    lengths: list[int] = []
    capacities: list[int] = []
    refcounts: list[int] = []
    terminators: list[int] = []
    for index in range(len(expected_utf8)):
        data_addr = struct.unpack(
            "<Q", engine.ql.mem.read(begin + index * 8, 8))[0]
        if data_addr < 24:
            raise ValueError("gnustl COW string data pointer is invalid")
        rep = bytes(engine.ql.mem.read(data_addr - 24, 24))
        length, string_capacity, refcount, _ = struct.unpack("<QQiI", rep)
        if length > 0x10000:
            raise ValueError("gnustl COW string length exceeds oracle limit")
        payload = bytes(engine.ql.mem.read(data_addr, length + 1))
        keys.append(payload[:-1].decode("utf-8"))
        data_addresses.append(data_addr)
        lengths.append(length)
        capacities.append(string_capacity)
        refcounts.append(refcount)
        terminators.append(payload[-1])

    strings_ok = (
        keys == expected_keys
        and lengths == [len(value) for value in expected_utf8]
        and all(capacity_value >= length for capacity_value, length in zip(
            capacities, lengths))
        and refcounts == [0] * len(expected_keys)
        and terminators == [0] * len(expected_keys)
        and len(set(data_addresses)) == len(data_addresses)
    )
    result.update({
        "keys": keys,
        "data_addresses": data_addresses,
        "string_lengths": lengths,
        "string_capacities": capacities,
        "cow_refcounts": refcounts,
        "nul_terminators": terminators,
        "distinct_string_reps":
            len(set(data_addresses)) == len(data_addresses),
        "reserve_exact_capacity": capacity == len(expected_keys),
        "strings_ok": strings_ok,
        "ok": (
            topology_ok
            and size == len(expected_keys)
            and capacity == len(expected_keys)
            and strings_ok
        ),
    })
    return result


def _tjs_type_name_for_node(data: bytes, node_offset: int) -> str:
    """Map the natural EnumMembers nodes used here to TJS ``typeof``."""
    if node_offset < 0 or node_offset >= len(data):
        raise ValueError("collection child lies outside the decoded PSB")
    tag = data[node_offset]
    if tag in INTEGER_NODE_SIZES:
        return "Integer"
    if tag in REAL_NODE_SIZES:
        return "Real"
    if tag in STRING_NODE_SIZES:
        return "String"
    if tag in RESOURCE_NODE_SIZES:
        return "Octet"
    if tag in {0x20, 0x21}:
        return "Object"
    raise ValueError(
        f"unsupported natural EnumMembers child tag 0x{tag:02x}")


def _signed_w32(value: int) -> int:
    low = value & 0xFFFFFFFF
    return low if low < 0x80000000 else low - 0x100000000


def _integer_variant_bytes(value: int) -> bytes:
    return struct.pack("<q8xI4x", value, TJS_VARIANT_INTEGER_TYPE)


def _variant_snapshot(engine, variant_addr: int) -> dict[str, object]:
    raw = engine.ql.mem.read(variant_addr, TJS_VARIANT_SIZE)
    variant_type = struct.unpack_from("<I", raw, 16)[0]
    return {
        "type": variant_type,
        "integer": (
            struct.unpack_from("<q", raw, 0)[0]
            if variant_type == TJS_VARIANT_INTEGER_TYPE else None),
        "payload_bits_le": raw[:8].hex(),
    }


def _read_tjs_string_variant(engine, variant_addr: int) -> dict:
    variant = engine.ql.mem.read(variant_addr, TJS_VARIANT_SIZE)
    variant_type = struct.unpack_from("<I", variant, 16)[0]
    result = {
        "variant_type": variant_type,
        "string_addr": 0,
        "utf16_length": None,
        "utf8": None,
    }
    if variant_type != TJS_VARIANT_STRING_TYPE:
        return result
    string_addr = struct.unpack_from("<Q", variant, 0)[0]
    result["string_addr"] = string_addr
    if string_addr == 0:
        return result
    utf16_length = struct.unpack(
        "<I", engine.ql.mem.read(string_addr + 60, 4))[0]
    if utf16_length < 22:
        chars_addr = string_addr + 16
    else:
        chars_addr = _read_pointer(engine, string_addr + 8)
    if chars_addr == 0 and utf16_length != 0:
        return result
    utf16 = engine.ql.mem.read(chars_addr, utf16_length * 2)
    result["utf16_length"] = utf16_length
    result["utf8"] = utf16.decode("utf-16-le").encode("utf-8")
    return result


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


def run_raw_holder_lifecycle_case(engine, input_path: Path) -> dict:
    """Observe GetRoot/Transfer ownership on one immutable natural PSB.

    Both Android entries return non-trivial C++ objects through hidden X8.
    GetRoot yields a two-pointer PSBRawNode and retains the owner once;
    Transfer yields a one-pointer PSBFile without a net refcount change and
    clears its source holder.  Every copied holder is released exactly once
    through Android's original one-pointer holder destructor helper.
    """
    data, input_format, declared_size = _input_info(input_path)
    decoded = _decoded_psb(data, input_format, declared_size)
    if len(decoded) < 0x40:
        raise ValueError("raw holder lifecycle requires a complete PSB header")
    entries_offset = struct.unpack_from("<I", decoded, 36)[0]
    if entries_offset >= len(decoded):
        raise ValueError("PSB entries offset lies outside the decoded input")

    engine.reset_heap()
    result = {
        "input": str(input_path),
        "entry": "raw-holder-lifecycle",
        "input_format": input_format,
        "input_size": len(data),
        "decoded_size": len(decoded),
        "declared_size": declared_size,
        "entries_offset": entries_offset,
        "expected_root_tag": f"0x{decoded[entries_offset]:02x}",
    }
    raw_holder_addr = 0
    root_holder_addr = 0
    transfer_holder_addr = 0
    raw_holder_owns_reference = False
    root_holder_owns_reference = False
    transfer_holder_owns_reference = False
    try:
        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result.update({
            "loaded": bool(loaded),
            "owner": owner_addr,
        })
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result
        raw_holder_owns_reference = True

        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        owner_refcount_before = struct.unpack_from("<I", owner, 0)[0]
        header_addr = struct.unpack_from("<Q", owner, 8)[0]
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        header_entries_addr = _read_pointer(engine, owner_addr + 80)
        expected_root_addr = raw_addr + entries_offset
        baseline_ok = (
            owner_refcount_before == 1
            and header_addr == owner_addr + 16
            and raw_size == declared_size
            and engine.ql.mem.read(raw_addr, 4) == PSB_MAGIC
            and header_entries_addr == expected_root_addr
        )
        result.update({
            "owner_refcount_before_getroot": owner_refcount_before,
            "header_is_inline": header_addr == owner_addr + 16,
            "owner_raw_size": raw_size,
            "header_entries_address": header_entries_addr,
            "expected_root_address": expected_root_addr,
            "baseline_ok": baseline_ok,
        })
        if not baseline_ok:
            result["status"] = "mismatch"
            return result

        root_sentinel = bytes(range(0xA0, 0xB0))
        root_holder_addr = engine.heap.write(root_sentinel, align=8)
        engine.call_sret(
            engine.offset(PSBFILE_GET_ROOT_OFFSET),
            result_addr=root_holder_addr,
            result_size=16,
            ints=(raw_holder_addr,),
        )
        root_owner, root_node = struct.unpack(
            "<QQ", engine.ql.mem.read(root_holder_addr, 16))
        root_holder_owns_reference = root_owner == owner_addr
        owner_refcount_after_getroot = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        root_node_tag = (
            engine.ql.mem.read(root_node, 1)[0]
            if root_node == expected_root_addr else None
        )
        getroot_ok = (
            root_owner == owner_addr
            and root_node == expected_root_addr
            and root_node_tag == decoded[entries_offset]
            and owner_refcount_after_getroot == 2
        )
        result.update({
            "root_owner": root_owner,
            "root_node": root_node,
            "root_node_tag": (
                f"0x{root_node_tag:02x}"
                if root_node_tag is not None else None),
            "owner_refcount_after_getroot": owner_refcount_after_getroot,
            "getroot_ok": getroot_ok,
        })
        if not getroot_ok:
            result["status"] = "mismatch"
            return result

        transfer_sentinel = bytes.fromhex("8877665544332211")
        transfer_holder_addr = engine.heap.write(
            transfer_sentinel, align=8)
        engine.call_sret(
            engine.offset(PSBFILE_TRANSFER_OFFSET),
            result_addr=transfer_holder_addr,
            result_size=8,
            ints=(raw_holder_addr,),
        )
        transfer_owner = _read_pointer(engine, transfer_holder_addr)
        source_owner_after_transfer = _read_pointer(engine, raw_holder_addr)
        if transfer_owner == owner_addr and source_owner_after_transfer == 0:
            transfer_holder_owns_reference = True
            raw_holder_owns_reference = False
        owner_refcount_after_transfer = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        transfer_ok = (
            transfer_owner == owner_addr
            and source_owner_after_transfer == 0
            and owner_refcount_after_transfer == 2
        )
        result.update({
            "transfer_owner": transfer_owner,
            "source_owner_after_transfer": source_owner_after_transfer,
            "owner_refcount_after_transfer": owner_refcount_after_transfer,
            "transfer_ok": transfer_ok,
        })
        if not transfer_ok:
            result["status"] = "mismatch"
            return result

        empty_source_addr = engine.heap.write(b"\0" * 8, align=8)
        empty_result_addr = engine.heap.write(transfer_sentinel, align=8)
        engine.call_sret(
            engine.offset(PSBFILE_TRANSFER_OFFSET),
            result_addr=empty_result_addr,
            result_size=8,
            ints=(empty_source_addr,),
        )
        empty_result_owner = _read_pointer(engine, empty_result_addr)
        empty_source_owner = _read_pointer(engine, empty_source_addr)
        empty_transfer_ok = (
            empty_result_owner == 0 and empty_source_owner == 0)
        result.update({
            "empty_transfer_result_owner": empty_result_owner,
            "empty_transfer_source_owner": empty_source_owner,
            "empty_transfer_ok": empty_transfer_ok,
        })

        engine.call(
            engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
            ints=(root_holder_addr,), ret="void",
        )
        root_holder_owns_reference = False
        owner_refcount_after_root_release = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        root_slot_after_release = _read_pointer(engine, root_holder_addr)

        engine.call(
            engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
            ints=(transfer_holder_addr,), ret="void",
        )
        transfer_holder_owns_reference = False
        transfer_slot_after_release = _read_pointer(
            engine, transfer_holder_addr)
        release_ok = (
            owner_refcount_after_root_release == 1
            and root_slot_after_release == owner_addr
            and transfer_slot_after_release == owner_addr
        )
        result.update({
            "owner_refcount_after_root_release":
                owner_refcount_after_root_release,
            "root_slot_after_release": root_slot_after_release,
            "transfer_slot_after_release": transfer_slot_after_release,
            "holder_release_leaves_slots_unchanged": (
                root_slot_after_release == owner_addr
                and transfer_slot_after_release == owner_addr),
            "terminal_owner_release_invoked": True,
            "release_ok": release_ok,
        })
        ok = getroot_ok and transfer_ok and empty_transfer_ok and release_ok
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        for label, holder_addr, owns_reference in (
            ("GetRoot result", root_holder_addr,
             root_holder_owns_reference),
            ("Transfer result", transfer_holder_addr,
             transfer_holder_owns_reference),
            ("source PSBFile", raw_holder_addr,
             raw_holder_owns_reference),
        ):
            if not holder_addr or not owns_reference:
                continue
            try:
                engine.call(
                    engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                    ints=(holder_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"{label}: {exc!r}")
        if cleanup_errors:
            result["cleanup_errors"] = cleanup_errors
            if result.get("status") in {"ok", "mismatch"}:
                result["status"] = "error"


def run_raw_dictionary_lifecycle_case(engine, input_path: Path) -> dict:
    """Observe raw Dictionary lookup and PSBRawNode holder lifetimes.

    The immutable natural root supplies two successful children plus both
    first-helper and second-helper misses.  The case also exercises Android's
    release-before-reload ``self == outNode`` path without manufacturing PSB
    bytes or replacing the packed containers with host-side equivalents.
    """
    data, input_format, declared_size = _input_info(input_path)
    decoded = _decoded_psb(data, input_format, declared_size)
    root_offset = struct.unpack_from("<I", decoded, 36)[0]
    if root_offset >= len(decoded) or decoded[root_offset] != 0x21:
        raise ValueError("raw Dictionary lifecycle requires a natural root")
    root_member_pairs = _collection_members(decoded, root_offset)
    root_members = dict(root_member_pairs)
    if len(root_members) != len(root_member_pairs):
        raise ValueError("root Dictionary unexpectedly contains duplicate keys")
    object_offset = root_members.get("object")
    version_offset = root_members.get("version")
    if object_offset is None or decoded[object_offset] != 0x21:
        raise ValueError("natural root object child pin changed")
    if version_offset is None or decoded[version_offset] != 0x1E:
        raise ValueError("natural root version child pin changed")
    object_members = dict(_collection_members(decoded, object_offset))

    hit_key = "object"
    overwrite_key = "version"
    second_helper_miss_key = "icon42"
    first_helper_miss_key = "__psbfile_raw_missing__"
    all_names = _all_names(decoded)
    if (second_helper_miss_key not in all_names
            or second_helper_miss_key in root_members
            or second_helper_miss_key in object_members):
        raise ValueError("global-only Dictionary miss key pin changed")
    if first_helper_miss_key in all_names:
        raise ValueError("first-helper miss key unexpectedly entered names")

    engine.reset_heap()
    result = {
        "input": str(input_path),
        "entry": "raw-dictionary-lifecycle",
        "input_format": input_format,
        "input_size": len(data),
        "decoded_size": len(decoded),
        "declared_size": declared_size,
        "root_offset": root_offset,
        "root_tag": "0x21",
        "hit_key": hit_key,
        "hit_child_offset": object_offset,
        "hit_child_tag": "0x21",
        "overwrite_key": overwrite_key,
        "overwrite_child_offset": version_offset,
        "overwrite_child_tag": "0x1e",
        "first_helper_miss_key": first_helper_miss_key,
        "second_helper_miss_key": second_helper_miss_key,
    }
    raw_holder_addr = 0
    root_holder_addr = 0
    strict_holder_addr = 0
    ordinary_holder_addr = 0
    raw_holder_owns_reference = False
    root_holder_owns_reference = False
    strict_holder_owns_reference = False
    ordinary_holder_owns_reference = False
    try:
        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result.update({"loaded": bool(loaded), "owner": owner_addr})
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result
        raw_holder_owns_reference = True

        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        owner_refcount_before = struct.unpack_from("<I", owner, 0)[0]
        expected_root_addr = raw_addr + root_offset
        expected_object_addr = raw_addr + object_offset
        expected_version_addr = raw_addr + version_offset
        baseline_ok = (
            owner_refcount_before == 1
            and raw_size == declared_size
            and _read_pointer(engine, owner_addr + 80) == expected_root_addr
        )
        result.update({
            "owner_refcount_before_getroot": owner_refcount_before,
            "owner_raw_size": raw_size,
            "expected_root_address": expected_root_addr,
            "expected_hit_child_address": expected_object_addr,
            "expected_overwrite_child_address": expected_version_addr,
            "baseline_ok": baseline_ok,
        })
        if not baseline_ok:
            result["status"] = "mismatch"
            return result

        root_holder_addr = engine.heap.write(
            bytes(range(0x80, 0x90)), align=8)
        engine.call_sret(
            engine.offset(PSBFILE_GET_ROOT_OFFSET),
            result_addr=root_holder_addr,
            result_size=16,
            ints=(raw_holder_addr,),
        )
        root_owner, root_node = struct.unpack(
            "<QQ", engine.ql.mem.read(root_holder_addr, 16))
        root_holder_owns_reference = root_owner == owner_addr
        owner_refcount_after_getroot = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        getroot_ok = (
            root_owner == owner_addr
            and root_node == expected_root_addr
            and owner_refcount_after_getroot == 2
        )
        result.update({
            "root_owner": root_owner,
            "root_node": root_node,
            "owner_refcount_after_getroot": owner_refcount_after_getroot,
            "getroot_ok": getroot_ok,
        })
        if not getroot_ok:
            result["status"] = "mismatch"
            return result

        empty_node_addr = engine.heap.write(b"\0" * 16, align=8)
        owner_only_node_addr = engine.heap.write(
            struct.pack("<QQ", owner_addr, 0), align=8)
        node_only_node_addr = engine.heap.write(
            struct.pack("<QQ", 0, expected_root_addr), align=8)
        isvalid_root = engine.call(
            engine.offset(PSBRAWNODE_IS_VALID_OFFSET),
            ints=(root_holder_addr,), ret="bool",
        )
        isvalid_empty = engine.call(
            engine.offset(PSBRAWNODE_IS_VALID_OFFSET),
            ints=(empty_node_addr,), ret="bool",
        )
        isvalid_owner_only = engine.call(
            engine.offset(PSBRAWNODE_IS_VALID_OFFSET),
            ints=(owner_only_node_addr,), ret="bool",
        )
        isvalid_node_only = engine.call(
            engine.offset(PSBRAWNODE_IS_VALID_OFFSET),
            ints=(node_only_node_addr,), ret="bool",
        )
        owner_refcount_after_isvalid = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        isvalid_ok = (
            isvalid_root
            and not isvalid_empty
            and not isvalid_owner_only
            and not isvalid_node_only
            and owner_refcount_after_isvalid == 2
        )
        result.update({
            "isvalid_root": bool(isvalid_root),
            "isvalid_empty": bool(isvalid_empty),
            "isvalid_owner_only": bool(isvalid_owner_only),
            "isvalid_node_only": bool(isvalid_node_only),
            "owner_refcount_after_isvalid": owner_refcount_after_isvalid,
            "isvalid_ok": isvalid_ok,
        })
        if not isvalid_ok:
            result["status"] = "mismatch"
            return result

        hit_key_addr = engine.heap.write(
            hit_key.encode("utf-8") + b"\0", align=8)
        overwrite_key_addr = engine.heap.write(
            overwrite_key.encode("utf-8") + b"\0", align=8)
        second_miss_key_addr = engine.heap.write(
            second_helper_miss_key.encode("utf-8") + b"\0", align=8)
        first_miss_key_addr = engine.heap.write(
            first_helper_miss_key.encode("utf-8") + b"\0", align=8)

        strict_holder_addr = engine.heap.write(
            bytes(range(0xA0, 0xB0)), align=8)
        engine.call_sret(
            engine.offset(
                PSBRAWNODE_GET_DICTIONARY_VALUE_STRICT_OFFSET),
            result_addr=strict_holder_addr,
            result_size=16,
            ints=(root_holder_addr, hit_key_addr),
        )
        strict_owner, strict_node = struct.unpack(
            "<QQ", engine.ql.mem.read(strict_holder_addr, 16))
        strict_holder_owns_reference = strict_owner == owner_addr
        owner_refcount_after_strict = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        strict_ok = (
            strict_owner == owner_addr
            and strict_node == expected_object_addr
            and owner_refcount_after_strict == 3
        )
        result.update({
            "strict_owner": strict_owner,
            "strict_node": strict_node,
            "owner_refcount_after_strict": owner_refcount_after_strict,
            "strict_ok": strict_ok,
        })
        if not strict_ok:
            result["status"] = "mismatch"
            return result

        # This is an in/out PSBRawNode, not a pure output buffer: Android
        # releases outNode->owner on hit before assigning the child.  Begin
        # with a real default-constructed {null,null} holder.
        ordinary_holder_addr = engine.heap.write(b"\0" * 16, align=8)
        ordinary_hit_rc = engine.call(
            engine.offset(PSBRAWNODE_GET_DICTIONARY_VALUE_OFFSET),
            ints=(root_holder_addr, hit_key_addr, ordinary_holder_addr),
            ret="bool",
        )
        ordinary_owner, ordinary_node = struct.unpack(
            "<QQ", engine.ql.mem.read(ordinary_holder_addr, 16))
        ordinary_holder_owns_reference = ordinary_owner == owner_addr
        owner_refcount_after_ordinary_hit = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        ordinary_hit_ok = (
            ordinary_hit_rc
            and ordinary_owner == owner_addr
            and ordinary_node == expected_object_addr
            and owner_refcount_after_ordinary_hit == 4
        )
        result.update({
            "ordinary_hit_rc": bool(ordinary_hit_rc),
            "ordinary_hit_owner": ordinary_owner,
            "ordinary_hit_node": ordinary_node,
            "owner_refcount_after_ordinary_hit":
                owner_refcount_after_ordinary_hit,
            "ordinary_hit_ok": ordinary_hit_ok,
        })
        if not ordinary_hit_ok:
            result["status"] = "mismatch"
            return result

        ordinary_before_misses = engine.ql.mem.read(
            ordinary_holder_addr, 16)
        first_miss_rc = engine.call(
            engine.offset(PSBRAWNODE_GET_DICTIONARY_VALUE_OFFSET),
            ints=(root_holder_addr, first_miss_key_addr,
                  ordinary_holder_addr),
            ret="bool",
        )
        ordinary_after_first_miss = engine.ql.mem.read(
            ordinary_holder_addr, 16)
        second_miss_rc = engine.call(
            engine.offset(PSBRAWNODE_GET_DICTIONARY_VALUE_OFFSET),
            ints=(root_holder_addr, second_miss_key_addr,
                  ordinary_holder_addr),
            ret="bool",
        )
        ordinary_after_second_miss = engine.ql.mem.read(
            ordinary_holder_addr, 16)
        owner_refcount_after_misses = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        misses_ok = (
            not first_miss_rc
            and not second_miss_rc
            and ordinary_after_first_miss == ordinary_before_misses
            and ordinary_after_second_miss == ordinary_before_misses
            and owner_refcount_after_misses == 4
        )
        result.update({
            "first_helper_miss_rc": bool(first_miss_rc),
            "first_helper_miss_preserved_output":
                ordinary_after_first_miss == ordinary_before_misses,
            "second_helper_miss_rc": bool(second_miss_rc),
            "second_helper_miss_preserved_output":
                ordinary_after_second_miss == ordinary_before_misses,
            "owner_refcount_after_misses": owner_refcount_after_misses,
            "misses_ok": misses_ok,
        })
        if not misses_ok:
            ordinary_holder_owns_reference = (
                _read_pointer(engine, ordinary_holder_addr) == owner_addr)
            result["status"] = "mismatch"
            return result

        overwrite_rc = engine.call(
            engine.offset(PSBRAWNODE_GET_DICTIONARY_VALUE_OFFSET),
            ints=(root_holder_addr, overwrite_key_addr,
                  ordinary_holder_addr),
            ret="bool",
        )
        overwrite_owner, overwrite_node = struct.unpack(
            "<QQ", engine.ql.mem.read(ordinary_holder_addr, 16))
        ordinary_holder_owns_reference = overwrite_owner == owner_addr
        owner_refcount_after_overwrite = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        overwrite_ok = (
            overwrite_rc
            and overwrite_owner == owner_addr
            and overwrite_node == expected_version_addr
            and owner_refcount_after_overwrite == 4
        )
        result.update({
            "overwrite_rc": bool(overwrite_rc),
            "overwrite_owner": overwrite_owner,
            "overwrite_node": overwrite_node,
            "owner_refcount_after_overwrite":
                owner_refcount_after_overwrite,
            "overwrite_release_then_retain_net_zero":
                owner_refcount_after_overwrite == 4,
            "overwrite_ok": overwrite_ok,
        })
        if not overwrite_ok:
            result["status"] = "mismatch"
            return result

        contains_hit = engine.call(
            engine.offset(PSBRAWNODE_CONTAINS_DICTIONARY_KEY_OFFSET),
            ints=(root_holder_addr, hit_key_addr), ret="bool",
        )
        contains_first_miss = engine.call(
            engine.offset(PSBRAWNODE_CONTAINS_DICTIONARY_KEY_OFFSET),
            ints=(root_holder_addr, first_miss_key_addr), ret="bool",
        )
        contains_second_miss = engine.call(
            engine.offset(PSBRAWNODE_CONTAINS_DICTIONARY_KEY_OFFSET),
            ints=(root_holder_addr, second_miss_key_addr), ret="bool",
        )
        contains_non_dictionary = engine.call(
            engine.offset(PSBRAWNODE_CONTAINS_DICTIONARY_KEY_OFFSET),
            ints=(ordinary_holder_addr, hit_key_addr), ret="bool",
        )
        owner_refcount_after_contains = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        contains_ok = (
            contains_hit
            and not contains_first_miss
            and not contains_second_miss
            and not contains_non_dictionary
            and owner_refcount_after_contains == 4
        )
        result.update({
            "contains_hit": bool(contains_hit),
            "contains_first_helper_miss": bool(contains_first_miss),
            "contains_second_helper_miss": bool(contains_second_miss),
            "contains_non_dictionary": bool(contains_non_dictionary),
            "owner_refcount_after_contains": owner_refcount_after_contains,
            "contains_temporary_net_zero":
                owner_refcount_after_contains == 4,
            "contains_ok": contains_ok,
        })
        if not contains_ok:
            result["status"] = "mismatch"
            return result

        alias_rc = engine.call(
            engine.offset(PSBRAWNODE_GET_DICTIONARY_VALUE_OFFSET),
            ints=(root_holder_addr, hit_key_addr, root_holder_addr),
            ret="bool",
        )
        alias_owner, alias_node = struct.unpack(
            "<QQ", engine.ql.mem.read(root_holder_addr, 16))
        root_holder_owns_reference = alias_owner == owner_addr
        owner_refcount_after_alias = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        alias_before_miss = engine.ql.mem.read(root_holder_addr, 16)
        alias_miss_rc = engine.call(
            engine.offset(PSBRAWNODE_GET_DICTIONARY_VALUE_OFFSET),
            ints=(root_holder_addr, second_miss_key_addr,
                  root_holder_addr),
            ret="bool",
        )
        alias_after_miss = engine.ql.mem.read(root_holder_addr, 16)
        owner_refcount_after_alias_miss = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        isvalid_after_alias = engine.call(
            engine.offset(PSBRAWNODE_IS_VALID_OFFSET),
            ints=(root_holder_addr,), ret="bool",
        )
        alias_ok = (
            alias_rc
            and alias_owner == owner_addr
            and alias_node == expected_object_addr
            and owner_refcount_after_alias == 4
            and not alias_miss_rc
            and alias_after_miss == alias_before_miss
            and owner_refcount_after_alias_miss == 4
            and isvalid_after_alias
        )
        result.update({
            "alias_rc": bool(alias_rc),
            "alias_owner": alias_owner,
            "alias_node": alias_node,
            "owner_refcount_after_alias": owner_refcount_after_alias,
            "alias_release_then_reload_retain_net_zero":
                owner_refcount_after_alias == 4,
            "alias_miss_rc": bool(alias_miss_rc),
            "alias_miss_preserved_output":
                alias_after_miss == alias_before_miss,
            "owner_refcount_after_alias_miss":
                owner_refcount_after_alias_miss,
            "isvalid_after_alias": bool(isvalid_after_alias),
            "alias_ok": alias_ok,
        })
        if not alias_ok:
            result["status"] = "mismatch"
            return result

        release_refcounts = []
        release_slots = []
        for holder_addr, label in (
            (strict_holder_addr, "strict"),
            (ordinary_holder_addr, "ordinary"),
            (root_holder_addr, "alias-root"),
        ):
            engine.call(
                engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                ints=(holder_addr,), ret="void",
            )
            if label == "strict":
                strict_holder_owns_reference = False
            elif label == "ordinary":
                ordinary_holder_owns_reference = False
            else:
                root_holder_owns_reference = False
            release_refcounts.append(struct.unpack(
                "<I", engine.ql.mem.read(owner_addr, 4))[0])
            release_slots.append(_read_pointer(engine, holder_addr))

        engine.call(
            engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
            ints=(raw_holder_addr,), ret="void",
        )
        raw_holder_owns_reference = False
        raw_slot_after_terminal_release = _read_pointer(
            engine, raw_holder_addr)
        release_ok = (
            release_refcounts == [3, 2, 1]
            and release_slots == [owner_addr, owner_addr, owner_addr]
            and raw_slot_after_terminal_release == owner_addr
        )
        result.update({
            "release_refcounts": release_refcounts,
            "release_slots": release_slots,
            "raw_slot_after_terminal_release":
                raw_slot_after_terminal_release,
            "holder_release_leaves_slots_unchanged": (
                release_slots == [owner_addr, owner_addr, owner_addr]
                and raw_slot_after_terminal_release == owner_addr),
            "terminal_owner_release_invoked": True,
            "release_ok": release_ok,
        })
        ok = (
            baseline_ok and getroot_ok and isvalid_ok and strict_ok
            and ordinary_hit_ok and misses_ok and overwrite_ok
            and contains_ok and alias_ok and release_ok
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        for label, holder_addr, owns_reference in (
            ("strict result", strict_holder_addr,
             strict_holder_owns_reference),
            ("ordinary result", ordinary_holder_addr,
             ordinary_holder_owns_reference),
            ("root/alias result", root_holder_addr,
             root_holder_owns_reference),
            ("source PSBFile", raw_holder_addr,
             raw_holder_owns_reference),
        ):
            if not holder_addr or not owns_reference:
                continue
            try:
                engine.call(
                    engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                    ints=(holder_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"{label}: {exc!r}")
        if cleanup_errors:
            result["cleanup_errors"] = cleanup_errors
            if result.get("status") in {"ok", "mismatch"}:
                result["status"] = "error"


def run_raw_dictionary_keys_lifecycle_case(engine, input_path: Path) -> dict:
    """Observe the target's ordered gnustl ``vector<string>`` result.

    GetDictionaryKeys returns a live 24-byte three-pointer vector through X8.
    Its one-pointer COW strings and backing storage are inspected in place,
    then destroyed by libkrkr2's own emitted vector destructor.  No STL object
    crosses into the harness runtime.
    """
    data, input_format, declared_size = _input_info(input_path)
    decoded = _decoded_psb(data, input_format, declared_size)
    root_offset = struct.unpack_from("<I", decoded, 36)[0]
    if root_offset >= len(decoded) or decoded[root_offset] != 0x21:
        raise ValueError("raw Dictionary keys lifecycle requires root tag 0x21")
    root_member_pairs = _collection_members(decoded, root_offset)
    root_members = dict(root_member_pairs)
    if len(root_members) != len(root_member_pairs):
        raise ValueError("root Dictionary unexpectedly contains duplicate keys")
    object_offset = root_members.get("object")
    version_offset = root_members.get("version")
    if object_offset is None or decoded[object_offset] != 0x21:
        raise ValueError("natural root object Dictionary pin changed")
    if version_offset is None or decoded[version_offset] != 0x1E:
        raise ValueError("natural root version Real pin changed")
    root_keys = [name for name, _ in root_member_pairs]
    object_keys = [
        name for name, _ in _collection_members(decoded, object_offset)
    ]
    if not root_keys or not object_keys:
        raise ValueError("natural Dictionary key vectors unexpectedly became empty")

    engine.reset_heap()
    result = {
        "input": str(input_path),
        "entry": "raw-dictionary-keys-lifecycle",
        "input_format": input_format,
        "input_size": len(data),
        "decoded_size": len(decoded),
        "declared_size": declared_size,
        "root_offset": root_offset,
        "object_offset": object_offset,
        "version_offset": version_offset,
        "expected_root_keys": root_keys,
        "expected_object_keys": object_keys,
    }
    raw_holder_addr = 0
    root_holder_addr = 0
    object_holder_addr = 0
    version_holder_addr = 0
    raw_holder_owns_reference = False
    root_holder_owns_reference = False
    object_holder_owns_reference = False
    version_holder_owns_reference = False
    live_vectors: set[int] = set()
    try:
        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result.update({"loaded": bool(loaded), "owner": owner_addr})
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result
        raw_holder_owns_reference = True

        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        owner_refcount_before = struct.unpack_from("<I", owner, 0)[0]
        expected_root_addr = raw_addr + root_offset
        expected_object_addr = raw_addr + object_offset
        expected_version_addr = raw_addr + version_offset
        baseline_ok = (
            owner_refcount_before == 1
            and raw_size == declared_size
            and _read_pointer(engine, owner_addr + 80) == expected_root_addr
        )
        result.update({
            "owner_refcount_before_getroot": owner_refcount_before,
            "owner_raw_size": raw_size,
            "expected_root_address": expected_root_addr,
            "expected_object_address": expected_object_addr,
            "expected_version_address": expected_version_addr,
            "baseline_ok": baseline_ok,
        })
        if not baseline_ok:
            result["status"] = "mismatch"
            return result

        root_holder_addr = engine.heap.write(
            bytes(range(0x80, 0x90)), align=8)
        engine.call_sret(
            engine.offset(PSBFILE_GET_ROOT_OFFSET),
            result_addr=root_holder_addr,
            result_size=16,
            ints=(raw_holder_addr,),
        )
        root_owner, root_node = struct.unpack(
            "<QQ", engine.ql.mem.read(root_holder_addr, 16))
        root_holder_owns_reference = root_owner == owner_addr
        owner_refcount_after_getroot = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        getroot_ok = (
            root_owner == owner_addr
            and root_node == expected_root_addr
            and owner_refcount_after_getroot == 2
        )
        result.update({
            "root_owner": root_owner,
            "root_node": root_node,
            "owner_refcount_after_getroot": owner_refcount_after_getroot,
            "getroot_ok": getroot_ok,
        })
        if not getroot_ok:
            result["status"] = "mismatch"
            return result

        object_key_addr = engine.heap.write(b"object\0", align=8)
        version_key_addr = engine.heap.write(b"version\0", align=8)
        object_holder_addr = engine.heap.write(
            bytes(range(0xA0, 0xB0)), align=8)
        engine.call_sret(
            engine.offset(
                PSBRAWNODE_GET_DICTIONARY_VALUE_STRICT_OFFSET),
            result_addr=object_holder_addr,
            result_size=16,
            ints=(root_holder_addr, object_key_addr),
        )
        object_owner, object_node = struct.unpack(
            "<QQ", engine.ql.mem.read(object_holder_addr, 16))
        object_holder_owns_reference = object_owner == owner_addr
        object_ok = (
            object_owner == owner_addr
            and object_node == expected_object_addr
        )

        version_holder_addr = engine.heap.write(
            bytes(range(0xB0, 0xC0)), align=8)
        engine.call_sret(
            engine.offset(
                PSBRAWNODE_GET_DICTIONARY_VALUE_STRICT_OFFSET),
            result_addr=version_holder_addr,
            result_size=16,
            ints=(root_holder_addr, version_key_addr),
        )
        version_owner, version_node = struct.unpack(
            "<QQ", engine.ql.mem.read(version_holder_addr, 16))
        version_holder_owns_reference = version_owner == owner_addr
        owner_refcount_after_children = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        children_ok = (
            object_ok
            and version_owner == owner_addr
            and version_node == expected_version_addr
            and owner_refcount_after_children == 4
        )
        result.update({
            "object_owner": object_owner,
            "object_node": object_node,
            "version_owner": version_owner,
            "version_node": version_node,
            "owner_refcount_after_children": owner_refcount_after_children,
            "children_ok": children_ok,
        })
        if not children_ok:
            result["status"] = "mismatch"
            return result

        def capture_keys(
            label: str, node_holder_addr: int, expected_keys: list[str],
        ) -> dict[str, object]:
            vector_addr = engine.heap.write(
                bytes(range(0xC0, 0xD8)), align=8)
            refcount_before = struct.unpack(
                "<I", engine.ql.mem.read(owner_addr, 4))[0]
            engine.call_sret(
                engine.offset(PSBRAWNODE_GET_DICTIONARY_KEYS_OFFSET),
                result_addr=vector_addr,
                result_size=24,
                ints=(node_holder_addr,),
            )
            live_vectors.add(vector_addr)
            refcount_after_call = struct.unpack(
                "<I", engine.ql.mem.read(owner_addr, 4))[0]
            observation = _inspect_cow_string_vector(
                engine, vector_addr, expected_keys)
            header_before_dtor = bytes(
                engine.ql.mem.read(vector_addr, 24))
            live_vectors.remove(vector_addr)
            engine.call(
                engine.offset(STD_VECTOR_STRING_DTOR_OFFSET),
                ints=(vector_addr,), ret="void",
            )
            header_after_dtor = bytes(
                engine.ql.mem.read(vector_addr, 24))
            refcount_after_dtor = struct.unpack(
                "<I", engine.ql.mem.read(owner_addr, 4))[0]
            lifecycle_ok = (
                refcount_before == 4
                and refcount_after_call == 4
                and refcount_after_dtor == 4
                and header_after_dtor == header_before_dtor
            )
            observation.update({
                "label": label,
                "vector_addr": vector_addr,
                "owner_refcount_before": refcount_before,
                "owner_refcount_after_call": refcount_after_call,
                "owner_refcount_after_dtor": refcount_after_dtor,
                "destructor_leaves_header_unchanged":
                    header_after_dtor == header_before_dtor,
                "lifecycle_ok": lifecycle_ok,
                "ok": bool(observation.get("ok")) and lifecycle_ok,
            })
            return observation

        vector_results = {
            "root": capture_keys("root", root_holder_addr, root_keys),
            "object": capture_keys(
                "object", object_holder_addr, object_keys),
            "version_non_dictionary": capture_keys(
                "version-non-dictionary", version_holder_addr, []),
        }
        vectors_ok = all(
            bool(observation["ok"])
            for observation in vector_results.values()
        )
        result.update({
            "vectors": vector_results,
            "vectors_ok": vectors_ok,
        })
        if not vectors_ok:
            result["status"] = "mismatch"
            return result

        release_refcounts = []
        release_slots = []
        for holder_addr, label in (
            (object_holder_addr, "object"),
            (version_holder_addr, "version"),
            (root_holder_addr, "root"),
        ):
            engine.call(
                engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                ints=(holder_addr,), ret="void",
            )
            if label == "object":
                object_holder_owns_reference = False
            elif label == "version":
                version_holder_owns_reference = False
            else:
                root_holder_owns_reference = False
            release_refcounts.append(struct.unpack(
                "<I", engine.ql.mem.read(owner_addr, 4))[0])
            release_slots.append(_read_pointer(engine, holder_addr))

        engine.call(
            engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
            ints=(raw_holder_addr,), ret="void",
        )
        raw_holder_owns_reference = False
        raw_slot_after_terminal_release = _read_pointer(
            engine, raw_holder_addr)
        release_ok = (
            release_refcounts == [3, 2, 1]
            and release_slots == [owner_addr, owner_addr, owner_addr]
            and raw_slot_after_terminal_release == owner_addr
        )
        result.update({
            "release_refcounts": release_refcounts,
            "release_slots": release_slots,
            "raw_slot_after_terminal_release":
                raw_slot_after_terminal_release,
            "holder_release_leaves_slots_unchanged": release_ok,
            "terminal_owner_release_invoked": True,
            "release_ok": release_ok,
        })
        ok = (
            baseline_ok and getroot_ok and children_ok
            and vectors_ok and release_ok
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        for vector_addr in tuple(live_vectors):
            live_vectors.remove(vector_addr)
            try:
                engine.call(
                    engine.offset(STD_VECTOR_STRING_DTOR_OFFSET),
                    ints=(vector_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(
                    f"vector<string> at 0x{vector_addr:x}: {exc!r}")
        for label, holder_addr, owns_reference in (
            ("object child", object_holder_addr,
             object_holder_owns_reference),
            ("version child", version_holder_addr,
             version_holder_owns_reference),
            ("root", root_holder_addr, root_holder_owns_reference),
            ("source PSBFile", raw_holder_addr,
             raw_holder_owns_reference),
        ):
            if not holder_addr or not owns_reference:
                continue
            try:
                engine.call(
                    engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                    ints=(holder_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"{label}: {exc!r}")
        if cleanup_errors:
            result["cleanup_errors"] = cleanup_errors
            if result.get("status") in {"ok", "mismatch"}:
                result["status"] = "error"


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
    case_name: str,
    input_path: Path,
    remote_path: str,
    node_offset: int,
    expected_node_bytes: bytes,
    tjs_expression: str,
    expected_variant_value: int,
    expected_get_int: int,
) -> dict:
    """Observe one natural integer node through raw and TJS paths.

    The public TJS expression exercises PSBFile factory/load/root, Dictionary
    PropGet, Array PropGetByNum and
    PSBValueDispatch::CreateVariant.  A second load builds only the
    two-pointer Android PSBRawNode view needed to invoke the
    original raw GetInt/GetDouble entries at the same pinned file offset.
    """
    data, input_format, declared_size = _input_info(input_path)
    if input_format != "psb":
        raise ValueError("integer boundary mode requires a raw PSB input")
    if node_offset < 0 or node_offset + len(expected_node_bytes) > len(data):
        raise ValueError("integer boundary node lies outside the input")
    if not expected_node_bytes:
        raise ValueError("integer boundary node bytes must not be empty")
    expected_tag = expected_node_bytes[0]
    expected_size = INTEGER_NODE_SIZES.get(expected_tag)
    if expected_size is None or len(expected_node_bytes) != expected_size:
        raise ValueError(
            "integer boundary node must pin one complete tag-0x04..0x0c "
            "payload")
    if data[node_offset:node_offset + len(expected_node_bytes)] \
            != expected_node_bytes:
        raise ValueError("integer boundary input bytes do not match the pin")
    if not -0x80000000 <= expected_get_int <= 0x7FFFFFFF:
        raise ValueError("expected GetInt result does not fit signed 32 bits")
    derived_get_int = struct.unpack(
        "<i", struct.pack("<I", expected_variant_value & 0xFFFFFFFF))[0]
    if expected_get_int != derived_get_int:
        raise ValueError(
            "expected GetInt result must equal the Variant low signed word")
    if not case_name:
        raise ValueError("integer boundary case name must not be empty")
    case_name.encode("ascii")
    remote_path.encode("ascii")
    tjs_expression.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": f"integer-boundary-{case_name}",
        "input_format": input_format,
        "input_size": len(data),
        "declared_size": declared_size,
        "expected_tag": f"0x{expected_tag:02x}",
        "node_offset": node_offset,
        "expected_node_bytes": expected_node_bytes.hex(),
        "expected_variant_value": expected_variant_value,
        "expected_get_int": expected_get_int,
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
            "var oracle_psb_integer_file = new PSBFile("
            f"{json.dumps(remote_path)});\n"
            "var oracle_psb_integer_value = "
            f"{tjs_expression};"
        )
        # The first statement may publish the PSBFile global before a later
        # property lookup throws, so enable best-effort cleanup before Exec.
        globals_created = True
        engine.tjs_exec(script)
        variant_addr = engine.tjs_global("oracle_psb_integer_value")
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
        expected_x0 = expected_get_int & 0xFFFFFFFF

        result.update({
            "variant_type": variant_type,
            "variant_value": variant_value,
            "raw_node_bytes": node_bytes.hex(),
            "raw_get_int_x0": raw_get_int_x0,
            "raw_get_int_w32": raw_get_int_w32,
            "expected_get_int_x0": expected_x0,
            "raw_get_double": raw_get_double,
        })
        ok = (
            variant_type == TJS_VARIANT_INTEGER_TYPE
            and variant_value == expected_variant_value
            and node_bytes == expected_node_bytes
            and raw_get_int_x0 == expected_x0
            and raw_get_int_w32 == expected_get_int
            and raw_get_double == float(expected_variant_value)
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
                    "try { oracle_psb_integer_value = void; } catch(e) {} "
                    "try { oracle_psb_integer_file = void; } catch(e) {}")
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


def run_real_boundary_case(
    engine,
    *,
    case_name: str,
    input_path: Path,
    remote_path: str,
    node_offset: int,
    expected_node_bytes: bytes,
    expected_double_bits_le: bytes,
    tjs_expression: str,
) -> dict:
    """Observe a natural Real through CreateVariant and raw GetDouble."""
    data, input_format, declared_size = _input_info(input_path)
    decoded = _decoded_psb(data, input_format, declared_size)
    if not expected_node_bytes:
        raise ValueError("real boundary node bytes must not be empty")
    expected_tag = expected_node_bytes[0]
    expected_size = REAL_NODE_SIZES.get(expected_tag)
    if expected_size is None or len(expected_node_bytes) != expected_size:
        raise ValueError(
            "real boundary node must pin one complete tag-0x1d..0x1f payload")
    if node_offset < 0 or node_offset + expected_size > len(decoded):
        raise ValueError("real boundary node lies outside the decoded PSB")
    if decoded[node_offset:node_offset + expected_size] != expected_node_bytes:
        raise ValueError("real boundary input bytes do not match the pin")
    if len(expected_double_bits_le) != 8:
        raise ValueError("real boundary expected double must contain 8 bytes")
    if expected_tag == 0x1D:
        derived_value = 0.0
    elif expected_tag == 0x1E:
        derived_value = struct.unpack("<f", expected_node_bytes[1:])[0]
    else:
        derived_value = struct.unpack("<d", expected_node_bytes[1:])[0]
    if struct.pack("<d", derived_value) != expected_double_bits_le:
        raise ValueError("real boundary decoded value does not match the pin")
    if not case_name:
        raise ValueError("real boundary case name must not be empty")
    case_name.encode("ascii")
    remote_path.encode("ascii")
    tjs_expression.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    expected_value = struct.unpack("<d", expected_double_bits_le)[0]
    result = {
        "input": str(input_path),
        "entry": f"real-boundary-{case_name}",
        "input_format": input_format,
        "input_size": len(data),
        "decoded_size": len(decoded),
        "declared_size": declared_size,
        "expected_tag": f"0x{expected_tag:02x}",
        "node_offset": node_offset,
        "expected_node_bytes": expected_node_bytes.hex(),
        "expected_double_bits_le": expected_double_bits_le.hex(),
        "expected_double_hex": expected_value.hex(),
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
            "var oracle_psb_real_file = new PSBFile("
            f"{json.dumps(remote_path)});\n"
            "var oracle_psb_real_value = "
            f"{tjs_expression};"
        )
        globals_created = True
        engine.tjs_exec(script)
        variant_addr = engine.tjs_global("oracle_psb_real_value")
        variant = engine.ql.mem.read(variant_addr, TJS_VARIANT_SIZE)
        variant_type = struct.unpack_from("<I", variant, 16)[0]
        variant_bits = variant[:8]

        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result["raw_loaded"] = bool(loaded)
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result
        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        if node_offset + expected_size > raw_size:
            raise RuntimeError("loaded owner is shorter than the pinned node")
        node_addr = raw_addr + node_offset
        node_bytes = engine.ql.mem.read(node_addr, expected_size)
        raw_node_addr = engine.heap.write(
            struct.pack("<QQ", owner_addr, node_addr), align=8)
        raw_value = engine.call(
            engine.offset(PSBRAWNODE_GET_DOUBLE_OFFSET),
            ints=(raw_node_addr,), ret="double",
        )
        raw_bits = struct.pack("<d", raw_value)

        result.update({
            "variant_type": variant_type,
            "variant_double_bits_le": variant_bits.hex(),
            "variant_double_hex": struct.unpack("<d", variant_bits)[0].hex(),
            "raw_node_bytes": node_bytes.hex(),
            "raw_get_double_bits_le": raw_bits.hex(),
            "raw_get_double_hex": raw_value.hex(),
        })
        ok = (
            variant_type == TJS_VARIANT_REAL_TYPE
            and variant_bits == expected_double_bits_le
            and node_bytes == expected_node_bytes
            and raw_bits == expected_double_bits_le
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        if raw_holder_addr:
            try:
                engine.call(
                    engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                    ints=(raw_holder_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"raw owner: {exc!r}")
        if variant_addr:
            try:
                engine.call(
                    engine.offset(TJS_VARIANT_DTOR_OFFSET),
                    ints=(variant_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"TJS output Variant: {exc!r}")
        if globals_created:
            try:
                engine.tjs_exec(
                    "try { oracle_psb_real_value = void; } catch(e) {} "
                    "try { oracle_psb_real_file = void; } catch(e) {}")
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


def run_string_boundary_case(
    engine,
    *,
    case_name: str,
    input_path: Path,
    remote_path: str,
    node_offset: int,
    expected_node_bytes: bytes,
    string_data_offset: int,
    expected_string_size: int,
    expected_string_sha256: str,
    tjs_expression: str,
) -> dict:
    """Observe a natural String through copied TJS and borrowed raw paths."""
    data, input_format, declared_size = _input_info(input_path)
    decoded = _decoded_psb(data, input_format, declared_size)
    if not expected_node_bytes:
        raise ValueError("string boundary node bytes must not be empty")
    expected_tag = expected_node_bytes[0]
    expected_node_size = STRING_NODE_SIZES.get(expected_tag)
    if (expected_node_size is None
            or len(expected_node_bytes) != expected_node_size):
        raise ValueError("string boundary node must pin one complete String tag")
    if node_offset < 0 or node_offset + expected_node_size > len(decoded):
        raise ValueError("string boundary node lies outside the decoded PSB")
    if decoded[node_offset:node_offset + expected_node_size] \
            != expected_node_bytes:
        raise ValueError("string boundary input bytes do not match the pin")
    string_end = string_data_offset + expected_string_size
    if (string_data_offset < 0 or expected_string_size < 0
            or string_end >= len(decoded)):
        raise ValueError("string boundary bytes lie outside the decoded PSB")
    expected_string = decoded[string_data_offset:string_end]
    if decoded[string_end] != 0:
        raise ValueError("string boundary pin is not followed by NUL")
    expected_string_sha256 = expected_string_sha256.lower()
    if len(expected_string_sha256) != 64:
        raise ValueError("string boundary SHA-256 must contain 64 hex digits")
    try:
        bytes.fromhex(expected_string_sha256)
    except ValueError as exc:
        raise ValueError("string boundary SHA-256 is not hexadecimal") from exc
    if hashlib.sha256(expected_string).hexdigest() != expected_string_sha256:
        raise ValueError("string boundary host bytes do not match the pin")
    expected_text = expected_string.decode("utf-8")
    if not case_name:
        raise ValueError("string boundary case name must not be empty")
    case_name.encode("ascii")
    remote_path.encode("ascii")
    tjs_expression.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": f"string-boundary-{case_name}",
        "input_format": input_format,
        "input_size": len(data),
        "decoded_size": len(decoded),
        "declared_size": declared_size,
        "expected_tag": f"0x{expected_tag:02x}",
        "node_offset": node_offset,
        "expected_node_bytes": expected_node_bytes.hex(),
        "string_data_offset": string_data_offset,
        "expected_string_size": expected_string_size,
        "expected_string_sha256": expected_string_sha256,
        "expected_utf16_length": len(expected_text.encode("utf-16-le")) // 2,
        "script_engine_ready": script_engine != 0,
    }
    if script_engine == 0:
        result["status"] = "setup-failed"
        return result

    tjs_initialized = False
    globals_created = False
    globals_cleared = False
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
            "var oracle_psb_string_file = new PSBFile("
            f"{json.dumps(remote_path)});\n"
            "var oracle_psb_string_value = "
            f"{tjs_expression};"
        )
        globals_created = True
        engine.tjs_exec(script)
        variant_addr = engine.tjs_global("oracle_psb_string_value")
        engine.tjs_exec(
            "oracle_psb_string_value = void; "
            "oracle_psb_string_file = void;")
        globals_cleared = True
        public_string = _read_tjs_string_variant(engine, variant_addr)
        public_utf8 = public_string["utf8"]

        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result["raw_loaded"] = bool(loaded)
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result
        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        if node_offset + expected_node_size > raw_size:
            raise RuntimeError("loaded owner is shorter than the pinned node")
        node_addr = raw_addr + node_offset
        node_bytes = engine.ql.mem.read(node_addr, expected_node_size)
        raw_node_addr = engine.heap.write(
            struct.pack("<QQ", owner_addr, node_addr), align=8)
        raw_string_addr = engine.call(
            engine.offset(PSBRAWNODE_GET_STRING_OFFSET),
            ints=(raw_node_addr,), ret="ptr",
        )
        expected_raw_string_addr = raw_addr + string_data_offset
        raw_string = None
        raw_nul = False
        if (raw_addr <= raw_string_addr
                and raw_string_addr + expected_string_size
                < raw_addr + raw_size):
            raw_with_nul = engine.ql.mem.read(
                raw_string_addr, expected_string_size + 1)
            raw_string = raw_with_nul[:-1]
            raw_nul = raw_with_nul[-1] == 0

        public_sha256 = (
            hashlib.sha256(public_utf8).hexdigest()
            if public_utf8 is not None else None
        )
        raw_sha256 = (
            hashlib.sha256(raw_string).hexdigest()
            if raw_string is not None else None
        )
        result.update({
            "variant_type": public_string["variant_type"],
            "variant_string_addr": public_string["string_addr"],
            "variant_utf16_length": public_string["utf16_length"],
            "variant_utf8_size": (
                len(public_utf8) if public_utf8 is not None else None),
            "variant_utf8_sha256": public_sha256,
            "variant_utf8_hex_prefix": (
                public_utf8[:64].hex() if public_utf8 is not None else None),
            "public_value_survived_owner_release": public_utf8 is not None,
            "raw_node_bytes": node_bytes.hex(),
            "raw_get_string": raw_string_addr,
            "expected_raw_get_string": expected_raw_string_addr,
            "raw_string_size": (
                len(raw_string) if raw_string is not None else None),
            "raw_string_sha256": raw_sha256,
            "raw_string_nul_terminated": raw_nul,
        })
        ok = (
            public_string["variant_type"] == TJS_VARIANT_STRING_TYPE
            and public_utf8 == expected_string
            and node_bytes == expected_node_bytes
            and raw_string_addr == expected_raw_string_addr
            and raw_string == expected_string
            and raw_nul
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        if raw_holder_addr:
            try:
                engine.call(
                    engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                    ints=(raw_holder_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"raw owner: {exc!r}")
        if variant_addr:
            try:
                engine.call(
                    engine.offset(TJS_VARIANT_DTOR_OFFSET),
                    ints=(variant_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"TJS output Variant: {exc!r}")
        if globals_created and not globals_cleared:
            try:
                engine.tjs_exec(
                    "try { oracle_psb_string_value = void; } catch(e) {} "
                    "try { oracle_psb_string_file = void; } catch(e) {}")
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


def run_null_boundary_case(
    engine,
    *,
    case_name: str,
    input_path: Path,
    remote_path: str,
    node_offset: int,
    expected_node_bytes: bytes,
    tjs_expression: str,
) -> dict:
    """Observe one natural Null through public Variant and raw category paths."""
    data, input_format, declared_size = _input_info(input_path)
    decoded = _decoded_psb(data, input_format, declared_size)
    if len(expected_node_bytes) != 1:
        raise ValueError("null boundary must pin exactly one tag byte")
    expected_tag = expected_node_bytes[0]
    if expected_tag not in {0x01, 0x23, 0x24, 0x25, 0x26, 0x3F}:
        raise ValueError(f"tag 0x{expected_tag:02x} is not a Null tag")
    if node_offset < 0 or node_offset >= len(decoded):
        raise ValueError("null boundary node lies outside the decoded PSB")
    if decoded[node_offset:node_offset + 1] != expected_node_bytes:
        raise ValueError("null boundary input byte does not match the pin")
    if not case_name:
        raise ValueError("null boundary case name must not be empty")
    case_name.encode("ascii")
    remote_path.encode("ascii")
    tjs_expression.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": f"null-boundary-{case_name}",
        "input_format": input_format,
        "input_size": len(data),
        "decoded_size": len(decoded),
        "declared_size": declared_size,
        "expected_tag": f"0x{expected_tag:02x}",
        "node_offset": node_offset,
        "expected_node_bytes": expected_node_bytes.hex(),
        "expected_raw_category": 0,
        "script_engine_ready": script_engine != 0,
    }
    if script_engine == 0:
        result["status"] = "setup-failed"
        return result

    tjs_initialized = False
    globals_created = False
    globals_cleared = False
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
            "var oracle_psb_shape_file = new PSBFile("
            f"{json.dumps(remote_path)});\n"
            "var oracle_psb_shape_value = "
            f"{tjs_expression};"
        )
        globals_created = True
        engine.tjs_exec(script)
        variant_addr = engine.tjs_global("oracle_psb_shape_value")
        engine.tjs_exec(
            "oracle_psb_shape_value = void; "
            "oracle_psb_shape_file = void;")
        globals_cleared = True
        public_variant = engine.ql.mem.read(variant_addr, TJS_VARIANT_SIZE)
        variant_type = struct.unpack_from("<I", public_variant, 16)[0]

        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result["raw_loaded"] = bool(loaded)
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result
        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        owner_refcount = struct.unpack_from("<I", owner, 0)[0]
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        if node_offset + 1 > raw_size:
            raise RuntimeError("loaded owner is shorter than the pinned node")
        node_addr = raw_addr + node_offset
        node_bytes = engine.ql.mem.read(node_addr, 1)
        raw_node_addr = engine.heap.write(
            struct.pack("<QQ", owner_addr, node_addr), align=8)
        raw_category = engine.call(
            engine.offset(PSBRAWNODE_GET_TYPE_CATEGORY_OFFSET),
            ints=(raw_node_addr,), ret="int",
        )
        result.update({
            "variant_type": variant_type,
            "public_value_survived_owner_release": variant_type == 0,
            "raw_owner_refcount": owner_refcount,
            "raw_owner_size": raw_size,
            "raw_node_bytes": node_bytes.hex(),
            "raw_category": raw_category,
            "globals_cleared_before_public_read": globals_cleared,
        })
        ok = (
            variant_type == 0
            and owner_refcount == 1
            and raw_size == declared_size
            and node_bytes == expected_node_bytes
            and raw_category == 0
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        if raw_holder_addr:
            try:
                engine.call(
                    engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                    ints=(raw_holder_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"raw owner: {exc!r}")
        if variant_addr:
            try:
                engine.call(
                    engine.offset(TJS_VARIANT_DTOR_OFFSET),
                    ints=(variant_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"TJS output Variant: {exc!r}")
        if globals_created and not globals_cleared:
            try:
                engine.tjs_exec(
                    "try { oracle_psb_shape_value = void; } catch(e) {} "
                    "try { oracle_psb_shape_file = void; } catch(e) {}")
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


def run_collection_lifecycle_case(
    engine,
    *,
    case_name: str,
    input_path: Path,
    remote_path: str,
    node_offset: int,
    expected_entry_count: int,
    expected_table_byte_size: int,
    expected_node_prefix: bytes,
    tjs_expression: str,
    probe_expression: str,
    expected_probe_type: int,
    expected_probe_value: int | None = None,
    expected_probe_bits_le: bytes | None = None,
    expected_negative_index_value: int | None = None,
) -> dict:
    """Observe Array/Dictionary dispatch and owner intrusive lifetimes."""
    data, input_format, declared_size = _input_info(input_path)
    decoded = _decoded_psb(data, input_format, declared_size)
    tag, entry_count, table_byte_size = _collection_header(
        decoded, node_offset)
    expected_category = {0x20: 6, 0x21: 7}[tag]
    expected_class_name = {0x20: "Array", 0x21: "Dictionary"}[tag]
    other_class_name = {0x20: "Dictionary", 0x21: "Array"}[tag]
    if not expected_node_prefix or expected_node_prefix[0] != tag:
        raise ValueError("collection prefix must start with the pinned tag")
    if node_offset + len(expected_node_prefix) > len(decoded):
        raise ValueError("collection prefix lies outside the decoded PSB")
    if decoded[node_offset:node_offset + len(expected_node_prefix)] \
            != expected_node_prefix:
        raise ValueError("collection input prefix does not match the pin")
    if entry_count != expected_entry_count:
        raise ValueError(
            f"collection count changed: {entry_count} != "
            f"{expected_entry_count}")
    if table_byte_size != expected_table_byte_size:
        raise ValueError(
            f"collection table size changed: {table_byte_size} != "
            f"{expected_table_byte_size}")
    if expected_table_byte_size < len(expected_node_prefix):
        raise ValueError("collection prefix is longer than its packed table")
    if expected_probe_type == TJS_VARIANT_INTEGER_TYPE:
        if expected_probe_value is None or expected_probe_bits_le is not None:
            raise ValueError("Integer probe requires only expected_probe_value")
    elif expected_probe_type == TJS_VARIANT_REAL_TYPE:
        if (expected_probe_value is not None
                or expected_probe_bits_le is None
                or len(expected_probe_bits_le) != 8):
            raise ValueError("Real probe requires one 8-byte expected payload")
    else:
        raise ValueError("collection probe must be Integer or Real")
    if tag == 0x20 and expected_negative_index_value is None:
        raise ValueError("Array boundary requires a negative-index value pin")
    if tag == 0x21 and expected_negative_index_value is not None:
        raise ValueError("Dictionary boundary has no negative-index value")
    negative_index_node_offset = None
    negative_index_node_bytes = None
    if tag == 0x20:
        if entry_count == 0:
            raise ValueError("negative-index boundary requires non-empty Array")
        relative_offset = _packed_array_value(
            decoded, node_offset + 1, entry_count - 1)
        negative_index_node_offset = (
            node_offset + table_byte_size + relative_offset)
        negative_value, negative_index_node_bytes = _integer_node_value(
            decoded, negative_index_node_offset)
        if negative_value != expected_negative_index_value:
            raise ValueError(
                "negative-index host value does not match the pin")
    missing_member_name = "__psbfile_oracle_missing__"
    collection_members = _collection_members(decoded, node_offset)
    if len(collection_members) != entry_count:
        raise ValueError("collection enumeration count changed")
    expected_enum_names = [name for name, _ in collection_members]
    expected_enum_types = [
        _tjs_type_name_for_node(decoded, child_offset)
        for _, child_offset in collection_members
    ]
    expected_enum_names_text = "".join(
        f"{name}\n" for name in expected_enum_names)
    expected_enum_types_text = "".join(
        f"{type_name}\n" for type_name in expected_enum_types)
    if (tag == 0x21
            and missing_member_name in expected_enum_names):
        raise ValueError("dictionary missing-member pin unexpectedly exists")
    if not case_name:
        raise ValueError("collection boundary case name must not be empty")
    case_name.encode("ascii")
    remote_path.encode("ascii")
    tjs_expression.encode("ascii")
    probe_expression.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": f"collection-boundary-{case_name}",
        "input_format": input_format,
        "input_size": len(data),
        "decoded_size": len(decoded),
        "declared_size": declared_size,
        "expected_tag": f"0x{tag:02x}",
        "node_offset": node_offset,
        "entry_count": entry_count,
        "table_byte_size": table_byte_size,
        "expected_node_prefix": expected_node_prefix.hex(),
        "expected_raw_category": expected_category,
        "expected_class_name": expected_class_name,
        "expected_probe_type": expected_probe_type,
        "missing_member_name": missing_member_name,
        "expected_enum_names": expected_enum_names,
        "expected_enum_types": expected_enum_types,
        "script_engine_ready": script_engine != 0,
    }
    if expected_probe_value is not None:
        result["expected_probe_value"] = expected_probe_value
    if expected_probe_bits_le is not None:
        result["expected_probe_bits_le"] = expected_probe_bits_le.hex()
    if expected_negative_index_value is not None:
        result["expected_negative_index_value"] = \
            expected_negative_index_value
        result["negative_index_node_offset"] = negative_index_node_offset
        result["negative_index_node_bytes"] = \
            negative_index_node_bytes.hex()
    if script_engine == 0:
        result["status"] = "setup-failed"
        return result

    tjs_initialized = False
    globals_created = False
    globals_cleared = False
    file_global_cleared = False
    variant_addr = 0
    probe_addr = 0
    enum_value_callback_addr = 0
    enum_no_value_callback_addr = 0
    direct_variant_addrs: list[int] = []
    clear_shape_globals_source = (
        "try { global.oracle_psb_enum_value_callback = void; } catch(e) {} "
        "try { global.oracle_psb_enum_no_value_callback = void; } catch(e) {} "
        "try { global.oracle_psb_enum_value_names = void; } catch(e) {} "
        "try { global.oracle_psb_enum_value_types = void; } catch(e) {} "
        "try { global.oracle_psb_enum_value_bad = void; } catch(e) {} "
        "try { global.oracle_psb_enum_no_value_names = void; } catch(e) {} "
        "try { global.oracle_psb_enum_no_value_bad = void; } catch(e) {} "
        "try { global.oracle_psb_shape_probe = void; } catch(e) {} "
        "try { global.oracle_psb_shape_value = void; } catch(e) {} "
        "try { global.oracle_psb_shape_file = void; } catch(e) {}"
    )
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

        value_script = (
            "global.oracle_psb_shape_file = new PSBFile("
            f"{json.dumps(remote_path)});\n"
            "global.oracle_psb_shape_value = "
            f"{tjs_expression};\n"
            "global.oracle_psb_shape_file = void;\n"
            "global.oracle_psb_shape_probe = "
            f"{probe_expression};"
        )
        callback_script = (
            "global.oracle_psb_enum_value_names = \"\";\n"
            "global.oracle_psb_enum_value_types = \"\";\n"
            "global.oracle_psb_enum_value_bad = \"\";\n"
            "function oracle_psb_enum_value_callback(args*) {\n"
            "  global.oracle_psb_enum_value_names = "
            "global.oracle_psb_enum_value_names + args[0] + \"\\n\";\n"
            "  global.oracle_psb_enum_value_types = "
            "global.oracle_psb_enum_value_types + "
            "(typeof args[2]) + \"\\n\";\n"
            "  if(args.count != 3) "
            "global.oracle_psb_enum_value_bad = "
            "global.oracle_psb_enum_value_bad + \"argc;\";\n"
            "  if(args[1] != 0) "
            "global.oracle_psb_enum_value_bad = "
            "global.oracle_psb_enum_value_bad + \"flags;\";\n"
            "  if(this !== global.oracle_psb_shape_value) "
            "global.oracle_psb_enum_value_bad = "
            "global.oracle_psb_enum_value_bad + \"this;\";\n"
            "  return -777;\n"
            "}\n"
            "global.oracle_psb_enum_no_value_names = \"\";\n"
            "global.oracle_psb_enum_no_value_bad = \"\";\n"
            "function oracle_psb_enum_no_value_callback(args*) {\n"
            "  global.oracle_psb_enum_no_value_names = "
            "global.oracle_psb_enum_no_value_names + args[0] + \"\\n\";\n"
            "  if(args.count != 2) "
            "global.oracle_psb_enum_no_value_bad = "
            "global.oracle_psb_enum_no_value_bad + \"argc;\";\n"
            "  if(args[1] != 0) "
            "global.oracle_psb_enum_no_value_bad = "
            "global.oracle_psb_enum_no_value_bad + \"flags;\";\n"
            "  if(this !== global.oracle_psb_shape_value) "
            "global.oracle_psb_enum_no_value_bad = "
            "global.oracle_psb_enum_no_value_bad + \"this;\";\n"
            "  return -778;\n"
            "}"
        )
        globals_created = True
        engine.tjs_exec(value_script)
        file_global_cleared = True
        engine.tjs_exec(callback_script)
        variant_addr = engine.tjs_global("oracle_psb_shape_value")
        probe_addr = engine.tjs_global("oracle_psb_shape_probe")
        enum_value_callback_addr = engine.tjs_global(
            "oracle_psb_enum_value_callback")
        direct_variant_addrs.append(enum_value_callback_addr)
        enum_no_value_callback_addr = engine.tjs_global(
            "oracle_psb_enum_no_value_callback")
        direct_variant_addrs.append(enum_no_value_callback_addr)

        variant = engine.ql.mem.read(variant_addr, TJS_VARIANT_SIZE)
        object_addr, objthis_addr = struct.unpack_from("<QQ", variant, 0)
        variant_type = struct.unpack_from("<I", variant, 16)[0]
        probe = engine.ql.mem.read(probe_addr, TJS_VARIANT_SIZE)
        probe_type = struct.unpack_from("<I", probe, 16)[0]
        probe_value = None
        probe_bits_le = None
        if probe_type == TJS_VARIANT_INTEGER_TYPE:
            probe_value = struct.unpack_from("<q", probe, 0)[0]
        elif probe_type == TJS_VARIANT_REAL_TYPE:
            probe_bits_le = probe[:8]
        result.update({
            "variant_type": variant_type,
            "dispatch_address": object_addr,
            "objthis_address": objthis_addr,
            "object_equals_objthis": object_addr == objthis_addr,
            "probe_type": probe_type,
            "probe_value": probe_value,
            "probe_bits_le": (
                probe_bits_le.hex() if probe_bits_le is not None else None),
            "file_global_cleared_before_dispatch_read": file_global_cleared,
        })
        if (variant_type != TJS_VARIANT_OBJECT_TYPE
                or object_addr == 0
                or object_addr != objthis_addr):
            result["status"] = "mismatch"
            return result

        enum_value_callback = engine.ql.mem.read(
            enum_value_callback_addr, TJS_VARIANT_SIZE)
        enum_value_object, enum_value_objthis = struct.unpack_from(
            "<QQ", enum_value_callback, 0)
        enum_value_type = struct.unpack_from(
            "<I", enum_value_callback, 16)[0]
        enum_no_value_callback = engine.ql.mem.read(
            enum_no_value_callback_addr, TJS_VARIANT_SIZE)
        enum_no_value_object, enum_no_value_objthis = struct.unpack_from(
            "<QQ", enum_no_value_callback, 0)
        enum_no_value_type = struct.unpack_from(
            "<I", enum_no_value_callback, 16)[0]
        result.update({
            "enum_value_callback_type": enum_value_type,
            "enum_value_callback_object": enum_value_object,
            "enum_value_callback_original_objthis": enum_value_objthis,
            "enum_no_value_callback_type": enum_no_value_type,
            "enum_no_value_callback_object": enum_no_value_object,
            "enum_no_value_callback_original_objthis":
                enum_no_value_objthis,
            "enum_callback_objthis_forced_null": True,
        })
        if (enum_value_type != TJS_VARIANT_OBJECT_TYPE
                or enum_value_object == 0
                or enum_no_value_type != TJS_VARIANT_OBJECT_TYPE
                or enum_no_value_object == 0):
            result["status"] = "mismatch"
            return result
        enum_value_closure_addr = engine.heap.write(
            struct.pack("<QQ", enum_value_object, 0), align=8)
        enum_no_value_closure_addr = engine.heap.write(
            struct.pack("<QQ", enum_no_value_object, 0), align=8)

        dispatch = engine.ql.mem.read(
            object_addr, PSBVALUE_DISPATCH_SIZE)
        primary_vptr, secondary_vptr = struct.unpack_from("<QQ", dispatch, 0)
        dispatch_refcount_before = struct.unpack_from("<I", dispatch, 16)[0]
        owner_addr = struct.unpack_from("<Q", dispatch, 24)[0]
        node_addr = struct.unpack_from("<Q", dispatch, 32)[0]
        dispatch_valid = dispatch[40]
        expected_primary_vptr = engine.offset(
            PSBVALUE_DISPATCH_PRIMARY_VTABLE_OFFSET)
        expected_secondary_vptr = engine.offset(
            PSBVALUE_DISPATCH_SECONDARY_VTABLE_OFFSET)
        expected_primary_vtable_entries = [
            engine.offset(offset)
            for offset in PSBVALUE_DISPATCH_PRIMARY_VTABLE_ENTRY_OFFSETS
        ]
        primary_vtable_entries = None
        primary_offset_to_top = None
        primary_rtti = None
        primary_native_instance_entry = None
        primary_construct_entry = None
        primary_native_invalidate_entry = None
        primary_native_destruct_entry = None
        secondary_offset_to_top = None
        secondary_rtti = None
        secondary_construct_entry = None
        secondary_native_invalidate_entry = None
        secondary_native_destruct_entry = None
        if (primary_vptr == expected_primary_vptr
                and secondary_vptr == expected_secondary_vptr):
            primary_vtable_entries = [
                _read_pointer(engine, primary_vptr + index * 8)
                for index in range(len(
                    PSBVALUE_DISPATCH_PRIMARY_VTABLE_ENTRY_OFFSETS))
            ]
            primary_offset_to_top = struct.unpack(
                "<q", engine.ql.mem.read(primary_vptr - 16, 8))[0]
            primary_rtti = _read_pointer(engine, primary_vptr - 8)
            primary_native_instance_entry = _read_pointer(
                engine, primary_vptr + 25 * 8)
            primary_construct_entry = _read_pointer(
                engine, primary_vptr + 29 * 8)
            primary_native_invalidate_entry = _read_pointer(
                engine, primary_vptr + 30 * 8)
            primary_native_destruct_entry = _read_pointer(
                engine, primary_vptr + 31 * 8)
            secondary_offset_to_top = struct.unpack(
                "<q", engine.ql.mem.read(secondary_vptr - 16, 8))[0]
            secondary_rtti = _read_pointer(engine, secondary_vptr - 8)
            secondary_construct_entry = _read_pointer(
                engine, secondary_vptr)
            secondary_native_invalidate_entry = _read_pointer(
                engine, secondary_vptr + 8)
            secondary_native_destruct_entry = _read_pointer(
                engine, secondary_vptr + 16)
        result.update({
            "dispatch_primary_vptr": primary_vptr,
            "dispatch_secondary_vptr": secondary_vptr,
            "expected_dispatch_primary_vptr": expected_primary_vptr,
            "expected_dispatch_secondary_vptr": expected_secondary_vptr,
            "primary_vtable_entries": primary_vtable_entries,
            "expected_primary_vtable_entries":
                expected_primary_vtable_entries,
            "primary_offset_to_top": primary_offset_to_top,
            "primary_rtti": primary_rtti,
            "primary_native_instance_entry":
                primary_native_instance_entry,
            "primary_construct_entry": primary_construct_entry,
            "primary_native_invalidate_entry":
                primary_native_invalidate_entry,
            "primary_native_destruct_entry":
                primary_native_destruct_entry,
            "secondary_offset_to_top": secondary_offset_to_top,
            "secondary_rtti": secondary_rtti,
            "secondary_construct_entry": secondary_construct_entry,
            "secondary_native_invalidate_entry":
                secondary_native_invalidate_entry,
            "secondary_native_destruct_entry":
                secondary_native_destruct_entry,
            "dispatch_refcount_before_global_release":
                dispatch_refcount_before,
            "dispatch_owner": owner_addr,
            "dispatch_node": node_addr,
            "dispatch_valid": dispatch_valid,
        })
        if owner_addr == 0 or node_addr == 0:
            result["status"] = "mismatch"
            return result

        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        owner_refcount_before = struct.unpack_from("<I", owner, 0)[0]
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        expected_node_addr = raw_addr + node_offset
        node_in_owner = (
            raw_addr <= node_addr
            and node_addr + len(expected_node_prefix) <= raw_addr + raw_size
        )
        node_prefix = (
            engine.ql.mem.read(node_addr, len(expected_node_prefix))
            if node_in_owner else b"")
        raw_category = None
        if node_in_owner:
            raw_node_addr = engine.heap.write(
                struct.pack("<QQ", owner_addr, node_addr), align=8)
            raw_category = engine.call(
                engine.offset(PSBRAWNODE_GET_TYPE_CATEGORY_OFFSET),
                ints=(raw_node_addr,), ret="int",
            )
        result.update({
            "owner_refcount_before_global_release": owner_refcount_before,
            "owner_raw_address": raw_addr,
            "owner_raw_size": raw_size,
            "expected_dispatch_node": expected_node_addr,
            "dispatch_node_in_owner": node_in_owner,
            "node_prefix": node_prefix.hex(),
            "raw_category": raw_category,
        })

        def run_native_lifecycle_probe() -> dict[str, object]:
            dispatch_before = engine.ql.mem.read(
                object_addr, PSBVALUE_DISPATCH_SIZE)
            owner_ref_before = struct.unpack(
                "<I", engine.ql.mem.read(owner_addr, 4))[0]
            construct_primary_rc = _signed_w32(engine.call(
                engine.offset(PSBVALUE_DISPATCH_CONSTRUCT_PRIMARY_OFFSET),
                ints=(object_addr, 0xFFFFFF85, 0x12345678, 0x23456789),
                ret="int",
            ))
            construct_secondary_rc = _signed_w32(engine.call(
                engine.offset(PSBVALUE_DISPATCH_CONSTRUCT_SECONDARY_OFFSET),
                ints=(object_addr + 8, 0xFFFFFF85,
                      0x12345678, 0x23456789),
                ret="int",
            ))
            engine.call(
                engine.offset(
                    PSBVALUE_DISPATCH_NATIVE_INVALIDATE_PRIMARY_OFFSET),
                ints=(object_addr,), ret="void",
            )
            engine.call(
                engine.offset(
                    PSBVALUE_DISPATCH_NATIVE_INVALIDATE_SECONDARY_OFFSET),
                ints=(object_addr + 8,), ret="void",
            )
            engine.call(
                engine.offset(
                    PSBVALUE_DISPATCH_NATIVE_DESTRUCT_PRIMARY_OFFSET),
                ints=(object_addr,), ret="void",
            )
            engine.call(
                engine.offset(
                    PSBVALUE_DISPATCH_NATIVE_DESTRUCT_SECONDARY_OFFSET),
                ints=(object_addr + 8,), ret="void",
            )
            dispatch_after = engine.ql.mem.read(
                object_addr, PSBVALUE_DISPATCH_SIZE)
            owner_ref_after = struct.unpack(
                "<I", engine.ql.mem.read(owner_addr, 4))[0]
            return {
                "construct_primary_rc": construct_primary_rc,
                "construct_secondary_rc": construct_secondary_rc,
                "dispatch_unchanged": dispatch_after == dispatch_before,
                "owner_refcount_before": owner_ref_before,
                "owner_refcount_after": owner_ref_after,
            }

        unsupported_output_sentinel = bytes(range(64))
        unsupported_output_addr = engine.heap.write(
            unsupported_output_sentinel, align=8)

        def run_unsupported_probe() -> dict[str, object]:
            engine.ql.mem.write(
                unsupported_output_addr, unsupported_output_sentinel)
            return_codes = {}
            transitions = []
            # A long Frida trace may let Full TJS compact unrelated persistent
            # Variant-stack temporaries between these direct calls.  Preserve
            # every local transition as diagnostics, but compare the dispatch
            # structure with only its independently changing refcount removed.
            for key, offset, arg_count in (
                    PSBVALUE_DISPATCH_UNSUPPORTED_PROBES):
                dispatch_before = engine.ql.mem.read(
                    object_addr, PSBVALUE_DISPATCH_SIZE)
                owner_ref_before = struct.unpack(
                    "<I", engine.ql.mem.read(owner_addr, 4))[0]
                args = (object_addr,) + (
                    unsupported_output_addr,) * (arg_count - 1)
                return_codes[key] = _signed_w32(engine.call(
                    engine.offset(offset), ints=args, ret="int"))
                dispatch_after = engine.ql.mem.read(
                    object_addr, PSBVALUE_DISPATCH_SIZE)
                owner_ref_after = struct.unpack(
                    "<I", engine.ql.mem.read(owner_addr, 4))[0]
                transitions.append({
                    "slot": key,
                    "dispatch_structure_unchanged": (
                        dispatch_before[:16] == dispatch_after[:16]
                        and dispatch_before[20:] == dispatch_after[20:]
                    ),
                    "dispatch_refcount_before": struct.unpack_from(
                        "<I", dispatch_before, 16)[0],
                    "dispatch_refcount_after": struct.unpack_from(
                        "<I", dispatch_after, 16)[0],
                    "owner_refcount_before": owner_ref_before,
                    "owner_refcount_after": owner_ref_after,
                })
            output_after = engine.ql.mem.read(
                unsupported_output_addr, len(unsupported_output_sentinel))
            return {
                "return_codes": return_codes,
                "output_unchanged":
                    output_after == unsupported_output_sentinel,
                "dispatch_structure_unchanged": all(
                    item["dispatch_structure_unchanged"]
                    for item in transitions
                ),
                "refcount_observer_stable": all(
                    item["dispatch_refcount_before"] ==
                        item["dispatch_refcount_after"]
                    and item["owner_refcount_before"] ==
                        item["owner_refcount_after"]
                    for item in transitions
                ),
                "refcount_transitions": transitions,
            }

        unsupported_before_invalidate = run_unsupported_probe()
        native_lifecycle_before_invalidate = run_native_lifecycle_probe()

        native_class_id_slot = engine.offset(
            PSBVALUE_CLASS_ID_SLOT_OFFSET)
        native_class_id_before = struct.unpack(
            "<i", engine.ql.mem.read(native_class_id_slot, 4))[0]
        native_pointer_sentinel = 0x1122334455667788
        native_pointer_addr = engine.heap.write(
            struct.pack("<Q", native_pointer_sentinel), align=8)
        native_non_get_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_NATIVE_INSTANCE_SUPPORT_OFFSET),
            ints=(object_addr, 0, 0, native_pointer_addr),
            ret="int",
        ))
        native_class_id_after_non_get = struct.unpack(
            "<i", engine.ql.mem.read(native_class_id_slot, 4))[0]
        native_pointer_after_non_get = _read_pointer(
            engine, native_pointer_addr)

        if native_class_id_before == 0:
            native_mismatch_class_id = 0
        else:
            native_mismatch_class_id = _signed_w32(
                (native_class_id_before & 0xFFFFFFFF) ^ 0x80000000)
        engine.ql.mem.write(
            native_pointer_addr, struct.pack("<Q", native_pointer_sentinel))
        native_mismatch_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_NATIVE_INSTANCE_SUPPORT_OFFSET),
            ints=(object_addr, TJS_NIS_GETINSTANCE,
                  native_mismatch_class_id, native_pointer_addr),
            ret="int",
        ))
        native_class_id_after_mismatch = struct.unpack(
            "<i", engine.ql.mem.read(native_class_id_slot, 4))[0]
        native_pointer_after_mismatch = _read_pointer(
            engine, native_pointer_addr)

        engine.ql.mem.write(
            native_pointer_addr, struct.pack("<Q", native_pointer_sentinel))
        dispatch_refcount_before_native_success = struct.unpack(
            "<I", engine.ql.mem.read(object_addr + 16, 4))[0]
        owner_refcount_before_native_success = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        native_success_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_NATIVE_INSTANCE_SUPPORT_OFFSET),
            ints=(object_addr, TJS_NIS_GETINSTANCE,
                  native_class_id_after_mismatch, native_pointer_addr),
            ret="int",
        ))
        native_pointer_after_success = _read_pointer(
            engine, native_pointer_addr)
        dispatch_refcount_after_native_success = struct.unpack(
            "<I", engine.ql.mem.read(object_addr + 16, 4))[0]
        owner_refcount_after_native_success = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]

        enum_value_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ENUMMEMBERS_OFFSET),
            ints=(object_addr, 0, enum_value_closure_addr, 0),
            ret="int",
        ))
        enum_no_value_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ENUMMEMBERS_OFFSET),
            ints=(object_addr, TJS_ENUM_NO_VALUE,
                  enum_no_value_closure_addr, 0),
            ret="int",
        ))

        count_sentinel = 0x51525354
        variant_sentinel = 0x11223344556677
        membername_addr = engine.heap.write(b"\0\0", align=8)
        count_member_addr = engine.heap.write(
            "count\0".encode("utf-16-le"), align=8)
        missing_member_addr = engine.heap.write(
            (missing_member_name + "\0").encode("utf-16-le"), align=8)
        expected_class_addr = engine.heap.write(
            (expected_class_name + "\0").encode("utf-16-le"), align=8)
        other_class_addr = engine.heap.write(
            (other_class_name + "\0").encode("utf-16-le"), align=8)
        case_mismatch_class_addr = engine.heap.write(
            (expected_class_name.lower() + "\0").encode("utf-16-le"),
            align=8,
        )
        instance_hint_sentinel = 0x41424344
        instance_hint_addr = engine.heap.write(
            struct.pack("<I", instance_hint_sentinel), align=8)
        instance_true_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ISINSTANCEOF_OFFSET),
            ints=(object_addr, 0x12345678, 0, instance_hint_addr,
                  expected_class_addr, 0),
            ret="int",
        ))
        instance_other_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ISINSTANCEOF_OFFSET),
            ints=(object_addr, 0, 0, instance_hint_addr,
                  other_class_addr, object_addr),
            ret="int",
        ))
        instance_case_mismatch_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ISINSTANCEOF_OFFSET),
            ints=(object_addr, 0, 0, instance_hint_addr,
                  case_mismatch_class_addr, object_addr),
            ret="int",
        ))
        instance_named_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ISINSTANCEOF_OFFSET),
            ints=(object_addr, 0x12345678, membername_addr,
                  instance_hint_addr, 0, 0),
            ret="int",
        ))

        prop_count_addr = 0
        prop_count_rc = None
        prop_count_variant = None
        if tag == 0x20:
            prop_count_addr = engine.heap.write(
                _integer_variant_bytes(variant_sentinel), align=8)
            direct_variant_addrs.append(prop_count_addr)
            prop_count_rc = _signed_w32(engine.call(
                engine.offset(PSBVALUE_DISPATCH_PROPGET_OFFSET),
                ints=(object_addr, 0, count_member_addr, 0,
                      prop_count_addr, object_addr),
                ret="int",
            ))
            prop_count_variant = _variant_snapshot(engine, prop_count_addr)
        prop_miss_addr = engine.heap.write(
            _integer_variant_bytes(variant_sentinel), align=8)
        prop_strict_miss_addr = engine.heap.write(
            _integer_variant_bytes(variant_sentinel), align=8)
        prop_null_name_addr = engine.heap.write(
            _integer_variant_bytes(variant_sentinel), align=8)
        direct_variant_addrs.extend((
            prop_miss_addr, prop_strict_miss_addr, prop_null_name_addr))
        prop_miss_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_PROPGET_OFFSET),
            ints=(object_addr, 0, missing_member_addr, 0,
                  prop_miss_addr, object_addr),
            ret="int",
        ))
        prop_strict_miss_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_PROPGET_OFFSET),
            ints=(object_addr, TJS_MEMBERMUSTEXIST, missing_member_addr, 0,
                  prop_strict_miss_addr, object_addr),
            ret="int",
        ))
        prop_null_name_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_PROPGET_OFFSET),
            ints=(object_addr, 0, 0, 0, prop_null_name_addr, object_addr),
            ret="int",
        ))
        prop_miss_variant = _variant_snapshot(engine, prop_miss_addr)
        prop_strict_miss_variant = _variant_snapshot(
            engine, prop_strict_miss_addr)
        prop_null_name_variant = _variant_snapshot(
            engine, prop_null_name_addr)

        count_addr = engine.heap.write(
            struct.pack("<i", count_sentinel), align=8)
        count_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_GETCOUNT_OFFSET),
            ints=(object_addr, count_addr, 0, 0, object_addr),
            ret="int",
        ))
        count_value = struct.unpack(
            "<i", engine.ql.mem.read(count_addr, 4))[0]
        named_count_addr = engine.heap.write(
            struct.pack("<i", count_sentinel), align=8)
        named_count_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_GETCOUNT_OFFSET),
            ints=(object_addr, named_count_addr, membername_addr, 0,
                  object_addr),
            ret="int",
        ))
        named_count_value = struct.unpack(
            "<i", engine.ql.mem.read(named_count_addr, 4))[0]

        negative_addr = engine.heap.write(
            _integer_variant_bytes(variant_sentinel), align=8)
        miss_addr = engine.heap.write(
            _integer_variant_bytes(variant_sentinel), align=8)
        strict_miss_addr = engine.heap.write(
            _integer_variant_bytes(variant_sentinel), align=8)
        direct_variant_addrs.extend(
            (negative_addr, miss_addr, strict_miss_addr))
        negative_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_PROPGETBYNUM_OFFSET),
            ints=(object_addr, 0, -1, negative_addr, object_addr),
            ret="int",
        ))
        miss_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_PROPGETBYNUM_OFFSET),
            ints=(object_addr, 0, entry_count, miss_addr, object_addr),
            ret="int",
        ))
        strict_miss_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_PROPGETBYNUM_OFFSET),
            ints=(object_addr, TJS_MEMBERMUSTEXIST, entry_count,
                  strict_miss_addr, object_addr),
            ret="int",
        ))
        negative_variant = _variant_snapshot(engine, negative_addr)
        miss_variant = _variant_snapshot(engine, miss_addr)
        strict_miss_variant = _variant_snapshot(engine, strict_miss_addr)

        isvalid_before = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ISVALID_OFFSET),
            ints=(object_addr, 0, 0, 0, object_addr), ret="int",
        ))
        named_invalidate_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_INVALIDATE_OFFSET),
            ints=(object_addr, 0, membername_addr, 0, object_addr),
            ret="int",
        ))
        valid_after_named_invalidate = engine.ql.mem.read(
            object_addr + 40, 1)[0]
        isvalid_after_named = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ISVALID_OFFSET),
            ints=(object_addr, 0, membername_addr, 0, object_addr),
            ret="int",
        ))
        dispatch_refcount_before_invalidate_call = struct.unpack(
            "<I", engine.ql.mem.read(object_addr + 16, 4))[0]
        owner_refcount_before_invalidate_call = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        invalidate_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_INVALIDATE_OFFSET),
            ints=(object_addr, 0, 0, 0, object_addr), ret="int",
        ))
        dispatch_refcount_after_invalidate_call = struct.unpack(
            "<I", engine.ql.mem.read(object_addr + 16, 4))[0]
        owner_refcount_after_invalidate_call = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        valid_after_invalidate = engine.ql.mem.read(
            object_addr + 40, 1)[0]
        isvalid_after = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ISVALID_OFFSET),
            ints=(object_addr, 0, 0, 0, object_addr), ret="int",
        ))
        repeat_invalidate_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_INVALIDATE_OFFSET),
            ints=(object_addr, 0, 0, 0, object_addr), ret="int",
        ))
        invalid_prop_addr = engine.heap.write(
            _integer_variant_bytes(variant_sentinel), align=8)
        direct_variant_addrs.append(invalid_prop_addr)
        invalid_prop_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_PROPGET_OFFSET),
            ints=(object_addr, 0, missing_member_addr, 0,
                  invalid_prop_addr, object_addr),
            ret="int",
        ))
        invalid_prop_variant = _variant_snapshot(engine, invalid_prop_addr)
        invalid_count_addr = engine.heap.write(
            struct.pack("<i", count_sentinel), align=8)
        invalid_count_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_GETCOUNT_OFFSET),
            ints=(object_addr, invalid_count_addr, 0, 0, object_addr),
            ret="int",
        ))
        invalid_count_value = struct.unpack(
            "<i", engine.ql.mem.read(invalid_count_addr, 4))[0]
        invalid_enum_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ENUMMEMBERS_OFFSET),
            ints=(object_addr, 0, enum_value_closure_addr, 0),
            ret="int",
        ))
        instance_after_invalidate_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_ISINSTANCEOF_OFFSET),
            ints=(object_addr, 0xFFFFFFFF, 0, instance_hint_addr,
                  expected_class_addr, 0),
            ret="int",
        ))
        instance_hint_value = struct.unpack(
            "<I", engine.ql.mem.read(instance_hint_addr, 4))[0]
        engine.ql.mem.write(
            native_pointer_addr, struct.pack("<Q", native_pointer_sentinel))
        native_after_invalidate_rc = _signed_w32(engine.call(
            engine.offset(PSBVALUE_DISPATCH_NATIVE_INSTANCE_SUPPORT_OFFSET),
            ints=(object_addr, TJS_NIS_GETINSTANCE,
                  native_class_id_after_mismatch, native_pointer_addr),
            ret="int",
        ))
        native_pointer_after_invalidate = _read_pointer(
            engine, native_pointer_addr)
        native_class_id_after_invalidate = struct.unpack(
            "<i", engine.ql.mem.read(native_class_id_slot, 4))[0]
        unsupported_after_invalidate = run_unsupported_probe()
        native_lifecycle_after_invalidate = run_native_lifecycle_probe()

        def read_global_string(name: str) -> str:
            value_addr = engine.tjs_global(name)
            direct_variant_addrs.append(value_addr)
            value = _read_tjs_string_variant(engine, value_addr)
            if value["variant_type"] != TJS_VARIANT_STRING_TYPE:
                raise RuntimeError(f"{name} is not a TJS String")
            utf8 = value["utf8"]
            if utf8 is None:
                if value["string_addr"] == 0:
                    return ""
                raise RuntimeError(f"{name} has an unreadable TJS String")
            return utf8.decode("utf-8")

        enum_value_names_text = read_global_string(
            "oracle_psb_enum_value_names")
        enum_value_types_text = read_global_string(
            "oracle_psb_enum_value_types")
        enum_value_bad = read_global_string(
            "oracle_psb_enum_value_bad")
        enum_no_value_names_text = read_global_string(
            "oracle_psb_enum_no_value_names")
        enum_no_value_bad = read_global_string(
            "oracle_psb_enum_no_value_bad")
        dispatch_refcount_after_invalidate = struct.unpack(
            "<I", engine.ql.mem.read(object_addr + 16, 4))[0]
        owner_refcount_after_invalidate = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]

        result.update({
            "unsupported_before_invalidate":
                unsupported_before_invalidate,
            "unsupported_after_invalidate":
                unsupported_after_invalidate,
            "native_lifecycle_before_invalidate":
                native_lifecycle_before_invalidate,
            "native_lifecycle_after_invalidate":
                native_lifecycle_after_invalidate,
            "native_class_id_before": native_class_id_before,
            "native_non_get_rc": native_non_get_rc,
            "native_class_id_after_non_get":
                native_class_id_after_non_get,
            "native_pointer_after_non_get": native_pointer_after_non_get,
            "native_mismatch_class_id": native_mismatch_class_id,
            "native_mismatch_rc": native_mismatch_rc,
            "native_class_id_after_mismatch":
                native_class_id_after_mismatch,
            "native_pointer_after_mismatch":
                native_pointer_after_mismatch,
            "native_success_rc": native_success_rc,
            "native_pointer_after_success": native_pointer_after_success,
            "expected_native_pointer": object_addr + 8,
            "dispatch_refcount_before_native_success":
                dispatch_refcount_before_native_success,
            "dispatch_refcount_after_native_success":
                dispatch_refcount_after_native_success,
            "owner_refcount_before_native_success":
                owner_refcount_before_native_success,
            "owner_refcount_after_native_success":
                owner_refcount_after_native_success,
            "native_after_invalidate_rc": native_after_invalidate_rc,
            "native_pointer_after_invalidate":
                native_pointer_after_invalidate,
            "native_class_id_after_invalidate":
                native_class_id_after_invalidate,
            "instanceof_true_rc": instance_true_rc,
            "instanceof_other_class_rc": instance_other_rc,
            "instanceof_case_mismatch_rc": instance_case_mismatch_rc,
            "instanceof_named_rc": instance_named_rc,
            "instanceof_after_invalidate_rc":
                instance_after_invalidate_rc,
            "instanceof_hint_value": instance_hint_value,
            "enum_value_rc": enum_value_rc,
            "enum_value_names": enum_value_names_text.splitlines(),
            "enum_value_types": enum_value_types_text.splitlines(),
            "enum_value_callback_bad": enum_value_bad,
            "enum_no_value_rc": enum_no_value_rc,
            "enum_no_value_names":
                enum_no_value_names_text.splitlines(),
            "enum_no_value_callback_bad": enum_no_value_bad,
            "invalid_enum_rc": invalid_enum_rc,
            "named_prop_count_rc": prop_count_rc,
            "named_prop_count_variant": prop_count_variant,
            "named_prop_miss_rc": prop_miss_rc,
            "named_prop_miss_variant": prop_miss_variant,
            "named_prop_strict_miss_rc": prop_strict_miss_rc,
            "named_prop_strict_miss_variant": prop_strict_miss_variant,
            "null_name_prop_rc": prop_null_name_rc,
            "null_name_prop_variant": prop_null_name_variant,
            "getcount_rc": count_rc,
            "getcount_value": count_value,
            "named_getcount_rc": named_count_rc,
            "named_getcount_value": named_count_value,
            "negative_index_rc": negative_rc,
            "negative_index_variant": negative_variant,
            "nonstrict_miss_rc": miss_rc,
            "nonstrict_miss_variant": miss_variant,
            "strict_miss_rc": strict_miss_rc,
            "strict_miss_variant": strict_miss_variant,
            "isvalid_before_invalidate": isvalid_before,
            "named_invalidate_rc": named_invalidate_rc,
            "valid_after_named_invalidate": valid_after_named_invalidate,
            "isvalid_after_named_invalidate": isvalid_after_named,
            "invalidate_rc": invalidate_rc,
            "dispatch_refcount_before_invalidate_call":
                dispatch_refcount_before_invalidate_call,
            "dispatch_refcount_after_invalidate_call":
                dispatch_refcount_after_invalidate_call,
            "owner_refcount_before_invalidate_call":
                owner_refcount_before_invalidate_call,
            "owner_refcount_after_invalidate_call":
                owner_refcount_after_invalidate_call,
            "valid_after_invalidate": valid_after_invalidate,
            "isvalid_after_invalidate": isvalid_after,
            "repeat_invalidate_rc": repeat_invalidate_rc,
            "invalid_named_prop_rc": invalid_prop_rc,
            "invalid_named_prop_variant": invalid_prop_variant,
            "invalid_getcount_rc": invalid_count_rc,
            "invalid_getcount_value": invalid_count_value,
            "dispatch_refcount_after_invalidate":
                dispatch_refcount_after_invalidate,
            "owner_refcount_after_invalidate":
                owner_refcount_after_invalidate,
        })

        vtable_topology_ok = (
            primary_vptr == expected_primary_vptr
            and secondary_vptr == expected_secondary_vptr
            and primary_vtable_entries == expected_primary_vtable_entries
            and primary_offset_to_top == 0
            and primary_rtti == 0
            and primary_native_instance_entry == engine.offset(
                PSBVALUE_DISPATCH_NATIVE_INSTANCE_SUPPORT_OFFSET)
            and primary_construct_entry == engine.offset(
                PSBVALUE_DISPATCH_CONSTRUCT_PRIMARY_OFFSET)
            and primary_native_invalidate_entry == engine.offset(
                PSBVALUE_DISPATCH_NATIVE_INVALIDATE_PRIMARY_OFFSET)
            and primary_native_destruct_entry == engine.offset(
                PSBVALUE_DISPATCH_NATIVE_DESTRUCT_PRIMARY_OFFSET)
            and secondary_offset_to_top == -8
            and secondary_rtti == 0
            and secondary_construct_entry == engine.offset(
                PSBVALUE_DISPATCH_CONSTRUCT_SECONDARY_OFFSET)
            and secondary_native_invalidate_entry == engine.offset(
                PSBVALUE_DISPATCH_NATIVE_INVALIDATE_SECONDARY_OFFSET)
            and secondary_native_destruct_entry == engine.offset(
                PSBVALUE_DISPATCH_NATIVE_DESTRUCT_SECONDARY_OFFSET)
        )
        native_lifecycle_ok = (
            native_lifecycle_before_invalidate["construct_primary_rc"] ==
                TJS_S_OK
            and native_lifecycle_before_invalidate[
                "construct_secondary_rc"] == TJS_S_OK
            and native_lifecycle_before_invalidate["dispatch_unchanged"]
            and native_lifecycle_before_invalidate[
                "owner_refcount_before"] ==
                native_lifecycle_before_invalidate[
                    "owner_refcount_after"]
            and native_lifecycle_after_invalidate["construct_primary_rc"] ==
                TJS_S_OK
            and native_lifecycle_after_invalidate[
                "construct_secondary_rc"] == TJS_S_OK
            and native_lifecycle_after_invalidate["dispatch_unchanged"]
            and native_lifecycle_after_invalidate[
                "owner_refcount_before"] ==
                native_lifecycle_after_invalidate[
                    "owner_refcount_after"]
        )
        unsupported_slots_ok = (
            len(unsupported_before_invalidate["return_codes"]) ==
                len(PSBVALUE_DISPATCH_UNSUPPORTED_PROBES)
            and all(
                code == TJS_E_NOTIMPL
                for code in unsupported_before_invalidate[
                    "return_codes"].values())
            and unsupported_before_invalidate["output_unchanged"]
            and unsupported_before_invalidate[
                "dispatch_structure_unchanged"]
            and len(unsupported_after_invalidate["return_codes"]) ==
                len(PSBVALUE_DISPATCH_UNSUPPORTED_PROBES)
            and all(
                code == TJS_E_NOTIMPL
                for code in unsupported_after_invalidate[
                    "return_codes"].values())
            and unsupported_after_invalidate["output_unchanged"]
            and unsupported_after_invalidate[
                "dispatch_structure_unchanged"]
        )
        native_instance_ok = (
            native_non_get_rc == TJS_E_NOTIMPL
            and native_class_id_after_non_get == native_class_id_before
            and native_pointer_after_non_get == native_pointer_sentinel
            and native_class_id_after_mismatch != 0
            and native_mismatch_class_id != native_class_id_after_mismatch
            and native_mismatch_rc == TJS_E_FAIL
            and native_pointer_after_mismatch == native_pointer_sentinel
            and (native_class_id_before == 0
                 or native_class_id_after_mismatch == native_class_id_before)
            and native_success_rc == TJS_S_OK
            and native_pointer_after_success == object_addr + 8
            and dispatch_refcount_after_native_success ==
                dispatch_refcount_before_native_success
            and owner_refcount_after_native_success ==
                owner_refcount_before_native_success
            and native_after_invalidate_rc == TJS_S_OK
            and native_pointer_after_invalidate == object_addr + 8
            and native_class_id_after_invalidate ==
                native_class_id_after_mismatch
        )
        instanceof_ok = (
            instance_true_rc == TJS_S_TRUE
            and instance_other_rc == TJS_S_FALSE
            and instance_case_mismatch_rc == TJS_S_FALSE
            and instance_named_rc == TJS_E_NOTIMPL
            and instance_after_invalidate_rc == TJS_S_TRUE
            and instance_hint_value == instance_hint_sentinel
        )
        enum_ok = (
            enum_value_rc == TJS_S_OK
            and enum_no_value_rc == TJS_S_OK
            and invalid_enum_rc == TJS_E_INVALIDOBJECT
            and enum_value_names_text == expected_enum_names_text
            and enum_value_types_text == expected_enum_types_text
            and enum_value_bad == ""
            and enum_no_value_names_text == expected_enum_names_text
            and enum_no_value_bad == ""
        )
        named_prop_ok = (
            prop_miss_rc == TJS_S_OK
            and prop_miss_variant["type"] == 0
            and prop_strict_miss_rc == TJS_E_MEMBERNOTFOUND
            and prop_strict_miss_variant["type"] ==
                TJS_VARIANT_INTEGER_TYPE
            and prop_strict_miss_variant["integer"] == variant_sentinel
            and prop_null_name_rc == TJS_E_NOTIMPL
            and prop_null_name_variant["type"] ==
                TJS_VARIANT_INTEGER_TYPE
            and prop_null_name_variant["integer"] == variant_sentinel
        )
        if tag == 0x20:
            named_prop_ok = (
                named_prop_ok
                and prop_count_rc == TJS_S_OK
                and prop_count_variant["type"] == TJS_VARIANT_INTEGER_TYPE
                and prop_count_variant["integer"] == entry_count
            )
        else:
            named_prop_ok = (
                named_prop_ok
                and prop_count_rc is None
                and prop_count_variant is None
            )
        count_ok = (
            named_count_rc == TJS_E_NOTIMPL
            and named_count_value == count_sentinel
        )
        index_ok = False
        if tag == 0x20:
            count_ok = (
                count_ok
                and count_rc == TJS_S_OK
                and count_value == entry_count
            )
            index_ok = (
                negative_rc == TJS_S_OK
                and negative_variant["type"] == TJS_VARIANT_INTEGER_TYPE
                and negative_variant["integer"] ==
                    expected_negative_index_value
                and miss_rc == TJS_S_OK
                and miss_variant["type"] == 0
                and strict_miss_rc == TJS_E_MEMBERNOTFOUND
                and strict_miss_variant["type"] ==
                    TJS_VARIANT_INTEGER_TYPE
                and strict_miss_variant["integer"] == variant_sentinel
            )
        else:
            count_ok = (
                count_ok
                and count_rc == TJS_E_NOTIMPL
                and count_value == count_sentinel
            )
            index_ok = (
                negative_rc == TJS_E_MEMBERNOTFOUND
                and negative_variant["type"] ==
                    TJS_VARIANT_INTEGER_TYPE
                and negative_variant["integer"] == variant_sentinel
                and miss_rc == TJS_E_MEMBERNOTFOUND
                and miss_variant["type"] == TJS_VARIANT_INTEGER_TYPE
                and miss_variant["integer"] == variant_sentinel
                and strict_miss_rc == TJS_E_MEMBERNOTFOUND
                and strict_miss_variant["type"] ==
                    TJS_VARIANT_INTEGER_TYPE
                and strict_miss_variant["integer"] == variant_sentinel
            )
        invalidation_ok = (
            isvalid_before == TJS_S_TRUE
            and named_invalidate_rc == TJS_E_NOTIMPL
            and valid_after_named_invalidate == 1
            and isvalid_after_named == TJS_S_TRUE
            and invalidate_rc == TJS_S_OK
            and valid_after_invalidate == 0
            and isvalid_after == TJS_S_FALSE
            and repeat_invalidate_rc == TJS_E_INVALIDOBJECT
            and invalid_prop_rc == TJS_E_INVALIDOBJECT
            and invalid_prop_variant["type"] ==
                TJS_VARIANT_INTEGER_TYPE
            and invalid_prop_variant["integer"] == variant_sentinel
            and invalid_count_rc == TJS_E_INVALIDOBJECT
            and invalid_count_value == count_sentinel
            and dispatch_refcount_after_invalidate_call ==
                dispatch_refcount_before_invalidate_call
            and owner_refcount_after_invalidate_call ==
                owner_refcount_before_invalidate_call
        )
        dispatch_edges_ok = (
            vtable_topology_ok and native_lifecycle_ok
            and unsupported_slots_ok and native_instance_ok
            and instanceof_ok and enum_ok
            and named_prop_ok and count_ok and index_ok and invalidation_ok)
        result["vtable_topology_ok"] = vtable_topology_ok
        result["native_lifecycle_ok"] = native_lifecycle_ok
        result["unsupported_slots_ok"] = unsupported_slots_ok
        result["native_instance_ok"] = native_instance_ok
        result["instanceof_ok"] = instanceof_ok
        result["enum_ok"] = enum_ok
        result["dispatch_edges_ok"] = dispatch_edges_ok

        engine.tjs_exec(clear_shape_globals_source)
        globals_cleared = True
        dispatch_after_global = engine.ql.mem.read(
            object_addr, PSBVALUE_DISPATCH_SIZE)
        dispatch_refcount_after = struct.unpack_from(
            "<I", dispatch_after_global, 16)[0]
        dispatch_valid_after_global = dispatch_after_global[40]
        owner_refcount_after = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]

        addref_result = engine.call(
            engine.offset(PSBVALUE_DISPATCH_ADDREF_OFFSET),
            ints=(object_addr,), ret="int",
        )
        dispatch_refcount_after_addref = struct.unpack(
            "<I", engine.ql.mem.read(object_addr + 16, 4))[0]
        release_result = engine.call(
            engine.offset(PSBVALUE_DISPATCH_RELEASE_OFFSET),
            ints=(object_addr,), ret="int",
        )
        dispatch_refcount_after_release = struct.unpack(
            "<I", engine.ql.mem.read(object_addr + 16, 4))[0]
        owner_refcount_after_release = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        result.update({
            "dispatch_refcount_after_global_release":
                dispatch_refcount_after,
            "dispatch_valid_after_global_release":
                dispatch_valid_after_global,
            "owner_refcount_after_global_release": owner_refcount_after,
            "addref_result": addref_result,
            "dispatch_refcount_after_addref":
                dispatch_refcount_after_addref,
            "release_result": release_result,
            "dispatch_refcount_after_release":
                dispatch_refcount_after_release,
            "owner_refcount_after_release": owner_refcount_after_release,
            "script_globals_cleared_before_refcount_probe": globals_cleared,
        })

        probe_ok = probe_type == expected_probe_type
        if expected_probe_type == TJS_VARIANT_INTEGER_TYPE:
            probe_ok = probe_ok and probe_value == expected_probe_value
        else:
            probe_ok = probe_ok and probe_bits_le == expected_probe_bits_le
        ok = (
            primary_vptr != 0
            and secondary_vptr != 0
            and dispatch_refcount_before >= 4
            and owner_refcount_before >= 1
            and raw_size == declared_size
            and node_addr == expected_node_addr
            and node_in_owner
            and node_prefix == expected_node_prefix
            and dispatch_valid == 1
            and raw_category == expected_category
            and dispatch_edges_ok
            and dispatch_refcount_after_invalidate >= 2
            and 2 <= dispatch_refcount_after <=
                dispatch_refcount_after_invalidate - 2
            and dispatch_valid_after_global == 0
            and owner_refcount_after == owner_refcount_after_invalidate
            and addref_result == dispatch_refcount_after + 1
            and dispatch_refcount_after_addref ==
                dispatch_refcount_after + 1
            and release_result == dispatch_refcount_after
            and dispatch_refcount_after_release == dispatch_refcount_after
            and owner_refcount_after_release == owner_refcount_after
            and probe_ok
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        for direct_variant_addr in direct_variant_addrs:
            try:
                engine.call(
                    engine.offset(TJS_VARIANT_DTOR_OFFSET),
                    ints=(direct_variant_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(
                    f"direct dispatch Variant: {exc!r}")
        if probe_addr:
            try:
                engine.call(
                    engine.offset(TJS_VARIANT_DTOR_OFFSET),
                    ints=(probe_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"TJS probe Variant: {exc!r}")
        if variant_addr:
            try:
                engine.call(
                    engine.offset(TJS_VARIANT_DTOR_OFFSET),
                    ints=(variant_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"TJS dispatch Variant: {exc!r}")
        if globals_created and not globals_cleared:
            try:
                engine.tjs_exec(clear_shape_globals_source)
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


def run_invalid_resource_boundary_case(
    engine,
    *,
    input_path: Path,
    node_offset: int,
    expected_node_bytes: bytes,
    expected_returned_offset: int,
    expected_resource_size: int,
) -> dict:
    """Observe a natural out-of-table Resource without dereferencing it.

    Android's raw GetResource has no Resource-index/count/range guard.  This
    case pins an existing PSB whose root tag reads both packed tables from
    addresses that are still inside the owner allocation, then verifies the
    returned owner-relative pointer and size.  The published range crosses the
    allocation end, so the adapter deliberately never reads returned bytes.
    """
    data, input_format, declared_size = _input_info(input_path)
    if input_format != "psb":
        raise ValueError("invalid Resource boundary requires a raw PSB input")
    if not expected_node_bytes:
        raise ValueError("invalid Resource boundary node bytes are empty")
    expected_tag = expected_node_bytes[0]
    expected_node_size = RESOURCE_NODE_SIZES.get(expected_tag)
    if (expected_node_size is None
            or len(expected_node_bytes) != expected_node_size):
        raise ValueError(
            "invalid Resource boundary must pin one complete resource tag")
    if node_offset < 0 or node_offset + expected_node_size > len(data):
        raise ValueError("invalid Resource boundary node is outside the input")
    if data[node_offset:node_offset + expected_node_size] \
            != expected_node_bytes:
        raise ValueError("invalid Resource boundary node bytes changed")
    entries_offset = struct.unpack_from("<I", data, 36)[0]
    if entries_offset != node_offset:
        raise ValueError("invalid Resource boundary is no longer the PSB root")
    if expected_returned_offset < 0 or expected_returned_offset >= len(data):
        raise ValueError("invalid Resource pointer must begin inside the input")
    if expected_resource_size <= 0:
        raise ValueError("invalid Resource size must be non-zero")
    expected_resource_end = expected_returned_offset + expected_resource_size
    if expected_resource_end <= len(data):
        raise ValueError("invalid Resource range must cross the input end")

    index = int.from_bytes(expected_node_bytes[1:], "little")

    def packed_probe(table_offset: int) -> dict[str, int]:
        count_tag = data[table_offset]
        if count_tag == 0x0D:
            count = data[table_offset + 1]
        elif count_tag == 0x0E:
            count = struct.unpack_from("<H", data, table_offset + 1)[0]
        elif count_tag == 0x0F:
            count = struct.unpack_from("<I", data, table_offset + 1)[0] \
                & 0xFFFFFF
        elif count_tag == 0x10:
            count = struct.unpack_from("<I", data, table_offset + 1)[0]
        else:
            count = 0
        header_delta = count_tag - 0x0B
        width = (data[table_offset + header_delta] - 0x0C) & 0xFFFFFFFF
        values_offset = table_offset + header_delta + 1
        read_offset = values_offset + ((index * width) & 0xFFFFFFFF)
        if read_offset < 0 or read_offset + 4 > len(data):
            raise ValueError("invalid Resource packed read leaves the input")
        raw_value = struct.unpack_from("<I", data, read_offset)[0]
        if 1 <= width <= 5:
            shift = (8 * (4 - width)) & 31
            value = raw_value & (0xFFFFFFFF >> shift)
        else:
            value = 0
        return {
            "count": count,
            "width": width,
            "values_offset": values_offset,
            "read_offset": read_offset,
            "raw_value": raw_value,
            "value": value,
        }

    chunk_offsets = struct.unpack_from("<I", data, 24)[0]
    chunk_lengths = struct.unpack_from("<I", data, 28)[0]
    chunk_data = struct.unpack_from("<I", data, 32)[0]
    offset_probe = packed_probe(chunk_offsets)
    length_probe = packed_probe(chunk_lengths)
    if chunk_data + offset_probe["value"] != expected_returned_offset:
        raise ValueError("invalid Resource returned-offset pin changed")
    if length_probe["value"] != expected_resource_size:
        raise ValueError("invalid Resource size pin changed")
    if index < offset_probe["count"] or index < length_probe["count"]:
        raise ValueError("invalid Resource index unexpectedly became in-table")

    engine.reset_heap()
    result = {
        "input": str(input_path),
        "entry": "invalid-resource-boundary",
        "input_format": input_format,
        "input_size": len(data),
        "declared_size": declared_size,
        "node_offset": node_offset,
        "expected_node_bytes": expected_node_bytes.hex(),
        "resource_index": index,
        "offset_table_count": offset_probe["count"],
        "offset_table_width": offset_probe["width"],
        "offset_read_offset": offset_probe["read_offset"],
        "offset_raw_value": offset_probe["raw_value"],
        "length_table_count": length_probe["count"],
        "length_table_width": length_probe["width"],
        "length_read_offset": length_probe["read_offset"],
        "length_raw_value": length_probe["raw_value"],
        "expected_returned_offset": expected_returned_offset,
        "expected_resource_size": expected_resource_size,
        "expected_resource_end": expected_resource_end,
    }
    raw_holder_addr = 0
    try:
        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result.update({"loaded": bool(loaded), "owner": owner_addr})
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result

        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        owner_refcount_before = struct.unpack_from("<I", owner, 0)[0]
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        node_addr = raw_addr + node_offset
        node_bytes = engine.ql.mem.read(node_addr, expected_node_size)
        raw_node_addr = engine.heap.write(
            struct.pack("<QQ", owner_addr, node_addr), align=8)
        size_addr = engine.heap.write(
            struct.pack("<I", 0xA5A5A5A5), align=8)
        returned_addr = engine.call(
            engine.offset(PSBRAWNODE_GET_RESOURCE_OFFSET),
            ints=(raw_node_addr, size_addr), ret="ptr",
        )
        resource_size = struct.unpack(
            "<I", engine.ql.mem.read(size_addr, 4))[0]
        owner_refcount_after = struct.unpack(
            "<I", engine.ql.mem.read(owner_addr, 4))[0]
        returned_offset = returned_addr - raw_addr
        resource_end = returned_offset + resource_size
        pointer_inside_input = 0 <= returned_offset < raw_size
        range_inside_input = (
            pointer_inside_input and resource_size <= raw_size - returned_offset)
        result.update({
            "owner_refcount_before": owner_refcount_before,
            "owner_refcount_after": owner_refcount_after,
            "owner_raw_size": raw_size,
            "node_bytes": node_bytes.hex(),
            "returned_address": returned_addr,
            "returned_offset": returned_offset,
            "resource_size": resource_size,
            "resource_end": resource_end,
            "pointer_inside_input": pointer_inside_input,
            "range_inside_input": range_inside_input,
            "returned_bytes_read": False,
        })
        ok = (
            owner_refcount_before == 1
            and owner_refcount_after == owner_refcount_before
            and raw_size == len(data)
            and node_bytes == expected_node_bytes
            and returned_offset == expected_returned_offset
            and resource_size == expected_resource_size
            and resource_end == expected_resource_end
            and pointer_inside_input
            and not range_inside_input
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        if raw_holder_addr:
            engine.call(
                engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                ints=(raw_holder_addr,), ret="void",
            )


def run_resource_boundary_case(
    engine,
    *,
    input_path: Path,
    remote_path: str,
    node_offset: int,
    expected_node_bytes: bytes,
    resource_data_offset: int,
    expected_resource_size: int,
    expected_resource_sha256: str,
    tjs_expression: str,
) -> dict:
    """Observe one natural Resource through copied and borrowed paths.

    Public TJS dispatch must publish an owning Octet copy that survives release
    of its PSBFile global.  A separate raw load invokes Android's original
    GetResource entry and must return the pinned owner-relative borrowed bytes.
    """
    data, input_format, declared_size = _input_info(input_path)
    if input_format != "psb":
        raise ValueError("resource boundary mode requires a raw PSB input")
    if not expected_node_bytes:
        raise ValueError("resource boundary node bytes must not be empty")
    expected_tag = expected_node_bytes[0]
    expected_node_size = RESOURCE_NODE_SIZES.get(expected_tag)
    if (expected_node_size is None
            or len(expected_node_bytes) != expected_node_size):
        raise ValueError(
            "resource boundary node must pin one complete resource tag")
    if node_offset < 0 or node_offset + expected_node_size > len(data):
        raise ValueError("resource boundary node lies outside the input")
    if data[node_offset:node_offset + expected_node_size] \
            != expected_node_bytes:
        raise ValueError("resource boundary input bytes do not match the pin")
    if expected_resource_size <= 0:
        raise ValueError("resource boundary requires a non-empty resource")
    resource_end = resource_data_offset + expected_resource_size
    if resource_data_offset < 0 or resource_end > len(data):
        raise ValueError("resource boundary bytes lie outside the input")
    expected_resource = data[resource_data_offset:resource_end]
    expected_resource_sha256 = expected_resource_sha256.lower()
    if len(expected_resource_sha256) != 64:
        raise ValueError("resource boundary SHA-256 must contain 64 hex digits")
    try:
        bytes.fromhex(expected_resource_sha256)
    except ValueError as exc:
        raise ValueError("resource boundary SHA-256 is not hexadecimal") from exc
    actual_resource_sha256 = hashlib.sha256(expected_resource).hexdigest()
    if actual_resource_sha256 != expected_resource_sha256:
        raise ValueError("resource boundary host bytes do not match the pin")
    remote_path.encode("ascii")
    tjs_expression.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": "resource-boundary",
        "input_format": input_format,
        "input_size": len(data),
        "declared_size": declared_size,
        "expected_tag": f"0x{expected_tag:02x}",
        "node_offset": node_offset,
        "expected_node_bytes": expected_node_bytes.hex(),
        "resource_data_offset": resource_data_offset,
        "expected_resource_size": expected_resource_size,
        "expected_resource_sha256": expected_resource_sha256,
        "script_engine_ready": script_engine != 0,
    }
    if script_engine == 0:
        result["status"] = "setup-failed"
        return result

    tjs_initialized = False
    globals_created = False
    file_global_cleared = False
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
            "var oracle_psb_resource_file = new PSBFile("
            f"{json.dumps(remote_path)});\n"
            "var oracle_psb_resource_value = "
            f"{tjs_expression};"
        )
        globals_created = True
        engine.tjs_exec(script)
        variant_addr = engine.tjs_global("oracle_psb_resource_value")
        variant = engine.ql.mem.read(variant_addr, TJS_VARIANT_SIZE)
        octet_addr = struct.unpack_from("<Q", variant, 0)[0]
        variant_type = struct.unpack_from("<I", variant, 16)[0]
        result.update({
            "variant_type": variant_type,
            "octet_address": octet_addr,
        })
        if variant_type != TJS_VARIANT_OCTET_TYPE or octet_addr == 0:
            result["status"] = "mismatch"
            return result

        octet = engine.ql.mem.read(octet_addr, 16)
        octet_length, octet_refcount, octet_data_addr = struct.unpack(
            "<IIQ", octet)
        result.update({
            "octet_length": octet_length,
            "octet_refcount_before_output_release": octet_refcount,
            "octet_data_address": octet_data_addr,
        })
        # Do not turn an ABI mismatch into an emulator-side invalid read or
        # use-after-free.  The stable Android boundary is length=612,
        # non-null data and at least the script-global + TJS_GLOBAL references.
        # ExecScript may leave an additional temporary Variant alive until the
        # next native call compacts its Variant stack, so the initial absolute
        # count is recorded but is not itself a plugin-lifecycle invariant.
        if (octet_length != expected_resource_size
                or octet_data_addr == 0
                or octet_refcount < 2):
            result["status"] = "mismatch"
            return result

        # The Octet owns a byte copy.  Drop the PSBFile global before reading
        # that copy to prove its lifetime no longer depends on the raw owner.
        engine.tjs_exec("oracle_psb_resource_file = void;")
        file_global_cleared = True
        public_data = engine.ql.mem.read(
            octet_data_addr, expected_resource_size)
        public_sha256 = hashlib.sha256(public_data).hexdigest()

        loaded, raw_holder_addr = _load_octet(engine, data)
        owner_addr = _read_pointer(engine, raw_holder_addr)
        result["raw_loaded"] = bool(loaded)
        if not loaded or owner_addr == 0:
            result["status"] = "load-failed"
            return result

        owner = engine.ql.mem.read(owner_addr, OWNER_SIZE)
        raw_addr = struct.unpack_from("<Q", owner, 88)[0]
        raw_size = struct.unpack_from("<Q", owner, 96)[0]
        if node_offset + expected_node_size > raw_size:
            raise RuntimeError("loaded owner is shorter than the pinned node")
        node_addr = raw_addr + node_offset
        node_bytes = engine.ql.mem.read(node_addr, expected_node_size)
        raw_node_addr = engine.heap.write(
            struct.pack("<QQ", owner_addr, node_addr), align=8)
        size_addr = engine.heap.write(struct.pack("<I", 0xA5A5A5A5), align=8)
        raw_resource_addr = engine.call(
            engine.offset(PSBRAWNODE_GET_RESOURCE_OFFSET),
            ints=(raw_node_addr, size_addr), ret="ptr",
        )
        raw_resource_size = struct.unpack(
            "<I", engine.ql.mem.read(size_addr, 4))[0]
        expected_raw_resource_addr = raw_addr + resource_data_offset
        raw_resource = b""
        raw_sha256 = None
        if (raw_resource_addr == expected_raw_resource_addr
                and raw_resource_size == expected_resource_size):
            raw_resource = engine.ql.mem.read(
                raw_resource_addr, expected_resource_size)
            raw_sha256 = hashlib.sha256(raw_resource).hexdigest()

        # TJS_GLOBAL holds an Octet reference.  Destroy its output copy; the
        # script global must keep the allocation and bytes alive.  Depending on
        # when the persistent TJS Variant stack compacts, an unrelated temporary
        # may still contribute another reference, so verify the local decrement
        # and surviving data rather than an absolute post-call count.
        engine.call(
            engine.offset(TJS_VARIANT_DTOR_OFFSET),
            ints=(variant_addr,), ret="void",
        )
        variant_addr = 0
        octet_after_release = engine.ql.mem.read(octet_addr, 16)
        _, octet_refcount_after_release, octet_data_after_release = \
            struct.unpack("<IIQ", octet_after_release)
        result.update({
            "octet_refcount_after_output_release":
                octet_refcount_after_release,
            "octet_data_address_after_output_release":
                octet_data_after_release,
        })
        if (octet_data_after_release == 0
                or octet_data_after_release != octet_data_addr):
            result["status"] = "mismatch"
            return result
        public_data_after_release = engine.ql.mem.read(
            octet_data_after_release, expected_resource_size)
        public_sha256_after_release = hashlib.sha256(
            public_data_after_release).hexdigest()

        result.update({
            "file_global_cleared_before_octet_read": file_global_cleared,
            "public_resource_sha256": public_sha256,
            "public_resource_sha256_after_output_release":
                public_sha256_after_release,
            "raw_node_bytes": node_bytes.hex(),
            "raw_resource_address": raw_resource_addr,
            "expected_raw_resource_address": expected_raw_resource_addr,
            "raw_resource_size": raw_resource_size,
            "raw_resource_sha256": raw_sha256,
            "public_copy_distinct_from_raw":
                octet_data_addr != raw_resource_addr,
        })
        ok = (
            octet_length == expected_resource_size
            and octet_refcount >= 2
            and 1 <= octet_refcount_after_release < octet_refcount
            and octet_data_after_release == octet_data_addr
            and public_data == expected_resource
            and public_data_after_release == expected_resource
            and node_bytes == expected_node_bytes
            and raw_resource_addr == expected_raw_resource_addr
            and raw_resource_size == expected_resource_size
            and raw_resource == expected_resource
            and octet_data_addr != raw_resource_addr
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        if raw_holder_addr:
            try:
                engine.call(
                    engine.offset(PSBRAWOWNER_HOLDER_RELEASE_OFFSET),
                    ints=(raw_holder_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"raw owner: {exc!r}")
        if variant_addr:
            try:
                engine.call(
                    engine.offset(TJS_VARIANT_DTOR_OFFSET),
                    ints=(variant_addr,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(f"TJS output Variant: {exc!r}")
        if globals_created:
            try:
                engine.tjs_exec(
                    "try { oracle_psb_resource_value = void; } catch(e) {} "
                    "try { oracle_psb_resource_file = void; } catch(e) {}")
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


def run_media_interface_lifecycle_case(engine) -> dict:
    """Exercise the simple PSBMedia vslots and both destructor forms.

    The process-lifetime singleton supplies the real Android vtable and the
    non-terminal intrusive-reference path.  Two target-allocated 0x28-byte
    objects reproduce the constructor's empty state so refcount zero and one
    can be observed without deleting or corrupting the registered singleton.
    No PSB/MDF bytes are generated or modified by this ABI-only probe.
    """
    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "entry": "media-interface-lifecycle",
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

    owned_ttstr_slots: list[int] = []
    underflow_probe = 0
    underflow_destructor_started = False
    terminal_probe = 0

    expected_vptr = engine.offset(PSBMEDIA_VTABLE_OFFSET)
    expected_entries = tuple(
        engine.offset(offset) for offset in PSBMEDIA_VTABLE_ENTRY_OFFSETS)

    def allocate_media_probe(refcount: int) -> int:
        probe = engine.call(
            engine.offset(CXX_OPERATOR_NEW_OFFSET),
            ints=(PSBMEDIA_SIZE,), ret="ptr",
        )
        if probe == 0:
            raise RuntimeError("target operator new returned null")
        raw = bytearray(PSBMEDIA_SIZE)
        struct.pack_into("<Q", raw, 0, expected_vptr)
        struct.pack_into("<I", raw, 8, refcount & 0xFFFFFFFF)
        try:
            engine.ql.mem.write(probe, bytes(raw))
        except Exception:
            # The target allocation has no live members until the write
            # succeeds, so raw operator delete is the only valid cleanup.
            try:
                engine.call(
                    engine.offset(CXX_OPERATOR_DELETE_PLT_OFFSET),
                    ints=(probe,), ret="void",
                )
            except Exception:
                pass
            raise
        return probe

    try:
        singleton_vptr = _read_pointer(engine, singleton_addr)
        vtable_entries = struct.unpack(
            "<11Q", engine.ql.mem.read(singleton_vptr, 11 * 8))
        refcount_before = struct.unpack(
            "<I", engine.ql.mem.read(singleton_addr + 8, 4))[0]
        vtable_ok = (
            singleton_vptr == expected_vptr
            and tuple(vtable_entries) == expected_entries
        )
        result.update({
            "singleton": singleton_addr,
            "vtable": singleton_vptr,
            "expected_vtable": expected_vptr,
            "vtable_entry_offsets": [
                entry - engine.load_base for entry in vtable_entries
            ],
            "vtable_exact": vtable_ok,
            "refcount_before": refcount_before,
        })
        # Never dispatch through an unexpected table, and never risk turning
        # a corrupt/under-retained process singleton into the terminal path.
        if not vtable_ok or refcount_before != 2:
            result["status"] = "mismatch"
            return result

        engine.call(
            vtable_entries[2], ints=(singleton_addr,), ret="void")
        refcount_after_addref = struct.unpack(
            "<I", engine.ql.mem.read(singleton_addr + 8, 4))[0]
        engine.call(
            vtable_entries[3], ints=(singleton_addr,), ret="void")
        refcount_after_release = struct.unpack(
            "<I", engine.ql.mem.read(singleton_addr + 8, 4))[0]

        old_name_payload, old_name_slot = _make_ttstr(engine, "old-media")
        owned_ttstr_slots.append(old_name_slot)
        old_name = _read_ttstr(engine, old_name_payload)
        engine.call(
            vtable_entries[4],
            ints=(singleton_addr, old_name_slot), ret="void",
        )
        name_payload_after = _read_pointer(engine, old_name_slot)
        name_after = _read_ttstr(engine, name_payload_after)

        normalize_payload, normalize_slot = _make_ttstr(
            engine, "MiXeD/Path")
        owned_ttstr_slots.append(normalize_slot)
        normalize_before = bytes(
            engine.ql.mem.read(normalize_payload, 64))
        engine.call(
            vtable_entries[5],
            ints=(singleton_addr, normalize_slot), ret="void",
        )
        normalize_domain_payload = _read_pointer(engine, normalize_slot)
        normalize_after_domain = bytes(
            engine.ql.mem.read(normalize_domain_payload, 64))
        engine.call(
            vtable_entries[6],
            ints=(singleton_addr, normalize_slot), ret="void",
        )
        normalize_path_payload = _read_pointer(engine, normalize_slot)
        normalize_after_path = bytes(
            engine.ql.mem.read(normalize_path_payload, 64))
        normalize_text_after = _read_ttstr(engine, normalize_path_payload)

        _, local_name_slot = _make_ttstr(engine, "private/path")
        owned_ttstr_slots.append(local_name_slot)
        local_name_before = _read_ttstr(
            engine, _read_pointer(engine, local_name_slot))
        engine.call(
            vtable_entries[10],
            ints=(singleton_addr, local_name_slot), ret="void",
        )
        local_name_after = _read_pointer(engine, local_name_slot)
        engine.call(
            vtable_entries[10],
            ints=(singleton_addr, local_name_slot), ret="void",
        )
        local_name_after_empty_call = _read_pointer(
            engine, local_name_slot)

        # refcount==0 must take the ordinary decrement path, wrap to
        # 0xffffffff, and leave the object alive.  Afterwards invoke the
        # complete destructor and matching target operator delete explicitly.
        underflow_probe = allocate_media_probe(0)
        underflow_vptr_before = _read_pointer(engine, underflow_probe)
        engine.call(
            vtable_entries[3], ints=(underflow_probe,), ret="void")
        underflow_refcount = struct.unpack(
            "<I", engine.ql.mem.read(underflow_probe + 8, 4))[0]
        underflow_vptr_after = _read_pointer(engine, underflow_probe)
        underflow_destructor_started = True
        engine.call(
            vtable_entries[0], ints=(underflow_probe,), ret="void")
        complete_destructor_returned = True
        underflow_to_delete = underflow_probe
        underflow_probe = 0
        engine.call(
            engine.offset(CXX_OPERATOR_DELETE_PLT_OFFSET),
            ints=(underflow_to_delete,), ret="void",
        )
        complete_delete_returned = True

        # refcount==1 must tail-dispatch vslot 1, which inlines member
        # destruction and then calls operator delete.  Relinquish the pointer
        # before the RPC because a lost reply may arrive after deletion.
        terminal_probe = allocate_media_probe(1)
        terminal_to_release = terminal_probe
        terminal_probe = 0
        engine.call(
            vtable_entries[3], ints=(terminal_to_release,), ret="void")
        terminal_release_returned = True

        final_singleton_vptr = _read_pointer(engine, singleton_addr)
        final_singleton_refcount = struct.unpack(
            "<I", engine.ql.mem.read(singleton_addr + 8, 4))[0]
        ok = (
            vtable_ok
            and refcount_before == 2
            and refcount_after_addref == 3
            and refcount_after_release == 2
            and old_name == "old-media"
            and name_after == "psb"
            and name_payload_after != 0
            and normalize_domain_payload == normalize_payload
            and normalize_path_payload == normalize_payload
            and normalize_after_domain == normalize_before
            and normalize_after_path == normalize_before
            and normalize_text_after == "MiXeD/Path"
            and local_name_before == "private/path"
            and local_name_after == 0
            and local_name_after_empty_call == 0
            and underflow_vptr_before == expected_vptr
            and underflow_vptr_after == expected_vptr
            and underflow_refcount == 0xFFFFFFFF
            and complete_destructor_returned
            and complete_delete_returned
            and terminal_release_returned
            and final_singleton_vptr == expected_vptr
            and final_singleton_refcount == refcount_before
        )
        result.update({
            "refcount_after_addref": refcount_after_addref,
            "refcount_after_release": refcount_after_release,
            "get_name_before": old_name,
            "get_name_after": name_after,
            "get_name_storage_before": old_name_payload,
            "get_name_storage_after": name_payload_after,
            "normalize_storage_before": normalize_payload,
            "normalize_storage_after_domain": normalize_domain_payload,
            "normalize_storage_after_path": normalize_path_payload,
            "normalize_domain_bits_unchanged":
                normalize_after_domain == normalize_before,
            "normalize_path_bits_unchanged":
                normalize_after_path == normalize_before,
            "normalize_text_after": normalize_text_after,
            "local_name_before": local_name_before,
            "local_name_after": local_name_after,
            "local_name_after_empty_call": local_name_after_empty_call,
            "zero_refcount_after_release": underflow_refcount,
            "zero_refcount_vptr_unchanged":
                underflow_vptr_after == underflow_vptr_before,
            "complete_destructor_returned": complete_destructor_returned,
            "complete_delete_returned": complete_delete_returned,
            "terminal_release_returned": terminal_release_returned,
            "final_singleton_refcount": final_singleton_refcount,
            "final_singleton_vptr": final_singleton_vptr,
            "status": "ok" if ok else "mismatch",
        })
        return result
    finally:
        cleanup_errors = []
        if underflow_probe and not underflow_destructor_started:
            try:
                engine.call(
                    expected_entries[0],
                    ints=(underflow_probe,), ret="void",
                )
                underflow_to_delete = underflow_probe
                underflow_probe = 0
                engine.call(
                    engine.offset(CXX_OPERATOR_DELETE_PLT_OFFSET),
                    ints=(underflow_to_delete,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(
                    f"underflow probe 0x{underflow_probe:x}: {exc!r}")
        if terminal_probe:
            terminal_to_delete = terminal_probe
            terminal_probe = 0
            try:
                engine.call(
                    expected_entries[0],
                    ints=(terminal_to_delete,), ret="void",
                )
                engine.call(
                    engine.offset(CXX_OPERATOR_DELETE_PLT_OFFSET),
                    ints=(terminal_to_delete,), ret="void",
                )
            except Exception as exc:
                cleanup_errors.append(
                    f"terminal probe 0x{terminal_to_delete:x}: {exc!r}")
        while owned_ttstr_slots:
            slot = owned_ttstr_slots.pop()
            try:
                payload = _read_pointer(engine, slot)
                if payload:
                    # Clear our host-side ownership marker before Release: a
                    # lost reply must never trigger a second decrement.
                    engine.ql.mem.write(slot, b"\0" * 8)
                    _release_ttstr(engine, payload)
            except Exception as exc:
                cleanup_errors.append(f"ttstr slot 0x{slot:x}: {exc!r}")
        if cleanup_errors:
            result["cleanup_errors"] = cleanup_errors
            if result.get("status") in {"ok", "mismatch"}:
                result["status"] = "error"


def run_media_adaptor_null_case(
    engine,
    *,
    input_path: Path,
    remote_dir: str,
    container: str,
) -> dict:
    """Exercise EnsureContainer's successful-load/null-adaptor boundary.

    This uses an existing valid PSB and temporarily clears only the target's
    PSBFile class-object slot.  The first native call must still return true,
    publish a Void ``_file`` and update ``_container``.  Restoring the class
    slot and repeating the same request must reload because the cache hit is
    gated on ``_file`` being Object.
    """
    _input_info(input_path)
    storage_auto_path = remote_dir.rstrip("/") + "/"
    request_name = f"{container}/probe"
    for value in (storage_auto_path, request_name):
        value.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": "media-adaptor-null",
        "container": container,
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
    class_slot = engine.offset(PSBFILE_CLASS_SLOT_OFFSET)
    class_slot_cleared = False
    try:
        auto_path_payload, auto_path_slot = _make_ttstr(
            engine, storage_auto_path)
        payloads.append(auto_path_payload)
        engine.call(
            engine.offset(TVP_ADD_AUTO_PATH_OFFSET),
            ints=(auto_path_slot,), ret="void",
        )

        name_payload, name_slot = _make_ttstr(engine, request_name)
        payloads.append(name_payload)

        engine.ql.mem.write(class_slot, b"\0" * 8)
        class_slot_cleared = True
        first_return = engine.call(
            engine.offset(PSBMEDIA_ENSURE_CONTAINER_OFFSET),
            ints=(singleton_addr, name_slot), ret="bool",
        )
        null_media = _inspect_media(engine, singleton_addr)

        engine.ql.mem.write(class_slot, struct.pack("<Q", class_object))
        class_slot_cleared = False
        restored_class_object = _read_pointer(engine, class_slot)
        retry_return = engine.call(
            engine.offset(PSBMEDIA_ENSURE_CONTAINER_OFFSET),
            ints=(singleton_addr, name_slot), ret="bool",
        )
        retry_media = _inspect_media(engine, singleton_addr)

        result.update({
            "first_return": bool(first_return),
            "null_file_object": null_media["file_object"],
            "null_file_objthis": null_media["file_objthis"],
            "null_file_type": null_media["file_type"],
            "null_loaded_container": null_media["container"],
            "class_slot_restored": restored_class_object == class_object,
            "retry_return": bool(retry_return),
            "retry_file_object": retry_media["file_object"],
            "retry_file_objthis": retry_media["file_objthis"],
            "retry_file_type": retry_media["file_type"],
            "retry_loaded_container": retry_media["container"],
        })
        ok = (
            first_return
            and null_media["file_type"] == 0
            and null_media["file_object"] == 0
            and null_media["file_objthis"] == 0
            and null_media["container"] == container
            and restored_class_object == class_object
            and retry_return
            and retry_media["file_type"] == 1
            and retry_media["file_object"] != 0
            and retry_media["file_object"] == retry_media["file_objthis"]
            and retry_media["container"] == container
        )
        result["status"] = "ok" if ok else "mismatch"
        return result
    finally:
        cleanup_errors = []
        if class_slot_cleared:
            try:
                engine.ql.mem.write(
                    class_slot, struct.pack("<Q", class_object))
            except Exception as exc:
                cleanup_errors.append(f"class slot: {exc!r}")
        while payloads:
            payload = payloads.pop()
            try:
                _release_ttstr(engine, payload)
            except Exception as exc:
                cleanup_errors.append(f"ttstr 0x{payload:x}: {exc!r}")
        if cleanup_errors:
            result["cleanup_errors"] = cleanup_errors
            if result.get("status") in {"ok", "mismatch"}:
                result["status"] = "error"


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


def run_media_array_case(
    engine,
    *,
    input_path: Path,
    remote_dir: str,
    container: str,
    array_path: str,
    node_offset: int,
    expected_node_prefix: bytes,
    expected_count: int,
    expected_packed_table_size: int,
) -> dict:
    """Exercise PSBMedia::GetListAt with an existing Array node.

    The input bytes pin the natural packed-Array boundary.  The harness only
    supplies the ABI-compatible lister; native PSBMedia performs resolution,
    count decoding, decimal-index construction, callback ordering and cleanup.
    """
    data, input_format, declared_size = _input_info(input_path)
    decoded = _decoded_psb(data, input_format, declared_size)
    normalized_path = array_path.strip("/")
    if not normalized_path or expected_count < 1:
        raise ValueError(
            "array path must be non-empty and expected count must be positive")
    if len(expected_node_prefix) != 32:
        raise ValueError("array node prefix pin must contain exactly 32 bytes")
    if not 0 <= node_offset < len(decoded):
        raise ValueError("array node offset lies outside the decoded PSB")
    if decoded[node_offset] != 0x20:
        raise ValueError(
            f"array node tag changed: expected 0x20, "
            f"got 0x{decoded[node_offset]:02x}")
    actual_prefix = decoded[
        node_offset:node_offset + len(expected_node_prefix)]
    if actual_prefix != expected_node_prefix:
        raise ValueError(
            "array node prefix changed: expected "
            f"{expected_node_prefix.hex()}, got {actual_prefix.hex()}")
    packed_count, packed_size = _packed_array_header(
        decoded, node_offset + 1)
    if packed_count != expected_count:
        raise ValueError(
            f"array count changed: expected {expected_count}, "
            f"got {packed_count}")
    if packed_size != expected_packed_table_size:
        raise ValueError(
            "array packed-table size changed: expected "
            f"{expected_packed_table_size}, got {packed_size}")

    storage_auto_path = remote_dir.rstrip("/") + "/"
    list_name = f"{container}/{normalized_path}"
    expected_entries = [str(index) for index in range(expected_count)]
    for value in (storage_auto_path, list_name, *expected_entries):
        value.encode("ascii")

    engine.reset_heap()
    script_engine = _wait_for_pointer(engine, TVP_SCRIPT_ENGINE_SLOT_OFFSET)
    result = {
        "input": str(input_path),
        "entry": "media-array-list",
        "container": container,
        "array_path": normalized_path,
        "node_offset": node_offset,
        "node_prefix": expected_node_prefix.hex(),
        "packed_table_size": packed_size,
        "expected_count": expected_count,
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
        listed_entries = engine.storage_list(
            engine.offset(PSBMEDIA_GET_LIST_AT_OFFSET),
            singleton_addr,
            list_slot,
        )
        media = _inspect_media(engine, singleton_addr)
        result.update({
            "listed_entries": listed_entries,
            "file_object": media["file_object"],
            "file_objthis": media["file_objthis"],
            "file_type": media["file_type"],
            "loaded_container": media["container"],
        })
        ok = (
            listed_entries == expected_entries
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
