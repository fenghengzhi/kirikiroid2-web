#!/usr/bin/env python3
"""Generate a non-authoritative inventory of local motionplayer NCB bindings.

The reference binaries remain authoritative.  This inventory only supplies
search candidates and local implementation locations for the four-IDB coverage
ledger; rows must not be promoted to MAPPED_4_4 without native evidence.
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path


BLOCK_START = re.compile(
    r"\b(?:NCB_ATTACH_CLASS(?:_WITH_HOOK)?|NCB_REGISTER_SUBCLASS(?:_DELAY)?|"
    r"NCB_REGISTER_CLASS)\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)"
)
MODULE_DEFINE = re.compile(r'#define\s+NCB_MODULE_NAME\s+TJS_W\("([^"]+)"\)')


@dataclass
class Row:
    module: str
    owner: str
    sequence: int
    kind: str
    script_name: str
    binding: str
    source_line: int


def normalize(statement: str) -> str:
    return " ".join(statement.split())


def stable_token(value: str) -> str:
    token = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").upper()
    return token or "EMPTY"


def parse_statement(statement: str) -> tuple[str, str, str] | None:
    text = normalize(statement)
    patterns = (
        ("constructor", r"^NCB_CONSTRUCTOR\s*\(", "<constructor>"),
        ("factory", r"^Factory\s*\(", "<factory>"),
        ("property", r"^NCB_PROPERTY\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", None),
        ("property_ro", r"^NCB_PROPERTY_RO\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", None),
        ("method_raw", r"^NCB_METHOD_RAW_CALLBACK\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", None),
        ("method_detail", r"^NCB_METHOD_DETAIL\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", None),
        ("method", r"^NCB_METHOD\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", None),
        ("variant", r'^Variant\s*\(\s*TJS_W\("([^"]+)"\)', None),
        ("method", r'^Method\s*\(\s*TJS_W\("([^"]+)"\)', None),
        ("property", r'^Property\s*\(\s*TJS_W\("([^"]+)"\)', None),
        ("raw_callback", r'^RawCallback\s*\(\s*TJS_W\("([^"]+)"\)', None),
        ("subclass", r"^NCB_SUBCLASS\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", None),
    )
    for kind, pattern, fixed_name in patterns:
        match = re.search(pattern, text)
        if match:
            name = fixed_name if fixed_name is not None else match.group(1)
            return kind, name, text
    return None


def inventory(source: Path) -> list[Row]:
    lines = source.read_text(encoding="utf-8").splitlines()
    module = "<unset>"
    owner: str | None = None
    depth = 0
    sequence = 0
    statement_parts: list[str] = []
    statement_line = 0
    rows: list[Row] = []

    for line_number, raw_line in enumerate(lines, 1):
        line = re.sub(r"//.*$", "", raw_line)
        module_match = MODULE_DEFINE.search(line)
        if module_match:
            module = module_match.group(1)

        if owner is None:
            block_match = BLOCK_START.search(line)
            if block_match and "{" in line:
                owner = block_match.group(1)
                depth = line.count("{") - line.count("}")
                sequence = 0
            continue

        previous_depth = depth
        depth += line.count("{") - line.count("}")
        if previous_depth >= 1 and depth >= 1:
            content = line.strip()
            if content and not content.startswith("//"):
                if not statement_parts:
                    statement_line = line_number
                statement_parts.append(content)
                joined = " ".join(statement_parts)
                while ";" in joined:
                    statement, joined = joined.split(";", 1)
                    parsed = parse_statement(statement.strip())
                    if parsed:
                        kind, script_name, binding = parsed
                        sequence += 1
                        rows.append(
                            Row(
                                module=module,
                                owner=owner,
                                sequence=sequence,
                                kind=kind,
                                script_name=script_name,
                                binding=binding,
                                source_line=statement_line,
                            )
                        )
                    statement_parts = [joined.strip()] if joined.strip() else []
                    if statement_parts:
                        statement_line = line_number

        if depth == 0:
            owner = None
            statement_parts = []

    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        default=Path("cpp/plugins/motionplayer/main.cpp"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/motionplayer_local_ncb_inventory.tsv"),
    )
    args = parser.parse_args()

    rows = inventory(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(
            (
                "candidate_id",
                "module",
                "owner",
                "sequence",
                "kind",
                "script_name",
                "binding",
                "local_source",
                "binary_status",
            )
        )
        seen_ids: dict[str, int] = {}
        for row in rows:
            base_id = "-".join(
                (
                    "LOCAL-NCB",
                    stable_token(row.module),
                    stable_token(row.owner),
                    stable_token(row.kind),
                    stable_token(row.script_name),
                )
            )
            occurrence = seen_ids.get(base_id, 0) + 1
            seen_ids[base_id] = occurrence
            candidate_id = (
                base_id if occurrence == 1 else f"{base_id}-DUP{occurrence}"
            )
            writer.writerow(
                (
                    candidate_id,
                    row.module,
                    row.owner,
                    row.sequence,
                    row.kind,
                    row.script_name,
                    row.binding,
                    f"{args.source}:{row.source_line}",
                    "UNMAPPED",
                )
            )

    print(f"wrote {len(rows)} rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
