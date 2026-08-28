#!/usr/bin/env python3
"""Map every closed vertical motionplayer task slice to existing unit tests.

The output is intentionally an inventory, not a claim that an existing test is
an independent native oracle.  Empty mappings are first-class rows consumed by
MP-V02; this script never creates fixtures or silently treats a shared test as
direct coverage.
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path


VERTICAL_TASK_RE = re.compile(r"MP-(?:A|L|C|D|R|G)\d+(?:[a-e])?")
LOCATION_RE = re.compile(r"^(tests/.*):(\d+)$")
TEST_CASE_START_RE = re.compile(
    r"^\s*(?:TEST_CASE|TEST_CASE_METHOD)\s*\("
)
STRING_LITERAL_RE = re.compile(r'"([^"]*)"')


@dataclass(frozen=True)
class TestReference:
    location: str
    case_name: str
    kind: str


def test_case_index(path: Path) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    lines = path.read_text(encoding="utf-8").splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        if not TEST_CASE_START_RE.match(line):
            index += 1
            continue
        start = index
        declaration = line
        while ")" not in declaration and index + 1 < len(lines):
            index += 1
            declaration += lines[index]
        name = "".join(STRING_LITERAL_RE.findall(declaration))
        if not name:
            raise ValueError(f"unparsed TEST_CASE at {path}:{start + 1}")
        result.append((start + 1, name))
        index += 1
    return result


def resolve_test_reference(
    root: Path,
    raw_reference: str,
    indices: dict[Path, list[tuple[int, str]]],
) -> TestReference:
    match = LOCATION_RE.match(raw_reference)
    if not match:
        return TestReference(
            raw_reference, "<test file; no line anchor>", "test_file"
        )

    relative_path = Path(match.group(1))
    line_number = int(match.group(2))
    absolute_path = root / relative_path
    if absolute_path not in indices:
        indices[absolute_path] = test_case_index(absolute_path)

    case_name = "<translation-unit compile-time contract/support>"
    kind = "compile_time"
    for case_line, candidate in indices[absolute_path]:
        if case_line > line_number:
            break
        case_name = candidate
        kind = "runtime_case"
    return TestReference(raw_reference, case_name, kind)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument(
        "--tasks-status",
        type=Path,
        default=Path("analysis/motionplayer_tasks_status.tsv"),
    )
    parser.add_argument(
        "--coverage",
        type=Path,
        default=Path("analysis/motionplayer_coverage.tsv"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("analysis/motionplayer_vertical_test_matrix.tsv"),
    )
    args = parser.parse_args()

    root = args.root.resolve()
    with args.tasks_status.open(encoding="utf-8", newline="") as handle:
        tasks = list(csv.DictReader(handle, delimiter="\t"))
    with args.coverage.open(encoding="utf-8", newline="") as handle:
        coverage = {
            row["slice_id"]: row
            for row in csv.DictReader(handle, delimiter="\t")
        }

    vertical_tasks = [
        row
        for row in tasks
        if VERTICAL_TASK_RE.fullmatch(row["task_id"])
        and row["status"] == "CLOSED_STATIC"
    ]
    if len(vertical_tasks) != 127:
        raise ValueError(
            f"expected 127 closed vertical tasks, found {len(vertical_tasks)}"
        )

    indices: dict[Path, list[tuple[int, str]]] = {}
    output_rows: list[tuple[str, ...]] = []
    for task in vertical_tasks:
        slice_ids = [
            item.strip()
            for item in task["coverage_slices"].split(";")
            if item.strip()
        ]
        if not slice_ids:
            raise ValueError(f"{task['task_id']} has no mapped coverage slice")

        for slice_id in slice_ids:
            slice_row = coverage.get(slice_id)
            if slice_row is None:
                raise ValueError(
                    f"{task['task_id']} references missing slice {slice_id}"
                )
            if slice_row["evidence_status"] not in {
                "IMPLEMENTED",
                "PLATFORM_BOUNDARY",
            }:
                raise ValueError(
                    f"{task['task_id']} references non-terminal slice "
                    f"{slice_id}: {slice_row['evidence_status']}"
                )

            references: list[TestReference] = []
            seen: set[tuple[str, str]] = set()
            for implementation in slice_row["implementation"].split(";"):
                implementation = implementation.strip()
                if not implementation.startswith("tests/"):
                    continue
                reference = resolve_test_reference(
                    root, implementation, indices
                )
                key = (
                    reference.location,
                    reference.case_name,
                    reference.kind,
                )
                if key not in seen:
                    seen.add(key)
                    references.append(reference)

            if references:
                if any(
                    reference.kind == "runtime_case"
                    for reference in references
                ):
                    mapping_kind = "EXISTING_RUNTIME_TEST"
                elif any(
                    reference.kind == "compile_time"
                    for reference in references
                ):
                    mapping_kind = "EXISTING_COMPILE_TIME_CONTRACT"
                else:
                    mapping_kind = "EXISTING_TEST_FILE"
                test_cases = "; ".join(
                    reference.case_name for reference in references
                )
                test_locations = "; ".join(
                    reference.location for reference in references
                )
                gap = (
                    "Existing local regression mapped; oracle strength and "
                    "runtime execution remain MP-V03 through MP-V08 work"
                )
            else:
                mapping_kind = "NO_EXISTING_TEST"
                test_cases = ""
                test_locations = ""
                gap = (
                    "MP-V02 must decide whether an existing fixture can "
                    "express this slice; do not create new fixture material"
                )

            output_rows.append(
                (
                    task["task_id"],
                    task["description"],
                    slice_id,
                    slice_row["evidence_status"],
                    mapping_kind,
                    test_cases,
                    test_locations,
                    gap,
                )
            )

    if len(output_rows) != 294:
        raise ValueError(
            f"expected 294 task-to-slice associations, found {len(output_rows)}"
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(
            (
                "task_id",
                "description",
                "slice_id",
                "evidence_status",
                "mapping_kind",
                "test_cases",
                "test_locations",
                "remaining_verification",
            )
        )
        writer.writerows(output_rows)

    print(
        f"wrote {len(output_rows)} associations for "
        f"{len(vertical_tasks)} vertical tasks to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
