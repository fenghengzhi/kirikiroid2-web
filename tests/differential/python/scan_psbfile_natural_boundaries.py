#!/usr/bin/env python3
"""Read-only inventory of natural PSB/MDF value/resource boundaries.

The walker follows the packed Array/Dictionary layouts jointly recovered from
the four current ``reference/binaries`` implementations of PSBValueDispatch
and PSBRawNode.  It never generates, patches, decrypts, or writes an input
file.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass, field
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import sys
import zlib


TAG_CATEGORY: dict[int, int] = {}
for category, tags in enumerate((
    (0x01, 0x23, 0x24, 0x25, 0x26, 0x3F),
    (0x02, 0x03, 0x27, 0x2F, 0x33, 0x37, 0x3B),
    (0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
     0x28, 0x29, 0x30, 0x31, 0x34, 0x35, 0x38, 0x39, 0x3C, 0x3D),
    (0x1D, 0x1E, 0x1F, 0x2E, 0x41),
    (0x15, 0x16, 0x17, 0x18, 0x2C),
    (0x19, 0x1A, 0x1B, 0x1C, 0x2D),
)):
    TAG_CATEGORY.update((tag, category) for tag in tags)
TAG_CATEGORY[0x20] = 6
TAG_CATEGORY[0x21] = 7

UINT16 = struct.Struct("<H")
UINT32 = struct.Struct("<I")
INTEGER_WIDTHS = {
    0x04: 0,
    0x05: 1,
    0x06: 2,
    0x07: 3,
    0x08: 4,
    0x09: 5,
    0x0A: 6,
    0x0B: 7,
    0x0C: 8,
}
RESOURCE_WIDTHS = {
    0x19: 1,
    0x1A: 2,
    0x1B: 3,
    0x1C: 4,
    0x2D: 0,
}
REAL_WIDTHS = {
    0x1D: 0,
    0x1E: 4,
    0x1F: 8,
}
STRING_WIDTHS = {
    0x15: 1,
    0x16: 2,
    0x17: 3,
    0x18: 4,
    0x2C: 0,
}


def read_u16(data: bytes, offset: int) -> int:
    return UINT16.unpack_from(data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return UINT32.unpack_from(data, offset)[0]


@dataclass(frozen=True)
class PackedArray:
    count: int
    width: int
    values: int
    byte_size: int

    @classmethod
    def parse(cls, data: bytes, offset: int) -> "PackedArray":
        tag = data[offset]
        if tag == 0x0D:
            count = data[offset + 1]
        elif tag == 0x0E:
            count = read_u16(data, offset + 1)
        elif tag == 0x0F:
            count = read_u32(data, offset + 1) & 0xFFFFFF
        elif tag == 0x10:
            count = read_u32(data, offset + 1)
        else:
            count = 0

        header_delta = tag - 0x0B
        width = (data[offset + header_delta] - 0x0C) & 0xFFFFFFFF
        values = offset + header_delta + 1
        byte_size = (count * width + header_delta + 1) & 0xFFFFFFFF
        return cls(count, width, values, byte_size)

    def get(self, data: bytes, index: int) -> int:
        # All four references accept widths 1..5.  Width 5 deliberately
        # follows the target register-shift modulo rule and keeps one byte.
        if not 1 <= self.width <= 5:
            return 0
        address = self.values + ((index * self.width) & 0xFFFFFFFF)
        raw = read_u32(data, address)
        shift = (8 * (4 - self.width)) & 31
        return raw & (0xFFFFFFFF >> shift)


@dataclass
class Tag0BNode:
    offset: int
    value: int
    raw_hex: str
    path: str | None = None

    def as_json(self) -> dict[str, object]:
        low32 = self.value & 0xFFFFFFFF
        signed32 = low32 if low32 < 0x80000000 else low32 - 0x100000000
        return {
            "offset": f"0x{self.offset:X}",
            "value": self.value,
            "low32_unsigned": low32,
            "get_int_signed32": signed32,
            "high32_nonzero": self.value > 0xFFFFFFFF,
            "raw_hex": self.raw_hex,
            "path": self.path,
        }


@dataclass
class IntegerNode:
    tag: int
    offset: int
    full_value: int
    get_int_signed32: int
    raw_hex: str
    path: str | None = None

    def as_json(self) -> dict[str, object]:
        return {
            "offset": f"0x{self.offset:X}",
            "full_variant_value": self.full_value,
            "get_int_signed32": self.get_int_signed32,
            "variant_get_int_delta": self.full_value - self.get_int_signed32,
            "raw_hex": self.raw_hex,
            "path": self.path,
        }


@dataclass
class RealNode:
    tag: int
    offset: int
    value: float
    raw_hex: str
    payload_bits_le: str
    classification: str
    path: str | None = None

    def as_json(self) -> dict[str, object]:
        return {
            "offset": f"0x{self.offset:X}",
            "value": self.value if math.isfinite(self.value) else None,
            "value_hex": self.value.hex(),
            "classification": self.classification,
            "payload_bits_le": self.payload_bits_le,
            "raw_hex": self.raw_hex,
            "path": self.path,
        }


@dataclass
class StringNode:
    tag: int
    offset: int
    index: int
    raw_hex: str
    index_in_table: bool
    string_offset: int | None = None
    absolute_string_offset: int | None = None
    byte_length: int | None = None
    bytes_sha256: str | None = None
    bytes_hex_prefix: str | None = None
    text_preview: str | None = None
    nul_terminated: bool = False
    path: str | None = None

    def as_json(self) -> dict[str, object]:
        return {
            "offset": f"0x{self.offset:X}",
            "index": self.index,
            "raw_hex": self.raw_hex,
            "index_in_table": self.index_in_table,
            "string_offset": self.string_offset,
            "absolute_string_offset": self.absolute_string_offset,
            "byte_length": self.byte_length,
            "bytes_sha256": self.bytes_sha256,
            "bytes_hex_prefix": self.bytes_hex_prefix,
            "text_preview": self.text_preview,
            "nul_terminated": self.nul_terminated,
            "path": self.path,
        }


@dataclass
class ShapeNode:
    tag: int
    offset: int
    entry_count: int | None
    table_byte_size: int
    raw_hex_prefix: str
    path: str | None = None

    def as_json(self) -> dict[str, object]:
        return {
            "offset": f"0x{self.offset:X}",
            "entry_count": self.entry_count,
            "table_byte_size": self.table_byte_size,
            "raw_hex_prefix": self.raw_hex_prefix,
            "path": self.path,
        }


@dataclass
class ResourceNode:
    tag: int
    offset: int
    index: int
    raw_hex: str
    index_in_tables: bool
    resource_offset: int | None
    resource_size: int | None
    resource_in_file: bool
    path: str | None = None

    def as_json(self) -> dict[str, object]:
        return {
            "offset": f"0x{self.offset:X}",
            "index": self.index,
            "raw_hex": self.raw_hex,
            "index_in_tables": self.index_in_tables,
            "resource_offset": self.resource_offset,
            "resource_size": self.resource_size,
            "resource_in_file": self.resource_in_file,
            "path": self.path,
        }


@dataclass
class DocumentResult:
    digest: str
    files: list[str] = field(default_factory=list)
    reachable_nodes: int = 0
    tag_counts: Counter[int] = field(default_factory=Counter)
    tag0b_nodes: list[Tag0BNode] = field(default_factory=list)
    integer_extremes: dict[int, dict[str, IntegerNode]] = field(
        default_factory=dict)
    real_extremes: dict[int, dict[str, RealNode]] = field(
        default_factory=dict)
    real_class_counts: Counter[tuple[int, str]] = field(
        default_factory=Counter)
    string_extremes: dict[int, dict[str, StringNode]] = field(
        default_factory=dict)
    string_index_valid_counts: Counter[int] = field(default_factory=Counter)
    boolean_first: dict[int, ShapeNode] = field(default_factory=dict)
    null_first: dict[int, ShapeNode] = field(default_factory=dict)
    collection_extremes: dict[int, dict[str, ShapeNode]] = field(
        default_factory=dict)
    resource_nodes: list[ResourceNode] = field(default_factory=list)
    error: str | None = None


def decode_container(raw: bytes) -> tuple[bytes, bool, bool]:
    """Return decoded PSB, whether input was MDF, and MDF size-match state."""
    if raw.startswith(b"mdf\0"):
        expected = read_u32(raw, 4)
        decoded = zlib.decompress(raw[8:])
        return decoded, True, len(decoded) == expected
    return raw, False, True


def decode_integer_node(data: bytes, offset: int) -> IntegerNode:
    tag = data[offset]
    width = INTEGER_WIDTHS[tag]
    end = offset + 1 + width
    if end > len(data):
        raise ValueError(f"integer tag 0x{tag:02X} at 0x{offset:X} is truncated")
    payload = data[offset + 1:end]
    if tag == 0x04:
        full_value = 0
    elif tag == 0x0B:
        full_value = int.from_bytes(payload, "little", signed=False)
    else:
        full_value = int.from_bytes(payload, "little", signed=True)
    low32 = full_value & 0xFFFFFFFF
    get_int = low32 if low32 < 0x80000000 else low32 - 0x100000000
    return IntegerNode(
        tag=tag,
        offset=offset,
        full_value=full_value,
        get_int_signed32=get_int,
        raw_hex=data[offset:end].hex(),
    )


def update_integer_extremes(
    extremes: dict[int, dict[str, IntegerNode]], node: IntegerNode,
) -> None:
    metrics = extremes.setdefault(node.tag, {})
    if ("min_full" not in metrics
            or node.full_value < metrics["min_full"].full_value):
        metrics["min_full"] = node
    if ("max_full" not in metrics
            or node.full_value > metrics["max_full"].full_value):
        metrics["max_full"] = node
    delta = abs(node.full_value - node.get_int_signed32)
    if ("max_abs_variant_get_int_delta" not in metrics
            or delta > abs(
                metrics["max_abs_variant_get_int_delta"].full_value
                - metrics["max_abs_variant_get_int_delta"].get_int_signed32)):
        metrics["max_abs_variant_get_int_delta"] = node


def classify_real(value: float) -> str:
    if math.isnan(value):
        return "nan"
    if math.isinf(value):
        return "positive_infinity" if value > 0 else "negative_infinity"
    if value == 0.0:
        return "negative_zero" if math.copysign(1.0, value) < 0 else "zero"
    return "finite"


def decode_real_node(data: bytes, offset: int) -> RealNode:
    tag = data[offset]
    width = REAL_WIDTHS[tag]
    end = offset + 1 + width
    if end > len(data):
        raise ValueError(f"real tag 0x{tag:02X} at 0x{offset:X} is truncated")
    payload = data[offset + 1:end]
    if tag == 0x1D:
        value = 0.0
    elif tag == 0x1E:
        value = struct.unpack("<f", payload)[0]
    else:
        value = struct.unpack("<d", payload)[0]
    payload_bits = (
        "0x0" if not payload
        else f"0x{int.from_bytes(payload, 'little'):0{width * 2}X}"
    )
    return RealNode(
        tag=tag,
        offset=offset,
        value=value,
        raw_hex=data[offset:end].hex(),
        payload_bits_le=payload_bits,
        classification=classify_real(value),
    )


def update_real_extremes(
    extremes: dict[int, dict[str, RealNode]],
    class_counts: Counter[tuple[int, str]],
    node: RealNode,
) -> None:
    metrics = extremes.setdefault(node.tag, {})
    class_counts[(node.tag, node.classification)] += 1
    metrics.setdefault("first", node)
    if node.classification != "finite" and node.classification != "zero":
        metrics.setdefault(f"first_{node.classification}", node)
    if not math.isfinite(node.value):
        return
    if ("min_value" not in metrics
            or node.value < metrics["min_value"].value):
        metrics["min_value"] = node
    if ("max_value" not in metrics
            or node.value > metrics["max_value"].value):
        metrics["max_value"] = node
    if ("max_abs" not in metrics
            or abs(node.value) > abs(metrics["max_abs"].value)):
        metrics["max_abs"] = node
    if node.value != 0.0 and (
            "min_abs_nonzero" not in metrics
            or abs(node.value) < abs(metrics["min_abs_nonzero"].value)):
        metrics["min_abs_nonzero"] = node


def decode_string_node(
    data: bytes,
    offset: int,
    offsets: PackedArray,
) -> StringNode:
    tag = data[offset]
    width = STRING_WIDTHS[tag]
    end = offset + 1 + width
    if end > len(data):
        raise ValueError(
            f"string tag 0x{tag:02X} at 0x{offset:X} is truncated")
    index = 0 if width == 0 else int.from_bytes(
        data[offset + 1:end], "little")
    return StringNode(
        tag=tag,
        offset=offset,
        index=index,
        raw_hex=data[offset:end].hex(),
        index_in_table=index < offsets.count,
    )


def update_string_extremes(
    extremes: dict[int, dict[str, StringNode]], node: StringNode,
) -> None:
    metrics = extremes.setdefault(node.tag, {})
    metrics.setdefault("first", node)
    if ("min_index" not in metrics
            or node.index < metrics["min_index"].index):
        metrics["min_index"] = node
    if ("max_index" not in metrics
            or node.index > metrics["max_index"].index):
        metrics["max_index"] = node
    if not node.index_in_table:
        metrics.setdefault("first_invalid", node)


def update_collection_extremes(
    extremes: dict[int, dict[str, ShapeNode]], node: ShapeNode,
) -> None:
    assert node.entry_count is not None
    metrics = extremes.setdefault(node.tag, {})
    metrics.setdefault("first", node)
    if ("min_count" not in metrics
            or node.entry_count < int(metrics["min_count"].entry_count)):
        metrics["min_count"] = node
    if ("max_count" not in metrics
            or node.entry_count > int(metrics["max_count"].entry_count)):
        metrics["max_count"] = node


def populate_string_sample(
    data: bytes,
    node: StringNode,
    offsets: PackedArray,
    strings_data_offset: int,
) -> None:
    if not node.index_in_table:
        return
    node.string_offset = offsets.get(data, node.index)
    node.absolute_string_offset = strings_data_offset + node.string_offset
    if not 0 <= node.absolute_string_offset < len(data):
        return
    end = data.find(b"\0", node.absolute_string_offset)
    if end < 0:
        return
    raw = data[node.absolute_string_offset:end]
    node.byte_length = len(raw)
    node.bytes_sha256 = hashlib.sha256(raw).hexdigest()
    node.bytes_hex_prefix = raw[:64].hex()
    node.text_preview = raw.decode("utf-8", "backslashreplace")[:80]
    node.nul_terminated = True


def decode_resource_node(
    data: bytes,
    offset: int,
    offsets: PackedArray,
    lengths: PackedArray,
    chunk_data_offset: int,
) -> ResourceNode:
    tag = data[offset]
    width = RESOURCE_WIDTHS[tag]
    end = offset + 1 + width
    if end > len(data):
        raise ValueError(
            f"resource tag 0x{tag:02X} at 0x{offset:X} is truncated")
    index = 0 if width == 0 else int.from_bytes(
        data[offset + 1:end], "little")
    index_in_tables = index < offsets.count and index < lengths.count
    resource_offset = None
    resource_size = None
    resource_in_file = False
    if index_in_tables:
        resource_offset = offsets.get(data, index)
        resource_size = lengths.get(data, index)
        resource_start = chunk_data_offset + resource_offset
        resource_end = resource_start + resource_size
        resource_in_file = (
            0 <= resource_start <= resource_end <= len(data))
    return ResourceNode(
        tag=tag,
        offset=offset,
        index=index,
        raw_hex=data[offset:end].hex(),
        index_in_tables=index_in_tables,
        resource_offset=resource_offset,
        resource_size=resource_size,
        resource_in_file=resource_in_file,
    )


def walk_reachable(
    data: bytes,
) -> tuple[
    int, Counter[int], list[Tag0BNode],
    dict[int, dict[str, IntegerNode]],
    dict[int, dict[str, RealNode]], Counter[tuple[int, str]],
    dict[int, dict[str, StringNode]], Counter[int],
    dict[int, ShapeNode], dict[int, ShapeNode],
    dict[int, dict[str, ShapeNode]],
    list[ResourceNode],
]:
    if not data.startswith(b"PSB\0"):
        raise ValueError("decoded input does not start with PSB\\0")
    if len(data) < 40:
        raise ValueError("PSB header is shorter than 40 bytes")

    stack = [read_u32(data, 36)]
    visited: set[int] = set()
    tags: Counter[int] = Counter()
    tag0b_nodes: list[Tag0BNode] = []
    integer_extremes: dict[int, dict[str, IntegerNode]] = {}
    real_extremes: dict[int, dict[str, RealNode]] = {}
    real_class_counts: Counter[tuple[int, str]] = Counter()
    string_extremes: dict[int, dict[str, StringNode]] = {}
    string_index_valid_counts: Counter[int] = Counter()
    string_table: tuple[PackedArray, int] | None = None
    boolean_first: dict[int, ShapeNode] = {}
    null_first: dict[int, ShapeNode] = {}
    collection_extremes: dict[int, dict[str, ShapeNode]] = {}
    resource_nodes: list[ResourceNode] = []
    resource_tables: tuple[PackedArray, PackedArray, int] | None = None

    while stack:
        offset = stack.pop()
        if offset in visited:
            continue
        if not 0 <= offset < len(data):
            raise ValueError(f"node offset 0x{offset:X} is outside input")
        visited.add(offset)

        tag = data[offset]
        tags[tag] += 1
        category = TAG_CATEGORY.get(tag)
        if category is None:
            raise ValueError(f"unknown tag 0x{tag:02X} at 0x{offset:X}")
        if category == 1:
            boolean_first.setdefault(tag, ShapeNode(
                tag=tag,
                offset=offset,
                entry_count=None,
                table_byte_size=1,
                raw_hex_prefix=data[offset:offset + 1].hex(),
            ))
        if category == 0:
            null_first.setdefault(tag, ShapeNode(
                tag=tag,
                offset=offset,
                entry_count=None,
                table_byte_size=1,
                raw_hex_prefix=data[offset:offset + 1].hex(),
            ))
        if tag in INTEGER_WIDTHS:
            update_integer_extremes(
                integer_extremes, decode_integer_node(data, offset))
        if tag in REAL_WIDTHS:
            update_real_extremes(
                real_extremes, real_class_counts,
                decode_real_node(data, offset))
        if tag in STRING_WIDTHS:
            if string_table is None:
                string_table = (
                    PackedArray.parse(data, read_u32(data, 16)),
                    read_u32(data, 20),
                )
            string_node = decode_string_node(data, offset, string_table[0])
            update_string_extremes(string_extremes, string_node)
            if string_node.index_in_table:
                string_index_valid_counts[tag] += 1
        if tag in RESOURCE_WIDTHS:
            if resource_tables is None:
                resource_tables = (
                    PackedArray.parse(data, read_u32(data, 24)),
                    PackedArray.parse(data, read_u32(data, 28)),
                    read_u32(data, 32),
                )
            resource_nodes.append(decode_resource_node(
                data, offset, *resource_tables))
        if tag == 0x0B:
            tag0b_nodes.append(Tag0BNode(
                offset=offset,
                value=int.from_bytes(data[offset + 1:offset + 8], "little"),
                raw_hex=data[offset:offset + 8].hex(),
            ))

        if category == 6:
            offsets = PackedArray.parse(data, offset + 1)
            base = offset + 1 + offsets.byte_size
            update_collection_extremes(collection_extremes, ShapeNode(
                tag=tag,
                offset=offset,
                entry_count=offsets.count,
                table_byte_size=base - offset,
                raw_hex_prefix=data[offset:min(base, offset + 32)].hex(),
            ))
            stack.extend(
                base + offsets.get(data, index)
                for index in range(offsets.count)
            )
        elif category == 7:
            keys = PackedArray.parse(data, offset + 1)
            offsets = PackedArray.parse(data, offset + 1 + keys.byte_size)
            base = offset + 1 + keys.byte_size + offsets.byte_size
            update_collection_extremes(collection_extremes, ShapeNode(
                tag=tag,
                offset=offset,
                entry_count=keys.count,
                table_byte_size=base - offset,
                raw_hex_prefix=data[offset:min(base, offset + 32)].hex(),
            ))
            stack.extend(
                base + offsets.get(data, index)
                for index in range(keys.count)
            )

    selected_integer_nodes = {
        node.offset
        for metrics in integer_extremes.values()
        for node in metrics.values()
    }
    selected_real_nodes = {
        node.offset
        for metrics in real_extremes.values()
        for node in metrics.values()
    }
    selected_string_nodes = {
        node.offset
        for metrics in string_extremes.values()
        for node in metrics.values()
    }
    selected_shape_nodes = {
        node.offset for node in boolean_first.values()
    } | {
        node.offset for node in null_first.values()
    } | {
        node.offset
        for metrics in collection_extremes.values()
        for node in metrics.values()
    }
    if string_table is not None:
        for metrics in string_extremes.values():
            for node in metrics.values():
                populate_string_sample(data, node, *string_table)
    selected_resource_nodes: set[int] = {
        node.offset for node in resource_nodes
        if not node.index_in_tables or not node.resource_in_file
    }
    for tag in sorted({node.tag for node in resource_nodes}):
        valid = [
            node for node in resource_nodes
            if (node.tag == tag and node.index_in_tables
                and node.resource_in_file)
        ]
        if valid:
            selected_resource_nodes.add(min(
                valid, key=lambda node: node.resource_size).offset)
            selected_resource_nodes.add(max(
                valid, key=lambda node: node.resource_size).offset)
            selected_resource_nodes.add(max(
                valid, key=lambda node: node.index).offset)
    path_targets = (
        {node.offset for node in tag0b_nodes}
        | selected_integer_nodes
        | selected_real_nodes
        | selected_string_nodes
        | selected_shape_nodes
        | selected_resource_nodes)
    if path_targets:
        paths = find_paths(data, path_targets)
        for node in tag0b_nodes:
            node.path = paths.get(node.offset)
        for metrics in integer_extremes.values():
            for node in metrics.values():
                node.path = paths.get(node.offset)
        for metrics in real_extremes.values():
            for node in metrics.values():
                node.path = paths.get(node.offset)
        for metrics in string_extremes.values():
            for node in metrics.values():
                node.path = paths.get(node.offset)
        for node in boolean_first.values():
            node.path = paths.get(node.offset)
        for node in null_first.values():
            node.path = paths.get(node.offset)
        for metrics in collection_extremes.values():
            for node in metrics.values():
                node.path = paths.get(node.offset)
        for node in resource_nodes:
            if node.offset in selected_resource_nodes:
                node.path = paths.get(node.offset)
    return (
        len(visited), tags, tag0b_nodes, integer_extremes,
        real_extremes, real_class_counts,
        string_extremes, string_index_valid_counts,
        boolean_first, null_first, collection_extremes,
        resource_nodes)


def make_name_decoder(data: bytes):
    names_offset = read_u32(data, 12)
    charset = PackedArray.parse(data, names_offset)
    names_data_offset = names_offset + charset.byte_size
    names_data = PackedArray.parse(data, names_data_offset)
    name_indexes = PackedArray.parse(
        data, names_data_offset + names_data.byte_size)
    cache: dict[int, str] = {}

    def decode(index: int) -> str:
        cached = cache.get(index)
        if cached is not None:
            return cached
        node = names_data.get(data, name_indexes.get(data, index))
        encoded: list[int] = []
        seen = 0
        while node != 0:
            seen += 1
            if seen > 100_000:
                raise ValueError("name parent cycle")
            parent = names_data.get(data, node)
            encoded.append((node - charset.get(data, parent)) & 0xFF)
            node = parent
        encoded.reverse()
        result = bytes(encoded).decode("utf-8", "backslashreplace")
        cache[index] = result
        return result

    return decode


def find_paths(data: bytes, targets: set[int]) -> dict[int, str]:
    decode_name = make_name_decoder(data)
    result: dict[int, str] = {}
    stack = [(read_u32(data, 36), "$")]
    visited: set[int] = set()

    while stack and targets:
        offset, path = stack.pop()
        if offset in visited:
            continue
        visited.add(offset)
        if offset in targets:
            result[offset] = path
            targets.remove(offset)

        category = TAG_CATEGORY.get(data[offset])
        if category == 6:
            offsets = PackedArray.parse(data, offset + 1)
            base = offset + 1 + offsets.byte_size
            for index in range(offsets.count - 1, -1, -1):
                stack.append((
                    base + offsets.get(data, index),
                    f"{path}[{index}]",
                ))
        elif category == 7:
            keys = PackedArray.parse(data, offset + 1)
            offsets = PackedArray.parse(data, offset + 1 + keys.byte_size)
            base = offset + 1 + keys.byte_size + offsets.byte_size
            for index in range(keys.count - 1, -1, -1):
                key_index = keys.get(data, index)
                try:
                    key = decode_name(key_index)
                except Exception:
                    key = f"<name#{key_index}>"
                stack.append((
                    base + offsets.get(data, index),
                    f"{path}/{key}",
                ))
    return result


def candidate_paths(roots: list[Path]):
    for root in roots:
        if root.is_file():
            paths = (root,)
        else:
            discovered: list[Path] = []
            for directory, directories, names in os.walk(root):
                directories.sort()
                discovered.extend(
                    Path(directory) / name for name in sorted(names))
            paths = discovered
        for path in paths:
            if path.suffix.lower() in (".apk", ".ipa"):
                continue
            try:
                with path.open("rb") as stream:
                    magic = stream.read(4)
            except OSError:
                continue
            if magic in (b"PSB\0", b"mdf\0"):
                yield path


def parse_int(value: str) -> int:
    return int(value, 0)


def build_report(args: argparse.Namespace) -> dict[str, object]:
    documents: dict[str, DocumentResult] = {}
    physical_candidates = 0
    mdf_files = 0
    mdf_zlib_ok = 0
    mdf_size_matches = 0
    container_errors: list[dict[str, str]] = []

    for path in candidate_paths(args.roots):
        physical_candidates += 1
        try:
            raw = path.read_bytes()
            data, is_mdf, size_matches = decode_container(raw)
            if is_mdf:
                mdf_files += 1
                mdf_zlib_ok += 1
                if size_matches:
                    mdf_size_matches += 1
                else:
                    raise ValueError("MDF declared size does not match output")
            digest = hashlib.sha256(data).hexdigest()
            document = documents.get(digest)
            if document is None:
                document = DocumentResult(digest=digest)
                documents[digest] = document
                try:
                    (document.reachable_nodes, document.tag_counts,
                     document.tag0b_nodes,
                     document.integer_extremes,
                     document.real_extremes,
                     document.real_class_counts,
                     document.string_extremes,
                     document.string_index_valid_counts,
                     document.boolean_first,
                     document.null_first,
                     document.collection_extremes,
                     document.resource_nodes) = walk_reachable(data)
                except Exception as error:
                    document.error = repr(error)
            document.files.append(str(path))
        except Exception as error:
            container_errors.append({"file": str(path), "error": repr(error)})

    aggregate_tags: Counter[int] = Counter()
    tag0b_details: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []
    aggregate_integer_extremes: dict[
        int, dict[str, tuple[DocumentResult, IntegerNode]]
    ] = {}
    aggregate_real_extremes: dict[
        int, dict[str, tuple[DocumentResult, RealNode]]
    ] = {}
    aggregate_real_class_counts: Counter[tuple[int, str]] = Counter()
    aggregate_string_extremes: dict[
        int, dict[str, tuple[DocumentResult, StringNode]]
    ] = {}
    aggregate_string_index_valid: Counter[int] = Counter()
    aggregate_boolean_first: dict[
        int, tuple[DocumentResult, ShapeNode]
    ] = {}
    aggregate_null_first: dict[
        int, tuple[DocumentResult, ShapeNode]
    ] = {}
    aggregate_collection_extremes: dict[
        int, dict[str, tuple[DocumentResult, ShapeNode]]
    ] = {}
    aggregate_resource_counts: Counter[int] = Counter()
    aggregate_resource_table_valid: Counter[int] = Counter()
    aggregate_resource_in_file: Counter[int] = Counter()
    aggregate_resource_extremes: dict[
        int, dict[str, tuple[DocumentResult, ResourceNode]]
    ] = {}
    invalid_resource_details: list[dict[str, object]] = []
    for document in documents.values():
        if document.error is not None:
            parse_errors.append({
                "files": document.files,
                "sha256": document.digest,
                "error": document.error,
            })
            continue
        aggregate_tags.update(document.tag_counts)
        for tag, metrics in document.integer_extremes.items():
            aggregate_metrics = aggregate_integer_extremes.setdefault(tag, {})
            for metric, node in metrics.items():
                current = aggregate_metrics.get(metric)
                replace = current is None
                if current is not None and metric == "min_full":
                    replace = node.full_value < current[1].full_value
                elif current is not None and metric == "max_full":
                    replace = node.full_value > current[1].full_value
                elif (current is not None
                      and metric == "max_abs_variant_get_int_delta"):
                    replace = abs(
                        node.full_value - node.get_int_signed32) > abs(
                            current[1].full_value
                            - current[1].get_int_signed32)
                if replace:
                    aggregate_metrics[metric] = (document, node)
        aggregate_real_class_counts.update(document.real_class_counts)
        for tag, metrics in document.real_extremes.items():
            aggregate_metrics = aggregate_real_extremes.setdefault(tag, {})
            for metric, node in metrics.items():
                current = aggregate_metrics.get(metric)
                replace = current is None
                if current is not None and metric == "min_value":
                    replace = node.value < current[1].value
                elif current is not None and metric == "max_value":
                    replace = node.value > current[1].value
                elif current is not None and metric == "max_abs":
                    replace = abs(node.value) > abs(current[1].value)
                elif current is not None and metric == "min_abs_nonzero":
                    replace = abs(node.value) < abs(current[1].value)
                if replace:
                    aggregate_metrics[metric] = (document, node)
        aggregate_string_index_valid.update(
            document.string_index_valid_counts)
        for tag, metrics in document.string_extremes.items():
            aggregate_metrics = aggregate_string_extremes.setdefault(tag, {})
            for metric, node in metrics.items():
                current = aggregate_metrics.get(metric)
                replace = current is None
                if current is not None and metric == "min_index":
                    replace = node.index < current[1].index
                elif current is not None and metric == "max_index":
                    replace = node.index > current[1].index
                if replace:
                    aggregate_metrics[metric] = (document, node)
        for tag, node in document.boolean_first.items():
            aggregate_boolean_first.setdefault(tag, (document, node))
        for tag, node in document.null_first.items():
            aggregate_null_first.setdefault(tag, (document, node))
        for tag, metrics in document.collection_extremes.items():
            aggregate_metrics = aggregate_collection_extremes.setdefault(
                tag, {})
            for metric, node in metrics.items():
                current = aggregate_metrics.get(metric)
                replace = current is None
                if current is not None and metric == "min_count":
                    replace = int(node.entry_count) < int(
                        current[1].entry_count)
                elif current is not None and metric == "max_count":
                    replace = int(node.entry_count) > int(
                        current[1].entry_count)
                if replace:
                    aggregate_metrics[metric] = (document, node)
        for node in document.tag0b_nodes:
            detail = node.as_json()
            detail["files"] = document.files
            detail["sha256"] = document.digest
            tag0b_details.append(detail)
        for node in document.resource_nodes:
            aggregate_resource_counts[node.tag] += 1
            if node.index_in_tables:
                aggregate_resource_table_valid[node.tag] += 1
            if node.resource_in_file:
                aggregate_resource_in_file[node.tag] += 1
            if not node.index_in_tables or not node.resource_in_file:
                detail = node.as_json()
                detail["tag"] = f"0x{node.tag:02X}"
                detail["files"] = document.files
                detail["sha256"] = document.digest
                invalid_resource_details.append(detail)
                continue
            metrics = aggregate_resource_extremes.setdefault(node.tag, {})
            for metric in ("min_size", "max_size", "max_index"):
                current = metrics.get(metric)
                replace = current is None
                if current is not None and metric == "min_size":
                    replace = node.resource_size < current[1].resource_size
                elif current is not None and metric == "max_size":
                    replace = node.resource_size > current[1].resource_size
                elif current is not None and metric == "max_index":
                    replace = node.index > current[1].index
                if replace:
                    metrics[metric] = (document, node)

    integer_extremes: dict[str, object] = {}
    for tag in sorted(aggregate_integer_extremes):
        metrics_json: dict[str, object] = {
            "count": aggregate_tags[tag],
        }
        for metric, (document, node) in sorted(
            aggregate_integer_extremes[tag].items()
        ):
            sample = node.as_json()
            sample["files"] = document.files
            sample["sha256"] = document.digest
            metrics_json[metric] = sample
        integer_extremes[f"0x{tag:02X}"] = metrics_json

    real_extremes: dict[str, object] = {}
    for tag in sorted(aggregate_real_extremes):
        metrics_json: dict[str, object] = {
            "count": aggregate_tags[tag],
            "classifications": {
                classification: count
                for (class_tag, classification), count in sorted(
                    aggregate_real_class_counts.items())
                if class_tag == tag
            },
        }
        for metric, (document, node) in sorted(
            aggregate_real_extremes[tag].items()
        ):
            sample = node.as_json()
            sample["files"] = document.files
            sample["sha256"] = document.digest
            metrics_json[metric] = sample
        real_extremes[f"0x{tag:02X}"] = metrics_json

    string_summary: dict[str, object] = {}
    for tag in sorted(aggregate_string_extremes):
        metrics_json: dict[str, object] = {
            "count": aggregate_tags[tag],
            "index_in_table": aggregate_string_index_valid[tag],
        }
        for metric, (document, node) in sorted(
            aggregate_string_extremes[tag].items()
        ):
            sample = node.as_json()
            sample["files"] = document.files
            sample["sha256"] = document.digest
            metrics_json[metric] = sample
        string_summary[f"0x{tag:02X}"] = metrics_json

    boolean_summary: dict[str, object] = {}
    for tag, (document, node) in sorted(aggregate_boolean_first.items()):
        sample = node.as_json()
        sample["count"] = aggregate_tags[tag]
        sample["files"] = document.files
        sample["sha256"] = document.digest
        boolean_summary[f"0x{tag:02X}"] = sample

    null_summary: dict[str, object] = {}
    for tag, (document, node) in sorted(aggregate_null_first.items()):
        sample = node.as_json()
        sample["count"] = aggregate_tags[tag]
        sample["files"] = document.files
        sample["sha256"] = document.digest
        null_summary[f"0x{tag:02X}"] = sample

    collection_summary: dict[str, object] = {}
    for tag in sorted(aggregate_collection_extremes):
        metrics_json: dict[str, object] = {
            "count": aggregate_tags[tag],
        }
        for metric, (document, node) in sorted(
            aggregate_collection_extremes[tag].items()
        ):
            sample = node.as_json()
            sample["files"] = document.files
            sample["sha256"] = document.digest
            metrics_json[metric] = sample
        collection_summary[f"0x{tag:02X}"] = metrics_json

    resource_summary: dict[str, object] = {}
    for tag in sorted(aggregate_resource_counts):
        metrics_json: dict[str, object] = {
            "count": aggregate_resource_counts[tag],
            "index_in_tables": aggregate_resource_table_valid[tag],
            "resource_in_file": aggregate_resource_in_file[tag],
        }
        for metric, (document, node) in sorted(
            aggregate_resource_extremes.get(tag, {}).items()
        ):
            sample = node.as_json()
            sample["files"] = document.files
            sample["sha256"] = document.digest
            metrics_json[metric] = sample
        resource_summary[f"0x{tag:02X}"] = metrics_json

    anchor: dict[str, object] | None = None
    if args.anchor_file is not None:
        try:
            anchor_data, _, _ = decode_container(args.anchor_file.read_bytes())
            actual = anchor_data[args.anchor_offset]
            anchor = {
                "file": str(args.anchor_file),
                "offset": f"0x{args.anchor_offset:X}",
                "expected_tag": f"0x{args.anchor_tag:02X}",
                "actual_tag": f"0x{actual:02X}",
                "match": actual == args.anchor_tag,
            }
        except Exception as error:
            anchor = {
                "file": str(args.anchor_file),
                "error": repr(error),
                "match": False,
            }

    parsed = [document for document in documents.values()
              if document.error is None]
    return {
        "physical_candidates": physical_candidates,
        "unique_decoded_psb": len(documents),
        "parsed_unique_psb": len(parsed),
        "failed_unique_psb": len(parse_errors),
        "reachable_nodes_unique": sum(
            document.reachable_nodes for document in parsed),
        "mdf": {
            "physical_files": mdf_files,
            "zlib_ok": mdf_zlib_ok,
            "declared_size_matches": mdf_size_matches,
        },
        "tag_counts_unique": {
            f"0x{tag:02X}": aggregate_tags[tag]
            for tag in sorted(aggregate_tags)
        },
        "integer_extremes_unique": integer_extremes,
        "real_extremes_unique": real_extremes,
        "string_summary_unique": string_summary,
        "boolean_summary_unique": boolean_summary,
        "null_summary_unique": null_summary,
        "collection_summary_unique": collection_summary,
        "resource_summary_unique": {
            "by_tag": resource_summary,
            "invalid_details": invalid_resource_details[:args.max_details],
            "invalid_details_truncated": (
                len(invalid_resource_details) > args.max_details),
        },
        "tag0b": {
            "unique_nodes": len(tag0b_details),
            "high32_nonzero": sum(
                bool(detail["high32_nonzero"]) for detail in tag0b_details),
            "details": tag0b_details[:args.max_details],
            "details_truncated": len(tag0b_details) > args.max_details,
        },
        "anchor": anchor,
        "container_errors": container_errors,
        "parse_errors": parse_errors,
    }


def print_human(report: dict[str, object]) -> None:
    mdf = report["mdf"]
    tag0b = report["tag0b"]
    print(
        f"physical_candidates={report['physical_candidates']} "
        f"unique_decoded_psb={report['unique_decoded_psb']} "
        f"parsed_unique_psb={report['parsed_unique_psb']} "
        f"failed_unique_psb={report['failed_unique_psb']} "
        f"reachable_nodes_unique={report['reachable_nodes_unique']}")
    print(
        f"mdf_files={mdf['physical_files']} zlib_ok={mdf['zlib_ok']} "
        f"declared_size_matches={mdf['declared_size_matches']}")
    print(
        f"tag0b_unique_nodes={tag0b['unique_nodes']} "
        f"tag0b_high32_nonzero={tag0b['high32_nonzero']}")
    for tag, metrics in report["integer_extremes_unique"].items():
        minimum = metrics["min_full"]
        maximum = metrics["max_full"]
        widest_delta = metrics["max_abs_variant_get_int_delta"]
        print(
            f"integer_tag={tag} count={metrics['count']} "
            f"min={minimum['full_variant_value']} "
            f"max={maximum['full_variant_value']} "
            f"max_variant_get_int_delta="
            f"{abs(widest_delta['variant_get_int_delta'])}")
    for tag, metrics in report["real_extremes_unique"].items():
        minimum = metrics.get("min_value")
        maximum = metrics.get("max_value")
        print(
            f"real_tag={tag} count={metrics['count']} "
            f"classifications={json.dumps(metrics['classifications'], sort_keys=True)} "
            f"min_hex={minimum['value_hex'] if minimum else 'none'} "
            f"max_hex={maximum['value_hex'] if maximum else 'none'}")
    for tag, metrics in report["string_summary_unique"].items():
        minimum = metrics.get("min_index")
        maximum = metrics.get("max_index")
        print(
            f"string_tag={tag} count={metrics['count']} "
            f"index_in_table={metrics['index_in_table']} "
            f"min_index={minimum['index'] if minimum else 'none'} "
            f"max_index={maximum['index'] if maximum else 'none'}")
    if report["boolean_summary_unique"]:
        for tag, metrics in report["boolean_summary_unique"].items():
            print(
                f"boolean_tag={tag} count={metrics['count']} "
                f"first_offset={metrics['offset']} path={metrics['path']}")
    else:
        print("boolean_tags_present=none")
    for tag, metrics in report["null_summary_unique"].items():
        print(
            f"null_tag={tag} count={metrics['count']} "
            f"first_offset={metrics['offset']} path={metrics['path']}")
    for tag, metrics in report["collection_summary_unique"].items():
        minimum = metrics.get("min_count")
        maximum = metrics.get("max_count")
        print(
            f"collection_tag={tag} count={metrics['count']} "
            f"min_entries={minimum['entry_count'] if minimum else 'none'} "
            f"max_entries={maximum['entry_count'] if maximum else 'none'}")
    resource_summary = report["resource_summary_unique"]
    for tag, metrics in resource_summary["by_tag"].items():
        minimum = metrics.get("min_size")
        maximum = metrics.get("max_size")
        print(
            f"resource_tag={tag} count={metrics['count']} "
            f"index_in_tables={metrics['index_in_tables']} "
            f"resource_in_file={metrics['resource_in_file']} "
            f"min_size={minimum['resource_size'] if minimum else 'none'} "
            f"max_size={maximum['resource_size'] if maximum else 'none'}")
    anchor = report["anchor"]
    if anchor is not None:
        print("anchor=" + json.dumps(anchor, ensure_ascii=False,
                                     sort_keys=True))
    for detail in tag0b["details"]:
        print("tag0b=" + json.dumps(detail, ensure_ascii=False,
                                    sort_keys=True))
    for detail in resource_summary["invalid_details"]:
        print("invalid_resource=" + json.dumps(
            detail, ensure_ascii=False, sort_keys=True))
    for error in report["container_errors"]:
        print("container_error=" + json.dumps(
            error, ensure_ascii=False, sort_keys=True))
    for error in report["parse_errors"]:
        print("parse_error=" + json.dumps(
            error, ensure_ascii=False, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "roots", nargs="*", type=Path,
        default=[Path("reference"), Path("tests/test_files")],
        help="files or directories to scan (default: reference tests/test_files)")
    parser.add_argument("--json", action="store_true",
                        help="emit the complete report as JSON")
    parser.add_argument("--max-details", type=int, default=200,
                        help=(
                            "maximum tag-0x0B and invalid-Resource details "
                            "to emit"))
    parser.add_argument(
        "--anchor-file", type=Path,
        default=Path("reference/xp3/logo_test/m2logo.mtn"),
        help="known natural file used to validate an offset/tag anchor")
    parser.add_argument("--anchor-offset", type=parse_int, default=0x36F8)
    parser.add_argument("--anchor-tag", type=parse_int, default=0x09)
    args = parser.parse_args()

    report = build_report(args)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2,
                         sort_keys=True))
    else:
        print_human(report)

    anchor = report["anchor"]
    failed = bool(report["container_errors"] or report["parse_errors"])
    if anchor is not None and not anchor.get("match", False):
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
